/*
 * main.c - Cutlet CLI entry point
 *
 * Usage:
 *   cutlet repl [--tokens] [--ast]                    start TCP REPL server (default)
 *   cutlet repl --listen [HOST:PORT] [--tokens] [--ast]  start TCP REPL server
 *   cutlet repl --connect [HOST:PORT] [--tokens] [--ast] connect to running server
 *
 * The default mode starts a TCP server. There is no local-only eval mode.
 *
 * Debug flags:
 *   --tokens  Enable token debug output
 *   --ast     Enable AST debug output
 *
 * Both flags can be combined. Server emits debug output only if started
 * with the corresponding flag. Client prints debug output only if started
 * with the flag. If the server sends debug fields the client didn't request,
 * the client warns once on stderr and ignores them.
 */

#include "json.h"
#include "repl.h"
#include "repl_server.h"
#include "runtime.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Maximum line length for input. */
#define MAX_LINE_LEN 4096

/* Default host and port for --listen / --connect. */
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7117

/*
 * Print usage information.
 */
static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s repl [--tokens] [--ast] [--listen [HOST:PORT] | --connect [HOST:PORT]]\n",
            prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Commands:\n");
    fprintf(
        stderr,
        "  repl                        Start the TCP REPL server (default, same as --listen)\n");
    fprintf(stderr, "  repl --listen [HOST:PORT]   Start a TCP REPL server (default: %s:%d)\n",
            DEFAULT_HOST, DEFAULT_PORT);
    fprintf(stderr,
            "  repl --connect [HOST:PORT]  Connect to a running REPL server (default: %s:%d)\n",
            DEFAULT_HOST, DEFAULT_PORT);
    fprintf(stderr, "\nDebug flags:\n");
    fprintf(stderr, "  --tokens    Show tokenizer output for each expression\n");
    fprintf(stderr, "  --ast       Show AST output for each expression\n");
}

/*
 * Parse a HOST:PORT string with defaults. Returns true on success.
 *
 * Accepted forms:
 *   NULL or ""       -> DEFAULT_HOST:DEFAULT_PORT
 *   ":PORT"          -> DEFAULT_HOST:PORT
 *   "HOST:PORT"      -> HOST:PORT
 */
static bool parse_host_port(const char *arg, char *host_buf, size_t host_buf_len,
                            uint16_t *port_out) {
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
 */
static int run_listen(bool enable_tokens, bool enable_ast, const char *addr) {
    char host[256];
    uint16_t port;

    if (!parse_host_port(addr, host, sizeof(host), &port)) {
        fprintf(stderr, "Error: invalid listen address '%s' (expected HOST:PORT)\n", addr);
        return 1;
    }

    const char *err = NULL;
    g_server = repl_server_start(host, port, enable_tokens, enable_ast, &err);
    if (!g_server) {
        fprintf(stderr, "Error: failed to start server: %s\n", err ? err : "unknown");
        return 1;
    }

    printf("Listening on %s:%u\n", host, repl_server_port(g_server));
    fflush(stdout);

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    while (g_server) {
        pause();
    }

    return 0;
}

/*
 * Run the REPL client.
 * Connects to a running TCP REPL server, reads expressions from stdin,
 * sends JSON-framed requests, and prints results.
 *
 * Shows a "cutlet> " prompt when stdin is a TTY.
 */
static int run_connect(bool want_tokens, bool want_ast, const char *addr) {
    char host[256];
    uint16_t port;

    if (!parse_host_port(addr, host, sizeof(host), &port)) {
        fprintf(stderr, "Error: invalid connect address '%s' (expected HOST:PORT)\n", addr);
        return 1;
    }

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

    int is_tty = isatty(fileno(stdin));
    char line[MAX_LINE_LEN];
    unsigned long req_id = 0;
    bool warned_extra_tokens = false;
    bool warned_extra_ast = false;

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

        /* Build and send JSON request. */
        req_id++;
        JsonRequest req = {
            .id = req_id,
            .expr = line,
            .want_tokens = want_tokens,
            .want_ast = want_ast,
        };
        char *req_json = json_encode_request(&req);
        if (!req_json) {
            fprintf(stderr, "Error: failed to encode request\n");
            continue;
        }

        if (!json_frame_write(fd, req_json, strlen(req_json))) {
            free(req_json);
            fprintf(stderr, "Error: connection lost\n");
            close(fd);
            return 1;
        }
        free(req_json);

        /* Read JSON response. */
        size_t resp_len = 0;
        char *resp_json = json_frame_read(fd, &resp_len, NULL);
        if (!resp_json) {
            fprintf(stderr, "Error: connection lost\n");
            close(fd);
            return 1;
        }

        JsonResponse resp;
        if (!json_parse_response(resp_json, resp_len, &resp)) {
            fprintf(stderr, "Error: invalid response from server\n");
            free(resp_json);
            close(fd);
            return 1;
        }
        free(resp_json);

        /* Warn once if server sends debug fields client didn't request. */
        if (!want_tokens && resp.tokens && !warned_extra_tokens) {
            fprintf(stderr, "Warning: server sent token debug output (not requested)\n");
            warned_extra_tokens = true;
        }
        if (!want_ast && resp.ast && !warned_extra_ast) {
            fprintf(stderr, "Warning: server sent AST debug output (not requested)\n");
            warned_extra_ast = true;
        }

        /* Print output in order: tokens, ast, value/error. */
        if (want_tokens && resp.tokens) {
            puts(resp.tokens);
        }
        if (want_ast && resp.ast) {
            puts(resp.ast);
        }

        if (resp.ok) {
            if (resp.value) {
                puts(resp.value);
            }
            /* value==NULL means blank input; print nothing. */
        } else {
            if (resp.error) {
                printf("ERR %s\n", resp.error);
            }
        }
        fflush(stdout);

        json_response_free(&resp);
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (!runtime_init()) {
        fprintf(stderr, "Error: failed to initialize runtime\n");
        return 1;
    }

    if (argc < 2) {
        print_usage(argv[0]);
        runtime_destroy();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "repl") == 0) {
        /* Parse flags: --tokens, --ast, --listen, --connect (order-independent). */
        bool flag_tokens = false;
        bool flag_ast = false;
        bool flag_listen = false;
        bool flag_connect = false;
        const char *listen_addr = NULL;
        const char *connect_addr = NULL;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--tokens") == 0) {
                flag_tokens = true;
            } else if (strcmp(argv[i], "--ast") == 0) {
                flag_ast = true;
            } else if (strcmp(argv[i], "--listen") == 0) {
                flag_listen = true;
                /* Next arg is optional address if it doesn't start with --. */
                if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                    listen_addr = argv[++i];
                }
            } else if (strcmp(argv[i], "--connect") == 0) {
                flag_connect = true;
                if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                    /* But allow ":PORT" form which starts with : not --. */
                    connect_addr = argv[++i];
                }
            } else {
                fprintf(stderr, "Unknown repl option: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }

        if (flag_listen && flag_connect) {
            fprintf(stderr, "Error: --listen and --connect are mutually exclusive\n");
            return 1;
        }

        if (flag_connect) {
            return run_connect(flag_tokens, flag_ast, connect_addr);
        }

        /* Default: start server (--listen is optional). */
        return run_listen(flag_tokens, flag_ast, listen_addr);
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
