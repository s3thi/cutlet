/*
 * json.c - Minimal JSON encode/decode for REPL protocol v1
 *
 * Hand-written JSON for the small, fixed schema used by the REPL.
 * No external dependencies.
 *
 * Encoding: builds JSON strings with snprintf.
 * Decoding: simple key-value scanner (no recursive descent needed
 * since the schema is flat).
 *
 * Frame I/O: Content-Length header read/write over file descriptors.
 */

#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ============================================================
 * JSON string escaping
 * ============================================================ */

/*
 * Compute the length needed for a JSON-escaped version of src.
 * Escapes: \, ", \n, \r, \t, and control chars as \uXXXX.
 */
static size_t json_escaped_len(const char *src) {
    size_t len = 0;
    for (const char *p = src; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t')
            len += 2;
        else if (c < 0x20)
            len += 6; /* \uXXXX */
        else
            len += 1;
    }
    return len;
}

/*
 * Write JSON-escaped version of src into dst.
 * dst must have room for json_escaped_len(src) + 1 bytes.
 * Returns pointer to the null terminator.
 */
static char *json_escape_into(char *dst, const char *src) {
    for (const char *p = src; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            *dst++ = '\\';
            *dst++ = '"';
        } else if (c == '\\') {
            *dst++ = '\\';
            *dst++ = '\\';
        } else if (c == '\n') {
            *dst++ = '\\';
            *dst++ = 'n';
        } else if (c == '\r') {
            *dst++ = '\\';
            *dst++ = 'r';
        } else if (c == '\t') {
            *dst++ = '\\';
            *dst++ = 't';
        } else if (c < 0x20) {
            dst += sprintf(dst, "\\u%04x", c);
        } else {
            *dst++ = (char)c;
        }
    }
    *dst = '\0';
    return dst;
}

/* ============================================================
 * Encoding
 * ============================================================ */

/*
 * Helper: append a JSON string field "key":"escaped_value"
 * to the buffer at *pos. Advances *pos.
 */
static void append_str_field(char *buf, size_t *pos, const char *key, const char *value,
                             bool comma_before) {
    if (comma_before) {
        buf[(*pos)++] = ',';
    }
    *pos += (size_t)sprintf(buf + *pos, "\"%s\":\"", key);
    char *end = json_escape_into(buf + *pos, value);
    *pos = (size_t)(end - buf);
    buf[(*pos)++] = '"';
}

char *json_encode_request(const JsonRequest *req) {
    /* Estimate size: fixed overhead + expr string. */
    size_t expr_esc = req->expr ? json_escaped_len(req->expr) : 0;
    size_t cap = 128 + expr_esc;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;

    size_t pos = 0;
    pos += (size_t)sprintf(buf + pos, "{\"type\":\"eval\",\"id\":%lu", req->id);

    if (req->expr) {
        append_str_field(buf, &pos, "expr", req->expr, true);
    } else {
        pos += (size_t)sprintf(buf + pos, ",\"expr\":\"\"");
    }

    pos += (size_t)sprintf(buf + pos, ",\"want_tokens\":%s", req->want_tokens ? "true" : "false");
    pos += (size_t)sprintf(buf + pos, ",\"want_ast\":%s", req->want_ast ? "true" : "false");
    buf[pos++] = '}';
    buf[pos] = '\0';
    return buf;
}

char *json_encode_response(const JsonResponse *resp) {
    /* Estimate size. */
    size_t val_esc = resp->value ? json_escaped_len(resp->value) : 0;
    size_t err_esc = resp->error ? json_escaped_len(resp->error) : 0;
    size_t tok_esc = resp->tokens ? json_escaped_len(resp->tokens) : 0;
    size_t ast_esc = resp->ast ? json_escaped_len(resp->ast) : 0;
    size_t cap = 256 + val_esc + err_esc + tok_esc + ast_esc;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;

    size_t pos = 0;
    pos += (size_t)sprintf(buf + pos, "{\"type\":\"result\",\"id\":%lu,\"ok\":%s", resp->id,
                           resp->ok ? "true" : "false");

    if (resp->ok && resp->value) {
        append_str_field(buf, &pos, "value", resp->value, true);
    }
    if (!resp->ok && resp->error) {
        append_str_field(buf, &pos, "error", resp->error, true);
    }
    if (resp->tokens) {
        append_str_field(buf, &pos, "tokens", resp->tokens, true);
    }
    if (resp->ast) {
        append_str_field(buf, &pos, "ast", resp->ast, true);
    }

    buf[pos++] = '}';
    buf[pos] = '\0';
    return buf;
}

/* ============================================================
 * Decoding helpers
 * ============================================================ */

/*
 * Skip whitespace in json starting at *pos.
 */
static void skip_ws(const char *json, size_t len, size_t *pos) {
    while (*pos < len &&
           (json[*pos] == ' ' || json[*pos] == '\t' || json[*pos] == '\n' || json[*pos] == '\r')) {
        (*pos)++;
    }
}

/*
 * Parse a JSON string starting at json[*pos] (which must be '"').
 * Returns a newly allocated, unescaped string. Advances *pos past
 * the closing quote. Returns NULL on error.
 */
