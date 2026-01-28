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
test_output "kebab-case" "kebab-case" "OK [IDENT kebab-case]"
test_output "symbol sandwich" "hello+world" "OK [IDENT hello+world]"
test_output "empty string" '""' "OK [STRING ]"
test_output "whitespace only" "   " "OK"
test_output "operator between idents" "a + b" "OK [IDENT a] [OPERATOR +] [IDENT b]"
test_output "operator between numbers" "1 + 2" "OK [NUMBER 1] [OPERATOR +] [NUMBER 2]"

echo
echo "Error cases:"
test_error_prefix "unterminated string" '"hello' "ERR 1:1 "
test_error_prefix "adjacent tokens string+ident" '"a"b' "ERR 1:1 "
test_error_prefix "negative number error" "-10" "ERR 1:1 "
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
echo "========================================"
echo "Tests run: $((PASS + FAIL))"
echo "Passed:    $PASS"
echo "Failed:    $FAIL"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
