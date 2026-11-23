# Realism and difficulty: the remaster layer on top of the faithful game

`src/game/settings.h` adds two independent axes to the `Fixes` switch of `FIXES.md`. Design study: `analysis/notes/remaster_design.md` (items A1..A12).

* **Realism** corrects the simulation model: `faithful` (nothing, the default), `corrected` (= `Fixes::restored()` + A1, A2, A3), `closedloop` (= corrected + A4, A6, A7, A11, A12). The four bug flags of unclear intent (now three: `plantFloodedUsesMax`, `condensationFlagPerCel`, `consumableCodeFromHotspot`) are in no level.
* **Difficulty** scales the pressure on the player with plain numbers (`Tuning`): `guided`, `original` (every value neutral), `survivor` (study D.2; Castaway is not offered).
* `Settings { Fixes fixes; Model model; Tuning tuning; }`, `Settings::parse(realism, difficulty, fixesOverride)`; held in `GameState` next to `Fixes` (`SaveGame::Options::set`, `BiosGame::Config::set`, `SaveGame::setSettings`, `BiosGame::setSettings`). Front end: `--realism faithful|corrected|closedloop`, `--difficulty guided|original|survivor`, `--fixes LIST` (developer option; replaces the fix part of the realism level, the model part stays); printed on stderr once at start when not default.
* Faithful + original is the unchanged translation: the multiplier of every knob is the last factor of its product (1.0 is exact) and every model flag is `if (flag) { corrected } else { original }`. Start values apply to a new game only (menu command 1 of `BiosGame::onMenuCommand`).
* **Saved games remember their settings.** The mode of a game (realism, difficulty, fix list) is part of the save file: after the game's blob, which is a fixed size, the window build appends one block (`src/engine/savetrailer.h`): the magic `BSYSMODE`, a version (1), the length of the text, the text (`src/app/savesettings.h`: `biosys-settings 1`, `realism=`, `difficulty=`, `fixes=`) and a 32-bit check sum of the text. The engine's part and the game's part stay byte for byte the original's and at the same offsets; the game's loader (`BiosGame::onLoadGame`, `SaveGame::blobOf`) stops at the end of the blob and so ignores the block. Loading continues under the block's settings (one line on stderr says so when they differ from the options); a save without a block uses, in this order, the `<name>.settings` file that versions of the port before this one wrote next to the save (it names the hash of the exact bytes of the `.sav`, so a replaced save does not take it), and then the options. A damaged block (cut short, a bad check sum, another version, a word the program does not know) is reported on stderr and counts as absent; `NewGame.sav` never has one, so a new game uses the options (which the New game screen sets, MANUAL.md 1.8). Nothing writes a `.settings` file any more; when a save is written over a slot, that slot's old `.settings` file is removed, after the save succeeded and only that one. Tests: `tests/test_settingsmenu.sh` part B, `tests/test_window.sh`. The one thing a load cannot change is `synthSoundHandleInit`, which `StoryLogic` reads when the program starts.
* Tests: `tests/test_settings.sh` (`build/biosys-settingstest`: 81 checks, one per flag and per knob; the measurement tool; the sensible player; test_play under two combinations). Measurements: `build/biosys-settingsmeasure`, tables by `python3 -I tests/settingsmeasure_tables.py` from `build/settingsmeasure/<realism>_<scenario>_<seed>.tsv`.

## Model flags

