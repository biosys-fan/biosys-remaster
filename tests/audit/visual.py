"""Picture stability: does the picture the player sees stay put when nothing is happening, and change only as it should when something is?

The unit tests look at one picture (one frame, one pixel) at a time. The glitches players report are about SEQUENCES of presented frames: a blue
blink while the compass is held, a wrong palette for a moment after Tab, the sides of the picture cut off while an overlay shows up. This module

  * drives headless runs of build/biosys (a scenario = a page, a mode, a list of actions) and, after every step of game time, asks the program for
    the comparison of the PRESENTED picture with the two before it (script command `overlay diff`, src/app/overlay_script.cpp). The presented
    picture is what the window shows: in the player's mode (`overlay on 3440 1440` + `overlay clean on`: --interface overlay --picture clean, the
    cleaned page texture filled band by band as the window does) the 3440 x 1440 composition, in the faithful mode the 640 x 480 frame. Nothing is
    written to disk per frame: the program prints, per frame, the mean colour, the extent, the show width, the palette hash, the held picture's
    hash and colour, the mean / maximum difference to the previous and the one before, and the same per cell of a 16 x 9 grid.
  * checks the sequence (`check_frames`), see the rules there. What may change by itself is told by the engine's state in each line: a sprite
    animation or a film playing (anim / movie), a timed palette (fade), a cross-fade (cross), the rain filter (filter), the message line and the
    inventory strip fading (msg / strip). A frame inside such a stretch is `masked` for the rule that would flag it.

Findings are Finding objects: scenario, rule, frames, measured values, a reproducing script.

Run as a script for exploration:  python3 -I tests/audit/visual.py PAGE [overlay|faithful]
The suite is tests/test_visual.py."""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
BIN = os.path.join(ROOT, 'build', 'biosys')
TMP = os.path.join(ROOT, 'build', 'test', 'visual')

DT = 33            # game ms between two presented frames (30 per second; the window presents at the display rate, 60 or more: finer, so any glitch seen here is longer there)
GX, GY = 16, 9


class Frame:
    """one `overlay diff` line"""
    __slots__ = ('ord', 'i', 'phase', 'kv', 'mean', 'cells', 'size', 'show', 'ext', 'held', 'pal', 'd1', 'd2', 'm1', 'm2', 't', 'clean', 'mix', 'bands', 'tb', 'hrgb', 'hd', 'flags', 'have1', 'have2', 'view')

    def __init__(self, i, phase, line):
        self.i, self.phase = i, phase
        kv = dict(t.split('=', 1) for t in line.split() if '=' in t and not t.startswith('grid='))
        self.kv = kv
        self.mean = tuple(float(x) for x in kv['mean'].split(','))
        self.size = kv['size']
        self.show = tuple(int(x) for x in kv['show'].split(','))
        self.ext = kv['ext']
        self.held = kv['held']
        self.pal = kv['pal']
        self.d1, self.d2, self.m1, self.m2 = float(kv['d1']), float(kv['d2']), int(kv['m1']), int(kv['m2'])
        self.t = int(kv['t'])
        self.hrgb, self.hd = tuple(float(x) for x in kv['hrgb'].split(',')), float(kv['hd'])
        self.mix = float(kv['mix'])
        self.clean, self.bands, self.tb = int(kv['clean']), int(kv['bands']), kv['tb']
        self.have1, self.have2 = kv['have1'] == '1', kv['have2'] == '1'
        self.view = kv['view'] == '1'
        self.flags = {k: kv[k] for k in ('pump', 'anim', 'movie', 'fade', 'cross', 'filter', 'status')}
        self.flags['msg'] = float(kv['msg'])
        self.flags['strip'] = float(kv['strip'])
        grid = line.split('grid=', 1)[1].split()[0]
        self.cells = []
        for c in grid.split(','):
            col, d1, d2 = c.split(':')
            self.cells.append((int(col[0:2], 16), int(col[2:4], 16), int(col[4:6], 16), int(d1) / 10.0, int(d2) / 10.0))


