// biosys-objdump: canonical text dumps of OBJECTS.DAT, TOUCHINF.DAT and .sav files.
// tests/golden_objects.py prints the same lines from the Python reference parsers.
#include "engine/objectfile.h"
#include "engine/savefile.h"
#include "engine/touchinfo.h"
#include <cstdio>
#include <cstdlib>
#include <zlib.h>

using namespace biosys;

static unsigned long long ull(uint64_t v) { return static_cast<unsigned long long>(v); }

static void normalLine(const char* tag, const std::string& id, int k, const NormalImage& n) {
    std::printf("%s %s %d image=%u flag=%u transp=%d\n", tag, id.c_str(), k, n.image, n.flag, n.transp);
}

static void holdLine(const char* tag, const std::string& id, int k, const HoldImage& h) {
    std::printf("%s %s %d image=%u x=%d y=%d flag=%u transp=%d group=%u\n", tag, id.c_str(), k, h.image, h.x, h.y,
                h.flag, h.transp, h.group);
}

static const char* areaName(ImageArea a) {
    return a == ImageArea::Inline ? "inline" : a == ImageArea::Resident ? "resident" : "ondemand";
}

static void cmdObjects(const ObjectFile& of) {
    std::printf("objects count=%d residentBase=%u residentSize=%u fileSize=%llu\n", of.objectCount(),
                of.residentImageBase(), of.residentSize(), ull(of.fileSize()));
    for (int i = 0; i < of.objectCount(); i++) {
        const ObjectDef& o = of.object(i);
        const std::string id = std::to_string(i);
        std::printf("obj %d name=[%s] tablename=[%s] off=%u size=%u nimg=%u nanim=%u nvar=%u oimg=%u oanim=%u ovar=%u unk18=%d\n",
                    i, o.name.c_str(), o.tableName.c_str(), o.recordOffset, o.recordSize, o.nImages, o.nAnims,
                    o.nVariations, o.imageTableOffset, o.animTableOffset, o.variationTableOffset, o.unk18);
        for (int k = 0; k < 3; k++) normalLine("normal", id, k, o.normal[k]);
        std::printf("icon %d image=%u unk44=%u\n", i, o.icon, o.unk44);
        for (size_t k = 0; k < o.holding.size(); k++) holdLine("hold", id, static_cast<int>(k), o.holding[k]);
        std::printf("holddelay %d %u\n", i, o.holdDelayMs);
        holdLine("wear", id, 0, o.wearing);
        for (int k = 0; k < 6; k++)
            std::printf("named %d %d image=%u name=[%s]\n", i, k, o.named[k].image, o.named[k].name.c_str());
        std::printf("misc %d short=[%s] moveable=%u randomzone=%u unk198=%u unk19c=%d wearable=%u fruit=%u unk1a8=%u initzone=%d initzonetype=%u initvar=[%s]\n",
                    i, o.shortName.c_str(), o.moveable, o.randomZone, o.unk198, o.unk19c, o.wearable, o.fruit,
                    o.unk1a8, o.initZone, static_cast<unsigned>(o.initZoneType), o.initVariation.c_str());
        for (size_t k = 0; k < o.anims.size(); k++) {
            const ObjectAnim& a = o.anims[k];
            std::printf("anim %d %zu event=%d x=%d y=%d unk0c=%d delay=%d file=[%s] wav=[%s] var=[%s]\n", i, k, a.event, a.x, a.y,
                        a.unk0c, a.delayMs, a.file.c_str(), a.wav.c_str(), a.variation.c_str());
        }
        for (size_t k = 0; k < o.variations.size(); k++) {
            const ObjectVariation& v = o.variations[k];
            const std::string vid = id + "." + std::to_string(k);
            std::printf("var %s name=[%s] short=[%s] icon=%u unk28=%u holddelay=%u\n", vid.c_str(), v.name.c_str(),
                        v.shortName.c_str(), v.icon, v.unk28, v.holdDelayMs);
            for (int j = 0; j < 3; j++) normalLine("varnormal", vid, j, v.normal[j]);
            for (size_t j = 0; j < v.holding.size(); j++) holdLine("varhold", vid, static_cast<int>(j), v.holding[j]);
            holdLine("varwear", vid, 0, v.wearing);
        }
        for (size_t k = 0; k < o.images.size(); k++) {
            const ObjectImage& im = o.images[k];
            Image8 px = of.loadImage(i, static_cast<int>(k + 1));
            u32 crc = static_cast<u32>(crc32(0, px.px.data(), static_cast<uInt>(px.px.size())));
            std::printf("img %d %zu %ux%u stride=%u comp=%u bpp=%u dataoff=%d ptr=%u area=%s fileoff=%llu crc=%08x\n",
                        i, k + 1, im.width, im.height, im.stride, im.compressed, im.bpp, im.dataOffset,
                        im.runtimePtr, areaName(im.area), ull(im.fileOffset), crc);
        }
    }
}

