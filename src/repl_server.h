/*
 * repl_server.h - TCP REPL server interface
 *
 * Provides a threaded TCP server that accepts line-based requests,
 * processes them through repl_format_line(), and returns formatted
 * responses.
 *
 * Protocol (v0):
 *   Token mode:
 *     Client sends: <id> <expr>\n
 *     Server responds: -> <id> OK <formatted tokens>\n
 *                   or: -> <id> ERR <line:col message>\n
 *     Empty/whitespace expr: -> <id> OK\n
 *
 *   AST mode:
 *     Client sends: AST <id> <expr>\n
 *     Server responds: -> <id> AST [TYPE value]\n
 *                   or: -> <id> ERR <line:col message>\n
 *     Empty/whitespace expr: -> <id> AST\n
 *
 *   Mode mismatch (AST prefix on non-AST server or vice versa)
 *   produces an explicit error so both sides must agree on --ast.
 *
 * Request IDs must be digit-only strings (e.g. "1", "42").
 *
 * The server uses thread-per-client. Each client connection is handled
 * in its own thread, reading lines and responding synchronously.
 *
 * Designed to be testable: start/stop API allows in-process testing
 * without forking the binary.
 */

#ifndef CUTLET_REPL_SERVER_H
#define CUTLET_REPL_SERVER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Opaque handle for a running TCP REPL server.
 */
typedef struct ReplServer ReplServer;

/*
 * Start a TCP REPL server listening on the given host and port.
 *
 * If ast_mode is true, the server expects the "AST <id> <expr>" prefix
 * on every request and uses the parser (repl_format_line_ast) instead of
 * the tokenizer. Both client and server must agree on --ast; a mismatch
 * produces an explicit error response.
 *
 * If port is 0, an ephemeral port is assigned by the OS.
 * The actual port can be retrieved with repl_server_port().
 *
 * Returns a server handle on success, NULL on failure.
 * On failure, if err_out is non-NULL, a static error message is stored.
 */
ReplServer *repl_server_start(const char *host, uint16_t port, bool ast_mode, const char **err_out);

/*
 * Get the port the server is actually listening on.
 * Useful when started with port 0 (ephemeral).
 */
uint16_t repl_server_port(const ReplServer *server);

/*
 * Stop the server and free all resources.
 * Waits for active client threads to finish (with a timeout).
 * Safe to call with NULL.
 */
void repl_server_stop(ReplServer *server);

#endif /* CUTLET_REPL_SERVER_H */
