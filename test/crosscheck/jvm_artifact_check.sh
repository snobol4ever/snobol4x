#!/bin/bash
# jvm_artifact_check.sh — MANDATORY end-of-session artifact check for JVM backend.
# Run this before every commit that touches emit_byrd_jvm.c or any .sno→.j path.
# Exits nonzero if any artifact changed but was not updated.
set -e
cd "$(dirname "$0")/../.."

JASMIN=src/backend/jvm/jasmin.jar
NULL_SNO=test/frontend/snobol4/null.sno
ROMAN=/home/claude/corpus/benchmarks/roman.sno
WORDCOUNT=/home/claude/corpus/crosscheck/strings/wordcount.sno

TMPD=$(mktemp -d)
trap "rm -rf $TMPD" EXIT

./sno2c -jvm $NULL_SNO      > $TMPD/null_new.j
./sno2c -jvm $ROMAN         > $TMPD/roman_new.j
./sno2c -jvm $WORDCOUNT     > $TMPD/wordcount_new.j

# Verify all three assemble clean
java -jar $JASMIN $TMPD/null_new.j      -d $TMPD/ 2>/dev/null || { echo "FAIL: hello_prog.j does not assemble"; exit 1; }
java -jar $JASMIN $TMPD/roman_new.j     -d $TMPD/ 2>/dev/null || { echo "FAIL: roman.j does not assemble";     exit 1; }
java -jar $JASMIN $TMPD/wordcount_new.j -d $TMPD/ 2>/dev/null || { echo "FAIL: wordcount.j does not assemble"; exit 1; }

CHANGED=0
diff -q $TMPD/null_new.j      artifacts/jvm/hello_prog.j         > /dev/null 2>&1 || { cp $TMPD/null_new.j      artifacts/jvm/hello_prog.j;         echo "UPDATED: hello_prog.j";    CHANGED=1; }
diff -q $TMPD/roman_new.j     artifacts/jvm/samples/roman.j      > /dev/null 2>&1 || { cp $TMPD/roman_new.j     artifacts/jvm/samples/roman.j;      echo "UPDATED: roman.j";         CHANGED=1; }
diff -q $TMPD/wordcount_new.j artifacts/jvm/samples/wordcount.j  > /dev/null 2>&1 || { cp $TMPD/wordcount_new.j artifacts/jvm/samples/wordcount.j;  echo "UPDATED: wordcount.j";     CHANGED=1; }

if [ $CHANGED -eq 1 ]; then
    git add artifacts/jvm/hello_prog.j artifacts/jvm/samples/roman.j artifacts/jvm/samples/wordcount.j
    echo "ARTIFACTS STAGED — include in your commit."
    exit 1  # nonzero: forces caller to notice and recommit
else
    echo "JVM artifacts unchanged — OK."
fi
