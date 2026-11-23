#include "engine/objectfile.h"
#include <cstdio>
#include <set>
#include <sys/types.h>

namespace biosys {

using u64 = uint64_t;

struct ObjectFile::Impl {
    std::string path;
    std::unique_ptr<FILE, int (*)(FILE*)> f{nullptr, std::fclose};
    u64 size = 0;
    u32 residentBase = 0, residentSize = 0;
    std::vector<ObjectDef> objs;

    std::vector<u8> readAt(u64 off, u64 len) const {
        if (off > size || len > size - off)
            throw Error(path + ": read of " + std::to_string(len) + " bytes at " + std::to_string(off) +
                        " exceeds file size " + std::to_string(size));
        std::vector<u8> v(static_cast<size_t>(len));
        if (len == 0) return v;
        if (fseeko(f.get(), static_cast<off_t>(off), SEEK_SET) != 0 ||
            std::fread(v.data(), 1, v.size(), f.get()) != v.size())
            throw Error(path + ": read error at offset " + std::to_string(off));
        return v;
    }
};

static HoldImage readHold(const ByteReader& r, size_t o) {
    HoldImage h;
    h.image = r.u32At(o); h.x = r.i32At(o + 4); h.y = r.i32At(o + 8);
    h.flag = r.u32At(o + 12); h.transp = r.i32At(o + 16); h.group = r.u32At(o + 20);
    return h;
}

static std::vector<HoldImage> readHoldList(const ByteReader& r, size_t o) {
    std::vector<HoldImage> v;
    for (int k = 0; k < 8; k++) {
        HoldImage h = readHold(r, o + 0x18 * static_cast<size_t>(k));
        if (h.image == 0) break;
        v.push_back(h);
    }
    return v;
}

static NormalImage readNormal(const ByteReader& r, size_t o) {
    NormalImage n;
    n.image = r.u32At(o); n.flag = r.u32At(o + 4); n.transp = r.i32At(o + 8);
    return n;
}

ObjectFile::ObjectFile(const std::string& path) : impl_(new Impl) {
    Impl& m = *impl_;
    m.path = path;
    m.f.reset(std::fopen(path.c_str(), "rb"));
    if (!m.f) throw Error("cannot open " + path);
    if (fseeko(m.f.get(), 0, SEEK_END) != 0) throw Error(path + ": cannot seek");
    m.size = static_cast<u64>(ftello(m.f.get()));

    std::vector<u8> head = m.readAt(0, 12);
    ByteReader hr(head);
    u32 count = hr.u32At(0);
    m.residentBase = hr.u32At(4);
    m.residentSize = hr.u32At(8);
    if (m.residentSize == 0) m.residentSize = static_cast<u32>(m.size);  // 0 = whole file resident
    const u64 tableEnd = 12 + static_cast<u64>(count) * 0x14;
    if (tableEnd > m.residentBase || m.residentBase > m.residentSize || m.residentSize > m.size)
        throw Error(path + ": inconsistent header (table end / resident base / resident size / file size)");

    std::vector<u8> area = m.readAt(0, m.residentBase);   // header, table and all records
    ByteReader r(area);
    for (u32 i = 0; i < count; i++) {
        ObjectDef ob;
        const size_t te = 12 + static_cast<size_t>(i) * 0x14;
        ob.recordOffset = r.u32At(te); ob.recordSize = r.u32At(te + 4); ob.tableName = r.cstrAt(te + 8, 12);
        const std::string where = "object " + std::to_string(i) + " (" + ob.tableName + ")";
        if (ob.recordSize < 0x1cc || ob.recordOffset < tableEnd ||
            static_cast<u64>(ob.recordOffset) + ob.recordSize > m.residentBase)
            throw Error(path + ": " + where + ": record outside the record area");
        const size_t o = ob.recordOffset;
        ob.nImages = r.u32At(o); ob.nAnims = r.u32At(o + 4); ob.nVariations = r.u32At(o + 8);
        ob.imageTableOffset = r.u32At(o + 0x0c); ob.animTableOffset = r.u32At(o + 0x10);
        ob.variationTableOffset = r.u32At(o + 0x14); ob.unk18 = r.i32At(o + 0x18);
        auto section = [&](u32 off, u32 n, u64 sz, const char* what) {
            if (n && (off < 0x1cc || static_cast<u64>(off) + static_cast<u64>(n) * sz > ob.recordSize))
                throw Error(path + ": " + where + ": " + what + " table out of range");
        };
        section(ob.animTableOffset, ob.nAnims, 0x228, "anim");
        section(ob.variationTableOffset, ob.nVariations, 0x120, "variation");
        section(ob.imageTableOffset, ob.nImages, 0x1c, "image");

        for (int k = 0; k < 3; k++) ob.normal[k] = readNormal(r, o + 0x1c + 12 * static_cast<size_t>(k));
        ob.icon = r.u32At(o + 0x40); ob.unk44 = r.u32At(o + 0x44);
        ob.holding = readHoldList(r, o + 0x48);
        ob.holdDelayMs = r.u32At(o + 0x108);
        ob.wearing = readHold(r, o + 0x10c);
        for (int k = 0; k < 6; k++) {
            size_t q = o + 0x124 + 16 * static_cast<size_t>(k);
            ob.named[k].image = r.u32At(q); ob.named[k].name = r.cstrAt(q + 4, 12);
        }
        ob.shortName = r.cstrAt(o + 0x184, 12);
        ob.moveable = r.u32At(o + 0x190); ob.randomZone = r.u32At(o + 0x194);
        ob.unk198 = r.u32At(o + 0x198); ob.unk19c = r.i32At(o + 0x19c);
        ob.wearable = r.u32At(o + 0x1a0); ob.fruit = r.u32At(o + 0x1a4); ob.unk1a8 = r.u32At(o + 0x1a8);
        ob.initZone = r.i32At(o + 0x1ac); ob.initZoneType = r.u8At(o + 0x1b0);
        ob.initVariation = r.cstrAt(o + 0x1b1, 12);
        ob.name = r.cstrAt(o + 0x1bd, 12);

        for (u32 k = 0; k < ob.nAnims; k++) {
            size_t a = o + ob.animTableOffset + 0x228 * static_cast<size_t>(k);
            ObjectAnim an;
            an.event = r.i32At(a); an.x = r.i32At(a + 4); an.y = r.i32At(a + 8);
            an.unk0c = r.i32At(a + 12); an.delayMs = r.i32At(a + 16); an.file = r.cstrAt(a + 0x14, 0x104);
            an.wav = r.cstrAt(a + 0x118, 0x104); an.variation = r.cstrAt(a + 0x21c, 12);
            ob.anims.push_back(an);
        }
        std::set<u32> dyn;   // images used as holding/wearing image by the object or a variation
        for (const HoldImage& h : ob.holding) dyn.insert(h.image);
        dyn.insert(ob.wearing.image);
        for (u32 k = 0; k < ob.nVariations; k++) {
            size_t v = o + ob.variationTableOffset + 0x120 * static_cast<size_t>(k);
            ObjectVariation va;
            for (int j = 0; j < 3; j++) va.normal[j] = readNormal(r, v + 12 * static_cast<size_t>(j));
            va.icon = r.u32At(v + 0x24); va.unk28 = r.u32At(v + 0x28);
            va.holding = readHoldList(r, v + 0x2c);
            va.holdDelayMs = r.u32At(v + 0xec);
            va.wearing = readHold(r, v + 0xf0);
            va.name = r.cstrAt(v + 0x114, 12);
            va.shortName = r.cstrAt(v + 0x108, 12);
            for (const HoldImage& h : va.holding) dyn.insert(h.image);
            dyn.insert(va.wearing.image);
            ob.variations.push_back(va);
        }
        u64 p = static_cast<u64>(o) + ob.imageTableOffset;
        const u64 recEnd = static_cast<u64>(o) + ob.recordSize;
        for (u32 k = 0; k < ob.nImages; k++) {
            if (p + 0x1c > recEnd) throw Error(path + ": " + where + ": image table out of range");
            ObjectImage im;
            im.compressed = r.u32At(p); im.width = r.u32At(p + 4); im.height = r.u32At(p + 8);
            im.stride = r.u32At(p + 12); im.bpp = r.u32At(p + 16); im.dataOffset = r.i32At(p + 20);
            im.runtimePtr = r.u32At(p + 24);
            p += 0x1c;
            const std::string iw = where + " image " + std::to_string(k + 1);
            if (im.stride < im.width) throw Error(path + ": " + iw + ": stride < width");
            const u64 n = static_cast<u64>(im.stride) * im.height;
            if (im.dataOffset == -1) {
                if (p + n > recEnd) throw Error(path + ": " + iw + ": inline pixel data out of range");
                im.area = ImageArea::Inline;
                im.fileOffset = p;
                p += n;
            } else {
                if (im.dataOffset < 0) throw Error(path + ": " + iw + ": bad data offset");
                const bool onDemand = dyn.count(k + 1) != 0;
                const u64 base = onDemand ? m.residentSize : m.residentBase;
                const u64 limit = onDemand ? m.size : m.residentSize;
                im.area = onDemand ? ImageArea::OnDemand : ImageArea::Resident;
                im.fileOffset = base + static_cast<u64>(im.dataOffset);
                if (im.fileOffset + n > limit) throw Error(path + ": " + iw + ": pixel data outside its image area");
            }
            ob.images.push_back(im);
        }
        m.objs.push_back(std::move(ob));
    }
}

ObjectFile::~ObjectFile() = default;

int ObjectFile::objectCount() const { return static_cast<int>(impl_->objs.size()); }

const ObjectDef& ObjectFile::object(int index) const {
    if (index < 0 || index >= objectCount()) throw Error("object index out of range: " + std::to_string(index));
    return impl_->objs[static_cast<size_t>(index)];
}

int ObjectFile::findObject(const std::string& name) const {
    for (size_t i = 0; i < impl_->objs.size(); i++)
        if (iequals(impl_->objs[i].name, name)) return static_cast<int>(i);
    return -1;
}

u32 ObjectFile::residentImageBase() const { return impl_->residentBase; }
u32 ObjectFile::residentSize() const { return impl_->residentSize; }
u64 ObjectFile::fileSize() const { return impl_->size; }

Image8 ObjectFile::loadImage(int objectIndex, int imageNumber) const {
    const ObjectDef& ob = object(objectIndex);
    if (imageNumber < 1 || static_cast<size_t>(imageNumber) > ob.images.size())
        throw Error("object " + ob.name + ": image number out of range: " + std::to_string(imageNumber));
    const ObjectImage& im = ob.images[static_cast<size_t>(imageNumber - 1)];
    const std::string where = "object " + ob.name + " image " + std::to_string(imageNumber);
    if (im.compressed != 0 || im.bpp != 8) throw Error(where + ": unsupported format (compressed or not 8 bpp)");
    const u64 n = static_cast<u64>(im.stride) * im.height;
    if (n > (u64(1) << 28)) throw Error(where + ": image too large");
    std::vector<u8> raw = impl_->readAt(im.fileOffset, n);
    Image8 out;
    out.w = static_cast<int>(im.width);
    out.h = static_cast<int>(im.height);
    out.px.resize(static_cast<size_t>(im.width) * im.height);
    for (u32 y = 0; y < im.height; y++) {
        const u8* src = raw.data() + static_cast<size_t>(im.height - 1 - y) * im.stride;
        std::copy(src, src + im.width, out.px.begin() + static_cast<size_t>(y) * im.width);
    }
    return out;
}

} // namespace biosys
