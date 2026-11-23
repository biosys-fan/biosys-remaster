// First-run import of the player's own Biosys CDs (contract: importer.h).
#include "install/importer.h"
#include "install/internal.h"
#include <algorithm>
#include <cerrno>
#include <cstring>

namespace biosys::install {

const char* discName(DiscKind k) {
    switch (k) {
        case DiscKind::SetupCd: return "Setup CD";
        case DiscKind::GameCd: return "Game CD";
        default: return "unknown";
    }
}

const char* treeName(DiscKind k) {
    switch (k) {
        case DiscKind::SetupCd: return "installed";
        case DiscKind::GameCd: return "game-cd";
        default: return "";
    }
}

Status status(const std::string& dest) {
    Status s;
    std::error_code ec;
    s.setup = fs::exists(fs::path(dest) / treeName(DiscKind::SetupCd) / ".complete", ec);
    s.game = fs::exists(fs::path(dest) / treeName(DiscKind::GameCd) / ".complete", ec);
    return s;
}

std::vector<std::string> dataRoots(const std::string& dest) {
    return {dest + "/" + treeName(DiscKind::SetupCd), dest + "/" + treeName(DiscKind::GameCd)};
}

namespace {

// ---- folder helpers -----------------------------------------------------------------------------------------
struct Listing {
    std::vector<std::string> files, dirs;   // names only, sorted; symlinks and other special files are left out
};

Listing listFolder(const fs::path& dir) {
    Listing l;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e2;
        fs::file_status st = it->symlink_status(e2);
        if (e2) continue;
        if (fs::is_regular_file(st)) l.files.push_back(it->path().filename().string());
        else if (fs::is_directory(st)) l.dirs.push_back(it->path().filename().string());
    }
    std::sort(l.files.begin(), l.files.end());
    std::sort(l.dirs.begin(), l.dirs.end());
    return l;
}

const std::string* findName(const std::vector<std::string>& v, const char* name) {
    for (const std::string& s : v) if (iequals(s, name)) return &s;
    return nullptr;
}

bool skipJunk(const std::string& n) { return n == ".DS_Store" || n.compare(0, 2, "._") == 0; }

bool isBinary(const std::string& n) { return endsWithI(n, ".exe") || endsWithI(n, ".dll"); }

// ---- what is it ----------------------------------------------------------------------------------------------
enum class Form { None, Zip, Image, DiscFolder, InstalledFolder };

struct Resolved {
    Form form = Form::None;
    DiscKind kind = DiscKind::Unknown;
    std::string path;     // the zip, image, or folder to read
    std::string why;      // when kind is Unknown
};

DiscKind classifyImage(const IsoImage& iso) {
    std::vector<IsoEntry> root = iso.list(iso.root());
    if (IsoImage::find(root, "DATA1.CAB", false)) return DiscKind::SetupCd;
    if (IsoImage::find(root, "BIODATA", true) && IsoImage::find(root, "BIOTIME", true)) return DiscKind::GameCd;
    return DiscKind::Unknown;
}

std::string baseName(const std::string& n) {
    size_t p = n.find_last_of("/\\");
    return p == std::string::npos ? n : n.substr(p + 1);
}

Resolved resolveFile(const std::string& path) {
    Resolved r;
    r.path = path;
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    if (ec) {
        r.why = "I cannot read this file.";
        return r;
    }
    if (size == 0) {
        r.why = "This file is empty.";
        return r;
    }
    uint8_t head[4096];
    size_t got = 0;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        got = std::fread(head, 1, sizeof head, f);
        std::fclose(f);
    }
    if (got >= 4 && head[0] == 'P' && head[1] == 'K' && ((head[2] == 3 && head[3] == 4) || (head[2] == 5 && head[3] == 6))) {
        try {
            ZipArchive za(path);
            const ZipEntry* im = za.findImage();
            if (!im) {
                r.why = "This zip file does not contain a CD image, so it is not one of the two Biosys CDs.";
                return r;
            }
            r.form = Form::Zip;
            const std::string b = lowerAscii(baseName(im->name)), full = lowerAscii(im->name);
            if (b.find("setup") != std::string::npos) r.kind = DiscKind::SetupCd;
            else if (b.find("game") != std::string::npos) r.kind = DiscKind::GameCd;
            else if (full.find("setup") != std::string::npos) r.kind = DiscKind::SetupCd;
            else if (full.find("game") != std::string::npos) r.kind = DiscKind::GameCd;
            else r.why = "The CD image inside this zip file is not named like one of the two Biosys CDs (Bio_setup or Bio_game).";
        } catch (const Error& e) {
            r.form = Form::None;
            r.why = e.what();
        }
        return r;
    }
    if (IsoImage::sniff(path)) {
        try {
            IsoImage iso(path);
            r.kind = classifyImage(iso);
            r.form = Form::Image;
            if (r.kind == DiscKind::Unknown)
                r.why = "This is a CD image, but not one of the two Biosys CDs (it has neither DATA1.CAB nor the BIODATA and BIOTIME folders).";
        } catch (const Error& e) {
            r.why = e.what();
        }
        return r;
    }
    bool text = true;
    for (size_t i = 0; i < got; i++)
        if (head[i] == 0 || (head[i] < 9) || (head[i] > 13 && head[i] < 32)) text = false;
    if (text) r.why = "This is a text file, not one of the two Biosys CDs.";
    else r.why = "This file is not one of the two Biosys CDs: it is neither a zip file nor a CD image.";
    return r;
}

