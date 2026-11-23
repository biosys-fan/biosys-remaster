"""Enhanced sound path and volume groups (src/engine/audio.*, tools biosys-audioenh / biosys-wavdump).
The numbers are computed here, independently of the C++ code: a hand-written FFT and Goertzel filter, standard library only."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import array, cmath, math, os, struct, subprocess, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/audioenh'
os.makedirs(T, exist_ok=True)
TOOL = 'build/biosys-audioenh'
DIRS = [(_DEV + '/setup-cd/installed/DATA1/Biotime'), (_DEV + '/game-cd/files/BIOTIME')]
failed = 0

# ---- stated tolerances
PASSBAND_TOL_DB = 0.02          # enhanced: |gain error| over 200 Hz .. 0.8 x source Nyquist
ALIAS_MAX_DB = -75.0            # enhanced: power above 1.18 x source Nyquist, relative to the tone
ALIAS_ORIG_MIN_DB = -45.0       # sanity: the original path is audibly worse than this (so the comparison means something)
CLICK_RATIO_MAX = 1.5           # enhanced: largest step at an event / largest step in its 20 ms neighbourhoods


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ((' -- ' + str(detail)[:400]) if detail else ''))
    if not ok:
        failed += 1


def tool(*a):
    r = subprocess.run([TOOL] + list(a), capture_output=True, text=True)
    if r.returncode != 0:
        print('tool failed:', a, r.stdout[-300:], r.stderr[-300:])
        sys.exit(1)
    return r.stdout


def find(rel):
    for d in DIRS:
        cur = d
        ok = True
        for part in rel.split('/'):
            try:
                m = [n for n in os.listdir(cur) if n.lower() == part.lower()]
            except OSError:
                m = []
            if not m:
                ok = False
                break
            cur = os.path.join(cur, m[0])
        if ok:
            return cur
    raise RuntimeError('missing ' + rel)


def render(path, kind, mode, rate, out, *extra):
    tool('render', out, '--' + kind, path, '--mode', mode, '--rate', str(rate), *extra)
    a = array.array('f')
    with open(out, 'rb') as f:
        a.frombytes(f.read())
    return a[0::2], a[1::2]


def write_wav8(path, rate, samples):   # samples in -1..1
    w = wave.open(path, 'wb')
    w.setnchannels(1); w.setsampwidth(1); w.setframerate(rate)
    w.writeframes(bytes(max(0, min(255, int(round(128 + 127.5 * s)))) for s in samples))
    w.close()


def read_wav8(path):
    w = wave.open(path, 'rb')
    d = w.readframes(w.getnframes())
    return [(b - 128) / 128.0 for b in d]


# ---- hand-written spectral tools
def fft(x):
    n = len(x)
    j = 0
    x = list(x)
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    ln = 2
    while ln <= n:
        w = cmath.exp(-2j * math.pi / ln)
        for i in range(0, n, ln):
            wk = 1
            for k in range(ln // 2):
                u, v = x[i + k], x[i + k + ln // 2] * wk
                x[i + k], x[i + k + ln // 2] = u + v, u - v
                wk *= w
        ln <<= 1
    return x


def hann(n):
    return [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]


def goertzel_amp(x, f, fs):
    """Amplitude of the sinusoid at f (Hann window, Goertzel recursion)."""
    n = len(x)
    w = hann(n)
    c = 2 * math.cos(2 * math.pi * f / fs)
    s1 = s2 = 0.0
    for i in range(n):
        s0 = w[i] * x[i] + c * s1 - s2
        s2, s1 = s1, s0
    re = s1 - s2 * math.cos(2 * math.pi * f / fs)
    im = s2 * math.sin(2 * math.pi * f / fs)
    return 2 * math.hypot(re, im) / sum(w)


def alias_db(x, fs, f_tone, f_from):
    """Power at f >= f_from (up to Nyquist) relative to the tone's power (peak bin +-3), Hann-windowed 65536-point FFT."""
    n = 65536
    seg = x[int(0.3 * fs): int(0.3 * fs) + n]
    w = hann(n)
    X = fft([a * b for a, b in zip(seg, w)])
    p = [abs(v) ** 2 for v in X[:n // 2]]
    kt = int(round(f_tone * n / fs))
    tone = sum(p[kt - 3: kt + 4])
    k0 = int(math.ceil(f_from * n / fs))
    return 10 * math.log10(sum(p[k0:]) / tone)


def db(r):
    return 20 * math.log10(r)


# ---- 1. original mode, all volumes 100: identical to the existing dump facility and to itself
print('-- original mode identity')
snd = find('bs4sound/loops/rain.wav')
for gain, pan in ((1, 0), (0.37, 0.6)):
    subprocess.run(['build/biosys-wavdump', 'render', T + '/ref.wav', snd, str(gain), str(pan), '0', '0'], capture_output=True, check=True)
    ref = open(T + '/ref.wav', 'rb').read()[44:]
    tool('render', T + '/o1.i16', '--wav', snd, '--int16', '--gain', str(gain), '--pan', str(pan), '--secs', '2.4')
    tool('render', T + '/o2.i16', '--wav', snd, '--int16', '--gain', str(gain), '--pan', str(pan), '--secs', '2.4',
         '--vol', 'master=100,effects=100,movies=100,terminal=100')
    a, b = open(T + '/o1.i16', 'rb').read(), open(T + '/o2.i16', 'rb').read()
    n = min(len(ref), len(a))
    check('original, volumes untouched / all 100 / wavdump render: byte-identical (gain %g pan %g, %d bytes)' % (gain, pan, n),
          a == b and a[:n] == ref[:n] and n > 100000)

# ---- 2. resampler
print('-- resampler')
sweep_rate, f0, f1, dur = 22050, 200.0, 10000.0, 4.0
sweep = [0.5 * math.sin(2 * math.pi * (f0 * t + (f1 - f0) * t * t / (2 * dur))) for t in (i / sweep_rate for i in range(int(dur * sweep_rate)))]
write_wav8(T + '/sweep22.wav', sweep_rate, sweep)
tones = {}
for f in (5000, 8000):
    p = '%s/tone%d.wav' % (T, f)
    write_wav8(p, 22050, [0.5 * math.sin(2 * math.pi * f * i / 22050) for i in range(2 * 22050)])
    tones[f] = p
src_sweep = read_wav8(T + '/sweep22.wav')
res = {}
for rate in (44100, 48000):
    for mode in ('original', 'enhanced'):
        L, R = render(T + '/sweep22.wav', 'wav', mode, rate, T + '/sw.f32', '--gain', '1')
        check('%s %d: right channel silent (hard-left pan)' % (mode, rate), max(abs(v) for v in R) == 0.0)
        worst, worst_f, rows = 0.0, 0, []
        t = 0.2
        while True:
            fc = f0 + (f1 - f0) * t / dur
            if fc > 0.8 * 11025:
                break
            n = int(0.04 * rate)
            o = L[int(t * rate) - n // 2: int(t * rate) + n // 2]
            m = int(0.04 * sweep_rate)
            s = src_sweep[int(t * sweep_rate) - m // 2: int(t * sweep_rate) + m // 2]
            wo, ws = hann(len(o)), hann(len(s))
            eo = sum(a * a * b for a, b in zip(o, wo)) / sum(wo)
            es = sum(a * a * b for a, b in zip(s, ws)) / sum(ws)
            g = 10 * math.log10(eo / es)
            rows.append((fc, g))
            if abs(g) > abs(worst):
                worst, worst_f = g, fc
            t += 0.1
        res[(mode, rate, 'sweep')] = (worst, worst_f)
        print('   sweep 200..%d Hz, %s, %d Hz: worst passband gain error %+.4f dB at %.0f Hz' % (0.8 * 11025, mode, rate, worst, worst_f))
    check('enhanced %d: passband gain flat within +-%.2f dB up to 0.8 x source Nyquist' % (rate, PASSBAND_TOL_DB), abs(res[('enhanced', rate, 'sweep')][0]) <= PASSBAND_TOL_DB)
    check('enhanced %d: passband error at least 5x smaller than the original path (%.3f vs %.3f dB)' % (rate, res[('enhanced', rate, 'sweep')][0], res[('original', rate, 'sweep')][0]),
          abs(res[('enhanced', rate, 'sweep')][0]) * 5 <= abs(res[('original', rate, 'sweep')][0]))
    for f in (5000, 8000):
        vals = {}
        for mode in ('original', 'enhanced'):
            L, _ = render(tones[f], 'wav', mode, rate, T + '/tn.f32')
            vals[mode] = alias_db(L, rate, f, 13000)
            src = read_wav8(tones[f])
            g = db(goertzel_amp(L[int(0.3 * rate): int(0.3 * rate) + 40000], f, rate) / goertzel_amp(src[6615: 6615 + 20000], f, 22050))
            vals[mode + '_gain'] = g
        print('   tone %d Hz at %d: alias/image power above 13 kHz: original %.1f dB, enhanced %.1f dB; tone gain original %+.4f, enhanced %+.4f dB' %
              (f, rate, vals['original'], vals['enhanced'], vals['original_gain'], vals['enhanced_gain']))
        check('enhanced %d, %d Hz tone: image power above 13 kHz below %.0f dB (%.1f)' % (rate, f, ALIAS_MAX_DB, vals['enhanced']), vals['enhanced'] <= ALIAS_MAX_DB)
        check('original %d, %d Hz tone: image power above %.0f dB, i.e. the comparison is meaningful (%.1f)' % (rate, f, ALIAS_ORIG_MIN_DB, vals['original']), vals['original'] >= ALIAS_ORIG_MIN_DB)
        check('enhanced %d, %d Hz tone: at least 30 dB less image energy than the original' % (rate, f), vals['enhanced'] <= vals['original'] - 30)
        check('enhanced %d, %d Hz tone: tone gain within +-%.2f dB' % (rate, f, PASSBAND_TOL_DB), abs(vals['enhanced_gain']) <= PASSBAND_TOL_DB)

# downsampling: a 15 kHz tone at 44.1 kHz played into a 22.05 kHz mixer (above its Nyquist: must be removed, not folded to 7.05 kHz)
write_wav8(T + '/tone44_15k.wav', 44100, [0.5 * math.sin(2 * math.pi * 15000 * i / 44100) for i in range(2 * 44100)])
fold = {}
for mode in ('original', 'enhanced'):
    L, _ = render(T + '/tone44_15k.wav', 'wav', mode, 22050, T + '/dn.f32')
    fold[mode] = db(max(goertzel_amp(L[int(0.3 * 22050): int(0.3 * 22050) + 20000], 7050, 22050), 1e-9) / 0.5)
print('   15 kHz tone (44.1 kHz source) into a 22.05 kHz mixer: alias at 7.05 kHz: original %.1f dB, enhanced %.1f dB (re source amplitude)' % (fold['original'], fold['enhanced']))
check('enhanced downsampling: the 15 kHz tone does not fold back (below -60 dB)', fold['enhanced'] <= -60)
check('original downsampling does fold it back (above -10 dB)', fold['original'] >= -10)

# ---- 3. clicks on real game sounds
print('-- clicks on real sounds')
EVENTS = [(0.05, 'play'), (0.30, 'stop'), (0.40, 'resume'), (0.60, 'restart'), (0.80, 'gain=0.3'), (0.90, 'pan=0'),
          (1.00, 'master=50'), (1.10, 'mute'), (1.20, 'unmute')]
SOUNDS = [('looped ambience SAVANNAH/RAIN.WAV', 'wav', find('sounds/savannah/rain.wav'), True, 9.0),
          ('one-shot OBJECTS/SQUASH.WAV'.replace('SOBJECTS', 'OBJECTS'), 'wav', find('sounds/objects/squash.wav'), False, 2.4),
          ('movie track DEATH.AVI', 'avi', find('movies/death/death.avi'), False, 7.3)]


def steps(x, t0, t1, rate):
    a, b = max(1, int(t0 * rate)), min(len(x), int(t1 * rate))
    return max((abs(x[i] - x[i - 1]) for i in range(a, b)), default=0.0)


def ev_report(L, R, rate, t, w0, w1, nb):
    """(window max step, neighbourhood max step) over both channels"""
    win = max(steps(L, t + w0, t + w1, rate), steps(R, t + w0, t + w1, rate))
    near = 0.0
    for (a, b) in nb:
        near = max(near, steps(L, t + a, t + b, rate), steps(R, t + a, t + b, rate))
    return win, near


summary = {}
for name, kind, path, loop, secs in SOUNDS:
    info = tool('render', T + '/probe.f32', '--' + kind, path, '--secs', '0.01')
    srcframes = int(info.split('srcframes=')[1].split()[0])
    srcrate = int(info.split('srcrate=')[1].split()[0])
    for rate in (44100, 48000):
        for mode in ('original', 'enhanced'):
            ev = ','.join('%g:%s' % e for e in EVENTS)
            extra = ['--secs', str(secs), '--ev', ev, '--pan', '-1']
            if loop:
                extra.append('--loop')
            # the event gains/pans move the hard-left voice into the right channel as well: measure both
            L, R = render(path, kind, mode, rate, T + '/ck.f32', *extra)
            restart_t = int(0.60 * rate) / rate
            cases = []
            for t, n in EVENTS:
                cases.append((n, t, -0.001, 0.008, [(-0.025, -0.005), (0.012, 0.032)]))
            if loop:
                k = 1
                while restart_t + k * srcframes / srcrate < secs - 0.05:
                    cases.append(('loop point %d' % k, restart_t + k * srcframes / srcrate, -0.001, 0.001, [(-0.025, -0.005), (0.005, 0.025)]))
                    k += 1
            else:
                cases.append(('natural end', restart_t + srcframes / srcrate, -0.006, 0.004, [(-0.030, -0.010)]))
            worst = 0.0
            lines = []
            for n, t, w0, w1, nb in cases:
                win, near = ev_report(L, R, rate, t, w0, w1, nb)
                if near < 0.002:            # nothing audible next to the event (a quiet part of the sound): no meaningful ratio
                    lines.append('%s: skipped (quiet neighbourhood)' % n)
                    continue
                ratio = win / near
                worst = max(worst, ratio)
                lines.append('%s: step %.4f / neighbourhood %.4f = %.2f' % (n, win, near, ratio))
            summary[(name, rate, mode)] = worst
            if rate == 48000:
                print('   %s, %s, %d Hz: worst ratio %.2f' % (name, mode, rate, worst))
                if mode == 'enhanced':
                    for l in lines:
                        print('        ' + l)
        e, o = summary[(name, rate, 'enhanced')], summary[(name, rate, 'original')]
        check('%s at %d Hz: enhanced largest step at start/stop/pause/restart/gain/pan/volume/mute/%s <= %.1f x its neighbourhood (%.2f; original %.2f)' %
              (name, rate, 'loop points and end' if loop else 'end', CLICK_RATIO_MAX, e, o), e <= CLICK_RATIO_MAX)
    if 'SQUASH' not in name:   # a percussive noise burst: its own steps are as large as any click, the ratio says nothing about the original
        check('%s: the original path has clicks the enhanced path does not (ratio original > 1.5 x enhanced at 48000)' % name,
              summary[(name, 48000, 'original')] > 1.5 * summary[(name, 48000, 'enhanced')])

# ---- 4. volume groups through the command-line tool (rendered real sound)
print('-- volume groups')
squash = find('sounds/objects/squash.wav')
base, _ = render(squash, 'wav', 'original', 44100, T + '/v0.f32', '--group', 'effects')
for grp, other in (('effects', 'movies'), ('movies', 'effects'), ('terminal', 'effects')):
    half, _ = render(squash, 'wav', 'original', 44100, T + '/v1.f32', '--group', grp, '--vol', grp + '=50')
    err = max(abs(a * 0.5 - b) for a, b in zip(base, half))
    check('original: group %s at 50 halves a voice of that group (max error %.2g, one int16 LSB is 3.1e-5)' % (grp, err), err <= 2 / 32768.0)
    keep, _ = render(squash, 'wav', 'original', 44100, T + '/v2.f32', '--group', grp, '--vol', other + '=0,master=100')
    check('original: group %s is untouched by the %s volume' % (grp, other), keep == base)
off, _ = render(squash, 'wav', 'original', 44100, T + '/v3.f32', '--vol', 'master=0')
check('original: master 0 is silence', max(abs(v) for v in off) == 0.0)
mu, _ = render(squash, 'wav', 'enhanced', 44100, T + '/v4.f32', '--ev', '0.5:mute')
check('enhanced: mute silences everything after a ramp of one block', max(abs(v) for v in mu[int(0.5 * 44100) + 600:]) == 0.0 and max(abs(v) for v in mu[:int(0.5 * 44100)]) > 0.01)

# ---- 5. C++ self test (groups, mute, saturation, key translation)
print('-- C++ self test')
r = subprocess.run([TOOL, 'selftest'], capture_output=True, text=True)
for l in r.stdout.splitlines():
    if l.startswith('FAIL'):
        print(l)
check('biosys-audioenh selftest: %d checks' % sum(1 for l in r.stdout.splitlines() if l.startswith('PASS')),
      r.returncode == 0 and 'AUDIOENH SELFTEST OK' in r.stdout and sum(1 for l in r.stdout.splitlines() if l.startswith('PASS')) >= 40)

# ---- 6. options of the window / the script runner, docs
print('-- options and docs')
h = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
check('--help lists --audio, --volume, --volume-effects, --volume-movies, --volume-terminal', all(o in h for o in ('--audio', '--volume N', '--volume-effects', '--volume-movies', '--volume-terminal')))
r = subprocess.run(['build/biosys', '--game', '--mute', '--audio', 'enhanced', '--volume', '60', '--volume-effects', '0', '--volume-movies', '100', '--volume-terminal', '5', '--script', 'echo ok'], capture_output=True, text=True)
check('options are accepted next to a script', r.returncode == 0 and 'ok' in r.stdout, r.stderr)
for bad in (['--audio', 'loud'], ['--volume', '101'], ['--volume', '-1'], ['--volume-movies', 'x'], ['--volume-effects', '5.5']):
    r = subprocess.run(['build/biosys', '--game', '--mute'] + bad + ['--script', 'echo ok'], capture_output=True, text=True)
    check('bad option %s is refused' % ' '.join(bad), r.returncode != 0 and 'ok' not in r.stdout)
manual = open('MANUAL.md').read()
check('MANUAL.md documents the options and the keys', all(s in manual for s in ('--audio', '--volume-movies', '--volume-effects', '--volume-terminal')))
check('src/app/README.md has the sound note', 'enhanced' in open('src/app/README.md').read())

# ---- 7. CPU
print('-- cost')
out = subprocess.run([TOOL, 'bench'], capture_output=True, text=True).stdout
print(out.rstrip())
vals = {(l.split()[1], int(l.split()[2].split('=')[1])): float(l.split('=')[-1]) for l in out.splitlines() if l.startswith('bench')}
check('enhanced mixer with 40 looping voices costs less than 0.5 s of CPU per second of audio (%.3f)' % vals[('enhanced', 40)], vals[('enhanced', 40)] < 0.5)

print('ALL AUDIOENH CHECKS PASSED' if not failed else '%d AUDIOENH CHECK(S) FAILED' % failed)
sys.exit(1 if failed else 0)
