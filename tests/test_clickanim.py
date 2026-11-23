"""Click interactions that start an animation, run headless the way the window runs them (tests/test_clickanim.sh).

Every interaction is driven by real input (script commands `click` / `clickp` / `key`, positions computed from the hotspot rectangles of
PAGES.DAT by tests/play_lib.py). The game is asked what it wants (`--log-api`: OpenSpriteAnimation / PlayObjectUseAnimation; `--log-events`:
the object animation record the engine chose), then the same input is replayed on the fake display of the window (`window HZ 33`: the Pacer
of src/app/pacing.h on a fake real clock) at 60 and 120 Hz and every presented frame whose content changed is written (`wshots`):

  * the original presentation: the 8-bit frame (main window) and the page surface rectangle of a page-relative sprite (`wregion`) are
    compared pixel by pixel with the frames of the .TAN decoded by the independent tool build/biosys-tandump: first and last frame, the
    order (play mode), how long each frame stays (delay quantised to the engine pass of 33 ms), what remains afterwards, the same on both
    displays;
  * the smooth presentation (`--panorama smooth`, panoramas only): the picture through a persistent SDL smooth session (the window's
    PanoramaLayer with all its incremental texture updates) must equal a fresh upload of the same frame at every present, and must show
    the sprite;
  * the sound that belongs to the animation starts with its first frame (`soundtrace`).
The expectations come from the object file (records), the game's own API calls and the TAN data, not from the picture the engine makes."""
import concurrent.futures as cf, os, re, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clickanim_lib import *
import play_lib

os.chdir(ROOT)
failed = 0
HZS = (60, 120)
SLACK = P_MS + 20          # the sound starts within this of the first frame


def slack(hz):
    return 1000.0 / hz + 6     # how far a frame's time on screen may be from its nominal time: one refresh of the display and a little


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name, flush=True)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:500] if detail else ''), flush=True)


VIEW = (0, VIEW_TOP, 640, VIEW_TOP + 368)


def step_ok(d, delay, hz):
    """a run of equal pictures of n steps lasts between n * delay (minus a refresh) and n * (delay + one pass) (plus a refresh): the next frame is
    drawn at the first engine pass at or after now + delay (spec 2.2), one pass later when the display's phase makes it miss by a hair"""
    for n in range(1, 40):
        if n * max(delay, 1) - slack(hz) <= d <= n * (max(delay, 1) + P_MS) + slack(hz):
            return True
    return False


def norm(f):
    return f.replace('/', '\\').strip('\\').lower()


# ------------------------------------------------------------------------------------------------------------------- the scenarios
class Scn:
    """kind 'rec': a blocking object animation (record of the object file), 'api': a blocking PlayObjectUseAnimation of the game,
    'fx': a looping page-relative sprite of a page effect. `pano`: the page is a projected panorama (smooth presentation applies)."""

    def __init__(self, name, kind, route, setup, action, pano, rec=None, expect=None, cheats=False):
        self.name, self.kind, self.route, self.setup, self.action, self.pano = name, kind, route, setup, action, pano
        self.rec, self.expect, self.cheats = rec, expect, cheats
        self.index = Scn.count
        Scn.count += 1

    count = 0


POWER = ['advance 2000', 'type SILVERFEATHER', 'advance 2800', 'type HUNKYDORY', 'advance 2800', 'game fuses']


def fan(name, pano, hot, button, tan, cheats):
    p = Play(start=pano)
    p.raw(*(POWER if cheats else ['advance 2000', 'game fuses']), 'page %s' % pano, 'advance 1000')
    p.click_hotspot(hot, page=pano, wait=2000)
    p.page = hot.upper()
    p.click_hotspot(button, wait=500)
    a = ['rclick 320,200', 'advance 2500']
    return Scn(name, 'fx', '%s: click %s (the fan machine), press %s, right click back' % (pano, hot, button), list(p.cmds), a, True,
               expect=[dict(file=tan, pagerel=True)])


