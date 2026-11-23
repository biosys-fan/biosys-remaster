"""Sleep at the rainy start. In the original's sleep loop (CSelf::OnIdle, dll:10065e80) every 90-second step ends the sleep when
energy is above 20, no sleeping pill is active and it is raining at the player's cel (cel flag +0x108 bit 1, set by the rain machine's
FX) on a site without the site-flag byte +0x12 bit 7 ("sheltered": RHUT, S4BAOBAB, DB6Cave in NewGame.sav). The wake reason is 7 and
nothing is said. The port does what the decompile says: so a sleep at the start (R4, raining) ends after one step, 90 game seconds, and
a sleep in the hut (RHUT, sheltered) lasts the whole night. The expectations come from that text and from the site flags read out of
NewGame.sav by build/biosys-gametest dump."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/sleep'
SAV = (_DEV + '/setup-cd/installed/DATA1/NewGame.sav')
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:400] if detail else ''))


def game(script):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--seed', '1', '--save-dir', T, '--skip-intro', '--script', script],
                       capture_output=True, text=True, timeout=900)
    return r.stdout.splitlines()


def num(lines, field, nth):
    vals = [l.split('=', 1)[1].strip() for l in lines if l.startswith(field + ' =')]
    return float(vals[nth])


def gtime(lines, nth):
    vals = [int(re.search(r'game time=(\d+)', l).group(1)) for l in lines if l.startswith('game time=')]
    return vals[nth]


# the site flags of the data
dump = subprocess.run(['build/biosys-gametest', 'dump', SAV], capture_output=True, text=True).stdout
flags = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'^sites/([^/]+)/m_dwFlags = (0x[0-9a-fA-F]+)', dump, re.M)}
sheltered = sorted(n for n, f in flags.items() if f & 0x00800000)
check('the sheltered sites of the data are RHUT, S4BAOBAB, DB6Cave; R4 is not', sheltered == ['DB6Cave', 'RHUT', 'S4BAOBAB'] and not flags['R4'] & 0x800000, sheltered)

SLEEP = 'keydown TAB; advance 100; move 460,68; click 460,68; advance 8000'
out = game('advance 3000; game dump; game self; ' + SLEEP + '; game dump; game self')
check('at the start (R4, raining) the player can sleep: energy 63 <= 80, the sleep starts', num(out, 'self.m_dEnergy', 0) == 63.0)
check('the sleep ends with wake reason 7 (rain) after one step: 90 game seconds, energy +0.25 (one step of 0.25)',
      num(out, 'self.m_wWakeReason', 1) == 7 and abs(num(out, 'self.m_dEnergy', 1) - 63.25) < 1e-9, (num(out, 'self.m_wWakeReason', 1), num(out, 'self.m_dEnergy', 1)))
check('the player is awake again', num(out, 'self.m_bAwake', 1) == 1)
check('only that step was slept (the clock moved by the real time of the run, about 130 s, not hours)', gtime(out, 1) - gtime(out, 0) < 400, (gtime(out, 0), gtime(out, 1)))

out = game('advance 3000; type SILVERFEATHER; advance 2600; type GOTORHUT\\n; advance 4000; game dump; game self; ' + SLEEP + '; game dump; game self')
check('in the hut (sheltered, same rain outside) the sleep runs its night: wake reason 1, hours of game time, energy up by 30 or more',
      num(out, 'self.m_wWakeReason', 1) == 1 and gtime(out, 1) - gtime(out, 0) > 10800 and num(out, 'self.m_dEnergy', 1) > num(out, 'self.m_dEnergy', 0) + 30,
      (num(out, 'self.m_wWakeReason', 1), gtime(out, 0), gtime(out, 1), num(out, 'self.m_dEnergy', 0), num(out, 'self.m_dEnergy', 1)))
sys.exit(1 if failed else 0)
