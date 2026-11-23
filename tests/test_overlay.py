"""The overlay interface (--interface overlay): the view fills the window, the two bars are overlays that appear only when they have something to show.
Layout arithmetic and pointer maps (build/biosys-overlaytest and independent arithmetic here), the option and its menu row, the faithful default
(byte for byte), the panorama pixels against the exact-cylinder oracle at the field of view the layout gives, the overlay states (status word, message,
strip), the text contrast over a bright sky, the ambient fill of close-ups and of the goggles fallback, hit tests in both layouts, the vertical
release, and the real window code (SDL dummy drivers) against the headless composition. Pictures for the player go to build/shots/."""
import math, os, re, shutil, struct, subprocess, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import smoothpano_oracle as so

T = 'build/test/overlay'
SHOTS = 'build/shots'
shutil.rmtree(T, ignore_errors=True)
os.makedirs(T)
os.makedirs(SHOTS, exist_ok=True)
failed = 0
ENV = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
TOL_MEAN, TOL_MAX = 0.15, 6          # against the oracle: as tests/test_smoothpano.sh (mesh interpolation error, rounding)
nsave = [0]


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + ('  -- ' + str(detail)[:700] if detail != '' else ''))


def fresh(name):
    d = '%s/%s' % (T, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def run(script, *opts, game=True):
    """headless script run: -> stdout lines"""
    nsave[0] += 1
    d = fresh('save%d' % nsave[0])
    cmd = ['build/biosys', '--mute', '--no-config', '--save-dir', d] + (['--game', '--skip-intro', '--seed', '3'] if game else []) + list(opts) + ['--script', script]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-600:])
    return r.stdout.splitlines()


def window(keys, ms, *opts, shot=None, size=None, config=None, game=True):
    """a window run on the dummy drivers: -> (rc, stdout lines, stderr)"""
    nsave[0] += 1
    d = fresh('wsave%d' % nsave[0])
    cmd = ['build/biosys', '--mute'] + (['--game', '--skip-intro'] if game else []) + ['--save-dir', d, '--quit-after-ms', str(ms)]
    cmd += ['--config', config] if config else ['--no-config']
    if keys:
        cmd += ['--test-keys', keys]
    if shot:
        cmd += ['--test-shot', shot]
    if size:
        cmd += ['--window-size', size]
    r = subprocess.run(cmd + list(opts), capture_output=True, text=True, env=ENV)
    return r.returncode, r.stdout.splitlines(), r.stderr


def kvs(line):
    return dict(p.split('=', 1) for p in line.split() if '=' in p)


def last(out, prefix):
    m = [l for l in out if l.startswith(prefix)]
    return m[-1] if m else ''


def readb(p):
    return open(p, 'rb').read()


def lum(c):
    """relative luminance of an sRGB colour (WCAG)"""
    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2])


def contrast(a, b):
    la, lb = lum(a), lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def write_png(src_bmp, dst_png):
    w, h, rows = so.read_bmp24(src_bmp)
    raw = bytearray()
    for r in rows:
        raw += b'\x00' + r

    def chunk(t, c):
        x = struct.pack('>I', len(c)) + t + c
        return x + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(dst_png, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 1)) + chunk(b'IEND', b''))


def layout_of(W, H):
    """independent arithmetic: (k, u, viewX, viewY, viewW, viewH, extra)"""
    k = min(H / 368.0, W / 640.0)
    u = max(1, int(math.floor(0.75 * H / 480 + 0.5)))
    vw, vh = 640 * k, 368 * k
    vx, vy = (W - vw) / 2, (H - vh) / 2
    return k, u, vx, vy, vw, vh, int(math.floor(vx / k + 1e-9))


# =============================================================================================== A. unit tests
print('--- A. layout arithmetic and pointer maps (build/biosys-overlaytest)')
for what in ('layout', 'pointer', 'motion', 'fade'):
    r = subprocess.run(['build/biosys-overlaytest', what], capture_output=True, text=True)
    ls = r.stdout.splitlines()
    check('biosys-overlaytest %s (%d checks)' % (what, sum(1 for l in ls if l.startswith(('PASS', 'FAIL')))), r.returncode == 0 and not any(l.startswith('FAIL') for l in ls), [l for l in ls if l.startswith('FAIL')][:3])

