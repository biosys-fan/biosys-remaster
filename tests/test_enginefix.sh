#!/bin/bash
# Engine fixes found by the game logic: idle count (exe:00416840), media freed before OnPageLoaded (exe:00401050), the top bar
# is opened by the game (exe:00450bd0), the commands 0x1b 0x40 0x5a 0x5d 0x66 0x68 0x6c 0x6d, verb 16 post-jump scroll.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I tests/test_enginefix.py
