#!/bin/bash
# tests/cases/009_negative_inputs.sh
#
# Negative / error-handling tests for tapemgr.
#
# These tests verify that tapemgr fails gracefully (non-zero exit, useful
# error message on stderr) for bad inputs instead of crashing or producing
# malformed IBM AWS tapes that could later be loaded on a real MVS system.
#
# All "create" negative cases are designed to fail before writing any tape
# file, or the produced file is immediately discarded.
#
# Categories covered:
#   - Missing required command-line options
#   - Malformed / invalid JSON configuration
#   - Config validation errors (bad record_format, out-of-range lengths, etc.)
#   - Missing input file for create
#   - Corrupted / truncated tape file for scan/extract

TEST_NAME="negative_inputs"

failures=0

fail() {
    echo "FAIL: $1"
    failures=$((failures + 1))
}

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
TAPEMGR="$ROOT_DIR/tapemgr"
OUTPUT_DIR="$ROOT_DIR/tests/output/009_negative"
CONFIG_DIR="$OUTPUT_DIR/config"
DATA_DIR="$OUTPUT_DIR/data"

mkdir -p "$OUTPUT_DIR" "$CONFIG_DIR" "$DATA_DIR"

# Helper to run a command and expect it to fail with a message containing a substring
expect_failure() {
    local desc="$1"; shift
    local expected_substring="$1"; shift
    local cmd=("$@")

    local stderr_file="$OUTPUT_DIR/err.$$"
    if "${cmd[@]}" 2>"$stderr_file"; then
        fail "$desc: command unexpectedly succeeded"
        cat "$stderr_file"
    else
        if grep -q "$expected_substring" "$stderr_file"; then
            echo "  OK: $desc (failed as expected with: $expected_substring)"
        else
            fail "$desc: failed but error message did not contain '$expected_substring'"
            echo "  Actual stderr:"
            cat "$stderr_file"
        fi
    fi
    rm -f "$stderr_file"
}

echo "=== tapemgr negative input tests ==="

# --------------------------------------------------------------------
# 1. create — missing required --volser
# --------------------------------------------------------------------
expect_failure "create missing --volser" "volser is required" \
    "$TAPEMGR" create -c "$CONFIG_DIR/dummy.json" -o "$OUTPUT_DIR/dummy.aws" "$DATA_DIR/dummy.txt"

# --------------------------------------------------------------------
# 2. create — missing required -o / --output
# --------------------------------------------------------------------
expect_failure "create missing --output" "output" \
    "$TAPEMGR" create --volser=TEST01 -c "$CONFIG_DIR/dummy.json" "$DATA_DIR/dummy.txt"

# --------------------------------------------------------------------
# 3. create — JSON syntax error in config
# --------------------------------------------------------------------
echo '{ "volume_serial": "TEST01", "files": [ }' > "$CONFIG_DIR/bad_syntax.json"
expect_failure "create with JSON syntax error" "JSON parse error" \
    "$TAPEMGR" create --volser=TEST01 -o "$OUTPUT_DIR/bad.aws" -c "$CONFIG_DIR/bad_syntax.json"
rm -f "$CONFIG_DIR/bad_syntax.json" "$OUTPUT_DIR/bad.aws"

# --------------------------------------------------------------------
# 4. create — config missing required "files" array
# --------------------------------------------------------------------
cat > "$CONFIG_DIR/missing_files.json" << EOF
{ "volume_serial": "TEST01" }
EOF
expect_failure "create config missing files array" "Missing required field 'files'" \
    "$TAPEMGR" create --volser=TEST01 -o "$OUTPUT_DIR/bad.aws" -c "$CONFIG_DIR/missing_files.json"
rm -f "$CONFIG_DIR/missing_files.json" "$OUTPUT_DIR/bad.aws"

# --------------------------------------------------------------------
# 5. create — invalid record_format
# --------------------------------------------------------------------
cat > "$CONFIG_DIR/bad_recFM.json" << EOF
{
  "volume_serial": "TEST01",
  "files": [{
    "dataset_name": "TEST.BADFM",
    "local_file": "$DATA_DIR/dummy.txt",
    "record_format": "XYZ",
    "record_length": 80,
    "block_size": 800
  }]
}
EOF
expect_failure "create with invalid record_format" "Invalid record_format" \
    "$TAPEMGR" create --volser=TEST01 -o "$OUTPUT_DIR/bad.aws" -c "$CONFIG_DIR/bad_recFM.json"
