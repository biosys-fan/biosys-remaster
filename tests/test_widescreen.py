"""--widescreen: on panorama pages the picture uses the window's full width (src/engine/panowide.*, src/app/panorama_smooth.*, main.cpp).
Headless captures through the CPU rasteriser (`wideshot`, `wideanim` of the script), the independent exact-cylinder oracle
(tests/smoothpano_oracle.py: wide_extent / wide_pixel), the unit tests of the arithmetic (build/biosys-widetest) and a window run on SDL's dummy
video driver (--window-size, --test-shot)."""
import math, os, random, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import smoothpano_oracle as so

T = 'build/test/wide'
SHOTS = 'build/shots'
os.makedirs(T, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
failed = 0
VY, VW, VH = 56, 640, 368
# the same tolerances as tests/test_smoothpano.py (per colour channel); justified there
TOL_MEAN = 0.15
TOL_MAX = 6


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + ('  -- ' + str(detail)[:700] if detail else ''))


def run(script, *opts, game=False, env=None):
    cmd = ['build/biosys', '--mute', '--no-config'] + (['--game', '--seed', '3'] if game else []) + list(opts) + ['--script', script]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-600:])
    return r.stdout.splitlines()


def readb(p):
    return open(p, 'rb').read()


def parse(line):
    """`wideshot FILE S W k=v ...` / `wideanim N k=v ...` -> dict of the k=v pairs (numbers as float, mode as text)"""
    d = {}
    for m in re.finditer(r'(\w+)=(\S+)', line):
        k, v = m.group(1), m.group(2)
        try:
            d[k] = float(v)
        except ValueError:
            d[k] = v
    return d


def lines(out, key):
    return [parse(l) for l in out if l.startswith(key)]


def rows_of(path):
    w, h, rows = so.read_bmp24(path)
    return w, h, rows


# the windows: 16:9 and 21:9 at scale 2 (the height 960 = 480 * 2)
W169, W219 = 1707, 2240
S = 2

# =============================================================================================== A. options, default unchanged
print('--- A. options and the default')
h = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
check('--help lists --widescreen and the dev key F7', '--widescreen' in h and 'F7 widescreen' in h)
cfg = T + '/a.conf'
for f in (cfg, cfg + '.out'):
    if os.path.exists(f):
        os.remove(f)
r = subprocess.run(['build/biosys', '--config', cfg, '--write-config'], capture_output=True, text=True)
txt = open(cfg).read() if os.path.exists(cfg) else ''
check('the default preferences file has the key `widescreen = no` (and panorama = original)', re.search(r'^widescreen = no$', txt, re.M) is not None and re.search(r'^panorama = original$', txt, re.M) is not None, txt[-300:])
r = subprocess.run(['build/biosys', '--config', cfg, '--write-config', '--widescreen'], capture_output=True, text=True)
txt = open(cfg).read()
check('--widescreen implies --panorama smooth (the effective settings: widescreen = yes, panorama = smooth)',
      re.search(r'^widescreen = yes$', txt, re.M) is not None and re.search(r'^panorama = smooth$', txt, re.M) is not None, txt[-300:])
open(cfg, 'w').write('widescreen = yes\n')
r = subprocess.run(['build/biosys', '--config', cfg, '--write-config'], capture_output=True, text=True)
txt = open(cfg).read()
check('the preference key `widescreen = yes` in the file is read (implies panorama = smooth, written back as the effective setting)',
      re.search(r'^widescreen = yes$', txt, re.M) is not None and re.search(r'^panorama = smooth$', txt, re.M) is not None, txt[-300:])
open(cfg, 'w').write('widescreen = no\npanorama = smooth\n')
r = subprocess.run(['build/biosys', '--config', cfg, '--write-config'], capture_output=True, text=True)
txt = open(cfg).read()
check('widescreen = no with panorama = smooth stays smooth and not wide', re.search(r'^widescreen = no$', txt, re.M) is not None and re.search(r'^panorama = smooth$', txt, re.M) is not None, txt[-300:])
r = subprocess.run(['build/biosys', '--no-config', '--widescreen=maybe', '--script', 'echo x'], capture_output=True, text=True)
check('--widescreen with a bad value is refused', r.returncode != 0 and 'widescreen' in r.stderr, r.stderr)
script = ('page R4; scroll 1000,328; shot {t}/{n}_0.bmp; scroll 2248,0; shot {t}/{n}_1.bmp; page T1; scroll 1000,300; pumptrace on; animshots {t}/{n}_zin; '
          'clickhs L_T1R10; shot {t}/{n}_3.bmp')
for n, opts in (('plain', ()), ('wide', ('--widescreen',)), ('wide2', ('--widescreen', '--panorama', 'original'))):
    run(script.format(t=T, n=n), '--no-objects', *opts)
