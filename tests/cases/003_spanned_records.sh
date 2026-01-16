#!/bin/bash
# tests/cases/003_spanned_records.sh

# Test configuration
TEST_NAME="spanned_records"
MAX_RECORD_LENGTH=1000  # Larger to allow for spanning
BLOCK_SIZE=100         # Smaller to force spanning

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

# Subtest 1: Mixed size records including spanning
echo "Subtest 1: Mixed size records with spanning"
cat > "${DATA_DIR}/input.txt" << EOF
Short record
$(python3 -c "print('M' * 95)")
$(python3 -c "print('L' * 180)")
Another short record
$(python3 -c "print('X' * 250)")
EOF

# Create config for tape creation
cat > "${CONFIG_DIR}/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.SPANNED",
      "local_file": "${DATA_DIR}/input.txt",
      "record_format": "VS",
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
      "dataset_name": "TEST.SPANNED",
      "local_file": "${OUTPUT_DIR}/output.txt",
      "record_format": "VS",
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
    echo "Basic spanned record test passed: Files match"
else
    echo "Basic spanned record test failed: Files differ"
    diff "${DATA_DIR}/input.txt" "${OUTPUT_DIR}/output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Large record spanning multiple blocks
echo "Subtest 2: Testing large spanned record"
# Create a record that will span at least 3 blocks
python3 -c "print('Y' * 280)" > "${DATA_DIR}/large_input.txt"

cat > "${CONFIG_DIR}/large_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.LARGE",
      "local_file": "${DATA_DIR}/large_input.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/large_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.LARGE",
      "local_file": "${OUTPUT_DIR}/large_output.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with large spanned record..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/large.aws -c ${CONFIG_DIR}/large_create.json ${DATA_DIR}/large_input.txt

echo "Extracting large spanned record..."
${TAPEMGR} extract -c ${CONFIG_DIR}/large_extract.json ${OUTPUT_DIR}/large.aws

if diff "${DATA_DIR}/large_input.txt" "${OUTPUT_DIR}/large_output.txt" > /dev/null; then
    echo "Large spanned record test passed: Files match"
else
    echo "Large spanned record test failed: Files differ"
    diff "${DATA_DIR}/large_input.txt" "${OUTPUT_DIR}/large_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: Multiple spanned records with varying segment counts
echo "Subtest 3: Testing multiple spanned records"
cat > "${DATA_DIR}/multi_input.txt" << EOF
$(python3 -c "print('A' * 150)")
$(python3 -c "print('B' * 90)")
$(python3 -c "print('C' * 220)")
$(python3 -c "print('D' * 85)")
EOF

cat > "${CONFIG_DIR}/multi_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MULTI",
      "local_file": "${DATA_DIR}/multi_input.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/multi_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MULTI",
      "local_file": "${OUTPUT_DIR}/multi_output.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with multiple spanned records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/multi.aws -c ${CONFIG_DIR}/multi_create.json ${DATA_DIR}/multi_input.txt

echo "Extracting multiple spanned records..."
${TAPEMGR} extract -c ${CONFIG_DIR}/multi_extract.json ${OUTPUT_DIR}/multi.aws

if diff "${DATA_DIR}/multi_input.txt" "${OUTPUT_DIR}/multi_output.txt" > /dev/null; then
    echo "Multiple spanned records test passed: Files match"
else
    echo "Multiple spanned records test failed: Files differ"
    diff "${DATA_DIR}/multi_input.txt" "${OUTPUT_DIR}/multi_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
