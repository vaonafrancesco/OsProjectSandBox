#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="test_cycle_detection"
CMD_FILE="$OUT_DIR/${TEST_NAME}.commands"
OUT_FILE="$OUT_DIR/${TEST_NAME}.out"

fail() {
    echo "[FAIL] $1"
    if [ -f "$OUT_FILE" ]; then
        echo "----- controller output -----"
        cat "$OUT_FILE"
        echo "-----------------------------"
    fi
    bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
    exit 1
}

pass() {
    echo "[PASS] $TEST_NAME"
    bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
    exit 0
}

bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true

cat > "$CMD_FILE" <<'EOF'
add hub
add hub
list
link 2 to 1
list
link 1 to 1
link 1 to 2
list
exit
EOF

./bin/domotics_controller < "$CMD_FILE" > "$OUT_FILE" 2>&1 || true

grep -q "Added device: id=1 type=hub" "$OUT_FILE" || fail "first hub not added as expected"
grep -q "Added device: id=2 type=hub" "$OUT_FILE" || fail "second hub not added as expected"

grep -q "Linked device 2 to 1" "$OUT_FILE" || fail "valid link 2 -> 1 not reported"

grep -E -q "^2[[:space:]]+hub[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$" "$OUT_FILE" || \
    fail "device 2 parent was not updated to 1 after valid link"

if grep -q "Linked device 1 to 1" "$OUT_FILE"; then
    fail "self-link 1 -> 1 unexpectedly succeeded"
fi

if grep -q "Linked device 1 to 2" "$OUT_FILE"; then
    fail "cycle-creating link 1 -> 2 unexpectedly succeeded"
fi

grep -q "Error: Self link not allowed. A device cannot be linked to itself." "$OUT_FILE" || \
    fail "missing self-link error message"

grep -q "Error: Cycle detected. Linking these devices would create an invalid loop." "$OUT_FILE" || \
    fail "missing cycle-detected error message"

grep -E -q "^1[[:space:]]+hub[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+0$" "$OUT_FILE" || \
    fail "device 1 parent changed unexpectedly"

grep -E -q "^2[[:space:]]+hub[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$" "$OUT_FILE" || \
    fail "device 2 parent changed unexpectedly after invalid operations"

pass