"""First-run import of the two original CDs (src/install, build/biosys-import)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import hashlib, os, shutil, struct, subprocess, sys, tempfile, time, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "build", "biosys-import")
B = _DEV
SETUP_ZIP = B + "/setup-cd/Biosys_Win_EN_Setup-CD.zip"
GAME_ZIP = B + "/game-cd/Biosys_Win_EN_Game-CD.zip"
SETUP_DL = B + "/setup-cd/extracted"                    # Safari unpacked: contains "Biosys Setup CD/Bio_setup.img"
GAME_DL = B + "/game-cd/extracted"
SETUP_IMG = B + "/setup-cd/extracted/Biosys Setup CD/Bio_setup.img"
GAME_IMG = B + "/game-cd/extracted/Biosys Game CD/Bio_game.img"
SETUP_ISO = B + "/setup-cd/Bio_setup.iso"
GAME_ISO = B + "/game-cd/Bio_game.iso"
SETUP_FILES = B + "/setup-cd/files"
GAME_FILES = B + "/game-cd/files"
INSTALLED = B + "/setup-cd/installed/DATA1"

passed = failed = 0
T0 = time.time()
tmp = tempfile.mkdtemp(prefix="biosys-import-test-")


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("ok   " + name)
    else:
        failed += 1
        print("FAIL " + name + (": " + detail if detail else ""))


def run(*args):
    t = time.time()
    p = subprocess.run([TOOL] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr, time.time() - t


def identify(path):
    rc, out, err, _ = run("identify", path)
    line = out.strip()
    prefix = path + ": "
    return line[len(prefix):] if line.startswith(prefix) else "?? " + line


def hashtree(root, skip_binaries=False):
    """{relative path: md5} of all files, plus the set of directories."""
    files, dirs = {}, set()
    for dp, dn, fn in os.walk(root):
        rel = os.path.relpath(dp, root)
        if rel != ".":
            dirs.add(rel)
        for f in fn:
            if skip_binaries and f.lower().endswith((".exe", ".dll")):
                continue
            h = hashlib.md5()
            with open(os.path.join(dp, f), "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
            files[os.path.normpath(os.path.join(rel, f))] = h.hexdigest()
    return files, dirs


def leftovers(dest):
    if not os.path.isdir(dest):
        return []
    return [n for n in os.listdir(dest) if n == ".tmp" or n.endswith(".partial")]


EXPECT_SETUP = None
EXPECT_GAME = None


def expect(kind):
    global EXPECT_SETUP, EXPECT_GAME
    if kind == "setup":
        if EXPECT_SETUP is None:
            EXPECT_SETUP = hashtree(INSTALLED, skip_binaries=True)
        return EXPECT_SETUP
    if EXPECT_GAME is None:
        EXPECT_GAME = hashtree(GAME_FILES)
    return EXPECT_GAME


def import_and_compare(label, kind, src, dest_name):
    dest = os.path.join(tmp, dest_name)
    t = time.time()
    rc, out, err, _ = run("import", dest, src)
    secs = time.time() - t
    check(label + ": exit 0", rc == 0, err.strip()[-300:])
    tree = os.path.join(dest, "installed" if kind == "setup" else "game-cd")
    check(label + ": .complete present", os.path.isfile(os.path.join(tree, ".complete")))
    check(label + ": no .tmp / .partial left", leftovers(dest) == [], str(leftovers(dest)))
    files, dirs = hashtree(tree)
    files.pop(".complete", None)
    ef, ed = expect(kind)
    check(label + ": same file set", set(files) == set(ef),
          "missing %s extra %s" % (sorted(set(ef) - set(files))[:3], sorted(set(files) - set(ef))[:3]))
    bad = [k for k in ef if files.get(k) != ef[k]]
    check(label + ": every file identical (%d files)" % len(ef), not bad, str(bad[:3]))
    if kind == "game":
        check(label + ": same folders", dirs == ed, str(sorted(dirs ^ ed)[:3]))
    st = run("status", dest)[1]
    check(label + ": status says " + kind + " complete", ("%s: yes" % kind) in st, st)
    print("     (%.1f s)" % secs)
    return dest


# ---------------------------------------------------------------------------------------------------------------------
# crafted ISO images for the hostile-input tests
def dirrec(name, extent, size, isdir):
    nb = name if isinstance(name, bytes) else name.encode("latin1")
    n = 33 + len(nb)
    n += n & 1
    r = bytearray(n)
    r[0] = n
    struct.pack_into("<I", r, 2, extent)
    struct.pack_into(">I", r, 6, extent)
    struct.pack_into("<I", r, 10, size)
    struct.pack_into(">I", r, 14, size)
    r[25] = 2 if isdir else 0
    r[32] = len(nb)
    r[33:33 + len(nb)] = nb
    return bytes(r)


def build_iso(layout, total_sectors=40):
    """layout: {sector: bytes}. Sector 16 is the PVD, root directory at 18."""
    img = bytearray(total_sectors * 2048)
    pvd = bytearray(2048)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    struct.pack_into("<I", pvd, 80, total_sectors)
    struct.pack_into("<H", pvd, 128, 2048)
    pvd[156:156 + 34] = dirrec(b"\x00", 18, 2048, True)[:34].ljust(34, b"\0")
    pvd[156] = 34
    img[16 * 2048:17 * 2048] = pvd
    for s, data in layout.items():
        img[s * 2048:s * 2048 + len(data)] = data
    return bytes(img)


def dirsector(extent, parent, entries):
    out = dirrec(b"\x00", extent, 2048, True) + dirrec(b"\x01", parent, 2048, True)
    for e in entries:
        out += dirrec(*e)
    return out


def game_iso(biodata_entries, extra=None, files=True):
    L = {
        18: dirsector(18, 18, [("BIODATA", 19, 2048, True), ("BIOTIME", 20, 2048, True)]),
        19: dirsector(19, 18, biodata_entries),
        20: dirsector(20, 18, [("MOVIES", 22, 2048, True)]),
        21: dirsector(21, 19, [("JON.TTF;1", 25, 6, False)]),
        22: dirsector(22, 20, []),
        23: b"pages!",
        24: b"touch!",
        25: b"jonttf",
        26: b"readme",
    }
    if extra:
        L.update(extra)
    return build_iso(L)


GOOD_BIODATA = [("PAGES.DAT;1", 23, 6, False), ("TOUCHINF.DAT;1", 24, 6, False), ("FONTS", 21, 2048, True),
                ("NOEXT.;1", 26, 6, False)]


def to_raw(iso):
    out = bytearray()
    for i in range(len(iso) // 2048):
        out += b"\x00" + b"\xff" * 10 + b"\x00" + bytes([0, 2, i % 75]) + b"\x01" + iso[i * 2048:(i + 1) * 2048] + b"\0" * 288
    return bytes(out)


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)
    return path


try:
    if not os.path.exists(TOOL):
        print("FAIL: %s is not built (run make first)" % TOOL)
        sys.exit(1)

    # ---- identify ----------------------------------------------------------------------------------------------------
    print("== identify")
    for label, p, k in [("setup zip", SETUP_ZIP, "Setup CD"), ("game zip", GAME_ZIP, "Game CD"),
                        ("setup download folder", SETUP_DL, "Setup CD"), ("game download folder", GAME_DL, "Game CD"),
                        ("setup iso", SETUP_ISO, "Setup CD"), ("game iso", GAME_ISO, "Game CD"),
                        ("setup raw img", SETUP_IMG, "Setup CD"), ("game raw img", GAME_IMG, "Game CD"),
                        ("setup unpacked disc", SETUP_FILES, "Setup CD"), ("game unpacked disc", GAME_FILES, "Game CD"),
                        ("installed copy", INSTALLED, "Setup CD")]:
        got = identify(p)
        check("identify " + label, got == k, got)

    neg = os.path.join(tmp, "neg")
    os.makedirs(neg + "/emptydir")
    write(neg + "/note.txt", b"hello, this is only text\n")
    write(neg + "/truncated.zip", open(SETUP_ZIP, "rb").read(3000000))
    write(neg + "/empty.bin", b"")
    with zipfile.ZipFile(neg + "/other.zip", "w") as z:
        z.writestr("readme.txt", "not a game")
    with zipfile.ZipFile(neg + "/noname.zip", "w") as z:
        z.writestr("disc/photo.img", b"x" * 100)
    with zipfile.ZipFile(neg + "/wrongzip.zip", "w") as z:
        z.writestr("Bio_other.img", build_iso({18: dirsector(18, 18, [("FOO.TXT;1", 19, 3, False)])}))
    write(neg + "/otheriso.iso", build_iso({18: dirsector(18, 18, [("FOO.TXT;1", 19, 3, False)])}))
    for name in ["note.txt", "emptydir", "truncated.zip", "other.zip", "noname.zip", "otheriso.iso", "empty.bin"]:
        got = identify(os.path.join(neg, name))
        check("identify negative " + name, got.startswith("unknown - ") and len(got) > 20, got)
    check("identify text file says text", "text file" in identify(neg + "/note.txt"))
    check("identify truncated zip says download again", "Download it again" in identify(neg + "/truncated.zip"))
    check("identify missing path", identify(neg + "/nothing").startswith("unknown - "))
    # a zip is judged by the name of its image; importing it must still refuse by the disc's own directory
    rc, out, err, _ = run("import", os.path.join(tmp, "d_wrong"), neg + "/wrongzip.zip")
    check("import of a zip with a foreign image is refused", rc == 1 and "not one of the two" in err, err)
    check("  ... leaves nothing", leftovers(os.path.join(tmp, "d_wrong")) == [])

    # ---- full imports of both downloads ------------------------------------------------------------------------------
    print("== import from the zips")
    dest_zip = import_and_compare("setup zip", "setup", SETUP_ZIP, "dest_zip")
    # a second disc into the same destination: both trees complete
    rc, out, err, secs = run("import", dest_zip, GAME_ZIP)
    check("game zip into the same dest: exit 0", rc == 0, err[-300:])
    check("both discs complete", "complete: yes" in run("status", dest_zip)[1])
    check("no leftovers after second disc", leftovers(dest_zip) == [])
    ef, ed = expect("game")
    gf, gd = hashtree(os.path.join(dest_zip, "game-cd"))
    gf.pop(".complete", None)
    check("game zip: every file identical (%d files)" % len(ef), gf == ef)
    check("game zip: same folders", gd == ed)
    print("     (%.1f s)" % secs)
    check("progress lines on stderr", "Game CD: unpack" in err and "extract" in err and "%" in err, err[:200])
    check("progress at most every 5 percent", len([l for l in err.splitlines() if "extract" in l]) <= 22)

    print("== other input forms")
    import_and_compare("setup iso", "setup", SETUP_ISO, "d_siso")
    import_and_compare("setup raw .img", "setup", SETUP_IMG, "d_simg")
    import_and_compare("game raw img in the Safari folder", "game", GAME_DL, "d_gdl")
    import_and_compare("game iso", "game", GAME_ISO, "d_giso")
    import_and_compare("setup unpacked disc folder", "setup", SETUP_FILES, "d_sfiles")
    import_and_compare("game unpacked disc folder", "game", GAME_FILES, "d_gfiles")
    import_and_compare("installed copy", "setup", INSTALLED, "d_inst")
    import_and_compare("setup Safari folder", "setup", SETUP_DL, "d_sdl")

    # ---- cancel ------------------------------------------------------------------------------------------------------
    print("== cancel")
    for label, src, n in [("game zip during unpack", GAME_ZIP, 5000000), ("game iso during extract", GAME_ISO, 100000000),
                          ("setup zip during extract", SETUP_ZIP, 400000000)]:
        dest = os.path.join(tmp, "d_cancel_" + label.split()[1] + label.split()[-1])
        rc, out, err, secs = run("import", "--cancel-after-bytes", str(n), dest, src)
        left = sorted(os.listdir(dest)) if os.path.isdir(dest) else []
        check("cancel %s: exit 1 and says cancelled" % label, rc == 1 and "cancelled" in err, err[-200:])
        check("cancel %s: nothing left (%s)" % (label, left), left == [])
        check("cancel %s: stopped quickly (%.1f s)" % (label, secs), secs < 4)
        check("cancel %s: status incomplete" % label, "complete: no" in run("status", dest)[1])
    # a cancelled re-import keeps the complete old tree
    before = hashtree(os.path.join(tmp, "d_siso", "installed"))
    rc, out, err, _ = run("import", "--cancel-after-bytes", "100000000", os.path.join(tmp, "d_siso"), SETUP_ISO)
    after = hashtree(os.path.join(tmp, "d_siso", "installed"))
    check("cancelled re-import keeps the old tree", rc == 1 and before == after and leftovers(os.path.join(tmp, "d_siso")) == [])

    # ---- damaged zip -------------------------------------------------------------------------------------------------
    print("== damaged zip")
    bad = os.path.join(tmp, "bad", "Setup-damaged.zip")
    os.makedirs(os.path.dirname(bad))
    shutil.copyfile(SETUP_ZIP, bad)
    with zipfile.ZipFile(bad) as z:
        zi = [i for i in z.infolist() if i.filename.endswith(".img")][0]
        with open(bad, "rb") as f:
            f.seek(zi.header_offset)
            lh = f.read(30)
        start = zi.header_offset + 30 + struct.unpack_from("<H", lh, 26)[0] + struct.unpack_from("<H", lh, 28)[0]
    with open(bad, "r+b") as f:
        f.seek(start + 5000000)
        c = f.read(1)
        f.seek(start + 5000000)
        f.write(bytes([c[0] ^ 0x5A]))
    dest = os.path.join(tmp, "d_bad")
    rc, out, err, _ = run("import", dest, bad)
    check("damaged zip: exit 1", rc == 1, err)
    check("damaged zip: message says damaged and names the file",
          "damaged" in err and "Setup-damaged.zip" in err and "Download it again" in err, err)
    check("damaged zip: nothing left", leftovers(dest) == [] and "complete: no" in run("status", dest)[1] and
          not os.path.exists(dest + "/installed"))
    os.remove(bad)

    # ---- hostile / small images -----------------------------------------------------------------------------------------
    print("== crafted images")
    cd = os.path.join(tmp, "crafted")
    os.makedirs(cd)
    good = game_iso(GOOD_BIODATA)
    gp = write(cd + "/good.iso", good)
    dest = os.path.join(tmp, "d_good")
    rc, out, err, _ = run("import", dest, gp)
    check("crafted game iso imports", rc == 0, err)
    names = sorted(os.path.relpath(os.path.join(dp, f), dest + "/game-cd") for dp, _, fn in os.walk(dest + "/game-cd") for f in fn)
    check("names: no ';1', no trailing dot",
          names == [".complete", "BIODATA/FONTS/JON.TTF", "BIODATA/NOEXT", "BIODATA/PAGES.DAT", "BIODATA/TOUCHINF.DAT"], str(names))
    check("content of crafted files", open(dest + "/game-cd/BIODATA/PAGES.DAT", "rb").read() == b"pages!")
    rawp = write(cd + "/raw.img", to_raw(good))
    dest = os.path.join(tmp, "d_raw")
    rc, out, err, _ = run("import", dest, rawp)
    check("crafted raw 2352 image imports", rc == 0 and open(dest + "/game-cd/BIODATA/FONTS/JON.TTF", "rb").read() == b"jonttf", err)
    for method, m in [("deflated", zipfile.ZIP_DEFLATED), ("stored", zipfile.ZIP_STORED)]:
        zp = cd + "/Bio_game_%s.zip" % method
        with zipfile.ZipFile(zp, "w", m) as z:
            z.writestr("Biosys Game CD/Bio_game.img", to_raw(good))
        dest = os.path.join(tmp, "d_zip_" + method)
        rc, out, err, _ = run("import", dest, zp)
        check("crafted raw image in %s zip imports" % method,
              rc == 0 and open(dest + "/game-cd/BIODATA/PAGES.DAT", "rb").read() == b"pages!" and leftovers(dest) == [], err)
    # replacing an existing tree
    rc, out, err, _ = run("import", dest, gp)
    check("existing tree is replaced", rc == 0 and leftovers(dest) == [])

    hostile = [
        ("name ..", game_iso(GOOD_BIODATA + [("..", 26, 6, False)]), "unsafe"),
        ("name with backslash", game_iso(GOOD_BIODATA + [("EVIL\\X;1", 26, 6, False)]), "unsafe"),
        ("name with slash", game_iso(GOOD_BIODATA + [("A/B;1", 26, 6, False)]), "unsafe"),
        ("directory loop", game_iso(GOOD_BIODATA + [("LOOP", 19, 2048, True)]), "loop"),
        ("file beyond the image", game_iso(GOOD_BIODATA + [("BIG.DAT;1", 30, 900000, False)]), "outside"),
        ("directory beyond the image", game_iso(GOOD_BIODATA + [("SUB", 39, 4096, True)]), "outside"),
        ("missing required file", game_iso([("PAGES.DAT;1", 23, 6, False), ("FONTS", 21, 2048, True)]), "TOUCHINF.DAT"),
    ]
    for label, img, word in hostile:
        p = write(cd + "/h.iso", img)
        dest = os.path.join(tmp, "d_h")
        shutil.rmtree(dest, ignore_errors=True)
        rc, out, err, _ = run("import", dest, p)
        check("hostile %s refused (%s)" % (label, word), rc == 1 and word in err, err)
        check("hostile %s: nothing left" % label, not os.path.exists(dest + "/game-cd") and leftovers(dest) == [] and
              not os.path.exists(os.path.join(dest, "BIODATA")))
    # the escape attempt must not have written anywhere above the destination
    check("nothing written next to dest", not os.path.exists(os.path.join(tmp, "X")) and not os.path.exists(tmp + "/d_h/../BIODATA"))

finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("")
print("%d passed, %d failed, %.0f s" % (passed, failed, time.time() - T0))
sys.exit(1 if failed else 0)
