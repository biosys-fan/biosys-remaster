"""Autosave, pages of save slots and the pause of an inactive window. Everything headless runs `build/biosys --game --script` on the injected
clock: the autosave's play time is the clock of `advance` (the window counts real time while the game runs and the window is not paused), so a
script can step over the interval in one command. The window itself (quit autosave, F9, the preferences) runs with SDL's dummy drivers and key
presses pushed as real SDL events (`--test-keys`, `--quit-after-ms`). Expected file names, rotation order and the refusal cases come from
MANUAL.md "Saving" and analysis/notes/menus_saveload.md, the slot rectangles from the page file read by the analysis scripts."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, shutil, stat, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
PAGES = D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'
PAGEPY = D + '/analysis/scripts/biosys_pages.py'
T = 'build/test/autosave'
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:700] if detail != '' else ''))


def fresh(name):
    d = '%s/%s' % (T, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def run(script, d, *opts):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--seed', '1', '--save-dir', d] + list(opts) + ['--script', script], capture_output=True, text=True)
    return r.returncode, r.stdout.splitlines(), r.stderr


def status(out, nth=-1):
    ls = [l for l in out if l.startswith('autosave enabled=')]
    m = re.match(r"autosave enabled=(\d) minutes=(\d+) played=(\d+) due=(\w+) blocker='([^']*)' newest=(\d)", ls[nth])
    return {'enabled': int(m.group(1)), 'minutes': int(m.group(2)), 'played': int(m.group(3)), 'due': m.group(4), 'blocker': m.group(5), 'newest': int(m.group(6))}


def files(d):
    return sorted(os.listdir(d))


def fnv(b):
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xffffffffffffffff
    return h


def gtime(out):
    return int(re.search(r'game time=(\d+)', [l for l in out if l.startswith('game time=')][-1]).group(1))


VOLATILE = ('Delta', 'UpdateDt', 'Unk298', 'SoundCue', 'lasttick', 'xft=', 'Hotspot')


def state(out):
    """the game dump lines that a saved game restores (minus the clocks of the run and the derived per-step values)"""
    keep = []
    for l in out:
        if l.startswith(('game time=', 'game site=', 'self.')) and not any(v in l for v in VOLATILE):
            keep.append(re.sub(r' now=\d+', '', l))
    return keep


# ---- the pure logic
r = subprocess.run(['build/biosys-apptest'], capture_output=True, text=True)
lines = r.stdout.splitlines()
for l in lines:
    if l.startswith('FAIL'):
        print(l)
check('apptest: %d checks, all passed' % sum(1 for l in lines if l.startswith('PASS')), r.returncode == 0 and 'all checks passed' in r.stdout)
for want in ('planner:', 'files:', 'F9:', 'pause:', 'sensitivity', 'page keys'):
    check('apptest covers ' + want, any(want in l and l.startswith('PASS') for l in lines))

# ================================================================== (a) periodic autosave on the injected clock
d = fresh('periodic')
rc, out, err = run('advance 59900; autosave status; advance 100; autosave status; advance 3000; autosave status', d, '--autosave-minutes', '1')
s0, s1, s2 = status(out, 0), status(out, 1), status(out, 2)
check('(a) not due before the interval: 59.9 s of play, no file', s0['due'] == 'no' and s0['newest'] == 0 and s0['played'] == 59900, s0)
check('(a) at the first update step after the interval the autosave is made and the interval starts again', s1['newest'] == 1 and s1['played'] <= 100 and 'autosave1.sav' in files(d), (s1, files(d)))
check('(a) a save through the normal path: the settings are in the file (a block after the game\'s part), no .settings file',
      files(d) == ['autosave1.sav'] and open(d + '/autosave1.sav', 'rb').read().count(b'BSYSMODE') == 1 and b'realism=faithful' in open(d + '/autosave1.sav', 'rb').read(), files(d))
check('(a) nothing more until the next interval', files(d) == ['autosave1.sav'], files(d))
check('(a) the .sav starts as the original format does (engine part header)', open(d + '/autosave1.sav', 'rb').read()[:4] == bytes([0xc0, 0x67, 0x5e, 0xff]))
d = fresh('off')
rc, out, err = run('advance 200000; autosave status', d, '--autosave-minutes', '0')
check('(a) autosave-minutes 0: no timed autosave however long the play', files(d) == [] and status(out)['due'] == 'no', out[-1:])
rc, out, err = run('advance 200000; autosave status', fresh('off2'), '--autosave', 'no', '--autosave-minutes', '1')
check('(a) --autosave no: nothing at all', files(T + '/off2') == [] and status(out)['enabled'] == 0)
d = fresh('implicit')
rc, out, err = run('advance 700000', d)
check('(a) a script that does not ask for autosave never writes one by itself (so no test writes into a save folder unasked)', files(d) == [], files(d))

# ================================================================== (b) rotation: autosave1 newest, three kept
d = fresh('rotate')
script = '; '.join(['advance 61000; autosave status'] * 4)
rc, out, err = run(script, d, '--autosave-minutes', '1')
check('(b) four intervals made four saves: three files are kept, nothing else (no autosave0, no autosave4)',
      files(d) == ['autosave1.sav', 'autosave2.sav', 'autosave3.sav'], files(d))
times = []
for n in (1, 2, 3):
    rc, o, e = run('game dump', d, '--load-autosave', str(n))
    times.append(gtime(o) if rc == 0 else None)
check('(b) the newest is autosave1: game time 1 > 2 > 3, an interval of play (minute of real time at 40 ticks) apart', None not in times and times[0] > times[1] > times[2], times)
rc, out, err = run('advance 61000', d, '--autosave-minutes', '1')
rc, o, e = run('game dump', d, '--load-autosave', '3')
check('(b) the next rotation drops the oldest: the old autosave2 is now autosave3', gtime(o) == times[1], (gtime(o), times))
# the settings file of a save of an earlier version travels with its save, and none is left behind for a save that was replaced
open(d + '/autosave1.settings', 'w').write('legacy')
rc, out, err = run('advance 61000', d, '--autosave-minutes', '1')
check('(b) rotation moves a settings file of an earlier version with its .sav and leaves no stale one', 'autosave1.settings' not in files(d) and 'autosave2.settings' in files(d) and 'autosave3.settings' not in files(d), files(d))

# ================================================================== (c) never while a normal save is refused; it waits for the next allowed moment
d = fresh('refused')
rc, out, err = run('advance 59000; key F5; advance 5000; autosave status; key ESC; advance 100; autosave status; key ESC; advance 100; autosave status', d, '--autosave-minutes', '1')
s = [status(out, i) for i in range(3)]
check('(c) the interval passes while the save menu is open: still due, not written', s[0]['due'] == 'timed' and s[0]['blocker'] == 'a menu is open' and s[0]['newest'] == 0, s[0])
check('(c) still waiting in the main menu', s[1]['due'] == 'timed' and s[1]['newest'] == 0, s[1])
check('(c) the menu closed: the autosave follows at the next step, and only one', s[2]['newest'] == 1 and s[2]['due'] == 'no' and s[2]['played'] < 200 and len(files(d)) == 1, (s[2], files(d)))
d = fresh('ended')
rc, out, err = run('advance 3000; type SILVERFEATHER; advance 2600; type HAPPYENDING\\n; advance 3000; autosave status; advance 70000; autosave status; autosave now; autosave quit', d, '--autosave-minutes', '1')
check('(c) after the end of the game nothing is saved, though due (the original refuses a save then)', status(out, 1)['due'] == 'timed' and status(out, 1)['blocker'] == 'the game has ended' and
      files(d) == [] and 'autosave now refused' in '\n'.join(out) and 'autosave quit refused' in '\n'.join(out), (out[-4:], files(d)))
d = fresh('asleep')
SLEEP = 'keydown TAB; advance 100; move 460,68; click 460,68'
rc, out, err = run('advance 3000; ' + SLEEP + '; advance 300; autosave status; advance 2000; autosave status; advance 6000; autosave status', d, '--autosave-minutes', '0')
s = [status(out, i) for i in range(3)]
check('(c) while the player sleeps no save is made (the blocker names it)', s[0]['blocker'] == 'the player is asleep' and s[1]['blocker'] == 'the player is asleep' and s[0]['newest'] == 0 and s[1]['newest'] == 0, (s[0], s[1]))
# ---- (d) wake from sleep (timer off: the wake is the only reason)
check('(d) waking up makes an autosave (with the timer off)', s[2]['newest'] == 1 and s[2]['blocker'] == '' and 'autosave1.sav' in files(d), (s[2], files(d)))
rc, o, e = run('game dump; game self', d, '--load-autosave', '1')
check('(d) the wake autosave is a game that is awake', 'self.m_bAwake = 1' in o, [l for l in o if 'Awake' in l])
d = fresh('wake_off')
rc, out, err = run('advance 3000; ' + SLEEP + '; advance 9000; autosave status', d, '--autosave', 'no')
check('(d) --autosave no: not even on waking up', files(d) == [], files(d))

# ================================================================== (e) a failing autosave never disturbs the game
d = fresh('readonly')
os.chmod(d, stat.S_IRUSR | stat.S_IXUSR)
rc, out, err = run('advance 61000; echo AFTER1; autosave status; advance 61000; echo AFTER2; game dump', d, '--autosave-minutes', '1')
os.chmod(d, stat.S_IRWXU)
check('(e) an unwritable save folder: the run goes on to the end (exit 0, the script output is complete)', rc == 0 and 'AFTER1' in out and 'AFTER2' in out and any(l.startswith('game time=') for l in out), (rc, err[-300:]))
check('(e) it is reported on stderr, once per interval, not once per frame', err.count('autosave (timed) failed') == 2, err[-400:])
check('(e) nothing was written, no temporary left', files(d) == [], files(d))
s = status(out, 0)
check('(e) the failed attempt restarted the interval (not due again at once)', s['due'] == 'no' and s['played'] < 1100, s)
rc, out, err = run('advance 61000; echo AFTER; autosave now', T + '/does/not/exist', '--autosave-minutes', '1')
check('(e) a folder that does not exist: reported, the game goes on', rc == 0 and 'AFTER' in out and 'autosave (timed) failed' in err and 'autosave now failed' in '\n'.join(out), (rc, err[-300:]))
# a failed save must not damage the older autosaves
d = fresh('keep_old')
run('advance 61000', d, '--autosave-minutes', '1')
before = open(d + '/autosave1.sav', 'rb').read()
os.chmod(d, stat.S_IRUSR | stat.S_IXUSR)
rc, out, err = run('advance 61000', d, '--autosave-minutes', '1')
os.chmod(d, stat.S_IRWXU)
check('(e) a failed autosave leaves the existing ones as they were', open(d + '/autosave1.sav', 'rb').read() == before and files(d) == ['autosave1.sav'], files(d))

# ================================================================== (f) --load-autosave restores the state
d = fresh('load')
rc, out, err = run('advance 3000; type SILVERFEATHER; advance 2600; type GOTORHUT\\n; advance 4000; autosave now; game dump; game self', d, '--autosave-minutes', '0')
saved = state(out)
check('(f) the state at save time was dumped', rc == 0 and len(saved) > 50 and 'autosave now saved' in out, (rc, err[-200:]))
rc, o, e = run('game dump; game self', d, '--load-autosave')
check('(f) --load-autosave: the program starts directly in the saved game (game dump equals the state at save time)', rc == 0 and state(o) == saved,
      [(a, b) for a, b in zip(saved, state(o)) if a != b][:3])
rc, o, e = run('game dump; game self', d, '--load-autosave', '1')
check('(f) --load-autosave 1 is the same', state(o) == saved)
rc, o, e = run('game dump', fresh('load_none'), '--load-autosave')
check('(f) without an autosave it says so and exits with an error', rc == 1 and 'no autosave to load' in e, (rc, e))
rc, o, e = run('game dump', d, '--load-autosave', '4')
check('(f) --load-autosave 4 is refused (1..3)', rc == 1 and 'bad --load-autosave' in e, (rc, e))
rc, o, e = subprocess.run(['build/biosys', '--mute', '--save-dir', d, '--load-autosave', '--script', 'echo x'], capture_output=True, text=True).returncode, '', ''
check('(f) --load-autosave needs --game', rc == 1)
rc, o, e = run('autosave load; game dump; game self', d, '--autosave-minutes', '0')
check('(f) `autosave load` in a running game (what the confirmed F9 does) restores the same state', 'autosave load ok' in '\n'.join(o) and state(o) == saved, [l for l in o if l.startswith('autosave')])
rc, o, e = run('autosave load 3', fresh('load_none2'), '--autosave-minutes', '0')
check('(f) a missing autosave is refused without harm', 'autosave load failed: no autosave to load' in '\n'.join(o) and rc == 0, o)
rc, out, err = run('advance 3000; ' + SLEEP + '; advance 300; autosave load', d, '--autosave-minutes', '0')
check('(f) loading is refused while the player sleeps (as the load key is)', 'cannot load now: the player is asleep' in '\n'.join(out), out[-1:])

# ================================================================== (g) the window: quit autosave, F9 (dummy video / audio drivers, key events pushed through SDL)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def window(d, *opts):
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--mute', '--no-config', '--save-dir', d, '--log-events'] + list(opts), capture_output=True, text=True, env=env)
    return r.returncode, [l for l in r.stdout.splitlines() if l.startswith('event ')], r.stderr


d = fresh('quit')
rc, ev, err = window(d, '--quit-after-ms', '2500')
check('(g) quitting with a game running makes an autosave (rotated into autosave1)', rc == 0 and files(d) == ['autosave1.sav'] and 'event savegame autosave0.sav' in ev, (rc, files(d), err[-200:]))
rc, ev, err = window(d, '--quit-after-ms', '2500')
rc, ev, err = window(d, '--quit-after-ms', '2500')
rc, ev, err = window(d, '--quit-after-ms', '2500')
check('(g) every quit rotates: three files stay', files(d) == ['autosave1.sav', 'autosave2.sav', 'autosave3.sav'], files(d))
rc, ev, err = window(fresh('quit_off'), '--quit-after-ms', '2500', '--autosave', 'no')
check('(g) --autosave no: nothing at quit', files(T + '/quit_off') == [] and rc == 0, files(T + '/quit_off'))
rc, ev, err = window(fresh('quit_unwritable') + '/nope', '--quit-after-ms', '2500')
check('(g) quitting with an unwritable save folder: reported, exits normally', rc == 0 and 'autosave (quit) failed' in err, (rc, err[-300:]))
d = fresh('f9')
window(d, '--quit-after-ms', '2500')
loads = lambda ev: [e for e in ev if e.startswith('event loadgame autosave')]
rc, ev, err = window(d, '--quit-after-ms', '2500', '--test-keys', 'f9')
check('(g) F9 once only shows the prompt: nothing is loaded', rc == 0 and loads(ev) == [], loads(ev))
rc, ev, err = window(d, '--quit-after-ms', '2500', '--test-keys', 'f9,f9')
check('(g) F9 twice within 3 s loads the newest autosave', rc == 0 and len(loads(ev)) == 1 and loads(ev)[0].startswith('event loadgame autosave1.sav'), (loads(ev), err[-200:]))
rc, ev, err = window(d, '--quit-after-ms', '2500', '--test-keys', 'f5,f9,f9')
check('(g) F9 in a menu does nothing', loads(ev) == [], loads(ev))
rc, ev, err = window(fresh('f9_none'), '--quit-after-ms', '2500', '--test-keys', 'f9,f9', '--autosave', 'no')
check('(g) F9 without an autosave loads nothing', loads(ev) == [], loads(ev))
rc, ev, err = window(d, '--quit-after-ms', '2500', '--load-autosave')
check('(g) the window with --load-autosave starts in the autosave (no intro, no new game replaced afterwards)', rc == 0 and len(loads(ev)) == 1, (loads(ev), err[-200:]))

# ================================================================== (h) pages of save slots, through real menu keys and clicks
def hot_dump(page):
    txt = subprocess.run(['python3', '-I', PAGEPY, PAGES, 'dump', page], capture_output=True, text=True).stdout
    hs = {}
    for m in re.finditer(r'hot\[\d+\] (\S+)\s+rect=\((\d+), (\d+), (\d+), (\d+)\)', txt):
        hs[m.group(1)] = tuple(int(m.group(i)) for i in range(2, 6))
    return hs


def centre(rect):
    return '%d,%d' % ((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)


SAVEH, LOADH = hot_dump('menusave'), hot_dump('menuload')


def menu_slotpage(out):
    ls = [l for l in out if l.startswith('slotpage ')]
    return ls[-1].split()[1] if ls else None


d = fresh('slots')
EV = ['--log-events']
rc, out, err = run('advance 2000; key F5; dump menu; key PAGEDOWN; dump menu; key PAGEDOWN; dump menu; key PAGEDOWN; dump menu; key PAGEDOWN; dump menu; key PAGEUP; dump menu; wheel up; dump menu; wheel down; wheel down; dump menu', d, *EV)
pages = [l.split()[1] for l in out if l.startswith('slotpage ')]
check('(h) the save menu opens on page 1; Page Down goes 2, 3, 4 and stops at 4; Page Up and the wheel go back and forth',
      pages == ['1/4', '2/4', '3/4', '4/4', '4/4', '3/4', '2/4', '4/4'], pages)
q = [l for l in out if l.startswith('event menuquery')]
check('(h) page 1 asks about slots 1..20 as the original did (the menu test of the original relies on it), page 2 the ten of its own (11..20), page 3 21..30',
      [l.split()[2] for l in q[:20]] == [str(i) for i in range(1, 21)] and [l.split()[2] for l in q[20:30]] == [str(i) for i in range(11, 21)] and
      [l.split()[2] for l in q[30:40]] == [str(i) for i in range(21, 31)], [l.split()[2] for l in q][:45])

# page 1: exactly the original's file; page 3: a distinct name the original never reads
rc, out, err = run('advance 2000; key F5; click %s; advance 300; key F5; key PAGEDOWN; key PAGEDOWN; click %s; advance 300; key F5; key PAGEDOWN; click %s; advance 300; key F5; key PAGEDOWN; key PAGEDOWN; key PAGEDOWN; click %s; advance 300'
                   % (centre(SAVEH['savegame3']), centre(SAVEH['savegame4']), centre(SAVEH['savegame1']), centre(SAVEH['savegame10'])), d)
names = sorted(f for f in files(d) if f.endswith('.sav'))
check('(h) saving on pages 1, 3, 2 and 4 writes Saved Game 3, 24, 11 and 40: original-style names only for page 1',
      names == ['Saved Game 11.sav', 'Saved Game 24.sav', 'Saved Game 3.sav', 'Saved Game 40.sav'], names)
check('(h) every one carries its settings in the file', all(b'BSYSMODE' in open(d + '/' + n, 'rb').read() for n in names), files(d))
orig = [n for n in names if int(re.search(r'(\d+)', n).group(1)) <= 10]
check('(h) the files the original would read are the page-1 saves only', orig == ['Saved Game 3.sav'], orig)

# load it back through the load menu: Page Down twice, the fourth slot
rc, out, err = run('advance 2000; game dump; game self; key F6; key PAGEDOWN; key PAGEDOWN; dump menu; click %s; advance 100; game dump; game self' % centre(LOADH['loadgame4']), d, *EV)
gd = [i for i, l in enumerate(out) if l.startswith('game time=')]
check('(h) loading page 3 slot 4 through the menu: the engine loaded Saved Game 24.sav', 'event loadgame Saved Game 24.sav offset=' in '\n'.join(out) or any(l.startswith('event loadgame Saved Game 24.sav') for l in out),
      [l for l in out if 'loadgame' in l][:4])
q = [l for l in out if l.startswith('event menuquery') and ' load ' in l]
check('(h) the load list of page 3 shows the slot as taken and the others as empty', 'event menuquery 24 load exists=1' in out and 'event menuquery 23 load exists=0' in out, q[-12:])
check('(h) the menu reported page 3/4', menu_slotpage(out) == '3/4', menu_slotpage(out))
# the state restored is the state that was saved
d2 = fresh('slots_state')
rc, out, err = run('advance 3000; type SILVERFEATHER; advance 2600; type GOTORHUT\\n; advance 4000; key F5; key PAGEDOWN; key PAGEDOWN; click %s; advance 300; game dump; game self' % centre(SAVEH['savegame5']), d2)
st = state(out)
rc, out, err = run('key F6; key PAGEDOWN; key PAGEDOWN; click %s; advance 10; game dump; game self' % centre(LOADH['loadgame5']), d2)
check('(h) a game saved on page 3 and loaded back through the menus is the same game', rc == 0 and len(st) > 50 and state(out) == st, [(a, b) for a, b in zip(st, state(out)) if a != b][:3])
# overwrite question on a later page
rc, out, err = run('advance 2000; key F5; key PAGEDOWN; click %s; advance 100; key F5; key PAGEDOWN; click %s; dump menu; key Y; advance 300' % ((centre(SAVEH['savegame2']),) * 2), fresh('slots_over'), *EV)
blocks = [l for l in out if l.startswith('page ') and 'saveconfirm' in l]
check('(h) the overwrite question works on page 2 (saveconfirm 1), Y saves slot 12 again', any('saveconfirm 1' in b for b in blocks) and [l for l in out if l.startswith('event savegame')] == ['event savegame Saved Game 12.sav'] * 2, (blocks[-1:], [l for l in out if 'savegame' in l]))
# one page = exactly the original
d3 = fresh('slots_one')
rc, out, err = run('advance 2000; key F5; key PAGEDOWN; dump menu; click %s; advance 300' % centre(SAVEH['savegame2']), d3, '--save-pages', '1')
check('(h) --save-pages 1: no page keys, no page number, the save lands on slot 2 as in the original', menu_slotpage(out) is None and sorted(f for f in files(d3) if f.endswith('.sav')) == ['Saved Game 2.sav'], (menu_slotpage(out), files(d3)))
rc, out, err = run('advance 2000; key F5; key PAGEDOWN; dump menu', fresh('slots_two'), '--save-pages', '2')
rc, out2, err = run('advance 2000; key F5; key PAGEDOWN; key PAGEDOWN; dump menu', fresh('slots_two'), '--save-pages', '2')
check('(h) --save-pages 2: two pages', menu_slotpage(out) == '2/2' and menu_slotpage(out2) == '2/2', (menu_slotpage(out), menu_slotpage(out2)))
# the page keys do nothing while the question is up, or in the main menu
rc, out, err = run('advance 2000; key F5; click %s; advance 100; key F5; click %s; key PAGEDOWN; dump menu' % ((centre(SAVEH['savegame2']),) * 2), fresh('slots_q'))
check('(h) a page key during the overwrite question changes nothing', menu_slotpage(out) == '1/4', menu_slotpage(out))
rc, out, err = run('advance 2000; key ESC; key PAGEDOWN; dump menu', fresh('slots_top'))
check('(h) in the main menu the page keys do nothing (no slot list)', menu_slotpage(out) == '0/4', menu_slotpage(out))
# a page shows the page number as text: the frame differs from the one of a single-page menu only in the strip at the top right
rc, o, e = run('advance 2000; key F5; shot %s/one.bmp' % T, fresh('shot1'), '--save-pages', '1')
rc, o, e = run('advance 2000; key F5; shot %s/four.bmp' % T, fresh('shot2'))
a, b = open(T + '/one.bmp', 'rb').read(), open(T + '/four.bmp', 'rb').read()
off = int.from_bytes(a[10:14], 'little')
w_, h_ = int.from_bytes(a[18:22], 'little'), int.from_bytes(a[22:26], 'little')
rows_diff = set()
cols_diff = set()
if len(a) == len(b):
    for y in range(h_):
        ra, rb = a[off + y * w_: off + (y + 1) * w_], b[off + y * w_: off + (y + 1) * w_]
        if ra != rb:
            rows_diff.add(h_ - 1 - y)
            for x in range(w_):
                if ra[x] != rb[x]:
                    cols_diff.add(x)
check('(h) "page 1/4" is drawn in the free strip right of the title (rows 26..48, columns 440..629) and nothing else of the menu changes',
      len(a) == len(b) and rows_diff and min(rows_diff) >= 26 and max(rows_diff) <= 48 and min(cols_diff) >= 440 and max(cols_diff) <= 629, (sorted(rows_diff)[:3], sorted(cols_diff)[:3]))

# ================================================================== (i) pause: no engine passes, game clock stopped, while the window is inactive
rc, out, err = run('advance 3000; echo A; game dump; advance 2000; echo B; activate off; echo C; game dump; advance 6000; game dump; echo D; activate on; advance 8000; echo E; game dump',
                   fresh('pause'), '--log-events', '--log-idle')
idx = {m: out.index(m) for m in 'ABCDE'}
idles = lambda a, b: sum(1 for l in out[idx[a]:idx[b]] if l.startswith('event idle'))
times = [gtime([l]) for l in out if l.startswith('game time=')]
check('(i) the engine passes run while active (idle events between A and B)', idles('A', 'B') > 50, idles('A', 'B'))
check('(i) while inactive: no idle pass at all for six seconds', idles('C', 'D') == 0, idles('C', 'D'))
check('(i) the game clock does not advance while inactive (game time at C = after six more seconds)', times[1] == times[2] and times[1] > times[0], times)
check('(i) and runs again afterwards', idles('D', 'E') > 50 and times[3] > times[2], (idles('D', 'E'), times))
check('(i) the original\'s activation events fire (event 11 / 12 equivalents: activateapp, appactive)', 'event activateapp 0' in out and 'event appactive 0' in out and 'event appactive 1' in out, [l for l in out if 'active' in l])
rc, out, err = run('advance 61000; activate off; advance 600000; activate on; autosave status', fresh('pause_auto'), '--autosave-minutes', '1')
s = status(out)
check('(i) the autosave interval does not run while the window is inactive (one save, none from the 10 minutes inactive)', s['newest'] == 1 and not os.path.exists(T + '/pause_auto/autosave2.sav'), (s, files(T + '/pause_auto')))
sys.exit(1 if failed else 0)
