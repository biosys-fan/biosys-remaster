// Internal helpers of the first-run import (src/install). Not part of the contract (importer.h).
#pragma once
#include "install/importer.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <vector>

namespace biosys::install {

namespace fs = std::filesystem;

constexpr uint64_t kMaxDiscBytes = 2ull << 30;      // one disc never writes more than this
constexpr uint64_t kMaxZipEntry = 1536ull << 20;    // a zip entry larger than this is refused
constexpr const char* kRedownload = "Download it again from the same page.";

std::string humanBytes(uint64_t n);

// ---- names -------------------------------------------------------------------------------------------
// One stored name component: throws Error when it could leave the destination (empty, ".", "..", separators, NUL,
// drive colon, control characters).
const std::string& checkComponent(const std::string& c);
// "a\b\c" (or with '/') -> components; leading separator or a drive letter is refused; empty and "." parts dropped.
std::vector<std::string> splitStoredPath(const std::string& stored);
bool endsWithI(const std::string& s, const char* suffix);
std::string lowerAscii(std::string s);

// ---- random access over bytes ----------------------------------------------------------------------------
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t size() const = 0;
    // Throws Error when [off, off+n) is outside the source or cannot be read.
    virtual void read(uint64_t off, void* buf, size_t n) const = 0;
};

class FileSource : public ByteSource {
public:
    explicit FileSource(const std::string& path);   // throws Error if it cannot be opened
    uint64_t size() const override { return size_; }
    void read(uint64_t off, void* buf, size_t n) const override;
    const std::string& path() const { return path_; }
private:
    struct Closer { void operator()(FILE* f) const { if (f) std::fclose(f); } };
    std::unique_ptr<FILE, Closer> f_;
    std::string path_;
    uint64_t size_ = 0;
};

class SubSource : public ByteSource {
public:
    SubSource(const ByteSource& parent, uint64_t off, uint64_t len);
    uint64_t size() const override { return len_; }
    void read(uint64_t off, void* buf, size_t n) const override;
private:
    const ByteSource& p_;
    uint64_t off_, len_;
};

// ---- progress ----------------------------------------------------------------------------------------------
class Reporter {
public:
    Reporter(const ProgressFn& fn, DiscKind d) : fn_(fn) { p_.disc = d; }
    void setDisc(DiscKind d) { p_.disc = d; }
    void begin(const char* step, uint64_t total);   // calls the callback at once
    void add(uint64_t n);                           // calls the callback at least every 1 MB
    void setTotal(uint64_t total) { p_.total = total; }
private:
    void call();
    const ProgressFn& fn_;
    Progress p_;
    uint64_t last_ = 0;
};

// ---- ISO 9660 image (primary volume descriptor only) ---------------------------------------------------
struct IsoEntry {
    std::string name;
    bool dir = false;
    uint32_t extent = 0;
    uint32_t size = 0;
};

// One file or directory to be written below a tree root.
struct TreeItem {
    std::string rel;        // '/'-separated, every component checked
    bool dir = false;
    uint64_t size = 0;
    uint32_t extent = 0;    // ISO extent
    std::string src;        // source path (folder copy)
    int cabIndex = -1;      // cabinet file index
};

class IsoImage : public ByteSource {
public:
    explicit IsoImage(const std::string& path);   // throws Error when it is not a CD image
    static bool sniff(const std::string& path);   // cheap: does it look like a CD image (2048 or 2352 byte sectors)?
    uint64_t size() const override { return sectors_ * 2048; }
    void read(uint64_t off, void* buf, size_t n) const override;
    const IsoEntry& root() const { return root_; }
    std::vector<IsoEntry> list(const IsoEntry& dir) const;
    // Appends the directory itself (as rel) and everything below it. Loops and excessive depth are errors.
    void collect(const IsoEntry& dir, const std::string& rel, std::vector<TreeItem>& out) const;
    // Case-insensitive lookup in a listing; nullptr if absent.
    static const IsoEntry* find(const std::vector<IsoEntry>& v, const char* name, bool dir);
    const std::string& path() const { return path_; }
private:
    struct Closer { void operator()(FILE* f) const { if (f) std::fclose(f); } };
    void collectRec(const IsoEntry& dir, const std::string& rel, std::vector<TreeItem>& out, int depth,
                    std::set<uint32_t>& seen) const;
    std::unique_ptr<FILE, Closer> f_;
    std::string path_;
    bool raw_ = false;
    uint64_t fileSize_ = 0, sectors_ = 0;
    IsoEntry root_;
    mutable std::vector<uint8_t> sec_;
};

// ---- zip ------------------------------------------------------------------------------------------------------
struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint64_t csize = 0, usize = 0, dataOff = 0;
    bool isDir = false;
};

class ZipArchive {
public:
    explicit ZipArchive(const std::string& path);   // throws Error: damaged, truncated, zip64, multi-disk
    const std::vector<ZipEntry>& entries() const { return entries_; }
    // The CD image: the biggest entry named *.img, *.iso or *.bin; nullptr if there is none.
    const ZipEntry* findImage() const;
    // Inflates the entry to outPath, converting raw 2352-byte sectors to 2048-byte sectors on the fly, and checks the
    // zip CRC of the raw data. Progress step "unpack".
    void unpackImage(const ZipEntry& e, const std::string& outPath, Reporter& rep) const;
    const std::string& path() const { return path_; }
private:
    FileSource src_;
    std::string path_;
    std::vector<ZipEntry> entries_;
};

// ---- InstallShield 5 cabinet ----------------------------------------------------------------------------
struct CabFile {
    std::string path;       // as stored, backslashes
    uint16_t flags = 0;
    uint32_t esize = 0, csize = 0;
    uint64_t dataOff = 0;
};

class Cabinet {
public:
    explicit Cabinet(const ByteSource& src);       // throws Error
    const std::vector<CabFile>& files() const { return files_; }   // entries with flag 8 are left out
    void extract(const CabFile& f, const std::function<void(const uint8_t*, size_t)>& sink) const;
private:
    const ByteSource& src_;
    std::vector<CabFile> files_;
};

} // namespace biosys::install
