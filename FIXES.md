# The remaster switch: what each fix repairs and what it changes

`src/game/fixes.h` has one `bool` per distinct bug of the original (30 flags). The default (`faithful`, all false) is the unchanged
translation. Presets: `safe`, `cosmetic` (= safe + cosmetic), `restored` (= cosmetic + every behaviour-affecting fix whose intent is
evident). Three flags (tier "unclear") are in no preset. `Fixes::parse("restored,-rainWashesPollution")` combines presets and flag names.

Numbers below come from `build/biosys-fixmeasure` (tables from `python3 -I tests/fixmeasure_compare.py [--rain]`, raw TSVs in
`build/fixmeasure/`). Tests: `tests/test_fixes.sh` (107 checks in `build/biosys-fixtest`, one or more per flag, plus the measurement checks).

## Count by tier

| tier | flags | in preset |
|---|---|---|
| safe | 10 | safe, cosmetic, restored |
| cosmetic | 7 | cosmetic, restored |
| restored (behaviour, intent evident) | 10 | restored |
| unclear (behaviour, intent not evident) | 3 | none |
| no flag (see end) | 3 | the port already cannot reproduce the original |

## The bugs

"Original" is what `faithful` does; "fix" is what the flag does. `file:line` is the flag test in `src/game/` (the original code sits in the else branch).

### safe (memory over-reads, uninitialised data, NULL reads, Load-calls-Save)

| flag | file:line | dll | original | fix | evidence of intent |
|---|---|---|---|---|---|
| `lungLoadBaseBlock` | feat_sensors.cpp:522 | 100102d0 | `CLung::Load` calls `CFeature::Save` on the load cursor: the saved 40-byte base block is ignored, the live state stays | loads the block (`CFeature::Load`) | every other feature's Load (e.g. `CWaterPump::Load` 10005d80) calls `CFeature::Load`; `CLung::Save` 100102b0 writes the block; with the fix a save round-trips byte for byte |
| `hatchLoadBaseBlock` | feat_story.cpp:367 | 1001ce10 | same slip in `CHatch::Load` | same | same |
| `gogglesImageUninit` | obj_gear.cpp:786 | 1001d6b0 | a power state outside 1..6 logs "duff goggles power state" and then places the goggles overlay with an uninitialised image name | places nothing | the default case already reports the state as invalid; no image is named for it |
| `synthSoundHandleInit` | story_hotspot.cpp:24 | FUN_100084b0 (ctor) | the synth handler's sound handle starts at 0 (nothing initialises it) | starts at -1 | the sub-tech handler's constructor (FUN_10022920) sets -1 and `CSynthHandler` tests `== -1` for "no sound" (story_handlers.cpp:215); handle 0 is a real engine handle |
| `journeySoundUninit` | story_path.cpp:199 | 10001ac0 | a path with no terrain bit calls `PlayLoopedWav` with an uninitialised file name | no call | the eight terrain bits each name a wav; no bit, no sound (the port already passes an empty name, which does nothing) |
| `selfDumpNullSite` | self_consume.cpp:545 | 100651d0 | `CSelf::Dump` reads `m_pSite->m_szName` with no NULL test (the port throws) | prints `<none>` | the next line (page) prints `<none>` for NULL |
| `ambientSoundIndex` | bg_site.cpp:197 | 1000e750 | index `rand() * n / 0x7fff + 1`: the draw 0x7fff gives n+1, one entry past the set (reads memory behind it; the port plays nothing) | the last entry | the scale 0x7fff is rand()'s maximum, so the intended range is 1..n |
| `variantSoundIndex` | bg_site.cpp:869 | 10025250 | same overshoot in `CSiteVariant::OnTimeOfDayChange` | the last entry | same |
| `plantStateTextUninit` | self_msg.cpp:444 | 1006aa80 | a plant health state with none of the bits 1, 0x800, 2, 4 loads the string whose id is left in a stack slot (the port: string 0) | draws no state text | the four known states each set the id and colour; no bit, nothing to say |
| `sealMovieNullCheck` | feat_story.cpp:617 | 10024ae0 | `CSeal::Close` tests the first movie (+0x74) for NULL but starts the second (+0x78): NULL read when only the first exists | tests the movie it starts | `CSeal::Open` tests and starts the same movie |

