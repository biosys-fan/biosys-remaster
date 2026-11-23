// Minimal zip reader (methods 0 and 8, no zip64) for the downloaded CD image.
#include "install/internal.h"
#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace biosys::install {

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

std::string damagedZip(const std::string& path, const char* what) {
    return "The zip file \"" + path + "\" " + what + " " + kRedownload;
}

// Writes 2048-byte sectors. A raw CloneCD image (first 12 bytes are the sync pattern) is converted sector by sector as
// img2iso.py does; anything else is passed through unchanged.
class SectorSink {
public:
    SectorSink(FILE* f, const std::string& path) : f_(f), path_(path) {}
    void feed(const uint8_t* p, size_t n) {
        if (mode_ == Mode::Unknown) {
            size_t take = std::min(n, size_t(12) - buf_.size());
            buf_.insert(buf_.end(), p, p + take);
            p += take;
            n -= take;
            if (buf_.size() < 12) return;
            bool sync = buf_[0] == 0 && buf_[11] == 0;
            for (int i = 1; i < 11 && sync; i++) sync = buf_[i] == 0xff;
            mode_ = sync ? Mode::Raw : Mode::Plain;
            if (mode_ == Mode::Plain) {
                emit(buf_.data(), buf_.size());
                buf_.clear();
            }
        }
        if (mode_ == Mode::Plain) {
            emit(p, n);
            return;
        }
        while (n > 0) {
            size_t take = std::min(n, size_t(2352) - buf_.size());
            buf_.insert(buf_.end(), p, p + take);
            p += take;
            n -= take;
            if (buf_.size() == 2352) {
                const uint8_t mode = buf_[15];
                if (mode == 1) emit(buf_.data() + 16, 2048);
                else if (mode == 2) emit(buf_.data() + 24, 2048);
                else {
                    static const uint8_t zeros[2048] = {};
                    emit(zeros, 2048);
                }
                buf_.clear();
            }
        }
    }
    void finish() {
        if (mode_ == Mode::Unknown && !buf_.empty()) emit(buf_.data(), buf_.size());
        if (std::fflush(f_) != 0) fail();
    }
private:
    enum class Mode { Unknown, Plain, Raw };
    void emit(const uint8_t* p, size_t n) {
        if (n && std::fwrite(p, 1, n, f_) != n) fail();
    }
    [[noreturn]] void fail() {
        throw Error("I cannot write \"" + path_ + "\". The disk may be full or not writable.");
    }
    FILE* f_;
    std::string path_;
    Mode mode_ = Mode::Unknown;
    std::vector<uint8_t> buf_;
};
} // namespace

ZipArchive::ZipArchive(const std::string& path) : src_(path), path_(path) {
    const uint64_t fsz = src_.size();
    if (fsz < 22) throw Error(damagedZip(path, "is too short to be a zip archive."));
    const uint64_t tailLen = std::min<uint64_t>(fsz, 22 + 65535);
    std::vector<uint8_t> tail(tailLen);
    src_.read(fsz - tailLen, tail.data(), tail.size());
    size_t eocd = SIZE_MAX;
    for (size_t i = tail.size() - 22 + 1; i-- > 0;) {
        if (le32(&tail[i]) == 0x06054b50 && i + 22 + le16(&tail[i + 20]) <= tail.size()) {
            eocd = i;
            break;
        }
    }
    if (eocd == SIZE_MAX)
        throw Error(damagedZip(path, "ends too early (it is incomplete or damaged)."));
    const uint8_t* e = &tail[eocd];
    const bool z64locator = eocd >= 20 && le32(&tail[eocd - 20]) == 0x07064b50;
    if (z64locator || le16(e + 10) == 0xffff || le32(e + 12) == 0xffffffffu || le32(e + 16) == 0xffffffffu)
        throw Error("The zip file \"" + path + "\" uses the Zip64 format, which this program cannot read. "
                    "Unpack it with your system's own tool and drop the unpacked folder here instead.");
    if (le16(e + 4) != 0 || le16(e + 6) != 0)
        throw Error("The zip file \"" + path + "\" is split over several parts, which is not supported.");
    const uint32_t count = le16(e + 10), cdSize = le32(e + 12), cdOff = le32(e + 16);
    if (uint64_t(cdOff) + cdSize > fsz) throw Error(damagedZip(path, "has a damaged directory."));
    std::vector<uint8_t> cd(cdSize);
    src_.read(cdOff, cd.data(), cd.size());
    size_t pos = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (pos + 46 > cd.size() || le32(&cd[pos]) != 0x02014b50) throw Error(damagedZip(path, "has a damaged directory."));
        const uint8_t* h = &cd[pos];
        const size_t nl = le16(h + 28), xl = le16(h + 30), cl = le16(h + 32);
        if (pos + 46 + nl + xl + cl > cd.size()) throw Error(damagedZip(path, "has a damaged directory."));
        ZipEntry z;
        z.name.assign(reinterpret_cast<const char*>(h + 46), nl);
        z.method = le16(h + 10);
        z.crc = le32(h + 16);
        z.csize = le32(h + 20);
        z.usize = le32(h + 24);
        const uint32_t lho = le32(h + 42);
        const uint16_t flags = le16(h + 8);
        z.isDir = !z.name.empty() && z.name.back() == '/';
        if (z.csize == 0xffffffffu || z.usize == 0xffffffffu || lho == 0xffffffffu)
            throw Error("The zip file \"" + path + "\" uses the Zip64 format, which this program cannot read. "
                        "Unpack it with your system's own tool and drop the unpacked folder here instead.");
        pos += 46 + nl + xl + cl;
        if (z.isDir) {
            entries_.push_back(z);
            continue;
        }
        uint8_t lh[30];
        if (uint64_t(lho) + 30 > fsz) throw Error(damagedZip(path, "has a damaged directory."));
        src_.read(lho, lh, 30);
        if (le32(lh) != 0x04034b50) throw Error(damagedZip(path, "has a damaged directory."));
        z.dataOff = uint64_t(lho) + 30 + le16(lh + 26) + le16(lh + 28);
        if (z.dataOff > fsz || z.csize > fsz - z.dataOff)
            throw Error(damagedZip(path, "ends too early (it is incomplete or damaged)."));
        if (flags & 1) throw Error("The zip file \"" + path + "\" is password protected, which is not supported.");
        entries_.push_back(z);
    }
}

