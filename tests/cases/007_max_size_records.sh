#!/bin/bash
# tests/cases/007_max_size_records.sh

# Test configuration for maximum sizes
TEST_NAME="max_size_records"
MAX_RECORD_LENGTH=32760  # Maximum allowed LRECL
MAX_V_RECORD_LENGTH=32752  # MAX_RECORD_LENGTH - 8 (BDW + RDW)
LARGE_BLOCK_SIZE=32760   # Maximum block size

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

# Subtest 1: F format at maximum record size
echo "Subtest 1: Fixed format maximum size records"
python3 -c "
# Create a few maximum size records with distinct patterns
patterns = [
    'A' * ${MAX_RECORD_LENGTH},
    'B' * ${MAX_RECORD_LENGTH},
    'C' * ${MAX_RECORD_LENGTH}
]
for pattern in patterns:
    print(f'{pattern}')" > "${DATA_DIR}/fixed_input.txt"

cat > "${CONFIG_DIR}/fixed_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "${DATA_DIR}/fixed_input.txt",
      "record_format": "F",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${MAX_RECORD_LENGTH}
    }
  ]
}
EOF

# Create matching extract config
cat > "${CONFIG_DIR}/fixed_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "${OUTPUT_DIR}/fixed_output.txt",
      "record_format": "F",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${MAX_RECORD_LENGTH}
    }
  ]
}
EOF

echo "Testing F format with maximum size records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/fixed.aws -c ${CONFIG_DIR}/fixed_create.json ${DATA_DIR}/fixed_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/fixed_extract.json ${OUTPUT_DIR}/fixed.aws

if diff "${DATA_DIR}/fixed_input.txt" "${OUTPUT_DIR}/fixed_output.txt" > /dev/null; then
    echo "Fixed format maximum size test passed"
else
    echo "Fixed format maximum size test failed"
    diff "${DATA_DIR}/fixed_input.txt" "${OUTPUT_DIR}/fixed_output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: V format with varying large records
echo "Subtest 2: Variable format large records"
python3 -c "
# Create records of various large sizes
sizes = [32750, 32751, 32752]  # Max size minus (BDW + RDW)
for i, size in enumerate(sizes):
    print('R' + str(i+1) + 'X' * (size-2))" > "${DATA_DIR}/var_input.txt"

cat > "${CONFIG_DIR}/var_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VAR",
      "local_file": "${DATA_DIR}/var_input.txt",
      "record_format": "V",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/var_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VAR",
      "local_file": "${OUTPUT_DIR}/var_output.txt",
      "record_format": "V",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing V format with large records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/var.aws -c ${CONFIG_DIR}/var_create.json ${DATA_DIR}/var_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/var_extract.json ${OUTPUT_DIR}/var.aws

if diff "${DATA_DIR}/var_input.txt" "${OUTPUT_DIR}/var_output.txt" > /dev/null; then
    echo "Variable format large records test passed"
else
    echo "Variable format large records test failed"
    diff "${DATA_DIR}/var_input.txt" "${OUTPUT_DIR}/var_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: VB format with maximum blocking
echo "Subtest 3: VB format with maximum block size"
python3 -c "
# Create a mix of large records that will test blocking
sizes = [16000, 16000, 16000]  # Should pack exactly two per block
for i, size in enumerate(sizes):
    print('B' + str(i+1) + 'Y' * (size-2))" > "${DATA_DIR}/vb_input.txt"

cat > "${CONFIG_DIR}/vb_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VB",
      "local_file": "${DATA_DIR}/vb_input.txt",
      "record_format": "VB",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/vb_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VB",
      "local_file": "${OUTPUT_DIR}/vb_output.txt",
      "record_format": "VB",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing VB format with maximum blocking..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/vb.aws -c ${CONFIG_DIR}/vb_create.json ${DATA_DIR}/vb_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/vb_extract.json ${OUTPUT_DIR}/vb.aws

if diff "${DATA_DIR}/vb_input.txt" "${OUTPUT_DIR}/vb_output.txt" > /dev/null; then
    echo "VB format maximum blocking test passed"
else
    echo "VB format maximum blocking test failed"
    diff "${DATA_DIR}/vb_input.txt" "${OUTPUT_DIR}/vb_output.txt"
    failures=$((failures + 1))
fi

# Subtest 4: VBS format with maximum spans
echo "Subtest 4: VBS format with maximum spans"
python3 -c "
# Create records that must span multiple maximum blocks
sizes = [32740, 32745]  # Large but within limits
for i, size in enumerate(sizes):
    print('S' + str(i+1) + 'Z' * (size-2))" > "${DATA_DIR}/vbs_input.txt"

cat > "${CONFIG_DIR}/vbs_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "${DATA_DIR}/vbs_input.txt",
      "record_format": "VBS",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

cat > "${CONFIG_DIR}/vbs_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "${OUTPUT_DIR}/vbs_output.txt",
      "record_format": "VBS",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing VBS format with maximum spans..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/vbs.aws -c ${CONFIG_DIR}/vbs_create.json ${DATA_DIR}/vbs_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/vbs_extract.json ${OUTPUT_DIR}/vbs.aws

if diff "${DATA_DIR}/vbs_input.txt" "${OUTPUT_DIR}/vbs_output.txt" > /dev/null; then
    echo "VBS format maximum spans test passed"
else
    echo "VBS format maximum spans test failed"
    diff "${DATA_DIR}/vbs_input.txt" "${OUTPUT_DIR}/vbs_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
