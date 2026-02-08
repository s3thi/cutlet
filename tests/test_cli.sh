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

# ============================================================
# say() output handling (client-side output frames)
# ============================================================
echo
echo "say() output (client-side):"
start_server ""

if [ -n "$SERVER_PORT" ]; then
    # say("hello") should produce output frame "hello\n" + result "nothing"
    say_hello_result=$(echo 'say("hello")' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    say_hello_expected="hello
nothing"
    if [ "$say_hello_result" = "$say_hello_expected" ]; then
        echo "  PASS: say(\"hello\") outputs hello then nothing"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: say(\"hello\") outputs hello then nothing"
        echo "    Expected: $say_hello_expected"
        echo "    Got:      $say_hello_result"
        FAIL=$((FAIL + 1))
    fi

    # say() with a number argument
    say_num_result=$(echo 'say(42)' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    say_num_expected="42
nothing"
    if [ "$say_num_result" = "$say_num_expected" ]; then
        echo "  PASS: say(42) outputs 42 then nothing"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: say(42) outputs 42 then nothing"
        echo "    Expected: $say_num_expected"
        echo "    Got:      $say_num_result"
        FAIL=$((FAIL + 1))
    fi

    # Multiple say() calls on separate lines (pipe mode sends each as separate request)
    multi_say_result=$(printf 'say("a")\nsay("b")' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    multi_say_expected="a
nothing
b
nothing"
    if [ "$multi_say_result" = "$multi_say_expected" ]; then
        echo "  PASS: multiple say() calls produce correct output"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: multiple say() calls produce correct output"
        echo "    Expected: $multi_say_expected"
        echo "    Got:      $multi_say_result"
        FAIL=$((FAIL + 1))
    fi

    # Regular expression without say() still works (no output frames)
    test_via_server "no say output for regular expr" "1 + 2" "3"

    # say() output appears before result in combined expressions
    # (pipe mode: each line is a separate request)
    combo_result=$(printf 'say("hi")\n1 + 2' | "$CUTLET" repl --connect "127.0.0.1:$SERVER_PORT" 2>/dev/null)
    combo_expected="hi
nothing
3"
    if [ "$combo_result" = "$combo_expected" ]; then
        echo "  PASS: say() then expression produces correct order"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: say() then expression produces correct order"
        echo "    Expected: $combo_expected"
        echo "    Got:      $combo_result"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  FAIL: server did not start"
    FAIL=$((FAIL + 5))
fi

stop_server

# ============================================================
# cutlet run <file> — file execution
# ============================================================
echo
echo "cutlet run (file execution):"

# Helper: run a cutlet file and check stdout output + exit code.
# Usage: test_run_file <name> <file_contents> <expected_stdout> [extra_flags]
test_run_file() {
    name="$1"
    contents="$2"
    expected="$3"
    flags="$4"

    tmpfile=$(mktemp /tmp/cutlet_test_XXXXXX)
    printf '%s' "$contents" > "$tmpfile"

    # shellcheck disable=SC2086
    actual=$("$CUTLET" run "$tmpfile" $flags 2>/dev/null)
    exit_code=$?

    if [ "$actual" = "$expected" ] && [ "$exit_code" -eq 0 ]; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    Expected stdout: $(echo "$expected" | head -5)"
        echo "    Got stdout:      $(echo "$actual" | head -5)"
        echo "    Exit code:       $exit_code (expected 0)"
        FAIL=$((FAIL + 1))
    fi

    rm -f "$tmpfile"
}

# Helper: run a cutlet file and expect failure (exit code 1) + stderr output.
test_run_file_error() {
    name="$1"
    contents="$2"
    expected_stderr_substr="$3"
    flags="$4"

    tmpfile=$(mktemp /tmp/cutlet_test_XXXXXX)
    printf '%s' "$contents" > "$tmpfile"

    # shellcheck disable=SC2086
    set +e
    stderr_out=$("$CUTLET" run "$tmpfile" $flags 2>&1 1>/dev/null)
    exit_code=$?
    set -e

    if [ "$exit_code" -ne 0 ] && echo "$stderr_out" | grep -qF "$expected_stderr_substr"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    Expected exit code != 0, got: $exit_code"
        echo "    Expected stderr containing: $expected_stderr_substr"
        echo "    Got stderr: $stderr_out"
        FAIL=$((FAIL + 1))
    fi

    rm -f "$tmpfile"
}

# Basic: say() produces output
test_run_file "say hello" 'say("hello")' "hello"

# say() with number
test_run_file "say number" 'say(42)' "42"

# Multiple say() calls
test_run_file "multiple say" 'say("a")
say("b")
say("c")' "a
b
c"

# Final expression value is NOT printed (unlike REPL)
test_run_file "no final value" '1 + 2' ""

# say() output only, final value suppressed
test_run_file "say then expr" 'say("hi")
1 + 2' "hi"

# Variables work across statements
test_run_file "variables" 'my x = 10
my y = 20
say(x + y)' "30"

# Empty file exits 0 with no output
test_run_file "empty file" '' ""

# Whitespace-only file exits 0 with no output
test_run_file "whitespace file" '
  ' ""

# Parse error: exit 1, error on stderr
test_run_file_error "parse error" '"unterminated' "unterminated string"

# Runtime error: exit 1, error on stderr
test_run_file_error "runtime error" '1 / 0' "division by zero"

# Unknown function error
test_run_file_error "unknown function" 'foo()' "unknown function"

# Nonexistent file: exit 1, error on stderr
set +e
nonexistent_stderr=$("$CUTLET" run "/tmp/cutlet_nonexistent_file_xyz.cutlet" 2>&1 1>/dev/null)
nonexistent_exit=$?
set -e
if [ "$nonexistent_exit" -ne 0 ] && echo "$nonexistent_stderr" | grep -qi "error"; then
    echo "  PASS: nonexistent file"
    PASS=$((PASS + 1))
else
    echo "  FAIL: nonexistent file"
    echo "    Exit code: $nonexistent_exit (expected != 0)"
    echo "    Stderr: $nonexistent_stderr"
    FAIL=$((FAIL + 1))
fi

# --tokens flag shows token debug output
tokens_tmpfile=$(mktemp /tmp/cutlet_test_XXXXXX)
printf '%s' 'say("hi")' > "$tokens_tmpfile"
tokens_result=$("$CUTLET" run "$tokens_tmpfile" --tokens 2>/dev/null)
if echo "$tokens_result" | grep -q "TOKENS"; then
    echo "  PASS: --tokens shows token output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: --tokens shows token output"
    echo "    Got: $tokens_result"
    FAIL=$((FAIL + 1))
fi
rm -f "$tokens_tmpfile"

# --ast flag shows AST debug output
ast_tmpfile=$(mktemp /tmp/cutlet_test_XXXXXX)
printf '%s' 'say("hi")' > "$ast_tmpfile"
ast_result=$("$CUTLET" run "$ast_tmpfile" --ast 2>/dev/null)
if echo "$ast_result" | grep -q "AST"; then
    echo "  PASS: --ast shows AST output"
    PASS=$((PASS + 1))
else
    echo "  FAIL: --ast shows AST output"
    echo "    Got: $ast_result"
    FAIL=$((FAIL + 1))
fi
rm -f "$ast_tmpfile"

# --tokens --ast combined
both_tmpfile=$(mktemp /tmp/cutlet_test_XXXXXX)
printf '%s' '1 + 2' > "$both_tmpfile"
both_result=$("$CUTLET" run "$both_tmpfile" --tokens --ast 2>/dev/null)
if echo "$both_result" | grep -q "TOKENS" && echo "$both_result" | grep -q "AST"; then
    echo "  PASS: --tokens --ast shows both"
    PASS=$((PASS + 1))
else
    echo "  FAIL: --tokens --ast shows both"
    echo "    Got: $both_result"
    FAIL=$((FAIL + 1))
fi
rm -f "$both_tmpfile"

# say() output appears even with --tokens/--ast
say_debug_tmpfile=$(mktemp /tmp/cutlet_test_XXXXXX)
printf '%s' 'say("world")' > "$say_debug_tmpfile"
say_debug_result=$("$CUTLET" run "$say_debug_tmpfile" --tokens --ast 2>/dev/null)
if echo "$say_debug_result" | grep -q "world" && echo "$say_debug_result" | grep -q "TOKENS"; then
    echo "  PASS: say() output with debug flags"
    PASS=$((PASS + 1))
else
    echo "  FAIL: say() output with debug flags"
    echo "    Got: $say_debug_result"
    FAIL=$((FAIL + 1))
fi
rm -f "$say_debug_tmpfile"

# cutlet run with no filename shows error
set +e
no_file_stderr=$("$CUTLET" run 2>&1 1>/dev/null)
no_file_exit=$?
set -e
if [ "$no_file_exit" -ne 0 ]; then
    echo "  PASS: run with no filename errors"
    PASS=$((PASS + 1))
else
    echo "  FAIL: run with no filename should error"
    FAIL=$((FAIL + 1))
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
