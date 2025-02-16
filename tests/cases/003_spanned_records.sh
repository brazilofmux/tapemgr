#!/bin/sh
# tests/cases/003_spanned_records.sh

# Test configuration
TEST_NAME="spanned_records"
MAX_RECORD_LENGTH=1000  # Larger to allow for spanning
BLOCK_SIZE=100         # Smaller to force spanning

# Track test failures
failures=0

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

# Subtest 1: Mixed size records including spanning
echo "Subtest 1: Mixed size records with spanning"
cat > "/data/tests/data/input.txt" << EOF
Short record
$(python3 -c "print('M' * 95)")
$(python3 -c "print('L' * 180)")
Another short record
$(python3 -c "print('X' * 250)")
EOF

# Create config for tape creation
cat > "/data/tests/config/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.SPANNED",
      "local_file": "/data/tests/data/input.txt",
      "record_format": "VS",
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
      "dataset_name": "TEST.SPANNED",
      "local_file": "/data/tests/output/output.txt",
      "record_format": "VS",
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
    echo "Basic spanned record test passed: Files match"
else
    echo "Basic spanned record test failed: Files differ"
    diff "/data/tests/data/input.txt" "/data/tests/output/output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Large record spanning multiple blocks
echo "Subtest 2: Testing large spanned record"
# Create a record that will span at least 3 blocks
python3 -c "print('Y' * 280)" > "/data/tests/data/large_input.txt"

cat > "/data/tests/config/large_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.LARGE",
      "local_file": "/data/tests/data/large_input.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/large_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.LARGE",
      "local_file": "/data/tests/output/large_output.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with large spanned record..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/large.aws -c /data/tests/config/large_create.json /data/tests/data/large_input.txt

echo "Extracting large spanned record..."
/app/tapemgr extract -c /data/tests/config/large_extract.json /data/tests/output/large.aws

if diff "/data/tests/data/large_input.txt" "/data/tests/output/large_output.txt" > /dev/null; then
    echo "Large spanned record test passed: Files match"
else
    echo "Large spanned record test failed: Files differ"
    diff "/data/tests/data/large_input.txt" "/data/tests/output/large_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: Multiple spanned records with varying segment counts
echo "Subtest 3: Testing multiple spanned records"
cat > "/data/tests/data/multi_input.txt" << EOF
$(python3 -c "print('A' * 150)")
$(python3 -c "print('B' * 90)")
$(python3 -c "print('C' * 220)")
$(python3 -c "print('D' * 85)")
EOF

cat > "/data/tests/config/multi_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MULTI",
      "local_file": "/data/tests/data/multi_input.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/multi_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MULTI",
      "local_file": "/data/tests/output/multi_output.txt",
      "record_format": "VS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with multiple spanned records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/multi.aws -c /data/tests/config/multi_create.json /data/tests/data/multi_input.txt

echo "Extracting multiple spanned records..."
/app/tapemgr extract -c /data/tests/config/multi_extract.json /data/tests/output/multi.aws

if diff "/data/tests/data/multi_input.txt" "/data/tests/output/multi_output.txt" > /dev/null; then
    echo "Multiple spanned records test passed: Files match"
else
    echo "Multiple spanned records test failed: Files differ"
    diff "/data/tests/data/multi_input.txt" "/data/tests/output/multi_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
