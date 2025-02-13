#!/bin/sh
# tests/cases/001_fixed_records.sh

# Test configuration
TEST_NAME="fixed_records"
RECORD_LENGTH=80
BLOCK_SIZE=800

# Track test failures
failures=0

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

# Subtest 1: Basic fixed record handling
echo "Subtest 1: Basic fixed record handling"
cat > "/data/tests/data/input.txt" << EOF
RECORD0001THIS IS A FIXED LENGTH RECORD
RECORD0002ANOTHER FIXED LENGTH RECORD
EOF

# Create config for tape creation
cat > "/data/tests/config/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "/data/tests/data/input.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

# Create separate extract config
cat > "/data/tests/config/extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FIXED",
      "local_file": "/data/tests/output/input.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
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

if diff "/data/tests/data/input.txt" "/data/tests/output/input.txt" > /dev/null; then
    echo "Basic test passed: Files match"
else
    echo "Basic test failed: Files differ"
    diff "/data/tests/data/input.txt" "/data/tests/output/input.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Space padding handling
echo "Subtest 2: Space padding handling"
cat > "/data/tests/data/padded_input.txt" << EOF
RECORD0001THIS IS A FIXED LENGTH RECORD                                        
RECORD0002ANOTHER FIXED LENGTH RECORD                                         
EOF

# Test space padding/trimming behavior
echo "Testing space handling..."
cat > "/data/tests/config/space_test.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.PADDED",
      "local_file": "/data/tests/data/padded_input.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

/app/tapemgr create --volser=TEST01 -o /data/tests/output/padded.aws -c /data/tests/config/space_test.json /data/tests/data/padded_input.txt

# Extract padded file
cat > "/data/tests/config/space_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.PADDED",
      "local_file": "/data/tests/output/padded_output.txt",
      "record_format": "FB",
      "record_length": ${RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

/app/tapemgr extract -c /data/tests/config/space_extract.json /data/tests/output/padded.aws

if diff <(cat "/data/tests/data/input.txt") <(cat "/data/tests/output/padded_output.txt") > /dev/null; then
    echo "Space handling test passed: Files match after trimming"
else
    echo "Space handling test failed: Files differ after trimming"
    diff "/data/tests/data/input.txt" "/data/tests/output/padded_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
