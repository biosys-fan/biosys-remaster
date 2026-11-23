"""Window helpers and the settings sidecar of saved games. What a headless run cannot show (the key events of a real window, the
full-screen switch, the look of the status screen on a Mac) is listed in MANUAL.md / the report as person-at-the-window checks."""
import os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/window'
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:500] if detail else ''))


# ---- the pure helpers (C++)
r = subprocess.run(['build/biosys-windowtest'], capture_output=True, text=True)
for l in r.stdout.splitlines():
    if l.startswith('FAIL'):
        print(l)
check('windowtest: %d checks' % sum(1 for l in r.stdout.splitlines() if l.startswith('PASS')),
      r.returncode == 0 and 'all checks passed' in r.stdout and sum(1 for l in r.stdout.splitlines() if l.startswith('PASS')) >= 30)

# ---- options
h = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
check('--help lists --dev (off by default), --fullscreen and the new keys', '--dev' in h and '--fullscreen' in h and 'Caps Lock' in h and 'Option+Return' in h)
r = subprocess.run(['build/biosys', '--game', '--mute', '--dev', '--fullscreen', '--script', 'echo ok'], capture_output=True, text=True)
check('--dev and --fullscreen are accepted next to a script', r.returncode == 0 and 'ok' in r.stdout, r.stderr)
r = subprocess.run(['build/biosys', '--game', '--mute', '--script', 'echo ok'], capture_output=True, text=True)
check('scripts need neither of them', r.returncode == 0 and 'ok' in r.stdout, r.stderr)
src = open('src/app/main.cpp').read()
check('every developer key of the window is behind o.dev',
      'if (!o.dev && (k == SDLK_F2 || k == SDLK_H || k == SDLK_O || k == SDLK_G || k == SDLK_M || k == SDLK_N || k == SDLK_BACKSPACE' in src)

# ---- the settings next to a saved game
S = T + '/saves'
shutil.rmtree(S, ignore_errors=True)
os.makedirs(S)


def run(script, *opts):
    p = subprocess.run(['build/biosys', '--game', '--mute', '--save-dir', S] + list(opts) + ['--script', script], capture_output=True, text=True)
    return p.returncode, p.stdout.splitlines(), p.stderr


def settings_line(lines, nth=0):
    ls = [l for l in lines if l.startswith('game settings ')]
    return ls[nth] if len(ls) > nth else None


def sav(n):
    return os.path.join(S, 'Saved Game %d.sav' % n)


def blob_end(b):
    return struct.unpack_from('<I', b, 8)[0] + 179017      # the game's blob is a fixed size (src/game/savegame.h)


def stripped(n):
    b = open(sav(n), 'rb').read()
    return b[:blob_end(b)]


def trailer_text(n):
    b = open(sav(n), 'rb').read()
    e = blob_end(b)
    return b[e + 16:-4].decode() if b[e:e + 8] == b'BSYSMODE' else ''


def side(n):
    return os.path.join(S, 'Saved Game %d.settings' % n)


# the reference: what a plain command line gives
_, out, _ = run('game settings', '--realism', 'corrected', '--difficulty', 'guided')
CORR = settings_line(out)
_, out, _ = run('game settings')
FAITH = settings_line(out)
check('the two command lines give different settings', CORR and FAITH and CORR != FAITH)

rc, out, err = run('advance 300; api SaveGame saved game 3.sav', '--realism', 'corrected', '--difficulty', 'guided')
check('a save under corrected/guided writes the .sav with the settings inside it, and no .settings file', rc == 0 and os.path.exists(sav(3)) and not os.path.exists(side(3)), err)
text = trailer_text(3)
check('the settings block names realism, difficulty and the fix list', 'realism=corrected' in text and 'difficulty=guided' in text and '\nfixes=\n' in text, text)
d = open(sav(3), 'rb').read()
check('the engine\'s part and the game\'s part are the original format (no settings text inside them; the block follows the game\'s blob)', b'realism' not in d[:blob_end(d)] and b'biosys-settings' not in d[:blob_end(d)] and len(d) > blob_end(d))

rc, out, err = run('game settings; api LoadGame -1 saved game 3.sav; game settings')
check('restart with the default command line: before the load faithful, after it the saved settings', settings_line(out, 0) == FAITH and settings_line(out, 1) == CORR, out[-3:])
check('one line says so', err.count('continues under the settings it was saved with') == 1 and 'realism corrected, difficulty guided' in err, err)

