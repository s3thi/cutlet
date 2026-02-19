#!/bin/sh
#
# test_examples.sh - Run each examples/*.cutlet program and compare stdout
# against the corresponding .expected file.
#
# For each examples/foo.cutlet, there must be an examples/foo.expected file
# containing the exact expected stdout output.
#
# Exit code: 0 if all examples match, 1 if any differ or .expected is missing.
#

set -e

CUTLET="${CUTLET:-./build/cutlet}"
PASS=0
FAIL=0

echo "Running example output tests..."
echo

for src in examples/*.cutlet; do
    name=$(basename "$src" .cutlet)
    expected_file="examples/${name}.expected"

    # Every .cutlet file must have a corresponding .expected file.
    if [ ! -f "$expected_file" ]; then
        echo "  FAIL: $name (missing $expected_file)"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Run the example and capture stdout. Stderr is discarded so runtime
    # warnings don't pollute the comparison.
    actual=$("$CUTLET" run "$src" 2>/dev/null)
    expected=$(cat "$expected_file")

    if [ "$actual" = "$expected" ]; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        # Show a unified diff for easy debugging.
        actual_file=$(mktemp)
        printf '%s\n' "$actual" > "$actual_file"
        diff -u "$expected_file" "$actual_file" | head -20 || true
        rm -f "$actual_file"
        FAIL=$((FAIL + 1))
    fi
done

echo
echo "========================================"
echo "Example tests run: $((PASS + FAIL))"
echo "Passed:            $PASS"
echo "Failed:            $FAIL"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
