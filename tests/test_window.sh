#!/bin/bash
# The game window without a window: key translation, the Caps Lock / backquote hold of the status screens, the developer-key
# option, full-screen arithmetic (build/biosys-windowtest) and the settings stored next to saved games (build/biosys --game --script).
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/window
exec python3 -I tests/test_window.py