rc, out, err = run('api LoadGame -1 saved game 3.sav; game settings', '--realism', 'corrected', '--difficulty', 'guided')
check('a command line equal to the saved settings: loaded, no message', settings_line(out) == CORR and 'continues under' not in err, err)

rc, out, err = run('api LoadGame -1 saved game 3.sav; game settings; api LoadGame -1 newgame.sav; game settings')
check('a new game (NewGame.sav has no settings file) goes back to the command line', settings_line(out, 0) == CORR and settings_line(out, 1) == FAITH, out[-4:])

# a game saved after loading keeps the settings it runs under
rc, out, err = run('api LoadGame -1 saved game 3.sav; api SaveGame saved game 5.sav')
check('saving a loaded game writes the settings in force, not the command line\'s', 'realism=corrected' in trailer_text(5), trailer_text(5))

# an original save: no block, no settings file
open(sav(4), 'wb').write(stripped(3))
rc, out, err = run('api LoadGame -1 saved game 4.sav; game settings', '--realism', 'closedloop')
_, o2, _ = run('game settings', '--realism', 'closedloop')
check('a save without a block and without a settings file uses the command line', settings_line(out) == settings_line(o2) and settings_line(out) != CORR and 'continues under' not in err, err)

# saves of an earlier version: a settings file next to the save, made for exactly its bytes (FNV-1a of the file)
def fnv(b):
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xffffffffffffffff
    return h


def legacy(n, data, hash_of=None):
    open(sav(n), 'wb').write(data)
    open(side(n), 'w').write('biosys-settings 1\nrealism=corrected\ndifficulty=guided\nfixes=\nsave=%016x\n' % fnv(data if hash_of is None else hash_of))


legacy(6, stripped(3), hash_of=b'some other save')
rc, out, err = run('api LoadGame -1 saved game 6.sav; game settings')
check('a save with a settings file made for other bytes is not taken for that game\'s', settings_line(out) == FAITH, out[-2:])
legacy(7, stripped(3))
rc, out, err = run('api LoadGame -1 saved game 7.sav; game settings')
check('a save of an earlier version (only the settings file next to it) still loads under it', settings_line(out) == CORR, out[-2:])
with open(side(7), 'w') as f:
    f.write('garbage\n')
rc, out, err = run('api LoadGame -1 saved game 7.sav; game settings')
check('a damaged settings file is ignored', rc == 0 and settings_line(out) == FAITH, out[-2:])

# overwriting a slot rewrites its settings; a failed or reduced save leaves none that is stale
rc, out, err = run('advance 300; api SaveGame saved game 3.sav')
check('overwriting slot 3 under the default command line replaces its settings', 'realism=faithful' in trailer_text(3) and 'corrected' not in trailer_text(3))
rc, out, err = run('api LoadGame -1 saved game 3.sav; game settings')
check('and the slot now loads as faithful', settings_line(out) == FAITH and 'continues under' not in err, err)
rc, out, err = run('api SaveGame saved game 8.sav', '--realism', 'closedloop', '--fixes', 'faithful')
check('the fix list is stored', 'realism=closedloop' in trailer_text(8) and 'fixes=faithful' in trailer_text(8), trailer_text(8))
rc, out, err = run('api LoadGame -1 saved game 8.sav; game settings', '--difficulty', 'survivor')
_, o3, _ = run('game settings', '--realism', 'closedloop', '--fixes', 'faithful')
check('a load restores realism and the fix list over the command line\'s', settings_line(out) == settings_line(o3), (settings_line(out) or '')[:200])
# a load that fails does not leave the stored settings behind
bad = open(sav(3), 'rb').read()[:-4000]
open(sav(9), 'wb').write(bad)
open(side(9), 'w').write('biosys-settings 1\nrealism=corrected\ndifficulty=guided\nfixes=\nsave=%016x\n' % fnv(bad))
rc, out, err = run('api LoadGame -1 saved game 9.sav; game settings')
check('a load that fails leaves the settings of before (no stored settings taken over)', 'api LoadGame rc=0' not in out and settings_line(out) == FAITH, (out[-3:], err[-300:]))
sys.exit(1 if failed else 0)
