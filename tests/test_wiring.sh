#!/bin/bash
# Media systems wired into the engine: blocking object animations, verb 4, page-relative / fixed sprites,
# movies, layer order. (The full suite itself is run by tests/run.sh.)
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/wire
exec python3 -I tests/test_wiring.py