class Run:
    """one headless run: a list of script commands. `frames(n)` adds n presented frames, `phase(name)` names what follows."""

    def __init__(self, name, mode='player', page='R4', w=3440, h=1440, opts=(), start_ms=1500, settle_ms=500, sharp=False):
        self.name, self.mode, self.page, self.w, self.h = name, mode, page, w, h
        self.opts = list(opts)
        self.cmds = ['advance %d' % start_ms]
        if page:
            self.cmds += ['page %s' % page, 'advance %d' % settle_ms]
        if mode != 'faithful':
            self.cmds.append('overlay on %d %d' % (w, h))
            if sharp:
                self.cmds.append('overlay sharp on')
            if mode == 'player':
                self.cmds.append('overlay clean on')
        self.setup_len = len(self.cmds)
        self.frames_n = 0
        self.events = []          # (after execute) indices of the frames that follow an action of the scenario
        self.phases = {}          # (after execute) phase name -> [first frame, end frame (exclusive)]
        self.frame_pos = []       # index in cmds just after each frame's `overlay diff`

    def raw(self, *c):
        self.cmds.extend(c)
        return self

    def phase(self, name):
        self.cmds.append('echo ph %s' % name)
        return self

    def _close_phase(self):
        pass

    def act(self, *c):
        """an action of the scenario (key, click, ...): the next frame is an event frame"""
        self.cmds.append('echo ev')
        self.cmds.extend(c)
        return self

    def frame(self, dt=DT):
        if dt:
            self.cmds.append('advance %d' % dt)
        if self.mode != 'faithful':
            self.cmds.append('overlay frame')
        self.cmds.append('overlay diff')
        self.frame_pos.append(len(self.cmds))
        self.frames_n += 1
        return self

    def frames(self, n, dt=DT):
        for _ in range(n):
            self.frame(dt)
        return self

    def script(self, shots=None):
        cmds = list(self.cmds)
        for idx in sorted(shots or {}, reverse=True):
            cmds.insert(self.frame_pos[idx], ('shot %s' if self.mode == 'faithful' else 'overlay shot %s') % shots[idx])
        return '; '.join(cmds)

    def argv(self, save, shots=None):
        return [BIN, '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', save] + self.opts + ['--script', self.script(shots)]

    def repro(self):
        return 'SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/biosys --game --skip-intro --seed 3 --mute --no-config --save-dir /tmp/vis %s --script "%s"' % (' '.join(self.opts), self.script())

    def execute(self, timeout=900, shots=None):
        """-> (frames, lines, seconds); raises RuntimeError if the run fails"""
        save = os.path.join(TMP, 'save_%s' % re.sub(r'\W', '_', self.name))
        shutil.rmtree(save, ignore_errors=True)
        os.makedirs(save)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        t0 = time.time()
        r = subprocess.run(self.argv(save, shots), capture_output=True, text=True, errors='replace', env=env, timeout=timeout, cwd=ROOT)
        secs = time.time() - t0
        shutil.rmtree(save, ignore_errors=True)
        if r.returncode:
            raise RuntimeError('%s: exit %d: %s' % (self.name, r.returncode, r.stderr[-300:]))
        frames, phase, nonpump = [], '', 0
        self.events, self.phases = [], {}
        lines = r.stdout.splitlines()
        for l in lines:
            if l.startswith('ph '):
                phase = l[3:].strip()
                self.phases[phase] = [len(frames), len(frames)]
            elif l == 'ev':
                self.events.append(len(frames))
            elif l.startswith('overlay diff '):
                frames.append(Frame(len(frames), phase, l))
                if frames[-1].flags['pump'] == '0':
                    frames[-1].ord = nonpump
                    nonpump += 1
                else:
                    frames[-1].ord = -1
                if phase:
                    self.phases[phase][1] = len(frames)
        return frames, lines, secs


# ------------------------------------------------------------------------------------------------------------------------------ the rules

class Finding:
    def __init__(self, rule, frames, detail, where=''):
        self.rule, self.frames, self.detail, self.where = rule, frames, detail, where

    def __repr__(self):
        return '%s frames %s: %s' % (self.rule, self.frames, self.detail)


