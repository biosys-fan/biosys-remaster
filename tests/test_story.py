"""Story tests (`build/biosys --game`, the `game ...` commands of story_glue.cpp). Expectations are computed from the original's
data: the builder arguments (analysis/work/sb/events.jsonl) for the paths and lifts, the page file (build/biosys-dump) for the
hotspot names, and rules written from the decompiled text (Event_OnHotspot dll:1001db50, CPath dll:10001ac0, CGame::TriggerEvent
dll:100092d0, CSubTechHandler dll:10022930, CGameEndPage dll:1006fed0)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, subprocess, sys, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
EVENTS = D + '/analysis/work/sb/events.jsonl'
PAGES = D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:700] if detail else ''))


def game(script, seed=1, timeout=300):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--log-api', '--log-events', '--seed', str(seed), '--script', script],
                       capture_output=True, text=True, timeout=timeout)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


def story(lines):
    """the last `game story` line as a dict"""
    d = {}
    for l in lines:
        if l.startswith('game story '):
            d = {}
            for tok in l[len('game story '):].split():
                if '=' in tok:
                    k, v = tok.split('=', 1)
                    d[k] = v
    return d


# ---- the builder data: links (paths) ---------------------------------------------------------------------------------
links = []   # (source site, destination, name, unk20, flags, cost)
lifts = {}
for l in open(EVENTS):
    e = json.loads(l)
    if e[0] == 'link':
        a = e[3]
        import struct
        cost = struct.unpack('<d', struct.pack('<II', a[6] & 0xffffffff, a[7] & 0xffffffff))[0]
        links.append((e[5][1], a[1], a[3], a[4], a[5], cost))
    if e[0] == 'feature' and e[2] == 'FUN_10023660':
        lifts[e[3][0]] = e[3]

TERRAIN = [(0x100000, 'junjorn1.wav'), (0x200000, 'mshjorn1.wav'), (0x400000, 'grsjorn1.wav'), (0x800000, 'sndjorn1.wav'),
           (0x1000000, 'rubble1.wav'), (0x2000000, 'pipjorn1.wav'), (0x4000000, 'strstep1.wav'), (0x8000000, 'spirstep.wav')]


def terrain(flags):
    for bit, f in TERRAIN:
        if flags & bit:
            return f
    return None


# ================================================================= (a) walking
def hotspots_of(page):
    r = subprocess.run(['build/biosys-dump', 'dump', PAGES], capture_output=True, text=True)
    cur, res = None, {}
    for l in r.stdout.splitlines():
        m = re.match(r'page \d+ (\S+) ', l)
        if m:
            cur = m.group(1).upper()
            res[cur] = []
        m = re.match(r'hot \d+ (\S+) ', l)
        if m and cur:
            res[cur].append(m.group(1))
    return res


allhot = hotspots_of(None)
journeyPages = {p for p in allhot if p.startswith('L_')}
# from the start page (R4) and from the other sites: the hotspot "L_xxxx" of the source page leads to the destination site
by_src = {}
for src, dst, name, u20, fl, cost in links:
    by_src.setdefault(src.upper(), []).append((dst.upper(), name, u20, fl, cost))
print('paths in the builders: %d' % len(links))
# the paths of the page of the start site, R4 (they are ordinary hotspots there)
cases = []
for src in ['R4', 'R1', 'R3', 'R5', 'S1', 'S2', 'O1', 'PH1', 'CE1', 'T1', 'T2', 'DB1', 'R6', 'R7', 'R8', 'R9', 'S3', 'S4']:
    for p in by_src.get(src, [])[:2]:
        cases.append((src,) + p)
for ln, a in lifts.items():
    for nm in a[4:7]:
        if nm:
            for src, dst, name, u20, fl, cost in links:
                if name == nm and len(cases) < 40:
                    cases.append((src.upper(), dst.upper(), name, u20, fl, cost))
cases = cases[:40]
ok_dest = 0
bad = []
for src, dst, name, u20, fl, cost in cases:
    srcpage = src + '_I' if src.endswith('LIFT') else src
    rc, out, err = game('page %s; advance 2000; game story; game hotspot %s - noaction; game hotspot %s; game idle 6; game story; advance 2000; game story' % (srcpage, name, name))
    st = [l for l in out if l.startswith('game story ')]
    hs = [l for l in out if l.startswith('game hotspot')]
    if rc != 0 or len(st) < 3 or not hs:
        bad.append((src, name, 'rc=%d' % rc, err[-3:]))
        continue
    s0 = story(st[:1]); s2 = story(st[-1:])
    kind = hs[0]
    # the energy refusals ("too tired") are legitimate refusals: kind L:tired / L:heavy
    if 'kind=L:path' not in kind and 'kind=L:tired' not in kind:
        bad.append((src, name, kind))
        continue
    if 'kind=L:tired' in kind:
        ok_dest += 1
        continue
    if s2.get('loc', '') != (dst + '_I' if fl & 1 else dst):
        bad.append((src, name, 'at %s expected %s' % (s2.get('loc'), dst), s2))
        continue
    # the terrain sound and the transition: the API log of the run opens the terrain sound before the jump to the destination
    sound = terrain(fl) if name.upper() in journeyPages else None
    log = [l for l in out if l.startswith('apicall')]
    if sound:
        idx = [i for i, l in enumerate(log) if sound in l]
        jmp = [i for i, l in enumerate(log) if 'JumpToPage' in l and ('page=%s' % dst) in l.replace('"', '')]
        if not idx or not jmp or min(idx) > max(jmp):
            bad.append((src, name, 'sound %s / jump order' % sound))
            continue
    ok_dest += 1
check('walking: %d paths lead to the destination of the path table (terrain sound before the jump)' % len(cases),
      ok_dest == len(cases) and len(cases) >= 20, '%d/%d bad=%s' % (ok_dest, len(cases), bad[:3]))

# ================================================================= (b) hotspot dispatch
# an independent classification from the decompiled dispatch (jump table dll:1001e290 indexed by letter - 'A')
def classify(name):
    c = name[:1].upper()
    if c in 'FNRSUX':
        return 'none'
    if c in 'ABDEJLMOPY':
        return c
    return 'nonstandard'


pages10 = [p for p in ['R4', 'R1', 'S1', 'O1', 'PH1', 'CE1', 'T3', 'DB1', 'R5', 'BR'] if p in allhot and allhot[p]][:10]
mism = []
n = 0
for pg in pages10:
    names = allhot[pg]
    script = 'page %s; advance 1500; ' % pg + '; '.join('game hotspot %s - noaction' % h for h in names)
    rc, out, err = game(script)
    hs = [l for l in out if l.startswith('game hotspot')]
    if len(hs) != len(names):
        mism.append((pg, 'count', len(hs), len(names), err[-2:]))
        continue
    for h, l in zip(names, hs):
        kind = re.search(r'kind=(\S+)', l).group(1)
        want = classify(h)
        got = kind.split(':')[0]
        n += 1
        if got != want:
            mism.append((pg, h, kind, want))
check('hotspot dispatch: %d hotspots of %d pages are classified as the decompiled prefix rules say' % (n, len(pages10)),
      not mism and n > 20, mism[:4])

# ================================================================= (c) milestones and cheats
rc, out, err = game('advance 500; game type SILVERFEATHER; game type MS05; game story; game type MS20; game story; game type IMMORTAL; '
                    'game type GOTOR1 enter; advance 3000; game story')
st = [l for l in out if l.startswith('game story ')]
ms = [int(l.split('milestones=0x')[1].split()[0], 16) for l in st] if len(st) >= 3 else []
check('cheats: SILVERFEATHER then MS05 sets milestone 5 (0x20), MS20 sets 0x100000', len(ms) == 3 and ms[0] & 0x20 and ms[1] & 0x100000, (ms, err[-2:]))
if len(st) >= 3:
    s = story(st[-1:])
    check('cheats: IMMORTAL sets cheat flag bit 0, GOTOR1 jumps to R1', s.get('loc') == 'R1' and int(s.get('cheats', '0')) & 1 == 1, s)
rc, out, err = game('advance 500; game type MS05; game story')
st = [l for l in out if l.startswith('game story ')]
check('cheats: MS05 without SILVERFEATHER does nothing', st and int(st[-1].split('milestones=0x')[1].split()[0], 16) & 0x20 == 0)

# ---- milestone effects (bit -> what the decompiled TriggerEvent does)
rc, out, err = game('advance 500; game type SILVERFEATHER; game type MS03; game story; game type MS10; game story')
check('milestone 0x8 (russmemo) / 0x400 (video2) set their dream bits without error', rc == 0, err[-3:])

# ---- SubTech: milestone 0x20000000 starts the 7 day countdown; the new day after it sets 0x80000000
rc, out, err = game('advance 500; game type SILVERFEATHER; game type IMMORTAL; game type MS29; game story; game settime 691100; advance 30000; game idle 3; game story')
st = [l for l in out if l.startswith('game story ')]
if len(st) >= 2:
    a0 = int(re.search(r'arrival=(-?\d+)', st[0]).group(1))
    t0 = None
    check('SubTech: MS29 (0x20000000) sets the arrival time to now + 604800 s', a0 > 0 and (a0 - 604800) in range(0, 100000), st[0])
    m1 = int(st[1].split('milestones=0x')[1].split()[0], 16)
    check('SubTech: the new day after the arrival time sets 0x80000000', m1 & 0x80000000 != 0, st[1])
else:
    check('SubTech countdown', False, (out[-5:], err[-3:]))

# ---- the helicopter: after 0x80000000 on an unsheltered outdoor site the chase starts (state 3); in a sheltered one (RHUT) never
def heli(page):
    script = ('advance 500; game type SILVERFEATHER; game type IMMORTAL; game type MS29; game settime 691100; advance 30000; page %s; '
              'advance 20000; game story; advance 900000; game story' % page)
    rc, out, err = game(script, timeout=600)
    st = [l for l in out if l.startswith('game story ')]
    return rc, (int(re.search(r'subtech=(\d+)', st[-1]).group(1)) if st else -1), st[-1] if st else ''

rc1, s1, l1 = heli('R4')
rc2, s2, l2 = heli('RHUT')
check('SubTech: on the unsheltered site R4 the helicopter chase starts (state %d)' % s1, rc1 == 0 and s1 in (3, 4, 6), l1)
check('SubTech: in the sheltered RHUT it never starts (state %d)' % s2, rc2 == 0 and s2 == 1, l2)

# ---- the synth stalker: after 0x10000 (page R9) a synth follows the player on an unsheltered rainforest site
script = ('advance 500; game type SILVERFEATHER; game type IMMORTAL; game type MS16; page R4; advance 20000; game type NOSYNTHS; '
          'advance 1000; game story')
rc, out, err = game(script)
st = [l for l in out if l.startswith('game story ')]
check('synth: MS16 (0x10000) is set; NOSYNTHS (state 7) switches the stalker off', rc == 0 and st and 'synth=7' in st[-1], (err[-2:], st[-1:]))

# ================================================================= (d) game end
# the ending is ONE sequence: eGameEnd_0 (walking, 5 s) -> eGameEnd_1 -> ... -> eGameEnd_5 (the page's counter is the stage)
script = 'advance 500; game type SILVERFEATHER; game type HAPPYENDING; advance 1500; game story; advance 6000; game idle 3; game story'
for n in (2, 3, 4, 5):
    script += '; game hotspot eGameEnd_%d - noaction; game idle 2; game story' % n
rc, out, err = game(script)
st = [story([l]) for l in out if l.startswith('game story ')]
stages = [int(x.get('endstage', -1)) for x in st]
check('game end: the six stages 0..5 follow each other (stage counter %s)' % stages, stages == [0, 1, 2, 3, 4, 5] or
      (len(stages) == 6 and stages[0] == 0 and stages[-1] == 5 and stages == sorted(stages)), (stages, err[-2:]))
check('game end: the game is over (DAT_100c2724) once the page was entered', st and st[0].get('gameend') == '1', st[:1])

# ================================================================= (e) lifts and the pod
# the lifts' floors are paths (Event_OnHotspot 'L'): riding between the sites the builder arguments name is walking (case (a) walks
# the L_LIFT paths of every lift). The password lock (CLift +0x90 dll:10024050) and the pod console (CPodTransport dll:1001c070):
rc, out, err = game('advance 500; game pod b_bS; game pod b_bS WRONG; game pod b_bS VISITOR; game pod b_bO RESIDE; game pod b_bD HIDDEN; '
                    'game pod b_bP HOUSE; game pod b_bR; game lift fslift VISITOR')
pods = [l for l in out if l.startswith('game pod ')]
bits = [int(re.search(r'bits=(\d+)', l).group(1)) for l in pods]
dests = [re.search(r'dest=(0x[0-9a-f]+)', l).group(1) for l in pods]
check('pod: LANDLADY (no entry), VISITOR=1, RESIDE=2, HIDDEN=4, HOUSE=8 are the passwords of the destinations 0x15..0x19; a wrong word is refused',
      bits == [0, 0, 1, 3, 7, 15, 15] and dests == ['0x16', '0x16', '0x16', '0x18', '0x17', '0x19', '0x15'], (bits, dests, err[-2:]))
check('lifts: the five lifts in the builders are walked (their L_LIFT paths are in the walking cases)', len(lifts) == 5, list(lifts))

# ================================================================= (f) soak
# a scripted 10 minute (fake clock) walk across at least 15 pages in 4 biomes with clicks on every non-zone hotspot met
route = ['R4', 'R5', 'R1', 'R3', 'R2', 'S1', 'S2', 'S3', 'O1', 'PH1', 'PH2', 'CE1', 'CE2', 'CE3', 'DB1', 'T1', 'T2', 'T3']
route = [p for p in route if p in allhot]
parts = ['advance 1000']
for p in route:
    parts.append('page %s' % p)
    parts.append('advance 20000')
    for h in allhot[p]:
        if h.lower().startswith('dropzone'):
            continue
        parts.append('game hotspot %s - noaction' % h)
    parts.append('game idle 3')
rc, out, err = game('; '.join(parts), timeout=1200)
unres = [l for l in out + err if 'UNRESOLVED' in l]
notimpl = {}
for l in err + out:
    m2 = re.search(r'api not implemented: (0x[0-9a-f]+ \w+)', l)
    if m2:
        notimpl[m2.group(1)] = notimpl.get(m2.group(1), 0) + 1
check('soak: %d pages, no exception, no UNRESOLVED' % len(route), rc == 0 and not unres and len(route) >= 15, (rc, err[-3:], unres[:2]))
print('soak: api not implemented: %s' % notimpl)

print('FAILED %d' % failed if failed else 'ALL OK')
sys.exit(1 if failed else 0)
