"""Held objects and the personal status screen in the wide pictures (--widescreen, --interface overlay).
1. The engine can render the frame without the held image and hand the image out (Engine::viewLayers, script `heldlayer`): the frame without it plus the
   image at its own place equals the normal frame byte for byte (compass at two view directions, the goggles, whose image does not touch the view's right
   edge; the status screen: not separable, nothing shown).
2. A held compass no longer keeps the picture at 4:3 and never causes the fallback: the picture is wide, and the hand stays where the original draws it
   (view columns 460..639, cut at the right border of the centre 640 columns): overlay 3440 x 1440, overlay 1920 x 1080, plain `wideshot`.
3. After a fallback that ends (the environment status screen, Caps Lock, is laid over the page and covers the edges) the wide picture returns although
   the compass is held; goggles worn keep the 4:3 picture, also through the personal status screen.
4. The personal status screen (Tab, own palette) with the compass held at 3440 x 1440: the picture keeps its width, the centre is the status picture, the
   sides are the panorama dimmed to 40 %, the first frame after the release is the frame before the key press.
5. Palette guard: no page-clean job is submitted while the status screen is up (overlay layout with `overlay clean`, plain 4:3 with `cleansim`).
6. Faithful: the frames the game sees (framehash) do not depend on the presentation.
Pictures for review go to build/shots/."""
import os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import play_lib

T = 'build/test/heldwide'
SHOTS = 'build/shots'
shutil.rmtree(T, ignore_errors=True)
os.makedirs(T + '/save')
os.makedirs(SHOTS, exist_ok=True)
failed = 0
ENV = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + '  -- ' + str(detail)[:700])


def run(p, opts=()):
    shutil.rmtree(T + '/save', ignore_errors=True)
    os.makedirs(T + '/save')
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', T + '/save'] + list(opts) +
                       ['--script', '; '.join(p.cmds)], capture_output=True, text=True, env=ENV)
    if r.returncode:
        print(r.stderr[-600:])
    return r.stdout.splitlines()


def bmp24(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    assert struct.unpack('<H', d[28:30])[0] == 24
    stride = (w * 3 + 3) & ~3
    rows = [d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w * 3] for y in range(h)]
    return w, h, rows


