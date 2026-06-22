#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="test_linking"
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

assert_contains() {
    local pattern="$1"
    local message="$2"
    grep -E -q "$pattern" "$OUT_FILE" || fail "$message"
}

assert_not_contains() {
    local pattern="$1"
    local message="$2"
    if grep -E -q "$pattern" "$OUT_FILE"; then
        fail "$message"
    fi
}

bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true

cat > "$CMD_FILE" <<'EOF'
add hub
add bulb
add window
add hub
list
link 2 to 1
list
info 2
link 3 to 2
list
info 3
link 4 to 3
list
info 4
link 2 to 4
list
info 2
exit
EOF

CONTROLLER_STATUS=0
./bin/domotics_controller < "$CMD_FILE" > "$OUT_FILE" 2>&1 || CONTROLLER_STATUS=$?

[ "$CONTROLLER_STATUS" -eq 0 ] || fail "controller exited with non-zero status: $CONTROLLER_STATUS"

assert_contains "Added device: id=1 type=hub" "hub 1 not added as expected"
assert_contains "Added device: id=2 type=bulb" "bulb 2 not added as expected"
assert_contains "Added device: id=3 type=window" "window 3 not added as expected"
assert_contains "Added device: id=4 type=hub" "hub 4 not added as expected"

assert_contains "Linked device 2 to 1" "valid link 2 -> 1 not reported"
assert_contains "^2[[:space:]]+bulb[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+1$" \
    "device 2 parent was not updated to 1 after valid link"

# Verifica dettaglio del device dopo il link valido.
# Con l'output che hai mostrato, info 2 produce una riga tipo:
# bulb id=2 state=off manual_override=false time=0
# Se il parent NON compare ancora nel dettaglio, questo test ti evidenzia proprio il gap richiesto.
assert_contains "bulb id=2 .*parent=1|bulb id=2 .* parent 1|bulb id=2 .*parent: 1" \
    "device 2 detail does not show parent 1 after valid link"

assert_not_contains "Linked device 3 to 2" "invalid link 3 -> 2 unexpectedly succeeded"
assert_contains "Error: The selected devices are not compatible\." \
    "missing incompatible-devices error for link 3 -> 2"
assert_contains "^3[[:space:]]+window[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+0$" \
    "device 3 parent changed unexpectedly after invalid link 3 -> 2"

# Anche info 3 non deve mostrare un parent cambiato
assert_not_contains "window id=3 .*parent=2|window id=3 .* parent 2|window id=3 .*parent: 2" \
    "device 3 detail unexpectedly shows parent 2 after failed link"

assert_not_contains "Linked device 4 to 3" "invalid link 4 -> 3 unexpectedly succeeded"
assert_contains "Error: The selected devices are not compatible\." \
    "missing incompatible-devices error for link 4 -> 3"
assert_contains "^4[[:space:]]+hub[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+0$" \
    "device 4 parent changed unexpectedly after invalid link 4 -> 3"

assert_not_contains "hub id=4 .*parent=3|hub id=4 .* parent 3|hub id=4 .*parent: 3" \
    "device 4 detail unexpectedly shows parent 3 after failed link"

assert_contains "Linked device 2 to 4" "re-link 2 -> 4 was not accepted as expected by this implementation"
assert_contains "^2[[:space:]]+bulb[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+4$" \
    "device 2 parent was not updated to 4 after re-link"

assert_contains "bulb id=2 .*parent=4|bulb id=2 .* parent 4|bulb id=2 .*parent: 4" \
    "device 2 detail does not show parent 4 after re-link"

assert_not_contains "Command not valid\." "unexpected invalid command found in controller output"
assert_not_contains "Parse error\." "unexpected parse error found in controller output"

pass