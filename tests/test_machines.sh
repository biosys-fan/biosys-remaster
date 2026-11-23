#!/bin/bash
# Audit packet a (machines): objects used on machine hotspots, headless. Extinguishers on a fire, the poem1 pickup, goggles on a charger.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/machines
exec python3 -I tests/test_machines.py