Resolved resolveFolder(const std::string& path) {
    Resolved r;
    r.path = path;
    Listing top = listFolder(path);
    if (top.files.empty() && top.dirs.empty()) {
        r.why = "This folder is empty.";
        return r;
    }
    if (findName(top.dirs, "Biodata") && findName(top.files, "NewGame.sav")) {
        r.form = Form::InstalledFolder;
        r.kind = DiscKind::SetupCd;
        return r;
    }
    if (const std::string* cab = findName(top.files, "DATA1.CAB")) {
        r.form = Form::DiscFolder;
        r.kind = DiscKind::SetupCd;
        (void)cab;
        return r;
    }
    if (findName(top.dirs, "BIODATA") && findName(top.dirs, "BIOTIME")) {
        r.form = Form::DiscFolder;
        r.kind = DiscKind::GameCd;
        return r;
    }
    // A downloaded disc that the browser already unpacked: a zip or an image at depth <= 2.
    std::vector<std::pair<std::string, int>> queue{{path, 0}};
    int inspected = 0;
    for (size_t qi = 0; qi < queue.size(); qi++) {
        Listing l = qi == 0 ? top : listFolder(queue[qi].first);
        for (const std::string& f : l.files) {
            if (!(endsWithI(f, ".zip") || endsWithI(f, ".img") || endsWithI(f, ".iso") || endsWithI(f, ".bin"))) continue;
            if (++inspected > 200) break;
            Resolved c = resolveFile((fs::path(queue[qi].first) / f).string());
            if (c.form != Form::None && c.kind != DiscKind::Unknown) return c;
        }
        if (queue[qi].second < 2)
            for (const std::string& d : l.dirs)
                if (queue.size() < 100) queue.push_back({(fs::path(queue[qi].first) / d).string(), queue[qi].second + 1});
    }
    r.why = "This folder does not contain one of the two Biosys CDs (no zip file, no .img or .iso image, and no unpacked disc).";
    return r;
}

Resolved resolve(const std::string& path) {
    Resolved r;
    r.path = path;
    std::error_code ec;
    fs::file_status st = fs::status(path, ec);
    if (ec || !fs::exists(st)) {
        r.why = "I cannot find that file or folder.";
        return r;
    }
    if (fs::is_directory(st)) return resolveFolder(path);
    if (fs::is_regular_file(st)) return resolveFile(path);
    r.why = "This is neither a file nor a folder.";
    return r;
}

// ---- writing -----------------------------------------------------------------------------------------------------
class FileOut {
public:
    explicit FileOut(const fs::path& p) : path_(p.string()) {
        f_ = std::fopen(path_.c_str(), "wbx");   // never overwrites: a name stored twice is an error
        if (!f_) {
            if (errno == EEXIST)
                throw Error("The disc stores the file \"" + p.filename().string() + "\" twice, so it is damaged. " + kRedownload);
            throw Error("I cannot write \"" + path_ + "\". Check the free space and the permissions of that folder.");
        }
    }
    ~FileOut() { if (f_) std::fclose(f_); }
    void write(const uint8_t* p, size_t n) {
        if (n && std::fwrite(p, 1, n, f_) != n)
            throw Error("I cannot write \"" + path_ + "\". The disk may be full.");
        written += n;
    }
    void close() {
        FILE* f = f_;
        f_ = nullptr;
        if (std::fclose(f) != 0) throw Error("I cannot write \"" + path_ + "\". The disk may be full.");
    }
    uint64_t written = 0;
private:
    FILE* f_ = nullptr;
    std::string path_;
};

