/*
 * main.c - Cutlet CLI entry point
 *
 * Usage:
 *   cutlet repl [--tokens] [--ast] [--bytecode]                       local REPL (default)
 *   cutlet repl --listen [HOST:PORT] [--tokens] [--ast] [--bytecode]  start TCP REPL server
 *   cutlet repl --connect [HOST:PORT] [--tokens] [--ast] [--bytecode] connect to running server
 *   cutlet run <file> [--tokens] [--ast] [--bytecode]                 execute a file directly
 *
 * The default mode starts a local in-process REPL (no networking).
 * Use --listen to start a TCP REPL server, --connect to connect to one.
 *
 * Debug flags:
 *   --tokens    Enable token debug output
 *   --ast       Enable AST debug output
 *   --bytecode  Enable bytecode disassembly output
 *
 * All flags can be combined.
 */

#include "json.h"
#include "parser.h"
#include "repl.h"
#include "repl_server.h"
#include "runtime.h"

#include <isocline.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* Maximum line length for input. */
#define MAX_LINE_LEN 4096

/* Maximum buffer size for accumulated multiline input.
 * Cast to size_t to avoid implicit widening conversion warnings. */
#define MAX_INPUT_BUF ((size_t)MAX_LINE_LEN * 64)

/* History file location: ~/.cutlet/history */
#define CUTLET_DIR ".cutlet"
#define HISTORY_FILE "history"

/* Default host and port for --listen / --connect. */
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7117

/*
 * Print usage information.
 */
static void print_usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr,
            "  %s repl [--tokens] [--ast] [--bytecode] [--listen [HOST:PORT] | --connect "
            "[HOST:PORT]]\n",
            prog);
    fprintf(stderr, "  %s run <file> [--tokens] [--ast] [--bytecode]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  repl                        Start a local interactive REPL (default)\n");
    fprintf(stderr, "  repl --listen [HOST:PORT]   Start a TCP REPL server (default: %s:%d)\n",
            DEFAULT_HOST, DEFAULT_PORT);
    fprintf(stderr,
            "  repl --connect [HOST:PORT]  Connect to a running REPL server (default: %s:%d)\n",
            DEFAULT_HOST, DEFAULT_PORT);
    fprintf(stderr, "  run <file>                  Execute a Cutlet source file\n");
    fprintf(stderr, "\nDebug flags:\n");
    fprintf(stderr, "  --tokens    Show tokenizer output for each expression\n");
    fprintf(stderr, "  --ast       Show AST output for each expression\n");
    fprintf(stderr, "  --bytecode  Show bytecode disassembly for each expression\n");
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

/*
 * Get the path to the history file (~/.cutlet/history).
 * Creates the ~/.cutlet directory if it doesn't exist.
 * Returns a heap-allocated string on success, NULL on failure.
 */
