# How smoothly does the original change the light over the day?

Measurement of the port (`build/biosys --game`, `--seed 1`, no source changed). Question: is a "continuous day and night
blending" remaster feature needed, or is the original already continuous enough?

## Conclusion (short)

**On every ordinary location the original is already visually continuous. No blending feature is needed there.**
Over a full sunrise (03:00 to 08:00 game time, 720 s real) and sunset (16:00 to 21:00) on a rainforest panorama (R4), the
savannah (S1) and the ocean (O1), the screen palette moves 421 to 524 times in the 432 s dawn, 304 to 355 times in the
432 s dusk, and in no case does any colour channel change by more than 2 between two consecutive palettes (typical: 1).
Nothing seen was near the visibility limit of 8 per channel. The longest time the palette stands still inside a
dawn or dusk phase is 5 to 11.5 s, and during those pauses the change is under one 8-bit level, so there is nothing to
see. Interpolating the displayed palette between engine palettes would only create fractions of a level that an 8-bit
per channel display cannot show.

**There are real stairs, but only in the places listed below, none of them the open-air time of day:**

1. **Feature close-up pages whose feature has flag 0x20 (lift, gate, hatch, cabinet; not on pages whose name starts with
   "B").** There `CEngineFX_Blend` does not blend. It shows the first palette of the phase until the phase is half
   over, then the second one. Measured on the dead-biome gate page (DB9, `fdb9gate_c`) and the hatch page: in the dawn
   the palette stands still for 3.9 min (232 s real), then jumps in about 5 s with steps of 12 to 13 per channel every
   0.5 s (mean brightness 12.5 to 47 of 255). The three other halves of the day show the same: morning snap at 07:00 (steps
   of 6 to 7, brightness 55 to 47), evening snap at 17:00 (steps of 4, 47 to 30), dusk snap at 19:30 (steps of 14 to 15,
   26 to 12). The stand-stills last 26 s to 6 min real. Leaving such a page after the snap shows the exact blend
   again at once: a jump of 53 per channel (measured, 04:31, brightness 53.7 to 35.3). This is the only place a remaster
   would gain from a change, and the change is in the game logic, not in the presentation (see "Smallest change").
2. Instant changes that should mostly stay instant (section 3).

## 1. From the code

| Fact | Value | Source |
|---|---|---|
| Who recomputes the lighting target | `CTimeline_RealTimeTick`, called from `OnTimer` phase 0 and `OnIdle` count 1, at most once per 5000 ms of real time; it calls `CTimeline_UpdateDiurnal(1, 1, 5, 1)` | `bg_timeline.cpp:61`, `biosgame.cpp:414,482` |
| How often | every 5.0 s real = 125 game seconds = 2.08 game minutes (40 ms per game second, 25x). Measured: 12 `SetPalette` calls in 60 s | run with `--log-api` |
| Other callers (always an instant set, steps 0) | page drawn with a changed palette set or palette group, `forcePalette`, site entered, `settime`, power on/off (`siteLightsChanged`), feature page with flag 0x20 (`resetDiurnal`) | `bg_site.cpp:555,841`, `bg_features.cpp:31,44`, `feature.cpp:350` |
| Phases (game time) | night 21:00 to 03:00; dawn 03:00 to 06:00 (10800 s); morning 06:00 to 08:00 (7200 s); day 08:00 to 16:00; evening 16:00 to 18:00 (7200 s); dusk 18:00 to 21:00 (10800 s) | `CTimeline_UpdateDiurnal` |
| Real length at 25x | dawn 432 s; morning 288 s; day 1152 s; evening 288 s; dusk 432 s; night 864 s | arithmetic |
| Palettes mixed | dawn: night to dawn palette; morning: dawn to day; day: the day palette alone; evening: day to dusk; dusk: dusk to night; night: night alone. Progress `frac` is linear in game time | `CSite_OnTimeOfDayChange` |
| Distinct intermediate levels per phase | one target per tick: dawn about 86, morning about 58, evening about 58, dusk about 86, day 1, night 1. Measured 137 to 139 different targets across a sunrise (18300 game s) or sunset | `SetPalette` hashes, 6 runs |
| Per-channel mixing | `a + trunc((b - a) * frac)`; entries 0 to 61 (the fixed ones) are copied from `a`; then the plant-health or ocean-health tint is added to green or blue entries (`CSite_TintPalette`) | `MakeIntermediatePalette` |
| Day palette | `CSite_BuildDayPalette` mixes cloud and day by the sky clearness `m_dBlueSky`, rebuilt on every weather update; the sky moves at most 0.1 per game hour; shown at the next 5 s tick | `bg_site.cpp:265`, `sim.cpp:82` |
| How the engine walks to a target | `SetPalette(pal, steps = 10, duration = 5 s)`. Timed, not blocking: step k shows `start + k * (target - start) / 10`, taken from the 500 ms timer tick (`tick500`), nominal interval 5000/10 = 500 ms. A new target 5 s later restarts the fade from the palette on display | `bg_enginefx.cpp:161`, `enginefx.cpp:42` |
| Real step interval | A step is taken at the first 500 ms tick at or after its due time (`now >= fade_.due`), and the due time is counted from the tick that took the previous step. Because ticks and passes jitter by a few ms, the interval is 500 ms or 1000 ms. In the window model (33 ms pass): 500 ms in 74 %, 1000 ms in 25 %; in the default headless clock (16 ms): 1000 ms in 75 %. Per 5 s the display therefore covers 50 % to 100 % of the gap to the target and trails the exact blend by 0 to one tick (5 s real, 125 game s) | measured, `an4` |
| Blocking fades | `FadeToPalette` (blocking, 30/60/100 frames) is used only when `immediate == 0`; the time of day never does that | `showPaletteWith` |
| Palette owner lock | `CEngineFX_Blend` and `SetPalette` return false while another token owns the palette (goggles token 1, sensor panel 5, sleep 7 and so on). The time of day is then not applied until the lock is released, and the next site redraw sets it at once (`UpdateDiurnal(1,1,0,1)`) | `state != token` |
| `plain` condition of `CEngineFX_Blend` | blend only if there is no current feature, or its flag byte lacks 0x20, or the location starts with "B"; otherwise `frac < 0.5 ? a : b` | `bg_enginefx.cpp:168` |

