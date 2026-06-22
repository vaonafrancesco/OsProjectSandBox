#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

CTRL_OUT="$OUT_DIR/run_crash_scenario.controller.out"
CTRL_IN="$OUT_DIR/run_crash_scenario.fifo"

bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
rm -f "$CTRL_IN"
: > "$CTRL_OUT"

mkfifo "$CTRL_IN"

./bin/domotics_controller < "$CTRL_IN" > "$CTRL_OUT" 2>&1 &
CTRL_PID=$!
exec {WRITER_FD}> "$CTRL_IN"

send_cmd() {
  echo "$1" >&"$WRITER_FD"
}

sleep 1

send_cmd "add hub"
sleep 1
send_cmd "add bulb"
sleep 2
send_cmd "link 2 to 1"
sleep 6
send_cmd "list"
sleep 3

BULB_PID="$(awk '/^2[[:space:]]+bulb[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$/ {print $3}' "$CTRL_OUT" | tail -n1)"

if [ -n "${BULB_PID:-}" ]; then
  kill -9 "$BULB_PID" 2>/dev/null || true
fi

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
rm -f "$CTRL_IN"

echo "===== CONTROLLER OUTPUT ====="
cat "$CTRL_OUT"

bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true