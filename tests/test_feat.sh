#!/bin/bash
# The machines and sensors of the game logic (src/game feat_*.cpp): property checks, the parameter interface, and the independent
# re-implementation (tests/feat_oracle.py) written from the decompiled text.
cd "$(dirname "$0")/.." || exit 1
bin=build/biosys-feattest
[ -x "$bin" ] || { echo "FAIL: $bin is missing (make first)"; exit 1; }
fail=0
echo "-- property checks (power, fuses, flows of fans / pumps / rain machines, parameter ids)"
out=$(echo check | "$bin") || fail=1
echo "$out" | grep -E "^FAIL|passed" 
echo "-- parameter ids of the BS4 panels, reproducibility"
python3 -I tests/test_feat.py || fail=1
echo "-- oracle: dispatch table, SetParam, 1 game hour and 1 game day against the decompiled text"
python3 -I tests/feat_oracle.py | grep -v "^events" || fail=1
[ "${PIPESTATUS[0]}" -eq 0 ] || fail=1
exit $fail