## 2. From runs (window pacing model, `window 60 33`, injected clock)

Method. `page X; game type SILVERFEATHER; game type IMMORTAL enter` (the original's own cheat, only to keep the
player alive; it changes no palette), `window 60 33`, `game settime T`, then `palettetrace on` for the whole span.
`palettetrace` prints every palette the engine puts on screen with the engine time. That is the displayed palette
exactly: between two prints it stands still. I resampled these at 100 ms (the requested sampling) and checked them
against direct polling (`advance 100; pal`, 1500 polls): the polled palettes are the same set as the traced ones
(153 vs 152 distinct, the extra one is the one set before the trace started). Brightness = mean Rec.601 luma
over the 640x368 view, weighted by the pixel count of each palette index in a screenshot of the page (so it is what
the player sees, not the plain average of the 256 entries). Game clock in the tables is `settime` plus real time / 40
(the game itself rounds to its 5 s ticks).

Palettes in the table are 768-byte RGB palettes. "Step" is the largest absolute change of any of the 768 bytes.
Dawn run starts 02:55 of day 2, dusk run 15:50 of day 1, midday 12:00 of day 1.

### 2.1 Per transition

| loc | transition | span s | palettes shown | max step (per change) | max step (100 ms sample) | typical (median of nonzero) | steps >= 3 | longest still between changes s | max brightness change per change |
|---|---|---|---|---|---|---|---|---|---|
| R4 | dawn 03-06 | 432 | 421 | 2 | 2 | 1 | 0 | 6.0 | 0.74 |
| R4 | morning 06-08 | 288 | 149 | 1 | 1 | 1 | 0 | 11.5 | 1.00 |
| R4 | evening 16-18 | 288 | 71 | 1 | 1 | 1 | 0 | 7.5 | 0.73 |
| R4 | dusk 18-21 | 432 | 304 | 1 | 1 | 1 | 0 | 5.5 | 1.00 |
| R4 | midday 12-13 (1 h) | 142 | 15 | 1 | 1 | 1 | 0 | 29.0 | 0.07 |
| R4 | whole day 08-16 | 1146 | 248 | 1 | 1 | 1 | 0 | 38.5 | 0.11 |
| S1 | dawn 03-06 | 432 | 524 | 1 | 1 | 1 | 0 | 8.0 | 0.67 |
| S1 | morning 06-08 | 288 | 92 | 1 | 1 | 1 | 0 | 5.5 | 1.00 |
| S1 | evening 16-18 | 288 | 124 | 1 | 1 | 1 | 0 | 5.0 | 0.69 |
| S1 | dusk 18-21 | 432 | 355 | 1 | 1 | 1 | 0 | 5.5 | 0.84 |
| S1 | midday 12-13 (1 h) | 142 | 31 | 1 | 1 | 1 | 0 | 15.5 | 0.23 |
| S1 | whole day 08-16 | 1146 | 204 | 1 | 1 | 1 | 0 | 63.0 | 0.35 |
| O1 | dawn 03-06 | 432 | 470 | 1 | 1 | 1 | 0 | 6.0 | 0.67 |
| O1 | morning 06-08 | 288 | 123 | 1 | 1 | 1 | 0 | 11.5 | 1.00 |
| O1 | evening 16-18 | 288 | 116 | 1 | 1 | 1 | 0 | 5.0 | 0.88 |
| O1 | dusk 18-21 | 432 | 328 | 1 | 1 | 1 | 0 | 5.5 | 1.00 |
| O1 | midday 12-13 (1 h) | 142 | 31 | 1 | 1 | 1 | 0 | 10.5 | 0.13 |
| O1 | whole day 08-16 | 1146 | 143 | 1 | 1 | 1 | 0 | 53.0 | 0.17 |

