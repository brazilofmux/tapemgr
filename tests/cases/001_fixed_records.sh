#!/bin/bash
# tests/cases/001_fixed_records.sh

# Test configuration
TEST_NAME="fixed_records"
RECORD_LENGTH=80
BLOCK_SIZE=800

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

# Subtest 1: Basic fixed record handling
echo "Subtest 1: Basic fixed record handling"
cat > "${DATA_DIR}/input.txt" << EOF
RECORD0001THIS IS A FIXED LENGTH RECORD
RECORD0002ANOTHER FIXED LENGTH RECORD
EOF

# Create config for tape creation
cat > "${CONFIG_DIR}/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "${DATA_DIR}/input.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

# Create separate extract config
cat > "${CONFIG_DIR}/extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "${OUTPUT_DIR}/input.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
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

if diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/input.txt" > /dev/null; then
    echo "Basic test passed: Files match"
else
    echo "Basic test failed: Files differ"
    diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/input.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Space padding handling
echo "Subtest 2: Space padding handling"
cat > "${DATA_DIR}/padded_input.txt" << EOF
RECORD0001THIS IS A FIXED LENGTH RECORD                                        
RECORD0002ANOTHER FIXED LENGTH RECORD                                         
EOF

# Test space padding/trimming behavior
echo "Testing space handling..."
cat > "${CONFIG_DIR}/space_test.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.PADDED",
      "local_file": "${DATA_DIR}/padded_input.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/padded.aws -c ${CONFIG_DIR}/space_test.json ${DATA_DIR}/padded_input.txt

# Extract padded file
cat > "${CONFIG_DIR}/space_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.PADDED",
      "local_file": "${OUTPUT_DIR}/padded_output.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

${TAPEMGR} extract -c ${CONFIG_DIR}/space_extract.json ${OUTPUT_DIR}/padded.aws

if diff <(cat "${DATA_DIR}/input.txt") <(cat "${OUTPUT_DIR}/padded_output.txt") > /dev/null; then
    echo "Space handling test passed: Files match after trimming"
else
    echo "Space handling test failed: Files differ after trimming"
    diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/padded_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
