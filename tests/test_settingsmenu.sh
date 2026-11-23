#!/bin/bash
# The game mode inside the save file, the New game screen and the settings menu (F10): model, input, drawing, preferences, window path.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/settingsmenu build/shots
exec python3 -I tests/test_settingsmenu.py