static char *parse_json_string(const char *json, size_t len, size_t *pos) {
    if (*pos >= len || json[*pos] != '"')
        return NULL;
    (*pos)++; /* skip opening quote */

    /* First pass: compute unescaped length. */
    size_t start = *pos;
    size_t ulen = 0;
    size_t i = start;
    while (i < len && json[i] != '"') {
        if (json[i] == '\\') {
            i++;
            if (i >= len)
                return NULL;
            if (json[i] == 'u') {
                i += 4; /* skip \uXXXX hex digits */
                ulen++;
            } else {
                i++;
                ulen++;
            }
        } else {
            i++;
            ulen++;
        }
    }
    if (i >= len)
        return NULL;

    /* Second pass: build unescaped string. */
    char *out = malloc(ulen + 1);
    if (!out)
        return NULL;

    size_t o = 0;
    i = start;
    while (json[i] != '"') {
        if (json[i] == '\\') {
            i++;
            switch (json[i]) {
            case '"':
                out[o++] = '"';
                break;
            case '\\':
                out[o++] = '\\';
                break;
            case 'n':
                out[o++] = '\n';
                break;
            case 'r':
                out[o++] = '\r';
                break;
            case 't':
                out[o++] = '\t';
                break;
            case 'u': {
                /* Parse 4 hex digits, produce single byte (ASCII subset). */
                char hex[5] = {json[i + 1], json[i + 2], json[i + 3], json[i + 4], '\0'};
                long val = strtol(hex, NULL, 16);
                out[o++] = (char)(val & 0x7f);
                i += 4;
                break;
            }
            default:
                out[o++] = json[i];
                break;
            }
            i++;
        } else {
            out[o++] = json[i++];
        }
    }
    if (o > ulen) {
        free(out);
        return NULL;
    }
    out[o] = '\0';
    *pos = i + 1; /* skip closing quote */
    return out;
}

/*
 * Try to match a literal at json[*pos]. If found, advance and return true.
 */
static bool match_literal(const char *json, size_t len, size_t *pos, const char *lit) {
    size_t llen = strlen(lit);
    if (*pos + llen > len)
        return false;
    if (memcmp(json + *pos, lit, llen) == 0) {
        *pos += llen;
        return true;
    }
    return false;
}

/*
 * Parse an unsigned long number at json[*pos].
 */
static bool parse_ulong(const char *json, size_t len, size_t *pos, unsigned long *out) {
    if (*pos >= len || !isdigit((unsigned char)json[*pos]))
        return false;
    unsigned long val = 0;
    while (*pos < len && isdigit((unsigned char)json[*pos])) {
        val = val * 10 + (unsigned long)(json[*pos] - '0');
        (*pos)++;
    }
    *out = val;
    return true;
}

/* ============================================================
 * Request parsing
 * ============================================================ */

bool json_parse_request(const char *json, size_t len, JsonRequest *req) {
    memset(req, 0, sizeof(*req));

    size_t pos = 0;
    skip_ws(json, len, &pos);
    if (pos >= len || json[pos] != '{')
        return false;
    pos++;

    while (pos < len && json[pos] != '}') {
        skip_ws(json, len, &pos);
        if (pos >= len)
            return false;
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        if (json[pos] == '}')
            break;

        /* Parse key. */
        char *key = parse_json_string(json, len, &pos);
        if (!key)
            return false;

        skip_ws(json, len, &pos);
        if (pos >= len || json[pos] != ':') {
            free(key);
            return false;
        }
        pos++;
        skip_ws(json, len, &pos);

        /* Parse value based on key. */
        if (strcmp(key, "type") == 0) {
            char *val = parse_json_string(json, len, &pos);
            free(val); /* We don't need it, always "eval". */
        } else if (strcmp(key, "id") == 0) {
            if (!parse_ulong(json, len, &pos, &req->id)) {
                free(key);
                return false;
            }
        } else if (strcmp(key, "expr") == 0) {
            req->expr = parse_json_string(json, len, &pos);
        } else if (strcmp(key, "want_tokens") == 0) {
            if (match_literal(json, len, &pos, "true"))
                req->want_tokens = true;
            else if (match_literal(json, len, &pos, "false"))
                req->want_tokens = false;
            else {
                free(key);
                return false;
            }
        } else if (strcmp(key, "want_ast") == 0) {
            if (match_literal(json, len, &pos, "true"))
                req->want_ast = true;
            else if (match_literal(json, len, &pos, "false"))
                req->want_ast = false;
            else {
                free(key);
                return false;
            }
        } else {
            /* Unknown key: skip value. */
            if (json[pos] == '"') {
                char *skip = parse_json_string(json, len, &pos);
                free(skip);
            } else if (isdigit((unsigned char)json[pos])) {
                unsigned long dummy;
                parse_ulong(json, len, &pos, &dummy);
            } else if (match_literal(json, len, &pos, "true") ||
                       match_literal(json, len, &pos, "false") ||
                       match_literal(json, len, &pos, "null")) {
                /* consumed */
            }
        }
        free(key);
    }

    return true;
}