rm -f "$CONFIG_DIR/bad_recFM.json" "$OUTPUT_DIR/bad.aws"

# --------------------------------------------------------------------
# 6. create — record_length out of range (0 and >32760)
# --------------------------------------------------------------------
cat > "$CONFIG_DIR/bad_len.json" << EOF
{
  "volume_serial": "TEST01",
  "files": [{
    "dataset_name": "TEST.BADLEN",
    "local_file": "$DATA_DIR/dummy.txt",
    "record_format": "F",
    "record_length": 0,
    "block_size": 800
  }]
}
EOF
expect_failure "create with record_length=0" "record_length must be between 1 and 32760" \
    "$TAPEMGR" create --volser=TEST01 -o "$OUTPUT_DIR/bad.aws" -c "$CONFIG_DIR/bad_len.json"
rm -f "$CONFIG_DIR/bad_len.json" "$OUTPUT_DIR/bad.aws"

# --------------------------------------------------------------------
# 7. create — references non-existent local_file
# --------------------------------------------------------------------
cat > "$CONFIG_DIR/missing_input.json" << EOF
{
  "volume_serial": "TEST01",
  "files": [{
    "dataset_name": "TEST.MISSING",
    "local_file": "$DATA_DIR/this_file_does_not_exist_12345.txt",
    "record_format": "F",
    "record_length": 80,
    "block_size": 800
  }]
}
EOF
expect_failure "create with missing local_file" "Input file not found" \
    "$TAPEMGR" create --volser=TEST01 -o "$OUTPUT_DIR/bad.aws" -c "$CONFIG_DIR/missing_input.json"
rm -f "$CONFIG_DIR/missing_input.json" "$OUTPUT_DIR/bad.aws"

# --------------------------------------------------------------------
# 8. scan / extract on a completely invalid (garbage) "tape" file
#    This tests graceful handling of input that is not a valid AWS tape at all.
# --------------------------------------------------------------------
printf 'This is not an AWS tape file at all. Garbage data for negative testing.' > "$OUTPUT_DIR/garbage.aws"

# scan is intentionally tolerant (reports 0 files + summary instead of hard error)
"$TAPEMGR" scan "$OUTPUT_DIR/garbage.aws" > "$OUTPUT_DIR/scan_garbage.out" 2>&1
if grep -q "Tape Summary" "$OUTPUT_DIR/scan_garbage.out" && grep -q "Total Files: 0" "$OUTPUT_DIR/scan_garbage.out"; then
    echo "  OK: scan on garbage file (graceful: reported 0 files, no crash)"
else
    fail "scan on garbage file did not produce expected summary"
    cat "$OUTPUT_DIR/scan_garbage.out"
fi
rm -f "$OUTPUT_DIR/scan_garbage.out"

cat > "$CONFIG_DIR/garbage_extract.json" << EOF
{
  "volume_serial": "GARB",
  "files": [{
    "dataset_name": "TEST.GARB",
    "local_file": "$OUTPUT_DIR/garb_out.txt",
    "record_format": "F",
    "record_length": 80,
    "block_size": 80
  }]
}
EOF
expect_failure "extract on garbage file" "No valid files\|malformed\|scan\|VOL1" \
    "$TAPEMGR" extract -c "$CONFIG_DIR/garbage_extract.json" "$OUTPUT_DIR/garbage.aws"

rm -f "$OUTPUT_DIR/garbage.aws" "$CONFIG_DIR/garbage_extract.json" "$OUTPUT_DIR/garb_out.txt"

# --------------------------------------------------------------------
# Final summary
# --------------------------------------------------------------------
echo
if [ $failures -eq 0 ]; then
    echo "Test 009_negative_inputs: All negative-input cases passed."
    exit 0
else
    echo "Test 009_negative_inputs: $failures failure(s)."
    exit 1
fi
