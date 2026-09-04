#!/bin/sh
# run_tests.sh - run every test case under tests/cases against ./tapemgr

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TEST_ROOT="$SCRIPT_DIR/tests"
TEST_OUTPUT="$TEST_ROOT/output"

if [ ! -x "$SCRIPT_DIR/tapemgr" ]; then
    echo "ERROR: $SCRIPT_DIR/tapemgr not found. Run 'make' first."
    exit 1
fi

# Most cases generate their fixtures with python3.
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required by the test cases but was not found in PATH."
    exit 1
fi

rm -rf "$TEST_OUTPUT"
mkdir -p "$TEST_OUTPUT" "$TEST_ROOT/data" "$TEST_ROOT/config"

failed_tests=""
for test_script in "$TEST_ROOT/cases"/*.sh; do
    [ -f "$test_script" ] || continue
    test_name=$(basename "$test_script" .sh)
    echo "Running test: $test_name"
    if bash "$test_script"; then
        echo "PASS $test_name"
    else
        echo "FAIL $test_name"
        failed_tests="$failed_tests $test_name"
    fi
done

echo
if [ -z "$failed_tests" ]; then
    echo "All tests passed."
    exit 0
fi
echo "Failed tests:$failed_tests"
exit 1
