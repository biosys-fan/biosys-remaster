"""--picture clean: the light picture cleaner (palette-aware de-dither + Catmull-Rom enlargement; src/engine/picclean.*, src/app/clean.*,
src/app/panorama_smooth.* and main.cpp for the window; spec upscale-trial/light_spec.md, reference upscale-trial/scripts/light/reference.py).

A. The filter against the reference, byte for byte: the five vectors, the integer weight tables, and more inputs generated with reference.py
   (real game pictures and palettes, keys, protect masks, wrap, every kernel and scale); the soft partner table (slew remedy) equals the
   reference for weights 0 / 1 and a Python re-derivation of its formula in between.
B. The default is unchanged: no option, --picture original and --picture clean give byte-identical 8-bit frames (scripts) and the default
   window picture is the --picture original one.
C. Headless captures (`cleanshot`, the CPU composition of what the window draws) against an independent Python composition built from reference.py:
   a panorama (day and night palette), a held object and a worn overlay on a panorama, a close-up with lettering, a page with game text,
   a terminal page, a movie frame; at 2x and 3x. Opaque pixels equal the Python result exactly, the panorama behind them is compared with the
   exact-cylinder bilinear sampling of the Python page texture (tests/smoothpano_oracle.py) within the tolerance stated below.
D. The window loop is never held up: with a fake clock and the cleaner on its worker threads, a dawn on a sky page; the longest main-thread step
   stays small while the worker needs ten times as long; after the cleaner has caught up, texture and frame equal a fresh computation exactly.
E. The open risk of the spec: the discrete neighbour decisions while the palette is mixed (a full dawn and dusk on two sky pages, real palette
   sequences of the game): flips and brightness jumps without and with the remedy.
F. --widescreen: the side areas are cleaned too.
G. Comparison images in build/shots/clean_*.bmp.
Pure standard library, run with `python3 -I`."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import importlib.util
import math
import os
import random
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import smoothpano_oracle as so

REFPY = (_DEV + '/upscale-trial/scripts/light/reference.py')
VEC = (_DEV + '/upscale-trial/out/light/vectors')
spec = importlib.util.spec_from_file_location('lightref', REFPY)
ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)

T = 'build/test/picclean'
SHOTS = 'build/shots'
shutil.rmtree(T, ignore_errors=True)
os.makedirs(T + '/saves', exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
failed = 0
TOOL = 'build/biosys-picclean'
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
VX, VY, VW, VH = 0, 56, 640, 368

# Tolerances of the panorama part (per colour channel, 0..255). The cleaned picture behind the overlay is the C++ mesh rasteriser sampling the page
# texture bilinearly, the oracle samples the same texture (computed by reference.py) at the exact cylinder point: the differences are the mesh's
# interpolation error (at most 0.05 output pixel at 2x, 0.07 at 3x: biosys-panotest mesh) times the steepest edge of the cleaned texture, and one
# level of rounding. Same numbers as tests/test_smoothpano.py. Opaque overlay pixels and everything outside the view must be EXACT.
TOL_MEAN = 0.15
TOL_MAX = 8


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + ('  -- ' + str(detail)[:900] if detail else ''))


def run(script, *opts, game=False, window=False, seed=1, timeout=1800):
    cmd = ['build/biosys', '--mute', '--no-config', '--save-dir', T + '/saves'] + (['--game', '--seed', str(seed), '--skip-intro'] if game else []) + list(opts) + ['--script', script]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if r.returncode:
        print(r.stderr[-800:])
    return r.stdout.splitlines()


def readb(p):
    return open(p, 'rb').read()


def writeb(p, d):
    open(p, 'wb').write(d)


def pal768(path):
    d = readb(path)
    return [(d[3 * i], d[3 * i + 1], d[3 * i + 2]) for i in range(256)] if len(d) == 768 else None


# =============================================================================================== A. the filter against the reference
print('--- A. the filter against reference.py')
names = ['v1_sky_s5_day', 'v2_foliage_r4_night_adaptive', 'v3_lettering_fr2_lanczos', 'v4_object_kukri_key', 'v5_wrap_protect_dusk']
for n in names:
    cmd = [l for l in open('%s/%s.cmd' % (VEC, n)).read().splitlines() if l.startswith('python3')][0]
    want_T = int(re.search(r'# T=(\d+)', open('%s/%s.cmd' % (VEC, n)).read()).group(1))
    tok = shlex.split(cmd)[3:]          # after "python3 -I reference.py"
    args = []
    i = 0
    while i < len(tok):
        k = tok[i]
        if k in ('--wrap', '--no-dedither'):
            args.append(k); i += 1
            continue
        v = tok[i + 1]
        if k in ('--idx', '--pal', '--ref-pal', '--prot'):
            v = '%s/%s' % (VEC, v)
        elif k == '--out':
            v = '%s/%s.out' % (T, n)
        elif k == '--out-dedither':
            v = '%s/%s.dd' % (T, n)
        args += [k, v]
        i += 2
    r = subprocess.run([TOOL, 'run'] + args, capture_output=True, text=True)
    exp = [f for f in os.listdir(VEC) if f.startswith(n + '.expected.')][0]
    same = readb('%s/%s' % (VEC, exp)) == readb('%s/%s.out' % (T, n))
    same_dd = readb('%s/%s.dedither.rgb' % (VEC, n)) == readb('%s/%s.dd' % (T, n))
    check('vector %s: final output (%s) and de-dithered picture are the reference\'s, byte for byte; T = %d' % (n, exp.split('.')[-1], want_T),
          same and same_dd and r.stdout.strip() == 'T=%d' % want_T, (same, same_dd, r.stdout, r.stderr))

# the integer weight tables
out = subprocess.run([TOOL, 'weights'], capture_output=True, text=True).stdout.splitlines()
ok = True
bad = ''
for kern in ('bilinear', 'catmull', 'mitchell', 'lanczos3'):
    for s in (1, 2, 3, 4):
        r_, tab = ref.weight_table(kern, s)
        want = '%s %d %d %s' % (kern, s, r_, ' '.join('[' + ','.join(str(v) for v in row) + ']' for row in tab))
        if want not in out:
            ok = False
            bad = want
check('the 16 weight tables (4 kernels x scales 1..4) are reference.weight_table\'s', ok, bad)


def tool_vs_reference(name, ix, w, h, pal, refpal=None, key=None, prot=None, wrap=False, scale=2, kernel='catmull', t0=14, dither=True):
    base = '%s/x_%s' % (T, name)
    writeb(base + '.idx', bytes(ix))
    writeb(base + '.pal', bytes(b for c in pal for b in c))
    args = ['--idx', base + '.idx', '--w', str(w), '--h', str(h), '--pal', base + '.pal', '--scale', str(scale), '--kernel', kernel, '--t0', str(t0),
            '--out', base + '.out', '--out-dedither', base + '.dd']
    if refpal is not None:
        writeb(base + '.ref.pal', bytes(b for c in refpal for b in c))
        args += ['--ref-pal', base + '.ref.pal']
    if key is not None:
        args += ['--key', str(key)]
    if prot is not None:
        writeb(base + '.prot', bytes(prot))
        args += ['--prot', base + '.prot']
    if wrap:
        args.append('--wrap')
    if not dither:
        args.append('--no-dedither')
    r = subprocess.run([TOOL, 'run'] + args, capture_output=True, text=True)
    dd, out, Tv = ref.process(bytes(ix), w, h, pal, refpal, key, bytes(prot) if prot is not None else None, wrap, scale, kernel, t0, dither)
    ch = len(out)
    same = readb(base + '.out') == ref.interleave(out, w * scale * h * scale) and readb(base + '.dd') == ref.interleave(dd, w * h)
    return same and r.stdout.strip() == 'T=%d' % Tv, (Tv, r.stdout.strip(), r.stderr[-200:])


# the cost, measured here (scalar; the numbers are printed, the bounds only catch a regression that would make a worker too slow)
bench = subprocess.run([TOOL, 'bench', '--reps', '3'], capture_output=True, text=True).stdout.splitlines()
for l in bench:
    print('     ' + l)
nums = [dict((k, float(v)) for k, v in re.findall(r'(de-dither|histogram|enlarge 2x|3x|4x) ([\d.]+) ms', l)) for l in bench]
check('bench: a full panorama page refresh (de-dither + 2x enlargement) takes %.0f ms and a 640 x 480 frame at 3x %.0f ms on this machine (bounds 400 / 100 ms)'
      % (nums[0]['de-dither'] + nums[0]['enlarge 2x'], nums[1]['de-dither'] + nums[1]['3x']), nums[0]['de-dither'] + nums[0]['enlarge 2x'] < 400 and nums[1]['de-dither'] + nums[1]['3x'] < 100)

# game pictures and palettes for the extra inputs
out = run('page R4; surfdump %s/r4.raw; slot night; dumppal %s/r4_night.pal; daypal %s/r4_day.pal; slot dusk; dumppal %s/r4_dusk.pal;'
          ' page S1; surfdump %s/s1.raw; slot day; dumppal %s/s1_day.pal; slot dawn; dumppal %s/s1_dawn.pal' % ((T,) * 7), '--no-objects')
r4 = readb(T + '/r4.raw')
s1 = readb(T + '/s1.raw')


def crop(surf, x0, y0, w, h):
    return [surf[(y0 + y) * 2560 + x0 + x] for y in range(h) for x in range(w)]


night, day, dusk = pal768(T + '/r4_night.pal'), pal768(T + '/r4_day.pal'), pal768(T + '/r4_dusk.pal')
s1day, s1dawn = pal768(T + '/s1_day.pal'), pal768(T + '/s1_dawn.pal')
rng = random.Random(1998)
cases = []
ix = crop(r4, 1100, 300, 96, 64)
cases.append(('r4_night_scale3', dict(ix=ix, w=96, h=64, pal=night, refpal=day, scale=3)))
ix = crop(s1, 1200, 200, 80, 60)
pal_s1 = s1dawn
kidx = max(set(ix), key=ix.count)
prot = [1 if (20 <= x < 50 and 10 <= y < 18) or (x, y) == (3, 40) else 0 for y in range(60) for x in range(80)]
cases.append(('s1_dawn_key_prot_wrap_lanczos4', dict(ix=ix, w=80, h=60, pal=pal_s1, refpal=s1day, key=kidx, prot=prot, wrap=True, scale=4, kernel='lanczos3')))
pal_n = [(rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(256)]
for i in range(0, 256, 2):                      # close pairs, as dither pairs are
    pal_n[i + 1] = tuple(min(255, max(0, c + rng.randrange(-9, 10))) for c in pal_n[i])
ixn = [rng.randrange(0, 40) if rng.random() < 0.8 else rng.randrange(256) for _ in range(64 * 48)]
cases.append(('noise_mitchell2_key', dict(ix=ixn, w=64, h=48, pal=pal_n, refpal=None, key=7, scale=2, kernel='mitchell', t0=12)))
cases.append(('noise_bilinear3_nodither', dict(ix=ixn, w=64, h=48, pal=pal_n, scale=3, kernel='bilinear', dither=False)))
cases.append(('noise_scale1', dict(ix=ixn, w=64, h=48, pal=pal_n, refpal=pal_n, scale=1)))
cases.append(('r4_dusk_catmull4_wrap', dict(ix=crop(r4, 2500, 600, 64, 48), w=64, h=48, pal=dusk, refpal=day, wrap=True, scale=4)))
for name, kw in cases:
    ok, info = tool_vs_reference(name, **kw)
    check('generated input %s (%dx%d, scale %d, %s): C++ = reference.py byte for byte (T = %s)' % (name, kw['w'], kw['h'], kw.get('scale', 2), kw.get('kernel', 'catmull'), info[0]), ok, info)

# the soft partner table of the slew remedy: weights 0 / 32 are the reference's decision exactly; between them a re-derived formula
ix = crop(r4, 1100, 300, 96, 64)
cl = ref.partner_table(night, 7, None)
wt = bytearray(65536)
for i in range(65536):
    wt[i] = 32 * cl[i]
writeb(T + '/soft_hard.tab', bytes(wt))
writeb(T + '/x_r4_night_scale3.idx', bytes(ix))
r = subprocess.run([TOOL, 'soft', '--idx', T + '/x_r4_night_scale3.idx', '--w', '96', '--h', '64', '--pal', T + '/x_r4_night_scale3.pal', '--table', T + '/soft_hard.tab',
                    '--out', T + '/soft_hard.out'], capture_output=True, text=True)
dd_ref = ref.dedither(bytes(ix), 96, 64, night, 7, None, None, False)
check('soft partner table with weights 0 / 32 gives the reference\'s de-dither exactly', readb(T + '/soft_hard.out') == ref.interleave(dd_ref, 96 * 64), r.stderr)


def soft_py(ix, w, h, pal, wt, wrap):
    """the soft de-dither re-derived from picclean.h: out = c + sum_b B_b (mean_b - c) / max(32, sum_b B_b), B_b = smallest weight of the box's three
    other pixels, in 1/32; rounded half up; all weights 0 / 32: the reference's arithmetic"""
    out = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            def at(dx, dy):
                yy = min(max(y + dy, 0), h - 1)
                xx = (x + dx) % w if wrap else min(max(x + dx, 0), w - 1)
                return ix[yy * w + xx]
            c = ix[y * w + x]
            nb = [at(-1, -1), at(0, -1), at(1, -1), at(-1, 0), at(1, 0), at(-1, 1), at(0, 1), at(1, 1)]
            ws = [wt[c * 256 + n] for n in nb]
            boxes = [(0, 1, 3), (1, 2, 4), (3, 5, 6), (4, 6, 7)]
            frac = any(0 < v < 32 for v in ws)
            res = []
            if not frac:
                for k in range(3):
                    s = sw = 0
                    for bx in boxes:
                        if all(ws[j] for j in bx):
                            s += sum(pal[nb[j]][k] for j in bx) + pal[c][k]
                            sw += 4
                    res.append(pal[c][k] if sw == 0 else (s * ref.RECIP[sw] + 32768) >> 16)
            else:
                for k in range(3):
                    num = S = 0
                    for bx in boxes:
                        B = min(ws[j] for j in bx)
                        if B == 0:
                            continue
                        S += B
                        num += B * (sum(pal[nb[j]][k] for j in bx) + pal[c][k] - 4 * pal[c][k])
                    if S == 0:
                        res.append(pal[c][k])
                    else:
                        D = max(32, S) * 4
                        res.append(min(255, max(0, (2 * (pal[c][k] * D + num) + D) // (2 * D))))
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(res)
    return bytes(out)


wt2 = bytearray(65536)
for a in range(256):
    for b in range(a, 256):
        d = ref.dist_w(night[a], night[b])
        v = 32 if a == b else (32 if d <= 7 else max(0, 32 - 4 * (d - 7)))
        wt2[a * 256 + b] = wt2[b * 256 + a] = v
writeb(T + '/soft_frac.tab', bytes(wt2))
for wrap in (False, True):
    r = subprocess.run([TOOL, 'soft', '--idx', T + '/x_r4_night_scale3.idx', '--w', '96', '--h', '64', '--pal', T + '/x_r4_night_scale3.pal', '--table', T + '/soft_frac.tab',
                        '--out', T + '/soft_frac.out'] + (['--wrap'] if wrap else []), capture_output=True, text=True)
    want = soft_py(ix, 96, 64, night, wt2, wrap)
    got = readb(T + '/soft_frac.out')
    nfrac = sum(1 for v in wt2 if 0 < v < 32)
    check('soft partner table with fractional weights (%d entries, wrap=%s): C++ = the re-derived formula, byte for byte' % (nfrac, wrap), got == want and nfrac > 0,
          sum(1 for a_, b_ in zip(got, want) if a_ != b_))

# =============================================================================================== B. the default is unchanged
print('--- B. the default is unchanged')
SCENES_B = [
    ('pano', ['--no-objects'], 'page R4; scroll 1000,328; shot {p}_a.bmp; scroll 2248,0; shot {p}_b.bmp; page T1; scroll 1000,300; shot {p}_c.bmp; surfdump {p}_s.raw'),
    ('flat', ['--no-objects'], 'page fsheater; shot {p}_a.bmp; page Fr2mixer; shot {p}_b.bmp'),
    ('game', [], 'advance 3000; shot {p}_a.bmp; keydown TAB; advance 100; shot {p}_b.bmp; keyup TAB; advance 5000; page FR4probe_o; advance 200; shot {p}_c.bmp', True),
]
for name, opts, script, *g in SCENES_B:
    digests = {}
    for tag, o in (('none', []), ('orig', ['--picture', 'original']), ('clean', ['--picture', 'clean']), ('cso', ['--picture', 'clean', '--text', 'original'])):
        run(script.format(p='%s/def_%s_%s' % (T, name, tag)), *opts, *o, game=bool(g))
        files = sorted(f for f in os.listdir(T) if f.startswith('def_%s_%s_' % (name, tag)))
        digests[tag] = [readb('%s/%s' % (T, f)) for f in files]
    check('%s: no option, --picture original and --picture clean give byte-identical 8-bit frames and page surfaces (%d files)' % (name, len(digests['none'])),
          digests['none'] == digests['orig'] == digests['clean'] == digests['cso'] and len(digests['none']) >= 2)


def window(path, *opts, page='R4', scroll='1000,328', ms=2600):
    if os.path.exists(path):
        os.remove(path)
    cmd = ['build/biosys', '--mute', '--no-config', '--no-objects', '--page', page] + (['--scroll', scroll] if scroll else []) + ['--quit-after-ms', str(ms), '--test-shot', path] + list(opts)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    return r


w0 = window(T + '/win_none.bmp', '--window-size', '1280x960')
w1 = window(T + '/win_orig.bmp', '--window-size', '1280x960', '--picture', 'original')
check('window (dummy video driver): the default picture is the --picture original one, byte for byte', os.path.exists(T + '/win_none.bmp') and readb(T + '/win_none.bmp') == readb(T + '/win_orig.bmp'), w0.stderr[-300:])
r = subprocess.run(['build/biosys', '--picture', 'clean', '--write-config', '--config', T + '/c.conf', '--script', 'echo x'], capture_output=True, text=True)
conf = readb(T + '/c.conf').decode() if os.path.exists(T + '/c.conf') else ''
check('--picture clean is a preference key "picture" (written by --write-config); an unknown value is refused', re.search(r'^picture\s*=\s*clean', conf, re.M) is not None
      and subprocess.run(['build/biosys', '--no-config', '--picture', 'soft', '--script', 'echo x'], capture_output=True, text=True).returncode != 0, conf[-400:])
# the flat page of the window against the headless composition (the same CleanService, a window texture 1:1 on the dummy driver's software renderer)
wf = window(T + '/win_flat.bmp', '--window-size', '1280x960', '--picture', 'clean', page='Fr2mixer', scroll='')
run('page Fr2mixer; cleanshot %s/hl_flat.bmp 2' % T, '--no-objects', '--picture', 'clean')
check('window --picture clean on a flat page at scale 2 equals the headless cleanshot, byte for byte',
      os.path.exists(T + '/win_flat.bmp') and readb(T + '/win_flat.bmp') == readb(T + '/hl_flat.bmp'), wf.stderr[-300:])
wp = window(T + '/win_pano.bmp', '--window-size', '1280x960', '--picture', 'clean')
wq = window(T + '/win_pano_plain.bmp', '--window-size', '1280x960', '--panorama', 'smooth', '--text', 'sharp')
check('window --picture clean on a panorama: a picture that differs from --panorama smooth (the cleaner ran)', os.path.exists(T + '/win_pano.bmp') and readb(T + '/win_pano.bmp') != readb(T + '/win_pano_plain.bmp'))

# =============================================================================================== C. captures against the Python composition
print('--- C. captures against an independent Python composition (reference.py)')


def hist_of(surf):
    h = [0] * 256
    for v in surf:
        h[v] += 1
    return h


def free_index(frame_rows):
    used = set()
    for r in frame_rows:
        used.update(r)
    for k in range(256):
        if k not in used:
            return k
    raise RuntimeError('no free index')


def clean_frame_py(frame_rows, pal, dayp, hist, scale, trn=None, raw=(), dither=True):
    """the cleaned 640 x 480 frame by reference.py: returns [R, G, B] planes (and the alpha plane when trn is given), enlarged by `scale` (Catmull-Rom)"""
    w, h = 640, 480
    ix = bytearray(b''.join(frame_rows))
    prot = bytearray(w * h)
    K = None
    if trn is not None:
        K = free_index(frame_rows)
        for i in range(w * h):
            if trn[i]:
                ix[i] = K
                prot[i] = 1
    for (rx, ry, rw, rh) in raw:
        for y in range(max(0, ry), min(h, ry + rh)):
            for x in range(max(0, rx), min(w, rx + rw)):
                prot[y * w + x] = 1
    if dither:
        Tt = ref.threshold(pal, dayp, hist) if hist else ref.threshold(pal, None, [0] * 256)
        dd = ref.dedither(bytes(ix), w, h, pal, Tt, None, bytes(prot) if any(prot) else None, False)
    else:
        Tt = 0
        dd = [bytearray(bytes(ix).translate(bytes(c[k] for c in pal))) for k in range(3)]
    src = ref.key_fill(dd, bytes(ix), w, h, K) if trn is not None else dd
    planes = [ref.enlarge_plane(p, w, h, scale, 'catmull', False) for p in src]
    if trn is not None:
        mask = bytearray(0 if trn[i] else 255 for i in range(w * h))
        al = ref.enlarge_plane(mask, w, h, scale, 'catmull', False)
        W = w * scale
        for Y in range(h * scale):       # (a decision of the port, not the reference: the panorama is not drawn outside the view, so alpha is 255 there)
            if Y < VY * scale or Y >= (VY + VH) * scale:
                al[Y * W:(Y + 1) * W] = b'\xff' * W
        planes.append(al)
    return planes, Tt


class TexSampler:
    """bilinear sampler of the cleaned page texture: planes of (2560 * k) x (1024 * k), texel i centred at (i + 0.5) / k page pixels; x wraps, y clamps"""

    def __init__(self, planes, k):
        self.p, self.k = planes, k
        self.W, self.H = 2560 * k, 1024 * k

    def at(self, u, v):
        k = self.k
        sx = u * k - 0.5
        sy = v * k - 0.5
        i0 = math.floor(sx)
        j0 = math.floor(sy)
        fx = sx - i0
        fy = sy - j0
        xa, xb = i0 % self.W, (i0 + 1) % self.W
        ya, yb = min(max(j0, 0), self.H - 1), min(max(j0 + 1, 0), self.H - 1)
        out = []
        for P in self.p:
            top = P[ya * self.W + xa] * (1 - fx) + P[ya * self.W + xb] * fx
            bot = P[yb * self.W + xa] * (1 - fx) + P[yb * self.W + xb] * fx
            out.append(min(255, int(math.floor(top * (1 - fy) + bot * fy + 0.5))))
        return out


_tex_cache = {}


def page_texture_py(name, surf, pal, dayp, k=2):
    key = (name, k)
    if key not in _tex_cache:
        t0 = time.time()
        hist = hist_of(surf)
        Tt = ref.threshold(pal, dayp, hist)
        dd = ref.dedither(surf, 2560, 1024, pal, Tt, None, None, True)
        planes = [ref.enlarge_plane(p, 2560, 1024, k, 'catmull', True) for p in dd]
        _tex_cache[key] = (TexSampler(planes, k), Tt)
        print('     (page texture %s by reference.py: T=%d, %.0f s)' % (name, Tt, time.time() - t0))
    return _tex_cache[key]


def rows_to_list(rows):
    return [bytes(r) for r in rows]


def sample_points(scale, rng_):
    a, b = rng_.randrange(11), rng_.randrange(11)
    return [(X, Y) for Y in range(VH * scale) for X in range(VW * scale) if (X * 7 + Y * 13 + a * 3 + b) % 11 == 0]


def blend(src, a, dst):
    return [(src[k] * a + dst[k] * (255 - a) + 127) // 255 for k in range(3)]


def compare_scene(name, scale, got_path, planes, alpha_plane, sampler, cam, frame_rows, pal, mask_skip=None, text_mask=None):
    """got: the C++ capture. planes: the Python cleaned frame (enlarged); sampler/cam: the panorama behind the transparent pixels or None"""
    gw, gh, got = so.read_bmp24(got_path)
    W, H = 640 * scale, 480 * scale
    check('%s at %dx: the capture is %d x %d' % (name, scale, W, H), (gw, gh) == (W, H))
    R, G, B = planes[0], planes[1], planes[2]
    exact_bad = exact_n = 0
    part = []
    for Y in range(H):
        grow = got[Y]
        base = Y * W
        for X in range(W):
            a = alpha_plane[base + X] if alpha_plane is not None else 255
            if text_mask is not None and text_mask[Y // scale][X // scale]:
                continue
            if a == 255:
                exp = (R[base + X], G[base + X], B[base + X])
                exact_n += 1
                if (grow[3 * X], grow[3 * X + 1], grow[3 * X + 2]) != exp:
                    exact_bad += 1
            else:
                part.append((X, Y, a))
    check('%s at %dx: the %d opaque output pixels equal the Python composition exactly (%d differ)' % (name, scale, exact_n, exact_bad), exact_bad == 0, exact_bad)
    if sampler is None:
        return
    # the transparent / partly transparent pixels: the overlay blended over the bilinear sample of the Python page texture
    rng_ = random.Random(7 + scale)
    pts = [(X, Y, a) for (X, Y, a) in part if (X * 7 + Y * 13) % 5 == 0]
    if len(pts) > 40000:
        pts = rng_.sample(pts, 40000)
    sm = mx = n = 0
    worst = None
    for X, Y, a in pts:
        u, v = so.page_point(cam, (X + 0.5) / scale - VX, (Y + 0.5) / scale - VY)
        under = sampler.at(u, v)
        ov = (R[Y * W + X], G[Y * W + X], B[Y * W + X])
        exp = blend(ov, a, under) if a > 0 else under
        g = got[Y][3 * X:3 * X + 3]
        e = [abs(exp[k] - g[k]) for k in range(3)]
        sm += sum(e)
        n += 3
        if max(e) > mx:
            mx, worst = max(e), (X, Y, a, exp, tuple(g))
    check('%s at %dx: %d sampled transparent / partly transparent view pixels equal overlay-over-oracle-texture (mean %.4f <= %.2f, max %d <= %d)'
          % (name, scale, len(pts), sm / max(n, 1), TOL_MEAN, mx, TOL_MAX), n > 0 and sm / n <= TOL_MEAN and mx <= TOL_MAX, worst)


def scene_pano(name, pre, opts, game=False, hold=None, slot=None, k=2):
    """one panorama scene: C++ captures at 2x and 3x + the Python composition"""
    sc = pre + '; shot %s/c_%s.bmp; barebmp %s/c_%s_bare.bmp; surfdump %s/c_%s.raw; dumppal %s/c_%s.pal; daypal %s/c_%s.day; panoinfo; ' % ((T, name) * 5)
    sc += 'cleanshot %s/c_%s_2.bmp 2 plain=%s/c_%s_2p.bmp; cleanshot %s/c_%s_3.bmp 3' % (T, name, T, name, T, name)
    out = run(sc, '--picture', 'clean', *opts, game=game)
    info = [l for l in out if l.startswith('panoinfo')][0]
    m = re.match(r'panoinfo active=1 view=(\d+),(\d+),(\d+),(\d+) yaw=(\S+) pitch=(\S+) fov=(\S+) focal=(\S+)', info)
    check('%s: a projected panorama frame' % name, m is not None, info)
    cam = (float(m.group(5)), float(m.group(6)), float(m.group(8)))
    w_, h_, pal8, frows = so.read_bmp8('%s/c_%s.bmp' % (T, name))
    _, _, _, brows = so.read_bmp8('%s/c_%s_bare.bmp' % (T, name))
    surf = readb('%s/c_%s.raw' % (T, name))
    pal = pal768('%s/c_%s.pal' % (T, name))
    dayp = pal768('%s/c_%s.day' % (T, name))
    sampler, Tpage = page_texture_py(name if hold is None else hold, surf, pal, dayp, k)
    trn = bytearray(640 * 480)
    nover = 0
    for y in range(VH):
        for x in range(VW):
            if frows[VY + y][x] == brows[y][x]:
                trn[(VY + y) * 640 + x] = 1
            else:
                nover += 1
    hist = hist_of(surf)
    Tcpp = [int(re.search(r' T=(\d+)', l).group(1)) for l in out if l.startswith('cleanshot')]
    for scale in (2, 3):
        planes, Tf = clean_frame_py(rows_to_list(frows), pal, dayp, hist, scale, trn)
        W = 640 * scale
        check('%s at %dx: the threshold T = %d the C++ reports is reference.threshold\'s (page histogram, day palette)' % (name, scale, Tf), Tcpp[scale - 2] == Tf)
        compare_scene(name, scale, '%s/c_%s_%d.bmp' % (T, name, scale), planes[:3], planes[3], sampler, cam, frows, pal)
    return nover, Tpage


nov, Tp = scene_pano('pano_day', 'page R4; scroll 1000,328', ['--no-objects'])
check('pano_day: the page threshold is 14 (the day palette is its own reference)', Tp == 14, Tp)
nov, Tp = scene_pano('pano_night', 'page R4; slot night; scroll 1000,328', ['--no-objects'])
check('pano_night: night palette against the day reference gives a lower threshold (%d)' % Tp, Tp < 14, Tp)
HOLD = 'page fce9cab_o; clickhs dropgog; page R4; scroll 1000,328; move 300,250; advance 300'
nov, _ = scene_pano('held', HOLD, [], hold='pano_day')
check('held: the held object is an overlay (%d view pixels differ from the bare projection)' % nov, nov > 300, nov)
WORN = 'page fce9cab_o; clickhs dropgog; page R4; scroll 1000,328; key SPACE; advance 400; move 320,452'
nov, _ = scene_pano('worn', WORN, [], hold='pano_day')
check('worn: the worn goggles are an overlay (%d view pixels differ from the bare projection)' % nov, nov > 1000, nov)


def scene_flat(name, script, opts, game=False, dither=True, raw=(), text=False, expect_T=None):
    sc = script + '; shot %s/c_%s.bmp; surfdump %s/c_%s.raw; dumppal %s/c_%s.pal; daypal %s/c_%s.day; ' % ((T, name) * 4)
    if text:
        sc += 'textframes %s/c_%s; ' % (T, name)
    sc += 'cleanshot %s/c_%s_2.bmp 2 plain=%s/c_%s_2p.bmp; cleanshot %s/c_%s_3.bmp 3' % (T, name, T, name, T, name)
    out = run(sc, '--picture', 'clean', *opts, game=game)
    if text:
        _, _, _, frows = so.read_bmp8('%s/c_%s_free.bmp' % (T, name))
        _, _, _, mrows = so.read_bmp8('%s/c_%s_mask.bmp' % (T, name))
    else:
        _, _, _, frows = so.read_bmp8('%s/c_%s.bmp' % (T, name))
    surf = readb('%s/c_%s.raw' % (T, name))
    pal = pal768('%s/c_%s.pal' % (T, name))
    dayp = pal768('%s/c_%s.day' % (T, name))
    hist = hist_of(surf) if (dither and not (name.startswith('terminal'))) else None
    Tcpp = [int(re.search(r' T=(\d+)', l).group(1)) for l in out if l.startswith('cleanshot')]
    for scale in (2, 3):
        planes, Tf = clean_frame_py(rows_to_list(frows), pal, dayp, hist, scale, None, raw, dither)
        if dither:
            check('%s at %dx: threshold %d (C++) = %d (reference.threshold of the page histogram and day palette)' % (name, scale, Tcpp[scale - 2], Tf), Tcpp[scale - 2] == Tf)
        tm = None
        if text:   # the sharp text sits on top: compare outside the 5 x 5 source pixels around the mask
            tm = [[0] * 640 for _ in range(480)]
            for y in range(480):
                for x in range(640):
                    if mrows[y][x]:
                        for yy in range(max(0, y - 2), min(480, y + 3)):
                            for xx in range(max(0, x - 2), min(640, x + 3)):
                                tm[yy][xx] = 1
        compare_scene(name, scale, '%s/c_%s_%d.bmp' % (T, name, scale), planes[:3], None, None, None, frows, pal, text_mask=tm)
    return out


scene_flat('lettering', 'page fsheater', ['--no-objects'])
scene_flat('closeup', 'page Fr2mixer', ['--no-objects'])
scene_flat('night_closeup', 'page Fr2mixer; slot night', ['--no-objects'])
scene_flat('gametext', 'advance 3000; page FR4probe_o; advance 200', [], game=True, text=True)
TERM = ('advance 500; page NbrBIOS4; advance 500; clickhs BUTTON; advance 600; type RUSSELL\\n; advance 100; click 482,394; advance 50; click 53,266; advance 50; '
        'bs4hs techsfan; advance 50; bs4hs 2_soils1; advance 1200')
scene_flat('terminal', TERM, [], game=True, dither=False, text=True)
MV = 'page fce9cab_o; movie movies\\intro\\INTRO1.AVI 0 0 palettes\\movies\\intro\\intro1.pal; advance 1500'
scene_flat('movie', MV, ['--no-objects'], raw=[(0, 56, 640, 368)])

# the filter really changes what it should: roughness (dither) of a sky patch before / after, and the terminal / movie get no de-dither
w_, h_, a_ = so.read_bmp24(T + '/c_pano_day_3.bmp')[:3]


def lumas(path):
    _, _, rows = so.read_bmp24(path)
    return [[(299 * r[3 * x] + 587 * r[3 * x + 1] + 114 * r[3 * x + 2]) / 1000 for x in range(len(r) // 3)] for r in rows]


def flat_blocks(img, x0, y0, x1, y1, B=24, rng_levels=14):
    out = []
    for by in range(y0, y1 - B, B):
        for bx in range(x0, x1 - B, B):
            v = [img[y][x] for y in range(by, by + B, 3) for x in range(bx, bx + B, 3)]
            if max(v) - min(v) <= rng_levels:
                out.append((bx, by))
    return out


def texture(img, blocks, B=24, d=2):
    s = n = 0
    for bx, by in blocks:
        for y in range(by, by + B - d):
            for x in range(bx, bx + B - d):
                s += abs(img[y][x] - img[y][x + d]) + abs(img[y][x] - img[y + d][x])
                n += 2
    return s / n


run('page S1; scroll 1200,300; cleanshot %s/sky_c.bmp 3 plain=%s/sky_p.bmp smooth=%s/sky_s.bmp' % (T, T, T), '--no-objects', '--picture', 'clean')
imc, imp, ims = lumas(T + '/sky_c.bmp'), lumas(T + '/sky_p.bmp'), lumas(T + '/sky_s.bmp')
blk = flat_blocks(imc, 10, 168 + 10, 1900, 168 + 1100)
tp, ts, tc = texture(imp, blk), texture(ims, blk), texture(imc, blk)
check('S1 sky (%d flat 24 x 24 blocks of the clean picture at 3x): dither texture (mean luma step over 2 output pixels) today %.2f, --panorama smooth %.2f, --picture clean %.2f: '
      'clean removes %.0f %% of today\'s and %.0f %% of the smooth picture\'s' % (len(blk), tp, ts, tc, 100 * (1 - tc / tp), 100 * (1 - tc / ts)), len(blk) > 100 and tc < 0.45 * ts and tc < 0.3 * tp, (tp, ts, tc))

# =============================================================================================== D. no hitches
print('--- D. no hitch: the window loop on a fake clock with the cleaner on its worker threads')
GOGO = 'advance 3000; page S1; game type SILVERFEATHER; game type IMMORTAL enter; window 60 33; game settime 10700; advance 500; '
out = run(GOGO + 'cleansim on tex=2 scale=3; advance 2000; cleansim wait; advance 150000; cleansim stats', game=True)
st = [l for l in out if l.startswith('cleansim frames=')][-1]
kv = {k: float(v) for k, v in re.findall(r'(\w+)=([\d.]+)', st)}
print('     ' + st)
check('the longest main-thread step of the frame loop (submit, take, upload budget of 8 MB) is %.2f ms over %d frames (<= 12 ms) while a page job takes up to %.0f ms and a frame job %.0f ms'
      % (kv['stepMsMax'], kv['frames'], kv['pageMsMax'], kv['frameMsMax']), kv['stepMsMax'] <= 12.0 and kv['pageJobs'] >= 10, kv)
check('the main thread\'s calls into the service (diff, copy, take) never exceeded %.2f ms (<= 2 ms)' % kv['serviceMainMsMax'], kv['serviceMainMsMax'] <= 2.0)
check('the heavy work is not on the main thread: the longest step is under a fifth of the longest page job', kv['stepMsMax'] * 5 <= kv['pageMsMax'])
# convergence: with slew 0 (no history in the decisions) the delivered texture and frame equal a fresh computation, also with an animation painting into the page
out = run(GOGO + 'cleansim on tex=2 scale=3 slew=0; advance 90000; cleansim check', game=True)
chk = [l for l in out if l.startswith('cleansim check')]
check('after the dawn the delivered texture and frame equal a fresh computation exactly (slew=0)', chk and chk[-1] == 'cleansim check texdiff=0 framediff=0', chk)
out = run('page R4; scroll 1000,328; window 60 33; cleansim on tex=2 scale=2 slew=0; advance 500; sprite anims\\synth\\r4d.tan 600 400 0 100 page loop; advance 4000; cleansim check',
          '--no-objects')
chk = [l for l in out if l.startswith('cleansim check')]
check('an animation painted into the page surface (changed rows only): texture and frame equal a fresh computation exactly', chk and chk[-1] == 'cleansim check texdiff=0 framediff=0', (chk, out[-3:]))

# =============================================================================================== E. flicker while the palette is mixed
print('--- E. the discrete decisions while the light changes (a full sunrise and sunset, real palette sequences, S1 and O1)')
F = T + '/flick'
os.makedirs(F, exist_ok=True)
jobs = {}
for page, tag in (('S1', 's1'), ('O1', 'o1')):
    for phase, start in (('dawn', 10700), ('dusk', 57500)):
        name = '%s_%s' % (tag, phase)
        cmd = ['build/biosys', '--mute', '--no-config', '--save-dir', T + '/saves', '--game', '--seed', '1', '--skip-intro', '--script',
               'advance 3000; page %s; game type SILVERFEATHER; game type IMMORTAL enter; window 60 33; game settime %d; advance 500; surfdump %s/%s.idx; daypal %s/%s.day;'
               ' palettetrace on; advance 725000' % (page, start, F, name, F, tag)]
        jobs[name] = subprocess.Popen(cmd, stdout=open('%s/%s.trace' % (F, name), 'w'), stderr=subprocess.DEVNULL)
for p in jobs.values():
    p.wait()
npal = {n: sum(1 for l in open('%s/%s.trace' % (F, n)) if l.startswith('palette ')) for n in jobs}
check('four palette sequences recorded (%s palettes)' % ', '.join(str(v) for v in npal.values()), all(v > 800 for v in npal.values()), npal)
ROWS = '150:350'
procs = {}
for n in jobs:
    day = '%s/%s.day' % (F, n.split('_')[0])
    for cfg, extra in (('base', []), ('slew', ['--slew', '1'])):
        procs[(n, cfg)] = subprocess.Popen([TOOL, 'flicker', '--idx', '%s/%s.idx' % (F, n), '--w', '2560', '--h', '1024', '--ref-pal', day, '--trace', '%s/%s.trace' % (F, n),
                                            '--rows', ROWS] + extra, stdout=subprocess.PIPE, text=True)
res = {}
for k, p in procs.items():
    o = p.communicate()[0].strip()
    res[k] = {a: float(b) for a, b in re.findall(r'(\w+)=([\d.]+)', re.sub(r'T=\S+', '', o))}
    print('     %-8s %-5s %s' % (k[0], k[1], o.replace('flicker ', '')))
for n in jobs:
    b, s = res[(n, 'base')], res[(n, 'slew')]
    check('%s without the remedy: the decisions flip in %d of %d palette steps (up to %d pixels), %d pixels flip again within 8 steps, the cleaned luma jumps by up to %.1f levels where the '
          'uncleaned picture jumps by %.1f; %d steps show >= 50 pixels jumping 3 levels more than their own uncleaned value (visible flicker)'
          % (n, b['flipSteps'], b['steps'], b['flipsMax'], b['toggles'], b['maxJumpCleaned'], b['maxJumpRaw'], b['stepsWithExcess50']),
          b['stepsWithExcess50'] >= 20 and b['maxJumpCleaned'] >= 8.0, b)
    check('%s with the remedy (slew 1/32 per palette step): cleaned jumps at most %.1f levels (uncleaned %.1f), %d steps with >= 50 excess pixels (was %d), at most %d excess pixels in a step (was %d)'
          % (n, s['maxJumpCleaned'], s['maxJumpRaw'], s['stepsWithExcess50'], b['stepsWithExcess50'], s['excessMax'], b['excessMax']),
          s['stepsWithExcess50'] == 0 and s['maxJumpCleaned'] <= 5.0 and s['excessMax'] <= 0.001 * 2560 * 200 and s['excessPixelsTotal'] * 50 < b['excessPixelsTotal'], s)

# =============================================================================================== F. widescreen
print('--- F. --widescreen: the side areas are cleaned')
out = run('page R4; scroll 1000,328; surfdump %s/w.raw; dumppal %s/w.pal; daypal %s/w.day; shot %s/w.bmp; panoinfo; wideshot %s/w_c.bmp 2 1704 clean; wideshot %s/w_p.bmp 2 1704; '
          'cleanshot %s/w_4x3.bmp 2' % ((T,) * 7), '--no-objects', '--picture', 'clean')
line = [l for l in out if l.startswith('wideshot') and 'mode=wide' in l]
check('the wide capture of R4 at 1704 px is wide (left / right area shown)', len(line) >= 1, out[-3:])
m = re.search(r'left=(\d+) right=(\d+)', line[0])
showL, showR = int(m.group(1)), int(m.group(2))
left = (1704 - 1280) // 2
info = [l for l in out if l.startswith('panoinfo')][0]
mi = re.match(r'panoinfo active=1 view=\d+,\d+,\d+,\d+ yaw=(\S+) pitch=(\S+) fov=\S+ focal=(\S+)', info)
cam = (float(mi.group(1)), float(mi.group(2)), float(mi.group(3)))
sampler, _ = page_texture_py('pano_day', readb(T + '/w.raw'), pal768(T + '/w.pal'), pal768(T + '/w.day'), 2)
_, _, cw = so.read_bmp24(T + '/w_c.bmp')
_, _, pw = so.read_bmp24(T + '/w_p.bmp')
_, _, c43 = so.read_bmp24(T + '/w_4x3.bmp')
pts = [(X, Y) for Y in range(VY * 2, (VY + VH) * 2) for X in list(range(left - showL, left)) + list(range(left + 1280, left + 1280 + showR)) if (X * 7 + Y * 13) % 11 == 0]
sm = mx = n = 0
for X, Y in pts:
    exp = so.wide_pixel(sampler, cam, X, Y, 2, left)
    g = cw[Y][3 * X:3 * X + 3]
    e = [abs(exp[k] - g[k]) for k in range(3)]
    sm += sum(e)
    n += 3
    mx = max(mx, max(e))
check('wide capture: %d sampled side-area pixels (%d px left, %d right) equal the exact cylinder over the Python cleaned texture (mean %.4f <= %.2f, max %d <= %d)'
      % (len(pts), showL, showR, sm / n, TOL_MEAN, mx, TOL_MAX), n > 0 and sm / n <= TOL_MEAN and mx <= TOL_MAX)
centre_same = all(cw[Y][3 * left:3 * (left + 1280)] == c43[Y][0:3 * 1280] for Y in range(0, 960))
check('wide capture: the centre columns are the 4:3 clean capture, pixel for pixel', centre_same)


iwc, iwp = lumas(T + '/w_c.bmp'), lumas(T + '/w_p.bmp')
wb = flat_blocks(iwc, 8, 130, left - 8, 880)
wb += flat_blocks(iwc, left + 1280 + 8, 130, 1704 - 8, 880)
twp, twc = texture(iwp, wb), texture(iwc, wb)
check('wide capture: in the side areas (%d flat blocks) the dither texture is %.2f (plain wide capture %.2f): %.0f %% gone' % (len(wb), twc, twp, 100 * (1 - twc / twp)),
      len(wb) > 20 and twc < 0.6 * twp, (twp, twc))

# =============================================================================================== G. pictures
print('--- G. comparison images (build/shots/clean_*.bmp, same view today / clean, 3x)')
views = [
    ('savannah_sky', 'page S1; scroll 1200,300', ['--no-objects'], False),
    ('rainforest_start', 'advance 3000', [], True),
    ('closeup_lettering', 'page fsheater', ['--no-objects'], False),
    ('terminal', TERM, [], True),
]
for name, script, opts, game in views:
    if name in ('savannah_sky', 'rainforest_start'):   # the choice of the panorama texture: 1x de-dithered + the GPU's linear filter, against the Catmull-Rom 2x texture (the default)
        run(script + '; cleanshot %s/clean_%s_tex1.bmp 3 tex=1' % (SHOTS, name), '--picture', 'clean', *opts, game=game)
    out = run(script + '; cleanshot %s/shot_%s_clean.bmp 3 plain=%s/shot_%s_today.bmp smooth=%s/shot_%s_smooth.bmp' % ((T, name) * 3), '--picture', 'clean', *opts, game=game)
    for tag in ('today', 'smooth', 'clean'):
        shutil.copy('%s/shot_%s_%s.bmp' % (T, name, tag), '%s/clean_%s_%s.bmp' % (SHOTS, name, tag))
    check('%s: today / smooth / clean images written' % name, all(os.path.exists('%s/clean_%s_%s.bmp' % (SHOTS, name, t)) for t in ('today', 'smooth', 'clean')))

print('')
print('picclean tests: %s' % ('ALL PASSED' if failed == 0 else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
