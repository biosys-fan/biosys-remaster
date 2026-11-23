"""Helpers of tests/test_clickanim.py: run a click scenario headless the way the window runs it (the `window HZ P` fake display of
src/app/pacing.h, `wshots` writes every presented frame whose content changed, `wregion` the page surface rectangle of a page-relative
sprite) and compare what was presented with the frames of the .TAN as decoded by the independent tool build/biosys-tandump."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import math, os, re, struct, subprocess, sys, threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play_lib import Play, ROOT, VIEW_TOP

D = _DEV
BIOTIME = [D + '/setup-cd/installed/DATA1/Biotime', D + '/game-cd/files/BIOTIME']
OBJECTS = D + '/setup-cd/installed/DATA1/Biodata/OBJECTS.DAT'
T = ROOT + '/build/test/clickanim'
P_MS = 33                         # the default engine pass interval (src/app/pacing.h)
os.makedirs(T, exist_ok=True)


def sh(args):
    r = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        raise RuntimeError('%s failed: %s' % (args[:3], r.stderr.strip()[:300]))
    return r.stdout


def find_time(rel):
    parts = rel.replace('\\', '/').strip('/').split('/')
    for root in BIOTIME:
        p = root
        for part in parts:
            try:
                names = os.listdir(p)
            except OSError:
                p = None
                break
            m = [n for n in names if n.lower() == part.lower()]
            if not m:
                p = None
                break
            p = p + '/' + m[0]
        if p:
            return p
    raise RuntimeError('missing ' + rel)


def read_bmp(path):
    """-> (rows of palette indices top-down, palette [(r,g,b)] or None for a 24 bit file, width)"""
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    bpp, = struct.unpack_from('<H', d, 28)
    if bpp == 24:
        stride = (w * 3 + 3) & ~3
        return [d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w * 3] for y in range(h)], None, w
    stride = (w + 3) & ~3
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    return [d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w] for y in range(h)], pal, w


_tan = {}
_tan_lock = threading.Lock()


def tan_frames(rel):
    """-> (frames: list of rows (bytes) top-down, w, h): the independent decoder, exported frame by frame"""
    with _tan_lock:
        return _tan_frames(rel)


def _tan_frames(rel):
    key = rel.lower().replace('\\', '/').strip('/')
    if key in _tan:
        return _tan[key]
    path = find_time(rel)
    info = sh([ROOT + '/build/biosys-tandump', 'info', path]).splitlines()[0]
    f = dict(p.split('=') for p in info.split()[1:])
    w, h = (int(v) for v in f['size'].split('x'))
    frames = []
    for k in range(int(f['frames'])):
        out = '%s/tan_%s_%d.bmp' % (T, re.sub(r'[^a-z0-9]', '_', key), k)
        sh([ROOT + '/build/biosys-tandump', 'export', path, str(k), out])
        frames.append(read_bmp(out)[0])
    _tan[key] = (frames, w, h)
    return _tan[key]


def play_sequence(n, mode):
    """frame numbers (0-based) in the order the engine shows them (spec 2.2)"""
    if mode == 2:
        return list(range(n - 1, -1, -1))
    if mode == 3:
        return list(range(n)) + list(range(n - 2, -1, -1))
    if mode == 4:
        return list(range(n - 1, -1, -1)) + list(range(1, n))
    return list(range(n))


def canon(frames):
    """frame index -> index of the first frame with identical pixels (a TAN may repeat a frame; they are the same picture)"""
    seen, out = {}, []
    for i, f in enumerate(frames):
        k = b''.join(f)
        out.append(seen.setdefault(k, i))
    return out


class Tan:
    """a decoded .TAN (the independent decoder): frames as rows, and per colour key the runs of non-key pixels of each frame"""

    def __init__(self, rel):
        self.rel = rel
        self.frames, self.w, self.h = tan_frames(rel)
        self.canon = canon(self.frames)
        self._runs = {}

    def runs_of(self, k, key):
        """[(row, first column, bytes)] of the non-key pixels of frame k"""
        ck = (k, key)
        if ck not in self._runs:
            out = []
            for j, row in enumerate(self.frames[k]):
                i = 0
                n = len(row)
                while i < n:
                    if row[i] == key:
                        i += 1
                        continue
                    e = i
                    while e < n and row[e] != key:
                        e += 1
                    out.append((j, i, bytes(row[i:e])))
                    i = e
            self._runs[ck] = out
        return self._runs[ck]

    def visible(self, k, key):
        return bool(self.runs_of(k, key))

    def matches(self, rows, x0, y0, k, key, clip):
        """(compared pixels, mismatching pixels) of frame k drawn at (x0, y0) into `rows`, inside clip = (x0, y0, x1, y1)"""
        cx0, cy0, cx1, cy1 = clip
        n = bad = 0
        for j, i, b in self.runs_of(k, key):
            y = y0 + j
            if y < cy0 or y >= cy1 or y >= len(rows):
                continue
            xa = max(x0 + i, cx0)
            xb = min(x0 + i + len(b), cx1)
            if xb <= xa:
                continue
            seg = b[xa - x0 - i: xb - x0 - i]
            got = rows[y][xa:xb]
            n += len(seg)
            if got != seg:
                bad += sum(1 for u, v in zip(got, seg) if u != v)
        return n, bad

    def identify(self, rows, x0, y0, key, clip, ids=None):
        """the canonical frame index shown (all its non-key pixels inside the clip equal), or None. Among the frames that match, the one
        with the most compared pixels"""
        best = None
        for k in (ids if ids is not None else range(len(self.frames))):
            if self.canon[k] != k:
                continue
            n, bad = self.matches(rows, x0, y0, k, key, clip)
            if n and not bad and (best is None or n > best[0]):
                best = (n, k)
        return best[1] if best else None


def runs(seq):
    """[(value, first index, last index)] of consecutive equal values in a list"""
    out = []
    for i, v in enumerate(seq):
        if out and out[-1][0] == v:
            out[-1][2] = i
        else:
            out.append([v, i, i])
    return out


def parse_apicalls(lines):
    """[(name, {arg: value}, line index)] of the `apicall` lines (--log-api)"""
    out = []
    for i, l in enumerate(lines):
        if not l.startswith('apicall '):
            continue
        name = l.split()[1]
        args = {}
        for m in re.finditer(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)', l):
            v = m.group(2)
            args[m.group(1)] = v[1:-1].replace('\\\\', '\\') if v.startswith('"') else v
        out.append((name, args, i))
    return out


def sprite_opens(lines, since=0):
    """the OpenSpriteAnimation / PlayObjectUseAnimation calls after line `since`: dicts with file, x, y, mode, delay, loop, keep, page, window, wav"""
    res = []
    for name, a, i in parse_apicalls(lines):
        if i < since:
            continue
        if name == 'OpenSpriteAnimation':
            res.append(dict(api=name, file=a['file'], x=int(a['x']), y=int(a['y']), mode=int(a['playMode']), delay=int(a['frameDelayMs']),
                            loop=int(a['loop']), keep=int(a['keepPainted']), page=int(a['pageRelative']), window=int(a['window']),
                            wav=a['wav'], line=i))
        elif name == 'PlayObjectUseAnimation':
            res.append(dict(api=name, file=a['tanFile'], x=int(a['x']), y=int(a['y']), mode=int(a['playMode']), delay=int(a['frameDelayMs']),
                            loop=0, keep=0, page=0, window=-1, wav=a['wavFile'], hide=int(a['hideHeld']), line=i))
    return res


_objrecs = None


def object_anim_records():
    """{(object, event code): [dict(x, y, mode, delay, file, wav, var)]} from the object file (independent dump tool)"""
    global _objrecs
    if _objrecs is None:
        _objrecs = {}
        cur = None
        for l in sh([ROOT + '/build/biosys-objdump', 'objects', OBJECTS]).splitlines():
            m = re.match(r'obj \d+ name=\[([^\]]*)\]', l)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r'anim \d+ \d+ event=(\d+) x=(-?\d+) y=(-?\d+) unk0c=(\d+) delay=(\d+) file=\[([^\]]*)\] wav=\[([^\]]*)\] var=\[([^\]]*)\]', l)
            if m and cur:
                _objrecs.setdefault((cur.lower(), int(m.group(1))), []).append(
                    dict(x=int(m.group(2)), y=int(m.group(3)), mode=int(m.group(4)), delay=int(m.group(5)), file=m.group(6), wav=m.group(7), var=m.group(8)))
    return _objrecs


class Result:
    def __init__(self, lines, err, shots, prefix):
        self.lines, self.err, self.shots, self.prefix = lines, err, shots, prefix


def run_window(setup, action, hz, smooth=False, prefix='w', region=None, tail=0, game_args=(), opts=('--log-api', '--log-events'), scale=2, timeout=900, pass_ms=P_MS):
    """Runs `setup` (script commands, headless), then the display `window HZ 33`, `wshots` (and `wregion`), the `action` commands and `tail`
    ms more. Returns Result: the stdout lines, the list of wshot dicts, the file prefix of the frames."""
    pre = '%s/%s' % (T, prefix)
    for f in os.listdir(T):
        if f.startswith(prefix + '_') and f[len(prefix) + 1:len(prefix) + 4].isdigit():
            os.remove(os.path.join(T, f))
    pre = '%s/%s_' % (T, prefix)
    cmds = list(setup) + ['echo @window', 'window %d %d' % (hz, pass_ms), 'advance 200']
    if region:
        cmds.append('wregion %d,%d,%d,%d' % region)
    cmds += ['echo @shots', 'wshots %s%s' % (pre, (' %d' % scale) if smooth else '')] + list(action)
    if tail:
        cmds.append('advance %d' % tail)
    cmds += ['wshots off', 'echo @end']
    args = [ROOT + '/build/biosys', '--game', '--mute', '--seed', '1'] + list(opts) + (['--panorama', 'smooth'] if smooth else []) + list(game_args) + \
           ['--script', '; '.join(cmds)]
    r = subprocess.run(args, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    lines = r.stdout.splitlines()
    shots = []
    for l in lines:
        if l.startswith('wshot '):
            d = dict(m.groups() for m in re.finditer(r'(\w+)=(\S+)', l))
            shots.append(dict(t=int(d['t']), n=int(d['n']), active=int(d['active']), same=d.get('smooth_same_as_fresh'),
                              uploads=int(d['uploads']) if 'uploads' in d else None))
    return Result(lines, r.stderr.splitlines() + ([] if r.returncode == 0 else ['rc=%d' % r.returncode]), shots, pre)


def after(lines, marker):
    for i, l in enumerate(lines):
        if l == marker:
            return lines[i + 1:], i + 1
    return [], 0


def shot_path(res, n, kind=''):
    return '%s%03d%s.%s' % (res.prefix, n, kind, 'reg' if kind == 'reg' else 'bmp')


def read_region(res, n, w, h):
    d = open('%s%03d.reg' % (res.prefix, n), 'rb').read()
    return [d[j * w:(j + 1) * w] for j in range(h)]


def cyan_index(pal):
    return pal.index((0, 255, 255)) if (0, 255, 255) in pal else -1


def run_durations(shots, labels, ignore=(None,)):
    """[(label, t first presented, t of the next different presentation)] over consecutive shots with the same label"""
    out = []
    for r in runs(labels):
        if r[0] in ignore:
            continue
        t0 = shots[r[1]]['t']
        t1 = shots[r[2] + 1]['t'] if r[2] + 1 < len(shots) else None
        out.append((r[0], t0, t1))
    return out


def nominal_ms(delay, pass_ms=P_MS):
    """how long a frame with delay `delay` stays: the next frame is drawn at the first engine pass at or after now + delay
    (spec 2.2: nextDue = now + delay, no catch-up; one frame per pass)"""
    return max(1, math.ceil(delay / pass_ms)) * pass_ms


def bmp_equal(a, b):
    return open(a, 'rb').read() == open(b, 'rb').read()
