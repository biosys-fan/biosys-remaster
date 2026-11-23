"""Picture stability of what the player sees (tests/audit/visual.py): sequences of presented frames in many scenarios, checked for flicker, colour
blips, wrong palettes, geometry changes and pictures that move when they should be still.

  python3 -I tests/test_visual.py [--only TEXT] [--jobs N] [--quick] [--list] [--no-shots]

Two families of scenarios, both headless:
  faithful   the 640 x 480 frame the original shows (the reference): every one of the 443 pages idle, then held objects, Tab, a turn, a walk;
  player     what `--picture clean --interface overlay` shows on a 3440 x 1440 display (`overlay on 3440 1440` + `overlay clean on`: the composition of the
             window, the cleaned page texture filled band by band), a sample of pages (every kind of site, indoors, close-ups, a film, the pod, a lift) idle,
             with the compass / torch / goggles, turning slowly and quickly, Tab, the inventory strip, the menu, walks by day, night and in the rain;
             and the same in the variants that change the path (no cleaner, 1920 x 1080, sharp text).
What can change by itself is read from the engine's state in each frame line (see visual.py), so a still page must be still to the pixel.

Results: build/audit/findings_visual.txt (per finding: scenario, rule, frames, values, a script that reproduces it), frames around the worst ones in
build/audit/visual/. tests/audit/verdicts/d.tsv holds the explained cases (`V <tab> scenario <tab> rule <tab> ok: REASON | open`): `open` does not
fail, a finding that is not in the table does."""
import os, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.chdir(ROOT)
sys.path.insert(0, os.path.join(HERE, 'audit'))
sys.path.insert(0, HERE)
import visual
import play_lib
from visual import Run, check_frames

OUT = os.path.join(ROOT, 'build', 'audit')
SHOTS = os.path.join(OUT, 'visual')
VERDICTS = os.path.join(HERE, 'audit', 'verdicts', 'd.tsv')
os.makedirs(visual.TMP, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)

args = sys.argv[1:]
QUICK = '--quick' in args
ONLY = args[args.index('--only') + 1] if '--only' in args else ''
JOBS = int(args[args.index('--jobs') + 1]) if '--jobs' in args else max(2, (os.cpu_count() or 4) - 2)
LIST = '--list' in args
NOSHOTS = '--no-shots' in args

W, H = 3440, 1440


class Scn:
    """a scenario: the run and how to judge it: checks = [(phase or None, kwargs of check_frames)]"""

    def __init__(self, name, run, checks=None, note='', expect=None):
        self.name, self.run, self.note, self.expect = name, run, note, expect
        self.checks = checks or [(None, {})]


SCN = []


def add(name, run, checks=None, note='', expect=None):
    run.name = name
    SCN.append(Scn(name, run, checks, note, expect))
    return run


def hold(r, obj, wait=300):
    r.raw('hold %s' % obj, 'advance %d' % wait)


def wear_goggles(r):
    r.raw('hold goggles', 'advance 300', 'key space', 'advance 1500')


def prep(r, what):
    """what is in the hand / worn: None | compass | torch | goggles"""
    if what == 'compass':
        hold(r, 'compass')
    elif what == 'torch':
        r.raw('hold torch', 'advance 300', 'var torch torchon', 'advance 300')
    elif what == 'goggles':
        wear_goggles(r)


def player(name, page, what=None, w=W, h=H, mode='player', sharp=False, start=None):
    r = Run(name, mode=mode, page=page, w=w, h=h, sharp=sharp)
    if start:
        r.cmds.insert(1, start)           # before the page is entered (the time of day)
    prep(r, what)
    return r


# ------------------------------------------------------------------------------------------------------------------------------ faithful family
ALL_PAGES = sorted(p for p in play_lib.load_pages() if not p.startswith('MENU') and p != 'TOPBAR')
for pg in ALL_PAGES:
    add('faithful.idle.' + pg, Run('f', mode='faithful', page=pg).phase('idle').frames(12), [(None, dict(still=True))])

for pg in ('R4', 'R5', 'CE7', 'PH1', 'S4', 'FPH6MIR'):
    for what in ('compass', 'torch', 'goggles'):
        r = player('f', pg, what, mode='faithful')
        add('faithful.hold-%s.%s' % (what, pg), r.phase('idle').frames(20), [(None, dict(still=(what != 'torch')))])

