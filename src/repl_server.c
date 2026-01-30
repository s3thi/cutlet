/*
 * repl_server.c - TCP REPL server implementation
 *
 * Threaded TCP server that accepts line-based requests and processes
 * them through repl_format_line(). Each client connection gets its
 * own thread (thread-per-client model).
 *
 * Protocol (v0):
 *   Client sends: <id> <expr>\n
 *   Server responds: -> <id> OK <tokens>\n  or  -> <id> ERR ...\n
 *
 * Request IDs must be digit-only. Max line length is 4096 bytes.
 *
 * The server is designed for testability: start/stop API allows
 * in-process use without forking the binary.
 */

#include "repl_server.h"
#include "repl.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LINE_LEN 4096

/*
 * ReplServer holds the listening socket, accept thread, and shutdown flag.
 * Client threads are detached so we don't need to track them individually.
 */
struct ReplServer {
    int listen_fd;
    uint16_t port;
    bool ast_mode; /* When true, require "AST " prefix on requests. */
    pthread_t accept_thread;
    volatile bool shutdown; /* Signals accept loop and client threads to stop. */
};

/* ============================================================
 * Line protocol helpers
 * ============================================================ */

/*
 * Read one line (up to \n) from a socket fd into buf.
 * Returns the number of bytes read (including \n), or -1 on
 * error/EOF. The result is null-terminated.
 *
 * If the line exceeds max_len-1 bytes before a \n is found,
 * returns -2 to indicate an oversized line. The caller should
 * drain remaining bytes up to the next \n before responding.
 */
static ssize_t read_line(int fd, char *buf, size_t max_len) {
    size_t pos = 0;
    while (pos < max_len - 1) {
        ssize_t n = recv(fd, buf + pos, 1, 0);
        if (n <= 0) {
            /* EOF or error before we got a complete line. */
            return -1;
        }
        pos++;
        if (buf[pos - 1] == '\n') {
            buf[pos] = '\0';
            return (ssize_t)pos;
        }
    }
    /* Line too long — didn't find \n within max_len-1 bytes. */
    buf[pos] = '\0';
    return -2;
}

/*
 * Drain bytes from fd until \n is found or connection closes.
 * Used after detecting an oversized line to resync to the next request.
 */
static void drain_until_newline(int fd) {
    char c;
    while (recv(fd, &c, 1, 0) == 1) {
        if (c == '\n')
            return;
    }
}

/*
 * Send a complete string on a socket. Returns true on success.
 */
static bool send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

/*
 * Send a formatted response line: -> <id> <body>\n
 * body is the full "OK ..." or "ERR ..." string.
 */
static bool send_response(int fd, const char *id, const char *body) {
    /* Build: "-> <id> <body>\n" */
    size_t id_len = strlen(id);
    size_t body_len = strlen(body);
    /* "-> " + id + " " + body + "\n" + '\0' */
    size_t total = 3 + id_len + 1 + body_len + 1;
    char *resp = malloc(total + 1);
    if (!resp)
        return false;

    snprintf(resp, total + 1, "-> %s %s\n", id, body);
    bool ok = send_all(fd, resp, total);
    free(resp);
    return ok;
}

/* ============================================================
 * Request parsing and handling
 * ============================================================ */

/*
 * Parse and handle one request line. The line includes the trailing
 * \n (and possibly \r\n).
 *
 * In token mode: format is "<id> <expr>\n"
 * In AST mode:   format is "AST <id> <expr>\n"
 *
 * If the mode doesn't match the prefix, a clear mismatch error is sent.
 * Returns true if the connection should continue, false to close.
 */
static bool handle_request(int fd, char *line, bool ast_mode) {
    /* Strip trailing \n and \r. */
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }

    /* Empty line (was just "\n") — no ID present. */
    if (len == 0) {
        const char *err_resp = "-> 0 ERR invalid request: missing ID\n";
        send_all(fd, err_resp, strlen(err_resp));
        return true;
    }

    /* Check for "AST " prefix to detect mode and enforce matching. */
    bool has_ast_prefix = (strncmp(line, "AST ", 4) == 0);

    if (ast_mode && !has_ast_prefix) {
        /* Server expects AST prefix but client didn't send it. */
        const char *err_resp = "-> 0 ERR mode mismatch: expected AST\n";
        send_all(fd, err_resp, strlen(err_resp));
        return true;
    }

    if (!ast_mode && has_ast_prefix) {
        /* Server is in token mode but client sent AST prefix. */
        const char *err_resp = "-> 0 ERR mode mismatch: server not in AST mode\n";
        send_all(fd, err_resp, strlen(err_resp));
        return true;
    }

    /* If AST mode, strip the "AST " prefix before parsing ID + expr. */
    char *rest = line;
    if (ast_mode) {
        rest = line + 4;
    }

    /* Extract ID: first whitespace-delimited token. */
    char *space = strchr(rest, ' ');
    const char *id;
    const char *expr;

    if (space) {
        *space = '\0';
        id = rest;
        expr = space + 1;
    } else {
        /* ID only, no expr. */
        id = rest;
        expr = "";
    }

    /* Validate ID is digits-only. */
    for (const char *p = id; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            /* Use "0" as fallback ID in error since the real ID is invalid. */
            const char *err_resp = "-> 0 ERR invalid request ID: must be digits\n";
            send_all(fd, err_resp, strlen(err_resp));
            return true;
        }
    }

    /* Process expr through the appropriate REPL core function. */
    char *result = ast_mode ? repl_format_line_ast(expr) : repl_format_line(expr);
    if (!result) {
        /* Memory allocation failure — send error and continue. */
        send_response(fd, id, "ERR internal error");
        return true;
    }

    send_response(fd, id, result);
    free(result);
    return true;
}

