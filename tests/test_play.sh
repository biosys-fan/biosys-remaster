#!/bin/bash
# The playthrough: the first part of a game played with real input events (click, key, type) in the headless engine with the game logic
# attached (build/biosys --game): wake on R4, walk, pick up and read things, eat, drink, operate a machine panel, enter another biome,
# the cheat words, the goggles, sleep. Screenshots in build/test/play/.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/play/saves
rm -f build/test/play/saves/*.sav
exec python3 -I tests/test_play.py
