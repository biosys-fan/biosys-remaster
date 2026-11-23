#include "engine/pagefile.h"
#include <cstdio>
#include <sys/types.h>
#include <zlib.h>

namespace biosys {

using u64 = uint64_t;

struct PageFile::Impl {
    std::string path;
    std::unique_ptr<FILE, int (*)(FILE*)> f{nullptr, std::fclose};
    u64 size = 0;
    std::vector<PageTableEntry> table;

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

PageFile::PageFile(const std::string& path) : impl_(new Impl) {
    impl_->path = path;
    impl_->f.reset(std::fopen(path.c_str(), "rb"));
    if (!impl_->f) throw Error("cannot open " + path);
    if (fseeko(impl_->f.get(), 0, SEEK_END) != 0) throw Error(path + ": cannot seek");
    impl_->size = static_cast<u64>(ftello(impl_->f.get()));
    std::vector<u8> head = impl_->readAt(0, 4);
    u32 count = ByteReader(head).u32At(0);
    if (4 + static_cast<u64>(count) * 28 > impl_->size) throw Error(path + ": page table exceeds file size");
    std::vector<u8> tab = impl_->readAt(4, static_cast<u64>(count) * 28);
    ByteReader r(tab);
    for (u32 i = 0; i < count; i++) {
        size_t o = static_cast<size_t>(i) * 28;
        PageTableEntry e;
        e.offset = r.u32At(o); e.staticSize = r.u32At(o + 4); e.dynaSize = r.u32At(o + 8);
        e.flags = r.u32At(o + 12); e.name = r.cstrAt(o + 16, 12);
        if (static_cast<u64>(e.offset) + e.staticSize + e.dynaSize > impl_->size)
            throw Error(path + ": page " + std::to_string(i + 1) + " (" + e.name + ") extends past end of file");
        impl_->table.push_back(e);
    }
}

PageFile::~PageFile() = default;

int PageFile::pageCount() const { return static_cast<int>(impl_->table.size()); }

const PageTableEntry& PageFile::entry(int number) const {
    if (number < 1 || number > pageCount()) throw Error("page number out of range: " + std::to_string(number));
    return impl_->table[static_cast<size_t>(number - 1)];
}

int PageFile::findPage(const std::string& name) const {
    for (size_t i = 0; i < impl_->table.size(); i++)
        if (iequals(impl_->table[i].name, name)) return static_cast<int>(i + 1);
    return 0;
}

Page PageFile::loadPage(int number) const {
    const PageTableEntry& te = entry(number);
    const std::string where = "page " + std::to_string(number) + " (" + te.name + ")";
    if (te.staticSize < 0x5c) throw Error(where + ": static part smaller than header");
    std::vector<u8> data = impl_->readAt(te.offset, te.staticSize);
    ByteReader r(data);
    const u64 total = data.size();
    auto section = [&](u32 off, u32 n, u32 sz, const char* what) {
        if (n && (off < 0x5c || static_cast<u64>(off) + static_cast<u64>(n) * sz > total))
            throw Error(where + ": " + what + " table out of range");
    };

    Page p;
    p.number = number;
    p.flags = r.u32At(0); p.width = r.u32At(4); p.height = r.u32At(8);
    u32 nOrn = r.u32At(0x0c), nFiles = r.u32At(0x10), nHot = r.u32At(0x14), nTimers = r.u32At(0x18);
    u32 nImages = r.u32At(0x1c), nXf = r.u32At(0x24);
    p.initialScrollX = r.u32At(0x28);
    u32 oOrn = r.u32At(0x2c), oFiles = r.u32At(0x30), oHot = r.u32At(0x34), oTimers = r.u32At(0x38);
    u32 oImages = r.u32At(0x3c), oXf = r.u32At(0x48);
    p.background = r.u32At(0x4c);
    p.name = r.cstrAt(0x50, 12);

    section(oOrn, nOrn, 0x24, "ornament");
    section(oHot, nHot, 0x48, "hotspot");
    section(oTimers, nTimers, 0x40, "timer");
    section(oXf, nXf, 0x58, "cross-fade");
    section(oImages, nImages, 0x1c, "image");

    auto readAction = [&](size_t o) {
        Action a;
        a.verb = r.i32At(o);
        for (int i = 0; i < 8; i++) a.arg[i] = r.i32At(o + 4 + 4 * static_cast<size_t>(i));
        return a;
    };

    for (u32 i = 0; i < nOrn; i++) {
        size_t o = oOrn + static_cast<size_t>(i) * 0x24;
        Ornament e;
        e.name = r.cstrAt(o, 12); e.type = r.u32At(o + 0x0c); e.flags = r.u32At(o + 0x10);
        e.x = r.i32At(o + 0x14); e.y = r.i32At(o + 0x18); e.image = r.u32At(o + 0x1c);
        e.transparent.b = r.u8At(o + 0x20); e.transparent.g = r.u8At(o + 0x21); e.transparent.r = r.u8At(o + 0x22);
        p.ornaments.push_back(e);
    }
    {
        size_t q = oFiles;
        for (u32 i = 0; i < nFiles; i++) {
            if (nFiles && oFiles < 0x5c) throw Error(where + ": file list out of range");
            size_t e = q;
            while (e < total && data[e]) e++;
            if (e >= total) throw Error(where + ": unterminated file name");
            p.files.emplace_back(reinterpret_cast<const char*>(&data[q]), e - q);
            q = e + 1;
        }
    }
    for (u32 i = 0; i < nHot; i++) {
        size_t o = oHot + static_cast<size_t>(i) * 0x48;
        Hotspot h;
        h.left = r.i32At(o); h.top = r.i32At(o + 4); h.right = r.i32At(o + 8); h.bottom = r.i32At(o + 12);
        h.flags = r.u32At(o + 0x10); h.button = r.u32At(o + 0x14);
        h.action = readAction(o + 0x18);
        h.name = r.cstrAt(o + 0x3c, 12);
        p.hotspots.push_back(h);
    }
    for (u32 i = 0; i < nTimers; i++) {
        size_t o = oTimers + static_cast<size_t>(i) * 0x40;
        PageTimer t;
        t.delay = r.u32At(o + 4);
        t.action = readAction(o + 0x10);
        t.name = r.cstrAt(o + 0x34, 12);
        p.timers.push_back(t);
    }
    for (u32 i = 0; i < nXf; i++) {
        size_t o = oXf + static_cast<size_t>(i) * 0x58;
        CrossFade c;
        c.steps = r.u32At(o + 4);
        c.name = r.cstrAt(o + 0x4c, 12);
        p.crossFades.push_back(c);
    }
    u64 q = oImages;
    const u64 dynaBase = static_cast<u64>(te.offset) + te.staticSize;
    for (u32 i = 0; i < nImages; i++) {
        if (q + 0x1c > total) throw Error(where + ": image table out of range");
        PageImage im;
        im.compressed = r.u32At(q); im.width = r.u32At(q + 4); im.height = r.u32At(q + 8);
        im.stride = r.u32At(q + 12); im.dataSize = r.u32At(q + 20); im.dynaOffset = r.i32At(q + 24);
        if (im.compressed > 1) throw Error(where + ": image " + std::to_string(i) + " has bad compression flag");
        if (im.stride < im.width) throw Error(where + ": image " + std::to_string(i) + " stride < width");
        q += 0x1c;
        u64 n = im.compressed ? im.dataSize : static_cast<u64>(im.stride) * im.height;
        if (im.dynaOffset == -1) {
            if (q + n > total) throw Error(where + ": inline pixel data of image " + std::to_string(i) + " out of range");
            im.fileOffset = static_cast<u32>(te.offset + q);
            q += n;
        } else {
            if (im.dynaOffset < 0 || static_cast<u64>(im.dynaOffset) + n > te.dynaSize)
                throw Error(where + ": dyna-load data of image " + std::to_string(i) + " out of range");
            im.fileOffset = static_cast<u32>(dynaBase + static_cast<u64>(im.dynaOffset));
        }
        p.images.push_back(im);
    }
    return p;
}

Image8 PageFile::loadImage(const Page& page, int imageIndex) const {
    if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= page.images.size())
        throw Error("page " + page.name + ": image index out of range: " + std::to_string(imageIndex));
    const PageImage& im = page.images[static_cast<size_t>(imageIndex)];
    const std::string where = "page " + page.name + " image " + std::to_string(imageIndex);
    if (im.stride < im.width) throw Error(where + ": stride < width");
    const u64 full = static_cast<u64>(im.stride) * im.height;
    if (full > (u64(1) << 28)) throw Error(where + ": image too large");
    const u64 n = im.compressed ? im.dataSize : full;
    std::vector<u8> raw = impl_->readAt(im.fileOffset, n);

    Image8 out;
    out.w = static_cast<int>(im.width);
    out.h = static_cast<int>(im.height);
    out.px.resize(static_cast<size_t>(im.width) * im.height);
    std::vector<u8> unz;
    const u8* src = raw.data();
    if (im.compressed) {
        unz.resize(static_cast<size_t>(full));
        uLongf len = static_cast<uLongf>(full);
        int rc = uncompress(unz.data(), &len, raw.data(), static_cast<uLong>(raw.size()));
        if (rc != Z_OK) throw Error(where + ": zlib error " + std::to_string(rc));
        if (len != full) throw Error(where + ": decompressed size " + std::to_string(len) + " != stride*height");
        src = unz.data();
    }
    for (u32 y = 0; y < im.height; y++) {
        u32 sy = im.compressed ? y : im.height - 1 - y;
        std::copy(src + static_cast<size_t>(sy) * im.stride, src + static_cast<size_t>(sy) * im.stride + im.width,
                  out.px.begin() + static_cast<size_t>(y) * im.width);
    }
    return out;
}

} // namespace biosys
