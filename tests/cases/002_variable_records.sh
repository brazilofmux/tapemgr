#!/bin/sh
# tests/cases/002_variable_records.sh

# Test configuration
TEST_NAME="variable_records"
MAX_RECORD_LENGTH=100
BLOCK_SIZE=1000

# Track test failures
failures=0

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

# Subtest 1: Basic variable length records
echo "Subtest 1: Basic variable length records"
cat > "/data/tests/data/input.txt" << EOF
This is a short record
This is a much longer record that will test variable length handling properly
A
Testing a medium-length record here
EOF

# Create config for tape creation
cat > "/data/tests/config/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VARIABLE",
      "local_file": "/data/tests/data/input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

# Create extract config
cat > "/data/tests/config/extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VARIABLE",
      "local_file": "/data/tests/output/output.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

# Run the test phases
echo "Creating tape file..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/test.aws -c /data/tests/config/create.json /data/tests/data/input.txt

echo "Extracting from tape file..."
/app/tapemgr extract -c /data/tests/config/extract.json /data/tests/output/test.aws

if diff "/data/tests/data/input.txt" "/data/tests/output/output.txt" > /dev/null; then
    echo "Basic variable record test passed: Files match"
else
    echo "Basic variable record test failed: Files differ"
    diff "/data/tests/data/input.txt" "/data/tests/output/output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Record at max length
echo "Subtest 2: Testing max length record"
# Create a record that's exactly MAX_RECORD_LENGTH-4 (to account for RDW)
python3 -c "print('X' * $(($MAX_RECORD_LENGTH - 4)))" > "/data/tests/data/max_input.txt"

cat > "/data/tests/config/max_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MAXVAR",
      "local_file": "/data/tests/data/max_input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/max_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MAXVAR",
      "local_file": "/data/tests/output/max_output.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with max length record..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/max.aws -c /data/tests/config/max_create.json /data/tests/data/max_input.txt

echo "Extracting max length record..."
/app/tapemgr extract -c /data/tests/config/max_extract.json /data/tests/output/max.aws

if diff "/data/tests/data/max_input.txt" "/data/tests/output/max_output.txt" > /dev/null; then
    echo "Max length record test passed: Files match"
else
    echo "Max length record test failed: Files differ"
    diff "/data/tests/data/max_input.txt" "/data/tests/output/max_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: Empty and minimal records
echo "Subtest 3: Testing empty and minimal records"
cat > "/data/tests/data/minimal_input.txt" << EOF

x

y
EOF

cat > "/data/tests/config/minimal_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MINIMAL",
      "local_file": "/data/tests/data/minimal_input.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/minimal_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MINIMAL",
      "local_file": "/data/tests/output/minimal_output.txt",
      "record_format": "V",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with minimal records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/minimal.aws -c /data/tests/config/minimal_create.json /data/tests/data/minimal_input.txt

echo "Extracting minimal records..."
/app/tapemgr extract -c /data/tests/config/minimal_extract.json /data/tests/output/minimal.aws

if diff "/data/tests/data/minimal_input.txt" "/data/tests/output/minimal_output.txt" > /dev/null; then
    echo "Minimal records test passed: Files match"
else
    echo "Minimal records test failed: Files differ"
    diff "/data/tests/data/minimal_input.txt" "/data/tests/output/minimal_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
