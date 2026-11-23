#!/bin/bash
# The remaster switch (src/game/fixes.h, FIXES.md): the parser and the presets, one focused check per flag (build/biosys-fixtest:
# the same situation with the flag off and on), and that the default is the faithful game (build/biosys-fixmeasure runs the world of
# NewGame.sav: no option, "faithful", "safe" and "cosmetic" must give the same numbers; "restored" must not). That the faithful
# game is the unchanged translation is covered by the suites of src/game (test_sim, test_feat, test_featstory, test_self,
# test_game, test_disp, test_story, test_play), which do not know the switch.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()   { echo "PASS $1"; }
bad()  { echo "FAIL $1"; fail=1; }

out=$(build/biosys-fixtest 2>&1); rc=$?
echo "$out" | grep -E "^(FAIL|note)" 
echo "$out" | tail -1
[ $rc -eq 0 ] && ok "biosys-fixtest: every flag, the parser and the presets" || bad "biosys-fixtest"
[ "$(echo "$out" | grep -c '^PASS')" -ge 100 ] && ok "biosys-fixtest ran the whole set (>= 100 checks)" || bad "biosys-fixtest ran fewer checks than expected"

M=build/biosys-fixmeasure
run() { "$M" --days "$1" --seed "$2" --fixes "$3" 2>&1; }
strip() { cut -f3-; }
a=$(run 5 1 faithful); b=$($M --days 5 --seed 1 2>&1); c=$(run 5 1 "")
[ -n "$a" ] && [ "$a" = "$b" ] && [ "$(echo "$a" | strip)" = "$(echo "$c" | strip)" ] && ok "fixmeasure: no option = faithful = empty list" || bad "fixmeasure: default differs from faithful"
for preset in safe cosmetic; do
    p=$(run 5 1 $preset)
    [ "$(echo "$p" | strip)" = "$(echo "$a" | strip)" ] && ok "fixmeasure: preset $preset gives the faithful numbers (days 0..5, seed 1)" || bad "fixmeasure: preset $preset changes the idle world"
done
# 30 days: the cold damage of the plants (the one fix that moves the idle world) shows in restored and in nothing else
f=$(run 30 2 faithful); r=$(run 30 2 restored); s=$(run 30 2 "safe,cosmetic")
[ "$(echo "$r" | strip)" != "$(echo "$f" | strip)" ] && ok "fixmeasure: restored changes the idle world within 30 days" || bad "fixmeasure: restored equals faithful"
[ "$(echo "$s" | strip)" = "$(echo "$f" | strip)" ] && ok "fixmeasure: safe,cosmetic equal faithful for 30 days (seed 2)" || bad "fixmeasure: safe,cosmetic differ"
one=$(run 30 2 plantColdDayMin)
[ "$(echo "$one" | strip)" = "$(echo "$r" | strip)" ] && ok "fixmeasure: plantColdDayMin alone gives the same world as restored (idle)" || bad "fixmeasure: restored has another effect than plantColdDayMin in the idle world"
rain_f=$($M --days 3 --seed 1 --rain 2>&1); rain_r=$($M --days 3 --seed 1 --rain --fixes rainWashesPollution 2>&1)
[ "$(echo "$rain_f" | strip)" != "$(echo "$rain_r" | strip)" ] && ok "fixmeasure --rain: rainWashesPollution changes the world" || bad "fixmeasure --rain: no effect of rainWashesPollution"
bad_flag=$($M --days 1 --fixes nosuchfix 2>&1); rc=$?
[ $rc -ne 0 ] && ok "fixmeasure: an unknown flag is refused" || bad "fixmeasure: accepted an unknown flag"

[ $fail -eq 0 ] && echo "fixes tests: all passed" || echo "fixes tests: FAILED"
exit $fail
