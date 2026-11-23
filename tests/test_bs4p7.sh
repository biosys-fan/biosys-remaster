#!/bin/bash
# BioSys 4 power terminals (src/engine/bs4panel_term.cpp: termsav termoc termevo termrf termhouse), with the real game logic:
# opened by real clicks from the power page, the rows and totals against the game's FEATURE_POWER_USAGE values, the once-per-second poll,
# the controls of the panels, the real game headless. Frames: build/shots/bs4p7_<page>.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/p7 build/shots
exec python3 -I tests/test_bs4p7.py
