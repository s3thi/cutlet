#!/bin/sh
#
# test_cli.sh - Integration tests for the Cutlet CLI
#
# Tests the cutlet repl command end-to-end.
# Exit code: 0 if all tests pass, 1 otherwise.
#

set -e

CUTLET="${CUTLET:-./build/cutlet}"
PASS=0
FAIL=0

# Test helper: check output matches expected
test_output() {
    name="$1"
    input="$2"
    expected="$3"
    extra_flag="$4"

    actual=$(echo "$input" | "$CUTLET" repl $extra_flag)

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

# Test helper: check error output contains expected prefix
test_error_prefix() {
    name="$1"
    input="$2"
    prefix="$3"
    extra_flag="$4"

    actual=$(echo "$input" | "$CUTLET" repl $extra_flag)

    case "$actual" in
        "$prefix"*)
            echo "  PASS: $name"
            PASS=$((PASS + 1))
            ;;
        *)
            echo "  FAIL: $name"
            echo "    Input:    $input"
            echo "    Expected prefix: $prefix"
            echo "    Got:      $actual"
            FAIL=$((FAIL + 1))
            ;;
    esac
}

echo "Running CLI integration tests..."
echo

echo "Basic evaluation (token mode):"
test_output "empty input" "" "OK"
test_output "single number" "42" "OK [NUMBER 42]"
test_output "single string" '"hello"' "OK [STRING hello]"
test_output "addition" "1 + 2" "OK [NUMBER 3]"
test_output "multiplication" "2 * 3" "OK [NUMBER 6]"
test_output "float division" "7 / 2" "OK [NUMBER 3.5]"
test_output "precedence" "1 + 2 * 3" "OK [NUMBER 7]"
test_output "parentheses" "(1 + 2) * 3" "OK [NUMBER 9]"
test_output "exponentiation" "2 ** 10" "OK [NUMBER 1024]"
test_output "unary minus" "-3" "OK [NUMBER -3]"
test_output "whitespace only" "   " "OK"

echo
echo "Error cases (token mode):"
test_error_prefix "unterminated string" '"hello' "ERR "
test_error_prefix "number adjacent ident" "42foo" "ERR "
test_error_prefix "unknown ident" "foo" "ERR "
test_error_prefix "division by zero" "1 / 0" "ERR "

echo
echo "Multiple lines:"
multi_result=$(printf '42\n1 + 2\n"hi"' | "$CUTLET" repl)
multi_expected="OK [NUMBER 42]
OK [NUMBER 3]
OK [STRING hi]"
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

echo
echo "AST mode (--ast):"
test_output "ast number" "42" "AST [NUMBER 42]" "--ast"
test_output "ast string" '"hello"' "AST [STRING hello]" "--ast"
test_output "ast ident" "foo" "AST [IDENT foo]" "--ast"
test_output "ast empty" "" "AST" "--ast"
test_output "ast whitespace" "   " "AST" "--ast"
test_output "ast binop" "1 + 2" "AST [BINOP + [NUMBER 1] [NUMBER 2]]" "--ast"
test_output "ast precedence" "1 + 2 * 3" "AST [BINOP + [NUMBER 1] [BINOP * [NUMBER 2] [NUMBER 3]]]" "--ast"
test_output "ast unary" "-3" "AST [UNARY - [NUMBER 3]]" "--ast"
test_error_prefix "ast parse error" "(" "ERR " "--ast"
test_error_prefix "ast tokenizer error" "42foo" "ERR " "--ast"

echo
echo "CLI arguments:"
# Test help
if "$CUTLET" --help 2>&1 | grep -q "Usage:"; then
    echo "  PASS: --help shows usage"
    PASS=$((PASS + 1))
else
    echo "  FAIL: --help should show usage"
    FAIL=$((FAIL + 1))
fi

# Test unknown command
if "$CUTLET" unknown 2>&1 | grep -q "Unknown command"; then
    echo "  PASS: unknown command shows error"
    PASS=$((PASS + 1))
else
    echo "  FAIL: unknown command should show error"
    FAIL=$((FAIL + 1))
fi

# Test no arguments
if "$CUTLET" 2>&1 | grep -q "Usage:"; then
    echo "  PASS: no args shows usage"
    PASS=$((PASS + 1))
else
    echo "  FAIL: no args should show usage"
    FAIL=$((FAIL + 1))
fi

echo
echo "TCP REPL (--listen and --connect):"

# Start a server on an ephemeral port, capture the port from stdout.
# The server prints "Listening on HOST:PORT" to stdout on startup.
SERVER_OUT=$(mktemp)
"$CUTLET" repl --listen 127.0.0.1:0 > "$SERVER_OUT" 2>&1 &
SERVER_PID=$!
# Wait for server to print its listening line.
for i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -q "^Listening on " "$SERVER_OUT" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

SERVER_PORT=$(grep "^Listening on " "$SERVER_OUT" | sed 's/.*:\([0-9]*\)$/\1/')

