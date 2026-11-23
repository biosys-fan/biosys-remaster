#!/bin/bash
# Engine defects found by the first playthrough of the ported game (keyed images under the game's palettes, user input after a
# journey, the right-click return page and its events, the DC redraw semantics, the popup flag rule, posted input).
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/defects/saves
exec python3 -I tests/test_defects.py