def scenarios():
    S = []
    # ---- blocking object animations of the object file (events 1 pick up, 2 stash, 5 bar -> hand, 6 put down, 7 use on hotspot)
    p = Play(); p.advance(2000); p.goto('L_R4R5', 'R5')
    q = Play(); q.page = 'R5'; play_lib.click_object(q, 'compass', wait=0)
    S.append(Scn('compass picked up on R5', 'rec', 'R5: click the compass (record 1)', list(p.cmds), q.cmds, True, rec=('compass', 1, '')))
    p2 = Play(); p2.advance(2000); p2.goto('L_R4R5', 'R5'); play_lib.click_object(p2, 'compass', wait=1500)
    S.append(Scn('compass stashed (S) on R5', 'rec', 'R5: key S with the compass in hand (record 2)', list(p2.cmds), ['key s'], True, rec=('compass', 2, '')))
    p3 = Play(); p3.advance(2000); p3.goto('L_R4R5', 'R5'); play_lib.click_object(p3, 'compass', wait=1500); p3.key('s', wait=1500)
    p3.raw('move 320,470', 'advance 800')
    S.append(Scn('compass bar -> hand on R5', 'rec', 'R5: click the compass icon on the objects bar (record 5)', list(p3.cmds), ['click 320,455'], True,
                 rec=('compass', 5, '')))
    p4 = Play(); p4.advance(2000); p4.goto('L_R4R5', 'R5'); play_lib.click_object(p4, 'compass', wait=1500)
    S.append(Scn('compass put down (D) on R5', 'rec', 'R5: key D with the compass in hand (record 6)', list(p4.cmds), ['key d'], True, rec=('compass', 6, '')))
    p5 = Play(); p5.advance(2000); p5.raw('page PH13', 'advance 1000'); p5.page = 'PH13'
    q5 = Play(); q5.page = 'PH13'; play_lib.click_object(q5, 'oxy_cyl', wait=0)
    S.append(Scn('oxygen cylinder picked up on PH13 (with its sound)', 'rec', 'PH13: click the cylinder (record 1, strain1.wav)', list(p5.cmds), q5.cmds,
                 True, rec=('oxy_cyl', 1, 'OXYoff')))
    p6 = Play(); p6.advance(2000); p6.raw('page O1', 'advance 1000'); p6.page = 'O1'
    q6 = Play(); q6.page = 'O1'; play_lib.click_object(q6, 'kukri', wait=0)
    S.append(Scn('kukri picked up on O1', 'rec', 'O1: click the kukri (record 1)', list(p6.cmds), q6.cmds, True, rec=('kukri', 1, '')))
    p7 = Play(); p7.advance(2000); p7.raw('page CE7', 'advance 1000'); p7.page = 'CE7'
    q7 = Play(); q7.page = 'CE7'; play_lib.click_object(q7, 'torch', wait=0)
    S.append(Scn('torch picked up on CE7', 'rec', 'CE7: click the torch (record 1, variation torchoff)', list(p7.cmds), q7.cmds, True, rec=('torch', 1, 'torchoff')))
    # the forekey on the lock of the cabinet (test_play.py): pick it up in the dead biome, use it on yLock of the cabinet, take the goggles
    c = Play(); c.advance(2000); c.raw(*POWER[1:])
    c.advance(2600); c.type('SILVERFEATHER', wait=200); c.advance(2600); c.type('GOTONDB4DEAD\\n', wait=200); c.advance(3000); c.page = 'NDB4DEAD'
    play_lib.click_object(c, 'forekey', wait=1500)
    c.advance(2600); c.type('GOTOCE9\\n', wait=200); c.advance(3000); c.page = 'CE9'
    c.click_hotspot('fce9cab', wait=2000); c.page = 'FCE9CAB_C'
    q8 = Play(); q8.page = 'FCE9CAB_C'; q8.click_hotspot('yLock', wait=0)
    S.append(Scn('forekey used on the cabinet lock', 'rec', 'FCE9CAB_C: click yLock with the forekey in hand (record 7, mode 3, unlock.wav)', list(c.cmds), q8.cmds,
                 False, rec=('forekey', 7, '')))
    c2 = Play(); c2.cmds = list(c.cmds); c2.page = 'FCE9CAB_C'; c2.click_hotspot('yLock', wait=3000); c2.page = 'FCE9CAB_O'
    q9 = Play(); q9.page = 'FCE9CAB_O'; play_lib.click_object(q9, 'goggles', wait=0)
    S.append(Scn('goggles picked up in the cabinet', 'rec', 'FCE9CAB_O: click the goggles (record 1)', list(c2.cmds), q9.cmds, False, rec=('goggles', 1, '')))
    # ---- blocking animation the game asks for (API 0x47)
    d = Play(); d.advance(2000); d.goto('L_R4R5', 'R5'); d.goto('L_R5R3', 'R3'); d.goto('L_R3R1', 'R1')
    dq = Play(); dq.page = 'R1'; dq.click_hotspot('ywater2', wait=0)
    S.append(Scn('drinking at the stream on R1', 'api', 'R1: click ywater2 (API 0x47 water.tan)', list(d.cmds), dq.cmds, True, expect=[dict(file='anims\\obj_use\\water.tan')]))
    # ---- page effects started by a button of a close-up (page-relative looping sprites, seen on the panorama after the right click)
    S.append(fan('fan of the rainforest R3', 'R3', 'FR3FAN', 'bStart', 'anims\\fans\\r3fan.tan', False))
    S.append(fan('fan of the rainforest R8', 'R8', 'FR8FAN', 'bStart', 'anims\\fans\\r8fan.tan', False))
    S.append(fan('fan of the savannah S4', 'S4', 'FS4fan', 'bStart', 'anims\\fans\\s4fan.tan', False))
    S.append(fan('fan of the dead biome DB3', 'DB3', 'fdb3fan', 'bStart', 'anims\\fans\\db3fan.tan', True))
    S.append(fan('fan of the dead biome DB6', 'DB6', 'fdb6fan', 'bstart', 'anims\\fans\\db6fan.tan', True))
    # ---- a button on a panorama itself: the bubble machine of the ocean (CBubbler, hotspot bstart of W3) starts its bubbles in the view
    b = Play(start='W3'); b.advance(2000); b.raw('game fuses', 'page W3', 'advance 1000'); b.page = 'W3'
    bq = Play(start='W3'); bq.page = 'W3'; bq.click_hotspot('bstart', page='W3', wait=0)
    S.append(Scn('bubble machine of the ocean W3', 'fx', 'W3: click bstart on the panorama (CBubbler, bubblesC.tan)', list(b.cmds), bq.cmds + ['scroll 760,250', 'advance 2500'], True,
                 expect=[dict(file='anims\\ocean\\bubblesc.tan', pagerel=True)]))
    # ---- an effect of a close-up page (page-relative on a page that is not a panorama): it starts when the page is shown
    m = Play(start='PH6'); m.advance(2000); m.raw('page PH6', 'advance 1000')
    mq = Play(start='PH6'); mq.page = 'PH6'; mq.click_hotspot('fph6mir_b', page='PH6', wait=0)
    S.append(Scn('mirror close-up of PH6', 'fx', 'PH6: click fph6mir_b (the mirror), the page zooms in and the mirror effect starts', list(m.cmds), mq.cmds + ['advance 4500'], False,
                 expect=[dict(file='anims\\house\\mirror.tan', pagerel=True)]))
    # ---- the kukri used on the liana of R4 (record 7, then the water drops that fall from the cut liana: a page effect of the close-up)
    k = Play(start='O1'); k.advance(2000); k.raw('page O1', 'advance 1000'); k.page = 'O1'
    play_lib.click_object(k, 'kukri', wait=1500)
    k.raw('page R4', 'advance 1000'); k.page = 'R4'
    k.click_hotspot('Ar4li', page='R4', wait=2500); k.page = 'AR4LI'
    kq = Play(start='AR4LI'); kq.page = 'AR4LI'; kq.click_hotspot('Y_kukri', wait=0)
    S.append(Scn('kukri used on the liana of R4', 'rec', 'AR4LI: click Y_kukri with the kukri in hand (record 7, hack.wav)', list(k.cmds), kq.cmds, False, rec=('kukri', 7, '')))
    S.append(Scn('water drops of the cut liana (R4DROP)', 'fx', 'AR4LI: after the cut the liana drips', list(k.cmds), kq.cmds + ['advance 4000'], False,
                 expect=[dict(file='anims\\rforest\\r4drop.tan', pagerel=True)]))
    return S


