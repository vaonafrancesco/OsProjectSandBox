#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="test_crash_handling"
CTRL_OUT="$OUT_DIR/${TEST_NAME}.controller.out"
CTRL_IN="$OUT_DIR/${TEST_NAME}.fifo"

fail() {
    echo "[FAIL] $1"
    if [ -f "$CTRL_OUT" ]; then
        echo "----- controller output -----"
        cat "$CTRL_OUT"
        echo "-----------------------------"
    fi
    [ -n "${WRITER_FD:-}" ] && exec {WRITER_FD}>&- 2>/dev/null || true
    [ -n "${CTRL_PID:-}" ] && wait "$CTRL_PID" 2>/dev/null || true
    rm -f "$CTRL_IN"
    bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
    exit 1
}

pass() {
    echo "[PASS] $TEST_NAME"
    [ -n "${WRITER_FD:-}" ] && exec {WRITER_FD}>&- 2>/dev/null || true
    [ -n "${CTRL_PID:-}" ] && wait "$CTRL_PID" 2>/dev/null || true
    rm -f "$CTRL_IN"
    bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
    exit 0
}

send_cmd() {
    echo "$1" >&"$WRITER_FD" || fail "failed to send command: $1"
}

bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
rm -f "$CTRL_IN"
: > "$CTRL_OUT"

mkfifo "$CTRL_IN" || fail "failed to create controller fifo"

./bin/domotics_controller < "$CTRL_IN" > "$CTRL_OUT" 2>&1 &
CTRL_PID=$!

exec {WRITER_FD}> "$CTRL_IN" || fail "failed to open controller input fifo"

sleep 1

send_cmd "add hub"
sleep 1
send_cmd "add bulb"
sleep 2
send_cmd "link 2 to 1"
sleep 6
send_cmd "list"
sleep 3

grep -q "Added device: id=1 type=hub" "$CTRL_OUT" || fail "hub not added as expected"
grep -q "Added device: id=2 type=bulb" "$CTRL_OUT" || fail "bulb not added as expected"
grep -q "Linked device 2 to 1" "$CTRL_OUT" || fail "link 2 -> 1 not reported"

BULB_PID="$(awk '/^2[[:space:]]+bulb[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$/ {print $3}' "$CTRL_OUT" | tail -n1)"
[ -n "$BULB_PID" ] || fail "could not extract bulb pid from list output"

kill -9 "$BULB_PID" 2>/dev/null || fail "failed to SIGKILL bulb process"

sleep 4
send_cmd "info 1"
sleep 8
send_cmd "switch 1 power on"
sleep 10
send_cmd "list"
sleep 3
send_cmd "exit"

exec {WRITER_FD}>&-
wait "$CTRL_PID" 2>/dev/null || true
unset CTRL_PID

grep -q "\[pending\] info 1" "$CTRL_OUT" || fail "info 1 was not issued after child crash"
grep -q "\[pending\] switch 1 power on" "$CTRL_OUT" || fail "switch 1 power on was not issued after child crash"

if ! grep -Eq "hub id=1 state=|hub 1 switched on" "$CTRL_OUT"; then
    fail "hub did not produce any visible response after child crash"
fi

LAST_LIST_LINE="$(grep -nE 'ID[[:space:]]+TYPE[[:space:]]+PID[[:space:]]+STATE[[:space:]]+PARENT[[:space:]]*$' "$CTRL_OUT" | tail -n1 | cut -d: -f1)"
if [ -z "$LAST_LIST_LINE" ]; then
    fail "final list header not found"
fi

TAIL_AFTER_LAST_LIST="$(tail -n +"$LAST_LIST_LINE" "$CTRL_OUT")"

if echo "$TAIL_AFTER_LAST_LIST" | grep -E -q "^2[[:space:]]+bulb[[:space:]]"; then
    fail "crashed child still appears in final device list"
fi

if ! echo "$TAIL_AFTER_LAST_LIST" | grep -E -q "^1[[:space:]]+hub[[:space:]]"; then
    fail "hub disappeared after child crash"
fi

pass