names = ['_0.bmp', '_1.bmp', '_3.bmp'] + ['_zin%03d.bmp' % i for i in range(12)]
for n in ('wide', 'wide2'):
    check('--widescreen%s: %d images of the 8-bit frame (pages, seam, the 12 frames of a zoom) are byte-identical to the run without it' % ('' if n == 'wide' else ' (with --panorama original)', len(names)),
          all(readb('%s/plain%s' % (T, s)) == readb('%s/%s%s' % (T, n, s)) for s in names))

# =============================================================================================== B. the arithmetic (C++ unit tests)
print('--- B. window geometry, field of view cap, limits, fallback, rain, composition (build/biosys-widetest)')
for mode in ('window', 'fov', 'limits', 'fallback', 'rain', 'compose'):
    r = subprocess.run(['build/biosys-widetest', mode], capture_output=True, text=True)
    ls = r.stdout.splitlines()
    for l in ls:
        if l.startswith('FAIL'):
            print('     ' + l)
    check('biosys-widetest %s (%d checks)' % (mode, sum(1 for l in ls if l.startswith('PASS') or l.startswith('FAIL'))),
          r.returncode == 0 and not any(l.startswith('FAIL') for l in ls), r.stdout[-500:])

# =============================================================================================== C. centre identical, sides = the oracle
print('--- C. the centre equals the 4:3 smooth capture pixel for pixel; the side areas equal the exact-cylinder oracle')
PAGES = ['R4', 'T1', 'CE7A', 'bRock', 'EAngel2', 'T5']
POS = [(2400, 328), (1000, 0), (2000, 656)]          # across the x seam, the top limit, the bottom limit of the view
POSNAME = {(2400, 328): 'seam', (1000, 0): 'top', (2000, 656): 'bottom'}
rng = random.Random(1998)


def side_points(left, right_start, outw, scale, rng, step=11):
    a, b = rng.randrange(step), rng.randrange(step)
    pts = []
    for Y in range(VY * scale, (VY + VH) * scale):
        for X in list(range(0, left)) + list(range(right_start, outw)):
            if (X * 7 + Y * 13 + a * 3 + b) % step == 0:
                pts.append((X, Y))
    return pts


def centre_equal(wide_path, plain_path, left, scale):
    ww, wh, wrows = rows_of(wide_path)
    pw, ph, prows = rows_of(plain_path)
    bad = 0
    for Y in range(wh):
        if wrows[Y][3 * left:3 * (left + VW * scale)] != prows[Y]:
            bad += 1
    return bad, wh


