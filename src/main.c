/*
 * main.c - Cutlet CLI entry point
 *
 * Usage: cutlet repl
 *
 * The REPL reads lines from stdin, tokenizes each line, and prints
 * a formatted result to stdout. Exits on EOF.
 *
 * Output format per line:
 * - Success: OK [TYPE value] [TYPE value] ...
 * - Error: ERR line:col message
 */

#include "repl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum line length for input */
#define MAX_LINE_LEN 4096

/*
 * Print usage information.
 */
static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s repl\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  repl    Start the REPL (read-eval-print loop)\n");
}

/*
 * Run the REPL loop.
 * Reads lines from stdin, formats each, prints result to stdout.
 * Returns 0 on success (EOF), 1 on error.
 */
static int run_repl(void) {
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
        char *result = repl_format_line(line);
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

int main(int argc, char *argv[]) {
    /* Need at least one argument (the command) */
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Parse command */
    const char *cmd = argv[1];

    if (strcmp(cmd, "repl") == 0) {
        return run_repl();
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