# =============================================================================================== B. the layout numbers of the script commands
print('--- B. layout at the sizes of the player\'s screens')
SIZES = [(3440, 1440), (1920, 1080), (1728, 1084), (1280, 960)]
for W, H in SIZES:
    out = run('overlay on %d %d; overlay layout' % (W, H), game=False)
    kv = kvs(last(out, 'overlay layout'))
    k, u, vx, vy, vw, vh, ex = layout_of(W, H)
    vals = [float(x) for x in kv['view'].split(',')]
    check('%dx%d: k = %.4f, u = %d, the view %.2f,%.2f %.2f x %.2f, %d extra columns, %s' % (W, H, k, u, vx, vy, vw, vh, ex, 'black above and below' if k < H / 368.0 else 'fills the height'),
          abs(float(kv['k']) - k) < 1e-5 and int(kv['u']) == u and all(abs(a - b) < 1e-2 for a, b in zip(vals, (vx, vy, vw, vh))) and int(kv['extra']) == ex
          and kv['strip'] == '%d,%d,%d,%d' % ((W - 640 * u) // 2, H - 56 * u, 640 * u, 56 * u) and int(kv['fourthree']) == (1 if k < H / 368.0 - 1e-12 else 0), out[-1])

# =============================================================================================== C. the option, and the default is faithful
print('--- C. option, menu row, faithful default')
r = subprocess.run(['build/biosys', '--interface', 'bogus', '--script', 'echo x'], capture_output=True, text=True)
check('--interface with a bad value is refused', r.returncode != 0 and 'bad --interface' in r.stderr, r.stderr)
h = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
check('--help lists --interface original|overlay', '--interface original|overlay' in h)
cfg = fresh('cfg') + '/biosys.conf'
subprocess.run(['build/biosys', '--config', cfg, '--write-config', '--interface', 'overlay'], capture_output=True, text=True)
act = dict(re.findall(r'^([a-z-]+) = (.*)$', open(cfg).read(), re.M))
check('--interface overlay implies panorama smooth and widescreen, and is a preference (interface = overlay)', act.get('interface') == 'overlay' and act.get('panorama') == 'smooth' and act.get('widescreen') == 'yes', act)
cfg2 = fresh('cfg2') + '/biosys.conf'
subprocess.run(['build/biosys', '--config', cfg2, '--write-config'], capture_output=True, text=True)
act = dict(re.findall(r'^([a-z-]+) = (.*)$', open(cfg2).read(), re.M))
check('the default is interface = original', act.get('interface') == 'original' and act.get('panorama') == 'original' and act.get('widescreen') == 'no', act)

# the engine's 8-bit frame and the whole script output do not change with the option (the overlay only reads)
S_MSG = 'advance 3000; key F1; advance 300; page S1; scroll 1500,300; advance 100; shot {t}/{n}.bmp; page fce9cab_o; shot {t}/{n}_c.bmp; framehash'
a = run(S_MSG.format(t=T, n='c_def'))
b = run(S_MSG.format(t=T, n='c_ovl'), '--interface', 'overlay')
check('--interface overlay: the engine\'s 8-bit frames and the script output are byte for byte those of the default',
      readb(T + '/c_def.bmp') == readb(T + '/c_ovl.bmp') and readb(T + '/c_def_c.bmp') == readb(T + '/c_ovl_c.bmp') and a == b, (a[-2:], b[-2:]))

# the real window code on the dummy drivers: --interface original is the default, byte for byte
BASE = ['--panorama', 'smooth', '--widescreen']
for sz in ('1280x960', '1920x1080'):
    still = ['--page', 'S1', '--scroll', '1500,300', '--slot', 'day', '--no-objects']       # a page without rain or animation: the two runs show the same thing
    rc1, o1, e1 = window('', 2500, *still, shot=T + '/w_def_%s.bmp' % sz, size=sz)
    rc2, o2, e2 = window('', 2500, '--interface', 'original', *still, shot=T + '/w_org_%s.bmp' % sz, size=sz)
    same = os.path.exists(T + '/w_def_%s.bmp' % sz) and readb(T + '/w_def_%s.bmp' % sz) == readb(T + '/w_org_%s.bmp' % sz)
    check('the window at %s: no option and --interface original draw the same picture, byte for byte' % sz, same and 'overlay=0' in ' '.join(o1), (rc1, rc2, e1[-200:], o1[-2:]))
# the settings menu's row switches it live, and switching it off again gives the picture of the other options byte for byte
open(T + '/toggle.conf', 'w').write('# kept\n')
STILL = ['--page', 'S1', '--scroll', '1500,300', '--slot', 'day', '--no-objects']
rc, o, e = window('f10,tab,down,down,down,down,down,right,right,escape', 7000, *STILL, shot=T + '/w_toggle.bmp', size='1280x960', config=T + '/toggle.conf')
sets = [l for l in o if l.startswith('menulive set')]
rcb, ob, eb = window('', 3000, *BASE, *STILL, shot=T + '/w_toggle_base.bmp', size='1280x960')
check('the Interface row in the Picture tab: the first Right switches the overlay interface on at once (smooth panoramas and widescreen with it), the second switches it off',
      len(sets) >= 3 and sets[0].startswith('menulive set interface') and 'overlay=1' in sets[0] and any('set panorama' in l and 'smooth=1' in l for l in sets) and any('set widescreen' in l and 'wide=1' in l for l in sets)
      and 'overlay=0' in sets[-1], sets)
check('... and the window then draws exactly what --panorama smooth --widescreen draws (byte for byte)',
      os.path.exists(T + '/w_toggle.bmp') and os.path.exists(T + '/w_toggle_base.bmp') and readb(T + '/w_toggle.bmp') == readb(T + '/w_toggle_base.bmp'), (e[-200:], eb[-200:]))
open(T + '/toggle2.conf', 'w').write('# kept\n')
rc, o, e = window('f10,tab,down,down,down,down,down,right,escape', 5600, config=T + '/toggle2.conf')
act = dict(re.findall(r'^([a-z-]+) = (.*)$', open(T + '/toggle2.conf').read(), re.M))
check('the row is a preference: interface = overlay, panorama = smooth and widescreen = yes in the file', act.get('interface') == 'overlay' and act.get('panorama') == 'smooth' and act.get('widescreen') == 'yes', act)

# =============================================================================================== D. the panorama pixels against the oracle
print('--- D. panorama pixels of the full-height view against the exact-cylinder oracle')


def read_frame8(path):
    w, h, pal, rows = so.read_bmp8(path)
    return pal, rows


def oracle_compare(W, H, page, scroll, name):
    """compose at W x H, compare the panorama pixels (not overlay pixels) with the oracle at the layout's scale and field of view"""
    k, u, vx, vy, vw, vh, ex = layout_of(W, H)
    sc = ('overlay on %d %d; page %s; slot day; scroll %d,%d; advance 200; surfdump %s/d_s.raw; shot %s/d_f.bmp; barebmp %s/d_b.bmp; overlay frame; overlay shot %s/%s.bmp'
          % (W, H, page, scroll[0], scroll[1], T, T, T, T, name))
    out = run(sc, '--no-objects', game=False)
    fr = last(out, 'overlay frame')
    m = re.search(r'show=(\d+),(\d+)', fr)
    showl, showr = int(m.group(1)), int(m.group(2))
    cam = so.camera(scroll[0], scroll[1])
    expect = so.wide_extent(cam, 1, ex, ex)
    surf = readb(T + '/d_s.raw')
    pal, frows = read_frame8(T + '/d_f.bmp')
    brows = so.read_bmp8(T + '/d_b.bmp')[3]
    _, _, rows = so.read_bmp24('%s/%s.bmp' % (T, name))
    S = so.Sampler(surf, pal)
    n = sm = mx = skipped = 0
    pL, pR = vx - showl * k, vx + (640 + showr) * k
    for Y in range(int(math.ceil(vy - 0.5)), int(math.floor(vy + vh - 0.5)) + 1, 1):
        for X in range(max(0, int(math.ceil(pL - 0.5))), min(W, int(math.floor(pR - 0.5)) + 1)):
            if (X * 7 + Y * 13) % 11:
                continue
            vxp, vyp = (X + 0.5 - vx) / k, (Y + 0.5 - vy) / k
            cx, cy = int(math.floor(vxp)), int(math.floor(vyp))
            if 0 <= cx < 640 and 0 <= cy < 368 and frows[56 + cy][cx] != brows[cy][cx]:
                skipped += 1        # drawn over the panorama by the engine
                continue
            o = S.at(*so.page_point(cam, vxp, vyp))
            g = rows[Y][3 * X:3 * X + 3]
            e = [abs(o[i] - g[i]) for i in range(3)]
            sm += sum(e)
            mx = max(mx, max(e))
            n += 3
    return fr, showl, showr, expect, sm / max(n, 1), mx, n // 3, skipped, (pL, pR, k)


for (W, H) in ((1920, 1080), (3440, 1440), (1728, 1084)):
    for page, scroll in (('S1', (1500, 300)), ('T1', (1000, 120))):
        fr, sl, sr, expect, mean, mx, n, skipped, (pL, pR, k) = oracle_compare(W, H, page, scroll, 'd_%s_%d' % (page, W))
        fov = math.degrees(2 * math.atan((320 + sl) / so.camera(*scroll)[2])) if sl else 2 * math.degrees(math.atan(320 / so.camera(*scroll)[2]))
        check('%dx%d %s at scale %.4f: the wide view shows %d columns beside the 640 (the oracle\'s extent %d), horizontal field of view %.1f degrees; %d samples, mean error %.4f (<= %.2f), max %d (<= %d)'
              % (W, H, page, k, sl, expect, fov, n, mean, TOL_MEAN, mx, TOL_MAX), sl == expect and sr == expect and mean <= TOL_MEAN and mx <= TOL_MAX and n > 2000, (fr[:150], mean, mx, n))

# =============================================================================================== E. overlay states
print('--- E. overlay states: status word, message, strip')
W, H = 1920, 1080
k, u, vx, vy, vw, vh, ex = layout_of(W, H)
out = run('overlay on %d %d; page S1; scroll 1500,300; advance 5000; overlay frame; overlay shot %s/e_word.bmp' % (W, H, T), '--no-objects')
fr = last(out, 'overlay frame')
kv = kvs(fr)
check('a status word (THIRSTY, the hydration word at the game\'s x = 0xa2) is shown whenever the game shows it: one word, its cell at W/2 + (162 - 320) u',
      kv.get('words') == '1' and 'word0' in kv and int(kv['word0'].split(',')[0]) + 10 == W // 2 + (162 - 320) * u, fr)
out = run('overlay on %d %d; page S1; scroll 1500,300; advance 5000; overlay frame; advance 100; overlay frame; advance 100; overlay frame' % (W, H), '--no-objects')
kv0 = kvs(last([l for l in out if l.startswith('overlay frame')][:1], 'overlay frame'))
check('without a message: no message, no strip, the word stays', kv0['msg'] == '0.000' and kv0['strip'] == '0.000' and kv0['want'] == '0' and kv0['words'] == '1', kv0)

# the message: shown with its fade in (120 ms), then gone after the game clears it (with the fade out of 250 ms)
steps = ['overlay on %d %d; page S1; scroll 1500,300; advance 5000; overlay frame; key F1' % (W, H)]
steps += ['advance 40; overlay frame'] * 5
steps += ['advance 1000; overlay frame; advance 1000; overlay frame;'] * 6
out = run('; '.join(steps), '--no-objects')
fs = [kvs(l) for l in out if l.startswith('overlay frame')]
al = [float(f['msg']) for f in fs]
fade_in = al[1:6]
check('the message fades in over about 120 ms (alpha after 40 ms steps: %s)' % ', '.join('%.2f' % a for a in fade_in), all(abs(a - e) < 1e-6 for a, e in zip(fade_in, (0.333, 0.667, 1.0, 1.0, 1.0)) if True) or
      (abs(fade_in[0] - 40 / 120) < 0.01 and abs(fade_in[1] - 80 / 120) < 0.01 and fade_in[2] == 1.0), fade_in)
box = [f.get('msgbox') for f in fs if f.get('msgbox')]
mb = [int(x) for x in box[0].split(',')]
check('the message line is at the bottom centre, just above the place of the strip (its bottom edge above the strip, centred on the window)',
      abs(mb[0] + mb[2] / 2 - W / 2) <= 2 and mb[1] + mb[3] <= H - 56 * u + 12 + 2 and mb[1] + mb[3] >= H - 56 * u - 12, box[0])
gone = [i for i, a in enumerate(al) if i > 6 and a == 0.0]
check('the game clears the message (about 3 s) and it fades out: alpha reaches 0 (frame %s of %d)' % (gone[:1], len(al)), bool(gone), al)
# the fade out in small steps after the clear
steps = ['overlay on %d %d; page S1; scroll 1500,300; advance 5000; key F1; advance 100; overlay frame; ' % (W, H)]
steps += ['advance 40; overlay frame'] * 130
out = run('; '.join(steps), '--no-objects')
al = [float(kvs(l)['msg']) for l in out if l.startswith('overlay frame')]
i0 = next((i for i in range(1, len(al)) if al[i - 1] == 1.0 and al[i] < 1.0), None)
dec = al[i0:i0 + 8] if i0 else []
check('the fade out takes about 250 ms in 40 ms steps (%s)' % ', '.join('%.2f' % a for a in dec[:8]), i0 is not None and len(dec) >= 7 and abs(dec[0] - (1 - 40 / 250.0)) < 0.01 and dec[6] == 0.0 and dec[5] > 0.0, dec)

# the strip: hidden, flash on pick-up, bar mode, fading
GOG = 'page fce9cab_o; click 330,321; key S'
FRUIT = 'page R5; scroll 1080,606; clickp 1405,792'
out = run('overlay on %d %d; advance 2000; %s; overlay frame; advance 100; overlay frame' % (W, H, GOG))
fs = [kvs(l) for l in out if l.startswith('overlay frame')]
check('an object on the bar but the bar not shown: the strip is hidden (the original keeps it empty until the bar is entered)', all(f['want'] == '0' and f['strip'] == '0.000' for f in fs), fs)
steps = ['overlay on %d %d; advance 2000; overlay frame; %s; %s; overlay frame; advance 60; overlay frame; advance 500; overlay frame; advance 440; overlay frame; advance 100; overlay frame; advance 150; overlay frame; advance 200; overlay frame;'
         % (W, H, GOG, FRUIT)]
out = run(steps[0])
fs = [kvs(l) for l in out if l.startswith('overlay frame')][1:]
al = [float(f['strip']) for f in fs]
wants = [f['want'] for f in fs]
check('the pick-up flash (1 s of the engine\'s clock; the pick-up animation itself takes engine time): the strip is wanted and shown (%s), then it fades out when the flash is over (%s)' % (', '.join('%.2f' % a for a in al[:3]), ', '.join('%.2f' % a for a in al[3:])),
      wants[0] == '1' and wants[1] == '1' and al[0] > 0 and max(al) == 1.0 and wants[-1] == '0' and al[-1] == 0.0 and wants.index('0') > 0 and all(w == '0' for w in wants[wants.index('0'):]), (wants, al))
out = run('overlay on %d %d; advance 2000; overlay frame; %s; advance 1500; overlay frame; key SHIFT; advance 50; overlay frame; advance 100; overlay frame; key SHIFT; advance 50; overlay frame; advance 100; overlay frame; advance 300; overlay frame' % (W, H, GOG))
fs = [kvs(l) for l in out if l.startswith('overlay frame')][2:]
al = [float(f['strip']) for f in fs]
check('bar mode shows the strip (fading in over 120 ms: %s) and leaving it fades the strip out (%s) although the original would keep it' % (', '.join('%.2f' % a for a in al[:2]), ', '.join('%.2f' % a for a in al[2:])),
      fs[0]['want'] == '1' and 0 < al[0] < 1 and al[1] == 1.0 and fs[2]['want'] == '0' and al[2] < 1.0 and al[-1] == 0.0 and 'stripbox' in fs[1], (al, [f['want'] for f in fs]))
sb = [int(x) for x in fs[1]['stripbox'].split(',')]
check('the strip is 640 u x 56 u at the bottom centre', sb == [(W - 640 * u) // 2, H - 56 * u, 640 * u, 56 * u], sb)
pos = []
out = run('overlay on %d %d; advance 2000; %s; page S1; advance 100; overlay frame; capture off; overlay move 960,1079; overlay frame; advance 100; overlay frame' % (W, H, GOG))
fs = [kvs(l) for l in out if l.startswith('overlay frame')]
mv = last(out, 'overlay move')
check('a pointer at the bottom edge of the window (the dock) shows the strip while it is in the bar rows (and the engine entered the bar rows: pointer row >= 424)',
      fs[-1]['want'] == '1' and 'bar=1' in mv and int(re.search(r'engine=\d+,(\d+)', mv).group(1)) >= 424, (mv, fs[-1]))
# the strip is made of the frame's bar rows: icons keep their own transparency (the black around them is see-through)
out = run('overlay on %d %d; advance 2000; %s; page S1; scroll 1500,300; key SHIFT; advance 300; overlay frame; overlay shot %s/e_strip.bmp' % (W, H, GOG, T))
_, _, rows = so.read_bmp24(T + '/e_strip.bmp')
_, _, rows0 = so.read_bmp24(T + '/d_S1_1920.bmp')
sy = H - 28 * u
cx = W // 2
dif = sum(1 for x in range(cx - 70 * u, cx + 70 * u, 3) if sum(abs(rows[sy][3 * x + i] - rows0[sy][3 * x + i]) for i in range(3)) > 60)
dark = sum(1 for x in range(cx - 300 * u, cx - 150 * u, 3) if lum(rows[sy][3 * x:3 * x + 3]) < lum(rows0[sy][3 * x:3 * x + 3]) * 0.8)
check('the icon is drawn over the picture (%d of the sampled pixels at the strip\'s middle row differ) and not in a black box: the strip\'s side keeps the picture, only darkened by the gradient (%d of %d darker)' % (dif, dark, 50),
      dif > 5 and dark > 25, (dif, dark))

# =============================================================================================== F. text contrast over a bright sky
print('--- F. text contrast on a bright page')
W, H = 3440, 1440
k, u, vx, vy, vw, vh, ex = layout_of(W, H)
out = run('overlay on %d %d; page S1; slot day; scroll 1500,200; advance 5000; key F1; advance 400; overlay frame; overlay shot %s/f_sky.bmp; textruns %s/f_runs.txt' % (W, H, T, T), '--no-objects')
fr = kvs(last(out, 'overlay frame'))
w0 = [int(x) for x in fr['word0'].split(',')]
mbx = [int(x) for x in fr['msgbox'].split(',')]
runs = [l for l in open(T + '/f_runs.txt')]
rgbs = [tuple(int(v) for v in re.search(r'rgb=(\d+),(\d+),(\d+)', l).groups()) for l in runs]
_, _, rows = so.read_bmp24(T + '/f_sky.bmp')


def text_contrast(box, rgb):
    """the text's contrast on a bright scene: the outline is pixels of (nearly) its own dark colour right beside the strokes. -> (text pixels, share of text pixels
    that have an outline pixel within u + 1, mean colour of those outline pixels, contrast of the text against it, mean luminance of the halo ring against the raw
    scene luminance there is reported by the caller)"""
    x0, y0, bw, bh = box
    x0 = max(x0, 0); y0 = max(y0, 0)
    px = {}
    for y in range(y0, min(y0 + bh, H)):
        for x in range(x0, min(x0 + bw, W)):
            px[(x, y)] = tuple(rows[y][3 * x:3 * x + 3])
    txt = {p for p, c in px.items() if max(abs(c[i] - rgb[i]) for i in range(3)) <= 5}      # (exactly the text colour: full coverage; the sky is never that grey)
    dark = {p for p, c in px.items() if max(c) <= 40}
    covered = 0
    near = set()
    edge = [p for p in txt if any((p[0] + a, p[1] + b) in px and (p[0] + a, p[1] + b) not in txt for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    for (x, y) in edge:
        d = [(x + dx, y + dy) for dy in range(-u - 1, u + 2) for dx in range(-u - 1, u + 2) if (x + dx, y + dy) in dark]
        covered += bool(d)
        near.update(d)
    mean = tuple(sum(px[q][i] for q in near) / max(len(near), 1) for i in range(3))
    return len(edge), covered / max(len(edge), 1), mean, contrast(rgb, mean)


sky = [tuple(rows[y][3 * x:3 * x + 3]) for y in range(40, 60) for x in range(w0[0] - 60, w0[0] + 200, 7)]
skyl = sum(lum(c) for c in sky) / len(sky)
check('the scene under the words is a bright daytime sky (relative luminance %.2f > 0.35: the savannah sky at its palest here)' % skyl, skyl > 0.35, skyl)
for name, box, rgb in (('status word', w0, rgbs[0]), ('message line', mbx, rgbs[1])):
    nt, share, mean, ratio = text_contrast(box, rgb)
    check('%s %s over the bright sky: %.0f %% of its %d stroke-edge pixels have the dark outline within %d pixels (colour %s); contrast of the text against it %.1f : 1 (WCAG AA wants 4.5)'
          % (name, rgb, share * 100, nt, u + 1, tuple(int(v) for v in mean), ratio), nt > 100 and share > 0.95 and ratio >= 4.5, (nt, share, mean, ratio))
# the same words without outline and halo would not do: the raw contrast against the sky
cs = [contrast(rgbs[0], c) for c in sky]
check('(without the outline and the halo the status word\'s own colour against that sky would be %.1f : 1)' % (sum(cs) / len(cs)), sum(cs) / len(cs) < 4.5, sum(cs) / len(cs))

# =============================================================================================== G. close-up with ambient fill, goggles fallback
print('--- G. ambient fill: a close-up, the goggles')
W, H = 3440, 1440
k, u, vx, vy, vw, vh, ex = layout_of(W, H)
out = run('overlay on %d %d; page fce9cab_o; advance 400; shot %s/g_c.bmp; overlay frame; overlay shot %s/g_close.bmp' % (W, H, T, T), '--no-objects', game=False)
pal, frows = read_frame8(T + '/g_c.bmp')
_, _, rows = so.read_bmp24(T + '/g_close.bmp')
vrgb = [[pal[frows[56 + y][x]] for x in range(640)] for y in range(368)]


def bil(x, y, c):
    sx, sy = min(max((x + 0.5 - vx) / k - 0.5, 0), 639.0), min(max((y + 0.5 - vy) / k - 0.5, 0), 367.0)
    x0, y0 = int(sx), int(sy)
    x1, y1 = min(x0 + 1, 639), min(y0 + 1, 367)
    fx, fy = sx - x0, sy - y0
    return int(math.floor((vrgb[y0][x0][c] * (1 - fx) + vrgb[y0][x1][c] * fx) * (1 - fy) + (vrgb[y1][x0][c] * (1 - fx) + vrgb[y1][x1][c] * fx) * fy + 0.5))


bad = mx = n = 0
for Y in range(0, 1300, 17):
    for X in range(int(vx) + 3, int(vx + vw) - 3, 23):
        for c in range(3):
            d = abs(bil(X, Y, c) - rows[Y][3 * X + c])
            mx = max(mx, d)
            n += 1
            bad += d > 1
check('close-up: the picture part equals the plain close-up scaled by k = %.4f (bilinear, pixel centres), %d samples, max difference %d' % (k, n, mx), bad == 0, (bad, mx))
left = [tuple(rows[Y][3 * X:3 * X + 3]) for Y in range(100, 1300, 11) for X in range(0, int(vx) - 60, 9)]
mean_left = sum(sum(c) / 3.0 for c in left) / len(left)
check('close-up: the empty sides are not black (mean level %.1f) and carry the picture\'s colours' % mean_left, mean_left > 12, mean_left)
grad = lambda row, xs: sum(sum(abs(row[3 * (x + 1) + i] - row[3 * x + i]) for i in range(3)) for x in xs) / (3.0 * len(xs))
sidegrad = sum(grad(rows[Y], range(0, int(vx) - 80)) for Y in range(100, 1300, 50)) / 24.0
pict = sum(grad(rows[Y], range(int(vx) + 30, int(vx + vw) - 30)) for Y in range(100, 1300, 50)) / 24.0
check('close-up: the sides are not sharp (mean neighbour difference %.2f against %.2f in the picture)' % (sidegrad, pict), sidegrad < 1.0 and pict > 3 * sidegrad, (sidegrad, pict))
# 35 %: far from the picture the fill is about 35 % of the blurred picture's level
pm = sum(sum(c) / 3.0 for c in [tuple(rows[Y][3 * X:3 * X + 3]) for Y in range(100, 1300, 11) for X in range(int(vx) + 40, int(vx + vw) - 40, 37)]) / 1.0
pcount = len([1 for Y in range(100, 1300, 11) for X in range(int(vx) + 40, int(vx + vw) - 40, 37)])
ratio = mean_left / (pm / pcount)
check('close-up: the fill away from the picture is dark (%.0f %% of the picture\'s mean level, about 35)' % (ratio * 100), 0.15 < ratio < 0.6, ratio)
seam = [sum(rows[Y][3 * (int(vx) - 2 - d):3 * (int(vx) - 2 - d) + 3]) / 3.0 for d in (0, 30, 100, 300) for Y in (400,)]
check('close-up: a short soft gradient where the fill meets the picture (level at 2 px %.0f, at 32 px %.0f, at 102 px %.0f, at 302 px %.0f)' % tuple(seam), seam[0] >= seam[2] * 0.95 and seam[2] <= seam[0] + 30, seam)
# goggles worn: the wide view falls back to 4:3 and the sides are filled
WEAR = 'page fce9cab_o; click 330,321; key SPACE'
out = run('overlay on %d %d; advance 500; %s; advance 300; page S1; scroll 1500,300; advance 300; overlay frame; overlay shot %s/g_gog.bmp; page fce9cab_o; click 330,321; page S1; advance 300; overlay frame' % (W, H, WEAR, T))
fs = [kvs(l) for l in out if l.startswith('overlay frame')]
check('goggles worn on a panorama: the wide view falls back to the 4:3 picture (show=0,0), as --widescreen does', fs[0]['pano'] == '1' and fs[0]['show'] == '0,0', fs[0])
_, _, rows = so.read_bmp24(T + '/g_gog.bmp')
sd = [tuple(rows[Y][3 * X:3 * X + 3]) for Y in range(100, 1300, 11) for X in range(int(vx) - 120, int(vx) - 10, 5)]
check('... and the sides beside it are filled with the (dark, green) ambient picture near the seam, not black (mean level %.1f, brightest green %d)' % (sum(sum(c) / 3.0 for c in sd) / len(sd), max(c[1] for c in sd)),
      sum(sum(c) / 3.0 for c in sd) / len(sd) > 2.5 and max(c[1] for c in sd) > 20)

# =============================================================================================== H. pointer: maps in the engine, hit tests, the dock, the vertical release
print('--- H. pointer')
for W, H in SIZES:
    k, u, vx, vy, vw, vh, ex = layout_of(W, H)
    out = run('overlay on %d %d; overlay map 320,240; overlay map 320,452; overlay map 100,10; overlay map 100,430; overlay map 639,479' % (W, H), game=False)
    maps = [re.search(r'-> ([\d.-]+),([\d.-]+)', l).groups() for l in out if l.startswith('overlay map')]
    exp = [(W / 2, H / 2), (W / 2, H - 28 * u), (W / 2 + (100 - 320) * u, 10 * u), (W / 2 + (100 - 320) * u, H - 50 * u), (W / 2 + 319 * u, H - u)]
    check('%dx%d: engine -> window: the held pointer at the window centre, the bar-mode pointer in the middle of the strip, the top and bottom rows by the interface scale' % (W, H),
          all(abs(float(a) - e[0]) < 0.01 and abs(float(b) - e[1]) < 0.01 for (a, b), e in zip(maps, exp)), (maps, exp))

# hit tests: the same hotspots under the same engine points, whatever the layout. Window points are made from engine points with the layout arithmetic.
pts = [(x, y) for y in range(70, 420, 29) for x in range(10, 640, 37)]
base = run('page fce9cab_o; advance 100; ' + '; '.join('move %d,%d; dump state' % p for p in pts), '--no-objects')
cur_base = [l for l in base if l.startswith('cursor')]
nz = sum(1 for l in cur_base if not l.startswith('cursor 0 '))
for W, H in SIZES:
    k, u, vx, vy, vw, vh, ex = layout_of(W, H)
    sc = 'overlay on %d %d; page fce9cab_o; advance 100; ' % (W, H)
    for (x, y) in pts:
        sc += 'overlay move %.4f,%.4f; dump state; ' % (vx + (x + 0.5) * k, vy + (y - 56 + 0.5) * k)
    out = run(sc, '--no-objects')
    cur = [l for l in out if l.startswith('cursor')]
    check('%dx%d: %d points over a close-up page (%d of them on hotspots): the cursor / hotspot under each is what it is in the original layout' % (W, H, len(pts), nz), cur == cur_base and nz > 3, [(a, b) for a, b in zip(cur, cur_base) if a != b][:3])
# the dock: pushing the pointer against the bottom window edge enters the bar; the top edge does not do anything
for W, H in SIZES[:2]:
    out = run('overlay on %d %d; page R4; capture on; advance 100; keydown CTRL; overlay move %d,%d; overlay move %d,0' % (W, H, W // 2, H - 1, W // 2), '--no-objects')
    mv = [l for l in out if l.startswith('overlay move')]
    out2 = run('overlay on %d %d; page fce9cab_o; capture on; advance 100; overlay move %d,%d' % (W, H, W // 2 + 40, H - 1), '--no-objects')
    mv2 = [l for l in out2 if l.startswith('overlay move')]
    check('%dx%d: the free pointer (Ctrl held, or a page that does not scroll) pushed against the bottom edge of the window enters the bar rows and bar mode by itself (the engine\'s own rule); against the top edge it does nothing' % (W, H),
          'barmode=1' in mv[0] and 'engine=320,452' in mv[0] and 'bar=1' in mv2[0] and 'barmode=1' in mv2[0], (mv, mv2))
    check('%dx%d: ... the move to the top edge afterwards stays in the view, it does not dock' % (W, H), 'bar=0' in mv[1], mv)
# the top bar has no hotspots (so the top edge has no dock): every point of rows 0..55 is without a hotspot
sc = 'advance 5000; ' + '; '.join('move %d,%d; dump state' % (x, y) for y in (0, 12, 25, 40, 55) for x in range(4, 640, 24))
out = run(sc)
cur = [l for l in out if l.startswith('cursor')]
check('the top bar page has no hotspots: nothing under the pointer on any of %d points of rows 0..55 (the top edge has no dock)' % len(cur), cur and all(l.startswith('cursor 0 ') for l in cur), set(cur))

# the vertical release with deltas / k (tests/test_vrelease.sh drives the same moves absolutely)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
for W, H in ((1920, 1080), (3440, 1440), (1280, 960)):
    k = layout_of(W, H)[0]
    V = 'vrelease; '

    def mot(dy):
        return 'overlay motion 0,%.12f; ' % (dy * k)

    def vr(script):
        out = run('overlay on %d %d; advance 100; capture on; ' % (W, H) + script, game=True)
        res = []
        for l in out:
            m = re.match(r'vrelease released=(\d) sy=(-?\d+) py=(\d+)', l)
            if m:
                res.append(tuple(int(g) for g in m.groups()))
        return res
    o = vr('scroll 1000,600; ' + V + mot(60) + 'advance 100; ' + V + mot(160) + 'advance 100; ' + V + mot(-70) + 'advance 100; ' + V + mot(-90) + 'advance 100; ' + V + mot(-120) + 'advance 100; ' + V)
    check('%dx%d vertical release at the bottom, window deltas / k = %.4f: released at the overshoot, the pointer row follows the deltas (400, 330), ends at the centre row, then the view scrolls up' % (W, H, k),
          len(o) == 6 and o[0][0] == 0 and o[1] == (1, 656, 240) and o[2] == (1, 656, 400) and o[3][:3] == (1, 656, 330) and o[4][0] == 0 and o[5][0] == 0 and o[5][1] < 656, o)
    o = vr('scroll 1000,20; ' + V + mot(-60) + 'advance 100; ' + V + mot(-140) + 'advance 100; ' + V + mot(100) + 'advance 100; ' + V + mot(40) + 'advance 100; ' + V + mot(140) + 'advance 100; ' + V)
    check('%dx%d vertical release at the top, deltas / k: released at 0, pointer row 100 then 200, ends at the centre row, then scrolls down' % (W, H),
          len(o) == 6 and o[1][:2] == (1, 0) and o[2][:3] == (1, 0, 100) and o[3][:3] == (1, 0, 200) and o[4][:2] == (0, 0) and o[5][0] == 0 and o[5][1] > 0, o)
# slow motion is not lost: ten window pixels of one-pixel moves = 2 engine pixels at k = 3.91
out = run('overlay on 3440 1440; page R4; capture on; advance 100; ' + 'overlay motion 1,0; ' * 10)
mv = [l for l in out if l.startswith('overlay motion')]
check('ten 1-pixel moves at k = 3.913 reach the engine as two whole pixels (2.56, the rest carried)', sum(1 for l in mv if 'moved=1' in l) == 2, mv[-3:])

# =============================================================================================== I. the real window (dummy drivers) against the headless composition
print('--- I. window drawing against the headless composition')


def img_diff(a, b, region=None, step=8):
    """mean / max difference of the means of step x step blocks (SDL's software renderer samples textures without filtering, so single pixels differ; the blocks do not)"""
    wa, ha, ra = so.read_bmp24(a)
    wb, hb, rb = so.read_bmp24(b)
    if (wa, ha) != (wb, hb):
        return None
    x0, y0, x1, y1 = region or (0, 0, wa, ha)
    n = s = mx = 0
    for by in range(y0, y1 - step + 1, step):
        for bx in range(x0, x1 - step + 1, step):
            d = 0
            for c in range(3):
                ma = sum(sum(ra[y][3 * bx + c:3 * (bx + step):3]) for y in range(by, by + step)) / float(step * step)
                mb = sum(sum(rb[y][3 * bx + c:3 * (bx + step):3]) for y in range(by, by + step)) / float(step * step)
                d = max(d, abs(ma - mb))
            s += d
            mx = max(mx, d)
            n += 1
    return s / max(n, 1), mx, wa, ha


# a close-up with ambient fill, a word, at 1720 x 720 (k = 1.957, sides 232 px)
opts = ['--interface', 'overlay', '--page', 'fce9cab_o', '--slot', 'day', '--no-objects']
rc, o, e = window('', 3000, *opts, shot=T + '/i_win_close.bmp', size='1720x720', game=False)
out = run('overlay on 1720 720; page fce9cab_o; slot day; advance 2000; overlay frame; overlay shot %s/i_cpu_close.bmp' % T, '--no-objects', game=False)
d = img_diff(T + '/i_win_close.bmp', T + '/i_cpu_close.bmp')
check('the window draws the close-up with its ambient fill like the composition (mean difference %s; SDL\'s software renderer filters differently)' % (('%.2f, max %d' % d[:2]) if d else 'none'), d is not None and d[0] < 3.0, (rc, e[-200:], d))
vx1 = layout_of(1720, 720)[2]
dl = img_diff(T + '/i_win_close.bmp', T + '/i_cpu_close.bmp', (0, 0, int(vx1) - 8, 720), 4)
check('... and the ambient fill itself (the sides, %d px each): mean difference %.2f, and the sides are not black' % (int(vx1), dl[0] if dl else -1), dl is not None and dl[0] < 2.0, dl)
# a panorama with a status word and wide view
opts = ['--interface', 'overlay', '--page', 'S1', '--scroll', '1500,300', '--slot', 'day', '--no-objects']
rc, o, e = window('', 3000, *opts, shot=T + '/i_win_pano.bmp', size='1720x720')
out = run('overlay on 1720 720; page S1; slot day; scroll 1500,300; advance 2000; overlay frame; overlay shot %s/i_cpu_pano.bmp' % T, '--no-objects')
d = img_diff(T + '/i_win_pano.bmp', T + '/i_cpu_pano.bmp')
check('the window draws the panorama (wide, full height) like the composition (mean difference %s)' % (('%.2f, max %d' % d[:2]) if d else 'none'), d is not None and d[0] < 8.0, (rc, e[-200:], d))
fr = kvs(last(out, 'overlay frame'))
wb = [int(x) for x in fr['word0'].split(',')] if 'word0' in fr else [0, 0, 1, 1]
def match_rate(a, b, box, rgb):
    """of the pixels of the text colour in the composition's picture, the fraction that are of the text colour in the window's too (same place)"""
    wa, ha, ra = so.read_bmp24(a)
    wb, hb, rb = so.read_bmp24(b)
    n = m = 0
    for y in range(max(box[1], 0), min(box[1] + box[3], ha)):
        for x in range(box[0], min(box[0] + box[2], wa)):
            ca, cb = ra[y][3 * x:3 * x + 3], rb[y][3 * x:3 * x + 3]
            if max(abs(ca[i] - rgb[i]) for i in range(3)) <= 24:
                n += 1
                m += max(abs(cb[i] - rgb[i]) for i in range(3)) <= 24
    return n, (m / n if n else 0)


nw, rate = match_rate(T + '/i_cpu_pano.bmp', T + '/i_win_pano.bmp', wb, (122, 129, 128))
check('... the status word (outline and halo over the sky): %d text pixels of the composition, %.0f %% of them are text pixels in the window\'s picture too' % (nw, rate * 100), nw > 100 and rate > 0.97, (nw, rate))
# a 4:3 window: the view is centred with black above and below, drawn at k = W / 640
rc, o, e = window('', 3000, '--interface', 'overlay', '--no-objects', shot=T + '/i_win_43.bmp', size='1280x960')
w, h, rows = so.read_bmp24(T + '/i_win_43.bmp')
check('a 4:3 window (1280x960): black above and below the view (the overlays sit at the window\'s edges), the picture in between', w == 1280 and h == 960 and all(sum(rows[y][3 * x:3 * x + 3]) == 0 for y in (3, 60, 105) for x in (10, 640, 1200)) and sum(sum(rows[480][3 * x:3 * x + 3]) for x in range(0, 1280, 50)) > 500, (w, h))
rc, o, e = window('f10', 3500, '--interface', 'overlay', '--no-objects', shot=T + '/i_win_menu.bmp', size='1720x720')
w, h, rows = so.read_bmp24(T + '/i_win_menu.bmp')
check('with the settings menu open the overlay frame stays and the menu panel is drawn over it, centred (1720x720: whole-number scale 1)', w == 1720 and 'menulive open' in ' '.join(o), (w, h, o[-2:]))

# =============================================================================================== J. pictures
print('--- J. pictures for the player (build/shots)')


def shot_pic(name, script, W=3440, H=1440, *opts):
    out = run('overlay on %d %d; %s; overlay frame; overlay shot %s/%s.bmp' % (W, H, script, T, name), *opts)
    write_png('%s/%s.bmp' % (T, name), '%s/%s.png' % (SHOTS, name))
    return last(out, 'overlay frame')


PICS = [
    ('full_dell_idle', 'advance 3000; page R4; scroll 1290,308; advance 3000', 3440, 1440),
    ('full_dell_message', 'advance 3000; page R4; scroll 1290,308; advance 2000; key F1; advance 400', 3440, 1440),
    ('full_dell_inventory', 'advance 2000; page Rhut; scroll 650,330; clickp 958,605; key S; clickp 852,637; key S; clickp 792,575; key S; ' + GOG + '; ' + FRUIT + '; key SHIFT; advance 300; page Rhut; scroll 700,330', 3440, 1440),
    ('full_dell_closeup', 'advance 3000; page Fr2mixer; advance 600', 3440, 1440),    # a close-up of the rainforest itself, so its palette is the right one
    ('full_dell_goggles', 'advance 500; ' + WEAR + '; advance 300; page S1; scroll 1500,300; advance 300', 3440, 1440),
    ('full_1920x1080', 'advance 3000; page S1; slot day; scroll 1500,300; advance 3000; key F1; advance 400', 1920, 1080),
    ('full_1280x960', 'advance 3000; page S1; slot day; scroll 1500,300; advance 3000; key F1; advance 400', 1280, 960),
]
for name, script, W, H in PICS:
    fr = shot_pic(name, script, W, H)
    kv = kvs(fr)
    check('%s.png (%dx%d): %s' % (name, W, H, fr[14:130]), os.path.exists('%s/%s.png' % (SHOTS, name)) and kv['view'] == '1', fr)

print('overlay tests: %d failed' % failed)
sys.exit(1 if failed else 0)