# ------------------------------------------------------------------------------------------------------------------- one scenario
def expected_specs(sc, pre):
    """what the game / the object file say will be shown: [dict(file, x, y, mode, delay, loop, pagerel, wav)] (the first is the one checked)"""
    specs = []
    if sc.kind == 'rec':
        obj, code, var = sc.rec
        recs = [r for r in object_anim_records().get((obj.lower(), code), []) if r['var'].lower() == var.lower()]
        r = recs[0]
        specs.append(dict(file=r['file'], x=r['x'], y=r['y'], mode=r['mode'], delay=r['delay'], loop=0, pagerel=False, wav=r['wav'], hide=1))
        evs = [l for l in pre if l.startswith('event anim ')]
        specs[0]['event_seen'] = any(l.split()[3].lower() == r['file'].lower() and int(l.split()[2]) == code for l in evs)
        return specs
    opens = sprite_opens(pre, next((i for i, l in enumerate(pre) if l == '@act'), 0))
    for want in sc.expect:
        m = [o for o in opens if norm(o['file']) == norm(want['file'])]
        if not m:
            return []
        o = m[0]
        specs.append(dict(file=o['file'], x=o['x'], y=o['y'], mode=o['mode'], delay=o['delay'], loop=o['loop'], pagerel=bool(o['page']), wav=o['wav']))
    return specs


