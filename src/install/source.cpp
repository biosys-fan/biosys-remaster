// Byte sources, name checks, progress and the ISO 9660 reader of the first-run import.
#include "install/internal.h"
#include <algorithm>
#include <cstring>

namespace biosys::install {

std::string humanBytes(uint64_t n) {
    char b[48];
    if (n >= (1ull << 30)) std::snprintf(b, sizeof b, "%.1f GB", double(n) / double(1ull << 30));
    else std::snprintf(b, sizeof b, "%.0f MB", double(n) / double(1ull << 20));
    return b;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

bool endsWithI(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && lowerAscii(s.substr(s.size() - n)) == suffix;
}

const std::string& checkComponent(const std::string& c) {
    bool bad = c.empty() || c == "." || c == ".." || c.size() > 255;
    for (char ch : c) {
        unsigned char u = static_cast<unsigned char>(ch);
        if (u < 0x20 || ch == '/' || ch == '\\' || ch == ':') bad = true;
    }
    if (bad)
        throw Error("The disc contains a file or folder with an unsafe name, so nothing was imported. " +
                    std::string(kRedownload));
    return c;
}

std::vector<std::string> splitStoredPath(const std::string& stored) {
    if (stored.empty() || stored[0] == '\\' || stored[0] == '/')
        throw Error("The disc contains a file with an unsafe name, so nothing was imported. " + std::string(kRedownload));
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty() && cur != ".") out.push_back(checkComponent(cur));
        cur.clear();
    };
    for (char ch : stored) {
        if (ch == '\\' || ch == '/') flush();
        else cur += ch;
    }
    flush();
    if (out.empty())
        throw Error("The disc contains a file with an empty name, so nothing was imported. " + std::string(kRedownload));
    return out;
}

// ---- FileSource / SubSource ------------------------------------------------------------------------------
FileSource::FileSource(const std::string& path) : path_(path) {
    f_.reset(std::fopen(path.c_str(), "rb"));
    if (!f_) throw Error("I cannot open \"" + path + "\". Check that the file is still there and can be read.");
    if (fseeko(f_.get(), 0, SEEK_END) != 0) throw Error("I cannot read \"" + path + "\".");
    off_t n = ftello(f_.get());
    if (n < 0) throw Error("I cannot read \"" + path + "\".");
    size_ = static_cast<uint64_t>(n);
}

void FileSource::read(uint64_t off, void* buf, size_t n) const {
    if (off > size_ || n > size_ - off)
        throw Error("The file \"" + path_ + "\" ends too early; it is damaged or incomplete. " + kRedownload);
    if (n == 0) return;
    if (fseeko(f_.get(), static_cast<off_t>(off), SEEK_SET) != 0 || std::fread(buf, 1, n, f_.get()) != n)
        throw Error("I cannot read \"" + path_ + "\" (read error). " + kRedownload);
}

SubSource::SubSource(const ByteSource& parent, uint64_t off, uint64_t len) : p_(parent), off_(off), len_(len) {
    if (off > parent.size() || len > parent.size() - off)
        throw Error("A file on the disc lies outside the disc image; the image is damaged. " + std::string(kRedownload));
}

void SubSource::read(uint64_t off, void* buf, size_t n) const {
    if (off > len_ || n > len_ - off) throw Error("A file on the disc ends too early; the image is damaged. " + std::string(kRedownload));
    p_.read(off_ + off, buf, n);
}

// ---- Reporter ----------------------------------------------------------------------------------------------
void Reporter::begin(const char* step, uint64_t total) {
    p_.step = step;
    p_.done = 0;
    p_.total = total;
    last_ = 0;
    call();
}

void Reporter::add(uint64_t n) {
    p_.done += n;
    if (p_.done - last_ >= (1u << 20)) call();
}

void Reporter::call() {
    last_ = p_.done;
    if (fn_ && !fn_(p_)) throw Cancelled();
}

