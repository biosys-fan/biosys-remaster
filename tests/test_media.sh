#!/bin/bash
# Media systems (sounds, sprite animations, AVI movies) via the headless driver biosys-mediatest.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test
exec python3 -I tests/test_media.py
