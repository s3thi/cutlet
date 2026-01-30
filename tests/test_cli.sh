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

    actual=$(echo "$input" | "$CUTLET" repl)

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

    actual=$(echo "$input" | "$CUTLET" repl)

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

echo "Basic functionality:"
test_output "empty input" "" "OK"
test_output "single number" "42" "OK [NUMBER 42]"
test_output "single string" '"hello"' "OK [STRING hello]"
test_output "single ident" "foo" "OK [IDENT foo]"
test_output "mixed tokens" 'foo 42 "bar"' "OK [IDENT foo] [NUMBER 42] [STRING bar]"

echo
echo "Edge cases:"
test_output "operator alone" "+" "OK [OPERATOR +]"
test_output "minus operator" "-" "OK [OPERATOR -]"
test_output "kebab-case" "kebab-case" "OK [IDENT kebab] [OPERATOR -] [IDENT case]"
test_output "no symbol sandwich" "hello+world" "OK [IDENT hello] [OPERATOR +] [IDENT world]"
test_output "empty string" '""' "OK [STRING ]"
test_output "whitespace only" "   " "OK"
test_output "operator between idents" "a + b" "OK [IDENT a] [OPERATOR +] [IDENT b]"
test_output "operator between numbers" "1 + 2" "OK [NUMBER 1] [OPERATOR +] [NUMBER 2]"

echo
echo "Error cases:"
test_error_prefix "unterminated string" '"hello' "ERR 1:1 "
test_output "adjacent string+ident" '"a"b' "OK [STRING a] [IDENT b]"
test_output "negative number" "-10" "OK [OPERATOR -] [NUMBER 10]"
test_error_prefix "number adjacent ident" "42foo" "ERR 1:1 "

echo
echo "Multiple lines:"
multi_result=$(printf 'foo\n42\n"hi"' | "$CUTLET" repl)
multi_expected="OK [IDENT foo]
OK [NUMBER 42]
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
    connect_result=$(echo "foo" | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$connect_result" | grep -q "OK \[IDENT foo\]"; then
        echo "  PASS: connect single expression"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: connect single expression"
        echo "    Got: $connect_result"
        FAIL=$((FAIL + 1))
    fi

    # Test: connect and send multiple expressions
    multi_connect=$(printf 'foo\n42\n"hi"' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    if echo "$multi_connect" | grep -q "OK \[IDENT foo\]" && \
       echo "$multi_connect" | grep -q "OK \[NUMBER 42\]" && \
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
    if echo "$err_connect" | grep -q "ERR 1:"; then
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

# Test: --listen with no arg defaults to 127.0.0.1:7878
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
    def_result=$(echo "foo" | "$CUTLET" repl --connect 2>/dev/null)
    if echo "$def_result" | grep -q "OK \[IDENT foo\]"; then
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
    cp_result=$(echo "bar" | "$CUTLET" repl --connect :9111 2>/dev/null)
    if echo "$cp_result" | grep -q "OK \[IDENT bar\]"; then
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