def l1(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


def dynamic(f):
    """the engine says something moves by itself in this frame (what must not be flagged as a glitch)"""
    fl = f.flags
    return fl['pump'] == '1' or fl['anim'] == '1' or fl['movie'] == '1' or fl['fade'] == '1' or fl['cross'] == '1' or 0 < fl['msg'] < 1 or 0 < fl['strip'] < 1


def chroma_dist(a, b):
    """how far apart two colours are in hue / saturation, brightness left out (r - g, b - g)"""
    return abs((a[0] - a[1]) - (b[0] - b[1])) + abs((a[2] - a[1]) - (b[2] - b[1]))


def median(v):
    v = sorted(v)
    return v[len(v) // 2]


def check_frames(F, still=False, turning=False, expect_geometry=False, expect_palette=False, events=(), skip_first=2, tol=1.0, roundtrip=None):
    """Findings in a frame sequence (the frames of one run, or of one phase).
    still: nothing is supposed to move (idle on a page): any change of the picture without a reason from the engine's state is a finding.
    turning: the view moves all the time: only the rules that do not depend on the picture standing still apply.
    expect_geometry: the width / size of the picture is allowed to change (a menu, a page change); excursions are still findings.
    expect_palette: the palette is allowed to change (page change, Tab); excursions are still findings.
    events: indices of frames at which the scenario did something (key, click): that frame and the next are exempt from the rules that compare with the
      frame before; everything after them is not.
    roundtrip: (a, b): the picture at frame b must be the picture at frame a (an action and its undoing, e.g. Tab down and up).
    A frame is `masked` when the engine says something runs by itself: a blocking loop (pump), a sprite animation or film, a timed palette, a cross-fade,
    the message line or the strip fading; the cleaned page texture taking over from the plain one (once, a few frames after a page is entered) is masked
    too. Windows that contain masked frames are not judged by the rules below (the rain is not masked: it moves drops, not cell colours).
    Rules (frame numbers are the index in F):
      notstill  still: the picture changes while nothing runs and no rain falls (any difference at all: the picture of a still page is still)
      drift     still: a grid cell's mean colour wanders (rain on: the drops move, the cell colours must not)
      flicker   whole picture: d1 large at n and n+1 but d2 small at n+1: A-B-A (the picture went away and came back)
      cellblip  a grid cell's mean colour leaves its value for 1..6 frames and comes back (a blue frame, a wrong palette for a moment)
      palblip   the palette in use is replaced for 1..6 frames and restored (outside a fade)
      geometry  the size / the width of the wide picture (show) changes for 1..6 frames and comes back; or changes at all in a still scenario
      heldblip  the held picture (its pixels or its colour) changes for 1..3 frames and comes back
      jump      a global mean colour step that does not come back and is not a fade, a palette change or an event
      roundtrip the picture after the action is not the picture before it"""
    out = []
    N = len(F)
    mask = [dynamic(f) or (n > 0 and (F[n].held != F[n - 1].held or F[n].clean != F[n - 1].clean or F[n].flags['msg'] != F[n - 1].flags['msg'] or F[n].flags['strip'] != F[n - 1].flags['strip'] or F[n].tb != F[n - 1].tb))
            for n, f in enumerate(F)]     # (the cleaned texture takes over from the plain one: the cleaner's catch-up, seen once)
    ev = set()
    for e in events:
        ev.update((e, e + 1))
    filt = [f.flags['filter'] == '1' for f in F]
    scale = tol
    # --- notstill / drift
    if still:
        bad = [n for n in range(skip_first, N) if not mask[n] and n not in ev and not filt[n] and F[n].have1 and F[n].d1 > 0.0005 * scale]
        if bad:
            worst = max(bad, key=lambda n: F[n].d1)
            out.append(Finding('notstill', bad[:12], '%d of %d frames differ from the previous one with nothing running; worst frame %d: mean diff %.4f max %d' % (len(bad), N, worst, F[worst].d1, F[worst].m1)))
        good = [n for n in range(skip_first, N) if not mask[n] and n not in ev and filt[n]]       # (without rain the exact rule above holds)
        if len(good) >= 4:
            for c in range(GX * GY):
                vals = [(F[n].cells[c][:3], n) for n in good if F[n].size == F[good[0]].size]
                if not vals:
                    continue
                med = tuple(median([v[0][k] for v in vals]) for k in range(3))
                worst = max(vals, key=lambda v: l1(v[0], med))
                if l1(worst[0], med) > 24 * scale:
                    out.append(Finding('drift', [worst[1]], 'cell %d,%d: colour %02x%02x%02x at frame %d, median %02x%02x%02x (L1 %d)' % (c % GX, c // GX, worst[0][0], worst[0][1], worst[0][2], worst[1], med[0], med[1], med[2], l1(worst[0], med))))
                    break
    # --- flicker (whole picture)
    for n in range(max(skip_first, 1), N - 1):
        a, b = F[n], F[n + 1]
        if not (a.have1 and b.have2) or mask[n] or mask[n + 1] or mask[n - 1] or n in ev or n + 1 in ev or turning:
            continue
        hi = 0.6 * scale
        if a.d1 > hi and b.d1 > hi and b.d2 < 0.25 * min(a.d1, b.d1):
            out.append(Finding('flicker', [n - 1, n, n + 1], 'd1(n)=%.3f d1(n+1)=%.3f d2(n+1)=%.3f' % (a.d1, b.d1, b.d2)))
    # --- excursions of cell colours, the palette, the geometry, the held picture
    K = 6
    held_vals = [f.held for f in F]
    held_cycles = sum(1 for n in range(1, N) if held_vals[n] != held_vals[n - 1]) > 10 and len(set(held_vals)) <= 3      # a held animation (the torch's flame): not a blip
    for n in range(max(skip_first, 1), N - 1):
        if n in ev:
            continue
        for m in range(1, K + 1):
            j = n + m
            if j >= N:
                break
            ref = F[n - 1]
            nomask = not any(mask[k] for k in range(n - 1, j + 1))
            # palette
            if nomask and F[n].pal != ref.pal and F[j].pal == ref.pal and all(F[k].pal != ref.pal for k in range(n, j)):
                out.append(Finding('palblip', list(range(n - 1, j + 1)), 'palette %s -> %s for %d frame(s) -> %s' % (ref.pal, F[n].pal, m, F[j].pal)))
            # geometry
            gk = (ref.size, ref.show)
            if (F[n].size, F[n].show) != gk and (F[j].size, F[j].show) == gk and all((F[k].size, F[k].show) != gk for k in range(n, j)):
                out.append(Finding('geometry', list(range(n - 1, j + 1)), 'size/show %s %s -> %s %s for %d frame(s), then back' % (ref.size, ref.show, F[n].size, F[n].show, m)))
            # held picture
            if nomask and not held_cycles and F[n].held != ref.held and F[j].held == ref.held and m <= 3 and all(F[k].held != ref.held for k in range(n, j)):
                out.append(Finding('heldblip', list(range(n - 1, j + 1)), 'held %s -> %s for %d frame(s) -> back' % (ref.held, F[n].held, m)))
            # cell colours
            if nomask and ref.size == F[n].size == F[j].size:
                for c in range(GX * GY):
                    r0 = ref.cells[c][:3]
                    dist = chroma_dist if turning else l1      # a turning view brings brighter and darker things past: only a change of hue is a blip
                    if dist(F[j].cells[c], r0) > (2 if turning else 4) * scale:
                        continue
                    ds = [dist(F[k].cells[c], r0) for k in range(n, j)]
                    thr = (60 if turning else 24) * scale
                    if min(ds) > thr:
                        out.append(Finding('cellblip', list(range(n - 1, j + 1)), 'cell %d,%d colour %s -> %s (L1 %d) for %d frame(s) and back' % (c % GX, c // GX, '%02x%02x%02x' % r0, '%02x%02x%02x' % F[n].cells[c][:3], max(ds), m)))
                        break
    # --- the held picture is fixed to the screen: its pixels change only when the held image changes (the compass needle, the torch flame) or the palette does
    bad = [n for n in range(max(skip_first, 1), N) if F[n].held != '-' and F[n].held == F[n - 1].held and F[n].pal == F[n - 1].pal and F[n].size == F[n - 1].size and not mask[n]
           and n not in ev and F[n].hd > 0.5 * scale]
    if bad:
        w = max(bad, key=lambda n: F[n].hd)
        out.append(Finding('heldpix', bad[:12], '%d frames: the held picture (same image, same palette) changes on the screen; worst frame %d: mean diff %.3f, colour %.0f,%.0f,%.0f' % (len(bad), w, F[w].hd, *F[w].hrgb)))
    # --- the cleaned page texture is refreshed band by band when the palette moves: a texture that mixes two palettes for long is a band sweeping across the picture
    run = 0
    for n in range(N):
        run = run + 1 if F[n].mix > 0 else 0
        if run == 13:
            out.append(Finding('mixed', list(range(n - 12, n + 1)), 'the cleaned page texture mixes two palettes (%.0f%% of its rows are not the current one) for more than 12 frames' % (100 * F[n].mix)))
    # --- geometry change at all in a still scenario
    if still and not expect_geometry:
        for n in range(1, N):
            if n not in ev and (F[n].size, F[n].show) != (F[n - 1].size, F[n - 1].show):
                out.append(Finding('geometry', [n - 1, n], 'size/show %s %s -> %s %s on a still page' % (F[n - 1].size, F[n - 1].show, F[n].size, F[n].show)))
    # --- global colour jump that does not return
    for n in range(max(skip_first, 1), N - 1):
        if mask[n] or mask[n - 1] or mask[n + 1] or n in ev or F[n].size != F[n - 1].size or F[n + 1].size != F[n].size or turning:
            continue
        a, b, c = F[n - 1].mean, F[n].mean, F[n + 1].mean
        if l1(a, b) > 12 * scale and l1(b, c) < 3 and not expect_palette and F[n].pal == F[n - 1].pal:
            out.append(Finding('jump', [n - 1, n, n + 1], 'mean colour %.1f,%.1f,%.1f -> %.1f,%.1f,%.1f without a palette change' % (a + b)))
    # --- after an action the picture must reach its final state at once (the frame of the action and the next are exempt): a wrong palette for a moment
    evs = sorted(set(events))
    for i, e in enumerate(evs):
        end = (evs[i + 1] if i + 1 < len(evs) else N) - 1
        while end > e + 3 and mask[end]:
            end -= 1
        if end - e < 4:
            continue
        fin = F[end]
        late = []
        for n in range(e + 2, min(end, e + 12)):
            if mask[n] or F[n].size != fin.size or any(mask[k] for k in range(e + 2, n + 1)):
                continue
            body = [c for c in range(GX * GY) if 0 < c // GX < GY - 1]
            worst = max(l1(F[n].cells[c], fin.cells[c]) for c in body)
            if worst > 40 * scale or l1(F[n].mean, fin.mean) > 8 * scale:
                late.append((n, worst))
        if late:
            out.append(Finding('unsettled', [e] + [n for n, _ in late], 'after the action at frame %d the picture is still different from its final state at frame(s) %s (worst cell L1 %d, mean colour %.1f,%.1f,%.1f vs final %.1f,%.1f,%.1f)' % (
                e, [n for n, _ in late][:8], max(w for _, w in late), *F[late[0][0]].mean, *fin.mean)))
    # --- the picture after an action is the picture before it
    if roundtrip:
        a, b = roundtrip
        if F[a].size == F[b].size and not mask[a] and not mask[b]:
            body = [c for c in range(GX * GY) if 0 < c // GX < GY - 1]        # (the status words, the message line and the strip are in the first and last rows and change with the game)
            worst = max(body, key=lambda c: l1(F[a].cells[c], F[b].cells[c]))
            d = l1(F[a].cells[worst], F[b].cells[worst])
            if d > 14 * scale or F[a].pal != F[b].pal:
                out.append(Finding('roundtrip', [a, b], 'frame %d vs %d: palette %s / %s, worst cell %d,%d colour %02x%02x%02x vs %02x%02x%02x (L1 %d)' % (a, b, F[a].pal, F[b].pal, worst % GX, worst // GX, *F[a].cells[worst][:3], *F[b].cells[worst][:3], d)))
    # de-duplicate: the same rule on overlapping frames is one finding
    out.sort(key=lambda f: (f.rule, f.frames[0]))
    merged = []
    for f in out:
        if merged and merged[-1].rule == f.rule and f.frames[0] <= merged[-1].frames[-1] + 1 and f.rule not in ('notstill', 'drift', 'roundtrip', 'unsettled'):
            merged[-1].frames = sorted(set(merged[-1].frames) | set(f.frames))
            if len(merged[-1].detail) < 300:
                merged[-1].detail += ' | ' + f.detail
        else:
            merged.append(f)
    return merged


def diverge(A, B, thr=30):
    """two runs of the same actions through two presentation paths (the cleaned page texture and the plain one): the frames must agree cell by cell
    (the cleaner removes the dither and sharpens, it does not change colours). Returns findings."""
    out = []
    bad = []
    for n in range(min(len(A), len(B))):
        a, b = A[n], B[n]
        if a.size != b.size or dynamic(a) or dynamic(b) or a.flags['filter'] == '1':
            continue
        c = max(range(GX * GY), key=lambda c: l1(a.cells[c], b.cells[c]))
        d = l1(a.cells[c], b.cells[c])
        if d > thr or a.pal != b.pal:
            bad.append((n, d, c))
    if bad:
        w = max(bad, key=lambda x: x[1])
        out.append(Finding('diverge', [x[0] for x in bad][:12], '%d frames differ between the paths; worst frame %d cell %d,%d: L1 %d' % (len(bad), w[0], w[2] % GX, w[2] // GX, w[1])))
    return out


if __name__ == '__main__':
    page = sys.argv[1] if len(sys.argv) > 1 else 'R4'
    mode = sys.argv[2] if len(sys.argv) > 2 else 'player'
    os.makedirs(TMP, exist_ok=True)
    r = Run('explore', mode=mode, page=page, w=int(sys.argv[3]) if len(sys.argv) > 3 else 3440, h=int(sys.argv[4]) if len(sys.argv) > 4 else 1440).phase('idle').frames(20)
    fr, lines, secs = r.execute()
    print('%d frames in %.1f s' % (len(fr), secs))
    for f in fr:
        print(f.i, f.phase, f.size, f.show, 'd1=%.4f d2=%.4f m1=%d' % (f.d1, f.d2, f.m1), f.flags['anim'], f.flags['filter'], f.pal, f.held)
    for x in check_frames(fr, still=True):
        print(x)
