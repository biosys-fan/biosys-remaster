#!/bin/bash
# Idle survival (a player who does nothing) on the three difficulties: the measuring tool (build/biosys-settingsmeasure) and the live game
# (build/biosys --game) agree. Both pass out at energy 0 (the full CSelf::Update), the rain machines' flag cuts a sleep in the open short
# (CSelf::OnIdle, wake reason 7); a night's length is random (CSelf::Sleep), so guided is a range, not one number.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/survival
exec python3 -I tests/test_survival.py
