#!/bin/bash
# API palette commands (SetPalette timed / immediate, FadeToColour, FadeToPalette, GetPalette), the cross-fade page
# jump and the page-image commands (DisplayImage, SetImageVisibility, slots, ClearWindow, FillScreen, JigglePage, page
# filter), all headless through `--script` / `api ...`; expected values computed independently in Python.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/palette_images
exec python3 -I tests/test_palette_images.py
