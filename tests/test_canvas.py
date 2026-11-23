"""GDI text and drawing (font.h, canvas.h, API 0x38 0x39 0x3a 0x3b). Expectations are computed here, independently of the engine:
TTF tables parsed in Python (name, head, OS/2, hdmx, hmtx, cmap), the palette search brute force, the line / rectangle rasteriser
rewritten from the documented rules, the page-surface semantics read off exe:0043da90 / 00413770 / 004137e0 (analysis/notes/gdi_usage.md).
The window tests are driven through `--script` (api GetWindowDC ..., gdi ...) and compare 640x480 screenshots pixel by pixel.
Samples are left in build/test/canvas/."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, random, re, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import projection_oracle as po

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
FONTS = D + '/game-cd/files/BIODATA/FONTS'
PAGES = D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'
PAL = D + '/game-cd/files/BIODATA/PALETTES/RFOREST/RF_DAY.PAL'
T = 'build/test/canvas'
os.makedirs(T, exist_ok=True)
BIN = os.environ.get('BIOSYS_BIN', 'build/biosys')
CT = 'build/biosys-canvastest'

failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:600] if detail else ''))


def sh(args, ok_rc=(0,)):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode not in ok_rc:
        raise RuntimeError('%s failed: %s' % (args[:3], r.stderr.strip()[:300]))
    return r.stdout


def run(script):
    cmd = [BIN] + (['--mute'] if BIN.endswith('biosys') else []) + ['--script', script]
    return sh(cmd).splitlines()


def run_err(script):
    cmd = [BIN] + (['--mute'] if BIN.endswith('biosys') else []) + ['--script', script]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


def read_bmp(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    stride = (w + 3) & ~3
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    rows = [bytearray(d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w]) for y in range(h)]
    return w, h, pal, rows


def nearest(pal, r, g, b):
    best, bi = None, 0
    for i, (pr, pg, pb) in enumerate(pal):
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if best is None or d < best:
            best, bi = d, i
    return bi


# ------------------------------------------------------------------------------------------- TTF reader
class Ttf:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        n, = struct.unpack_from('>H', d, 4)
        self.t = {}
        for i in range(n):
            tag, cs, off, ln = struct.unpack_from('>4sIII', d, 12 + 16 * i)
            self.t[tag.decode('latin1')] = (off, ln)
        self.upm, = struct.unpack_from('>H', d, self.t['head'][0] + 18)
        o = self.t['OS/2'][0]
        self.wasc, self.wdesc = struct.unpack_from('>HH', d, o + 74)
        no = self.t['name'][0]
        cnt, so = struct.unpack_from('>HH', d, no + 2)
        self.names = {}
        for i in range(cnt):
            p, e, l, nid, ln, off = struct.unpack_from('>6H', d, no + 6 + 12 * i)
            s = d[no + so + off:no + so + off + ln]
            self.names[(p, nid)] = s.decode('utf-16-be') if p == 3 else s.decode('latin1')
        self.family = self.names.get((3, 1)) or self.names[(1, 1)]
        nm, = struct.unpack_from('>H', d, self.t['hhea'][0] + 34)
        self.nhm = nm
        self.nglyphs, = struct.unpack_from('>H', d, self.t['maxp'][0] + 4)
        self.cmap = {}
        co = self.t['cmap'][0]
        nt, = struct.unpack_from('>H', d, co + 2)
        for i in range(nt):
            p, e, off = struct.unpack_from('>HHI', d, co + 4 + 8 * i)
            if p == 3 and struct.unpack_from('>H', d, co + off)[0] == 4:
                self.cmap = self._fmt4(co + off)
        ho = self.t['hdmx'][0]
        nrec, = struct.unpack_from('>h', d, ho + 2)
        sz, = struct.unpack_from('>i', d, ho + 4)
        self.hdmx = {d[ho + 8 + i * sz]: d[ho + 8 + i * sz + 2:ho + 8 + i * sz + 2 + self.nglyphs] for i in range(nrec)}

    def _fmt4(self, o):
        d = self.d
        segx2, = struct.unpack_from('>H', d, o + 6)
        sc = segx2 // 2
        ends = struct.unpack_from('>%dH' % sc, d, o + 14)
        starts = struct.unpack_from('>%dH' % sc, d, o + 16 + segx2)
        deltas = struct.unpack_from('>%dh' % sc, d, o + 16 + 2 * segx2)
        roff_pos = o + 16 + 3 * segx2
        roffs = struct.unpack_from('>%dH' % sc, d, roff_pos)
        m = {}
        for k in range(sc):
            for c in range(starts[k], ends[k] + 1):
                if c == 0xffff:
                    continue
                if roffs[k] == 0:
                    g = (c + deltas[k]) & 0xffff
                else:
                    p = roff_pos + 2 * k + roffs[k] + 2 * (c - starts[k])
                    g, = struct.unpack_from('>H', d, p)
                    if g:
                        g = (g + deltas[k]) & 0xffff
                m[c] = g
        return m

    def glyph(self, ch):
        c = ord(ch)
        return self.cmap.get(c) or self.cmap.get(0xF000 + c) or 0

    def hmtx(self, g):
        o = self.t['hmtx'][0]
        return struct.unpack_from('>H', self.d, o + 4 * min(g, self.nhm - 1))[0]

    def ppem(self, height):
        if height < 0:
            return -height
        cell = self.wasc + self.wdesc
        return (height * self.upm + cell // 2) // cell

    def asc_desc(self, ppem):
        return (self.wasc * ppem + self.upm // 2) // self.upm, (self.wdesc * ppem + self.upm // 2) // self.upm


FILES = ['JON.TTF', 'HIGHGFB.TTF', 'BRIERLEY.TTF', 'OLDBRI~1.TTF', 'OLDJON.TTF']
FACE = {f: Ttf('%s/%s' % (FONTS, f)) for f in FILES}

# ================================================================== A. fonts load, family names
out = sh([CT, 'faces'] + ['%s/%s' % (FONTS, f) for f in FILES]).splitlines()
fam = {}
for l in out:
    m = re.match(r'face family=(.*) style=.* upm=(\d+) winascent=(\d+) windescent=(\d+) path=(.*)$', l)
    if m:
        fam[os.path.basename(m.group(5))] = (m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)))
check('all five shipped TTFs load', sorted(fam) == sorted(FILES), out)
check('family names read from the name tables: ' + ', '.join('%s=%s' % (f, FACE[f].family) for f in FILES),
      all(fam.get(f, ('',))[0] == FACE[f].family for f in FILES), fam)
check('the game\'s two faces are found by the names the DLL asks for ("systemfont" -> JON.TTF, "HighlandGothicFLF" -> HIGHGFB.TTF)',
      fam['JON.TTF'][0].lower() == 'systemfont' and fam['HIGHGFB.TTF'][0] == 'HighlandGothicFLF')
check('upm and OS/2 win metrics agree with the tables', all(fam[f][1:] == (FACE[f].upm, FACE[f].wasc, FACE[f].wdesc) for f in FILES))

# ================================================================== B. rendering at the sizes the DLL uses
SAMPLE = 'Hamburg 0123456789 +++ 12.5%'
DLL_SIZES = [('JON.TTF', 'systemfont', 18), ('HIGHGFB.TTF', 'HighlandGothicFLF', 48), ('HIGHGFB.TTF', 'HighlandGothicFLF', 72)]
EXTRA = [(f, FACE[f].family, h) for f in FILES for h in (18, -12)]
bad = []
dev = []
for fi, face, h in DLL_SIZES + EXTRA:
    ttf = FACE[fi]
    o = sh([CT, 'text', '%s/%s' % (FONTS, fi), face, str(h), SAMPLE, '%s/text_%s_%d.bmp' % (T, fi.split('.')[0].replace('~', ''), h)]).splitlines()
    m = re.match(r'font family=(.*) ppem=(\d+) ascent=(-?\d+) descent=(-?\d+) height=(\d+)', o[0])
    ppem, asc, desc, hgt = map(int, m.groups()[1:])
    adv = list(map(int, o[1].split()[1:]))
    ex = re.match(r'extent cx=(\d+) cy=(\d+) ftsum=(\d+) ok=(\d)', o[2])
    cx, cy, ftsum, ok = map(int, ex.groups())
    n, x0, y0, x1, y1 = map(int, re.match(r'ink pixels=(\d+) bbox=(-?\d+),(-?\d+),(-?\d+),(-?\d+)', o[3]).groups())
    ea, ed = ttf.asc_desc(ppem)
    problems = []
    if m.group(1) != ttf.family: problems.append('family')
    if ppem != ttf.ppem(h): problems.append('ppem %d != %d' % (ppem, ttf.ppem(h)))
    if (asc, desc, hgt) != (ea, ed, ea + ed): problems.append('metrics %s' % ((asc, desc, hgt),))
    if cx != ftsum or cx != sum(adv) or cy != hgt or not ok: problems.append('extent %d ftsum %d sum %d' % (cx, ftsum, sum(adv)))
    gl = [ttf.glyph(c) for c in SAMPLE]
    if ppem in ttf.hdmx:
        want = [ttf.hdmx[ppem][g] for g in gl]
        if want != adv: problems.append('hdmx widths differ')
    else:
        want = [(ttf.hmtx(g) * ppem + ttf.upm // 2) // ttf.upm for g in gl]
        dev.append(max(abs(a - b) for a, b in zip(adv, want)))
        if any(abs(a - b) > 1 for a, b in zip(adv, want)): problems.append('advances far from hmtx scaling %s vs %s' % (adv, want))
    if n == 0: problems.append('no ink')
    if not (x0 >= 10 - 1 and x1 <= 10 + cx and y0 >= 10 and y1 < 10 + hgt):
        problems.append('ink outside the measured box: bbox %s box x %d..%d y %d..%d' % ((x0, y0, x1, y1), 10, 10 + cx, 10, 10 + hgt))
    if problems:
        bad.append((fi, h, problems))
check('text in 5 fonts x sizes (the DLL\'s 18 / 48 / 72, plus 18 and -12 in all): ppem and cell metrics follow GDI\'s height rule, '
      'extent == sum of FreeType advances == hdmx widths (inside 9..24 ppem) or hmtx within 1 px, ink non-empty and inside the box',
      not bad, bad)
print('     (largest advance deviation from hmtx scaling outside the hdmx range: %d px)' % (max(dev) if dev else 0))
o = sh([CT, 'text', '%s/JON.TTF' % FONTS, 'systemfont', '18', 'Hamburg', T + '/t.bmp']).splitlines()
check('JON.TTF "systemfont" 18 is ppem 26 with cell 14 + 4 = 18', o[0] == 'font family=Systemfont ppem=26 ascent=14 descent=4 height=18', o[0])
a1 = sh([CT, 'text', '%s/JON.TTF' % FONTS, 'SYSTEMFONT', '18', 'Hamburg', T + '/t2.bmp']).splitlines()[0]
check('face names match case-insensitively', a1 == o[0])
w, h_, pal, rows = read_bmp(T + '/t.bmp')
check('rendered glyphs are monochrome (only the two palette entries are used)', {v for r in rows for v in r} <= {0, 1})

# ================================================================== C. word wrap
JF, JFACE = '%s/JON.TTF' % FONTS, 'systemfont'
TEXTS = [
    '+++SEEK TREATMENT - BRONCHIAL INHALERS AVAILABLE FROM DISPENSARY OR IF NECESSARY TAKE PROCESSED EPHEDRA FROM SAVANNAH BIOME+++',
    '  +HIGH CARBON DIOXIDE LEVEL INDICATES REDUCED OXYGEN CONTENT IN AIR',
    'a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h',
    'ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ short words',
]
bad = []
for txt in TEXTS:
    for width in (404, 150, 90, 40):
        o = sh([CT, 'wrap', JF, JFACE, '18', str(width), '0x10', txt]).splitlines()
        adv = list(map(int, o[0].split()[1:]))
        lines = [re.match(r'line \[(.*)\] width=(\d+)$', l) for l in o[1:]]
        got = [m.group(1) for m in lines]
        wid = [int(m.group(2)) for m in lines]
        # independent greedy wrap from the advances: break at the last blank that keeps the line within width; blanks hang
        exp, pos, n = [], 0, len(txt)
        while pos < n:
            q, wsum, last = pos, 0, -1
            while q < n:
                if txt[q] != ' ' and wsum + adv[q] > width and q > pos:
                    break
                wsum += adv[q]
                if txt[q] == ' ':
                    last = q
                q += 1
            if q >= n:
                exp.append(txt[pos:n]); break
            if last > pos:
                e = last
                while e > pos and txt[e - 1] == ' ':
                    e -= 1
                exp.append(txt[pos:e]); pos = last + 1
                while pos < n and txt[pos] == ' ':
                    pos += 1
            else:
                exp.append(txt[pos:q]); pos = q
        if got != exp:
            bad.append((width, got, exp))
        # the property GDI documents: every line fits (unless one word is wider than the rect) and the next word would not have fitted
        for i, (l, lw) in enumerate(zip(got, wid)):
            if lw > width and ' ' in l.strip():
                bad.append((width, 'line too wide', l, lw))
            if i + 1 < len(got) and ' ' in txt:
                nxt = got[i + 1].split(' ')[0] if got[i + 1].strip() else ''
                if nxt and l and not (l.endswith(' ')):
                    joined = l + ' ' + nxt
                    jw = sum(adv[txt.index(joined) + k] for k in range(len(joined))) if joined in txt else None
                    if jw is not None and jw <= width and not any(x for x in bad):
                        bad.append((width, 'not greedy', l, nxt))
check('DrawText word wrap breaks where the measured widths say (4 texts x 4 widths, incl. the 404 px of the spirometer texts)', not bad, bad[:2])
o = sh([CT, 'wrap', JF, JFACE, '18', '404', '0x10', TEXTS[0]]).splitlines()
check('the longest DLL string (0x84, 124 characters) wraps into 3 lines of at most 404 px',
      len(o) - 1 == 3 and all(int(re.search(r'width=(\d+)', l).group(1)) <= 404 for l in o[1:]), o)
o = sh([CT, 'wrap', JF, JFACE, '18', '404', '0x10', 'one\ntwo\r\nthree\rfour\n\nsix\n']).splitlines()
check('line breaks \\n \\r\\n \\r end a line, empty lines count, a final break adds none',
      [re.match(r'line \[(.*)\]', l).group(1) for l in o[1:]] == ['one', 'two', 'three', 'four', '', 'six'], o)
o = sh([CT, 'wrap', JF, JFACE, '18', '404', '0x10', 'Q && A &x']).splitlines()
check('DrawText prefix processing: && = &, & before a character removed', re.match(r'line \[(.*)\]', o[1]).group(1) == 'Q & A x', o)

# ================================================================== D. palette mapping
random.seed(1998)
pal = []
d = open(PAL, 'rb').read()
for i in range(256):
    pal.append((d[0x18 + 4 * i], d[0x18 + 4 * i + 1], d[0x18 + 4 * i + 2]))
# a palette with duplicate entries: the lowest index must win
dup = [(i * 7 % 256, (i * 13) % 256, (i * 29) % 256) for i in range(128)] * 2
with open(T + '/dup.pal', 'wb') as f:
    f.write(b'RIFF' + struct.pack('<I', 0x410) + b'PAL ' + b'data' + struct.pack('<I', 0x404) + struct.pack('<HH', 0x300, 256))
    for r, g, b in dup:
        f.write(bytes((r, g, b, 0)))
colours = [(0, 0, 0), (255, 255, 255), (186, 184, 134), (255, 25, 28), (0, 135, 166), (165, 132, 41), (127, 127, 127)] + \
          [(random.randrange(256), random.randrange(256), random.randrange(256)) for _ in range(60)]
for name, p, pf in (('RF_DAY.PAL', pal, PAL), ('duplicate-entry palette', dup, T + '/dup.pal')):
    o = sh([CT, 'map', pf] + ['%d,%d,%d' % c for c in colours]).splitlines()
    got = [tuple(map(int, re.match(r'map \S+ palettergb=(\d+) rgb=(\d+)', l).groups())) for l in o]
    want = [(nearest(p, *c),) * 2 for c in colours]
    check('PALETTERGB / RGB -> nearest palette index equals brute force (%s, %d colours)' % (name, len(colours)), got == want,
          [(c, g, w) for c, g, w in zip(colours, got, want) if g != w][:3])

# ================================================================== E. lines and rectangles
def raster(w, h, ops):
    s = [[0] * w for _ in range(h)]
    pen_w, pen_c, brush, pos = 1, 0, 255, (0, 0)

    def plot(x, y, c):
        if 0 <= x < w and 0 <= y < h:
            s[y][x] = c

    def bres(x0, y0, x1, y1, incl):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err, pts = dx + dy, []
        while True:
            last = x0 == x1 and y0 == y1
            if not last or incl:
                pts.append((x0, y0))
            if last:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy; x0 += sx
            if e2 <= dx:
                err += dx; y0 += sy
        return pts

    for op in ops:
        t = op.split(':')
        n = [int(v) for v in t[1:]]
        if t[0] == 'pen': pen_w, pen_c = n
        elif t[0] == 'brush': brush = n[0]
        elif t[0] == 'move': pos = (n[0], n[1])
        elif t[0] == 'line':
            for x, y in bres(pos[0], pos[1], n[0], n[1], pen_w > 1):
                if pen_w > 1:
                    for j in range(pen_w):
                        for i in range(pen_w):
                            plot(x - pen_w // 2 + i, y - pen_w // 2 + j, pen_c)
                else:
                    plot(x, y, pen_c)
            pos = (n[0], n[1])
        elif t[0] == 'fill':
            for y in range(n[1], n[3]):
                for x in range(n[0], n[2]): plot(x, y, n[4])
        elif t[0] == 'solid':
            for y in range(n[1], n[1] + n[3]):
                for x in range(n[0], n[0] + n[2]): plot(x, y, n[4])
        elif t[0] == 'rect':
            l, tp, r, b = n
            for y in range(tp + 1, b - 1):
                for x in range(l + 1, r - 1): plot(x, y, brush)
            for x in range(l, r): plot(x, tp, pen_c); plot(x, b - 1, pen_c)
            for y in range(tp, b): plot(l, y, pen_c); plot(r - 1, y, pen_c)
    return s


OPS = ['pen:1:200', 'move:5:5', 'line:5:60', 'move:10:60', 'line:10:5', 'move:12:8', 'line:70:8', 'move:12:12', 'line:40:50', 'move:90:50', 'line:50:20',
       'pen:3:150', 'move:20:70', 'line:20:100', 'move:25:100', 'line:80:100', 'move:60:70', 'line:90:95',
       'pen:2:100', 'move:100:10', 'line:100:40', 'move:101:40', 'line:130:40',
       'fill:110:50:140:70:90', 'solid:112:52:10:6:30', 'brush:40', 'pen:1:230', 'rect:100:80:130:95',
       'pen:3:77', 'move:-5:-5', 'line:15:15', 'move:115:100', 'line:140:110', 'move:60:110', 'line:60:130',
       'fill:-10:-10:3:3:222', 'fill:118:100:150:130:11', 'pen:1:5', 'move:30:115', 'line:30:115', 'move:33:115', 'line:34:116']
o = sh([CT, 'draw', '120', '120', T + '/lines.bmp'] + OPS)
w_, h_, pal_, rows = read_bmp(T + '/lines.bmp')
exp = raster(120, 120, OPS)
diff = [(x, y, rows[y][x], exp[y][x]) for y in range(120) for x in range(120) if rows[y][x] != exp[y][x]]
check('lines (1, 2, 3 px pens, all octants, clipped, zero length), FillRect, FillSolidRect, Rectangle match the Python rasteriser pixel for pixel',
      not diff and any(v for r in rows for v in r), diff[:5])
# the vertical / horizontal bars of the DLL's bar graphs: MoveTo(x, base); LineTo(x, base - v) with a 3-px pen
OPS2 = ['pen:3:90', 'move:60:100', 'line:60:30', 'pen:1:90', 'move:70:100', 'line:70:30', 'move:80:30', 'line:120:30']
sh([CT, 'draw', '130', '110', T + '/bars.bmp'] + OPS2)
rows = read_bmp(T + '/bars.bmp')[3]
exp = raster(130, 110, OPS2)
check('bar graph lines: 3-px vertical bar from base to value (end included), 1-px bar and horizontal line stop before the end point',
      all(rows[y][x] == exp[y][x] for y in range(110) for x in range(130)) and rows[100][60] == 90 and rows[30][60] == 90 and rows[30][70] == 0 and rows[31][70] == 90
      and rows[30][119] == 90 and rows[30][120] == 0, None)
sh([CT, 'draw', '40', '40', T + '/dot.bmp', 'pen:1:9', 'brush:9', 'ellipse:5:5:10:10', 'ellipse:20:20:34:30'])
rows = read_bmp(T + '/dot.bmp')[3]
dot = {(x, y) for y in range(40) for x in range(5, 10) if y in range(5, 10) and rows[y][x] == 9}
want = {(5 + x, 5 + y) for y in range(5) for x in range(5) if ((x + .5 - 2.5) / 2.5) ** 2 + ((y + .5 - 2.5) / 2.5) ** 2 <= 1}
big = [(x, y) for y in range(40) for x in range(40) if rows[y][x] == 9 and 20 <= x < 34 and 20 <= y < 30]
sym = all(((x, y) in set(big)) == ((53 - x, 49 - y) in set(big)) for x in range(20, 34) for y in range(20, 30))
check('Ellipse: the 5x5 dot of the status screens is the disc without corners (21 px); a 14x10 ellipse is non-empty, inside its box and symmetric',
      dot == want and len(want) == 21 and len(big) > 60 and sym, (len(dot), len(want), len(big)))

# ================================================================== F. the API commands and the windows (script host)
SCRIPT_PAGE = 'fce9VIDPHO'
JON = 'fonts\\JON.TTF'
HDR = 'page %s; shot %s/w0.bmp; api AddRemoveFontResource %s 1; api CreateDeleteLogicalFont 1 18 systemfont; api GetWindowDC -1' % (SCRIPT_PAGE, T, JON)
TXT1, TXT2 = 'Hamburg 12.5 +++', 'under the image'
TC = (255, 193, 28)
BARC = (0, 0, 255)
RECTC = (200, 50, 50)
script = (HDR + '; gdi lastdc font lastfont; gdi lastdc bkmode 1; gdi lastdc textcolor 255,193,28; gdi lastdc textout 20 100 %s; gdi lastdc textout 280 120 %s; '
          'gdi lastdc map 255,193,28; gdi lastdc pen 3 0,0,255; gdi lastdc moveto 100,200; gdi lastdc lineto 300,200; gdi lastdc fillrect 50,250,120,280 200,50,50; '
          'gdi lastdc map 0,0,255; gdi lastdc map 200,50,50; pal; '
          'api ReleaseWindowDC -1 lastdc; shot %s/w1.bmp; api RedrawScreen; advance 500; shot %s/w2.bmp; '
          'api DisplayImage -1 blank 264 59 0 0 -1; shot %s/w3.bmp; api RedrawPage; shot %s/w4.bmp; '
          'api GetWindowDC -1; gdi lastdc font lastfont; gdi lastdc textout 20 100 again; api ReleaseWindowDC -1 lastdc; shot %s/w5.bmp; '
          'page O1; page %s; shot %s/w6.bmp; echo done') % (TXT1, TXT2, T, T, T, T, T, SCRIPT_PAGE, T)
o = run(script)
check('script ran to the end', o[-1] == 'done', o[-3:])
idx = {}
for l in o:
    m = re.match(r'gdi map index=(\d+)', l)
    if m:
        idx.setdefault('maps', []).append(int(m.group(1)))
palhex = [l for l in o if l.startswith('pal ')][0].split()[1]
dpal = [tuple(int(palhex[6 * i + 2 * k:6 * i + 2 * k + 2], 16) for k in range(3)) for i in range(256)]
tidx, bidx, ridx = idx['maps']
check('GetWindowDC/gdi: text colour mapped to the nearest entry of the palette on display (3 colours, brute force)',
      (tidx, bidx, ridx) == (nearest(dpal, *TC), nearest(dpal, *BARC), nearest(dpal, *RECTC)), (idx, [nearest(dpal, *c) for c in (TC, BARC, RECTC)]))
getdc = [l for l in o if l.startswith('api GetWindowDC')][0]
check('GetWindowDC reports the page size: width 640, height 368 (a full 640x368 page)', 'width=640 height=368' in getdc, getdc)


def ink(fontfile, face, h, txt):
    sh([CT, 'text', '%s/%s' % (FONTS, fontfile), face, str(h), txt, T + '/ref.bmp'])
    r = read_bmp(T + '/ref.bmp')[3]
    return {(x - 10, y - 10) for y in range(len(r)) for x in range(len(r[0])) if r[y][x]}


I1, I2 = ink('JON.TTF', 'systemfont', 18, TXT1), ink('JON.TTF', 'systemfont', 18, TXT2)
VT = 56     # the view starts below the 56-pixel top bar; this page is 640 x 368, no scrolling


def shot(n):
    return read_bmp('%s/w%d.bmp' % (T, n))[3]


B0, B1, B2, B3, B4, B5, B6 = [shot(n) for n in range(7)]
E1 = [bytearray(r) for r in B0]
for (dx, dy) in I1:
    E1[VT + 100 + dy][20 + dx] = tidx
for (dx, dy) in I2:
    E1[VT + 120 + dy][280 + dx] = tidx
# the rasteriser starts from 0 everywhere; rendering the same ops with two different colours tells which pixels they touch
marks = raster(640, 368, ['pen:3:%d' % bidx, 'move:100:200', 'line:300:200', 'fill:50:250:120:280:%d' % ridx])
mk = raster(640, 368, ['pen:3:%d' % (bidx ^ 1), 'move:100:200', 'line:300:200', 'fill:50:250:120:280:%d' % (ridx ^ 1)])
touched = {(x, y) for y in range(368) for x in range(640) if marks[y][x] != mk[y][x]}
for (x, y) in touched:
    E1[VT + y][x] = marks[y][x]
dif = [(x, y) for y in range(480) for x in range(640) if B1[y][x] != E1[y][x]]
check('text appears at the requested position (top-left of the cell) in the mapped colour, exactly the glyph pixels of the font; lines and rectangle '
      'next to it; everything else unchanged', not dif and len(I1) > 50, dif[:5])
check('the drawing is part of the page: it persists across RedrawScreen and 500 ms of frames', B2 == B1)
# DisplayImage over the text: opaque pixels of the image replace it, transparent ones leave it
sh(['build/biosys-dump', 'export', PAGES, SCRIPT_PAGE, '2', T + '/blank_img.bmp', PAL])
iw, ih, _, irows = read_bmp(T + '/blank_img.bmp')
key = next(i for i, c in enumerate(dpal) if c == (0, 255, 255))
E3 = [bytearray(r) for r in B1]
for y in range(ih):
    for x in range(iw):
        if irows[y][x] != key:
            E3[VT + 59 + y][264 + x] = irows[y][x]
dif = [(x, y) for y in range(480) for x in range(640) if B3[y][x] != E3[y][x]]
under = [(280 + dx, 120 + dy) for dx, dy in I2]
kept = sum(1 for (x, y) in under if B3[VT + y][x] == tidx)
check('DisplayImage paints over the text: opaque pixels replace it, keyed pixels leave it (%d of %d glyph pixels survive)' % (kept, len(under)),
      not dif and 0 < iw and kept < len(under) and sum(1 for (x, y) in [(20 + dx, 100 + dy) for dx, dy in I1] if B3[VT + y][x] == tidx) == len(I1), dif[:5])
check('RedrawPage repaints the visible ornaments over the whole surface: all drawing is gone (the page background covers every pixel)', B4 == B0)
check('a new drawing after that is there; entering the page again builds a new surface and drops it', B5 != B0 and B6 == B0)

# a panorama scrolls the page under the view: text at page x 1000 shows at screen x 100 for scroll x 900
o = run('page R4; shot %s/p0.bmp; scroll 900,0; api AddRemoveFontResource %s 1; api CreateDeleteLogicalFont 1 18 systemfont; api GetWindowDC -1; gdi lastdc font lastfont; '
        'gdi lastdc bkmode 1; gdi lastdc textcolor 255,255,255; gdi lastdc textout 1000 100 Panorama; gdi lastdc extent Panorama; gdi lastdc map 255,255,255; pal; '
        'api ReleaseWindowDC -1 lastdc; surfdump %s/p1.raw; shot %s/p1.bmp' % (T, JON, T, T))
pw = [l for l in o if l.startswith('api GetWindowDC')][0]
pidx = [int(re.match(r'gdi map index=(\d+)', l).group(1)) for l in o if l.startswith('gdi map')][0]
pp1 = read_bmp(T + '/p1.bmp')[3]
Ip = ink('JON.TTF', 'systemfont', 18, 'Panorama')
psurf = open(T + '/p1.raw', 'rb').read()
pexp = po.Projection(75.0).render(psurf, 900, 0)
check('panorama: page 2560 x 1024; text at page (1000, 100) is in the page surface and the view (scrolled to x 900) is its cylindrical projection',
      'width=2560 height=1024' in pw and all(psurf[(100 + dy) * 2560 + 1000 + dx] == pidx for dx, dy in Ip) and len(Ip) > 20 and
      all(pp1[VT + y] == pexp[y * 640:(y + 1) * 640] for y in range(368)), pw)

# the top bar (window 0): its own 640 x 56 surface, kept as long as the bar is open
script = ('page %s; api DisplayPopupPage topbar; shot %s/b0.bmp; api AddRemoveFontResource %s 1; api CreateDeleteLogicalFont 1 18 systemfont; api GetWindowDC 0; '
          'gdi lastdc font lastfont; gdi lastdc bkmode 2; gdi lastdc bkcolor 0,0,0; gdi lastdc textcolor 225,220,210; gdi lastdc textout 100 10 Top bar message; '
          'gdi lastdc map 225,220,210; pal; api ReleaseWindowDC 0 lastdc; shot %s/b1.bmp; api RedrawPage; api RedrawScreen; advance 300; shot %s/b2.bmp; '
          'api DisplayImage 0 topbar -1 -1 0 0 -1; shot %s/b3.bmp; api GetWindowDC 1; api GetWindowDC -3; api GetWindowDC 0; gdi lastdc textout 0 0 x; echo done') % (SCRIPT_PAGE, T, JON, T, T, T)
o = run(script)
gd = [l for l in o if l.startswith('api GetWindowDC')]
bpal = [l for l in o if l.startswith('pal ')][0].split()[1]
bdpal = [tuple(int(bpal[6 * i + 2 * k:6 * i + 2 * k + 2], 16) for k in range(3)) for i in range(256)]
bidx0 = [int(re.match(r'gdi map index=(\d+)', l).group(1)) for l in o if l.startswith('gdi map')][0]
b0, b1, b2, b3 = [read_bmp('%s/b%d.bmp' % (T, i))[3] for i in range(4)]
Ib = ink('JON.TTF', 'systemfont', 18, 'Top bar message')
exp = [bytearray(r) for r in b0]
cell = sh([CT, 'text', '%s/JON.TTF' % FONTS, 'systemfont', '18', 'Top bar message', T + '/ref.bmp'])
cw = int(re.search(r'extent cx=(\d+)', cell).group(1))
for y in range(10, 28):
    for x in range(100, 100 + cw):
        exp[y][x] = nearest(bdpal, 0, 0, 0)     # OPAQUE background in the bk colour fills the cell
for dx, dy in Ib:
    exp[10 + dy][100 + dx] = bidx0
check('top bar (window 0): 640 x 56 surface, OPAQUE text draws its cell in the background colour and the glyphs in the text colour',
      'width=640 height=56' in gd[0] and b1 == exp and b1 != b0 and len(Ib) > 30, [(x, y) for y in range(60) for x in range(640) if b1[y][x] != exp[y][x]][:5])
check('top bar text persists across RedrawPage / RedrawScreen and frames', b2 == b1 and b1 != b0)
check('DisplayImage on the top bar paints over its text (the 640 x 56 image covers it)', b3 == b0)
check('GetWindowDC errors: no popup (window 1) 0x11, window -3 (status bar) 0x11, the DC of the top bar after the page was repainted fine', gd[1].endswith('rc=17') and gd[2].endswith('rc=17') and gd[3].startswith('api GetWindowDC rc=0'), gd)

# error codes of the four commands
script = ('page %s; api GetWindowDC 7; api GetWindowDC 1; api GetWindowDC -3; api GetWindowDC -1; api GetWindowDC -1; api ReleaseWindowDC -1 99999; api ReleaseWindowDC 0 lastdc; '
          'api ReleaseWindowDC -1 lastdc; api ReleaseWindowDC -1 lastdc; api CreateDeleteLogicalFont 0 12345; api CreateDeleteLogicalFont 1 18 nosuchface; '
          'api CreateDeleteLogicalFont 0 lastfont; api CreateDeleteLogicalFont 0 lastfont; api AddRemoveFontResource fonts\\nothere.ttf 1; api AddRemoveFontResource %s 0; '
          'api AddRemoveFontResource %s 1; api AddRemoveFontResource %s 1; api AddRemoveFontResource %s 0; api AddRemoveFontResource %s 0; api AddRemoveFontResource %s 0; '
          'api AddRemoveFontResource fonts\\PAGES.DAT 1; apistats 0x38 0x39 0x3a 0x3b') % (SCRIPT_PAGE, JON, JON, JON, JON, JON, JON)
o = run(script)
rcs = lambda name: [int(re.search(r'rc=(\d+)', l).group(1)) for l in o if l.startswith('api %s ' % name)]
check('GetWindowDC: 0x11 for a window without a page (7, 1), -3, and for a second DC on a surface whose DC is out; ReleaseWindowDC: 0x11 for an unknown DC, the wrong window, a DC already released',
      rcs('GetWindowDC') == [17, 17, 17, 0, 17] and rcs('ReleaseWindowDC') == [17, 17, 0, 17], (rcs('GetWindowDC'), rcs('ReleaseWindowDC')))
check('CreateDeleteLogicalFont: any face name creates (falls back), delete once ok, twice 0x11, unknown font 0x11',
      rcs('CreateDeleteLogicalFont') == [17, 0, 0, 17], rcs('CreateDeleteLogicalFont'))
check('AddRemoveFontResource: missing file 0x11, remove without add 0x11, add / add / remove / remove ok, third remove 0x11, a non-font file 0x11',
      rcs('AddRemoveFontResource') == [17, 17, 0, 0, 0, 0, 17, 17], rcs('AddRemoveFontResource'))
check('the four commands are counted and none answers kApiNotImplemented', [l for l in o if l.startswith('apistats')] == [
      'apistats 0x38 calls=8 notimpl=0', 'apistats 0x39 calls=4 notimpl=0', 'apistats 0x3a calls=5 notimpl=0', 'apistats 0x3b calls=4 notimpl=0'],
      [l for l in o if l.startswith('apistats')])
rc, so, se = run_err('page %s; api GetWindowDC -1; api ReleaseWindowDC -1 lastdc; gdi lastdc textout 0 0 x' % SCRIPT_PAGE)
check('a released DC handle is dead: EngineApi::canvas() returns nullptr for it', rc != 0 and any('not an open device context' in l for l in se), (rc, se))

# the colour mapping of the engine itself (display palette on the page), PALETTEINDEX and plain RGB
cols = [(random.randrange(256), random.randrange(256), random.randrange(256)) for _ in range(40)]
o = run('page %s; api GetWindowDC -1; pal; ' % SCRIPT_PAGE + '; '.join('gdi lastdc map %d,%d,%d; gdi lastdc map rgb:%d,%d,%d' % (c + c) for c in cols) + '; gdi lastdc map i:200')
mp = [l for l in o if l.startswith('pal ')][0].split()[1]
dp = [tuple(int(mp[6 * i + 2 * k:6 * i + 2 * k + 2], 16) for k in range(3)) for i in range(256)]
got = [int(re.match(r'gdi map index=(\d+)', l).group(1)) for l in o if l.startswith('gdi map')]
want = []
for c in cols:
    want += [nearest(dp, *c)] * 2
want.append(200)
check('engine: 40 random colours as PALETTERGB and RGB map to the brute-force nearest index of the palette on display; PALETTEINDEX 200 is 200', got == want, (got[:6], want[:6]))

# samples that look like the DLL's screens (CSelf::ShowMessage strip on the top bar; the spirometer read-out, dll:1000ae40; the clock, dll:100165a0)
TXT = ['+++SPIROMETER - LUNG FUNCTION AND PULMONARY FLOW+++', '  +PEAK EXPIRATORY FLOW SUB NORMAL\\n  +TOTAL LUNG CAPACITY SUB NORMAL\\n  +REDUCED EXPIRATORY FLOW INDICATES WEAKENING OR RESTRICTION OF AIRWAYS \\n  +POSSIBLE EMPHYSEMA - DAMAGE TO ELASTIC TISSUE OF LUNG\\n',
       '+++SEEK TREATMENT - BRONCHIAL INHALERS AVAILABLE FROM DISPENSARY OR IF NECESSARY TAKE PROCESSED EPHEDRA FROM SAVANNAH BIOME+++']
o = run('page R4; scroll 0,0; api AddRemoveFontResource %s 1; api AddRemoveFontResource fonts\\HIGHGFB.TTF 1; api CreateDeleteLogicalFont 1 18 systemfont; api CreateDeleteLogicalFont 1 48 HighlandGothicFLF; '
        'api CreateDeleteLogicalFont 1 72 HighlandGothicFLF; api GetWindowDC -1; gdi lastdc fillrect 0,0,640,368 0,0,0; gdi lastdc font 49; gdi lastdc bkmode 1; gdi lastdc textcolor 0,135,166; '
        'gdi lastdc textout 24 10 %s; gdi lastdc drawtext 24,30,428,100 16 %s; gdi lastdc textout 24 120 +++ANALYSING TRACE GASES IN EXHALATION+++; '
        'gdi lastdc drawtext 24,140,428,400 16 %s; gdi lastdc font 51; gdi lastdc textcolor 3,119,3; gdi lastdc textout 440 20 12:34; gdi lastdc font 50; gdi lastdc textcolor 255,193,28; gdi lastdc textout 440 80 12:34; '
        'api ReleaseWindowDC -1 lastdc; shot %s/sample_spirometer.bmp' % (JON, TXT[0], TXT[1], TXT[2], T))
check('sample screen drawn with the DLL\'s fonts, strings and rectangles (%s/sample_spirometer.bmp)' % T,
      os.path.exists(T + '/sample_spirometer.bmp') and any(l.startswith('gdi drawtext height=108') for l in o), [l for l in o if 'drawtext' in l])
o = run('page %s; api DisplayPopupPage topbar; api AddRemoveFontResource %s 1; api CreateDeleteLogicalFont 1 18 systemfont; api GetWindowDC 0; gdi lastdc font lastfont; gdi lastdc bkmode 2; gdi lastdc bkcolor 0,0,0; '
        'gdi lastdc fillsolid 0,0,639,55 0,0,0; gdi lastdc textcolor 255,25,28; gdi lastdc textout 538 18 DYING; gdi lastdc textcolor 127,127,127; gdi lastdc textout 289 18 TIRED; gdi lastdc textcolor 225,220,210; '
        'gdi lastdc textout 160 36 You pick up the fruit; api ReleaseWindowDC 0 lastdc; shot %s/sample_topbar.bmp' % (SCRIPT_PAGE, JON, T))
check('top bar strip sample (%s/sample_topbar.bmp)' % T, os.path.exists(T + '/sample_topbar.bmp'))

# coverage tool
if os.path.exists('build/biosys-apicoverage'):
    cov = {}
    for l in sh(['build/biosys-apicoverage']).splitlines():
        m = re.match(r'0x([0-9a-f]+) (\w+) (implemented|stub)', l)
        if m:
            cov[int(m.group(1), 16)] = m.group(3)
    check('apicoverage: 0x38 0x39 0x3a 0x3b implemented, and the sim-status window 0x4a .. 0x4e 0x56 with its DC 0x4c',
          all(cov.get(c) == 'implemented' for c in (0x38, 0x39, 0x3a, 0x3b, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x56)), {hex(c): cov.get(c) for c in (0x38, 0x39, 0x3a, 0x3b, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x56)})

print('\n%d check(s) failed' % failed if failed else '\nall canvas checks passed')
print('samples: %s/sample_spirometer.bmp sample_topbar.bmp text_JON_18.bmp text_HIGHGFB_48.bmp text_HIGHGFB_72.bmp lines.bmp bars.bmp dot.bmp w1.bmp w3.bmp p1.bmp b1.bmp' % T)
sys.exit(1 if failed else 0)
