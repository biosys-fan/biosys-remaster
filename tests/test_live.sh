#!/bin/bash
# The game logic (build/biosys --game): start-up, clock, palettes, effects, ambient sounds, fruit, soak.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/live
exec python3 -I tests/test_live.py
