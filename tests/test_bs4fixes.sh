#!/bin/bash
# The fixes of the BioSys 4 terminal (src/engine/bs4fixes.h, engine-side switch) and the game-side defects the terminal work found:
# one check per fix, off and on, through real clicks where a click triggers it. See FIXES.md section "Terminal".
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/bs4fixes
exec python3 -I tests/test_bs4fixes.py