if [ -n "$SERVER_PORT" ]; then
    echo "  (server started on port $SERVER_PORT)"

    # Test: connect and send a single expression
    connect_result=$(echo "1 + 2" | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$connect_result" | grep -q "OK \[NUMBER 3\]"; then
        echo "  PASS: connect single expression"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: connect single expression"
        echo "    Got: $connect_result"
        FAIL=$((FAIL + 1))
    fi

    # Test: connect and send multiple expressions
    multi_connect=$(printf '42\n1 + 2\n"hi"' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$multi_connect" | grep -q "OK \[NUMBER 42\]" && \
       echo "$multi_connect" | grep -q "OK \[NUMBER 3\]" && \
       echo "$multi_connect" | grep -q "OK \[STRING hi\]"; then
        echo "  PASS: connect multiple expressions"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: connect multiple expressions"
        echo "    Got: $multi_connect"
        FAIL=$((FAIL + 1))
    fi

    # Test: connect with tokenization error
    err_connect=$(echo '"unterminated' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$err_connect" | grep -q "ERR "; then
        echo "  PASS: connect error expression"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: connect error expression"
        echo "    Got: $err_connect"
        FAIL=$((FAIL + 1))
    fi

    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 3))
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
fi
rm -f "$SERVER_OUT"

# Test: --listen with no arg defaults to 127.0.0.1:7117
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
    # Verify we can connect with default --connect (no arg)
    def_result=$(echo "42" | "$CUTLET" repl --connect 2>/dev/null)
    if echo "$def_result" | grep -q "OK \[NUMBER 42\]"; then
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
    if echo "$cp_result" | grep -q "OK \[NUMBER 42\]"; then
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

# Test: --listen --ast with --connect --ast (AST over TCP)
SERVER_OUT_AST=$(mktemp)
"$CUTLET" repl --ast --listen 127.0.0.1:0 > "$SERVER_OUT_AST" 2>&1 &
SERVER_PID_AST=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -q "^Listening on " "$SERVER_OUT_AST" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

AST_PORT=$(grep "^Listening on " "$SERVER_OUT_AST" | sed 's/.*:\([0-9]*\)$/\1/')

if [ -n "$AST_PORT" ]; then
    echo "  (AST server started on port $AST_PORT)"

    # Both sides --ast: should get AST output
    ast_result=$(echo "1 + 2" | "$CUTLET" repl --ast --connect "127.0.0.1:$AST_PORT" 2>/dev/null)
    if echo "$ast_result" | grep -q "AST \[BINOP"; then
        echo "  PASS: --ast --listen + --ast --connect"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: --ast --listen + --ast --connect"
        echo "    Got: $ast_result"
        FAIL=$((FAIL + 1))
    fi

    # Mismatch: server --ast, client no --ast → error, non-zero exit
    mismatch1_exit=0
    mismatch1=$(echo "42" | "$CUTLET" repl --connect "127.0.0.1:$AST_PORT" 2>/dev/null) || mismatch1_exit=$?
    if echo "$mismatch1" | grep -q "mode mismatch"; then
        echo "  PASS: server --ast, client no --ast gives mismatch error"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: server --ast, client no --ast should give mismatch error"
        echo "    Got: $mismatch1"
        FAIL=$((FAIL + 1))
    fi
    if [ "$mismatch1_exit" -ne 0 ]; then
        echo "  PASS: server --ast, client no --ast exits non-zero"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: server --ast, client no --ast should exit non-zero"
        FAIL=$((FAIL + 1))
    fi

    kill "$SERVER_PID_AST" 2>/dev/null || true
    wait "$SERVER_PID_AST" 2>/dev/null || true
else
    echo "  FAIL: AST server did not start"
    FAIL=$((FAIL + 3))
    kill "$SERVER_PID_AST" 2>/dev/null || true
    wait "$SERVER_PID_AST" 2>/dev/null || true
fi
rm -f "$SERVER_OUT_AST"

# Mismatch: server no --ast, client --connect --ast
SERVER_OUT_NOAST=$(mktemp)
"$CUTLET" repl --listen 127.0.0.1:0 > "$SERVER_OUT_NOAST" 2>&1 &
SERVER_PID_NOAST=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -q "^Listening on " "$SERVER_OUT_NOAST" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

NOAST_PORT=$(grep "^Listening on " "$SERVER_OUT_NOAST" | sed 's/.*:\([0-9]*\)$/\1/')

if [ -n "$NOAST_PORT" ]; then
    mismatch2_exit=0
    mismatch2=$(echo "42" | "$CUTLET" repl --ast --connect "127.0.0.1:$NOAST_PORT" 2>/dev/null) || mismatch2_exit=$?
    if echo "$mismatch2" | grep -q "mode mismatch"; then
        echo "  PASS: server no --ast, client --ast gives mismatch error"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: server no --ast, client --ast should give mismatch error"
        echo "    Got: $mismatch2"
        FAIL=$((FAIL + 1))
    fi
    if [ "$mismatch2_exit" -ne 0 ]; then
        echo "  PASS: server no --ast, client --ast exits non-zero"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: server no --ast, client --ast should exit non-zero"
        FAIL=$((FAIL + 1))
    fi

    kill "$SERVER_PID_NOAST" 2>/dev/null || true
    wait "$SERVER_PID_NOAST" 2>/dev/null || true
else
    echo "  FAIL: non-AST server did not start"
    FAIL=$((FAIL + 2))
    kill "$SERVER_PID_NOAST" 2>/dev/null || true
    wait "$SERVER_PID_NOAST" 2>/dev/null || true
fi
rm -f "$SERVER_OUT_NOAST"

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
