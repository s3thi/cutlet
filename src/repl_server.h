/*
 * repl_server.h - TCP REPL server interface
 *
 * Provides a threaded TCP server that accepts line-based requests,
 * processes them through repl_format_line(), and returns formatted
 * responses.
 *
 * Protocol (v0):
 *   Client sends: <id> <expr>\n
 *   Server responds: -> <id> OK <formatted tokens>\n
 *                 or: -> <id> ERR <line:col message>\n
 *   Empty/whitespace expr: -> <id> OK\n
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

#include <stdint.h>
#include <stdbool.h>

/*
 * Opaque handle for a running TCP REPL server.
 */
typedef struct ReplServer ReplServer;

/*
 * Start a TCP REPL server listening on the given host and port.
 *
 * If port is 0, an ephemeral port is assigned by the OS.
 * The actual port can be retrieved with repl_server_port().
 *
 * Returns a server handle on success, NULL on failure.
 * On failure, if err_out is non-NULL, a static error message is stored.
 */
ReplServer *repl_server_start(const char *host, uint16_t port,
                              const char **err_out);

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
