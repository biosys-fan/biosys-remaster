import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, struct, subprocess, sys, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import projection_oracle as po
sys.path.insert(0, (_DEV + '/analysis/scripts'))
import biosys_pages as bp

T = 'build/test'
fails = 0


def check(name, ok, info=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else '  ' + str(info)))
    if not ok:
        fails += 1


def run(script, extra=()):
    r = subprocess.run(['build/biosys', '--no-objects', '--mute', '--log-events'] + list(extra) + ['--script', script],
                       capture_output=True, text=True)
    return r.stdout.splitlines()


def bmp_pal(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    return [tuple(d[54 + 4 * i + 2:54 + 4 * i - 1:-1]) for i in range(256)] if off >= 54 + 1024 else None


def bmp_view(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    st = (w + 3) & ~3
    rows = [d[off + (h - 1 - y) * st:off + (h - 1 - y) * st + w] for y in range(h)]
    return b''.join(rows[56:56 + 368])


pf = bp.PageFile((_DEV + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'))
entries = {e['name']: e for e in pf.table}

# ------------------------------------------------------------------ forward render and inverse, 8 pages x 4 scroll positions
PAGES = ['R4', 'T1', 'CE7A', 'bRock', 'EAngel2', 'T5', 'CE3', 'T10']
POS = [(0, 0), (2248, 328), (1000, 656), (2400, 100)]   # (2248, 328): the view straddles the seam (slow path of the line renderer)
GRID = [(sx, 56 + sy) for sx in range(0, 640, 40) for sy in range(0, 368, 23)] + [(639, 56 + 367), (320, 56)]
nview = ninv = 0
for page in PAGES:
    p = pf.page(entries[page])
    assert (p.width, p.height) == (2560, 1024), page
    script = 'surfdump %s/proj_s.raw; ' % T
    for i, (x, y) in enumerate(POS):
        script += 'scroll %d,%d; shot %s/proj_%d.bmp; inv %s; ' % (x, y, T, i, ' '.join('%d,%d' % g for g in GRID))
    out = run('page ' + page + '; ' + script)
    surf = open(T + '/proj_s.raw', 'rb').read()
    pr = po.Projection(75.0)
    allv = True
    alli = True
    badinfo = None
    invlines = [l for l in out if l.startswith('inv ')]
    for i, (x, y) in enumerate(POS):
        exp = pr.render(surf, x, y)
        got = bmp_view(T + '/proj_%d.bmp' % i)
        nview += 1
        if got != exp:
            allv = False
            badinfo = (page, x, y, sum(1 for a, b in zip(got, exp) if a != b))
        for k, (sx, sy) in enumerate(GRID):
            line = invlines[i * len(GRID) + k]
            ex = pr.inverse(sx, sy - 56)
            exp_line = 'inv %d,%d -> %d,%d' % (sx, sy, ex[0], ex[1])
            ninv += 1
            if line != exp_line:
                alli = False
                badinfo = badinfo or (page, x, y, line, exp_line)
    check('%s: 4 projected views (640x368 = 235520 pixels each) equal the oracle pixel for pixel' % page, allv, badinfo)
    check('%s: view -> page mapping on a %d-point grid x 4 scroll positions equals the oracle' % (page, len(GRID)), alli, badinfo)
print('compared %d views (%d pixels) and %d inverse points' % (nview, nview * 640 * 368, ninv))

# the mapping must agree with the forward picture: the centre column at view row 184 shows page column yaw
out = run('page R4; scroll 1000,328; inv 320,240')
check('R4 scroll 1000,328: view centre maps to page (1312, 512) = scroll + 312', 'inv 320,240 -> 1312,512' in out, out[-3:])

# ------------------------------------------------------------------ zoom-in jump through a real click
def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def cdiv2(v):   # signed division by 2 truncating toward zero
    return int(v / 2)


def trunc(v):
    return int(v)


hs = [h for h in pf.page(entries['T1']).hotspots if h['name'] == 'L_T1R10'][0]
l, t, r, b = hs['rect']
x0, y0 = 1000, 300
cx = cdiv2(r - l) - 320 + l
cy = min(max(cdiv2(b - t) - 184 + t, 0), 1024 - 368)
fdx = f32((cx - x0) * f32(0.083333336))
alt = f32(((2560 - x0) + cx) * f32(0.083333336))
if 2560 - 640 < x0 and abs(alt) < abs(fdx):
    fdx = alt
fdy = f32((cy - y0) * f32(0.083333336))
out = run('page T1; scroll %d,%d; surfdump %s/z_s.raw; pumptrace on; animshots %s/zin; clickhs L_T1R10; dump returnpage' % (x0, y0, T, T))
surf = open(T + '/z_s.raw', 'rb').read()
pumps = [int(l.split('t=')[1]) for l in out if l.startswith('pump')]
check('zoom-in jump: 12 frames, one host turn each, the clock advances 40 ms per turn', len(pumps) == 12 and pumps == [1000 + 40 * i for i in range(12)], pumps)
ev = [l for l in out if l.startswith('event ') and not l.startswith('event hover')]
names = [l.split()[1] for l in ev]
i_pump = [i for i, l in enumerate(out) if l.startswith('pump')]
i_exit = [i for i, l in enumerate(out) if l.startswith('event exitpage')]
i_enter = [i for i, l in enumerate(out) if l.startswith('event enterpage')]
check('zoom-in jump: the 12 frames come before OnExitPage; OnEnterPage of the new page follows', i_exit and i_pump[-1] < i_exit[-1] < i_enter[-1], (i_pump[-1:], i_exit, i_enter))
ok = True
bad = None
pr = po.Projection(75.0)
off = pr.yaw_offset
for i in range(1, 13):
    x = x0 + trunc(fdx * i)
    y = y0 + trunc(fdy * i)
    if x < 0:
        x += 2560
    pr.set_fov(72 - 3 * (i - 1))
    pr.yaw_offset = off          # exe:00445cc0 keeps the yaw offset of fov 75
    exp = pr.render(surf, x % 2560, y)
    got = bmp_view('%s/zin%03d.bmp' % (T, i - 1))
    if got != exp:
        ok = False
        bad = (i, x, y, sum(1 for a, b in zip(got, exp) if a != b))
        break
check('zoom-in jump: all 12 frames equal the oracle (scroll start + trunc(i * delta), fov 72 - 3 (i-1), yaw offset 312 kept)', ok, bad)
rp = [l for l in out if l.startswith('returnpage')]
check('zoom-in jump: return data saved (page T1, scroll %d,%d)' % (x0, y0), rp and 'valid=1 from=T1' in rp[0] and 'x=%d y=%d' % (x0, y0) in rp[0], rp)

# a zoom jump from a panorama to a panorama: 12 zoom-in frames, the picture of fov 128, 18 frames of fov 126 .. 75
hs = [h for h in pf.page(entries['CE7A']).hotspots if h['name'] == 'JL_brock'][0]
l, t, r, b = hs['rect']
x0, y0 = 320, 600
cx = cdiv2(r - l) - 320 + l
cy = min(max(cdiv2(b - t) - 184 + t, 0), 1024 - 368)
fdx = f32((cx - x0) * f32(0.083333336))
fdy = f32((cy - y0) * f32(0.083333336))
out = run('page CE7A; scroll %d,%d; surfdump %s/z_a.raw; pumptrace on; animshots %s/zar; clickhs JL_brock; surfdump %s/z_b.raw; dump state' % (x0, y0, T, T, T))
out_pp = out
surfa = open(T + '/z_a.raw', 'rb').read()
surfb = open(T + '/z_b.raw', 'rb').read()
st = [l for l in out if l.startswith('page ') and ' scroll ' in l][-1]
bx, by = [int(v) for v in st.split(' scroll ')[1].split(',')]
pumps = [int(l.split('t=')[1]) for l in out if l.startswith('pump')]
check('zoom jump panorama -> panorama: 12 + 1 (black, exe:004449f0) + 1 (fov 128) + 18 = 32 frames, one host turn each (the script clock: 40 ms)',
      len(pumps) == 32 and pumps == [pumps[0] + 40 * i for i in range(32)], len(pumps))
i_enter = [i for i, l in enumerate(out) if l.startswith('event enterpage')]
i_pump = [i for i, l in enumerate(out) if l.startswith('pump')]
check('zoom jump panorama -> panorama: OnEnterPage after the last frame', i_enter and i_enter[-1] > i_pump[-1], (i_enter, i_pump[-1]))
pr = po.Projection(75.0)
off = pr.yaw_offset
frames = []
for i in range(1, 13):
    x = (x0 + trunc(fdx * i)) % 2560
    frames.append((surfa, x, y0 + trunc(fdy * i), 72 - 3 * (i - 1)))
frames.append(None)    # the black frame
frames.append((surfb, bx, by, 128.25))
for fov in range(126, 74, -3):
    frames.append((surfb, bx, by, fov))
ok = True
bad = None
for k, fr in enumerate(frames):
    if fr is None:
        got = bmp_view('%s/zar%03d.bmp' % (T, k))
        pal = bmp_pal('%s/zar%03d.bmp' % (T, k))
        black = pal.index((0, 0, 0)) if pal and (0, 0, 0) in pal else -1
        check('zoom jump panorama -> panorama: frame 12 is the black fill of exe:004449f0 (all %d view pixels = the first palette index of RGB 0,0,0 = %d)' % (len(got), black),
              black >= 0 and got == bytes([black]) * len(got), (black, set(got)))
        continue
    s, x, y, fov = fr
    pr.set_fov(fov)
    pr.yaw_offset = off
    exp = pr.render(s, x, y)
    got = bmp_view('%s/zar%03d.bmp' % (T, k))
    if got != exp:
        ok = False
        bad = (k, x, y, fov, sum(1 for a, b in zip(got, exp) if a != b))
        break
check('zoom jump panorama -> panorama: the 31 picture frames equal the oracle', ok, bad)
out = run('page bRock; scroll 100,100; shot %s/zfin.bmp' % T)
pr = po.Projection(75.0)
sb = open(T + '/z_b.raw', 'rb').read()
fin = bmp_view(T + '/zfin.bmp')
check('after the zoom the field of view is 75 again (a later view equals the 75 degree oracle)',
      fin == pr.render(sb, 100, 100))

# the page-load compass of the zoom jump: the event of the new page is computed (exe:00401050, before the first render of the page) with the yaw of
# the LAST render, the last frame of the zoom-in (scroll start + trunc(12 * delta) + 312)
sector = po.compass_sector


i_load = max(i for i, l in enumerate(out_pp) if l.startswith('event pageloaded bRock')) if any(l.startswith('event pageloaded bRock') for l in out_pp) else -1
comp = [l for l in out_pp[i_load:] if l.startswith('event compass ')]
lastx = (x0 + trunc(fdx * 12)) % 2560
start_b = entries['bRock']['name'] and pf.page(entries['bRock']).start_x
old = sector((lastx + 312) % 2560, 2560, start_b)
new = sector((start_b + 312) % 2560, 2560, start_b)
check('zoom jump panorama -> panorama: the compass event of the new page uses the yaw of the last zoom-in frame (sector %d; the new page start would give %d)' % (old, new),
      comp and comp[0] == 'event compass %d' % old and old != new, (comp, old, new))

# ordinary page jumps: the same quirk (scroll, jump to a panorama with another start)
for (pa, (xa, ya), pb) in (('R4', (1000, 328), 'CE7A'), ('T1', (2400, 100), 'bRock'), ('CE3', (1700, 656), 'T10'), ('T5', (300, 0), 'EAngel2')):
    o = run('page %s; scroll %d,%d; echo @b; page %s; echo @c; api GetPanoramaInfo' % (pa, xa, ya, pb))
    pr = po.Projection(75.0)
    pr._prepare(xa, ya)
    startb = pf.page(entries[pb]).start_x
    old = sector(pr.heading(), 2560, startb)
    new = sector((startb + 312) % 2560, 2560, startb)
    seg = o[o.index('@b'):o.index('@c')]
    cl = [l for l in seg if l.startswith('event compass ')]
    api = [l for l in o if l.startswith('api GetPanoramaInfo')]
    check('%s %d,%d -> %s: page-load compass sector %d from the previous render (%d if the new scroll were used); API 0x2f afterwards: the new heading %d' %
          (pa, xa, ya, pb, old, new, (startb + 312) % 2560),
          cl == ['event compass %d' % old] and api and 'centre=%d' % ((startb + 312) % 2560) in api[0], (cl, api))

# ------------------------------------------------------------------ the zoom-out of the right-click return (exe:00445f80)
hs = [h for h in pf.page(entries['R10']).hotspots if h['name'] == 'FR10htch_C'][0]
l, t, r, b = hs['rect']
x0, y0 = 1000, 300
cx = cdiv2(r - l) - 320 + l
cy = min(max(cdiv2(b - t) - 184 + t, 0), 1024 - 368)
fdx = f32((cx - x0) * f32(0.083333336))
fdy = f32((cy - y0) * f32(0.083333336))
out = run('page R10; scroll %d,%d; surfdump %s/zo_s.raw; clickhs FR10htch_C; pumptrace on; animshots %s/zout; echo @ret; rclick 320,200; echo @end; dump state' % (x0, y0, T, T))
surf = open(T + '/zo_s.raw', 'rb').read()
seg = out[out.index('@ret'):out.index('@end')]
pumps = [int(l.split('t=')[1]) for l in seg if l.startswith('pump')]
check('zoom-out: 13 frames (the picture of the page change, 11 steps, the last picture), one host turn each', len(pumps) == 13 and pumps == [pumps[0] + 40 * i for i in range(13)], pumps)
idx = {k: [i for i, l in enumerate(seg) if l.startswith(k)] for k in ('event exitpage', 'event returntosavedpage', 'event enterpage', 'pump')}
check('zoom-out: the frames come after OnExitPage / event 36 of the return and before OnEnterPage of the page',
      idx['event returntosavedpage'] and idx['event enterpage'] and idx['pump'][0] > idx['event returntosavedpage'][0] and idx['pump'][-1] < idx['event enterpage'][-1],
      (idx['event returntosavedpage'], idx['event enterpage'], idx['pump'][:1], idx['pump'][-1:]))
pr = po.Projection(75.0)
off = pr.yaw_offset
ok = True
bad = None
for j, (x, y, fov) in enumerate(po.zoom_out_schedule(x0, y0, fdx, fdy)):
    pr.set_fov(fov)
    if j < 12:
        pr.yaw_offset = off
    exp = pr.render(surf, x, y)
    got = bmp_view('%s/zout%03d.bmp' % (T, j))
    if got != exp:
        ok = False
        bad = (j, x, y, fov, sum(1 for a, b in zip(got, exp) if a != b))
        break
check('zoom-out: all 13 frames equal the oracle (fov 39, 42 .. 72 with scroll start + trunc(k * delta), k = 12 .. 1, then fov 75 at the start)', ok, bad)
check('zoom-out: back on the page at the scroll position it was left at, field of view 75', 'page R10 scroll %d,%d' % (x0, y0) in out)
# the same through the frame clock of a host: the step is whatever the host's turn lasts (here 17 ms), nothing else is waited for
out = run('page R10; scroll %d,%d; pumpstep 17; pumptrace on; clickhs FR10htch_C; echo @ret; rclick 320,200; echo @end' % (x0, y0))
p1 = [int(l.split('t=')[1]) for l in out[:out.index('@ret')] if l.startswith('pump')]
p2 = [int(l.split('t=')[1]) for l in out[out.index('@ret'):] if l.startswith('pump')]
check('frame timing: zoom-in 12 frames and zoom-out 13 frames advance the injected clock by exactly one host turn (17 ms) each, nothing more',
      len(p1) == 12 and len(p2) == 13 and all(b - a == 17 for q in (p1, p2) for a, b in zip(q, q[1:])), (p1, p2))

# exe:00445cc0: the main window's movies are dealt with before the 12 frames (exe:00457a60(0)): an unbound one is freed (callbacks 3, 4), a bound
# playing one is stopped without callback and goes on after the jump (exe:00457af0)
out = run('page T1; scroll 1000,300; movie movies\\rforest\\r1_mist.avi 5 7; advance 200; echo @z; clickhs L_T1R10; echo @e')
seg = out[out.index('@z'):out.index('@e')]
names = [l for l in seg if l.startswith(('event movie', 'event event37', 'event exitpage'))]
check('zoom-in: an unbound movie of the page is stopped and freed (events 3, 4) before the jump saves the page (event 37, OnExitPage)',
      names == ['event movie 0 3', 'event movie 0 4', 'event event37', 'event exitpage T1'], names)
out = run('page T1; scroll 1000,300; api OpenAVIMovie movies\\rforest\\r1_mist.avi 5 7 1; api SetAVIMoviePage 0 T1; api PlayAVIMovie 0 1; advance 200; '
          'echo @z; clickhs L_T1R10; echo @e; api IsAVIMoviePlaying 0')
seg = out[out.index('@z'):out.index('@e')]
check('zoom-in: a movie bound to the page is stopped silently (no event 3) and plays again after the jump',
      not any(l.startswith('event movie') for l in seg) and 'api IsAVIMoviePlaying rc=0 playing=1' in out, [l for l in seg if 'movie' in l])

sys.exit(1 if fails else 0)
