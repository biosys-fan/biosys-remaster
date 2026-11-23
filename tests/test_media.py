"""Tests for src/engine/media.* through build/biosys-mediatest (script driver) and the decoder tools."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import math, os, re, struct, subprocess, sys

PORT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = os.path.join(PORT, 'build', 'test')
os.makedirs(T, exist_ok=True)
ROOTS = [(_DEV + '/setup-cd/installed/DATA1'), (_DEV + '/game-cd/files')]
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + name + (' ' + str(detail) if (detail and not ok) else ''))
    if not ok:
        fails += 1


def find_time(rel):
    parts = rel.replace('\\', '/').split('/')
    for root in ROOTS:
        cur = None
        for d in os.listdir(root):
            if d.lower() == 'biotime':
                cur = os.path.join(root, d)
        ok = cur is not None
        for p in parts if ok else []:
            m = [e for e in os.listdir(cur) if e.lower() == p.lower()]
            if not m:
                ok = False
                break
            cur = os.path.join(cur, m[0])
        if ok:
            return cur
    raise RuntimeError('missing ' + rel)


def run(script):
    r = subprocess.run([os.path.join(PORT, 'build/biosys-mediatest')], input=script, capture_output=True, text=True, cwd=PORT)
    if r.returncode != 0:
        print('mediatest failed:', r.stdout[-400:], r.stderr[-400:])
    return r.stdout.splitlines()


def tag(lines, prefix):
    return [l for l in lines if l.startswith(prefix)]


def kvs(line):
    return dict(x.split('=') for x in line.split()[1:] if '=' in x)


def read_bmp(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    stride = (w + 3) & ~3
    rows = [list(d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w]) for y in range(h)]
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    return w, h, rows, pal


def tan_crcs(path):
    out = subprocess.run([os.path.join(PORT, 'build/biosys-tandump'), 'info', path], capture_output=True, text=True).stdout
    return [m.group(1) for m in re.finditer(r'^frame \d+ off=\S+ crc=([0-9a-f]+)', out, re.M)]


def pattern(x, y):
    return (x * 7 + y * 13) % 251 + 1


def region(rows, x, y, w, h):
    return [r[x:x + w] for r in rows[y:y + h]]


# ------------------------------------------------------------------------------------ sprites
OPQ = 'anims\\synth\\r4d.tan'
KEY = 'anims\\insects\\grub_3.tan'
opq_path = find_time(OPQ)
key_path = find_time(KEY)
crcs = tan_crcs(opq_path)
n = len(crcs)
W, H = 193, 156


def order(mode):
    if mode == 1: return list(range(n))
    if mode == 2: return list(range(n - 1, -1, -1))
    if mode == 3: return list(range(n)) + list(range(n - 2, -1, -1))
    return list(range(n - 1, -1, -1)) + list(range(1, n))


for mode in (1, 2, 3, 4):
    seq = order(mode)
    s = ['page 400 300 pattern', 'pal tan ' + OPQ, 'crc page 0 0 400 300',
         'sprite.open file=%s x=10 y=20 mode=%d delay=50 rel=1' % (OPQ, mode), 'sprite.play 0 1']
    for _ in seq:
        s += ['update', 'paint', 'crc page 10 20 %d %d' % (W, H), 'adv 50']
    s += ['update', 'paint', 'sprite.info 0', 'crc page 0 0 400 300', 'events', 'sprite.close 0', 'events']
    out = run('\n'.join(s) + '\n')
    cr = [l.split()[1] for l in tag(out, 'crc ')]
    bg = cr[0]
    got = cr[1:1 + len(seq)]
    check('sprite mode %d frame order (%d steps)' % (mode, len(seq)), got == [crcs[i] for i in seq])
    info = kvs(tag(out, 'sprite.info')[0])
    check('sprite mode %d finishes after last delay and restores the page' % mode,
          info['playing'] == '0' and cr[-1] == bg)
    ev = [(l.split()[1], kvs(l)['code']) for l in tag(out, 'event ')]
    check('sprite mode %d events 1,2,3 then close gives 4' % mode, [e[1] for e in ev] == ['1', '2', '3', '4'], ev)

# delay: no early change, one step per update, no catch-up
out = run('\n'.join(['page 400 300 0', 'sprite.open file=%s x=0 y=0 delay=100 rel=1' % OPQ, 'clock 5000', 'sprite.play 0 1',
                     'update', 'sprite.info 0', 'adv 99', 'update', 'sprite.info 0', 'adv 1', 'update', 'sprite.info 0',
                     'adv 5000', 'update', 'sprite.info 0', 'adv 99', 'update', 'sprite.info 0', 'adv 1', 'update',
                     'sprite.info 0']) + '\n')
fr = [int(kvs(l)['frame']) for l in tag(out, 'sprite.info')]
check('sprite delay honoured, no catch-up', fr == [0, 0, 1, 2, 2, 3], fr)

KEY2 = 'anims\\clubeden\\ce15font.tan'
key2_path = find_time(KEY2)
# colour key + save-under
kcr = tan_crcs(find_time(KEY))
tp = subprocess.run([os.path.join(PORT, 'build/biosys-tandump'), 'export', key2_path, '0', os.path.join(T, 'k0.bmp')])
kw, kh, krows, kpal = read_bmp(os.path.join(T, 'k0.bmp'))
keyidx = kpal.index((0, 255, 255))
out = run('\n'.join(['page 500 400 pattern', 'pal tan ' + KEY2, 'crc page 0 0 500 400',
                     'sprite.open file=%s x=20 y=30 delay=10 rel=1 erase=1' % KEY2, 'sprite.play 0 1', 'update', 'paint',
                     'bmp page %s/kp.bmp' % T, 'sprite.stop 0', 'paint', 'crc page 0 0 500 400']) + '\n')
cr = [l.split()[1] for l in tag(out, 'crc ')]
_, _, prow, _ = read_bmp(os.path.join(T, 'kp.bmp'))
exp_ok, trans = True, 0
for y in range(kh):
    for x in range(kw):
        v = krows[y][x]
        e = pattern(20 + x, 30 + y) if v == keyidx else v
        trans += v == keyidx
        exp_ok &= prow[30 + y][20 + x] == e
check('colour-keyed sprite: transparent pixels keep the background (%d keyed px)' % trans, exp_ok and trans > 0)
check('colour-keyed sprite: stop restores save-under exactly', cr[0] == cr[1])

# keep painted
out = run('\n'.join(['page 500 400 pattern', 'pal tan ' + KEY2, 'crc page 0 0 500 400',
                     'sprite.open file=%s x=20 y=30 delay=10 rel=1 keep=1' % KEY2, 'sprite.play 0 1', 'update', 'paint',
                     'crc page 0 0 500 400', 'sprite.stop 0', 'paint', 'crc page 0 0 500 400']) + '\n')
cr = [l.split()[1] for l in tag(out, 'crc ')]
check('keep-painted: last frame stays after stop', cr[1] == cr[2] and cr[0] != cr[1])

# loop + time limit
s = ['sprite.open file=%s x=0 y=0 delay=100 loop=1 max=450 window=-1' % KEY, 'clock 1000', 'sprite.play 0 1']
for i in range(5):
    s += ['update', 'sprite.info 0', 'adv 100']
s += ['adv -50' if False else 'adv 0', 'update', 'sprite.info 0']
out = run('\n'.join(['page 100 100 0'] + s) + '\n')
pl = [kvs(l)['playing'] for l in tag(out, 'sprite.info')]
fr = [kvs(l)['frame'] for l in tag(out, 'sprite.info')]
check('loop with time limit: running at 400 ms, stopped at 500 (limit 450)', pl == ['1'] * 5 + ['0'], pl)
check('loop: frames advance while looping', fr[:5] == ['0', '1', '2', '3', '4'], fr)
# loop wraps
nk = len(tan_crcs(find_time(KEY)))
s = ['page 100 100 0', 'sprite.open file=%s x=0 y=0 delay=10 loop=1' % KEY, 'sprite.play 0 1']
for i in range(nk + 2):
    s += ['update', 'sprite.info 0', 'adv 10']
fr = [int(kvs(l)['frame']) for l in tag(run('\n'.join(s) + '\n'), 'sprite.info')]
check('loop wraps to frame 0', fr[nk - 1] == nk - 1 and fr[nk] == 0 and fr[nk + 1] == 1, fr)

# slot limit and eviction (fixed mode)
s = ['page 100 100 0'] + ['sprite.open file=%s x=0 y=0' % KEY] * 11
op = tag(run('\n'.join(s) + '\n'), 'sprite.open')
check('11th sprite open fails with 0x11 (10 locked)', [kvs(l)['rc'] for l in op] == ['0'] * 10 + ['17'])
s = ['page 100 100 0'] + ['sprite.open file=%s x=0 y=0 free=1' % KEY] * 11
op = tag(run('\n'.join(s) + '\n'), 'sprite.open')
check('11th sprite open evicts an unlocked idle animation', [kvs(l)['rc'] for l in op] == ['0'] * 11)
s = ['page 100 100 0', 'sprite.open file=%s x=0 y=0' % KEY, 'sprite.close 0', 'sprite.close 0', 'sprite.play 0 1']
out = run('\n'.join(s) + '\n')
check('sprite close frees; second close / play give 0x13', [l for l in out if l.startswith('rc=')] == ['rc=0', 'rc=19', 'rc=19'])

# fixed drawing at a view origin, with attached wav and page change
out = run('\n'.join(['page 100 100 0', 'sprite.open file=%s x=10 y=20 delay=10 wav=sounds\\house\\pan3.wav' % OPQ,
                     'sprite.sound 0', 'sprite.play 0 1', 'update', 'frame clear 0', 'draw 100 50', 'crc frame 110 70 %d %d' % (W, H),
                     'sprite.freeall', 'sound.count', 'events']) + '\n')
check('fixed sprite drawn at view origin + (x,y), exact frame 0', tag(out, 'crc ')[0].split()[1] == crcs[0])
check('attached wav gets a sound handle and is closed with the animation',
      int(tag(out, 'sprite.sound')[0].split()[1]) >= 0 and tag(out, 'sound.count')[0].split()[1] == '0')
evs = [kvs(l)['code'] for l in tag(out, 'event sprite')]
check('page change frees animations with events 3,4', evs[-2:] == ['3', '4'], evs)

# ------------------------------------------------------------------------------------ movies
MV = 'movies\\rforest\\r1_mist.avi'
mv_path = find_time(MV)
AV = os.path.join(PORT, 'build/biosys-avidump')


def avi_frame(path, k):
    f = os.path.join(T, 'avi_%d.bmp' % k)
    subprocess.run([AV, 'export', path, str(k), f], check=True)
    return read_bmp(f)[2]


US = int(run('avi.us ' + MV + '\n')[0].split()[1])


def due(k, us=US):
    return (k * us + 999) // 1000


info = subprocess.run([AV, 'info', mv_path], capture_output=True, text=True).stdout
mw, mh = [int(v) for v in re.search(r'(\d+)x(\d+)', info).groups()]
nf = int(re.search(r'frames=(\d+)', info).group(1))
s = ['page 400 300 pattern', 'pal gray', 'crc page 0 0 400 300', 'movie.open file=%s x=5 y=7' % MV, 'clock 1000', 'movie.play 0 1']
ks = list(range(nf))
for k in ks:
    s += ['clock %d' % (1000 + due(k)), 'update', 'paint', 'bmp page %s/mp%d.bmp' % (T, k), 'movie.info 0']
s += ['clock %d' % (1000 + due(nf) - 1), 'update', 'movie.info 0', 'clock %d' % (1000 + due(nf)), 'update', 'movie.info 0', 'events']
out = run('\n'.join(s) + '\n')
good = True
for k in ks:
    _, _, rows, _ = read_bmp(os.path.join(T, 'mp%d.bmp' % k))
    good &= region(rows, 5, 7, mw, mh) == avi_frame(mv_path, k)
check('movie: frame k exactly at k * frame period for all %d frames' % nf, good)
infos = [kvs(l) for l in tag(out, 'movie.info')]
check('movie: frame index follows the clock', [int(i['frame']) for i in infos[:nf]] == ks)
check('movie: still playing until one period after the last frame, then stops',
      infos[nf]['playing'] == '1' and infos[nf + 1]['playing'] == '0', infos[-2:])
codes = [kvs(l)['code'] for l in tag(out, 'event movie')]
check('movie events: play, one per frame, stop', codes == ['2'] + ['5'] * nf + ['3'], codes[:3] + codes[-2:])
# early: one ms before due shows previous frame
out = run('\n'.join(['page 400 300 0', 'pal gray', 'movie.open file=%s x=0 y=0' % MV, 'clock 0', 'movie.play 0 1', 'update',
                     'clock %d' % (due(1) - 1), 'update', 'movie.info 0', 'clock %d' % 5000, 'update', 'movie.info 0']) + '\n')
fr = [int(kvs(l)['frame']) for l in tag(out, 'movie.info')]
check('movie: no early frame, no frame dropping (one frame per update)', fr == [0, 1], fr)

# looping
s = ['page 400 300 0', 'pal gray', 'movie.open file=%s x=0 y=0 loop=1' % MV, 'clock 0', 'movie.play 0 1']
for t in [due(k) for k in range(nf + 2)]:
    s += ['clock %d' % t, 'update', 'movie.info 0']
fr = [int(kvs(l)['frame']) for l in tag(run('\n'.join(s) + '\n'), 'movie.info')]
check('movie loop wraps to frame 0 and keeps playing', fr[nf - 1] == nf - 1 and fr[nf] == 0 and fr[nf + 1] == 1, fr[nf - 2:])

# restore on stop / close, save background
for how in ('stop', 'close'):
    s = ['page 400 300 pattern', 'pal gray', 'crc page 0 0 400 300', 'movie.open file=%s x=5 y=7' % MV, 'movie.savebg 0',
         'movie.restore 0 %d %d' % (how == 'stop', how == 'close'), 'clock 0', 'movie.play 0 1', 'update', 'paint',
         'crc page 0 0 400 300', 'movie.%s 0' % how, 'paint', 'crc page 0 0 400 300']
    cr = [l.split()[1] for l in tag(run('\n'.join(s) + '\n'), 'crc ')]
    check('movie %s with restore option puts the background back' % how, cr[0] == cr[2] and cr[0] != cr[1])
s = ['page 400 300 pattern', 'pal gray', 'crc page 0 0 400 300', 'movie.open file=%s x=5 y=7' % MV, 'clock 0', 'movie.play 0 1',
     'update', 'paint', 'crc page 0 0 400 300', 'movie.stop 0', 'paint', 'crc page 0 0 400 300']
cr = [l.split()[1] for l in tag(run('\n'.join(s) + '\n'), 'crc ')]
check('movie stop without restore option leaves the last frame', cr[1] == cr[2] and cr[0] != cr[1])

# colour key (palette index 0 = black)
s = ['page 400 300 pattern', 'pal gray', 'movie.open file=%s x=5 y=7' % MV, 'movie.key 0 0 0 0', 'clock 0', 'movie.play 0 1',
     'update', 'paint', 'bmp page %s/mk.bmp' % T]
run('\n'.join(s) + '\n')
_, _, rows, _ = read_bmp(os.path.join(T, 'mk.bmp'))
f0 = avi_frame(mv_path, 0)
exp = [[pattern(5 + x, 7 + y) if f0[y][x] == 0 else f0[y][x] for x in range(mw)] for y in range(mh)]
nz = sum(v == 0 for r in f0 for v in r)
check('movie colour key leaves background where the frame has the key (%d keyed px)' % nz, region(rows, 5, 7, mw, mh) == exp and nz > 0)

# line doubling
run('\n'.join(['page 400 600 pattern', 'pal gray', 'movie.open file=%s x=5 y=7' % MV, 'movie.double 0', 'clock 0', 'movie.play 0 1',
               'update', 'paint', 'bmp page %s/md.bmp' % T]) + '\n')
_, _, rows, _ = read_bmp(os.path.join(T, 'md.bmp'))
ok = all(rows[7 + 2 * y][5:5 + mw] == f0[y] and rows[8 + 2 * y][5:5 + mw] == [0] * mw for y in range(mh))
check('movie line doubling: every second row is 0', ok)

# option preconditions, limits, play-fit failure, bind
s = ['page 400 300 0', 'pal gray', 'movie.open file=%s x=0 y=0' % MV, 'clock 0', 'movie.play 0 1', 'movie.double 0', 'movie.savebg 0',
     'movie.synth 0', 'movie.bind 0 x', 'movie.vol 0 -600', 'movie.vol 0 100']
out = run('\n'.join(s) + '\n')
check('movie options after play fail with 0x11; volume on a silent movie is a no-op',
      [l for l in out if l.startswith('rc=')][1:] == ['rc=17'] * 4 + ['rc=0', 'rc=17'], out)
s = ['page 400 300 0', 'pal gray'] + ['movie.open file=%s x=0 y=0' % MV] * 5
rc = [kvs(l)['rc'] for l in tag(run('\n'.join(s) + '\n'), 'movie.open')]
check('5th movie open fails with 0x11 (4 slots)', rc == ['0'] * 4 + ['17'], rc)
out = run('\n'.join(['page 300 200 0', 'pal gray', 'movie.open file=%s x=0 y=0' % MV, 'movie.play 0 1', 'movie.info 0', 'events']) + '\n')
check('movie that does not fit the page: play fails 0x11 and destroys it',
      'rc=17' in out and tag(out, 'movie.info')[0].split()[-1] == 'count=0' and 'event movie h=0 code=4 arg=0' in out, out)
s = ['page 400 300 0', 'pal gray', 'movie.open file=%s x=0 y=0' % MV, 'movie.open file=%s x=0 y=0' % MV, 'movie.bind 1 here',
     'clock 0', 'movie.play 0 1', 'movie.play 1 1', 'pagechange here', 'movie.info 0', 'movie.info 1', 'events']
out = run('\n'.join(s) + '\n')
check('page change: unbound movie destroyed, bound movie survives',
      'event movie h=0 code=4 arg=0' in out and 'event movie h=1 code=4 arg=0' not in out
      and tag(out, 'movie.info')[1].split()[-1] == 'count=1', out)

# movie with audio
AVA = 'movies\\pod\\pod.avi'
ap = find_time(AVA)
ai = subprocess.run([AV, 'info', ap], capture_output=True, text=True).stdout
asamples = int(re.search(r'samples=(\d+)', ai).group(1))
arate = int(re.search(r'(\d+)Hz', ai).group(1))
base = ['page 700 300 0', 'pal gray', 'movie.open file=%s x=0 y=0' % AVA]
out = run('\n'.join(base + ['clock 0', 'movie.play 0 1', 'movie.info 0', 'mixuntil movie 0 30']) + '\n')
mu = kvs(tag(out, 'mixuntil')[0])
expect = asamples * 44100 / arate
check('movie audio duration matches (got %s, expected %.0f frames, one block tolerance)' % (mu['frames'], expect),
      0 <= int(mu['frames']) - expect <= 512 and int(mu['peakL']) > 100)
pk = {}
for v in (0, -600):
    out = run('\n'.join(base + ['movie.vol 0 %d' % v, 'clock 0', 'movie.play 0 1', 'mix 0.5']) + '\n')
    pk[v] = int(kvs(tag(out, 'mix ')[0])['peakL'])
check('movie volume -600 -> gain 0.501 (%d/%d)' % (pk[-600], pk[0]), abs(pk[-600] / pk[0] - 0.501) < 0.01)
out = run('\n'.join(base + ['clock 0', 'movie.play 0 1', 'mix 0.2', 'movie.stop 0', 'movie.info 0', 'mix 0.2']) + '\n')
check('movie stop silences the audio', tag(out, 'mix ')[1].endswith('peakL=0 peakR=0') and not tag(out, 'mix ')[0].endswith('peakL=0 peakR=0'))

# ------------------------------------------------------------------------------------ sounds
SW = 'sounds\\house\\pan3.wav'


def peaks(open_extra='', pre=(), seconds=0.1):
    out = run('\n'.join(['sound.open file=%s %s' % (SW, open_extra)] + list(pre) + ['sound.play 0 1', 'mix %s' % seconds]) + '\n')
    k = kvs(tag(out, 'mix ')[0])
    return int(k['peakL']), int(k['peakR'])


p0 = peaks()
p6 = peaks('vol=-600')
check('sound volume -600 -> gain 0.501 (%d/%d)' % (p6[0], p0[0]), abs(p6[0] / p0[0] - 0.501) < 0.005)
p6b = peaks('', ['sound.prop 0 volume -600'])
check('sound volume property -600 -> gain 0.501', abs(p6b[0] / p0[0] - 0.501) < 0.005)
ph = peaks('pan=-10000')
check('pan hard left silences the right channel', ph[1] == 0 and ph[0] > 0, ph)
phr = peaks('', ['sound.prop 0 pan 10000'])
check('pan hard right silences the left channel', phr[0] == 0 and phr[1] > 0, phr)
ph = peaks('pan=-1000')
check('pan -1000 (-10 dB): right channel at 0.316 of left', abs(ph[1] / ph[0] - 10 ** -0.5) < 0.01, ph)
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.prop 0 volume 5', 'sound.prop 0 bogus 1', 'sound.prop 0 FREQUENCY 50',
                     'sound.prop 9 pan 0', 'sound.prop 0 Frequency 11025']) + '\n')
check('property errors: range 0x11, unknown name 0xf, bad handle 0x10, case-insensitive',
      [l for l in out if l.startswith('rc=')] == ['rc=17', 'rc=15', 'rc=17', 'rc=16', 'rc=0'], out)

# loop / end / events
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.play 0 1', 'mix 0.6', 'update', 'sound.info 0', 'update', 'events']) + '\n')
ev = [kvs(l)['code'] for l in tag(out, 'event sound')]
check('finished non-looping sound: stops, event 3 exactly once', tag(out, 'sound.info')[0].endswith('playing=0') and ev == ['1', '2', '3'], ev)
out = run('\n'.join(['sound.open file=%s loop=1' % SW, 'sound.play 0 1', 'mix 1.5', 'update', 'sound.info 0']) + '\n')
check('sound "loop" open parameter loops (still playing after 1.5 s of a 0.31 s sound)', tag(out, 'sound.info')[0].endswith('playing=1'))
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.prop 0 loop 1', 'sound.play 0 1', 'mix 1.5', 'update', 'sound.info 0',
                     'sound.prop 0 loop 0', 'mix 1.5', 'update', 'sound.info 0']) + '\n')
check('sound "loop" property on/off', [l.split()[-1] for l in tag(out, 'sound.info')] == ['playing=1', 'playing=0'])
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.play 0 1', 'mix 0.1', 'sound.stop 0', 'mix 0.1', 'sound.play 0 0', 'mix 0.1',
                     'sound.info 0']) + '\n')
check('stop keeps position, play(0) resumes', tag(out, 'sound.info')[0].endswith('playing=1') and tag(out, 'mix ')[1].endswith('peakL=0 peakR=0'))
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.open file=%s' % SW, 'sound.close 0', 'sound.close 0', 'sound.count']) + '\n')
check('close frees once, second close 0x10', [l for l in out if l.startswith('rc=')] == ['rc=0', 'rc=16'] and 'sound.count 1' in out)
out = run('\n'.join(['sound.open file=%s' % SW] * 41 + ['sound.count']) + '\n')
rc = [kvs(l)['rc'] for l in tag(out, 'sound.open')]
check('41st sound open fails with 0x11', rc == ['0'] * 40 + ['17'], rc[-3:])
out = run('\n'.join(['sound.open file=sounds\\nothere.wav', 'sound.open file=%s freq=50' % SW]) + '\n')
check('missing file / bad frequency -> 0x11', [kvs(l)['rc'] for l in tag(out, 'sound.open')] == ['17', '17'])

# playwave + page scope
out = run('\n'.join(['sound.playwave %s 1' % SW, 'sound.count', 'pagechange x', 'sound.count',
                     'sound.playwave %s 0' % SW, 'sound.playwave %s 0' % SW, 'sound.playwave %s 4' % SW, 'sound.count',
                     'pagechange x', 'sound.count', 'mix 0.6', 'update', 'sound.count', 'events']) + '\n')
cnt = [l.split()[1] for l in tag(out, 'sound.count')]
hs = [kvs(l)['h'] for l in tag(out, 'sound.playwave')]
check('verb-4 page-scoped sound closed by the page-change call; others survive', cnt[:4] == ['1', '0', '2', '2'], cnt)
check('verb-4 same path is reused unless flag 4 forces a new instance', hs[1] == hs[2] and hs[3] != hs[2], hs)
check('verb-4 sounds auto-close after finishing', cnt[-1] == '0', cnt)
check('verb-4 sounds queue no events', not tag(out, 'event sound'), out)

# pan to page position
W_, vx, x_ = 2560, 400, 1000
q = W_ >> 2
d = (x_ - 320) - vx
pan = int(1400 / q * d)
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.view %d %d' % (vx, W_), 'sound.pan 0 1 %d 1' % x_, 'sound.play 0 1', 'mix 0.1',
                     'sound.pan 0 1 %d 0' % x_]) + '\n')
k = kvs(tag(out, 'mix ')[0])
check('pan-to-page: source right of view centre pans right by DirectSound law (pan %d)' % pan,
      abs(int(k['peakL']) / int(k['peakR']) - 10 ** (-pan / 2000)) < 0.01, k)
check('pan-to-page on a non-360 page returns 0x14', 'rc=20' in out)
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.view 0 %d' % W_, 'sound.pan 0 1 2500 1', 'sound.play 0 1', 'mix 0.1']) + '\n')
k = kvs(tag(out, 'mix ')[0])
check('pan-to-page wraps the short way round the 360 seam (source just left of the seam pans left)', int(k['peakL']) > int(k['peakR']), k)
out = run('\n'.join(['sound.open file=%s' % SW, 'sound.play 0 1', 'mix 0.1', 'sound.pause', 'mix 0.1', 'sound.info 0', 'sound.resume 1',
                     'mix 0.1', 'events']) + '\n')
mx = tag(out, 'mix ')
check('pause silences, resume continues (event 2 on notify)', mx[1].endswith('peakL=0 peakR=0') and not mx[2].endswith('peakL=0 peakR=0')
      and [kvs(l)['code'] for l in tag(out, 'event sound')] == ['1', '2', '2'])

print('ALL MEDIA CHECKS PASSED' if fails == 0 else '%d MEDIA CHECK(S) FAILED' % fails)
sys.exit(1 if fails else 0)