def run_scn(sc, hz, smooth, pre=None):
    spec = expected_specs(sc, pre)
    if not spec:
        return None, None, spec
    s0 = spec[0]
    tan = Tan(s0['file'])
    region = (s0['x'], s0['y'], tan.w, tan.h) if s0['pagerel'] else None
    prefix = 'sc%02d_%d%s' % (sc.index, hz, 's' if smooth else 'o')
    res = run_window(sc.setup + ['soundtrace on'], ['echo @act'] + sc.action, hz, smooth=smooth, prefix=prefix, region=region,
                     tail=300 if s0['pagerel'] else 400, opts=('--log-api', '--log-events'))
    return res, tan, spec


def label_shots(res, tan, s0, sc_pano=True):
    """canonical frame number shown at each present (None: none of the frames)"""
    labels = []
    for sh in res.shots:
        rows, pal, _ = read_bmp(shot_path(res, sh['n']))
        key = cyan_index(pal)
        if s0['pagerel']:
            reg = read_region(res, sh['n'], tan.w, tan.h)
            labels.append(tan.identify(reg, 0, 0, key, (0, 0, tan.w, tan.h)))
        else:
            labels.append(tan.identify(rows, s0['x'], VIEW_TOP + s0['y'], key, VIEW))
    return labels


def key_of(res):
    return cyan_index(read_bmp(shot_path(res, 0))[1])


def analyse_blocking(sc, res, tan, s0, hz):
    """returns (list of problems, [(frame, t0, t1)] runs, sprite start time, end time)"""
    probs = []
    labels = label_shots(res, tan, s0, sc.pano)
    key = key_of(res)
    seq_all = play_sequence(len(tan.frames), s0['mode'])
    exp = []                                       # expected visible frames per step (None: a frame without a visible pixel)
    for k in seq_all:
        exp.append(tan.canon[k] if tan.visible(tan.canon[k], key) else None)
    runs_ = [r for r in runs(labels)]
    # the presented sequence of frames between the first and the last frame of the sprite
    seen = [r[0] for r in runs_]
    while seen and seen[0] is None:
        seen = seen[1:]
        runs_ = runs_[1:]
    while seen and seen[-1] is None:
        seen = seen[:-1]
        runs_ = runs_[:-1]
    collapsed = [v for i, v in enumerate(exp) if i == 0 or v != exp[i - 1]]
    while collapsed and collapsed[0] is None:
        collapsed = collapsed[1:]
    while collapsed and collapsed[-1] is None:
        collapsed = collapsed[:-1]
    if seen != collapsed:
        probs.append('frames shown %s, expected %s' % (seen[:14], collapsed[:14]))
    # what remains afterwards: the last presents show no frame of the sprite
    tail = labels[-1] if labels else 0
    if tail is not None:
        probs.append('the last present still shows frame %s' % tail)
    return probs, labels, runs_


def run_times(res, labels, runs_):
    """[(label, first present time, time of the first present of a different label)]"""
    out = []
    for lab, a, b in runs_:
        t0 = res.shots[a]['t']
        t1 = res.shots[b + 1]['t'] if b + 1 < len(res.shots) else None
        out.append((lab, t0, t1))
    return out


def blocking_total(res, labels, runs_):
    t = run_times(res, labels, runs_)
    return t[0][1], t[-1][2]


def sound_check(res, s0, first_t):
    """the sound of the animation starts at its first frame"""
    want = norm(s0['wav']) if s0.get('wav') else ''
    plays = [(int(l.split('t=')[1].split()[0]), norm(l.split('file=')[1].rsplit(' restart=', 1)[0])) for l in res.lines if l.startswith('soundplay ')]
    if not want:
        return True, 'no wav'
    hit = [t for t, f in plays if f.endswith(want.split('\\')[-1])]
    if not hit:
        return False, 'no start of %s in %s' % (want, plays[:5])
    return (first_t - SLACK <= hit[0] <= first_t + 2), 'sound at %s, first frame at %s' % (hit[0], first_t)


