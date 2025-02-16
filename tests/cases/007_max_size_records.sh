#!/bin/sh
# tests/cases/007_max_size_records.sh

# Test configuration for maximum sizes
TEST_NAME="max_size_records"
MAX_RECORD_LENGTH=32760  # Maximum allowed LRECL
MAX_V_RECORD_LENGTH=32752  # MAX_RECORD_LENGTH - 8 (BDW + RDW)
LARGE_BLOCK_SIZE=32760   # Maximum block size

# Track test failures
failures=0

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

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
    print(f'{pattern}')" > "/data/tests/data/fixed_input.txt"

cat > "/data/tests/config/fixed_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "/data/tests/data/fixed_input.txt",
      "record_format": "F",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${MAX_RECORD_LENGTH}
    }
  ]
}
EOF

# Create matching extract config
cat > "/data/tests/config/fixed_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "/data/tests/output/fixed_output.txt",
      "record_format": "F",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${MAX_RECORD_LENGTH}
    }
  ]
}
EOF

echo "Testing F format with maximum size records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/fixed.aws -c /data/tests/config/fixed_create.json /data/tests/data/fixed_input.txt
/app/tapemgr extract -c /data/tests/config/fixed_extract.json /data/tests/output/fixed.aws

if diff "/data/tests/data/fixed_input.txt" "/data/tests/output/fixed_output.txt" > /dev/null; then
    echo "Fixed format maximum size test passed"
else
    echo "Fixed format maximum size test failed"
    diff "/data/tests/data/fixed_input.txt" "/data/tests/output/fixed_output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: V format with varying large records
echo "Subtest 2: Variable format large records"
python3 -c "
# Create records of various large sizes
sizes = [32750, 32751, 32752]  # Max size minus (BDW + RDW)
for i, size in enumerate(sizes):
    print('R' + str(i+1) + 'X' * (size-2))" > "/data/tests/data/var_input.txt"

cat > "/data/tests/config/var_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VAR",
      "local_file": "/data/tests/data/var_input.txt",
      "record_format": "V",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/var_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VAR",
      "local_file": "/data/tests/output/var_output.txt",
      "record_format": "V",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing V format with large records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/var.aws -c /data/tests/config/var_create.json /data/tests/data/var_input.txt
/app/tapemgr extract -c /data/tests/config/var_extract.json /data/tests/output/var.aws

if diff "/data/tests/data/var_input.txt" "/data/tests/output/var_output.txt" > /dev/null; then
    echo "Variable format large records test passed"
else
    echo "Variable format large records test failed"
    diff "/data/tests/data/var_input.txt" "/data/tests/output/var_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: VB format with maximum blocking
echo "Subtest 3: VB format with maximum block size"
python3 -c "
# Create a mix of large records that will test blocking
sizes = [16000, 16000, 16000]  # Should pack exactly two per block
for i, size in enumerate(sizes):
    print('B' + str(i+1) + 'Y' * (size-2))" > "/data/tests/data/vb_input.txt"

cat > "/data/tests/config/vb_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VB",
      "local_file": "/data/tests/data/vb_input.txt",
      "record_format": "VB",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/vb_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VB",
      "local_file": "/data/tests/output/vb_output.txt",
      "record_format": "VB",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing VB format with maximum blocking..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/vb.aws -c /data/tests/config/vb_create.json /data/tests/data/vb_input.txt
/app/tapemgr extract -c /data/tests/config/vb_extract.json /data/tests/output/vb.aws

if diff "/data/tests/data/vb_input.txt" "/data/tests/output/vb_output.txt" > /dev/null; then
    echo "VB format maximum blocking test passed"
else
    echo "VB format maximum blocking test failed"
    diff "/data/tests/data/vb_input.txt" "/data/tests/output/vb_output.txt"
    failures=$((failures + 1))
fi

# Subtest 4: VBS format with maximum spans
echo "Subtest 4: VBS format with maximum spans"
python3 -c "
# Create records that must span multiple maximum blocks
sizes = [32740, 32745]  # Large but within limits
for i, size in enumerate(sizes):
    print('S' + str(i+1) + 'Z' * (size-2))" > "/data/tests/data/vbs_input.txt"

cat > "/data/tests/config/vbs_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "/data/tests/data/vbs_input.txt",
      "record_format": "VBS",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/vbs_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "/data/tests/output/vbs_output.txt",
      "record_format": "VBS",
      "record_length": ${MAX_V_RECORD_LENGTH},
      "block_size": ${LARGE_BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing VBS format with maximum spans..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/vbs.aws -c /data/tests/config/vbs_create.json /data/tests/data/vbs_input.txt
/app/tapemgr extract -c /data/tests/config/vbs_extract.json /data/tests/output/vbs.aws

if diff "/data/tests/data/vbs_input.txt" "/data/tests/output/vbs_output.txt" > /dev/null; then
    echo "VBS format maximum spans test passed"
else
    echo "VBS format maximum spans test failed"
    diff "/data/tests/data/vbs_input.txt" "/data/tests/output/vbs_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