static char *get_history_path(void) {
    /* Get home directory */
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    if (!home) {
        return NULL;
    }

    /* Build path: $HOME/.cutlet/history */
    size_t home_len = strlen(home);
    size_t dir_len = home_len + 1 + strlen(CUTLET_DIR);
    size_t path_len = dir_len + 1 + strlen(HISTORY_FILE) + 1;

    char *dir_path = malloc(dir_len + 1);
    if (!dir_path) {
        return NULL;
    }
    snprintf(dir_path, dir_len + 1, "%s/%s", home, CUTLET_DIR);

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(dir_path, &st) != 0) {
        if (mkdir(dir_path, 0755) != 0) {
            free(dir_path);
            return NULL;
        }
    }

    /* Build full path */
    char *path = malloc(path_len);
    if (!path) {
        free(dir_path);
        return NULL;
    }
    snprintf(path, path_len, "%s/%s", dir_path, HISTORY_FILE);
    free(dir_path);

    return path;
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
static int run_listen(bool enable_tokens, bool enable_ast, bool enable_bytecode, const char *addr) {
    char host[256];
    uint16_t port;

    if (!parse_host_port(addr, host, sizeof(host), &port)) {
        fprintf(stderr, "Error: invalid listen address '%s' (expected HOST:PORT)\n", addr);
        return 1;
    }

    const char *err = NULL;
    g_server = repl_server_start(host, port, enable_tokens, enable_ast, enable_bytecode, &err);
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

/* ANSI escape codes for colored output (used only when stdout is a TTY). */
#define ANSI_GREEN "\033[32m"
#define ANSI_DIM "\033[2m"
#define ANSI_RESET "\033[0m"

/*
 * Print a REPL result (tokens, ast, value/error) to stdout.
 *
 * Shared formatting logic used by both the TCP client (send_and_print)
 * and the local REPL (run_local_repl). Handles color/prefix formatting
 * based on whether stdout is a TTY.
 *
 * When use_color is true (stdout is a TTY):
 *   - Debug output: dim "# TOKENS ..." / "# AST ..."
 *   - Results: green "=> value"
 *   - Errors: "ERR message"
 * When use_color is false (pipe/redirect):
 *   - All output is raw, no ANSI codes, no prefixes.
 *
 * ok/value/error/tokens/ast correspond to ReplResult or JsonResponse fields.
 */
static void print_repl_result(bool ok, const char *value, const char *error, const char *tokens,
                              const char *ast, const char *bytecode, bool want_tokens,
                              bool want_ast, bool want_bytecode, bool use_color) {
    /* Print output in order: tokens, ast, bytecode, value/error. */
    if (want_tokens && tokens) {
        if (use_color)
            printf(ANSI_DIM "# %s" ANSI_RESET "\n", tokens);
        else
            puts(tokens);
    }
    if (want_ast && ast) {
        if (use_color)
            printf(ANSI_DIM "# %s" ANSI_RESET "\n", ast);
        else
            puts(ast);
    }
    if (want_bytecode && bytecode) {
        if (use_color)
            printf(ANSI_DIM "# %s" ANSI_RESET "\n", bytecode);
        else
            puts(bytecode);
    }

    if (ok) {
        if (value) {
            if (use_color)
                printf(ANSI_GREEN "=> %s" ANSI_RESET "\n", value);
            else
                puts(value);
        }
        /* value==NULL means blank input; print nothing. */
    } else {
        if (error) {
            printf("ERR %s\n", error);
        }
    }
    fflush(stdout);
}

/*
 * Helper: send an expression to the server and print the response.
 * Reads frames in a loop (nREPL-style): output frames from say() are
 * printed to stdout immediately, and the terminal result frame is
 * printed with optional color/prefix formatting.
 *
 * When use_color is true (stdout is a TTY):
 *   - Results: green "=> value"
 *   - Debug:   dim "# TOKENS ..." / "# AST ..."
 *   - say() output: raw (no prefix, no color)
 * When use_color is false (pipe/redirect):
 *   - All output is raw, no ANSI codes, no prefixes.
 *
 * Returns true on success, false if connection is lost.
 */
static bool send_and_print(int fd, const char *expr, unsigned long *req_id, bool want_tokens,
                           bool want_ast, bool want_bytecode, bool *warned_extra_tokens,
                           bool *warned_extra_ast, bool *warned_extra_bytecode, bool use_color) {
    (*req_id)++;
    /* Cast expr to char* for JsonRequest; we don't call json_request_free so it's safe */
    JsonRequest req = {
        .id = *req_id,
        .expr = (char *)expr,
        .want_tokens = want_tokens,
        .want_ast = want_ast,
        .want_bytecode = want_bytecode,
    };
    char *req_json = json_encode_request(&req);
    if (!req_json) {
        fprintf(stderr, "Error: failed to encode request\n");
        return true; /* Not a connection error, continue */
    }

    if (!json_frame_write(fd, req_json, strlen(req_json))) {
        free(req_json);
        fprintf(stderr, "Error: connection lost\n");
        return false;
    }
    free(req_json);

    /* Read frames in a loop until we get the terminal result frame.
     * Output frames (from say()) are printed immediately.
     * The result frame is handled last. */
    while (true) {
        size_t frame_len = 0;
        char *frame_json = json_frame_read(fd, &frame_len, NULL);
        if (!frame_json) {
            fprintf(stderr, "Error: connection lost\n");
            return false;
        }

        JsonFrameType ftype = json_frame_type(frame_json, frame_len);

        if (ftype == JSON_FRAME_OUTPUT) {
            /* Print say() output immediately, raw (no prefix/color). */
            JsonOutputFrame oframe;
            if (json_parse_output(frame_json, frame_len, &oframe)) {
                if (oframe.data) {
                    fputs(oframe.data, stdout);
                    fflush(stdout);
                }
                json_output_frame_free(&oframe);
            }
            free(frame_json);
            continue;
        }

        if (ftype == JSON_FRAME_RESULT) {
            JsonResponse resp;
            if (!json_parse_response(frame_json, frame_len, &resp)) {
                fprintf(stderr, "Error: invalid response from server\n");
                free(frame_json);
                return false;
            }
            free(frame_json);

            /* Warn once if server sends debug fields client didn't request. */
            if (!want_tokens && resp.tokens && !*warned_extra_tokens) {
                fprintf(stderr, "Warning: server sent token debug output (not requested)\n");
                *warned_extra_tokens = true;
            }
            if (!want_ast && resp.ast && !*warned_extra_ast) {
                fprintf(stderr, "Warning: server sent AST debug output (not requested)\n");
                *warned_extra_ast = true;
            }
            if (!want_bytecode && resp.bytecode && !*warned_extra_bytecode) {
                fprintf(stderr, "Warning: server sent bytecode debug output (not requested)\n");
                *warned_extra_bytecode = true;
            }

            print_repl_result(resp.ok, resp.value, resp.error, resp.tokens, resp.ast, resp.bytecode,
                              want_tokens, want_ast, want_bytecode, use_color);

            json_response_free(&resp);
            return true;
        }

        /* Unknown frame type — skip it. */
        free(frame_json);
    }
}

/*
 * Run the REPL client in interactive mode (TTY) using isocline.
 * Supports multiline input with continuation prompts.
 */
static int run_connect_interactive(int fd, bool want_tokens, bool want_ast, bool want_bytecode) {
    unsigned long req_id = 0;
    bool warned_extra_tokens = false;
    bool warned_extra_ast = false;
    bool warned_extra_bytecode = false;
    bool use_color = isatty(fileno(stdout));

    /* Initialize isocline */
    char *history_path = get_history_path();
    if (history_path) {
        ic_set_history(history_path, 200);
        free(history_path);
    }

    /* Disable isocline's built-in multiline mode; we handle continuation ourselves
     * using parser_is_complete() to auto-detect when an expression is finished. */
    ic_enable_multiline(false);

    /* Disable default file completion — not useful for a language REPL */
    ic_set_default_completer(NULL, NULL);

    /* Buffer for accumulating multiline input */
    char *input_buf = malloc(MAX_INPUT_BUF);
    if (!input_buf) {
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }
    input_buf[0] = '\0';
    size_t input_len = 0;
    bool continuing = false;

    while (1) {
        /* Use isocline's prompt marker API for prompt display.
         * Update the marker each iteration to switch between primary
         * and continuation prompts. */
        ic_set_prompt_marker(continuing ? "    ... " : "cutlet> ", NULL);
        char *line = ic_readline(NULL);

        if (!line) {
            /* EOF (Ctrl+D) or error */
            if (input_len > 0) {
                /* Send any accumulated incomplete input to get error message */
                if (!send_and_print(fd, input_buf, &req_id, want_tokens, want_ast, want_bytecode,
                                    &warned_extra_tokens, &warned_extra_ast, &warned_extra_bytecode,
                                    use_color)) {
                    free(input_buf);
                    return 1;
                }
            }
            break;
        }

        /* Append line to input buffer, adding newline if continuing */
        size_t line_len = strlen(line);
        if (input_len + line_len + 2 >= MAX_INPUT_BUF) {
            fprintf(stderr, "Error: input too long\n");
            ic_free(line);
            input_buf[0] = '\0';
            input_len = 0;
            continuing = false;
            continue;
        }

        if (continuing && input_len > 0) {
            /* Add newline separator between lines */
            input_buf[input_len++] = '\n';
        }
        memcpy(input_buf + input_len, line, line_len);
        input_len += line_len;
        input_buf[input_len] = '\0';
        ic_free(line);

        /* Check if input is complete */
        if (parser_is_complete(input_buf)) {
            /* Input is complete (or has a real error), send to server */
            if (!send_and_print(fd, input_buf, &req_id, want_tokens, want_ast, want_bytecode,
                                &warned_extra_tokens, &warned_extra_ast, &warned_extra_bytecode,
                                use_color)) {
                free(input_buf);
                return 1;
            }
            /* Reset buffer for next expression */
            input_buf[0] = '\0';
            input_len = 0;
            continuing = false;
        } else {
            /* Input is incomplete, continue reading */
            continuing = true;
        }
    }

    free(input_buf);
    return 0;
}

/*
 * Run the REPL client in non-interactive mode (pipe/file).
 * Reads line by line without accumulation (original behavior).
 */
static int run_connect_pipe(int fd, bool want_tokens, bool want_ast, bool want_bytecode) {
    char line[MAX_LINE_LEN];
    unsigned long req_id = 0;
    bool warned_extra_tokens = false;
    bool warned_extra_ast = false;
    bool warned_extra_bytecode = false;
    bool use_color = isatty(fileno(stdout));

    while (fgets(line, sizeof(line), stdin) != NULL) {
        /* Strip trailing newline/CRLF. */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (!send_and_print(fd, line, &req_id, want_tokens, want_ast, want_bytecode,
                            &warned_extra_tokens, &warned_extra_ast, &warned_extra_bytecode,
                            use_color)) {
            return 1;
        }
    }

    return 0;
}

/*
 * Run the REPL client.
 * Connects to a running TCP REPL server, reads expressions from stdin,
 * sends JSON-framed requests, and prints results.
 *
 * Uses isocline for interactive input with multiline support when stdin is a TTY.
 */
static int run_connect(bool want_tokens, bool want_ast, bool want_bytecode, const char *addr) {
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

    int result;
    if (isatty(fileno(stdin))) {
        result = run_connect_interactive(fd, want_tokens, want_ast, want_bytecode);
    } else {
        result = run_connect_pipe(fd, want_tokens, want_ast, want_bytecode);
    }

    close(fd);
    return result;
}

/*
 * EvalContext write callback for file execution mode.
 * Writes output directly to stdout via fwrite().
 * userdata is unused (NULL).
 */
static void stdout_write_fn(void *userdata, const char *data, size_t len) {
    (void)userdata;
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

/*
 * Execute a Cutlet source file directly (no TCP server).
 *
 * Reads the entire file into memory, parses it as a block,
 * evaluates it, and prints any say() output to stdout.
 * The final expression value is NOT printed (unlike the REPL).
 * Parse/eval errors are printed to stderr.
 *
 * Returns 0 on success, 1 on error.
 */
static int run_file(const char *filename, bool enable_tokens, bool enable_ast,
                    bool enable_bytecode) {
    /* Open and read the file. */
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return 1;
    }

    /* Get file size. */
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: cannot read file '%s'\n", filename);
        fclose(f);
        return 1;
    }
    long file_size = ftell(f);
    if (file_size < 0) {
        fprintf(stderr, "Error: cannot read file '%s'\n", filename);
        fclose(f);
        return 1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: cannot read file '%s'\n", filename);
        fclose(f);
        return 1;
    }

    /* Read file contents into a buffer. */
    char *source = malloc((size_t)file_size + 1);
    if (!source) {
        fprintf(stderr, "Error: memory allocation failed\n");
        fclose(f);
        return 1;
    }
    /* Pre-zero the buffer so it's always null-terminated regardless
     * of how many bytes fread returns. */
    memset(source, 0, (size_t)file_size + 1);
    fread(source, 1, (size_t)file_size, f);
    if (ferror(f)) {
        fprintf(stderr, "Error: cannot read file '%s'\n", filename);
        free(source);
        fclose(f);
        return 1;
    }
    fclose(f);

    /* Create an EvalContext that writes say() output to stdout. */
    EvalContext ctx = {.write_fn = stdout_write_fn, .userdata = NULL};

    /* Evaluate the file contents. repl_eval_line() handles parsing
     * as a block (multi-line) and evaluation in one call. */
    ReplResult r = repl_eval_line(source, enable_tokens, enable_ast, enable_bytecode, &ctx);
    free(source);

    /* Print debug output if requested. */
    if (enable_tokens && r.tokens) {
        printf("%s\n", r.tokens);
    }
    if (enable_ast && r.ast) {
        printf("%s\n", r.ast);
    }
    if (enable_bytecode && r.bytecode) {
        printf("%s\n", r.bytecode);
    }

    int exit_code = 0;

    if (!r.ok) {
        /* Print error to stderr. */
        if (r.error) {
            fprintf(stderr, "Error: %s\n", r.error);
        } else {
            fprintf(stderr, "Error: unknown error\n");
        }
        exit_code = 1;
    }
    /* On success, do NOT print the final value (unlike REPL). */

    repl_result_free(&r);
    return exit_code;
}

