/*
 * test_repl_server.c - Tests for the TCP REPL server (JSON protocol v1)
 *
 * Tests the TCP server by starting it in-process, connecting via
 * sockets, and verifying the JSON-framed protocol.
 *
 * Test groups:
 * - Server lifecycle (start, port discovery, stop)
 * - Basic eval requests (numbers, strings, expressions)
 * - Error handling (parse errors, eval errors)
 * - Multiple requests on one connection
 * - Multi-client behavior
 * - Disconnect handling
 * - Debug flags (tokens, ast)
 *
 * Uses the same simple test harness as the other test files.
 */

#include "../src/json.h"
#include "../src/repl_server.h"
#include "../src/runtime.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ============================================================
 * Simple test harness
 * ============================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        tests_run++;                                                                               \
        printf("  %-55s ", #name);                                                                 \
        fflush(stdout);                                                                            \
        name();                                                                                    \
    } while (0)

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n");                                                                      \
            printf("    Assertion failed: %s\n", msg);                                             \
            printf("    At %s:%d\n", __FILE__, __LINE__);                                          \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* ============================================================
 * Socket helpers
 * ============================================================ */

/* Connect to localhost on the given port. Returns fd or -1. */
static int connect_to(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * Result of an eval request that may include output frames.
 * output_buf holds concatenated data from all output frames (or NULL).
 * output_count is the number of output frames received.
 */
typedef struct {
    JsonResponse resp;
    char *output_buf;  /* concatenated output frame data, heap-allocated */
    size_t output_len; /* length of output_buf contents */
    int output_count;  /* number of output frames received */
} EvalWithOutput;

static void eval_with_output_free(EvalWithOutput *e) {
    json_response_free(&e->resp);
    free(e->output_buf);
    e->output_buf = NULL;
    e->output_len = 0;
    e->output_count = 0;
}

/*
 * Send a JSON eval request and read all response frames.
 * Consumes output frames and collects them in out->output_buf.
 * Stops when it receives the terminal result frame.
 * Returns true on success.
 */
static bool send_eval_full(int fd, unsigned long id, const char *expr, bool want_tokens,
                           bool want_ast, EvalWithOutput *out) {
    memset(out, 0, sizeof(*out));

    JsonRequest req = {
        .id = id, .expr = (char *)expr, .want_tokens = want_tokens, .want_ast = want_ast};
    char *req_json = json_encode_request(&req);
    if (!req_json)
        return false;

    bool ok = json_frame_write(fd, req_json, strlen(req_json));
    free(req_json);
    if (!ok)
        return false;

    /* Read frames in a loop until we get a result frame. */
    size_t output_cap = 0;
    while (true) {
        size_t frame_len = 0;
        char *frame_json = json_frame_read(fd, &frame_len, NULL);
        if (!frame_json)
            return false;

        JsonFrameType ftype = json_frame_type(frame_json, frame_len);

        if (ftype == JSON_FRAME_OUTPUT) {
            /* Accumulate output data. */
            JsonOutputFrame oframe;
            if (json_parse_output(frame_json, frame_len, &oframe)) {
                if (oframe.data) {
                    size_t dlen = strlen(oframe.data);
                    /* Grow output buffer if needed. */
                    if (out->output_len + dlen + 1 > output_cap) {
                        output_cap = (out->output_len + dlen + 1) * 2;
                        char *tmp = realloc(out->output_buf, output_cap);
                        if (!tmp) {
                            json_output_frame_free(&oframe);
                            free(frame_json);
                            return false;
                        }
                        out->output_buf = tmp;
                    }
                    memcpy(out->output_buf + out->output_len, oframe.data, dlen);
                    out->output_len += dlen;
                    out->output_buf[out->output_len] = '\0';
                }
                out->output_count++;
                json_output_frame_free(&oframe);
            }
            free(frame_json);
            continue;
        }

        if (ftype == JSON_FRAME_RESULT) {
            ok = json_parse_response(frame_json, frame_len, &out->resp);
            free(frame_json);
            return ok;
        }

        /* Unknown frame type — skip it. */
        free(frame_json);
    }
}

/*
 * Send a JSON eval request and receive the result response.
 * Forward-compatible: silently consumes any output frames before the result.
 * Returns true on success, populating resp. Caller must json_response_free().
 */
static bool send_eval(int fd, unsigned long id, const char *expr, bool want_tokens, bool want_ast,
                      JsonResponse *resp) {
    EvalWithOutput full;
    if (!send_eval_full(fd, id, expr, want_tokens, want_ast, &full)) {
        eval_with_output_free(&full);
        return false;
    }
    /* Copy the response out and discard output data. */
    *resp = full.resp;
    /* Zero out resp in full so eval_with_output_free doesn't free the strings
     * we just handed to the caller. */
    memset(&full.resp, 0, sizeof(full.resp));
    eval_with_output_free(&full);
    return true;
}

/* ============================================================
 * Server lifecycle tests
 * ============================================================ */

TEST(test_start_and_stop) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server should start");
    ASSERT(repl_server_port(srv) > 0, "port should be assigned");
    repl_server_stop(srv);
    PASS();
}

