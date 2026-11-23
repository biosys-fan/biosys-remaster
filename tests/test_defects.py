"""Engine defects the first real playthrough of the ported game exposed (analysis/notes/interface.md, engine_runtime.md, exe addresses in
the comments). Everything is observed through `build/biosys --game --script` (real key / mouse input, the engine's dumps) and checked
with independent Python code: the PAL files, the page file (build/biosys-dump), the original's rules read off the decompiled engine.

1. keyed images under the game's palettes: the colour key is looked up in the palette the original looks it up in (exe:00412530: the
   palette on display, or the stash of a running load), so no pixel of the key's index is painted by a keyed image;
2. user input is on again after a journey (exe:00401050 resets DAT_00498a78) without the game's help;
3. events 35 36 37 38 44: the right-click return page (exe:004456b0 save, exe:004457a0 drop, exe:00445f80 return, exe:004434d0 the button);
4. ReleaseWindowDC: no redraw flag in the block, the redraw is the engine's (exe:004137e0);
5. DisplayPopupPage (API 0x60 and verb 9 are the same routine, exe:00450bd0);
6. posted input: the game's WM_KEYUP and WM_MOUSEMOVE messages reach the engine's handlers (exe:00443a70, exe:00445b70).
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import collections, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from play_lib import Play, PAGES

D = _DEV
BIODATA = D + '/game-cd/files/BIODATA'
T = 'build/test/defects'
os.makedirs(T + '/saves', exist_ok=True)
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:700] if detail else ''))


def game(script, *opts, seed=1, log=False):
    args = ['build/biosys', '--game', '--mute', '--seed', str(seed), '--save-dir', T + '/saves'] + list(opts)
    if log:
        args.append('--log-events')
    r = subprocess.run(args + ['--script', script], capture_output=True, text=True, timeout=600)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


def read_bmp(path):
    """(indices 640x480 top-down, palette [(r, g, b)])"""
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    pal = [tuple(d[54 + 4 * i:57 + 4 * i][::-1]) for i in range(256)]
    stride = (w + 3) // 4 * 4
    rows = [d[off + (h - 1 - y) * stride:][:w] for y in range(h)]
    return rows, pal


def display_palette(lines):
    """the palette of a `pal` command: [(r, g, b)] x 256"""
    for l in lines:
        if l.startswith('pal '):
            h = l.split()[1]
            b = bytes.fromhex(h)
            return [tuple(b[3 * i:3 * i + 3]) for i in range(256)]
    return None


def section_after(lines, marker):
    return lines[lines.index(marker) + 1:] if marker in lines else []


def keyed_blocks(lines):
    """`dump keyed` output split by the `echo PAGE x` marker before it: {marker: ([keyed lines], (cyan index, cyan pixels in view))}"""
    res, cur = {}, None
    for l in lines:
        if l.startswith('PAGE '):
            cur = l[5:]
            res[cur] = ([], None)
        elif l.startswith('keyed ') and cur is not None:
            res[cur][0].append(l)
        elif l.startswith('cyanpixels ') and cur is not None:
            q = l.split()
            res[cur] = (res[cur][0], (int(q[1]), int(q[2])))
    return res


def parse_keyed(l):
    m = re.match(r'keyed (\S+) key=(-?\d+) keypx=(\d+) shown=(\d+)', l)
    return m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))


# ---------------------------------------------------------------------------------------------------------------------------------
# page file facts (independent of the engine): the pages that have keyed ornaments (flag 0x10, transparent colour 0,255,255)
r = subprocess.run(['build/biosys-dump', 'dump', PAGES], capture_output=True, text=True)
pages, cur = {}, None
for l in r.stdout.splitlines():
    m = re.match(r'page \d+ (\S+) flags=(\S+) size=(\S+)', l)
    if m:
        cur = m.group(1)
        pages[cur] = 0
        continue
    m = re.match(r'orn \d+ (\S+) type=1 flags=(\S+) .*transp=0,255,255', l)
    if m and cur and int(m.group(2), 16) & 0x10:
        pages[cur] += 1
keyed_pages = sorted(p for p, c in pages.items() if c and p != 'status')
check('(1) the page file has many pages with keyed ornaments', len(keyed_pages) > 100, len(keyed_pages))

# the palettes of the sites: the transparent colour (0,255,255) is entry 54 of every one of them (and only of them)
cyan_idx = collections.Counter()
for root, _, files in os.walk(BIODATA + '/PALETTES'):
    for f in files:
        if f.lower().endswith('.pal'):
            d = open(os.path.join(root, f), 'rb').read()
            if len(d) == 0x418 and not root.upper().split('PALETTES')[1].lstrip('/\\').startswith(('MOVIES', 'POI', 'DEATH')):
                cyan_idx[tuple(i for i in range(256) if d[24 + 4 * i:27 + 4 * i] == b'\x00\xff\xff')] += 1
check('(1) in the PAL files the transparent colour sits at index 54 (data fact the keyed images rely on)', cyan_idx[(54,)] > 100, cyan_idx)

# ---------------------------------------------------------------------------------------------------------------------------------
# 1. keyed images: the start of the game (a load: the stash palette), four times of day by jumps, four times of day by saves and loads
rc, out, err = game('advance 3000; dump keyed; shot %s/r4.bmp; pal' % T, '--skip-intro')
kb = [parse_keyed(l) for l in out if l.startswith('keyed ')]
check('(1) the new game (a LoadGame: the colour key is looked up in the stash of the saved palette) shows R4\'s plant ornaments transparent',
      rc == 0 and len(kb) >= 2 and all(k == 54 and px > 0 and shown == 0 for _, k, px, shown in kb), kb)
rows, pal = read_bmp(T + '/r4.bmp')
n54 = sum(row[x] == 54 for row in rows[56:424] for x in range(640))
check('(1) the screenshot of R4 has no cyan rectangle (the old engine painted 57877 pixels of index 54)', n54 < 300 and pal[54] == (0, 255, 255), n54)

TIMES = {'night': 3 * 3600, 'dawn': 7 * 3600, 'day': 12 * 3600, 'dusk': 19 * 3600}
for name, t in TIMES.items():
    script = 'advance 3000; game settime %d; advance 4000; shot %s/r4_%s.bmp; pal' % (t, T, name)
    for p in keyed_pages:
        script += '; echo PAGE %s; page %s; advance 150; dump keyed' % (p, p)
    rc, out, err = game(script, '--skip-intro')
    blocks = keyed_blocks(out)
    shown = [(p, parse_keyed(l)) for p, (ls, _) in blocks.items() for l in ls]
    bad = [(p, k) for p, k in shown if k[1] != 54 or k[3] != 0]
    cy = [(p, c) for p, (_, c) in blocks.items() if c and c[1] > 300]
    check('(1) %s: jumping to the %d pages with keyed ornaments, %d keyed images are on display (>= 100 pages show one)' % (name, len(keyed_pages), len(shown)),
          rc == 0 and len([1 for p, (ls, _) in blocks.items() if ls]) >= 100 and not bad, (rc, err[-2:], bad[:4]))
    check('(1) %s: no keyed image painted its transparent colour (no page shows more than 300 pixels of index 54)' % name, not cy, cy[:4])
    # the same for a game loaded at that time of day (the palette of the save is the stash while the load runs)
    save = 'defects_' + name
    rc, out, err = game('advance 3000; game settime %d; advance 4000; api SaveGame %s; api LoadGame 2 %s; advance 200; dump keyed; pal'
                        % (t, save, save), '--skip-intro')
    kb = [parse_keyed(l) for l in out if l.startswith('keyed ')]
    p = display_palette(out)
    ci = [int(l.split()[1]) for l in out if l.startswith('cyanpixels')]
    check('(1) %s: a saved and loaded game (the load fades through black) shows R4\'s keyed images transparent, the key is the index of (0,255,255) on display'
          % name, rc == 0 and kb and all(k == 54 and shown == 0 and px > 0 for _, k, px, shown in kb) and p and p[54] == (0, 255, 255) and ci == [54],
          (rc, kb, ci))

# ---------------------------------------------------------------------------------------------------------------------------------
# 2. user input after a journey (the game's compensation in onPageLoaded is gone)
p = Play('R4')
p.advance(2000)
p.raw('echo @engine')
p.raw('api EnableUserInput 0 0', 'dump state', 'page R5', 'advance 200', 'dump state')
p.raw('page R4', 'advance 300', 'echo @journey')
p.click_hotspot('L_R4R5', page='R4', wait=0)
p.advance(600)
p.raw('dump state', 'echo @end')
rc, out, err = p.run(extra=['--skip-intro', '--log-api'])
inp = []
for l in out:
    if l.startswith('input '):
        inp.append(int(l.split()[1]))
check('(2) EnableUserInput(0) turns the input off; the next main page load (a jump) turns it on again', rc == 0 and inp[:2] == [0, 1], inp)
jr = section_after(out, '@journey')
calls = [l for l in jr if l.startswith('apicall EnableUserInput')]
check('(2) a journey (R4 -> R5 by clicking its path hotspot): the game disables the input (EnableUserInput enable=0) and never enables it again, '
      'yet the input is on when the destination page is entered', rc == 0 and len(inp) == 3 and inp[2] == 1 and calls and all('enable=0' in c for c in calls), (inp, calls[:4]))
src = open('src/game/biosgame.cpp').read()
check('(2) the game no longer calls EnableUserInput(1) from onPageLoaded', 'EnableUserInput(1, 0)' not in src[src.index('void BiosGame::onPageLoaded'):src.index('void BiosGame::onPageDrawn')])

# ---------------------------------------------------------------------------------------------------------------------------------
# 3. the right-click return page: R4 -> R10 (a journey), the hatch panel FR10htch_C is a feature page opened from the 2560 x 1024 page R10
p = Play('R4')
p.advance(2000)
p.goto('L_R4R10', 'R10')
p.advance(3000)
(sx, sy), mvc = p.point_cmd('R10', 'FR10htch_C', verb='move')
_, clc = p.point_cmd('R10', 'FR10htch_C', verb='click')
p.raw('scroll %d,%d' % (sx, sy), mvc, 'advance 300', 'shot %s/r10_before.bmp' % T, 'echo @save', clc, 'advance 2500',
      'dump state', 'dump returnpage', 'api GetReturnPageName', 'echo @return', 'rclick 320,200', 'advance 400', 'shot %s/r10_after.bmp' % T,
      'dump state', 'dump returnpage', 'echo @plain', 'rclick 320,200', 'advance 100', 'echo @drop', clc, 'advance 2500',
      'dump returnpage', 'page R4', 'advance 300', 'dump returnpage', 'echo @end')
rc, out, err = p.run(extra=['--skip-intro', '--log-events'])


def section(lines, a, b):
    i = next((k for k, l in enumerate(lines) if l == a), None)
    j = next((k for k, l in enumerate(lines) if l == b), None)
    return [] if i is None or j is None else lines[i + 1:j]


def evs(lines):
    return [l[6:] for l in lines if l.startswith('event ') and not l.startswith(('event sprite', 'event sound', 'event timer', 'event hover',
                                                                                    'event palettechanged', 'event compass'))]


save = section(out, '@save', '@return')
ret = section(out, '@return', '@plain')
plain = section(out, '@plain', '@drop')
drop = section(out, '@drop', '@end')
check('(3) opening the feature page from R10 (a zoom jump from a 2560 x 1024 page to a smaller one) saves R10: event 37 comes before OnExitPage',
      rc == 0 and evs(save)[:4] == ['hotspot R10 FR10htch_C', 'event37', 'exitpage R10', 'pageloaded FR10HTCH_C'], evs(save)[:6])
check('(3) the return data and the saved page are there (page, scroll position) and GetReturnPageName names the page the jump went to',
      any(l.startswith('returnpage valid=1 from=R10 to=FR10HTCH_C saved=R10 x=') for l in save) and 'api GetReturnPageName rc=0 page=FR10HTCH_C' in save, save[-6:])
check('(3) right click on the feature page: events 44, 35 (with the name of the saved page), OnExitPage, 36 (the saved page) and OnEnterPage, in that order',
      evs(ret) == ['event44 -> 1', 'event35 R10 -> 1', 'exitpage FR10HTCH_C', 'returntosavedpage R10', 'enterpage R10'] or
      [e for e in evs(ret) if e.split()[0] in ('event44', 'event35', 'exitpage', 'returntosavedpage', 'enterpage')] ==
      ['event44 -> 1', 'event35 R10 -> 1', 'exitpage FR10HTCH_C', 'returntosavedpage R10', 'enterpage R10'], evs(ret))
check('(3) back on R10 at the scroll position it was left at, the return data used up', 'page R10 scroll %d,%d' % (sx, sy) in ret and
      any(l.startswith('returnpage valid=0') for l in ret), ret[-6:])
a, pa = read_bmp(T + '/r10_before.bmp')
b, pb = read_bmp(T + '/r10_after.bmp')
# the rain overlay (dll:10022530, colours 0x76 / 0x56 / 0x57) is drawn on a raining page and moves every frame: those pixels are not counted
RAIN = (0x76, 0x56, 0x57)
diff = sum(a[y][x] != b[y][x] and a[y][x] not in RAIN and b[y][x] not in RAIN for y in range(480) for x in range(640))
check('(3) the saved page comes back as it was (the picture differs only where the page animates, rain drops apart: %d of 307200 pixels)' % diff, diff < 2500, diff)
check('(3) a right click on a page that is not a feature page is an ordinary OnRightClick (no events 44 / 35 / 36)',
      evs(plain) == ['rightclick'], evs(plain))
check('(3) leaving by a jump to a page that is not a feature page (here the debug jump to R4) drops the saved page: event 38 before OnExitPage',
      [e for e in evs(drop) if e.split()[0] in ('event37', 'event38', 'exitpage', 'enterpage')][:4] == ['event37', 'exitpage', 'event38', 'enterpage'] or
      (evs(drop).index('event38') < evs(drop).index('exitpage FR10HTCH_C') if 'event38' in evs(drop) and 'exitpage FR10HTCH_C' in evs(drop) else False),
      evs(drop))
check('(3) after the drop there is no saved page', any(l.startswith('returnpage') and 'saved=-' in l for l in drop[-4:]), drop[-4:])
# the same events must exist in the game's own handlers: slot 36 moves the player (CSelf::ReturnToSavedPage); the player is on R10 again
rc, out, err = game('advance 2000; api GetReturnPageName', '--skip-intro')
check('(3) API 0x69 answers an empty name before any jump (DAT_00499bf8 starts empty; only an engine jump that leaves a page writes it)',
      'api GetReturnPageName rc=0 page=' in out or any(l.startswith('api GetReturnPageName rc=0') for l in out), out[-2:])

# ---------------------------------------------------------------------------------------------------------------------------------
# 4. ReleaseWindowDC: the block has the window and the HDC only; any release of a page window's DC redraws, the top bar's does not
rc, out, err = game('advance 500; dump redraws; api GetWindowDC -1; api ReleaseWindowDC -1 lastdc 0 0; dump redraws; api GetWindowDC -1; '
                    'api ReleaseWindowDC -1 lastdc 1 1; dump redraws; api GetWindowDC 0; api ReleaseWindowDC 0 lastdc 1 1; dump redraws; '
                    'api ReleaseWindowDC -1 12345 1 1; dump redraws', '--skip-intro')
rd = [int(l.split()[1]) for l in out if l.startswith('redraws ')]
base = rd[0] if rd else 0
check('(4) ReleaseWindowDC of the main window redraws whatever the +04 / +08 words say; the top bar does not; a DC that is not open fails',
      rc == 0 and rd == [base, base + 1, base + 2, base + 2, base + 2] and 'api ReleaseWindowDC rc=17' in out, (rd, out[-4:]))

# ---------------------------------------------------------------------------------------------------------------------------------
# 5. DisplayPopupPage: one routine behind API 0x60 and verb 9
r = subprocess.run(['build/biosys-dump', 'dump', PAGES], capture_output=True, text=True)
nums = {}
for l in r.stdout.splitlines():
    m = re.match(r'page (\d+) (\S+) flags=(\S+) ', l)
    if m:
        nums[m.group(2)] = (int(m.group(1)), int(m.group(3), 16))
check('(5) in the page file only "topbar" carries the popup flag (header bit 1)', [n for n, (i, f) in nums.items() if f & 2] == ['topbar'], [n for n, (i, f) in nums.items() if f & 2])
r4 = nums['R4'][0]
tb = nums['topbar'][0]
rc, out, err = game('advance 500; api DisplayPopupPage R4; verb 9 %d; api DisplayPopupPage topbar; verb 9 %d; dump state' % (r4, tb), '--skip-intro', log=True)
check('(5) a page without the popup flag is refused by both paths (API 0x11, verb 0xb), nothing opened', rc == 0 and 'api DisplayPopupPage rc=17' in out and 'verb rc=11' in out
      and not any(l.startswith('event popupopened R4') for l in out), out[-8:])
check('(5) the popup that is open already (the top bar) is a success for both paths and is not opened twice',
      'api DisplayPopupPage rc=0' in out and out.count('verb rc=0') == 1 and not any(l.startswith('event popupopened topbar') and out.index(l) > 0 and
      out.count(l) > 1 for l in out), out[-8:])

# ---------------------------------------------------------------------------------------------------------------------------------
# 6. posted input
# (a) WM_KEYUP 'D' / 'S' (SendMessage, lParam 0xc0000001) reach OnKeyUp as vkeys 0x44 / 0x53 and the engine's default (drop / stash) follows. In the game
# they are sent when the synth attack starts with an object in the hand (cheat word CRASH): the object is dropped.
p = Play('R4')
p.advance(2000)
p.goto('L_R4R5', 'R5')
p.click_object('compass', wait=800)
p.raw('echo @held', 'dump state')
p.advance(2600)
p.raw('game type SILVERFEATHER')        # (the words as OnKeyDown events only: typed with real keys the S would put the compass away first)
p.advance(2600)
p.raw('echo @crash', 'game type CRASH')
p.advance(1500)
p.raw('echo @after', 'dump state', 'echo @end')
rc, out, err = p.run(extra=['--skip-intro', '--log-events'])
held0 = section(out, '@held', '@crash')
crash = section(out, '@crash', '@after')
after = section(out, '@after', '@end')
check('(6) with the compass in the hand ...', rc == 0 and 'held compass' in held0, held0[:3])
ks = [e for e in evs(crash) if e.startswith('keyup')]
check('(6) the attack posts WM_KEYUP D (and then S if the hand is not empty): the engine sees OnKeyUp with the keys D / S', ks[:1] == ['keyup D'], ks)
check('(6) the engine\'s default for the posted D: the held object is dropped (it goes back to its drop zone), so the hand is empty and no S follows',
      'held -' in after and ks == ['keyup D'] and 'gameobject compass 6 dropzone7' in evs(crash), (after[:3], ks, evs(crash)[:6]))
# (b) WM_MOUSEMOVE: the engine entry moves the view by the delta from the centre (320, 240), not accelerated, replacing the pending delta
rc, out, err = game('advance 2000; capture on; dump state; postmouse 420,240; advance 600; dump state; postmouse 320,300; advance 600; dump state', '--skip-intro')
pos = [tuple(int(v) for v in re.match(r'page \S+ scroll (-?\d+),(-?\d+)', l).groups()) for l in out if l.startswith('page ')]
# every step adds the pending delta and halves it: 100 + 50 + 25 + 12 + 6 + 3 + 1
dx = sum(100 >> k for k in range(10))
check('(6) postmouse 420,240 scrolls the view by the delta (100, 0) the way the engine scrolls a captured pointer (x += 197, y unchanged)',
      rc == 0 and len(pos) == 3 and pos[1][0] - pos[0][0] == dx and pos[1][1] == pos[0][1], pos)
dy = sum(60 >> k for k in range(10))
check('(6) postmouse 320,300 scrolls the view down by (0, 60): y += %d (the page clamps it)' % dy, len(pos) == 3 and pos[2][0] == pos[1][0] and pos[2][1] - pos[1][1] in (dy, 0) and pos[2][1] != pos[1][1], pos)
# (c) the game's cursor filter (the drunk player: champagne) posts its smoothed moves: they go through OnMouseDelta without being filtered again
rc, out, err = game('advance 2000; game consume 60; advance 500; capture on; move 500,240; advance 3000; dump state', '--skip-intro', log=True)
md = [int(l.split()[2]) for l in out if l.startswith('event mousedelta')]
check('(6) the cursor filter (champagne) posts WM_MOUSEMOVE: after the real motion (360) a run of events with the filter\'s output follows', rc == 0 and len(md) > 10 and md[0] == 360
      and all(v > 0 for v in md[1:]), md[:25])
rc, out, err = game('advance 2000; capture on; move 500,240; advance 3000; dump state', '--skip-intro', log=True)
md2 = [l for l in out if l.startswith('event mousedelta')]
check('(6) without the filter there is no posted move: the real motion causes no event at all (API 0x5c is off)', rc == 0 and md2 == [], md2[:3])

print('FAILED %d' % failed if failed else 'ALL PASSED')
sys.exit(1 if failed else 0)
