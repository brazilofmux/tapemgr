#!/bin/sh
# run_tests.sh - Main test runner

# Setup test environment
SCRIPT_DIR=$(dirname "$0")
TEST_ROOT="$SCRIPT_DIR/tests"
TEST_OUTPUT="$TEST_ROOT/output"

# tests/data, tests/config, and tests/expected are committed to the repo;
# only the (gitignored) output dir needs creating at runtime.
mkdir -p "$TEST_OUTPUT"

# Many tape-format tests (002–008, 028, etc.) rely on python3 to generate
# edge-case data and JSON configs. Fail early with a clear message if it is missing.
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required by several test cases (002–008, 028, ...)"
    echo "       but was not found in PATH."
    echo "Install python3 and re-run ./run_tests.sh"
    exit 1
fi

# Function to run a single test case
run_test() {
    test_name=$1
    test_script="$TEST_ROOT/cases/$test_name.sh"

    echo "Running test: $test_name"

    bash "$test_script"
    status=$?

    if [ $status -eq 0 ]; then
        echo "✅ Test $test_name passed"
    else
        echo "❌ Test $test_name failed"
    fi
    return $status
}

# Clean up previous test runs
cleanup() {
    echo "Cleaning up test artifacts..."
    rm -rf "$TEST_OUTPUT"/*
    mkdir -p "$TEST_OUTPUT"
}

# Run all tests
run_all_tests() {
    failed_tests=""

    for test_script in "$TEST_ROOT/cases"/*.sh; do
        [ -f "$test_script" ] || continue
        test_name=$(basename "$test_script" .sh)
        if ! run_test "$test_name"; then
            if [ -z "$failed_tests" ]; then
                failed_tests="$test_name"
            else
                failed_tests="$failed_tests $test_name"
            fi
        fi
    done

    # Report results
    echo
    echo "Test Summary:"
    echo "============="
    if [ -z "$failed_tests" ]; then
        echo "All tests passed! 🎉"
        return 0
    else
        echo "Failed tests: $failed_tests"
        return 1
    fi
}

# Main execution
cleanup
run_all_tests
test_status=$?

# Cross-stack report parity (C++ vs COBOL) when real reports from ./batch.sh exist.
# This closes the documented gap where the test runner could pass while
# compare_reports.sh (the actual financial report equivalence check) failed.
parity_status=0
if [ -f "reports_cpp/journal-svd.prn" ] && [ -f "reports_cobol/journal-svd.prn" ]; then
    echo
    echo "Running cross-stack report parity check (C++ vs COBOL)..."
    if ! ./tests/compare_reports.sh; then
        echo "❌ Report parity failures — see tests/output/diffs/"
        parity_status=1
    else
        echo "✅ All compared reports match between C++ and COBOL stacks"
    fi
else
    echo
    echo "Note: skipping report parity (run ./batch.sh first to generate reports_*/)"
fi

if [ $test_status -ne 0 ] || [ $parity_status -ne 0 ]; then
    exit 1
fi
exit 0
