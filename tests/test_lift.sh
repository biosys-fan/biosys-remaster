#!/bin/bash
# A ride in the rainforest lift (CLift dll:10023660): on arrival the lift's page is painted again (Eng_RedrawPage), headless script run.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/lift
exec python3 -I tests/test_lift.py
