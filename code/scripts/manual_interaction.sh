#!/usr/bin/env bash

set -u

ID="${1:-}"
CMD="${2:-}"
PARAM1="${3:-}"
PARAM2="${4:-}"

if [[ -z "$ID" || -z "$CMD" ]]; then
  echo "Usage: ./scripts/manual_interaction.sh <id> <command> [parameters]"
  exit 2
fi

./bin/manual_client "$ID" "$CMD" "$PARAM1" "$PARAM2"
EXIT_CODE=$?

case "$EXIT_CODE" in
  0) echo "Manual interaction completed successfully." ;;
  1) echo "Device not found." ;;
  2) echo "Invalid command." ;;
  3) echo "IPC communication failed." ;;
  4) echo "Invalid parameters." ;;
  *) echo "Manual interaction failed with code $EXIT_CODE." ;;
esac

exit "$EXIT_CODE"