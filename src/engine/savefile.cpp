#include "engine/savefile.h"

namespace biosys {

SaveFile::SaveFile(const std::string& path) { parse(readFile(path), path); }

void SaveFile::parse(const std::vector<u8>& d, const std::string& path, bool strict) {
    ByteReader r(d);
    auto fail = [&](const std::string& m) { throw Error(path + ": " + m); };
    fileSize = d.size();
    magic = r.u32At(0); version = r.u32At(4); blobOffset = r.u32At(8);
    if (magic != 0xff5e67c0u) fail("bad save magic");
    if (version != 0x12) fail("unsupported save version " + std::to_string(version));
    if (strict && (blobOffset < 0x548 || blobOffset > d.size())) fail("control-program blob offset out of range");
    title = r.cstrAt(0x0c, 80);
    unk5c = r.u32At(0x5c); bs4Active = r.u32At(0x60);
    primaryPage = r.cstrAt(0x64, 12);
    scrollX = r.i32At(0x70); scrollY = r.i32At(0x74);
    paletteGrabResult = r.u32At(0x78);
    for (size_t i = 0; i < 256; i++)
        for (size_t k = 0; k < 4; k++) palette[i][k] = r.u8At(0x7c + 4 * i + k);
    unk47c = r.u32At(0x47c); unk480 = r.u32At(0x480);
    for (size_t k = 0; k < 4; k++) bytes484[k] = r.u8At(0x484 + k);
    for (size_t k = 0; k < 10; k++) pending488[k] = r.u32At(0x488 + 4 * k);
    pendingPage = r.cstrAt(0x4b0, 11); pendingHotspot = r.cstrAt(0x4bb, 13);
    pendingFlag = r.i32At(0x4c8);
    flag4cc = r.u32At(0x4cc); flag4d0 = r.u32At(0x4d0); flag4d4 = r.u32At(0x4d4); unk4d8 = r.u32At(0x4d8);
    string4dc = r.cstrAt(0x4dc, 100);
    unk540 = r.u32At(0x540); unk544 = r.u32At(0x544);

    sectionsEnd = 0x548;
    size_t p = 0x548;
    bs4Section.clear();
    if (bs4Active == 1) {    // exe:00452b70: u32 size, then size bytes (exe:00407000 writes the 0x480-byte section)
        const u32 n = r.u32At(p);
        p += 4;
        if (n > d.size() - p) fail("BioSys 4 section exceeds file size");
        bs4Section.assign(d.begin() + static_cast<std::ptrdiff_t>(p), d.begin() + static_cast<std::ptrdiff_t>(p + n));
        p += n;
    }
    hasObjectsSection = true;
    objectCount = r.u32At(p); p += 4;
    if (objectCount) {
        moveableCount = r.u32At(p); zoneCount = r.u32At(p + 4);
        const u32 nBar = r.u32At(p + 8);
        p += 12;
        if (static_cast<uint64_t>(objectCount) * 0x58 + static_cast<uint64_t>(nBar) * 11 > d.size() - p)
            fail("game-objects section exceeds file size");
        for (u32 i = 0; i < objectCount; i++, p += 0x58) {
            SavedObject s;
            s.name = r.cstrAt(p, 12); s.status = r.u32At(p + 0x0c); s.obj198 = r.u32At(p + 0x10);
            s.obj19c = r.i32At(p + 0x14);
            s.curZonePage = r.cstrAt(p + 0x18, 11); s.curZoneHotspot = r.cstrAt(p + 0x23, 11);
            s.homeZonePage = r.cstrAt(p + 0x2e, 11); s.homeZoneHotspot = r.cstrAt(p + 0x39, 11);
            s.holdGroup = r.u32At(p + 0x44); s.holdGroupCount = r.u32At(p + 0x48);
            s.variation = r.cstrAt(p + 0x4c, 12);
            objects.push_back(s);
        }
        for (u32 i = 0; i < nBar; i++, p += 11) statusBar.push_back(r.cstrAt(p, 11));
    }
    sectionsEnd = p;
    if (strict && sectionsEnd != blobOffset) fail("engine sections end at " + std::to_string(sectionsEnd) +
                                         " but blob offset is " + std::to_string(blobOffset));
}

namespace {

struct Out {
    std::vector<u8> b;
    void u32le(u32 v) { for (int i = 0; i < 4; i++) b.push_back(static_cast<u8>(v >> (8 * i))); }
    // char[n] field: the string, NUL padded
    void str(const std::string& s, size_t n, const char* what) {
        if (s.size() >= n) throw Error(std::string("save file: ") + what + " '" + s + "' does not fit its field of " + std::to_string(n) + " bytes");
        b.insert(b.end(), s.begin(), s.end());
        b.insert(b.end(), n - s.size(), 0);
    }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
};

} // namespace

// exe:00452410 (header) and exe:0044aaf0 (objects section), field by field as the reader above reads them.
std::vector<u8> SaveFile::serialize() const {
    Out o;
    o.u32le(0xff5e67c0u);
    o.u32le(0x12);
    o.u32le(0);                                   // +0x08 blob offset, patched below
    o.str(title, 80, "title");
    o.u32le(unk5c);
    o.u32le(bs4Active == 1 ? 1 : 0);                  // +0x60 BioSys 4 mode
    o.str(primaryPage, 12, "page name");
    o.u32le(static_cast<u32>(scrollX));
    o.u32le(static_cast<u32>(scrollY));
    o.u32le(paletteGrabResult);
    for (size_t i = 0; i < 256; i++)
        for (size_t k = 0; k < 4; k++) o.b.push_back(palette[i][k]);
    o.u32le(unk47c);
    o.u32le(unk480);
    for (size_t k = 0; k < 4; k++) o.b.push_back(bytes484[k]);
    for (size_t k = 0; k < 10; k++) o.u32le(pending488[k]);
    o.str(pendingPage, 11, "pending page");
    o.str(pendingHotspot, 13, "pending hotspot");
    o.u32le(static_cast<u32>(pendingFlag));
    o.u32le(flag4cc);
    o.u32le(flag4d0);
    o.u32le(flag4d4);
    o.u32le(unk4d8);
    o.str(string4dc, 100, "text");
    o.u32le(unk540);
    o.u32le(unk544);
    if (o.b.size() != 0x548) throw Error("save file: header layout error");
    if (bs4Active == 1) {
        o.u32le(static_cast<u32>(bs4Section.size()));
        o.b.insert(o.b.end(), bs4Section.begin(), bs4Section.end());
    }
    if (!hasObjectsSection || objectCount == 0) {
        o.u32le(0);
    } else {
        if (objects.size() != objectCount) throw Error("save file: object count does not match the records");
        o.u32le(objectCount);
        o.u32le(moveableCount);
        o.u32le(zoneCount);
        o.u32le(static_cast<u32>(statusBar.size()));
        for (const SavedObject& so : objects) {
            const size_t at = o.b.size();
            o.str(so.name, 12, "object name");
            o.u32le(so.status);
            o.u32le(so.obj198);
            o.u32le(static_cast<u32>(so.obj19c));
            o.str(so.curZonePage, 11, "zone page");
            o.str(so.curZoneHotspot, 11, "zone hotspot");
            o.str(so.homeZonePage, 11, "zone page");
            o.str(so.homeZoneHotspot, 11, "zone hotspot");
            o.u32le(so.holdGroup);
            o.u32le(so.holdGroupCount);
            o.str(so.variation, 12, "variation");
            if (o.b.size() - at != 0x58) throw Error("save file: object record layout error");
        }
        for (const std::string& n : statusBar) o.str(n, 11, "bar object");
    }
    const u32 size = static_cast<u32>(o.b.size());
    for (int i = 0; i < 4; i++) o.b[8 + static_cast<size_t>(i)] = static_cast<u8>(size >> (8 * i));
    return o.b;
}

} // namespace biosys
