"""The game mode inside the save file, the New game screen and the settings menu (F10).

Everything that needs no window runs through `build/biosys --script` (the `menu` command drives the same Controller and the same drawing
code as the window); the window's own code (key and mouse routing, the pause, the live switching of the picture layers, the preferences)
runs on SDL's dummy video and audio drivers with key and mouse events pushed through SDL (`--test-keys`). What a person at the window has
to judge (how it looks and feels on a real screen) is listed in MANUAL.md section 10.3."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import hashlib, os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/settingsmenu'
SHOTS = 'build/shots'
os.makedirs(T, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
D = _DEV
PAGES = D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'
PAGEPY = D + '/analysis/scripts/biosys_pages.py'
failed = 0
count = 0
ENV = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def check(name, ok, detail=''):
    global failed, count
    count += 1
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:900] if detail != '' else ''))


def fresh(name):
    d = '%s/%s' % (T, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def run(script, d, *opts, env=None, game=True):
    base = ['build/biosys', '--mute', '--skip-intro', '--save-dir', d] + (['--game'] if game else [])
    r = subprocess.run(base + list(opts) + ['--script', script], capture_output=True, text=True, env=env)
    return r.returncode, r.stdout.splitlines(), r.stderr


def window(d, keys, ms, *opts):
    """a window run on the dummy drivers: -> (rc, stdout lines, stderr)"""
    args = ['build/biosys', '--game', '--skip-intro', '--save-dir', d, '--quit-after-ms', str(ms)]
    if keys:
        args += ['--test-keys', keys]
    r = subprocess.run(args + list(opts), capture_output=True, text=True, env=ENV)
    return r.returncode, r.stdout.splitlines(), r.stderr


def kv(line):
    return dict(p.split('=', 1) for p in line.split() if '=' in p)


def lines(out, prefix):
    return [l for l in out if l.startswith(prefix)]


def last(out, prefix):
    l = lines(out, prefix)
    return l[-1] if l else ''


def engine(out):
    return kv(last(out, 'menu engine'))


def options(out):
    return dict(l[len('menu option '):].split('=', 1) for l in lines(out, 'menu option '))


def fnv1a64(b):
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def fnv32(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def trailer_block(text):
    t = text.encode()
    return b'BSYSMODE' + struct.pack('<II', 1, len(t)) + t + struct.pack('<I', fnv32(t))


BLOB = 179017   # the game's blob (src/game/savegame.h): fixed by the table constants


def blob_end(sav):
    return struct.unpack_from('<I', sav, 8)[0] + BLOB


def read_bmp(path):
    """-> (w, h, rows of (r, g, b)) for a 24-bit or 8-bit BMP, top row first"""
    b = open(path, 'rb').read()
    off, hdr, w, h, planes, bpp = struct.unpack_from('<IIiiHH', b, 10)
    top_down = h < 0
    h = abs(h)
    rows = []
    if bpp == 24:
        stride = (w * 3 + 3) & ~3
        for y in range(h):
            r = off + y * stride
            rows.append([(b[r + 3 * x + 2], b[r + 3 * x + 1], b[r + 3 * x]) for x in range(w)])
    elif bpp == 8:
        pal = [(b[14 + hdr + 4 * i + 2], b[14 + hdr + 4 * i + 1], b[14 + hdr + 4 * i]) for i in range(256)]
        stride = (w + 3) & ~3
        for y in range(h):
            r = off + y * stride
            rows.append([pal[b[r + x]] for x in range(w)])
    else:
        raise Exception('bmp %d bpp' % bpp)
    if not top_down:
        rows.reverse()
    return w, h, rows


# the layout of the 640 x 480 grid (src/app/settingsmenu.h), written out again here
PX, PY, PW, PH = 60, 32, 520, 416
NX, NY, NW, NH = 40, 24, 560, 432
TABS = ['Game', 'Picture', 'Sound', 'Controls', 'Saving']
ROWS = {'Game': ['info:realism', 'info:difficulty', 'realism', 'difficulty'],
        'Picture': ['panorama', 'panorama-filter', 'widescreen', 'picture', 'text', 'interface', 'fullscreen', 'display'],
        'Sound': ['audio', 'volume', 'volume-effects', 'volume-movies', 'volume-terminal', 'start-muted'],
        'Controls': ['pass-ms', 'mouse-sensitivity', 'pause-on-focus-loss'],
        'Saving': ['autosave', 'autosave-minutes', 'save-pages']}


def tab_xy(i):
    return PX + 16 + i * 98 + 47, PY + 44 + 13


def label_xy(r):
    return PX + 40, PY + 82 + 28 * r + 14


def value_xy(r):
    return PX + 262 + 130, PY + 82 + 28 * r + 14


def pick(tab, key):
    """script text: open tab and select the row of `key` by clicking its label"""
    t = TABS.index(tab)
    r = ROWS[tab].index(key)
    x, y = label_xy(r)
    return 'menu click %d,%d; menu click %d,%d' % (tab_xy(t) + (x, y))


# =================================================================================== A. the unit checks
print('--- A. model, input, layout, trailer (build/biosys-menutest)')
r = subprocess.run(['build/biosys-menutest'], capture_output=True, text=True)
for l in r.stdout.splitlines():
    if l.startswith('FAIL'):
        print(l)
n = sum(1 for l in r.stdout.splitlines() if l.startswith('PASS'))
check('menutest: %d checks, all passed' % n, r.returncode == 0 and 'checks passed, 0 failed' in r.stdout and n >= 85, r.stdout[-300:])

# =================================================================================== B. the mode inside the save file
print('--- B. the game mode inside the save file')
d = fresh('trailer')
rc, out, err = run('advance 300; menu trailer off; api SaveGame saved game 1.sav', d, '--realism', 'corrected', '--difficulty', 'guided')
rc2, out2, err2 = run('advance 300; api SaveGame saved game 2.sav', d, '--realism', 'corrected', '--difficulty', 'guided')
rc3, out3, err3 = run('advance 300; api SaveGame saved game 3.sav', d, '--realism', 'corrected', '--difficulty', 'guided')
check('three saves were written (the same game state, three runs)', rc == rc2 == rc3 == 0 and all(os.path.exists('%s/Saved Game %d.sav' % (d, i)) for i in (1, 2, 3)), (rc, err[-300:], out[-4:]))
check('no .settings file is written any more', not [f for f in os.listdir(d) if f.endswith('.settings')], os.listdir(d))
s1, s2, s3 = [open('%s/Saved Game %d.sav' % (d, i), 'rb').read() for i in (1, 2, 3)]
text = 'biosys-settings 1\nrealism=corrected\ndifficulty=guided\nfixes=\n'
check('without the trailer the file ends where the game\'s blob ends', len(s1) == blob_end(s1), (len(s1), blob_end(s1)))
check('with it the file is that file and the block: the original part is byte for byte the same',
      s2.startswith(s1) and s2[len(s1):] == trailer_block(text), (len(s2) - len(s1), s2[len(s1):][:40]))
check('the block names the realism, the difficulty and the fix list (magic, version 1, length, text, check sum)',
      s2[len(s1):len(s1) + 8] == b'BSYSMODE' and struct.unpack_from('<II', s2, len(s1) + 8) == (1, len(text)), s2[len(s1):len(s1) + 20])
check('two saves of the same state with the block are identical (runs repeat)', s2 == s3)
check('the engine part starts as the original\'s does (magic, version 0x12)', struct.unpack_from('<II', s2, 0) == (0xff5e67c0, 0x12))


def settings_line(out, nth=0):
    ls = [l for l in out if l.startswith('game settings ')]
    return ls[nth] if len(ls) > nth else None


_, o, _ = run('game settings', d)
FAITH = settings_line(o)
_, o, _ = run('game settings', d, '--realism', 'corrected', '--difficulty', 'guided')
CORR = settings_line(o)
_, o, _ = run('game settings', d, '--realism', 'closedloop', '--difficulty', 'survivor')
CLOSED = settings_line(o)
check('three different command lines give three different settings', len({FAITH, CORR, CLOSED}) == 3 and None not in (FAITH, CORR, CLOSED))

rc, o, err = run('game settings; api LoadGame -1 saved game 2.sav; game settings', d)
check('load under the default command line: the save\'s settings win', settings_line(o, 0) == FAITH and settings_line(o, 1) == CORR and 'rc=0' in last(o, 'api LoadGame'), (o[-4:], err))
check('one line says so', err.count('continues under the settings it was saved with') == 1 and 'realism corrected, difficulty guided' in err, err)
rc, o, err = run('api LoadGame -1 saved game 2.sav; game settings', d, '--realism', 'corrected', '--difficulty', 'guided')
check('a command line equal to the saved settings: loaded, no message', settings_line(o) == CORR and 'continues under' not in err, err)
rc, o, err = run('api LoadGame -1 saved game 2.sav; game settings; api LoadGame -1 newgame.sav; game settings', d)
check('NewGame.sav carries no settings: a new game goes back to the command line', settings_line(o, 0) == CORR and settings_line(o, 1) == FAITH, o[-4:])
rc, o, err = run('api LoadGame -1 saved game 2.sav; api SaveGame saved game 5.sav', d)
t5 = open(d + '/Saved Game 5.sav', 'rb').read()
check('saving a loaded game writes the settings in force, not the command line\'s', t5[blob_end(t5):] == trailer_block(text), t5[blob_end(t5):][:60])

# neither block nor settings file: the command line
rc, o, err = run('api LoadGame -1 saved game 1.sav; game settings', d, '--realism', 'closedloop', '--difficulty', 'survivor')
check('a save without block and without settings file loads under the command line', settings_line(o) == CLOSED and 'continues under' not in err and 'rc=0' in last(o, 'api LoadGame'), (o[-3:], err))

# a save of an earlier version: only the settings file next to it
side = 'biosys-settings 1\nrealism=closedloop\ndifficulty=survivor\nfixes=\nsave=%016x\n' % fnv1a64(s1)
open(d + '/Saved Game 1.settings', 'w').write(side)
rc, o, err = run('api LoadGame -1 saved game 1.sav; game settings', d)
check('a save with only a settings file (an earlier version) still loads under it', settings_line(o) == CLOSED and 'closedloop' in err, (o[-2:], err))
open(d + '/Saved Game 1.settings', 'w').write(side.replace('%016x' % fnv1a64(s1), '%016x' % (fnv1a64(s1) ^ 1)))
rc, o, err = run('api LoadGame -1 saved game 1.sav; game settings', d)
check('a settings file made for other bytes is ignored', settings_line(o) == FAITH, (o[-2:], err))
open(d + '/Saved Game 2.settings', 'w').write('biosys-settings 1\nrealism=closedloop\ndifficulty=survivor\nfixes=\nsave=%016x\n' % fnv1a64(s2))
rc, o, err = run('api LoadGame -1 saved game 2.sav; game settings', d)
check('the block wins over a settings file', settings_line(o) == CORR, o[-2:])
os.remove(d + '/Saved Game 1.settings')
os.remove(d + '/Saved Game 2.settings')

# damaged blocks: reported, treated as absent, never a crash
cut = s2[:-9]
flip = bytearray(s2)
flip[len(s1) + 20] ^= 0x40
garbage = s1 + b'BSYSMODE' + bytes(range(7, 120))
few = s1 + b'BSY'
junk = s1 + bytes((i * 37) & 255 for i in range(50))
for name, data, expect_load in (('cut', cut, True), ('flip', bytes(flip), True), ('garbage', garbage, True), ('few', few, True), ('junk', junk, False)):
    open('%s/Saved Game 9.sav' % d, 'wb').write(data)
    rc, o, err = run('api LoadGame -1 saved game 9.sav; game settings', d, '--realism', 'closedloop', '--difficulty', 'survivor')
    ok = rc == 0 and 'Traceback' not in err and 'Segmentation' not in err
    if expect_load:
        check('damaged block (%s): reported, loaded under the command line' % name,
              ok and 'rc=0' in last(o, 'api LoadGame') and settings_line(o) == CLOSED and 'ignored' in err and 'continues under' not in err, (rc, o[-2:], err[-300:]))
    else:
        check('bytes that are not our block are refused like any bad file, no crash', ok and 'rc=0' not in last(o, 'api LoadGame') and 'rc=' in last(o, 'api LoadGame'), (rc, o[-2:], err[-300:]))
os.remove(d + '/Saved Game 9.sav')

# a settings text that names an unknown word
bad = s1 + trailer_block('biosys-settings 1\nrealism=nonsense\ndifficulty=original\nfixes=\n')
open('%s/Saved Game 9.sav' % d, 'wb').write(bad)
rc, o, err = run('api LoadGame -1 saved game 9.sav; game settings', d)
check('a block with an unknown word is ignored and reported', rc == 0 and settings_line(o) == FAITH and 'ignored' in err, (o[-2:], err[-200:]))
os.remove('%s/Saved Game 9.sav' % d)

# the slot's stale settings file goes when the slot is written; nothing else
open(d + '/Saved Game 4.settings', 'w').write('x')
open(d + '/Saved Game 6.settings', 'w').write('x')
rc, o, err = run('api SaveGame saved game 4.sav', d)
check('a save over a slot removes that slot\'s settings file and no other', not os.path.exists(d + '/Saved Game 4.settings') and os.path.exists(d + '/Saved Game 6.settings'), os.listdir(d))
os.makedirs(d + '/Saved Game 7.sav')
open(d + '/Saved Game 7.settings', 'w').write('x')
rc, o, err = run('api SaveGame saved game 7.sav', d)
check('a save that fails leaves the settings file alone', os.path.exists(d + '/Saved Game 7.settings') and 'rc=0' not in last(o, 'api SaveGame'), (o[-2:], err[-200:]))
shutil.rmtree(d + '/Saved Game 7.sav')

# autosave: the same block, no settings file; rotation and F9's load take the settings from it
d2 = fresh('autosave')
rc, o, err = run('advance 300; autosave now; advance 300; autosave now', d2, '--autosave', 'yes', '--realism', 'corrected', '--difficulty', 'guided')
a1 = open(d2 + '/autosave1.sav', 'rb').read() if os.path.exists(d2 + '/autosave1.sav') else b''
check('autosaves carry the block and have no settings file', sorted(os.listdir(d2)) == ['autosave1.sav', 'autosave2.sav'] and a1[blob_end(a1):] == trailer_block(text), (os.listdir(d2), err[-200:]))
rc, o, err = run('game settings; autosave load; game settings', d2, '--autosave', 'yes')
check('loading an autosave continues under its settings', settings_line(o, 0) == FAITH and settings_line(o, 1) == CORR, o[-4:])

# =================================================================================== C. the New game screen (script)
print('--- C. the New game screen')


def centre(rect):
    return '%d,%d' % ((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)


txt = subprocess.run(['python3', '-I', PAGEPY, PAGES, 'dump', 'menutop'], capture_output=True, text=True).stdout
m = re.search(r'hot\[\d+\] newgame\s+rect=\((\d+), (\d+), (\d+), (\d+)\)', txt)
NEWGAME = centre(tuple(int(m.group(i)) for i in range(1, 5))) if m else '190,188'
LOG = ['--log-events']


def ev(out):
    return [l for l in out if l.startswith('event ')]


d = fresh('newgame')
# today's game: the menu's New game without the screen
rc, o, err = run('advance 100; key ESC; click %s; advance 1200; framehash; game settings; menu engine' % NEWGAME, d, *LOG)
ref_hash, ref_settings = last(o, 'framehash').split()[2], settings_line(o)
check('reference: New game from the menu without the screen starts at once', o.count('event menucommand 1 0') == 2 and ref_settings == FAITH and ref_hash, (o[-5:], err[-200:]))
# the same with the screen, default choice
rc, o, err = run('menu newgame on; advance 100; key ESC; click %s; menu poll; advance 600; menu key return; advance 1200; framehash; game settings; menu engine' % NEWGAME, d, *LOG)
st = [l for l in o if l.startswith('menu mode=')]
check('with the screen on, New game opens it first and starts nothing', len(st) >= 2 and st[0].startswith('menu mode=newgame') and 'realism=faithful difficulty=original' in st[0] and 'paused=1' in st[0]
      and o.index(st[0]) < [i for i, l in enumerate(o) if l == 'event menucommand 1 0'][1], st[:2])
check('the default choice starts exactly today\'s game (same settings; the frame after the same time is the same)', last(o, 'framehash').split()[2] == ref_hash and settings_line(o) == ref_settings,
      (last(o, 'framehash'), ref_hash))
check('the engine\'s menu is closed and the game is not paused afterwards', engine(o).get('paused') == '0' and o.count('event menucommand 1 0') == 2, engine(o))
# another choice
d3 = fresh('newgame2')
conf = d3 + '/biosys.conf'
open(conf, 'w').write('# my comment\nvolume = 70\n')
rc, o, err = run('menu newgame on; advance 100; key ESC; click %s; menu poll; menu key left; menu key down; menu key right; menu key up; menu key return; advance 300; game settings; menu engine; menu options' % NEWGAME,
                 d3, '--config', conf, *LOG)
check('another choice (Corrected, Guided) starts the game under those settings', settings_line(o) == CORR, (settings_line(o), err[-200:]))
check('... and the engine now holds them as the options a game without stored settings loads under', engine(o).get('cmdline') == 'corrected/guided' and engine(o).get('running') == 'corrected/guided', engine(o))
text_c = open(conf).read()
check('the choice is written to the preferences as the preselection (comments kept)', 'realism = corrected' in text_c and 'difficulty = guided' in text_c and '# my comment' in text_c, text_c)
rc, o, err = run('menu newgame on; advance 100; key ESC; click %s; menu poll' % NEWGAME, d3, '--config', conf, *LOG)
check('the next new game is preselected with it', 'realism=corrected difficulty=guided' in last(o, 'menu mode='), last(o, 'menu mode='))
# a save made in that game carries the choice
rc, o, err = run('menu newgame on; advance 100; key ESC; click %s; menu poll; menu key return; advance 300; api SaveGame saved game 1.sav' % NEWGAME, d3, '--config', conf)
sv = open(d3 + '/Saved Game 1.sav', 'rb').read()
check('a game started through the screen saves its mode in the file', sv[blob_end(sv):] == trailer_block(text), sv[blob_end(sv):][:50])
# Back
d4 = fresh('newgame3')
rc, o, err = run('menu newgame on; advance 100; key ESC; click %s; menu poll; menu key escape; advance 100; dump menu; game settings' % NEWGAME, d4, *LOG)
check('Back: no game was started (only the start-up\'s New game), the engine\'s menu is still up', o.count('event menucommand 1 0') == 1 and 'page menutop' in '\n'.join(o), o[-6:])
check('... and the screen is closed, the game not paused', 'mode=closed' in last(o, 'menu mode=') and 'paused=0' in last(o, 'menu mode='), last(o, 'menu mode='))
# scripted starts skip the screen
rc, o, err = run('menu state; newgame; advance 100; menu state; game settings', d4, '--realism', 'corrected', '--difficulty', 'guided')
check('a scripted new game (and --skip-intro) uses the options and shows no screen', settings_line(o) == CORR and all('mode=closed' in l for l in lines(o, 'menu mode=')), o[-4:])

# =================================================================================== D. the settings menu (script)
print('--- D. the settings menu')
d = fresh('menu')
rc, o, err = run('menu open; menu items; menu tab Picture; menu items; menu tab Sound; menu items; menu tab Controls; menu items; menu tab Saving; menu items; menu close', d, '--no-config')
for tab in TABS:
    pass
got = {}
cur = None
for l in o:
    if l.startswith('menu mode='):
        cur = kv(l)['tab']
    elif l.startswith('menu item '):
        got.setdefault(cur if cur else 'Game', []).append(kv(l)['key'])
check('the tabs and their rows are as listed: Game, Picture, Sound, Controls, Saving', got == ROWS, got)
rc, o, err = run('menu options', d)
optkeys = set(options(o))
menukeys = {k for ks in ROWS.values() for k in ks if not k.startswith('info:')}
check('every row names a preference of the option table', menukeys <= optkeys, menukeys - optkeys)
notin = optkeys - menukeys
check('the preferences not in the menu are exactly --game, --skip-intro, --mute, --fixes, display-less test aids', notin == {'game', 'skip-intro', 'mute', 'fixes'}, notin)
rc, o, err = run('menu textcheck', d)
check('every explanation fits two lines (three at most), every description three (four at most); the game\'s typeface is found',
      'help=2' in last(o, 'menu textcheck') and 'bad=0' in last(o, 'menu textcheck') and 'fontface=1' in last(o, 'menu textcheck'), last(o, 'menu textcheck'))

# pause, not while a text entry is up
rc, o, err = run('menu state; menu open; menu engine; menu close; menu engine; menu open; menu key down; menu key down; menu close; page fce9cab_o; api BeginTextInput 100,100,300,130 8 0; menu open; menu state; api EndTextInput 1',
                 d, '--no-config', *LOG)
check('open pauses the game, close lets it go on (once per menu: one appactive event pair each, however many keys)', 'paused=1' in lines(o, 'menu mode=')[1] and
      [l for l in o if l.startswith('event appactive')] == ['event appactive 0', 'event appactive 1'] * 2, [l for l in o if 'appactive' in l])
check('not while the game asks for typed text', 'menu open refused' in o and 'mode=closed' in last(o, 'menu mode='), o[-4:])

# every live item changes the state it names
cfg = fresh('prefs') + '/biosys.conf'
open(cfg, 'w').write('# my own comment\nvolume = 70\n# volume-effects = 100\nbogus-key = 1\n')
KEY = 'menu key '
script = ';'.join([
    'menu open',
    # Sound
    pick('Sound', 'audio'), KEY + 'right',
    pick('Sound', 'volume'), KEY + 'left', KEY + 'left',
    pick('Sound', 'volume-effects'), KEY + 'left',
    pick('Sound', 'volume-movies'), KEY + 'left', KEY + 'left', KEY + 'left',
    pick('Sound', 'volume-terminal'), KEY + 'left', KEY + 'left', KEY + 'left', KEY + 'left',
    pick('Sound', 'start-muted'), KEY + 'return',
    # Controls
    pick('Controls', 'pass-ms'), KEY + 'right',
    pick('Controls', 'mouse-sensitivity'), KEY + 'right', KEY + 'right',
    pick('Controls', 'pause-on-focus-loss'), KEY + 'return',
    # Saving
    pick('Saving', 'autosave'), KEY + 'return',
    pick('Saving', 'autosave-minutes'), KEY + 'right',
    pick('Saving', 'save-pages'), KEY + 'right',
    # Picture
    pick('Picture', 'panorama-filter'), KEY + 'right',
    pick('Picture', 'picture'), KEY + 'right',
    # Game
    pick('Game', 'realism'), KEY + 'right',
    pick('Game', 'difficulty'), KEY + 'left',
    'menu engine', 'menu options', 'menu close'])
rc, o, err = run(script, d, '--config', cfg, '--autosave', 'yes')
en, op = engine(o), options(o)
check('audio mode: the mixer is in the enhanced mode', en.get('audio') == 'enhanced', en)
check('master 70 -> 60, effects 100 -> 95, movies -> 85, terminal -> 80: the mixer\'s groups', (en.get('master'), en.get('effects'), en.get('movies'), en.get('terminal')) == ('60', '95', '85', '80'), en)
check('muted: the mixer is muted', en.get('muted') == '1', en)
check('pace 33 -> 34 ms: the engine\'s pass interval', en.get('passms') == '34', en)
check('mouse sensitivity 1 -> 1.1 (two steps of 5 percent)', op.get('mouse-sensitivity') == '1.1', op.get('mouse-sensitivity'))
check('pause on focus loss: off', op.get('pause-on-focus-loss') == 'no', op.get('pause-on-focus-loss'))
check('autosave off, 11 minutes (the planner)', en.get('autosave') == '0' and en.get('autosaveminutes') == '11', en)
check('save pages 4 -> 5 (the engine)', en.get('slotpages') == '5', en)
check('panorama filter nearest; clean picture: smooth panorama and sharp text come with it (text tracking in the engine)',
      op.get('panorama-filter') == 'nearest' and op.get('picture') == 'clean' and op.get('panorama') == 'smooth' and op.get('text') == 'sharp' and en.get('texttracking') == '1',
      (op, en))
check('realism / difficulty: the next new game (and what a save without settings loads under); the running game is untouched',
      en.get('cmdline') == 'corrected/guided' and en.get('running') == 'faithful/original', en)
body = open(cfg).read()
act = dict(re.findall(r'^([a-z-]+) = (.*)$', body, re.M))
check('the preferences file holds every change as key = value',
      act.get('volume') == '60' and act.get('volume-effects') == '95' and act.get('volume-movies') == '85' and act.get('volume-terminal') == '80' and act.get('start-muted') == 'yes' and
      act.get('audio') == 'enhanced' and act.get('pass-ms') == '34' and act.get('mouse-sensitivity') == '1.1' and act.get('pause-on-focus-loss') == 'no' and act.get('autosave') == 'no' and
      act.get('autosave-minutes') == '11' and act.get('save-pages') == '5' and act.get('panorama-filter') == 'nearest' and act.get('picture') == 'clean' and act.get('panorama') == 'smooth' and
      act.get('text') == 'sharp' and act.get('realism') == 'corrected' and act.get('difficulty') == 'guided', act)
check('comments and unknown lines are kept, the commented default stays', '# my own comment' in body and 'bogus-key = 1' in body and '# volume-effects = 100\nvolume-effects = 95' in body, body)
# the screen row (one screen here), widescreen and text round trip
rc, o, err = run(';'.join(['menu open', pick('Picture', 'widescreen'), KEY + 'return', 'menu options', pick('Picture', 'panorama'), KEY + 'left', 'menu options',
                            pick('Picture', 'text'), KEY + 'right', KEY + 'left', 'menu engine', 'menu options', 'menu close']), fresh('deps'), '--no-config')
ops = [kv('x ' + l[len('menu option '):].replace('=', ' ', 0)) for l in []]
allopts = [l for l in o if l.startswith('menu option ')]
first = dict(l[len('menu option '):].split('=', 1) for l in allopts[:len(allopts) // 3])
second = dict(l[len('menu option '):].split('=', 1) for l in allopts[len(allopts) // 3:2 * len(allopts) // 3])
third = dict(l[len('menu option '):].split('=', 1) for l in allopts[2 * len(allopts) // 3:])
check('widescreen on brings the smooth panorama', first.get('widescreen') == 'yes' and first.get('panorama') == 'smooth', first)
check('panorama back to original takes widescreen off', second.get('panorama') == 'original' and second.get('widescreen') == 'no', second)
check('text sharp and back to original: tracking off in the engine', third.get('text') == 'original' and engine(o).get('texttracking') == '0', (third, engine(o)))

# no preferences file: nothing is written, the menu says so
d5 = fresh('noconfig')
rc, o, err = run('menu open; ' + pick('Sound', 'volume') + '; menu key left; menu key left; menu state; menu engine; menu close', d5, '--no-config')
check('--no-config: the change applies, nothing is written (no file at all), the menu knows', engine(o).get('master') == '90' and os.listdir(d5) == [] and 'persistent=0' in last(o, 'menu mode='), (os.listdir(d5), last(o, 'menu mode=')))
check('with a preferences file the menu says so', 'persistent=1' in last(run('menu open; menu state', fresh('p2'), '--config', cfg)[1], 'menu mode='))

# opened and closed without a change: the game and the preferences are as they were
d6 = fresh('same')
cfg2 = d6 + '/biosys.conf'
open(cfg2, 'w').write('# c\nvolume = 70\n')
rc, oa, err = run('advance 2000; framehash; game dump', d6, '--config', cfg2)
rc, ob, err = run('advance 1000; menu open; menu key down; menu key up; menu key tab; menu key tab; menu key escape; advance 1000; framehash; game dump', d6, '--config', cfg2)
ga = [l for l in oa if l.startswith('game ')]
gb = [l for l in ob if l.startswith('game ')]
ta, tb = int(re.search(r'game time=(\d+)', ga[0]).group(1)), int(re.search(r'game time=(\d+)', gb[0]).group(1))
check('a menu opened and closed without a change leaves the game state and the picture as they were (the game\'s clock is stamped at the resume, as after a lost focus: at most one step)',
      last(oa, 'framehash') == last(ob, 'framehash') and ga[1:] == gb[1:] and abs(ta - tb) <= 20 and re.sub(r'time=\d+|tod=\d+|clock=\S+|lasttick=\d+', '', ga[0]) == re.sub(r'time=\d+|tod=\d+|clock=\S+|lasttick=\d+', '', gb[0]),
      (last(oa, 'framehash'), last(ob, 'framehash'), ga[:2], gb[:2]))
check('... and the preferences file as it was', open(cfg2).read() == '# c\nvolume = 70\n', open(cfg2).read())

# the engine's frame never holds the menu
rc, o, err = run('framehash' , d6, '--no-config')
fh = lines(o, 'framehash')
rc, o2, err = run('menu open; framehash; shot %s/e.bmp; menu shot %s/f.bmp; menu close' % (d6, d6), d6, '--no-config')
rc, o3, err = run('framehash; shot %s/g.bmp' % d6, d6, '--no-config')
check('the 640 x 480 frame is the same with the menu up as without it (the menu is an overlay, never drawn into it)',
      last(o2, 'framehash') == last(o3, 'framehash') and open(d6 + '/e.bmp', 'rb').read() == open(d6 + '/g.bmp', 'rb').read(), (last(o2, 'framehash'), last(o3, 'framehash')))

# =================================================================================== E. the pictures
print('--- E. pictures of the composed window')
d7 = fresh('shots')
steps = ['advance 100', 'shot %s/frame.bmp' % d7, 'menu open']
for i, tab in enumerate(TABS):
    steps += ['menu tab %s' % tab, 'menu shot %s/menu_%s.bmp 1280 960' % (SHOTS, tab.lower())]
steps += ['menu tab Picture', 'menu shot %s/menu_3440x1440.bmp 3440 1440' % SHOTS, 'menu shot %s/menu_1x.bmp 640 480' % d7, 'menu close']
rc, o, err = run(';'.join(steps), d7, '--no-config')
check('pictures of every tab were written', rc == 0 and all(os.path.exists('%s/menu_%s.bmp' % (SHOTS, t.lower())) for t in TABS) and os.path.exists(SHOTS + '/menu_3440x1440.bmp'), (rc, err[-300:]))
fw, fh_, frows = read_bmp(d7 + '/frame.bmp')


def panel_check(path, W, H, k, px, py, pw, ph, name, dim=True):
    w, h, rows = read_bmp(path)
    x0, y0 = (W - 640 * k) // 2, (H - 480 * k) // 2
    ok_size = abs(w - W) <= 4 and h == H
    x0 = (w - 640 * k) // 2
    # outside the panel the frame shows, darkened by about 59 percent (alpha 150 of 255)
    gx, gy = 12, 240
    fx, fy = gx, gy
    src = frows[fy][fx]
    got = rows[y0 + gy * k + k // 2][x0 + gx * k + k // 2]
    want = tuple(c * 105 // 255 for c in src)
    dimmed = all(abs(a - b) <= 3 for a, b in zip(got, want)) or not dim
    # the panel's border, 2 grid pixels wide, at its left edge, in the middle
    bx, by = x0 + px * k + k // 2, y0 + (py + ph // 2) * k
    border = rows[by][bx]
    bordered = abs(border[0] - 118) < 8 and abs(border[1] - 160) < 8 and abs(border[2] - 128) < 8
    # centred: the same distance from the middle of the window on both sides
    rb = rows[by][x0 + (px + pw) * k - k // 2 - 1]
    right = abs(rb[0] - 118) < 8 and abs(rb[1] - 160) < 8
    check('%s: %dx%d, the picture behind is darkened, the panel sits on the 640 x 480 grid in the middle (border left and right)' % (name, W, H),
          ok_size and dimmed and bordered and right, (w, h, got, want, border, rb))
    bright = sum(1 for y in range(y0 + (py + 8) * k, y0 + (py + ph - 8) * k, max(1, k // 2)) for x in range(x0 + (px + 8) * k, x0 + (px + pw - 8) * k, max(1, k // 2)) if sum(rows[y][x]) > 600)
    check('%s: text is there (bright glyph pixels in the panel)' % name, bright > 300 * k, bright)
    return rows


rows2 = {}
for tab in TABS:
    rows2[tab] = panel_check('%s/menu_%s.bmp' % (SHOTS, tab.lower()), 1280, 960, 2, PX, PY, PW, PH, 'menu_%s' % tab.lower())
sig = {t: hashlib.md5(b''.join(bytes(c for px in r[200:700] for c in px) for r in rows2[t][300:900:7])).hexdigest() for t in TABS}
check('the five tabs look different', len(set(sig.values())) == 5)
panel_check(SHOTS + '/menu_3440x1440.bmp', 3440, 1440, 3, PX, PY, PW, PH, 'menu at 3440x1440')
panel_check(d7 + '/menu_1x.bmp', 640, 480, 1, PX, PY, PW, PH, 'menu at 640x480')

# the New game screen and the hint
steps = ['menu newgame on', 'advance 100', 'key ESC', 'menu shot %s/hint.bmp 1280 960' % d7, 'click %s' % NEWGAME, 'menu poll', 'menu shot %s/menu_newgame.bmp 1280 960' % SHOTS,
         'menu shot %s/newgame_3440.bmp 3440 1440' % d7]
rc, o, err = run(';'.join(steps), fresh('ngshot'), '--no-config')
fx0, fy0, frows = read_bmp(d7 + '/frame.bmp')
panel_check(SHOTS + '/menu_newgame.bmp', 1280, 960, 2, NX, NY, NW, NH, 'menu_newgame', dim=False)
panel_check(d7 + '/newgame_3440.bmp', 3440, 1440, 3, NX, NY, NW, NH, 'new game screen at 3440x1440', dim=False)
hw, hh, hrows = read_bmp(d7 + '/hint.bmp')
corner = [rows for rows in (hrows[y][1000:1270] for y in range(900, 950))]
lit = sum(1 for r_ in corner for px in r_ if sum(px) > 500)
check('while the engine\'s menu is shown the corner has the "F10 settings" hint (an overlay on the picture)', lit > 150, lit)
rc, o, err = run('advance 100; menu shot %s/nohint.bmp 1280 960' % d7, fresh('nohint'), '--no-config')
nw, nh, nrows = read_bmp(d7 + '/nohint.bmp')
lit2 = sum(1 for y in range(900, 950) for px in nrows[y][1000:1270] if sum(px) > 500)
check('on a game page there is no hint', lit2 < 20, lit2)

# =================================================================================== F. the window (SDL dummy drivers)
print('--- F. the window path')
# F10 and Esc: open and close; nothing reaches the game
d8 = fresh('win1')
rc, o, err = window(d8, 'f10,escape,f10,f10', 6500, '--no-config', '--log-events')
mlive = lines(o, 'menulive')
check('F10 opens the menu (the game pauses), Esc closes it, F10 opens and closes it again',
      [l.split()[1] for l in mlive] == ['open', 'close', 'open', 'close'] and 'paused=1 menu=1' in mlive[0] and 'paused=0 menu=0' in mlive[1], (mlive, err[-300:]))
evs = ev(o)
check('no key of that reaches the game (no F10, no Escape, no key at all), nothing opens the engine\'s menu (the only New game is the start-up\'s)',
      not [e for e in evs if e.startswith('event keydown') or e.startswith('event keyup')] and evs.count('event menucommand 1 0') == 1 and not [e for e in evs if e.startswith('event menucommand') and e != 'event menucommand 1 0'], [e for e in evs if 'key' in e or 'menucommand' in e])
rc, o, err = window(fresh('win1b'), 'escape', 4000, '--no-config', '--log-events')
check('(control) without the menu Esc does reach the game', 'event keydown vk27' in ev(o), ev(o)[:5])
rc, o, err = window(fresh('win1c'), 'f10,cmd-comma,cmd-comma,cmd-comma', 6500, '--no-config')
check('Cmd+, opens and closes it too (F10 first, then Cmd+, closes, opens, closes)', [l.split()[1] for l in lines(o, 'menulive')] == ['open', 'close', 'open', 'close'], lines(o, 'menulive'))
# New game chosen in the main menu during a session that was started with --skip-intro / --load-autosave: the screen appears (only the automatic first start skips it)
nx, ny = NEWGAME.split(',')
rc, o, err = window(fresh('win_ng1'), 'escape,click@%s:%s' % (nx, ny), 7000, '--no-config', '--log-events')
ml = lines(o, 'menulive')
check('--skip-intro: New game chosen in the menu opens the New game screen (and starts nothing yet: the start-up\'s New game is the only one so far)',
      ml and ml[0].split()[1] == 'open' and ev(o).count('event menucommand 1 0') == 1, (ml, ev(o)[-4:], err[-300:]))
dA = fresh('win_ng2')
rcA, oA, errA = run('autosave now', dA, '--autosave', 'yes')
rc, o, err = window(dA, 'escape,click@%s:%s' % (nx, ny), 7000, '--no-config', '--log-events', '--load-autosave')
ml = lines(o, 'menulive')
check('--load-autosave: the same, the screen appears when New game is chosen later', ml and ml[0].split()[1] == 'open' and ev(o).count('event menucommand 1 0') == 1,
      (ml, ev(o)[-4:], errA[-200:], err[-300:]))
# the mouse: tabs, rows, slider drag, wheel is covered by the unit checks; the Close button
d9 = fresh('win2')
cfgw = d9 + '/biosys.conf'
open(cfgw, 'w').write('# kept\n')
tx, ty = tab_xy(2)
lx, ly = label_xy(1)
tr0 = PX + 262 + 4
close_x, close_y = PX + PW - 96 + 40, PY + 8 + 13
keys = ','.join(['f10', 'click@%d:%d' % (tx, ty), 'click@%d:%d' % (lx, ly), 'drag@%d:%d:%d:%d' % (tr0 + 2, ly, tr0 + 75, ly), 'click@%d:%d' % (close_x, close_y)])
rc, o, err = window(d9, keys, 7800, '--config', cfgw, '--log-events')
act = dict(re.findall(r'^([a-z-]+) = (.*)$', open(cfgw).read(), re.M))
vol = int(act.get('volume', '-1'))
check('mouse in the window: a click on the Sound tab, on the master volume row, a drag on its slider to the middle, then Close', 40 <= vol <= 60 and [l.split()[1] for l in lines(o, 'menulive')][-1] == 'close' and 'menu=0' in last(o, 'menulive'),
      (act, lines(o, 'menulive')[-3:], err[-300:]))
check('... the preferences were written (once the drag ended) and nothing of it reached the game',
      '# kept' in open(cfgw).read() and not [e for e in ev(o) if e.startswith(('event hotspot', 'event rightclick'))], [e for e in ev(o) if 'hotspot' in e or 'click' in e][:5])
# live picture layers in the window: panorama, filter, widescreen, picture clean (sharp text), text, full screen
d10 = fresh('win3')
cfg3 = d10 + '/biosys.conf'
open(cfg3, 'w').write('# kept\n')
keys = ','.join(['f10', 'tab',
                 'right', 'down', 'right', 'down', 'return', 'down', 'return', 'down', 'return', 'escape'])
rc, o, err = window(d10, keys, 8800, '--config', cfg3, '--no-config' if False else '--mute')
sets = lines(o, 'menulive set')
check('panorama smooth, filter nearest, widescreen, clean picture, text original: each switches the window\'s own state at once',
      len(sets) >= 6 and 'smooth=1' in sets[0] and 'linear=0' in sets[1] and 'wide=1' in sets[2] and any('clean=1' in l for l in sets) and any('sharp=1' in l and 'clean=1' in l for l in sets)
      and 'sharp=0' in sets[-1] and 'clean=0' in sets[-1], sets)
act = dict(re.findall(r'^([a-z-]+) = (.*)$', open(cfg3).read(), re.M))
check('... and into the preferences file', act.get('panorama') == 'smooth' and act.get('panorama-filter') == 'nearest' and act.get('widescreen') == 'yes' and act.get('picture') == 'original' and act.get('text') == 'original', act)
# audio mode and pace and the focus pause in the window; the opened menu paused the game
d11 = fresh('win4')
cfg4 = d11 + '/biosys.conf'
open(cfg4, 'w').write('')
keys = ','.join(['f10', 'tab', 'tab', 'right', 'tab', 'right', 'down', 'down', 'return', 'escape'])
rc, o, err = window(d11, keys, 7600, '--config', cfg4)
sets = lines(o, 'menulive set')
check('audio mode enhanced (the device is opened again), pace 34 ms, pause on focus loss off: the window\'s own state',
      any('audio=enhanced' in l for l in sets) and any('pass=34' in l for l in sets) and any('pause=0' in l for l in sets), (sets, err[-300:]))
# the window's picture with the menu on the dummy driver, at 2x and at 3440x1440 with widescreen
for name, size, extra in (('window_2x', '1280x960', []), ('window_3440', '3440x1440', ['--widescreen'])):
    out_bmp = '%s/%s.bmp' % (SHOTS, name)
    if os.path.exists(out_bmp):
        os.remove(out_bmp)
    rc, o, err = window(fresh('shot_' + name), 'f10', 4800, '--no-config', '--mute', '--window-size', size, '--test-shot', out_bmp, *extra)
    W, H = map(int, size.split('x'))
    k = min(W // 640, H // 480)
    if os.path.exists(out_bmp):
        w, h, rows = read_bmp(out_bmp)
        x0 = (W - 640 * k) // 2
        by = H // 2
        pl = rows[by][x0 + PX * k + 1]
        pr = rows[by][x0 + (PX + PW) * k - 2]
        x0 = (w - 640 * k) // 2
        ok = abs(w - W) <= 4 and h == H and all(abs(a - b) < 10 for a, b in zip(pl, (118, 160, 128))) and all(abs(a - b) < 10 for a, b in zip(pr, (118, 160, 128)))
        check('the real window draws the menu centred on the grid (%s%s): border at the left and the right of the panel' % (size, ' --widescreen' if extra else ''), ok, (w, h, pl, pr))
    else:
        check('the real window wrote its test picture (%s)' % size, False, (rc, err[-300:], o[-3:]))

# =================================================================================== G. the window: the New game screen
print('--- G. the window: New game')
d12 = fresh('win5')
cfg5 = d12 + '/biosys.conf'
open(cfg5, 'w').write('# kept\n')
NGX, NGY = map(int, NEWGAME.split(','))


def winrun(name, keys, ms, conf='# kept\n', *opts):
    d = fresh(name)
    cf = d + '/biosys.conf'
    open(cf, 'w').write(conf)
    r = subprocess.run(['build/biosys', '--game', '--intro', '--mute', '--config', cf, '--save-dir', d, '--quit-after-ms', str(ms), '--test-keys', keys, '--log-events'],
                       capture_output=True, text=True, env=ENV)
    return d, cf, r.stdout.splitlines(), r.stderr


def auto_mode(d):
    p = d + '/autosave1.sav'
    if not os.path.exists(p):
        return None
    a = open(p, 'rb').read()
    return a[blob_end(a):]


# the original start-up (title, intro, Esc for the main menu): New game asks first
d13, cf, o, err = winrun('win5', 'escape,click@%d:%d,return' % (NGX, NGY), 9000)
ml = lines(o, 'menulive')
check('start-up flow: New game in the main menu opens the New game screen (the game pauses), Start closes it', [l.split()[1] for l in ml] == ['open', 'close'] and 'menu=1' in ml[0], (ml, err[-300:]))
check('the default choice starts the game as today (NewGame.sav is loaded) and writes nothing into the preferences', 'event loadgame NewGame.sav offset=37360' in o and open(cf).read() == '# kept\n', (ev(o)[:12], open(cf).read()))
check('the autosave of the quit holds the default mode', auto_mode(d13) == trailer_block('biosys-settings 1\nrealism=faithful\ndifficulty=original\nfixes=\n'), auto_mode(d13))
d14, cf, o, err = winrun('win6', 'escape,click@%d:%d,left,down,return' % (NGX, NGY), 9000)
check('another choice in the window (Corrected, Original): the game starts under it, the choice is the next preselection in the file',
      'event loadgame NewGame.sav offset=37360' in o and 'realism = corrected' in open(cf).read() and '# kept' in open(cf).read() and 'difficulty' not in open(cf).read(), (open(cf).read(), err[-300:]))
check('... and the first save of that game carries it', auto_mode(d14) == trailer_block('biosys-settings 1\nrealism=corrected\ndifficulty=original\nfixes=\n'), auto_mode(d14))
d15, cf, o, err = winrun('win7', 'escape,click@%d:%d,escape' % (NGX, NGY), 7500)
check('Back (Esc): nothing started, the engine\'s menu stays up', 'event loadgame NewGame.sav offset=37360' not in o and [l.split()[1] for l in lines(o, 'menulive')] == ['open', 'close'], ev(o)[:10])
# the hint in the real window
hint_bmp = d15 + '/hint.bmp'
subprocess.run(['build/biosys', '--game', '--intro', '--mute', '--no-config', '--save-dir', d15, '--quit-after-ms', '4800', '--test-keys', 'escape', '--window-size', '1280x960',
                '--test-shot', hint_bmp], capture_output=True, text=True, env=ENV)
if os.path.exists(hint_bmp):
    w, h, rows = read_bmp(hint_bmp)
    lit = sum(1 for y in range(900, 950) for px in rows[y][1000:1270] if sum(px) > 500)
    check('the real window shows "F10 settings" while the engine\'s main menu is up (an overlay)', lit > 150, lit)
else:
    check('the real window wrote its picture of the main menu', False)

print('%d checks, %d failed' % (count, failed))
sys.exit(1 if failed else 0)
