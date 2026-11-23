// TOUCHINF.DAT reader (publication info, drop-zone table, buttons and backgrounds).
// Format reference: analysis/notes/formats.md section 1.
// Python reference: analysis/scripts/biosys_touchinf.py. Engine routines: exe:00414ca0, exe:0040e690, exe:0040c760.
#pragma once
#include "engine/common.h"

namespace biosys {

struct CompileTime {          // file header 0x00: C struct tm of the compile (guess)
    i32 sec = 0, min = 0, hour = 0, mday = 0, mon = 0, year = 0, wday = 0, yday = 0, isdst = 0;
};

struct PubInfo {              // 0x464 bytes at header +0x24
    u32 version = 0;          // +0x000 must be 20
    u32 bookWidth = 0;        // +0x004
    u32 bookHeight = 0;       // +0x008
    u32 unk0c = 0;            // +0x00c (8, bits per pixel?)
    u32 releaseBuild = 0;     // +0x010
    u32 firstPage = 0;        // +0x014
    u32 unk18 = 0;
    u32 mainTopY = 0;         // +0x01c
    u32 unk20 = 0, unk24 = 0;
    u32 statusBarWidth = 0;   // +0x028
    u32 statusBarHeight = 0;  // +0x02c
    u32 statusBarPos = 0;     // +0x030
    std::string title;        // +0x034 char[80]
    std::string copyright;    // +0x084 char[120]
    std::string fontFile;     // +0x0fc char[257]
    std::string controlProgram;  // +0x1fd char[257]
    std::string titleBitmap;  // +0x2fe char[257]
};

struct DropZone {             // 0x24 bytes; index in the table = global drop-zone number
    u32 page = 0;             // 1-based page number in PAGES.DAT
    u32 hotspot = 0;          // hotspot index within the page
    std::string pageName;     // +0x08 char[11]
    std::string hotspotName;  // +0x13 char[13]
    u32 type = 0;             // 1..3
};

struct TextStyle {            // 0x2c bytes (none in Biosys)
    i32 height = 0;
    u8 r = 0, g = 0, b = 0;
    std::string face;         // +0x08 char[36]
};

struct ButtonRecord {         // 0x34 bytes, raw dwords; +0x04 type, +0x08/+0x0c width/height (guess)
    u32 w[13] = {};
};

struct BackgroundRecord {     // 0x90 bytes, raw dwords
    u32 w[36] = {};
};

class TouchInfo {
public:
    explicit TouchInfo(const std::string& path);   // throws Error on bad data

    uint64_t fileSize = 0;
    CompileTime compileTime;
    u32 pubInfoOffset = 0, pubDataOffset = 0, buttonsOffset = 0;   // file header 0x24/0x28/0x2c
    PubInfo pubInfo;
    // Publication data header (0x74 bytes); counts come from the tables below.
    u32 zoneTableOffset = 0, styleTableOffset = 0;                  // relative to pubDataOffset
    std::vector<DropZone> dropZones;
    std::vector<TextStyle> textStyles;
    // Buttons and backgrounds
    u32 imageDataOffset = 0, imageDataSize = 0;                     // header +0x08/+0x0c
    std::vector<ButtonRecord> buttons;                              // button n is buttons[n-1]
    std::vector<BackgroundRecord> backgrounds;
};

} // namespace biosys