def check_blocking(sc, hz, smooth, res, tan, s0):
    tag = '%s: %d Hz %s' % (sc.name, hz, 'smooth' if smooth else 'original')
    check(tag + ': ran (no error)', not res.err and len(res.shots) > 3, res.err)
    probs, labels, runs_ = analyse_blocking(sc, res, tan, s0, hz)
    check(tag + ': every frame of the .TAN (%d, mode %d) in order, nothing left afterwards' % (len(tan.frames), s0['mode']), not probs, probs)
    if runs_:
        times = run_times(res, labels, runs_)
        key = key_of(res)
        steps = [tan.canon[k] for k in play_sequence(len(tan.frames), s0['mode'])]
        # every step lasts the delay rounded up to whole passes; a run of equal pictures lasts a multiple of it
        nom = nominal_ms(s0['delay'])
        bad = []
        for (lab, t0, t1), r in zip(times[1:-1], runs_[1:-1]):
            if t1 is not None and not step_ok(t1 - t0, s0['delay'], hz):
                bad.append((lab, t1 - t0))
        check(tag + ': each frame stays its delay (%d ms; at most one pass %d ms more)' % (s0['delay'], P_MS), not bad, bad[:6])
        first_t = times[0][1]
        check(tag + ': the sound of the record starts with the first frame (%s)' % (sound_check(res, s0, first_t)[1]), sound_check(res, s0, first_t)[0],
              sound_check(res, s0, first_t)[1])
        total = (times[-1][2] or times[-1][1]) - times[0][1]
        return dict(total=total, runs=[(l, b - a if b else None) for l, a, b in times], steps=len(steps))
    return None


def check_pagerel(sc, hz, smooth, res, tan, s0):
    tag = '%s: %d Hz %s' % (sc.name, hz, 'smooth' if smooth else 'original')
    check(tag + ': ran (no error)', not res.err and len(res.shots) > 3, res.err)
    labels = label_shots(res, tan, s0, sc.pano)
    key = key_of(res)
    vis = [k for k in range(len(tan.frames)) if tan.canon[k] == k and tan.visible(k, key)]
    rs = [r for r in runs(labels) if r[0] is not None]
    seq = [r[0] for r in rs]
    # looping: 0 1 2 0 1 2 ... (play mode 1), from the first frame
    exp = [tan.canon[k] for k in play_sequence(len(tan.frames), s0['mode'])]
    exp = [v for i, v in enumerate(exp) if i == 0 or v != exp[i - 1]]
    cyc = len(exp)
    okseq = len(seq) >= cyc + 2 and seq[0] == exp[0] and all(seq[i] == exp[i % cyc] for i in range(len(seq)))
    check(tag + ': the .TAN (%d frames) loops in order from its first frame (%d changes in view)' % (len(tan.frames), len(seq)), okseq, seq[:12])
    times = []
    for r in rs:
        t0 = res.shots[r[1]]['t']
        t1 = res.shots[r[2] + 1]['t'] if r[2] + 1 < len(res.shots) else None
        times.append((r[0], t0, t1))
    nom = nominal_ms(s0['delay'])
    bad = []
    for (l, t0, t1) in times[1:-1]:           # a run of equal pictures (a TAN may repeat a frame) lasts several steps
        if t1 is not None and not step_ok(t1 - t0, s0['delay'], hz):
            bad.append((l, t1 - t0))
    check(tag + ': every frame stays its delay (%d ms; at most one pass %d ms more)' % (s0['delay'], P_MS), not bad, bad[:6])
    steps_ms = [(t1 - t0) / max(1, round((t1 - t0) / nom)) for l, t0, t1 in times[1:-1] if t1 is not None]
    return dict(steps_ms=steps_ms, n=len(seq))


