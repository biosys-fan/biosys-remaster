import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import glob, json, os, subprocess, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = [(_DEV + "/game-cd/files/BIOTIME"),
        (_DEV + "/setup-cd/installed/DATA1/Biotime")]
DUMP = os.path.join(ROOT, "build", "biosys-wavdump")
TMP = os.path.join(ROOT, "build", "test")
os.makedirs(TMP, exist_ok=True)

files = []
for d in DIRS:
    for dp, _, fn in os.walk(d):
        files += [os.path.join(dp, f) for f in fn if f.lower().endswith(".wav")]
files.sort()
ok = True
def fail(msg):
    global ok
    ok = False
    print("FAIL:", msg)

if not files:
    fail("no WAV files found")

# Our loader's view of every file (chunks of args to keep the command line short).
ours = {}
for i in range(0, len(files), 100):
    out = subprocess.run([DUMP, "info"] + files[i:i + 100], capture_output=True, text=True)
    for line in out.stdout.splitlines():
        path, _, rest = line.partition(" ")
        # paths contain no spaces in the game data; verify anyway
        if path not in files[i:i + 100]:
            path, _, rest = line.partition(" rate=")
            rest = "rate=" + rest
        ours[path] = rest

def ffprobe(path):
    j = json.loads(subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
         "stream=sample_rate,channels,duration_ts", "-of", "json", path],
        capture_output=True, text=True).stdout)
    s = j["streams"][0]
    return int(s["sample_rate"]), int(s["channels"]), int(s["duration_ts"])

n_wave = n_ff = 0
bits_seen = set()
for f in files:
    rest = ours.get(f)
    if rest is None or rest.startswith("ERROR"):
        fail("%s: our loader failed: %s" % (f, rest)); continue
    kv = dict(p.split("=") for p in rest.split())
    mine = (int(kv["rate"]), int(kv["channels"]), int(kv["frames"]))
    bits_seen.add(int(kv["bits"]))
    try:
        with wave.open(f, "rb") as w:
            ref = (w.getframerate(), w.getnchannels(), w.getnframes())
            if w.getsampwidth() * 8 != int(kv["bits"]):
                fail("%s: bits %d vs wave %d" % (f, int(kv["bits"]), w.getsampwidth() * 8))
        n_wave += 1
        src = "wave"
    except Exception as e:
        try:
            ref = ffprobe(f); src = "ffprobe"; n_ff += 1
            print("note: wave module cannot read %s (%s); compared with ffprobe" % (f, e))
        except Exception as e2:
            fail("%s: neither wave nor ffprobe could read it (%s)" % (f, e2)); continue
    if mine != ref:
        fail("%s: ours %s vs %s %s" % (f, mine, src, ref))

print("%s: %d files loaded, %d checked against wave, %d against ffprobe, bit depths %s"
      % ("PASS" if ok else "FAIL", len(files), n_wave, n_ff, sorted(bits_seen)))

# Render round trip: a real file through the mixer, read back with wave.
src = next((f for f in files if ours.get(f, "").find("channels=1") >= 0 and "rate=22050" in ours.get(f, "")), None)
if src:
    outp = os.path.join(TMP, "render.wav")
    r = subprocess.run([DUMP, "render", outp, src, "1", "0", "0"], capture_output=True, text=True)
    good = r.returncode == 0
    if good:
        with wave.open(outp, "rb") as w:
            frames = int(ours[src].split("frames=")[1].split()[0])
            good = (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (44100, 2, 2) \
                and 0 <= w.getnframes() - 2 * frames < 1024
    print("%s: render round trip (%s)" % ("PASS" if good else "FAIL", os.path.basename(src)))
    ok &= good

# Garbage input must be rejected cleanly, not crash.
bad = os.path.join(TMP, "bad.wav")
cases = {
    "not_riff": b"hello world, this is not a wave file",
    "truncated_header": b"RIFF\x04\x00\x00\x00WAVE",
    "non_pcm": b"RIFF\x24\x00\x00\x00WAVEfmt \x10\x00\x00\x00\x02\x00\x01\x00\x22\x56\x00\x00\x22\x56\x00\x00\x01\x00\x04\x00data\x00\x00\x00\x00",
    "no_data": b"RIFF\x1c\x00\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x22\x56\x00\x00\x22\x56\x00\x00\x01\x00\x08\x00",
}
for name, blob in cases.items():
    open(bad, "wb").write(blob)
    r = subprocess.run([DUMP, "info", bad], capture_output=True, text=True)
    good = r.returncode == 1 and "ERROR" in r.stdout
    print("%s: reject %s" % ("PASS" if good else "FAIL", name)); ok &= good
# data size running past EOF is clamped, odd-sized unknown chunk is padded
blob = (b"RIFF\x00\x00\x00\x00WAVEjunk\x03\x00\x00\x00abc\x00fmt \x10\x00\x00\x00\x01\x00\x01\x00\x22\x56\x00\x00"
        b"\x22\x56\x00\x00\x01\x00\x08\x00data\xff\xff\xff\x7f" + bytes([128, 255, 0, 128, 128]))
open(bad, "wb").write(blob)
r = subprocess.run([DUMP, "info", bad], capture_output=True, text=True)
good = "frames=5" in r.stdout and "peak=32768" in r.stdout
print("%s: clamp oversize data chunk, skip padded junk chunk (%s)" % ("PASS" if good else "FAIL", r.stdout.strip()[-60:]))
ok &= good
sys.exit(0 if ok else 1)
