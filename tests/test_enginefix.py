"""Tests of build/biosys-enginefix (src/tools/enginefix.cpp) and build/biosys-apicoverage."""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:700] if detail else ''))


def tool(mode):
    r = subprocess.run(['build/biosys-enginefix', mode], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('enginefix %s failed: %s' % (mode, r.stderr.strip()[:300]))
    return r.stdout.splitlines()


def kvs(lines):
    d = {}
    for l in lines:
        m = re.match(r'([\w-]+)=(.*)$', l)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def section(lines, start, end=None):
    i = lines.index('-- ' + start) + 1
    j = lines.index('-- ' + end, i) if end else len(lines)
    return lines[i:j]


# ============================================================ 1. idle count (exe:00416840 + MFC's message loop)
# The game's count restarts at 0 after every handled window message (WM_TIMER, a WM_MOUSEMOVE to a new position, keys, buttons)
# and grows by one per idle pass while the game's last result was exactly 1. The test game returns 1 for counts 0..2 and 0 for 3.
o = tool('idle')
seq = [('start', [])]         # (step tag, [counts seen in that step])
for l in o:
    if l.startswith('-- '):
        seq.append((l[3:], []))
    elif l.startswith('idle '):
        seq[-1][1].append(int(l.split()[1]))
steps = [(t, c) for t, c in seq]
def counts(tag, nth=None):
    r = [c for t, c in steps if t == tag]
    return r if nth is None else r[nth]
flat_idle_only = counts('idle only')
check('idle: first pass after start is 0, then 1 2 3, then silent (the game answered 0 at count 3)',
      steps[0] == ('start', [0]) and flat_idle_only == [[1], [2], [3], [], []], (steps[0], flat_idle_only))
check('idle: a mouse move to a new position restarts the count at 0, then 1 2 3, then silent',
      counts('after a mouse move') == [[0], [1], [2], [3], []], counts('after a mouse move'))
check('idle: a redundant mouse move (same position) is not a message that restarts the count', counts('redundant mouse move') == [[]])
check('idle: a moved mouse restarts it, the next pass goes on with 1', counts('moved mouse') == [[0]] and counts('moved mouse, next pass') == [[1]])
check('idle: key down, key up, button down and button up each restart at 0',
      counts('key down') == [[0]] and counts('key up') == [[0]] and counts('button down') == [[0]] and counts('button up') == [[0]])
check('idle: the WM_TIMER tick (every 500 ms) restarts the count at 0; before it the count just goes on',
      counts('before the timer') == [[1]] and counts('timer tick') == [[0]] and counts('after the timer tick') == [[1]],
      (counts('before the timer'), counts('timer tick'), counts('after the timer tick')))
check('idle: a result of 0 silences the game (count 1 was the last call) until the next restart; the restart begins at 0 again',
      counts('result 0 at count 1') == [[0], [1], [], []] and counts('restart after the silence') == [[0], [1], [], []],
      (counts('result 0 at count 1'), counts('restart after the silence')))

# ============================================================ 2. media freed by a primary page change (exe:00401050)
o = tool('media')
hd = kvs(o)
m = re.match(r'sprite=(\d+) movie=(\d+) sound=(\d+)', [l for l in o if l.startswith('handles')][0].split(' ', 1)[1].replace(' ', ' '))
sp, mv = m.group(1), m.group(2)
pc = [l[3:] for l in section(o, 'page change', 'after the page change') if l.startswith('ev ')]
def pos(ev):
    return pc.index(ev) if ev in pc else None
check('page change: OnExitPage comes first, then the sprite and the movie report stopped (3) and freed (4), then OnPageLoaded',
      pos('exit') == 0 and None not in [pos('sprite %s 3' % sp), pos('sprite %s 4' % sp), pos('movie %s 3' % mv), pos('movie %s 4' % mv), pos('loaded R4 -1')]
      and 0 < pos('sprite %s 3' % sp) < pos('sprite %s 4' % sp) < pos('movie %s 3' % mv) < pos('movie %s 4' % mv) < pos('loaded R4 -1'), pc)
check('page change: a sprite opened by OnPageLoaded reuses the handle of the freed one; no old-handle report comes after it',
      ('reuse-sprite rc=0 handle=%s' % sp) in pc and pos('reuse-sprite rc=0 handle=%s' % sp) > pos('sprite %s 4' % sp)
      and not [e for e in pc[pos('loaded R4 -1'):] if re.match(r'(sprite|movie) \d+ [34]$', e)], pc)
after = [l[3:] for l in section(o, 'after the page change', 'end') if l.startswith('ev ')]
check('page change: after the page change nothing of the old media is reported again (only the new sprite starts)',
      not [e for e in after if re.match(r'(sprite|movie) \d+ [34]$', e)] and not [e for e in after if e.startswith('movie')], after)
check('page change: OnPageDrawn and OnEnterPage follow OnPageLoaded', [e.split()[0] for e in pc if e.split()[0] in ('loaded', 'drawn', 'enter')] == ['loaded', 'drawn', 'enter'], pc)

# ============================================================ 3. the top bar
o = tool('topbar')
hd = kvs(o)
st = section(o, 'startup', 'DisplayPopupPage topbar')
check('topbar: Engine::startup() sends OnStartup only; no topbar events and no top bar window yet',
      [l for l in st if l.startswith('ev ')] == ['ev startup'] and hd['topbar-open-after-startup'] == '0' and hd['window0-after-startup'] == '0', st)
op = section(o, 'DisplayPopupPage topbar', 'DisplayPopupPage topbar again')
check('topbar: API 0x60 "topbar" opens it as window 0: OnPageLoaded, OnPageDrawn, OnPopupOpened(0), once',
      [l for l in op if l.startswith('ev ')] == ['ev loaded topbar 0', 'ev drawn topbar 0', 'ev popupopened topbar 0'] and hd['rc-first'] == '0' and hd['topbar-open'] == '1', op)
check('topbar: opening it again is a no-op that succeeds (exe:00450bd0 returns 1 for an open popup)',
      [l for l in section(o, 'DisplayPopupPage topbar again', 'end') if l.startswith('ev ')] == [] and hd['rc-second'] == '0')
o = tool('topbar-hotspot')
hd = kvs(o)
hs = section(o, 'DoHotspotAction xTopBar', 'end')
check('topbar: the intro page has the hotspot xTopBar = verb 9, page 13 (topbar); running it opens the top bar (OnPopupOpened window 0)',
      hd['xtopbar-page'] == 'eintro' and hd['xtopbar-verb'] == '9' and hd['xtopbar-target'] == '13' and hd['topbar-open-before'] == '0'
      and [l for l in hs if l.startswith('ev ')] == ['ev loaded topbar 0', 'ev drawn topbar 0', 'ev popupopened topbar 0'] and hd['topbar-open-after'] == '1', (hd, hs))
o = tool('media')
evs = [l[3:] for l in o if l.startswith('ev ')]
check('topbar: starting the game without the intro page opens the top bar (before the first page, as the intro would have)',
      evs[:5] == ['startup', 'loaded topbar 0', 'drawn topbar 0', 'popupopened topbar 0', 'loaded R4 -1'], evs[:6])

# ============================================================ 4. the commands
o = tool('api')
a = kvs(o)
def ai(k):
    return int(a[k])
check('0x1b GetDataDirectory: the BIODATA directory; DataFiles resolves names that start with it (dir + "\\name")',
      ai('getdatadir-rc') == 0 and a['datadir'].endswith('/Biodata') and ai('datadir-resolves') == 1 and ai('datadir-fonts-sep') == 1, a.get('datadir'))
check('0x66 SetCursorOverride: cursorCode() is 0 while on, comes back when off, the page load clears the flag',
      ai('cursor-code') != 0 and ai('cursor-code-override') == 0 and ai('cursor-code-off') == ai('cursor-code') and
      ai('cursor-code-after-page-load') == ai('cursor-code-expected'), a)
check('0x40 ShowCursor: false hides the cursor sprite, true shows it, a primary page load shows it again',
      ai('visible') == 1 and ai('visible-hidden') == 0 and ai('visible-shown') == 1 and ai('visible-after-page') == 1, a)
check('0x68 SetCursorClipRect: automatic = the main view; manual = the RECT; null = none; automatic again',
      ai('clip-auto') == 1 and a['clip-auto-rect'] == '0,56,640,368' and ai('clip-manual') == 1 and a['clip-manual-rect'] == '10,70,190,230'
      and ai('clip-null') == 0 and ai('clip-auto-again') == 1, a)
check('0x5d MoveMainWindowView: moves the 640x368 view inside 640x480 (y 0..112, x 0); anything else is 0x11 and changes nothing',
      ai('viewtop') == 56 and ai('move-0-0') == 0 and ai('viewtop-0') == 0 and ai('move-0-112') == 0 and ai('viewtop-112') == 112 and
      ai('move-0-113') == 17 and ai('viewtop-still-112') == 112 and ai('move-1-56') == 17 and ai('move-0--1') == 17 and
      ai('move-back') == 0 and ai('viewtop-back') == 56, a)
check('0x5a SetPanoramaScrollLimits: max < min is 0x12, a limit beyond the page width 0x11, valid limits clamp the view at once and while scrolling',
      ai('limits-bad-order') == 18 and ai('limits-too-wide') == 17 and ai('limits-rc') == 0 and ai('scrollx-after-limits') == 1000
      and ai('scrollx-right') == 1200 and ai('scrollx-left') == 1000, a)
check('0x5a: (-1, -1) clears the limits; the scrolling is free again',
      ai('limits-clear') == 0 and ai('scrollx-free') < 1000, a)
check('0x5a: the vertical lock puts y in the middle (1024 - 368) / 2 and keeps it; without it y moves',
      ai('vlock-rc') == 0 and ai('scrolly-locked') == 328 and ai('scrolly-locked-after') == 328 and ai('vlock-off') == 0 and ai('scrolly-free') != 328, a)
check('0x5a: the limits are dropped by the next primary page load', ai('limits-after-reload') == 1, a)
check('0x6c GetScrollState: active in scroll mode, not with Ctrl held or scroll mode off; reports the mouse-delta flag',
      ai('state-rc') == 0 and ai('state-active') == 1 and ai('state-active-2') == 1 and ai('state-mousedelta') == 1 and ai('state-released') == 0
      and ai('state-active-ctrl') == 0 and ai('state-active-mode-off') == 0, a)
check('0x6d SetScrollOptions: with "release at top" the pointer is released vertically when y overshoots the top (GetScrollState: released, top);'
      ' the pointer keeps its real y; clearing the option takes the pointer back to the view centre',
      ai('options-rc') == 0 and ai('release-active') == 1 and ai('release-released') == 1 and ai('release-top') == 1 and ai('release-pointer-y') == 100
      and ai('options-top-off') == 0 and ai('release-cleared') == 0 and ai('pointer-y-back') == 240, a)
check('0x6d: the bottom overshoot of a 2560x1024 page releases the pointer too (top = 0)', ai('release-bottom') == 1 and ai('release-bottom-top') == 0, a)

# ============================================================ 5. verb 16: start position and post-jump scroll
o = tool('jump')
a = kvs(o)
ev = [l[3:] for l in o if l.startswith('ev ')]
check('jump (plain path, exe:0040d4f0): the jump runs the post-jump scroll animation, then OnEnterPage',
      ev[:2] == ['startup', 'exit'] and ev[-1].startswith('enter ') and ev[-1].endswith('pumps=32') and a.get('pumps') == '32' and a.get('rc') == '0', (ev, a))
check('jump: the view starts at (a3, a4) = (500, 100) and ends scrolled by a6 = 96', a.get('scrollx') == '596' and a.get('scrolly') == '100' and a.get('page') == '1', a)

# ============================================================ 6. coverage
cov = {}
for l in subprocess.run(['build/biosys-apicoverage'], capture_output=True, text=True).stdout.splitlines():
    mm = re.match(r'0x([0-9a-f]+) (\w+) (implemented|stub)', l)
    if mm:
        cov[mm.group(2)] = mm.group(3)
names = ['GetDataDirectory', 'ShowCursor', 'SetPanoramaScrollLimits', 'MoveMainWindowView', 'SetCursorOverride', 'SetCursorClipRect', 'GetScrollState', 'SetScrollOptions']
check('apicoverage lists the 8 commands as implemented', all(cov.get(n) == 'implemented' for n in names), {n: cov.get(n) for n in names})

print('FAILED %d' % failed if failed else 'enginefix: all passed')
sys.exit(1 if failed else 0)