def smooth_checks(sc, hz, res, tan, s0, labels=None):
    tag = '%s: %d Hz smooth' % (sc.name, hz)
    same = [sh['same'] for sh in res.shots if sh['active']]
    check(tag + ': the persistent smooth picture equals a fresh upload at every one of %d presents' % len(same), same and all(v == '1' for v in same),
          [i for i, v in enumerate(same) if v != '1'][:5])
    if s0['pagerel']:
        ups = [sh['uploads'] for sh in res.shots if sh['uploads'] is not None]
        check(tag + ': the page texture is refreshed with the animation (uploads %s -> %s for %d frames shown)' % (ups[0], ups[-1], len(set(labels or []))),
              ups and ups[-1] - ups[0] >= min(3, len(set(labels or [])) - 1), ups[:3])
        # the sprite shows in the smooth picture: the rows of the sprite differ between presents of different frames
        scale = 2
        diffs = 0
        prev = None
        for sh, lab in zip(res.shots, labels):
            if lab is None or not sh['active']:
                continue
            rows = read_bmp(shot_path(res, sh['n'], 's'))[0]
            if prev is not None and prev[0] != lab:
                d = sum(1 for y in range(VIEW_TOP * scale, (VIEW_TOP + 368) * scale) if rows[y] != prev[1][y])
                diffs += 1 if d else 0
            prev = (lab, rows)
        check(tag + ': a change of frame changes the smooth picture (%d of the changes)' % diffs, diffs >= 3, diffs)
    else:
        # blocking sprite over the panorama: its pixels are drawn opaque over the smooth view at the output scale
        key = key_of(res)
        ok = n = 0
        for sh, lab in zip(res.shots, labels):
            if lab is None or not sh['active']:
                continue
            rows8, pal, _ = read_bmp(shot_path(res, sh['n']))
            rows = read_bmp(shot_path(res, sh['n'], 's'))[0]
            hits = tot = 0
            for j, i, b in tan.runs_of(lab, key):
                y = (VIEW_TOP + s0['y'] + j)
                if y >= VIEW_TOP + 368 or y < VIEW_TOP:
                    continue
                for dx, v in enumerate(b):
                    x = s0['x'] + i + dx
                    if x >= 640:
                        continue
                    r, g, bl = pal[v]
                    px = rows[2 * y][6 * x: 6 * x + 3]
                    tot += 1
                    hits += px == bytes((bl, g, r))
            n += 1
            ok += tot > 0 and hits >= 0.9 * tot
        check(tag + ': the sprite is drawn opaque over the smooth view (>= 90 %% of its pixels exact, in %d/%d presents)' % (ok, n), n and ok == n, (ok, n))


def check_held(hz):
    """held-object animation (spec 1.6.1): the goggles in the hand show their 4 holding images (10, 11, 12, 11) one after the other, each
    for the record's delay (250 ms, one frame per engine pass), the same on both displays; read from the presented frames"""
    c = Play(); c.advance(2000); c.raw(*POWER[1:])
    c.advance(2600); c.type('SILVERFEATHER', wait=200); c.advance(2600); c.type('GOTONDB4DEAD\\n', wait=200); c.advance(3000); c.page = 'NDB4DEAD'
    play_lib.click_object(c, 'forekey', wait=1500)
    c.advance(2600); c.type('GOTOCE9\\n', wait=200); c.advance(3000); c.page = 'CE9'
    c.click_hotspot('fce9cab', wait=2000); c.page = 'FCE9CAB_C'
    c.click_hotspot('yLock', wait=3000); c.page = 'FCE9CAB_O'
    q = Play(); q.page = 'FCE9CAB_O'; play_lib.click_object(q, 'goggles', wait=0)
    res = run_window(c.cmds, q.cmds + ['advance 4200'], hz, prefix='held%d' % hz, opts=())
    tag = 'goggles held in the hand: %d Hz' % hz
    check(tag + ': ran', not res.err and len(res.shots) > 5, res.err)
    rows_of = lambda n: read_bmp(shot_path(res, n))[0]
    hashes = []
    for sh in res.shots:
        rows = rows_of(sh['n'])
        hashes.append(hash(b''.join(rows[VIEW_TOP + 165 + j][380:620] for j in range(203))))
    t0 = res.shots[0]['t'] + 1000                     # the pick-up animation (blocking) is over
    pairs = [(sh['t'], h) for sh, h in zip(res.shots, hashes) if sh['t'] >= t0]
    rs = runs([h for t, h in pairs])
    states = [r[0] for r in rs]
    times = [(pairs[r[1]][0], pairs[r[2] + 1][0] if r[2] + 1 < len(pairs) else None) for r in rs]
    distinct = len(set(states))
    check(tag + ': three different pictures, in the order A B C B A B C B (%d changes)' % len(states), distinct == 3 and len(states) >= 9 and
          all(states[i] == states[i + 4] for i in range(len(states) - 4)), [states.index(x) for x in states][:12])
    durs = [b - a for a, b in times[1:-1] if b is not None]
    check(tag + ': each picture stays 250 ms (one frame per pass: %s)' % durs[:6], durs and all(250 - slack(hz) <= d <= 250 + P_MS + slack(hz) for d in durs), durs[:8])
    return sum(durs[:8])


