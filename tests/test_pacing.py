import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play_lib import *

os.chdir(ROOT)
fails = 0
DISPLAYS = (60, 120, 144, 0)          # 0: a display that does not wait for the vertical sync
P = 33


def check(name, ok, info=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else '  -- ' + str(info)[:600]))
    if not ok:
        fails += 1


def run(script, extra=(), game=False):
    cmd = ['build/biosys', '--mute'] + (['--game', '--seed', '1'] if game else ['--no-objects']) + list(extra) + ['--script', script]
    return subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()


def kv(line):
    return dict(m.groups() for m in re.finditer(r'(\w+)=(\S+)', line))


def wtimes(out):
    return [int(l.split('t=')[1]) for l in out if l.startswith('wtime')]


# 1. passes and presents ------------------------------------------------------------------------------------------------------------
for pm in (33, 50, 20):
    counts = {}
    for hz in DISPLAYS:
        o = run('page R4; window %d %d; advance 100; wreset; advance 10000; wstats' % (hz, pm))
        k = kv([l for l in o if l.startswith('wstats')][0])
        counts[hz] = (int(k['passes']), int(k['presents']), int(k['ms']))
    exp = 10000 / pm
    check('pass interval %d ms: engine passes per 10 simulated seconds are the same on every display (%s), within one pass of 10000 / P' %
          (pm, ', '.join('%s Hz: %d' % (h, counts[h][0]) for h in DISPLAYS)),
          max(c[0] for c in counts.values()) - min(c[0] for c in counts.values()) <= 1 and all(abs(c[0] - exp) <= 1.5 for c in counts.values()), counts)
    if pm == 33:
        check('the window still presents at the display\'s rate (60, 120, 144 Hz: frames per 10 s within 1 %; unthrottled display: more than 500 a second at most 1000)',
              all(abs(counts[h][1] - h * 10) <= max(3, h * 0.1) for h in (60, 120, 144)) and 5000 <= counts[0][1] <= 10001, counts)

# 2. zooms, fades: the time they take is the same on every display and equals frames x P (or the original's clock delay) ---------------
def timed(setup, action, hz, pm=P, trace=True):
    o = run('%s; window %d %d; advance 200; pumptrace on; wtime; %s; wtime' % (setup, hz, pm, action))
    t = wtimes(o)
    pumps = [int(l.split('t=')[1]) for l in o if l.startswith('pump')]
    return t[1] - t[0], len(pumps), o


cases = (('zoom-in (12 frames)', 'page T1; scroll 1000,300', 'clickhs L_T1R10'),
         ('zoom-out of the right-click return (13 frames)', 'page R10; scroll 1000,300; clickhs FR10htch_C', 'rclick 320,200'),
         ('arrival zoom into a big page', 'page CE7A; scroll 320,600', 'clickhs JL_brock'))
for name, setup, action in cases:
    res = {hz: timed(setup, action, hz) for hz in DISPLAYS}
    frames = {r[1] for r in res.values()}
    durs = {hz: r[0] for hz, r in res.items()}
    n = list(frames)[0]
    check('%s: %d frames on every display' % (name, n), len(frames) == 1 and n >= 12, frames)
    # the first frame is shown from wherever the pass schedule is (0 .. P before its next slot), the others for P each
    check('%s: takes frames x P (%d ms; minus the phase of the first turn, up to one pass) on every display, and the same time on all (%s)' % (name, n * P, durs),
          all(n * P - P - 8 <= d <= n * P + 3 for d in durs.values()) and max(durs.values()) - min(durs.values()) <= P + 5, durs)

# a mouse scroll: the same gesture ends at the same view position, and takes the same time
ends = {}
for hz in DISPLAYS:
    o = run('page R4; scroll 1000,300; capture on; window %d %d; advance 100; move 320,240; wtrace on; wtime; move 340,240; advance 1500; wtime' % (hz, P))
    fr = [kv(l) for l in o if l.startswith('wframe')]
    t0 = wtimes(o)[0]
    last = fr[-1]
    first_at = next(int(f['t']) for f in fr if (f['sx'], f['sy']) == (last['sx'], last['sy']))
    ends[hz] = (last['sx'], last['sy'], first_at - t0)
check('mouse scroll of one gesture (+20 px): ends at the same position on every display and settles at the same time (%s)' % ends,
      len({(v[0], v[1]) for v in ends.values()}) == 1 and ends[60][0] != '1000' and
      max(v[2] for v in ends.values()) - min(v[2] for v in ends.values()) <= P, ends)
# motion events that arrive between two passes add up (a display faster than the pass rate loses no distance)
o1 = run('page R4; scroll 1000,300; capture on; window 120 33; advance 100; move 320,240; move 340,240; advance 1500; wtrace on; advance 40')
o2 = run('page R4; scroll 1000,300; capture on; window 120 33; advance 100; move 320,240; move 330,240; move 340,240; advance 1500; wtrace on; advance 40')
a = [l for l in o1 if l.startswith('wframe')][-1]; b = [l for l in o2 if l.startswith('wframe')][-1]
check('two motion events of 10 px before one pass scroll as far as one of 20 px (the pending delta adds up)',
      kv(a)['sx'] == kv(b)['sx'] and kv(a)['sx'] != '1000', (a, b))

# palette fade (FadeToColour, speed 2: 100 steps, 16 ms each: the original spins on GetTickCount) and cross-fade (9 frames)
fd = {hz: timed('page R4', 'api FadeToColour 255 128 0 0 2 -1 100', hz)[0] for hz in DISPLAYS}
check('palette fade speed 2 (100 steps x 16 ms by the clock, not by the pass): %s ms, the same on every display (within 15 %%)' % fd,
      all(1550 <= d <= 1950 for d in fd.values()) and max(fd.values()) - min(fd.values()) <= 0.15 * 1700, fd)
