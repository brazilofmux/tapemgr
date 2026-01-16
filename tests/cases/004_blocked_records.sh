#!/bin/bash
# tests/cases/004_blocked_records.sh

# Test configuration
TEST_NAME="blocked_records"
MAX_RECORD_LENGTH=80
BLOCK_SIZE=400  # Large enough to pack multiple records

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

# Subtest 1: Basic blocked records - multiple records per block
echo "Subtest 1: Basic blocked records"
cat > "${DATA_DIR}/input.txt" << EOF
This is record 1
A slightly longer record 2
Record 3 is also here
Short rec 4
This record 5 will help ensure we have multiple records per block
Record 6 continues the pattern
Lucky number 7
Record 8 keeps going
Number 9 is fine
Final record 10
EOF

# Create config for tape creation
cat > "${CONFIG_DIR}/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.BLOCKED",
      "local_file": "${DATA_DIR}/input.txt",
      "record_format": "VB",
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
      "dataset_name": "TEST.BLOCKED",
      "local_file": "${OUTPUT_DIR}/output.txt",
      "record_format": "VB",
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
    echo "Basic blocked records test passed: Files match"
else
    echo "Basic blocked records test failed: Files differ"
    diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Block boundary testing
echo "Subtest 2: Testing block boundaries"
# Create records that should force block boundaries
python3 -c "
import random
random.seed(42)  # For reproducibility
lengths = [20, 35, 50, 65, 75]  # Various lengths to test blocking
for i in range(15):
    print('X' * random.choice(lengths))" > "${DATA_DIR}/boundary_input.txt"

cat > "${CONFIG_DIR}/boundary_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.BOUNDARY",
      "local_file": "${DATA_DIR}/boundary_input.txt",
      "record_format": "VB",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/boundary_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.BOUNDARY",
      "local_file": "${OUTPUT_DIR}/boundary_output.txt",
      "record_format": "VB",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with boundary test records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/boundary.aws -c ${CONFIG_DIR}/boundary_create.json ${DATA_DIR}/boundary_input.txt

echo "Extracting boundary test records..."
${TAPEMGR} extract -c ${CONFIG_DIR}/boundary_extract.json ${OUTPUT_DIR}/boundary.aws

if diff "${DATA_DIR}/boundary_input.txt" "${OUTPUT_DIR}/boundary_output.txt" > /dev/null; then
    echo "Block boundary test passed: Files match"
else
    echo "Block boundary test failed: Files differ"
    diff "${DATA_DIR}/boundary_input.txt" "${OUTPUT_DIR}/boundary_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: Mixed length records with exact block fills
echo "Subtest 3: Testing mixed length records"
# Create a mix of records where some combinations exactly fill blocks
python3 -c "
records = [
    'A' * 75,  # With RDW: 79 bytes
    'B' * 70,  # With RDW: 74 bytes
    'C' * 65,  # With RDW: 69 bytes
    'D' * 60,  # With RDW: 64 bytes
    'E' * 55,  # With RDW: 59 bytes
    'F' * 50,  # With RDW: 54 bytes
]
for rec in records:
    print(rec)" > "${DATA_DIR}/mixed_input.txt"

cat > "${CONFIG_DIR}/mixed_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MIXED",
      "local_file": "${DATA_DIR}/mixed_input.txt",
      "record_format": "VB",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/mixed_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MIXED",
      "local_file": "${OUTPUT_DIR}/mixed_output.txt",
      "record_format": "VB",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with mixed length records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/mixed.aws -c ${CONFIG_DIR}/mixed_create.json ${DATA_DIR}/mixed_input.txt

echo "Extracting mixed length records..."
${TAPEMGR} extract -c ${CONFIG_DIR}/mixed_extract.json ${OUTPUT_DIR}/mixed.aws

if diff "${DATA_DIR}/mixed_input.txt" "${OUTPUT_DIR}/mixed_output.txt" > /dev/null; then
    echo "Mixed length records test passed: Files match"
else
    echo "Mixed length records test failed: Files differ"
    diff "${DATA_DIR}/mixed_input.txt" "${OUTPUT_DIR}/mixed_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
