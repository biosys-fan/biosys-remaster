"""The rain overlay. The start page of a new game is raining, the rain FX has begun the page filter (colours 0x76 / 0x56 / 0x57).
Every frame is compared with the same frame of the page without the filter: the only pixels that may differ are the ones the
decompiled text of dll:10022530 draws for the drops the port reports (`game rain`: position and speed of each drop before the
frame). The expectation below is written from the decompile, not from the port's code: a thin drop is five pixels in a column
(colour a), a long one 16 rows with the pattern c b c in the middle rows and b alone at the ends (the engine hands the three bytes
as a, c, b: engineapi.h). The x starts of the thin drops are read from the DLL image, the speeds and starts are range-checked."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/rain'
DLL = (_DEV + '/analysis/bin/BiosGame.dll')
failed = 0
TOP, W, VH = 56, 640, 368
A, B, C = 0x76, 0x56, 0x57       # BeginPageFilter(a, b, c) of the rain FX


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:400] if detail else ''))


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    stride = (w + 3) // 4 * 4
    return [bytearray(d[off + (h - 1 - y) * stride:][:w]) for y in range(h)]


def dll_thin_x():
    d = open(DLL, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    for i in range(nsec):
        so = pe + 24 + opt + 40 * i
        vsz, va, rsz, ro = struct.unpack_from('<IIII', d, so + 8)
        a = base + va
        if a <= 0x100a1f90 < a + rsz:
            o = 0x100a1f90 - a + ro
            return [struct.unpack_from('<h', d, o + 12 * k)[0] for k in range(217)]


def run(script, *opts):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--seed', '3'] + list(opts) + ['--script', script], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-500:])
    return r.stdout.splitlines()


N = 30
parts = ['advance 500', 'shot %s/warm.bmp' % T, 'api EndPageFilter', 'shot %s/base.bmp' % T, 'api BeginPageFilter %d %d %d' % (A, B, C)]
for i in range(N):
    parts += ['game rain', 'shot %s/f%02d.bmp' % (T, i)]
out = run('; '.join(parts))
# the dumps, one per frame
frames, cur = [], None
for l in out:
    if l.startswith('rain started='):
        cur = {'started': l.endswith('1'), 'thin': [], 'tall': []}
        frames.append(cur)
    elif l.startswith('rain thin ') or l.startswith('rain tall '):
        _, k, x, y, s = l.split()
        cur[k].append((int(x), int(y), int(s)))
check('one dump per frame', len(frames) == N and all(f['started'] and len(f['thin']) == 217 and len(f['tall']) == 15 for f in frames), len(frames))

base = read_bmp(T + '/base.bmp')
warm = read_bmp(T + '/warm.bmp')
check('with the filter off the picture is the page alone (differs from the first, filtered frame)', base != warm)

xs = dll_thin_x()
f0 = frames[0]
check('thin drops start at the x of the DLL data and fall 10..25 per frame', [d[0] for d in f0['thin']] == xs and all(10 <= d[2] <= 25 for d in f0['thin']))
check('long drops: x < 630, speed 30..60', all(0 <= d[0] < 630 and 30 <= d[2] <= 60 for d in f0['tall']))
check('the first positions of the thin drops are y < 100 plus one step (one frame was drawn)', all(0 <= d[1] < 125 for d in f0['thin']), f0['thin'][:3])


def expected(base_rows, f):
    rows = [bytearray(r) for r in base_rows]
    end = VH
    for x, y, s in f['thin']:
        if y >= end:
            continue
        for i in range(5):
            if y + i >= end:
                break
            rows[TOP + y + i][x] = A
    for x, y, s in f['tall']:
        if y >= end:
            continue
        for i in range(16):
            if y + i >= end:
                break
            r = rows[TOP + y + i]
            if i < 4 or i > 14:
                r[x + 1] = B
            else:
                r[x] = C
                r[x + 1] = B
                r[x + 2] = C
    return rows


bad = []
changed = 0
for i in range(N):
    got = read_bmp('%s/f%02d.bmp' % (T, i))
    exp = expected(base, frames[i])
    if got != exp:
        bad.append(i)
    changed += sum(1 for y in range(480) if got[y] != base[y])
check('every frame is the page plus exactly the drops of the decompiled drawing', not bad, bad[:5])
check('drops are drawn (the rain overlay changes rows of the view)', changed > 0)
# the drops move: a thin drop of frame k+1 is 'speed' lower than in frame k (until it leaves the view and restarts at the top)
moved = 0
for i in range(N - 1):
    for a, b in zip(frames[i]['thin'], frames[i + 1]['thin']):
        if b[0] == a[0] and b[1] == a[1] + a[2]:
            moved += 1
check('drops fall by their speed each frame', moved > 100 * (N - 1), moved)
restarted = sum(1 for i in range(N - 1) for a, b in zip(frames[i]['thin'], frames[i + 1]['thin']) if b[1] < a[1])
check('drops that leave the view start again at the top (y < 100)', restarted > 0 and all(b[1] < 100 for i in range(N - 1) for a, b in zip(frames[i]['thin'], frames[i + 1]['thin']) if b[1] < a[1]))
sys.exit(1 if failed else 0)
