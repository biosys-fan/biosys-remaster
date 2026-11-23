# The audit: does every part of the game do what the original does?

Unit tests check what somebody thought of. The audit goes the other way: it enumerates **everything** — every routine of the
original game DLL, every page, every hotspot — and reports each one that is missing, silent, broken or changed. Nothing of the
original is executed; its decompiler listing (`analysis/decompiled/BiosGame.dll.c`) is read as text.

| Suite | What it enumerates | Finds | Time |
|---|---|---|---|
| `tests/test_vtables.sh` | every virtual method of the original's classes | a method nobody ported | 1 s |
| `tests/test_parity.sh` | every routine of the original (2113 in the game part) against its translation | a routine nobody ported; a translation that never uses a picture / sound / animation name the original uses, or never calls a routine the original calls | 5 s |
| `tests/test_visual.sh` | sequences of presented frames: 640 x 480 faithful, and the player's `--picture clean --interface overlay` on 3440 x 1440, in ~560 scenarios (every page idle, held objects, Tab, turning, strip, menu, Caps, walks, dawn / dusk) | flicker, a wrong colour or palette for some frames, the picture's width changing, a still page that is not still, a picture that does not settle after a key | 3 min |
| `tests/test_sweep.sh` | every page entered, every hotspot clicked, in a headless run each (2596 interactions per variant) | crashes, clicks that do nothing, hotspots that cannot be reached, files that fail to open, and **any change** in what a click does compared with the record | 25 s per variant |

## Parity (`tests/audit/parity.py`, `portmap.py`)

The port writes the original's address on every translated routine (`// dll:1001d970`, or `FUN_1001d970` at a call). `portmap.py`
reads the listing (per routine: the strings it refers to, the routines it calls) and the port (per address: the function bodies
that carry it). Findings:

- **A** a game routine that is mentioned nowhere in `src/` and is not a compiler-made destructor, exception funclet or debug dump.
- **B** a string the original routine uses (not a debug text) that does not appear in the port routines carrying its address, nor in
  the helpers they call.
- **C** a game routine the original calls that the port routines carrying the address do not reach (directly, through helpers, or
  named in a comment at the call).

`python3 -I tests/audit/parity.py` prints them. It compares text: it finds a translation that left a part out, not one that
computes differently.

## Sweep (`tests/audit/sweep.py`)

One run of `build/biosys --game --script` per interaction: new game, `page P` (jump), then the click; 6 s of game time later the
state is read, 12 s later once more. What the page does by itself in those 6 s (daylight palette, thirst, looping films) is
measured in the page's own run and subtracted. Classes: `page` (entering it), `moved`, `react`, `off` (hotspot switched off in
this state), and the suspect ones `crash`, `stuck`, `dead`, `unreached`. Variants: `base`, `power` (after `game fuses`),
`right` (right click), `hold:OBJECT` (object in the hand; script command `hold`).

`python3 -I tests/audit/sweep.py --variant base --pages FS2FAN,R4` runs a part and writes `build/audit/sweep_base.tsv` with the
full signature of each interaction. To see one interaction in full, run its script by hand with `--log-events --log-api`
(the script is in `Sweep.script`).

Limits: the page is reached by a jump, not by walking, so site state (palette, rain, where the lift is) can be inconsistent;
a hotspot that is `off` in a new game is not exercised by the `base` variant.

## Picture stability (`tests/audit/visual.py`, `tests/test_visual.py`)

