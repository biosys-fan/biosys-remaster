"""Live game-logic tests (`build/biosys --game`): the port of BiosGame.dll's first slice. Every expectation comes from
the original data or the decompiled text with independent Python code: the PAL files and the blend arithmetic of
MakeIntermediatePalette (dll:10060020), the builder arguments (analysis/work/sb/events.jsonl) for CanStart, the call order
of the decompiled functions (analysis/scripts/dllpack.py), the saved game through analysis/scripts/biosys_savblob.py."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, subprocess, sys, json, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
SAV = D + '/setup-cd/installed/DATA1/NewGame.sav'
BIODATA = [D + '/setup-cd/installed/DATA1/Biodata', D + '/game-cd/files/BIODATA']
DLLPACK = D + '/analysis/scripts/dllpack.py'
SAVBLOB = D + '/analysis/scripts/biosys_savblob.py'
EVENTS = D + '/analysis/work/sb/events.jsonl'
T = 'build/test/live'
os.makedirs(T, exist_ok=True)
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:600] if detail else ''))


def game(script, *opts, seed=1):
    """run `build/biosys --game --mute --log-api --log-events --script`; returns (rc, stdout lines, stderr lines)"""
    r = subprocess.run(['build/biosys', '--game', '--mute', '--log-api', '--log-events', '--seed', str(seed)] + list(opts) +
                       ['--script', script], capture_output=True, text=True)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


def kv(line):
    """`game dump` line -> dict"""
    d = {}
    for tok in line.split()[1:]:
        if '=' in tok:
            k, v = tok.split('=', 1)
            d[k] = v
    return d


def first(lines, prefix):
    for l in lines:
        if l.startswith(prefix):
            return l
    return None


def find_data(rel):
    cur_roots = BIODATA
    for root in cur_roots:
        p = root
        ok = True
        for part in rel.replace('\\', '/').split('/'):
            try:
                names = os.listdir(p)
            except OSError:
                ok = False
                break
            m = [n for n in names if n.lower() == part.lower()]
            if not m:
                ok = False
                break
            p = os.path.join(p, m[0])
        if ok:
            return p
    raise RuntimeError('missing ' + rel)


def read_pal(rel):
    d = open(find_data(rel), 'rb').read()
    assert len(d) == 0x418 and d[:4] == b'RIFF', rel
    return [tuple(d[0x18 + 4 * i: 0x18 + 4 * i + 4]) for i in range(256)]


def blend(a, b, frac, fixed=0x3e):
    """MakeIntermediatePalette dll:10060020: out = a for the first `fixed`, then a + trunc((b - a) * frac) mod 256"""
    out = []
    for i in range(256):
        if i < fixed:
            out.append(a[i])
        else:
            out.append(tuple((a[i][c] + int((b[i][c] - a[i][c]) * frac)) & 255 for c in range(3)) + (a[i][3],))
    return out


def tint_green(p, avg):
    """CSite::TintPalette dll:1000e470 for a site with flag 0x100: green pixels move towards green by 1 - avg"""
    k = 1.0 - avg
    out = list(p)
    for i in range(0x3e, 256):
        r, g, b, f = p[i]
        if r * 1.2 < g and b * 1.2 < g:
            out[i] = ((r + int((g - r) * k)) & 255, g, b, f)
    return out


def pal_line(lines):
    l = first(lines, 'pal ')
    h = l.split()[1]
    return [(int(h[i:i + 2], 16), int(h[i + 2:i + 4], 16), int(h[i + 4:i + 6], 16)) for i in range(0, 1536, 6)]


# ---------------------------------------------------------------------------------------------------------------
# the oracle: NewGame.sav decoded by the analysis tool
oracle = {}
for l in subprocess.run(['python3', '-I', SAVBLOB, 'dump', SAV], capture_output=True, text=True).stdout.splitlines():
    if ' = ' in l:
        k, v = l.split(' = ', 1)
        oracle[k] = v
BLUE = float(oracle['world/state/m_dBlueSky'])
T_TIME = int(oracle['timeline/timeline/m_dwTime'])
T_DAY = int(oracle['timeline/timeline/m_dwDay'])

# ================================================================= (a) start-up
rc, out, err = game('advance 3000; game dump; game sounds')
errs = [l for l in err if not l.startswith('api not implemented:')]
check('(a) --game start-up: exit 0 and no error output other than "api not implemented"', rc == 0 and not errs, (rc, errs[:3]))
dump = first(out, 'game time=')
site = first(out, 'game site=')
check('(a) world built and NewGame blob loaded: clock = the saved one (day %d, %d s)' % (T_DAY, T_TIME),
      dump is not None and kv(dump)['time'] == str(T_TIME) and kv(dump)['day'] == str(T_DAY), dump)
check('(a) the start page is the saved one (R4): location, site and page', site is not None and kv(site)['site'] == 'R4' and kv(site)['page'] == 'R4' and kv(site)['location'] == 'R4', site)
check('(a) game time runs again after the load (ticks per game second 0x28 = 40; the saved game stores -1)', kv(dump)['ticks'] == '40', dump)
ev = [l for l in out if l.startswith('event ')]
names = [l.split()[1] for l in ev]
check('(a) events in the order startup, pageloaded R4, pagedrawn R4, enterpage R4',
      'startup' in names and [n for n, l in zip(names, ev) if 'R4' in l and n in ('pageloaded', 'pagedrawn', 'enterpage')] == ['pageloaded', 'pagedrawn', 'enterpage'], names[:12])
api = [l for l in out if l.startswith('apicall ')]
# The new game is the menu's command now (dll:10008010 case 1): the DLL's palette functions are inert while the engine loads
# (DAT_1009f840), so the palette of the start page is the one NewGame.sav saved, which the ENGINE sets (SetPalette(1, 0, ..): event 13).
check('(a) the saved palette was set by the engine on load (OnPaletteChanged), the DLL set none while loading',
      'palettechanged' in names and not any(l.startswith('apicall SetPalette ') for l in api[:api.index('apicall SetTimerInterval seconds=1 -> 0')]))
check('(a) the timer interval was set to 1 s after the new game was loaded', 'apicall SetTimerInterval seconds=1 -> 0' in api)


# ---- the order of the API calls against the decompiled text (small script: static expansion of the callees)
wr = {}
for l in open('data/dll_api_wrappers.tsv'):
    if l.startswith('#'):
        continue
    f = l.rstrip('\n').split('\t')
    wr[f[1]] = f[3]
_cache = {}


def decomp(name):
    if name not in _cache:
        r = subprocess.run(['python3', '-I', DLLPACK, 'show', name], capture_output=True, text=True)
        _cache[name] = r.stdout
    return _cache[name]


SKIP = re.compile(r'^(World__|FUN_1002b700|FUN_10039130|FUN_10051a10|FUN_10057f30|FUN_1005a|_|CWorld__UpdateBiomeCels|CWorld__Find|CWorld__PowerOff)')


def expand(name, depth=0, seen=None, virt=None):
    """API commands (EngineApi names) in textual order of the function and of its direct callees; `virt` maps a
    vtable-call marker (`+0x14`) found in the text to the callee it dispatches to"""
    seen = seen if seen is not None else set()
    if name in seen or depth > 4:
        return []
    seen = seen | {name}
    text = decomp(name)
    body = text[text.find('{'):]
    res = []
    for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(|\(\*\*\(code \*\*\)\(\*[\w ]+ \+ (0x[0-9a-f]+)\)\)\(', body):
        callee, vt = m.group(1), m.group(2)
        if vt:
            for target in (virt or {}).get((name, vt), []):
                res += expand(target, depth + 1, seen, virt)
            continue
        if callee in ('if', 'while', 'for', 'switch', 'sizeof', name):
            continue
        if callee in wr:
            res.append(wr[callee])
        elif callee.startswith('Eng_'):
            res.append(callee)
        elif not SKIP.match(callee) and (callee.startswith('FUN_') or '__' in callee):
            res += expand(callee, depth + 1, seen, virt)
    return res


def first_order(seq):
    out_, seen_ = [], set()
    for x in seq:
        if x not in seen_:
            seen_.add(x)
            out_.append(x)
    return out_


def observed(lines, lo=None, hi=None):
    names_ = []
    on = lo is None
    for l in lines:
        if lo and l.startswith(lo):
            on = True
        if hi and l.startswith(hi):
            break
        # the two EnableHotspot calls of CSynthHandler::OnEnterPage (dll:10008830, the first thing CSelf::OnEnterPage does) are not
        # part of CSite::UpdateDisplayState
        # the player's top-bar DC calls (window 0: FUN_1006f100, the message strip) are not part of the site's functions
        if on and l.startswith('apicall ') and 'hotspot="SYNTH' not in l and not (l.split()[1] in ('GetWindowDC', 'ReleaseWindowDC') and 'window=0 ' in l):
            names_.append(l.split()[1])
    return names_


VIRT = {
    ('Event_OnStartup', '0x94'): [],
    ('Game__InitNewGameState', '0x94'): ['FUN_10029510'],
}
exp_start = first_order(expand('Event_OnStartup', virt=VIRT))
obs_start = first_order([n for n in observed(out, hi='event pageloaded topbar') if n in exp_start])
want_start = [n for n in exp_start if n in obs_start]
check('(a) API call order of the start-up path = textual order of Event_OnStartup (first occurrences)',
      obs_start == want_start and len(obs_start) >= 6, (obs_start, want_start))


# The two page-entry segments: the commands whose order does not depend on the branch taken (state images and hotspots
# of UpdateDisplayState; the palette / screen sequence of the page-drawn code). The expectation is the textual order of
# the decompiled functions CSite::UpdateDisplayState (dll:1000cab0) and FUN_1000d600 (dll:1000d600, CSite::OnPageDrawn).
S_LOADED = {'SetImageVisibility', 'EnableHotspot'}
exp_loaded = [n for n in first_order(expand('CSite__UpdateDisplayState')) if n in S_LOADED]
seg = observed(out, lo='event pageloaded R4', hi='event pagedrawn R4')
obs_loaded = [n for n in first_order(seg) if n in S_LOADED]
check('(a) API call order of the page entry (OnPageLoaded R4): SetImageVisibility before EnableHotspot, as in the text of CSite::UpdateDisplayState',
      obs_loaded == exp_loaded[:len(obs_loaded)] and len(obs_loaded) == 2, (obs_loaded, exp_loaded))
S_DRAWN = {'ClearWindow', 'RedrawScreen', 'GetWindowDC', 'ReleaseWindowDC'}   # no SetPalette: the load inhibits it (see above)
exp_drawn = [n for n in first_order(expand('FUN_1000d600')) if n in S_DRAWN]
seg = observed(out, lo='event pagedrawn R4', hi='event enterpage R4')
obs_drawn = [n for n in first_order(seg) if n in S_DRAWN]
want_drawn = [n for n in exp_drawn if n in obs_drawn]
check('(a) API call order of OnPageDrawn R4: ClearWindow, RedrawScreen, GetWindowDC, ReleaseWindowDC = textual order of FUN_1000d600 (SetPalette is inert during a load)',
      obs_drawn == want_drawn and len(obs_drawn) == 4, (obs_drawn, want_drawn))

# ================================================================= (b) time
rc, o1, _ = game('advance 6000; game dump; advance 40000; game dump; game settime 28800; game dump')
dumps = [kv(l) for l in o1 if l.startswith('game time=')]
a, b = dumps[0], dumps[1]
dt = int(b['time']) - int(a['time'])
dtick = int(b['lasttick']) - int(a['lasttick'])
check('(b) game time advances by real ms / 40 = 25 x real time (%d game s over %d ms)' % (dt, dtick),
      dtick > 30000 and abs(dt - dtick // 40) <= 3 and abs(dt * 40 - dtick) < 40 * 3, (a, b))
check('(b) the clock only moves in ticks of >= 5 s: last tick within 5.1 s of now', int(b['now']) - int(b['lasttick']) <= 5100, b)
c = dumps[2]
check('(b) day / time of day / phase after setting 08:00: day 1, 28800, phase 0x14 (day), daylight 1',
      c['day'] == '1' and c['tod'] == '28800' and c['phase'] == '0x14' and float(c['daylight']) == 1.0, c)
# phases and daylight at other times, from the arithmetic of CTimeline::UpdateDiurnal (dll:10015710), computed here
def phase_of(tod):
    if tod < 0x2a30 or tod > 0x1274f: return 0x28, 0.0
    if tod < 0x5460: return 5, (tod - 0x2a30) / 10800 * 0.5
    if tod < 0x7080: return 0xf, (tod - 0x5460) / 7200 * 0.5 + 0.5
    if tod < 0xe100: return 0x14, 1.0
    if tod < 0xfd20: return 0x1e, (tod - 0xe100) / 7200 * -0.5 + 1.0
    return 0x23, (tod - 0xfd20) / 10800 * -0.5 + 0.5
cmds = []
times = [3600, 15000, 22000, 43200, 60000, 68400, 80000, 86399]
for tt in times:
    cmds.append('game settime %d; game dump' % (tt + 86400))        # day 2
rc, o2, _ = game('; '.join(cmds))
ok = True
bad = []
for tt, l in zip(times, [x for x in o2 if x.startswith('game time=')]):
    d = kv(l)
    ph, dl = phase_of(tt)
    if not (d['phase'] == '0x%x' % ph and abs(float(d['daylight']) - dl) < 1e-6 and d['day'] == '2' and d['tod'] == str(tt)):
        ok = False
        bad.append((tt, d, ph, dl))
check('(b) time-of-day phase and daylight level at 8 times of day (day 2 rollover included)', ok, bad[:2])

# --- the time-of-day palette on page R4 (flag 0x100 site, group A, open air: day palette = cloud blended into day by the sky)
rf = {n: read_pal('palettes\\rforest\\rf_%s.pal' % n) for n in ('night', 'dawn', 'day', 'dusk', 'cloud')}
daymix = blend(rf['cloud'], rf['day'], BLUE)
rc, o3, _ = game('advance 500; game settime 28800; pal; game dump; game settime 43200; pal; game settime 68400; pal; game settime 3600; pal')
pals = [l for l in o3 if l.startswith('pal ')]
pls = [pal_line([l]) for l in pals]
avg = float(kv(first(o3, 'game site='))['plantavg'])
check('(b) R4 plant health average of the cel as saved (1.0) is what the site entry computed', abs(avg - float(oracle['cels/R4/m_dPlantHealthAvg'])) < 1e-9, avg)


def rgb(p): return [(x[0], x[1], x[2]) for x in p]


exp0800 = rgb(tint_green(daymix, avg))
check('(b) 08:00 palette on R4 = rf_cloud blended into rf_day by the clear-sky fraction %.4f' % BLUE, pls[0] == exp0800,
      sum(1 for x, y in zip(pls[0], exp0800) if x != y))
check('(b) 12:00 palette on R4 = the same day palette', pls[1] == exp0800)
dusk_night = rgb(tint_green(blend(rf['dusk'], rf['night'], (68400 - 0xfd20) / 10800), avg))
check('(b) 19:00 palette on R4 = rf_dusk blended 1/3 of the way into rf_night (MakeIntermediatePalette arithmetic)',
      pls[2] == dusk_night, sum(1 for x, y in zip(pls[2], dusk_night) if x != y))
check('(b) 01:00 palette on R4 = rf_night.pal', pls[3] == rgb(rf['night']), sum(1 for x, y in zip(pls[3], rgb(rf['night'])) if x != y))
dawn_day = rgb(tint_green(blend(rf['dawn'], daymix, (25200 - 0x5460) / 7200), avg))
rc, o4, _ = game('advance 500; game settime 25200; pal')
check('(b) 07:00 palette on R4 = rf_dawn blended into the day palette (morning phase)', pal_line(o4) == dawn_day)

# ================================================================= (c) effects of pages and sites
fxrows = {}
for l in open(EVENTS):
    e = json.loads(l)
    if e[0] == 'fx' and e[5]:
        fxrows.setdefault(e[5][2][0], []).append((e[2], e[3]))
CTORS = {'FUN_100706b0': (5, 6, 0x2), 'FUN_10002eb0': (4, 5, 0x1), 'FUN_10015ce0': (5, 6, 0x2), 'FUN_10017870': (3, 4, 0x4)}


def fx_expected(page, phase, site_state, lights, ocean_health=1.0):
    """CSpotFX::CanStart (dll:1000a5b0) evaluated from the builder arguments only (state / health flags need the site)"""
    res = []
    for ctor, a in fxrows[page]:
        fi, oi, typ = CTORS[ctor]
        flags, on = a[fi] | typ, a[oi]
        ok = bool(on)
        if flags & 0x1000 and phase in (0xf, 0x14, 0x1e): ok = False
        if flags & 0x2000 and phase in (0x23, 0x28, 5): ok = False
        if flags & 0x10000 and site_state != 2: ok = False
        if flags & 0x40000 and site_state != 4: ok = False
        if flags & 0x20000 and site_state != 1: ok = False
        if flags & 0x80000 and ocean_health < 0.1: ok = False
        if flags & 0x100000 and ocean_health >= 0.1: ok = False
        if flags & 0x4000 and not lights: ok = False
        if flags & 0x8000 and lights: ok = False
        if flags & 0x10 or flags & 0x200000 or flags & 0x400000 or flags & 0x800000:
            ok = None            # depends on a game object / the plant health: not decided here
        res.append((a[0].upper(), flags, ok))
    return res


for label, tod in (('night', 3600), ('day', 43200)):
    rc, o5, _ = game('advance 500; game settime %d; page R1; advance 200; game dump; game fx' % tod, seed=3)
    d = kv(first(o5, 'game time='))
    st = int(kv(first(o5, 'game site='))['sitestate'])
    lights = int(kv(first(o5, 'game site='))['lights'])
    got = {}
    for l in o5:
        if l.startswith('fx '):
            p = l.split()
            got[p[2]] = dict(kv('x ' + ' '.join(p[3:])))
            got[p[2]]['kind'] = p[1]
    exp = fx_expected('R1', int(d['phase'], 16), st, lights)
    bad = []
    for name, flags, ok in exp:
        key = name.replace('\\\\', '\\')
        g_ = got.get(key)
        if g_ is None:
            bad.append((key, 'missing'))
        elif ok is not None and (g_['canstart'] == '1') != ok:
            bad.append((key, g_['canstart'], ok))
    check('(c) R1 at %s: CanStart of all %d FX of the page equals the independent evaluation of the builder flags' % (label, len(exp)),
          not bad and len(got) == len(exp), bad[:3])
    shoot = [v for k, v in got.items() if 'SHOOT' in k]
    opened = [v for v in shoot if v['handle'] != '-1']
    if label == 'night':
        check('(c) R1 at night: the shooting-star animations (flag 0x1000) were opened on entry', len(shoot) == 4 and len(opened) == 4, shoot)
    else:
        check('(c) R1 by day: no shooting-star animation opened or started', len(shoot) == 4 and not opened and all(v['canstart'] == '0' for v in shoot), shoot)

# the occasional shooting stars at night are started by the idle code with a forced seed; by day never
def sprite_opens(out_lines, needle):
    return sum(1 for l in out_lines if l.startswith('apicall OpenSpriteAnimation') and needle in l)


rc, n1, _ = game('advance 500; game settime 3600; game seed 5; page R1; advance 60000; game fx', seed=5)
plays_night = [l for l in n1 if l.startswith('apicall PlaySpriteAnimation')]
rc, d1, _ = game('advance 500; game settime 43200; game seed 5; page R1; advance 60000; game fx', seed=5)
started_day = [l for l in d1 if l.startswith('fx occ') and 'SHOOT' in l and 'playing=1' in l]
check('(c) night: shooting-star animations opened (OpenSpriteAnimation stars\\shoot*) on R1', sprite_opens(n1, 'STARS\\\\SHOOT') >= 4, sprite_opens(n1, 'STARS'))
check('(c) day: no shooting-star animation is ever opened on R1 in 60 s', sprite_opens(d1, 'STARS') == 0 and not started_day)
# the generator: the same seed gives the same run
rc, n2, _ = game('advance 500; game settime 3600; game seed 5; page R1; advance 60000; game fx', seed=5)
check('(c) forced seed: the run is reproducible (identical API log)', [l for l in n1 if l.startswith('apicall')] == [l for l in n2 if l.startswith('apicall')])

# ---- ambient sounds by time of day and by site
sites = {}
for l in open(EVENTS):
    e = json.loads(l)
    if e[0] == 'site':
        a = e[3]
        sites[a[0]] = a
rc, o6, _ = game('advance 500; game dump; page S1; advance 200; game sounds; page R4; advance 200; game sounds')
api6 = [l for l in o6 if l.startswith('apicall')]


def opens(lines, needle=None):
    r = []
    for l in lines:
        m = re.match(r'apicall OpenSound file="(.*?)" .*out handle=(\d+)', l)
        if m and (needle is None or needle in m.group(1).lower()):
            r.append((m.group(1).replace('\\\\', '\\'), int(m.group(2))))
    return r


s_r4 = sites.get('R4')
s_s1 = sites.get('S1')
day_r4 = s_r4[9].lower() if s_r4 else None
day_s1 = s_s1[9].lower() if s_s1 else None
check('(c) entering R4 starts its looped day ambient wav (loop=1, volume -10000 then faded by time of day)',
      day_r4 and any(l.startswith('apicall OpenSound file="%s" loop=1' % day_r4.replace('\\', '\\\\')) for l in [x.lower() for x in api6]) is not None, day_r4)
loops_open = [x for x in o6 if re.match(r'apicall OpenSound file=".*" loop=1', x)]
check('(c) entering R4 opened looped ambient wavs (day, night, dead)', len(loops_open) >= 3, len(loops_open))
# leaving R4 for S1 (another biome: other ambient files) closes R4's looped handles and opens S1's
i_s1 = next(i for i, l in enumerate(api6) if l.startswith('apicall JumpToPage')) if any(l.startswith('apicall JumpToPage') for l in api6) else None
rc, o7, _ = game('advance 500; page S1; advance 200')
a7 = [l for l in o7 if l.startswith('apicall')]
closes = [l for l in a7 if l.startswith('apicall CloseSound handle=') and not l.startswith('apicall CloseSound handle=-1')]
opened_s1 = [l for l in a7 if re.match(r'apicall OpenSound file=".*" loop=1', l)]
first_loops = [l for l in o7 if re.match(r'apicall OpenSound file=".*" loop=1', l)]
check('(c) leaving R4 for S1 closes the looped ambient sounds of R4 and opens those of S1', len(closes) >= 1 and len(opened_s1) >= 6, (len(closes), len(opened_s1)))
check('(c) S1 ambient sounds are other files than R4\'s', len(set(re.findall(r'file="(.*?)"', ' '.join(opened_s1)))) >= 4)
# night: the night sound is loud, the day sound muted (SetSoundProperty VOLUME on the loop handles)
rc, o8, _ = game('advance 500; game settime 3600')
a8 = [l for l in o8 if l.startswith('apicall')]
hs = {}
for l in a8:
    m = re.match(r'apicall OpenSound file="(.*?)" loop=1 .*out handle=(\d+)', l)
    if m:
        hs[m.group(1)] = int(m.group(2))
vols = [(int(m.group(1)), int(m.group(2))) for l in a8 for m in [re.match(r'apicall SetSoundProperty handle=(\d+) property="VOLUME" value=(-?\d+)', l)] if m]
day_h = [h for f, h in hs.items() if 'day' in f.lower()]
night_h = [h for f, h in hs.items() if 'night' in f.lower() and 'rfnight' in f.lower()]
lastv = {}
for h, v in vols:
    lastv[h] = v
check('(c) at 01:00 the night ambient wav plays at full volume and the day wav is silent',
      day_h and night_h and lastv.get(night_h[0]) == 0 and lastv.get(day_h[0]) == -10000, (hs, lastv))
# health state selects the variant: R4 (flag 0x100|0x2000): soil water 47.5 -> flood variants; plant health 1.0 -> healthy
sw = float(oracle['cels/R4/m_dSoilWater'])
exp_img = ('hFlood' if avg > 0.2 else 'dFlood') if sw >= 47.5 else ('healthy' if avg > 0.2 else 'dead')
shown = [l for l in api if re.match(r'apicall SetImageVisibility window=-1 image="(\w+)" visible=1', l)]
check('(c) R4 shows the %s variant (soil water %.1f, plant health %.2f) and no other state image' % (exp_img, sw, avg),
      shown and shown[0].split('"')[1] == exp_img and len([s for s in shown if s.split('"')[1] in ('dead', 'dFlood', 'healthy', 'hFlood', 'unhealthy')]) == 1, shown[:3])

# ================================================================= (d) fruit
# page R5: the fruit ornament R5bz_1 lies inside its zone Rr5bz_1 (rect 1348,737 .. 1462,847; the page is a panorama)
rc, f1, _ = game('page R5; scroll 1080,606; api RedrawPage; surfdump %s/fruit_a.raw; shot %s/fruit_a.bmp; echo #c; clickp 1405,792; echo #d; dump objects; dump bar; echo #end' % (T, T))
# (the plant's own display now paints the fruit images with DisplayImage, as the original does: a picked fruit is hidden and the page
# painted again: CPlant::Display(plant, 1); so the comparison is between two painted-again pages)
rc, f2, _ = game('page R5; scroll 1080,606; remove R5bz_1; api SetImageVisibility -1 R5bz_1 0; api RedrawPage; surfdump %s/fruit_b.raw; shot %s/fruit_b.bmp' % (T, T))
evs = [l for l in f1 if l.startswith('event ')]
check('(d) R5: event 29 asks for the fruit ornaments and the game answers with the ornament name',
      any(l.startswith('event fruit R5bz_1') for l in evs), [l for l in evs if 'fruit' in l][:3])


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    assert struct.unpack_from('<H', d, 28)[0] == 8
    stride = (w + 3) & ~3
    rows = [d[off + y * stride: off + y * stride + w] for y in range(abs(h))]
    if h > 0:
        rows.reverse()
    return w, abs(h), rows


w, h, ra = read_bmp(T + '/fruit_a.bmp')
w, h, rb = read_bmp(T + '/fruit_b.bmp')
# the fruit is painted into the page surface (2560 x 1024; the view is its cylindrical projection): compare the surfaces, page coordinates
sa, sb = open(T + '/fruit_a.raw', 'rb').read(), open(T + '/fruit_b.raw', 'rb').read()
diff = [(i % 2560, i // 2560) for i in range(len(sa)) if sa[i] != sb[i]]
inzone = [(x, y) for x, y in diff if 1348 - 8 <= x <= 1462 + 8 and 737 - 8 <= y <= 847 + 8]
diff_view = sum(1 for y in range(h) for x in range(w) if ra[y][x] != rb[y][x])
check('(d) R5: the fruit ornament R5bz_1 is painted (taking the object away changes %d pixels, all inside its zone)' % len(diff),
      len(diff) > 20 and len(inzone) == len(diff) and diff_view > 0, (len(diff), len(inzone), diff_view))
evc = [l.split()[3] for l in f1 if l.startswith('event gameobject R5bz_1')]
check('(d) clicking the fruit: events 0x0a, 0x01, 0x0b, 0x02', evc == ['10', '1', '11', '2'], evc)
sec_d = f1[f1.index('#d') + 1:f1.index('#end')]
check('(d) the fruit is on the inventory bar (status 3, slot 0)',
      any(re.match(r'obj \d+ R5bz_1 status=3', l) for l in sec_d) and any(l.startswith('slot 0 R5bz_1 ') for l in sec_d), [l for l in sec_d if 'R5bz_1' in l])

# ================================================================= (e) soak
cov = {}
for l in subprocess.run(['build/biosys-apicoverage'], capture_output=True, text=True).stdout.splitlines():
    m = re.match(r'0x([0-9a-f]+) (\w+) (implemented|stub)', l)
    if m:
        cov[m.group(2)] = m.group(3)
pages = ['R1', 'S1', 'W1', 'T1', 'R4']
script = 'game type SILVERFEATHER; game type IMMORTAL; advance 2000'   # the player model now lives: the ocean page would drown the player and stop the clock
for i, p in enumerate(pages):
    script += '; page %s; game settime %d; advance 11000' % (p, [30000, 3600, 43200, 68000, 30000][i])
script += '; mixdump %s/soak.wav 2000; game dump' % T
rc, s1, serr = game(script, seed=11)
nimp = [l.split()[3] for l in serr if l.startswith('api not implemented:')]
bad_impl = [n for n in nimp if cov.get(n) == 'implemented']
check('(e) 60 s soak on 5 pages with page jumps: exit 0, no exception', rc == 0 and not [l for l in serr if l.startswith('biosys:')], (rc, serr[-3:]))
check('(e) soak: no "api not implemented" for a command the coverage tool lists as implemented', not bad_impl, bad_impl)
wav = open(T + '/soak.wav', 'rb').read()
data = wav[44:]
check('(e) soak: the mixer output is not silent at some point', any(data[i] not in (0, 0x80) for i in range(0, len(data), 2)) if data else False, len(data))
dsk = first(s1, 'game time=')
check('(e) soak: the clock ran through the 60 s (last tick within 5.1 s of the end)', dsk and int(kv(dsk)['now']) - int(kv(dsk)['lasttick']) <= 5100 and int(kv(dsk)['lasttick']) > 50000, dsk)

# ================================================================= (f) default behaviour
r = subprocess.run(['build/biosys', '--log-events', '--script', 'advance 300'], capture_output=True, text=True)
lines = r.stdout.splitlines()
check('(f) without --game: the viewer logic (no apicall / game output, same events as before)',
      r.returncode == 0 and not any(l.startswith(('apicall', 'game ')) for l in lines) and 'event startup' in lines and 'event enterpage R4' in lines, lines[:6])

print('live tests: %s' % ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
