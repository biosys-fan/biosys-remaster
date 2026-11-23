#!/bin/bash
# BioSys 4 terminal packet P5 (device panels B + hint boxes: src/engine/bs4panel_algs / _cond / _oxy / _power / _hint.cpp, bs4panels_b.*):
# every panel and variant opened by real clicks on the real game logic (NewGame.sav), its gauges / readouts against the game's answers
# (also after the game state changed and the 1 Hz poll ran), every control by a real click or drag. Driver: build/biosys-bs4p5
# (src/tools/bs4p5.cpp). Frames: build/shots/bs4p5_*.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/shots
exec python3 -I tests/test_bs4p5.py
