#!/bin/sh
# tests/cases/006_empty_records.sh

# Test configuration
TEST_NAME="empty_records"
MAX_RECORD_LENGTH=100
BLOCK_SIZE=400

# Track test failures
failures=0

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

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
    print(pad_record(rec))" > "/data/tests/data/fixed_input.txt"

cat > "/data/tests/config/fixed_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "/data/tests/data/fixed_input.txt",
      "record_format": "F",
      "record_length": ${FIXED_RECORD_LENGTH},
      "block_size": ${FIXED_BLOCK_SIZE}
    }
  ]
}
EOF

# Subtest 2: Variable format with empty records
echo "Subtest 2: Variable format empty records"
cat > "/data/tests/data/var_input.txt" << EOF

Single character: X

Another empty line
Single character: Y

EOF

cat > "/data/tests/config/var_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VAR",
      "local_file": "/data/tests/data/var_input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
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
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing V format with empty records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/var.aws -c /data/tests/config/var_create.json /data/tests/data/var_input.txt
/app/tapemgr extract -c /data/tests/config/var_extract.json /data/tests/output/var.aws

if diff "/data/tests/data/var_input.txt" "/data/tests/output/var_output.txt" > /dev/null; then
    echo "Variable format empty records test passed"
else
    echo "Variable format empty records test failed"
    diff "/data/tests/data/var_input.txt" "/data/tests/output/var_output.txt"
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
" > "/data/tests/data/vb_input.txt"

cat > "/data/tests/config/vb_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VB",
      "local_file": "/data/tests/data/vb_input.txt",
      "record_format": "VB",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
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
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Testing VB format with empty records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/vb.aws -c /data/tests/config/vb_create.json /data/tests/data/vb_input.txt
/app/tapemgr extract -c /data/tests/config/vb_extract.json /data/tests/output/vb.aws

if diff "/data/tests/data/vb_input.txt" "/data/tests/output/vb_output.txt" > /dev/null; then
    echo "VB format empty records test passed"
else
    echo "VB format empty records test failed"
    diff "/data/tests/data/vb_input.txt" "/data/tests/output/vb_output.txt"
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
" > "/data/tests/data/vbs_input.txt"

cat > "/data/tests/config/vbs_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "/data/tests/data/vbs_input.txt",
      "record_format": "VBS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": 100
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
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": 100
    }
  ]
}
EOF

echo "Testing VBS format with empty records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/vbs.aws -c /data/tests/config/vbs_create.json /data/tests/data/vbs_input.txt
/app/tapemgr extract -c /data/tests/config/vbs_extract.json /data/tests/output/vbs.aws

if diff "/data/tests/data/vbs_input.txt" "/data/tests/output/vbs_output.txt" > /dev/null; then
    echo "VBS format empty records test passed"
else
    echo "VBS format empty records test failed"
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