/* ============================================================
 * Response parsing
 * ============================================================ */

bool json_parse_response(const char *json, size_t len, JsonResponse *resp) {
    memset(resp, 0, sizeof(*resp));

    size_t pos = 0;
    skip_ws(json, len, &pos);
    if (pos >= len || json[pos] != '{')
        return false;
    pos++;

    while (pos < len && json[pos] != '}') {
        skip_ws(json, len, &pos);
        if (pos >= len)
            return false;
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        if (json[pos] == '}')
            break;

        char *key = parse_json_string(json, len, &pos);
        if (!key)
            return false;

        skip_ws(json, len, &pos);
        if (pos >= len || json[pos] != ':') {
            free(key);
            return false;
        }
        pos++;
        skip_ws(json, len, &pos);

        if (strcmp(key, "type") == 0) {
            char *val = parse_json_string(json, len, &pos);
            free(val);
        } else if (strcmp(key, "id") == 0) {
            if (!parse_ulong(json, len, &pos, &resp->id)) {
                free(key);
                return false;
            }
        } else if (strcmp(key, "ok") == 0) {
            if (match_literal(json, len, &pos, "true"))
                resp->ok = true;
            else if (match_literal(json, len, &pos, "false"))
                resp->ok = false;
            else {
                free(key);
                return false;
            }
        } else if (strcmp(key, "value") == 0) {
            resp->value = parse_json_string(json, len, &pos);
        } else if (strcmp(key, "error") == 0) {
            resp->error = parse_json_string(json, len, &pos);
        } else if (strcmp(key, "tokens") == 0) {
            resp->tokens = parse_json_string(json, len, &pos);
        } else if (strcmp(key, "ast") == 0) {
            resp->ast = parse_json_string(json, len, &pos);
        } else {
            /* Unknown key: skip value. */
            if (json[pos] == '"') {
                char *skip = parse_json_string(json, len, &pos);
                free(skip);
            } else if (isdigit((unsigned char)json[pos])) {
                unsigned long dummy;
                parse_ulong(json, len, &pos, &dummy);
            } else if (match_literal(json, len, &pos, "true") ||
                       match_literal(json, len, &pos, "false") ||
                       match_literal(json, len, &pos, "null")) {
                /* consumed */
            }
        }
        free(key);
    }

    return true;
}

/* ============================================================
 * Free helpers
 * ============================================================ */

void json_request_free(JsonRequest *req) {
    if (!req)
        return;
    free(req->expr);
    req->expr = NULL;
}

void json_response_free(JsonResponse *resp) {
    if (!resp)
        return;
    free(resp->value);
    free(resp->error);
    free(resp->tokens);
    free(resp->ast);
    resp->value = NULL;
    resp->error = NULL;
    resp->tokens = NULL;
    resp->ast = NULL;
}

/* ============================================================
 * Frame I/O
 * ============================================================ */

/*
 * Send all bytes on a file descriptor. Returns true on success.
 */
static bool send_all_bytes(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

bool json_frame_write(int fd, const char *json, size_t len) {
    /* Build header: "Content-Length: <N>\r\n\r\n" */
    char header[64];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    if (hlen < 0)
        return false;

    if (!send_all_bytes(fd, header, (size_t)hlen))
        return false;
    if (!send_all_bytes(fd, json, len))
        return false;
    return true;
}

/*
 * Read exactly n bytes from fd into buf. Returns true on success.
 */
static bool recv_exact(int fd, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0)
            return false;
        got += (size_t)r;
    }
    return true;
}

char *json_frame_read(int fd, size_t *out_len) {
    /*
     * Read headers byte-by-byte until we see "\r\n\r\n".
     * Parse Content-Length from the header.
     */
    char hdr[256];
    size_t hpos = 0;

    while (hpos < sizeof(hdr) - 1) {
        ssize_t n = recv(fd, hdr + hpos, 1, 0);
        if (n <= 0)
            return NULL;
        hpos++;

        /* Check for end of headers: \r\n\r\n */
        if (hpos >= 4 && hdr[hpos - 4] == '\r' && hdr[hpos - 3] == '\n' && hdr[hpos - 2] == '\r' &&
            hdr[hpos - 1] == '\n') {
            break;
        }
    }
    hdr[hpos] = '\0';

    /* Find Content-Length. */
    const char *cl = strstr(hdr, "Content-Length:");
    if (!cl)
        cl = strstr(hdr, "content-length:");
    if (!cl)
        return NULL;

    cl += strlen("Content-Length:");
    while (*cl == ' ')
        cl++;
    size_t content_len = (size_t)strtoul(cl, NULL, 10);
    if (content_len == 0 || content_len > JSON_MAX_FRAME_SIZE)
        return NULL;

    /* Read the JSON body. */
    char *body = malloc(content_len + 1);
    if (!body)
        return NULL;

    if (!recv_exact(fd, body, content_len)) {
        free(body);
        return NULL;
    }
    body[content_len] = '\0';

    if (out_len)
        *out_len = content_len;
    return body;
}