static void cmdTouchinf(const TouchInfo& t) {
    const CompileTime& c = t.compileTime;
    std::printf("touchinf size=%llu\n", ull(t.fileSize));
    std::printf("tm sec=%d min=%d hour=%d mday=%d mon=%d year=%d wday=%d yday=%d isdst=%d\n", c.sec, c.min, c.hour,
                c.mday, c.mon, c.year, c.wday, c.yday, c.isdst);
    std::printf("offsets pubinfo=%u pubdata=%u buttons=%u\n", t.pubInfoOffset, t.pubDataOffset, t.buttonsOffset);
    const PubInfo& p = t.pubInfo;
    std::printf("pubinfo version=%u book=%ux%u unk0c=%u release=%u firstpage=%u unk18=%u topy=%u unk20=%u unk24=%u statusbar=%ux%u pos=%u\n",
                p.version, p.bookWidth, p.bookHeight, p.unk0c, p.releaseBuild, p.firstPage, p.unk18, p.mainTopY,
                p.unk20, p.unk24, p.statusBarWidth, p.statusBarHeight, p.statusBarPos);
    std::printf("title [%s]\ncopyright [%s]\nfont [%s]\ncontrol [%s]\ntitlebmp [%s]\n", p.title.c_str(),
                p.copyright.c_str(), p.fontFile.c_str(), p.controlProgram.c_str(), p.titleBitmap.c_str());
    std::printf("pubdata zones=%zu styles=%zu zoneoff=%u styleoff=%u\n", t.dropZones.size(), t.textStyles.size(),
                t.zoneTableOffset, t.styleTableOffset);
    for (size_t i = 0; i < t.dropZones.size(); i++) {
        const DropZone& z = t.dropZones[i];
        std::printf("zone %zu page=%u hotspot=%u pagename=[%s] hotspotname=[%s] type=%u\n", i, z.page, z.hotspot,
                    z.pageName.c_str(), z.hotspotName.c_str(), z.type);
    }
    for (size_t i = 0; i < t.textStyles.size(); i++) {
        const TextStyle& s = t.textStyles[i];
        std::printf("style %zu height=%d rgb=%u,%u,%u face=[%s]\n", i, s.height, s.r, s.g, s.b, s.face.c_str());
    }
    std::printf("buttons count=%zu backgrounds=%zu imagedataoff=%u imagedatasize=%u\n", t.buttons.size(),
                t.backgrounds.size(), t.imageDataOffset, t.imageDataSize);
    for (size_t i = 0; i < t.buttons.size(); i++) {
        std::printf("button %zu", i + 1);
        for (u32 w : t.buttons[i].w) std::printf(" %x", w);
        std::printf("\n");
    }
    for (size_t i = 0; i < t.backgrounds.size(); i++) {
        std::printf("background %zu", i + 1);
        for (u32 w : t.backgrounds[i].w) std::printf(" %x", w);
        std::printf("\n");
    }
}

