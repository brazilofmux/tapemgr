#!/bin/sh
# tests/cases/005_blocked_spanned.sh

# Test configuration
TEST_NAME="blocked_spanned"
MAX_RECORD_LENGTH=1000  # Large to allow for big records
BLOCK_SIZE=100         # Small to force spanning

# Track test failures
failures=0

# Ensure our directories exist
mkdir -p /data/tests/data
mkdir -p /data/tests/config
mkdir -p /data/tests/output

# Subtest 1: Mixed records requiring both blocking and spanning
echo "Subtest 1: Mixed blocked and spanned records"
cat > "/data/tests/data/input.txt" << EOF
Small record 1
Another small record 2
$(python3 -c "print('M' * 150)")
Tiny
$(python3 -c "print('L' * 250)")
Small record again
$(python3 -c "print('S' * 95)")
$(python3 -c "print('X' * 180)")
EOF

# Create config for tape creation
cat > "/data/tests/config/create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.VBS",
      "local_file": "/data/tests/data/input.txt",
      "record_format": "VBS",
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
      "dataset_name": "TEST.VBS",
      "local_file": "/data/tests/output/output.txt",
      "record_format": "VBS",
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
    echo "Mixed VBS records test passed: Files match"
else
    echo "Mixed VBS records test failed: Files differ"
    diff "/data/tests/data/input.txt" "/data/tests/output/output.txt"
    failures=$((failures + 1))
fi

# Subtest 2: Complex block boundaries
echo "Subtest 2: Testing complex blocking scenarios"
# Create a sequence that forces interesting block boundaries
python3 -c "
sequences = [
    'A' * 85,   # Just under one block
    'B' * 120,  # Spans blocks but not too large
    'C' * 40,   # Small record that should block with others
    'D' * 35,   # Another small one for blocking
    'E' * 190,  # Large record that must span
    'F' * 45,   # Small record after a spanned one
    'G' * 150,  # Medium record that spans
    'H' * 30,   # Small record
    'I' * 30    # Small record that might share block with H
]
for seq in sequences:
    print(seq)" > "/data/tests/data/boundary_input.txt"

cat > "/data/tests/config/boundary_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.BOUNDARY",
      "local_file": "/data/tests/data/boundary_input.txt",
      "record_format": "VBS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/boundary_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.BOUNDARY",
      "local_file": "/data/tests/output/boundary_output.txt",
      "record_format": "VBS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with boundary test records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/boundary.aws -c /data/tests/config/boundary_create.json /data/tests/data/boundary_input.txt

echo "Extracting boundary test records..."
/app/tapemgr extract -c /data/tests/config/boundary_extract.json /data/tests/output/boundary.aws

if diff "/data/tests/data/boundary_input.txt" "/data/tests/output/boundary_output.txt" > /dev/null; then
    echo "Complex boundary test passed: Files match"
else
    echo "Complex boundary test failed: Files differ"
    diff "/data/tests/data/boundary_input.txt" "/data/tests/output/boundary_output.txt"
    failures=$((failures + 1))
fi

# Subtest 3: Large records with small trailing records
echo "Subtest 3: Testing large records followed by small ones"
python3 -c "
# Create a pattern of large records followed by small ones
for i in range(3):
    print('X' * 280)  # Large record that must span
    print('A' * 20)   # Small record that should block
    print('B' * 25)   # Small record that should block
    print('C' * 30)   # Small record that should block
" > "/data/tests/data/mixed_input.txt"

cat > "/data/tests/config/mixed_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MIXED",
      "local_file": "/data/tests/data/mixed_input.txt",
      "record_format": "VBS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

cat > "/data/tests/config/mixed_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.MIXED",
      "local_file": "/data/tests/output/mixed_output.txt",
      "record_format": "VBS",
      "record_length": ${MAX_RECORD_LENGTH},
      "block_size": ${BLOCK_SIZE}
    }
  ]
}
EOF

echo "Creating tape file with mixed large/small records..."
/app/tapemgr create --volser=TEST01 -o /data/tests/output/mixed.aws -c /data/tests/config/mixed_create.json /data/tests/data/mixed_input.txt

echo "Extracting mixed large/small records..."
/app/tapemgr extract -c /data/tests/config/mixed_extract.json /data/tests/output/mixed.aws

if diff "/data/tests/data/mixed_input.txt" "/data/tests/output/mixed_output.txt" > /dev/null; then
    echo "Mixed large/small records test passed: Files match"
else
    echo "Mixed large/small records test failed: Files differ"
    diff "/data/tests/data/mixed_input.txt" "/data/tests/output/mixed_output.txt"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    exit 0
else
    echo "Failed $failures subtests"
    exit 1
fi