Reading: no step of 3 or more anywhere (0 of 18 rows); the largest is 2 (R4 dawn, where the plant-health tint adds
to the blend because the plants of R4 decay while the clock jumps). Typical step 1. A palette that stands still does so only where
the blend moves by less than one 8-bit level (the morning and the cloud-only day); the standing-still times are 5 to 11.5 s
in dawn, morning, evening and dusk, and up to 63 s in the clear day, where the cloud mix changes the screen by under 1.5
brightness units in total (R4 51.0 to 51.8, S1 152.2 to 153.5, O1 136.7 to 137.6 over the whole day). With
`m_dBlueSky` moving 0.1 per game hour, cloud is not a visible lighting effect in this port.

One non-time-of-day effect showed up: in R4 the plant-health tint moved one palette entry (index 158, red channel)
by 2 to 3 per 100 ms for about 5 s, 134 s after the start, while the plants died. That is the health tint, not the day.

### 2.2 Mean brightness curve (0 to 255), sunrise and sunset

Time since 03:00 (sunrise) or 16:00 (sunset) is real seconds at 25x: 30 game minutes = 45 s.

| game clock | real s | R4 | S1 | O1 | | game clock | real s | R4 | S1 | O1 |
|---|---|---|---|---|---|---|---|---|---|---|
| 03:00 | 0 | 10.3 | 8.3 | 15.7 | | 16:00 | 0 | 51.0 | 152.2 | 136.8 |
| 03:30 | 72 | 15.2 | 25.7 | 29.1 | | 16:30 | 72 | 45.9 | 143.8 | 125.7 |
| 04:00 | 144 | 24.3 | 51.1 | 49.3 | | 17:00 | 144 | 38.5 | 132.5 | 111.5 |
| 04:30 | 216 | 33.4 | 76.3 | 69.4 | | 17:30 | 216 | 31.5 | 122.3 | 98.0 |
| 05:00 | 288 | 41.8 | 101.3 | 89.1 | | 18:00 | 288 | 24.9 | 112.1 | 85.0 |
| 05:30 | 360 | 50.8 | 127.0 | 109.6 | | 18:30 | 360 | 21.7 | 95.7 | 73.2 |
| 06:00 | 432 | 59.8 | 152.2 | 129.6 | | 19:00 | 432 | 19.4 | 78.7 | 61.7 |
| 06:30 | 504 | 61.3 | 158.7 | 135.8 | | 19:30 | 504 | 17.2 | 61.7 | 50.9 |
| 07:00 | 576 | 58.8 | 157.0 | 136.4 | | 20:00 | 576 | 14.9 | 44.4 | 39.6 |
| 07:30 | 648 | 56.1 | 155.3 | 136.3 | | 20:30 | 648 | 12.7 | 27.3 | 28.1 |
| 08:00 | 720 | 53.5 | 152.8 | 137.0 | | 21:00 | 720 | 10.5 | 10.4 | 17.3 |

(The rows are the palette on screen at that game time; the display trails the exact blend by 0 to one 5 s tick. The
two columns are not exactly mirror images because dawn and dusk are different palettes. The dawn palette is the
brightest of the day on all three locations: the morning half goes slightly down.)

Midday hour (12:00 to 13:00, clouds): R4 51.01 to 51.23, S1 152.21 to 153.50, O1 136.79 to 137.59, with 15, 31 and 31
distinct palettes.

### 2.3 Does the pacing of the run change the result?

R4 sunrise (03:00 to 08:00), the same script under different engine pacings:

