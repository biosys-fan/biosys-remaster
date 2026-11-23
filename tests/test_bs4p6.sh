#!/bin/bash
# BioSys 4 air and water panels (src/engine/bs4panel_air.cpp, bs4panel_water.cpp, packet P6) with the real game logic against the independent rules of
# tests/bs4p6_oracle.py: opening by clicks, readouts and items for many game states, every routing control, the route checks, the 1 Hz poll.
# Frames: build/shots/bs4p6_<page>.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/shots
exec python3 -I tests/test_bs4p6.py
