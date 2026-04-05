#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
FIXTURE_DIR="$REPO_ROOT/tests/output/accounts_bad_inputs"
CSV_DIR="$FIXTURE_DIR/csv"
REPORT_DIR="$FIXTURE_DIR/reports_cpp"
LOG_FILE="$FIXTURE_DIR/accounts.log"

rm -rf "$FIXTURE_DIR"
mkdir -p "$CSV_DIR" "$REPORT_DIR"

cat > "$CSV_DIR/descriptions.csv" <<'EOF'
desc_id,description
EOF

cat > "$CSV_DIR/companies.csv" <<'EOF'
company_id,company_code,re_acct_id,company_name
1,TST,9999,Test Company
EOF

cat > "$CSV_DIR/accounts.csv" <<'EOF'
acct_id,company_id,acct_number,acct_type,acct_class,acct_subclass,isactive,acct_name
1,1,1102100036,D,A,10,Y,Cash
2,1,5510000039,C,I,51,Y,Income
EOF

cat > "$CSV_DIR/assertions.csv" <<'EOF'
assertion_id,acct_id,assert_date,balance
EOF

cat > "$CSV_DIR/transactions.csv" <<'EOF'
txn_id,txn_number,company_id,txn_date,txn_ref,txn_src,desc_id
1,1001,1,2024-12-15,OPEN,JE,999
2,1002,1,2025-01-15,CURR,JE,999
EOF

cat > "$CSV_DIR/lines.csv" <<'EOF'
line_id,txn_id,acct_id,line_type,line_amount,desc_id
1,1,1,D,100.00,999
2,1,2,C,100.00,999
3,2,1,D,25.00,999
4,2,2,C,25.00,999
EOF

if ! DATA_DIR="$FIXTURE_DIR" "$REPO_ROOT/accounts" 2025-01 > "$LOG_FILE" 2>&1; then
    echo "accounts failed on missing descriptions / retained earnings fixture"
    cat "$LOG_FILE"
    exit 1
fi

if ! grep -q "Skipping retained earnings posting for company TST: account 9999 was not found." "$LOG_FILE"; then
    echo "expected retained earnings warning was not emitted"
    cat "$LOG_FILE"
    exit 1
fi

for report in journal activity profitloss balancesheet chartofaccounts1 chartofaccounts2; do
    if [ ! -f "$REPORT_DIR/${report}-tst.prn" ]; then
        echo "missing report output: $REPORT_DIR/${report}-tst.prn"
        exit 1
    fi
done

if ! grep -q "CURR" "$REPORT_DIR/journal-tst.prn"; then
    echo "journal report did not include the in-period transaction"
    exit 1
fi

if ! grep -q "CURR" "$REPORT_DIR/activity-tst.prn"; then
    echo "activity report did not include the in-period transaction"
    exit 1
fi
