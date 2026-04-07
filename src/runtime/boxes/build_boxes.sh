#!/usr/bin/env bash
# build_boxes.sh — assemble bb_boxes.il into boxes.dll
#
# Usage:
#   bash src/runtime/boxes/build_boxes.sh
#   bash src/runtime/boxes/build_boxes.sh /path/to/output/boxes.dll
#
# Requires: ilasm (mono-complete)
# Output: boxes.dll (default: src/runtime/boxes/shared/boxes.dll)
#
# AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$SCRIPT_DIR/boxes.dll}"

IL="$SCRIPT_DIR/bb_boxes.il"
[ -f "$IL" ] || { echo "ERROR: $IL not found"; exit 1; }

echo "Assembling $IL → $OUT"
ilasm "$IL" /dll /output:"$OUT" /quiet

if [ $? -eq 0 ]; then
  echo "  OK  boxes.dll built: $OUT"
else
  echo "  FAIL  ilasm error — see output above"
  exit 1
fi