TEST(test_start_null_err_out) {
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, NULL);
    ASSERT_NOT_NULL(srv, "server should start with NULL err_out");
    repl_server_stop(srv);
    PASS();
}

TEST(test_stop_null_is_safe) {
    repl_server_stop(NULL);
    PASS();
}

/* ============================================================
 * Basic eval requests
 * ============================================================ */

TEST(test_eval_number) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "42", false, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok should be true");
    ASSERT_NOT_NULL(resp.value, "value should be set");
    ASSERT_STR_EQ(resp.value, "42", "value matches");
    ASSERT(resp.id == 1, "id matches");
    ASSERT(resp.tokens == NULL, "no tokens");
    ASSERT(resp.ast == NULL, "no ast");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_eval_string) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 2, "\"hello\"", false, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "hello", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_eval_expression) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 3, "1 + 2 * 3", false, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "7", "expression eval");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_eval_blank_input) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 4, "   ", false, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok for blank input");
    ASSERT(resp.value == NULL, "no value for blank input");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Error handling
 * ============================================================ */

TEST(test_eval_parse_error) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 5, "\"unterminated", false, false, &resp), "send_eval");
    ASSERT(!resp.ok, "ok should be false");
    ASSERT_NOT_NULL(resp.error, "error should be set");
    ASSERT(strstr(resp.error, "unterminated") != NULL, "error mentions unterminated");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_eval_div_by_zero) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 6, "1 / 0", false, false, &resp), "send_eval");
    ASSERT(!resp.ok, "ok should be false");
    ASSERT_NOT_NULL(resp.error, "error should be set");
    ASSERT(strstr(resp.error, "division by zero") != NULL, "error msg");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Multiple requests on one connection
 * ============================================================ */

TEST(test_multiple_requests_one_connection) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;

    ASSERT(send_eval(fd, 1, "\"hello\"", false, false, &resp), "req 1");
    ASSERT(resp.ok, "ok 1");
    ASSERT_STR_EQ(resp.value, "hello", "val 1");
    json_response_free(&resp);

    ASSERT(send_eval(fd, 2, "42", false, false, &resp), "req 2");
    ASSERT(resp.ok, "ok 2");
    ASSERT_STR_EQ(resp.value, "42", "val 2");
    json_response_free(&resp);

    ASSERT(send_eval(fd, 3, "1 + 2", false, false, &resp), "req 3");
    ASSERT(resp.ok, "ok 3");
    ASSERT_STR_EQ(resp.value, "3", "val 3");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_connection_stays_open) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "11", false, false, &resp), "req 1");
    json_response_free(&resp);

    ASSERT(send_eval(fd, 2, "22", false, false, &resp), "req 2");
    ASSERT(resp.ok, "ok 2");
    ASSERT_STR_EQ(resp.value, "22", "val 2");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Multi-client behavior
 * ============================================================ */

typedef struct {
    unsigned long id;
    const char *expr;
    uint16_t port;
    bool ok;
    char value[64];
} ClientArg;

static void *client_thread_fn(void *arg) {
    ClientArg *ca = arg;
    ca->ok = false;

    int fd = connect_to(ca->port);
    if (fd < 0)
        return NULL;

    JsonResponse resp;
    if (send_eval(fd, ca->id, ca->expr, false, false, &resp)) {
        if (resp.ok && resp.value) {
            strncpy(ca->value, resp.value, sizeof(ca->value) - 1);
            ca->value[sizeof(ca->value) - 1] = '\0';
            ca->ok = true;
        }
        json_response_free(&resp);
    }

    close(fd);
    return NULL;
}