/*
 * Run the local REPL in interactive mode (TTY) using isocline.
 * Same multiline support as the TCP client's interactive mode,
 * but evaluates directly via repl_eval_line() — no networking.
 */
static int run_local_repl_interactive(bool enable_tokens, bool enable_ast, bool enable_bytecode) {
    bool use_color = isatty(fileno(stdout));

    /* Initialize isocline */
    char *history_path = get_history_path();
    if (history_path) {
        ic_set_history(history_path, 200);
        free(history_path);
    }

    /* Disable isocline's built-in multiline mode; we handle continuation ourselves
     * using parser_is_complete() to auto-detect when an expression is finished. */
    ic_enable_multiline(false);

    /* Disable default file completion — not useful for a language REPL */
    ic_set_default_completer(NULL, NULL);

    /* Create an EvalContext that writes say() output directly to stdout. */
    EvalContext ctx = {.write_fn = stdout_write_fn, .userdata = NULL};

    /* Buffer for accumulating multiline input */
    char *input_buf = malloc(MAX_INPUT_BUF);
    if (!input_buf) {
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }
    input_buf[0] = '\0';
    size_t input_len = 0;
    bool continuing = false;

    while (1) {
        ic_set_prompt_marker(continuing ? "    ... " : "cutlet> ", NULL);
        char *line = ic_readline(NULL);

        if (!line) {
            /* EOF (Ctrl+D) or error */
            if (input_len > 0) {
                /* Send any accumulated incomplete input to get error message */
                ReplResult r =
                    repl_eval_line(input_buf, enable_tokens, enable_ast, enable_bytecode, &ctx);
                print_repl_result(r.ok, r.value, r.error, r.tokens, r.ast, r.bytecode,
                                  enable_tokens, enable_ast, enable_bytecode, use_color);
                repl_result_free(&r);
            }
            break;
        }

        /* Append line to input buffer, adding newline if continuing */
        size_t line_len = strlen(line);
        if (input_len + line_len + 2 >= MAX_INPUT_BUF) {
            fprintf(stderr, "Error: input too long\n");
            ic_free(line);
            input_buf[0] = '\0';
            input_len = 0;
            continuing = false;
            continue;
        }

        if (continuing && input_len > 0) {
            input_buf[input_len++] = '\n';
        }
        memcpy(input_buf + input_len, line, line_len);
        input_len += line_len;
        input_buf[input_len] = '\0';
        ic_free(line);

        /* Check if input is complete */
        if (parser_is_complete(input_buf)) {
            ReplResult r =
                repl_eval_line(input_buf, enable_tokens, enable_ast, enable_bytecode, &ctx);
            print_repl_result(r.ok, r.value, r.error, r.tokens, r.ast, r.bytecode, enable_tokens,
                              enable_ast, enable_bytecode, use_color);
            repl_result_free(&r);

            /* Reset buffer for next expression */
            input_buf[0] = '\0';
            input_len = 0;
            continuing = false;
        } else {
            continuing = true;
        }
    }

    free(input_buf);
    return 0;
}

