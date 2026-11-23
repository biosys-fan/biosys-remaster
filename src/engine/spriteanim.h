// .TAN sprite animations (format notes: analysis/notes/formats.md section 4; engine reader exe:00453930).
#pragma once
#include "engine/common.h"
#include "engine/image.h"

namespace biosys {

class TanFile {
public:
    // Reads and validates the header only. Throws Error if the file is not a TAN or the header is
    // inconsistent. A file whose frame data is truncated still opens; frame() throws for the
    // missing frames.
    //
    // Header variant: the frame table holds 200 entries (header 0x758 bytes) or 100 entries
    // (older writer, header 0x5c8). Like the reference, the header size is the smallest offset in
    // the first frameCount() table entries (= start of frame data); it must be 0x5c8 or 0x758.
    explicit TanFile(const std::string& path);

    int frameCount() const { return static_cast<int>(offsets_.size()); }
    int width() const { return width_; }
    int height() const { return height_; }
    u32 transparency() const { return transp_; }   // +0x08: 1 = sprite has transparency
    u32 unk0c() const { return unk0c_; }           // +0x0c, meaning unknown
    const Palette& palette() const { return pal_; }
    int tableSize() const { return tableSize_; }   // 100 or 200
    u32 frameOffset(int index) const;              // absolute file offset of logical frame
    int physicalFrameCount() const;                // distinct frame offsets
    bool hasFrame(int index) const;                // frame data fully inside the file
    // Top-down, unpadded pixels. Throws Error for a bad index or a frame beyond end of file.
    Image8 frame(int index) const;

private:
    std::string path_;
    uint64_t fileSize_ = 0;
    u32 transp_ = 0, unk0c_ = 0;
    int width_ = 0, height_ = 0;
    bool topDown_ = false;
    int tableSize_ = 200;
    Palette pal_;
    std::vector<u32> offsets_;
};

} // namespace biosys
