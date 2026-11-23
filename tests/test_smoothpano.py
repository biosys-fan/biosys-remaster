"""Smooth panoramas (the optional high-resolution presentation, src/engine/panomesh.*, src/app/panorama_smooth.*).
The default must stay the faithful 640 x 368 projection bit for bit; the smooth capture (`smoothshot`) must match an independent
exact-cylinder bilinear oracle (tests/smoothpano_oracle.py), must not be an upscaled copy of the 640-wide view, must leave everything
drawn over the panorama exactly as the scaled original frame, and the displayed camera must glide continuously and snap where it should."""
import math, os, random, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import smoothpano_oracle as so

T = 'build/test/smooth'
SHOTS = 'build/shots'
os.makedirs(T, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
failed = 0
VX, VY, VW, VH = 0, 56, 640, 368

# Tolerances against the oracle (per colour channel, 0..255), justified: the C++ rounds the bilinear result half up in double
# precision exactly like the oracle, so the only differences are (a) the interpolation error of the mesh (measured by
# biosys-panotest mesh: at most 0.05 output pixel at 2x, 0.07 at 3x), which moves a sample by that much against the image gradient
# (the steepest edge of the pages is ~60 levels per page pixel, i.e. < 6 levels at 0.1 pixel) and (b) 1 level of rounding where
# float32 vertex coordinates change a weight in the last place. The mean must be far below one level.
TOL_MEAN = 0.15
TOL_MAX = 6
SDL_NEAREST_MEAN = 6.0   # SDL's software renderer samples nearest with its own phase (about one output pixel off): a structural check only


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + ('  -- ' + str(detail)[:600] if detail else ''))


def run(script, *opts, game=False):
    cmd = ['build/biosys', '--mute'] + (['--game', '--seed', '3'] if game else []) + list(opts) + ['--script', script]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-600:])
    return r.stdout.splitlines()


def readb(p):
    return open(p, 'rb').read()


# =============================================================================================== A. the default is unchanged
print('--- A. default unchanged')
shots = []
script = ('page R4; scroll 1000,328; shot {t}/{n}_0.bmp; scroll 2248,0; shot {t}/{n}_1.bmp; scroll 100,656; shot {t}/{n}_2.bmp; '
          'page T1; scroll 1000,300; surfdump {t}/{n}_s.raw; pumptrace on; animshots {t}/{n}_zin; clickhs L_T1R10; shot {t}/{n}_3.bmp')
outs = {}
for n, opts in (('plain', ()), ('orig', ('--panorama', 'original')), ('smooth', ('--panorama', 'smooth')), ('nearest', ('--panorama', 'smooth', '--panorama-filter', 'nearest'))):
    outs[n] = run(script.format(t=T, n=n), '--no-objects', *opts)
names = ['_0.bmp', '_1.bmp', '_2.bmp', '_3.bmp', '_s.raw'] + ['_zin%03d.bmp' % i for i in range(12)]
for n in ('orig', 'smooth', 'nearest'):
    same = all(readb('%s/plain%s' % (T, s)) == readb('%s/%s%s' % (T, n, s)) for s in names)
    check('%s: %d images of the 8-bit output (pages, seam, limits, the 12 frames of a zoom jump) are byte-identical to the run without the option'
          % ('--panorama ' + {'orig': 'original', 'smooth': 'smooth', 'nearest': 'smooth --panorama-filter nearest'}[n], len(names)), same)
    check('%s: the script output is identical' % n, outs[n] == outs['plain'])
r = subprocess.run(['build/biosys', '--panorama', 'bogus', '--script', 'echo x'], capture_output=True, text=True)
check('--panorama with a bad value is refused', r.returncode != 0 and 'bad --panorama' in r.stderr, r.stderr)
r = subprocess.run(['build/biosys', '--panorama-filter', 'cubic', '--script', 'echo x'], capture_output=True, text=True)
check('--panorama-filter with a bad value is refused', r.returncode != 0 and 'bad --panorama-filter' in r.stderr, r.stderr)
h = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
check('--help lists --panorama, --panorama-filter and the dev keys F3 / F4', '--panorama original|smooth' in h and '--panorama-filter linear|nearest' in h and 'F3' in h)

