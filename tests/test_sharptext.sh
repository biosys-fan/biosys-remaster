#!/bin/bash
# Sharp text (--text sharp): default unchanged, the run list and the text-free frame reproduce the original frame, sharp output at 2x / 3x,
# glyph placement against a Python reading of the run list, comparison images in build/shots.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/sharptext build/shots
exec python3 -I tests/test_sharptext.py
