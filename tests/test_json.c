/*
 * test_json.c - Tests for JSON protocol encode/decode
 *
 * Tests the JSON output frame (type: "output") encode/decode and
 * the json_frame_type() helper for frame type discrimination.
 *
 * Test groups:
 * - Output frame encoding
 * - Output frame decoding (round-trip)
 * - Output frame edge cases (special chars, empty data)
 * - Frame type discrimination
 * - Error handling (malformed input)
 */

#include "../src/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Simple test harness (same as other test files)
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
 * Output frame encoding
 * ============================================================ */

TEST(test_encode_output_basic) {
    /* Encode a simple output frame and verify it contains expected fields. */
    JsonOutputFrame frame = {.id = 1, .data = "hello\n"};
    char *json = json_encode_output(&frame);
    ASSERT_NOT_NULL(json, "encode should succeed");

    /* Verify key fields are present in the JSON string. */
    ASSERT(strstr(json, "\"type\":\"output\"") != NULL, "type field");
    ASSERT(strstr(json, "\"id\":1") != NULL, "id field");
    ASSERT(strstr(json, "\"data\":\"hello\\n\"") != NULL, "data field with escaped newline");

    free(json);
    PASS();
}

TEST(test_encode_output_large_id) {
    /* Verify large request IDs encode correctly. */
    JsonOutputFrame frame = {.id = 999999, .data = "x"};
    char *json = json_encode_output(&frame);
    ASSERT_NOT_NULL(json, "encode should succeed");
    ASSERT(strstr(json, "\"id\":999999") != NULL, "large id");
    free(json);
    PASS();
}

TEST(test_encode_output_empty_data) {
    /* Empty data string should encode as "data":"" */
    JsonOutputFrame frame = {.id = 5, .data = ""};
    char *json = json_encode_output(&frame);
    ASSERT_NOT_NULL(json, "encode should succeed");
    ASSERT(strstr(json, "\"data\":\"\"") != NULL, "empty data");
    free(json);
    PASS();
}

/* ============================================================
 * Output frame round-trip (encode then decode)
 * ============================================================ */

TEST(test_output_roundtrip_basic) {
    /* Encode and decode a simple output frame. */
    JsonOutputFrame orig = {.id = 42, .data = "hello world\n"};
    char *json = json_encode_output(&orig);
    ASSERT_NOT_NULL(json, "encode");

    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed");
    ASSERT(parsed.id == 42, "id matches");
    ASSERT_NOT_NULL(parsed.data, "data set");
    ASSERT_STR_EQ(parsed.data, "hello world\n", "data matches");

    json_output_frame_free(&parsed);
    free(json);
    PASS();
}

TEST(test_output_roundtrip_empty_data) {
    /* Empty data round-trips correctly. */
    JsonOutputFrame orig = {.id = 0, .data = ""};
    char *json = json_encode_output(&orig);
    ASSERT_NOT_NULL(json, "encode");

    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed");
    ASSERT(parsed.id == 0, "id matches");
    ASSERT_NOT_NULL(parsed.data, "data set");
    ASSERT_STR_EQ(parsed.data, "", "empty data matches");

    json_output_frame_free(&parsed);
    free(json);
    PASS();
}

TEST(test_output_roundtrip_special_chars) {
    /* Data with quotes, backslashes, tabs, and newlines round-trips. */
    JsonOutputFrame orig = {.id = 7, .data = "line1\nline2\ttab\\slash\"quote"};
    char *json = json_encode_output(&orig);
    ASSERT_NOT_NULL(json, "encode");

    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed");
    ASSERT(parsed.id == 7, "id matches");
    ASSERT_STR_EQ(parsed.data, "line1\nline2\ttab\\slash\"quote", "special chars preserved");

    json_output_frame_free(&parsed);
    free(json);
    PASS();
}

TEST(test_output_roundtrip_multiline) {
    /* Multiple newlines in data. */
    JsonOutputFrame orig = {.id = 3, .data = "a\nb\nc\n"};
    char *json = json_encode_output(&orig);
    ASSERT_NOT_NULL(json, "encode");

    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed");
    ASSERT_STR_EQ(parsed.data, "a\nb\nc\n", "multiline data preserved");

    json_output_frame_free(&parsed);
    free(json);
    PASS();
}

