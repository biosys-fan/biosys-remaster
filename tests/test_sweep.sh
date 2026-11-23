#!/bin/bash
# Every page of the game entered and every hotspot clicked, headless (about half a minute), compared with the recorded behaviour.
# See tests/audit/README.md.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I tests/test_sweep.py
