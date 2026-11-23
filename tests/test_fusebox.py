"""The fuse box pages. CFuseBox::Display (dll:10026d10, vtable +0x5c) shows, for every hole n, the image "fuse_NN" when a working fuse sits
in it, "blown_NN" for a blown one and the lamp "light_NN" while the fuse works and its circuit has power. The port lacked this function (and
vtable +0x60, dll:10026ed0) until 2026-11-22: every hole looked empty and every lamp was off, whatever the player did, although taking a fuse
out and plugging one in worked. The page is reached with a jump (the boxes are far from the start); the expectations come from the page file
(hotspot "bfuse_01" at 44,17..83,104, images "fuse_01" at 48,23 and "light_01" at 56,111) and the API log."""
import os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = 'build/test/fusebox'
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D + '/save')
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:500]))
    failed += 0 if ok else 1


def run(script):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', D + '/save', '--log-events', '--log-api',
                        '--script', script], capture_output=True, text=True, env=env)
    return r.returncode, (r.stdout + r.stderr).splitlines()


def region(path, x0, y0, x1, y1):
    """the palette indices of a rectangle of the 8-bit screenshot (frame coordinates)"""
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    return b''.join(d[off + (h - 1 - y) * w + x0: off + (h - 1 - y) * w + x1] for y in range(y0, y1))


def vis(lines, image):
    """the visibility values the game set for an image, in order"""
    return [int(m.group(1)) for l in lines for m in [re.search(r'SetImageVisibility window=-1 image="%s" visible=(\d)' % image, l)] if m]


def split(lines, marks):
    out, cur = {}, None
    for l in lines:
        if l in marks:
            cur = l
            out[cur] = []
        elif cur:
            out[cur].append(l)
    return out


HOLE1 = '63,116'      # the middle of "bfuse_01" (page 63,60; the view starts at row 56)
# --- the rainforest box: every hole holds a working fuse at the start
rc, out = run('advance 1500; page fbrfuse; advance 500; echo A; shot %s/a.bmp; click %s; advance 900; echo B; shot %s/b.bmp; click %s; advance 900; echo C; shot %s/c.bmp'
              % (D, HOLE1, D, HOLE1, D))
check('the run ends without error', rc == 0, out[-3:])
enter = out[:out.index('A')] if 'A' in out else []
names = ['%02d' % n for n in (1, 2, 3, 4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17)]
check('on entering, the 14 fuses of the rainforest box are shown and none as blown',
      all(vis(enter, 'fuse_' + n)[-1:] == [1] and vis(enter, 'blown_' + n)[-1:] == [0] for n in names), [(n, vis(enter, 'fuse_' + n)) for n in names])
part = split(out, ('A', 'B', 'C'))
took, plugged = part.get('A', []), part.get('B', [])
check('a click on hole 1 takes its fuse out: the hand holds it, "fuse_01" and "light_01" are hidden, the page is painted again',
      any('DoHotspotAction' in l and 'FUSE_01' in l for l in took) and vis(took, 'fuse_01')[-1:] == [0] and vis(took, 'light_01')[-1:] == [0]
      and any('apicall RedrawPage' in l for l in took), [l for l in took if 'fuse_01' in l or 'Redraw' in l][:6])
check('a click on the empty hole with the fuse in the hand plugs it in: "fuse_01" is shown again',
      any('DropHeldObject' in l and 'FUSE_01' in l and l.rstrip().endswith('-> 0') for l in plugged) and vis(plugged, 'fuse_01')[-1:] == [1]
      and any('apicall RedrawPage' in l for l in plugged), [l for l in plugged if 'fuse_01' in l or 'DropHeld' in l][:6])
# the picture: the middle of the fuse image (48,23 on the page, about 36 x 80) and of the lamp, away from the hand (which is at the right)
fa, fb, fc = [region('%s/%s.bmp' % (D, n), 52, 56 + 40, 80, 56 + 90) for n in 'abc']
diff = lambda p, q: sum(1 for i in range(len(p)) if p[i] != q[i]) / len(p)
check('the hole looks different without its fuse and as before with it again (pixels of the hole: %.0f %% changed, then %.0f %%)' % (100 * diff(fa, fb), 100 * diff(fa, fc)),
      diff(fa, fb) > 0.6 and diff(fa, fc) < 0.25, (diff(fa, fb), diff(fa, fc)))
# --- the desert box: holes 1..14 are empty at the start, 15..17 hold fuses
rc, out = run('advance 1500; page fbDfuse; advance 500; echo A')
enter = out[:out.index('A')] if 'A' in out else []
check('the desert box: hole 1 is shown empty, hole 15 with its fuse', rc == 0 and vis(enter, 'fuse_01')[-1:] == [0] and vis(enter, 'fuse_15')[-1:] == [1],
      (vis(enter, 'fuse_01'), vis(enter, 'fuse_15')))
print('fusebox: %s' % ('all checks passed' if failed == 0 else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
