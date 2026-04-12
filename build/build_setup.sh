#!/usr/bin/env bash
# build_setup.sh — full environment bootstrap for snobol4ever development
#
# Runs all build_*.sh scripts in dependency order for a complete environment.
# Each script is idempotent — safe to run multiple times.
# For goal-specific builds, run only the scripts listed in the REPO file.
#
# Usage: bash build/build_setup.sh
set -euo pipefail
BUILD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

run() { echo ""; echo "════ $1 ════"; bash "$BUILD/$1"; }

run build_packages.sh
run build_csnobol4.sh
run build_spitbol.sh
run build_scrip.sh
run build_java.sh
run build_monitor_ipc.sh

echo ""
echo "Setup complete. All components built."
echo "  csnobol4: $(cd "$BUILD/../../csnobol4" && pwd)/snobol4"
echo "  spitbol:  $(cd "$BUILD/../../x64" && pwd)/bin/sbl"
echo "  scrip:    $(cd "$BUILD/.." && pwd)/scrip"
