#!/usr/bin/env bash

set +e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

find runtime/fifos -type p -delete 2>/dev/null
find runtime/pids -type f -delete 2>/dev/null
find runtime/logs -type f -delete 2>/dev/null
find runtime/registry -type f -delete 2>/dev/null

pkill -f domotics_controller 2>/dev/null
pkill -f manual_client 2>/dev/null

exit 0