def all_records(hz, combos, batch_id):
    """every distinct animation of the object file (API 0x47 plays them with the same blocking loop as the object events): frames in order
    at the record's position, the delay, nothing left; returns {combo: problem or None}"""
    cmds = ['page T1', 'advance 1500', 'window %d %d' % (hz, P_MS), 'advance 200']
    for i, c in enumerate(combos):
        f, x, y, mode, delay = c
        cmds += ['echo @c%d' % i, 'wshots %s/rec%d_%d_%d_' % (T, batch_id, hz, i), 'useanim %s - %d %d %d %d 0 0' % (f.replace('\\', '/'), x, y, mode, delay),
                 'advance 250', 'wshots off']
    r = subprocess.run([ROOT + '/build/biosys', '--mute', '--no-objects', '--script', '; '.join(cmds)], capture_output=True, text=True,
                       cwd=ROOT, timeout=900)
    shots = {}
    cur = None
    for l in r.stdout.splitlines():
        if l.startswith('@c'):
            cur = int(l[2:])
            shots[cur] = []
        elif l.startswith('wshot ') and cur is not None:
            d = dict(m.groups() for m in re.finditer(r'(\w+)=(\S+)', l))
            shots[cur].append((int(d['t']), int(d['n'])))
    out = {}
    for i, c in enumerate(combos):
        f, x, y, mode, delay = c
        try:
            tan = Tan(f)
        except RuntimeError as e:
            out[c] = 'cannot decode: %s' % str(e)[:60]
            continue
        sh = shots.get(i, [])
        pre = '%s/rec%d_%d_%d_' % (T, batch_id, hz, i)
        labels = []
        key = None
        for t, n in sh:
            rows, pal, _ = read_bmp('%s%03d.bmp' % (pre, n))
            key = cyan_index(pal)
            labels.append(tan.identify(rows, x, VIEW_TOP + y, key, VIEW))
        for t, n in sh:
            os.remove('%s%03d.bmp' % (pre, n))
        if key is None:
            out[c] = 'no frames presented'
            continue
        exp = [tan.canon[k] if tan.visible(tan.canon[k], key) else None for k in play_sequence(len(tan.frames), mode)]
        exp = [v for j, v in enumerate(exp) if j == 0 or v != exp[j - 1]]
        while exp and exp[0] is None:
            exp = exp[1:]
        while exp and exp[-1] is None:
            exp = exp[:-1]
        rs = runs(labels)
        while rs and rs[0][0] is None:
            rs = rs[1:]
        while rs and rs[-1][0] is None:
            rs = rs[:-1]
        seen = [q[0] for q in rs]
        probs = []
        if seen != exp:
            probs.append('frames %s want %s' % (seen[:10], exp[:10]))
        if labels and labels[-1] is not None:
            probs.append('frame %s left on the screen' % labels[-1])
        for q in rs[1:-1]:
            d = sh[q[2] + 1][0] - sh[q[1]][0]
            if not step_ok(d, delay, hz):
                probs.append('frame %s stays %d ms' % (q[0], d))
        out[c] = '; '.join(probs) or None
    return out


def check_all_records():
    recs = object_anim_records()
    combos = {}
    for (o, code), lst in recs.items():
        for r in lst:
            if r['file']:
                combos.setdefault((r['file'].lower(), r['x'], r['y'], r['mode'], r['delay']), []).append((o, code))
    keys = []
    for c in sorted(combos):
        try:
            find_time(c[0])
        except RuntimeError:
            continue                  # the file is not in the data (the original checks, too, and plays nothing: rotup.tan, OBJ_USE\\cylup.tan)
        keys.append(c)
    missing = sorted(set(c for c in combos if c not in keys))
    for c in keys:
        Tan(c[0])
    batches = [keys[i::8] for i in range(8)]
    bad = {}
    jobs = [(hz, b, i) for hz in HZS for i, b in enumerate(batches)]
    with cf.ThreadPoolExecutor(4) as ex:
        for (hz, b, i), res in zip(jobs, ex.map(lambda j: all_records(*j), jobs)):
            for c, v in res.items():
                if v:
                    bad[(hz, c)] = v
    check('all %d animations of the object file (%d records; %d files are missing from the data and, as in the original, play nothing): every visible frame '
          'in order at its position, each for its delay, nothing left behind, on a 60 and a 120 Hz display' % (len(keys), sum(len(v) for v in combos.values()), len(missing)),
          not bad, list(bad.items())[:5])


