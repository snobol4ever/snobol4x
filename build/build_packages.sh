#!/usr/bin/env bash
# build_packages.sh — install system packages needed for snobol4ever development
# Idempotent. Safe to run multiple times.
# Usage: bash build/build_packages.sh
set -euo pipefail
PKGS="build-essential libgmp-dev m4 nasm libgc-dev wabt"
MISSING=""
for p in $PKGS; do
    dpkg -s "$p" &>/dev/null || MISSING="$MISSING $p"
done
if [ -n "$MISSING" ]; then
    apt-get install -y $MISSING 2>&1 | tail -3
    echo "OK  installed:$MISSING"
else
    echo "SKIP all packages already installed"
fi
