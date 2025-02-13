# tests/cases/001_fixed_records.sh
#!/bin/sh

# Test configuration
TEST_NAME="fixed_records"
RECORD_LENGTH=80
BLOCK_SIZE=800

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

# Create source test data
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

# Verify results
if diff "/data/tests/data/input.txt" "/data/tests/output/input.txt" > /dev/null; then
    echo "Test passed: Files match"
    exit 0
else
    echo "Test failed: Files differ"
    diff "/data/tests/data/input.txt" "/data/tests/output/input.txt"
    exit 1
fi
