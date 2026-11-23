"""Behaviour tests of the player model and the objects (build/biosys-selftest, build/biosys --game).
1. every tests/self_scripts/*.txt gives the same dump in C++ and in tests/self_oracle.py (tests/self_compare.py)
2. property checks on the dumps: stats stay in their clamps; each cause of death a script is meant to reach is reached
3. symptom string ids resolve to texts (and the string ids used by the code: build/biosys-selftext_check)
4. object event sequences through the real dispatcher of the game shell (build/biosys --game --script)"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import glob, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
SAV = (_DEV + '/setup-cd/installed/DATA1/NewGame.sav')
ST = 'build/biosys-selftest'
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:500]))
    if not ok:
        failed += 1


def blocks(text):
    out, cur = [], None
    for ln in text.splitlines():
        if ln.startswith('== '):
            cur = {}
            out.append(cur)
        elif cur is not None and ' = ' in ln:
            k, v = ln.split(' = ', 1)
            cur[k] = v
    return out


def run(script):
    r = subprocess.run([ST, 'run', SAV, script], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


# ---- 1. C++ against the oracle
scripts = sorted(glob.glob('tests/self_scripts/*.txt'))
for s in scripts:
    r = subprocess.run([sys.executable, '-I', 'tests/self_compare.py', ST, SAV, s], capture_output=True, text=True)
    check('oracle ' + os.path.basename(s), r.returncode == 0, r.stdout + r.stderr)

# ---- 2. clamps in every block of every script; causes of death
DEATHS = {   # script -> cause that must be in m_wCauseOfDeath at the end (the decompiled code raises it, see the report)
    'cold.txt': 1, 'hot.txt': 13, 'starve.txt': 12, 'thirst.txt': 12, 'adrenaline2.txt': 15,
    'death_spider.txt': 17, 'death_fumes.txt': 7, 'death_synth.txt': 8, 'death_protein.txt': 11, 'death_nutrition.txt': 4,
    'death_oxygen.txt': 18, 'death_emetic.txt': 16, 'death_malaria.txt': 2, 'death_prussic.txt': 5, 'death_dioscorin.txt': 14,
}
for f in sorted(glob.glob('tests/self_scripts/death_*.txt')):
    m = re.search(r'^# expect-cause (\d+)', open(f).read(), re.M)
    if m:
        DEATHS[os.path.basename(f)] = int(m.group(1))
reached = set()
for s in scripts:
    rc, out, err = run(s)
    if rc:
        check('run ' + os.path.basename(s), False, err)
        continue
    bl = blocks(out)
    bad = []
    for b in bl:
        for k in ('m_dHydration', 'm_dNutrition', 'm_dProtein', 'm_dTemperature', 'm_dEnergy', 'm_dHealth'):
            v = float(b['self.' + k])
            if k == 'm_dHealth' and (b.get('self.m_bDieOnUpdate') == '1' or b.get('global.dead') == '1'):
                continue   # LoseHealth lets health go below 0 until the next Update / Die
            if not (-1e-9 <= v <= 100 + 1e-9):
                bad.append((k, v))
        if float(b['self.m_dHealth']) > 100 + 1e-9:
            bad.append(('health', b['self.m_dHealth']))
    # sleeping may leave health above the clamp only until WakeUp: the dump is taken after it
    check('clamps ' + os.path.basename(s), not bad, bad[:3])
    if bl and bl[-1].get('global.dead') == '1':
        reached.add(int(bl[-1]['self.m_wCauseOfDeath']))
        want = DEATHS.get(os.path.basename(s))
        if want is not None:
            check('death cause ' + os.path.basename(s), int(bl[-1]['self.m_wCauseOfDeath']) == want, bl[-1]['self.m_wCauseOfDeath'])
for name, want in sorted(DEATHS.items()):
    p = 'tests/self_scripts/' + name
    if os.path.exists(p):
        rc, out, err = run(p)
        bl = blocks(out)
        check('dies of %d: %s' % (want, name), bool(bl) and bl[-1].get('global.dead') == '1' and int(bl[-1]['self.m_wCauseOfDeath']) == want)
print('causes of death reached by the scripts: %s' % sorted(reached))

# ---- 3. symptoms
open('build/test/self/sy.txt', 'w').write('symptoms\n') if os.path.isdir('build/test/self') else None
os.makedirs('build/test/self', exist_ok=True)
open('build/test/self/sy.txt', 'w').write('symptoms\n')
rc, out, err = run('build/test/self/sy.txt')
sy = [l.split(None, 3) for l in out.splitlines() if l.startswith('symptom ')]
check('symptom bits listed', len(sy) == 32)
check('every symptom string id resolves', all(p[2] == '0' or (len(p) > 3 and p[3] != 'MISSING') for p in sy), [p for p in sy if len(p) > 3 and p[3] == 'MISSING'])
if os.path.exists('build/biosys-selftext_check'):
    r = subprocess.run(['build/biosys-selftext_check'], capture_output=True, text=True)
    check('string ids used in the code resolve (selftext_check)', r.returncode == 0, r.stdout[-300:])

# ---- 4. object events through BiosGame::onGameObject
def game(script):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--log-events', '--script', script], capture_output=True, text=True)
    return r.stdout.splitlines()

def val(lines, key):
    for l in lines:
        if l.startswith(key + ' = '):
            return l.split(' = ', 1)[1]
    return None

o = game('advance 500; game self m_dWeightCarried; game event r4bf_1 1; game self m_dWeightCarried; game obj r4bf_1; '
         'game event r4bf_1 2; game event r4bf_1 3; game self m_dNutrition; game event r4bf_1 4; game self m_dNutrition; '
         'game self m_dwLastAte; game obj r4bf_1')
w = [l for l in o if l.startswith('self.m_dWeightCarried')]
check('pick up: weight carried grows by the fruit weight', len(w) == 2 and float(w[1].split(' = ')[1]) - float(w[0].split(' = ')[1]) == 10.0, w)
nut = [float(l.split(' = ')[1]) for l in o if l.startswith('self.m_dNutrition = ')]
check('eat the fruit (event 4): nutrition rises', len(nut) == 2 and nut[1] > nut[0], nut)
check('eat the fruit: last ate set', any(l.startswith('self.m_dwLastAte = ') and l.split(' = ')[1] != '1' for l in o), [l for l in o if 'LastAte' in l])
o = game('advance 500; game event goggles 1; game event goggles 8; game self m_pGoggles; game obj goggles; game event goggles 9; game self m_pGoggles')
g = [l for l in o if l.startswith('self.m_pGoggles')]
check('wear the goggles (event 8) then take them off (9)', len(g) == 2 and g[0].endswith('= goggles') and g[1].endswith('= '), g)

# ---- 5. a save made after playing actions round-trips
open('build/test/self/rt.txt', 'w').write('time 28918\nevent r4bf_1 1\nevent r4bf_1 4\nconsume 0x1e\nevent goggles 1\nevent goggles 8\n'
                                          'run 7200 90\nsleep 0 1 1\nroundtrip\n')
rc, out, err = run('build/test/self/rt.txt')
check('save after actions round-trips (repairOriginalLoadBugs)', 'roundtrip ok' in out, out[-200:] + err)

sys.exit(1 if failed else 0)
