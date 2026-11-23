#include "engine/avi.h"
#include <algorithm>
#include <cstring>

namespace biosys {

namespace {

struct FileCloser { void operator()(FILE* f) const { if (f) std::fclose(f); } };

struct Chunk {
    uint64_t offset = 0;   // payload offset
    u32 size = 0;
    u8 stream = 0;
    char kind = 0;         // 'V' video, 'A' audio, 'P' palette change
    u32 videoBefore = 0;   // number of video chunks preceding this one
};

u32 le32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24); }
u16 le16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

struct AviFile::Impl {
    std::unique_ptr<FILE, FileCloser> f;
    uint64_t fileSize = 0;
    std::string path;

    u32 usPerFrame = 0;
    int w = 0, h = 0;
    int vStream = -1, aStream = -1;
    Palette initPal, pal;
    int aRate = 0, aChannels = 0, aBits = 0;
    u32 aBytes = 0;

    std::vector<Chunk> chunks;   // video, audio and palette chunks of the video/audio streams, in order
    size_t cursor = 0;           // next chunk to process for video
    size_t audioCursor = 0;
    u32 decoded = 0;
    u32 nVideo = 0;
    Image8 img;

    void readAt(uint64_t off, void* dst, size_t n) {
        if (off + n > fileSize || off + n < off) throw Error(path + ": read past end of file");
        if (n == 0) return;
        if (fseeko(f.get(), static_cast<off_t>(off), SEEK_SET) != 0 || std::fread(dst, 1, n, f.get()) != n)
            throw Error(path + ": read error");
    }
    std::vector<u8> readVec(uint64_t off, size_t n) {
        std::vector<u8> v(n);
        readAt(off, v.data(), n);
        return v;
    }

    void parseHeaderList(uint64_t p, uint64_t end);
    void parseStream(uint64_t p, uint64_t end, int index);
    void scanMovi(uint64_t p, uint64_t end);
    void decode(const std::vector<u8>& d);
};

