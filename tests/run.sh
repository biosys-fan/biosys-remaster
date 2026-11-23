#!/bin/bash
# Runs every tests/test_*.sh from the port root. Each must exit 0.
cd "$(dirname "$0")/.." || exit 1
fail=0
for t in tests/test_*.sh; do
    [ -e "$t" ] || continue
    echo "== $t"
    if bash "$t"; then echo "   ok"; else echo "   FAILED: $t"; fail=1; fi
done
[ "$fail" -eq 0 ] && echo "ALL TESTS PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
