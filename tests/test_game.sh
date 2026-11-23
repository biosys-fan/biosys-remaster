#!/bin/bash
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
DEVROOT="${BIOSYS_DEV_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Game-logic framework: world built from the replay, blob round trip, rejection of bad blobs, typed members vs oracle.
cd "$(dirname "$0")/.." || exit 1
BIN=build/biosys-gametest
SAV=$DEVROOT/setup-cd/installed/DATA1/NewGame.sav
T=build/test/game
mkdir -p "$T"
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

# (a) items of the replayed world: counts and names, in order, as the oracle prints them
$BIN items "$SAV" > "$T/items.tsv"; check "items listing" $?
python3 -I tests/test_game.py names "$T/items.tsv" "$SAV"; check "item counts and names equal the oracle" $?

# (b) byte-identical round trip (the dump and round trip tools load with the original's two load bugs repaired)
out=$($BIN roundtrip "$SAV"); rc=$?
echo "$out"
check "roundtrip NewGame.sav" $rc

# (c) corrupted / truncated blobs are rejected with biosys::Error (exit status 2)
python3 -I tests/test_game.py corrupt "$SAV" "$T/bad"; check "corrupted copies written" $?
for f in "$T"/bad/*.sav; do
    out=$($BIN roundtrip "$f" 2>&1); rc=$?
    if [ "$rc" -eq 2 ] && echo "$out" | grep -q '^biosys::Error: '; then
        echo "PASS reject $(basename "$f"): ${out#biosys::Error: }"
    else
        echo "FAIL reject $(basename "$f"): exit $rc: $out"; fail=1
    fi
done

# (d) every typed item dumps exactly like the oracle (grows automatically as classes become typed)
$BIN dump "$SAV" > "$T/dump.cpp"; check "dump" $?
python3 -I tests/test_game.py dump "$T/dump.cpp" "$SAV"; check "typed items equal the oracle dump" $?

$BIN coverage > "$T/coverage.txt"; check "coverage" $?
tail -n 1 "$T/coverage.txt"
awk '$1=="TOTAL" { exit !($2 == $3 && $4 == 0) }' "$T/coverage.txt" && grep -q "raw 0," "$T/coverage.txt"; check "every item is typed (0 raw bytes)" $?

# (e) the post-constructor statements of the original builders are applied to a freshly built world, before any load.
# Expected values come from the decompiled builder functions (dllpack.py show), read by an independent Python parser.
python3 -I tests/test_game.py builder-queries "$BIN" "$T/q.txt" "$T/expect.tsv"; check "builder statements extracted" $?
$BIN fresh "$T/q.txt" > "$T/fresh.txt"; check "fresh world answers the queries" $?
python3 -I tests/test_game.py builder-compare "$T/fresh.txt" "$T/expect.tsv"; check "builder statements are applied to the fresh world" $?
python3 -I tests/test_game.py builder-deferred "$BIN" src/game/world_build.gen.cpp; check "deferred virtual calls are tagged, none untagged" $?
# the plants carry the fruit the food objects register in their constructors (CFood dll:10063520)
python3 -I tests/test_game.py fruit "$BIN" "$T"; check "food constructors register their fruit on the plants" $?

# (f) game random generator: the C runtime rand() (MSVC 4.2: seed*214013+2531011, bits 16..30), seed 1 unless srand'ed
[ "$($BIN rng)" = "41 18467 6334 26500 19169 15724 11478 29358 26962 24464" ]; check "rand() from seed 1 = 41 18467 6334 26500 19169 ..." $?
[ "$($BIN rng 12345)" = "$(python3 -I -c "
s=12345; o=[]
for i in range(10):
    s=(s*214013+2531011)&0xffffffff; o.append(str((s>>16)&0x7fff))
print(' '.join(o))")" ]; check "rand() after srand(12345)" $?
echo "feature fr1probe sensor" > "$T/q2.txt"
$BIN fresh "$T/q2.txt" > "$T/sensor.txt"; check "first sensor readings" $?
python3 -I tests/test_game.py sensor "$T/sensor.txt"; check "first sensor readings use RandPlusMinus in the original order" $?

# (g) the two ORIGINAL BUG load paths: by default a lung/hatch ignores the saved base block as the original does
out=$($BIN loadbugs "$SAV"); rc=$?
echo "$out"
check "load -> save differs from NewGame.sav only in the base blocks of lungs and hatches" $rc

# (i) typed links between the classes and the World's look-up functions
$BIN links "$SAV"; check "cross-class links (pump flows, ocean cel, rain machine, video messages, look-ups)" $?

# (h) two games in one process
$BIN worlds "$SAV"; check "two worlds in one process do not share state" $?
exit $fail