### cosmetic (display and sound only)

| flag | file:line | dll | original | fix | evidence of intent |
|---|---|---|---|---|---|
| `panelNumberAlign` | disp_features.cpp:75, 125, 204 (+ `Panel::right`, disp_util.h) | 100037e0, 100047b0 (2 numbers), 10025580 (2 numbers) | the number is right-aligned by the width of the "%d" text formatted just before, not of the text drawn ("12" measured, "12.50" drawn) | measures its own text | the tide machine and the other panels measure what they draw (`GetTextExtentPointA` then `TextOutA` of the same string) |
| `sensorAlarmFxStop` | feat_sensors.cpp:88 | 10026770 | `CTankSensor::Update` stops the alarm FX when there was no alarm before (`prev == 0`), so an ended alarm keeps its FX and a quiet sensor calls StopPageFX every update | stops it when an alarm has just ended (`prev != 0`) | the end of `COceanProbe::Update` 10027de0 (same step, same FX call) tests `prev != 0` |
| `coffeeMakerCloseSound` | feats_story2.cpp:361 | 1006f6c0 | closes handle -1 (the variable is reset first), the bubbling sound keeps playing | closes the handle that was playing | the handle is only stored to be closed here |
| `oxyCylinderEmptyVariation` | obj_gear.cpp:1100 | 10062fa0 | an emptying cylinder changes to variation "off" | "oxyoff" | `COxyCylinder::Use` 10062e30 and the other path use "oxyoff" for the closed valve |
| `pumpParamShared` | feat_water.cpp:37 | 10005bb0 | source and destination code share +0x64: setting the source after the destination overwrites the destination code `GetParam(0x14)` reports | a set destination keeps its code | `GetParam(0x14)` is documented as the destination (10005cf0) |
| `helpKeyFallthrough` | story_keys.cpp:105 | 1001e6b0 | F1 falls through into the typed-letters (cheat-word) code and types the key's value | returns | F5 and F6 in the same switch return; F1 is a help key |

### restored (behaviour-affecting, intent evident)

