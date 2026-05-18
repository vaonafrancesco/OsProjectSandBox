#!/usr/bin/env bash

set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

./bin/domotics_controller <<'EOF'
add bulb
list
info 1
switch 1 power on
info 1
del 1
exit
EOF