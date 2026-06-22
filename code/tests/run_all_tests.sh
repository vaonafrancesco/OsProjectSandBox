#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

TESTS=(
  "tests/test_linking.sh"
  "tests/test_cycle_detection.sh"
  "tests/test_override.sh"
  "tests/test_timer_validation.sh"
  "tests/test_cascade_delete.sh"
  "tests/test_crash_handling.sh"
)

PASS_COUNT=0
FAIL_COUNT=0

echo "Running domotics test suite..."
echo

for t in "${TESTS[@]}"; do
    echo "==> ${t}"
    if bash "$t"; then
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
    echo
done

echo "Summary: ${PASS_COUNT} passed, ${FAIL_COUNT} failed"

if [ "$FAIL_COUNT" -ne 0 ]; then
    exit 1
fi

exit 0