The other suites look at one frame; players report sequences (a blue blink, a wrong colour for a moment after Tab, the sides of the picture
cut). `overlay diff` (src/app/overlay_script.cpp) prints, after every `overlay frame` / `advance`, the comparison of the PRESENTED picture
(the window's composition at W x H with the cleaned page texture refilled band by band, or the 640 x 480 frame) with the previous two:
mean colour, per cell of a 16 x 9 grid colour and change, palette hash, held picture, what runs by itself (sprite animation, film, timed
palette, cross-fade, rain, message / strip fade, blocking pump). `overlay pump on` presents a frame on every turn of the engine's blocking
loops (transition film). visual.py checks the sequences; the suite also runs the checker against injected faults (`overlay fault ...`).
Findings: build/audit/findings_visual.txt (with a reproducing script), frames in build/audit/visual/; verdicts `V <tab> scenario <tab> rule <tab> ok: REASON | open`
in tests/audit/verdicts/d.tsv. What it cannot see: the GPU path (SDL textures, filtering), the real timing of the asynchronous cleaner, the settings menu
layer, the sound.

## Object sweeps (`sweep.py --variant take|use|look|apply`)

What needs an object in the hand is swept in the same way (one headless run per interaction, same classes, same record format; rows are
keyed by `(where, what)`):

| Variant | Rows | What is done | Time |
|---|---|---|---|
| `take` (in `test_sweep`) | `page, OBJECT@zone` | click the object's drop zone on its page, where a new game puts it (pick-up animation, events, `held->OBJECT` in the signature) | 10 s |
| `use` (in `test_sweep`) | `OBJECT, use|stash|drop` | `hold OBJECT` on page R4, then Space / S / D, one run each | 25 s |
| `look` (in `test_sweep`) | `PAGE, rclick`; `OBJECT, rclick`; `OBJECT, use+rclick` | right click. The engine's right click is not hotspot-specific: it goes to the held object (`ObjPaperItem::onRightClick`: a paper closes) and to the page's feature (`slot50`), so it is sent once per page, with each object in the hand, and after Space has opened the object. Nothing happening is class `quiet` (not suspect) | 20 s |
| `apply` (on demand) | `page, HOTSPOT|OBJECT` | one representative object per object class (39 of them: objects with the same member set, for food / drugs / extinguishers also the same code; a plant's fruit cannot be held) in the hand, left click on every `B*` and `D*` hotspot (about 1 080 x 39 runs); per hotspot the most common outcome becomes one row `HOTSPOT|*` (only if it differs from the empty-handed click in `sweep_base.tsv`), every object that differs from it gets its own row `HOTSPOT|OBJECT`, crash / stuck always; all runs go to `build/audit/sweep_apply_full.tsv` | 13 min |

`python3 -I tests/audit/sweep.py --variant apply [--kinds B] [--pages P1,P2] [--objects goggles,fuse1] [--out DIR]`; `apply+power` after
`game fuses` (compared with `sweep_power.tsv`). `python3 -I tests/test_sweep.py apply` compares the record `sweep_apply.tsv`
(the default run leaves on-demand variants out). Objects in class `off` with `hold rc=18` are plant fruits (`GameObjects::debugHold`
refuses them: they are eaten at pick-up).

## Coverage (`tests/audit/coverage.py`)

`python3 -I tests/audit/coverage.py` (about 30 min with the apply matrix; `--quick` leaves the apply matrix out, `--merge-only` re-evaluates the last run)
builds an instrumented game into `build-cov/` (clang `-fprofile-instr-generate -fcoverage-mapping`), runs every sweep variant with it
(`BIOSYS_BIN`; the runs write into a pool of `LLVM_PROFILE_FILE` files that merge on exit), and writes
`build/audit/coverage_functions.tsv` (file, function, executed count, lines covered, lines total of every function of `src/game/*.cpp`).
It prints the share of functions and lines reached and the never-executed functions grouped by source file, largest first. A report,
not a test. What the sweeps cannot reach (a later day, walking, a sick player, a terminal session) is what scenario tests must cover.

## State files and verdicts

- `tests/audit/parity_state.tsv`: every parity finding with a verdict, `open` or `ok: REASON`.
- `tests/audit/sweep_<variant>.tsv`: every interaction with class, digest, and for suspect classes a verdict.
- `tests/audit/verdicts/*.tsv`: verdicts written by whoever looked (they override the two files above; one file per person / packet):
  `P <tab> kind <tab> address <tab> finding text <tab> ok: REASON` (the three middle fields copied from `parity_state.tsv`), or
  `S <tab> PAGE <tab> hotspot <tab> ok: REASON`.

A suite fails on anything that is not in its record (a new gap, a changed behaviour). `open` entries are counted and printed as the
to-do list; they do not fail. After an intended change: look at the printed differences, then
`python3 -I tests/test_sweep.py --record` / `python3 -I tests/test_parity.py --record` (verdicts are kept).

A verdict `ok:` needs a reason that somebody else can check: the original's address and what it does there, e.g.
`ok: dll:1005b100 only acts when the bees are awake (m_bAwake), new game has them asleep`.