using Sink = std::function<void(const uint8_t*, size_t)>;
using Produce = std::function<void(const TreeItem&, const Sink&)>;

void needSpace(const fs::path& where, uint64_t need) {
    std::error_code ec;
    fs::space_info si = fs::space(where, ec);
    if (ec) return;   // cannot tell: try anyway
    need += 16u << 20;
    if (si.available < need)
        throw Error("There is not enough free space. Importing needs about " + humanBytes(need) + " on the disk that holds \"" +
                    where.string() + "\", but only " + humanBytes(si.available) + " is free. Free up some space and try again.");
}

void writeTree(const std::vector<TreeItem>& items, const fs::path& root, Reporter& rep, const Produce& produce) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) throw Error("I cannot create the folder \"" + root.string() + "\".");
    for (const TreeItem& it : items) {
        fs::path p = root;
        size_t s = 0;
        while (s <= it.rel.size()) {
            size_t e = it.rel.find('/', s);
            if (e == std::string::npos) e = it.rel.size();
            if (e > s) p /= checkComponent(it.rel.substr(s, e - s));
            s = e + 1;
        }
        if (it.dir) {
            fs::create_directories(p, ec);
            if (ec) throw Error("I cannot create the folder \"" + p.string() + "\".");
            continue;
        }
        fs::create_directories(p.parent_path(), ec);
        if (ec) throw Error("I cannot create the folder \"" + p.parent_path().string() + "\".");
        FileOut out(p);
        produce(it, [&](const uint8_t* d, size_t n) {
            out.write(d, n);
            rep.add(n);
        });
        if (out.written != it.size) throw Error("The file \"" + it.rel + "\" has an unexpected size; the disc is damaged. " + std::string(kRedownload));
        out.close();
    }
}

void collectFolder(const fs::path& dir, const std::string& rel, std::vector<TreeItem>& out, int depth, bool skipBinaries,
                   const fs::path& skipPath) {
    if (depth > 32) throw Error("The folder \"" + dir.string() + "\" is nested too deeply.");
    if (!rel.empty()) {
        TreeItem d;
        d.rel = rel;
        d.dir = true;
        out.push_back(d);
    }
    Listing l = listFolder(dir);
    for (const std::string& f : l.files) {
        if (skipJunk(f) || (skipBinaries && isBinary(f))) continue;
        std::error_code ec;
        TreeItem t;
        t.rel = rel.empty() ? checkComponent(f) : rel + "/" + checkComponent(f);
        t.src = (dir / f).string();
        t.size = fs::file_size(dir / f, ec);
        if (ec) throw Error("I cannot read \"" + t.src + "\".");
        out.push_back(t);
        if (out.size() > 200000) throw Error("That folder has far too many files.");
    }
    for (const std::string& d : l.dirs) {
        if (skipJunk(d)) continue;
        std::error_code ec;
        if (!skipPath.empty() && fs::equivalent(dir / d, skipPath, ec)) continue;
        collectFolder(dir / d, rel.empty() ? checkComponent(d) : rel + "/" + checkComponent(d), out, depth + 1, skipBinaries,
                      skipPath);
    }
}

// Case-insensitive path lookup below root; returns empty path if absent.
fs::path findCI(const fs::path& root, const std::string& rel) {
    fs::path cur = root;
    size_t s = 0;
    while (s < rel.size()) {
        size_t e = rel.find('/', s);
        if (e == std::string::npos) e = rel.size();
        const std::string comp = rel.substr(s, e - s);
        s = e + 1;
        Listing l = listFolder(cur);
        const std::string* hit = findName(l.dirs, comp.c_str());
        if (!hit) hit = findName(l.files, comp.c_str());
        if (!hit) return fs::path();
        cur /= *hit;
    }
    return cur;
}

