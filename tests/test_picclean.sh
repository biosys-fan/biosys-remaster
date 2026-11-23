#!/bin/bash
# --picture clean (the light picture cleaner): the filter byte for byte against upscale-trial's reference.py, the default unchanged, headless captures
# against an independent Python composition, no hitches (worker threads on a fake clock), the flicker numbers of a full dawn and dusk, widescreen side
# areas, comparison images in build/shots/clean_*.bmp.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/picclean build/shots
exec python3 -I tests/test_picclean.py