/* ============================================================
 * Output frame parsing edge cases
 * ============================================================ */

TEST(test_parse_output_ignores_unknown_keys) {
    /* Extra keys in JSON should be ignored gracefully. */
    const char *json = "{\"type\":\"output\",\"id\":10,\"data\":\"hi\",\"extra\":\"ignored\"}";
    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed with extra keys");
    ASSERT(parsed.id == 10, "id matches");
    ASSERT_STR_EQ(parsed.data, "hi", "data matches");

    json_output_frame_free(&parsed);
    PASS();
}

TEST(test_parse_output_reordered_keys) {
    /* Keys in different order than encoding produces. */
    const char *json = "{\"data\":\"reordered\",\"id\":99,\"type\":\"output\"}";
    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed with reordered keys");
    ASSERT(parsed.id == 99, "id matches");
    ASSERT_STR_EQ(parsed.data, "reordered", "data matches");

    json_output_frame_free(&parsed);
    PASS();
}

TEST(test_parse_output_malformed_garbage) {
    /* Garbage input should fail. */
    const char *json = "not json";
    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(!ok, "parse should fail on garbage");
    PASS();
}

TEST(test_parse_output_empty_object) {
    /* Empty object — id defaults to 0, data is NULL. Parse succeeds
     * (matching existing parser behavior for request/response). */
    const char *json = "{}";
    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    ASSERT(ok, "parse should succeed on empty object");
    ASSERT(parsed.id == 0, "id defaults to 0");
    ASSERT(parsed.data == NULL, "data is NULL");

    json_output_frame_free(&parsed);
    PASS();
}

TEST(test_parse_output_null_data) {
    /* JSON with null data value. Our protocol never produces null for
     * string fields, and the parser (like request/response parsers)
     * doesn't handle null for known string keys — parse_json_string
     * fails to advance, so the parse loop gets stuck. This is expected
     * and consistent behavior. */
    const char *json = "{\"type\":\"output\",\"id\":1,\"data\":null}";
    JsonOutputFrame parsed;
    bool ok = json_parse_output(json, strlen(json), &parsed);
    /* data:null leaves parse_json_string unable to advance pos,
     * so the key loop gets confused and returns true prematurely
     * (hits '}' check) but data is NULL. Actually: parse_json_string
     * returns NULL, data stays NULL, pos doesn't advance past "null",
     * next loop iteration sees 'n' and can't parse a key string. */
    ASSERT(!ok, "parse fails on null data value");
    PASS();
}

/* ============================================================
 * Frame type discrimination
 * ============================================================ */

TEST(test_frame_type_result) {
    /* A result frame should be identified as JSON_FRAME_RESULT. */
    const char *json = "{\"type\":\"result\",\"id\":1,\"ok\":true,\"value\":\"42\"}";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_RESULT, "should be RESULT");
    PASS();
}

TEST(test_frame_type_output) {
    /* An output frame should be identified as JSON_FRAME_OUTPUT. */
    const char *json = "{\"type\":\"output\",\"id\":1,\"data\":\"hello\\n\"}";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_OUTPUT, "should be OUTPUT");
    PASS();
}

TEST(test_frame_type_eval) {
    /* An eval request is not result or output — should be UNKNOWN. */
    const char *json = "{\"type\":\"eval\",\"id\":1,\"expr\":\"42\"}";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_UNKNOWN, "eval type should be UNKNOWN");
    PASS();
}

TEST(test_frame_type_unknown_type) {
    /* Unrecognized type string. */
    const char *json = "{\"type\":\"something_else\",\"id\":1}";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_UNKNOWN, "unrecognized type should be UNKNOWN");
    PASS();
}

TEST(test_frame_type_no_type_field) {
    /* JSON object with no "type" key. */
    const char *json = "{\"id\":1,\"data\":\"hello\"}";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_UNKNOWN, "missing type should be UNKNOWN");
    PASS();
}

TEST(test_frame_type_garbage) {
    /* Non-JSON input. */
    const char *json = "not json at all";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_UNKNOWN, "garbage should be UNKNOWN");
    PASS();
}

