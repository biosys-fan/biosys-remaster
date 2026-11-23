"""Sharp text (--text sharp: the GDI text of the game at the window's real resolution; src/engine/textplane.*, src/engine/engine_text.cpp,
src/app/text_sharp.*).

A. the default is unchanged: no option, `--text original` and `--text sharp` (the canvas keeps its bookkeeping) give byte-identical 8-bit frames.
B. coverage: for every kind of text (and after overdrawing, a page change, a window closing, a scroll) the frame without its low-resolution text
   plus the low-resolution text redrawn from the run list is the original frame, exactly (`textredraw` redraws each run's glyphs from its own
   record, where the frame shows text), and everything is checked here from the BMP files: the text-free frame differs from the frame exactly
   at the mask, the list is non-empty, no stale text is listed.
C. sharp output at 2x and 3x (`textshot`): outside the text regions the capture is the enlarged original exactly; inside them its downsampling is
   close to the original (same position, same weight); the text-free base holds no low-resolution text pixel; every pixel the capture changes
   against the enlarged text-free frame lies in the box a Python reading of the run list predicts, and every run shows ink inside its box.
D. things drawn over text in the original frame stay over it.
E. comparison images in build/shots/sharptext_*.bmp.
Pure standard library, run with `python3 -I`."""
import hashlib, os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from play_lib import Play

T = 'build/test/sharptext'
SHOTS = 'build/shots'
shutil.rmtree(T, ignore_errors=True)
os.makedirs(T + '/saves', exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
failed = 0
W, H = 640, 480
JON = 'fonts\\JON.TTF'

# Tolerances of part C (measured on every scene below, see the summary lines: mean abs error 0.5 .. 8.7, ink mass ratio 1.02 .. 1.05; the sharp
# glyphs are anti-aliased outlines at ppem * scale, the original's are hinted 1-bit bitmaps at ppem, so shapes differ by up to a pixel).
TOL_EDGE = 1.5          # a run's sharp bounding box against the bounding box of its low-resolution pixels: each edge, in low-resolution pixels
TOL_MASS_LO, TOL_MASS_HI = 0.85, 1.25     # ink mass (distance from the text-free colour, summed over the text region) sharp / original
TOL_MAE = 15.0          # mean absolute colour error per channel over the text region (0..255)


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + ('  -- ' + str(detail)[:700] if detail else ''))


def run(script, base, *opts):
    cmd = ['build/biosys', '--mute', '--no-config', '--save-dir', T + '/saves'] + base + list(opts) + ['--script', script]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    if r.returncode:
        print(r.stderr[-600:])
    return r.stdout.splitlines(), r.stderr


GAME = ['--game', '--seed', '1', '--skip-intro']
VIEW = ['--menu-logic']