fd1 = {hz: timed('page R4', 'api FadeToColour 255 128 0 0 1 -1 100', hz)[0] for hz in DISPLAYS}
check('palette fade speed 1 (60 steps, 1 ms delay) is quick, not 60 passes (%s ms)' % fd1, all(d < 600 for d in fd1.values()) and max(fd1.values()) - min(fd1.values()) < 300, fd1)
xf = {}
for hz in DISPLAYS:
    d, n, o = timed('page fce9cab_c; api EnableHotspot -1 b_locked 0; xfade lookups\\clubeden\\ce_day.xft current', 'click 300,350', hz)
    xf[hz] = (d, n)
check('cross-fade (9 frames): takes 9 x P on every display (%s)' % xf, all(n == 9 and 9 * P - P - 8 <= d <= 9 * P + 3 for d, n in xf.values()), xf)

# 3. the smooth camera -----------------------------------------------------------------------------------------------------------------
for hz in (60, 144):
    o = run('page R4; scroll 1000,300; capture on; window %d %d; advance 100; move 320,240; wtrace on; move 360,240; advance 800' % (hz, P))
    fr = [kv(l) for l in o if l.startswith('wframe')]
    yaw = [float(f['yaw']) for f in fr]
    sx = [int(f['sx']) for f in fr]
    steps = len({s for s in sx})
    check('smooth camera at %d Hz: the displayed yaw never moves backwards, ends on the engine\'s camera, and takes more positions than the engine has steps (%d > %d)' %
          (hz, len(set(yaw)), steps),
          all(b >= a - 1e-9 for a, b in zip(yaw, yaw[1:])) and yaw[-1] > yaw[0] + 20 and len(set(yaw)) > steps and abs(yaw[-1] - yaw[-2]) < 1e-9, (yaw[:5], yaw[-3:]))
    o = run('page CE7A; scroll 320,600; window %d %d; advance 100; wtrace on; clickhs JL_brock; advance 200' % (hz, P))
    fr = [kv(l) for l in o if l.startswith('wframe') and 'fov' in l]
    fov = [float(f['fov']) for f in fr]
    k = fov.index(max(fov))
    zs = fov[k:]
    zs = zs[:next((i for i, v in enumerate(zs) if v == 75.0 and all(w == 75.0 for w in zs[i:])), len(zs))]
    check('smooth camera at %d Hz through the arrival zoom: the field of view comes down monotonically from %.1f to 75 through %d different values (more than the 19 zoom frames)' %
          (hz, fov[k], len(set(zs))), all(b <= a + 1e-9 for a, b in zip(zs, zs[1:])) and fov[k] > 120 and len(set(zs)) > (30 if hz == 144 else 19), (zs[:6], len(set(zs))))
    o = run('page R10; scroll 1000,300; window %d %d; advance 100; wtrace on; clickhs FR10htch_C; advance 100; rclick 320,200; advance 100' % (hz, P))
    fr = [kv(l) for l in o if l.startswith('wframe') and 'fov' in l]
    check('smooth camera at %d Hz: some frames of the zoom-out glide instead of snapping (snap=0 with fov below 75)' % hz,
          any(f['snap'] == '0' and float(f['fov']) < 74.9 for f in fr), [f['fov'] for f in fr][:8])

# 4. the journey's busy wait (FUN_10007a40 of CPath::TravelStep, a GetTickCount spin of (1 - energy / 100) seconds), performed by the window only
p = Play()
p.advance(2000)
p.raw('window 60 %d' % P, 'advance 200', 'game self', 'echo @a', 'pumptrace on')
p.goto('L_R4R5', 'R5', wait=0)
p.raw('advance 3000', 'pumptrace off', 'dump state')
rc, out, err = p.run()
en = float([l for l in out if 'self.m_dEnergy =' in l][0].split('=')[1])
pu = [int(l.split('=')[1]) for l in out if l.startswith('pump')]
want = int((en * -0.01 + 1.0) * 1000.0)
quiet = Play()
quiet.advance(2000)
quiet.raw('echo @a', 'pumptrace on')
quiet.goto('L_R4R5', 'R5', wait=0)
quiet.raw('advance 3000', 'pumptrace off')
rc2, out2, err2 = quiet.run()
pq = [int(l.split('=')[1]) for l in out2 if l.startswith('pump')]
check('journey: the window waits %d ms (energy %.0f) before it jumps to the destination: the turns span %d ms, the headless run (no wait, 40 ms turns) %d ms and still arrives' %
      (want, en, pu[-1] - pu[0], pq[-1] - pq[0]),
      rc == 0 and 'page R5' in ' '.join(l for l in out if l.startswith('page ')) and abs((pu[-1] - pu[0]) - (len(pq) - 1) * P - want) <= 2 * P + 40 and
      all(b - a == 40 for a, b in zip(pq, pq[1:])), (pu[:3], pu[-3:], want))

# 5. the option and the default
r = subprocess.run(['build/biosys', '--pass-ms', '5', '--script', 'echo x'], capture_output=True, text=True)
r2 = subprocess.run(['build/biosys', '--pass-ms', '101', '--script', 'echo x'], capture_output=True, text=True)
r3 = subprocess.run(['build/biosys', '--no-objects', '--mute', '--pass-ms', '10', '--script', 'echo ok'], capture_output=True, text=True)
check('--pass-ms accepts 10..100 and refuses 5 and 101', r.returncode != 0 and r2.returncode != 0 and r3.returncode == 0 and 'ok' in r3.stdout, (r.stderr, r3.stderr))
h = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
check('--help lists --pass-ms with its default 33', '--pass-ms N' in h and 'default 33' in h)

print('%d failure(s)' % fails)
sys.exit(1 if fails else 0)
