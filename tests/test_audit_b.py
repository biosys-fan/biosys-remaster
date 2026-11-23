"""Audit packet b.
1. Event_OnGetCrossFade (dll:10021d60) hands the engine the lookup file "<data directory>\\<lookup name>" (format "%s\\%s"). The port glued the two
   without the separator, the file was never found and no page jump of the game ever did its cross-fade (door of the sublevel, lift ...).
   The page file defines "fce9cab_c" on page fce9cab_o; clicking b_close there must run the 8 steps of the fade (9 frames) before the page is entered.
2. The alarm clock (dll:10015f20) registers highgfb.ttf (family "HighlandGothicFLF") and asks for exactly that family (dll:10016230); the port
   asked for "HighlandGothicFL" and never registered the file, so the clock face was drawn in the wrong font."""
import os, re, shutil, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = 'build/test/audit_b'
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D + '/save')
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:500]))
    failed += 0 if ok else 1


def run(script):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', D + '/save', '--log-events', '--log-api',
                        '--script', script], capture_output=True, text=True, env=env)
    return (r.stdout + r.stderr).splitlines()


# 1. cross-fade (the lookup of the day palette is set after a few seconds of play)
o = run('advance 8000; page fce9cab_o; advance 100; pumptrace on; click 100,362; advance 600')
ev = [l for l in o if (l.startswith('event ') and not l.startswith('event hover')) or l.startswith('pump')]
i0 = [i for i, l in enumerate(ev) if l.startswith('event getcrossfade')]
seq = [l.split()[1] if l.startswith('event') else 'pump' for l in ev[i0[0]:]] if i0 else []
seq = [x for x in seq if x not in ('sound', 'timer')]
check('page jump with a cross-fade: getcrossfade, exitpage, then 9 fade frames, then enterpage',
      seq[:2] == ['getcrossfade', 'exitpage'] and seq.count('pump') == 9 and 'enterpage' in seq, seq)

# 2. alarm clock font
o = run('advance 3000; hold alarm; page xclock; advance 300')
reg = [l for l in o if 'AddRemoveFontResource' in l and 'highgfb.ttf' in l and 'add=1' in l]
check('the alarm clock face registers highgfb.ttf', len(reg) == 1, reg)

print('FAILED' if failed else 'ALL PASSED')
raise SystemExit(1 if failed else 0)