/*
 * Run the local REPL in pipe mode (stdin is not a TTY).
 * Reads line by line, accumulating multiline input using
 * parser_is_complete() (like Python's stdin behavior).
 * Evaluates directly via repl_eval_line() — no networking.
 */
static int run_local_repl_pipe(bool enable_tokens, bool enable_ast, bool enable_bytecode) {
    bool use_color = isatty(fileno(stdout));
    EvalContext ctx = {.write_fn = stdout_write_fn, .userdata = NULL};

    char line[MAX_LINE_LEN];

    /* Buffer for accumulating multiline input */
    char *input_buf = malloc(MAX_INPUT_BUF);
    if (!input_buf) {
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }
    input_buf[0] = '\0';
    size_t input_len = 0;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        /* Strip trailing newline/CRLF. */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Append line to input buffer */
        if (input_len + len + 2 >= MAX_INPUT_BUF) {
            fprintf(stderr, "Error: input too long\n");
            input_buf[0] = '\0';
            input_len = 0;
            continue;
        }

        if (input_len > 0) {
            input_buf[input_len++] = '\n';
        }
        memcpy(input_buf + input_len, line, len);
        input_len += len;
        input_buf[input_len] = '\0';

        /* Check if input is complete */
        if (parser_is_complete(input_buf)) {
            ReplResult r =
                repl_eval_line(input_buf, enable_tokens, enable_ast, enable_bytecode, &ctx);
            print_repl_result(r.ok, r.value, r.error, r.tokens, r.ast, r.bytecode, enable_tokens,
                              enable_ast, enable_bytecode, use_color);
            repl_result_free(&r);

            /* Reset buffer for next expression */
            input_buf[0] = '\0';
            input_len = 0;
        }
        /* If not complete, continue accumulating lines */
    }

    /* Flush any remaining incomplete input (EOF reached mid-expression) */
    if (input_len > 0) {
        ReplResult r = repl_eval_line(input_buf, enable_tokens, enable_ast, enable_bytecode, &ctx);
        print_repl_result(r.ok, r.value, r.error, r.tokens, r.ast, r.bytecode, enable_tokens,
                          enable_ast, enable_bytecode, use_color);
        repl_result_free(&r);
    }

    free(input_buf);
    return 0;
}

