#!/bin/bash
# The overlay interface (--interface overlay): layout arithmetic, pointer maps, the faithful default, panorama pixels against the oracle, overlay states, contrast,
# ambient fill, hit tests, the vertical release, the window code on the SDL dummy drivers. Writes pictures to build/shots.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/overlay build/shots
exec python3 -I tests/test_overlay.py
