#!/usr/bin/env python3
"""BioSys 4 power terminals (spec analysis/notes/bs4.md 2 / 4, packet P7). The feature lists below are typed from the decompile of exe:0041e5f0
(one list per page and row); the expected figures are computed here from values the game logic returns (`get FEATURE 0x8e` of
build/biosys-bs4test --game, i.e. the real BiosGame port), never from the panel. Frames: build/shots/bs4p7_<page>.bmp"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bs4_oracle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
BIN = 'build/biosys-bs4test'
T = 'build/test/p7'
SHOTS = 'build/shots'
os.makedirs(T, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
fails = 0


def check(ok, what, detail=''):
    global fails
    if ok:
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (' -- ' + str(detail)[:900] if detail != '' else ''))


# page -> (hotspot on the power page that opens it, highlight ornament, rows: the features whose FEATURE_POWER_USAGE (0x8e) are summed)
PAGES = {
    'termsav': ('0_terms1', 'hiterms1', [['fs9mixer', 'fs3mixer', 'fs7mixer'], ['fbs2lung'], ['fbs4revos'], ['fbs4UV'],
                                         ['fbs5scrub', 'fbs3scrub', 'fbs2scrub'], ['fbs3watpmp'], ['fs2rain', 'fs5rain', 'fs8rain'],
                                         ['fs1fan', 'fs4fan', 'fs7fan'], ['fsheater']]),
    'termoc': ('0_termo1', 'hitermo1', [['fomixer1', 'fomixer2', 'fomixer3'], ['fbo1lung'], ['fbo4revos'], ['fbo3UV'], ['fbo3algae'],
                                        ['fbo2watpmp'], ['fo1rain'], ['fbo1tide'], ['foheater'], ['fbo1wave']]),
    'termevo': ('0_terme1', 'hiterme1', [['fdbmixer1', 'fdbmixer2', 'fdbmixer3'], ['fbd5lung'], ['fbd3revos'], ['fbd2UV'],
                                         ['fbd1scrub', 'fbd4scrub', 'fbd5scrub'], ['fbd3watpmp'], ['fdbrain1', 'fdbrain2', 'fdbrain3'],
                                         ['fdb6fan', 'fdb3fan', 'fdbfan3'], ['fdbheater']]),
    'termrf': ('0_termr', 'hitermr1', [['fr9mixer', 'fr5mixer', 'fr2mixer'], ['fbr2lung'], ['fbr4revos'], ['fbr3UV'],
                                       ['fbr5scrub', 'fbr2scrub', 'fbr1scrub'], ['fbr3watpmp'], ['fr9rain', 'fr6rain', 'fr1rain'],
                                       ['fr8fan', 'fr3fan', 'fr10fan'], ['frfheater'], ['fr9mist', 'fr5mist', 'fr1mist']]),
    'termhouse': ('0_termh', 'hitermh1', [['fphheater'], ['fbp1UV']]),
}
ORDER = ['termsav', 'termoc', 'termevo', 'termrf', 'termhouse']
PANEL_X, PANEL_Y = 442, 72            # the verb-0x1a hotspots open the panels at (442, 72)
oracle = bs4_oracle.Oracle()


def run_term(lines, *opts):
    lines = ['echo ' + l if l.startswith('@') else l for l in lines]
    p = subprocess.run([BIN, '--game'] + list(opts), input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout.splitlines(), p.stderr


def clamp(v):
    return min(10.0, max(0.0, v))


def parse(out):
    """split the output at `@name` markers: name -> lines"""
    st, cur = {}, None
    for l in out:
        if l.startswith('@'):
            cur = l[1:]
            st[cur] = []
        elif cur is not None:
            st[cur].append(l)
    return st


def term_rows(lines):
    rows = []
    for l in lines:
        m = re.match(r"term row (\d+) sum (\S+) text '(.*)' rect (\d+),(\d+),(\d+),(\d+)", l)
        if m:
            rows.append((float(m.group(2)), m.group(3), tuple(int(m.group(i)) for i in range(4, 8))))
    return rows


def gets(lines):
    return {m.group(1): float(m.group(2)) for m in (re.match(r'get (\S+) 142 = ok int=-?\d+ dbl=(\S+)', l) for l in lines) if m}


def expected(page, vals):
    return [sum(clamp(vals[f]) for f in row) for row in PAGES[page][2]]


def feats(page):
    return [f for row in PAGES[page][2] for f in row]


def state_of(lines):
    for l in lines:
        if l.startswith('state '):
            return l
    return ''


def fmt1(v):
    return '%.1f' % v


# ---- the lists against the strings of the decompile (analysis/out/bs4_inventory.tsv: the names the code passes by symbol)
inv = open((_DEV + '/analysis/out/bs4_inventory.tsv')).read().splitlines()
line = [l for l in inv if l.startswith('0041e5f0')][0]
named = set(re.findall(r's_([A-Za-z0-9]+)_[0-9a-f]{8}#0x8e', line))
mine = {f for p in PAGES for f in feats(p)}
check(len(named) >= 24 and named <= mine, 'the feature lists contain every name the decompile of exe:0041e5f0 references by symbol (%d names)' % len(named), sorted(named - mine))
check(sum(len(feats(p)) for p in PAGES) == 68 and [len(PAGES[p][2]) for p in ORDER] == [9, 10, 9, 10, 2],
      'rows per page 9 / 10 / 9 / 10 / 2 as the handler exe:00435a90 draws them; 68 features in all')

# ---- 1..3 each terminal, real clicks from the power page
for page in ORDER:
    hs, hl, rows = PAGES[page]
    F = feats(page)
    odd = F if page == 'termhouse' else F[1::2]      # the house heater is on from the start: it is the machine whose power can change
    L = ['load power', 'clickh techterm', 'clickh ' + hs, '@open', 'state', 'mask panel', 'term'] + ['get %s 0x8e' % f for f in F]
    L += ['@on'] + ['setp %s %d 1' % (f, i) for f in F for i in (1, 2)] + ['advance 400', 'term', 'advance 1100', 'term'] + ['get %s 0x8e' % f for f in F]
    L += ['shotbb %s/bs4p7_%s.bmp' % (SHOTS, page)]
    L += ['@off'] + ['setp %s %d 0' % (f, i) for f in odd for i in (1, 2)] + ['advance 1100', 'term'] + ['get %s 0x8e' % f for f in F]
    L += ['@alloff'] + ['setp %s %d 0' % (f, i) for f in F for i in (1, 2)] + ['advance 1100', 'term'] + ['get %s 0x8e' % f for f in F]
    L += ['@end', 'events', 'logs']
    rc, out, err = run_term(L)
    st = parse(out)
    o = st.get('open', [])
    sline = state_of(o)
    check(rc == 0 and 'panel=%s ' % page in sline and 'opener=%s ' % hs in sline and ('highlight=%s/0' % hl) in sline,
          '%s: opened by real clicks techterm, %s on the power page; highlight %s, bit 0' % (page, hs, hl), sline or out[-5:])
    base = term_rows(o)
    v0 = gets(o)
    check(len(base) == len(rows) and all(b[2] == (2, 0x56 + 16 * i, 0x30, 0x64 + 16 * i) for i, b in enumerate(base)),
          '%s: %d rows at the rectangles (2, 0x56 + 16 i, 0x30, 0x64 + 16 i)' % (page, len(rows)), base)
    exp0 = expected(page, v0)
    check(len(v0) == len(F) and [b[0] for b in base] == exp0 and [b[1] for b in base] == [fmt1(x) for x in exp0],
          '%s: rows at open show the sums of the game\'s power usage per row (total %s) as %%.1f' % (page, fmt1(sum(exp0))), (base, exp0))
    # switched on: no change before the poll, the new figures after it
    on = st.get('on', [])
    n = len(rows)
    tl = [l for l in on if l.startswith('term ')]           # two `term` dumps: after 400 ms and after the poll (n rows and a count line each)
    t1, t2 = term_rows(tl[:n + 1]), term_rows(tl[n + 1:])
    v1 = gets(on)
    exp1 = expected(page, v1)
    check([r[1] for r in t1] == [r[1] for r in base], '%s: 400 ms after switching the machines on the rows are unchanged (poll is once per second)' % page,
          ([r[1] for r in t1], [r[1] for r in base]))
    check([r[0] for r in t2] == exp1 and [r[1] for r in t2] == [fmt1(x) for x in exp1] and sum(exp1) > 0 and (exp1 != exp0 or page == 'termhouse'),
          '%s: after the poll the rows show the new sums (total %s)' % (page, fmt1(sum(exp1))), (t2, exp1))
    # a frame: the rows are drawn; equal text = equal pixels, different text = different pixels
    w, h, px = bs4_oracle.read_bmp('%s/bs4p7_%s.bmp' % (SHOTS, page))
    white = oracle.white
    crops = []
    for r in t2:
        l, t, rr, b = r[2]
        crops.append(tuple(bytes(px[PANEL_Y + y][PANEL_X + l:PANEL_X + rr]) for y in range(t, b)))
    inked = [any(any(c != white for c in line) for line in cr) for cr in crops]
    same_ok = all((crops[i] == crops[j]) == (t2[i][1] == t2[j][1]) for i in range(n) for j in range(i + 1, n))
    check(all(inked) and same_ok, '%s: the frame shows ink in every row, equal text gives equal pixels and different text different pixels' % page, (inked, same_ok))
    # some off, then all off
    off = st.get('off', [])
    tro = term_rows(off)
    v2 = gets(off)
    exp2 = expected(page, v2)
    check([r[0] for r in tro] == exp2 and [r[1] for r in tro] == [fmt1(x) for x in exp2] and exp2 != exp1,
          '%s: switching every second machine off: the next poll shows the new sums (total %s)' % (page, fmt1(sum(exp2))), (tro, exp2))
    al = st.get('alloff', [])
    tra = term_rows(al)
    exp3 = expected(page, gets(al))
    check([r[0] for r in tra] == exp3 and [r[1] for r in tra] == [fmt1(x) for x in exp3], '%s: all machines off: rows and total follow (total %s)' % (page, fmt1(sum(exp3))), (tra, exp3))
    ev = st.get('end', [])
    check(not any('range' in l or 'Failed' in l for l in ev if l.startswith('log')), '%s: no failed reads / out-of-range logs from the real game values' % page, ev[-5:])

# ---- 4. the controls: the tab hotspots (verb 0x1a) switch terminals, the 103 one opens specterm; no parameter is set by any of them
for page in ORDER:
    pg = oracle.page(oracle.names.index(page) + 1)
    for hi, h in enumerate(pg.hotspots):
        a = h['action']
        l, t, r, b = h['rect']
        tgt = oracle.names[a[1] - 1]
        L = ['load power', 'clickh techterm', 'clickh ' + PAGES[page][0], 'events', 'click %d %d' % (PANEL_X + (l + r) // 2, PANEL_Y + (t + b) // 2), '@click', 'state', 'term', 'events']
        rc, out, err = run_term(L)
        st = parse(out)
        c = st.get('click', [])
        s = state_of(c)
        rows_n = len(term_rows(c))
        ok = ('panel=%s ' % tgt) in s and not any(x.startswith('set ') for x in c)
        if tgt in PAGES:
            ok = ok and rows_n == len(PAGES[tgt][2])
        else:
            ok = ok and rows_n == 0
        if not ok:
            check(False, '%s hotspot %d (rect %s) -> %s' % (page, hi, h['rect'], tgt), (s, rows_n))
    check(True, '%s: all %d hotspots open their target panel (verb 0x1a), the terminals show their rows, and no hotspot sends a set-parameter event' % (page, len(pg.hotspots)))

# the tab buttons: switching terminals shows the values of the new terminal's features
rc, out, err = run_term(['load power', 'clickh techterm', 'clickh 0_terms1'] + ['setp %s %d 1' % (f, i) for f in feats('termrf') for i in (1, 2)] +
                        ['click %d %d' % (PANEL_X + 180, PANEL_Y + 66), '@tab', 'state', 'term'] + ['get %s 0x8e' % f for f in feats('termrf')])
tab = parse(out).get('tab', [])
rows_now = term_rows(tab)
exp = expected('termrf', gets(tab))
check('panel=termrf ' in state_of(tab) and [r[0] for r in rows_now] == exp and sum(exp) > 0,
      'termsav -> termrf through the tab hotspot: the new terminal shows the sums of its own machines (total %s)' % fmt1(sum(exp)), (state_of(tab), rows_now, exp))

# ---- the real game headless (build/biosys --game --script): the terminal reached by real clicks through the pages, the terminal's frame has the rows
sys.path.insert(0, ROOT)


class Sim:
    def __init__(self, n):
        self.p = oracle.page(n)
        self.en = [not (h['flags'] & 1) for h in self.p.hotspots]

    def top_at(self, x, y):
        for i in range(len(self.p.hotspots) - 1, -1, -1):
            l, t, r, b = self.p.hotspots[i]['rect']
            if self.en[i] and l <= x <= r and t <= y <= b:
                return i
        return -1

    def point(self, hi):
        l, t, r, b = self.p.hotspots[hi]['rect']
        for y in range(t, b + 1):
            for x in range(l, r + 1):
                if self.top_at(x, y) == hi:
                    return x, y
        return None


def page_no(name):
    return oracle.names.index(name) + 1


def edges(n):
    s = Sim(n)
    out = []
    for i, h in enumerate(s.p.hotspots):
        if s.en[i] and h['action'][0] == 0x12:
            pt = s.point(i)
            if pt:
                t = h['action'][1]
                if t == page_no('email1'):
                    t = page_no('emailpw')
                if t == page_no('synth1'):
                    t = page_no('synthpw')
                out.append((t, pt))
    return out


start = page_no('menuscreen')
paths = {start: []}
queue = [start]
while queue:
    n = queue.pop(0)
    for t, pt in edges(n):
        if t not in paths and 1 <= t <= 45:
            paths[t] = paths[n] + [pt]
            queue.append(t)
cmds = []
for x, y in paths[page_no('power')]:
    cmds += ['click %d,%d' % (x, y), 'advance 50']
d = '%s/glue' % T
shutil.rmtree(d, ignore_errors=True)
os.makedirs(d)
script = ['advance 500', 'page NbrBIOS4', 'advance 500', 'clickhs BUTTON', 'advance 600', 'type RUSSELL\\n', 'advance 100'] + cmds + \
         ['bs4hs techterm', 'advance 50', 'bs4hs 0_terms1', 'advance 1500', 'dump bs4', 'shot %s/bs4p7_glue.bmp' % T]
r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--mute', '--seed', '1', '--save-dir', d, '--script', '; '.join(script)],
                   capture_output=True, text=True, timeout=600)
m = re.search(r"bs4 page (\S+) panel (\S+) opener '(.*?)' mask (0x[0-9a-f]+)", r.stdout)
check(r.returncode == 0 and m and m.group(1) == 'power' and m.group(2) == 'termsav' and m.group(3) == '0_terms1',
      'the real game headless: menuscreen -> power by real clicks, techterm and 0_terms1 open termsav', (r.stdout[-300:], r.stderr[-300:]))
if m and m.group(2) == 'termsav':
    w, h, px = bs4_oracle.read_bmp('%s/bs4p7_glue.bmp' % T)
    ink = []
    for i in range(9):
        t = 0x56 + 16 * i
        ink.append(any(px[PANEL_Y + y][PANEL_X + x] != oracle.white for y in range(t, t + 14) for x in range(2, 0x30)))
    check(all(ink), 'the real game headless: the nine rows of termsav carry the drawn figures', ink)

print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
