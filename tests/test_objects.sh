#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Compare biosys-objdump against the Python reference parsers (OBJECTS.DAT, TOUCHINF.DAT, NewGame.sav).
cd "$(dirname "$0")/.." || exit 1
D=$DEVROOT
OBJ=$D/setup-cd/installed/DATA1/Biodata/OBJECTS.DAT
TCH=$D/game-cd/files/BIODATA/TOUCHINF.DAT
SAV=$D/setup-cd/installed/DATA1/NewGame.sav
PAL=$D/game-cd/files/BIODATA/PALETTES/RFOREST/RF_DAY.PAL
T=build/test
mkdir -p "$T"
fail=0
check() {  # name, status
    if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi
}
for spec in "objects:$OBJ:OBJECTS.DAT" "touchinf:$TCH:TOUCHINF.DAT" "save:$SAV:NewGame.sav"; do
    IFS=: read -r cmd file name <<< "$spec"
    python3 -I tests/golden_objects.py "$cmd" "$file" > "$T/$name.dump.py" 2> "$T/$name.dump.py.err"
    build/biosys-objdump "$cmd" "$file" > "$T/$name.dump.cpp"
    cmp -s "$T/$name.dump.py" "$T/$name.dump.cpp"
    check "$name dump ($(wc -l < "$T/$name.dump.cpp" | tr -d ' ') lines)" $?
done
n=$(grep -c '^img .* crc=' "$T/OBJECTS.DAT.dump.cpp")
[ "$n" -gt 1800 ]; check "OBJECTS.DAT image lines with crc ($n)" $?
grep -q 'SELF-CHECK FAILED' "$T"/*.dump.cpp "$T"/*.dump.py; [ $? -ne 0 ]; check "reference scripts self-checks" $?

# export: C++ (1-based image number) must give the same BMP as the Python reference (0-based index);
# goggles image 1 is in the resident area, image 10 in the on-demand area
for n in 1 10; do
    python3 -I $D/analysis/scripts/biosys_objects.py "$OBJ" export goggles $((n - 1)) "$T/goggles$n.py.bmp" "$PAL" > /dev/null
    build/biosys-objdump export "$OBJ" GOGGLES $n "$T/goggles$n.cpp.bmp" "$PAL" > /dev/null
    cmp -s "$T/goggles$n.py.bmp" "$T/goggles$n.cpp.bmp"; check "export goggles image $n BMP identical to reference" $?
done

# corrupt input must be rejected with an error, not a crash
head -c 100 "$OBJ" > "$T/trunc.dat"
build/biosys-objdump objects "$T/trunc.dat" > /dev/null 2>&1; [ $? -eq 1 ]; check "truncated OBJECTS.DAT rejected" $?
head -c 1000 "$SAV" > "$T/trunc.sav"
build/biosys-objdump save "$T/trunc.sav" > /dev/null 2>&1; [ $? -eq 1 ]; check "truncated .sav rejected" $?
exit $fail
