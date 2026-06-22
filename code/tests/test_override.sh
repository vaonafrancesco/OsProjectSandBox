#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="test_override"
OUT_FILE="$OUT_DIR/${TEST_NAME}.controller.out"
MANUAL_OUT="$OUT_DIR/${TEST_NAME}.manual.out"
CTRL_IN="$OUT_DIR/${TEST_NAME}.fifo"

fail() {
    echo "[FAIL] $1"
    if [ -f "$OUT_FILE" ]; then
        echo "----- controller output -----"
        cat "$OUT_FILE"
        echo "-----------------------------"
    fi
    if [ -f "$MANUAL_OUT" ]; then
        echo "----- manual client output -----"
        cat "$MANUAL_OUT"
        echo "--------------------------------"
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
: > "$OUT_FILE"
: > "$MANUAL_OUT"

mkfifo "$CTRL_IN" || fail "failed to create controller fifo"

./bin/domotics_controller < "$CTRL_IN" > "$OUT_FILE" 2>&1 &
CTRL_PID=$!

exec {WRITER_FD}> "$CTRL_IN" || fail "failed to open controller input fifo"

sleep 1

send_cmd "add hub"
sleep 1
send_cmd "add bulb"
sleep 1
send_cmd "add bulb"
sleep 2
send_cmd "link 2 to 1"
sleep 2
send_cmd "link 3 to 1"
sleep 2
send_cmd "switch 1 power on"
sleep 8
send_cmd "info 2"
sleep 6
send_cmd "info 3"
sleep 6

grep -q "Added device: id=1 type=hub" "$OUT_FILE" || fail "hub not added as expected"
grep -q "Added device: id=2 type=bulb" "$OUT_FILE" || fail "first bulb not added as expected"
grep -q "Added device: id=3 type=bulb" "$OUT_FILE" || fail "second bulb not added as expected"
grep -q "Linked device 2 to 1" "$OUT_FILE" || fail "link 2 -> 1 not reported"
grep -q "Linked device 3 to 1" "$OUT_FILE" || fail "link 3 -> 1 not reported"
grep -q "hub 1 switched on" "$OUT_FILE" || fail "hub switch on not confirmed"

./bin/manual_client 2 switch power off > "$MANUAL_OUT" 2>&1 || fail "manual override command failed"
grep -q "Manual command sent successfully to device 2" "$MANUAL_OUT" || fail "manual client did not confirm command"

sleep 3
send_cmd "info 2"
sleep 6
send_cmd "switch 1 power on"
sleep 8
send_cmd "info 2"
sleep 6
send_cmd "info 3"
sleep 6
send_cmd "exit"

exec {WRITER_FD}>&-
wait "$CTRL_PID" 2>/dev/null || true
unset CTRL_PID

grep -q "bulb id=2 state=off manual_override=true" "$OUT_FILE" || \
    fail "bulb 2 did not report manual override OFF state"

MATCH_BULB2_ON="$(grep -c "bulb id=2 state=on manual_override=false" "$OUT_FILE" || true)"
if [ "$MATCH_BULB2_ON" -lt 1 ]; then
    fail "bulb 2 did not return to ON with manual_override=false"
fi

MATCH_BULB3_ON="$(grep -c "bulb id=3 state=on manual_override=false" "$OUT_FILE" || true)"
if [ "$MATCH_BULB3_ON" -lt 1 ]; then
    fail "bulb 3 did not remain coherent during override recovery"
fi

pass