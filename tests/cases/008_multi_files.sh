#!/bin/bash
# tests/cases/008_multi_files.sh

# Test multiple files on same tape
# 
# Note about trailing spaces:
# When extracting text files (non-binary mode), trailing spaces are trimmed.
# This is expected behavior - spaces are added when sending data to the mainframe
# and trimmed when extracting data from the mainframe formats.
# This test accounts for this behavior in its verification steps.
TEST_NAME="multi_files"

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

# Create test data files with different record formats
echo "Preparing test data..."

# File 1: Fixed format (FB) - explicitly adding trailing spaces to highlight trimming behavior
cat > "${DATA_DIR}/file1.txt" << EOF
RECORD0001THIS IS FILE 1 - FIXED FORMAT RECORDS               
RECORD0002SECOND RECORD IN FILE 1                             
RECORD0003THIRD RECORD WITH FIXED LENGTH                      
EOF

# Add a comment to document the expected behavior
echo "# Note: When extracted, trailing spaces will be trimmed from fixed-length text records" > "${DATA_DIR}/trimming_note.txt"

# File 2: Variable format (V)
cat > "${DATA_DIR}/file2.txt" << EOF
This is file 2 - variable length records
A shorter record
This is a much longer record that will test if variable length handling works properly with multiple files
Another medium length record
EOF

# File 3: Spanned format (VS)
python3 -c "
lines = [
    'File 3 - spanned records',
    'A' * 20,
    'B' * 120,
    'C' * 40,
    'D' * 200,
    'Short record',
    'E' * 150
]
for line in lines:
    print(line)" > "${DATA_DIR}/file3.txt"

# Create multi-file config 
cat > "${CONFIG_DIR}/multi_create.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FILE1",
      "local_file": "${DATA_DIR}/file1.txt",
      "record_format": "FB",
      "record_length": 60,
      "block_size": 300
    },
    {
      "dataset_name": "TEST.FILE2",
      "local_file": "${DATA_DIR}/file2.txt",
      "record_format": "V",
      "record_length": 100,
      "block_size": 400
    },
    {
      "dataset_name": "TEST.FILE3",
      "local_file": "${DATA_DIR}/file3.txt",
      "record_format": "VS",
      "record_length": 250,
      "block_size": 100
    }
  ]
}
EOF

# Create extract config
cat > "${CONFIG_DIR}/multi_extract.json" << EOF
{
  "volume_serial": "TEST01",
  "owner_code": "TESTUSER",
  "files": [
    {
      "dataset_name": "TEST.FILE1",
      "local_file": "${OUTPUT_DIR}/file1_output.txt",
      "record_format": "FB",
      "record_length": 60,
      "block_size": 300
    },
    {
      "dataset_name": "TEST.FILE2",
      "local_file": "${OUTPUT_DIR}/file2_output.txt",
      "record_format": "V",
      "record_length": 100,
      "block_size": 400
    },
    {
      "dataset_name": "TEST.FILE3",
      "local_file": "${OUTPUT_DIR}/file3_output.txt",
      "record_format": "VS",
      "record_length": 250,
      "block_size": 100
    }
  ]
}
EOF

# Run the test phases
echo "Creating multi-file tape..."
${TAPEMGR} create --volser=TEST01 -o ${OUTPUT_DIR}/multi_files.aws -c ${CONFIG_DIR}/multi_create.json

echo "Scanning tape to verify multiple files..."
${TAPEMGR} scan ${OUTPUT_DIR}/multi_files.aws > ${OUTPUT_DIR}/scan_results.txt

# Verify correct number of files in scan output
file_count=$(grep -c "Dataset:" ${OUTPUT_DIR}/scan_results.txt)
if [ "$file_count" -eq 3 ]; then
    echo "✓ Correct number of files found on tape: $file_count"
else
    echo "✗ Incorrect number of files found. Expected: 3, Got: $file_count"
    failures=$((failures + 1))
fi

echo "Extracting files from tape..."
${TAPEMGR} extract -c ${CONFIG_DIR}/multi_extract.json ${OUTPUT_DIR}/multi_files.aws

# Verify each file was extracted correctly
for file_num in 1 2 3; do
    # For text files, compare with trailing whitespace removed (expected trimming behavior)
    if [ $file_num -eq 1 ]; then
        # Fixed length text files have trailing spaces trimmed on extraction
        if diff <(sed 's/ *$//' "${DATA_DIR}/file${file_num}.txt") "${OUTPUT_DIR}/file${file_num}_output.txt" > /dev/null; then
            echo "✓ File $file_num extraction test passed: Files match after trimming"
        else
            echo "✗ File $file_num extraction test failed: Files differ even after accounting for trimming"
            diff <(sed 's/ *$//' "${DATA_DIR}/file${file_num}.txt") "${OUTPUT_DIR}/file${file_num}_output.txt"
            failures=$((failures + 1))
        fi
    else
        # For other files, compare normally
        if diff "${DATA_DIR}/file${file_num}.txt" "${OUTPUT_DIR}/file${file_num}_output.txt" > /dev/null; then
            echo "✓ File $file_num extraction test passed: Files match"
        else
            echo "✗ File $file_num extraction test failed: Files differ"
            diff "${DATA_DIR}/file${file_num}.txt" "${OUTPUT_DIR}/file${file_num}_output.txt"
            failures=$((failures + 1))
        fi
    fi
done

# Test init mode
echo "Testing init mode on multi-file tape..."
${TAPEMGR} init -o ${OUTPUT_DIR}/auto_config.json ${OUTPUT_DIR}/multi_files.aws

# Verify JSON config contains all three files
file_entries=$(grep -c "dataset_name" ${OUTPUT_DIR}/auto_config.json)
if [ "$file_entries" -eq 3 ]; then
    echo "✓ Init mode generated correct config with $file_entries files"
else
    echo "✗ Init mode failed: Expected 3 file entries, found $file_entries"
    failures=$((failures + 1))
fi

# Final exit
if [ $failures -eq 0 ]; then
    echo "All multi-file tests passed! 🎉"
    exit 0
else
    echo "Failed $failures multi-file tests"
    exit 1
fi