| flag | file:line | dll | original | fix | evidence of intent |
|---|---|---|---|---|---|
| `oceanProbeClarity` | feat_sensors.cpp:607 | 10027de0 | the salinity reading is clamped twice, the clarity just computed never | clamps the clarity to 0..100 | every other reading is clamped right after it is drawn; clarity is a percentage (`100 - algae`) |
| `oceanProbeLevelNoise` | feat_sensors.cpp:614 | 10027de0 | the level reading is drawn with the temperature error range (+0x98) | uses the level error range (+0xc0) | `m_dLevelError` (+0xc0) is initialised in the constructor, saved, and read by nothing else; `CTankSensor` uses its own level error for the level reading |
| `bodyTempAtTarget` | self_update.cpp:380 | 10066aa0 | when the body temperature equals its target exactly, the cel's air temperature (the stale local) is added as the change | no change | both neighbouring branches are proportional to the distance to the target (0 at 0 distance) |
| `plantColdDayMin` | plant.cpp:113 | 100733c0 | the cold damage is measured from the night minimum even when the day minimum made the leaf cold | measured from the minimum that was tested | the test just above picks `tMin` (day or night) and the damage uses another variable |
| `rainWashesPollution` | waterflow.cpp:163 | 10071fc0 | rain lowers the cel's current pollution only; the next commit copies the untouched "new" value back, nothing is ever washed out | lowers the new value too | the humidity lines below do the same pair (new, then current); rain machines and the dead-biome clean-up are the game's pollution mechanic |
| `podDoorSoundHandle` | feats_pod.cpp:195 | 10002910 | after the arrival sound the sound-handle global DAT_1009f424 becomes 4 | becomes -1 | -1 is its "no sound" value everywhere else (`CPodFeature::Load` 10002800); 4 would match a later sound with handle 4 |
| `lungParamUnknownId` | feat_sensors.cpp:409 | 10010050 | an unknown source code of id 0x13 falls into the id 0x14 code: stops the lung, stores the code as the mode | rejects it (returns false) | the id 0x14 switch's own `default` rejects unknown codes, and the code stored first stops the lung; `CWaterPump::SetParam` has the same fall-through but there it only returns false |
| `lungParamUnknownMode` | feat_sensors.cpp:424 | 10010050 | a rejected mode returns after the lung was stopped and never restarts it | restarts it | a rejected change must not stop the machine (the success path restarts it) |
| `demoExitQuits` | story_hotspot.cpp:117 | 1001db50 | the test is `strcmpi(&local_20, "MEXIT")` on a stack slot never written, so the quit is never reached | quits when the hotspot is MEXIT | the string "MEXIT" and the call `Eng_QuitGame()` are in the original; notes/player.md: "MEXIT quits". Only demo hotspots start with M |
| `coffeeThirdDose` | self_consume.cpp:111 | 1006ccd0 | the dose limit `2 < m_byDosesCoffee` is tested after `AddHydration(10)`: the refused third coffee still gives +10 hydration (no clamp, no message, not consumed) | the test comes first: a refused coffee changes nothing | the doses are a deliberate limit (they drain energy later and are reset in `self_update.cpp`); the slip is only the order |

### intent unclear (no preset; flag implements the first candidate)

| flag | file:line | dll | original | candidate readings |
|---|---|---|---|---|
| `plantFloodedUsesMax` | plant.cpp:79 | 100733c0 | flooded water availability = `1 - (sw - 47.5) / (swMin - 47.5)` times the base | A (implemented): `swMax` for `swMin`, same shape: availability 0 at `sw = swMax`, rising to 1 at 47.5 (more flooded = more water: inverted). B: `swMax` and no `1 -`, the mirror of the dry branch `(sw - 1) / (swMin - 1)`: 1 at `swMax`, 0 at 47.5. C: the original is intended. The note in analysis/notes/simulation.md says only "[sic: uses swMin]". The dry branch's shape supports B, the "(probably)" in the port's comment supports A. |
| `condensationFlagPerCel` | biomecel.cpp:112 | 10018530 | one static on/off flag for all cels, and the flow id passed is the cel's `m_wId`, which is 0 in every cel, so `SwitchWaterFlows(0, ..)` does nothing | per-cel flag (implemented; changes nothing because the id is 0). Which flow ids were meant is unknown (the evaporation flows have ids but no cel carries them) |
| `consumableCodeFromHotspot` | obj_consumable.cpp:174 | 10077150 | `CConsumable::FillFromHotspot` (no callers in the DLL) fills with the first two characters of the hotspot name when it contains none of WORMS / WATER / LOMB | the content stays (implemented). Or a lookup of the hotspot's own content code that was never written |

### No flag

* Symptom list loop (`self_msg.cpp:298`, dll:1006aa80): the original loop never ends with bit 31 set in `m_dwSymptoms`; the port has always stopped when the bit wraps (symptom bits end at 0x8000, so it cannot happen).
* Start-up `UpdateBiomeCels` with the construction time (`biosgame.cpp:145`): dt wraps and the cels saturate their clamps; the new game overwrites the cels, no effect.
* `featureGetParameter` 2000 (`featparam.cpp:61`): the original copies the cel name into a 7-byte buffer; the port looks the whole name up. No second behaviour to switch to.

## Terminal (BioSys 4): `src/engine/bs4fixes.h`

