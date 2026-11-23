"""Engine wiring tests: every expectation is computed from the original data with the independent tools
(biosys-tandump / biosys-avidump / biosys-objdump export, biosys-wavdump render) and compared with what
`build/biosys --script` produces."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import projection_oracle as po

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
OBJ = D + '/setup-cd/installed/DATA1/Biodata/OBJECTS.DAT'
PAL = D + '/game-cd/files/BIODATA/PALETTES/RFOREST/RF_DAY.PAL'
BIOTIME = [D + '/setup-cd/installed/DATA1/Biotime', D + '/game-cd/files/BIOTIME']
T = 'build/test/wire'
os.makedirs(T, exist_ok=True)
CYAN = (0, 255, 255)
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:400] if detail else ''))


def sh(args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('%s failed: %s' % (args[:3], r.stderr.strip()[:300]))
    return r.stdout


def run(script):
    return sh(['build/biosys', '--log-events', '--script', script]).splitlines()


def find_time(rel):
    parts = rel.replace('\\', '/').split('/')
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
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    stride = (w + 3) & ~3
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    rows = [bytearray(d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w]) for y in range(h)]
    return rows, pal


def key_index(pal, color=CYAN):
    return pal.index(color) if color in pal else -1


def tan_frame(rel, k):
    out = '%s/tan_%d.bmp' % (T, k)
    sh(['build/biosys-tandump', 'export', find_time(rel), str(k), out])
    return read_bmp(out)[0]


def tan_info(rel):
    line = sh(['build/biosys-tandump', 'info', find_time(rel)]).splitlines()[0]
    f = dict(p.split('=') for p in line.split()[1:])
    w, h = f['size'].split('x')
    return int(f['frames']), int(w), int(h), int(f['unk0c'], 16)


def obj_image(name, n):
    out = '%s/obj_%s_%d.bmp' % (T, name, n)
    sh(['build/biosys-objdump', 'export', OBJ, name, str(n), out, PAL])
    return read_bmp(out)[0]


def overlay(rows, img, dx, dy, key, clip=(0, 640, 0, 480)):
    """img over rows at (dx,dy), pixels == key skipped, clipped to clip = (x0,x1,y0,y1). Returns a copy."""
    out = [bytearray(r) for r in rows]
    for j, ir in enumerate(img):
        y = dy + j
        if not clip[2] <= y < clip[3]:
            continue
        for i, v in enumerate(ir):
            x = dx + i
            if clip[0] <= x < clip[1] and v != key:
                out[y][x] = v
    return out


def same(a, b, y0=0, y1=480):
    return all(a[y] == b[y] for y in range(y0, y1))


def ndiff(a, b, y0, y1):
    return sum(1 for y in range(y0, y1) for x in range(640) if a[y][x] != b[y][x])


def pcm(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    n, = struct.unpack_from('<I', d, i + 4)
    d = d[i + 8:i + 8 + n]
    return struct.unpack('<%dh' % (len(d) // 2), d)


def oracle(rel):
    out = T + '/oracle.wav'
    sh(['build/biosys-wavdump', 'render', out, find_time(rel)])
    return pcm(out)


def events(lines, kind):
    return [l.split()[2:] for l in lines if l.startswith('event ' + kind + ' ')]


VIEW = (56, 424)

# ====================================================================== (a) kukri pick-up animation
rel = 'anims\\OBJ_UP\\kukup.tan'
nfr, tw, th, unk = tan_info(rel)
frames = [tan_frame(rel, k) for k in range(nfr)]
out = run('page O1; slot day; scroll 420,470; shot %s/pre.bmp; animshots %s/ka; clickp 740,714; animshots ; '
          'shot %s/post.bmp; remove kukri; shot %s/rem.bmp' % (T, T, T, T))
pre, pal = read_bmp(T + '/pre.bmp')
post = read_bmp(T + '/post.bmp')[0]
rem = read_bmp(T + '/rem.bmp')[0]
key = key_index(pal, ((unk >> 16) & 255, (unk >> 8) & 255, unk & 255))
shots = sorted(f for f in os.listdir(T) if f.startswith('ka') and f.endswith('.bmp'))
rec = [read_bmp(T + '/' + f)[0] for f in shots]
# the record of kukri: x=410 y=70 (objdump), view top 56
sx, sy = 410, 56 + 70
seq = []
for fr in rec:
    got = None
    for base in (pre, rem):
        for j in range(nfr):
            if same(fr, overlay(base, frames[j], sx, sy, key), 0, VIEW[1]):
                got = j
                break
        if got is not None:
            break
    seq.append(got)
dedup = [s for i, s in enumerate(seq) if i == 0 or s != seq[i - 1]]
check('(a) blocking loop recorded %d frames' % len(rec), len(rec) >= nfr, seq)
check('(a) frames of kukup.tan appear in order at the record position, nothing else changes (held image absent)',
      dedup == list(range(nfr)), seq)
check('(a) the held image would be visible without the animation (%d px differ)' % ndiff(pre, post, *VIEW),
      ndiff(pre, post, *VIEW) > 1000)
ev = [(l.split()[2], l.split()[3]) for l in out if l.startswith('event gameobject')]
check('(a) event order gameobject kukri 10 then kukri 1', ev == [('kukri', '10'), ('kukri', '1')], ev)
check('(a) animation trace printed, held after the animation',
      any(l.startswith('event anim 1 anims\\OBJ_UP\\kukup.tan hideHeld=1') for l in out))

# API 0x47: missing file -> 0x11, existing -> 0
out = run('page O1; slot day; useanim anims\\OBJ_UP\\kukup.tan - 100 20 1 40 0 0; useanim anims\\nothere.tan - 100 20 1 40 0 0')
check('(a) API 0x47 returns 0 / 0x11 (17) when the animation cannot be opened',
      [l for l in out if l.startswith('api ')] == ['api 0', 'api 17'])

# ====================================================================== (b) wav of an anim record
# sb_pick (anim record event 1: pickerup.tan + SOUNDS\OBJECTS\pickext.wav) lies on page Rhut / dropzone1
rec_line = [l for l in sh(['build/biosys-objdump', 'objects', OBJ]).splitlines() if l.startswith('anim 12 0 ')][0]
check('(b) objdump shows the wav of the sb_pick pick-up record', 'wav=[SOUNDS\\OBJECTS\\pickext.wav]' in rec_line, rec_line)
out = run('page Rhut; slot day; scroll 500,300; click 292,247; dump state; mixdump %s/b.wav 0' % T)
check('(b) sb_pick was picked up', 'held sb_pick' in out, out[-6:])
a = pcm(T + '/b.wav')
o = oracle('SOUNDS\\OBJECTS\\pickext.wav')
n = min(len(a), len(o))
check('(b) mixer output during the animation is non-silent (peak %d, %d frames)' % (max(map(abs, a)), len(a) // 2),
      max(map(abs, a)) > 1000)
check('(b) the output is the record\'s wav from its start', n > 20000 and a[:n] == o[:n], (len(a), len(o)))

# ====================================================================== (c) verb 4 PLAYWAVE
for (cx, cy, hot, wav) in ((200, 56 + 200, 'bsquelch3', 'sounds\\house\\squelch3.wav'),
                           (300, 56 + 70, 'bsquelch2', 'sounds\\house\\squelch2.wav')):
    out = run('page nPH13dead; slot day; click %d,%d; mixdump %s/c.wav 300' % (cx, cy, T))
    a = pcm(T + '/c.wav')
    o = oracle(wav)
    check('(c) hotspot %s: event, non-silent output (peak %d)' % (hot, max(map(abs, a))),
          'event hotspot nPH13dead ' + hot in out and max(map(abs, a)) > 1000)
    check('(c) %s: the output is exactly the page file' % wav, a[:len(a)] == o[:len(a)], (len(a), len(o)))
out = run('page nPH13dead; slot day; click 5,100; mixdump %s/c0.wav 300' % T)
check('(c) a click outside the hotspots makes no sound', max(map(abs, pcm(T + '/c0.wav'))) == 0)
out = run('page nPH13dead; sound sounds\\house\\squelch1.wav 0; mixdump %s/c1.wav 200' % T)
check('(c) script `sound` plays through the SoundSystem', 'sound handle 0' in out and max(map(abs, pcm(T + '/c1.wav'))) > 1000, out[-2:])

# ====================================================================== (d) fixed / page-relative sprites
rel = 'anims\\OBJ_UP\\kukup.tan'
PX, PY = 100, 500
S1, S2, SY = 2400, 0, 470   # S2: the projected view shows page x 312 +- ~270, so the sprite at x 100 is in view
FX, FY = 100, 20


PROJ = po.Projection(75.0)


def page_overlay(rows, img, px, py, scx, scy, pagew=2560):
    """the screen `rows` (top bar and bars kept) with the view replaced by the cylindrical projection (exe:0040cf00) of the page surface with
    `img` painted into it at page position (px, py): the page-relative sprite is part of the page surface"""
    surf = bytearray(SURF)
    if img is not None:
        for j, ir in enumerate(img):
            for i, v in enumerate(ir):
                if v != key:
                    surf[(py + j) * pagew + px + i] = v
    view = PROJ.render(bytes(surf), scx, scy)
    out = [bytearray(r) for r in rows]
    for y in range(368):
        out[56 + y] = bytearray(view[y * 640:(y + 1) * 640])
    return out


script = ('page O1; slot day; surfdump %s/d_surf.raw; scroll %d,%d; shot %s/d_b1.bmp; scroll %d,%d; shot %s/d_b2.bmp; scroll %d,%d; '
          'sprite %s %d %d 1 100 page; shot %s/d_p1.bmp; scroll %d,%d; shot %s/d_p2.bmp; scroll %d,%d; '
          'sprite %s %d %d 1 100 fixed; shot %s/d_f2.bmp; scroll %d,%d; shot %s/d_f1.bmp; advance 3000; shot %s/d_end.bmp') % (
    T, S1, SY, T, S2, SY, T, S1, SY, rel, PX, PY, T, S2, SY, T, S2, SY, rel, FX, FY, T, S1, SY, T, T)
out = run(script)
SURF = open(T + '/d_surf.raw', 'rb').read()
b1 = read_bmp(T + '/d_b1.bmp')[0]
b2 = read_bmp(T + '/d_b2.bmp')[0]
p1 = read_bmp(T + '/d_p1.bmp')[0]
p2 = read_bmp(T + '/d_p2.bmp')[0]
f2 = read_bmp(T + '/d_f2.bmp')[0]
f1 = read_bmp(T + '/d_f1.bmp')[0]
end = read_bmp(T + '/d_end.bmp')[0]
check('(d) without sprites the views are the projection of the page surface', same(b1, page_overlay(b1, None, 0, 0, S1, SY), 0, VIEW[1]) and same(b2, page_overlay(b2, None, 0, 0, S2, SY), 0, VIEW[1]))
check('(d) page-relative sprite at scroll %d wraps through the seam into view (screen x %d)' % (S1, (PX - S1) % 2560),
      same(p1, page_overlay(b1, frames[0], PX, PY, S1, SY), 0, VIEW[1]) and not same(p1, b1, 0, VIEW[1]))
check('(d) after scrolling to %d the page-relative sprite moved with the page' % S2,
      same(p2, page_overlay(b2, frames[0], PX, PY, S2, SY), 0, VIEW[1]) and not same(p2, b2, 0, VIEW[1]))
check('(d) fixed sprite stays at screen (%d,%d) at scroll %d (the page-relative one still moves)' % (FX, FY, S2),
      same(f2, overlay(page_overlay(b2, frames[0], PX, PY, S2, SY), frames[0], FX, 56 + FY, key, (0, 640, 56, 424)), 0, VIEW[1]))
check('(d) ... and at the other scroll position %d' % S1,
      same(f1, overlay(page_overlay(b1, frames[0], PX, PY, S1, SY), frames[0], FX, 56 + FY, key, (0, 640, 56, 424)), 0, VIEW[1]))
check('(d) after both animations stopped the page surface is restored exactly', same(end, b1))

# ====================================================================== (e) movie
mv = 'movies\\intro\\jumpstrt.avi'
info = sh(['build/biosys-avidump', 'info', find_time(mv)]).split()
fps = float([t for t in info if t.startswith('fps=')][0][4:])
period = 1000.0 / fps
MX, MY = 100, 100
mframes = {}


def mframe(k):
    """rows 0..367 of the expected view: the movie is painted into the page surface (page-relative) at (MX, MY), the view is its projection"""
    if k not in mframes:
        o = '%s/mv_%d.bmp' % (T, k)
        sh(['build/biosys-avidump', 'export', find_time(mv), str(k), o])
        fr = read_bmp(o)[0]
        surf = bytearray(open(T + '/e_surf.raw', 'rb').read())
        for j, r in enumerate(fr):
            surf[(MY + j) * 2560 + MX:(MY + j) * 2560 + MX + len(r)] = bytes(r)
        view = po.Projection(75.0).render(bytes(surf), MX, MY)
        mframes[k] = [view[y * 640:(y + 1) * 640] for y in range(368)]
    return mframes[k]


ks = [0, 1, 2, 5, 9, 14]
script = 'page O1; slot day; scroll %d,%d; surfdump %s/e_surf.raw; shot %s/e_base.bmp; movie %s %d %d' % (MX, MY, T, T, mv, MX, MY)
t = 0
for k in ks:
    tk = int(round(k * period)) + 3 if k else 0
    if k:
        script += '; advance %d; shot %s/e_%d_before.bmp; advance %d' % (max(tk - t - 7, 0), T, k, 7 if tk - t >= 7 else tk - t)
    script += '; shot %s/e_%d.bmp' % (T, k)
    t = tk
script += '; page R4; advance 16; shot %s/e_gone.bmp; page O1; scroll %d,%d; advance 16; shot %s/e_back.bmp' % (T, MX, MY, T)
out = run(script)
eb = read_bmp(T + '/e_base.bmp')[0]
okk = True
bad = []
for k in ks:
    got = read_bmp('%s/e_%d.bmp' % (T, k))[0]
    exp = mframe(k)
    if not all(got[56 + y][:640] == exp[y][:640] for y in range(368)):
        okk = False
        bad.append(k)
check('(e) movie frame k is shown inside the view at time k x period (k=%s)' % ks, okk, bad)
okb = True
for k in ks[1:]:
    got = read_bmp('%s/e_%d_before.bmp' % (T, k))[0]
    exp = mframe(k - 1) if k - 1 in mframes or True else None
    if not all(got[56 + y][:640] == exp[y][:640] for y in range(368)):
        okb = False
check('(e) just before the due time the previous frame is still shown', okb)
gone = read_bmp(T + '/e_gone.bmp')[0]
ref = run('page R4; advance 16; shot %s/e_ref.bmp' % T)
check('(e) after a page change the movie is gone (page R4 as without a movie)', same(gone, read_bmp(T + '/e_ref.bmp')[0]))
back = read_bmp(T + '/e_back.bmp')[0]
check('(e) and the page it was playing on is re-baked without it', same(back, eb, 0, VIEW[1]))
check('(e) movie events: played (2), per frame (5), destroyed (4) at the page change',
      ['0', '2'] in events(out, 'movie') and ['0', '5', '0'] in events(out, 'movie') and ['0', '4'] in events(out, 'movie'),
      events(out, 'movie')[:4])

# ====================================================================== (f) layer order: held < fixed sprite < wearing
held_hold = [l for l in sh(['build/biosys-objdump', 'objects', OBJ]).splitlines() if l.startswith('hold 10 0 ')][0]
hx, hy = [int(v.split('=')[1]) for v in held_hold.split()[4:6]]
held = obj_image('CEblue', 4)
wear = obj_image('goggles', 13)
pal0 = read_bmp(T + '/pre.bmp')[1]
k = key_index(pal0)


clip = (0, 640, 56, 424)
blank = [bytearray(640) for _ in range(480)]


def stack(order, spx, spy, base=blank):
    layers = {'h': (held, hx, 56 + hy, k), 's': (frames[0], spx, 56 + spy, key), 'w': (wear, 0, 56, k)}
    r = base
    for c in order:
        img, x, y, kk = layers[c]
        r = overlay(r, img, x, y, kk, clip)
    return r


best = None
for x in range(0, 500, 20):
    for y in range(0, 70, 10):
        a = stack('hsw', x, y)
        d1 = ndiff(a, stack('shw', x, y), 56, 424)    # held above the sprite
        d2 = ndiff(a, stack('hws', x, y), 56, 424)    # sprite above the wearing overlay
        if min(d1, d2) > 0 and (best is None or min(d1, d2) > best[0]):
            best = (min(d1, d2), x, y, d1, d2)
check('(f) geometry: a sprite position exists where the layer order matters', best is not None)
SPX, SPY = best[1], best[2]
check('(f) sprite at (%d,%d): %d px differ if held were above it, %d if it were above the wearing overlay' % (SPX, SPY, best[3], best[4]), True)
script = ('page fce9cab_o; slot day; click 340,316; key SPACE; click 200,306; dump state; '
          'sprite %s %d %d 1 100 fixed; shot %s/f1.bmp; stopall; remove CEblue; remove goggles; shot %s/f_base.bmp; dump state') % (
    rel, SPX, SPY, T, T)
out = run(script)
check('(f) setup: CEblue held, goggles worn', 'held CEblue' in out and 'worn goggles' in out, [l for l in out if l.startswith(('held', 'worn'))])
f1 = read_bmp(T + '/f1.bmp')[0]
fb = read_bmp(T + '/f_base.bmp')[0]
exp = stack('hsw', SPX, SPY, fb)
check('(f) held image, then fixed sprite, then wearing overlay (exact frame)', same(f1, exp, 0, VIEW[1]))
check('(f) not the order held-above-sprite / sprite-above-wearing',
      not same(f1, stack('shw', SPX, SPY, fb), 0, VIEW[1]) and not same(f1, stack('hws', SPX, SPY, fb), 0, VIEW[1]))

print('wiring tests: %s' % ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
