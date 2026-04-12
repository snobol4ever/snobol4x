#!/usr/bin/env bash
# build_java.sh — ensure Java + Jasmin are available for the JVM backend
# Idempotent. Safe to run multiple times.
# Usage: bash build/build_java.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ONE4ALL="$ROOT/one4all"

# Java
if which javac &>/dev/null; then
    echo "SKIP javac already installed: $(javac -version 2>&1)"
else
    apt-get install -y openjdk-21-jdk-headless 2>&1 | tail -2
    which javac &>/dev/null || { echo "FAIL javac not found after install"; exit 1; }
    echo "OK  javac installed"
fi

# Jasmin
JASMIN="$ONE4ALL/src/backend/jasmin.jar"
[ -f "$JASMIN" ] || { echo "FAIL jasmin.jar missing at $JASMIN"; exit 1; }
echo "OK  jasmin.jar present: $JASMIN"
