#!/bin/bash
# tests/cases/006_empty_records.sh

# Test configuration
TEST_NAME="empty_records"
MAX_RECORD_LENGTH=100
BLOCK_SIZE=400

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

# Subtest 1: Fixed format with empty records
echo "Subtest 1: Fixed format empty records"

# For F format, set LRECL=BLKSIZE
FIXED_RECORD_LENGTH=80  # Standard fixed length
FIXED_BLOCK_SIZE=80     # Must equal LRECL for F format

# Create test data - padding each record to full length
python3 -c "
# Helper to pad records
def pad_record(s):
    return '{:<${length}}'.format(s, length=${FIXED_RECORD_LENGTH})

records = [
    '',           # Empty record
    'X',          # Single character
    '',           # Another empty record
    'Y',          # Single character
    ''            # Final empty record
]

for rec in records:
    print(pad_record(rec))" > "${DATA_DIR}/fixed_input.txt"

cat > "${CONFIG_DIR}/fixed_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "${DATA_DIR}/fixed_input.txt",
      "record_format": "F",
      "record_length": ${FIXED_RECORD_LENGTH},
      "block_size": ${FIXED_BLOCK_SIZE}
    }
  ]
}
EOF

# Subtest 2: Variable format with empty records
echo "Subtest 2: Variable format empty records"
cat > "${DATA_DIR}/var_input.txt" << EOF

Single character: X

Another empty line
Single character: Y

EOF

cat > "${CONFIG_DIR}/var_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VAR",
      "local_file": "${DATA_DIR}/var_input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
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
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing V format with empty records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/var.aws -c ${CONFIG_DIR}/var_create.json ${DATA_DIR}/var_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/var_extract.json ${OUTPUT_DIR}/var.aws

if diff "${DATA_DIR}/var_input.txt" "${OUTPUT_DIR}/var_output.txt" > /dev/null; then
    echo "Variable format empty records test passed"
else
    echo "Variable format empty records test failed"
    diff "${DATA_DIR}/var_input.txt" "${OUTPUT_DIR}/var_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: VB format with empty records
echo "Subtest 3: VB format empty records"
python3 -c "
for i in range(20):
    if i % 5 == 0:
        print()
    elif i % 5 == 1:
        print('X')
    elif i % 5 == 2:
        print('YY')
    elif i % 5 == 3:
        print()
    else:
        print('ZZZ')
" > "${DATA_DIR}/vb_input.txt"

cat > "${CONFIG_DIR}/vb_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VB",
      "local_file": "${DATA_DIR}/vb_input.txt",
      "record_format": "VB",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
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
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing VB format with empty records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/vb.aws -c ${CONFIG_DIR}/vb_create.json ${DATA_DIR}/vb_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/vb_extract.json ${OUTPUT_DIR}/vb.aws

if diff "${DATA_DIR}/vb_input.txt" "${OUTPUT_DIR}/vb_output.txt" > /dev/null; then
    echo "VB format empty records test passed"
else
    echo "VB format empty records test failed"
    diff "${DATA_DIR}/vb_input.txt" "${OUTPUT_DIR}/vb_output.txt"
    failures=$((failures + 1))
fi

# Subtest 4: VBS format with empty records
echo "Subtest 4: VBS format with empty records and minimal spans"
python3 -c "
# Create a mix of empty records and records just large enough to span
print()
print('X' * 95)  # Just under block size
print()
print()
print('Y' * 120)  # Just over block size
print()
print('Z')
print()
print('W' * 150)  # Definitely spans
print()
" > "${DATA_DIR}/vbs_input.txt"

cat > "${CONFIG_DIR}/vbs_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "${DATA_DIR}/vbs_input.txt",
      "record_format": "VBS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": 100
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
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": 100
    }
  ]
}
EOF

echo "Testing VBS format with empty records..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/vbs.aws -c ${CONFIG_DIR}/vbs_create.json ${DATA_DIR}/vbs_input.txt
${TAPEMGR} extract -c ${CONFIG_DIR}/vbs_extract.json ${OUTPUT_DIR}/vbs.aws

if diff "${DATA_DIR}/vbs_input.txt" "${OUTPUT_DIR}/vbs_output.txt" > /dev/null; then
    echo "VBS format empty records test passed"
else
    echo "VBS format empty records test failed"
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
