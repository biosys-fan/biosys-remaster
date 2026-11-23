"""Palette commands, cross-fade jump and page-image commands through `build/biosys --script`.

Every expected value is computed here from the original's arithmetic (decompiled exe, see the comments) and from the
game data decoded with analysis/scripts/biosys_pages.py, not from the engine's own output."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
sys.path.insert(0, D + '/analysis/scripts')
import biosys_pages as bp   # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import projection_oracle as po   # noqa: E402

T = 'build/test/palette_images'
os.makedirs(T, exist_ok=True)
DATA = [D + '/setup-cd/installed/DATA1/Biodata', D + '/game-cd/files/BIODATA']
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:700] if detail else ''))


def run(script, *opts, err=False):
    r = subprocess.run(['build/biosys', '--mute', '--log-events'] + list(opts) + ['--script', script], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('biosys failed: %s\n%s' % (script[:200], r.stderr[:400]))
    return (r.stdout.splitlines(), r.stderr.splitlines()) if err else r.stdout.splitlines()


def find_data(rel):
    p = None
    for root in DATA:
        cur = root
        for part in rel.replace('\\', '/').split('/'):
            try:
                m = [n for n in os.listdir(cur) if n.lower() == part.lower()]
            except OSError:
                m = []
            if not m:
                cur = None
                break
            cur = cur + '/' + m[0]
        if cur:
            return cur
    raise RuntimeError('missing ' + rel)


def read_pal(rel):
    d = open(find_data(rel), 'rb').read()
    assert len(d) == 0x418 and d[:4] == b'RIFF'
    return [tuple(d[24 + 4 * i:27 + 4 * i]) for i in range(256)]


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    st = (w + 3) // 4 * 4
    rows = [d[off + (h - 1 - y) * st: off + (h - 1 - y) * st + w] for y in range(h)]
    return pal, rows


def hex_pal(h):
    b = bytes.fromhex(h)
    return [tuple(b[3 * i:3 * i + 3]) for i in range(256)]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def fade_pal(start, target, divisor, k, keep=None):
    out = []
    for i in range(256):
        e = []
        for c in range(3):
            delta = f32((target[i][c] - start[i][c]) / divisor)      # exe:00412630 / 00412ae0: (float)(t - s) / (float)n
            e.append(int(delta * float(k) + start[i][c]) & 0xff)     # __ftol truncates, (char) keeps the low byte
        out.append(tuple(e))
    if keep is not None:
        out[keep] = start[keep]
    return out


def pal_lines(lines):
    out = []
    for l in lines:
        m = re.match(r'palette t=(\d+) ([0-9a-f]+)$', l)
        if m:
            out.append((int(m.group(1)), hex_pal(m.group(2))))
    return out


def one(lines, prefix):
    return [l for l in lines if l.startswith(prefix)]


NIGHT = 'palettes\\clubeden\\ce_night.pal'
DAYP = 'palettes\\clubeden\\ce_day.pal'

# ============================================================ SetPalette immediate / GetPalette / authority
night = read_pal(NIGHT)
o = run('page R4; pal; palettetrace on; api SetPalette %s 0 0; api GetPalette; pal; shot %s/imm.bmp' % (NIGHT, T))
p0 = hex_pal(one(o, 'pal ')[0].split()[1])
tr = pal_lines(o)
check('SetPalette immediate: one palette, the one given, event 13 once after it',
      len(tr) == 1 and tr[0][1] == night and o.count('event palettechanged') == 1 and
      o.index('event palettechanged') > o.index([l for l in o if l.startswith('palette t=')][0]) and 'api SetPalette rc=0' in o, tr[:1])
gp = [l for l in o if l.startswith('api GetPalette')][0]
check('GetPalette returns the palette just set (256 x R,G,B,0)',
      bytes.fromhex(gp.split('hex=')[1]) == b''.join(bytes(c) + b'\0' for c in night), gp[:80])
bpal, rows = read_bmp(T + '/imm.bmp')
check('next frame: the BMP palette is the one given', bpal == night)
check('the provisional page palette differs (so the test is meaningful)', p0 != night)
o = run('page R4; api SetPalette %s 0 0; page O1; pal; slot dusk; pal' % NIGHT)
pa = [hex_pal(l.split()[1]) for l in one(o, 'pal ')]
o2 = run('page O1; slot dusk; pal')
pd = hex_pal(one(o2, 'pal ')[0].split()[1])
check('API palette is authoritative: a page jump keeps it; slot (debug key) takes the page palette back',
      pa[0] == night and pa[1] == pd and pd != night, (pa[0] == night, pa[1] == pd))
o = run('page R4; api SetPalette %s 0 0; slot night; pal' % NIGHT)
check('without game logic the provisional palette still follows the slot (no API call)',
      hex_pal(one(run('page R4; slot night; pal'), 'pal ')[0].split()[1]) != p0)
o = run('page R4; api SetPalette %s 3 0; api SetPalette %s 0 2; activate off; api SetPalette %s 0 0; api FadeToColour 1 2 3 0 1 -1 100; '
        'api ClearWindow -1 3; api JigglePage 0 0 0; api FadeToPalette %s 0 1 100' % (NIGHT, NIGHT, NIGHT, NIGHT))
check('SetPalette: steps 0 with a duration -> 0x12; inactive app -> 0x15 (ClearWindow / JigglePage answer 0 there)',
      [l.split()[2] for l in o if l.startswith('api ')] == ['rc=0', 'rc=18', 'rc=21', 'rc=21', 'rc=0', 'rc=0', 'rc=21'], [l for l in o if l.startswith('api')])
tab = [l for l in o if l.startswith('api SetPalette')]

# ============================================================ timed SetPalette: the original's float arithmetic, 500 ms ticks
def sim_timed(start_now, steps, seconds, run_ms):
    """exe:00412630: steps <= seconds*2, interval = seconds*1000/steps, one step per 500 ms tick once `due` is reached."""
    steps = min(steps, seconds * 1000 // 500)
    interval = seconds * 1000 // steps
    due = start_now + interval
    now, last_tick, k, out = start_now, 1000, 0, []
    end = start_now + run_ms
    while now < end:
        now = min(end, now + 16)
        if now - last_tick >= 500:
            last_tick = now - (now - last_tick) % 500
            if k < steps and now >= due:
                k += 1
                out.append((now, k))
                due = now + interval
    return steps, out


for steps, secs in ((4, 2), (10, 1), (3, 3), (1, 2)):
    run_ms = steps * 1100 + 2000
    o = run('page R4; pal; palettetrace on; api SetPalette %s %d %d; advance %d; api GetPalette' % (NIGHT, steps, secs, run_ms))
    start = hex_pal(one(o, 'pal ')[0].split()[1])
    tr = pal_lines(o)
    n, sched = sim_timed(1000, steps, secs, run_ms)
    exp = [(t, fade_pal(start, night, n, k)) for t, k in sched]
    check('timed SetPalette %d steps / %ds: %d steps (clamped to 2/s), each palette = the original formula, at the tick times' % (steps, secs, n),
          len(tr) == n and tr == exp, (len(tr), n, [t for t, _ in tr], [t for t, _ in exp]))
    ev = [i for i, l in enumerate(o) if l == 'event palettechanged']
    last = max(i for i, l in enumerate(o) if l.startswith('palette t='))
    check('  event 13 fires once, after the last step', len(ev) == 1 and ev[0] == last + 1, ev)
    gp = [l for l in o if l.startswith('api GetPalette')][0]
    check('  GetPalette after the fade = the last step', bytes.fromhex(gp.split('hex=')[1]) == b''.join(bytes(c) + b'\0' for c in tr[-1][1]))
o = run('page R4; palettetrace on; api SetPalette %s 4 2; advance 700; slot day; advance 3000' % NIGHT)
check('a debug key (slot) cancels a running fade: no further steps after it', len(pal_lines(o)) == 1 and o.count('event palettechanged') == 0, len(pal_lines(o)))

# ============================================================ blocking fades (speed 0..3, percent, back, keep index)
tgt = read_pal(DAYP)
SP = {0: (30, 0), 1: (60, 1), 2: (100, 16), 3: (100, 32)}
for speed in (0, 1, 2, 3):
    for pct, back in ((100, 0), (50, 0), (100, 1)):
        o = run('page R4; pal; palettetrace on; pumpstep 40; api FadeToPalette %s %d %d %d; pal' % (DAYP, back, speed, pct))
        start = hex_pal(one(o, 'pal ')[0].split()[1])
        tr = pal_lines(o)
        N = SP[speed][0]
        n = N * pct // 100
        exp = [fade_pal(start, tgt, N, k) for k in range(1, n + 1)]
        if back:
            exp += [fade_pal(start, tgt, N, k) for k in range(n - 1, -1, -1)] + [start]
        elif pct == 100:
            exp += [tgt]
        ok = [p for _, p in tr] == exp
        times = [t for t, _ in tr]
        if speed == 0:
            tok = len(set(times)) == 1                                   # no waiting at all
        else:
            main = times[:n] if not back else times[:2 * n]
            tok = all(b - a == 40 for a, b in zip(main, main[1:])) and times[0] == 1040   # one host turn (40 ms) per palette
        fin = hex_pal(one(o, 'pal ')[-1].split()[1])
        check('FadeToPalette speed %d percent %d back %d: %d palettes = formula, end state %s, timing' % (speed, pct, back, len(exp), 'start' if back else ('target' if pct == 100 else 'step %d' % n)),
              ok and tok and fin == exp[-1] and 'event palettechanged' not in o, (len(tr), len(exp), times[:4]))

for keep, speed, pct, back in ((None, 2, 100, 0), (7, 1, 60, 0), (200, 3, 100, 1)):
    o = run('page R4; pal; palettetrace on; api FadeToColour 255 128 0 %d %d %d %d' % (back, speed, -1 if keep is None else keep, pct))
    start = hex_pal(one(o, 'pal ')[0].split()[1])
    tr = pal_lines(o)
    N = SP[speed][0]
    n = N * pct // 100
    col = [(255, 128, 0)] * 256
    exp = [fade_pal(start, col, N, k, keep) for k in range(1, n + 1)]
    if back:
        exp += [fade_pal(start, col, N, k, keep) for k in range(n - 1, -1, -1)] + ([start] if pct == 100 else [])
    check('FadeToColour speed %d percent %d back %d keep %s: %d palettes = formula' % (speed, pct, back, keep, len(exp)),
          [p for _, p in tr] == exp, (len(tr), len(exp)))
o = run('page R4; palettetrace on; api SetPalette %s 6 3; advance 700; api FadeToColour 0 0 0 0 0 -1 100; advance 4000' % NIGHT)
check('a blocking fade without "back" cancels the running timed fade (no event 13, no more steps)',
      o.count('event palettechanged') == 0 and len(pal_lines(o)) == 1 + 30, len(pal_lines(o)))
o = run('page R4; palettetrace on; api SetPalette %s 6 3; advance 700; api FadeToColour 0 0 0 1 0 -1 100; advance 9000' % NIGHT)
check('with "back" the timed fade keeps running (original: only the other branch clears DAT_00497a24)',
      o.count('event palettechanged') == 1, o.count('event palettechanged'))

# ============================================================ cross-fade page jump (page 58 -> 59, 8 steps)
pf = bp.PageFile(D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT')


def page_images(name):
    p = pf.page(pf.find(name))
    return p, p.images


def orn_image(page, oname):
    """(x, y, w, h, key rgb or None, rows top-down) of the named ornament"""
    o = [x for x in page.ornaments if x['name'].lower() == oname.lower() and x['type'] == 1][0]
    w, h, st, raw = page.image_pixels(o['image'])
    rows = [raw[(h - 1 - y) * st:(h - 1 - y) * st + w] for y in range(h)]      # bottom-up -> top-down
    return o, w, h, rows


p58, _ = page_images('fce9cab_c')
p59, _ = page_images('fce9cab_o')
_, w, h, from_rows = orn_image(p58, 'image')
_, w2, h2, to_rows = orn_image(p59, 'image')
assert (w, h, w2, h2) == (640, 368, 640, 368)
xft_rel = 'lookups\\clubeden\\ce_day.xft'
xft = open(find_data(xft_rel), 'rb').read()
dayp = read_pal(DAYP)


def blend(frm, to, level):
    out = []
    cache = {}
    for fr, tr_ in zip(frm, to):
        row = bytearray(len(fr))
        for i in range(len(fr)):
            key = (fr[i], tr_[i])
            v = cache.get(key)
            if v is None:
                a, b = dayp[fr[i]], dayp[tr_[i]]
                s = [a[c] * level + b[c] * (1024 - level) for c in range(3)]       # exe:004421d0: 10-bit fixed point sums
                v = xft[((s[0] >> 12) << 12) | ((s[1] >> 12) << 6) | (s[2] >> 12)]  # XFT index R6 G6 B6
                cache[key] = v
            row[i] = v
        out.append(bytes(row))
    return out


pre = ('page fce9cab_c; api EnableHotspot -1 b_locked 0; ')
o = run(pre + 'xfade %s current; pumptrace on; animshots %s/xf; click 300,350; shot %s/xf_after.bmp' % (xft_rel, T, T), '--no-objects')
ev = [l for l in o if (l.startswith('event ') and not l.startswith('event hover') and not l.startswith('event shutdown')) or l.startswith('pump')]
i0 = [i for i, l in enumerate(ev) if l.startswith('event getcrossfade')]
seq = [l.split()[1] if l.startswith('event') else 'pump' for l in ev[i0[0]:]] if i0 else []
check('cross-fade jump: events getcrossfade, exitpage, pageloaded, pagedrawn, then 9 frames (8 steps + 1), then enterpage',
      seq == ['getcrossfade', 'exitpage', 'pageloaded', 'pagedrawn'] + ['pump'] * 9 + ['enterpage'], seq)
ok = True
bad = None
for k in range(9):
    pal_k, rws = read_bmp('%s/xf%03d.bmp' % (T, k))
    exp = blend(from_rows, to_rows, 0x400 - (k * 0x400) // 8)
    got = rws[56:56 + 368]
    if got != exp:
        ok = False
        bad = k
        break
check('cross-fade: all 9 frames equal the independent blend of the two page images through palette and XFT (levels 0x400 .. 0)', ok, bad)
_, rows_after = read_bmp(T + '/xf_after.bmp')
check('cross-fade: the jump lands on the target page showing its own picture', rows_after[56:56 + 368] == to_rows)
o = run(pre + 'xfade - current; pumptrace on; click 300,350')
check('no lookup from the logic: plain jump, no frames', 'pump' not in ' '.join(o) and 'event pageloaded fce9cab_o' in o and o.count('event enterpage fce9cab_o') == 1)
o = run(pre + 'xfade %s - ; pumptrace on; click 300,350' % xft_rel)
check('no palette from the logic: the jump happens, no frames', not [l for l in o if l.startswith('pump')] and 'event enterpage fce9cab_o' in o)
o = run(pre + 'pumptrace on; click 300,350')
check('logic without event 25: plain jump, no frames', not [l for l in o if l.startswith('pump')] and 'event getcrossfade fce9cab_o fce9cab_o' in o and 'event enterpage fce9cab_o' in o)
o = run(pre + 'xfade %s current nosuch; pumptrace on; click 300,350' % xft_rel)
check('alternative definition that does not exist: plain jump', not [l for l in o if l.startswith('pump')] and 'event enterpage fce9cab_o' in o)

# ============================================================ page images on PH3
ph3, _ = page_images('PH3')


def ph3_img(name):
    return orn_image(ph3, name)


SCR = (300, 400)     # view offset on PH3 for the tin / night tests
class Rows(list):
    """screenshot rows with the page surface (2560 x 1024 indices, `surfdump`) the view was projected from"""
    surf = b''


PROJ = po.Projection(75.0)


def with_surf(rows, name, scroll):
    r = Rows(rows)
    r.surf = open('%s/%s.raw' % (T, name), 'rb').read()
    exp = PROJ.render(r.surf, scroll[0], scroll[1])
    r.projected = all(r[56 + y] == exp[y * 640:(y + 1) * 640] for y in range(368))   # exe:0040cf00: the view is the projection of the surface
    return r


base_o = run('page PH3; scroll %d,%d; surfdump %s/ph3_base.raw; shot %s/ph3_base.bmp' % (SCR[0], SCR[1], T, T), '--no-objects')
bpal, base = read_bmp(T + '/ph3_base.bmp')
base = with_surf(base, 'ph3_base', SCR)
check('PH3 base view is the projection of its page surface', base.projected)
KEY = (0, 255, 255)      # the file stores the colour key as B,G,R: cyan
key_idx = bpal.index(KEY)


def view_px(rows, px, py):
    return rows.surf[py * 2560 + px]   # page surface pixel (the ornaments are painted into the surface)


def check_paint(rows_after, rows_before, name, x, y, widthlimit=None, label='', viewonly=False):
    o, w, h, img = ph3_img(name)
    tr_ = o['flags'] & 0x10
    ww = w if widthlimit is None else min(w, widthlimit)
    exp_s = bytearray(rows_before.surf)
    for yy in range(h):
        for xx in range(w):
            py, px = y + yy, x + xx
            if xx < ww and not (tr_ and img[yy][xx] == key_idx):
                exp_s[py * 2560 + px] = img[yy][xx]
    if viewonly:   # the page outside the view may differ (game logic attached): compare what is shown
        exp = PROJ.render(bytes(exp_s), SCR[0], SCR[1])
        return None if all(rows_after[56 + yy] == exp[yy * 640:(yy + 1) * 640] for yy in range(368)) else ('view',)
    if bytes(exp_s) != rows_after.surf:   # only the ornament rectangle changed in the page surface
        i = next(i for i in range(len(exp_s)) if exp_s[i] != rows_after.surf[i])
        return ('surface', i % 2560, i // 2560)
    if not rows_after.projected:
        return ('view is not the projection of the surface',)
    return None


def shot_rows(script, name):
    run('page PH3; scroll %d,%d; %s; surfdump %s/%s.raw; shot %s/%s.bmp' % (SCR[0], SCR[1], script, T, name, T, name), '--no-objects')
    return with_surf(read_bmp('%s/%s.bmp' % (T, name))[1], name, SCR)


for nm, x, y, args in (('night', 622, 447, 'night -1 -1 1 0 -1'), ('TIN3', 441, 434, 'TIN3 -1 -1 1 0 -1'),
                       ('night', 330, 480, 'night 330 480 1 0 -1'), ('empty', 360, 700, 'empty 360 700 0 0 -1')):
    r = shot_rows('api DisplayImage -1 ' + args, 'p_' + nm)
    d = check_paint(r, base, nm, x, y)
    check('DisplayImage %s at (%d,%d): only the ornament rectangle changes, to the ornament image%s' % (nm, x, y, ' (key colour transparent)' if nm.startswith('TIN') else ''), d is None, d)
r = shot_rows('api DisplayImage -1 night -1 -1 1 0 60', 'p_wl')
check('DisplayImage with a width limit of 60 paints only 60 columns', check_paint(r, base, 'night', 622, 447, 60) is None)
r = shot_rows('api DisplayImage -1 night -1 -1 1 0 0', 'p_wl0')
check('width limit 0 paints nothing (and succeeds)', r == base)
o = run('page PH3; api DisplayImage -1 nosuch; api DisplayImage 5 night; api DisplayImage -1 night -1 -1 1 0 -1 slot; api DisplayImage 0 night; api DisplayImage 1 night', '--no-objects')
check('DisplayImage errors: unknown image 7, unknown window 3; topbar (window 0) has no such image', [l.split()[2] for l in o if l.startswith('api')] == ['rc=7', 'rc=3', 'rc=0', 'rc=7', 'rc=3'], [l for l in o if l.startswith('api')])
# SetImageVisibility: flag only, the picture changes at the next paint of the page
r = shot_rows('api SetImageVisibility -1 night 1', 'p_vis1')
check('SetImageVisibility(show) alone changes nothing on screen (exe:00403390 only flips the flag)', r == base)
r = shot_rows('api SetImageVisibility -1 night 1; api RedrawPage', 'p_vis2')
check('show + RedrawPage: the ornament is painted (ReRender, table order)', check_paint(r, base, 'night', 622, 447) is None)
r = shot_rows('api SetImageVisibility -1 night 1; api RedrawPage; api SetImageVisibility -1 night 0; api RedrawPage', 'p_vis3')
check('hide + RedrawPage: the opaque page picture is painted over it again -> original page', r == base)
r = shot_rows('api SetImageVisibility -1 night 1; page O1; page PH3; scroll %d,%d; api RedrawPage' % SCR, 'p_vis4')
check('ornament flags are run-time state of the loaded page: leaving and re-entering resets them (night hidden again)', r == base)
r = shot_rows('api DisplayImage -1 night -1 -1 1 0 -1; page O1; page PH3; scroll %d,%d' % SCR, 'p_vis5')
check('DisplayImage paints do not survive leaving the page either (the page record is reloaded)', r == base)
r = shot_rows('api DisplayImage -1 night -1 -1 1 0 -1; slot dusk; slot day', 'p_reb')
check('DisplayImage paints are re-applied when the surface is re-baked (palette change)', check_paint(r, base, 'night', 622, 447) is None)
r = shot_rows('api SetImageVisibility -1 night 1; gamelogic on; api SetImageVisibility -1 empty 1; slot dusk; slot day', 'p_reb2')
check('hidden/visible flags survive a re-bake too', check_paint(r, base, 'night', 622, 447, viewonly=True) is None)
o2, wn, hn, _ = ph3_img('night')
# slots
o = run('page PH3; scroll %d,%d; api DisplayImage -1 night -1 -1 1 0 -1 slot; api DisplayImage -1 TIN3 -1 -1 1 0 -1 slot; api ReleasePageImageSlot -1 0; '
        'api DisplayImage -1 TIN3 -1 -1 1 0 -1 slot; api ReleasePageImageSlot -1 7; api ReleasePageImageSlot 5 0; api ReleasePageImageSlot -1 0; api ReleasePageImageSlot -1 0' % SCR, '--no-objects')
res = [l.split()[2:] for l in o if l.startswith('api')]
check('slots: numbered from 0, reused after release; release of a free slot -> 0x27 (39), bad window -> 3',
      res == [['rc=0', 'slot=0'], ['rc=0', 'slot=1'], ['rc=0'], ['rc=0', 'slot=0'], ['rc=39'], ['rc=3'], ['rc=0'], ['rc=39']], res)
r = shot_rows('api DisplayImage -1 night -1 -1 1 0 -1 slot; api ReleasePageImageSlot -1 0', 'p_sl1')
check('ReleasePageImageSlot restores exactly what the image covered (no key)', r == base)
r = shot_rows('api DisplayImage -1 TIN3 -1 -1 1 0 -1 slot; api ReleasePageImageSlot -1 0', 'p_sl2')
check('slot of a transparent image without the keyed flag: whole rectangle restored', r == base)
r = shot_rows('api DisplayImage -1 TIN3 -1 -1 1 1 -1 slot; api DisplayImage -1 night 430 430 1 0 -1; api ReleasePageImageSlot -1 0', 'p_sl3')
mid = shot_rows('api DisplayImage -1 TIN3 -1 -1 1 1 -1 slot; api DisplayImage -1 night 430 430 1 0 -1', 'p_sl3mid')
ot, tw, th, timg = ph3_img('TIN3')
bad = None
for yy in range(th):
    for xx in range(tw):
        px, py = 441 + xx, 434 + yy
        if timg[yy][xx] != key_idx and timg[yy][xx] != view_px(base, px, py):
            exp = view_px(base, px, py)            # the pixels TIN3 had changed go back to the page
        else:
            exp = view_px(mid, px, py)             # the others keep what was painted meanwhile (the night image)
        if view_px(r, px, py) != exp:
            bad = (xx, yy)
check('keyed slot release (unk08 = 1, transparent image): only the pixels the image changed are restored, the rest keeps later paints', bad is None, bad)
o = run('page PH3; ' + '; '.join(['api DisplayImage -1 empty 0 0 0 0 -1 slot'] * 17), '--no-objects')
sl = [l.split('slot=')[1] for l in o if 'slot=' in l]
check('16 slots, the 17th request gets -1', sl == [str(i) for i in range(16)] + ['-1'], sl)
# RaisePageImage: the record moves to the end of the table
r = shot_rows('api SetImageVisibility -1 night 1; api RaisePageImage -1 image; api RedrawPage', 'p_raise')
check('RaisePageImage: the page picture is now painted last by RedrawPage and covers the shown ornament', r == base)
o = run('page PH3; api RaisePageImage -1 nosuch; api RaisePageImage -1 night; api RaisePageImage 5 night; api RaisePageImage -1 night', '--no-objects')
check('RaisePageImage errors: 7 unknown, 3 bad window; raising the last record is fine', [l.split()[2] for l in o if l.startswith('api')] == ['rc=7', 'rc=0', 'rc=3', 'rc=0'])

# objects: an object lying under / over a displayed ornament
fo = ph3_img  # (unused alias)
p_o1, _ = page_images('O1')
o1x, o1w, o1h, o1img = orn_image(p_o1, 'fo1htch_o')
SCR1 = (500, 450)
sc = 'page O1; scroll %d,%d' % SCR1


def o1_rows(script, name, *opts):
    run('%s; %s; surfdump %s/%s.raw; shot %s/%s.bmp' % (sc, script, T, name, T, name), *opts)
    return with_surf(read_bmp('%s/%s.bmp' % (T, name))[1], name, SCR1)


nob = o1_rows('api EnableUserInput 1', 'o1_noobj', '--no-objects')
ob = o1_rows('api EnableUserInput 1', 'o1_obj')
kx, ky, kw, kh = 694, 685, 92, 46                       # kukri rectangle in its zone (dump objects)
mask = [[ob.surf[(ky + j) * 2560 + kx + i] != nob.surf[(ky + j) * 2560 + kx + i] for i in range(kw)] for j in range(kh)]
check('(setup) the kukri changes pixels of page O1', sum(map(sum, mask)) > 200, sum(map(sum, mask)))
bpal1 = read_bmp(T + '/o1_obj.bmp')[0]
key1 = bpal1.index(KEY)
ox, oy = 640, 560                                       # overlaps the kukri rectangle
aft = o1_rows('api DisplayImage -1 fo1htch_o %d %d 1 0 -1' % (ox, oy), 'o1_disp')
bad = None if aft.projected else ('view',)
for yy in range(o1h):
    for xx in range(o1w):
        px, py = ox + xx, oy + yy
        under = nob.surf[py * 2560 + px]
        over = o1img[yy][xx] if o1img[yy][xx] != key1 else under        # background (no object) with the ornament on top
        i, j = px - kx, py - ky
        if 0 <= i < kw and 0 <= j < kh and mask[j][i]:
            over = ob.surf[py * 2560 + px]                              # the object stays on top
        if aft.surf[py * 2560 + px] != over:
            bad = (px, py)
check('object under a displayed ornament: the object is painted on top of the new image, the rest is the image', bad is None, bad)
rel = o1_rows('api DisplayImage -1 fo1htch_o %d %d 1 0 -1 slot; api ReleasePageImageSlot -1 0' % (ox, oy), 'o1_rel')
check('slot released over an object: page and object back exactly as before (no ghost)', rel == ob)
rel = o1_rows('api DisplayImage -1 fo1htch_o %d %d 1 0 -1 slot; api ReleasePageImageSlot -1 0; api RemoveGameObject kukri' % (ox, oy), 'o1_rm')
rem = o1_rows('api RemoveGameObject kukri', 'o1_rm0')
check('taking the object afterwards restores the page behind it (save-under intact)', rel == rem)

# provisional rule vs game logic attached (O1: every ornament starts hidden)
prov = o1_rows('api EnableUserInput 1', 'o1_prov', '--no-objects')
_, hw, hh, himg = orn_image(p_o1, 'h_high')
okp = prov.surf == b''.join(bytes(r) for r in himg) and prov.projected
check('no game logic: O1 (all ornaments hidden) shows its first ornament h_high (provisional rule)', okp)
att = o1_rows('gamelogic on', 'o1_att', '--no-objects')
bpal_a = read_bmp(T + '/o1_att.bmp')[0]
blk = min(range(256), key=lambda i: sum(c * c for c in bpal_a[i]))
check('game logic attached: nothing is painted but the flagged ornaments (page stays black)', all(set(r) == {blk} for r in att[56:56 + 368]), set(att[100]))
sh = o1_rows('gamelogic on; api SetImageVisibility -1 h_high 1; api RedrawPage', 'o1_sh', '--no-objects')
check('game logic shows h_high through SetImageVisibility + RedrawPage: same picture', sh[56:56 + 368] == prov[56:56 + 368])

# ============================================================ ClearWindow / FillScreen
bpal_c, base_c = read_bmp(T + '/ph3_base.bmp')
r = shot_rows('api ClearWindow -1 7', 'c1')
check('ClearWindow(-1, 7): the view rectangle is index 7, the rest of the screen untouched',
      all(set(x) == {7} for x in r[56:424]) and r[:56] == base_c[:56] and r[424:] == base_c[424:])
r = shot_rows('api ClearWindow -3 9', 'c2')
check('ClearWindow(-3, 9): the status bar rectangle', all(set(x) == {9} for x in r[424:]) and r[:424] == base_c[:424])
r = shot_rows('api ClearWindow -1 7; scroll 310,400', 'c3')
check('a fill lasts until the next page blit: scrolling shows the page again', r[56:424] != [bytes([7]) * 640] * 368 and all(set(x) != {7} for x in r[100:110]))
r = shot_rows('api FillScreen 5', 'c4')
check('FillScreen(5): the whole 640x480 frame', all(set(x) == {5} for x in r))
r = shot_rows('api FillScreen 5; api RedrawPage', 'c5')
check('RedrawPage after FillScreen shows the page again', r == base_c)
o = run('page PH3; api ClearWindow 9 1; api ClearWindow 1 1; api ClearWindow 0 1', '--no-objects')
check('ClearWindow on an unknown / not open window -> 0x11, top bar window works', [l.split()[2] for l in o if l.startswith('api')] == ['rc=17', 'rc=17', 'rc=0'])

# ============================================================ page filter (event 42)
r = shot_rows('filtertest on; api BeginPageFilter 11 22 33', 'f1')
check('BeginPageFilter: OnBlitPage gets (a, c, b) on the view after the page blit',
      [r[56][0], r[57][0], r[58][0]] == [11, 33, 22] and set(r[56]) == {11} and r[59] == base_c[59] or set(r[56]) == {11} and r[59][5:] == base_c[59][5:], [r[56][0], r[57][0], r[58][0]])
r = shot_rows('filtertest on; api BeginPageFilter 11 22 33; api EndPageFilter', 'f2')
check('EndPageFilter: the page is shown unfiltered again', r == base_c)
r = shot_rows('filtertest on; api BeginPageFilter 1 2 3; api SetPageFilterBuffer 4', 'f3')
check('SetPageFilterBuffer hands the buffer to the handler', r[59][0] == 0x5a, r[59][0])
o = run('page PH3; api SetPageFilterBuffer 4; api BeginPageFilter 1 2 3; api SetPageFilterBuffer 0; api SetPageFilterBuffer 4; api EndPageFilter; api EndPageFilter', '--no-objects')
check('SetPageFilterBuffer without BeginPageFilter -> 0x11; EndPageFilter is always OK', [l.split()[2] for l in o if l.startswith('api')] == ['rc=17', 'rc=0', 'rc=0', 'rc=0', 'rc=0', 'rc=0'])
r = shot_rows('filtertest on; api BeginPageFilter 11 22 33; page O1; page PH3; scroll %d,%d' % SCR, 'f4')
check('a page load switches the filter off', r == base_c)

# ============================================================ JigglePage
def jig(script):
    o = run('page PH3; api SetScrollMode 1; ' + script + '; dump state', '--no-objects')
    sc = re.search(r'page PH3 scroll (-?\d+),(-?\d+)', '\n'.join(o))
    return o, (int(sc.group(1)), int(sc.group(2))), len([l for l in o if l.startswith('pump')])


o, sc_, n = jig('scroll 400,300; pumptrace on; api JigglePage 0 0 0 40 0 10 0')
check('JigglePage dx=40 in steps of 10: the view moves 40 px in 4 turns', sc_ == (440, 300) and n == 4 and 'api JigglePage rc=0' in o, (sc_, n))
o, sc_, n = jig('scroll 400,300; pumptrace on; api JigglePage 0 0 0 -30 25 0 0')
check('JigglePage with step 0: one turn, the whole offset (y clamped to the page)', sc_ == (370, 325) and n == 1, (sc_, n))
o, sc_, n = jig('scroll 5,5; pumptrace on; api JigglePage 0 0 0 0 -40 0 10')
check('JigglePage stops when the view stops moving at an edge (y 5 -> 0, one more turn)', sc_ == (5, 0) and n == 2, (sc_, n))
o, sc_, n = jig('scroll 400,300; pumptrace on; api JigglePage 1 0 1 10 0 0 0 0 10 0 0')
check('JigglePage repeat count 1, records 0..1: 2 x 2 turns', sc_ == (400 + 20, 300 + 20) and n == 4, (sc_, n))
o, sc_, n = jig('scroll 2550,300; pumptrace on; api JigglePage 0 0 0 20 0 0 0')
check('JigglePage wraps on a panorama', sc_ == (10, 300), sc_)
o, _, _ = jig('api JigglePage 0 0 0; api JigglePage 0 5 2 1 0 0 0; api JigglePage 0 0 0 1 1 0 0')
check('JigglePage: no records -> 0x12, last < first -> 0x12, valid -> 0', [l.split()[2] for l in o if l.startswith('api JigglePage')] == ['rc=18', 'rc=18', 'rc=0'])
o = run('page fce9cab_c; pumptrace on; api JigglePage 0 0 0 40 0 10 0', '--no-objects')
check('JigglePage on a page that does not scroll: OK, nothing happens', 'api JigglePage rc=0' in o and not [l for l in o if l.startswith('pump')], o[-3:])

# ============================================================ coverage table
cov = subprocess.run(['build/biosys-apicoverage'], capture_output=True, text=True).stdout.splitlines()
want = {'DisplayImage', 'SetImageVisibility', 'SetPalette', 'GetPalette', 'FadeToColour', 'FadeToPalette', 'ClearWindow', 'FillScreen',
        'ReleasePageImageSlot', 'RaisePageImage', 'JigglePage', 'BeginPageFilter', 'EndPageFilter', 'SetPageFilterBuffer'}
impl = {l.split()[1] for l in cov if len(l.split()) > 2 and l.split()[2] == 'implemented'}
check('apicoverage lists %d palette / page-image commands as implemented' % len(want), want <= impl, want - impl)

print('palette/image tests: %s' % ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
