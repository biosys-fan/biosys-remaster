#include "engine/touchinfo.h"

namespace biosys {

TouchInfo::TouchInfo(const std::string& path) {
    const std::vector<u8> d = readFile(path);
    ByteReader r(d);
    fileSize = d.size();
    auto fail = [&](const std::string& m) { throw Error(path + ": " + m); };

    i32* tm[9] = {&compileTime.sec, &compileTime.min, &compileTime.hour, &compileTime.mday, &compileTime.mon,
                  &compileTime.year, &compileTime.wday, &compileTime.yday, &compileTime.isdst};
    for (size_t i = 0; i < 9; i++) *tm[i] = r.i32At(i * 4);
    pubInfoOffset = r.u32At(0x24); pubDataOffset = r.u32At(0x28); buttonsOffset = r.u32At(0x2c);

    const size_t b = pubInfoOffset;
    if (static_cast<uint64_t>(b) + 0x464 > d.size()) fail("PubInfo out of range");
    PubInfo& p = pubInfo;
    p.version = r.u32At(b); p.bookWidth = r.u32At(b + 4); p.bookHeight = r.u32At(b + 8);
    p.unk0c = r.u32At(b + 0x0c); p.releaseBuild = r.u32At(b + 0x10); p.firstPage = r.u32At(b + 0x14);
    p.unk18 = r.u32At(b + 0x18); p.mainTopY = r.u32At(b + 0x1c); p.unk20 = r.u32At(b + 0x20);
    p.unk24 = r.u32At(b + 0x24); p.statusBarWidth = r.u32At(b + 0x28); p.statusBarHeight = r.u32At(b + 0x2c);
    p.statusBarPos = r.u32At(b + 0x30);
    p.title = r.cstrAt(b + 0x34, 80); p.copyright = r.cstrAt(b + 0x84, 120);
    p.fontFile = r.cstrAt(b + 0xfc, 257); p.controlProgram = r.cstrAt(b + 0x1fd, 257);
    p.titleBitmap = r.cstrAt(b + 0x2fe, 257);
    if (p.version != 20) fail("unsupported file-set version " + std::to_string(p.version));

    const size_t pd = pubDataOffset;
    if (static_cast<uint64_t>(pd) + 0x74 > d.size()) fail("publication data out of range");
    const u32 nZones = r.u32At(pd), nStyles = r.u32At(pd + 4);
    zoneTableOffset = r.u32At(pd + 8); styleTableOffset = r.u32At(pd + 12);
    if (static_cast<uint64_t>(pd) + zoneTableOffset + static_cast<uint64_t>(nZones) * 0x24 > d.size()) fail("drop-zone table out of range");
    if (static_cast<uint64_t>(pd) + styleTableOffset + static_cast<uint64_t>(nStyles) * 0x2c > d.size()) fail("text-style table out of range");
    for (u32 i = 0; i < nZones; i++) {
        size_t o = pd + zoneTableOffset + static_cast<size_t>(i) * 0x24;
        DropZone z;
        z.page = r.u32At(o); z.hotspot = r.u32At(o + 4);
        z.pageName = r.cstrAt(o + 8, 11); z.hotspotName = r.cstrAt(o + 0x13, 13);
        z.type = r.u32At(o + 0x20);
        dropZones.push_back(z);
    }
    for (u32 i = 0; i < nStyles; i++) {
        size_t o = pd + styleTableOffset + static_cast<size_t>(i) * 0x2c;
        TextStyle s;
        s.height = r.i32At(o); s.r = r.u8At(o + 4); s.g = r.u8At(o + 5); s.b = r.u8At(o + 6);
        s.face = r.cstrAt(o + 8, 36);
        textStyles.push_back(s);
    }

    const size_t bb = buttonsOffset;
    if (static_cast<uint64_t>(bb) + 16 > d.size()) fail("buttons header out of range");
    const u32 nButtons = r.u32At(bb), nBack = r.u32At(bb + 4);
    imageDataOffset = r.u32At(bb + 8); imageDataSize = r.u32At(bb + 12);
    if (static_cast<uint64_t>(bb) + 16 + static_cast<uint64_t>(nButtons) * 0x34 + static_cast<uint64_t>(nBack) * 0x90 +
            imageDataSize > d.size())
        fail("button/background records out of range");
    size_t q = bb + 16;
    for (u32 i = 0; i < nButtons; i++, q += 0x34) {
        ButtonRecord br;
        for (size_t k = 0; k < 13; k++) br.w[k] = r.u32At(q + 4 * k);
        buttons.push_back(br);
    }
    for (u32 i = 0; i < nBack; i++, q += 0x90) {
        BackgroundRecord bg;
        for (size_t k = 0; k < 36; k++) bg.w[k] = r.u32At(q + 4 * k);
        backgrounds.push_back(bg);
    }
}

} // namespace biosys
