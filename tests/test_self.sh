#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Typed player record (src/game/self.*): members equal the oracle dump, 600 bytes round-trip, bad data rejected.
cd "$(dirname "$0")/.." || exit 1
SAV=$DEVROOT/setup-cd/installed/DATA1/NewGame.sav
T=build/test/self
mkdir -p "$T"
fail=0
build/biosys-playertest dump "$SAV" > "$T/self.dump" || fail=1
python3 -I tests/test_self.py "$T/self.dump" "$SAV" || fail=1
build/biosys-playertest check "$SAV" | tee "$T/check.txt" || fail=1
grep -q '^FAIL' "$T/check.txt" && fail=1
build/biosys-playertest objects | tee "$T/objects.txt" || fail=1
grep -q '^FAIL' "$T/objects.txt" && fail=1
# behaviour: the scenario scripts against the independent Python oracle, clamps, death causes, symptom texts, object events
mkdir -p build/test/self
python3 -I tests/test_self_props.py | tee build/test/self/props.txt || fail=1
exit $fail
