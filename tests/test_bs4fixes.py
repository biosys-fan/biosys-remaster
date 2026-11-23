#!/usr/bin/env python3
"""Terminal fixes (Bs4Fixes) and game-side defects found by the terminal work.

Game side (build/biosys-bs4test --game, the real game logic of NewGame.sav):
  1. CSeal::open / close (dll:100249f0 / 10024ae0) write +0x58 of the other seals: panellock opens and closes every seal of the group
  2. a lung given an unknown mode and switched on no longer crashes (dll:10010a30 reads a NULL target cel); with the fix the mode is not stored
  3. confirmed original behaviour: FEATURE_ACTIVE reads the running flag (dll:10019640), a lung that sends to another lung switches itself off
     when no seal is open (dll:10010a30)
Terminal (Bs4Fixes), each off and on:
  4. healthOceanOrnaments  5. panelFollowsVariant (cfan, lock, sfan)  6. the 86400 clamp of time values (original behaviour, no flag)
  7. the device overlays are built by every item change (original behaviour, no flag)  8. the command line (--realism, --fixes)
  9. the real front end: the fix reaches the terminal (build/biosys --game --script)"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, 'build', 'biosys-bs4test')
MAIN = os.path.join(ROOT, 'build', 'biosys')
TMP = os.path.join(ROOT, 'build', 'test', 'bs4fixes')
os.makedirs(TMP, exist_ok=True)

fails = 0
passes = 0


def check(ok, what, detail=''):
    global fails, passes
    if ok:
        passes += 1
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (': ' + str(detail)[:1500] if detail != '' else ''))


def run(lines, game=False, fixes='', gamefixes=''):
    cmd = [BIN] + (['--game'] if game else [])
    if fixes:
        cmd += ['--fixes', fixes]
    if gamefixes:
        cmd += ['--gamefixes', gamefixes]
    r = subprocess.run(cmd, input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=300)
    return r.returncode, [l for l in r.stdout.splitlines() if l != 'unclip']


def geti(out, feature, pid):
    """value of the last `get FEATURE ID = ok int=N` line"""
    v = None
    for l in out:
        m = re.match(r'get %s %d = (ok|no) int=(-?\d+)' % (feature, pid), l)
        if m:
            v = int(m.group(2))
    return v


def p4(out, n=-1):
    ls = [l for l in out if l.startswith('p4 ')]
    return dict(re.findall(r'(\w+)=(\S*)', ls[n]))


def state(out, n=-1):
    ls = [l for l in out if l.startswith('state ')]
    return dict(re.findall(r'(\w+)=(\S*)', ls[n]))


def ornament_hidden(out, name, n=-1):
    """flags bit 0 (hidden) of the first ornament `name` in the n-th `orn` listing"""
    blocks, cur = [], None
    for l in out:
        if l == 'orn-start':
            cur = []
            blocks.append(cur)
        elif l.startswith('orn ') and cur is not None:
            cur.append(l.split())
    for o in blocks[n]:
        if o[2] == name:
            return int(o[3]) & 1
    return None


# ================================================================================================================ 1. the seals
SEALS = ['ft3seal', 'fsoseal', 'frpseal', 'fspseal', 'fdpseal', 'fopseal']
script = ['load air', 'clickh techlock', 'clickh 18_locks1', 'echo @@ before'] + ['get %s 10' % s for s in SEALS] + \
         ['act 17 0 0 2 0 0 4', 'echo @@ open'] + ['get %s 10' % s for s in SEALS] + ['logs'] + \
         ['act 17 0 0 4 0 0 2', 'echo @@ closed'] + ['get %s 10' % s for s in SEALS] + ['logs']
rc, out = run(script, game=True)


def block(out, name):
    r, cur = [], False
    for l in out:
        if l.startswith('@@ '):
            cur = l == '@@ ' + name
            continue
        if cur:
            r.append(l)
    return r


def seals_in(out, tag):
    b = block(out, tag)
    return [geti(b, s, 10) for s in SEALS]


check(rc == 0 and seals_in(out, 'before') == [0] * 6 and seals_in(out, 'open') == [1] * 6 and seals_in(out, 'closed') == [0] * 6,
      'panellock in the real game: showing the open item opens the seal and the other five seals of its group (CSeal::storeDword 0x58), the closed item closes all',
      (rc, seals_in(out, 'before'), seals_in(out, 'open'), seals_in(out, 'closed'), out[-5:]))
check(not [l for l in out if 'Failed to set' in l], 'panellock: no "Failed to set value of parameter FEATURE_OPEN" is logged', [l for l in out if 'Failed' in l][-3:])

# ================================================================================================================ 2. the lung
LUNG = ['setp fbs2lung 20 25', 'setp fbs2lung 2 1', 'advance 3000', 'get fbs2lung 2', 'get fbs2lung 20', 'echo done']
rc, out = run(['get fbs2lung 20'] + LUNG, game=True)
mode0 = int(re.match(r'get fbs2lung 20 = ok int=(-?\d+)', out[0]).group(1))
check(rc == 0 and out[-1] == 'done' and geti(out[1:], 'fbs2lung', 20) == 25 and geti(out, 'fbs2lung', 2) == 0,
      'lung with an unknown mode (25) switched on: no crash (the original dereferences a NULL target cel), the lung ends its stroke and is off; faithful stores the mode',
      (rc, out[-4:]))
rc, out = run(['get fbs2lung 20'] + LUNG, game=True, gamefixes='lungParamUnknownMode')
check(rc == 0 and out[-1] == 'done' and geti(out[1:], 'fbs2lung', 20) == mode0 and geti(out, 'fbs2lung', 2) == 1,
      'lung with an unknown mode, Fixes::lungParamUnknownMode on: the rejected mode is not stored (%s stays) and the lung runs when switched on' % mode0, (rc, out[-4:]))

# ================================================================================================================ 3. confirmed original behaviour
rc, out = run(['get fdbheater 7', 'setp fdbheater 1 1', 'setp fdbheater 2 1', 'get fdbheater 2', 'get fdbheater 7'], game=True)
check(geti(out, 'fdbheater', 7) == 1 and geti(out, 'fdbheater', 2) == 0,
      'FEATURE_ACTIVE reads the running flag (dll:10019640): an unpowered machine set to ACTIVE = 1 reads 0')
rc, out = run(['setp fbs2lung 1 1', 'setp fbs2lung 20 22', 'setp fbs2lung 2 1', 'get fbs2lung 2'], game=True)
check(geti(out, 'fbs2lung', 2) == 0, 'lung, destination first: ACTIVE = 1 is refused at once with every seal closed (CLung::Update dll:10010a30 switches it off, CFeature::SetParam updates)')
rc, out = run(['setp fbs2lung 1 1', 'setp fbs2lung 2 1', 'setp fbs2lung 20 22', 'get fbs2lung 2'], game=True)
check(geti(out, 'fbs2lung', 2) == 1, 'lung, ACTIVE = 1 first, destination after: it stays on (SetParam 0x14 does not call Update; the next tick would switch it off)')
rc, out = run(['setp ft3seal 10 1', 'setp fbs2lung 1 1', 'setp fbs2lung 20 22', 'setp fbs2lung 2 1', 'get fbs2lung 2'], game=True)
check(geti(out, 'fbs2lung', 2) == 1, 'lung, destination first, a seal open (DAT_100c42cc != 0): ACTIVE = 1 is accepted')

# ================================================================================================================ 4. the health page
ROWS = 's1 s2 s3 s4 s5 s6 s7 s8 s9 o1 ocean db1 db3 db4 db5 db6 db7 db9 ce r1 r2 r3 r4 r5 r6 r7 r8 r9'.split()
for state_id, label, shown, hidden in ((0x7d1, 'dead', 'ocd', ('ocu', 'och')), (0x7d2, 'unhealthy', 'ocu', ('ocd', 'och')), (0x7d3, 'healthy', 'och', ('ocd', 'ocu'))):
    res = {}
    for fx in ('', 'healthOceanOrnaments'):
        s = ['set %s 2000 %d 0' % (r, 0x7d3) for r in ROWS] + ['set ocean 2000 %d 0' % state_id, 'load health', 'echo orn-start', 'orn']
        rc, out = run(s, fixes=fx)
        res[fx] = [ornament_hidden(out, n) for n in (shown,) + hidden]
    check(res[''] == [1, 1, 1] and res['healthOceanOrnaments'] == [0, 1, 1],
          'health page, ocean %s: off nothing of ocd / ocu / och changes (all hidden), on %s is shown and the other two hidden' % (label, shown), res)
# the other zones are untouched by the fix
rc0, o0 = run(['set %s 2000 %d 0' % (r, 0x7d1) for r in ROWS] + ['load health', 'echo orn-start', 'orn'])
rc1, o1 = run(['set %s 2000 %d 0' % (r, 0x7d1) for r in ROWS] + ['load health', 'echo orn-start', 'orn'], fixes='healthOceanOrnaments')
d = [(a, b) for a, b in zip(o0, o1) if a != b]
check(len(d) == 1 and ' ocd ' in d[0][0], 'health page: the fix changes the ocean ornaments only (%d differing lines)' % len(d), d[:3])

# ================================================================================================================ 5. variant tabs
FAN = ['load air', 'clickh techcfan', 'clickh 14_cfans1', 'p4', 'state', 'panelrect', 'hotspots panel']


def tab_click(out, index):
    """the page point of the index-th variant tab (a verb 0x17 hotspot of the panel)"""
    pr = [int(x) for x in [l for l in out if l.startswith('panelrect')][-1].split()[1:]]
    hs = [l for l in out if l.startswith('hot ') and 'verb=17' in l]
    m = re.match(r'hot \d+ \S+ (-?\d+) (-?\d+) (-?\d+) (-?\d+)', hs[index])
    x0, y0, x1, y1 = [int(v) for v in m.groups()]
    return pr[0] + (x0 + x1) // 2, pr[1] + (y0 + y1) // 2


rc, out = run(FAN, game=True)
x, y = tab_click(out, 1)
for fx in ('', 'panelFollowsVariant'):
    # fan 2 (fs3mixer) is switched on in the game; the panel was opened for fan 1 (fs9mixer, off)
    rc, out = run(['setp fs3mixer 1 1'] + FAN + ['click %d %d' % (x, y), 'p4', 'state'], game=True, fixes=fx)
    a, b = p4(out, -2), p4(out, -1)
    s = state(out)
    if fx == '':
        check(a['feature'] == 'fs9mixer' and b['feature'] == 'fs9mixer' and b['on'] == '0' and s['panelmask'] == '0x00000014' and s['highlight'] == 'hicfans1/14',
              'panelcfan, second variant tab, off: the pictures switch (panel mask 0x14) but the panel stays on fan 1: feature fs9mixer, ON = 0, highlight hicfans1', (a, b, s))
    else:
        check(a['feature'] == 'fs9mixer' and b['feature'] == 'fs3mixer' and b['on'] == '1' and b['reloadname'] == '14_cfans2' and s['highlight'] == 'hicfans2/14',
              'panelcfan, second variant tab, on: the panel follows the picture: feature fs3mixer, its ON = 1, highlight hicfans2, reload name 14_cfans2', (a, b, s))
# a state button does not re-initialise the panel (fix on): feature stays, the event reaches the game
rc, out = run(FAN + ['act 17 0 0 2 0 0 4', 'get fs9mixer 1', 'p4'], game=True, fixes='panelFollowsVariant')
check(p4(out)['feature'] == 'fs9mixer' and geti(out, 'fs9mixer', 1) == 1, 'panelcfan with the fix on: a state button (item 1) still only sends FEATURE_ON = 1 to the same fan', out[-3:])
# panellock: the variant tab moves the panel to another seal, and the item buttons then act on that seal
LOCK = ['load air', 'clickh techlock', 'clickh 18_locks1', 'p4', 'panelrect', 'hotspots panel']
rc, out = run(LOCK, game=True)
x, y = tab_click(out, 1)
for fx in ('', 'panelFollowsVariant'):
    rc, out = run(LOCK + ['click %d %d' % (x, y), 'p4', 'state'], game=True, fixes=fx)
    b = p4(out, -1)
    check(b['feature'] == ('fspseal' if fx == '' else 'fsoseal'),
          'panellock, second variant tab, %s: the panel works on %s' % ('off' if fx == '' else 'on', 'the first seal (fspseal)' if fx == '' else 'the seal shown (fsoseal)'), b)
# panelsfan (time-limit slider and air-flow bar): the readouts follow
SFAN = ['set fs1fan 7 0 0', 'set fs4fan 7 0 0', 'set fs1fan 5 3600 0', 'set fs4fan 5 7200 0', 'set fs1fan 2 0 0', 'set fs4fan 2 0 0',
        'set fs1fan 80 0 2.0', 'set fs4fan 80 0 8.0', 'load air', 'clickh techsfan', 'clickh 2_soils1', 'p4', 'panelrect', 'hotspots panel']
rc, out = run(SFAN)
x, y = tab_click(out, 1)
for fx in ('', 'panelFollowsVariant'):
    rc, out = run(SFAN + ['click %d %d' % (x, y), 'p4'], fixes=fx)
    b = p4(out, -1)
    want = ('1:0', 'fs1fan') if fx == '' else ('2:0', 'fs4fan')
    check(b['feature'] == want[1] and b['time'] == want[0],
          'panelsfan, second variant tab, %s: feature %s and the time-limit readout %s' % ('off' if fx == '' else 'on', want[1], want[0]), b)

# ================================================================================================================ 6. the 86400 clamp
rc, out = run(['set fs1fan 7 0 0', 'set fs1fan 5 100000 0', 'set fs1fan 2 0 0', 'set fs1fan 80 0 5.0', 'load air', 'clickh techsfan', 'clickh 2_soils1', 'p4', 'logs'])
check(p4(out)['time'] == '24:0' and any('FEATURE_TIME_LIMIT' in l and 'Value = 100000' in l for l in out),
      'panelsfan: a time limit above 86400 is logged with its value and clamped to 24:00 (exe:00422590; the decompile hides the clamp store, the disassembly shows it)')
ALG = ['set fbo3algae 7 0 0', 'set fbo3algae 1 0 0', 'set fbo3algae 3 90000 0', 'set fbo3algae 6 100000 0', 'set fbo3algae 2 0 0', 'load watpur', 'panel panelalgs', 'p4', 'logs']
rc, out = run(ALG)
check('sched=24:00-24:00' in [t for t in out if t.startswith('p4')][-1] and any('FEATURE_DAILY_TIME_LIMIT' in l and 'Value = 100000' in l for l in out) and
      any('FEATURE_TIME_ACTIVATE' in l and 'Value = 90000' in l for l in out),
      'panelalgs (group B): TIME_ACTIVATE and DAILY_TIME_LIMIT above 86400 are logged and clamped (schedule 24:00 - 24:00)')

# ================================================================================================================ 7. overlays
rc, out = run(['load air', 'p3', 'clickh techah', 'p3', 'overlays', 'clickh techoxy', 'p3'], game=True)
p3 = [dict(re.findall(r'(\w+)=(\S*)', l)) for l in out if l.startswith('p3 ')]
check(rc == 0 and p3[0]['dev'] == '-1' and p3[1]['dev'] not in ('-1', '') and p3[2]['dev'] != p3[1]['dev'],
      'device overlays: the row of the table is chosen by every item change of the page (a click on the tab), not only after a panel closed or a game was loaded (no flag needed)', [x['dev'] for x in p3])

# ================================================================================================================ 8. the command line
def front(args):
    r = subprocess.run([MAIN, '--game', '--skip-intro', '--mute', '--screenshot', os.path.join(TMP, 'x.bmp')] + args, capture_output=True, text=True, timeout=300)
    return r.returncode, r.stderr


rc, err = front([])
check(rc == 0 and 'terminal fixes' not in err, 'command line: faithful (default) has no terminal fixes', err[-200:])
rc, err = front(['--realism', 'corrected'])
check(rc == 0 and 'terminal fixes=healthOceanOrnaments,panelFollowsVariant' in err, '--realism corrected switches every terminal fix on', err[-200:])
rc, err = front(['--realism', 'closedloop'])
check(rc == 0 and 'terminal fixes=healthOceanOrnaments,panelFollowsVariant' in err, '--realism closedloop switches every terminal fix on', err[-200:])
rc, err = front(['--realism', 'corrected', '--fixes', 'faithful'])
check(rc == 0 and 'terminal fixes' not in err or 'terminal fixes=faithful' in err, '--fixes faithful replaces the terminal fixes of the realism level too', err[-200:])
rc, err = front(['--fixes', 'panelFollowsVariant'])
check(rc == 0 and 'fixes=faithful;' in err and 'terminal fixes=panelFollowsVariant' in err, '--fixes with a terminal flag only: that flag, no fix of the game logic', err[-200:])
rc, err = front(['--fixes', 'lungParamUnknownMode,healthOceanOrnaments'])
check(rc == 0 and 'fixes=lungParamUnknownMode;' in err and 'terminal fixes=healthOceanOrnaments' in err, '--fixes with a game flag and a terminal flag: each goes where it belongs', err[-200:])
rc, err = front(['--fixes', 'restored,-panelFollowsVariant'])
check(rc == 0 and 'terminal fixes=healthOceanOrnaments' in err and 'panelFollowsVariant' not in err.split('terminal fixes=')[1], '--fixes restored,-flag: the preset covers the terminal, -flag removes one', err[-200:])
rc, err = front(['--fixes', 'noSuchFix'])
check(rc != 0 and 'noSuchFix' in err, '--fixes with an unknown name is refused', (rc, err[-200:]))

# ================================================================================================================ 9. the real front end
def pages_hot(out):
    pg, h = None, {}
    for l in out:
        m = re.match(r'@@page (\d+)', l)
        if m:
            pg = int(m.group(1))
            h[pg] = []
            continue
        m = re.match(r'hot \d+ (\S+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) flags=(\d+) verb=([0-9a-f]+) arg0=(-?\d+)', l)
        if m and pg is not None:
            h[pg].append(dict(name=m.group(1), r=[int(m.group(i)) for i in range(2, 6)], flags=int(m.group(6)), verb=int(m.group(7), 16), arg=int(m.group(8))))
    return h


try:
    # a path of three main-page clicks from menuscreen to air (page 26): breadth-first over the verb 0x12 hotspots of every page
    rc, out = run(sum([['echo @@page %d' % n, 'load %d' % n, 'hotspots'] for n in range(1, 46)], []))
    H = pages_hot(out)
    rc, o3 = run(['load menuscreen', 'state'])
    cur = int(state(o3)['no'])
    paths, queue = {cur: []}, [cur]
    while queue:
        n = queue.pop(0)
        for h in H.get(n, []):
            if h['verb'] == 0x12 and not (h['flags'] & 1) and h['arg'] not in paths and 1 <= h['arg'] <= 45:
                paths[h['arg']] = paths[n] + [((h['r'][0] + h['r'][2]) // 2, (h['r'][1] + h['r'][3]) // 2)]
                queue.append(h['arg'])
    rc, o4 = run(['load air', 'state'])
    air = int(state(o4)['no'])
    path = paths[air]
    for realism, want in (('faithful', 'fs9mixer'), ('corrected', 'fs3mixer')):
        sv = os.path.join(TMP, 'save')
        os.makedirs(sv, exist_ok=True)
        enter = ['advance 500', 'page NbrBIOS4', 'advance 500', 'clickhs BUTTON', 'advance 600', 'type RUSSELL\\n', 'advance 100']
        s = ['bs4trace on'] + enter + sum([['click %d,%d' % p, 'advance 50'] for p in path], []) + \
            ['bs4hs techcfan', 'advance 50', 'bs4hs 14_cfans1', 'advance 50', 'dump bs4']
        # the second variant tab: the verb 0x17 hotspot at panel (36,37)-(52,45), the panel at (442,72)
        s += ['click %d,%d' % (442 + 44, 72 + 41), 'advance 50', 'dump bs4']
        r = subprocess.run([MAIN, '--game', '--skip-intro', '--mute', '--seed', '1', '--realism', realism, '--save-dir', sv, '--script', '; '.join(s)],
                           capture_output=True, text=True, timeout=600)
        feats = re.findall(r"bs4 panelfeature '(\S*)' highlight '(\S*)'", r.stdout)
        check(len(feats) == 2 and feats[0][0] == 'fs9mixer' and feats[1][0] == want,
              'the real front end (build/biosys --game --realism %s): panelcfan opened by clicks from the terminal entry, then the second variant tab: feature %s' % (realism, want), (feats, r.stdout[-300:]))
except Exception as e:  # noqa: BLE001
    check(False, 'the real front end run', repr(e))

print('%d passed, %d failed' % (passes, fails))
print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