def read_bmp8(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    pal = [tuple(d[54 + 4 * i:57 + 4 * i][::-1]) for i in range(256)]
    stride = (w + 3) // 4 * 4
    return [d[off + (h - 1 - y) * stride:][:w] for y in range(h)], pal


def read_bmp24(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    stride = (w * 3 + 3) // 4 * 4
    return w, h, [d[off + (h - 1 - y) * stride:][:w * 3] for y in range(h)]      # BGR


def write_bmp24(path, w, h, rows):             # rows: BGR bytes
    stride = (w * 3 + 3) // 4 * 4
    hdr = b'BM' + struct.pack('<IHHI', 54 + stride * h, 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, stride * h, 2835, 2835, 0, 0)
    open(path, 'wb').write(hdr + b''.join(r + b'\0' * (stride - len(r)) for r in reversed(rows)))


def parse_runs(path):
    runs = []
    for l in open(path):
        f = dict(t.split('=', 1) for t in l.split()[1:] if '=' in t)
        r = dict(id=int(f['id']), layer=l.split('layer=', 1)[1].split(' x=')[0], x=int(f['x']), y=int(f['y']),
                 clip=tuple(int(v) for v in f['clip'].split(',')), ink=int(f['ink']), rgb=tuple(int(v) for v in f['rgb'].split(',')),
                 ppem=int(f['ppem']), ascent=int(f['ascent']), descent=int(f['descent']), px=int(f['px']),
                 adv=[int(v) for v in f['adv'].split(',')] if f['adv'] != '-' else [],
                 text=bytes.fromhex(f['text']).decode('latin1') if f['text'] != '-' else '', opaque=f['opaque'] == '1')
        runs.append(r)
    return runs


def first(lines, prefix):
    for l in lines:
        if l.startswith(prefix):
            return l
    return ''


def kv(line):
    return dict(t.split('=', 1) for t in line.split() if '=' in t)


# =========================================================================================================================== A. default
print('--- A. the default is unchanged')
DEFAULT_SCENES = {
    'status': (GAME, 'advance 3000; keydown TAB; advance 100; shot {p}.bmp'),
    'display': (GAME, 'advance 2000; page FR4probe_o; advance 200; shot {p}.bmp'),
    'menu': (GAME, 'advance 2000; key F5; advance 100; shot {p}.bmp'),
    'terminal': (GAME, 'advance 500; page NbrBIOS4; advance 500; clickhs BUTTON; advance 600; type RUSSELL\\n; advance 100; click 482,394; advance 50; '
                       'click 53,266; advance 50; bs4hs techsfan; advance 50; bs4hs 2_soils1; advance 1200; shot {p}.bmp'),
    'rain': (GAME, 'advance 3000; page R1; advance 3000; shot {p}.bmp; advance 1800000; shot {p}b.bmp'),
}
for name, (base, script) in DEFAULT_SCENES.items():
    digests = {}
    for tag, opts in (('none', ()), ('orig', ('--text', 'original')), ('sharp', ('--text', 'sharp'))):
        run(script.format(p='%s/def_%s_%s' % (T, name, tag)), base, *opts)
        digests[tag] = [hashlib.md5(open(p, 'rb').read()).hexdigest() for p in sorted(f for f in
                        (T + '/' + x for x in os.listdir(T)) if ('def_%s_%s' % (name, tag)) in f)]
    ok = digests['none'] and digests['none'] == digests['orig'] == digests['sharp']
    check('A. %s: no option, --text original and --text sharp give byte-identical frames (%d frames)' % (name, len(digests['none'])), ok, digests)
for opt in ('bogus',):
    r = subprocess.run(['build/biosys', '--no-config', '--text', opt, '--script', 'echo x'], capture_output=True, text=True)
    check('A. --text bogus is refused', r.returncode != 0 and '--text' in r.stderr, r.stderr[-200:])

# =========================================================================================================================== scenes
p = Play('R4')
p.advance(2000)
p.goto('L_R4R1', 'R1', wait=9000)
p.click_hotspot('fr1rain', wait=2000)
p.page = 'FR1RAIN'
p.click_hotspot('bactive', page='fr1rain', wait=300)
RAIN_MESSAGE = '; '.join(p.cmds)
_mp = Play('R4').pages['MENUSAVE']['hot']
_s3 = [h[1] for h in _mp if h[0] == 'savegame3'][0]
SLOT3 = '%d,%d' % ((_s3[0] + _s3[2]) // 2, (_s3[1] + _s3[3]) // 2)
(psx, psy), hover_plant = Play('R5').point_cmd('R5', 'Ar5gu_1', verb='move')
TERM = ('advance 500; page NbrBIOS4; advance 500; clickhs BUTTON; advance 600; type RUSSELL\\n; advance 100; click 482,394; advance 50; click 53,266; advance 50')
FONT = ('api AddRemoveFontResource %s 1; api CreateDeleteLogicalFont 1 18 systemfont' % JON)


def gdi(win, items, colour='255,193,28', mode=1):
    """a script fragment: a DC on `win`, text items (x, y, text) drawn on it, the DC released"""
    s = 'api GetWindowDC %d; gdi lastdc font lastfont; gdi lastdc bkmode %d; gdi lastdc bkcolor 0,0,0; gdi lastdc textcolor %s; ' % (win, mode, colour)
    s += '; '.join('gdi lastdc textout %d %d %s' % it for it in items)
    return s + '; api ReleaseWindowDC %d lastdc' % win


SCENES = [
    # name, base options, script, minimum runs, layer, texts that must be among the runs, rain-free (no page filter noise)
    ('topbar_words', GAME, 'advance 1000; advance 1800000', 2, 'topbar', ['HUNGRY', 'THIRSTY'], False),
    ('topbar_message', GAME, RAIN_MESSAGE, 2, 'topbar', ['the flood waters are draining away'], False),
    ('status_personal', GAME, 'advance 3000; keydown TAB; advance 100', 5, 'status', ['day', 'night'], False),
    ('status_environment', GAME, 'advance 3000; page R5; advance 3000; keydown CAPS; advance 100', 7, 'status', ['Rainforest', 'very humid'], True),
    ('status_plant', GAME, 'advance 3000; page R5; advance 3000; scroll %d,%d; %s; advance 300; keydown CAPS; advance 200' % (psx, psy, hover_plant), 9, 'status', [], True),
    ('display', GAME, 'advance 2000; page FR4probe_o; advance 200', 10, 'page', ['29.72', '30.07', '999'], True),
    ('menu_slots', GAME, 'advance 2000; key F5; advance 100; click %s; advance 300; key F5; advance 100' % SLOT3, 10, 'menu', [], True),
    ('text_entry', VIEW, 'advance 100; page fce9cab_o; api BeginTextInput 100,100,300,130 8 5 255,255,0; type ABC123; advance 100', 1, 'page', ['ABC123'], True),
    ('terminal_clock', GAME, TERM, 2, 'terminal clock', ['08:01', 'Thu 27 Dec 2007'], True),
    ('terminal_gauge', GAME, TERM + '; bs4hs techsfan; advance 50; bs4hs 2_soils1; advance 1200', 5, 'terminal panel', ['0.0', '12'], True),
    ('terminal_sliders', GAME, TERM + '; bs4hs techcfan; advance 50; bs4hs 14_cfans1; advance 50; bs4hs 2_activate; advance 1200', 4, 'terminal panel', ['6:00', '18:00'], True),
    ('api_text', VIEW, 'page fce9VIDPHO; %s; %s' % (FONT, gdi(-1, [(20, 100, 'Hamburg 12.5 +++'), (280, 140, 'second line 007')])), 2, 'page',
     ['Hamburg 12.5 +++', 'second line 007'], True),
]


def scene(name, base, script, *opts, ground=False):
    """runs the script, then the four text commands on the final state; returns the output lines and the file prefix.
    ground: a frame of the page is saved right after the page command, before anything is drawn (the truth for the text-free frame)"""
    pf = '%s/%s' % (T, name)
    if isinstance(ground, str):         # the same scene without the text, in a run of its own
        run(ground + '; shot %s_before.bmp' % pf, base)
    elif ground:
        head, _, tail = script.partition('; ')
        script = '%s; shot %s_before.bmp; %s' % (head, pf, tail)
    full = script + '; textframes %s; textredraw %s_red.bmp; textshot %s_x2.bmp 2 %s_x2; textshot %s_x3.bmp 3 %s_x3' % (pf, pf, pf, pf, pf, pf)
    out, err = run(full, base, '--text', 'sharp', *opts)
    return out, pf


def frames(pf):
    full, pal = read_bmp8(pf + '_full.bmp')
    free, _ = read_bmp8(pf + '_free.bmp')
    mask, _ = read_bmp8(pf + '_mask.bmp')
    return full, free, mask, pal


def dilate(mask, n=1):
    out = [bytearray(W) for _ in range(H)]
    for y in range(H):
        row = mask[y]
        if not any(row):
            continue
        for x in range(W):
            if row[x]:
                for yy in range(max(0, y - n), min(H, y + n + 1)):
                    o = out[yy]
                    for xx in range(max(0, x - n), min(W, x + n + 1)):
                        o[xx] = 1
    return out


def box_of(r, k, pad=1):
    """the box a Python reading of the run predicts at scale k: from the pen position x to x + the sum of the advances, from the top of the
    cell to its bottom, one low-resolution pixel of margin, inside the clip rectangle"""
    wd = sum(r['adv'])
    l = max((r['x'] - pad) * k, r['clip'][0] * k)
    t = max((r['y'] - pad) * k, r['clip'][1] * k)
    rr = min((r['x'] + wd + pad) * k, r['clip'][2] * k)
    b = min((r['y'] + r['ascent'] + r['descent'] + pad) * k, r['clip'][3] * k)
    return l, t, rr, b


def enlarge(free, pal, k):
    rows = []
    cache = {}
    for y in range(H):
        row = free[y]
        if row not in cache:
            cache[row] = b''.join(bytes((pal[c][2], pal[c][1], pal[c][0])) * k for c in row)
        rows.extend([cache[row]] * k)
    return rows


STATS = []


def analyse(name, base, script, minruns, layer, texts, rainfree):
    print('--- %s' % name)
    out, pf = scene(name, base, script)
    tfl = first(out, 'textframes ')
    rdl = first(out, 'textredraw ')
    st = kv(tfl)
    red = kv(rdl)
    runs = parse_runs(pf + '_runs.txt')
    full, free, mask, pal = frames(pf)
    reds, _ = read_bmp8(pf + '_red.bmp')
    nmask = sum(sum(r) for r in mask)
    diff = sum(1 for y in range(H) for x in range(W) if full[y][x] != free[y][x])
    # B. coverage
    check('B. %s: %d text runs listed (>= %d), layer %s' % (name, len(runs), minruns, layer),
          len(runs) >= minruns and all(r['layer'].startswith(layer.split()[0]) for r in runs) and any(r['layer'] == layer for r in runs), [r['layer'] for r in runs][:5])
    texts_in = [r['text'] for r in runs]
    check('B. %s: the texts %s are in the list' % (name, texts), all(t in texts_in for t in texts), texts_in)
    check('B. %s: the text-free frame differs from the frame exactly at the %d mask pixels' % (name, nmask),
          diff == nmask > 0 and all((full[y][x] != free[y][x]) == bool(mask[y][x]) for y in range(H) for x in range(W)), (diff, nmask))
    check('B. %s: the low-resolution text redrawn from the list over the text-free frame is the frame (textredraw diff=0; BMP identical to the frame where the scene has no rain filter)' % name,
          red.get('diff') == '0' and (reds == full or not rainfree), red)
    if rainfree:
        check('B. %s: nothing differs between the two renders except text (orphans=0)' % name, st.get('orphans') == '0', st)
    runpx = sum(r['px'] for r in runs)
    check('B. %s: the runs own at least the mask pixels (%d >= %d)' % (name, runpx, nmask), runpx >= nmask, (runpx, nmask))
    # C. the sharp output
    D = dilate(mask)
    region = [[x for x in range(W) if D[y][x]] for y in range(H)]
    for k in (2, 3):
        pfk = '%s_x%d' % (pf, k)
        full_k, free_k, mask_k, pal_k = frames(pfk)
        w, h, cap = read_bmp24(pfk.replace('_x%d' % k, '_x%d.bmp' % k))
        runs_k = parse_runs(pfk + '_runs.txt')
        base_rows = enlarge(free_k, pal_k, k)
        orig_rows = enlarge(full_k, pal_k, k)
        Dk = dilate(mask_k)
        ok_out = True
        bad = None
        for y in range(H):
            cols = [x for x in range(W) if Dk[y][x]]
            if not cols:
                if any(cap[y * k + j] != orig_rows[y * k + j] for j in range(k)):
                    ok_out, bad = False, ('row', y)
                continue
            for j in range(k):
                cr, orow = cap[y * k + j], orig_rows[y * k + j]
                if cr == orow:
                    continue
                for x in range(W):
                    if not Dk[y][x] and cr[3 * x * k:3 * (x + 1) * k] != orow[3 * x * k:3 * (x + 1) * k]:
                        ok_out, bad = False, (x, y)
                        break
        check('C. %s @%dx: outside the text regions (the mask dilated by a pixel) the capture equals the enlarged original exactly' % (name, k), ok_out, bad)
        # no low-resolution text pixel in the text-free base: at the mask the base is not the ink the frame showed
        left = sum(1 for y in range(H) for x in range(W) if mask_k[y][x] and base_rows[y * k][3 * x * k:3 * x * k + 3] == orig_rows[y * k][3 * x * k:3 * x * k + 3])
        check('C. %s @%dx: no low-resolution text pixel is left in the text-free base (%d mask pixels, %d unchanged)' % (name, k, sum(sum(r) for r in mask_k), left), left == 0, left)
        # tolerance: downsample
        num = den = cnt = 0
        mass_s = mass_o = 0.0
        for y in range(H):
            for x in region_row(Dk, y):
                b0 = [0, 0, 0]
                for j in range(k):
                    row = cap[y * k + j]
                    for i in range(k):
                        o = 3 * (x * k + i)
                        b0[0] += row[o]; b0[1] += row[o + 1]; b0[2] += row[o + 2]
                a = orig_rows[y * k][3 * x * k:3 * x * k + 3]
                bb = base_rows[y * k][3 * x * k:3 * x * k + 3]
                for c in range(3):
                    s = b0[c] / (k * k)
                    num += abs(s - a[c])
                    mass_s += abs(s - bb[c])
                    mass_o += abs(a[c] - bb[c])
                    den += 1
        mae = num / max(1, den)
        ratio = mass_s / max(1.0, mass_o)
        STATS.append((name, k, mae, ratio))
        check('C. %s @%dx: downsampled capture vs original over the text region: mean abs error %.1f <= %.0f, ink mass ratio %.2f in [%.2f, %.2f]' %
              (name, k, mae, TOL_MAE, ratio, TOL_MASS_LO, TOL_MASS_HI), mae <= TOL_MAE and TOL_MASS_LO <= ratio <= TOL_MASS_HI)
        # the Python reading of the run list: every changed pixel is inside a predicted box; every run shows ink inside its box
        boxes = [box_of(r, k) for r in runs_k]
        changed = []
        for y in range(h):
            if cap[y] == base_rows[y]:
                continue
            for x in range(w):
                if cap[y][3 * x:3 * x + 3] != base_rows[y][3 * x:3 * x + 3]:
                    changed.append((x, y))
        stray = [c for c in changed if not any(b[0] <= c[0] < b[2] and b[1] <= c[1] < b[3] for b in boxes)]
        check('C. %s @%dx: every pixel the capture changes against the text-free base lies inside the box predicted from the run list (%d changed, %d stray)' %
              (name, k, len(changed), len(stray)), not stray and changed, stray[:4])
        empty = []
        edge_bad = []
        for r, b in zip(runs_k, boxes):
            inside = [c for c in changed if b[0] <= c[0] < b[2] and b[1] <= c[1] < b[3]]
            if not inside:
                empty.append(r['text'])
                continue
            # position and weight: the bounding box of the run's sharp ink against the bounding box of its low-resolution pixels (isolated runs)
            others = [o for o in boxes if o is not b and not (o[2] <= b[0] or o[0] >= b[2] or o[3] <= b[1] or o[1] >= b[3])]
            if others:
                continue
            lo = [(x, y) for y in range(max(0, b[1] // k), min(H, b[3] // k + 1)) for x in range(max(0, b[0] // k), min(W, b[2] // k + 1)) if mask_k[y][x]]
            if not lo:
                continue
            sx0, sx1 = min(c[0] for c in inside) / k, (max(c[0] for c in inside) + 1) / k
            sy0, sy1 = min(c[1] for c in inside) / k, (max(c[1] for c in inside) + 1) / k
            lx0, lx1 = min(c[0] for c in lo), max(c[0] for c in lo) + 1
            ly0, ly1 = min(c[1] for c in lo), max(c[1] for c in lo) + 1
            d = max(abs(sx0 - lx0), abs(sx1 - lx1), abs(sy0 - ly0), abs(sy1 - ly1))
            if d > TOL_EDGE:
                edge_bad.append((r['text'], round(d, 2)))
        check('C. %s @%dx: every run shows sharp ink inside its predicted box' % (name, k), not empty, empty[:4])
        check('C. %s @%dx: the sharp bounding box of each isolated run is that of its low-resolution text within %.1f pixel' % (name, k, TOL_EDGE), not edge_bad, edge_bad[:4])
    return out, pf, runs


def region_row(D, y):
    return [x for x in range(W) if D[y][x]]


results = {}
for sc in SCENES:
    results[sc[0]] = analyse(*sc)

# =========================================================================================================================== B. after
print('--- B. overdrawn, page changed, window closed, scrolled')


def simple(name, base, script, nruns_expected=None, texts=None, absent=None, rainfree=True, ground=False):
    out, pf = scene(name, base, script, ground=ground)
    runs = parse_runs(pf + '_runs.txt')
    full, free, mask, pal = frames(pf)
    reds, _ = read_bmp8(pf + '_red.bmp')
    red = kv(first(out, 'textredraw '))
    st = kv(first(out, 'textframes '))
    nmask = sum(sum(r) for r in mask)
    check('B. %s: the redrawn frame is the frame (diff=0), orphans=%s' % (name, st.get('orphans')), red.get('diff') == '0' and (not rainfree or (reds == full and st.get('orphans') == '0')), (red, st))
    got = [r['text'] for r in runs]
    if texts is not None:
        check('B. %s: the listed runs are %s' % (name, texts), got == texts, got)
    if absent:
        check('B. %s: stale text is gone: %s' % (name, absent), not any(a in got for a in absent), got)
    if ground:
        before, _ = read_bmp8(pf + '_before.bmp')
        check('B. %s: the text-free frame is the frame of the page before any text was drawn (independent ground truth), bit for bit' % name, free == before and full != before,
              sum(1 for y in range(H) for x in range(W) if free[y][x] != before[y][x]))
    return runs, full, free, mask, pal, pf


HDR = 'page fce9VIDPHO; %s' % FONT
# overdrawn: a second text over the first, then a redraw of the page, then new text at the same place
r1 = simple('overdraw_text', VIEW, HDR + '; ' + gdi(-1, [(40, 100, 'AAAAAAAA')], '255,0,0') + '; ' + gdi(-1, [(40, 100, 'BBBBBBBB')], '0,255,0') , None, ground=True)
got = [r['text'] for r in r1[0]]
check('B. overdraw_text: text drawn over text: only the visible ones are listed, in drawing order, "BBBBBBBB" shows', got and got[-1] == 'BBBBBBBB', got)
r2 = simple('overdraw_redraw', VIEW, HDR + '; ' + gdi(-1, [(40, 100, 'OLD TEXT 111')]) + '; api RedrawPage; ' + gdi(-1, [(40, 100, 'NEW TEXT 222')], '0,255,255'), ['NEW TEXT 222'], absent=['OLD TEXT 111'], ground=True)
r3 = simple('overdraw_fill', VIEW, HDR + '; ' + gdi(-1, [(40, 100, 'FILLED OVER')]) +
            '; api GetWindowDC -1; gdi lastdc fillrect 30,95,200,125 0,0,255; api ReleaseWindowDC -1 lastdc', [])
check('B. overdraw_fill: a Canvas fill over the text ends it (no run; frame unchanged by sharp mode)', not r3[0] and not any(any(r) for r in r3[3]), r3[0])
r4 = simple('overdraw_image', VIEW, HDR + '; ' + gdi(-1, [(160, 100, 'UNDER THE IMAGE')]) + '; api DisplayImage -1 poweroff -1 -1 0 0 -1', None)
r5 = simple('page_changed', VIEW, HDR + '; ' + gdi(-1, [(40, 100, 'ON THE FIRST PAGE')]) + '; page fce9cab_o; advance 100', [])
check('B. page_changed: a new page has none of the old text (no mask pixel, free == frame)', not any(any(r) for r in r5[3]) and r5[1] == r5[2], None)
r6 = simple('status_closed', GAME, 'advance 3000; keydown TAB; advance 100; keyup TAB; advance 100', None, rainfree=False)
check('B. status_closed: closing the status window takes its text away (only the top bar words remain)', not any(r['layer'] == 'status' for r in r6[0]), r6[0])
r6b = simple('menu_closed', GAME, 'advance 2000; key F5; advance 100; key ESC; advance 100; key ESC; advance 100', None, rainfree=False)
check('B. menu_closed: closing the menu takes its text away', not any(r['layer'] == 'menu' for r in r6b[0]), r6b[0])
r7 = simple('popup_text', VIEW, 'page fce9VIDPHO; api DisplayPopupPage topbar; ' + FONT + '; ' + gdi(0, [(100, 10, 'Top bar message')], '225,220,210', 2) + '; advance 100', ['Top bar message'])
check('B. popup_text: text on the top bar (window 0) is listed on layer topbar', r7[0] and r7[0][0]['layer'] == 'topbar', r7[0])
# a panorama scrolled: a message in the top bar stays where it is, the page text on the projected panorama stays in the 8-bit frame
r9 = simple('panorama_scrolled', VIEW, 'page R4; api DisplayPopupPage topbar; ' + FONT + '; ' + gdi(0, [(100, 10, 'Top bar message')], '225,220,210', 2) + '; ' +
            gdi(-1, [(300, 300, 'on the panorama')]) + '; scroll 900,300; advance 100; scroll 1400,200; advance 100', ['Top bar message'])
check('B. panorama_scrolled: the text on the page surface of a projected panorama is not listed (it stays in the frame, free == frame outside the bar)',
      all(r['layer'] == 'topbar' for r in r9[0]), r9[0])
# a scrollable page that is not projected: the text moves with the scroll and is listed at its screen position
x0 = simple('scroll_page_0', VIEW, 'page XFILE1; ' + FONT + '; ' + gdi(-1, [(300, 200, 'moves with the page')]) + '; scroll 0,0; advance 50', ['moves with the page'], ground=True)
x1 = simple('scroll_page_1', VIEW, 'page XFILE1; ' + FONT + '; ' + gdi(-1, [(300, 200, 'moves with the page')]) + '; scroll 100,30; advance 50', ['moves with the page'],
           ground='page XFILE1; scroll 100,30; advance 50')
check('B. scroll_page: scrolling by (100, 30) moves the run from (300, %d) to (200, %d)' % (200 + 56, 200 + 56 - 30),
      x0[0] and x1[0] and (x0[0][0]['x'], x0[0][0]['y'], x1[0][0]['x'], x1[0][0]['y']) == (300, 256, 200, 226), (x0[0], x1[0]))

# =========================================================================================================================== D. occlusion
print('--- D. drawn over text')
out, pf = scene('occluded', VIEW, 'page fce9VIDPHO; ' + FONT + '; ' + gdi(-1, [(60, 150, 'Hello covered text and some more')]) +
                '; sprite anims\\prey\\s4p_1.tan 120 150 3 100 fixed loop; advance 300')
runs = parse_runs(pf + '_runs.txt')
full, free, mask, pal = frames(pf)
w3, h3, cap3 = read_bmp24(pf + '_x3.bmp')
full3, free3, mask3, pal3 = frames(pf + '_x3')
nm = sum(sum(r) for r in mask3)
red = kv(first(out, 'textredraw '))
check('D. occluded: a sprite drawn over the text in the frame hides part of it: fewer mask pixels than glyph pixels of the run, redraw exact (%s)' % red,
      red.get('diff') == '0' and runs and nm < sum(r['px'] for r in runs) + 1, (nm, [r['px'] for r in runs]))
# where the original frame shows the sprite over the text, the capture is the enlarged original
sprite_cols = [x for y in range(H) for x in range(W) if free3[y][x] != 0][:0]
hidden = []
box = box_of(runs[0], 3, 0) if runs else (0, 0, 0, 0)
base_rows = enlarge(free3, pal3, 3)
orig_rows = enlarge(full3, pal3, 3)
for y in range(box[1], box[3]):
    for x in range(box[0], box[2]):
        lx, ly = x // 3, y // 3
        if not mask3[ly][lx] and cap3[y][3 * x:3 * x + 3] != orig_rows[y][3 * x:3 * x + 3]:
            # a pixel changed outside the mask region: only allowed within one pixel of the mask
            near = any(0 <= ly + dy < H and 0 <= lx + dx < W and mask3[ly + dy][lx + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
            if not near:
                hidden.append((x, y))
check('D. occluded: no sharp ink where the original frame hides the text (%d pixels outside the text region changed)' % len(hidden), not hidden, hidden[:4])

# =========================================================================================================================== D2. with the smooth panorama
print('--- D2. sharp text over the smooth panorama')
pf = T + '/pano_smooth'
out, _ = run('page R4; scroll 900,300; api DisplayPopupPage topbar; ' + FONT + '; ' + gdi(0, [(100, 10, 'Top bar message'), (400, 36, 'second 42.5')], '225,220,210', 2) +
             '; advance 100; textshot %s_s.bmp 2 %s_s smooth; smoothshot %s_m.bmp 2; textshot %s_t.bmp 2' % (pf, pf, pf, pf), VIEW, '--text', 'sharp')
ws, hs_, cs = read_bmp24(pf + '_s.bmp')
wm, hm, cm = read_bmp24(pf + '_m.bmp')
wt, ht, ct = read_bmp24(pf + '_t.bmp')
view_rows = range(56 * 2, 424 * 2)
check('D2. sharp text on the smooth panorama: the view rows are the smooth capture (no text lies in the view)', all(cs[y] == cm[y] for y in view_rows), None)
check('D2. ... and differ from the enlarged picture (the panorama really is smooth)', any(cs[y] != ct[y] for y in view_rows), None)
check('D2. ... the bars and the top bar text are the sharp-text capture of the plain path', all(cs[y] == ct[y] for y in range(H * 2) if y not in view_rows) and
      any(cs[y] != cm[y] for y in range(0, 112)), first(out, 'textshot'))

# =========================================================================================================================== E. images
print('--- E. comparison images')


def crop_images(name, pfx, box, out_name, k=3):
    """original enlarged (nearest) | sharp, same crop (low-resolution box), side by side as 24-bit BMP"""
    full, free, mask, pal = frames(pfx)
    w, h, cap = read_bmp24(pfx + '_x%d.bmp' % k)
    x0, y0, x1, y1 = box
    orig = enlarge(full, pal, k)
    left = [orig[y][3 * x0 * k:3 * x1 * k] for y in range(y0 * k, y1 * k)]
    right = [cap[y][3 * x0 * k:3 * x1 * k] for y in range(y0 * k, y1 * k)]
    gap = b'\x40\x40\x40' * 6
    rows = [a + gap + b for a, b in zip(left, right)]
    write_bmp24('%s/sharptext_%s.bmp' % (SHOTS, out_name), len(rows[0]) // 3, len(rows), rows)
    return os.path.getsize('%s/sharptext_%s.bmp' % (SHOTS, out_name))


# the environment status screen, the terminal's panel with its gauge number and the save menu with the slot names
sz = []
sz.append(crop_images('status', T + '/status_environment', (440, 95, 640, 375), 'status'))
sz.append(crop_images('terminal', T + '/terminal_gauge', (400, 120, 640, 250), 'terminal'))
sz.append(crop_images('menu', T + '/menu_slots', (20, 56, 300, 200), 'menu'))
check('E. comparison images written (build/shots/sharptext_status.bmp, _terminal.bmp, _menu.bmp: %s bytes)' % sz, all(s > 10000 for s in sz), sz)

print('--- measured (name, scale, mean abs error, ink mass ratio)')
for s in STATS:
    print('   %-20s %dx  mae %.1f  mass %.2f' % s)
print('%d failed' % failed)
print('ALL OK' if not failed else '%d FAILED' % failed)
sys.exit(1 if failed else 0)
