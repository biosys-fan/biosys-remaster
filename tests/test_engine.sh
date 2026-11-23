#!/bin/bash
# Engine core: new-game objects, pick-up / stash / swap / drop / wear / variation / fruit, bar, cursors.
cd "$(dirname "$0")/.." || exit 1
python3 -I tests/test_engine.py
