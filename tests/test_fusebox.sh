#!/bin/bash
# The fuse box pages (CFuseBox dll:10026b30): the fuses in the holes, the blown ones and the lamps are shown (Display dll:10026d10), headless.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/fusebox
exec python3 -I tests/test_fusebox.py