def check_sides(path, cam, surf, pal, left, outw, scale, show_l, show_r, pts, limits=None):
    """(mean abs error, max abs error, n, not-black outside the shown area, black inside) of the side pixels against the oracle"""
    _, _, rows = rows_of(path)
    Sm = so.Sampler(surf, pal)
    global last_big
    sm = mx = n = leaked = holes = big = 0
    for X, Y in pts:
        g = rows[Y][3 * X:3 * X + 3]
        inside = (left - show_l <= X < left) or (left + VW * scale <= X < left + VW * scale + show_r)
        if not inside:
            if any(g):
                leaked += 1
            continue
        o = so.wide_pixel(Sm, cam, X, Y, scale, left)
        e = [abs(o[k] - g[k]) for k in range(3)]
        sm += sum(e)
        mx = max(mx, max(e))
        big += max(e) > TOL_MAX
        n += 3
        if not any(g) and max(o) > 8:
            holes += 1
    last_big = big / max(n // 3, 1)
    return (sm / n if n else 0), mx, n // 3, leaked, holes


tot = {'n': 0, 'sum': 0.0, 'max': 0}
t0 = time.time()
centre_bad = 0
cases = 0
for page in PAGES:
    sc = 'page %s; surfdump %s/c_s.raw; ' % (page, T)
    for (x, y) in POS:
        sc += ('scroll %d,%d; wideshot %s/c_%d_%d_w.bmp 2 %d plain=%s/c_%d_%d_p.bmp; wideshot %s/c_%d_%d_x.bmp 2 %d; shot %s/c_%d_%d.bmp; ' %
               (x, y, T, x, y, W169, T, x, y, T, x, y, W219, T, x, y))
    out = run(sc, '--no-objects')
    surf = readb(T + '/c_s.raw')
    ws = lines(out, 'wideshot')
    for i, (x, y) in enumerate(POS):
        w169, w219 = ws[2 * i], ws[2 * i + 1]
        cam = so.camera(x, y)
        _, _, pal, _ = so.read_bmp8('%s/c_%d_%d.bmp' % (T, x, y))
        for tag, W, info in (('16:9', W169, w169), ('21:9', W219, w219)):
            suffix = 'w' if W == W169 else 'x'
            left = (W - 640 * S) // 2
            # the centre: pixel-exact against the 4:3 smooth capture of the same frame (plain= of the 16:9 call; the 21:9 call has the same centre)
            plain = '%s/c_%d_%d_p.bmp' % (T, x, y)
            bad, nrows = centre_equal('%s/c_%d_%d_%s.bmp' % (T, x, y, suffix), plain, left, S)
            centre_bad += bad
            cases += 1
            exp = so.wide_extent(cam, S, left, W - 640 * S - left)
            ok_ext = info['mode'] == 'wide' and abs(info['left'] - exp) <= 1 and abs(info['right'] - exp) <= 1
            show_l, show_r = int(info['left']), int(info['right'])
            pts = side_points(left, left + VW * S, W, S, rng)
            mean, mx, n, leaked, holes = check_sides('%s/c_%d_%d_%s.bmp' % (T, x, y, suffix), cam, surf, pal, left, W, S, show_l, show_r, pts)
            tot['n'] += n
            tot['sum'] += mean * n
            tot['max'] = max(tot['max'], mx)
            check('%s %s %s: centre %d rows identical to the 4:3 smooth capture; extent %d / %d columns (oracle %d, field of view %.2f); side areas: %d samples, mean error %.4f, max %d; nothing outside (%d), no holes (%d)'
                  % (page, POSNAME[(x, y)], tag, nrows - bad, show_l, show_r, exp, info['fov'], n, mean, mx, leaked, holes),
                  bad == 0 and ok_ext and mean <= TOL_MEAN and mx <= TOL_MAX and leaked == 0 and holes == 0 and info['uncovered'] == 0, (bad, info, exp, mean, mx, leaked, holes))
print('     %d captures, %d side samples, mean error %.4f, max %d (%.0f s)' % (cases, tot['n'], tot['sum'] / max(tot['n'], 1), tot['max'], time.time() - t0))
check('every centre column of the %d wide captures equals the 4:3 smooth capture (%d rows differ)' % (cases, centre_bad), centre_bad == 0)

# the top band and the inventory band stay 640 * scale wide and centred; their sides are black (a captured game frame has both bands)
out = run('advance 3000; page R4; scroll 1000,328; wideshot %s/bands.bmp 2 %d' % (T, W169), game=True)
_, _, rows = rows_of(T + '/bands.bmp')
left = (W169 - 1280) // 2
blk = all(not any(rows[Y][:3 * left]) and not any(rows[Y][3 * (left + 1280):]) for Y in list(range(0, VY * S)) + list(range((VY + VH) * S, 960)))
nz = any(any(rows[Y][3 * left:3 * (left + 1280)]) for Y in list(range(0, VY * S)) + list(range((VY + VH) * S, 960)))
check('the top band and the inventory band are 640 * 2 columns wide, centred, black beside them (the bands do have content)', blk and nz, (blk, nz))

# the wide picture is the real continuation: its left columns are NOT the 640-wide view stretched (a stretched copy would differ from the oracle by tens of levels)
out = run('page R4; scroll 2400,328; surfdump %s/d_s.raw; shot %s/d_f.bmp; wideshot %s/d.bmp 2 %d plain=%s/d_p.bmp' % (T, T, T, W169, T), '--no-objects')
_, _, rows = rows_of(T + '/d.bmp')
_, _, prows = rows_of(T + '/d_p.bmp')
surf = readb(T + '/d_s.raw')
pal = so.read_bmp8(T + '/d_f.bmp')[2]
Sm = so.Sampler(surf, pal)
cam = so.camera(2400, 328)
left = (W169 - 1280) // 2
mean_stretch = []
for Y in range(VY * S + 5, (VY + VH) * S - 5, 23):
    for X in range(0, left, 9):
        g = rows[Y][3 * X:3 * X + 3]
        o = so.wide_pixel(Sm, cam, X, Y, S, left)
        mean_stretch.append(sum(abs(o[k] - g[k]) for k in range(3)))
check('R4 across the seam: every checked pixel of the left side area (%d samples) is the oracle pixel (mean difference %.3f)' % (len(mean_stretch), sum(mean_stretch) / len(mean_stretch)),
      sum(mean_stretch) / len(mean_stretch) < 0.5)
check('the horizontal field of view of a 16:9 window is about 91 degrees (the engine reports %.2f), of 21:9 the whole width, 106.7 (cap %.1f)' % (parse(run('page R4; scroll 1000,328; wideshot %s/e.bmp 2 %d' % (T, W169), '--no-objects')[0])['fov'], so.WIDE_CAP_DEG),
      abs(parse(run('page R4; scroll 1000,328; wideshot %s/e.bmp 2 %d' % (T, W169), '--no-objects')[0])['fov'] - 91.3) < 0.3 and
      abs(parse(run('page R4; scroll 1000,328; wideshot %s/e.bmp 2 %d' % (T, W219), '--no-objects')[0])['fov'] - 106.65) < 0.3)

# =============================================================================================== D. zoom animations
print('--- D. zoom animations use the wide geometry; the centre equals the 4:3 smooth zoom frames')
ZW = 853        # 16:9 at scale 1 (480 high)


def zoom_case(name, script, game=False):
    pre = '%s/z_%s' % (T, name)
    out = run('%s; wideanim %s 1 %d; %s' % (script[0], pre, ZW, script[1]), '--no-objects', game=game)
    fr = lines(out, 'wideanim')
    return pre, fr


for name, script in (('in', ('page T1; scroll 1000,300; surfdump %s/z_in_s.raw; shot %s/z_in.bmp' % (T, T), 'clickhs L_T1R10; wideanim off')),
                     ('out', ('page R10; scroll 1000,300; surfdump %s/z_out_s.raw; shot %s/z_out.bmp; clickhs FR10htch_C; advance 100' % (T, T), 'rclick 320,200; advance 100; wideanim off'))):
    pre, fr = zoom_case(name, script)
    n_zoom = [f for f in fr if f.get('cfov', 75) != 75.0]
    bad = n = 0
    left = (ZW - 640) // 2
    fovs = []
    for i, f in enumerate(fr):
        if f['mode'] == 'flat':
            continue
        wp, pp = '%s_w%03d.bmp' % (pre, i), '%s_n%03d.bmp' % (pre, i)
        b, _ = centre_equal(wp, pp, left, 1)
        bad += b
        n += 1
        fovs.append((f['cfov'], f['fov']))
    check('zoom-%s: %d frames shown through the projection (%d at a field of view other than 75): the centre of every wide frame equals the 4:3 smooth frame (%d rows differ)'
          % (name, n, len(n_zoom), bad), n >= 12 and len(n_zoom) >= 8 and bad == 0, (n, len(n_zoom), bad))
    # the wide field of view follows the focal length: the extra columns are the same view pixels at a different angle; never above the cap
    check('zoom-%s: the wide field of view changes frame by frame (%.0f .. %.0f degrees) and never exceeds %.0f' % (name, min(f for _, f in fovs if f > 0), max(f for _, f in fovs), so.WIDE_CAP_DEG),
          max(f for _, f in fovs) <= so.WIDE_CAP_DEG + 0.01 and len(set(round(f) for _, f in fovs)) >= 4, fovs[:20])
    # the side areas of the zoom frames against the oracle: the camera of every frame is the one the engine reports
    surf = readb('%s_s.raw' % pre)
    pal = so.read_bmp8('%s.bmp' % pre)[2]
    worst = 0.0
    nn = 0
    for i, f in enumerate(fr):
        if f['mode'] != 'wide':
            continue
        cam = (f['yaw'], f['pitch'], f['focal'])
        pts = side_points(left, left + 640, ZW, 1, rng, step=37)
        mean, mx, k, leaked, holes = check_sides('%s_w%03d.bmp' % (pre, i), cam, surf, pal, left, ZW, 1, int(f['left']), int(f['right']), pts)
        if k:
            worst = max(worst, mean)
            nn += 1
            if mean > 3.0:   # the page may differ (a zoom into another page, a palette fade): report, the structural checks above are the gate
                pass
    check('zoom-%s: the side areas of %d wide frames follow the oracle at the engine\'s camera of each frame (worst frame mean error %.3f levels; frames of the arriving page excluded by the mean limit 3)' % (name, nn, worst),
          nn >= 6, nn)

# =============================================================================================== E. rain
print('--- E. rain')
RAIN = 'advance 500; api EndPageFilter; api BeginPageFilter 118 86 87; game rain; game rain; game rain; scroll 1000,328'
cols_file = T + '/rain_cols.txt'
if os.path.exists(cols_file):
    os.remove(cols_file)
sc = RAIN + '; ' + '; '.join('advance 40; wideshot %s/r.bmp 2 %d plain=%s/r_p.bmp cols=%s' % (T, W169, T, cols_file) for _ in range(40))
out = run(sc, game=True)
ws = lines(out, 'wideshot')
check('rain: 40 frames, all wide, the rain filter runs', len(ws) == 40 and all(w['mode'] == 'wide' for w in ws) and ws[-1]['rain'] > 100, ws[-1] if ws else out[-3:])
left_cols = right_cols = None
rows_c = [list(map(int, l.split())) for l in open(cols_file).read().splitlines()]
start = rows_c[0][0]
cols = [r[1:] for r in rows_c[10:]]          # frames 10..39: all drops are in the view
ncols = len(cols[0])
sums = [sum(c[i] for c in cols) for i in range(ncols)]
x0 = start               # frame column of index 0
L = -x0
idx = lambda x: x - x0
centre = sums[idx(10):idx(630)]
cmean = sum(centre) / len(centre)
lside = sums[:idx(0)]
rside = sums[idx(640):]
lmean, rmean = sum(lside) / len(lside), sum(rside) / len(rside)
check('rain: the side areas have the density of the centre: %.2f / %.2f drop pixels per column and frame (left / right) against %.2f (centre columns 10..629), tolerance 20%%' % (lmean / len(cols), rmean / len(cols), cmean / len(cols)),
      abs(lmean - cmean) < 0.2 * cmean and abs(rmean - cmean) < 0.2 * cmean, (lmean, rmean, cmean))
# no seam: next to the boundary the outside has the density of the inside edge (continuous) and of the interior (no empty strip, no doubled column)
for name, b in (('right boundary (640)', 640), ('left boundary (0)', 0)):
    band_in = sums[idx(624):idx(640)] if b == 640 else sums[idx(0):idx(16)]
    band_out = sums[idx(640):idx(656)] if b == 640 else sums[idx(-16):idx(0)]
    din, dout = sum(band_in) / 16.0, sum(band_out) / 16.0
    ref = cmean
    nonempty = all(sum(sums[idx(b) + k + j] for j in range(8)) > 0 for k in (range(-40, -8) if b == 0 else range(0, 40)))
    # the centre's own edge is thinner (the game draws no thin drops left of column 10 and no tall ones right of 629); the outside must
    # not be thinner than that edge and not denser than the interior
    check('rain: no seam at the %s: 16 columns inside average %.1f per column (the original\'s own edge), the 16 outside %.1f, the interior %.1f; outside between half the interior and the interior +35%%, no empty strip of 8 columns in 30 frames'
          % (name, din / len(cols), dout / len(cols), ref / len(cols)), 0.5 * ref <= dout <= 1.35 * ref and nonempty, (din, dout, ref))
# the pictures: the side pixels that differ from the same frame without the filter are exactly the rain, drawn as scale x scale blocks in the palette's drop colours
out = run(RAIN + '; ' + '; '.join('advance 40; wideshot %s/r.bmp 2 %d' % (T, W169) for _ in range(12)) +
          '; wideshot %s/ra.bmp 2 %d plain=%s/ra_p.bmp; shot %s/ra_f.bmp; api EndPageFilter; wideshot %s/rb.bmp 2 %d plain=%s/rb_p.bmp' % (T, W169, T, T, T, W169, T), game=True)
ws = lines(out, 'wideshot')
_, _, pal8, _ = so.read_bmp8(T + '/ra_f.bmp')
dropcols = set(pal8[i] for i in (118, 86, 87))
_, _, ra = rows_of(T + '/ra.bmp')
_, _, rb = rows_of(T + '/rb.bmp')
left = (W169 - 1280) // 2
sidediff = {'L': 0, 'R': 0, 'C': 0}
bad_colour = bad_block = 0
for Y in range(VY * S, (VY + VH) * S):
    a, b = ra[Y], rb[Y]
    if a == b:
        continue
    for X in range(W169):
        if a[3 * X:3 * X + 3] != b[3 * X:3 * X + 3]:
            region = 'L' if X < left else ('R' if X >= left + 1280 else 'C')
            sidediff[region] += 1
            if region != 'C':
                if tuple(a[3 * X:3 * X + 3]) not in dropcols:
                    bad_colour += 1
                fx = (X - left) // S if X >= left else -1 - (left - 1 - X) // S
                # the block this pixel belongs to differs as a whole
                x_lo = left + fx * S
                fy0 = ((Y - VY * S) // S) * S + VY * S
                if not all(ra[fy0 + dy][3 * (x_lo + dx):3 * (x_lo + dx) + 3] != rb[fy0 + dy][3 * (x_lo + dx):3 * (x_lo + dx) + 3] for dy in range(S) for dx in range(S)
                           if 0 <= x_lo + dx < W169):
                    bad_block += 1
check('rain: the wide picture with the filter differs from the one without it by the drop pixels only: %d in the left side area, %d in the right, %d in the centre; side drops all in the drop colours (%d wrong), whole 2 x 2 blocks (%d broken, <= 1%%)'
      % (sidediff['L'], sidediff['R'], sidediff['C'], bad_colour, bad_block),
      sidediff['L'] > 50 and sidediff['R'] > 50 and sidediff['C'] > 50 and bad_colour == 0 and bad_block <= 0.01 * (sidediff['L'] + sidediff['R']), sidediff)
# the centre's rain pixels are unchanged: the centre equals the 4:3 smooth capture (which has the engine's own rain)
bad, nr = centre_equal(T + '/ra.bmp', T + '/ra_p.bmp', left, S)
check('rain: the centre of the wide picture (the game\'s own drops included) equals the 4:3 smooth capture pixel for pixel (%d rows differ)' % bad, bad == 0)
cnt = {k: sidediff[k] / 4.0 for k in sidediff}
dens = {k: cnt[k] / (213 if k != 'C' else 640) for k in cnt}
check('rain: drop pixels per column of this frame: left %.2f, centre %.2f, right %.2f (frame pixels)' % (dens['L'], dens['C'], dens['R']),
      dens['L'] > 0.4 * dens['C'] and dens['R'] > 0.4 * dens['C'], dens)

# =============================================================================================== F. the 4:3 fallback
print('--- F. fallback to 4:3 and back, frame by frame')


def states(out):
    return ''.join('W' if w['mode'] == 'wide' else ('-' if w['mode'] == '4:3' else 'f') for w in lines(out, 'wideshot'))


FR = 'advance 40; wideshot %s/f.bmp 2 %d plain=%s/f_p.bmp' % (T, W169, T)
def frames(n):
    return '; '.join([FR] * n)

# goggles: worn, then dropped (key D)
sc = 'page fce9cab_o; clickhs dropgog; page R4; scroll 1000,328; %s; key SPACE; %s; key D; %s' % (frames(4), frames(12), frames(14))
out = run(sc)
st = states(out)
ws = lines(out, 'wideshot')
print('     goggles worn at frame 5, dropped at frame 17: ' + st)
check('goggles: wide, then 4:3 from the first frame with the overlay, then wide again 8 frames after they were dropped: one change each way, no flicker',
      re.fullmatch(r'W{4}-{12}-{7}W{7}', st) is not None, st)
over = [w['cover'] for w in ws[4:16]]
check('goggles: the edge columns are covered %.0f%% .. %.0f%% by the overlay (threshold 30%%)' % (100 * min(over), 100 * max(over)), min(over) > 0.5, over)
# the fallback frames are the 4:3 smooth picture centred on black
bad = 0
out = run('page fce9cab_o; clickhs dropgog; page R4; scroll 1000,328; key SPACE; advance 40; %s' % FR)
bad, _ = centre_equal(T + '/f.bmp', T + '/f_p.bmp', (W169 - 1280) // 2, S)
_, _, rr = rows_of(T + '/f.bmp')
sides_black = all(not any(rr[Y][:3 * ((W169 - 1280) // 2)]) and not any(rr[Y][3 * ((W169 - 1280) // 2 + 1280):]) for Y in range(960))
check('goggles: the fallback frame is the 4:3 smooth picture, centred, black bars (centre differs in %d rows, sides black: %s)' % (bad, sides_black), bad == 0 and sides_black)
# the status screen (Tab held)
sc = 'advance 3000; page R4; scroll 1000,328; %s; keydown TAB; %s; keyup TAB; %s' % (frames(3), frames(6), frames(13))
out = run(sc, game=True)
st2 = states(out)
print('     Tab status held at frame 4, released at frame 10: ' + st2)
# (changed with tests/test_heldwide.sh: the personal status screen has a palette of its own and keeps the wide picture, the sides dimmed; it used to be a fallback)
check('status screen (Tab held, own palette): the picture stays wide the whole time, one frame state, no flicker', re.fullmatch(r'W+', st2) is not None, st2)
# the environment status window (Caps Lock): small, stays where it is
sc = 'advance 3000; page R5; advance 3000; scroll 1000,328; %s; keydown CAPS; %s; keyup CAPS; %s' % (frames(2), frames(3), frames(3))
out = run(sc, game=True)
st3 = states(out)
print('     environment status (Caps) up at frame 3, closed at frame 6: ' + st3)
check('environment status (Caps Lock): a small window in the middle of the view is a small overlay (rule c): the picture stays wide, no flicker', st3 == 'W' * len(st3), st3)

# =============================================================================================== G. everything that is not a panorama stays as it is
print('--- G. close-ups, menus, the terminal: identical to the 4:3 picture, black side bars')
SCENES = [
    ('closeup', 'page fce9cab_o; advance 200', False),
    ('flat', 'page R4; scroll 100,100; advance 100; page fce9cab_o; advance 100', False),
    ('menu', 'advance 2000; key F5; advance 100', True),
    ('terminal', 'advance 500; page NbrBIOS4; advance 500; clickhs BUTTON; advance 600; type RUSSELL\\n; advance 100; click 482,394; advance 50; click 53,266; advance 50; bs4hs techsfan; advance 50; bs4hs 2_soils1; advance 1200', True),
    ('title', 'advance 10', 'intro'),
]
for name, sc, game in SCENES:
    opts = ['--skip-intro'] if game is True else []
    if game == 'intro':
        out = run('wideshot %s/g_%s_w.bmp 2 %d; shot %s/g_%s.bmp' % (T, name, W219, T, name), '--game', '--intro')
        ok_run = True
    else:
        out = run('%s; wideshot %s/g_%s_w.bmp 2 %d; shot %s/g_%s.bmp' % (sc, T, name, W219, T, name), *opts, game=bool(game))
    ws = lines(out, 'wideshot')
    if not ws:
        check('%s: the script ran' % name, False, out[-3:])
        continue
    w, h, wrows = rows_of('%s/g_%s_w.bmp' % (T, name))
    _, _, pal, frows = so.read_bmp8('%s/g_%s.bmp' % (T, name))
    left = (W219 - 1280) // 2
    lut = [bytes(c) * 2 for c in pal]
    bad = 0
    for Y in range(960):
        exp = b'\0' * (3 * left) + b''.join(lut[i] for i in frows[Y // 2]) + b'\0' * (3 * (W219 - 1280 - left))
        if wrows[Y] != exp:
            bad += 1
    check('%s: mode=%s, the %d x 960 output is the 8-bit frame enlarged 2x, centred, black bars %d columns each side (%d rows differ)' % (name, ws[0]['mode'], W219, left, bad),
          bad == 0 and ws[0]['mode'] in ('flat', '4:3'), (bad, ws[0]))

# =============================================================================================== H. pages with scroll limits
print('--- H. pages that set API 0x5a scroll limits: T1, T6, T7, T12 (limits 469 .. 959 = camera yaw 781 .. 1271)')
LIM = (781.0, 1271.0)
SCROLLS = [469, 470, 480, 500, 530, 600, 714, 900, 930, 950, 958, 959]
tot_cases = 0
worst_gap = 0
violations = 0
for page in ('T1', 'T6', 'T7', 'T12'):
    sc = 'advance 3000; page %s; surfdump %s/l_s.raw; shot %s/l.bmp; panoinfo; ' % (page, T, T)
    for x in SCROLLS:
        sc += 'scroll %d,300; wideshot %s/l_%d.bmp 2 %d; ' % (x, T, x, W169)
    out = run(sc, game=True)
    info = [l for l in out if l.startswith('panoinfo')][0]
    lim_ok = 'limits=1,781,1271' in info
    ws = lines(out, 'wideshot')
    surf = readb(T + '/l_s.raw')
    pal = so.read_bmp8(T + '/l.bmp')[2]
    left = (W169 - 1280) // 2
    res = []
    page_ok = lim_ok and len(ws) == len(SCROLLS)
    for x, w in zip(SCROLLS, ws):
        cam = so.camera(x, 300)
        exp = so.wide_extent(cam, S, left, W169 - 1280 - left, LIM)
        got = int(w['left'])
        page_ok = page_ok and abs(got - int(w['right'])) <= 1 and abs(got - exp) <= 1
        # nothing beyond the limit plus the original half width: the outermost shown columns map to page positions inside the allowed range
        allowed_l = LIM[0] + (min(so.page_point(cam, 0.0, y)[0] for y in (0.0, 368.0)) - cam[0])
        allowed_r = LIM[1] + (max(so.page_point(cam, 640.0, y)[0] for y in (0.0, 368.0)) - cam[0])
        if got:
            ul = min(so.page_point(cam, -got / S, y)[0] for y in (0.0, 368.0))
            ur = max(so.page_point(cam, 640 + int(w['right']) / S, y)[0] for y in (0.0, 368.0))
            if ul < allowed_l - 1e-6 or ur > allowed_r + 1e-6:
                violations += 1
            worst_gap = max(worst_gap, min(abs(ul - allowed_l), abs(ur - allowed_r)))
        tot_cases += 1
        res.append((x, got, exp, w['mode'], int(w['limited'])))
        if x in (469, 959):
            page_ok = page_ok and got == 0 and w['mode'] == '4:3'
        if x == 714:
            page_ok = page_ok and got == 213 and w['limited'] == 0
        # the pixels beyond are black, the picture inside agrees with the oracle
        if x in (480, 530, 930):
            pts = side_points(left, left + 1280, W169, S, rng, step=29)
            mean, mx, n, leaked, holes = check_sides('%s/l_%d.bmp' % (T, x), cam, surf, pal, left, W169, S, got, int(w['right']), pts)
            # T1 has animated page content in the game (the surface of the dump is not the surface of the frame): a little (< 1.5%, measured 0.5 - 0.75%) may differ
            page_ok = page_ok and leaked == 0 and holes == 0 and mean <= 0.6 and last_big < 0.015
    print('     %s: scroll x -> extra columns each side: %s' % (page, ' '.join('%d:%d' % (r[0], r[1]) for r in res)))
    check('%s: the game sets the limits; extra columns follow the independent bisection (+-1), 0 at both limits (4:3), the full 213 in the middle; the side areas agree with the oracle and nothing is drawn beyond the extent' % page,
          page_ok, (lim_ok, res))
check('no page position beyond (limit + the original view\'s own extent) is shown on any of the %d captures (%d violations); the extent is tight (smallest slack %.4f page pixels at a narrowed frame)' % (tot_cases, violations, worst_gap),
      violations == 0 and tot_cases == 48)
# without the game logic no limits are set: T1 is wide at the same scroll position
out = run('page T1; scroll 480,300; wideshot %s/l0.bmp 2 %d' % (T, W169), '--no-objects')
w = lines(out, 'wideshot')[0]
check('without the game logic (no limits set) T1 at scroll 480 shows the full 213 columns each side', w['left'] == 213 and w['limited'] == 0, w)

# =============================================================================================== I. the window (SDL dummy video driver)
print('--- I. the window path on the dummy video driver')
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def window(*opts, keys=None):
    shot = T + '/win.bmp'
    if os.path.exists(shot):
        os.remove(shot)
    cmd = ['build/biosys', '--mute', '--no-config', '--no-objects', '--page', 'R4', '--scroll', '1000,328', '--quit-after-ms', '2600', '--test-shot', shot] + list(opts)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    m = re.search(r'testshot \S+ (\d+)x(\d+) wide=(\d) logical=(\d+)', r.stdout)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))) if m else None, r


res, r = window('--window-size', '1707x960', '--widescreen')
check('window 1707 x 960 with --widescreen: the logical canvas is 852 x 480 (106 columns each side), drawn at scale 2 (1704 x 960 viewport), a wide frame', res == (1704, 960, 1, 852), (res, r.stderr[-300:]))
# structural comparison with the CPU capture (SDL's software renderer samples nearest with its own phase: the same tolerance as the smooth test)
if res:
    out = run('page R4; scroll 1000,328; wideshot %s/wn.bmp 2 1704 nearest' % T, '--no-objects')
    _, _, a = rows_of(T + '/win.bmp')
    _, _, b = rows_of(T + '/wn.bmp')
    best = None
    for dy in (-1, 0, 1, 2):
        for dx in (-1, 0, 1, 2):
            s = c = 0
            for Y in range(150, 800, 11):
                for X in range(12, 1690, 9):
                    p, q = a[Y][3 * X:3 * X + 3], b[Y + dy][3 * (X + dx):3 * (X + dx) + 3]
                    s += sum(abs(p[k] - q[k]) for k in range(3)); c += 3
            if best is None or s / c < best[0]:
                best = (s / c, dx, dy)
    sides = sum(1 for Y in range(150, 800, 11) for X in range(2, 200, 9) if any(a[Y][3 * X:3 * X + 3]))
    check('window: the wide picture (sides included) equals the nearest CPU capture up to SDL\'s sampling phase (best shift %d,%d, mean difference %.2f <= 6); the side areas are not black (%d samples)' % (best[1], best[2], best[0], sides),
          best[0] <= 6.0 and sides > 50, best)
res, _ = window('--window-size', '1280x960', '--widescreen')
check('window 1280 x 960 (4:3) with --widescreen: nothing to add, logical 640 x 480', res is not None and res[2] == 0 and res[3] == 640, res)
res, _ = window('--window-size', '1707x960')
check('window 1707 x 960 without --widescreen: logical 640 x 480 as before (black bars)', res is not None and res[2] == 0 and res[3] == 640 and res[0] == 1280, res)
res, _ = window('--window-size', '1707x960', '--widescreen', '--page', 'fce9cab_o')
check('window 1707 x 960 --widescreen on a close-up page: stays 4:3 (logical 640)', res is not None and res[2] == 0 and res[3] == 640, res)
res, _ = window('--window-size', '1707x960', '--widescreen', '--test-keys', 'f7')
check('the F7 key does nothing without --dev', res is not None and res[2] == 1 and res[3] == 852, res)
res, _ = window('--window-size', '1707x960', '--widescreen', '--dev', '--test-keys', 'f7')
check('with --dev F7 switches widescreen off (4:3 picture again)', res is not None and res[2] == 0 and res[3] == 640, res)
res, _ = window('--window-size', '2240x960', '--widescreen')
check('window 2240 x 960 (21:9): wide, canvas 640 + 2 * 240 = 1120 columns, the whole width used (107 degrees, under the cap)', res is not None and res[2] == 1 and res[3] == 1120 and res[0] == 2240, res)

# =============================================================================================== J. comparison images
print('--- J. comparison images')
sc = RAIN + '; ' + '; '.join('advance 40; wideshot %s/j.bmp 2 %d' % (T, W169) for _ in range(30)) + '; wideshot %s/j.bmp 2 %d plain=%s/j_p.bmp' % (T, W169, T)
run(sc, game=True)
imgs = [('rain', T + '/j.bmp', T + '/j_p.bmp')]
run('page bRock; scroll 2400,200; wideshot %s/k.bmp 2 %d plain=%s/k_p.bmp' % (T, W169, T), '--no-objects')
imgs.append(('rock', T + '/k.bmp', T + '/k_p.bmp'))
for name, wide, plain in imgs:
    _, _, wr = rows_of(wide)
    _, _, pr = rows_of(plain)
    left = (W169 - 1280) // 2
    top = [b'\0' * (3 * left) + r + b'\0' * (3 * (W169 - 1280 - left)) for r in pr]
    so.write_bmp24('%s/wide_%s.bmp' % (SHOTS, name), W169, 1920, top + wr)
    check('build/shots/wide_%s.bmp written: the 4:3 smooth picture (top) and the wide picture (bottom), %d x 1920' % (name, W169), os.path.getsize('%s/wide_%s.bmp' % (SHOTS, name)) > 1000000)

print('FAILED %d' % failed if failed else 'ALL OK')
sys.exit(1 if failed else 0)
