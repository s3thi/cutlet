#!/bin/sh
#
# test_cli.sh - Integration tests for the Cutlet CLI
#
# Tests the cutlet repl command end-to-end using the JSON protocol.
# Default mode is server (--listen). Tests use --listen + --connect.
# Exit code: 0 if all tests pass, 1 otherwise.
#

set -e

CUTLET="${CUTLET:-./build/cutlet}"
PASS=0
FAIL=0

# Helper: start a server, get its port, run a test, then kill it.
# Usage: with_server [server_flags...] -- test_body
# Sets SERVER_PORT for use in the test body.

start_server() {
    local server_out
    server_out=$(mktemp)
    local server_flags="$1"

    # shellcheck disable=SC2086
    "$CUTLET" repl $server_flags --listen 127.0.0.1:0 > "$server_out" 2>&1 &
    local server_pid=$!

    # Wait for server to start.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if grep -q "^Listening on " "$server_out" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    SERVER_PORT=$(grep "^Listening on " "$server_out" | sed 's/.*:\([0-9]*\)$/\1/')
    SERVER_PID=$server_pid
    SERVER_OUT=$server_out
}

stop_server() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUT"
}

# Test helper: send input to server via --connect and check output.
test_via_server() {
    name="$1"
    input="$2"
    expected="$3"
    connect_flags="$4"

    # shellcheck disable=SC2086
    actual=$(echo "$input" | "$CUTLET" repl $connect_flags --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)

    if [ "$actual" = "$expected" ]; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    Input:    $input"
        echo "    Expected: $expected"
        echo "    Got:      $actual"
        FAIL=$((FAIL + 1))
    fi
}

# Test helper: check output contains expected string.
test_via_server_contains() {
    name="$1"
    input="$2"
    expected="$3"
    connect_flags="$4"

    # shellcheck disable=SC2086
    actual=$(echo "$input" | "$CUTLET" repl $connect_flags --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)

    if echo "$actual" | grep -qF "$expected"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    Input:    $input"
        echo "    Expected to contain: $expected"
        echo "    Got:      $actual"
        FAIL=$((FAIL + 1))
    fi
}

echo "Running CLI integration tests..."
echo

# ============================================================
# Basic evaluation (via server)
# ============================================================
echo "Basic evaluation (via server):"
start_server ""

if [ -n "$SERVER_PORT" ]; then
    test_via_server "single number" "42" "42"
    test_via_server "single string" '"hello"' "hello"
    test_via_server "addition" "1 + 2" "3"
    test_via_server "multiplication" "2 * 3" "6"
    test_via_server "float division" "7 / 2" "3.5"
    test_via_server "precedence" "1 + 2 * 3" "7"
    test_via_server "parentheses" "(1 + 2) * 3" "9"
    test_via_server "exponentiation" "2 ** 10" "1024"
    test_via_server "unary minus" "-3" "-3"
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 9))
fi

stop_server

# ============================================================
# Error cases
# ============================================================
echo
echo "Error cases:"
start_server ""

if [ -n "$SERVER_PORT" ]; then
    test_via_server_contains "unterminated string" '"hello' "ERR"
    test_via_server_contains "number adjacent ident" "42foo" "ERR"
    test_via_server_contains "unknown ident" "foo" "ERR"
    test_via_server_contains "division by zero" "1 / 0" "ERR"
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 4))
fi

stop_server

# ============================================================
# Multiple lines
# ============================================================
echo
echo "Multiple lines:"
start_server ""

if [ -n "$SERVER_PORT" ]; then
    multi_result=$(printf '42\n1 + 2\n"hi"' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    multi_expected="42
3
hi"
    if [ "$multi_result" = "$multi_expected" ]; then
        echo "  PASS: multiple lines"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: multiple lines"
        echo "    Expected:"
        echo "$multi_expected"
        echo "    Got:"
        echo "$multi_result"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 1))
fi

stop_server

# ============================================================
# Logical operators
# ============================================================
echo
echo "Logical operators:"
start_server ""

