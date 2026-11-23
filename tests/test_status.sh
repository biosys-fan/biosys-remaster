#!/bin/bash
# The sim-status window and the two status screens (Tab: personal condition, Caps Lock: environment) with real key events.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/status/saves
exec python3 -I tests/test_status.py
