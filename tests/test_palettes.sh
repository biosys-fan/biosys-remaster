#!/bin/bash
# Validates data/palette_sets.tsv and data/page_palettes.tsv against PAGES.DAT and the game data.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I tests/test_palettes.py