The terminal is engine code and cannot see `game::Fixes`; it has its own switch, `Bs4Fixes` (one bool per bug, default all false), held by
`Bs4` (`Bs4Options::fixes`, `Bs4::fixes()`) and set by the front end (`Engine::setBs4Fixes`). `--realism corrected|closedloop` switches on every
flag whose preset column is 1, `faithful` none; `--fixes a,-b,...` names flags of either switch (a list replaces the terminal's flags of the
realism level as it replaces the game's; a list naming only terminal flags leaves the game faithful; the words faithful / safe / cosmetic /
restored apply to both, unknown words are refused). Tests: `tests/test_bs4fixes.sh` (off and on, by real clicks where a click triggers it;
`build/biosys-bs4test --fixes LIST --gamefixes LIST`).

| flag | preset | site | exe | original | fixed | evidence of intent |
|---|---|---|---|---|---|---|
| `healthOceanOrnaments` | yes | bs4main_pages.cpp `updateHealthStates` | 0043a5f0 (table row 10 at 00488630) | the row `ocean` names `oceand` / `oceanu` / `oceanh`, ornaments the page `health` does not have: the ocean zone never changes its picture | the row uses `ocd` / `ocu` / `och` | every other row names `<zone>d/u/h` and the page has those for every zone; `ocd`, `ocu`, `och` are the only ornaments of the page that belong to no row |
| `panelFollowsVariant` | yes | bs4page.cpp `Bs4Page::initPanel` | 0041d0a0 (InitPanel) | a variant tab inside a panel (verb 0x17 on the panel: cfan, sfan, vent, heat, rain, mist, filt, osm, uv, sprk, lock, sump, cond, sub, ah) switches the pictures, but a cached panel only calls its setter for the state-button bits (`0xfeff8007`): the controls, the readouts, the loop sound and the highlight stay on the first device | the panel is initialised again for the variant it shows (`def->init(variant)`) | the tab shows the other device's picture and name in the same panel; sending its buttons to a device the panel no longer shows cannot be meant |

### Terminal: checked, no flag
* **86400 clamp of time values** (exe:00421de0 and the same block in 00422590, 00423e60, 00426f20, 00428e30, 00429d50): the report that the clamp stores into the wrong stack variable and the message prints a stale word is not what the code does. The Ghidra decompile shows neither the store nor the right argument; the disassembly does: the checked variable is `[esp+0x10]` / `[esp+0x14]`, the message reads `[esp+0x18]` / `[esp+0x1c]` while two arguments are still pushed (= the same variable) and the clamp `mov [esp+0x18], 0x15180` (e.g. 0x4223b6, 0x4229ef) is made with the same two arguments pushed (= the same variable again). The original logs the value and clamps it. It was the port that did not clamp (the schedule and time-limit panels showed hours above 24 for a value above 86400); it now clamps. A default-behaviour change for values above 86400 only, which no game state produces.
* **Dead `+0x4ac == 1` branch** (paneloxy exe:00433fc0 and the poll handlers 004331d0, 00433440, 004336b0, 00433740, 004342a0): the handler stores 0 into +0x4ac and tests it for 1, so the branch that would compare the FEATURE_ON mirror (+0x15c) with the value of FEATURE_ACTIVE is never taken. Only the ACTIVE mirror is ever compared, which is the sensible behaviour; the dead branch has no effect and no intended alternative (comparing ON with ACTIVE would reload the panel every second). Nothing to fix.
* **Lung route test** (exe:0043b5e0): a lung that draws air from a biome, or has destination 0x1a, answers "ok" with `open = 0`. The only caller (`overlayState`) lights an overlay for `ok && open == 1`, so the answer "ok, not open" and the answer "no" give the same picture: no observable effect, no flag.
* **Device overlays** (`deviceRow`, exe:00439450 / 0041ddf0): not a bug of the port. exe:00407c40 calls 0041ddf0 whenever an item of the main page changed (the `changed` pointer that the decompile drops), so a click on a device tab builds the row at once; a page is loaded with no row item visible. The earlier comment in bs4main_probe.cpp that said "only after a panel closed or a game loaded" was wrong; checked by a test.
* **Slider leave** (exe:00436d60, mouse leaves while the time-limit marker is dragged): sets both text fields to 30 (hours and minutes); the next 1 Hz poll rewrites them. The intent (restore the value from before the drag? clear?) is not evident: left as it is, no flag.
* **Alarm icon** (exe:00439140): paints the ornament `dangerlev`, which no page of BIOSYS4.DAT has (the page loader looks it up by name at load). It cannot be fixed without new art; left.

