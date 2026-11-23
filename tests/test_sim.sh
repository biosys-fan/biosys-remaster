#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Simulation core (src/game/sim.*, biomecel/airflow/waterflow/watertank/plant): the C++ port against an independent
# Python re-implementation written from the decompiled functions (tests/sim_oracle.py), and property checks.
cd "$(dirname "$0")/.." || exit 1
BIN=build/biosys-simtest
SAV=$DEVROOT/setup-cd/installed/DATA1/NewGame.sav
[ -f "$SAV" ] || SAV=$DEVROOT/game-cd/files/NewGame.sav
T=build/test/sim
mkdir -p "$T"
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
PY="python3 -I tests/test_sim.py"
H=3600; D=86400; D5=432000

$BIN static "$SAV" > "$T/static.tsv"; check "static data of the world" $?

# (1) the C++ simulation equals the oracle on every member of the world, cels, tanks, flows, plants and timeline
# after 1 game hour, 1 game day and 5 game days: the Event_OnTimer schedule (cycles of 125 game seconds, dumps at the end
# of the first cycle that reaches the time) and the CTimeline::Advance schedule (90 s steps), two seeds, and stress
# scenarios that switch on every flow class (soil bed, bubbler, rain, mister, evaporation, drains), pollute the air and
# drain or flood the soil
compare() {   # name mode seed at extra-args...
    local name=$1 mode=$2 seed=$3 at=$4; shift 4
    local rng
    rng=$($BIN run "$SAV" --seed "$seed" --at 0 | grep -m1 'sim/world/rngState' | sed 's/.*= //')
    $BIN run "$SAV" --seed "$seed" --mode "$mode" --at "$at" "$@" > "$T/$name.cpp.txt"; check "C++ run $name" $?
    python3 -I tests/sim_oracle.py "$SAV" "$T/static.tsv" --rng "$rng" --mode "$mode" --at "$at" "$@" > "$T/$name.py.txt"; check "oracle run $name" $?
    out=$($PY compare "$T/$name.cpp.txt" "$T/$name.py.txt"); rc=$?
    echo "$out" | tail -n 3
    check "C++ equals oracle: $name" $rc
}
AT=$H,$D,$D5
compare timer_s1      timer   1 $AT
compare timer_s7      timer   7 $AT
compare advance_s1    advance 1 $AT
compare advance_s9    advance 9 $AT
compare flows_on      timer   1 $AT --flows on
compare flows_off     timer   2 $AT --flows off
compare polluted      timer   3 $D,$D5 --pollute 60 --drain 0.1
compare flooded       timer   4 $D,$D5 --drain 3
compare all_stress    advance 5 $D,$D5 --flows on --pollute 30 --drain 5

# (2) property checks
# no NaN / inf and every value inside the ranges the original clamps to, after 30 game days
$BIN run "$SAV" --at $D5,864000,2592000 > "$T/d30.txt"; check "30 game days run" $?
$PY props "$T/d30.txt"; check "30 days, baseline: finite values inside the clamps" $?
$BIN run "$SAV" --seed 11 --flows on --pollute 80 --drain 5 --at $D5,2592000 > "$T/d30x.txt"; check "30 game days run, every flow on, polluted" $?
$PY props "$T/d30x.txt"; check "30 days, every flow on, polluted, flooded: finite values inside the clamps" $?
grep -q '^sim/log/' "$T/d30.txt" "$T/d30x.txt"; [ $? -ne 0 ]; check "the simulation logged no warning in 30 days" $?

# totals the equations conserve: water (every flow moves what it takes) and air volume (two-way flows)
$BIN run "$SAV" --flows on --at 0,$D,$D5 > "$T/cons_on.txt"; check "conservation run (flows on)" $?
$PY conserve "$T/cons_on.txt" water; check "water of all tanks is conserved with every flow on" $?
$PY conserve "$T/cons_on.txt" air; check "air volume of all cels is conserved with every flow on" $?
$BIN run "$SAV" --flows off --at 0,$D,$D5 > "$T/cons_off.txt"; check "conservation run (flows off)" $?
$PY conserve "$T/cons_off.txt" water; check "water of all tanks is conserved with every flow off" $?
$PY conserve "$T/cons_off.txt" air; check "air volume of all cels is conserved with every flow off" $?

# the day / night temperature of a cel follows the outside temperature with the 12000 s time constant
AT2=0,125,250; for h in $(seq 2 2 96); do AT2="$AT2,$((h * 3600))"; done
$BIN run "$SAV" --flows off --cel PH:airTemp=40,soilTemp=40 --at "$AT2" > "$T/cycle.txt"; check "cycle run" $?
$PY cycle "$T/cycle.txt"; check "temperature of a cel follows the outside temperature (tau 12000 s)" $?

# plants: good conditions keep them healthy, dark polluted air and a drained soil hurt them
$BIN run "$SAV" --flows off --at 0,$H > "$T/pl_base.txt"; check "plants run (good conditions: no rain, no flooding)" $?
$BIN run "$SAV" --pollute 90 --at 0,$D > "$T/pl_poll.txt"; check "plants run (pollution 90 %)" $?
$BIN run "$SAV" --drain 0.05 --at 0,$D > "$T/pl_dry.txt"; check "plants run (drained soil)" $?
$PY plants "$T/pl_base.txt" "$T/pl_poll.txt" "$T/pl_dry.txt" "$T/static.tsv"; check "plants keep or lose health as the conditions say" $?

# determinism: the same seed gives byte-identical dumps, another seed another weather
$BIN run "$SAV" --seed 5 --at $D > "$T/det1.txt"; $BIN run "$SAV" --seed 5 --at $D > "$T/det2.txt"; $BIN run "$SAV" --seed 6 --at $D > "$T/det3.txt"
cmp -s "$T/det1.txt" "$T/det2.txt"; check "same seed, identical dump" $?
$PY differs "$T/det1.txt" "$T/det3.txt"; check "different seed, different weather" $?

# a save made after simulating loads, and saves again to the same bytes
$BIN run "$SAV" --at $D5 --roundtrip | tail -n 1 | grep -q '^ROUNDTRIP OK'; check "save after 5 game days reloads to the same bytes" $?

[ "$fail" -eq 0 ]
