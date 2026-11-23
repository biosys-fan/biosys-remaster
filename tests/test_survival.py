"""Time of death of an idle player (cause 12, dehydration), game seconds since the start, `--start` values of the difficulty.
The tool measures it in 90 s steps; the live game is sampled every 600 s with `game story` (dead=1), so its death lies in the 600 s before
the first sample that says dead. Original and survivor die before the player's first night sleep is long enough to matter: the two
agree to a few hundred seconds. On guided the player passes out twice into rain-cut naps and once into a night of 3 to 6 hours whose length
is random (Rng draws differ between the tool and the game), so the five seeds of each give a range and the two ranges must overlap and
lie around 5 days (before the collapse and rain flag were modelled the tool said 3.6 days)."""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:300] if detail else ''))


def tool(diff, seed, realism='faithful'):
    r = subprocess.run(['build/biosys-settingsmeasure', '--difficulty', diff, '--realism', realism, '--start', '--days', '8', '--seed', str(seed)],
                       capture_output=True, text=True)
    rows = [l.split('\t') for l in r.stdout.splitlines()]
    h = rows[0]
    for row in rows[1:]:
        if row[h.index('player.alive')] == '0':
            assert row[h.index('player.deathCause')] == '12'
            return int(row[h.index('player.deathTime')])
    return None


def live(diff, seed):
    steps = 760
    s = 'advance 1000; game dump; game story'
    for i in range(steps):
        s += '; advance 24000; game story; game dump'
    r = subprocess.run(['build/biosys', '--game', '--mute', '--skip-intro', '--difficulty', diff, '--seed', str(seed), '--script', s], capture_output=True, text=True)
    times = [int(m.group(1)) for m in re.finditer(r'game time=(\d+)', r.stdout)]
    deads = [m.group(1) for m in re.finditer(r' dead=(\d)', r.stdout)]
    for i in range(1, len(deads)):
        if deads[i] == '1':
            return times[i] - times[0]
    return None


for diff in ('survivor', 'original'):
    t = [tool(diff, sd) for sd in (1, 2, 3)]
    l = live(diff, 1)
    check('%s: the tool gives one time of death for seeds 1..3 (%s)' % (diff, t), None not in t and max(t) - min(t) <= 200)
    check('%s: the live game dies within 600 s before a sample that is within 400 s of the tool (%s live, %s tool)' % (diff, l, t[0]),
          l is not None and l - 600 - 400 <= t[0] <= l + 400)
t = [tool('guided', sd) for sd in (1, 2, 3, 4, 5)]
l = [live('guided', sd) for sd in (1, 2, 3, 4, 5)]
print('guided tool', t, 'live', l)
check('guided: everyone dies', None not in t and None not in l)
day = 86400.0
check('guided: the tool\'s range is 4.7 .. 5.8 days (not 3.6)', all(4.7 * day <= x <= 5.8 * day for x in t), t)
check('guided: the live game\'s range is 4.7 .. 5.8 days', all(4.7 * day <= x <= 5.8 * day for x in l), l)
check('guided: the two ranges overlap and the means are within half a day', max(min(t), min(l)) <= min(max(t), max(l)) and abs(sum(t) / 5 - sum(l) / 5) < 0.5 * day, (sum(t) / 5 / day, sum(l) / 5 / day))
t2 = [tool('guided', 1, r) for r in ('corrected', 'closedloop')]
check('the realism levels do not change it (seed 1: %s)' % t2, all(abs(x - t[0]) <= 200 for x in t2))
sys.exit(1 if failed else 0)