## Measured effect (idle world, 5 seeds, 30 game days)

Method: `biosys-fixmeasure` runs NewGame.sav headless in 90 game-second steps (weather, ocean, air flows, water flows, objects, features,
plants, then the player's `CSelf::Update`), no player action, one row per game day (columns: per-biome means of air temperature,
humidity, CO2, O2, pollution and soil water; ocean; tank fill and purity; plants alive, mean health, fruit count; power
stored/generation/demand/balance; player health, temperature, hydration, nutrition, protein, energy, time and cause of death).
The idle player dies of dehydration (cause 12) at game second 130590 (seed 1) or 130680 (seeds 2-5), about day 1.5, in every run, with
every fix; no fix changes it. The world keeps running after the death (only the player's updates stop). Values below are means over
seeds 1..5 as "faithful -> fixed"; the seed only drives the sensor noise, so the seeds differ little.

Fixes that change **nothing** in the idle world at any of the 31 days and 5 seeds (every column identical): `lungLoadBaseBlock`,
`hatchLoadBaseBlock`, `gogglesImageUninit`, `synthSoundHandleInit`, `journeySoundUninit`, `selfDumpNullSite`, `ambientSoundIndex`,
`variantSoundIndex`, `plantStateTextUninit`, `sealMovieNullCheck`, `panelNumberAlign`, `sensorAlarmFxStop`, `coffeeMakerCloseSound`,
`oxyCylinderEmptyVariation`, `pumpParamShared`, `helpKeyFallthrough`, `oceanProbeClarity`, `oceanProbeLevelNoise`,
`bodyTempAtTarget`, `rainWashesPollution` (idle: no rain machine runs), `podDoorSoundHandle`, `lungParamUnknownId`,
`lungParamUnknownMode`, `demoExitQuits`, `condensationFlagPerCel`, `coffeeThirdDose`, `consumableCodeFromHotspot`. Hence the presets
`safe` and `cosmetic` equal `faithful` exactly (checked in `tests/test_fixes.sh`). The reasons: they act on player input, machines
the idle player never touches, saves, or sounds (most of them); `bodyTempAtTarget` needs a double equal to its target, which never
happens; the sensor fixes change readings by far less than their alarm thresholds in this run. They are verified by the focused checks
of `biosys-fixtest` only.

### `plantColdDayMin` (the one evident behaviour fix that moves the idle world)

| metric | day 1 | day 3 | day 10 | day 30 |
|---|---|---|---|---|
| plants alive (of 57) | 57 -> 57 | 57 -> 57 | 54.2 -> 52 | 38.2 -> 33 |
| mean plant health | 0.8145 -> 0.8138 | 0.7776 -> 0.7760 | 0.7406 -> 0.7266 | 0.3852 -> 0.3846 |
| savannah CO2 % | 0.1881 -> 0.1881 | 0.4369 -> 0.4406 | 1.486 -> 1.554 | 5.042 -> 5.235 |
| savannah O2 % | 20.84 -> 20.84 | 20.59 -> 20.59 | 19.54 -> 19.48 | 15.99 -> 15.80 |

Plants die earlier (5 more of 38 at day 30), most visibly in the savannah cels. Largest relative move: plants alive at day 30 (-13 %).
Fruit count, power, ocean, tanks, player: unchanged within these 5 seeds (fruit 60 -> 60 at day 30).

### `rainWashesPollution` (only when a rain machine runs; scenario `--rain`: every CRain switched on at the start)

| metric | day 1 | day 3 | day 10 | day 30 |
|---|---|---|---|---|
| dead-biome pollution | 89.51 -> 74.67 | 89.51 -> 74.67 | 89.51 -> 74.67 | 89.51 -> 74.67 |
| tunnels pollution | 11.18 -> 9.335 | 11.19 -> 9.334 | 11.19 -> 9.334 | 11.19 -> 9.334 |

With the bug the rain never lowers any cel's pollution (identical to the machines being off). With the fix the pollution drops once
(-14.8 points in the dead biome, -1.85 in the tunnels, 12 columns in all) during the first game day and then stays: the clean-water source
runs dry (or the soil is full) within the first day. No effect on plants, power or the player in 30 days (the pollution of these
cels is not what kills them in this run).

### `plantFloodedUsesMax` (unclear; candidate A)

| metric | day 1 | day 3 | day 10 | day 30 |
|---|---|---|---|---|
| plants alive | 57 -> 46 | 57 -> 57 | 54.2 -> 54.2 | 38.2 -> 38.2 |
| mean plant health | 0.8145 -> 0.6915 | 0.7776 -> 0.6692 | 0.7406 -> 0.7406 | 0.3852 -> 0.3852 |
| rainforest humidity | 82.98 -> 51.12 | 58.78 -> 23.63 | 0.0002126 -> 0.0001992 | 0.00009015 -> 0.0000879 |
| rainforest soil water | 36.26 -> 36.32 | 37.38 -> 37.38 | 11.24 -> 11.15 | 6.138 -> 6.011 |
| fruit | 149 -> 130 | 145 -> 130 | 135 -> 135 | 60 -> 60 |

The largest single move of all fixes: at the end of day 1, 11 of 57 plants have health 0 where the original keeps them above 0 (candidate A makes
the water availability 0 right where flooding starts); they are back above 0 on day 3 and the effect has gone by day 10, once the soil has dried.
That strong dependence on the formula is the reason it stays "unclear".

### `condensationFlagPerCel`, `consumableCodeFromHotspot`; `coffeeThirdDose` (restored since the settings packet)
No measurable effect (the cel id is 0, no coffee is drunk, no hotspot is used).

### Combined: `restored`

* Idle world: identical to `plantColdDayMin` alone (checked bit for bit in `tests/test_fixes.sh`): plants alive 38.2 -> 33 and mean health 0.3852 -> 0.3846 at day 30; the other eight restored fixes add nothing in the idle world.
* Rain scenario (all rain machines on): `rainWashesPollution` + `plantColdDayMin`: dead-biome pollution 89.51 -> 74.67 from day 1; plants alive at day 30 39.4 -> 35; mean health 0.8544 -> 0.8309 at day 10; savannah CO2 3.683 -> 3.918 at day 30.
* Player death time and cause: unchanged in every run (idle: second 130590 / 130680, rain: 130770, cause 12 dehydration).

## What could not be measured

* Everything that needs a player action or a running machine: sound, display, save/load, the SubTech and synth handlers, the lift, the pod, the coffee machine, the lungs' parameters, the ocean probe's alarms, the oxygen cylinder, MEXIT. They are covered by `biosys-fixtest` (the fixed path does what its comment says) but no effect on the simulation was measured.
* `bodyTempAtTarget` needs an exactly equal double; it never occurred in 150 game days of idle player (5 seeds x 30 days).
* The idle player dies on day 2, so player metrics after that are the state at death; "the world keeps running" for everything else.
* `plantFloodedUsesMax` was measured only with candidate A; B and C were not measured.
* The manual (`docs/Biosys_Manual_Win_EN.pdf`) is scanned images without a text layer: it was not searched, no evidence in this table comes from it.
* The 8 suites that cover `src/game` pass unchanged with the default (test_sim, test_feat, test_featstory, test_self, test_game, test_disp, test_story, test_play); the full `make test` was not run.