TEST(test_frame_type_empty) {
    /* Empty string. */
    JsonFrameType t = json_frame_type("", 0);
    ASSERT(t == JSON_FRAME_UNKNOWN, "empty should be UNKNOWN");
    PASS();
}

TEST(test_frame_type_type_not_first_key) {
    /* "type" key is not the first key in the object. */
    const char *json = "{\"id\":5,\"type\":\"output\",\"data\":\"hi\"}";
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_OUTPUT, "should find type even if not first key");
    PASS();
}

/* ============================================================
 * Free helper
 * ============================================================ */

TEST(test_output_frame_free_null_safe) {
    /* Freeing a frame with NULL data should not crash. */
    JsonOutputFrame frame = {.id = 0, .data = NULL};
    json_output_frame_free(&frame);
    PASS();
}

TEST(test_output_frame_free_null_ptr) {
    /* Passing NULL pointer should not crash. */
    json_output_frame_free(NULL);
    PASS();
}

/* ============================================================
 * Existing response encode/decode (verify "type":"result" present)
 * ============================================================ */

TEST(test_response_encodes_type_result) {
    /* Verify the existing response encoder includes type:"result"
     * so that json_frame_type() can distinguish it. */
    JsonResponse resp = {.id = 1, .ok = true, .value = "42"};
    char *json = json_encode_response(&resp);
    ASSERT_NOT_NULL(json, "encode");
    ASSERT(strstr(json, "\"type\":\"result\"") != NULL, "response has type:result");
    free(json);
    PASS();
}

TEST(test_frame_type_on_encoded_response) {
    /* json_frame_type() on an encoded response returns RESULT. */
    JsonResponse resp = {.id = 1, .ok = false, .error = "oops"};
    char *json = json_encode_response(&resp);
    ASSERT_NOT_NULL(json, "encode");
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_RESULT, "encoded response is RESULT");
    free(json);
    PASS();
}

TEST(test_frame_type_on_encoded_output) {
    /* json_frame_type() on an encoded output frame returns OUTPUT. */
    JsonOutputFrame frame = {.id = 1, .data = "hello\n"};
    char *json = json_encode_output(&frame);
    ASSERT_NOT_NULL(json, "encode");
    JsonFrameType t = json_frame_type(json, strlen(json));
    ASSERT(t == JSON_FRAME_OUTPUT, "encoded output is OUTPUT");
    free(json);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== JSON Protocol Tests ===\n\n");

    printf("Output frame encoding:\n");
    RUN_TEST(test_encode_output_basic);
    RUN_TEST(test_encode_output_large_id);
    RUN_TEST(test_encode_output_empty_data);

    printf("\nOutput frame round-trip:\n");
    RUN_TEST(test_output_roundtrip_basic);
    RUN_TEST(test_output_roundtrip_empty_data);
    RUN_TEST(test_output_roundtrip_special_chars);
    RUN_TEST(test_output_roundtrip_multiline);

    printf("\nOutput frame parsing edge cases:\n");
    RUN_TEST(test_parse_output_ignores_unknown_keys);
    RUN_TEST(test_parse_output_reordered_keys);
    RUN_TEST(test_parse_output_malformed_garbage);
    RUN_TEST(test_parse_output_empty_object);
    RUN_TEST(test_parse_output_null_data);

    printf("\nFrame type discrimination:\n");
    RUN_TEST(test_frame_type_result);
    RUN_TEST(test_frame_type_output);
    RUN_TEST(test_frame_type_eval);
    RUN_TEST(test_frame_type_unknown_type);
    RUN_TEST(test_frame_type_no_type_field);
    RUN_TEST(test_frame_type_garbage);
    RUN_TEST(test_frame_type_empty);
    RUN_TEST(test_frame_type_type_not_first_key);

    printf("\nFree helper:\n");
    RUN_TEST(test_output_frame_free_null_safe);
    RUN_TEST(test_output_frame_free_null_ptr);

    printf("\nIntegration with existing types:\n");
    RUN_TEST(test_response_encodes_type_result);
    RUN_TEST(test_frame_type_on_encoded_response);
    RUN_TEST(test_frame_type_on_encoded_output);

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("    %d FAILED\n", tests_failed);
    }

    return tests_failed > 0 ? 1 : 0;
}
