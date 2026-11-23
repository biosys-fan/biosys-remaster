#!/bin/bash
# Clicks that looked dead in the interaction sweep (packet e): the Y hotspots (use something here) with the right tool in the hand,
# the oxygen cylinder's yCylinder, the alarm clock's page. Headless.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/clicks_e
exec python3 -I tests/test_clicks_e.py
