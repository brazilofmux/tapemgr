#!/bin/bash
# tests/cases/011_codepages.sh
#
# Build tests/codepage_check.cpp against the converter sources and run it
# over every tr_utf8_cpNNN.txt mapping file in src/.

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT_DIR="$ROOT_DIR/tests/output/011_codepages"
mkdir -p "$OUTPUT_DIR"

CXX="${CXX:-c++}"
if ! "$CXX" -std=c++17 -O1 -I"$ROOT_DIR/src" -o "$OUTPUT_DIR/codepage_check" \
        "$ROOT_DIR/tests/codepage_check.cpp" \
        "$ROOT_DIR/src/ebcdic_converter.cpp" "$ROOT_DIR"/src/cp*_tables.cpp; then
    echo "FAIL: could not build codepage_check"
    exit 1
fi

"$OUTPUT_DIR/codepage_check" "$ROOT_DIR/src"
