#!/bin/bash
# The BioSys 4 terminal inside the running game (src/engine/engine_bs4.cpp, bs4hooks.cpp): enter through verb 15 / a load, navigation and passwords
# with real clicks and keys, hover and cursor, click sounds, the clock, save / restart / load, leave. Frames: build/shots/bs4glue_*.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/glue build/shots
exec python3 -I tests/test_bs4glue.py
