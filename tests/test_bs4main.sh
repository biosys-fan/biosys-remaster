#!/bin/bash
# BioSys 4 terminal, main-page dynamics (src/engine/bs4main_*.cpp, packet P3): health / pod / menuscreen / probe pages / device-highlight overlays /
# alarm icons / mail marks against the independent oracle (tests/bs4main_oracle.py), with the scripted host and with the real game logic.
# Frames: build/shots/bs4main_*.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/bs4main build/shots
exec python3 -I tests/test_bs4main.py
