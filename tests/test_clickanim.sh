#!/bin/bash
# Click interactions that start an animation (blocking object animations, page effects of machine buttons, close-up effects), shown the
# way the window shows them: fake 60 / 120 Hz displays (`window HZ 33`, src/app/pacing.h), the original and the smooth presentation,
# frames and timing against the independent TAN decoder, the sound of the animation, the smooth texture refresh.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test
exec python3 -I tests/test_clickanim.py
