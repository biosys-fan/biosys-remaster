#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Compare biosys-dump against the Python reference parser.
cd "$(dirname "$0")/.." || exit 1
REF=$DEVROOT/analysis/scripts/biosys_pages.py
T=build/test
mkdir -p "$T"
fail=0
check() {  # name, status
    if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi
}
for f in $DEVROOT/game-cd/files/BIODATA/PAGES.DAT \
         $DEVROOT/setup-cd/installed/DATA1/Biodata/BIOSYS4.DAT; do
    n=$(basename "$f")
    python3 -I "$REF" "$f" list > "$T/$n.list.py"
    build/biosys-dump list "$f" > "$T/$n.list.cpp"
    cmp -s "$T/$n.list.py" "$T/$n.list.cpp"; check "$n list" $?
    python3 -I tests/golden_pages.py "$f" > "$T/$n.dump.py"
    build/biosys-dump dump "$f" > "$T/$n.dump.cpp"
    cmp -s "$T/$n.dump.py" "$T/$n.dump.cpp"; check "$n dump ($(wc -l < "$T/$n.dump.cpp" | tr -d ' ') lines)" $?
done
build/biosys-dump export $DEVROOT/game-cd/files/BIODATA/PAGES.DAT R4 0 "$T/r4.bmp" \
    $DEVROOT/game-cd/files/BIODATA/PALETTES/RFOREST/RF_DAY.PAL > /dev/null
python3 -I - "$T/r4.bmp" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
w, h = struct.unpack_from('<ii', d, 18)
bpp, = struct.unpack_from('<H', d, 28)
sys.exit(0 if (d[:2], w, h, bpp) == (b'BM', 2560, 1024, 8) else 1)
PY
check "export R4 image 0 is 2560x1024x8 BMP" $?
exit $fail