static void cmdSave(const SaveFile& s) {
    std::printf("save size=%llu magic=%08x version=%u blobOffset=%u blobSize=%llu\n", ull(s.fileSize), s.magic,
                s.version, s.blobOffset, ull(s.blobSize()));
    std::printf("title [%s]\nunk5c=%u bs4=%u\nprimarypage [%s]\nscroll %d,%d\npalettegrab=%u\n", s.title.c_str(),
                s.unk5c, s.bs4Active, s.primaryPage.c_str(), s.scrollX, s.scrollY, s.paletteGrabResult);
    for (int i = 0; i < 256; i++)
        std::printf("pal %d %u,%u,%u,%u\n", i, s.palette[i][0], s.palette[i][1], s.palette[i][2], s.palette[i][3]);
    std::printf("unk47c=%u unk480=%u bytes484=%u,%u,%u,%u\n", s.unk47c, s.unk480, s.bytes484[0], s.bytes484[1],
                s.bytes484[2], s.bytes484[3]);
    std::printf("pending words=");
    for (int i = 0; i < 10; i++) std::printf(i ? ",%u" : "%u", s.pending488[i]);
    std::printf(" page=[%s] hotspot=[%s] flag=%d\n", s.pendingPage.c_str(), s.pendingHotspot.c_str(), s.pendingFlag);
    std::printf("flags4cc=%08x flags4d0=%08x flags4d4=%08x unk4d8=%u\nstring4dc [%s]\nunk540=%u unk544=%u\n", s.flag4cc,
                s.flag4d0, s.flag4d4, s.unk4d8, s.string4dc.c_str(), s.unk540, s.unk544);
    if (!s.hasObjectsSection) {
        std::printf("objects section absent (bs4)\n");
    } else {
        std::printf("objects count=%u moveable=%u zones=%u statusbar=%zu\n", s.objectCount, s.moveableCount,
                    s.zoneCount, s.statusBar.size());
        for (size_t i = 0; i < s.objects.size(); i++) {
            const SavedObject& o = s.objects[i];
            std::printf("sobj %zu name=[%s] status=%u u198=%u u19c=%d cur=[%s],[%s] home=[%s],[%s] group=%u/%u variation=[%s]\n",
                        i, o.name.c_str(), o.status, o.obj198, o.obj19c, o.curZonePage.c_str(),
                        o.curZoneHotspot.c_str(), o.homeZonePage.c_str(), o.homeZoneHotspot.c_str(), o.holdGroup,
                        o.holdGroupCount, o.variation.c_str());
        }
        for (size_t i = 0; i < s.statusBar.size(); i++) std::printf("bar %zu [%s]\n", i, s.statusBar[i].c_str());
    }
    std::printf("sectionsEnd=%llu\n", ull(s.sectionsEnd));
}

static int usage() {
    std::fprintf(stderr,
                 "usage: biosys-objdump objects <OBJECTS.DAT>\n"
                 "       biosys-objdump touchinf <TOUCHINF.DAT>\n"
                 "       biosys-objdump save <file.sav>\n"
                 "       biosys-objdump export <OBJECTS.DAT> <objectName> <imageNumber(1-based)> <out.bmp> <palette.pal>\n");
    return 2;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) return usage();
        const std::string cmd = argv[1];
        if (cmd == "objects" && argc == 3) {
            cmdObjects(ObjectFile(argv[2]));
        } else if (cmd == "touchinf" && argc == 3) {
            cmdTouchinf(TouchInfo(argv[2]));
        } else if (cmd == "save" && argc == 3) {
            cmdSave(SaveFile(argv[2]));
        } else if (cmd == "export" && argc == 7) {
            ObjectFile of(argv[2]);
            int idx = of.findObject(argv[3]);
            if (idx < 0) throw Error(std::string("no such object: ") + argv[3]);
            char* end = nullptr;
            long n = std::strtol(argv[4], &end, 10);
            if (!*argv[4] || *end) throw Error(std::string("bad image number: ") + argv[4]);
            Image8 img = of.loadImage(idx, static_cast<int>(n));
            writeBmp(argv[5], img, Palette::load(argv[6]));
            std::printf("wrote %s %dx%d\n", argv[5], img.w, img.h);
        } else {
            return usage();
        }
        return 0;
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