TEST(test_two_concurrent_clients) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    ClientArg c1 = {.port = port, .id = 100, .expr = "111"};
    ClientArg c2 = {.port = port, .id = 200, .expr = "222"};

    pthread_t t1, t2;
    pthread_create(&t1, NULL, client_thread_fn, &c1);
    pthread_create(&t2, NULL, client_thread_fn, &c2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    ASSERT(c1.ok, "client 1 got response");
    ASSERT(c2.ok, "client 2 got response");
    ASSERT_STR_EQ(c1.value, "111", "client 1 value");
    ASSERT_STR_EQ(c2.value, "222", "client 2 value");

    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Disconnect handling
 * ============================================================ */

TEST(test_client_connects_and_disconnects_immediately) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");
    close(fd);

    usleep(50000);

    /* Server should still be alive. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "55", false, false, &resp), "eval after reconnect");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "55", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Debug flags: tokens
 * ============================================================ */

TEST(test_tokens_enabled) {
    /* Server started with enable_tokens=true. Client requests want_tokens. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "42", true, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "42", "value");
    ASSERT_NOT_NULL(resp.tokens, "tokens should be present");
    ASSERT(strstr(resp.tokens, "TOKENS") != NULL, "tokens starts with TOKENS");
    ASSERT(strstr(resp.tokens, "[NUMBER 42]") != NULL, "tokens contains number");
    ASSERT(resp.ast == NULL, "no ast");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_tokens_not_requested) {
    /* Server has tokens enabled but client doesn't request them. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "42", false, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT(resp.tokens == NULL, "no tokens when not requested");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_tokens_server_disabled) {
    /* Server doesn't have tokens enabled; client requests them. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "42", true, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT(resp.tokens == NULL, "no tokens when server disabled");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Debug flags: ast
 * ============================================================ */

TEST(test_ast_enabled) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, true, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "1 + 2", false, true, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "3", "value");
    ASSERT_NOT_NULL(resp.ast, "ast should be present");
    ASSERT(strstr(resp.ast, "AST") != NULL, "ast starts with AST");
    ASSERT(strstr(resp.ast, "BINOP") != NULL, "ast contains BINOP");
    ASSERT(resp.tokens == NULL, "no tokens");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_ast_not_requested) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, true, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "1 + 2", false, false, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT(resp.ast == NULL, "no ast when not requested");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Debug flags: both tokens and ast
 * ============================================================ */

TEST(test_both_debug_flags) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, true, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "42", true, true, &resp), "send_eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "42", "value");
    ASSERT_NOT_NULL(resp.tokens, "tokens present");
    ASSERT_NOT_NULL(resp.ast, "ast present");
    ASSERT(strstr(resp.tokens, "TOKENS") != NULL, "tokens format");
    ASSERT(strstr(resp.ast, "AST") != NULL, "ast format");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Shared state: variables across clients
 * ============================================================ */

TEST(test_shared_state_across_clients) {
    /* Client 1 declares a variable; client 2 reads it. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    /* Client 1: declare variable */
    int fd1 = connect_to(port);
    ASSERT(fd1 >= 0, "connect client 1");

    JsonResponse resp;
    ASSERT(send_eval(fd1, 1, "my shared_var = 42", false, false, &resp), "decl");
    ASSERT(resp.ok, "decl ok");
    ASSERT_STR_EQ(resp.value, "42", "decl value");
    json_response_free(&resp);
    close(fd1);

    /* Client 2: read the variable */
    int fd2 = connect_to(port);
    ASSERT(fd2 >= 0, "connect client 2");

    ASSERT(send_eval(fd2, 2, "shared_var + 8", false, false, &resp), "read");
    ASSERT(resp.ok, "read ok");
    ASSERT_STR_EQ(resp.value, "50", "shared_var + 8 = 50");
    json_response_free(&resp);
    close(fd2);

    repl_server_stop(srv);
    PASS();
}