| pacing | palettes shown | max step | median nonzero step | longest still s | step interval 500 / 1000 ms |
|---|---|---|---|---|---|
| default headless clock (16 ms) | 554 | 2 | 1 | 6.5 | 12 % / 75 % |
| window 33 ms pass, 60 Hz display | 569 | 2 | 1 | 11.5 | 74 % / 25 % |
| window 33 ms, 144 Hz display | 581 | 2 | 1 | 10.0 | 32 % / 61 % |
| window 10 ms | 554 | 2 | 1 | 9.0 | 89 % / 11 % |
| window 100 ms | 536 | 2 | 1 | 14.0 | 89 % / 11 % |
| window 33 ms, start skewed by 7 ms and 20 ms | 577, 581 | 2, 2 | 1 | 11.0, 10.5 | about 74 % / 25 % |

The step size and the number of distinct palettes (about 540 to 580) do not depend on the pacing. Only the cadence
(500 or 1000 ms) does.

### 2.4 The stairs: feature close-up pages (flag 0x20)

The same dawn and dusk run on the dead-biome gate page (`page DB9; page fdb9gate_c`) and the hatch page (`page DB1;
page Fdb1HTCH_C`), against the plain site page of the same biome:

| page | phase | palettes shown | max step | steps >= 8 | what happens |
|---|---|---|---|---|---|
| DB9 / DB1 site page | dawn / morning / evening / dusk | 317 / 140 / 80 / 163 | 1 / 1 / 1 / 1 | 0 | continuous (as in 2.1) |
| gate and hatch page | dawn 03-06 | 21 | 13 | 8 | still until 04:32, then 8 steps of 12 to 13 (brightness 12.5 to 47.1 in 5 s); still 3.9 min real |
| gate and hatch page | morning 06-08 | 28 to 30 | 7 | 0 | still 5.7 min, snap at 07:01 in 8 steps of 6 to 7 (55 to 47) |
| gate and hatch page | evening 16-18 | 34 | 4 | 0 | snap at 17:01 in 8 steps of 4 (47 to 30), 26 s still before it |
| gate and hatch page | dusk 18-21 | 20 | 15 | 8 | still 5.9 min, snap at 19:32 in 8 steps of 14 to 15 (26 to 15) |

Gate and hatch gave identical numbers. Longest stand-still: 232 s (dawn), 343 s (morning), 353 s (dusk). Lift and
cabinet pages have the same flag (`CLift` `a1|0x220`, `CGate` `a1|0x420`, `CHatch` `a1|0x620`, `CCabinet` `a1|0x20`);
I did not run those two. Leaving the gate page to the site page after the snap (04:31): the palette jumps by 53 per
channel at once (brightness 53.7 to 35.3) because the next redraw runs `UpdateDiurnal(1,1,0,1)` with an instant set. In my
script entering the gate page did not trigger a visible set (palette unchanged, 33.6 before and after), so I do not
claim a jump on entry.

## 3. Everything that is not a blend

All of these call `SetPalette` with steps 0 (instant, one frame) or run a blocking fade, from the code; the first
three I also measured.

| Event | What it does | Measured | Should it stay instant? |
|---|---|---|---|
| Entering a location with another palette set (R4 to S1, O1) | `UpdateDiurnal(1,1,0,1)`, one instant set | step of 204 / 197 per channel in one frame; R4 to R3 (same set): 0 | Yes. It happens at a page cut or cross-fade; the new picture appears with it. Fading the palette alone under the old picture would be wrong. |
| Entering a page with the same set | nothing | 0 | - |
| Feature page with flag 0x20, leaving it | instant exact blend | 53 per channel (DB9 gate, 04:31) | No: the snap should go with the fix of section 2.4; the jump on leaving then disappears. |
| Start of a time-of-day change after `settime` (debug) | instant set | exact blend at once | Debug only |
| Goggles on / off, and on every page redraw while worn | `CEngineFX_LoadGogglesPalette`: `LoadPalette(gogs, steps 0, immediate 1, token 1)`; the lock stops the day blend until the goggles go off | not run (code) | Yes: goggles are an item you put on; a short fade (0.3 s) would be a pure remaster choice. |
| Power on / off, lights (`siteLightsChanged`): group A to B palettes at the lit sites (Club Eden `lights\*`, dead-biome night, house sets) | `LoadPalettes` + `UpdateDiurnal(0,1,0,1)`, instant | not run (code) | Yes, a switch is instant; a 0.2 to 0.3 s fade would be nicer but not needed. Lamps without a palette group are images (`celights`), not palette. |
| Sensor panel, sleep screen, intro, title, death, game end, movies, demo palettes | `LoadPalette` with a token, steps 0 | not run (code) | Yes (scene changes). |
| Fades to colour (heat red, cold blue, green flash, sleep black, the journey) | blocking `FadeToColour` / `FadeToPalette`, 30/60/100 frames | not run (code) | Yes (already timed). |
| Special sites (EAngel, EDeath variants) | `CSiteVariant_OnTimeOfDayChange` ignores the time and toggles night and day palette on every tick, with the normal 5-step fade | not run (code) | Yes: deliberate flicker, not a lighting transition. |
| Lightning, rain flash | none found in the game logic (no palette call in the rain code) | - | - |
| Catch-up after a palette lock (goggles, sensor panel, sleep) | the next site redraw sets the time of day at once | not run (code) | Yes, or a short fade; the game time moved at most a few minutes. |
| Game clock jump after 10 s or more without ticks | clock restarts, no time passes | - | - |