void AviFile::Impl::parseStream(uint64_t p, uint64_t end, int index) {
    char type[4] = {0};
    std::vector<u8> strh, strf;
    while (p + 8 <= end) {
        u8 hd[8];
        readAt(p, hd, 8);
        u32 sz = le32(hd + 4);
        if (p + 8 + sz > end) throw Error(path + ": stream header chunk overruns list");
        if (!std::memcmp(hd, "strh", 4)) strh = readVec(p + 8, sz);
        else if (!std::memcmp(hd, "strf", 4)) strf = readVec(p + 8, sz);
        p += 8 + sz + (sz & 1);
    }
    if (strh.size() < 36 || strf.empty()) throw Error(path + ": incomplete stream header");
    std::memcpy(type, strh.data(), 4);
    if (!std::memcmp(type, "vids", 4)) {
        if (vStream >= 0) throw Error(path + ": more than one video stream");
        if (strf.size() < 40) throw Error(path + ": short BITMAPINFOHEADER");
        const u8* b = strf.data();
        u32 comp = le32(b + 16);
        char cc[5] = {static_cast<char>(comp), static_cast<char>(comp >> 8), static_cast<char>(comp >> 16),
                      static_cast<char>(comp >> 24), 0};
        std::string c = cc;
        for (auto& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (c != "CRAM" && c != "MSVC" && c != "WHAM") throw Error(path + ": unsupported video codec " + c);
        if (le16(b + 14) != 8) throw Error(path + ": video is not 8 bits per pixel");
        int32_t bw = static_cast<int32_t>(le32(b + 4)), bh = static_cast<int32_t>(le32(b + 8));
        if (bh < 0) bh = -bh;
        if (bw <= 0 || bh <= 0 || bw > 4096 || bh > 4096) throw Error(path + ": bad video size");
        if ((bw & 3) || (bh & 3)) throw Error(path + ": video size not a multiple of 4");
        w = bw; h = bh;
        u32 used = le32(b + 32);
        if (used == 0 || used > 256) used = 256;
        u32 hdrSize = le32(b);
        if (hdrSize < 40 || hdrSize > strf.size()) throw Error(path + ": bad BITMAPINFOHEADER size");
        if (hdrSize + used * 4 > strf.size()) used = static_cast<u32>((strf.size() - hdrSize) / 4);
        for (u32 i = 0; i < used; i++) {
            const u8* e = b + hdrSize + i * 4;
            initPal.c[i] = Color{e[2], e[1], e[0]};
        }
        vStream = index;
    } else if (!std::memcmp(type, "auds", 4)) {
        if (aStream >= 0) throw Error(path + ": more than one audio stream");
        if (strf.size() < 16) throw Error(path + ": short WAVEFORMATEX");
        if (le16(strf.data()) != 1) throw Error(path + ": audio is not PCM");
        aChannels = le16(strf.data() + 2);
        aRate = static_cast<int>(le32(strf.data() + 4));
        aBits = le16(strf.data() + 14);
        if (aBits != 8 || aChannels < 1 || aChannels > 2 || aRate <= 0) throw Error(path + ": unsupported audio format");
        aStream = index;
    } else {
        throw Error(path + ": unsupported stream type");
    }
}

void AviFile::Impl::parseHeaderList(uint64_t p, uint64_t end) {
    int streamIndex = 0;
    while (p + 8 <= end) {
        u8 hd[12];
        readAt(p, hd, 8);
        u32 sz = le32(hd + 4);
        if (p + 8 + sz > end) throw Error(path + ": header chunk overruns list");
        if (!std::memcmp(hd, "avih", 4)) {
            if (sz < 4) throw Error(path + ": short avih");
            u8 v[4];
            readAt(p + 8, v, 4);
            usPerFrame = le32(v);
        } else if (!std::memcmp(hd, "LIST", 4)) {
            if (sz < 4) throw Error(path + ": short LIST");
            readAt(p + 8, hd + 8, 4);
            if (!std::memcmp(hd + 8, "strl", 4)) parseStream(p + 12, p + 8 + sz, streamIndex++);
        }
        p += 8 + sz + (sz & 1);
    }
}

void AviFile::Impl::scanMovi(uint64_t p, uint64_t end) {
    while (p + 8 <= end) {
        u8 hd[12];
        readAt(p, hd, 8);
        u32 sz = le32(hd + 4);
        if (!std::memcmp(hd, "LIST", 4)) {
            if (sz < 4 || p + 8 + sz > end) throw Error(path + ": bad nested LIST in movi");
            scanMovi(p + 12, p + 8 + sz);
        } else {
            int hi = hexv(static_cast<char>(hd[0])), lo = hexv(static_cast<char>(hd[1]));
            if (hi >= 0 && lo >= 0) {
                if (p + 8 + sz > end) throw Error(path + ": truncated chunk in movi");
                int stream = hi * 16 + lo;
                char k = 0;
                if (stream == vStream && hd[2] == 'p' && hd[3] == 'c') k = 'P';
                else if (stream == vStream && hd[2] == 'd' && (hd[3] == 'c' || hd[3] == 'b')) k = 'V';
                else if (stream == aStream && hd[2] == 'w' && hd[3] == 'b') k = 'A';
                if (k) {
                    Chunk c;
                    c.offset = p + 8; c.size = sz; c.stream = static_cast<u8>(stream); c.kind = k;
                    c.videoBefore = nVideo;
                    if (k == 'V') nVideo++;
                    if (k == 'A') aBytes += sz;
                    chunks.push_back(c);
                }
            }
        }
        p += 8 + static_cast<uint64_t>(sz) + (sz & 1);
    }
}

// Pixels: block row 0 of the flags is the bottom row of the block; blocks run left to right,
// bottom to top.
void AviFile::Impl::decode(const std::vector<u8>& d) {
    const int bw = w / 4, bh = h / 4;
    size_t pos = 0;
    long skip = 0;
    auto need = [&](size_t n) { if (pos + n > d.size()) throw Error(path + ": truncated MS Video 1 frame"); };
    for (int by = bh - 1; by >= 0; by--) {
        for (int bx = 0; bx < bw; bx++) {
            if (skip > 0) { skip--; continue; }
            need(2);
            u8 a = d[pos], b = d[pos + 1];
            pos += 2;
            if ((b & 0xFC) == 0x84) {
                long n = ((b - 0x84) << 8) + a;
                if (n < 1) throw Error(path + ": zero-length skip run");
                skip = n - 1;
                continue;
            }
            u8* dst = &img.px[static_cast<size_t>(by) * 4 * w + static_cast<size_t>(bx) * 4];
            u8 col[8];
            u32 flags = static_cast<u32>(b << 8) | a;
            if (b < 0x80) {
                need(2);
                col[0] = d[pos]; col[1] = d[pos + 1];
                pos += 2;
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++, flags >>= 1) dst[(3 - r) * w + c] = col[(flags & 1) ^ 1];
            } else if (b >= 0x90) {
                need(8);
                std::memcpy(col, &d[pos], 8);
                pos += 8;
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++, flags >>= 1)
                        dst[(3 - r) * w + c] = col[((r & 2) << 1) + (c & 2) + ((flags & 1) ^ 1)];
            } else {
                for (int r = 0; r < 4; r++) std::memset(dst + r * w, a, 4);
            }
        }
    }
}

