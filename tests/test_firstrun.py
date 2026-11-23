"""The first-run screen (src/app/firstrun*.{h,cpp}, build/biosys with SDL_VIDEODRIVER=dummy) and its logic (build/biosys-firstruntest)."""
import os, re, shutil, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
DEV = os.environ.get('BIOSYS_DEV_ROOT') or os.path.dirname(ROOT)
SETUP_ZIP = DEV + '/setup-cd/Biosys_Win_EN_Setup-CD.zip'
GAME_ZIP = DEV + '/game-cd/Biosys_Win_EN_Game-CD.zip'
APP = 'build/biosys'
tmp = tempfile.mkdtemp(prefix='biosys-firstrun-test-')
passed = failed = 0


def check(name, cond, detail=''):
    global passed, failed
    if cond:
        passed += 1
        print('ok   ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (': ' + str(detail)[:600] if detail else ''))


ENV = {k: v for k, v in os.environ.items() if k not in ('BIOSYS_DATA', 'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER')}
ENV.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def run(args, timeout=600):
    p = subprocess.run([APP] + args, capture_output=True, text=True, env=ENV, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def frlines(out):
    return [l for l in out.splitlines() if l.startswith('firstrun: ')]


def bmp(path):
    d = open(path, 'rb').read()
    off, w, h, bpp = struct.unpack_from('<I', d, 10)[0], struct.unpack_from('<i', d, 18)[0], struct.unpack_from('<i', d, 22)[0], struct.unpack_from('<H', d, 28)[0]
    bpp //= 8
    stride = (w * bpp + 3) // 4 * 4
    top_down = h < 0
    h = abs(h)

    def px(x, y):
        row = y if top_down else h - 1 - y
        o = off + row * stride + x * bpp
        return d[o + 2], d[o + 1], d[o]
    return w, h, px


def leftovers(home):
    bad = []
    for dp, dn, fn in os.walk(home):
        for n in dn + fn:
            if n.endswith('.partial') or n.endswith('.tmp') or n == '.tmp':
                bad.append(os.path.join(dp, n))
    return bad


def import_status(home):
    p = subprocess.run(['build/biosys-import', 'status', home], capture_output=True, text=True)
    return p.stdout


try:
    # ---- the logic header
    p = subprocess.run(['build/biosys-firstruntest'], capture_output=True, text=True)
    for l in p.stdout.splitlines():
        if l.startswith('FAIL'):
            print(l)
    n = sum(1 for l in p.stdout.splitlines() if l.startswith('PASS'))
    check('firstruntest: %d unit cases of the logic' % n, p.returncode == 0 and 'all checks passed' in p.stdout and n >= 35, p.stdout[-300:])

    # ---- --version
    p = subprocess.run([APP, '--version'], capture_output=True, text=True, env=ENV)
    check('--version prints "Biosys Remaster <version>"', p.returncode == 0 and re.fullmatch(r'Biosys Remaster \S+\n', p.stdout) is not None, p.stdout)

    # ---- the idle window: both missing, a picture with the gradient
    H = tmp + '/home-idle'
    shot = tmp + '/idle.bmp'
    rc, out, err = run(['--data-home', H, '--firstrun-log', '--firstrun-shot', shot, '--quit-after-ms', '1500'])
    L = frlines(out)
    check('idle window: exit 0, log says both missing and nothing else', rc == 0 and L == ['firstrun: setup=missing game=missing msg=""'], (rc, out, err))
    check('idle window: no game data was written', not os.path.exists(H) or os.listdir(H) == [], os.listdir(H) if os.path.exists(H) else '')
    ok = os.path.exists(shot)
    check('idle window: the picture exists', ok)
    if ok:
        w, h, px = bmp(shot)
        colours = {px(x, y) for y in range(0, h, 3) for x in range(0, w, 3)}
        check('idle window: 960 x 600 and not a flat colour', (w, h) == (960, 600) and len(colours) > 40, (w, h, len(colours)))
        top, bottom = px(10, 0), px(10, h - 1)
        check('idle window: the background is a gradient, greener / brighter at the top', sum(top) > sum(bottom) + 40 and top[1] > bottom[1] + 40, (top, bottom))
        check('idle window: the top row is #14584a (+- 3)', all(abs(a - b) <= 3 for a, b in zip(top, (0x14, 0x58, 0x4a))), top)
        green = sum(1 for y in range(150, 215) for x in range(580, 860, 4) if px(x, y)[1] > 190 and px(x, y)[0] > 120)
        check('idle window: the primary button is #8fe07a', green > 500, green)

    # ---- a text file is not a disc
    txt = tmp + '/notes.txt'
    open(txt, 'w').write('hello\n')
    ident = subprocess.run(['build/biosys-import', 'identify', txt], capture_output=True, text=True).stdout
    why = ident.split(' - ', 1)[1].strip() if ' - ' in ident else '?'
    rc, out, err = run(['--data-home', tmp + '/home-txt', '--firstrun-log', '--test-drop', txt, '--quit-after-ms', '2200'])
    L = frlines(out)
    check('text file: the message is the identify sentence, both discs still missing',
          rc == 0 and len(L) >= 2 and L[-1] == 'firstrun: setup=missing game=missing msg="%s"' % why.replace('"', "'"), (rc, L, why))

    # ---- both real zips
    have = os.path.exists(SETUP_ZIP) and os.path.exists(GAME_ZIP)
    check('the two CD zips are there (%s)' % DEV, have)
    if have:
        H = tmp + '/home-full'
        shot = tmp + '/done.bmp'
        rc, out, err = run(['--data-home', H, '--firstrun-only', '--firstrun-log', '--firstrun-shot', shot, '--test-drop', GAME_ZIP, '--test-drop', SETUP_ZIP])
        L = frlines(out)
        for who in ('setup', 'game'):
            seen = [re.search(who + r'=(missing|queued|unpack|extract|done)', l).group(1) for l in L]
            order = [s for i, s in enumerate(seen) if i == 0 or s != seen[i - 1]]
            check('both zips: %s goes through unpack, extract, done' % who, 'unpack' in order and 'extract' in order and order[-1] == 'done' and order.index('unpack') < order.index('extract'), order)
        check('both zips: the second disc waited in the queue', any('setup=queued' in l and 'game=unpack' in l for l in L))
        check('both zips: exit 0, last line is all done', rc == 0 and L[-1].startswith('firstrun: setup=done game=done msg="All set'), (rc, L[-1:], err[-300:]))
        check('both zips: biosys-import status says complete', 'complete: yes' in import_status(H), import_status(H))
        check('both zips: no .partial / .tmp left', not leftovers(H), leftovers(H))
        check('both zips: the picture of the last state exists', os.path.exists(shot))
        if os.path.exists(shot):
            w, h, px = bmp(shot)
            ring = sum(1 for y in range(330, 450) for x in (101, 102, 491, 492) if px(x, y)[1] > 200 and px(x, y)[0] > 120 and px(x, y)[0] < 180)
            check('both zips: the cards have a green outline when done', ring > 150, ring)

        # ---- the next start: nothing to ask, the game window starts
        S = tmp + '/save'
        rc, out, err = run(['--data-home', H, '--play', '--mute', '--no-config', '--save-dir', S, '--firstrun-log', '--quit-after-ms', '3000'])
        check('second start: no first-run line, the game window starts and quits with 0', rc == 0 and not frlines(out), (rc, out[-300:], err[-300:]))
        rc, out, err = run(['--data-home', H, '--firstrun-only', '--firstrun-log'])
        check('--firstrun-only with everything imported exits 0 at once', rc == 0 and not frlines(out), (rc, out, err))

        # ---- quit during an import: exit 0, nothing half-written
        H2 = tmp + '/home-cancel'
        rc, out, err = run(['--data-home', H2, '--firstrun-log', '--test-drop', GAME_ZIP, '--quit-after-ms', '1900'])
        L = frlines(out)
        check('quit during unpack: it was unpacking, never done', any('game=unpack' in l for l in L) and not any('game=done' in l for l in L), L[-2:])
        check('quit during unpack: exit 0, no .partial / .tmp left, nothing complete', rc == 0 and not leftovers(H2) and 'complete: yes' not in import_status(H2) and 'game: no' in import_status(H2),
              (rc, leftovers(H2), err[-300:]))

        # ---- one disc imported in an earlier run shows as done from the start; dropping it again says so
        H3 = tmp + '/home-half'
        rc, out, err = run(['--data-home', H3, '--firstrun-log', '--test-drop', GAME_ZIP, '--quit-after-ms', '7000'])
        L = frlines(out)
        check('half: the game disc alone ends with the hint for the other', rc == 0 and L and L[-1] == 'firstrun: setup=missing game=done msg="The Game CD is done. Now drop the Setup CD."', (rc, L[-2:], err[-200:]))
        rc, out, err = run(['--data-home', H3, '--firstrun-log', '--test-drop', GAME_ZIP, '--quit-after-ms', '2300'])
        L = frlines(out)
        check('half: next run starts with the game disc done', L and L[0].startswith('firstrun: setup=missing game=done'), L[:1])
        check('half: dropping it again says it is already imported, nothing is redone', L and 'already imported' in L[-1] and not any('unpack' in l for l in L), L[-2:])

    # ---- headless without data: an explanatory error (--data-home is given, so the development roots are not used)
    rc, out, err = run(['--data-home', tmp + '/home-none', '--script', 'dump'])
    check('headless without data: exit 1 and the Error says how to get it', rc == 1 and 'not here yet' in err and 'two original CDs' in err and 'Traceback' not in err, (rc, err))
    rc, out, err = run(['--data-home', tmp + '/home-none', '--screenshot', tmp + '/x.bmp'])
    check('--screenshot without data: the same Error', rc == 1 and 'not here yet' in err, (rc, err))

    # ---- no personal path in what the first-run work owns
    bad = []
    for f in ('src/app/main.cpp', 'src/app/firstrun.cpp', 'src/app/firstrun.h', 'src/app/firstrunlogic.h', 'src/engine/datafiles.cpp', 'src/engine/datafiles.h', 'src/tools/firstruntest.cpp',
              'tests/test_firstrun.py'):
        if '/Us' + 'ers/' in open(f).read():
            bad.append(f)
    bad += ['src/engine/engine.cpp'] if '/Us' + 'ers/' in open('src/engine/engine.cpp').read() else []
    check('no personal path in the app, datafiles, engine.cpp and this test', not bad, bad)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print('firstrun: %d passed, %d failed' % (passed, failed))
sys.exit(1 if failed else 0)
