"""Compare biosys-avidump against ffmpeg for every movie: frame count, per-frame RGB24 CRC, audio bytes."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, subprocess, sys, zlib
from concurrent.futures import ThreadPoolExecutor

ROOT = os.getcwd()
TOOL = os.path.join(ROOT, "build", "biosys-avidump")
MOVIES = (_DEV + "/game-cd/files/BIOTIME/MOVIES")
TMP = os.path.join(ROOT, "build", "test")

# Files whose frame CRCs legitimately differ from ffmpeg: path (relative to MOVIES) -> reason.
# All 17 have biClrUsed = 257 in the BITMAPINFOHEADER and a 257-entry colour table whose first entry
# is a stray white (ffffff) followed by the real-looking palette. We use the first 256 entries (the
# DIB convention: at most 256 colours, table starts at entry 0). ffmpeg's AVI demuxer instead takes
# the LAST 256 bytes*4 of the colour table, i.e. entries 1..256. For these files the CRC comparison
# is replaced by a stricter structural one: sampled frames (every 10th) must equal ffmpeg's RGB
# when our palette indices are looked up in table[idx + 1] (see shifted_check). Frame count and
# audio are still compared exactly.
SHIFT257 = """ANGEL/FISHTRIP.AVI RFOREST/FIREFLY.AVI RFOREST/R1_MIST.AVI RFOREST/FLY.AVI RFOREST/R5_MIST.AVI
RFOREST/R1_LEAK.AVI RFOREST/R2WATER.AVI RFOREST/R9_MIST.AVI UNDERSEA/REDFISH.AVI UNDERSEA/BIGFISHC.AVI
UNDERSEA/BIGFISHB.AVI UNDERSEA/BIGFISHD.AVI HOUSE/TAP_PH.AVI HOUSE/FPH1VID/NOISE.AVI CLUBEDEN/CE3FONT.AVI
PEYOTE/PEYOTE.AVI DEADBIOM/FLIES.AVI""".split()
ALLOW = {r: "257-entry colour table (ffmpeg uses entries 1..256, we use 0..255)" for r in SHIFT257}

def colour_table(path):
    d = open(path, "rb").read(0x4000)
    i = d.index(b"strf")
    size = int.from_bytes(d[i + 4:i + 8], "little")
    n = int.from_bytes(d[i + 8 + 32:i + 8 + 36], "little")
    t = d[i + 8 + 40:i + 8 + 40 + size - 40]
    assert n == 257 and len(t) == 1028, "unexpected colour table"
    return [(t[k * 4 + 2], t[k * 4 + 1], t[k * 4]) for k in range(257)]

def bmp_rgb_topdown(bmp, w, h, table, shift):
    off = int.from_bytes(bmp[10:14], "little")
    px = bmp[off:]
    out = bytearray()
    for y in range(h - 1, -1, -1):
        for v in px[y * w:(y + 1) * w]:
            out += bytes(table[v + shift])
    return bytes(out)

def shifted_check(path, w, h, ref_frames, errors):
    table = colour_table(path)
    bmp = os.path.join(TMP, "shift." + os.path.basename(path) + ".bmp")
    for i, ref in ref_frames.items():
        run([TOOL, "export", path, str(i), bmp])
        if bmp_rgb_topdown(open(bmp, "rb").read(), w, h, table, 1) != ref:
            errors.append("frame %d differs from ffmpeg even with palette shifted by one" % i)
            break
    if os.path.exists(bmp): os.remove(bmp)

def run(args):
    return subprocess.run(args, check=True, capture_output=True).stdout

def check(path):
    rel = os.path.relpath(path, MOVIES)
    out = {"rel": rel, "frames": 0, "errors": []}
    info = run([TOOL, "info", path]).decode().split()
    wh = info[0].split("x"); w, h = int(wh[0]), int(wh[1])
    size = w * h * 3
    mine = [l.split("crc=")[1] for l in run([TOOL, "frames", path]).decode().splitlines()]
    theirs = []
    sampled = {}
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", path, "-an", "-fps_mode", "passthrough",
                          "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    while True:
        b = p.stdout.read(size)
        if len(b) < size:
            if b: out["errors"].append("partial ffmpeg frame")
            break
        if rel in ALLOW and len(theirs) % 10 == 0: sampled[len(theirs)] = b
        theirs.append("%08x" % (zlib.crc32(b) & 0xffffffff))
    err = p.stderr.read().decode().strip(); p.wait()
    if p.returncode or err: out["errors"].append("ffmpeg video: " + err[:200])
    out["frames"] = len(mine)
    if len(mine) != len(theirs):
        out["errors"].append("frame count %d != ffmpeg %d" % (len(mine), len(theirs)))
    bad = [i for i, (a, b) in enumerate(zip(mine, theirs)) if a != b]
    if rel in ALLOW:
        if not bad: out["errors"].append("allow-listed file now matches; remove from ALLOW")
        shifted_check(path, w, h, sampled, out["errors"])
    elif bad:
        out["errors"].append("%d frame CRC mismatches, first at %d" % (len(bad), bad[0]))
    has_audio = bool(run(["ffprobe", "-v", "error", "-select_streams", "a", "-show_entries", "stream=index",
                          "-of", "csv=p=0", path]).strip())
    if has_audio != ("audio=none" not in " ".join(info)):
        out["errors"].append("audio presence differs from ffprobe")
    elif has_audio:
        raw = os.path.join(TMP, rel.replace("/", "_") + ".raw")
        run([TOOL, "audio", path, raw])
        ref = run(["ffmpeg", "-v", "error", "-i", path, "-vn", "-f", "u8", "-acodec", "pcm_u8", "-"])
        with open(raw, "rb") as f: mine_a = f.read()
        os.remove(raw)
        if mine_a != ref:
            out["errors"].append("audio differs (%d vs ffmpeg %d bytes)" % (len(mine_a), len(ref)))
    return out

def main():
    os.makedirs(TMP, exist_ok=True)
    files = sorted(os.path.join(d, f) for d, _, fs in os.walk(MOVIES) for f in fs if f.lower().endswith(".avi"))
    with ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(check, files))
    frames = sum(r["frames"] for r in results)
    mism = 0
    print("allow-listed (verified with shifted palette): %d" % len(ALLOW))
    for r in results:
        if not r["errors"]: continue
        mism += 1
        print("MISMATCH %s: %s" % (r["rel"], "; ".join(r["errors"])))
    print("files %d" % len(files)); print("frames compared %d" % frames); print("mismatches %d" % mism)
    if len(files) != 97: print("expected 97 files"); mism += 1
    return 1 if mism else 0

sys.exit(main())
