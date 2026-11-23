#!/bin/bash
# Autosave (rotation, when it is due and allowed, wake from sleep, quit, --load-autosave, F9), the pages of save slots, pause while the window
# is inactive, and the pure logic of all of it (build/biosys-apptest). Headless scripts on the injected clock; the window paths run with SDL's
# dummy video and audio drivers and test keys pushed as real SDL events.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/autosave
exec python3 -I tests/test_autosave.py