# =============================================================================================== B. the mathematics (C++ unit tests)
print('--- B. mesh, seam, smoother (build/biosys-panotest)')
for mode in ('mesh', 'seam', 'smoother'):
    r = subprocess.run(['build/biosys-panotest', mode], capture_output=True, text=True)
    lines = r.stdout.splitlines()
    for l in lines:
        if l.startswith('chosen cell') or l.startswith('worst chosen') or l.startswith('cell') or re.match(r'^\s+\d+ ', l):
            print('     ' + l)
    check('biosys-panotest %s (%d checks)' % (mode, sum(1 for l in lines if l.startswith('PASS') or l.startswith('FAIL'))),
          r.returncode == 0 and not any(l.startswith('FAIL') for l in lines), r.stdout[-500:])

# =============================================================================================== C. against the oracle
print('--- C. smooth capture against the exact-cylinder bilinear oracle')


def sample_points(scale, rng):
    """about 1 pixel in 11 of the view, pseudo-random phase"""
    a, b = rng.randrange(11), rng.randrange(11)
    return [(X, Y) for Y in range(VH * scale) for X in range(VW * scale) if (X * 7 + Y * 13 + a * 3 + b) % 11 == 0]


def compare_view(path, scale, frame_rows, pal, surf, cam, pts, mask=None):
    """(mean abs error, max abs error, mean abs error of the upscaled 640-wide original) over the points; mask: overlay pixel -> skipped"""
    _, _, rows = so.read_bmp24(path)
    S = so.Sampler(surf, pal)
    n = sm = mx = up = upmx = stale = skipped = 0
    for X, Y in pts:
        if mask is not None and mask[Y // scale][X // scale]:
            skipped += 1
            continue
        o = S.pixel(cam, X, Y, scale)
        g = rows[(VY * scale) + Y][3 * X:3 * X + 3]
        q = pal[frame_rows[VY + Y // scale][X // scale]]
        e = [abs(o[k] - g[k]) for k in range(3)]
        e2 = [abs(o[k] - q[k]) for k in range(3)]
        sm += sum(e); mx = max(mx, max(e)); up += sum(e2); upmx = max(upmx, max(e2)); n += 3
        if tuple(g) == tuple(q) and max(e2) > TOL_MAX:
            stale += 1
    return sm / n, mx, up / n, upmx, stale, n // 3


PAGES = ['R4', 'T1', 'CE7A', 'bRock', 'EAngel2', 'T5']
POS = [(2400, 328), (1000, 0), (2000, 656)]    # across the x seam (view centre yaw 152), top limit, bottom limit
POSNAME = {(2400, 328): 'seam', (1000, 0): 'top', (2000, 656): 'bottom'}
rng = random.Random(1998)
tot = {2: [0, 0, 0, 0, 0], 3: [0, 0, 0, 0, 0]}   # samples, sum of means, max, upscaled mean sum, upscaled max
worst = {2: (0, ''), 3: (0, '')}
worst_mean = {2: 0, 3: 0}
cases = 0
t0 = time.time()
unc = 0
for page in PAGES:
    sc = 'page %s; surfdump %s/c_s.raw; ' % (page, T)
    for (x, y) in POS:
        sc += 'scroll %d,%d; shot %s/c_%d_%d.bmp; smoothshot %s/c_%d_%d_2.bmp 2; smoothshot %s/c_%d_%d_3.bmp 3; panoinfo; ' % (x, y, T, x, y, T, x, y, T, x, y)
    out = run(sc, '--no-objects')
    surf = readb(T + '/c_s.raw')
    infos = [l for l in out if l.startswith('panoinfo')]
    unc += sum(int(re.search(r'uncovered=(\d+)', l).group(1)) for l in out if l.startswith('smoothshot'))
    for (x, y), info in zip(POS, infos):
        cam = so.camera(x, y)
        m = re.match(r'panoinfo active=1 view=(\d+),(\d+),(\d+),(\d+) yaw=(\S+) pitch=(\S+) fov=(\S+) focal=(\S+)', info)
        ok = m and int(m.group(2)) == 56 and abs(float(m.group(5)) - cam[0]) < 1e-9 and abs(float(m.group(6)) - cam[1]) < 1e-6 and abs(float(m.group(8)) - cam[2]) < 1e-6
        check('%s %s: the engine reports the camera the oracle derives from the scroll position (yaw %.0f, pitch %.4f, focal %.3f)' % (page, POSNAME[(x, y)], cam[0], cam[1], cam[2]),
              ok, info)
        _, _, pal, frows = so.read_bmp8('%s/c_%d_%d.bmp' % (T, x, y))
        for scale in (2, 3):
            pts = sample_points(scale, rng)
            mean, mx, upm, upmx, stale, n = compare_view('%s/c_%d_%d_%d.bmp' % (T, x, y, scale), scale, frows, pal, surf, cam, pts)
            cases += 1
            t = tot[scale]
            t[0] += n; t[1] += mean * n; t[2] = max(t[2], mx); t[3] += upm * n; t[4] = max(t[4], upmx)
            worst_mean[scale] = max(worst_mean[scale], mean)
            if mx > worst[scale][0]:
                worst[scale] = (mx, '%s %s' % (page, POSNAME[(x, y)]))
            check('%s %s at %dx: %d samples, mean error %.4f (<= %.2f), max %d (<= %d); the upscaled 640-wide view: mean %.2f max %d'
                  % (page, POSNAME[(x, y)], scale, n, mean, TOL_MEAN, mx, TOL_MAX, upm, upmx),
                  mean <= TOL_MEAN and mx <= TOL_MAX and mean < upm / 10 and stale == 0, (mean, mx, upm, stale))
for scale in (2, 3):
    t = tot[scale]
    print('     %dx over %d pages x %d positions: %d samples, mean error %.4f, max %d (%s); the upscaled original: mean %.2f, max %d'
          % (scale, len(PAGES), len(POS), t[0], t[1] / t[0], t[2], worst[scale][1], t[3] / t[0], t[4]))
check('no view pixel left unpainted by the mesh in %d captures (seam, limits)' % (cases), unc == 0, unc)
print('     (%.1f s)' % (time.time() - t0))

# a full-image comparison of one view across the seam at 2x (every pixel of the view)
out = run('page R4; surfdump %s/f_s.raw; scroll 2400,328; shot %s/f.bmp; smoothshot %s/f2.bmp 2' % (T, T, T), '--no-objects')
surf = readb(T + '/f_s.raw')
_, _, pal, frows = so.read_bmp8(T + '/f.bmp')
pts = [(X, Y) for Y in range(VH * 2) for X in range(VW * 2)]
mean, mx, upm, upmx, stale, n = compare_view(T + '/f2.bmp', 2, frows, pal, surf, so.camera(2400, 328), pts)
check('R4 across the seam at 2x, EVERY view pixel (%d): mean error %.4f, max %d; upscaled original mean %.2f' % (n, mean, mx, upm), mean <= TOL_MEAN and mx <= TOL_MAX and stale == 0)

# nearest filter: the capture equals the oracle's nearest sampling except where the mesh moves a sample over a texel boundary
out = run('page R4; scroll 2400,328; smoothshot %s/n2.bmp 2 nearest' % T, '--no-objects')
_, _, nrows = so.read_bmp24(T + '/n2.bmp')
S = so.Sampler(surf, pal)
cam = so.camera(2400, 328)
diff = tot_n = 0
for X, Y in sample_points(2, rng):
    u, v = so.page_point(cam, (X + 0.5) / 2, (Y + 0.5) / 2)
    q = pal[surf[min(max(math.floor(v), 0), 1023) * 2560 + math.floor(u) % 2560]]
    tot_n += 1
    diff += tuple(nrows[112 + Y][3 * X:3 * X + 3]) != tuple(q)
check('nearest filter: %d of %d samples differ from the oracle\'s nearest pixel (only where the mesh error crosses a texel edge; < 3%%)' % (diff, tot_n), diff < 0.03 * tot_n, diff)

# SDL's software renderer (nearest only) draws the same mesh: same place up to its sampling phase
out = run('page R4; scroll 2400,328; smoothshot %s/s2.bmp 2 sdl' % T, '--no-objects')
check('smoothshot sdl: SDL_RenderGeometry through the software renderer works', any(l.startswith('smoothshot') for l in out), out)
_, _, srows = so.read_bmp24(T + '/s2.bmp')
best = None
for dy in (-1, 0, 1, 2):
    for dx in (-1, 0, 1, 2):
        s = c = 0
        for Y in range(150, 700, 7):
            for X in range(20, 1260, 7):
                a = nrows[112 + Y][3 * X:3 * X + 3]
                b = srows[112 + Y + dy][3 * (X + dx):3 * (X + dx) + 3]
                s += sum(abs(a[k] - b[k]) for k in range(3)); c += 3
        if best is None or s / c < best[0]:
            best = (s / c, dx, dy)
check('smoothshot sdl: equals the nearest CPU capture up to SDL\'s sampling phase (best shift %d,%d output pixels, mean difference %.2f <= %.1f); '
      'SDL\'s software renderer cannot filter, so the filtered capture is the CPU rasteriser' % (best[1], best[2], best[0], SDL_NEAREST_MEAN), best[0] <= SDL_NEAREST_MEAN, best)

# =============================================================================================== D. overlays
print('--- D. overlays: what is drawn over the panorama stays exactly the scaled original frame')


def upscale_rows(frows, pal, s):
    lut = [bytes(c) * s for c in pal]
    rows = []
    for r in frows:
        b = b''.join(lut[i] for i in r)
        rows.extend([b] * s)
    return rows


def overlay_case(name, script, *opts, game=False, want_over=1):
    pre = '%s/o_%s' % (T, name)
    caps = {}
    out = run(script + '; surfdump %s_s.raw; panoinfo; smoothshot %s_2.bmp 2 frame=%s_2f.bmp bare=%s_2b.bmp; smoothshot %s_3.bmp 3 frame=%s_3f.bmp bare=%s_3b.bmp; '
              'smoothshot %s_sdl.bmp 2 sdl frame=%s_sf.bmp bare=%s_sb.bmp' % ((pre,) * 10), *opts, game=game)
    info = [l for l in out if l.startswith('panoinfo')][-1]
    check('%s: the frame shows a projected panorama' % name, 'active=1' in info, info)
    surf = readb(pre + '_s.raw')
    mm = re.search(r'scroll (\d+),(\d+)', script)
    cam = so.camera(int(mm.group(1)), int(mm.group(2)))
    for scale, sfx, fsfx in ((2, '_2', '_2'), (3, '_3', '_3'), (2, '_sdl', '_s')):
        _, _, pal, frows = so.read_bmp8('%s%sf.bmp' % (pre, fsfx))
        _, _, _, brows = so.read_bmp8('%s%sb.bmp' % (pre, fsfx))
        mask = [[frows[VY + y][x] != brows[y][x] for x in range(VW)] for y in range(VH)]
        if sfx == '_2':
            nover = sum(sum(r) for r in mask)
            check('%s: %d view pixels (%.1f%%) were drawn over the panorama' % (name, nover, 100.0 * nover / (VW * VH)), nover >= want_over, nover)
        w, h, rows = so.read_bmp24('%s%s.bmp' % (pre, sfx))
        exp = upscale_rows(frows, pal, scale)
        # overlay pixels and everything outside the view: equal to the scaled original, byte for byte
        bad = cnt = 0
        for Y in range(480 * scale):
            fy = Y // scale
            if not (VY <= fy < VY + VH):
                cnt += VW * scale
                if rows[Y] != exp[Y]:
                    bad += sum(1 for x in range(VW * scale) if rows[Y][3 * x:3 * x + 3] != exp[Y][3 * x:3 * x + 3])
                continue
            mrow = mask[fy - VY]
            er, gr = exp[Y], rows[Y]
            cnt += sum(mrow) * scale
            if gr != er:
                for x in range(VW):
                    if mrow[x] and gr[3 * x * scale:3 * (x + 1) * scale] != er[3 * x * scale:3 * (x + 1) * scale]:
                        bad += 1
        check('%s at %dx%s: the %d overlay / outside-view output pixels equal the scaled original exactly (%d differ)'
              % (name, scale, ' via SDL' if sfx == '_sdl' else '', cnt, bad), bad == 0, bad)
        if sfx == '_sdl':
            continue
        # stale low-resolution panorama pixels: non-overlay view pixels that show the scaled original instead of the smooth picture
        pts = sample_points(scale, rng)
        mean, mx, upm, upmx, stale, n = compare_view('%s%s.bmp' % (pre, sfx), scale, frows, pal, surf, cam, pts, mask=mask)
        check('%s at %dx: the %d sampled non-overlay view pixels match the oracle (mean %.4f, max %d) and %d show stale low-resolution pixels'
              % (name, scale, n, mean, mx, stale), mean <= TOL_MEAN and mx <= TOL_MAX and stale == 0, (mean, mx, stale))
    return nover


overlay_case('held', 'page fce9cab_o; clickhs dropgog; page R4; scroll 1000,328; move 300,250; advance 300', want_over=500)
overlay_case('goggles', 'page fce9cab_o; clickhs dropgog; page R4; scroll 1000,328; key SPACE; advance 400; move 320,452', want_over=1000)
overlay_case('outlines', 'page R4; scroll 1000,328; hotspots on', '--no-objects', want_over=50)
overlay_case('status', 'advance 3000; page R4; scroll 1000,328; keydown TAB; advance 300', game=True, want_over=1000)
overlay_case('sprite', 'page R4; scroll 1000,328; sprite anims\\synth\\r4d.tan 100 100 0 100 fixed loop; advance 300', '--no-objects', want_over=1000)
# rain: the page filter of the new game's start page (test_rain.py)
overlay_case('rain', 'advance 500; api EndPageFilter; api BeginPageFilter 118 86 87; game rain; game rain; game rain; scroll 1000,328', game=True, want_over=100)

# =============================================================================================== E. continuous motion
print('--- E. continuous motion (display clock faked by the script)')


def trace(script, *opts):
    out = run(script, *opts)
    rows = []
    for l in out:
        m = re.match(r'smooth t=(\d+) active=1 yaw=(\S+) pitch=(\S+) ty=(\S+) tp=(\S+) fov=(\S+) snap=(\d) serial=(\d+)', l)
        if m:
            rows.append(dict(t=int(m.group(1)), yaw=float(m.group(2)), pitch=float(m.group(3)), ty=float(m.group(4)), tp=float(m.group(5)),
                             fov=float(m.group(6)), snap=int(m.group(7)), serial=int(m.group(8))))
    return rows, out


for dt in (4, 7, 17):
    for axis, post in (('x', 'postmouse 420,240'), ('y', 'postmouse 320,340'), ('x back', 'postmouse 220,240')):
        rows, out = trace('page R4; scroll 1000,300; capture on; advance 400; smoothtrace %d; %s; advance 400' % (dt, post), '--no-objects')
        # engine step interval: the times at which the target changes
        ch = [rows[i]['t'] for i in range(1, len(rows)) if (rows[i]['ty'], rows[i]['tp']) != (rows[i - 1]['ty'], rows[i - 1]['tp'])]
        # the display is sampled every dt ms, so the step times are those of the samples that first show the change: gaps are multiples of dt around 16
        step = 16
        key = 'yaw' if axis != 'y' else 'pitch'
        tkey = 'ty' if axis != 'y' else 'tp'
        vals = [r[key] for r in rows]
        sign = 1 if vals[-1] >= vals[0] else -1
        mono = all(sign * (vals[i] - vals[i - 1]) >= -1e-9 for i in range(1, len(vals)))
        over = all(sign * (r[key] - r[tkey]) <= 1e-9 for r in rows)
        # latency: never more than one engine step behind (the target of 16 ms ago)
        by_t = {r['t']: r for r in rows}
        lag = True
        for r in rows:
            prev = [q for q in rows if q['t'] <= r['t'] - step]
            if prev and sign * (r[key] - prev[-1][tkey]) < -1e-9:
                lag = False
        final = rows[-1]
        sub = sum(1 for r in rows if abs(r[key] - round(r[key])) > 1e-6 and abs(r[key] - r[tkey]) > 1e-6)
        nsteps = len(ch)
        snaps = sum(r['snap'] for r in rows)
        check('scroll %s, display every %d ms: %d engine steps %s ms apart; displayed camera monotonic, never ahead of the engine, never more than one step (16 ms) behind, '
              '%d samples between whole pixels, ends exactly on the engine camera, %d snaps (the first frame)'
              % (axis, dt, nsteps, sorted(set(b - a for a, b in zip(ch, ch[1:]))) or '-', sub, snaps),
              mono and over and lag and final[key] == final[tkey] and nsteps >= 5 and sub >= 3 and snaps == 1 and abs(vals[-1] - vals[0]) > (4 if axis == 'y' else 50), (mono, over, lag, final, nsteps, sub, snaps))

# a page change snaps; so does the first frame
rows, out = trace('page R4; smoothtrace 4; advance 40; page T1; advance 40; scroll 500,300; advance 40; page R4; advance 40')
sn = [(i, r) for i, r in enumerate(rows) if r['snap']]
ser = [r['serial'] for r in rows]
changes = [i for i in range(1, len(rows)) if ser[i] != ser[i - 1]]
check('page changes snap: %d serial changes, every one of them a snap (first frame included)' % len(changes),
      len(changes) == 2 and all(rows[i]['snap'] for i in changes) and rows[0]['snap'], [(i, rows[i]) for i in changes])
check('after a page change the displayed camera is the engine camera at once', all(rows[i]['yaw'] == rows[i]['ty'] for i in changes))
rows2, _ = trace('page R4; scroll 1000,300; smoothtrace 4; advance 40; scroll 1500,300; advance 40')
check('an API-style jump of 500 pixels snaps (no glide across the view)', rows2 and any(r['snap'] and abs(r['yaw'] - r['ty']) < 1e-9 and r['ty'] != rows2[0]['ty'] for r in rows2[1:]), rows2[:6])

# zoom animations: every frame with a field of view other than 75, and the first one back at 75, snaps
rows, out = trace('page T1; scroll 1000,300; smoothtrace 16; pumptrace on; clickhs L_T1R10; advance 100')
zin = [r for r in rows if r['fov'] != 75.0]
check('zoom-in jump: %d frames at fov %s, every one snapped' % (len(zin), sorted(set(r['fov'] for r in zin), reverse=True)[:3]),
      len(zin) == 12 and [r['fov'] for r in zin] == [72 - 3 * i for i in range(12)] and all(r['snap'] and r['yaw'] == r['ty'] for r in zin), [(r['fov'], r['snap']) for r in zin])
rows, out = trace('page CE7A; scroll 320,600; smoothtrace 16; clickhs JL_brock; advance 100')
zf = [r for r in rows if r['fov'] != 75.0]
last_zoom = max(i for i, r in enumerate(rows) if r['fov'] != 75.0)
check('zoom jump panorama -> panorama: %d frames at fov != 75 (12 zoom-in, the picture at 128, 18 zoom frames of the arrival), all snapped, and the frame after them' % len(zf),
      len(zf) >= 30 and all(r['snap'] for r in zf) and rows[last_zoom + 1]['snap'], [(r['t'], r['fov'], r['snap']) for r in rows if r['snap'] == 0 and r['fov'] != 75.0][:5])
rows, out = trace('page R10; scroll 1000,300; clickhs FR10htch_C; advance 100; smoothtrace 16; rclick 320,200; advance 100')
zo = [r for r in rows if r['fov'] != 75.0]
check('zoom-out (right-click return): %d frames at fov != 75 (39, 42 .. 72), every one snapped' % len(zo),
      len(zo) >= 11 and all(r['snap'] for r in zo), [(r['fov'], r['snap']) for r in zo])
# ordinary frames after a zoom glide again
rows, out = trace('page R4; scroll 1000,300; capture on; advance 100; smoothtrace 4; postmouse 420,240; advance 200')
check('ordinary scrolling does not snap', sum(r['snap'] for r in rows) == 1)

# =============================================================================================== F. comparison images
print('--- F. comparison images')
for page, x, y in (('R4', 1000, 328), ('bRock', 2400, 200)):
    out = run('page %s; scroll %d,%d; shot %s/cmp.bmp; smoothshot %s/cmp2.bmp 2' % (page, x, y, T, T), '--no-objects')
    _, _, pal, frows = so.read_bmp8(T + '/cmp.bmp')
    up = upscale_rows(frows, pal, 2)
    _, _, sm = so.read_bmp24(T + '/cmp2.bmp')
    rows = [up[2 * VY + i] + sm[2 * VY + i] for i in range(2 * VH)]
    so.write_bmp24('%s/smooth_%s.bmp' % (SHOTS, page), 4 * VW, 2 * VH, rows)
    check('build/shots/smooth_%s.bmp written: the same view as the original upscaled 2x (left) and smooth (right), %d x %d' % (page, 4 * VW, 2 * VH),
          os.path.getsize('%s/smooth_%s.bmp' % (SHOTS, page)) > 1000000)

print('FAILED %d' % failed if failed else 'ALL OK')
sys.exit(1 if failed else 0)
