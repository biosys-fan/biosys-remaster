#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Compare biosys-tandump against the Python reference parser over all 292 .TAN files.
cd "$(dirname "$0")/.." || exit 1
ROOT=$DEVROOT
T=build/test
mkdir -p "$T"
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

files=()
while IFS= read -r f; do files+=("${f#$ROOT/}"); done < <(
    find "$ROOT/game-cd/files/BIOTIME/ANIMS" "$ROOT/setup-cd/installed/DATA1/Biotime/Anims" \
        -type f -iname '*.tan' | LC_ALL=C sort)
[ "${#files[@]}" -eq 292 ]; check "found 292 TAN files (${#files[@]})" $?

(cd "$ROOT" && python3 -I port/tests/golden_tan.py "${files[@]}") > "$T/tan.py" 2> "$T/tan.py.err"
check "golden_tan.py ran" $?
(cd "$ROOT" && port/build/biosys-tandump info "${files[@]}") > "$T/tan.cpp" 2> "$T/tan.cpp.err"
check "biosys-tandump info ran" $?
cmp -s "$T/tan.py" "$T/tan.cpp"; check "info+frame CRCs match reference ($(wc -l < "$T/tan.cpp" | tr -d ' ') lines)" $?
[ "$(grep -c MISSING "$T/tan.cpp")" -gt 0 ] && grep -q MISSING "$T/tan.py"; check "truncated co2nouse.tan reports MISSING frames" $?

f="$ROOT/${files[0]}"
build/biosys-tandump export "$f" 0 "$T/tan0.bmp"
python3 -I - "$T/tan0.bmp" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
w, h = struct.unpack_from('<ii', d, 18)
bpp, = struct.unpack_from('<H', d, 28)
sys.exit(0 if (d[:2], bpp) == (b'BM', 8) and w > 0 and h > 0 else 1)
PY
check "export frame 0 is an 8bpp BMP" $?
exit $fail
