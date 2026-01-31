/*
 * json.h - Minimal JSON encode/decode for REPL protocol v1
 *
 * Supports only the schema needed by the Cutlet REPL:
 * - Request: { type, id, expr, want_tokens, want_ast }
 * - Response: { type, id, ok, value, error, tokens, ast }
 *
 * Frame format (LSP-style):
 *   Content-Length: <N>\r\n
 *   \r\n
 *   <N bytes of JSON>
 *
 * Maximum frame size: 64KB (JSON_MAX_FRAME_SIZE).
 */

#ifndef CUTLET_JSON_H
#define CUTLET_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JSON_MAX_FRAME_SIZE 65536

/*
 * Request from client to server.
 */
typedef struct {
    unsigned long id;
    char *expr;        /* owned, caller must free */
    bool want_tokens;
    bool want_ast;
} JsonRequest;

/*
 * Response from server to client.
 */
typedef struct {
    unsigned long id;
    bool ok;
    char *value;   /* owned, set when ok=true */
    char *error;   /* owned, set when ok=false */
    char *tokens;  /* owned, optional debug */
    char *ast;     /* owned, optional debug */
} JsonResponse;

/*
 * Encode a request as a JSON string (no framing).
 * Returns a newly allocated string. Caller must free.
 */
char *json_encode_request(const JsonRequest *req);

/*
 * Encode a response as a JSON string (no framing).
 * Returns a newly allocated string. Caller must free.
 */
char *json_encode_response(const JsonResponse *resp);

/*
 * Parse a JSON request string into a JsonRequest struct.
 * Returns true on success. On failure, fields are zeroed.
 * Caller must free req->expr when done.
 */
bool json_parse_request(const char *json, size_t len, JsonRequest *req);

/*
 * Parse a JSON response string into a JsonResponse struct.
 * Returns true on success. On failure, fields are zeroed.
 * Caller must free value/error/tokens/ast when done.
 */
bool json_parse_response(const char *json, size_t len, JsonResponse *resp);

/*
 * Free the owned fields in a JsonRequest.
 */
void json_request_free(JsonRequest *req);

/*
 * Free the owned fields in a JsonResponse.
 */
void json_response_free(JsonResponse *resp);

/*
 * Write a framed JSON message to a file descriptor.
 * Prepends "Content-Length: <N>\r\n\r\n" header.
 * Returns true on success.
 */
bool json_frame_write(int fd, const char *json, size_t len);

/*
 * Read a framed JSON message from a file descriptor.
 * Returns a newly allocated string with the JSON body, or NULL on error.
 * Sets *out_len to the length of the JSON body.
 */
char *json_frame_read(int fd, size_t *out_len);

#endif /* CUTLET_JSON_H */
