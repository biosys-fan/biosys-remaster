#!/bin/bash
# Menu, saved games, restart, text entry and the start-up flow of the engine (analysis/notes/menus_saveload.md).
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/menus
exec python3 -I tests/test_menus.py