// ---- IsoImage --------------------------------------------------------------------------------------------------
namespace {
constexpr size_t kRawSector = 2352;
constexpr size_t kSector = 2048;

bool isSync(const uint8_t* s) {
    if (s[0] != 0 || s[11] != 0) return false;
    for (int i = 1; i < 11; i++) if (s[i] != 0xff) return false;
    return true;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

enum class Layout { None, Plain, Raw };

Layout sniffFile(FILE* f, uint64_t fileSize) {
    uint8_t b[32];
    if (fileSize >= 17 * kSector + 8) {
        if (fseeko(f, 16 * off_t(kSector), SEEK_SET) == 0 && std::fread(b, 1, 8, f) == 8 && b[0] == 1 &&
            std::memcmp(b + 1, "CD001", 5) == 0)
            return Layout::Plain;
    }
    if (fileSize >= 17 * kRawSector) {
        if (fseeko(f, 16 * off_t(kRawSector), SEEK_SET) == 0 && std::fread(b, 1, 32, f) == 32 && isSync(b)) {
            const uint8_t* d = b[15] == 1 ? b + 16 : b[15] == 2 ? b + 24 : nullptr;
            if (d && d[0] == 1 && std::memcmp(d + 1, "CD001", 5) == 0) return Layout::Raw;
        }
    }
    return Layout::None;
}
} // namespace

bool IsoImage::sniff(const std::string& path) {
    std::unique_ptr<FILE, Closer> f(std::fopen(path.c_str(), "rb"));
    if (!f) return false;
    if (fseeko(f.get(), 0, SEEK_END) != 0) return false;
    off_t n = ftello(f.get());
    return n > 0 && sniffFile(f.get(), uint64_t(n)) != Layout::None;
}

IsoImage::IsoImage(const std::string& path) : path_(path) {
    f_.reset(std::fopen(path.c_str(), "rb"));
    if (!f_) throw Error("I cannot open \"" + path + "\". Check that the file is still there and can be read.");
    if (fseeko(f_.get(), 0, SEEK_END) != 0) throw Error("I cannot read \"" + path + "\".");
    off_t n = ftello(f_.get());
    fileSize_ = n < 0 ? 0 : uint64_t(n);
    Layout l = sniffFile(f_.get(), fileSize_);
    if (l == Layout::None)
        throw Error("\"" + path + "\" is not a CD image I can read (no ISO 9660 disc found in it).");
    raw_ = l == Layout::Raw;
    sectors_ = raw_ ? fileSize_ / kRawSector : fileSize_ / kSector;
    sec_.resize(kRawSector);

    uint8_t pvd[kSector];
    read(16 * uint64_t(kSector), pvd, kSector);
    if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0 || pvd[6] != 1)
        throw Error("\"" + path + "\" has no usable volume descriptor; the image is damaged. " + kRedownload);
    if (pvd[128] != 0 || pvd[129] != 8)   // logical block size 2048, little endian half
        throw Error("\"" + path + "\" uses a block size other than 2048 bytes, which is not supported.");
    const uint8_t* r = pvd + 156;
    root_.name = "";
    root_.dir = true;
    root_.extent = le32(r + 2);
    root_.size = le32(r + 10);
    if (r[0] < 34 || !(r[25] & 2) || root_.size == 0 || uint64_t(root_.extent) * kSector + root_.size > size())
        throw Error("The root folder of \"" + path + "\" is damaged. " + kRedownload);
}

void IsoImage::read(uint64_t off, void* buf, size_t n) const {
    if (off > size() || n > size() - off)
        throw Error("A file on the disc lies outside the disc image; the image is damaged. " + std::string(kRedownload));
    uint8_t* out = static_cast<uint8_t*>(buf);
    if (!raw_) {
        if (n == 0) return;
        if (fseeko(f_.get(), static_cast<off_t>(off), SEEK_SET) != 0 || std::fread(out, 1, n, f_.get()) != n)
            throw Error("I cannot read \"" + path_ + "\" (read error). " + kRedownload);
        return;
    }
    while (n > 0) {
        uint64_t sector = off / kSector;
        size_t inSec = size_t(off % kSector);
        size_t take = std::min(n, kSector - inSec);
        if (fseeko(f_.get(), static_cast<off_t>(sector * kRawSector), SEEK_SET) != 0 ||
            std::fread(sec_.data(), 1, kRawSector, f_.get()) != kRawSector)
            throw Error("I cannot read \"" + path_ + "\" (read error). " + kRedownload);
        const uint8_t mode = sec_[15];   // img2iso.py: mode 1 -> +16, mode 2 -> +24, anything else -> zeros
        if (mode == 1) std::memcpy(out, sec_.data() + 16 + inSec, take);
        else if (mode == 2) std::memcpy(out, sec_.data() + 24 + inSec, take);
        else std::memset(out, 0, take);
        out += take;
        off += take;
        n -= take;
    }
}

