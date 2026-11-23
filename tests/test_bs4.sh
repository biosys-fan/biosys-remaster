#!/bin/bash
# BioSys 4 terminal (src/engine/bs4*.cpp, src/tools/bs4test.cpp): static pictures against the independent Python oracle (tests/bs4_oracle.py),
# the built-in checks, navigation with real clicks, the ported panel driven by the real game logic. Frames: build/shots/bs4_*.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/bs4 build/shots
exec python3 -I tests/test_bs4.py
