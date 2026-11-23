#!/bin/bash
# Enhanced sound path (resampler, no clicks), volume groups, mute, volume keys; original mode stays bit-identical.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test
exec python3 -I tests/test_audioenh.py
