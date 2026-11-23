#!/bin/bash
# Picture stability: sequences of presented frames (faithful 640 x 480 and the player's --picture clean --interface overlay on 3440 x 1440) checked for
# flicker, colour blips, wrong palettes, geometry jumps and pictures that move when they should be still. About 3 minutes. See tests/audit/visual.py.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/visual build/audit/visual
exec python3 -I tests/test_visual.py "$@"