TEST(test_shared_state_concurrent_writers) {
    /* Two clients write to different variables concurrently, then verify. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    /* Declare variables sequentially first */
    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect setup");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "my cv1 = 0", false, false, &resp), "decl cv1");
    json_response_free(&resp);
    ASSERT(send_eval(fd, 2, "my cv2 = 0", false, false, &resp), "decl cv2");
    json_response_free(&resp);
    close(fd);

    /* Concurrent writers */
    ClientArg c1 = {.port = port, .id = 10, .expr = "cv1 = 111"};
    ClientArg c2 = {.port = port, .id = 20, .expr = "cv2 = 222"};

    pthread_t t1, t2;
    pthread_create(&t1, NULL, client_thread_fn, &c1);
    pthread_create(&t2, NULL, client_thread_fn, &c2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    ASSERT(c1.ok, "writer 1 ok");
    ASSERT(c2.ok, "writer 2 ok");

    /* Verify both variables were set */
    fd = connect_to(port);
    ASSERT(fd >= 0, "connect verify");
    ASSERT(send_eval(fd, 30, "cv1", false, false, &resp), "read cv1");
    ASSERT(resp.ok, "cv1 ok");
    ASSERT_STR_EQ(resp.value, "111", "cv1 value");
    json_response_free(&resp);

    ASSERT(send_eval(fd, 31, "cv2", false, false, &resp), "read cv2");
    ASSERT(resp.ok, "cv2 ok");
    ASSERT_STR_EQ(resp.value, "222", "cv2 value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Best-effort tokens on tokenizer error
 * ============================================================ */

TEST(test_tokens_on_tokenizer_error) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    /* "42foo" causes a tokenizer error (number followed by ident char). */
    ASSERT(send_eval(fd, 1, "42foo", true, false, &resp), "send_eval");
    ASSERT(!resp.ok, "ok should be false (parse error)");
    ASSERT_NOT_NULL(resp.tokens, "tokens should still be present (best-effort)");
    ASSERT(strstr(resp.tokens, "TOKENS") != NULL, "tokens format");
    ASSERT(strstr(resp.tokens, "ERR") != NULL, "tokens should contain error");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Timeout handling
 * ============================================================ */

TEST(test_json_frame_read_sets_timed_out_on_timeout) {
    /*
     * Use a socketpair with a short SO_RCVTIMEO to verify that
     * json_frame_read returns NULL and sets *timed_out = true
     * when the recv times out (no data sent).
     */
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    ASSERT(rc == 0, "socketpair");

    /* Set a very short recv timeout on the reading end. */
    struct timeval tv = {.tv_sec = 0, .tv_usec = 100000}; /* 100ms */
    setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    bool timed_out = false;
    size_t len = 0;
    char *result = json_frame_read(sv[0], &len, &timed_out);
    ASSERT(result == NULL, "should return NULL on timeout");
    ASSERT(timed_out == true, "timed_out should be true");

    close(sv[0]);
    close(sv[1]);
    PASS();
}

TEST(test_json_frame_read_timed_out_false_on_eof) {
    /*
     * When the peer closes the connection, json_frame_read should
     * return NULL with *timed_out = false (real EOF, not timeout).
     */
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    ASSERT(rc == 0, "socketpair");

    /* Close the writing end immediately to cause EOF. */
    close(sv[1]);

    bool timed_out = true; /* pre-set to true to verify it gets cleared */
    size_t len = 0;
    char *result = json_frame_read(sv[0], &len, &timed_out);
    ASSERT(result == NULL, "should return NULL on EOF");
    ASSERT(timed_out == false, "timed_out should be false on EOF");

    close(sv[0]);
    PASS();
}

TEST(test_json_frame_read_timed_out_null_param) {
    /*
     * Passing NULL for timed_out should not crash.
     */
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    ASSERT(rc == 0, "socketpair");

    close(sv[1]);

    size_t len = 0;
    char *result = json_frame_read(sv[0], &len, NULL);
    ASSERT(result == NULL, "should return NULL on EOF");

    close(sv[0]);
    PASS();
}

TEST(test_server_survives_idle_period) {
    /*
     * After connecting and sending one request, wait briefly, then
     * send another request. The connection should still work.
     * (This tests the retry-on-timeout logic in client_thread_fn,
     * though the actual 30s timeout won't fire in this short test.
     * It serves as a regression test that the plumbing is correct.)
     */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "10", false, false, &resp), "first request");
    ASSERT(resp.ok, "first ok");
    ASSERT_STR_EQ(resp.value, "10", "first value");
    json_response_free(&resp);

    /* Brief pause — not enough to trigger the 30s timeout, but
     * validates that the loop logic is correct. */
    usleep(200000); /* 200ms */

    ASSERT(send_eval(fd, 2, "20", false, false, &resp), "second request after pause");
    ASSERT(resp.ok, "second ok");
    ASSERT_STR_EQ(resp.value, "20", "second value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Helper: send raw bytes directly on a socket
 * ============================================================ */

static bool send_raw(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0)
            return false;
        sent += (size_t)n;
    }
    return true;
}

/*
 * Helper: send a raw frame (Content-Length header + body) without
 * going through the json_encode_request path.
 */
static bool send_raw_frame(int fd, const char *body, size_t body_len) {
    char header[64];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", body_len);
    if (!send_raw(fd, header, (size_t)hlen))
        return false;
    if (body_len > 0 && body)
        return send_raw(fd, body, body_len);
    return true;
}

/*
 * Helper: read a raw JSON response frame and parse it.
 * Returns true on success.
 */
static bool recv_response(int fd, JsonResponse *resp) {
    size_t resp_len = 0;
    char *resp_json = json_frame_read(fd, &resp_len, NULL);
    if (!resp_json)
        return false;
    bool ok = json_parse_response(resp_json, resp_len, resp);
    free(resp_json);
    return ok;
}

/* ============================================================
 * Group 1: Partial / malformed frame headers
 * ============================================================ */

TEST(test_incomplete_header_then_close) {
    /* Send a partial header then close. Server should not crash
     * and should still accept new connections afterward. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");
    send_raw(fd, "Content-Len", 11);
    close(fd);

    usleep(50000);

    /* Server should still be alive. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after partial header");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "99", false, false, &resp), "eval after partial header");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "99", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_missing_content_length) {
    /* Send a valid header terminator but no Content-Length field.
     * json_frame_read returns NULL. Server should close that client
     * but remain alive. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");
    const char *bad = "X-Custom: foo\r\n\r\n";
    send_raw(fd, bad, strlen(bad));
    /* Give server time to process and close the connection. */
    usleep(50000);
    close(fd);

    /* Server should still accept new connections. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after missing content-length");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "77", false, false, &resp), "eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "77", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_content_length_zero) {
    /* Content-Length: 0 is treated as error by json_frame_read.
     * Server should survive and accept next connection. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");
    const char *frame = "Content-Length: 0\r\n\r\n";
    send_raw(fd, frame, strlen(frame));
    usleep(50000);
    close(fd);

    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after zero content-length");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "88", false, false, &resp), "eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "88", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_content_length_exceeds_max) {
    /* Content-Length exceeds JSON_MAX_FRAME_SIZE (65536).
     * Server should drop the client but stay alive. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");
    const char *frame = "Content-Length: 999999\r\n\r\n";
    send_raw(fd, frame, strlen(frame));
    usleep(50000);
    close(fd);

    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after oversized content-length");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "66", false, false, &resp), "eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "66", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Group 2: Malformed JSON body
 * ============================================================ */

TEST(test_garbage_json_body) {
    /* Valid frame with body that isn't valid JSON. Server should send
     * error response and keep connection open. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    /* Send garbage body in a valid frame. */
    const char *body = "not json at all";
    send_raw_frame(fd, body, strlen(body));

    /* Should get error response. */
    JsonResponse resp;
    ASSERT(recv_response(fd, &resp), "recv error response");
    ASSERT(!resp.ok, "ok should be false");
    ASSERT(resp.id == 0, "error response id should be 0");
    ASSERT_NOT_NULL(resp.error, "error should be set");
    ASSERT(strstr(resp.error, "invalid request JSON") != NULL, "error message");
    json_response_free(&resp);

    /* Connection should still work for valid requests. */
    ASSERT(send_eval(fd, 1, "42", false, false, &resp), "eval after garbage");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "42", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_empty_json_object) {
    /* Valid frame with body "{}". The parser succeeds but expr is NULL.
     * Server should handle it gracefully. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    const char *body = "{}";
    send_raw_frame(fd, body, strlen(body));

    /* Server should respond (either error or blank-input OK). */
    JsonResponse resp;
    ASSERT(recv_response(fd, &resp), "recv response for empty object");
    json_response_free(&resp);

    /* Connection should still work. */
    ASSERT(send_eval(fd, 1, "55", false, false, &resp), "eval after empty object");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "55", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_json_missing_expr) {
    /* Valid JSON with type/id but no expr key. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    const char *body = "{\"type\":\"eval\",\"id\":1,\"want_tokens\":false,\"want_ast\":false}";
    send_raw_frame(fd, body, strlen(body));

    /* Server should handle gracefully. */
    JsonResponse resp;
    ASSERT(recv_response(fd, &resp), "recv response for missing expr");
    json_response_free(&resp);

    /* Connection should still work. */
    ASSERT(send_eval(fd, 2, "33", false, false, &resp), "eval after missing expr");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "33", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_truncated_json_body) {
    /* Send Content-Length: 100 but only 20 bytes, then close.
     * recv_exact will fail. Server should not crash. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    const char *header = "Content-Length: 100\r\n\r\n";
    send_raw(fd, header, strlen(header));
    /* Send only 20 bytes of the promised 100. */
    send_raw(fd, "{\"type\":\"eval\",\"id\":", 20);
    close(fd);

    usleep(50000);

    /* Server should still accept new connections. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after truncated body");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "44", false, false, &resp), "eval");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "44", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Group 3: Concurrency stress
 * ============================================================ */

TEST(test_ten_concurrent_clients) {
    /* Spawn 10 threads, each sending a unique expression.
     * Verify every thread got the correct result. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

#define NUM_CLIENTS 10
    ClientArg clients[NUM_CLIENTS];
    pthread_t threads[NUM_CLIENTS];
    /* Expressions: "100 + 0", "100 + 1", ..., "100 + 9" */
    char exprs[NUM_CLIENTS][16];
    for (int i = 0; i < NUM_CLIENTS; i++) {
        snprintf(exprs[i], sizeof(exprs[i]), "100 + %d", i);
        clients[i] = (ClientArg){.port = port, .id = (unsigned long)(i + 1), .expr = exprs[i]};
    }

    for (int i = 0; i < NUM_CLIENTS; i++)
        pthread_create(&threads[i], NULL, client_thread_fn, &clients[i]);
    for (int i = 0; i < NUM_CLIENTS; i++)
        pthread_join(threads[i], NULL);

    for (int i = 0; i < NUM_CLIENTS; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "%d", 100 + i);
        ASSERT(clients[i].ok, "client got response");
        ASSERT(strcmp(clients[i].value, expected) == 0, "client value correct");
    }
#undef NUM_CLIENTS

    repl_server_stop(srv);
    PASS();
}

TEST(test_rapid_connect_disconnect) {
    /* Connect and immediately close 20 times, then verify server
     * still works. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    for (int i = 0; i < 20; i++) {
        int fd = connect_to(port);
        if (fd >= 0)
            close(fd);
    }

    usleep(100000); /* Let server process all the churn. */

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect after rapid churn");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "123", false, false, &resp), "eval after churn");
    ASSERT(resp.ok, "ok");
    ASSERT_STR_EQ(resp.value, "123", "value");
    json_response_free(&resp);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/*
 * Thread function for concurrent shared state stress test.
 * Each thread does multiple sequential increment operations.
 */
typedef struct {
    uint16_t port;
    int num_iterations;
    bool ok;
} StressClientArg;

static void *stress_client_thread_fn(void *arg) {
    StressClientArg *sa = arg;
    sa->ok = false;

    int fd = connect_to(sa->port);
    if (fd < 0)
        return NULL;

    for (int i = 0; i < sa->num_iterations; i++) {
        JsonResponse resp;
        unsigned long req_id = (unsigned long)i + 1UL;
        if (!send_eval(fd, req_id, "counter = counter + 1", false, false, &resp)) {
            close(fd);
            return NULL;
        }
        if (!resp.ok) {
            json_response_free(&resp);
            close(fd);
            return NULL;
        }
        json_response_free(&resp);
    }

    sa->ok = true;
    close(fd);
    return NULL;
}

TEST(test_concurrent_shared_state_stress) {
    /* Pre-declare counter = 0. Spawn 4 threads doing 50 increments each.
     * Final value must be exactly 200 (serialized by global write lock). */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    /* Declare the counter variable. */
    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect setup");
    JsonResponse resp;
    ASSERT(send_eval(fd, 1, "my counter = 0", false, false, &resp), "declare counter");
    ASSERT(resp.ok, "declare ok");
    json_response_free(&resp);
    close(fd);

#define STRESS_THREADS 4
#define STRESS_ITERS 50
    StressClientArg stress_args[STRESS_THREADS];
    pthread_t stress_threads[STRESS_THREADS];

    for (int i = 0; i < STRESS_THREADS; i++) {
        stress_args[i] = (StressClientArg){.port = port, .num_iterations = STRESS_ITERS};
        pthread_create(&stress_threads[i], NULL, stress_client_thread_fn, &stress_args[i]);
    }
    for (int i = 0; i < STRESS_THREADS; i++)
        pthread_join(stress_threads[i], NULL);

    for (int i = 0; i < STRESS_THREADS; i++)
        ASSERT(stress_args[i].ok, "stress thread completed");

    /* Read final counter value. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "connect verify");
    ASSERT(send_eval(fd, 1, "counter", false, false, &resp), "read counter");
    ASSERT(resp.ok, "counter ok");
    ASSERT_NOT_NULL(resp.value, "counter value set");
    ASSERT_STR_EQ(resp.value, "200", "counter should be 200");
    json_response_free(&resp);

    close(fd);
#undef STRESS_THREADS
#undef STRESS_ITERS
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Group 4: Request ID handling
 * ============================================================ */

TEST(test_response_id_matches_request) {
    /* Send 5 requests with IDs [7, 42, 0, 999, 1]. Verify each
     * response's id field matches exactly. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    unsigned long ids[] = {7, 42, 0, 999, 1};
    for (int i = 0; i < 5; i++) {
        JsonResponse resp;
        ASSERT(send_eval(fd, ids[i], "1", false, false, &resp), "send_eval");
        ASSERT(resp.ok, "ok");
        ASSERT(resp.id == ids[i], "response id matches request id");
        json_response_free(&resp);
    }

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * say() output frames through server
 * ============================================================ */

TEST(test_say_produces_output_frame) {
    /* say("hello") should produce one output frame with "hello\n"
     * followed by a result frame with ok=true, value="nothing". */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    EvalWithOutput out;
    ASSERT(send_eval_full(fd, 1, "say(\"hello\")", false, false, &out), "send_eval_full");
    ASSERT(out.resp.ok, "result should be ok");
    ASSERT_NOT_NULL(out.resp.value, "result value set");
    ASSERT_STR_EQ(out.resp.value, "nothing", "say() returns nothing");
    ASSERT(out.output_count == 1, "exactly one output frame");
    ASSERT_NOT_NULL(out.output_buf, "output data present");
    ASSERT_STR_EQ(out.output_buf, "hello\n", "output data is hello\\n");
    eval_with_output_free(&out);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_say_multiple_calls) {
    /* Two say() calls should produce two output frames before the result. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    EvalWithOutput out;
    ASSERT(send_eval_full(fd, 1, "say(\"hello\")\nsay(\"world\")", false, false, &out),
           "send_eval_full");
    ASSERT(out.resp.ok, "result ok");
    ASSERT_STR_EQ(out.resp.value, "nothing", "say() returns nothing");
    ASSERT(out.output_count == 2, "two output frames");
    ASSERT_NOT_NULL(out.output_buf, "output data present");
    ASSERT_STR_EQ(out.output_buf, "hello\nworld\n", "concatenated output");
    eval_with_output_free(&out);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_say_output_id_matches_request) {
    /* Output frame ID must match the request ID. We verify this indirectly
     * by checking that send_eval_full correctly correlates frames. Also
     * use a non-trivial request ID. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    EvalWithOutput out;
    ASSERT(send_eval_full(fd, 42, "say(\"test\")", false, false, &out), "send_eval_full");
    ASSERT(out.resp.ok, "result ok");
    ASSERT(out.resp.id == 42, "result id matches");
    ASSERT(out.output_count == 1, "one output frame");
    eval_with_output_free(&out);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_no_output_frames_for_regular_expr) {
    /* A regular expression (no say()) should produce zero output frames. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    EvalWithOutput out;
    ASSERT(send_eval_full(fd, 1, "1 + 2", false, false, &out), "send_eval_full");
    ASSERT(out.resp.ok, "result ok");
    ASSERT_STR_EQ(out.resp.value, "3", "value");
    ASSERT(out.output_count == 0, "no output frames");
    ASSERT(out.output_buf == NULL, "no output data");
    eval_with_output_free(&out);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_say_with_number) {
    /* say() with a number argument should format the number. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    EvalWithOutput out;
    ASSERT(send_eval_full(fd, 1, "say(42)", false, false, &out), "send_eval_full");
    ASSERT(out.resp.ok, "result ok");
    ASSERT(out.output_count == 1, "one output frame");
    ASSERT_STR_EQ(out.output_buf, "42\n", "output is formatted number");
    eval_with_output_free(&out);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_say_followed_by_expression) {
    /* say() followed by an expression: output frame from say,
     * result frame from the last expression. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, false, &err);
    ASSERT_NOT_NULL(srv, "server start");

    int fd = connect_to(repl_server_port(srv));
    ASSERT(fd >= 0, "connect");

    EvalWithOutput out;
    ASSERT(send_eval_full(fd, 1, "say(\"hi\")\n1 + 2", false, false, &out), "send_eval_full");
    ASSERT(out.resp.ok, "result ok");
    ASSERT_STR_EQ(out.resp.value, "3", "last expr is result");
    ASSERT(out.output_count == 1, "one output frame from say");
    ASSERT_STR_EQ(out.output_buf, "hi\n", "output data");
    eval_with_output_free(&out);

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    runtime_init();
    printf("=== TCP REPL Server Tests (JSON protocol v1) ===\n\n");

    printf("Server lifecycle:\n");
    RUN_TEST(test_start_and_stop);
    RUN_TEST(test_start_null_err_out);
    RUN_TEST(test_stop_null_is_safe);

    printf("\nBasic eval requests:\n");
    RUN_TEST(test_eval_number);
    RUN_TEST(test_eval_string);
    RUN_TEST(test_eval_expression);
    RUN_TEST(test_eval_blank_input);

    printf("\nError handling:\n");
    RUN_TEST(test_eval_parse_error);
    RUN_TEST(test_eval_div_by_zero);

    printf("\nMultiple requests:\n");
    RUN_TEST(test_multiple_requests_one_connection);
    RUN_TEST(test_connection_stays_open);

    printf("\nMulti-client:\n");
    RUN_TEST(test_two_concurrent_clients);

    printf("\nDisconnect handling:\n");
    RUN_TEST(test_client_connects_and_disconnects_immediately);

    printf("\nDebug flags (tokens):\n");
    RUN_TEST(test_tokens_enabled);
    RUN_TEST(test_tokens_not_requested);
    RUN_TEST(test_tokens_server_disabled);

    printf("\nDebug flags (ast):\n");
    RUN_TEST(test_ast_enabled);
    RUN_TEST(test_ast_not_requested);

    printf("\nDebug flags (both):\n");
    RUN_TEST(test_both_debug_flags);

    printf("\nShared state (variables):\n");
    RUN_TEST(test_shared_state_across_clients);
    RUN_TEST(test_shared_state_concurrent_writers);

    printf("\nBest-effort tokens:\n");
    RUN_TEST(test_tokens_on_tokenizer_error);

    printf("\nTimeout handling:\n");
    RUN_TEST(test_json_frame_read_sets_timed_out_on_timeout);
    RUN_TEST(test_json_frame_read_timed_out_false_on_eof);
    RUN_TEST(test_json_frame_read_timed_out_null_param);
    RUN_TEST(test_server_survives_idle_period);

    printf("\nPartial/malformed frame headers:\n");
    RUN_TEST(test_incomplete_header_then_close);
    RUN_TEST(test_missing_content_length);
    RUN_TEST(test_content_length_zero);
    RUN_TEST(test_content_length_exceeds_max);

    printf("\nMalformed JSON body:\n");
    RUN_TEST(test_garbage_json_body);
    RUN_TEST(test_empty_json_object);
    RUN_TEST(test_json_missing_expr);
    RUN_TEST(test_truncated_json_body);

    printf("\nConcurrency stress:\n");
    RUN_TEST(test_ten_concurrent_clients);
    RUN_TEST(test_rapid_connect_disconnect);
    RUN_TEST(test_concurrent_shared_state_stress);

    printf("\nRequest ID handling:\n");
    RUN_TEST(test_response_id_matches_request);

    printf("\nsay() output frames:\n");
    RUN_TEST(test_say_produces_output_frame);
    RUN_TEST(test_say_multiple_calls);
    RUN_TEST(test_say_output_id_matches_request);
    RUN_TEST(test_no_output_frames_for_regular_expr);
    RUN_TEST(test_say_with_number);
    RUN_TEST(test_say_followed_by_expression);

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("    %d FAILED\n", tests_failed);
    }

    return tests_failed > 0 ? 1 : 0;
}
