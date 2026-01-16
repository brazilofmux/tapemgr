#!/bin/bash
# tests/cases/002_variable_records.sh

# Test configuration
TEST_NAME="variable_records"
MAX_RECORD_LENGTH=100
BLOCK_SIZE=1000

# Track test failures
failures=0

# Resolve repo-relative paths
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
DATA_DIR="$TESTS_DIR/data"
CONFIG_DIR="$TESTS_DIR/config"
OUTPUT_DIR="$TESTS_DIR/output"
TAPEMGR="$ROOT_DIR/tapemgr"


# Ensure our directories exist
mkdir -p "$DATA_DIR"
mkdir -p "$CONFIG_DIR"
mkdir -p "$OUTPUT_DIR"

# Subtest 1: Basic variable length records
echo "Subtest 1: Basic variable length records"
cat > "${DATA_DIR}/input.txt" << EOF
This is a short record
This is a much longer record that will test variable length handling properly
A
Testing a medium-length record here
EOF

# Create config for tape creation
cat > "${CONFIG_DIR}/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VARIABLE",
      "local_file": "${DATA_DIR}/input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

# Create extract config
cat > "${CONFIG_DIR}/extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VARIABLE",
      "local_file": "${OUTPUT_DIR}/output.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

# Run the test phases
echo "Creating tape file..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/test.aws -c ${CONFIG_DIR}/create.json ${DATA_DIR}/input.txt

echo "Extracting from tape file..."
${TAPEMGR} extract -c ${CONFIG_DIR}/extract.json ${OUTPUT_DIR}/test.aws

if diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/output.txt" > /dev/null; then
    echo "Basic variable record test passed: Files match"
else
    echo "Basic variable record test failed: Files differ"
    diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Record at max length
echo "Subtest 2: Testing max length record"
# Create a record that's exactly MAX_RECORD_LENGTH-4 (to account for RDW)
python3 -c "print('X' * $(($MAX_RECORD_LENGTH - 4)))" > "${DATA_DIR}/max_input.txt"

cat > "${CONFIG_DIR}/max_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MAXVAR",
      "local_file": "${DATA_DIR}/max_input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/max_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MAXVAR",
      "local_file": "${OUTPUT_DIR}/max_output.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with max length record..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/max.aws -c ${CONFIG_DIR}/max_create.json ${DATA_DIR}/max_input.txt

echo "Extracting max length record..."
${TAPEMGR} extract -c ${CONFIG_DIR}/max_extract.json ${OUTPUT_DIR}/max.aws

if diff "${DATA_DIR}/max_input.txt" "${OUTPUT_DIR}/max_output.txt" > /dev/null; then
    echo "Max length record test passed: Files match"
else
    echo "Max length record test failed: Files differ"
    diff "${DATA_DIR}/max_input.txt" "${OUTPUT_DIR}/max_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: Empty and minimal records
echo "Subtest 3: Testing empty and minimal records"
cat > "${DATA_DIR}/minimal_input.txt" << EOF

x

y
EOF

cat > "${CONFIG_DIR}/minimal_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MINIMAL",
      "local_file": "${DATA_DIR}/minimal_input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/minimal_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MINIMAL",
      "local_file": "${OUTPUT_DIR}/minimal_output.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with minimal records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/minimal.aws -c ${CONFIG_DIR}/minimal_create.json ${DATA_DIR}/minimal_input.txt

echo "Extracting minimal records..."
${TAPEMGR} extract -c ${CONFIG_DIR}/minimal_extract.json ${OUTPUT_DIR}/minimal.aws

if diff "${DATA_DIR}/minimal_input.txt" "${OUTPUT_DIR}/minimal_output.txt" > /dev/null; then
    echo "Minimal records test passed: Files match"
else
    echo "Minimal records test failed: Files differ"
    diff "${DATA_DIR}/minimal_input.txt" "${OUTPUT_DIR}/minimal_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
