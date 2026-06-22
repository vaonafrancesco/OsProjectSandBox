#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

mkdir -p runtime/fifos runtime/pids runtime/logs runtime/registry

find runtime/fifos -type p -delete 2>/dev/null || true
find runtime/pids -type f -delete 2>/dev/null || true
find runtime/logs -type f -delete 2>/dev/null || true
find runtime/registry -type f -delete 2>/dev/null || true
find runtime/test_outputs -type f -delete 2>/dev/null || true

pkill -f "./bin/domotics_controller" 2>/dev/null || true
pkill -f "./bin/manual_client" 2>/dev/null || true

exit 0