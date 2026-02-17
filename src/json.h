/*
 * json.h - Minimal JSON encode/decode for REPL protocol v1
 *
 * Supports only the schema needed by the Cutlet REPL:
 * - Request:  { type:"eval",   id, expr, want_tokens, want_ast }
 * - Response: { type:"result", id, ok, value, error, tokens, ast }
 * - Output:   { type:"output", id, data }
 *
 * The server may send zero or more output frames (from say()) before
 * the terminal result frame for each eval request (nREPL-style).
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
 * Frame type discriminator for multiplexed protocol frames.
 * Used by json_frame_type() to peek at the "type" field before
 * choosing the appropriate parse function.
 */
typedef enum {
    JSON_FRAME_UNKNOWN, /* unrecognized or malformed */
    JSON_FRAME_RESULT,  /* "result" — terminal eval response */
    JSON_FRAME_OUTPUT,  /* "output" — incremental say() output */
} JsonFrameType;

/*
 * Request from client to server.
 */
typedef struct {
    unsigned long id;
    char *expr; /* owned when parsed, NULL to free; borrowed when encoding */
    bool want_tokens;
    bool want_ast;
    bool want_bytecode;
} JsonRequest;

/*
 * Response from server to client (type: "result").
 */
typedef struct {
    unsigned long id;
    bool ok;
    char *value;    /* owned, set when ok=true */
    char *error;    /* owned, set when ok=false */
    char *tokens;   /* owned, optional debug */
    char *ast;      /* owned, optional debug */
    char *bytecode; /* owned, optional debug */
} JsonResponse;

/*
 * Output frame from server to client (type: "output").
 * Sent zero or more times per eval request, before the result frame.
 * Carries incremental output from say() calls.
 */
typedef struct {
    unsigned long id;
    char *data; /* owned, the output text */
} JsonOutputFrame;

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
 * Encode an output frame as a JSON string (no framing).
 * Returns a newly allocated string. Caller must free.
 */
char *json_encode_output(const JsonOutputFrame *frame);

/*
 * Parse a JSON output frame string into a JsonOutputFrame struct.
 * Returns true on success. On failure, fields are zeroed.
 * Caller must free frame->data when done.
 */
bool json_parse_output(const char *json, size_t len, JsonOutputFrame *frame);

/*
 * Peek at the "type" field of a JSON frame without full parsing.
 * Returns the frame type, or JSON_FRAME_UNKNOWN if unrecognized.
 * This allows the client to decide which parse function to call.
 */
JsonFrameType json_frame_type(const char *json, size_t len);

/*
 * Free the owned fields in a JsonRequest.
 */
void json_request_free(JsonRequest *req);

/*
 * Free the owned fields in a JsonResponse.
 */
void json_response_free(JsonResponse *resp);

/*
 * Free the owned fields in a JsonOutputFrame.
 */
void json_output_frame_free(JsonOutputFrame *frame);

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
 *
 * If timed_out is non-NULL, it is set to true when the read failed due
 * to a socket timeout (EAGAIN/EWOULDBLOCK), or false otherwise. This
 * lets callers distinguish idle timeouts from real errors/EOF.
 */
char *json_frame_read(int fd, size_t *out_len, bool *timed_out);

#endif /* CUTLET_JSON_H */
