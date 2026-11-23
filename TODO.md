# To do

## Windows version

**Goal:** the same foolproof experience as on macOS: download one file, start it, drop the two original CD images
on the window, play. Target: Windows 10 and 11, x64 (ARM64 if it comes for free).

The engine (`src/engine`), the game rules (`src/game`) and the importer (`src/install`) are plain C++17 and already
kept free of platform calls on purpose; SDL3, FreeType and zlib all exist for Windows. What is left:

- [ ] **Build system.** The Makefile assumes clang and Homebrew paths. Add a CMake build (one `CMakeLists.txt` for
      macOS, Windows and Linux) that fetches or finds SDL3, FreeType and zlib (vcpkg or the SDL3 `-devel-VC` package),
      and builds with MSVC and clang-cl. Keep the Makefile for the existing test scripts until they are moved.
- [ ] **POSIX calls in the engine.** `src/engine/datafiles.cpp` uses `dirent.h` and `sys/stat.h` for the
      case-insensitive file lookup; `src/app/main.cpp` uses `stat`. Replace with `std::filesystem` (the importer
      already does). Check every `fopen` for binary mode (`"rb"` / `"wb"`) and paths with non-ASCII characters
      (user names): use `std::filesystem::path` built from UTF-8 and `_wfopen` or `ifstream(path)`.
- [ ] **64-bit and compiler differences.** The code was only ever built with clang on macOS (arm64 and x86_64).
      Build with `/W4`, fix what MSVC reports (`long` is 32 bits there, `ssize_t`, designated initialisers, `M_PI`).
      The simulation uses `double` almost everywhere: build with `/fp:precise`, no fused multiply-add, and run the
      simulation tests to confirm the numbers match the macOS build exactly.
- [ ] **First-run screen.** It already lists Windows font candidates (`C:\Windows\Fonts\segoeui.ttf`); verify
      drag and drop, the file dialog and the "open the download page" button through SDL3 on Windows, and that
      long paths and paths with spaces work.
- [ ] **Where files go.** `SDL_GetPrefPath` gives `%APPDATA%\biosys\biosys\`; check saves, `biosys.conf` and
      `gamedata\` there, and the documentation that names the macOS folder.
- [ ] **Keys.** The manual names Option and Cmd (Option+Return, Option+N, Option+M, Cmd+,). Map and document
      Alt+Enter, Alt+N, Alt+M, Ctrl+, and check that Alt combinations do not open the window menu.
- [ ] **Window and display.** High-DPI scaling (per-monitor v2 manifest), full screen on several displays,
      vertical sync and the pace clock (`src/app/pacing.h`) with Windows' timer resolution.
- [ ] **Sound.** WASAPI through SDL3: device rate, the enhanced path, re-opening the device from the settings menu.
- [ ] **Packaging.** A portable `.zip` (exe, `SDL3.dll`, `data\`, licences) and an installer (Inno Setup or MSIX)
      with a Start menu entry and the app icon (`.ico` from `packaging/make_art.swift`'s picture). Unsigned builds
      trigger SmartScreen: document the "More info › Run anyway" step, as the README does for Gatekeeper.
- [ ] **Tests.** The test scripts are bash and Python 3 (standard library). Run them under Git Bash or WSL against
      a Windows build, or port `tests/run.sh` to Python. At least: `test_import`, `test_firstrun`, `test_sim`,
      `test_game`, `test_prefs`.
- [ ] **Continuous builds.** A workflow that builds the macOS `.dmg` and the Windows `.zip` for every tag. (Today
      `.github/workflows/release.yml` only publishes a disk image that was built and tested by hand.)
- [ ] **Play it on a real Windows machine** from first start to a saved game, and note in the manual what was seen.

## Later

- [ ] Linux build (the CMake work above covers most of it; package as AppImage or Flatpak).
- [ ] An Apple-notarised macOS build, so that the "Open Anyway" step disappears (needs a paid developer identity).
- [ ] In-app "choose a folder" button on the first-run screen (folders can only be dropped for now).
- [ ] Turning the view by dragging on a trackpad; translations; subtitles for the films (see MANUAL.md, 10.3).
- [ ] The held-back realism level ("living system") and difficulty ("castaway"), once the existing ones have been
      played for long (SETTINGS.md).
