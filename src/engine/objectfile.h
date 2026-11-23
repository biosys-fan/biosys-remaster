// OBJECTS.DAT reader (game object definitions + image pixels).
// Format reference: analysis/notes/formats.md section 3 (offsets there are
// authoritative). Python reference: analysis/scripts/biosys_objects.py.
// Engine routines: exe:00446e40 (load/enumerate), exe:0044fb20 (on-demand holding/wearing images).
#pragma once
#include "engine/common.h"
#include "engine/image.h"
#include <memory>

namespace biosys {

// "Normal" (lying in the world) image for one drop-zone type; record +0x1c + 12*k. Image numbers
// in all structs below are 1-based indices into ObjectDef::images, 0 = none.
struct NormalImage {
    u32 image = 0;
    u32 flag = 0;
    i32 transp = 0;
};

// User-holding / user-wearing image (0x18 bytes).
struct HoldImage {
    u32 image = 0;
    i32 x = 0, y = 0;     // position in the 640x368 view
    u32 flag = 0;
    i32 transp = 0;
    u32 group = 0;
};

struct NamedImage {       // record +0x124, 6 entries (goggles: power0..power5)
    u32 image = 0;
    std::string name;
};

struct ObjectAnim {       // 0x228 bytes
    i32 event = 0;        // +0x00
    i32 x = 0, y = 0;     // +0x04, +0x08
    i32 unk0c = 0;        // +0x0c (1..3)
    i32 delayMs = 0;      // +0x10
    std::string file;     // +0x14, char[0x104]
    std::string wav;      // +0x118, char[0x104]: optional wav played with the animation
    std::string variation;   // +0x21c, char[12]: variation the record is valid for ("" = any/base)
};

struct ObjectVariation {  // 0x120 bytes
    NormalImage normal[3];            // +0x00
    u32 icon = 0;                     // +0x24 status-bar icon image
    u32 unk28 = 0;                    // +0x28
    std::vector<HoldImage> holding;   // +0x2c, up to 8, terminated by image == 0
    u32 holdDelayMs = 0;              // +0xec
    HoldImage wearing;                // +0xf0
    std::string name;                 // +0x114, char[12]
    std::string shortName;            // +0x108, char[12]
};

enum class ImageArea { Inline, Resident, OnDemand };

// Image descriptor (0x1c bytes).
struct ObjectImage {
    u32 compressed = 0;   // always 0
    u32 width = 0, height = 0, stride = 0;
    u32 bpp = 0;          // always 8
    i32 dataOffset = -1;  // -1 = pixels follow the descriptor; else offset inside the area below
    u32 runtimePtr = 0;   // 0 in the file
    ImageArea area = ImageArea::Inline;  // computed: on-demand iff used as holding/wearing image
    uint64_t fileOffset = 0;   // computed: absolute file offset of the pixels (raw bottom-up rows)
};

struct ObjectDef {
    // Table entry (file header at 0x0c)
    u32 recordOffset = 0, recordSize = 0;
    std::string tableName;
    // Record header
    u32 nImages = 0, nAnims = 0, nVariations = 0;
    u32 imageTableOffset = 0, animTableOffset = 0, variationTableOffset = 0;  // relative to record
    i32 unk18 = 0;
    NormalImage normal[3];            // per drop-zone type 1..3
    u32 icon = 0;                     // +0x40 status-bar icon image
    u32 unk44 = 0;
    std::vector<HoldImage> holding;   // +0x48, up to 8
    u32 holdDelayMs = 0;              // +0x108
    HoldImage wearing;                // +0x10c
    NamedImage named[6];              // +0x124
    std::string shortName;            // +0x184
    u32 moveable = 0;                 // +0x190
    u32 randomZone = 0;               // +0x194
    u32 unk198 = 0;
    i32 unk19c = 0;
    u32 wearable = 0;                 // +0x1a0
    u32 fruit = 0;                    // +0x1a4
    u32 unk1a8 = 0;
    i32 initZone = -1;                // +0x1ac index into TOUCHINF drop-zone table
    u8 initZoneType = 0;              // +0x1b0
    std::string initVariation;        // +0x1b1
    std::string name;                 // +0x1bd
    std::vector<ObjectAnim> anims;
    std::vector<ObjectVariation> variations;
    std::vector<ObjectImage> images;
};

class ObjectFile {
public:
    // Reads the header and all records (not the image areas). Throws Error on failure.
    explicit ObjectFile(const std::string& path);
    ~ObjectFile();
    ObjectFile(const ObjectFile&) = delete;
    ObjectFile& operator=(const ObjectFile&) = delete;

    int objectCount() const;
    const ObjectDef& object(int index) const;           // 0-based; throws Error if out of range
    int findObject(const std::string& name) const;      // case-insensitive; -1 if not found
    u32 residentImageBase() const;                      // header +0x04
    u32 residentSize() const;                           // header +0x08 (= on-demand area start)
    uint64_t fileSize() const;
    // Decodes one image; imageNumber is 1-based (as in the record fields). Reads pixels from the
    // file on demand; result is top-down, unpadded.
    Image8 loadImage(int objectIndex, int imageNumber) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace biosys
