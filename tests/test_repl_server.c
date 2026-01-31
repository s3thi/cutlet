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
 * Send a JSON eval request and receive the response.
 * Helper that builds request, sends frame, reads frame, parses response.
 * Returns true on success, populating resp. Caller must json_response_free().
 */
static bool send_eval(int fd, unsigned long id, const char *expr, bool want_tokens, bool want_ast,
                      JsonResponse *resp) {
    JsonRequest req = {
        .id = id, .expr = (char *)expr, .want_tokens = want_tokens, .want_ast = want_ast};
    char *req_json = json_encode_request(&req);
    if (!req_json)
        return false;

    bool ok = json_frame_write(fd, req_json, strlen(req_json));
    free(req_json);
    if (!ok)
        return false;

    size_t resp_len = 0;
    char *resp_json = json_frame_read(fd, &resp_len);
    if (!resp_json)
        return false;

    ok = json_parse_response(resp_json, resp_len, resp);
    free(resp_json);
    return ok;
}

/* ============================================================
 * Server lifecycle tests
 * ============================================================ */

TEST(test_start_and_stop) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
    ASSERT_NOT_NULL(srv, "server should start");
    ASSERT(repl_server_port(srv) > 0, "port should be assigned");
    repl_server_stop(srv);
    PASS();
}

TEST(test_start_null_err_out) {
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, NULL);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    uint16_t port;
    unsigned long id;
    const char *expr;
    char value[64];
    bool ok;
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, false, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, true, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, false, true, &err);
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
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, true, &err);
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
 * Best-effort tokens on tokenizer error
 * ============================================================ */

TEST(test_tokens_on_tokenizer_error) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, true, false, &err);
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

    printf("\nBest-effort tokens:\n");
    RUN_TEST(test_tokens_on_tokenizer_error);

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("    %d FAILED\n", tests_failed);
    }

    return tests_failed > 0 ? 1 : 0;
}
