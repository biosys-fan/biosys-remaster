#!/bin/bash
# The pace of the window: the engine's idle passes and the steps of the blocking animations run at a fixed interval on the real clock,
# whatever the display's refresh rate (fake clock and fake displays: script command `window HZ [PASS_MS]`, src/app/pacing.h).
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test
exec python3 -I tests/test_pacing.py
