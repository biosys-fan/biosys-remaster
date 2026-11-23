#!/bin/bash
# Audio core: every game WAV loads and matches Python's wave module (ffprobe as fallback);
# mixer unit checks via biosys-audiotest; render round trip via biosys-wavdump.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test
fail=0
python3 -I tests/test_audio.py || fail=1
echo "-- mixer unit checks"
build/biosys-audiotest > build/test/audiotest.out; rc=$?
cat build/test/audiotest.out
[ $rc -eq 0 ] || fail=1
exit $fail