def bmp8(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    assert struct.unpack('<H', d[28:30])[0] == 8
    stride = (w + 3) & ~3
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    return lambda x, y: pal[d[off + (h - 1 - y) * stride + x]]


def px(img, x, y):
    r = img[2][y]
    return (r[3 * x + 2], r[3 * x + 1], r[3 * x])


def kv(line):
    return dict(t.split('=', 1) for t in line.split() if '=' in t)


def frames(out):
    return [kv(l) for l in out if l.startswith('overlay frame')]


def shows(out):
    return [tuple(int(v) for v in kv(l)['show'].split(',')) for l in out if l.startswith('overlay frame')]


def layout(out):
    l = [x for x in out if x.startswith('overlay layout')][-1]
    v = kv(l)
    vx, vy, vw, vh = [float(t) for t in v['view'].split(',')]
    return float(v['k']), vx, vy


def at_r5(p, overlay=None, hold='compass'):
    """walk R4 -> R5 and pick the object up (real input)"""
    if overlay:
        p.raw('overlay on %d %d' % overlay)
    p.advance(1000)
    p.goto('L_R4R5', 'R5')
    p.click_object(hold, wait=800)


def turn(p, n=12):
    p.raw('capture on')
    for i in range(n):
        p.raw('move %d,240' % (330 + 6 * i))
        p.advance(33)


# ------------------------------------------------------------------------------------------------------------------------ 1. the invariant
p = play_lib.Play('R4')
at_r5(p)
p.raw('scroll 400,200', 'advance 300', 'heldlayer')
turn(p, 14)
p.advance(300)
p.raw('heldlayer')
p.raw('keydown TAB', 'advance 100', 'heldlayer', 'keyup TAB', 'advance 100', 'heldlayer')
out = run(p)
hl = [kv(l) for l in out if l.startswith('heldlayer')]
check('the run gives four heldlayer lines', len(hl) == 4, out[-5:])
if len(hl) == 4:
    for i, name in ((0, 'compass at the first direction'), (1, 'compass after turning')):
        h = hl[i]
        check('%s: the image is separable (shown), frame without it + image = normal frame, and the frame without it differs (x=%s w=%s)' % (name, h['x'], h['w']),
              h['shown'] == '1' and h['equal'] == '1' and h['differs'] == '1', h)
    check('the compass image is the hand cut at the right edge of the view (180 x 153 at 460,215: x + w = 640)',
          (hl[0]['x'], hl[0]['y'], hl[0]['w'], hl[0]['h']) == ('460', '215', '180', '153'), hl[0])
    check('status screen up (Tab): nothing is separated (shown 0) and the frames are equal', hl[2]['shown'] == '0' and hl[2]['equal'] == '1', hl[2])
    check('after the status screen the image is separable again', hl[3]['shown'] == '1' and hl[3]['equal'] == '1', hl[3])

p = play_lib.Play()
cheat = lambda w: (p.advance(2600), p.type(w, wait=200))
p.advance(2000)
cheat('SILVERFEATHER')
cheat('GOTONDB4DEAD\\n'); p.advance(3000); p.page = 'NDB4DEAD'
p.click_object('forekey', wait=800)
cheat('GOTOCE9\\n'); p.advance(3000); p.page = 'CE9'
p.click_hotspot('fce9cab', wait=2000); p.page = 'FCE9CAB_C'
p.click_hotspot('yLock', page='fce9cab_c', wait=3000); p.page = 'FCE9CAB_O'
p.click_object('goggles', wait=800)
cheat('GOTOR5\\n'); p.advance(3000)
p.raw('scroll 400,200', 'advance 500', 'overlay on 3440 1440', 'heldlayer', 'overlay frame', 'overlay layout', 'overlay shot %s/goggles.bmp' % T)
p.raw('echo mark worn', 'key space', 'advance 1500', 'heldlayer', 'overlay frame', 'overlay frame')
p.raw('keydown TAB', 'advance 100', 'overlay frame', 'overlay frame', 'keyup TAB', 'advance 100', 'overlay frame', 'overlay frame')
out = run(p)
i = out.index('mark worn')
h = [kv(l) for l in out if l.startswith('heldlayer')]
check('goggles held (image 240 x 203 at 380,165: does not touch the right edge): separable, frame without it + image = normal frame (%s)' % (h[0] if h else None),
      bool(h) and h[0]['shown'] == '1' and h[0]['equal'] == '1' and h[0]['differs'] == '1' and h[0]['x'] == '380' and h[0]['w'] == '240', h)
fr0 = shows(out[:i])
check('... and the picture is wide with them held (show=%s)' % str((fr0[-1] if fr0 else None)), bool(fr0) and fr0[-1] == (119, 119), fr0)
frw = shows(out[i:])
check('goggles worn: the 4:3 picture (show 0,0 in all %d frames), also during and after the status screen (the state is kept)' % len(frw),
      len(frw) == 6 and all(s == (0, 0) for s in frw), frw)

# ------------------------------------------------------------------------------------------------------------------------ 2. the hand stays where it is, the picture is wide
def hand_columns(a, b, X0, X1, Y0, Y1, step=1, thresh=15):
    """columns of the window picture where `a` and `b` differ in at least thresh rows of Y0..Y1 -> list of X"""
    cols = []
    for X in range(X0, X1):
        n = 0
        for Y in range(Y0, Y1, step):
            if px(a, X, Y) != px(b, X, Y):
                n += 1
        if n >= thresh:
            cols.append(X)
    return cols


for W, H in ((3440, 1440), (1920, 1080)):
    p = play_lib.Play('R4')
    at_r5(p, (W, H))
    p.raw('overlay layout', 'overlay frame', 'overlay frame', 'overlay shot %s/held_%d.bmp' % (T, W), 'echo mark stash')
    p.key('s', wait=2000)
    p.raw('overlay frame', 'overlay frame', 'overlay shot %s/free_%d.bmp' % (T, W))
    out = run(p)
    k, vx, vy = layout(out)
    fr = shows(out)
    check('overlay %dx%d, compass held: wide picture (show=%s), no fallback' % (W, H, fr[1] if len(fr) > 1 else None), len(fr) >= 3 and fr[1][0] > 0 and fr[1][0] == fr[1][1], fr)
    A, B = bmp24('%s/held_%d.bmp' % (T, W)), bmp24('%s/free_%d.bmp' % (T, W))
    Y0, Y1 = int(vy + 230 * k), int(vy + 360 * k)
    cols = hand_columns(A, B, int(vx + 420 * k), min(W, int(vx + 640 * k) + 150), Y0, Y1, step=2)
    vc = [(X - vx) / k for X in cols]
    check('... the hand is at view columns %.0f..%.0f (460..639 expected: unmoved) and nothing of it right of the centre picture (last column %.1f)'
          % (min(vc), max(vc), max(vc)), 455 <= min(vc) <= 480 and 636 <= max(vc) <= 640.5, (min(vc), max(vc)))
    if W == 3440:
        sips = subprocess.run(['sips', '-s', 'format', 'png', '%s/held_%d.bmp' % (T, W), '--out', SHOTS + '/held_dell_compass.png'], capture_output=True)
    else:
        subprocess.run(['sips', '-s', 'format', 'png', '%s/held_%d.bmp' % (T, W), '--out', SHOTS + '/held_1080_compass.png'], capture_output=True)

p = play_lib.Play('R4')
at_r5(p)
p.raw('wideshot %s/wh.bmp 2 2560 plain=%s/wh4.bmp' % (T, T), 'echo mark stash')
p.key('s', wait=2000)
p.raw('wideshot %s/wf.bmp 2 2560' % T)
out = run(p)
ws = [l for l in out if l.startswith('wideshot')]
kv0 = kv(ws[0]) if ws else {}
check('plain --widescreen capture, compass held: mode=wide left=%s right=%s layer=%s' % (kv0.get('left'), kv0.get('right'), kv0.get('layer')),
      bool(ws) and 'mode=wide' in ws[0] and int(kv0['left']) > 0 and kv0['layer'] == '1', ws)
A, B = bmp24(T + '/wh.bmp'), bmp24(T + '/wf.bmp')
c0 = 640        # (2560 - 1280) / 2: the centre picture starts at this output column, 2 pixels per frame pixel
cols = hand_columns(A, B, c0 + 2 * 420, 2560, 2 * (56 + 230), 2 * (56 + 360), step=2)
vc = [(X - c0) / 2 for X in cols]
check('... the hand is at view columns %.0f..%.0f (460..639 expected) and nothing right of the centre picture' % (min(vc), max(vc)),
      455 <= min(vc) <= 480 and 638 <= max(vc) <= 640, (min(vc), max(vc)))
subprocess.run(['sips', '-s', 'format', 'png', T + '/wh.bmp', '--out', SHOTS + '/held_wide_plain.png'], capture_output=True)

# ------------------------------------------------------------------------------------------------------------------------ 3. a fallback that ends
p = play_lib.Play('R4')
at_r5(p, (3440, 1440))
p.raw('scroll 400,200', 'advance 300', 'overlay frame', 'overlay frame', 'echo mark caps', 'keydown CAPS', 'advance 100')
for _ in range(3):
    p.raw('overlay frame')
p.raw('keyup CAPS', 'advance 33')
for _ in range(14):
    p.raw('overlay frame', 'advance 33')
out = run(p)
i = out.index('mark caps')
before, after = shows(out[:i]), shows(out[i:])
check('compass held, wide before the environment screen (show=%s)' % str((before[-1] if before else None)), bool(before) and before[-1] == (119, 119), before)
check('the environment screen (laid over the page) drops the picture to 4:3 (show=%s)' % str((after[0] if after else None)), bool(after) and after[0] == (0, 0), after[:4])
check('and after it is released the wide picture returns with the compass held (after %d frames: %s)' % (len(after), str(after[-1])), after[-1] == (119, 119) and (0, 0) in after[:4], after)

# ------------------------------------------------------------------------------------------------------------------------ 4. the status screen keeps the wide picture
W, H = 3440, 1440
p = play_lib.Play('R4')
at_r5(p, (W, H))
p.raw('scroll 400,200', 'advance 300', 'overlay layout', 'overlay frame', 'overlay frame', 'overlay shot %s/s_before.bmp' % T, 'echo mark tab', 'keydown TAB', 'advance 100')
p.raw('overlay frame', 'overlay shot %s/s_tab.bmp' % T, 'shot %s/s_tab8.bmp' % T)
for _ in range(5):
    p.raw('overlay frame', 'advance 33')
p.raw('wideshot %s/ws_tab.bmp 2 2560' % T)
p.raw('keyup TAB', 'advance 33', 'overlay frame', 'overlay shot %s/s_after.bmp' % T, 'echo mark rest', 'overlay frame', 'overlay frame')
out = run(p)
i, j = out.index('mark tab'), out.index('mark rest')
k, vx, vy = layout(out)
sb, sd, sa = shows(out[:i]), shows(out[i:j]), shows(out[j:])
check('Tab with the compass held: the picture stays wide (show %s before, %s of %d frames while it is up, %s after the release)' % (str(sb[-1]), str(sd[0]), len(sd), str(sa[0] if sa else None)),
      sb[-1] == (119, 119) and all(s == (119, 119) for s in sd) and bool(sa) and all(s == (119, 119) for s in sa), (sb, sd, sa))
A, S, C = bmp24(T + '/s_before.bmp'), bmp24(T + '/s_tab.bmp'), bmp24(T + '/s_after.bmp')
ref = bmp8(T + '/s_tab8.bmp')
tot = n = base = 0
for r in range(10, 300, 4):
    for c in range(8, 632, 4):
        X, Y = int(vx + (c + 0.5) * k), int(vy + (r + 0.5) * k)
        e = ref(c, 56 + r)
        tot += sum(abs(a - b) for a, b in zip(px(S, X, Y), e))
        base += sum(abs(a - b) for a, b in zip(px(A, X, Y), e))
        n += 1
check('the centre columns are the status picture, opaque (mean abs difference to the 4:3 status picture %.2f per channel; the picture before Tab would give %.1f)' % (tot / n / 3, base / n / 3),
      tot / n / 3 < 6 and base / n / 3 > 3 * (tot / n / 3), (tot / n / 3, base / n / 3))


def mean(img, x0, x1, y0, y1):
    return sum(sum(img[2][y][3 * x0:3 * x1]) for y in range(y0, y1)) / ((x1 - x0) * (y1 - y0) * 3)


for side, (x0, x1) in (('left', (20, int(vx) - 10)), ('right', (int(vx + 640 * k) + 10, W - 20))):
    a, s = mean(A, x0, x1, int(vy) + 10, int(vy + 368 * k) - 10), mean(S, x0, x1, int(vy) + 10, int(vy + 368 * k) - 10)
    check('the %s side columns are the panorama dimmed to 40 %% (mean brightness %.1f -> %.1f, ratio %.3f)' % (side, a, s, s / a), 0.37 <= s / a <= 0.43, (a, s))
diff = 0
for Y in range(80, 1250, 2):
    ra, rc = A[2][Y], C[2][Y]
    if ra != rc:
        diff += sum(1 for X in range(0, W) if ra[3 * X:3 * X + 3] != rc[3 * X:3 * X + 3])
check('the first frame after the release is the frame before the key press (differing pixels outside the text rows: %d)' % diff, diff == 0, diff)
wt = [l for l in out if l.startswith('wideshot')]
check('plain --widescreen capture during Tab: wide (%s)' % (wt[0].split('mode=')[1].split()[0:3] if wt else None), bool(wt) and 'mode=wide' in wt[0], wt)
Aw = bmp24(T + '/ws_tab.bmp')
tot = n = 0
for r in range(0, 368, 3):
    for c in range(0, 640, 3):
        tot += sum(abs(a - b) for a, b in zip(px(Aw, 640 + 2 * c, 2 * (56 + r)), ref(c, 56 + r)))
        n += 1
check('... its centre columns are the status picture (mean abs difference %.3f per channel)' % (tot / n / 3), tot / n / 3 < 0.5, tot / n / 3)
wl = mean(Aw, 40, 600, 2 * 70, 2 * 400)
check('... and its side columns are dimmed (left side mean brightness %.1f)' % wl, wl < 60, wl)
subprocess.run(['sips', '-s', 'format', 'png', T + '/s_tab.bmp', '--out', SHOTS + '/held_dell_status.png'], capture_output=True)

# ------------------------------------------------------------------------------------------------------------------------ 5. palette guard
p = play_lib.Play('R4')
at_r5(p, (W, H))
p.raw('scroll 400,200', 'advance 300', 'overlay clean on')
for _ in range(3):
    p.raw('overlay frame', 'advance 33')
p.raw('overlay clean stats', 'keydown TAB', 'advance 100')
for _ in range(4):
    p.raw('overlay frame', 'advance 33')
p.raw('overlay clean stats', 'keyup TAB', 'advance 33')
for _ in range(4):
    p.raw('overlay frame', 'advance 33')
p.raw('overlay clean stats')
out = run(p)
st = [kv(l) for l in out if l.startswith('overlay clean')]
pj = [int(s['pagejobs']) for s in st]
check('overlay layout, --picture clean: page-clean jobs before Tab / while it is up / after the release: %s (none submitted with the status palette)' % pj,
      len(pj) == 3 and pj[0] >= 1 and pj[0] == pj[1] == pj[2], pj)
p = play_lib.Play('R4')
at_r5(p)
p.raw('window 60 33', 'cleansim on tex=2 scale=2', 'advance 1500', 'cleansim wait', 'cleansim stats', 'keydown TAB', 'advance 600', 'cleansim wait', 'cleansim stats',
      'keyup TAB', 'advance 600', 'cleansim wait', 'cleansim stats', 'cleansim off', 'window off')
out = run(p)
st = [kv(l) for l in out if l.startswith('cleansim frames=')]
pj = [int(s['pageJobs']) for s in st]
check('4:3 window simulation, --picture clean: page-clean jobs before Tab / while it is up / after the release: %s' % pj, len(pj) == 3 and pj[0] >= 1 and pj[0] == pj[1] == pj[2], pj)

# ------------------------------------------------------------------------------------------------------------------------ 6. faithful
def faithful(extra_overlay, opts):
    p = play_lib.Play('R4')
    if extra_overlay:
        p.raw('overlay on 3440 1440')
    p.advance(1000)
    p.goto('L_R4R5', 'R5')
    p.click_object('compass', wait=800)
    marks = ['framehash']
    p.raw('framehash')
    for step in ('keydown TAB', 'keyup TAB'):
        p.raw(step, 'advance 33')
        if extra_overlay:
            p.raw('overlay frame')
        p.raw('framehash')
    turn(p, 6)
    if extra_overlay:
        p.raw('overlay frame')
    p.raw('framehash')
    out = run(p, opts)
    return [l.split()[-1] for l in out if l.startswith('framehash')]


base = faithful(False, ())
check('framehash sequence of compass + Tab + turning (%d frames) is made of distinct frames where the game changes' % len(base), len(base) == 4 and len(set(base)) >= 2, base)
for label, extra, opts in (('--widescreen', False, ('--widescreen',)), ('--interface overlay', False, ('--interface', 'overlay')), ('overlay frames drawn between', True, ())):
    h = faithful(extra, opts)
    check('framehash sequence identical with %s' % label, h == base, (h, base))

print('heldwide tests: %d failed' % failed)
sys.exit(1 if failed else 0)