def per_scn(sc):
    out = {}
    pre = None
    # the game's own request (headless, no window) and the record the engine took
    import subprocess
    cmds = sc.setup + ['echo @act', 'soundtrace on'] + sc.action + ['advance 800']
    r = subprocess.run([ROOT + '/build/biosys', '--game', '--mute', '--seed', '1', '--log-api', '--log-events', '--script', '; '.join(cmds)],
                       capture_output=True, text=True, cwd=ROOT, timeout=600)
    pre = r.stdout.splitlines()
    pre = [l for i, l in enumerate(pre)]
    # the marker `echo @act` is printed on its own line
    spec = expected_specs(sc, pre)
    return pre, spec


def main():
    scs = scenarios()
    object_anim_records()          # (filled before the threads start)
    print('--- %d interactions' % len(scs), flush=True)
    pres = {}
    with cf.ThreadPoolExecutor(4) as ex:
        for sc, (pre, spec) in zip(scs, ex.map(per_scn, scs)):
            pres[sc.name] = (pre, spec)
            check('%s -- %s: the game asks for / the object file has %s' % (sc.name, sc.route, spec[0]['file'] if spec else '?'), bool(spec) and (sc.kind != 'rec' or spec[0]['event_seen']),
                  [l for l in pre if l.startswith('event anim')][:3])
    jobs = []
    for sc in scs:
        spec = pres[sc.name][1]
        if not spec:
            continue
        for hz in HZS:
            jobs.append((sc, hz, False))
            if sc.pano:
                jobs.append((sc, hz, True))
    check_all_records()
    held = {}
    with cf.ThreadPoolExecutor(2) as ex:
        for hz, v in zip(HZS, ex.map(check_held, HZS)):
            held[hz] = v
    t0 = time.time()
    for sc in scs:
        spec = pres[sc.name][1]
        if not spec:
            continue
        jobs = [(hz, smooth) for hz in HZS for smooth in ((False, True) if sc.pano else (False,))]
        with cf.ThreadPoolExecutor(4) as ex:
            rs = list(ex.map(lambda j: run_scn(sc, j[0], j[1], pres[sc.name][0]), jobs))
        results = {(sc.name, hz, smooth): r for (hz, smooth), r in zip(jobs, rs)}
        s0 = spec[0]
        info = {}
        for hz in HZS:
            for smooth in (False, True):
                r = results.get((sc.name, hz, smooth))
                if not r:
                    continue
                res, tan, _ = r
                if sc.kind == 'fx':
                    info[(hz, smooth)] = check_pagerel(sc, hz, smooth, res, tan, s0)
                else:
                    info[(hz, smooth)] = check_blocking(sc, hz, smooth, res, tan, s0)
                if smooth:
                    labels = label_shots(res, tan, s0, sc.pano)
                    smooth_checks(sc, hz, res, tan, s0, labels)
        for fn in os.listdir(T):           # the frames of this interaction are not needed any more (a rainy page changes at every present)
            if fn.startswith('sc%02d_' % sc.index) and fn.endswith(('.bmp', '.reg')):
                os.remove(os.path.join(T, fn))
        a, b = info.get((60, False)), info.get((120, False))
        if a and b:
            if sc.kind == 'fx':
                ra, rb = a['steps_ms'], b['steps_ms']
                ma, mb = sum(ra) / max(1, len(ra)), sum(rb) / max(1, len(rb))
                check('%s: the same pace at 60 and 120 Hz (mean time per frame %.1f / %.1f ms)' % (sc.name, ma, mb), ra and rb and abs(ma - mb) <= 8, (ra[:6], rb[:6]))
            else:
                check('%s: the same duration at 60 and 120 Hz (%s / %s ms)' % (sc.name, a['total'], b['total']), abs(a['total'] - b['total']) <= 2 * SLACK, (a, b))


if __name__ == '__main__':
    main()
    print('FAILED: %d' % failed if failed else 'ALL CLICKANIM CHECKS PASSED')
    sys.exit(1 if failed else 0)
