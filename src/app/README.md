# src/app: the SDL3 front end

`main.cpp` (window, input, options), `script.cpp` (headless `--script`), `audio_sdl.*`, `clean.*` (the picture cleaner's workers and compositions, SDL-free), small SDL-free helpers (`menulogic.h`,
`windowlogic.h`, `savesettings.h`, `pacing.h`, `prefs.h`, `autosave.h`). SDL is only used here.

## Options and the preferences file (`prefs.h`, `optionTable()` in `main.cpp`)
One table (`OptTable<Options>`: command-line name, preference key, kind switch / value / optional value, default, help line, a setter from text
and a getter to text) drives everything: `parseArgs` (also `--name=value`), the preferences file, the generated default file and
`--write-config`; a key is the option name without the dashes, command-line-only options have an empty key. `loadOptions` reads the
command line twice: the first pass (nothing from the file) finds `--config`, `--no-config`, `--save-dir` and whether the run is headless
(`--script`, `--screenshot`, `--selftest-all-pages`), the second starts from the file's values and lets the command line override them.
`biosys.conf` lives in the save folder; a window start without one writes the commented default file; a headless run ignores the file unless
`--config FILE` names one. Lines that are not understood are reported once and ignored (`OptTable::apply`). At exit `saveRuntimePrefs`
rewrites only the keys `volume`, `start-muted`, `fullscreen` and `display`, and only if the run changed them (`updatePrefsText` keeps comments and
unknown lines). Test aids (command line only): `--quit-after-ms N`, `--test-keys LIST` (key names pushed as real SDL key events 300 ms
apart from 1.5 s on: minus, equals, backslash, f5, f6, f9, pagedown, pageup, escape, return); with `SDL_VIDEODRIVER=dummy
SDL_AUDIODRIVER=dummy` the window runs without a display (`tests/test_prefs.sh`, `tests/test_autosave.sh`).

