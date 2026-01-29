/*
 * test_repl_server.c - Tests for the TCP REPL server
 *
 * Tests the TCP server by starting it in-process, connecting via
 * sockets, and verifying the line-based protocol.
 *
 * Test groups:
 * - Server lifecycle (start, port discovery, stop)
 * - Single client / basic protocol
 * - Single connection, multiple requests
 * - Multi-client behavior
 * - Disconnect handling
 * - Error cases (bad IDs, oversized lines)
 * - CRLF handling
 *
 * Uses the same simple test harness as the other test files.
 */

#include "../src/repl_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>

/* ============================================================
 * Simple test harness (same as other test files)
 * ============================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %-55s ", #name); \
    fflush(stdout); \
    name(); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL\n"); \
        printf("    Assertion failed: %s\n", msg); \
        printf("    At %s:%d\n", __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

/* ============================================================
 * Socket helpers
 * ============================================================ */

/* Connect to localhost on the given port. Returns fd or -1. */
static int connect_to(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* Set a 2-second recv timeout to avoid hanging tests. */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
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

/* Send a string on a socket. Returns true on success. */
static bool send_line(int fd, const char *line) {
    size_t len = strlen(line);
    ssize_t n = send(fd, line, len, 0);
    return n == (ssize_t)len;
}

/*
 * Read one line (up to \n) from socket into buf.
 * Returns the length read (including \n), or -1 on error/timeout.
 * The line is null-terminated. buf must be at least bufsz bytes.
 */
static ssize_t recv_line(int fd, char *buf, size_t bufsz) {
    size_t pos = 0;
    while (pos < bufsz - 1) {
        ssize_t n = recv(fd, buf + pos, 1, 0);
        if (n <= 0) return -1;
        pos++;
        if (buf[pos - 1] == '\n') break;
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

/* ============================================================
 * Server lifecycle tests
 * ============================================================ */

TEST(test_start_and_stop) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server should start");
    ASSERT(repl_server_port(srv) > 0, "port should be assigned");
    repl_server_stop(srv);
    PASS();
}

TEST(test_start_null_err_out) {
    /* err_out can be NULL — should not crash. */
    ReplServer *srv = repl_server_start("127.0.0.1", 0, NULL);
    ASSERT_NOT_NULL(srv, "server should start with NULL err_out");
    repl_server_stop(srv);
    PASS();
}

TEST(test_stop_null_is_safe) {
    /* Calling stop with NULL should not crash. */
    repl_server_stop(NULL);
    PASS();
}

/* ============================================================
 * Single client / basic protocol
 * ============================================================ */

TEST(test_ident_token) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "1 foo\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 1 OK [IDENT foo]\n", "response matches");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_string_token) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "2 \"hi\"\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 2 OK [STRING hi]\n", "response matches");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_number_token) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "10 42\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 10 OK [NUMBER 42]\n", "response matches");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_error_unterminated_string) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "3 \"unterminated\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    /* Check prefix only — exact error message may vary. */
    ASSERT(strncmp(buf, "-> 3 ERR 1:", 11) == 0, "error prefix matches");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_error_adjacent_tokens) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "4 10+10\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT(strncmp(buf, "-> 4 ERR 1:", 11) == 0, "error prefix matches");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_whitespace_only_expr) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    /* "5 \n" means id=5 and expr is empty (just whitespace after id). */
    ASSERT(send_line(fd, "5 \n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 5 OK\n", "empty expr returns OK");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_whitespace_expr_spaces) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "6     \n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 6 OK\n", "spaces-only expr returns OK");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_crlf_line_ending) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "7 foo\r\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 7 OK [IDENT foo]\n", "CRLF handled");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Single connection, multiple requests
 * ============================================================ */

TEST(test_multiple_requests_one_connection) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    char buf[512];
    ssize_t n;

    /* Request 1 */
    ASSERT(send_line(fd, "1 hello\n"), "send 1");
    n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv 1");
    ASSERT_STR_EQ(buf, "-> 1 OK [IDENT hello]\n", "response 1");

    /* Request 2 */
    ASSERT(send_line(fd, "2 42\n"), "send 2");
    n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv 2");
    ASSERT_STR_EQ(buf, "-> 2 OK [NUMBER 42]\n", "response 2");

    /* Request 3 */
    ASSERT(send_line(fd, "3 +\n"), "send 3");
    n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv 3");
    ASSERT_STR_EQ(buf, "-> 3 OK [OPERATOR +]\n", "response 3");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_connection_stays_open) {
    /* Verify server doesn't close connection after first response. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    char buf[512];

    ASSERT(send_line(fd, "1 a\n"), "send 1");
    recv_line(fd, buf, sizeof(buf));

    /* Second request on same connection should work. */
    ASSERT(send_line(fd, "2 b\n"), "send 2");
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "second request succeeded");
    ASSERT_STR_EQ(buf, "-> 2 OK [IDENT b]\n", "response 2");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Multi-client behavior
 * ============================================================ */

/* Thread arg for concurrent client test. */
typedef struct {
    uint16_t port;
    const char *request;
    char response[512];
    bool ok;
} ClientArg;

static void *client_thread_fn(void *arg) {
    ClientArg *ca = arg;
    ca->ok = false;

    int fd = connect_to(ca->port);
    if (fd < 0) return NULL;

    if (!send_line(fd, ca->request)) { close(fd); return NULL; }

    ssize_t n = recv_line(fd, ca->response, sizeof(ca->response));
    if (n > 0) ca->ok = true;

    close(fd);
    return NULL;
}

