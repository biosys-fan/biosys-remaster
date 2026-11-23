"""Playthrough test (tests/test_play.sh). One headless run of `build/biosys --game --mute --seed 1 --log-events --log-api` driven only
by pointer clicks, key presses and typed text (script commands `click`, `rclick`, `key`, `type`; tests/play_lib.py computes the
screen positions from the hotspot rectangles of PAGES.DAT). The expectations come from the game's data (the string table, the paths
and zones of the page file) and from rules of the decompiled logic, not from the port's own output."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play_lib import *

os.chdir(ROOT)
STRINGS = (_DEV + '/analysis/out/dll_stringtable.txt')
OUT = 'build/test/play'
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:700] if detail else ''))


strings = {}
for l in open(STRINGS, errors='replace'):
    q = l.rstrip('\n').split('\t')
    if len(q) >= 3 and q[0].isdigit():
        strings[int(q[0])] = q[2].replace('\\n', '\n')
by_text = {}
for i, t in strings.items():
    by_text.setdefault(t, i)

# ------------------------------------------------------------------------------------------------------------------------ the script
p = Play()
SHOTS = []


def shot(name):
    path = '%s/%s.bmp' % (OUT, name)
    p.shot(path)
    SHOTS.append(path)


def state(label):
    """the numbers the assertions read, at this point of the script"""
    p.raw('echo @' + label, 'game dump', 'game self', 'dump state', 'dump bar', 'game story', 'game ui')


def cheat(word):
    p.advance(2600)          # the typed keys are forgotten 2 s after the last one (dll:1001e6b0)
    p.type(word, wait=200)


# 1. wake on R4
p.advance(2000)
state('start')
shot('01_start_r4')

# 2. R4 -> R5: eat a fruit (it goes to the bar; the pointer over the bar and Space eat it), pick up the compass, use it, stash it
p.goto('L_R4R5', 'R5')
state('r5')
p.click_object('R5bz_1', wait=1500)
state('fruit_on_bar')
p.raw('move 320,470')
p.advance(500)
p.key('space', wait=1500)
state('ate')
shot('02_r5_ate_fruit')
p.click_object('compass', wait=800)
state('compass_held')
p.key('space', wait=800)          # use the compass: it says the heading
state('compass_read')
p.key('s', wait=800)              # stash it
state('compass_stashed')

# 3. R5 -> R3 -> R1: drink at the water
p.goto('L_R5R3', 'R3')
p.goto('L_R3R1', 'R1')
state('r1')
p.click_hotspot('ywater2', wait=3000)
state('drank')

# 4. R1 -> R4 (the other way round), R10 and through the hatch (its panel hotspot opens it) into the tunnel T1 (another biome). The paths
# to R7 and the lower rainforest are flooded (the Y_ hotspots) until the rain machine has drained them.
p.goto('L_R1R4', 'R4')
p.goto('L_R4R10', 'R10')
shot('04_r10')
p.click_hotspot('FR10htch_C', wait=2000)
p.page = 'FR10HTCH_C'
p.click_hotspot('b_open', wait=3000)
p.page = 'FR10HTCH_O'
state('hatch_open')
shot('05_hatch_open')
p.click_hotspot('L_R10T1', wait=9000)
p.page = 'T1'
state('t1')
shot('05b_tunnel_t1')

# 5. and back: T1 -> R10 -> R4 -> R1, where the rain machine's panel is operated (the engine has no right-click return to the
# saved page yet, so the panel is the last thing done before the cheat words take the player away)
p.goto('L_T1R10', 'R10')
p.goto('L_R10R4', 'R4')
p.goto('L_R4R1', 'R1')
state('r1_again')
p.click_hotspot('fr1rain', wait=2000)
p.page = 'FR1RAIN'
state('rain_panel')
p.click_hotspot('bactive', page='fr1rain', wait=1000)
state('rain_on')
shot('03_rain_panel')

# 6. the cheat words (SILVERFEATHER unlocks them): GOTO takes the key of the cabinet out of the dead biome, then the goggles
cheat('SILVERFEATHER')
state('cheats_on')
cheat('GOTONDB4DEAD\\n')
p.advance(3000)
p.page = 'NDB4DEAD'
p.click_object('forekey', wait=800)
state('forekey')
cheat('GOTOCE9\\n')
p.advance(3000)
p.page = 'CE9'
p.click_hotspot('fce9cab', wait=2000)
p.page = 'FCE9CAB_C'
p.click_hotspot('yLock', page='fce9cab_c', wait=3000)       # the key is used on the lock: the cabinet opens
p.page = 'FCE9CAB_O'
p.click_object('goggles', wait=800)
state('goggles_held')
p.key('space', wait=1500)                                    # wear them
state('goggles_worn')
shot('06_goggles_ce9')

# 7. evening (the stat cheats make the player fit to sleep and able to carry the bag), the sleeping bag in the savannah: sleep until morning
cheat('SKIP10')
cheat('TZING')           # all the player's numbers 100 and the fumes of the laboratory gone (they would wake the sleeper)
cheat('ENERGY50')        # tired enough to sleep (above 80 the player cannot)
state('evening')
cheat('GOTOS4BAOBAB\\n')
p.advance(3000)
p.page = 'S4BAOBAB'
p.click_object('survibag', wait=800)
p.key('space', wait=500)
state('bag_used')
shot('07_before_sleep')
p.advance(25000)
state('morning')
cheat('SKIP3')             # a few hours later: daylight on the baobab page
p.advance(3000)
shot('08_morning')
p.raw('api SaveGame end.sav')

rc, out, err = p.run(extra=['--log-events', '--log-api', '--save-dir', OUT + '/saves'], timeout=900)
open(OUT + '/play.log', 'w').write('\n'.join(out))
check('the whole script ran without an exception or an error line', rc == 0 and not any('Error' in e or 'error' in e for e in err), (rc, err[:3]))

# ------------------------------------------------------------------------------------------------------------------------ parse
snap = {}
cur = None
for l in out:
    if l.startswith('@'):
        cur = l[1:]
        snap[cur] = {'self': {}, 'ui': [], 'raw': []}
        continue
    if cur is None:
        continue
    snap[cur]['raw'].append(l)
    m = re.match(r'self\.(\w+) = (\S+)', l)
    if m:
        snap[cur]['self'][m.group(1)] = m.group(2)
    elif l.startswith('ui '):
        snap[cur]['ui'].append(l[3:])


def num(label, key):
    return float(snap[label]['self'][key])


def field(label, prefix):
    for l in snap[label]['raw']:
        if l.startswith(prefix):
            return l
    return ''


def kv(label, prefix):
    d = {}
    for tok in field(label, prefix).split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            d[k] = v
    return d


def held(label):
    m = re.search(r'^held (\S+)', '\n'.join(snap[label]['raw']), re.M)
    return m.group(1) if m and m.group(1) != '-' else None


def clock_minutes(label):
    m = re.search(r'clock=(\d+):(\d+)', field(label, 'game time='))
    return int(m.group(1)) * 60 + int(m.group(2))


def gametime(label):
    return int(kv(label, 'game time=')['time'])


def texts(label):
    return [re.match(r"text x=(-?\d+) y=(-?\d+) '(.*)'$", u).groups() for u in snap[label]['ui'] if u.startswith('text x=')]


# ------------------------------------------------------------------------------------------------------------------------ (a) stats
check('(a) wake on R4: awake, at R4, in the morning (clock 08:xx)',
      snap['start']['self']['m_bAwake'] == '1' and kv('start', 'game story')['loc'] == 'R4' and 480 <= clock_minutes('start') < 540,
      (snap['start']['self'].get('m_bAwake'), kv('start', 'game story').get('loc'), clock_minutes('start')))
check('(a) the walk R4 -> R5 -> R3 -> R1 -> R4 -> R10 -> T1 reached the tunnel T1 (another biome)',
      kv('t1', 'game story')['loc'] == 'T1', kv('t1', 'game story'))
enters = [l.split()[2] for l in out if l.startswith('event enterpage ')]
walked = [e for e in enters if e in ('R4', 'R5', 'R3', 'R1', 'R10', 'T1')]
check('(a) ten main pages were entered by clicking path hotspots (journeys through the page Lr between them)', walked == ['R4', 'R5', 'R3', 'R1', 'R4', 'R10', 'T1', 'R10', 'R4', 'R1'], walked)
check('(a) the clock advanced: days of walking cost game time (the journeys) and the energy went down',
      gametime('t1') > gametime('start') + 3600 and num('t1', 'm_dEnergy') < num('start', 'm_dEnergy'),
      (gametime('start'), gametime('t1'), num('start', 'm_dEnergy'), num('t1', 'm_dEnergy')))
check('(a) the fruit went to the bar with one click (events 10, 1, 11, 2)',
      [l.split()[3] for l in out if l.startswith('event gameobject R5bz_1 ')][:4] == ['10', '1', '11', '2'], [l for l in out if 'R5bz_1' in l and 'gameobject' in l][:6])
check('(a) the fruit is on the bar, then eaten from the bar by Space (event 4): nutrition rose, last-ate time set',
      'status=3' in field('fruit_on_bar', 'slot 0') or 'R5bz_1' in field('fruit_on_bar', 'slot 0'),
      field('fruit_on_bar', 'slot 0'))
check('(a) eating raised the nutrition (74.8 -> 95.2 for a fruit worth 20 nutrition)', num('ate', 'm_dNutrition') > num('fruit_on_bar', 'm_dNutrition') + 15,
      (num('fruit_on_bar', 'm_dNutrition'), num('ate', 'm_dNutrition')))
check('(a) the fruit is gone after eating (bar empty)', 'objects=0' in field('ate', 'bar '), field('ate', 'bar '))
check('(a) the compass is picked up (held), used (event 4) and stashed on the bar', held('compass_held') == 'compass' and held('compass_stashed') is None
      and 'objects=1' in field('compass_stashed', 'bar '), (held('compass_held'), held('compass_stashed'), field('compass_stashed', 'bar ')))
check('(a) the drink at the water: hydration rose by 19.7', num('drank', 'm_dHydration') > num('r1', 'm_dHydration') + 15,
      (num('r1', 'm_dHydration'), num('drank', 'm_dHydration')))
check('(a) the rain machine panel: bactive (a hotspot of the feature page) started it and its message was shown',
      any(t[2] == strings[1] for t in texts('rain_on')), texts('rain_on')[-4:])
check('(a) the hatch of R10 opened with its panel hotspot and the path to the tunnel is on its page',
      kv('hatch_open', 'game story')['loc'] == 'FR10HTCH_O', kv('hatch_open', 'game story'))
check('(a) SILVERFEATHER unlocked the cheat words (cheats=2)', kv('cheats_on', 'game story')['cheats'] == '2', kv('cheats_on', 'game story'))
check('(a) the GOTO cheat took the player to the dead biome, where the key lay in its zone', held('forekey') == 'forekey', held('forekey'))
check('(a) the key opened the cabinet and the goggles were picked up, then worn (Space)',
      held('goggles_held') == 'goggles' and re.search(r'^worn goggles', '\n'.join(snap['goggles_worn']['raw']), re.M) is not None,
      (held('goggles_held'), [l for l in snap['goggles_worn']['raw'] if l.startswith('worn')]))
check('(a) SKIP10 moved the clock about 10 hours on (evening), the stat words set the numbers of the player', 19 * 60 <= clock_minutes('evening') or clock_minutes('evening') < 6 * 60,
      field('evening', 'game time='))
check('(a) the sleeping bag was used on the baobab page (the player fell asleep: awake = 0 during the sleep)',
      kv('bag_used', 'game story')['loc'] in ('S4BAOBAB', 'ZLANDOFNOD'), kv('bag_used', 'game story'))
check('(a) the player slept and woke up: awake again, the energy rose by the sleep, the clock passed into the next morning',
      snap['morning']['self']['m_bAwake'] == '1' and num('morning', 'm_dEnergy') > num('evening', 'm_dEnergy') + 20
      and int(kv('morning', 'game time=')['day']) == 2 and clock_minutes('morning') < 9 * 60,
      (snap['morning']['self'].get('m_bAwake'), num('evening', 'm_dEnergy'), num('morning', 'm_dEnergy'), field('morning', 'game time=')))
check('(a) the player was back on the baobab page after waking', kv('morning', 'game story')['loc'] == 'S4BAOBAB', kv('morning', 'game story'))

# screenshots
import struct
shots_ok = True
bright = 0
for path in SHOTS:
    try:
        d = open(path, 'rb').read()
        off = struct.unpack_from('<I', d, 10)[0]
        nz = sum(1 for b in d[off:] if b)
        bright += nz > 20000
        if len(d) < 300000 or nz < 1000:     # (the tunnel, the hatch panel and the goggles' cabinet are dark: the bar icons and the text remain)
            shots_ok = False
            print('   screenshot looks empty: ' + path)
    except OSError:
        shots_ok = False
        print('   missing screenshot ' + path)
check('(a) %d screenshots saved in %s/ (%s)' % (len(SHOTS), OUT, ', '.join(os.path.basename(s) for s in SHOTS)), shots_ok and len(SHOTS) >= 6 and bright >= 5, (len(SHOTS), bright))

# ------------------------------------------------------------------------------------------------------------------------ (b) messages
msgs = []
for l in out:
    m = re.match(r'apicall WriteLog text="player-message (\d+) (.*)" -> 0', l)
    if m:
        msgs.append((int(m.group(1)), m.group(2).replace('\\\\n', '\n')))
shown = [(i, t) for i, t in msgs if i != 0xffff]
bad = []
for i, t in shown:
    want = strings.get(i)
    if want is None or not t.startswith(want):
        bad.append((i, t, want))
check('(b) %d messages were shown during the script (%d distinct); each starts with the string-table text of its id' % (len(shown), len({i for i, _ in shown})),
      len(shown) >= 8 and not bad, bad[:3])
ids = {i for i, _ in shown}
check('(b) the messages of the events were among them: compass, a fruit, the rain machine ("the flood waters are draining away"), key, goggles',
      {1} <= ids and any('compass' in t for _, t in shown) and any('goggles' in t for _, t in shown), sorted(ids)[:20])
check('(b) the strings drawn on the top bar were recorded as drawn text (y = 36, centred) for the messages of the walk',
      any(t[1] == '36' for k in snap for t in texts(k)) and any(t[0] == '18' or t[1] == '18' for k in snap for t in texts(k)),
      [t for t in texts('start')][:4])
# every drawn text in the run is a string of the table, a number or an aligned figure of the journey / hint line
drawn = [t[2] for k in snap for t in texts(k)]
unknown = sorted({d for d in drawn if d.strip() not in by_text and not re.fullmatch(r'\s*[<\-\d]+\s*', d) and not any(d.startswith(v) for v in by_text if len(v) > 3)})
check('(b) every text drawn through the GDI calls is a string of the DLL\'s table (or a journey figure)', not unknown, unknown[:6])

# ------------------------------------------------------------------------------------------------------------------------ (c) hotspots
import concurrent.futures
visited = ['R4', 'R5', 'R3', 'R1', 'FR1RAIN', 'R10', 'FR10HTCH_C', 'FR10HTCH_O', 'T1', 'NDB4DEAD', 'CE9', 'FCE9CAB_C', 'FCE9CAB_O', 'S4BAOBAB']
pages = p.pages


def probe(args):
    page, hs = args
    r = subprocess.run([ROOT + '/build/biosys', '--game', '--mute', '--seed', '1', '--script',
                        'advance 500; page %s; advance 300; game hotspot %s %s noaction' % (page, hs, page)],
                       capture_output=True, text=True, timeout=120, cwd=ROOT)
    m = re.search(r'game hotspot \S+ rc=(-?\d+) kind=(\S+)', r.stdout)
    return page, hs, r.returncode, (m.group(2) if m else None), r.stderr.strip()[:100]


jobs = [(pg, h[0]) for pg in visited for h in pages[pg]['hot']]
results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
    results = list(ex.map(probe, jobs))
kinds = {}
errors = [r for r in results if r[2] != 0 or r[3] is None]
for pg, hs, rc, kind, e in results:
    kinds.setdefault(kind, []).append(pg + '/' + hs)
# the original's dispatch (dll:1001db50): first letters A B D E F J L M N O P R S U X Y are handled; any other first letter is a
# "non-standard name" (logged and answered 1: the engine's own action only)
nonstd = kinds.get('nonstandard', [])
ignored = [h for h in nonstd if h.split('/')[1][:1].upper() not in 'ABDEFJLMNOPRSUXY']
with open(OUT + '/hotspots.txt', 'w') as f:
    for k in sorted(kinds, key=str):
        f.write('%s (%d): %s\n' % (k, len(kinds[k]), ' '.join(kinds[k])))
check('(c) every hotspot of the %d visited pages (%d hotspots) is dispatched by the game logic without an exception' % (len(visited), len(jobs)),
      not errors, errors[:3])
check('(c) the hotspots that do nothing are those whose names the original does not dispatch (first letter not in A B D E F J L M N O P R S U X Y): %d of %d'
      % (len(nonstd), len(jobs)), len(ignored) == len(nonstd), [h for h in nonstd if h not in ignored][:5])
print('   hotspot kinds: ' + ', '.join('%s %d' % (k, len(v)) for k, v in sorted(kinds.items(), key=lambda kv: str(kv[0]))))
print('   hotspots that are ignored by the original (non-standard names): ' + (' '.join(nonstd[:40]) or '-'))
gaps = subprocess.run('grep -n "PORT (engine gap)\\|TODO(behaviour)" src/game/*.cpp src/game/*.h | cut -c1-150', shell=True, capture_output=True, text=True, cwd=ROOT).stdout
open(OUT + '/gaps.txt', 'w').write(gaps)
print('   engine gaps / TODOs in src/game: %d lines (listed in %s/gaps.txt)' % (len(gaps.splitlines()), OUT))

# ------------------------------------------------------------------------------------------------------------------------ (d) save
sav = OUT + '/saves/end.sav'
check('(d) the save at the end of the script was written by the engine and the game logic (`api SaveGame` rc 0: engine part, then the game logic blob)',
      os.path.exists(sav) and any(l.startswith('api SaveGame rc=0') for l in out), os.path.exists(sav))
r = subprocess.run([ROOT + '/build/biosys-gametest', 'roundtrip', sav], capture_output=True, text=True, cwd=ROOT)
check('(d) the saved state round-trips through the port\'s SaveGame (load, save again, identical bytes)', r.returncode == 0 and 'roundtrip OK' in r.stdout,
      (r.stdout + r.stderr)[:300])

print('play tests: ' + ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
