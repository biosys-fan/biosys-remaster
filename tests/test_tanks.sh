#!/bin/bash
# The water tanks cabinet (CTanksCabinet dll:10004620): a drink from the tap, the shower; headless script run.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/tanks
exec python3 -I tests/test_tanks.py
