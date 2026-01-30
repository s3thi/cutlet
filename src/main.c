/*
 * main.c - Cutlet CLI entry point
 *
 * Usage:
 *   cutlet repl                            stdin/stdout REPL
 *   cutlet repl --listen HOST:PORT         start TCP REPL server
 *   cutlet repl --connect HOST:PORT        connect to running server
 *
 * The stdin REPL reads lines from stdin, tokenizes each line, and prints
 * a formatted result to stdout.
 *
 * The TCP server accepts multiple clients, each in its own thread.
 * The client auto-generates request IDs and strips protocol framing
 * so the user just types expressions and sees results.
 */

#include "repl.h"
#include "repl_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Maximum line length for input */
#define MAX_LINE_LEN 4096

/* Default host and port for --listen / --connect. */
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7117

/*
 * Print usage information.
 */
static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s repl [--ast] [--listen [HOST:PORT] | --connect [HOST:PORT]]\n",
            prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  repl                      Start the REPL (read-eval-print loop)\n");
    fprintf(stderr, "  repl --listen [HOST:PORT]  Start a TCP REPL server (default: %s:%d)\n",
            DEFAULT_HOST, DEFAULT_PORT);
    fprintf(stderr,
            "  repl --connect [HOST:PORT] Connect to a running REPL server (default: %s:%d)\n",
            DEFAULT_HOST, DEFAULT_PORT);
}

/*
 * Run the stdin/stdout REPL loop.
 * Reads lines from stdin, formats each, prints result to stdout.
 * Returns 0 on success (EOF), 1 on error.
 */
static int run_repl(bool ast_mode) {
    char line[MAX_LINE_LEN];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        /* Remove trailing newline if present */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        /* Also handle CRLF */
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        /* Format and print result */
        char *result = ast_mode ? repl_format_line_ast(line) : repl_format_line(line);
        if (result == NULL) {
            fprintf(stderr, "Error: memory allocation failed\n");
            return 1;
        }

        puts(result);
        fflush(stdout);
        free(result);
    }

    /* Check for read error vs EOF */
    if (ferror(stdin)) {
        fprintf(stderr, "Error: failed to read from stdin\n");
        return 1;
    }

    return 0;
}

/*
 * Parse a HOST:PORT string with defaults. Returns true on success.
 *
 * Accepted forms:
 *   NULL or ""       -> DEFAULT_HOST:DEFAULT_PORT
 *   ":PORT"          -> DEFAULT_HOST:PORT
 *   "HOST:PORT"      -> HOST:PORT
 *
 * On success, writes the host into host_buf (must be at least host_buf_len)
 * and the port into *port_out.
 */
static bool parse_host_port(const char *arg, char *host_buf, size_t host_buf_len,
                            uint16_t *port_out) {
    /* No arg or empty string: use all defaults. */
    if (!arg || *arg == '\0') {
        if (strlen(DEFAULT_HOST) >= host_buf_len)
            return false;
        strcpy(host_buf, DEFAULT_HOST);
        *port_out = DEFAULT_PORT;
        return true;
    }

    const char *colon = strrchr(arg, ':');
    if (!colon)
        return false;

    /* ":PORT" form — default host. */
    if (colon == arg) {
        if (strlen(DEFAULT_HOST) >= host_buf_len)
            return false;
        strcpy(host_buf, DEFAULT_HOST);
    } else {
        size_t host_len = (size_t)(colon - arg);
        if (host_len >= host_buf_len)
            return false;
        memcpy(host_buf, arg, host_len);
        host_buf[host_len] = '\0';
    }

    const char *port_str = colon + 1;
    if (*port_str == '\0')
        return false;

    char *end;
    long port_val = strtol(port_str, &end, 10);
    if (*end != '\0' || port_val < 0 || port_val > 65535)
        return false;

    *port_out = (uint16_t)port_val;
    return true;
}

/* Global server handle for signal-based shutdown. */
static ReplServer *g_server = NULL;

static void handle_sigint(int sig) {
    (void)sig;
    if (g_server) {
        repl_server_stop(g_server);
        g_server = NULL;
    }
    _exit(0);
}

/*
 * Run the TCP REPL server.
 * Listens on the given HOST:PORT, prints the actual listening address
 * to stdout (important for ephemeral port), then blocks until killed.
 */
