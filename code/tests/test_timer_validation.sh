#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="test_timer_validation"
CTRL_OUT="$OUT_DIR/${TEST_NAME}.controller.out"
CTRL_IN="$OUT_DIR/${TEST_NAME}.fifo"
MANUAL_OUT="$OUT_DIR/${TEST_NAME}.manual.out"

fail() {
    echo "[FAIL] $1"
    if [ -f "$CTRL_OUT" ]; then
        echo "----- controller output -----"
        cat "$CTRL_OUT"
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
: > "$CTRL_OUT"
: > "$MANUAL_OUT"

mkfifo "$CTRL_IN" || fail "failed to create controller fifo"

./bin/domotics_controller < "$CTRL_IN" > "$CTRL_OUT" 2>&1 &
CTRL_PID=$!

exec {WRITER_FD}> "$CTRL_IN" || fail "failed to open controller input fifo"

sleep 1
send_cmd "add timer"
sleep 1
send_cmd "add bulb"
sleep 2
send_cmd "link 2 to 1"
sleep 6

grep -q "Added device: id=1 type=timer" "$CTRL_OUT" || fail "timer not added as expected"
grep -q "Added device: id=2 type=bulb" "$CTRL_OUT" || fail "bulb not added as expected"
grep -q "Linked device 2 to 1" "$CTRL_OUT" || fail "link 2 -> 1 not reported"

./bin/manual_client 1 set begin 99:99 > "$MANUAL_OUT" 2>&1 || true
grep -q "Manual command sent successfully to device 1" "$MANUAL_OUT" || fail "manual set begin did not reach timer"

sleep 3
send_cmd "info 1"
sleep 6

if grep -q "begin=99:99" "$CTRL_OUT"; then
    fail "timer accepted invalid time format 99:99"
fi

./bin/manual_client 1 set begin 23:00 >> "$MANUAL_OUT" 2>&1 || true
./bin/manual_client 1 set end 08:00 >> "$MANUAL_OUT" 2>&1 || true

sleep 3
send_cmd "info 1"
sleep 6

grep -q "timer id=1 state=off begin=23:00 end=08:00" "$CTRL_OUT" || \
    fail "timer did not accept overnight schedule 23:00 -> 08:00"

send_cmd "exit"
exec {WRITER_FD}>&-
wait "$CTRL_PID" 2>/dev/null || true
unset CTRL_PID

pass