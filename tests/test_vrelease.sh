#!/bin/bash
# The vertical release of the captured pointer (DAT_004990cc, ended in the WM_MOUSEMOVE handler exe:00442de8..00442e2a): headless script run.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/vrelease
exec python3 -I tests/test_vrelease.py
