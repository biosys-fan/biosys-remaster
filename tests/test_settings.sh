#!/bin/bash
# The realism and difficulty layer (src/game/settings.h, SETTINGS.md): the parser and the presets, one focused check per model flag and
# per tuning knob (build/biosys-settingstest), that realism faithful + difficulty original is the unchanged game (the other suites of
# src/game cover it bit for bit; here: the measurement tool gives the same numbers as biosys-fixmeasure and neutral knobs change nothing),
# that the realism levels move the world as stated (build/biosys-settingsmeasure), and that the scripted playthrough of test_play.sh
# runs to its end under two other combinations.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()  { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }

out=$(build/biosys-settingstest 2>&1); rc=$?
echo "$out" | grep -E "^(FAIL|note)"
echo "$out" | tail -1
[ $rc -eq 0 ] && ok "biosys-settingstest: parser, presets, every model flag, every knob" || bad "biosys-settingstest"
[ "$(echo "$out" | grep -c '^PASS')" -ge 80 ] && ok "biosys-settingstest ran the whole set (>= 80 checks)" || bad "biosys-settingstest ran fewer checks than expected"

M=build/biosys-settingsmeasure
# neutrality: the default, the explicit faithful/original, and every knob set to its original value give identical rows
a=$($M --days 4 --seed 2); b=$($M --days 4 --seed 2 --realism faithful --difficulty original)
tunes=$(python3 -I - <<'PY'
import re
src = open('src/game/settings.h').read()
for m in re.finditer(r'X\((\w+), (-?[\d.]+),', src.split('#define BIOSYS_TUNING(X)')[1]):
    print('--tune %s=%s' % (m.group(1), m.group(2)))
PY
)
c=$($M --days 4 --seed 2 $tunes)
strip() { cut -f3-; }
[ -n "$a" ] && [ "$(echo "$a" | strip)" = "$(echo "$b" | strip)" ] && ok "settingsmeasure: no option = faithful + original" || bad "settingsmeasure: default differs from faithful + original"
[ "$(echo "$a" | strip)" = "$(echo "$c" | strip)" ] && ok "settingsmeasure: all 24 knobs at their original value change nothing" || bad "settingsmeasure: a neutral knob changes the world"
# the idle world of settingsmeasure equals the idle world of fixmeasure in the columns they share
f=$(build/biosys-fixmeasure --days 4 --seed 2)
a0=$($M --days 4 --seed 2 --no-collapse)     # fixmeasure's idle player never passes out; the tool's does (below), so compare with it switched off
if python3 -I - "$f" "$a0" <<'PY'
import sys
def parse(t):
    rows = [l.split('\t') for l in t.strip().split('\n')]
    return rows[0], rows[1:]
h1, r1 = parse(sys.argv[1]); h2, r2 = parse(sys.argv[2])
common = [c for c in h1 if c in h2 and c not in ('seed', 'fixes', 'settings')]
bad = [(c, i) for c in common for i in range(len(r1)) if r1[i][h1.index(c)] != r2[i][h2.index(c)]]
sys.exit(1 if bad or len(common) < 60 else 0)
PY
then ok "settingsmeasure --scenario idle reproduces biosys-fixmeasure (every shared column, days 0..4)"; else bad "settingsmeasure differs from fixmeasure"; fi

# the realism levels
cor=$($M --days 12 --seed 1 --realism corrected | strip); fa=$($M --days 12 --seed 1 | strip)
[ "$cor" != "$fa" ] && ok "corrected changes the world within 12 days" || bad "corrected equals faithful"
cl=$($M --days 12 --seed 1 --realism closedloop | strip)
[ "$cl" != "$cor" ] && ok "closedloop differs from corrected" || bad "closedloop equals corrected"
hum() { $M --days 5 --seed 1 --realism "$1" | python3 -I -c "
import sys
rows=[l.rstrip('\n').split('\t') for l in sys.stdin]
h=rows[0]
print(rows[-1][h.index('rainforest.humidity')])"; }
python3 -I -c "import sys; sys.exit(0 if float('$(hum closedloop)') > 50 and float('$(hum faithful)') < 10 else 1)" && ok "closedloop: the rainforest still has humidity on day 5 (faithful: dry)" || bad "closedloop: rainforest humidity"
o2() { $M --days 20 --seed 1 --scenario frozen --realism "$1" | python3 -I -c "
import sys
rows=[l.rstrip('\n').split('\t') for l in sys.stdin]
h=rows[0]
print(rows[-1][h.index('rainforest.o2')])"; }
python3 -I -c "import sys; sys.exit(0 if float('$(o2 closedloop)') > float('$(o2 faithful)') + 5 else 1)" && ok "closedloop, machines off: the rainforest loses its oxygen more slowly (dead plants breathe less)" || bad "closedloop, machines off: oxygen"
bad_opt=$($M --realism nonsense --days 1 2>&1); rc=$?
[ $rc -ne 0 ] && ok "settingsmeasure: an unknown realism level is refused" || bad "accepted an unknown realism"
for opt in "--difficulty easy" "--fixes nosuchfix"; do
    $M $opt --days 1 >/dev/null 2>&1 && bad "accepted $opt" || ok "settingsmeasure: $opt is refused"
done

# the sensible player lives 8 days under every combination
for r in faithful corrected closedloop; do for d in guided original survivor; do
    alive=$($M --days 8 --seed 1 --realism "$r" --difficulty "$d" --player sensible --start | python3 -I -c "
import sys
rows=[l.rstrip('\n').split('\t') for l in sys.stdin]
h=rows[0]
print(rows[-1][h.index('player.alive')], rows[-1][2])")
    [ "$alive" = "1 8" ] && ok "sensible player, $r / $d: alive on day 8" || bad "sensible player, $r / $d: $alive"
done; done

# the front end
help=$(build/biosys --help 2>&1)
for o in "--realism" "--difficulty" "--fixes"; do echo "$help" | grep -q -- "$o" && ok "--help lists $o" || bad "--help lacks $o"; done
echo "$help" | grep -- "--fixes" | grep -q "developer" && ok "--help marks --fixes as a developer option" || bad "--help: --fixes not marked"
build/biosys --game --mute --realism nonsense --script "advance 10" >/dev/null 2>&1 && bad "biosys accepted --realism nonsense" || ok "biosys refuses --realism nonsense"

# the scripted playthrough of test_play.sh under two other combinations (the route must not change)
for combo in "closedloop survivor" "corrected guided"; do
    set -- $combo
    mkdir -p build/test/play/saves; rm -f build/test/play/saves/*.sav
    pout=$(python3 -I tests/settings_play.py "$1" "$2" 2>&1); prc=$?
    # the numbers may differ (guided starts with 90 nutrition: a fruit of 20 cannot raise it by 20); every other check must pass:
    # the route, the pages reached, no death
    other=$(echo "$pout" | grep -E "^FAIL" | grep -v "eating raised the nutrition")
    echo "$other" | head -5
    echo "$pout" | tail -1
    npass=$(echo "$pout" | grep -c "^PASS")
    [ -z "$other" ] && [ "$npass" -ge 25 ] && ok "test_play playthrough under --realism $1 --difficulty $2 runs to its end ($npass checks pass; only the nutrition number may differ)" || bad "test_play playthrough under $1 / $2"
done

[ $fail -eq 0 ] && echo "settings tests: all passed" || echo "settings tests: FAILED"
exit $fail
