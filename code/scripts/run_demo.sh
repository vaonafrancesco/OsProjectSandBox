#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SCENARIO="${3:-controller}"

case "$SCENARIO" in
  basic)
    exec bash scripts/run_basic_scenario.sh
    ;;
  override)
    exec bash scripts/run_override_scenario.sh
    ;;
  crash)
    exec bash scripts/run_crash_scenario.sh
    ;;
  controller)
    bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
    exec ./bin/domotics_controller
    ;;
  *)
    echo "Usage: ./scripts/run_demo.sh [basic|override|crash|controller]"
    exit 2
    ;;
esac