TEST(test_two_concurrent_clients) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    ClientArg c1 = { .port = port, .request = "100 alpha\n" };
    ClientArg c2 = { .port = port, .request = "200 beta\n" };

    pthread_t t1, t2;
    pthread_create(&t1, NULL, client_thread_fn, &c1);
    pthread_create(&t2, NULL, client_thread_fn, &c2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    ASSERT(c1.ok, "client 1 got response");
    ASSERT(c2.ok, "client 2 got response");

    /* Each client should get its own response with matching ID. */
    ASSERT_STR_EQ(c1.response, "-> 100 OK [IDENT alpha]\n", "client 1 response");
    ASSERT_STR_EQ(c2.response, "-> 200 OK [IDENT beta]\n", "client 2 response");

    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Disconnect handling
 * ============================================================ */

TEST(test_client_connects_and_disconnects_immediately) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");
    close(fd);

    /* Give server a moment to handle the disconnect. */
    usleep(50000);

    /* Server should still be alive — try another connection. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after disconnect");

    ASSERT(send_line(fd, "1 ok\n"), "send after reconnect");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv after reconnect");
    ASSERT_STR_EQ(buf, "-> 1 OK [IDENT ok]\n", "response after reconnect");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_partial_line_then_disconnect) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    /* Send partial line (no newline) then close. */
    send_line(fd, "1 partial");
    close(fd);

    usleep(50000);

    /* Server should not crash — verify with another connection. */
    fd = connect_to(port);
    ASSERT(fd >= 0, "reconnect after partial disconnect");

    ASSERT(send_line(fd, "2 test\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 2 OK [IDENT test]\n", "response ok");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Error cases: bad request IDs
 * ============================================================ */

TEST(test_non_digit_id_is_error) {
    /*
     * IDs must be digits-only. A non-digit ID should produce
     * an error response (not crash, not silently succeed).
     */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "abc foo\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    /* Expect error response about invalid ID. */
    ASSERT(strstr(buf, "ERR") != NULL, "error response for non-digit ID");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

TEST(test_missing_id_is_error) {
    /* A line with no content (just newline) should be an error. */
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "\n"), "send empty line");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT(strstr(buf, "ERR") != NULL, "error response for empty line");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Oversized line handling
 * ============================================================ */

TEST(test_oversized_line_returns_error) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    /* Build a line longer than 4096 bytes: "1 " + 4095 'a' + "\n" */
    char big[4200];
    memset(big, 'a', sizeof(big));
    big[0] = '1';
    big[1] = ' ';
    big[4196] = '\n';
    big[4197] = '\0';

    ASSERT(send_line(fd, big), "send oversized");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    /* Should get an error, not a crash. */
    ASSERT(strstr(buf, "ERR") != NULL, "error for oversized line");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Multiple tokens in one request
 * ============================================================ */

TEST(test_multiple_tokens) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    ASSERT(send_line(fd, "1 foo 42 \"bar\" +\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf,
        "-> 1 OK [IDENT foo] [NUMBER 42] [STRING bar] [OPERATOR +]\n",
        "multiple tokens");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * ID-only request (no expr)
 * ============================================================ */

TEST(test_id_only_no_expr) {
    const char *err = NULL;
    ReplServer *srv = repl_server_start("127.0.0.1", 0, &err);
    ASSERT_NOT_NULL(srv, "server start");
    uint16_t port = repl_server_port(srv);

    int fd = connect_to(port);
    ASSERT(fd >= 0, "connect");

    /* Just an ID with no space after it. */
    ASSERT(send_line(fd, "99\n"), "send");
    char buf[512];
    ssize_t n = recv_line(fd, buf, sizeof(buf));
    ASSERT(n > 0, "recv");
    ASSERT_STR_EQ(buf, "-> 99 OK\n", "id-only returns OK");

    close(fd);
    repl_server_stop(srv);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== TCP REPL Server Tests ===\n\n");

    printf("Server lifecycle:\n");
    RUN_TEST(test_start_and_stop);
    RUN_TEST(test_start_null_err_out);
    RUN_TEST(test_stop_null_is_safe);

    printf("\nSingle client / basic protocol:\n");
    RUN_TEST(test_ident_token);
    RUN_TEST(test_string_token);
    RUN_TEST(test_number_token);
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_error_adjacent_tokens);
    RUN_TEST(test_whitespace_only_expr);
    RUN_TEST(test_whitespace_expr_spaces);
    RUN_TEST(test_crlf_line_ending);

    printf("\nSingle connection, multiple requests:\n");
    RUN_TEST(test_multiple_requests_one_connection);
    RUN_TEST(test_connection_stays_open);

    printf("\nMulti-client behavior:\n");
    RUN_TEST(test_two_concurrent_clients);

    printf("\nDisconnect handling:\n");
    RUN_TEST(test_client_connects_and_disconnects_immediately);
    RUN_TEST(test_partial_line_then_disconnect);

    printf("\nError cases:\n");
    RUN_TEST(test_non_digit_id_is_error);
    RUN_TEST(test_missing_id_is_error);
    RUN_TEST(test_oversized_line_returns_error);

    printf("\nMultiple tokens / edge cases:\n");
    RUN_TEST(test_multiple_tokens);
    RUN_TEST(test_id_only_no_expr);

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("    %d FAILED\n", tests_failed);
    }

    return tests_failed > 0 ? 1 : 0;
}