std::vector<IsoEntry> IsoImage::list(const IsoEntry& dir) const {
    constexpr uint32_t kMaxDir = 16u << 20;
    if (!dir.dir || dir.size == 0 || dir.size > kMaxDir || uint64_t(dir.extent) * kSector + dir.size > size())
        throw Error("A folder on the disc is damaged. " + std::string(kRedownload));
    std::vector<uint8_t> b(dir.size);
    read(uint64_t(dir.extent) * kSector, b.data(), b.size());
    std::vector<IsoEntry> out;
    size_t pos = 0;
    while (pos < b.size()) {
        const uint8_t len = b[pos];
        if (len == 0) {
            pos = (pos / kSector + 1) * kSector;
            continue;
        }
        if (len < 34 || pos + len > b.size() || (pos % kSector) + len > kSector)
            throw Error("A folder on the disc is damaged. " + std::string(kRedownload));
        const uint8_t* r = &b[pos];
        const size_t nl = r[32];
        if (33 + nl > len) throw Error("A folder on the disc is damaged. " + std::string(kRedownload));
        pos += len;
        if (nl == 1 && (r[33] == 0 || r[33] == 1)) continue;   // "." and ".."
        IsoEntry e;
        e.name.assign(reinterpret_cast<const char*>(r + 33), nl);
        size_t semi = e.name.find(';');
        if (semi != std::string::npos) e.name.resize(semi);
        while (!e.name.empty() && e.name.back() == '.') e.name.pop_back();
        checkComponent(e.name);
        e.dir = (r[25] & 2) != 0;
        e.extent = le32(r + 2);
        e.size = le32(r + 10);
        if (uint64_t(e.extent) * kSector + e.size > size())
            throw Error("\"" + e.name + "\" lies outside the disc image; the image is damaged or incomplete. " + kRedownload);
        bool dup = false;
        for (const IsoEntry& o : out) if (o.name == e.name) dup = true;
        if (!dup) out.push_back(std::move(e));
        if (out.size() > 100000) throw Error("A folder on the disc has far too many entries; the image is damaged.");
    }
    return out;
}

const IsoEntry* IsoImage::find(const std::vector<IsoEntry>& v, const char* name, bool dir) {
    for (const IsoEntry& e : v)
        if (e.dir == dir && iequals(e.name, name)) return &e;
    return nullptr;
}

void IsoImage::collect(const IsoEntry& dir, const std::string& rel, std::vector<TreeItem>& out) const {
    std::set<uint32_t> seen;
    collectRec(dir, rel, out, 0, seen);
}

void IsoImage::collectRec(const IsoEntry& dir, const std::string& rel, std::vector<TreeItem>& out, int depth,
                          std::set<uint32_t>& seen) const {
    if (depth > 16 || !seen.insert(dir.extent).second)
        throw Error("The folders on the disc loop back into themselves; the image is damaged. " + std::string(kRedownload));
    TreeItem d;
    d.rel = rel;
    d.dir = true;
    out.push_back(d);
    for (const IsoEntry& e : list(dir)) {
        std::string r = rel + "/" + e.name;
        if (e.dir) {
            collectRec(e, r, out, depth + 1, seen);
        } else {
            TreeItem t;
            t.rel = r;
            t.size = e.size;
            t.extent = e.extent;
            out.push_back(t);
        }
        if (out.size() > 200000) throw Error("The disc has far too many files; the image is damaged.");
    }
}

} // namespace biosys::install
