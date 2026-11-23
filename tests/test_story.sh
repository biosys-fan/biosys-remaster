#!/bin/bash
# The input handling, travel and story of the game logic (build/biosys --game): hotspot dispatch, paths, milestones, cheats,
# the SubTech countdown, the ending, lifts and the pod passwords, a 10-minute soak.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/story
exec python3 -I tests/test_story.py