## 4. Does the scripted clock pace the palette like the window?

Mostly yes. The `window HZ P` script command runs the same `Pacer` (`src/app/pacing.h`) and the same `Engine::update`
as `main.cpp`: a pass every 33 ms on the clock, `tick500` every 500 ms, the timed fade handled by `fade_.due` against
the clock. The numbers above (step size, distinct palettes) come from that, and they do not depend on pass interval
(10, 33, 100 ms), start phase or display rate (2.3). The plain `--script` without `window` steps the clock by 16 ms
instead of 33 ms; that changes only the 500 / 1000 ms mixture.

What could differ in the window:

- The real clock has jitter and the display swap at 60 or 144 Hz shifts when a pass runs; the script clock is exact. That changes the
  500/1000 ms mixture and the stand-still times by a second or two (2.3: longest still 6.5 to 14 s). It cannot change
  the step size, which is set by the palettes.
- The window shows a palette at the next display frame after the pass that set it (at most about 16 ms later); the
  script uses the palette of the moment. Not visible.
- The window turns the 8-bit palette into 24-bit colour without smoothing (`main.cpp:1057`); nothing the script
  would miss.
- I measured 100 ms samples of the engine clock, not of a real display. I did not run the SDL window.

## 5. Conclusion and the smallest change

- **The time-of-day light is continuous.** Per 125 game seconds the target moves by at most 2 levels per channel; the
  engine fades to it in 10 steps (500 or 1000 ms each) so the screen moves by 1 level at a time, 140 to 520 times per
  phase. No stair, no stand-still longer than 11.5 s in a moving phase. A "continuous blending" feature would add only
  fractions of a level, which 8 bits per channel cannot display, so it would give no visible gain. Do not build it
  for the open-air locations.
- **Where the stairs are:** feature close-up pages with flag 0x20 (lift, gate, hatch, cabinet), where the logic snaps
  between two palettes at half phase (steps of 4 to 15 per channel, stand-stills of 0.4 to 6 min real, one jump of
  53 per channel when the page is left), and the instant scene changes of section 3, which are deliberate.
- **Smallest change:** not a presentation-side interpolation, because the engine never receives a mixed palette on those
  pages; it receives `a` and then `b`. One line in `CEngineFX_Blend` (`bg_enginefx.cpp:168`): treat the 0x20 case as plain
  (blend) behind a fixes flag. The reason why the original snaps there is not in my sources (it may be because the
  F-page images use lookup tables that expect the whole palette of one slot); check it on those four feature pages
  before the change goes into the faithful level. If that is wrong, the fallback is the presentation-side one: on
  those pages, interpolate the displayed palette from the one on screen to the new one over 2 s instead of the 5 s
  snap fade. It only makes the existing 8 jumps per day softer and does not remove the 3 to 6 min stand-still.
- A presentation-side interpolation over the 0.5 to 1 s fade step interval for ordinary pages has nothing to remove:
  the step is already 1 level.

## 6. Limits

- The three locations have plant or ocean tint (R4, O1: health decays while the clock jumps; S1 stays at about 0.95);
  the tint adds a small drift to the numbers for R4 and O1. S1 shows the same step sizes, so the conclusion does not depend on it.
- Starting each run with `game settime` is a debug jump of the clock; the plants and sky have to catch up, which is
  why the first minutes of a run were left in the trace but analysed only inside the phase window.
- One seed (1). The cloud numbers depend on the weather the seed gives (0.1 per game hour is the bound).
- Lift and cabinet pages, goggles, power cuts and the sleep fade were taken from the code, not run.
- Scripts and raw traces of this measurement are in the scratchpad of the session, not in the port.
