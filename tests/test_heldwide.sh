#!/bin/bash
# Held objects and the status screen in the wide pictures (--widescreen, --interface overlay): headless script runs.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/heldwide build/shots
exec python3 -I tests/test_heldwide.py