for pg in ('R4', 'R5', 'CE7', 'PH1'):
    for what in (None, 'compass', 'goggles'):
        r = player('f', pg, what, mode='faithful')
        r.phase('before').frames(6)
        r.act('keydown TAB').phase('down').frames(20)
        r.act('keyup TAB').phase('up').frames(24)
        add('faithful.tab-%s.%s' % (what or 'empty', pg), r, [(None, dict(events=None, roundtrip=(5, 'last')))])

# ------------------------------------------------------------------------------------------------------------------------------ the checker against faults it must see
# (`overlay fault ...`: the bug of the "weird colour after Tab" (the status screen's palette given to the page cleaner), and one frame with red and blue swapped)
r = player('selftest', 'R5', 'compass')
r.raw('overlay fault statuspal')
r.phase('before').frames(6)
r.act('keydown TAB').phase('down').frames(12)
r.act('keyup TAB').phase('up').frames(30)
add('selftest.statuspal-after-tab', r, [(None, dict(roundtrip=(5, 'last')))], expect={'unsettled', 'jump', 'cellblip', 'flicker', 'mixed'})
r = player('selftest', 'R5', 'compass').phase('idle').frames(4)
r.raw('overlay fault blue 3').frames(8)
add('selftest.one-blue-frame', r, [(None, dict(still=True))], expect={'notstill', 'flicker', 'cellblip', 'jump'})

# ------------------------------------------------------------------------------------------------------------------------------ player family
IDLE_PAGES = ['R4', 'R5', 'S4', 'W1', 'T1', 'DB1', 'DB6CAVE', 'CE7', 'CE3', 'PH1', 'PH6', 'O1', 'BD', 'EANGEL1', 'BPOD', 'BDLIFT_I', 'FPH6MIR', 'FCE9CAB_C', 'FBD5LUNG',
              'XFILE1', 'AR1LO', 'EDEATH']
for pg in IDLE_PAGES:
    add('player.idle.' + pg, player('p', pg).phase('idle').frames(20), [(None, dict(still=True))])
for pg in ('R4', 'R5', 'CE7', 'PH1', 'S4', 'DB1'):
    for what in ('compass', 'torch', 'goggles'):
        add('player.hold-%s.%s' % (what, pg), player('p', pg, what).phase('idle').frames(20), [(None, dict(still=(what != 'torch')))])


def turn(r, n, step, start=100):
    """the captured pointer turns the view: x must grow every step (a page pixel per mouse pixel x 3)"""
    r.raw('capture on', 'move %d,240' % start)
    for i in range(n):
        r.raw('move %d,240' % (start + step * (i + 1)))
        r.frame()


def turn_pair(name, pg, what, n, step):
    """a turn through the player's path and, as the reference, through the plain one (no cleaner): the two must agree"""
    def build(mode):
        r = player('p', pg, what, mode=mode).phase('turn')
        turn(r, n, step)
        return r
    s = add(name, build('player'), [(None, dict(turning=True))])
    t = add(name + '~plain', build('overlay'), [(None, dict(turning=True))])
    TWIN[name] = name + '~plain'


TWIN = {}
turn_pair('player.turn-slow-compass.R5', 'R5', 'compass', 146, 6)                         # 146 frames x 18 page pixels: a full circle of the 2560-pixel page
turn_pair('player.turn-slow-compass.R4', 'R4', 'compass', 70, 6)                          # half a circle, in the rain
for pg, what in (('R5', None), ('R5', 'compass'), ('S4', 'compass'), ('CE7', None), ('T1', 'goggles')):
    turn_pair('player.turn-quick-%s.%s' % (what or 'empty', pg), pg, what, 40, 40)        # 40 frames x 120 page pixels: a bit over a full circle
for pg in ('R5', 'R4'):                   # stop the turn and look: the picture must settle at once
    r = player('p', pg, 'compass').phase('turn')
    turn(r, 30, 20)
    r.phase('rest').frames(20)
    add('player.turn-stop.' + pg, r, [('turn', dict(turning=True)), ('rest', dict(still=True, skip_first=3))])

