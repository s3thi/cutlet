/*
 * repl_server.c - TCP REPL server implementation
 *
 * Threaded TCP server using JSON-framed protocol (v1).
 * Each client connection gets its own thread (thread-per-client model).
 *
 * Protocol (v1):
 *   Framing: Content-Length header + JSON body (LSP-style).
 *   See repl_server.h for full schema.
 *
 * The server is designed for testability: start/stop API allows
 * in-process use without forking the binary.
 */

#include "repl_server.h"
#include "json.h"
#include "repl.h"
#include "value.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * ReplServer holds the listening socket, accept thread, shutdown flag,
 * and debug capabilities.
 */
struct ReplServer {
    int listen_fd;
    uint16_t port;
    bool enable_tokens;   /* Server capability: can produce token debug output. */
    bool enable_ast;      /* Server capability: can produce AST debug output. */
    bool enable_bytecode; /* Server capability: can produce bytecode debug output. */
    pthread_t accept_thread;
    volatile bool shutdown; /* Signals accept loop and client threads to stop. */
};

/* ============================================================
 * say() output callback
 * ============================================================ */

/*
 * Context passed as userdata to the EvalContext write callback.
 * Holds the client's socket fd and the current request ID so the
 * callback can build and send a JSON output frame.
 */
typedef struct {
    int fd;
    unsigned long request_id;
} ServerOutputCtx;

/*
 * EvalWriteFn callback: encodes data as a JSON output frame and
 * sends it to the client socket. Called by say() during eval.
 *
 * Runs under the global eval lock. The fd is owned exclusively
 * by this client thread, so no contention on the socket write.
 */
static void server_output_write(void *userdata, const char *data, size_t len) {
    ServerOutputCtx *ctx = userdata;

    /* Build the output string (data may not be null-terminated at len). */
    char *data_copy = malloc(len + 1);
    if (!data_copy)
        return; /* Best-effort: drop output on OOM. */
    memcpy(data_copy, data, len);
    data_copy[len] = '\0';

    JsonOutputFrame frame = {.id = ctx->request_id, .data = data_copy};
    char *json = json_encode_output(&frame);
    free(data_copy);

    if (json) {
        json_frame_write(ctx->fd, json, strlen(json));
        free(json);
    }
}

/* ============================================================
 * Request handling
 * ============================================================ */

/*
 * Process one JSON request and send the JSON response.
 * Returns:
 *   1  = success, continue
 *   0  = connection closed / real error, stop
 *  -1  = timeout (EAGAIN/EWOULDBLOCK), caller should retry
 */
static int handle_json_request(int fd, ReplServer *srv) {
    /* Read a JSON frame from the client. */
    size_t req_len = 0;
    bool timed_out = false;
    char *req_json = json_frame_read(fd, &req_len, &timed_out);
    if (!req_json) {
        if (timed_out)
            return -1; /* Timeout — caller retries. */
        return 0;      /* EOF or real error. */
    }

    /* Parse the request. */
    JsonRequest req;
    if (!json_parse_request(req_json, req_len, &req)) {
        free(req_json);
        /* Send an error response. */
        JsonResponse resp = {.id = 0, .ok = false, .error = "invalid request JSON"};
        char *resp_json = json_encode_response(&resp);
        if (resp_json) {
            json_frame_write(fd, resp_json, strlen(resp_json));
            free(resp_json);
        }
        return 1;
    }
    free(req_json);

    /* Intersect client's wants with server capabilities. */
    bool want_tokens = req.want_tokens && srv->enable_tokens;
    bool want_ast = req.want_ast && srv->enable_ast;
    bool want_bytecode = req.want_bytecode && srv->enable_bytecode;

    /* Build an EvalContext that streams say() output as JSON output
     * frames back to the client. The callback fires inside eval(),
     * which runs under the global eval lock. Each client thread owns
     * its fd exclusively, so no contention on the socket write. */
    ServerOutputCtx out_ctx = {.fd = fd, .request_id = req.id};
    EvalContext eval_ctx = {.write_fn = server_output_write, .userdata = &out_ctx};
    ReplResult rr = repl_eval_line(req.expr, want_tokens, want_ast, want_bytecode, &eval_ctx);

    /* Build the response. */
    JsonResponse resp = {0};
    resp.id = req.id;
    resp.ok = rr.ok;
    if (rr.ok) {
        resp.value = rr.value; /* borrow, we free after encode */
    } else {
        resp.error = rr.error;
    }
    resp.tokens = rr.tokens;
    resp.ast = rr.ast;
    resp.bytecode = rr.bytecode;

    char *resp_json = json_encode_response(&resp);
    bool send_ok = false;
    if (resp_json) {
        send_ok = json_frame_write(fd, resp_json, strlen(resp_json));
        free(resp_json);
    }

    json_request_free(&req);
    repl_result_free(&rr);

    return send_ok ? 1 : 0;
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

    while (!srv->shutdown) {
        int rc = handle_json_request(fd, srv);
        if (rc == 0)
            break; /* Real EOF or error. */
        /* rc == -1 (timeout) or rc == 1 (success): keep looping. */
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
            if (srv->shutdown)
                break;
            continue;
        }

        /* Set a recv timeout so threads don't block forever. */
        struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ClientArg *ca = malloc(sizeof(ClientArg));
        if (!ca) {
            close(client_fd);
            continue;
        }
        ca->client_fd = client_fd;
        ca->server = srv;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        snprintf(ca->addr_str, sizeof(ca->addr_str), "%s:%u", ip, ntohs(client_addr.sin_port));

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread_fn, ca) != 0) {
            free(ca);
            close(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    return NULL;
}

/* ============================================================
 * Public API
 * ============================================================ */

ReplServer *repl_server_start(const char *host, uint16_t port, bool enable_tokens, bool enable_ast,
                              bool enable_bytecode, const char **err_out) {
    ReplServer *srv = calloc(1, sizeof(ReplServer));
    if (!srv) {
        if (err_out)
            *err_out = "out of memory";
        return NULL;
    }
    srv->listen_fd = -1;
    srv->enable_tokens = enable_tokens;
    srv->enable_ast = enable_ast;
    srv->enable_bytecode = enable_bytecode;

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        if (err_out)
            *err_out = "failed to create socket";
        free(srv);
        return NULL;
    }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

    if (listen(srv->listen_fd, 16) < 0) {
        if (err_out)
            *err_out = "failed to listen";
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

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

    server->shutdown = true;
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }

    pthread_join(server->accept_thread, NULL);

    /*
     * Client threads are detached. Give in-flight clients time to finish.
     */
    usleep(100000); /* 100ms grace period. */

    free(server);
}