## Autosave (`autosave.h`, `Setup::autosave`, `BiosGame::saveBlocker / saveToFile / loadFromFile`, `src/game/bg_autosave.cpp`)
`AutosavePlanner` is the state machine (play time, wake edge, enabled); main.cpp's `autosaveTick(Setup&, dtMs)` runs after every frame of the
window loop and after every update step of a script's `advance`; it advances the planner only while the game runs and the engine is not
`mediaPaused()`, and makes the due autosave when `saveBlockerOf` says "" (no blocking operation, text entry, title screen, status window,
terminal or menu; the game's own `saveBlocker()`: running, not dead, not ended, awake, no vision / sequence, not on a 'U' / 'X' site: the
conditions of the F5 key and of menu command 2 in dll:1001e6b0 / dll:10008010). At quit the game menu (where Quit was chosen) is allowed.
`runAutosave` writes `autosave0.sav` through `BiosGame::saveToFile` (= Eng_SaveGame with the sounds off, so `FrontLogic::onSaveGame` writes the
settings block, see below), then `autosave::rotate` moves 2->3, 1->2, 0->1; any failure is reported on stderr and leaves the old files alone.
**Clock:** the planner counts play time handed to it, not a clock. The window hands it real milliseconds per loop turn (not while paused, a
gap above 2 s counts as 2 s); a script hands it the injected milliseconds of every update step of `advance` (`ScriptHooks::tick`). A
headless run autosaves only when `--autosave` or `--autosave-minutes` is given (or in an explicit `--config` file), so no test writes into
a save folder by itself. Script command `autosave status | minutes N | on | off | now | quit | load [N]`.
`--load-autosave [N]`: after the usual start-up the new game is replaced by `BiosGame::loadFromFile` (= case 3 of the menu command with the
autosave's file name, no menu). F9 (`DoublePress`, 3 s) calls the same.

## Save-slot pages (`Engine::setSlotPages`, `engine_menu.cpp`)
`MenuState::slotPage` (0-based). Slot k of page p (0-based) is slot p * 10 + k for the game logic (events 31 / 32 and so the DLL's file name
`saved game N.sav`): page 1 is exactly the original (it still asks about slots 1..20), page p > 1 asks about its ten slots. Page Up / Down
(0x21 / 0x22, only read by the menu; the front end forwards them only while `gameMenuOpen()`; the wheel sends the same keys) reload the
menu page, which redraws the slot texts; they do nothing during a question. The indicator "page p/N" is drawn with the menu's font and colour
into the strip x 440..629, y 26..48 right of the title (nothing of the art of menusave / menuload is there; see the check in test_autosave);
the window title also says it. Not drawn with one page. `dump menu` prints `slotpage p/N` (0 = not on a slot list).

## Pause, mouse sensitivity (`windowlogic.h`)
`PauseState`: pause = minimised, or lost focus with `--pause-on-focus-loss`; an edge calls `Engine::setMediaPaused` (the original's inactive
application: media stop, API 0x29 / 0x2a answer 0x15, no idle event, the game's timeline pauses through event 12) and the audio device;
a minimised window is neither drawn nor stepped. `MotionScaler` scales the motion deltas fed to `Engine::mouseMove` only while the pointer
is captured and not in bar mode, carrying the rounding remainder; factor 1 is the identity.

## Pace (`--pass-ms N`, 10..100, default 33; `pacing.h`)
The window loop and the engine's pump (`Engine::setPump`, used by every blocking operation) run on one schedule, `PassClock`: one engine
idle pass (`Engine::update`) every P ms on the real clock, never faster, never in a burst after a stall. `Pacer<Host>::step` is one turn of
the main loop (events are handled by the caller at the display's rate; the pass only when due; the frame is drawn and presented every turn;
`idle(1)` when the display does not wait for the vertical sync). `Pacer::pumpTurn` is the pump: a whole-pass turn presents the new state
at once and keeps re-presenting it (the displayed smooth camera moves) until the next pass is due, so a zoom frame, a cross-fade step or a
jiggle step lasts P whatever the refresh rate; a clock wait of the original (`Engine::pumpMaxMs()` != 0: the delay of a palette fade step,
the journey's busy wait through `Engine::waitBlocking`) lasts that long, not a pass. The engine's scroll step and the mouse-delta decay
use `Engine::setPassMs(P)` (16 ms headless), the held arrow keys scroll once per pass, motion events add up between passes
(`setScrollAccumulate`), and the smooth camera glides over P and through zoom frames (`PanoCameraSmoother::setStepMs / setZoomGlide`).
Headless scripts do none of this (injected clock, `pumpstep`); `window HZ [P]` in a script runs the same Pacer on a fake clock and a fake
display (`wreset`, `wstats`, `wtime`, `wtrace`; `tests/test_pacing.sh`).

## Smooth panoramas (`--panorama smooth`, `--panorama-filter linear|nearest`; `--dev`: F3 / F4)
An optional second output path for the 84 panoramas; the default (`original`) and everything the tests of the 8-bit frame see are unchanged.
* `engine/engine.h` `Engine::panorama()`: after `render()` says whether the frame shows a projected page, the view rectangle, the camera
  (`PanoCamera`: yaw, pitch, fov, focal, the same numbers `projection.cpp` uses, also in the zoom animations), the page surface, the palette
  and `bare`, the engine's own 640 x 368 projection of the bare page. A view pixel where the final frame differs from `bare` was drawn over
  the panorama (held object, sprites, wearing overlays, rain, text, outlines): those stay the original pixels.
* `engine/panomesh.*` (SDL-free, unit-tested by `build/biosys-panotest`): `panoMap` (exact cylinder for a view point; pixel n has c = n + 0.5
  - 320.5, so the picture is the original's with more samples, not shifted), `buildPanoMesh` (grid of 8 / 4 / 2 view pixels by output scale,
  texture coordinates exact per vertex, triangles cut at the x seam), the page texture with a one-texel wrap border, a CPU rasteriser of the
  mesh (bilinear), `composeSmooth` (the headless capture), `PanoCameraSmoother`.
* `panorama_smooth.*`: `PanoramaLayer` keeps the page as an RGBA texture (re-uploads only changed rows, or everything when the palette
  changed) and draws the mesh with `SDL_RenderGeometry` under the usual 640 x 480 frame, whose bare-projection view pixels are transparent.
  `captureSdlSoftware` does the same on an off-screen surface with SDL's software renderer (nearest sampling only: SDL's software renderer
  cannot filter, so filtered captures use the CPU rasteriser).
* Camera: the engine's scroll changes in whole pixels at its idle passes (`Engine::scrollStep`, mouse deltas halve each pass; P ms, see Pace); the
  shown camera glides linearly from the camera of the engine's step time to the new one in P ms (16 headless) (never ahead, never more than one step behind,
  exact afterwards). It snaps on a page change, at the first frame and for a jump of more than 160 pixels; in the window it also glides (position, field
  of view, focal length) from one frame of a zoom animation to the next; headless traces (`smoothtrace`) still snap in zoom frames.
* Script: `panoinfo`, `smoothshot FILE SCALE [nearest|sdl] [frame=F] [bare=B]`, `barebmp`, `smoothtrace DT|off`. Test: `tests/test_smoothpano.sh`
  with the independent oracle `tests/smoothpano_oracle.py`.

## Widescreen panoramas (`--widescreen`; `engine/panowide.*`, `PanoramaLayer::plan / drawPlanned`, `main.cpp`; `tests/test_widescreen.sh`)
Option / preference `widescreen` (default no, implies `--panorama smooth`, dev key F7). On projected pages the view is drawn across the window
width at the same scale, vertical extent and focal length (the horizontal field of view grows: 91.3 degrees at 16:9, scale 2); everything else is 4:3.
* Geometry: `buildPanoMeshRange` / `rasterizePanoRegion` extend the same mesh grid (same cells, same triangles) so the centre 640 * s columns are pixel
  identical to `composeSmooth`. Window: `wideWindow` (scale of the 4:3 picture, `extra` whole frame columns per side), the renderer's logical size
  is (640 + 2 * extra) x 480 on wide frames, 640 x 480 otherwise (`setLogical` in main.cpp, set after `PanoramaLayer::plan` and before the clear); pointer
  positions go through `fromWindow` / `toWindow` (`widePointerFrameX`, `wideClipToLogical`: the pointer and the engine's clip rectangle stay on the centre).
* Cap: `kWideMaxFovDeg` = 110 (edge stretch 1/cos^2 = 3.0; 16:9 is 91 degrees, 21:9 is 108 and filled; 32:9 gets black strips; it was 100 at first, which left black on all four sides of a 21:9 screen). Scroll limits (API 0x5a, `PanoramaFrame::limitsOn`,
  only the transit tubes T1 T6 T7 T12: scroll 469..959): the extent narrows, symmetrically and in closed form (`wideExtra`), so nothing beyond limit + the
  original half-width is shown; 4:3 at the limits. The vertical limit needs nothing: the edge columns never show more rows than the centre column.
* Fallback (`WideController`): more than 30 % of the 16 outermost view columns at either edge covered by non-rain overlay pixels (the render without the
  page filter, `Engine::renderNoFilter`) -> 4:3 at once; back after 8 frames below 10 %. **The held object is not counted**: with `Engine::setSplitLayers(true)` (the
  wide paths of main.cpp, `overlay frame`, `wideshot`) render() leaves the held image out of the frame when that is exactly separable (`Engine::viewLayers().held`, `engine/viewlayer.h`;
  checked by drawing it over the frame without it and comparing with the normal draw order; not separable: cross-fade, status window, a ClearWindow fill, a sprite or wearing overlay over it),
  the cover is measured on that frame, and the image is blitted back at its own place (`ViewLayers::blitInto`) before anything is drawn, so the hand stays cut at the border of the centre
  columns, as in the original. Draw order of render(): page, page filter (rain), synth movies, held image, fixed sprites, wearing overlays, status window: the rain is under the hand.
  Before this a held compass (about 25 % of the right strip) kept the picture at 4:3 for good once any fallback had started. Script `heldlayer` prints the invariant.
* Status screen with a palette of its own (Tab; `Engine::simStatusOpaque()`): `WideInput::keep` (the cover is not tested, the controller keeps its state: wide stays wide, a fallback such as
  goggles stays), `PanoramaLayer::setStatusScreen` (no page-texture upload with the status palette, the camera follows, the centre is opaque), the cleaner gets no page (`CleanFront::step` with
  no panorama: the frame is cleaned as an opaque flat picture, so no re-clean sweep after the release; also in the 4:3 window), the side columns are dimmed to
  `kWideStatusSideBrightness` = 0.4 (a black layer of alpha 0.6). The CPU twins (`wideshot`, `overlay frame` / `shot`, `cleansim`, `overlay clean on|stats`) do the same; `tests/test_heldwide.sh`.
* Rain: `panoRainMap` (frame != no-filter frame) continued by `wideSideRain` (mirrored about interior columns, turned vertically by a different amount per
  side), drawn at the frame scale; in the window as opaque pixels in the side columns of the wide overlay texture.
* Script: `wideshot FILE SCALE WIDTH [nearest] [plain=F] [cols=F]` (one call = one frame of the hysteresis), `wideanim PREFIX SCALE WIDTH|off`;
  test aids `--window-size WxH`, `--test-shot FILE` (with `--quit-after-ms`, SDL dummy driver); unit tests `build/biosys-widetest`.

## Sound: `--audio original|enhanced`, `--volume*`, volume keys
`engine/audio.*` (`Mixer`) has two output paths, chosen by `Mixer::setMode` (main.cpp sets it from `--audio` in `makeEngine`):
the original one (linear interpolation, float sum without intermediate clipping, rounded and hard-clipped to int16) is untouched and
bit-identical with all volumes at 100; the enhanced one (Kaiser-windowed sinc resampler, 3 ms gain ramps, ghosts that fade out a
restarted or closed voice, soft-knee saturation above 0.8 full scale) is float all the way (`Mixer::mixFloat`). `audio_sdl.cpp` opens
the device as F32 at the device's own rate (`SDL_GetAudioDeviceFormat`, then `Mixer::setOutRate`) in enhanced mode, as S16 / 44100 Hz
otherwise (not tested on a real device). Volume groups: `VoiceGroup::Effects / Movies / Terminal` (a sound whose file starts with
`bs4sound\` is Terminal, a movie's audio track Movies, the rest Effects), `Mixer::setGroupVolume / setMasterVolume / setMuted`
(percent, linear; changes ramp within one 256-frame block). Keys (`windowlogic.h` `volumeKeyOf` / `stepVolume`, unit-tested without a
window by `build/biosys-audioenh selftest`): minus / keypad minus quieter, equals / plus / keypad plus louder, backslash mute; ignored
while a menu or text entry is up; the title shows the level for 1.5 s. Tests: `tests/test_audioenh.sh` (independent Python FFT /
Goertzel analysis; `biosys-audioenh render ... | bench | selftest`).

## Sharp text (`--text sharp`; `app/text_sharp.*`, `text_sharp_sdl.*`; `tests/test_sharptext.sh`)
An optional second output path for the GDI text (everything `Canvas` draws: top-bar messages, status screens, machine displays, menu slot
names, text entry, the terminal's clock and numbers); sibling of the smooth panoramas, default (`original`) and every 8-bit frame unchanged.
* Engine side (SDL-free, nothing recorded unless `Engine::setTextTracking(true)`, which main.cpp does for `--text sharp`): `engine/textplane.h`
  states the rule. A `TextPlane` hangs off every `Surface` that `Canvas` draws text into (`Surface::text`) and keeps per pixel the last text
  pixel (ink index, the pixel and layer mask it replaced, the run that drew it) plus the runs (`TextRun`: string, `Font` (shared), position after
  alignment, clip, ink, COLORREF, align, opaque). A record is LIVE while the surface still holds its ink (and the layer mask is set): any
  overwrite ends it without a hook (RedrawPage, DisplayImage, sprites, engine fills, later text), a sprite's save-under restoring the ink revives it;
  `Canvas` itself ends the record of everything else it draws (fills, lines, ellipses, opaque text backgrounds); a rebuilt page / reset layer /
  re-initialised surface clears the plane. Known blind spot: a non-Canvas write of exactly the ink value keeps a record live.
* `Engine::renderText(TextFrame&)` (`engine/engine_text.cpp`): strips the live text of the shown surfaces (puts the pixels beneath back,
  clears layer masks), renders (`free`, without the page filter, so its drops move once per frame), restores, renders again (`full`, the
  frame `panorama()` describes). `mask` = `full != free`; the runs of every shown surface are listed in screen coordinates (page surface
  at -scroll + view top, top-bar / popup layers, sim-status window, menu pages, the terminal's page surfaces at their offsets and its clock on
  the back buffer, whose text-free twin `Bs4::lastFlippedTextFree` is composed at every flip) with the pixels each owns that show in `full`
  as the ink. A difference the list cannot account for (the rain filter's drops, a text pixel on a projected panorama page) is put back
  into `free` and counted in `orphans`: it stays low-resolution.
* `Font::glyphScaled(c, k)`: the same face at ppem * k, FreeType anti-aliasing, unhinted. `renderTextOverlay` draws each run with the pen
  at (low-resolution pen) * k and the baseline at (y + ascent) * k, so advances, centring and right alignment are the 640 x 480 layout
  (including `panelNumberAlign`'s odd right edge: the run's x is what the low-resolution text used). A glyph pixel is drawn only inside the run's
  cell, clip and the 3 x 3 neighbourhood of the run's own visible text pixels: what the frame draws over text stays over it.
* Window: `SharpTextLayer` (`text_sharp_sdl.*`) keeps the overlay as one ARGB texture of the output size, rebuilt when the signature of the
  runs (text, positions, visible pixels, colours) or the scale changes, and `main.cpp` draws `free` as the frame (under the smooth panorama
  if that is on: `free` is what its transparent view pixels are cut from) and the overlay over it.
* Left low-resolution: text on a surface shown through the cylindrical projection (the page surface of the 2560 x 1024 panoramas; it
  turns with the view), the frozen menu frame during a fade, the title picture, anything the list cannot account for (`orphans`).
* Script: `sharptext on|off` (before anything is drawn), `textframes PREFIX` (`_full.bmp`, `_free.bmp`, `_mask.bmp`, `_runs.txt`),
  `textruns FILE`, `textredraw FILE.bmp` (the text-free frame with the runs' low-resolution glyphs drawn again where the mask is; prints
  `diff=` against the frame), `textshot FILE.bmp SCALE [PREFIX]` (CPU composition of the sharp frame, 24-bit; PREFIX also saves the frames
  and run list of that very render). Test: `tests/test_sharptext.sh`.

## Clean pictures (`--picture original|clean`; `engine/picclean.*`, `app/clean.*`, `PanoramaLayer::setClean`, `main.cpp`; `tests/test_picclean.sh`)
An optional second output path, last in the family of smooth panoramas / widescreen / sharp text; default (`original`) and every 8-bit frame unchanged.
`--picture clean` implies `--panorama smooth` and `--text sharp` (smooth enlargement would soften the pixel-stepped typeface; the text is drawn by the
sharp layer on top; the cursor is the OS cursor and stays crisp). Developer options (command line only): `--clean-texture 1|2` (panorama texture scale, default 2),
`--clean-slew N` (0..16, default 1, see below).
* **The filter** (`engine/picclean.h/.cpp`, SDL-free, specified by `upscale-trial/light_spec.md` and its `reference.py`, reproduced byte for byte: the five vectors and
  more inputs in the test): `dedither` (four 2x2 boxes, partner test on the palette colours, `T = 14 * g`), `keyFill`, `enlargeRgb / enlargePlane` (Catmull-Rom, integer
  weights, scale 1..4). A *skip mask* generalises the reference's key index and protect mask (not filtered, not part of a neighbour's average): a keyed object, the
  transparent view pixels of an overlay over a panorama and film frames are skip masks. `biosys-picclean run | weights | bench | flicker | soft` is the tool.
* **Engine side** (`Engine::cleanInfo`): `Page` (de-dither) or `Terminal` (BioSys 4 frame: enlargement only), the rectangles of the film frames (enlargement only),
  the page serial, its surface (whole-page histogram for `g`) and its day palette (`PageView::dayPalette`: palette set group A, slot day; the reference of `g`;
  none for fixed-palette pages: then `T = 14`). Panoramas: the threshold of the overlay frame and of the page texture come from the same histogram and palette.
* **Two workers** (`app/clean.h`, `CleanService`; one thread each, so a 30 ms frame job never waits for a 70 ms page job): the *page* stream cleans the 2560 x 1024
  page (de-dither with wrap, Catmull-Rom by `texScale`; 2 = a 5120 x 2048 texture, 1 = de-dithered 1x with the GPU's linear filter) and delivers bands of 64 page rows
  (`CleanBand`, with the one-texel border); a changed palette or a new page recomputes everything, changed page rows (an animation painted into the surface) only
  those rows and their neighbours (de-dither rows +-1, enlarged rows +-2). The *frame* stream cleans the 640 x 480 frame into 640k x 480k (k = min(scale, 4)), over a
  panorama with transparent view pixels (frame == bare projection), key fill under them (6 passes; the enlarged alpha hides the rest) and the alpha plane enlarged by
  the same kernel. A job replaces one that has not started (newest wins; pending page rows are merged); the old result stays on screen until the new one is ready.
  The main thread (`CleanFront::step`) copies what changed (a memcmp of the page against its mirror, the frame), takes finished results and uploads at most
  8 MB of bands per frame (`popUploads`). Until all bands of a new page have arrived the plain smooth texture is drawn (`PanoramaLayer`: `applyCleanBands`, `useClean_`);
  a result that was made for another scale or for "panorama under / not under" is not drawn (the plain frame for a frame or two).
* **Panorama texture: 1x or 2x?** Decided for 2x (`texScale` 2, 5120 x 2048, the default; `--clean-texture 1` gives the other). Pictures: `build/shots/clean_savannah_sky_tex1.bmp`
  against `clean_savannah_sky_clean.bmp` (and the rainforest start page), 3x. Numbers (S1 at 3x, view area): mean gradient of the luma 5.40 (1x texture) / 5.60 (2x), mean absolute
  luma difference between the two 1.0 grey level; at 4x the leaf edges and the truss of R4 / S1 are visibly crisper with the 2x texture, at 2x output scale the two are hard to tell apart.
  Cost: the page thread needs about 33 ms (1x) or 60 ms (2x) per full refresh, the texture is 10.5 MB or 42 MB on the GPU, the upload of a refresh 10.5 MB or 42 MB (in bands).
  Nothing of it runs on the frame loop, a refresh is needed only at a page change, a palette change and for rows an animation repaints, so the 2x texture was taken: the
  extra work is invisible to the player and the gain is a little sharpness at the window sizes where the dither would be seen most. The 1x texture is the fallback for a weak GPU.
* **Flicker remedy** (`PartnerState::updateSlew`, soft weights 0..32 in the `dedither` soft path): the reference's partner decisions are discrete; while the light is
  mixed a pair of colours crosses the threshold and a patch of sky changes from dithered to smooth in one palette step. The partner weights follow the reference's decision
  by at most `slew` / 32 per palette change (about 0.5 to 1 s apart in dawn and dusk), so a patch fades over some seconds; a new picture or a palette that moved by more
  than 4 in a channel (lights, goggles, a new place) takes the reference's decision at once. With `slew = 0` the decisions are the reference's every time (the headless
  tests of the picture use whatever the option says; a static picture is identical either way). Numbers: MANUAL.md 9.9, `tests/test_picclean.sh` part E.
* **Measured cost** (this machine, scalar C++, `build/biosys-picclean bench`, mean of 20; replaces the spec's estimates, which were 40-80 ms de-dither and 60-100 / 130-200 ms
  for the 2x / 3x enlargement at 2560 x 1024; the tap count of the enlargement is a template parameter, which made it three times faster than the estimate, no SIMD needed):

  | stage | 2560 x 1024 | 640 x 480 |
  |---|---|---|
  | index histogram (new page only) | 3.6 ms | 0.4 ms |
  | threshold + partner table (palette change) | 0.05 ms | 0.05 ms |
  | de-dither (reference decisions; the soft table of the slew remedy costs a little more while weights are fractional) | 28.4 ms | 3.3 ms |
  | Catmull-Rom 2x / 3x / 4x | 29.3 / 49.4 / 78.5 ms | 3.4 / 5.8 / 9.2 ms |
  | a full refresh of a panorama page (de-dither + 2x), on the page thread | about 60 ms (a palette change) | |
  | a changed frame (de-dither + 3x), on the frame thread | | about 9 ms (4x: 13 ms) |

  Measured in the fake-clock loop (`cleansim`, a sunrise on S1 at scale 3, 9136 frames): the longest main-thread step (diff, copy, take, upload budget) 0.5 ms, the longest
  call into the service 0.4 ms, while the longest page job took 85-150 ms and the longest frame job 16-36 ms (with other jobs running on the machine). Not measured: the cost
  of `SDL_UpdateTexture` on a real GPU (the dummy driver's software renderer is no guide); it is bounded by 8 MB of bands (2 x 4 MB) per frame.
* Script: `cleanshot FILE SCALE [tex=1|2] [slew=N] [nearest] [plain=FILE] [smooth=FILE]` (the CPU composition of what the window draws, 24-bit; the sharp text on top
  when tracking is on; plain = today's block enlargement, smooth = `--panorama smooth --text sharp`), `wideshot ... clean`, `cleansim on [tex=N] [slew=N] [scale=N] | wait |
  stats | check | off` (the workers inside the `window HZ P` fake-clock loop; `stats` prints the longest main-thread step, `check` compares texture and frame with a fresh
  computation), `dumppal FILE`, `daypal FILE`.

## Click-animation tests (`tests/test_clickanim.sh`)
Script commands for them: `wshots PREFIX [SCALE]` (in `window` mode: every presented frame whose content changed as PREFIXnnn.bmp, with SCALE
also the smooth picture of a projected panorama from a persistent `SdlSmoothSession`, i.e. with all the incremental page-texture updates, and
whether it equals a fresh upload), `wregion X,Y,W,H` (a rectangle of the page surface, written as .reg with every wshot), `soundtrace on|off`
(every start of a sound), `framehash`, and the game command `game fuses` (all fuses in, for the machines' power).

## Screens and the released mouse

* `--display N|NAME|list` (preference `display`): `listScreens()` puts the main screen first; `pickDisplay` / `nextDisplay` /
  `displayPrefFor` (windowlogic.h, unit checks in `biosys-windowtest`) choose and remember the screen. The window is created as a
  normal window centred on that screen and made full screen afterwards, so full screen is on the screen the window is on.
* Option+N (`moveToScreen`): leaves full screen, moves, shrinks to the largest whole-number scale that fits, enters full screen
  again; `SDL_SyncWindow` after each step. Checked with the real window on a two-screen setup through `--test-keys alt-n,...`
  (the program prints the screen and the pixel size after each move).
* Option+M (`releaseMouse`): a third pause reason (`PauseState::released`); while it holds there is no pointer warp, no mouse
  rectangle, no mouse or game key input. A click in the window takes the mouse back and is swallowed. A focus gain after a focus
  loss with the pointer outside the window's content releases the mouse too (not in scripted test runs, and not within 3 s of the
  program's own window moves).

## The game mode inside the save (`engine/savetrailer.h`, `savesettings.h`, `FrontLogic::onSaveGame / onLoadGame`)
After the game's blob (a fixed 179017 bytes) `FrontLogic::onSaveGame` appends a block: `BSYSMODE`, u32 version 1, u32 length, the settings text
(`settingsText`), u32 FNV-1a check sum. It is found from the end of the blob (`savetrailer::inspect(file, off + SaveGame::blobSize())`), not searched for.
`BiosGame::onLoadGame` and `SaveGame::blobOf` end the blob there (`savetrailer::blobEnd`: only if the extra bytes are, or begin like, our block; other extra bytes are
refused as before). Loading (`readStoredSettings`): the block if valid and understood, else the settings file of earlier versions (`readSidecar`, hash of the file
bytes), else the options (`cmdline`). Damaged = reported, absent. `NewGame.sav` and files that are not `.sav` get no block. No sidecar is written; after a successful
save the slot's old one is removed (`dropSidecar`; `Engine::saveFileDropped` is no longer used). Script `menu trailer off|on` (test aid) writes saves without the block.

## The settings menu and the New game screen (`settingsmenu.h`, `settingsmenu_draw.*`, `main.cpp` "the settings menu", `tests/test_settingsmenu.sh`)
* `settingsmenu.h` (SDL-free, header only, unit-tested by `build/biosys-menutest`): the layout on the 640 x 480 grid (`kPX.. rowRect, tabRect, ngBlockRect`), `Model`
  (items by preference key: Choice / Range / Info, `When` = Now / NextStart / NextNewGame / Fixed, `load(key -> text)`, `change`, `takeChanges()` -> `Change{key, text}`,
  the rules between picture items), `NewGameModel`, and `Controller` (open / close, `key`, `mouseDown / Move / Up`, `wheel`, `revision()` that changes with everything drawn).
  Dragging a slider is `dragging()`; hover rows / buttons for the picture.
* `settingsmenu_draw.*` (SDL-free): `MenuFonts` (BIODATA\FONTS\JON.TTF as "systemfont" in an own `FontLibrary`, so the engine's font handles are not touched; cell heights 26 / 18 / 16),
  `renderMenuLayer(controller, fonts, k, hint, argb)` = ARGB of 640k x 480k (glyphs through `Font::glyphScaled`, laid out with the 640 x 480 integer advances like sharp text),
  `composeMenuPicture` (CPU: frame at the whole-number scale, centred on black, darkened by `kMenuDim`, layer over it). The window uploads the layer as a texture
  (`drawMenuLayer`, rebuilt when `revision()`, the scale or the hint changes) and draws a black rectangle of alpha `kMenuDim` under it; it is drawn last, after the sharp text,
  into the renderer's logical canvas at column `wideK`, so never into the engine's frame.
* Host (`main.cpp`): `Setup::cur` = the options as they are now; `menuRefresh` loads the model from `menuLiveOptions` (options + mixer + planner + `snapshotWindow`);
  `menuApplyChange` sets the option through the option table (`OptSpec::set`), the engine (mixer, `setPassMs`, autosave planner, `setSlotPages`, `setTextTracking`, `cmdline`) and, in
  the window, `applyWindow` (panorama / filter / widescreen locals, `CleanFront` and `SharpTextLayer` created or destroyed, full screen, `moveToScreen`, audio device reopened,
  `pacer.clock() = PassClock(ms)`, `MotionScaler`, `PauseState::setPauseOnFocusLoss`); the preference goes into `pendingPrefs` and `menuFlush` writes it (`updatePrefsText`; not
  during a slider drag; nothing with `--no-config`). `saveRuntimePrefs` at exit compares against `Setup::cur`, which holds what the menu wrote.
* Input: while `Controller::isOpen()` every key, click, motion and wheel event goes to the menu (`swallowedKeys`: the release of a key the menu took does not reach the game
  either; a click that closes it sets `swallowButtonUp`). F10 / Cmd+, are never passed to the game (it would type F10 into its cheat buffer). `PauseState::menu(bool)` is the new
  pause reason (`isMenu()`): no pointer warp, system cursor, `Engine::setMediaPaused`, no arrow-key scrolling, no autosave play time. Not while `textInputActive()` or `blocking()`.
* New game: `FrontLogic::onMenuCommand(1)` with `newGameScreen` set (window, `--game`, no `--skip-intro`, no `--load-autosave`) only sets `pendingNewGame`; `menuPoll` opens the screen;
  Start calls `FrontLogic::startNewGame(realism, difficulty)` (sets `cmdline`, then forwards the command: `onLoadGame` of NewGame.sav applies `cmdline` through the same `apply`
  that a load of a save with other settings uses); Back leaves the engine's menu as it was.
* Script (`menuCommand`): `menu open | close | poll | key NAME [shift] | tab NAME | click / down / up / move X,Y | drag X,Y X,Y | wheel X,Y up|down | state | items | engine |
  options | shot FILE [W H] | newgame on|off | hint on|off | trailer on|off | textcheck`. Test aids of the window: `--test-keys` also takes f10, up, down, left, right, tab,
  home, end, comma, space, the prefixes `alt-`, `shift-`, `cmd-`, and `click@X:Y`, `drag@X0:Y0:X1:Y1` (grid coordinates); with `--test-keys` the window prints
  `menulive ...` lines (its own state after each menu change, open and close).

## The overlay interface (`--interface overlay`; `overlaylayout.h`, `overlaycompose.*`, `overlay_sdl.*`, `overlay_script.cpp`, `tests/test_overlay.sh`)
Option / preference `interface` (`original` default, `overlay`; implies `--panorama smooth --widescreen`; row "Interface" in the Picture tab, applies now; the menu
model forces panorama / widescreen when it is switched on and the interface back to `original` when either of them is switched off). With it off nothing of
this exists in the frame path (`drawFrame` starts with `overlayOn && e.viewFrame()`), so every frame is byte-identical to before (checked in the test).
* **Layout** (`overlaylayout.h`, SDL-free, `build/biosys-overlaytest`): `layoutFor(W, H)`: view scale `k = min(H / 368, W / 640)` (not whole), the 640 x 368 view
  centred, interface scale `u = max(1, round(0.75 H / 480))`, the strip (640 u x 56 u, bottom centre), `extraCols` = whole frame columns that fit beside the 640.
  Pointer maps: `engineToWindow` (view rows through the view, rows >= 424 through the bottom strip `y = H - (480 - py) u`, rows < 56 through the top),
  `windowToEngine` for the free pointer (linear, confined to the view, the last `kDockPx` rows of the window are the dock: bar rows, x through the strip; no dock at
  the top because the top bar has no hotspots, checked). `MotionCarry`: captured-pointer motion in window pixels divided by `k` (in bar mode by `u`), the
  remainder carried (the pointer is warped back after every frame, so without the carry slow motion would be lost). The vertical release uses the same deltas
  (`overlayCapturedMove`: pointer row = engine pointer row + delta; no inverse mapping); `Engine::verticalRelease` and the rest of the engine are unchanged.
* **Frames** (`Engine::viewFrame()`, the only engine change besides `StatusBar::flashing()` and `Font::glyphScaledF`, which are read-only additions): the frame has the usual 56 / 368 / 56
  layout, i.e. no title, menu, frozen frame, terminal, full-screen page. Other frames go through the old path (the renderer's logical presentation 640 x 480 is
  switched back on; while an overlay frame is drawn it is off and render pixels = window pixels, `overlayShown` selects the pointer maps in `fromWindow` / `toWindow`).
  With `--interface overlay` the text tracking is on (`Engine::renderText` supplies the "topbar" runs).
* **Scene** (`OverlayScene`, SDL-free): the runs of layer "topbar" with y < 0x24 are the status words (black ink = not shown), the run at y 0x24 the message; each is
  drawn again (`buildTextSprite`: the glyphs at `u`, a dark outline of radius `u` = coverage dilated by a disc, a soft halo = that blurred three times) and
  placed at `W/2 + (x - 320) u` (words: top; message: its cell bottom 2 u above the strip). Faders: message in 120 ms / out 250 ms, strip in 120 / out 300 (`ovl::Fader`);
  the strip is wanted while `barMode()`, `bar().flashing(now)` or the pointer is in the bar rows (`stripWanted`). The strip sprite is rows 424..479 of the frame
  (so stack counts are as today) at `u`, nearest, with the black connected to the rim made transparent (flood fill; black inside an icon stays) and kept while it fades out;
  its gradient (alpha 0 to 0.6 over 1.4 strip heights) is drawn as a quad. The ambient fill is the view's rows reduced to 64 x 37 (area average), blurred twice (3 x 3),
  rebuilt when the picture or the palette changes (hash); drawn at 35 % brightness beside the picture, brighter towards it over `24 u` pixels (a second, alpha-ramped quad).
* **Drawing** (`main.cpp drawOverlayFrame`, `OverlayRenderer`): clear, ambient, the smooth panorama (`PanoramaLayer::plan(..., scale 1)` decides the extent in whole frame columns
  with the usual cap and fallback; `drawPlannedAffine` draws the mesh at scale `k` from the view's corner), the frame's view rows as one texture (transparent where the panorama
  shows through; the rain beside the centre in the side columns; linear filter unless `k` is whole), the sharp text of the view at `k` (`renderTextOverlayScaled`, `Font::glyphScaledF`),
  the overlays, the settings menu (at the whole-number scale, centred, `menuGrid` maps through `menuOx / menuOy / menuMk`). The free pointer's OS rectangle is the whole window.
* **Headless composition** (`composeOverlay`, the CPU twin of the above; `rasterizePanoAffine` is `rasterizePanoRegion` with a fractional scale and offsets): script
  `overlay on [W H] | off | sharp on|off | frame | shot FILE | move WX,WY | motion DX,DY | map EX,EY | layout` (`overlay_script.cpp`). Test: `tests/test_overlay.sh`.
* Not combined with `--picture clean` for flat pages (the cleaned frame is not used; the cleaned panorama texture is).

## Where the game's files come from; the first-run screen (`firstrun.*`, `firstrunlogic.h`, `src/install`)
The app ships without a file of the original game. `resolveDataRoots` in `main.cpp` looks, in this order, at `BIOSYS_DATA`
(colon-separated roots), the imported copy in the data home (`--data-home DIR`, default `<save folder>/gamedata`; used when
`install::status` says both trees are complete), and, in a development build only (`BIOSYS_DEV_ROOT`, see the Makefile) and only
without `--data-home`, the developer's `setup-cd/installed/DATA1` and `game-cd/files`. With none of these a headless run fails
with an `Error` that says how to get the data, and a window run shows the first-run screen before the engine exists.
`runFirstRun` opens its own window (960 x 600 design, scaled uniformly, drawn at pixel resolution; text through the engine's
font code with a system typeface, `SDL_RenderDebugText` if none loads). Dropped files and folders (`SDL_EVENT_DROP_FILE`, also
files dropped on the app's icon) and the files of **Choose files…** go through `install::identify`; a known disc is queued and
imported by `install::importDisc` on a worker thread, one at a time, with its progress on the two cards; closing the window
cancels a running import, which leaves nothing half-written. When both discs are in, the window closes and the normal start
goes on in the same process. `firstrunlogic.h` is the SDL-free state machine (`build/biosys-firstruntest`).
`--play` is `--game`; it is added by itself when the program is started without arguments from inside an app bundle
(`.app/Contents/MacOS/` in `argv[0]`). The port's own tables are looked for in `SDL_GetBasePath() + "data"` first (in a bundle:
`Contents/Resources/data`). In window mode a fatal `Error` is also shown in a message box. `--version` prints the build's
version. Test aids (command line only): `--test-drop PATH` (repeatable), `--firstrun-shot FILE.bmp`, `--firstrun-log`,
`--firstrun-only` (ignore `BIOSYS_DATA` and the development roots, exit when the import is complete); `tests/test_firstrun.sh`.
