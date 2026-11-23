#!/bin/bash
# The first-run screen and the start-up logic around it: the SDL-free logic (build/biosys-firstruntest), the window without a display
# (SDL_VIDEODRIVER=dummy) with real drop events, the import of the two real CD zips, cancel at quit, the second start, the headless error.
# Needs build/biosys, build/biosys-import and the two zips (BIOSYS_DEV_ROOT or the folder above port/). Takes about a minute and a half;
# temporary files go to a fresh folder under $TMPDIR and are removed.
cd "$(dirname "$0")/.." || exit 1
python3 -I tests/test_firstrun.py
rc=$?
if [ "$rc" -eq 0 ]; then echo "FIRSTRUN TEST PASSED"; else echo "FIRSTRUN TEST FAILED"; fi
exit $rc
