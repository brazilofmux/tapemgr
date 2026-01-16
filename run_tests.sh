#!/bin/sh
# run_tests.sh - Main test runner

# Setup test environment
SCRIPT_DIR=$(dirname "$0")
TEST_ROOT="$SCRIPT_DIR/tests"
TEST_DATA="$TEST_ROOT/data"
TEST_CONFIG="$TEST_ROOT/config"
TEST_OUTPUT="$TEST_ROOT/output"
TEST_EXPECTED="$TEST_ROOT/expected"

# Create test directory structure
mkdir -p "$TEST_DATA" "$TEST_CONFIG" "$TEST_OUTPUT" "$TEST_EXPECTED"

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
