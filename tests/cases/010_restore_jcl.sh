#!/bin/bash
# tests/cases/010_restore_jcl.sh
#
# create writes RESTORE.JCL in the current directory. Check that the tape
# volser, the tape-level DASD defaults, and per-file overrides all land in it.

failures=0
fail() { echo "FAIL: $1"; failures=$((failures + 1)); }

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
TAPEMGR="$ROOT_DIR/tapemgr"
OUTPUT_DIR="$ROOT_DIR/tests/output/010_restore_jcl"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

printf 'ONE\nTWO\n' > "$OUTPUT_DIR/a.txt"
printf 'THREE\n' > "$OUTPUT_DIR/b.txt"

# Subtest 1: no DASD keys at all -> built-in defaults, tape SER from --volser
cat > "$OUTPUT_DIR/plain.json" <<JSON
{
  "volume_serial": "TAP001",
  "files": [
    { "dataset_name": "TEST.A", "local_file": "$OUTPUT_DIR/a.txt",
      "record_format": "FB", "record_length": 80, "block_size": 800 }
  ]
}
JSON
(cd "$OUTPUT_DIR" && "$TAPEMGR" create --volser=TAP001 -o plain.aws -c plain.json >/dev/null) || fail "create (plain)"
JCL="$OUTPUT_DIR/RESTORE.JCL"
grep -q 'SER=TAP001),LABEL=(1,SL)' "$JCL" || fail "tape volser TAP001 not in restore JCL"
grep -q 'UNIT=3350,VOL=SER=PUB001' "$JCL" || fail "default DASD PUB001/3350 not in restore JCL"
grep -q 'SER=240001' "$JCL" && fail "stale hard-coded tape volser 240001 still present"

# Subtest 2: tape-level defaults plus a per-file override
cat > "$OUTPUT_DIR/dasd.json" <<JSON
{
  "volume_serial": "TAP002",
  "default_volser": "WORK01",
  "default_unit": "3380",
  "files": [
    { "dataset_name": "TEST.A", "local_file": "$OUTPUT_DIR/a.txt",
      "record_format": "FB", "record_length": 80, "block_size": 800 },
    { "dataset_name": "TEST.B", "local_file": "$OUTPUT_DIR/b.txt",
      "record_format": "FB", "record_length": 80, "block_size": 800,
      "target_volser": "PUB010", "target_unit": "3390" }
  ]
}
JSON
(cd "$OUTPUT_DIR" && "$TAPEMGR" create --volser=TAP002 -o dasd.aws -c dasd.json >/dev/null) || fail "create (dasd)"
grep -q 'SER=TAP002),LABEL=(1,SL)' "$JCL" || fail "TEST.A should be file 1 on TAP002"
grep -q 'SER=TAP002),LABEL=(2,SL)' "$JCL" || fail "TEST.B should be file 2 on TAP002"
grep -q 'UNIT=3380,VOL=SER=WORK01' "$JCL" || fail "default_volser/default_unit not applied to TEST.A"
grep -q 'UNIT=3390,VOL=SER=PUB010' "$JCL" || fail "target_volser/target_unit not applied to TEST.B"
grep -q 'SER=PUB001' "$JCL" && fail "built-in default leaked through when config supplied one"

echo
if [ $failures -eq 0 ]; then
    echo "Test 010_restore_jcl: all checks passed."
    exit 0
fi
echo "Test 010_restore_jcl: $failures failure(s)."
exit 1