if [ -n "$SERVER_PORT" ]; then
    test_via_server "and true" "true and true" "true"
    test_via_server "and false" "true and false" "false"
    test_via_server "or true" "false or true" "true"
    test_via_server "or false" "false or false" "false"
    test_via_server "not true" "not true" "false"
    test_via_server "not false" "not false" "true"
    test_via_server "and returns operand" "1 and 2" "2"
    test_via_server "or returns operand" "0 or 2" "2"
    test_via_server "not zero" "not 0" "true"
    test_via_server "precedence or/and" "true or true and false" "true"
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 10))
fi

stop_server

# ============================================================
# Default mode starts server
# ============================================================
echo
echo "Default mode:"

# cutlet repl (no --listen) should start a server on default port
SERVER_OUT_DEF=$(mktemp)
"$CUTLET" repl --listen > "$SERVER_OUT_DEF" 2>&1 &
SERVER_PID_DEF=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -q "^Listening on " "$SERVER_OUT_DEF" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if grep -q "^Listening on 127.0.0.1:7117$" "$SERVER_OUT_DEF" 2>/dev/null; then
    def_result=$(echo "42" | "$CUTLET" repl --connect 2>/dev/null)
    if [ "$def_result" = "42" ]; then
        echo "  PASS: --listen default, --connect default"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --connect default should reach default server"
        echo "    Got: $def_result"
        FAIL=$((FAIL + 1))
    fi
else
    if grep -q "failed to bind" "$SERVER_OUT_DEF" 2>/dev/null; then
        echo "  SKIP: --listen default (port 7117 in use)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --listen default should use 127.0.0.1:7117"
        echo "    Got: $(cat "$SERVER_OUT_DEF")"
        FAIL=$((FAIL + 1))
    fi
fi
kill "$SERVER_PID_DEF" 2>/dev/null || true
wait "$SERVER_PID_DEF" 2>/dev/null || true
rm -f "$SERVER_OUT_DEF"

# Test: --listen :PORT uses default host with custom port
SERVER_OUT_CP=$(mktemp)
"$CUTLET" repl --listen :9111 > "$SERVER_OUT_CP" 2>&1 &
SERVER_PID_CP=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -q "^Listening on " "$SERVER_OUT_CP" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if grep -q "^Listening on 127.0.0.1:9111$" "$SERVER_OUT_CP" 2>/dev/null; then
    cp_result=$(echo "42" | "$CUTLET" repl --connect :9111 2>/dev/null)
    if [ "$cp_result" = "42" ]; then
        echo "  PASS: --listen :PORT, --connect :PORT"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --connect :PORT should reach custom port server"
        echo "    Got: $cp_result"
        FAIL=$((FAIL + 1))
    fi
else
    if grep -q "failed to bind" "$SERVER_OUT_CP" 2>/dev/null; then
        echo "  SKIP: --listen :PORT (port 9111 in use)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --listen :9111 should use 127.0.0.1:9111"
        echo "    Got: $(cat "$SERVER_OUT_CP")"
        FAIL=$((FAIL + 1))
    fi
fi
kill "$SERVER_PID_CP" 2>/dev/null || true
wait "$SERVER_PID_CP" 2>/dev/null || true
rm -f "$SERVER_OUT_CP"

# ============================================================
# Debug flags: --tokens
# ============================================================
echo
echo "Debug flags (--tokens):"
start_server "--tokens"

if [ -n "$SERVER_PORT" ]; then
    # Client with --tokens should see TOKENS line + value
    tokens_result=$(echo "42" | "$CUTLET" repl --tokens --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$tokens_result" | grep -q "TOKENS" && echo "$tokens_result" | grep -q "42"; then
        echo "  PASS: --tokens shows token output"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --tokens should show token output"
        echo "    Got: $tokens_result"
        FAIL=$((FAIL + 1))
    fi

    # Client without --tokens should NOT see TOKENS line
    no_tokens_result=$(echo "42" | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$no_tokens_result" | grep -q "TOKENS"; then
        echo "  FAIL: no --tokens should not see TOKENS"
        echo "    Got: $no_tokens_result"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: no --tokens hides token output"
        PASS=$((PASS + 1))
    fi
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 2))
fi

