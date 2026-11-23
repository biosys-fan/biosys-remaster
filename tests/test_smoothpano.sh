#!/bin/bash
# Smooth panoramas (optional high-resolution presentation): default unchanged, mesh mathematics, exact-cylinder bilinear oracle,
# overlays, continuous motion, comparison images in build/shots.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/smooth build/shots
exec python3 -I tests/test_smoothpano.py