AviFile::AviFile(const std::string& path) : p_(new Impl) {
    Impl& s = *p_;
    s.path = path;
    s.f.reset(std::fopen(path.c_str(), "rb"));
    if (!s.f) throw Error("cannot open " + path);
    fseeko(s.f.get(), 0, SEEK_END);
    s.fileSize = static_cast<uint64_t>(ftello(s.f.get()));
    u8 hd[12];
    s.readAt(0, hd, 12);
    if (std::memcmp(hd, "RIFF", 4) || std::memcmp(hd + 8, "AVI ", 4)) throw Error(path + ": not an AVI file");
    uint64_t end = std::min<uint64_t>(s.fileSize, 8 + static_cast<uint64_t>(le32(hd + 4)));
    uint64_t p = 12;
    bool haveMovi = false;
    while (p + 8 <= end) {
        s.readAt(p, hd, 8);
        u32 sz = le32(hd + 4);
        uint64_t cend = p + 8 + sz;
        if (!std::memcmp(hd, "LIST", 4)) {
            if (sz < 4) throw Error(path + ": short LIST");
            s.readAt(p + 8, hd + 8, 4);
            if (cend > end) cend = end;   // a truncated final list is tolerated; chunk scan validates
            if (!std::memcmp(hd + 8, "hdrl", 4)) s.parseHeaderList(p + 12, cend);
            else if (!std::memcmp(hd + 8, "movi", 4)) {
                if (s.vStream < 0) throw Error(path + ": movi before video stream header");
                s.scanMovi(p + 12, cend);
                haveMovi = true;
            }
        }
        p = cend + (sz & 1);
    }
    if (s.vStream < 0) throw Error(path + ": no video stream");
    if (!haveMovi) throw Error(path + ": no movi list");
    if (s.nVideo == 0) throw Error(path + ": no video frames");
    if (s.usPerFrame == 0) throw Error(path + ": zero frame duration");
    s.pal = s.initPal;
    s.img.w = s.w; s.img.h = s.h;
    s.img.px.assign(static_cast<size_t>(s.w) * s.h, 0);
}

AviFile::AviFile(AviFile&&) noexcept = default;
AviFile& AviFile::operator=(AviFile&&) noexcept = default;
AviFile::~AviFile() = default;

int AviFile::frameCount() const { return static_cast<int>(p_->nVideo); }
int AviFile::width() const { return p_->w; }
int AviFile::height() const { return p_->h; }
u32 AviFile::microsPerFrame() const { return p_->usPerFrame; }
double AviFile::fps() const { return 1e6 / p_->usPerFrame; }
int AviFile::framesDecoded() const { return static_cast<int>(p_->decoded); }
const Image8& AviFile::frame() const { return p_->img; }
const Palette& AviFile::palette() const { return p_->pal; }
const Palette& AviFile::initialPalette() const { return p_->initPal; }

bool AviFile::nextFrame() {
    Impl& s = *p_;
    while (s.cursor < s.chunks.size()) {
        const Chunk& c = s.chunks[s.cursor++];
        if (c.kind == 'P') {
            std::vector<u8> d = s.readVec(c.offset, c.size);
            if (d.size() < 4) throw Error(s.path + ": short palette chunk");
            int first = d[0], n = d[1] ? d[1] : 256;
            if (first + n > 256 || d.size() < 4u + n * 4u) throw Error(s.path + ": bad palette chunk");
            for (int i = 0; i < n; i++) s.pal.c[first + i] = Color{d[4 + i * 4], d[5 + i * 4], d[6 + i * 4]};
        } else if (c.kind == 'V') {
            if (c.size) s.decode(s.readVec(c.offset, c.size));   // empty chunk: repeat previous frame
            s.decoded++;
            return true;
        }
    }
    return false;
}

void AviFile::rewind() {
    Impl& s = *p_;
    s.cursor = 0; s.audioCursor = 0; s.decoded = 0;
    s.pal = s.initPal;
    std::fill(s.img.px.begin(), s.img.px.end(), 0);
}

bool AviFile::hasAudio() const { return p_->aStream >= 0; }
int AviFile::audioSampleRate() const { return p_->aRate; }
int AviFile::audioChannels() const { return p_->aChannels; }
int AviFile::audioBits() const { return p_->aBits; }
u32 AviFile::audioSampleCount() const {
    return p_->aStream < 0 ? 0 : p_->aBytes / static_cast<u32>(p_->aChannels * p_->aBits / 8);
}

std::vector<u8> AviFile::takeAudio() {
    Impl& s = *p_;
    std::vector<u8> out;
    // Chunks stored before the video chunk of frame number `decoded` (i.e. videoBefore < decoded),
    // plus those before the first video chunk when nothing is decoded yet.
    u32 limit = s.decoded ? s.decoded : 1;
    while (s.audioCursor < s.chunks.size()) {
        const Chunk& c = s.chunks[s.audioCursor];
        if (c.videoBefore >= limit) break;
        s.audioCursor++;
        if (c.kind != 'A') continue;
        size_t o = out.size();
        out.resize(o + c.size);
        s.readAt(c.offset, out.data() + o, c.size);
    }
    return out;
}

std::vector<u8> AviFile::readAllAudio() {
    Impl& s = *p_;
    std::vector<u8> out;
    for (const Chunk& c : s.chunks) {
        if (c.kind != 'A') continue;
        size_t o = out.size();
        out.resize(o + c.size);
        s.readAt(c.offset, out.data() + o, c.size);
    }
    return out;
}

} // namespace biosys
