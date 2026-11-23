# Rules for anyone (human or model) working on this port

Project: native re-implementation of the 1998 game Biosys, reading the original data files.
Root: this repository. For development it is cloned as `port/` inside a folder (the *development folder*, `<dev>` below)
that also holds the developer's own copy of the original discs and the reverse-engineering notes, none of which are in
the repository: `<dev>/setup-cd`, `<dev>/game-cd`, `<dev>/analysis` (read-only reference material), `<dev>/docs`.
The Makefile hands `<dev>` to the code as `BIOSYS_DEV_ROOT` (default: the folder above the repository); tests take it
from the environment variable of the same name, with the same default. The released app has no such fallback.

## Hard rules
- Never execute any original game binary (`*.exe`, `*.dll`). Static data parsing only.
- Game data is untrusted input: validate sizes/offsets before use, throw `biosys::Error` on bad data.
- Do not modify anything under `<dev>/{analysis,setup-cd,game-cd,docs}`.
- Run Python with `python3 -I`. Python is for tests/tools only, standard library only.
- No new dependencies beyond the C++17 standard library, zlib, FreeType and SDL3 (SDL3 only in `src/app`; FreeType only in
  `src/engine/font.cpp`, for the TrueType text the original draws through GDI; installed headers `/opt/homebrew/include/freetype2`).
- Never add a file of the original game (or a piece of one) to the repository; `.gitignore` lists the usual kinds.
- Do not commit, push or rewrite history unless you were asked to. Do not install anything.
- No personal paths, names or machine details in files: use `BIOSYS_DEV_ROOT` / `<dev>`.

## Layout
- `src/engine/` — platform-independent core. **No SDL includes here.** Headers marked
  "CONTRACT" are fixed interfaces: implement them, do not change them. If a contract is wrong or
  insufficient, stop and say so in your final message instead of editing it.
- `src/install/` — the importer for the player's own CD images (zlib and `<filesystem>` only; no SDL, no engine).
- `src/app/` — SDL3 front end.  `tests/` — test scripts (`tests/test_<topic>.sh`, run by `tests/run.sh`).
- `src/tools/<name>.cpp` — command-line tools; each file builds `build/biosys-<name>` (engine core only).
  Add your own tool file rather than editing someone else's.
- Several workers run at the same time. Touch only the files your task names (plus new files of
  your own); never edit another topic's sources, tests, the Makefile, or `tests/run.sh`.
- `ffmpeg`/`ffprobe` (installed) may be used in tests as a decoding oracle, never in the port itself.
- `data/` — small generated tables shipped with the port (TSV).  `build/` — build output.

## Build and test
- `make` builds everything; it must finish with zero warnings.
- `make test` runs `tests/run.sh`; it must print `ALL TESTS PASSED`.
- A full `make test` now takes about 40 minutes (24+ suites). While working, run only the suites that
  cover what you touch (`bash tests/test_<topic>.sh`); run the whole suite once at the end if time allows,
  and say in your final message whether you did.

## Game data locations (search order: installed copy first, then CD)
- Installed: `<dev>/setup-cd/installed/DATA1` (`Biodata/`, `Biotime/`)
- CD: `<dev>/game-cd/files` (`BIODATA/`, `BIOTIME/`)
File names inside game data use backslashes and arbitrary case; always go through `DataFiles`.

## Style
- C++17, namespace `biosys`, 4-space indent, no exceptions other than `biosys::Error`,
  no raw `new`/`delete`, fixed-width integer types for file fields, little-endian reads via `ByteReader`.
- Keep comments to what the code cannot say: file-format facts and the reference address
  (`exe:XXXXXXXX` / `dll:XXXXXXXX`) of the original routine being re-implemented.

## Final message
Report: what you built, test results (paste the summary lines), anything you could not do or
had to guess. Be brief.

## Game logic (`src/game/`, namespace `biosys::game`) — the port of BiosGame.dll
- `src/game/` may include `src/engine/` headers but talks to the engine ONLY through
  `engine/engineapi.h` (the original's 110 API commands as typed methods) and receives the original's
  46 events through `engine/gamelogic.h`. No SDL, no wall clock, no `rand()` outside the one wrapper
  the game code provides (so runs are reproducible).
- This code is a faithful translation of the decompiled DLL, not a redesign: keep the original's
  control flow, constants, evaluation order and rounding (the original uses `double` almost everywhere).
  Give members and functions readable names (from `analysis/notes/*.md` and `analysis/notes/all.renames.tsv`),
  and put the original address in a comment on every translated function: `// dll:1001d970`.
- Do not fix the original's bugs. Where the original is visibly wrong, translate it as is and mark it
  `// ORIGINAL BUG: ...`. Where you cannot tell what the original does, do not invent: leave
  `// UNRESOLVED dll:XXXXXXXX: ...`, make the code fail loudly (`throw biosys::Error`) if reached, and
  list it in your final message.
- Struct member order and sizes matter only where the save format depends on them; follow
  `analysis/notes/saveblob.md` there.
