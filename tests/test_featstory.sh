#!/bin/bash
# The story feature classes (feats_story2.cpp) through the live path: pages of the features, hotspots, the text they draw.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/featstory
exec python3 -I tests/test_featstory.py