/* ============================================================
 * Client thread
 * ============================================================ */

/* Argument passed to each client thread. */
typedef struct {
    int client_fd;
    ReplServer *server;
    char addr_str[64]; /* "HOST:PORT" of the connected client. */
} ClientArg;

static void *client_thread_fn(void *arg) {
    ClientArg *ca = arg;
    int fd = ca->client_fd;
    ReplServer *srv = ca->server;
    char addr[64];
    memcpy(addr, ca->addr_str, sizeof(addr));
    free(ca);

    fprintf(stderr, "[server] client connected: %s\n", addr);

    char buf[MAX_LINE_LEN + 1];

    while (!srv->shutdown) {
        ssize_t n = read_line(fd, buf, sizeof(buf));

        if (n == -1) {
            /* EOF or read error — client disconnected. */
            break;
        }

        if (n == -2) {
            /* Oversized line. Drain the rest, send error. */
            drain_until_newline(fd);
            const char *err = "-> 0 ERR line too long (max 4096 bytes)\n";
            send_all(fd, err, strlen(err));
            continue;
        }

        if (!handle_request(fd, buf, srv->ast_mode)) {
            break;
        }
    }

    fprintf(stderr, "[server] client disconnected: %s\n", addr);
    close(fd);
    return NULL;
}

/* ============================================================
 * Accept loop (runs in its own thread)
 * ============================================================ */

static void *accept_loop(void *arg) {
    ReplServer *srv = arg;

    while (!srv->shutdown) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_fd < 0) {
            /* If we're shutting down, the listen socket was closed. */
            if (srv->shutdown)
                break;
            /* Transient error — keep going. */
            continue;
        }

        /* Set a recv timeout on the client socket so threads don't
         * block forever if a client goes silent. */
        struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ClientArg *ca = malloc(sizeof(ClientArg));
        if (!ca) {
            close(client_fd);
            continue;
        }
        ca->client_fd = client_fd;
        ca->server = srv;

        /* Format client address for logging. */
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        snprintf(ca->addr_str, sizeof(ca->addr_str), "%s:%u", ip, ntohs(client_addr.sin_port));

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread_fn, ca) != 0) {
            free(ca);
            close(client_fd);
            continue;
        }
        /* Detach so we don't need to join each client thread. */
        pthread_detach(tid);
    }

    return NULL;
}

/* ============================================================
 * Public API
 * ============================================================ */

ReplServer *repl_server_start(const char *host, uint16_t port, bool ast_mode,
                              const char **err_out) {
    ReplServer *srv = calloc(1, sizeof(ReplServer));
    if (!srv) {
        if (err_out)
            *err_out = "out of memory";
        return NULL;
    }
    srv->listen_fd = -1;
    srv->ast_mode = ast_mode;

    /* Create TCP socket. */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        if (err_out)
            *err_out = "failed to create socket";
        free(srv);
        return NULL;
    }

    /* Allow port reuse to avoid "address already in use" in tests. */
    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind. */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        if (err_out)
            *err_out = "invalid host address";
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (err_out)
            *err_out = "failed to bind";
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    /* Listen with a modest backlog. */
    if (listen(srv->listen_fd, 16) < 0) {
        if (err_out)
            *err_out = "failed to listen";
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    /* Retrieve the actual port (important for ephemeral port 0). */
    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    if (getsockname(srv->listen_fd, (struct sockaddr *)&bound_addr, &bound_len) < 0) {
        if (err_out)
            *err_out = "failed to get socket name";
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }
    srv->port = ntohs(bound_addr.sin_port);

    /* Start the accept loop thread. */
    if (pthread_create(&srv->accept_thread, NULL, accept_loop, srv) != 0) {
        if (err_out)
            *err_out = "failed to create accept thread";
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    return srv;
}

uint16_t repl_server_port(const ReplServer *server) {
    if (!server)
        return 0;
    return server->port;
}

void repl_server_stop(ReplServer *server) {
    if (!server)
        return;

    /* Signal shutdown and close the listening socket to unblock accept(). */
    server->shutdown = true;
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }

    /* Wait for the accept thread to finish. */
    pthread_join(server->accept_thread, NULL);

    /*
     * Client threads are detached. They will see shutdown==true on
     * their next recv() timeout (or immediately if they're blocked
     * in recv and the 30s timeout fires). For tests, a short sleep
     * gives in-flight clients time to finish.
     */
    usleep(100000); /* 100ms grace period for client threads. */

    free(server);
}
