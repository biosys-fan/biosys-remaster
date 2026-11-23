"""After the view overshoots the top or bottom of a 2560 x 1024 panorama the pointer is released vertically; the release ends when the pointer
is back at the centre row of the view (release at the bottom: pointer at or above it; at the top: at or below it). Without that the player could
not look up or down again. Headless: `vrelease` prints released / scroll y / pointer row."""
import os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = 'build/test/vrelease'
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D)
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:500]))
    failed += 0 if ok else 1


def run(script):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--no-config', '--skip-intro', '--save-dir', D, '--script', 'advance 100; capture on; ' + script],
                       capture_output=True, text=True)
    out = []
    for l in r.stdout.splitlines():
        m = re.match(r'vrelease released=(\d) sy=(-?\d+) py=(\d+)', l)
        if m:
            out.append(tuple(int(g) for g in m.groups()))
    return out, r.stderr


V = 'vrelease; '
# bottom: the view can be at most 656 high (1024 - 368); the pointer moves 60 rows below the centre (240) and the step overshoots
o, e = run('scroll 1000,600; ' + V + 'move 320,300; advance 100; ' + V + 'move 320,400; advance 100; ' + V + 'move 320,330; advance 100; ' + V +
           'move 320,240; advance 100; ' + V + 'move 320,120; advance 100; ' + V)
check('bottom: scrolling down past 656 releases the pointer (the view stops at 656)', len(o) == 6 and o[0][0] == 0 and o[1] == (1, 656, 240), o)
check('bottom: further movement down moves the pointer row, not the view', o[2] == (1, 656, 400), o)
check('bottom: a pointer row below the centre keeps the release', o[3][0] == 1 and o[3][1] == 656 and o[3][2] == 330, o)
check('bottom: back at the centre row the release ends', o[4][0] == 0 and o[4][1] == 656, o)
check('bottom: after that moving up scrolls the view up again', o[5][0] == 0 and o[5][1] < 656, o)
# top
o, e = run('scroll 1000,20; ' + V + 'move 320,180; advance 100; ' + V + 'move 320,100; advance 100; ' + V + 'move 320,200; advance 100; ' + V +
           'move 320,240; advance 100; ' + V + 'move 320,380; advance 100; ' + V)
check('top: scrolling up past 0 releases the pointer (the view stops at 0)', len(o) == 6 and o[0][0] == 0 and o[1][:2] == (1, 0), o)
check('top: further movement up moves the pointer row, not the view', o[2][:2] == (1, 0) and o[2][2] == 100, o)
check('top: a pointer row above the centre keeps the release', o[3][:2] == (1, 0) and o[3][2] == 200, o)
check('top: back at the centre row the release ends', o[4][0] == 0 and o[4][1] == 0, o)
check('top: after that moving down scrolls the view down again', o[5][0] == 0 and o[5][1] > 0, o)

# The same in the overlay interface (--interface overlay, app/overlaylayout.h): there the pointer is held at the window centre and the window's motion arrives in
# window pixels; it reaches the engine as deltas divided by the view scale k (not as an absolute position), and the vertical release follows those deltas.
# Each `move` above is a delta from the pointer row the engine shows (240 after an overshoot, then the row it was moved to): the same sequences, as `overlay motion`.
def run_ovl(script, W, H):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--no-config', '--skip-intro', '--save-dir', D, '--script',
                        'overlay on %d %d; advance 100; capture on; ' % (W, H) + script], capture_output=True, text=True)
    out = []
    for l in r.stdout.splitlines():
        m = re.match(r'vrelease released=(\d) sy=(-?\d+) py=(\d+)', l)
        if m:
            out.append(tuple(int(g) for g in m.groups()))
    return out


for (W, H) in ((1920, 1080), (3440, 1440)):
    k = min(H / 368.0, W / 640.0)

    def mot(dy):
        return 'overlay motion 0,%.12f; ' % (dy * k)
    o = run_ovl('scroll 1000,600; ' + V + mot(60) + 'advance 100; ' + V + mot(160) + 'advance 100; ' + V + mot(-70) + 'advance 100; ' + V +
                mot(-90) + 'advance 100; ' + V + mot(-120) + 'advance 100; ' + V, W, H)
    check('overlay %dx%d (k = %.3f) bottom: the same release, pointer rows 400 / 330, the end at the centre row, then the view scrolls up' % (W, H, k),
          len(o) == 6 and o[0][0] == 0 and o[1] == (1, 656, 240) and o[2] == (1, 656, 400) and o[3] == (1, 656, 330) and o[4][0] == 0 and o[4][1] == 656 and o[5][0] == 0 and o[5][1] < 656, o)
    o = run_ovl('scroll 1000,20; ' + V + mot(-60) + 'advance 100; ' + V + mot(-140) + 'advance 100; ' + V + mot(100) + 'advance 100; ' + V +
                mot(40) + 'advance 100; ' + V + mot(140) + 'advance 100; ' + V, W, H)
    check('overlay %dx%d top: released at 0, pointer rows 100 / 200, the end at the centre row, then the view scrolls down' % (W, H),
          len(o) == 6 and o[1][:2] == (1, 0) and o[2] == (1, 0, 100) and o[3] == (1, 0, 200) and o[4][:2] == (0, 0) and o[5][0] == 0 and o[5][1] > 0, o)
sys.exit(1 if failed else 0)