void verifyTree(const fs::path& root, DiscKind kind) {
    std::vector<std::string> need;   // trailing '/' = directory
    if (kind == DiscKind::SetupCd) need = {"NewGame.sav", "Biodata/PAGES.DAT", "Biodata/OBJECTS.DAT", "Biodata/BIOSYS4.DAT"};
    else need = {"BIODATA/PAGES.DAT", "BIODATA/TOUCHINF.DAT", "BIODATA/FONTS/JON.TTF", "BIOTIME/MOVIES/"};
    for (std::string n : need) {
        const bool dir = n.back() == '/';
        if (dir) n.pop_back();
        fs::path p = findCI(root, n);
        std::error_code ec;
        const bool ok = !p.empty() && (dir ? fs::is_directory(p, ec) : fs::is_regular_file(p, ec));
        if (!ok)
            throw Error(std::string("The ") + discName(kind) + " does not contain \"" + n + "\", which the game needs. " +
                        "Is this the right disc? If it is, the copy is damaged. " + std::string(kRedownload));
    }
}

struct Cleanup {
    fs::path tmp, partial;
    ~Cleanup() {
        std::error_code ec;
        fs::remove_all(tmp, ec);
        if (!partial.empty()) fs::remove_all(partial, ec);
    }
};

} // namespace

DiscKind identify(const std::string& path, std::string* why) {
    try {
        Resolved r = resolve(path);
        if (r.kind == DiscKind::Unknown && why) *why = r.why;
        return r.kind;
    } catch (const Error& e) {
        if (why) *why = e.what();
        return DiscKind::Unknown;
    }
}

