#!/bin/bash
# Cylindrical panorama projection (exe:0040cf00 / exe:0041a03c / exe:00403470) and the zoom-in jump (exe:004449f0):
# the C++ view against tests/projection_oracle.py (written independently from the decompiled code).
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test
exec python3 -I tests/test_projection.py
