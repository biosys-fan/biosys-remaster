#include "engine/spriteanim.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>

namespace biosys {

static const u32 kMagic = 0x36fe078b;
static const size_t kTableOff = 0x438;
static const u32 kHdr100 = 0x5c8, kHdr200 = 0x758;

TanFile::TanFile(const std::string& path) : path_(path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("cannot open " + path);
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz < 0) throw Error("cannot size " + path);
    fileSize_ = static_cast<uint64_t>(sz);
    std::vector<u8> hdr(static_cast<size_t>(std::min<uint64_t>(fileSize_, kHdr200)));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    if (!f) throw Error("cannot read header of " + path);

    ByteReader r(hdr);
    if (r.u32At(0) != kMagic) throw Error(path + ": not a TAN file");
    u32 n = r.u32At(4);
    transp_ = r.u32At(8);
    unk0c_ = r.u32At(0x0c);
    if (n == 0 || n > 200) throw Error(path + ": bad frame count");
    if (r.u32At(0x10) != 40) throw Error(path + ": bad BITMAPINFOHEADER size");
    i32 w = r.i32At(0x14), h = r.i32At(0x18);
    if (r.u16At(0x1c) != 1 || r.u16At(0x1e) != 8) throw Error(path + ": not 8bpp");
    if (r.u32At(0x20) != 0) throw Error(path + ": compressed TAN");
    topDown_ = h < 0;
    if (h == INT32_MIN || w <= 0 || w > 8192 || h == 0 || std::abs(h) > 8192)
        throw Error(path + ": bad dimensions");
    width_ = w;
    height_ = std::abs(h);

    for (int i = 0; i < 256; i++) {
        pal_.c[i].b = r.u8At(0x38 + 4 * i);
        pal_.c[i].g = r.u8At(0x38 + 4 * i + 1);
        pal_.c[i].r = r.u8At(0x38 + 4 * i + 2);
    }
    offsets_.resize(n);
    for (u32 i = 0; i < n; i++) offsets_[i] = r.u32At(kTableOff + 4 * i);
    u32 hdrSize = *std::min_element(offsets_.begin(), offsets_.end());
    if (hdrSize == kHdr100) tableSize_ = 100;
    else if (hdrSize == kHdr200) tableSize_ = 200;
    else throw Error(path + ": unexpected header size");
    if (static_cast<int>(n) > tableSize_) throw Error(path + ": frame count exceeds table");
}

u32 TanFile::frameOffset(int index) const {
    if (index < 0 || index >= frameCount()) throw Error("frame index out of range");
    return offsets_[static_cast<size_t>(index)];
}

int TanFile::physicalFrameCount() const {
    return static_cast<int>(std::set<u32>(offsets_.begin(), offsets_.end()).size());
}

bool TanFile::hasFrame(int index) const {
    uint64_t stride = (static_cast<uint64_t>(width_) + 3) & ~3ull;
    return static_cast<uint64_t>(frameOffset(index)) + stride * static_cast<uint64_t>(height_) <= fileSize_;
}

Image8 TanFile::frame(int index) const {
    if (!hasFrame(index)) throw Error(path_ + ": frame " + std::to_string(index) + " is beyond end of file");
    size_t stride = (static_cast<size_t>(width_) + 3) & ~size_t(3);
    std::vector<u8> raw(stride * static_cast<size_t>(height_));
    std::ifstream f(path_, std::ios::binary);
    f.seekg(frameOffset(index));
    f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!f) throw Error(path_ + ": cannot read frame " + std::to_string(index));
    Image8 img;
    img.w = width_;
    img.h = height_;
    img.px.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_));
    for (int y = 0; y < height_; y++) {
        int srcRow = topDown_ ? y : height_ - 1 - y;
        std::copy_n(raw.begin() + static_cast<std::ptrdiff_t>(stride * srcRow), width_,
                    img.px.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(width_) * y));
    }
    return img;
}

} // namespace biosys
