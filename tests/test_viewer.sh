#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Headless rendering checks for the page viewer (--screenshot, --selftest-all-pages).
cd "$(dirname "$0")/.." || exit 1
T=build/test
mkdir -p "$T"
PAGES=$DEVROOT/game-cd/files/BIODATA/PAGES.DAT
PAL=$DEVROOT/game-cd/files/BIODATA/PALETTES/RFOREST/RF_DAY.PAL
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

build/biosys-dump export "$PAGES" R4 2 "$T/v_r4.bmp" "$PAL" > /dev/null
build/biosys --no-objects --page R4 --slot day --scroll 0,0 --screenshot "$T/v_a.bmp"
python3 -I tests/viewer_check.py proj "$T/v_a.bmp" "$T/v_r4.bmp" 0 0
check "R4 scroll 0,0 is the cylindrical projection of the HEALTHY image (2)" $?

build/biosys --no-objects --page R4 --slot day --scroll 2400,0 --screenshot "$T/v_b.bmp"
python3 -I tests/viewer_check.py proj "$T/v_b.bmp" "$T/v_r4.bmp" 2400 0
check "R4 scroll 2400,0 is the projection across the seam (2400..2559 then 0..)" $?

build/biosys-dump export "$PAGES" menutop 0 "$T/v_menu.bmp" "$PAL" > /dev/null
build/biosys --no-objects --page menutop --screenshot "$T/v_c.bmp"
python3 -I tests/viewer_check.py full "$T/v_c.bmp" "$T/v_menu.bmp"
check "menutop is a full-screen 640x480 copy of image 0" $?

out=$(build/biosys --selftest-all-pages); rc=$?
echo "$out" | tail -1
[ $rc -eq 0 ]; check "all pages render" $?
exit $fail