stop_server

# ============================================================
# Debug flags: --ast
# ============================================================
echo
echo "Debug flags (--ast):"
start_server "--ast"

if [ -n "$SERVER_PORT" ]; then
    ast_result=$(echo "1 + 2" | "$CUTLET" repl --ast --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$ast_result" | grep -q "AST" && echo "$ast_result" | grep -q "BINOP"; then
        echo "  PASS: --ast shows AST output"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --ast should show AST output"
        echo "    Got: $ast_result"
        FAIL=$((FAIL + 1))
    fi

    # Client without --ast should NOT see AST
    no_ast_result=$(echo "1 + 2" | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$no_ast_result" | grep -q "AST"; then
        echo "  FAIL: no --ast should not see AST"
        echo "    Got: $no_ast_result"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: no --ast hides AST output"
        PASS=$((PASS + 1))
    fi
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 2))
fi

stop_server

# ============================================================
# Debug flags: --tokens --ast combined
# ============================================================
echo
echo "Debug flags (combined):"
start_server "--tokens --ast"

if [ -n "$SERVER_PORT" ]; then
    both_result=$(echo "42" | "$CUTLET" repl --tokens --ast --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$both_result" | grep -q "TOKENS" && echo "$both_result" | grep -q "AST"; then
        echo "  PASS: --tokens --ast shows both"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --tokens --ast should show both"
        echo "    Got: $both_result"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 1))
fi

stop_server

# ============================================================
# Debug flags: server without flags, client with flags
# ============================================================
echo
echo "Debug flags (server without, client with):"
start_server ""

if [ -n "$SERVER_PORT" ]; then
    # Client requests --tokens but server doesn't have it — no tokens in output
    no_srv_tokens=$(echo "42" | "$CUTLET" repl --tokens --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$no_srv_tokens" | grep -q "TOKENS"; then
        echo "  FAIL: server without --tokens should not produce tokens"
        echo "    Got: $no_srv_tokens"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: server without --tokens produces no tokens"
        PASS=$((PASS + 1))
    fi
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 1))
fi

stop_server

# ============================================================
# CLI arguments
# ============================================================
echo
echo "CLI arguments:"
if "$CUTLET" --help 2>&1 | grep -q "Usage:"; then
    echo "  PASS: --help shows usage"
    PASS=$((PASS + 1))
else
    echo "  FAIL: --help should show usage"
    FAIL=$((FAIL + 1))
fi

if "$CUTLET" unknown 2>&1 | grep -q "Unknown command"; then
    echo "  PASS: unknown command shows error"
    PASS=$((PASS + 1))
else
    echo "  FAIL: unknown command should show error"
    FAIL=$((FAIL + 1))
fi

if "$CUTLET" 2>&1 | grep -q "Usage:"; then
    echo "  PASS: no args shows usage"
    PASS=$((PASS + 1))
else
    echo "  FAIL: no args should show usage"
    FAIL=$((FAIL + 1))
fi

# Test: invalid --listen arg
if "$CUTLET" repl --listen "badaddr" 2>&1 | grep -qi "error\|invalid\|failed"; then
    echo "  PASS: invalid --listen arg"
    PASS=$((PASS + 1))
else
    echo "  FAIL: invalid --listen arg should error"
    FAIL=$((FAIL + 1))
fi

# Test: connect to nothing fails with non-zero exit
if "$CUTLET" repl --connect "127.0.0.1:1" > /dev/null 2>&1; then
    echo "  FAIL: connect to nothing should fail"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: connect to nothing fails"
    PASS=$((PASS + 1))
fi

echo
echo "========================================"
echo "Tests run: $((PASS + FAIL))"
echo "Passed:    $PASS"
echo "Failed:    $FAIL"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
