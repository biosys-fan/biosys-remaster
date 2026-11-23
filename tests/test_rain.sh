#!/bin/bash
# The rain overlay (Event_OnBlitPage, dll:10022530): scripted frames of the raining start page against an independent statement of what must be drawn.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/rain
exec python3 -I tests/test_rain.py
