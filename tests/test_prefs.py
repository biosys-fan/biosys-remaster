"""The preferences file. The pure logic (parse, precedence, unknown keys, write-back, the generated default) is in src/app/prefs.h and runs without
a window or an engine in build/biosys-apptest. The real option table of main.cpp is checked through the program: `--config FILE
--write-config` writes the effective settings (file, then command line) into FILE and exits, which shows what each layer did."""
import os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/prefs'
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:600] if detail != '' else ''))


def fresh(name):
    d = '%s/%s' % (T, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def prog(*args, env=None):
    r = subprocess.run(['build/biosys'] + list(args), capture_output=True, text=True, env=env)
    return r.returncode, r.stdout, r.stderr


def active(text):
    """key -> value of the active lines of a preferences file"""
    d = {}
    for l in text.splitlines():
        l = l.strip()
        if l and l[0] not in '#;' and '=' in l:
            k, v = l.split('=', 1)
            d[k.strip()] = v.strip()
    return d


# ---- the unit
r = subprocess.run(['build/biosys-apptest'], capture_output=True, text=True)
lines = r.stdout.splitlines()
for l in lines:
    if l.startswith('FAIL'):
        print(l)
check('apptest: %d checks, all passed' % sum(1 for l in lines if l.startswith('PASS')),
      r.returncode == 0 and 'all checks passed' in r.stdout and sum(1 for l in lines if l.startswith('PASS')) >= 60)
for want in ('parse:', 'apply:', 'precedence:', 'default file:', 'write-back round trip', 'update:'):
    check('apptest covers ' + want, any(want in l and l.startswith('PASS') for l in lines))

# ---- the real table: what the help says is an option, what the file says is a key
help_text = subprocess.run(['build/biosys', '--help'], capture_output=True, text=True).stdout
d = fresh('write')
conf = d + '/biosys.conf'
rc, out, err = prog('--config', conf, '--write-config')
check('--write-config on a missing file writes it and exits', rc == 0 and os.path.exists(conf) and 'preferences written' in out, (rc, err))
text = open(conf).read()
keys = active(text)
check('the written file has every preference, active, with its default', len(keys) >= 20 and keys.get('volume') == '100' and keys.get('autosave-minutes') == '10' and
      keys.get('save-pages') == '4' and keys.get('pause-on-focus-loss') == 'yes' and keys.get('mouse-sensitivity') == '1' and keys.get('panorama') == 'original' and
      keys.get('fullscreen') == 'no', keys)
check('every key is an option of the command line (one table)', all(('--' + k) in help_text for k in keys), [k for k in keys if ('--' + k) not in help_text])
check('the options the player has to retype are all keys', all(k in keys for k in (
    'realism', 'difficulty', 'panorama', 'panorama-filter', 'audio', 'volume', 'volume-effects', 'volume-movies', 'volume-terminal', 'fullscreen',
    'pass-ms', 'mute', 'game', 'skip-intro', 'autosave', 'autosave-minutes', 'save-pages', 'pause-on-focus-loss', 'mouse-sensitivity')), sorted(keys))
check('command-line-only options are not keys (page, script, screenshot, save-dir, config, load-autosave, seed)',
      not any(k in keys for k in ('page', 'script', 'screenshot', 'save-dir', 'config', 'no-config', 'write-config', 'load-autosave', 'seed', 'dev', 'test-keys')), sorted(keys))
check('the file explains itself: a comment with the help line above each key', '# Master volume' in text and '# Pages of save slots' in text and text.startswith('# Biosys preferences'), text[:200])

# the generated default (what the first start writes) parses as nothing active; applying the written file changes nothing
rc, out, err = prog('--config', conf, '--write-config')
check('write -> write is stable (a round trip)', open(conf).read() == text, err)

# ---- precedence: default < file < command line
d = fresh('prec')
conf = d + '/biosys.conf'
open(conf, 'w').write('# my settings\nvolume = 30\npanorama = smooth\nautosave-minutes = 3\nfullscreen = yes\nsave-pages = 2\nmouse-sensitivity = 2.5\n')
rc, out, err = prog('--config', conf, '--write-config')
k = active(open(conf).read())
check('the file overrides the defaults', k['volume'] == '30' and k['panorama'] == 'smooth' and k['autosave-minutes'] == '3' and k['fullscreen'] == 'yes' and
      k['save-pages'] == '2' and k['mouse-sensitivity'] == '2.5' and k['audio'] == 'original', k)
check('comments written by the player are kept', open(conf).read().startswith('# my settings\n'))
rc, out, err = prog('--config', conf, '--volume', '70', '--fullscreen=no', '--autosave-minutes', '0', '--write-config')
k = active(open(conf).read())
check('the command line overrides the file (value, --opt=value, a number)', k['volume'] == '70' and k['fullscreen'] == 'no' and k['autosave-minutes'] == '0' and
      k['panorama'] == 'smooth', k)
check('and the settings it did not name stay as the file had them', k['save-pages'] == '2' and k['mouse-sensitivity'] == '2.5')

# ---- unknown keys, bad values, damaged lines
d = fresh('bad')
conf = d + '/biosys.conf'
open(conf, 'w').write('bogus = 1\nvolume = 20\nbogus = 2\nvolume = 500\nfullscreen = maybe\nthis is not a setting\nmouse-sensitivity = 9\nscript = x\nrealism = nonsense\n')
rc, out, err = prog('--config', conf, '--write-config')
check('the program starts anyway (exit 0)', rc == 0, (rc, err))
check('an unknown key is reported once, however often it appears', err.count('unknown key `bogus`') == 1, err)
check('a key that is command line only is unknown in the file', 'unknown key `script`' in err, err)
check('bad values are reported with their line and ignored', 'line 4: volume' in err and 'line 5: fullscreen' in err and 'line 7: mouse-sensitivity' in err and 'line 9: realism' in err, err)
check('a line without = is reported', 'line 6' in err and 'expected `key = value`' in err, err)
text = open(conf).read()
k = active(text)
check('after a bad line the earlier good value stays (volume 20), the rest are defaults', k['volume'] == '20' and k['fullscreen'] == 'no' and k['mouse-sensitivity'] == '1' and
      k['realism'] == 'faithful', k)
check('unknown lines are kept in the file, not deleted', 'bogus = 1' in text and 'this is not a setting' in text)
rc, out, err = prog('--config', d + '/missing.conf')
check('a --config file that does not exist is an error (a typo must not start a default game)', rc == 1 and 'cannot read the preferences file' in err, (rc, err))
rc, out, err = prog('--no-config', '--write-config')
check('--write-config with --no-config is refused', rc == 1 and 'contradict' in err, (rc, err))
rc, out, err = prog('--volume', '101', '--script', 'echo x')
check('a bad command line is still an error', rc == 1 and 'bad --volume' in err, (rc, err))
rc, out, err = prog('--mouse-sensitivity', '0.1', '--script', 'echo x')
check('mouse-sensitivity outside 0.25 .. 4 is refused', rc == 1 and 'bad --mouse-sensitivity' in err, (rc, err))

# ---- scripts, screenshots and the self-test ignore the file in the save folder; --config applies one explicitly
d = fresh('scripts')
open(d + '/biosys.conf', 'w').write('game = yes\nrealism = corrected\ndifficulty = guided\n')
base = ['--mute', '--save-dir', d]
rc, out0, err = prog(*base, '--game', '--script', 'game settings')
rc2, out1, err2 = prog('--mute', '--save-dir', fresh('empty'), '--game', '--script', 'game settings')
check('--script ignores biosys.conf in the save folder (settings as without the file)', out0 == out1 and not err and 'preferences' not in err, (out0[:100], out1[:100], err))
rc, out2, err = prog(*base, '--script', 'game settings')
check('... also its `game = yes` (without --game a script has no game logic)', rc == 1 and "needs --game" in err, (rc, err))
_, ref_cg, _ = prog(*base, '--game', '--realism', 'corrected', '--difficulty', 'guided', '--script', 'game settings')
_, ref_cs, _ = prog(*base, '--game', '--realism', 'corrected', '--difficulty', 'survivor', '--script', 'game settings')
rc, out3, err = prog(*base, '--config', d + '/biosys.conf', '--script', 'game settings')
check('--config FILE applies the file to a script (game = yes, realism corrected, difficulty guided)', rc == 0 and out3 == ref_cg and out3 != out1, (rc, out3[:100], err))
rc, out4, err = prog(*base, '--config', d + '/biosys.conf', '--game', '--difficulty', 'survivor', '--script', 'game settings')
check('... and the command line still wins over it', out4 == ref_cs and out4 != out3, out4[:100])
rc, out5, err = prog(*base, '--config', d + '/biosys.conf', '--no-config', '--game', '--script', 'game settings')
check('--no-config beats --config', out5 == out1, out5[:100])
rc, out6, err = prog(*base, '--screenshot', d + '/s.bmp')
check('--screenshot does not create or read a file either', rc == 0 and sorted(os.listdir(d)) == ['biosys.conf', 's.bmp'], os.listdir(d))
e = fresh('noconf')
rc, out7, err = prog('--mute', '--save-dir', e, '--game', '--script', 'echo hi')
check('a script run writes no preferences file into the save folder', rc == 0 and not os.path.exists(e + '/biosys.conf') and os.listdir(e) == [], os.listdir(e))

# ---- the window: first start writes the commented default file; the volume, mute and full screen set while playing are saved at exit
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
w = fresh('window')
rc, out, err = prog('--game', '--skip-intro', '--mute', '--save-dir', w, '--quit-after-ms', '2500', env=env)
check('first start: a commented default file appears in the save folder', rc == 0 and os.path.exists(w + '/biosys.conf') and active(open(w + '/biosys.conf').read()) == {}, (rc, err[-300:]))
text0 = open(w + '/biosys.conf').read()
rc, out, err = prog('--game', '--skip-intro', '--mute', '--save-dir', w, '--quit-after-ms', '2500', env=env)
check('second start: the file is left as it is (apart from nothing)', open(w + '/biosys.conf').read() == text0, err[-200:])
rc, out, err = prog('--game', '--skip-intro', '--mute', '--save-dir', w, '--quit-after-ms', '2500', '--test-keys', 'minus,minus,minus,backslash', env=env)
k = active(open(w + '/biosys.conf').read())
check('the volume keys and the mute key are saved at exit (volume 70, start-muted yes)', k.get('volume') == '70' and k.get('start-muted') == 'yes', (k, err[-200:]))
body = open(w + '/biosys.conf').read()
check('the saved lines sit next to their commented defaults, the comments are intact', '# volume = 100\nvolume = 70\n' in body and '# Master volume 0..100' in body, body[:300])
rc, out, err = prog('--game', '--skip-intro', '--mute', '--save-dir', w, '--quit-after-ms', '2500', '--test-keys', 'equals', env=env)
check('the next start begins with those values and + raises from 70 to 80 (saved again)', active(open(w + '/biosys.conf').read()).get('volume') == '80' and
      active(open(w + '/biosys.conf').read()).get('start-muted') == 'no', active(open(w + '/biosys.conf').read()))
rc, out, err = prog('--game', '--skip-intro', '--mute', '--save-dir', w, '--no-config', '--quit-after-ms', '2500', '--test-keys', 'minus', env=env)
check('--no-config neither reads nor writes: nothing changed in the file', active(open(w + '/biosys.conf').read()).get('volume') == '80', err[-200:])
rc, out, err = prog('--game', '--skip-intro', '--mute', '--save-dir', w, '--volume', '40', '--quit-after-ms', '2500', env=env)
check('a command-line value that the player did not change is not written back over the file', active(open(w + '/biosys.conf').read()).get('volume') == '80', err[-200:])
sys.exit(1 if failed else 0)
