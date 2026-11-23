#!/bin/bash
# The sleep rules: rain cuts a sleep short on an unsheltered spot (decompile dll:10065e80), a roof does not.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/sleep
exec python3 -I tests/test_sleeprule.py
