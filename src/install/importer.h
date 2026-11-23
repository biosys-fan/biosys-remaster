// CONTRACT — first-run import of the original game data (the player's own copy of the two CDs).
//
// The port ships without any file of the original game. The player downloads the two CD images
// ("Setup CD" and "Game CD") and hands them to the port, which copies what it needs into its own
// per-user folder. This module does that copy. It is platform independent: C++17, zlib, <filesystem>;
// no SDL, no engine headers except engine/common.h (biosys::Error). Nothing here executes anything
// from the discs, and everything read from them is untrusted input (check every size and offset,
// never follow a path out of the destination).
//
// What a player may hand over (all of these must work, for either disc):
//   1. the downloaded .zip (a CloneCD set: "<dir>/Bio_game.img" + .ccd/.cue/.sub; the .img holds raw
//      2352-byte sectors, user data at +16 for mode 1 and +24 for mode 2, see analysis/scripts/img2iso.py);
//      zip methods 0 and 8, CRC checked, no zip64 needed (a zip64 archive is a clear Error);
//   2. the .img itself (raw 2352-byte sectors) or a plain 2048-byte-sector .iso / .img / .bin;
//   3. a folder that contains 1. or 2. at depth <= 2 (Safari unpacks downloads by itself);
//   4. a folder that is the unpacked disc: DATA1.CAB at its top (Setup CD), or BIODATA + BIOTIME (Game CD),
//      names in any case;
//   5. a folder that is an installed copy of the game: Biodata + NewGame.sav at its top (counts as Setup CD).
//
// What is written (names exactly as stored on the disc / in the cabinet):
//   Setup CD -> <dest>/installed/   every file of DATA1.CAB (InstallShield 5 cabinet, see
//                                   analysis/scripts/is5cab.py) except *.exe and *.dll
//                                   (Biodata/..., Biotime/..., NewGame.sav, Readme.txt)
//   Game CD  -> <dest>/game-cd/     the trees BIODATA and BIOTIME of the disc
// Each tree is built in "<dest>/<name>.partial" and renamed into place at the very end, after an
// empty file ".complete" was written into it; an existing tree of the same name is replaced.
// Temporary files live in "<dest>/.tmp" and are removed on success, failure and cancel alike.
#pragma once
#include "engine/common.h"
#include <cstdint>
#include <functional>
#include <string>

namespace biosys::install {

enum class DiscKind { Unknown, SetupCd, GameCd };

// "Setup CD" / "Game CD" / "unknown"
const char* discName(DiscKind k);

// The folder names under <dest> ("installed", "game-cd").
const char* treeName(DiscKind k);

// Thrown when the progress callback asked to stop. Everything partial has been removed.
struct Cancelled : Error {
    Cancelled() : Error("cancelled") {}
};

// What kind of disc a dropped file or folder is, without writing anything (cheap: for a .zip only the
// directory is read, the kind is taken from the name of the image inside, "setup" / "game"; importDisc
// decides for good from the disc's own directory). Unknown: *why says in one plain sentence, for a
// player, what the thing is instead ("This is a text file, not one of the two Biosys CDs.").
DiscKind identify(const std::string& path, std::string* why = nullptr);

struct Progress {
    DiscKind disc = DiscKind::Unknown;
    const char* step = "";     // "unpack" (zip -> image), "extract" (image/cabinet/folder -> files), "finish"
    uint64_t done = 0;         // bytes of this step so far
    uint64_t total = 0;        // bytes of this step in all (0 = not known yet)
};
// Called often (at least every 4 MB and at every step change) from the thread that runs importDisc.
// Return false to cancel.
using ProgressFn = std::function<bool(const Progress&)>;

// Imports one disc from path into dest (created if missing). Returns which disc it was.
// Throws Cancelled, or Error with a message a player can act on: not a Biosys disc, damaged archive
// (CRC), a file missing on the disc, not enough free space (checked before writing: say how much is
// needed and how much is free), cannot write. Never leaves a half-written tree behind.
DiscKind importDisc(const std::string& path, const std::string& dest, const ProgressFn& progress = nullptr);

// Which trees are completely imported under dest (the ".complete" file is there).
struct Status {
    bool setup = false;
    bool game = false;
    bool complete() const { return setup && game; }
};
Status status(const std::string& dest);

// The two data roots for DataFiles, in search order (installed copy first, then the CD), whether or
// not they exist: { dest + "/installed", dest + "/game-cd" }.
std::vector<std::string> dataRoots(const std::string& dest);

} // namespace biosys::install
