#!/bin/bash
# --widescreen: panorama pages use the window's full width (optional, off by default): the centre equals the 4:3 smooth picture pixel for pixel,
# the sides equal an independent exact-cylinder oracle, field of view cap, scroll limits, rain, the 4:3 fallback, pointer and window arithmetic,
# comparison images in build/shots.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/wide build/shots
exec python3 -I tests/test_widescreen.py