for pg in ('R5', 'R4', 'CE7', 'PH1', 'S4'):
    for what in (None, 'compass', 'goggles', 'torch'):
        if what in ('torch', 'goggles') and pg not in ('R5', 'CE7'):
            continue
        if what is None and pg not in ('R5', 'R4'):
            continue
        r = player('p', pg, what)
        r.phase('before').frames(6)
        r.act('keydown TAB').phase('down').frames(12)
        r.act('keyup TAB').phase('up').frames(30)
        add('player.tab-%s.%s' % (what or 'empty', pg), r, [(None, dict(roundtrip=(5, 'last'), still=(what != 'torch')))])

for pg, what in (('R5', None), ('R5', 'compass'), ('CE7', None)):
    r = player('p', pg, what)
    r.phase('before').frames(6)
    r.act('key SHIFT').phase('strip-open').frames(12)
    r.act('key SHIFT').phase('strip-closed').frames(14)
    r.act('overlay move %d,%d' % (W // 2, H - 3)).phase('dock').frames(12)
    r.act('overlay move %d,%d' % (W // 2, H // 2)).phase('away').frames(14)
    add('player.strip-%s.%s' % (what or 'empty', pg), r, [(None, dict(roundtrip=(5, 'last')))])

for pg, what in (('R5', None), ('R5', 'compass'), ('CE7', None)):
    r = player('p', pg, what)
    r.phase('before').frames(6)
    r.act('key ESC').phase('menu').frames(12)
    r.act('key ESC').phase('after').frames(16)
    add('player.menu-%s.%s' % (what or 'empty', pg), r, [(None, dict(expect_geometry=True, expect_palette=True, roundtrip=(5, 'last')))])

for t, tag, what in ((18000, 'dawn', 'compass'), (64000, 'dusk', None)):       # the daylight palette moves in 12 timed steps: the cleaned texture follows band by band
    if True:
        add('player.%s-%s.R5' % (tag, what or 'empty'), player('p', 'R5', what, start='game settime %d' % t).phase('idle').frames(150), [(None, dict(still=True))])

# Caps Lock: the environment screen is laid over the page and covers its edges: the wide picture drops to 4:3 for as long as it does and comes back
for pg, what in (('R5', None), ('R5', 'compass')):
    r = player('p', pg, what)
    r.phase('before').frames(6)
    r.act('keydown CAPS').phase('caps').frames(8)
    r.act('keyup CAPS').phase('after').frames(24)
    add('player.caps-%s.%s' % (what or 'empty', pg), r, [(None, dict(expect_geometry=True, expect_palette=True))])

WALKS = [('R4', 'L_R4R5', 'rain'), ('R5', 'L_R5R3', ''), ('S4', 'L_S4S5', ''), ('T1', 'L_T1T2', ''), ('CE7', 'L_CE7CE3', ''), ('BDLIFT_I', 'L_LIFTBD', '')]


def walk(name, page, hs, what=None, start=None, **kw):
    r = player(name, page, what, start=start, **kw)
    r.phase('before').frames(5)
    r.phase('walk').act('overlay pump on', 'clickhs %s' % hs, 'overlay pump off')
    r.frame()
    r.phase('after').frames(40)
    return r


for pg, hs, tag in WALKS:
    add('player.walk-day.%s.%s' % (pg, hs), walk('p', pg, hs, start='game settime 43200'), [('before', dict(still=True)), ('after', dict(still=True, skip_first=3))])
for pg, hs, tag in WALKS[:3]:
    add('player.walk-night.%s.%s' % (pg, hs), walk('p', pg, hs, start='game settime 80000'), [('before', dict(still=True)), ('after', dict(still=True, skip_first=3))])
for pg, hs, tag in WALKS[:2]:
    add('player.walk-compass.%s.%s' % (pg, hs), walk('p', pg, hs, 'compass'), [('before', dict(still=True)), ('after', dict(still=True, skip_first=3))])
add('faithful.walk.R4.L_R4R5', walk('f', 'R4', 'L_R4R5', mode='faithful'), [('before', dict(still=True)), ('after', dict(still=True, skip_first=3))])
add('faithful.walk.R5.L_R5R3', walk('f', 'R5', 'L_R5R3', mode='faithful'), [('before', dict(still=True)), ('after', dict(still=True, skip_first=3))])

# the variants that change the path: no cleaner, 1920 x 1080, sharp text
for pg in ('R5', 'R4'):
    for tag, kw in (('noclean', dict(mode='overlay')), ('1080', dict(w=1920, h=1080)), ('sharp', dict(sharp=True))):
        r = player('p', pg, 'compass', **kw)
        r.phase('before').frames(6)
        r.act('keydown TAB').phase('down').frames(14)
        r.act('keyup TAB').phase('up').frames(24)
        add('player-%s.tab-compass.%s' % (tag, pg), r, [(None, dict(roundtrip=(5, 'last')))])
        r = player('p', pg, 'compass', **kw)
        r.phase('idle').frames(16)
        turn(r, 30, 20)
        add('player-%s.turn-compass.%s' % (tag, pg), r, [('idle', dict(still=True)), (None, dict(turning=True))] if False else [(None, dict(turning=True))])
for tag, kw in (('noclean', dict(mode='overlay')), ('1080', dict(w=1920, h=1080)), ('sharp', dict(sharp=True))):
    add('player-%s.walk-day.R4.L_R4R5' % tag, walk('p', 'R4', 'L_R4R5', start='game settime 43200', **kw), [('before', dict(still=True)), ('after', dict(still=True, skip_first=3))])

if QUICK:
    keep = ('faithful.idle.R', 'faithful.tab-compass.R5', 'player.idle.R5', 'player.hold-compass.R5', 'player.turn-quick-compass.R5', 'player.tab-compass.R5',
            'player.walk-day.R4', 'player.strip-empty.R5', 'player.menu-empty.R5')
    SCN[:] = [s for s in SCN if s.name.startswith(keep)]
    TWIN.clear()
if ONLY:
    SCN[:] = [s for s in SCN if ONLY in s.name]
    names = set(s.name for s in SCN)
    for k in list(TWIN):
        if k not in names or TWIN[k] not in names:
            del TWIN[k]

if LIST:
    for s in SCN:
        print(s.name, s.run.frames_n)
    sys.exit(0)


# ------------------------------------------------------------------------------------------------------------------------------ running and judging
def judge(s, frames):
    out = judge_rules(s, frames)
    if s.expect:
        if any(f.rule in s.expect for f in out):
            out = [f for f in out if f.rule not in s.expect]
        else:
            out.append(visual.Finding('selftest', [], 'the injected fault was not noticed (expected one of %s)' % sorted(s.expect)))
    return out


def judge_rules(s, frames):
    out = []
    for phase, kw in s.checks:
        kw = dict(kw)
        ph = s.run.phases
        lo, hi = (0, len(frames))
        if phase is not None:
            if phase not in ph:
                out.append(visual.Finding('nophase', [], 'phase %s produced no frame' % phase))
                continue
            lo, hi = ph[phase]
        part = frames[lo:hi]
        if len(part) < 3:
            out.append(visual.Finding('noframes', [], 'phase %s: %d frames' % (phase, len(part))))
            continue
        kw['events'] = [e - lo for e in s.run.events if lo <= e < hi]
        rt = kw.get('roundtrip')
        if rt is not None:
            a, b = rt
            kw['roundtrip'] = (a, len(part) - 1 if b == 'last' else b) if a < len(part) else None
        for f in check_frames(part, **kw):
            f.frames = [x + lo for x in f.frames]
            out.append(f)
    return out


def run_one(s):
    t0 = time.time()
    try:
        frames, lines, secs = s.run.execute()
    except Exception as e:      # a crash is a finding
        return s, None, [visual.Finding('crash', [], str(e)[:300])], time.time() - t0
    return s, frames, judge(s, frames), time.time() - t0


def load_verdicts():
    v = {}
    if os.path.exists(VERDICTS):
        for l in open(VERDICTS, errors='replace'):
            if l.startswith('#') or not l.strip():
                continue
            p = l.rstrip('\n').split('\t')
            if len(p) >= 4 and p[0] == 'V':
                v[(p[1], p[2])] = p[3]
    return v


def png(bmp):
    r = subprocess.run(['sips', '-s', 'format', 'png', bmp, '--out', bmp[:-4] + '.png'], capture_output=True)
    if r.returncode == 0:
        os.remove(bmp)
        return bmp[:-4] + '.png'
    return bmp


def save_shots(s, f, tag):
    """re-run the scenario with a picture written at the frames around the finding (the run is deterministic)"""
    if not f.frames:
        return []
    fr = s.frames
    idx = sorted(set(x for x in (f.frames[0], f.frames[0] + 1, f.frames[-1], f.frames[-1] + 1) if 0 <= x < len(fr) and fr[x].ord >= 0))[:5]
    if not idx:
        return []
    shots = {}
    for x in idx:
        shots[fr[x].ord] = os.path.join(SHOTS, '%s_f%03d.bmp' % (tag, x))
    try:
        s.run.execute(shots=shots)
    except Exception:
        return []
    return [png(p) for p in shots.values() if os.path.exists(p)]


def main():
    t0 = time.time()
    verdicts = load_verdicts()
    print('visual: %d scenarios, %d jobs' % (len(SCN), JOBS))
    sys.stdout.flush()
    with ThreadPoolExecutor(JOBS) as ex:
        results = list(ex.map(run_one, SCN))
    frames_total = sum(len(fr) for _, fr, _, _ in results if fr)
    findings = []                      # (scn, finding)
    byname = {s.name: (s, fr) for s, fr, fs, secs in results}
    for s, fr, fs, secs in results:
        s.frames = fr
        for f in fs:
            findings.append((s, f))
        if s.name in TWIN and fr and byname[TWIN[s.name]][1]:
            for f in visual.diverge(fr, byname[TWIN[s.name]][1]):
                findings.append((s, f))
    new, ok, op = [], [], []
    for s, f in findings:
        v = verdicts.get((s.name, f.rule))
        if v is None:
            new.append((s, f))
        elif v.startswith('ok:'):
            ok.append((s, f, v))
        else:
            op.append((s, f))
    # the report
    os.makedirs(OUT, exist_ok=True)
    for fn in os.listdir(SHOTS):
        os.remove(os.path.join(SHOTS, fn))
    worst = []
    with open(os.path.join(OUT, 'findings_visual.txt'), 'w') as fh:
        fh.write('# picture stability: %d scenarios, %d presented frames, %d findings (%d new, %d open, %d explained)\n' % (len(SCN), frames_total, len(findings), len(new), len(op), len(ok)))
        fh.write('# status\tscenario\trule\tframes\tdetail\n\n')
        for status, lst in (('NEW', new), ('OPEN', op), ('OK', ok)):
            for item in lst:
                s, f = item[0], item[1]
                tag = re.sub(r'\W+', '_', s.name) + '_' + f.rule
                shots = []
                if status != 'OK' and len(worst) < 5 and not NOSHOTS and f.rule not in ('crash', 'nophase', 'noframes'):
                    shots = save_shots(s, f, tag)
                    worst.append(tag)
                fh.write('%s\t%s\t%s\tframes %s\n    %s\n' % (status, s.name, f.rule, f.frames[:14], f.detail[:700]))
                if status == 'OK':
                    fh.write('    verdict: %s\n' % item[2])
                else:
                    fh.write('    reproduce: %s\n' % s.run.repro())
                    if shots:
                        fh.write('    frames: %s\n' % ' '.join(os.path.relpath(p, ROOT) for p in shots))
                fh.write('\n')
    secs = time.time() - t0
    fam = {}
    for s, fr, fs, sc in results:
        k = s.name.split('.')[0] + '.' + (s.name.split('.')[1].split('-')[0] if '.' in s.name else '')
        fam[k] = fam.get(k, 0) + sc
    print('cpu seconds by family: ' + ', '.join('%s %.0f' % kv for kv in sorted(fam.items(), key=lambda kv: -kv[1])[:12]))
    for s, f in new:
        print('NEW   %s %s frames %s: %s' % (s.name, f.rule, f.frames[:8], f.detail[:200]))
    for s, f in op:
        print('open  %s %s frames %s' % (s.name, f.rule, f.frames[:8]))
    print('visual: %d scenarios, %d frames, %d findings: %d new, %d open, %d explained; %.0f s (details: build/audit/findings_visual.txt)' % (
        len(SCN), frames_total, len(findings), len(new), len(op), len(ok), secs))
    print('VISUAL FAILED' if new else 'VISUAL OK')
    return 1 if new else 0


if __name__ == '__main__':
    sys.exit(main())
