#!/bin/bash
# The preferences file (biosys.conf): parse / override / unknown keys / write-back through the SDL-free unit (build/biosys-apptest) and through
# the real option table (build/biosys --config FILE --write-config); scripts ignore the file unless --config names it.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/prefs
exec python3 -I tests/test_prefs.py