| flag | item | site | original | corrected | evidence |
|---|---|---|---|---|---|
| `proteinPerSecond` | A1 | `self_update.cpp` (`LoseProtein`, dll:10066aa0) | `LoseProtein(1/10800)` once per update (about 0.064 per game day) | `dtd / 10800` like every neighbouring drain: 100 -> 20 in 10 game days | the other drains of the function are `dtd * c`; vitamins and the drug dispenser exist only for this bar. Measured idle: protein 99.87 -> 87.9 at day 10 (and day 30: the idle player is dead from day 2, the stat stops). Sensible player day 8: 100 -> 51..59 |
| `savannahSoilAlarm` | A2 | `feat_sensors.cpp` `CBiomeSensor::update` (table 0x100b7988) | low soil-water alarm at 4 | 7 | the savannah plants need 6..7 (AS3AL 6, AS3OK/AS5OK/AS5WM 7). Alarm only: nothing in the world changes (checked: identical columns) |
| `waveMachineSetpoint` | A3 | `watertank.cpp` `COcean::updateState` | the wave machine aims at O2 3.4, below the 3.5 damage limit and probe alarm | 4.3 (the ocean's build value) | with the machine on the flag 0x20 was set for ever. Idle ocean O2 3.40 -> 4.30; ocean health day 5 0.956 -> 1.0, day 6 0.911 -> 0.965, day 7 0.50 -> 0.56. The ocean still dies on day 8 in every level (algae over 70 %, nothing to do with this flag) |
| `deadPlantsRespire` | A4 | `plant.cpp` (gas exchange) | respiration 0.25 maxPS whatever the health | times `0.1 + 0.9 * health` | a dead plant (PS 0) kept eating O2. Idle: dead biome O2 day 30 4.4 -> 14.7, savannah 16.0 -> 20.3, rainforest 11.8 -> 14.4; machines off: rainforest day 30 5.5 -> 19.0 |
| `vapourFromSoil` | A6 | `waterflow.cpp` `CEvaporation::update`, `biomecel.cpp` `updateState` | the air loses `0.1 H` %RH per hour while it is warmer than the biome target and nothing puts vapour back; the flow moves the condensation amount straight from the soil tank to the condensate tank | vapour model v2 (below): the soil tank evaporates into the air all the time, `dH = kE * wet^n * (1 - H/100)` per hour (kE 30, n 1.5, wet = fill fraction of the soil tank); the condensation sink is unchanged and puts the vapour it removes into the condensate tank; the flow no longer transfers soil -> condensate | 1 %RH = mag / 10 = 4.1667e-6 volumes (the mist calibration; the study's numerology) |
| `transpirationHumidifies` | A7 | `plant.cpp` | `dt * transp * 2.3148e-9` %RH | `dt * transp * 6e-4 * health` %RH (`Model::transpRate`), the water taken from the soil tank (4.1667e-6 volumes per %RH); the rate keeps its original deficit factor `1 - H/100` | a dead plant does not transpire |
| `houseEnvelopeSlow` | A11 | `biomecel.cpp` `updateState` | house cel envelope 1/12000 s like a glasshouse | house only: 1/86400 s | calibration below |
| `floodedPlantFormula` | A12 | `plant.cpp` | availability `1 + (sw - 47.5) / (47.5 - swMin)` (> 1: flooding helps) | `(47.5 - sw) / (47.5 - swMax)` clamped 0..1, 0 for any flood when `swMax >= 47.5` (rainforest); flat flood damage unchanged | the reviewer's form: the mirror of the dry rule `(sw - 1) / (swMin - 1)`, continuous at swMax. It replaces both formulas; `plantFloodedUsesMax` has no effect while it is on (never apply both: tested) |

### Vapour model v2 (A6 + A7, closedloop) and where it deviates from the study

The study's A6 (add `amt / 4.1667e-6` %RH in every `CEvaporation` update) is a run-away: the flow is proportional to H and always on, the condensation only while the air is above the target, so while it is off dH/dt = +0.1 H per hour (measured: savannah 10 % -> 98 % in 5 days). My first replacement (give back only what condenses) pinned the rainforest at 99.8 %. v2 (reviewer's design):

1. **Evaporation** is a deficit-limited source that runs all the time: per `CEvaporation` flow (the cels with a condensate tank: rainforest, savannah, dead biome) `dH = kE * wet^n * (1 - H/100) * hours`, `wet` = fill fraction of the cel's soil tank (= (soilWater - 1) / 46.5, clamped 0..1). It stops at 100 %.
2. The original **condensation sink** is untouched (0.1 H per hour while the air is above the biome's target).
3. **Water is conserved** (1 %RH = 4.1667e-6 volumes): the soil tank loses what evaporation puts into the air (salt stays behind), the condensate tank gains what the sink removes (mixed like the original flow: fresh water of purity 0.7), and the old always-on soil -> condensate transfer is off under the flag.
4. **Transpiration** keeps its rate (already deficit-limited by `1 - H/100`); its contribution is `transpRate * rate * health` %RH per second and comes out of the soil tank like the evaporation.

Constants (`Model::evapRate / evapExponent / transpRate`; the tool overrides them with `--calib kE= n= k7=`): **kE = 30 %RH/h, n = 1.5, k7 = 6e-4 %RH/s** per unit of transpiration. One set for all biomes; no per-biome constants.

### Calibration of v2, targets in the reviewer's priority (5 seeds, `closedloop`, `idle` = the machines as NewGame.sav saves them)

| target | result |
|---|---|
| (a) days 1-3 daily-mean humidity within 10 points of the start and inside the sensor band | rainforest start 85.0: 84.8 / 83.5 / 84.4 (band 60..100) **met**. Savannah start 10.0: 16.7 / 17.0 / 14.1 (band 0..20) **met** (+7). Dead biome start 10.1: 14.5 / 14.0 / 11.8 **within 10**; it starts below its own band (20..100) in the saved game and has no healthy plants, so the band cannot apply. House 65: 65 (no evaporation flow, no plants, unchanged). **Ocean start 70: 88.0 / 88.1 / 88.8 (day 30: 87.8) with the ocean extension below: still a miss by +18**, inside its 0..100 band (without the extension 96..100). The ocean cel has no soil and no condensate tank; under the flag it now evaporates from its tank (wet = 1) and has the original condensation sink, which works only while its air is above the biome target (mostly at night), so it settles near 88 and not at the 75 of the day-and-night formula. |
| (b) plants matter | plants' share of the rainforest vapour input over days 0-3: **0.25** (roughly a quarter, not a third: a higher share costs the savannah, see below). Rainforest mean humidity days 1..6, with plants 85 83 84 77 73 70, plants dead (`--kill-plants`) 84 82 83 70 61 53: equal while the rain machine runs (days 1-3), clearly lower after it stops, **above 60 until soil water 25 (day 5), below it from soil 21 (day 6)**: partly met. |
| (c) rain | rainforest day 2 (rain machine on): 78 % -> 100 % within 3 h, back to 85 by 20:00 and 78 by the next morning: **met**. Savannah in the `programme` scenario (rain 10..14 on every day; the rain wets its soil to 6..20): base 54..61 %, peak 79 %: the humidity rises and falls by 25 points after the rain but **stays at about 55 %, far over the savannah's 20 % alarm** (faithful: 3..20 %). Not met. |
| (d) drying | idle rainforest humidity / soil water: day 5 69 / 25.4, day 10 54 / 11.2, day 20 35 / 6.6, day 30 30 / 5.9; savannah 18.3 -> 7.9 -> 2.9 -> 1.3 (soil 4.8 -> 1.0); dead biome 11.9 -> 4.8 -> 1.2 -> 0.003: **met in every biome**; machines off (`frozen`): rainforest 14 % on day 10. |

Why (c) and the savannah miss: with the same wetness the rainforest needs about 8 %/h of source to hold 85 % against its sink, while a savannah cel whose soil the rain has wetted to 0.3..0.4 gets 4..6 %/h from evaporation and as much from its plants (air at 15 % transpires three times as much as air at 85 %) against a sink that works only at night. Scan of the pair, programme savannah days 3-5 mean: kE 30 n 1.5 64 %; n 3 51 %; kE=10 with no plants 25 %; no model terms (kE = 0, k7 = 0) 5 %. A temperature factor `clamp((T-10)/20,0,1)^p` on the evaporation (p = 2 or 4, tried) lowers the savannah by only 3..6 points because the plants dominate there; lowering k7 to 1.5e-4 gives 52 %, 0 gives 47 %. No one pair of constants satisfies (b) and (c): the smallest extension that I can see is a stronger sink for a dry biome (condensation also outside the "air above the target" condition), not a constant. **Ocean extension (built):** the ocean cel gets its own evaporation (wet = 1, from its tank) and the same condensation sink (`biomecel.cpp`); the equilibrium of `kE (1 - H/100) = 0.1 H` would be 75 % if the sink ran all day, with the real condition it is 88 %.

Choices that cost something: kE = 30, n = 1.5, k7 = 6e-4 is the point of the grid {kE 15..90} x {n 1..3} x {k7 4e-4..1e-3} that meets (a) for the three biomes with a condensate tank and keeps the killed-plant rainforest at 61 % at soil 25; n = 1 (killed 72 %) pushes the dry savannah to 28 % at the start; n = 3 drops the killed rainforest to 41 %.

**A11** (unchanged): machines off, house air on day 10: 4.8 C -> 9.4 C, day 30 3.9 -> 7.8 C; saved programme 24.5 -> 23.3 C. Temperatures are never in alarm in the living biomes at any level.

Alarm shares (share of game days with an alarm in the biome's sensors, 5 seeds; d1-7 / d1-30):

| level, scenario | rainforest RH | savannah RH |
|---|---|---|
| faithful / corrected, idle and programme | 69 / 93 % | 0 / 0 % |
| closedloop, idle | **0 / 77 %** | 0 / 0 % |
| closedloop, programme | 0 / 75 % | **100 / 37 %** |
| closedloop, frozen | 86 / 97 % | 14 / 3 % |

The rainforest holds its band for the first week (target "settle for 30 days" is **not met**: its clean-water tank is empty by day 5..7, the soil dries 36 -> 11 by day 10 and the humidity follows: (d)). It is the water economy of the default programme, not the humidity model.

### Water conservation check (`water.tanks`, `water.vapour`, `water.total` columns; vapour = sum over cels of H * 4.1667e-6; 30 days, seed 1)

| scenario | faithful total drift | closedloop total drift (day 1 / 10 / 30) |
|---|---|---|
| frozen | -4.08e-3 | -1.3e-4 / -1.6e-4 / -1.7e-4 |
| idle | -4.08e-3 | -1.3e-4 / -1.7e-4 / -1.7e-4 |
| programme | -3.75e-3 | +4.1e-5 / +1.6e-4 / +5.5e-4 |

Total water is 236.2: the closedloop drifts are below 3e-6 of it (faithful: the condensation sink destroys the vapour, 2e-5). Under the flag the humidity that `CRain` and the misters add is drawn from their source tank (1 %RH = 4.1667e-6 volumes; nothing is added above 100 %), the ocean cel evaporates from and condenses into the ocean tank, evaporation and condensation use one constant (an earlier version took the flow's own `mag / 10`, which differs per cel, and leaked 0.02 volumes in 10 days). Remaining: **open**: a day-1 loss of 1.3e-4 (the same in all scenarios, not traced; air flows exchanging %RH between cels of different air volume are the suspect, not verified); the programme drift (+5.5e-4) is the rain humidity added to a cel that then clamps or exchanges; not counted: drinking, flows into a null tank, the bubbler's humidity (original leaks). Nothing else was chased.

### What changes for the player (closedloop)

* The rainforest keeps its humidity while it has water and loses it as its soil dries (about a week without a supply).
* **Irrigation makes a biome humid.** A savannah whose rain machines run every day has wet soil, evaporates and transpires: it stays at about 55 % and its humidity alarm (limit 20 %) stays on. That is accepted as the model's consequence (the alarm says "over-watered"), not tuned away. In the original the same savannah drops to 3..20 %.
* Plants matter: dead plants do not humidify; a rainforest without its plants is 10..20 points drier once the rain machine stops.
* The ocean cel is humid (about 88 %) instead of a constant 70 %.

## Tuning knobs

Original value / guided / survivor. "keep" = the saved value (start values: study 40 / 75 / 63).

| knob | site | original | guided | survivor |
|---|---|---|---|---|
| `startHydration`, `startNutrition`, `startEnergy` | `Self::applyStartValues`, `BiosGame::onMenuCommand` (new game only) | keep | 80 / 90 / 90 | keep |
| `hydrationDrain` | `self_update.cpp` `LoseHydration(dtd/864)` | 1 | 0.5 | 1.25 |
| `nutritionDrain` | `LoseNutrition(dtd/576)` | 1 | 0.5 | 1.25 |
| `heatWaterTerm` | the extra drain above 20 C | 1 | 0.5 | 1 |
| `starveDamage`, `thirstDamage`, `heatDamage`, `coldDamage` | the four `LoseHealth` terms of `Update` | 1 | 0.5 | 1.5 |
| `oxygenDamage` | `UpdateAilments` (O2 < 14 %) | 1 | 0.5 | 1.5 |
| `awakeLimitHours` | energy drain after 0x15180 s awake | 24 | 36 | 24 |
| `healing` | `AddHealth(dtd/43200)` | 1 | 1.5 | 0.75 |
| `sleepHealing` | `self_sleep.cpp` health per 90 s step | 1 | 1.5 | 0.75 |
| `plantDamage` | `plant.cpp`, every damage rate (water, temperature, pollution) | 1 | 0.5 | 1.25 |
| `plantRecovery` | short and long term recovery | 1 | 1.5 | 1 |
| `powerGeneration` | `feat_power.cpp` solar and windmill | 1 | 1.5 | 0.8 |
| `siltClog` | `waterflow.cpp` clog rate (0.1 per day) | 1 | 0.5 | 1.5 |
| `heliDelaySeconds` | `story_events.cpp` (0x20000000) | 604800 | 1209600 | 432000 |
| `huntChancePercent`, `huntCooldownSeconds` | `story_handlers.cpp` `CSubTechHandler::Update` | 2 / 300 | 1 / 600 | 2 / 300 |
| `malaria` | `Update` (dt/72 percent) | 1 | 0.5 | 1 |
| `sensorNoise` | `CBiomeSensor::update` error ranges (same random draws) | 1 | 1 | 5 |
| `alarmMargin` | each alarm limit that is not a physical bound moves inward by this fraction of the band | 0 | 0.1 | 0 |

Not built from D.2: default machine duty (guided RO 24 h, algae 4 h) because it is saved game data, not a constant of the code; the sleep "cap 80/100"; sensor drift.

## Measurements (seeds 1..5, 30 game days, means; `idle` = the machines as saved: heaters on, rain machine of R1 always, two more rain machines 10..14, RO and UV 13..14; `programme` = idle + every machine with a timer switched on at its default window; `frozen` = every machine off). Humidity in %RH, soil water on the 1..47.5 scale.

Note on the scenario names: `biosys-fixmeasure` already runs the saved machines, so its idle world equals `--scenario idle` here (tested, every shared column).

| realism | scenario | day | RF | SAV | DB | house | ocean | RF soil | SAV soil | plants alive |
|---|---|---|---|---|---|---|---|---|---|---|
| faithful | idle | 10 | 0.0002126 | 1.113e-05 | 0.0003285 | 65 | 70 | 11.24 | 1.629 | 54.2 |
| faithful | idle | 30 | 9.015e-05 | 5.249e-06 | 1.004e-08 | 65 | 70 | 6.138 | 1.012 | 38.2 |
| corrected | idle | 10 | 0.0002126 | 1.113e-05 | 0.0003285 | 65 | 70 | 11.24 | 1.629 | 52 |
| corrected | idle | 30 | 9.015e-05 | 5.249e-06 | 1.004e-08 | 65 | 70 | 6.138 | 1.012 | 33 |
| closedloop | idle | 10 | 54.2 | 2.895 | 1.215 | 65 | 85 | 11.14 | 1.617 | 52 |
| closedloop | idle | 30 | 30.15 | 1.329 | 0.003034 | 65 | 84.76 | 5.839 | 1.008 | 33 |
| faithful | programme | 10 | 0.0001955 | 7.412e-05 | 0.0003285 | 65 | 100 | 9.966 | 6.301 | 57 |
| faithful | programme | 30 | 3.455e-06 | 5.945e-06 | 1.004e-08 | 65 | 100 | 1.164 | 1.097 | 32.4 |
| corrected | programme | 10 | 0.0001955 | 7.412e-05 | 0.0003285 | 65 | 100 | 9.966 | 6.301 | 57 |
| corrected | programme | 30 | 3.455e-06 | 5.945e-06 | 1.004e-08 | 65 | 100 | 1.164 | 1.097 | 28 |
| closedloop | programme | 10 | 52.47 | 26.73 | 1.215 | 65 | 86.38 | 9.874 | 6.223 | 57 |
| closedloop | programme | 30 | 0.1647 | 1.474 | 0.003034 | 65 | 86.04 | 1.155 | 1.09 | 28 |
| faithful | frozen | 10 | 0.000201 | 2.485e-05 | 0.0003285 | 65 | 70 | 5.474 | 1.629 | 6.2 |
| faithful | frozen | 30 | 2.405e-06 | 7.65e-06 | 1.004e-08 | 65 | 70 | 1.082 | 1.012 | 4.4 |
| corrected | frozen | 10 | 0.000201 | 2.485e-05 | 0.0003285 | 65 | 70 | 5.474 | 1.629 | 1 |
| corrected | frozen | 30 | 2.405e-06 | 7.65e-06 | 1.004e-08 | 65 | 70 | 1.082 | 1.012 | 0.4 |
| closedloop | frozen | 10 | 14.08 | 0.9385 | 1.215 | 65 | 89.48 | 5.441 | 1.624 | 1 |
| closedloop | frozen | 30 | 0.04182 | 0.03555 | 0.003034 | 65 | 88.99 | 1.081 | 1.011 | 0.4 |

Columns: RF = rainforest, SAV = savannah, DB = dead biome; humidity in %RH, O2 in %, plants alive of 57. Ocean health is 0 on day 10 in every row: the algae (> 70 %) kill it on day 7..8 whatever the realism, because the algae scrubber runs 1 h a day against 12 %/day of growth (study A.5). `corrected` moves it only a little (day 5 0.956 -> 1.0, day 7 0.50 -> 0.56).

What changes nothing measurable: `savannahSoilAlarm` (an alarm, no effect on the world), `floodedPlantFormula` in the idle world (the flooded cels of the opening dry out in 4 days; see below), `condensationFlagPerCel` with or without any level, `waveMachineSetpoint` with the machines frozen (the wave machine is off), `proteinPerSecond` on everything but the protein bar.

### The opening flood (A12): R4, 5 plants, soil water 47.97 at the start (1 seed; faithful vs `floodedPlantFormula`)

| day | soil water | plant health (both) | R4 O2 faithful / formula | R4 CO2 faithful / formula | fruit |
|---|---|---|---|---|---|
| 1 | 47.97 | 0.72 | 20.74 / 20.67 | 0.294 / 0.364 | 4 / 4 |
| 2 | 47.98 | 0.44 | 20.71 / 20.66 | 0.323 / 0.373 | 4 / 4 |
| 3 | 46.93 | 0.68 | 20.72 / 20.70 | 0.311 / 0.330 | 4 / 4 |

Time to death: nobody dies in either (the flat flood damage is unchanged, so the plants fall to 0.44 on day 2 and recover from day 3 as the soil drains). With the formula the flooded plants photosynthesise nothing while flooded: O2 0.05..0.07 points lower and CO2 0.03..0.07 higher on days 1-2, no effect on fruit. **The opening flood does not become more urgent.**

## The player

The sensible player (in `settingsmeasure --player sensible`, `--start`): by day in R4, from 21:00 to 06:00 in the house (site PH5, page NPH5BED); drinks `Self::Drink(0xbe)` when hydration < 45; eats the best food of the world in the scene (`ObjFood::pickUp` + `use`, no drugs, plant health > 0.4) when nutrition < 55; at night calls `Self::Sleep(0,0,1)` and `Self::OnIdle()` (the night in 90 s steps). It drinks and eats anywhere (no walking, no queues).

8 game days, seeds 1..5, `--start` (start values of the difficulty):

| realism | difficulty | alive | drinks / meals / sleeps (mean) | protein day 8 | hydration / nutrition day 8 |
|---|---|---|---|---|---|
| faithful | guided / original / survivor | 5/5 each | 20/13/4.0, 48/25/4.6, 56/31/4.6 | 100 | 57/81, 54/75, 51/69 |
| corrected | guided / original / survivor | 5/5 each | same | 51 / 57 / 59 | same |
| closedloop (vapour model v2) | guided / original / survivor | 5/5 each | same | 51 / 57 / 59 | rainforest humidity day 8: 61 / 63 / 66 |

It survives every combination; the minimum health is the starting 90 (health only rises: the player never gets below 20 hydration). The cost shows in the number of actions (a drink every 9 game hours on guided, every 3.4 on survivor) and, with A1, in the protein bar falling to about half in 8 days.

Idle player (no action, `--start`), time of death in game seconds from the start, cause 12 (dehydration):

| difficulty | measuring tool, seeds 1..5 | live game (`build/biosys --game`), seeds 1..5 |
|---|---|---|
| guided | 422602 .. 478260 (4.9 .. 5.5 days, mean 5.2) | 414258 .. 463023 (4.8 .. 5.4 days, mean 5.0) |
| original | 143100 .. 143190 (1.66 days) | 143094 (1.66 days, seed 1) |
| survivor | 98370 (1.14 days) | 98294 (1.14 days, seed 1) |

The realism levels do not change it. Test: `tests/test_survival.sh`.

An earlier version of this section said guided 3.6 days (315000 s), original 1.5, survivor 1.0, and the live game gave about 4.9 / 1.6 / 1.1. **The tool was wrong, not the game.** The tool called `Self::Update(0)` and never ran the sleep, so an idle player whose energy reached 0 never passed out. In the game the full update (`CSelf::Update(1)`, the timer's) does pass out at energy 0 (`Sleep(1,1,1)`, dll:10066aa0) and the night runs in `CSelf::OnIdle`: 90-second steps with a health gain of 0.05 x quality (x `sleepHealing`) per step, and without the thirst and hunger damage of `Update`. Guided, with the awake limit of 36 hours, passes out at about 40 hours, with 64 health left; each sleep adds 6 to 15 health. Where it matters is rain: the rain machines raise the cel's rain flag, and a sleeper on an unsheltered spot with energy above 20 wakes at the next 90-second step (wake reason 7; the original does this, see `analysis/notes/player.md` section 5), so the first naps (days 1.7 and 2.4 on guided, seed 1) last 81 steps (2 hours, energy 0 to 20.25) and only a nap that starts while the rain machines are off runs its 3 to 6 hours (the length is `RandRange(10800, 21600)`, hence the spread between seeds on guided; original and survivor show none). The tool now does both: an exhausted player passes out through the full update and `OnIdle`, and a small feature host (`RainFxHost` in `src/tools/settingsmeasure.cpp`) switches the rain FX and with them the cel's flag when the machines start and stop, as `BiosGame::localFxUpdate` does. `--no-collapse` restores the old idle player; with it the idle world equals `biosys-fixmeasure` in all 74 shared columns over 30 days (the rain flag changes nothing but the player's sleep in the idle world, so the world tables of this file stand). The remaining difference between the two guided columns is the random night length: the generator's draws are not the same in the tool and the game.

## Needs play-testing

* Everything in the real-time game: the balance of `guided` (a drink every 9 h) and `survivor` (every 3.4 h; sensor noise x5 against alarms with a 0.1 C band), the 14-day helicopter, hunting at 1 % / 600 s.
* `closedloop` humidity (vapour model v2): the savannah stays humid (about 55 %) once rain machines have wetted its soil; the ocean cel drifts to 100 % (no sink); the rainforest follows its soil water. Both open points have a proposed extension above.
* The ocean dies on day 8 in every level (algae); the realism levels do not touch the algae balance.
* The sensible player is simple (no walking cost, drinks anywhere, one plan for every day); the survival result says the numbers are not lethal for a careful player, not that the story route is easy.
* A-items of the study not built: A5, A8, A9, A10, weather feedback.
