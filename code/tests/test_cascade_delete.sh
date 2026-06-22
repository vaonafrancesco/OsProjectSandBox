#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="test_cascade_delete"
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
: > "$OUT_FILE"

cat > "$CMD_FILE" <<'EOF'
add hub
add bulb
add window
link 2 to 1
link 3 to 1
list
del 1
list
exit
EOF

./bin/domotics_controller < "$CMD_FILE" > "$OUT_FILE" 2>&1 || true

grep -q "Added device: id=1 type=hub" "$OUT_FILE" || fail "hub not added as expected"
grep -q "Added device: id=2 type=bulb" "$OUT_FILE" || fail "bulb not added as expected"
grep -q "Added device: id=3 type=window" "$OUT_FILE" || fail "window not added as expected"

grep -q "Linked device 2 to 1" "$OUT_FILE" || fail "link 2 -> 1 not reported"
grep -q "Linked device 3 to 1" "$OUT_FILE" || fail "link 3 -> 1 not reported"

grep -E -q "^2[[:space:]]+bulb[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$" "$OUT_FILE" || \
    fail "device 2 parent was not updated before deletion"

grep -E -q "^3[[:space:]]+window[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$" "$OUT_FILE" || \
    fail "device 3 parent was not updated before deletion"

grep -q "Deleted device: id=2" "$OUT_FILE" || fail "child device 2 was not deleted during cascade"
grep -q "Deleted device: id=3" "$OUT_FILE" || fail "child device 3 was not deleted during cascade"

LAST_LIST_LINE="$(grep -nE 'ID[[:space:]]+TYPE[[:space:]]+PID[[:space:]]+STATE[[:space:]]+PARENT[[:space:]]*$' "$OUT_FILE" | tail -n1 | cut -d: -f1)"
if [ -z "$LAST_LIST_LINE" ]; then
    fail "list header not found"
fi

TAIL_AFTER_LAST_LIST="$(tail -n +"$LAST_LIST_LINE" "$OUT_FILE")"

if echo "$TAIL_AFTER_LAST_LIST" | grep -E -q "^1[[:space:]]+hub[[:space:]]"; then
    fail "hub still appears in final list after cascade delete"
fi

if echo "$TAIL_AFTER_LAST_LIST" | grep -E -q "^2[[:space:]]+bulb[[:space:]]"; then
    fail "bulb still appears in final list after cascade delete"
fi

if echo "$TAIL_AFTER_LAST_LIST" | grep -E -q "^3[[:space:]]+window[[:space:]]"; then
    fail "window still appears in final list after cascade delete"
fi

pass