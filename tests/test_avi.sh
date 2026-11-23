#!/bin/bash
# AVI reader / MS Video 1 decoder vs ffmpeg oracle (frames and audio) for all movies.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I tests/test_avi.py