/*
 * Run the local REPL (no networking).
 * Dispatches to interactive (TTY) or pipe mode based on stdin.
 */
static int run_local_repl(bool enable_tokens, bool enable_ast, bool enable_bytecode) {
    if (isatty(fileno(stdin))) {
        return run_local_repl_interactive(enable_tokens, enable_ast, enable_bytecode);
    }
    return run_local_repl_pipe(enable_tokens, enable_ast, enable_bytecode);
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
        /* Parse flags: --tokens, --ast, --bytecode, --listen, --connect (order-independent). */
        bool flag_tokens = false;
        bool flag_ast = false;
        bool flag_bytecode = false;
        bool flag_listen = false;
        bool flag_connect = false;
        const char *listen_addr = NULL;
        const char *connect_addr = NULL;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--tokens") == 0) {
                flag_tokens = true;
            } else if (strcmp(argv[i], "--ast") == 0) {
                flag_ast = true;
            } else if (strcmp(argv[i], "--bytecode") == 0) {
                flag_bytecode = true;
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
            return run_connect(flag_tokens, flag_ast, flag_bytecode, connect_addr);
        }

        if (flag_listen) {
            return run_listen(flag_tokens, flag_ast, flag_bytecode, listen_addr);
        }

        /* Default: start local REPL (no networking). */
        return run_local_repl(flag_tokens, flag_ast, flag_bytecode);
    } else if (strcmp(cmd, "run") == 0) {
        /* Parse: cutlet run <file> [--tokens] [--ast] */
        if (argc < 3) {
            fprintf(stderr, "Error: 'run' requires a filename\n");
            print_usage(argv[0]);
            runtime_destroy();
            return 1;
        }

        const char *filename = argv[2];
        bool flag_tokens = false;
        bool flag_ast = false;
        bool flag_bytecode = false;

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--tokens") == 0) {
                flag_tokens = true;
            } else if (strcmp(argv[i], "--ast") == 0) {
                flag_ast = true;
            } else if (strcmp(argv[i], "--bytecode") == 0) {
                flag_bytecode = true;
            } else {
                fprintf(stderr, "Unknown run option: %s\n", argv[i]);
                print_usage(argv[0]);
                runtime_destroy();
                return 1;
            }
        }

        int result = run_file(filename, flag_tokens, flag_ast, flag_bytecode);
        runtime_destroy();
        return result;
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