const ZipEntry* ZipArchive::findImage() const {
    const ZipEntry* best = nullptr;
    for (const ZipEntry& e : entries_) {
        if (e.isDir) continue;
        if (!(endsWithI(e.name, ".img") || endsWithI(e.name, ".iso") || endsWithI(e.name, ".bin"))) continue;
        if (!best || e.usize > best->usize) best = &e;
    }
    return best;
}

void ZipArchive::unpackImage(const ZipEntry& e, const std::string& outPath, Reporter& rep) const {
    if (e.method != 0 && e.method != 8)
        throw Error("The zip file \"" + path_ + "\" uses a compression method this program cannot read.");
    if (e.usize > kMaxZipEntry)
        throw Error("The disc image inside \"" + path_ + "\" is larger than any Biosys CD (" + humanBytes(e.usize) +
                    "), so it is refused. " + kRedownload);
    if (e.method == 0 && e.csize != e.usize) throw Error(damagedZip(path_, "is damaged."));

    std::unique_ptr<FILE, int (*)(FILE*)> out(std::fopen(outPath.c_str(), "wb"), [](FILE* f) { return f ? std::fclose(f) : 0; });
    if (!out) throw Error("I cannot write \"" + outPath + "\". Check the free space and the permissions of that folder.");
    SectorSink sink(out.get(), outPath);
    rep.begin("unpack", e.usize);

    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    const bool defl = e.method == 8;
    if (defl && inflateInit2(&zs, -15) != Z_OK) throw Error("Internal error: cannot start the decompressor.");
    struct Ender { z_stream* z; bool on; ~Ender() { if (on) inflateEnd(z); } } ender{&zs, defl};

    std::vector<uint8_t> in(1 << 16), outBuf(1 << 18);
    uint64_t readPos = 0, produced = 0;
    uLong crc = crc32(0, Z_NULL, 0);
    bool ended = !defl;
    auto emit = [&](const uint8_t* p, size_t n) {
        produced += n;
        if (produced > e.usize) throw Error(damagedZip(path_, "is damaged."));
        crc = crc32(crc, p, static_cast<uInt>(n));
        sink.feed(p, n);
        rep.add(n);
    };
    while (readPos < e.csize) {
        const size_t n = size_t(std::min<uint64_t>(in.size(), e.csize - readPos));
        src_.read(e.dataOff + readPos, in.data(), n);
        readPos += n;
        if (!defl) {
            emit(in.data(), n);
            continue;
        }
        if (ended) break;   // data after the end of the deflate stream: ignored
        zs.next_in = in.data();
        zs.avail_in = static_cast<uInt>(n);
        while (zs.avail_in > 0 && !ended) {
            zs.next_out = outBuf.data();
            zs.avail_out = static_cast<uInt>(outBuf.size());
            int r = inflate(&zs, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) throw Error(damagedZip(path_, "is damaged (its data cannot be unpacked)."));
            const size_t got = outBuf.size() - zs.avail_out;
            if (got) emit(outBuf.data(), got);
            if (r == Z_STREAM_END) ended = true;
            else if (r == Z_BUF_ERROR && got == 0) break;
        }
    }
    if (defl) {   // flush what is still inside the decompressor
        while (!ended) {
            zs.next_out = outBuf.data();
            zs.avail_out = static_cast<uInt>(outBuf.size());
            int r = inflate(&zs, Z_FINISH);
            const size_t got = outBuf.size() - zs.avail_out;
            if (got) emit(outBuf.data(), got);
            if (r == Z_STREAM_END) ended = true;
            else if (r != Z_BUF_ERROR && r != Z_OK) throw Error(damagedZip(path_, "is damaged (its data cannot be unpacked)."));
            else if (got == 0) break;
        }
    }
    if (!ended || produced != e.usize || static_cast<uint32_t>(crc) != e.crc)
        throw Error(damagedZip(path_, "is damaged (its checksum does not match)."));
    sink.finish();
    if (std::fclose(out.release()) != 0) throw Error("I cannot write \"" + outPath + "\". The disk may be full.");
}

} // namespace biosys::install
