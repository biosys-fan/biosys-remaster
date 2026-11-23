// Saved-game engine part: header (0x548 bytes) and game-objects section, read and written. The game logic's blob follows
// at blobOffset (its container is game/savegame.h) and is not touched here.
// Format reference: analysis/notes/formats.md sections 6.1-6.2 and menus_saveload.md.
// Python reference: analysis/scripts/biosys_sav.py. Engine routines: exe:00452b70 (reader), exe:0044afd0, exe:00452410 (writer),
// exe:0044aaf0.
#pragma once
#include "engine/common.h"

namespace biosys {

struct SavedObject {          // 0x58 bytes, in OBJECTS.DAT order
    std::string name;         // +0x00 char[12]
    u32 status = 0;           // +0x0c 1 = in a drop zone, 6 = fruit in its zone, 4 = other
    u32 obj198 = 0;           // +0x10
    i32 obj19c = 0;           // +0x14
    std::string curZonePage, curZoneHotspot;    // +0x18 char[11], +0x23 char[11]
    std::string homeZonePage, homeZoneHotspot;  // +0x2e char[11], +0x39 char[11]
    u32 holdGroup = 0, holdGroupCount = 0;      // +0x44, +0x48
    std::string variation;    // +0x4c char[12]
};

class SaveFile {
public:
    SaveFile() = default;                          // an empty image to fill and serialize()
    explicit SaveFile(const std::string& path);    // throws Error on bad magic/version/layout
    // The engine part as exe:00452410 + exe:0044aaf0 write it: header, then (bs4Active only) u32 size + the BioSys 4 section
    // (exe:00407000), then the objects section.
    // magic, version and blobOffset (= the size of the result) are set here; every padding byte is 0 (the original leaves
    // stack garbage there). Throws Error if a string does not fit its field.
    std::vector<u8> serialize() const;
    // The header and objects section of an in-memory file (what the constructor does with the file's bytes).
    // strict: the objects section must end exactly at the blob offset of the header (a game load does not check it: exe:00452b70
    // takes the offset from where it stopped reading).
    void parse(const std::vector<u8>& bytes, const std::string& what, bool strict = true);

    // Engine header (0x548 bytes)
    uint64_t fileSize = 0;
    u32 magic = 0, version = 0;
    u32 blobOffset = 0;            // +0x08: control-program blob runs from here to EOF
    uint64_t blobSize() const { return fileSize - blobOffset; }
    std::string title;             // +0x0c char[80]
    u32 unk5c = 0;
    u32 bs4Active = 0;             // +0x60; if 1 the BioSys 4 section (below) follows the header, before the objects section
    std::vector<u8> bs4Section;    // u32 size + the bytes (0x480 for a valid one; the owner checks the size): bs4.h Bs4::saveSection
    std::string primaryPage;       // +0x64 char[12]
    i32 scrollX = 0, scrollY = 0;  // +0x70
    u32 paletteGrabResult = 0;     // +0x78
    u8 palette[256][4] = {};       // +0x7c R,G,B,flags
    u32 unk47c = 0, unk480 = 0;
    u8 bytes484[4] = {};           // +0x484 three flags + pad
    u32 pending488[10] = {};       // +0x488 eight dwords + two page numbers
    std::string pendingPage;       // +0x4b0 char[11]
    std::string pendingHotspot;    // +0x4bb char[13]
    i32 pendingFlag = 0;           // +0x4c8
    u32 flag4cc = 0, flag4d0 = 0, flag4d4 = 0, unk4d8 = 0;
    std::string string4dc;         // +0x4dc char[100]
    u32 unk540 = 0, unk544 = 0;

    // Game-objects section: at 0x548, or after the BioSys 4 section when bs4Active
    bool hasObjectsSection = false;
    u32 objectCount = 0;
    u32 moveableCount = 0;
    u32 zoneCount = 0;             // "unk" in the spec (943 = drop zones)
    std::vector<SavedObject> objects;
    std::vector<std::string> statusBar;   // object names, left to right
    uint64_t sectionsEnd = 0;      // file offset where the engine sections end (== blobOffset)
};

} // namespace biosys