DiscKind importDisc(const std::string& path, const std::string& dest, const ProgressFn& progress) {
    Resolved r = resolve(path);
    if (r.form == Form::None || (r.kind == DiscKind::Unknown && r.form != Form::Zip))
        throw Error(r.why.empty() ? "This is not one of the two Biosys CDs." : r.why);

    const fs::path d(dest);
    std::error_code ec;
    fs::create_directories(d, ec);
    if (ec || !fs::is_directory(d)) throw Error("I cannot create the folder \"" + dest + "\".");
    const fs::path tmp = d / ".tmp";
    Cleanup guard{tmp, fs::path()};
    fs::remove_all(tmp, ec);
    fs::remove_all(d / "installed.partial", ec);
    fs::remove_all(d / "game-cd.partial", ec);
    fs::create_directories(tmp, ec);
    if (ec) throw Error("I cannot create the folder \"" + tmp.string() + "\".");

    Reporter rep(progress, r.kind);
    std::unique_ptr<IsoImage> iso;
    std::unique_ptr<FileSource> cabFile;
    std::unique_ptr<SubSource> cabSub;
    std::unique_ptr<Cabinet> cab;
    std::vector<TreeItem> items;
    Produce produce;
    DiscKind kind = r.kind;
    std::string imagePath = r.path;

    if (r.form == Form::Zip) {
        ZipArchive za(r.path);
        const ZipEntry* e = za.findImage();
        if (!e) throw Error("This zip file does not contain a CD image.");
        if (e->usize > kMaxZipEntry)
            throw Error("The disc image inside \"" + r.path + "\" is larger than any Biosys CD (" + humanBytes(e->usize) +
                        "), so it is refused. " + kRedownload);
        const uint64_t tempBytes = e->usize / 2352 * 2048;
        const uint64_t treeGuess = kind == DiscKind::GameCd ? tempBytes : tempBytes + tempBytes / 3;
        needSpace(d, tempBytes + treeGuess);
        imagePath = (tmp / "image.iso").string();
        rep.setDisc(kind);
        za.unpackImage(*e, imagePath, rep);
    }

    if (r.form == Form::Zip || r.form == Form::Image) {
        iso = std::make_unique<IsoImage>(imagePath);
        kind = classifyImage(*iso);
        if (kind == DiscKind::Unknown)
            throw Error("This is a CD image, but not one of the two Biosys CDs (it has neither DATA1.CAB nor the BIODATA and BIOTIME folders).");
        std::vector<IsoEntry> root = iso->list(iso->root());
        if (kind == DiscKind::SetupCd) {
            const IsoEntry* c = IsoImage::find(root, "DATA1.CAB", false);
            cabSub = std::make_unique<SubSource>(*iso, uint64_t(c->extent) * 2048, c->size);
        } else {
            for (const char* n : {"BIODATA", "BIOTIME"}) {
                const IsoEntry* e = IsoImage::find(root, n, true);
                iso->collect(*e, e->name, items);
            }
            const IsoImage* im = iso.get();
            produce = [im](const TreeItem& it, const Sink& sink) {
                std::vector<uint8_t> buf(1 << 18);
                for (uint64_t pos = 0; pos < it.size;) {
                    const size_t n = size_t(std::min<uint64_t>(buf.size(), it.size - pos));
                    im->read(uint64_t(it.extent) * 2048 + pos, buf.data(), n);
                    sink(buf.data(), n);
                    pos += n;
                }
            };
        }
    } else if (r.form == Form::DiscFolder) {
        Listing top = listFolder(r.path);
        if (kind == DiscKind::SetupCd) {
            const std::string* c = findName(top.files, "DATA1.CAB");
            cabFile = std::make_unique<FileSource>((fs::path(r.path) / *c).string());
            cabSub = std::make_unique<SubSource>(*cabFile, 0, cabFile->size());
        } else {
            for (const char* n : {"BIODATA", "BIOTIME"})
                collectFolder(fs::path(r.path) / *findName(top.dirs, n), *findName(top.dirs, n), items, 0, false, d);
        }
    } else {   // installed copy
        collectFolder(r.path, "", items, 0, true, d);
    }

    if (cabSub) {
        cab = std::make_unique<Cabinet>(*cabSub);
        const Cabinet* cp = cab.get();
        for (size_t i = 0; i < cp->files().size(); i++) {
            const CabFile& f = cp->files()[i];
            if (isBinary(baseName(f.path))) continue;
            TreeItem t;
            for (const std::string& c : splitStoredPath(f.path)) t.rel += (t.rel.empty() ? "" : "/") + c;
            t.size = f.esize;
            t.cabIndex = int(i);
            items.push_back(t);
        }
        produce = [cp](const TreeItem& it, const Sink& sink) { cp->extract(cp->files()[size_t(it.cabIndex)], sink); };
    } else if (r.form == Form::DiscFolder || r.form == Form::InstalledFolder) {
        produce = [](const TreeItem& it, const Sink& sink) {
            std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(it.src.c_str(), "rb"), [](FILE* p) { return p ? std::fclose(p) : 0; });
            if (!f) throw Error("I cannot read \"" + it.src + "\".");
            std::vector<uint8_t> buf(1 << 18);
            uint64_t total = 0;
            for (;;) {
                size_t n = std::fread(buf.data(), 1, buf.size(), f.get());
                if (n == 0) break;
                total += n;
                if (total > it.size) throw Error("The file \"" + it.src + "\" changed while it was copied. Try again.");
                sink(buf.data(), n);
            }
            if (total != it.size) throw Error("I cannot read all of \"" + it.src + "\".");
        };
    }

    uint64_t total = 0;
    for (const TreeItem& t : items) if (!t.dir) total += t.size;
    if (total > kMaxDiscBytes)
        throw Error("The files on this disc add up to more than 2 GB, which is far more than a Biosys CD holds, so nothing was imported.");
    needSpace(d, total);

    rep.setDisc(kind);
    const fs::path partial = d / (std::string(treeName(kind)) + ".partial");
    guard.partial = partial;
    rep.begin("extract", total);
    writeTree(items, partial, rep, produce);
    verifyTree(partial, kind);
    {
        FileOut c(partial / ".complete");
        c.close();
    }
    rep.begin("finish", 1);

    const fs::path final_ = d / treeName(kind);
    const fs::path old = tmp / "old";
    bool moved = false;
    if (fs::exists(fs::symlink_status(final_, ec))) {
        fs::rename(final_, old, ec);
        if (ec) throw Error("I cannot replace the old copy in \"" + final_.string() + "\". Close anything that uses it and try again.");
        moved = true;
    }
    fs::rename(partial, final_, ec);
    if (ec) {
        std::error_code ec2;
        if (moved) fs::rename(old, final_, ec2);
        throw Error("I cannot move the imported files into \"" + final_.string() + "\".");
    }
    return kind;
}

} // namespace biosys::install