static int run_listen(const char *addr) {
    char host[256];
    uint16_t port;

    if (!parse_host_port(addr, host, sizeof(host), &port)) {
        fprintf(stderr, "Error: invalid listen address '%s' (expected HOST:PORT)\n", addr);
        return 1;
    }

    const char *err = NULL;
    g_server = repl_server_start(host, port, &err);
    if (!g_server) {
        fprintf(stderr, "Error: failed to start server: %s\n", err ? err : "unknown");
        return 1;
    }

    /* Print the actual address so callers (and tests) can discover
     * the port when using ephemeral port 0. Flush immediately. */
    printf("Listening on %s:%u\n", host, repl_server_port(g_server));
    fflush(stdout);

    /* Install signal handler for clean shutdown. */
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* Block until signaled. */
    while (g_server) {
        pause();
    }

    return 0;
}

/*
 * Run the REPL client.
 * Connects to a running TCP REPL server, reads expressions from stdin,
 * sends them with auto-generated IDs, and prints the results (with
 * protocol framing stripped).
 *
 * Shows a "cutlet> " prompt when stdin is a TTY.
 */
static int run_connect(const char *addr) {
    char host[256];
    uint16_t port;

    if (!parse_host_port(addr, host, sizeof(host), &port)) {
        fprintf(stderr, "Error: invalid connect address '%s' (expected HOST:PORT)\n", addr);
        return 1;
    }

    /* Create and connect socket. */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Error: failed to create socket\n");
        return 1;
    }

    struct sockaddr_in saddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    if (inet_pton(AF_INET, host, &saddr.sin_addr) != 1) {
        fprintf(stderr, "Error: invalid host '%s'\n", host);
        close(fd);
        return 1;
    }

    if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        fprintf(stderr, "Error: failed to connect to %s:%u\n", host, port);
        close(fd);
        return 1;
    }

    /* Set a recv timeout so we don't hang forever. */
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int is_tty = isatty(fileno(stdin));
    char line[MAX_LINE_LEN];
    unsigned long req_id = 0;

    while (1) {
        if (is_tty) {
            printf("cutlet> ");
            fflush(stdout);
        }

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        /* Strip trailing newline/CRLF. */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Build protocol line: "<id> <expr>\n" */
        req_id++;
        char sendbuf[MAX_LINE_LEN + 32];
        int slen = snprintf(sendbuf, sizeof(sendbuf), "%lu %s\n", req_id, line);
        if (slen < 0 || (size_t)slen >= sizeof(sendbuf))
            continue;

        /* Send request. */
        ssize_t sent = send(fd, sendbuf, (size_t)slen, 0);
        if (sent <= 0) {
            fprintf(stderr, "Error: connection lost\n");
            close(fd);
            return 1;
        }

        /* Read response line. */
        char recvbuf[MAX_LINE_LEN + 128];
        size_t rpos = 0;
        while (rpos < sizeof(recvbuf) - 1) {
            ssize_t n = recv(fd, recvbuf + rpos, 1, 0);
            if (n <= 0) {
                fprintf(stderr, "Error: connection lost\n");
                close(fd);
                return 1;
            }
            rpos++;
            if (recvbuf[rpos - 1] == '\n')
                break;
        }
        recvbuf[rpos] = '\0';

        /*
         * Strip protocol framing. Response is "-> <id> <body>\n".
         * We want to print just <body>.
         * Skip "-> ", then skip the ID and the space after it.
         */
        char *body = recvbuf;
        if (strncmp(body, "-> ", 3) == 0) {
            body += 3;
            /* Skip the ID (digits). */
            while (*body && *body != ' ' && *body != '\n')
                body++;
            /* Skip the space after ID. */
            if (*body == ' ')
                body++;
        }

        /* Remove trailing newline for clean output. */
        size_t blen = strlen(body);
        if (blen > 0 && body[blen - 1] == '\n')
            body[blen - 1] = '\0';

        puts(body);
        fflush(stdout);
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    /* Need at least one argument (the command) */
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Parse command */
    const char *cmd = argv[1];

    if (strcmp(cmd, "repl") == 0) {
        /* Check for --ast flag */
        bool ast_mode = false;
        int arg_idx = 2;
        if (arg_idx < argc && strcmp(argv[arg_idx], "--ast") == 0) {
            ast_mode = true;
            arg_idx++;
        }

        /* Check for --listen or --connect flags. */
        if (arg_idx < argc && strcmp(argv[arg_idx], "--listen") == 0) {
            return run_listen(arg_idx + 1 < argc ? argv[arg_idx + 1] : NULL);
        }
        if (arg_idx < argc && strcmp(argv[arg_idx], "--connect") == 0) {
            return run_connect(arg_idx + 1 < argc ? argv[arg_idx + 1] : NULL);
        }
        if (arg_idx == argc) {
            return run_repl(ast_mode);
        }
        /* Unknown repl flag */
        fprintf(stderr, "Unknown repl option: %s\n", argv[arg_idx]);
        print_usage(argv[0]);
        return 1;
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
