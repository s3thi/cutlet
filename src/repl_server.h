/*
 * repl_server.h - TCP REPL server interface
 *
 * Provides a threaded TCP server that accepts JSON-framed requests,
 * processes them through repl_eval_line(), and returns JSON responses.
 *
 * Protocol (v1, JSON frames with Content-Length header):
 *
 *   Framing:
 *     Content-Length: <N>\r\n
 *     \r\n
 *     <N bytes of JSON>
 *
 *   Request (client → server):
 *     { "type":"eval", "id":<uint>, "expr":"<input>",
 *       "want_tokens":bool, "want_ast":bool }
 *
 *   Response (server → client):
 *     { "type":"result", "id":<uint>, "ok":bool,
 *       "value":"...",    // if ok=true
 *       "error":"...",    // if ok=false
 *       "tokens":"...",   // optional debug
 *       "ast":"..."       // optional debug
 *     }
 *
 *   Debug fields are included only when the server was started with
 *   the corresponding capability AND the client requests it.
 *
 * The server uses thread-per-client. Each client connection is handled
 * in its own thread, reading frames and responding synchronously.
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
 * enable_tokens and enable_ast control whether the server is capable
 * of producing debug output. The client must also request it via
 * want_tokens/want_ast in each request.
 *
 * If port is 0, an ephemeral port is assigned by the OS.
 * The actual port can be retrieved with repl_server_port().
 *
 * Returns a server handle on success, NULL on failure.
 * On failure, if err_out is non-NULL, a static error message is stored.
 */
ReplServer *repl_server_start(const char *host, uint16_t port, bool enable_tokens, bool enable_ast,
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
