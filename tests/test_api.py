"""Tests of the game-logic interface: engineapi.h (110 commands), data/dll_api_wrappers.tsv, the engine's
EngineApi implementation (through `api` script commands), the restriction masks and the event order."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
T = 'build/test/api'
os.makedirs(T, exist_ok=True)
BIOTIME = [D + '/setup-cd/installed/DATA1/Biotime', D + '/game-cd/files/BIOTIME']
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:600] if detail else ''))


def sh(args, stderr=False):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('%s failed: %s' % (args[:3], r.stderr.strip()[:300]))
    return (r.stdout, r.stderr) if stderr else r.stdout


def run(script, *opts):
    return sh(['build/biosys', '--log-events', '--mute'] + list(opts) + ['--script', script]).splitlines()


def run_err(script):
    o, e = sh(['build/biosys', '--log-events', '--mute', '--script', script], stderr=True)
    return o.splitlines(), e.splitlines()


def find_time(rel):
    parts = rel.replace('\\', '/').split('/')
    for root in BIOTIME:
        p = root
        for part in parts:
            try:
                names = os.listdir(p)
            except OSError:
                p = None
                break
            m = [n for n in names if n.lower() == part.lower()]
            if not m:
                p = None
                break
            p = p + '/' + m[0]
        if p:
            return p
    raise RuntimeError('missing ' + rel)


def api_rc(lines, name):
    return [int(re.search(r'rc=(\d+)', l).group(1)) for l in lines if l.startswith('api %s ' % name)]


def norm(lines):
    """`api Name rc=N ...` -> `api N` so that API runs compare with the direct script commands."""
    out = []
    for l in lines:
        m = re.match(r'api \w+ rc=(\d+)', l)
        out.append('api ' + m.group(1) if m else l)
    return out


def same_files(a, b):
    return open(a, 'rb').read() == open(b, 'rb').read()


def pcm(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    n, = struct.unpack_from('<I', d, i + 4)
    d = d[i + 8:i + 8 + n]
    return struct.unpack('<%dh' % (len(d) // 2), d)


# ================================================================== the header
hdr = open('src/engine/engineapi.h').read()
methods = re.findall(r'// (0x[0-9a-f]{2}) (\w+)   engine .*?   DLL wrapper (\S+)\n(?:    //.*\n)*?    virtual int (\w+)\(([^)]*)\) = 0;', hdr)
check('engineapi.h declares 110 commands, each comment names its method', len(methods) == 110 and all(m[1] == m[3] for m in methods), len(methods))
codes = [int(m[0], 16) for m in methods]
check('codes 0x01 .. 0x6e, each exactly once, in order', codes == list(range(1, 0x6f)), codes[:5])
pnames = {m[1]: [re.split(r'[ &*]+', p.strip())[-1] for p in m[4].split(',')] if m[4].strip() else [] for m in methods}

# ================================================================== biosys-apicoverage
cov = sh(['build/biosys-apicoverage']).splitlines()
rows = [l.split() for l in cov if l.startswith('0x')]
check('apicoverage: one line per command, 110 distinct codes 0x01..0x6e', [int(r[0], 16) for r in rows] == list(range(1, 0x6f)), len(rows))
check('apicoverage: names agree with engineapi.h', [r[1] for r in rows] == [m[1] for m in methods])
nimpl = sum(1 for r in rows if r[2] == 'implemented')
nstub = sum(1 for r in rows if r[2] == 'stub')
summ = [l for l in cov if l.startswith('summary:')][0]
check('apicoverage: summary agrees with the lines (%d implemented, %d stubs)' % (nimpl, nstub),
      'commands=110 implemented=%d stub=%d' % (nimpl, nstub) in summ and nimpl + nstub == 110, summ)
wr_rows = [r for r in rows if r[3] != 'dll=-']
check('apicoverage: 81 commands have a DLL wrapper', len(wr_rows) == 81 and 'dll-wrappers=81' in summ, summ)
check('apicoverage: every stub names the feature it needs', all(any(t.startswith('needs=') for t in r) for r in rows if r[2] == 'stub'))
check('apicoverage: at least 15 implemented commands', nimpl >= 15, nimpl)
hdr_dll = {int(m[0], 16): m[2] for m in methods}
check('apicoverage: the wrapper addresses of the header comments agree with the table',
      all((hdr_dll[int(r[0], 16)] == 'dll:' + r[3][4:]) if r[3] != 'dll=-' else hdr_dll[int(r[0], 16)] == '-' for r in rows))

# ================================================================== data/dll_api_wrappers.tsv
tsv = [l.rstrip('\n').split('\t') for l in open('data/dll_api_wrappers.tsv') if l.strip() and not l.startswith('#')]
check('dll_api_wrappers.tsv: 81 rows with 5 columns', len(tsv) == 81 and all(len(r) == 5 for r in tsv), len(tsv))
dll = open(D + '/analysis/decompiled/BiosGame.dll.c', errors='replace').read()
funcs = re.split(r'(?m)^// ===== ', dll)
found = {}
for f in funcs:
    m = re.match(r'([0-9a-f]{8}) (\S+) \(size', f)
    mm = re.search(r'SendMessageA\(DAT_100c4324,0x8000,(0x[0-9a-f]+|\d+),', f)
    if m and mm:
        found[m.group(1)] = (m.group(2), int(mm.group(1), 0), f)
check('every wrapper of the DLL (independent scan: %d) is in the table, and nothing else' % len(found),
      {r[0] for r in tsv} == set(found) and all(found[r[0]][0] == r[1] and found[r[0]][1] == int(r[2], 16) for r in tsv))
ok_m = all(int(r[2], 16) in range(1, 0x6f) and methods[int(r[2], 16) - 1][1] == r[3] for r in tsv)
check('every row maps to an existing EngineApi method with the same command code', ok_m)
bad = []
for r in tsv:
    names = pnames[r[3]]
    mapping = r[4].split(' ; ')[0]
    for tok in re.findall(r'p[\d,p]+(?:->|<-)([A-Za-z0-9_]+)', mapping):
        if tok not in names:
            bad.append((r[3], tok))
check('every parameter named in the argument mappings exists in the method', not bad, bad[:5])
sigs = {}
for addr, (nm, code, f) in found.items():
    sig = re.search(r'=====[^\n]*\n(?:\n)?(.*?)\{', f, re.S)
    sigs[addr] = len(re.findall(r'param_\d+', f.split('{', 1)[0]))
miss = []
for r in tsv:
    n = sigs[r[0]]
    present = set()
    for t in re.findall(r'p([\d,p]+)(?:->|<-)', r[4].split(' ; ')[0]):
        for k in re.findall(r'\d+', t):
            present.add(int(k))
    if n and not set(range(1, n + 1)) <= present:
        miss.append((r[1], n, sorted(present)))
check('every argument of every wrapper appears in its mapping', not miss, miss[:5])
check('wrapper names carry the Ghidra names of the decompile', all(r[1].startswith('Eng_') for r in tsv))

# ================================================================== API vs direct calls: objects
SC = 'page O1; slot day; scroll 420,470; shot {d}/a0.bmp; clickp 740,714; shot {d}/a1.bmp; dump state; dump bar; ' \
     '{rm}; shot {d}/a2.bmp; dump objects; {ret}; shot {d}/a3.bmp; dump objects; dump state'
for d in ('dir', 'api'):
    os.makedirs('%s/%s' % (T, d), exist_ok=True)
direct = run(SC.format(d=T + '/dir', rm='remove kukri', ret='returnobj kukri'))
viaapi = run(SC.format(d=T + '/api', rm='api RemoveGameObject kukri', ret='api ReturnGameObject kukri'))
check('objects: RemoveGameObject / ReturnGameObject give the same text output as the direct calls', norm(viaapi) == direct,
      [l for l in norm(viaapi) if l not in direct][:3])
check('objects: ... and the same frames', all(same_files('%s/dir/a%d.bmp' % (T, k), '%s/api/a%d.bmp' % (T, k)) for k in range(4)))
check('objects: the kukri was picked up, removed and returned', 'held kukri' in direct and api_rc(viaapi, 'RemoveGameObject') == [0], direct[-3:])
out = run('page O1; slot day; scroll 420,470; api GetGameObjectInfo kukri; clickp 740,714; api GetGameObjectInfo kukri; '
          'api RemoveGameObject kukri; api GetGameObjectInfo kukri; api GetGameObjectInfo nothere; api GetGameObjectCount; '
          'api GetGameObjectName 0; api GetGameObjectName 100000; api GetDropZoneCount; api GetDropZoneInfo 0; '
          'api GetDropZoneInfo 100000; api SetGameObjectProperty kukri weight 500; api SetGameObjectProperty kukri bogus 1; '
          'api GetGameObjectInfo kukri')
info = [re.search(r'info=([-\d,]+)', l).group(1).split(',') for l in out if l.startswith('api GetGameObjectInfo') and 'info=' in l]
check('objects: GetGameObjectInfo: in zone, then held (4), then removed (6), weight clamped to 100',
      len(info) == 4 and info[0][2] == '1' and info[1][4] == '1' and info[2][6] == '1' and info[3][8] == '100', info)
check('objects: unknown object 0xd, bad zone 0xe, unknown property 0xf',
      api_rc(out, 'GetGameObjectInfo')[3] == 13 and api_rc(out, 'GetDropZoneInfo')[1] == 14 and api_rc(out, 'SetGameObjectProperty') == [0, 15] and api_rc(out, 'GetGameObjectName')[1] == 13, out[-12:])

# the object animation player (API 0x47)
ua = 'anims\\OBJ_UP\\kukup.tan'
sd = run('page O1; slot day; animshots %s/dir/u; useanim %s - 100 20 1 40 0 0; useanim anims\\nothere.tan - 100 20 1 40 0 0' % (T, ua))
sa = run('page O1; slot day; animshots %s/api/u; api PlayObjectUseAnimation %s - 100 20 1 40 0 0; api PlayObjectUseAnimation anims\\nothere.tan - 100 20 1 40 0 0' % (T, ua))
check('PlayObjectUseAnimation: same results as the direct call (0 and 0x11)', norm(sa) == sd and api_rc(sa, 'PlayObjectUseAnimation') == [0, 17], sa[-4:])
fd = sorted(f for f in os.listdir(T + '/dir') if f.startswith('u'))
fa = sorted(f for f in os.listdir(T + '/api') if f.startswith('u'))
check('PlayObjectUseAnimation: the same animation frames (%d)' % len(fd), len(fd) >= 3 and fd == fa and all(same_files(T + '/dir/' + f, T + '/api/' + f) for f in fd))

# ================================================================== API vs direct calls: sprite animations
rel = 'anims\\OBJ_UP\\kukup.tan'
for kind, (x, y, scroll, pagerel) in (('fixed', (100, 20, '420,470', 0)), ('page', (100, 500, '2400,470', 1))):
    pre = 'page O1; slot day; scroll %s; ' % scroll
    post = 'shot {d}/s_%s0.bmp; advance 150; shot {d}/s_%s1.bmp; advance 5000; shot {d}/s_%s2.bmp' % (kind, kind, kind)
    od = run(pre + 'sprite %s %d %d 1 100 %s; ' % (rel, x, y, kind) + post.format(d=T + '/dir'))
    oa = run(pre + 'api OpenSpriteAnimation %s -1 %d %d 1 100 1 0 0 - 0 0 %d -1 0; api PlaySpriteAnimation 0 1 0; ' % (rel, x, y, pagerel) + post.format(d=T + '/api'))
    check('sprite (%s): OpenSpriteAnimation + PlaySpriteAnimation = the `sprite` command (frames)' % kind,
          all(same_files('%s/dir/s_%s%d.bmp' % (T, kind, k), '%s/api/s_%s%d.bmp' % (T, kind, k)) for k in range(3)))
    ev_d = [l for l in od if l.startswith('event sprite')]
    ev_a = [l for l in oa if l.startswith('event sprite')]
    check('sprite (%s): the same callback events (%s)' % (kind, ' '.join(l.split()[2:] and ','.join(l.split()[3:]) for l in ev_d[:4])),
          ev_d == ev_a and len(ev_d) >= 3, (ev_d, ev_a))
    check('sprite (%s): the animation actually changed the frame' % kind,
          not same_files('%s/api/s_%s0.bmp' % (T, kind), '%s/api/s_%s2.bmp' % (T, kind)) or kind == 'fixed')
o = run('page O1; api OpenSpriteAnimation %s -1 100 20 1 100 1 0 0 - 0 0 0 -1 0; api IsSpriteAnimationPlaying 0; api PlaySpriteAnimation 0 1 0; '
        'api IsSpriteAnimationPlaying 0; api GetSpriteAnimationSound 0; api StopSpriteAnimation 0; api IsSpriteAnimationPlaying 0; '
        'api CloseSpriteAnimation 0; api CloseSpriteAnimation 0; api PlaySpriteAnimation 0 1 0; api IsSpriteAnimationPlaying 7' % rel)
check('sprite: play / is-playing / stop / close and the bad-handle error 0x13',
      api_rc(o, 'IsSpriteAnimationPlaying') == [0, 0, 0, 19] and [re.search(r'playing=(\d)', l).group(1) for l in o if 'IsSpriteAnimationPlaying rc=0' in l] == ['0', '1', '0']
      and api_rc(o, 'CloseSpriteAnimation') == [0, 19] and api_rc(o, 'PlaySpriteAnimation') == [0, 19] and api_rc(o, 'GetSpriteAnimationSound') == [0], o)

# ================================================================== API vs direct calls: movie
mv = 'movies\\intro\\jumpstrt.avi'
info = sh(['build/biosys-avidump', 'info', find_time(mv)]).split()
fps = float([t for t in info if t.startswith('fps=')][0][4:])
steps = 'shot {d}/m0.bmp; advance %d; shot {d}/m1.bmp; advance %d; shot {d}/m2.bmp; advance 4000; shot {d}/m3.bmp' % (int(1000 / fps * 2) + 3, int(1000 / fps * 7))
pre = 'page O1; slot day; scroll 100,100; '
od = run(pre + 'movie %s 100 100; ' % mv + steps.format(d=T + '/dir'))
oa = run(pre + 'api OpenAVIMovie %s 100 100; api PlayAVIMovie 0 1; ' % mv + steps.format(d=T + '/api'))
check('movie: OpenAVIMovie + PlayAVIMovie = the `movie` command (frames)', all(same_files('%s/dir/m%d.bmp' % (T, k), '%s/api/m%d.bmp' % (T, k)) for k in range(4)))
evd = [l for l in od if l.startswith('event movie')]
eva = [l for l in oa if l.startswith('event movie')]
check('movie: the same callback events (%d)' % len(evd), evd == eva and len(evd) > 4, (evd[:3], eva[:3]))
check('movie: the movie shows (frames differ)', not same_files(T + '/api/m0.bmp', T + '/api/m2.bmp'))
o = run('page O1; api OpenAVIMovie %s 100 100; api IsAVIMoviePlaying 0; api PlayAVIMovie 0 1; api IsAVIMoviePlaying 0; api StopAVIMovie 0; '
        'api IsAVIMoviePlaying 0; api CloseAVIMovie 0; api CloseAVIMovie 0; api PlayAVIMovie 0 1; api OpenAVIMovie nothere.avi 0 0' % mv)
check('movie: play / is-playing / stop / close; bad handle 0x18; missing file 0x11',
      [re.search(r'playing=(\d)', l).group(1) for l in o if 'IsAVIMoviePlaying rc=0' in l] == ['0', '1', '0'] and api_rc(o, 'CloseAVIMovie') == [0, 24]
      and api_rc(o, 'PlayAVIMovie') == [0, 24] and api_rc(o, 'OpenAVIMovie') == [0, 17], o)

# ================================================================== sounds through the API
wav = 'sounds\\house\\squelch3.wav'
sh(['build/biosys-wavdump', 'render', T + '/oracle.wav', find_time(wav)])
orc = pcm(T + '/oracle.wav')
o = run('page nPH13dead; slot day; api OpenSound %s 0 1 0 0 0; api IsSoundPlaying 0; api PlaySound 0 1; api IsSoundPlaying 0; mixdump %s/snd.wav 200; '
        'api SetSoundProperty 0 volume -1000; api SetSoundProperty 0 bogus 1; api StopSound 0; api IsSoundPlaying 0; api CloseSound 0; '
        'api PlaySound 0 1; api IsSoundPlaying 3; api SetSoundPanToPagePosition 5 1 100 -' % (wav, T))
a = pcm(T + '/snd.wav')
check('sound: OpenSound + PlaySound mix exactly the wav file', len(a) > 10000 and max(map(abs, a)) > 1000 and a[:len(a)] == orc[:len(a)], (len(a), len(orc)))
check('sound: IsSoundPlaying 0 then 1; property errors 0xf; close; bad handle 0x10',
      [re.search(r'playing=(\d)', l).group(1) for l in o if 'IsSoundPlaying rc=0' in l] == ['0', '1', '0'] and api_rc(o, 'SetSoundProperty') == [0, 15]
      and api_rc(o, 'CloseSound') == [0] and api_rc(o, 'PlaySound') == [0, 16] and api_rc(o, 'IsSoundPlaying')[-1] == 16
      and api_rc(o, 'SetSoundPanToPagePosition') == [16], o)
check('sound: callback events created (1), played (2), stopped (3), destroyed (4)',
      [l.split()[3] for l in o if l.startswith('event sound 0 ')] == ['1', '2', '3', '4'], [l for l in o if l.startswith('event sound')])

# ================================================================== hotspots, timers, popups, misc
o = run('page nPH13dead; slot day; api EnableHotspot -1 bsquelch3 0; click 200,256; api EnableHotspot -1 bsquelch3 1; click 200,256; '
        'api EnableHotspot -1 nothere 1; api EnableHotspot 9 bsquelch3 1; api EnableHotspot -1 bsquelch* 0 1; click 200,256; '
        'api EnableHotspot -1 bsquelch* 1 1; click 200,256; api GetHotspotNameByIndex -1 0; api GetHotspotNameByIndex -1 99999')
hs = [l for l in o if l.startswith('event hotspot')]
check('EnableHotspot: a disabled hotspot is not hit (no OnHotspot), enabling gives it back, the prefix wildcard works',
      len(hs) == 2 and api_rc(o, 'EnableHotspot') == [0, 0, 4, 3, 0, 0], (hs, api_rc(o, 'EnableHotspot')))
check('EnableHotspot: previous state reported; GetHotspotNameByIndex', 'previous=0' in ' '.join(o) and api_rc(o, 'GetHotspotNameByIndex') == [0, 4], o[-3:])
o = run('page ur1rain_2; advance 1800')
check('page timer (delay 8 x 250 ms): not before 2 s', not [l for l in o if l.startswith('event pagetimer')])
o = run('page ur1rain_2; advance 2600')
pt = [l for l in o if l.startswith('event pagetimer')]
check('page timer (delay 8 x 250 ms) fires once, OnPageTimer then its action (jump to page 186 fr1rain)',
      len(pt) == 1 and o.index(pt[0]) < [i for i, l in enumerate(o) if l == 'event exitpage ur1rain_2'][-1] and 'event enterpage fr1rain' in o, o[-8:])
o = run('page ur1rain_2; api EnablePageTimer -1 - 0; api EnablePageTimer -1 nothere 0; api EnablePageTimer 9 - 0; api EnablePageTimer -1 - 1')
check('EnablePageTimer error codes: 5 timer not found, 3 bad window', api_rc(o, 'EnablePageTimer') == [0, 5, 3, 0], o)
o = run('page ur1rain_2; api EnablePageTimer -1 - 0; advance 4000')
check('EnablePageTimer 0 stops the timer (no event, no jump)', not [l for l in o if l.startswith('event pagetimer')] and not any(l.startswith('event exitpage ur1rain_2') for l in o))
o = run('page O1; api DoHotspotAction -1 nothere; api DisplayPopupPage nothere; api DisplayPopupPage O1; api ClosePopupWindow 1; api ClosePopupWindow 99; '
        'api GetPanoramaInfo; api GetStatusBarObjectUnderCursor; api GetReturnPageName; api EnableUserInput 0; api EnableUserInput 1; '
        'api RedrawPage; api RedrawScreen; api EnableMouseDeltaEvent 1; api SetScrollMode 1; api GetPalette; api NoOp13 x 1; api NoOp44; '
        'api NoOp59 x; api Disabled62; api Disabled63; api SetTimerInterval 7; api WriteLog hello world; api ShowMessage hi')
check('misc: error codes of DoHotspotAction 4, DisplayPopupPage 6 / 0x11, ClosePopupWindow 0x28 / 3, disabled commands 10',
      api_rc(o, 'DoHotspotAction') == [4] and api_rc(o, 'DisplayPopupPage') == [6, 17] and api_rc(o, 'ClosePopupWindow') == [40, 3]
      and api_rc(o, 'Disabled62') == [10] and api_rc(o, 'Disabled63') == [10], o)
check('misc: GetPanoramaInfo on the panorama O1 (width 2560), GetReturnPageName',
      [l for l in o if l.startswith('api GetPanoramaInfo')][0].startswith('api GetPanoramaInfo rc=0 width=2560') and 'page=O1' in ' '.join(o))
check('misc: the no-op / log / timer commands return 0', all(api_rc(o, n) == [0] for n in
      ('NoOp13', 'NoOp44', 'NoOp59', 'SetTimerInterval', 'WriteLog', 'ShowMessage', 'RedrawPage', 'RedrawScreen', 'EnableUserInput', 'GetPalette')) or
      all(api_rc(o, n)[0] == 0 for n in ('NoOp13', 'NoOp44', 'NoOp59', 'SetTimerInterval', 'WriteLog', 'ShowMessage', 'RedrawPage', 'RedrawScreen', 'GetPalette')))

# every implemented command at least once; stubs answer 0x10000, are counted and logged once
codes_done = [0x01, 0x03, 0x05, 0x06, 0x09, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x14, 0x15, 0x17, 0x1c, 0x1f, 0x20, 0x22, 0x23, 0x25, 0x26, 0x28, 0x29,
              0x2a, 0x2c, 0x2d, 0x2e, 0x2f, 0x41, 0x47, 0x4f, 0x52, 0x40, 0x5c, 0x5f, 0x60, 0x61, 0x62, 0x64, 0x65, 0x69]
script = ('page O1; api SetTimerInterval 3; api EnableHotspot -1 x 1; api JumpToPage R4 none 1; api WriteLog x; api GetGameObjectCount; api GetGameObjectName 1; '
          'api GetGameObjectInfo kukri; api GetDropZoneCount; api GetDropZoneInfo 1; api GetHotspotNameByIndex -1 0; api SetGameObjectProperty kukri weight 5; '
          'api OpenSound %s 0 1 0 0 0; api PlaySound 0 1; api IsSoundPlaying 0; api GetPalette; api OpenSpriteAnimation %s -1 100 20 1 100 1 0 0 - 0 0 0 -1 0; '
          'api PlaySpriteAnimation 0 1 0; api IsSpriteAnimationPlaying 0; api CloseSpriteAnimation 0; api GetDropZoneForHotspot nothere; api RemoveGameObject kukri; '
          'api RedrawPage; api OpenAVIMovie %s 0 0; api PlayAVIMovie 0 1; api IsAVIMoviePlaying 0; api CloseAVIMovie 0; api EnableUserInput 1; page O1; api GetPanoramaInfo; '
          'api DoHotspotAction -1 nothere; api PlayObjectUseAnimation anims\\nothere.tan - 1 1 1 40 0 0; api AviOption41 9; api AviOption4f 9 1 1; api ReturnGameObject kukri; api EnableMouseDeltaEvent 0; api SetScrollMode 0; api DisplayPopupPage O1; '
          'api ClosePopupWindow 1; api Disabled62; api GetSpriteAnimationSound 9; api GetStatusBarObjectUnderCursor; api GetReturnPageName; '
          'api ShowCursor 1') % (wav, rel, mv)
o, err = run_err(script)
stats = sh(['build/biosys', '--mute', '--script', script + '; apistats ' + ' '.join('0x%02x' % c for c in codes_done)]).splitlines()
st = {int(l.split()[1], 16): (int(l.split()[2][6:]), int(l.split()[3][8:])) for l in stats if l.startswith('apistats')}
exercised = [c for c in codes_done if st[c][0] > 0 and st[c][1] == 0]
check('%d implemented commands exercised through the EngineApi, none answered kApiNotImplemented' % len(exercised), len(exercised) >= 15 and len(exercised) == len(codes_done), [hex(c) for c in codes_done if c not in exercised])
# SaveGame / LoadGame / OpenMenu / RestartGame are implemented now (menus_saveload.md): the commands that are still stubs
# are the sim-status window and a few others; here the four are called in an empty save directory (nothing is written)
o2, err2 = sh(['build/biosys', '--mute', '--save-dir', '/nonexistent-biosys-save-dir', '--script',
               'api SaveGame x; api LoadGame 0 x; api OpenMenu bogus; api GetSaveDirectory'], stderr=True)
o2 = o2.splitlines()
check('SaveGame / LoadGame / OpenMenu are implemented: 0x1c cannot open, 0x1f cannot open, 0x12 bad menu name; none is "not implemented"',
      api_rc(o2, 'SaveGame') == [0x1c] and api_rc(o2, 'LoadGame') == [0x1f] and api_rc(o2, 'OpenMenu') == [0x12] and
      'api GetSaveDirectory rc=0 directory=/nonexistent-biosys-save-dir' in o2 and not [l for l in err2.splitlines() if l.startswith('api not implemented:')], (o2, err2))
check('no scripted command was answered kApiNotImplemented (a stub would be logged once as "api not implemented: <code> <name>")',
      not [l for l in err if l.startswith('api not implemented:')], err)

# ================================================================== restriction masks (interface.md 3.2)
SPEC = {}
am = sh(['python3', '-I', D + '/analysis/scripts/api_masks.py']).splitlines()          # independent: read off the exe's switch
for l in am:
    m = re.match(r'bit (\d+) : (.*)', l)
    if m:
        SPEC[int(m.group(1))] = {int(t, 16) for t in m.group(2).split()}
CODE = {'JumpToPage': 5, 'EnableHotspot': 3, 'DoHotspotAction': 6, 'EnablePageTimer': 4, 'RedrawPage': 0x28, 'DisplayPopupPage': 0x60,
        'GetHotspotNameByIndex': 0x11, 'LoadGame': 0x35, 'RestartGame': 0x53, 'ReleasePageImageSlot': 0x5e, 'GetGameObjectCount': 0x0c,
        'WriteLog': 9, 'PlaySound': 0x15}
EVENT_MASK = {'BS4Screen': 1, 'ExitPage': 6, 'PageLoaded': 4, 'Hotspot': 8}


def refused(mask, code):
    return bool(mask & 1) or any(mask & b and code in SPEC.get(b, ()) for b in (2, 4, 8))


out = sh(['build/biosys-apitest', 'mask'], stderr=True)[0].splitlines()
pr = [dict(kv.split('=') for kv in l.split()[1:]) for l in out if l.startswith('probe ')]
bad = [(p['event'], p['cmd'], p['rc']) for p in pr if (p['rc'] == '10') != refused(int(p['mask']), CODE[p['cmd']])]
check('masks: %d probes; error 10 exactly where the exe switch says (bits 1 / 2 / 4 / 8)' % len(pr), len(pr) >= 60 and not bad, bad[:4])
check('masks: each event runs with its mask (BS4Screen 1, ExitPage 6, PageLoaded 4, Hotspot 8, Timer 0)',
      all(int(p['mask']) == {**EVENT_MASK, 'Timer': 0}[p['event']] for p in pr) and {p['event'] for p in pr} == {'BS4Screen', 'ExitPage', 'PageLoaded', 'Hotspot', 'Timer'}, {p['event'] for p in pr})
check('masks: the mask is restored after every event', 'mask-after-event=0' in out and 'mask-after-all=0' in out)
check('masks: the table has the page-jump rule: JumpToPage refused under masks 1, 4, 6, 8, allowed under 0',
      {(p['mask'], p['rc']) for p in pr if p['cmd'] == 'JumpToPage'} >= {('1', '10'), ('4', '10'), ('6', '10'), ('8', '10'), ('0', '0')})
out = sh(['build/biosys-apitest', 'real']).splitlines()
check('masks (real events): OnPageLoaded / OnHotspot / OnExitPage refuse JumpToPage, the page stays',
      'page-after-load-probe=O1 (was 226)' in out and any(l.startswith('probe event=Hotspot mask=8 cmd=JumpToPage rc=10') for l in out), out[:4])
check('masks (real events): OnEnterPage (mask 0) may jump: the nested jump arrives', any(l.startswith('enterjump from=O1 to=R4 rc=0 now=R4') for l in out) and 'final=R4' in out, out[-3:])

# ================================================================== events
o = run('page O1; page R4')
names = [l.split()[1] for l in o]
k = len(o) - 1 - [l for l in o][::-1].index('event exitpage O1')
seq = [l.split()[1] + ' ' + l.split()[2] for l in o[k:] if l.split()[1] in ('exitpage', 'pageloaded', 'pagedrawn', 'enterpage')]
check('page jump order: OnExitPage(old) -> OnPageLoaded(new) -> OnPageDrawn(new) -> OnEnterPage(new)',
      seq == ['exitpage O1', 'pageloaded O1', 'pagedrawn O1', 'enterpage O1'][:1] + ['pageloaded R4', 'pagedrawn R4', 'enterpage R4'] or
      seq == ['exitpage O1', 'pageloaded R4', 'pagedrawn R4', 'enterpage R4'], seq)
o = run('page O1; api JumpToPage R4 none 1; api JumpToPage nothere none 1; api JumpToPage R4 bogus 1')
seq = [l.split()[1] + ' ' + l.split()[2] for l in o if l.split()[1] in ('exitpage', 'pageloaded', 'pagedrawn', 'enterpage')]
check('JumpToPage through the API fires the same order and reports 6 / 8 for a bad page / transition',
      seq[-4:] == ['exitpage O1', 'pageloaded R4', 'pagedrawn R4', 'enterpage R4'] and api_rc(o, 'JumpToPage') == [0, 6, 8], (seq[-4:], api_rc(o, 'JumpToPage')))
ev = run('page O1', '--log-idle')
o = run('page nPH13dead; slot day; move 200,256; click 200,256; rclick 5,5; key S; key SPACE; activate off; activate on; advance 100', '--log-idle')
tags = [l.split()[1] for l in o]
for want in ('startup', 'popupopened', 'hotspot', 'hover', 'rightclick', 'keydown', 'keyup', 'activateapp', 'appactive', 'idle', 'pageloaded', 'pagedrawn', 'enterpage', 'shutdown'):
    check('event fired: ' + want, want in tags, sorted(set(tags)))
check('event order: hover in, then hotspot, on a click', tags.index('hover') < tags.index('hotspot'))
check('event order: startup is the first event, shutdown the last', tags[0] == 'startup' and tags[-1] == 'shutdown')
o = run('page O1; advance 11000')
check('OnTimer: default period 5000 ms (api 1 not called): two ticks in 11 s', len([l for l in o if l == 'event timer']) == 2, len([l for l in o if l == 'event timer']))
o = run('page O1; api SetTimerInterval 1; advance 5200; api SetTimerInterval 0; advance 5200')
n = len([l for l in o if l == 'event timer'])
check('OnTimer: API 1 sets the period (1 s -> ~5 ticks in 5.2 s) and 0 stops it', 4 <= n <= 5, n)
o = run('page O1; scroll 420,470; clickp 740,714; key S; capture on; key SHIFT')
check('status bar event: the object under the bar centre (statusbar kukri)', any(l.startswith('event statusbar') and 'kukri' in l for l in o), o[-6:])
o = run('page O1')
check('compass event on a 360-degree page', any(l.startswith('event compass ') for l in o))
o = run('page T1', '--test-fruit', 'R4nv_1=CEblue')
o2 = run('page R4', '--test-fruit', 'R4nv_1=CEblue')
check('fruit question (event 29) for the fruit ornaments of a page', any(l.startswith('event fruit R4nv_1 CEblue') for l in o2), [l for l in o2 if 'fruit' in l][:3])
o = run('page O1; api OpenSound %s 0 0 0 0 0; api PlaySound 0 1' % wav)
check('OpenSound with notify = 0 delivers no sound events', not [l for l in o if l.startswith('event sound')])

print('api tests: %s' % ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
