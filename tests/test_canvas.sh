#!/bin/bash
# GDI text and drawing: TrueType fonts (FreeType), Canvas (text, word wrap, lines, rectangles, palette mapping) and the
# API commands 0x38 0x39 0x3a 0x3b on the page surfaces. Samples: build/test/canvas/*.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/canvas
exec python3 -I tests/test_canvas.py
