"""Menu, saved games, restart, text entry and start-up of the engine (analysis/notes/menus_saveload.md), driven headless through
`build/biosys --script` with real key and mouse events. Expectations come from the decompiled code of BioSys.exe (the names and
branches of exe:004184e0 / exe:004172a0, the file layout of exe:00452410) and the data files, with independent Python code: the pages
are read with analysis/scripts/biosys_pages.py, the saved files with struct and analysis/scripts/biosys_sav.py.

Two game logics are used: --menu-logic (src/app/menulogic.h: Escape / F5 / F6 open the menu, the commands do what dll:10008010 does, the
blob is the real container's header and trailer) and --game (the port of BiosGame.dll: real blobs and descriptions)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
SAV = D + '/setup-cd/installed/DATA1/NewGame.sav'
PAGES = D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'
PAGEPY = D + '/analysis/scripts/biosys_pages.py'
SAVPY = D + '/analysis/scripts/biosys_sav.py'
T = 'build/test/menus'
os.makedirs(T, exist_ok=True)
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


def run(script, savedir, *opts, game=False):
    """-> (rc, stdout lines, stderr lines)"""
    base = ['build/biosys', '--mute', '--log-events', '--save-dir', savedir]
    base += ['--game', '--skip-intro'] if game else ['--menu-logic']
    r = subprocess.run(base + list(opts) + ['--script', script], capture_output=True, text=True)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


def events(out):
    return [l[6:] for l in out if l.startswith('event ')]


def hot_dump(page):
    """hotspots of a page: name -> (l, t, r, b) read by the independent page parser"""
    txt = subprocess.run(['python3', '-I', PAGEPY, PAGES, 'dump', page], capture_output=True, text=True).stdout
    hs = {}
    for m in re.finditer(r'hot\[\d+\] (\S+)\s+rect=\((\d+), (\d+), (\d+), (\d+)\)', txt):
        hs[m.group(1)] = tuple(int(m.group(i)) for i in range(2, 6))
    return hs


def centre(rect):
    return '%d,%d' % ((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)


TOP, LOAD, SAVE = hot_dump('menutop'), hot_dump('menuload'), hot_dump('menusave')
CRED = [hot_dump('menucred%d' % i) for i in range(1, 12)]
HELP, HELPA = hot_dump('menuhelp1'), hot_dump('menuhelp1a')


def menu_blocks(out):
    """every `dump menu` result: dict(open, page, credits, help, saveconfirm, quitconfirm, hover, hot: name -> on)"""
    res, cur = [], None
    for l in out:
        if l.startswith('menu '):
            cur = {'open': l.split()[1] == 'open', 'hot': {}}
            res.append(cur)
        elif cur is not None and l.startswith('page ') and ' kind ' in l:
            t = l.split()
            for i in range(0, len(t) - 1, 2):
                cur[t[i]] = t[i + 1]
        elif cur is not None and l.startswith('hotspot '):
            cur['hot'][l.split()[1]] = l.split()[2] == 'on'
    return res


def obj_state(out):
    """the state lines that a saved game must restore: held / worn / page+scroll, the objects, the bar"""
    keep = []
    for l in out:
        if l.startswith(('held ', 'worn ', 'obj ', 'bar ', 'slot ', 'statuscount', 'objects ')) or (l.startswith('page ') and ' scroll ' in l):
            keep.append(l)
    return keep


# ---------------------------------------------------------------------------------------------------------------
# the file: parsed with struct (independent of the engine's reader)
def cstr(b):
    return b.split(b'\0')[0].decode('latin-1')


def parse_sav(path):
    d = open(path, 'rb').read()
    h = {'size': len(d)}
    h['magic'], h['version'], h['blob'] = struct.unpack_from('<III', d, 0)
    h['title'] = cstr(d[0x0c:0x5c])
    h['unk5c'], h['bs4'] = struct.unpack_from('<II', d, 0x5c)
    h['page'] = cstr(d[0x64:0x70])
    h['scroll'] = struct.unpack_from('<ii', d, 0x70)
    h['grab'] = struct.unpack_from('<I', d, 0x78)[0]
    h['palette'] = [tuple(d[0x7c + 4 * i:0x7c + 4 * i + 3]) for i in range(256)]
    h['pal_flags'] = [d[0x7c + 4 * i + 3] for i in range(256)]
    h['f47c'], h['f480'] = struct.unpack_from('<II', d, 0x47c)
    h['b484'] = tuple(d[0x484:0x487])
    h['pending'] = struct.unpack_from('<10I', d, 0x488)
    h['f4c8'], h['f4cc'], h['f4d0'], h['f4d4'], h['f4d8'] = struct.unpack_from('<iIIII', d, 0x4c8)
    h['s4dc'] = cstr(d[0x4dc:0x540])
    h['a540'], h['a544'] = struct.unpack_from('<II', d, 0x540)
    n, = struct.unpack_from('<I', d, 0x548)
    h['n'] = n
    p = 0x548 + 4
    h['objs'] = []
    h['bar'] = []
    if n:
        h['nmove'], h['nzone'], nbar = struct.unpack_from('<III', d, p)
        p += 12
        for i in range(n):
            r = d[p:p + 0x58]
            o = {'name': cstr(r[0:12]), 'status': struct.unpack_from('<I', r, 12)[0], 'w': struct.unpack_from('<I', r, 16)[0],
                 'u': struct.unpack_from('<i', r, 20)[0], 'cur': (cstr(r[0x18:0x23]), cstr(r[0x23:0x2e])),
                 'home': (cstr(r[0x2e:0x39]), cstr(r[0x39:0x44])), 'grp': struct.unpack_from('<II', r, 0x44),
                 'var': cstr(r[0x4c:0x58]), 'raw': r}
            h['objs'].append(o)
            p += 0x58
        for i in range(nbar):
            h['bar'].append(cstr(d[p:p + 11]))
            p += 11
    h['end'] = p
    return h, d


# the regions of the engine part that are strings in NUL-padded fields (the original leaves garbage after the NUL)
def string_fields(d):
    f = [(0x0c, 80), (0x64, 12), (0x4b0, 11), (0x4bb, 13), (0x4dc, 100)]
    n, = struct.unpack_from('<I', d, 0x548)
    if n:
        p = 0x548 + 16
        for i in range(n):
            f += [(p, 12), (p + 0x18, 11), (p + 0x23, 11), (p + 0x2e, 11), (p + 0x39, 11), (p + 0x4c, 12)]
            p += 0x58
        nbar, = struct.unpack_from('<I', d, 0x548 + 12)
        for i in range(nbar):
            f.append((p, 11))
            p += 11
    return f


def diff_bytes(a, b, strs):
    """the byte positions where a and b differ, after ignoring what follows the NUL in the string fields"""
    ign = set()
    for off, size in strs:
        z = a[off:off + size].find(b'\0')
        z = size if z < 0 else z
        ign.update(range(off + z, off + size))
    ign.update([0x487])                       # the pad byte after the three filter bytes
    return [i for i in range(min(len(a), len(b))) if a[i] != b[i] and i not in ign], ign


# ================================================================= (a) Escape
d = fresh('esc')
rc, out, err = run('advance 100; key ESC; dump menu; key ESC; dump menu; key ESC; dump menu; key ESC; dump menu', d)
mb = menu_blocks(out)
check('(a) Escape on a game page opens the main menu, Escape closes it, again and again (open, closed, open, closed)',
      rc == 0 and [m['open'] for m in mb] == [True, False, True, False] and mb[0]['page'] == 'menutop', (rc, mb[:2], err[:3]))
ev = events(out)
check('(a) the game logic sees only the Escapes that open the menu (the menu takes the others) and OnMenuCommand(0, 0) for each close',
      ev.count('keydown vk27') == 2 and ev.count('menucommand 0 0') == 2, ev)
first_menu = ev.index('keydown vk27')
check('(a) the page under the menu is untouched: no page events at all from opening and closing the menu',
      not [e for e in ev[first_menu:] if e.startswith(('exitpage', 'pageloaded', 'pagedrawn', 'enterpage'))], ev[first_menu:])
check('(a) the menu opens on menutop with every button enabled and yes / no disabled',
      all(mb[0]['hot'].get(n) for n in ('newgame', 'jumpload', 'jumpsave', 'exitgame', 'jumpcredit', 'jumphelp')) and
      not mb[0]['hot'].get('yes') and not mb[0]['hot'].get('no'), mb[0])
rc, out, err = run('advance 100; api GetGameObjectCount; key ESC; api GetGameObjectCount; api JumpToPage S1 none 1; api GetSaveDirectory; api WriteLog x; '
                   'api OpenMenu top; key ESC; api GetGameObjectCount', d)
rcs = [l for l in out if l.startswith('api ')]
check('(a) while the menu is up every command except 2 9 0x34 0x35 0x53 0x55 0x59 0x6a answers 0x26 (38)',
      'api GetGameObjectCount rc=0 count=409' in rcs[0] and rcs[1:5] == ['api GetGameObjectCount rc=38', 'api JumpToPage rc=38',
      'api GetSaveDirectory rc=0 directory=' + d, 'api WriteLog rc=0'] and rcs[5] == 'api OpenMenu rc=38' and rcs[6].startswith('api GetGameObjectCount rc=0'),
      rcs)

# ================================================================= (b) the hotspots of the main menu
EXE_NAMES = {'newgame', 'jumpload', 'jumpsave', 'jumpcredit', 'jumphelp', 'exitgame', 'next', 'back', 'main', 'done', 'yes', 'no'} | \
            {'savegame%d' % i for i in range(1, 21)} | {'loadgame%d' % i for i in range(1, 21)}     # the strings exe:004184e0 compares
names_all = set(TOP) | set(LOAD) | set(SAVE) | set(HELP) | set(HELPA) | {n for c in CRED for n in c}
check('(b) every hotspot name of the menu pages is one of the names exe:004184e0 handles', names_all <= EXE_NAMES, names_all - EXE_NAMES)
check('(b) menutop has the hotspots newgame jumpload jumpsave exitgame jumpcredit jumphelp yes no',
      set(TOP) == {'newgame', 'jumpload', 'jumpsave', 'exitgame', 'jumpcredit', 'jumphelp', 'yes', 'no'}, TOP)


def page_after(clicks):
    d = fresh('hot')
    rc, out, err = run('advance 100; key ESC; ' + clicks + '; dump menu', d)
    return menu_blocks(out)[-1], events(out), err


m, ev, err = page_after('click ' + centre(TOP['jumpload']))
check('(b) jumpload -> menuload (hover highlight events with window -6 on the way)', m['page'] == 'menuload' and 'hover jumpload in win=-6' in ev, m)
m, ev, err = page_after('click ' + centre(TOP['jumpsave']))
check('(b) jumpsave -> menusave', m['page'] == 'menusave', m)
m, ev, err = page_after('click ' + centre(TOP['jumpcredit']))
check('(b) jumpcredit -> menucred1, credits index 1', m['page'] == 'menucred1' and m['credits'] == '1', m)
m, ev, err = page_after('click ' + centre(TOP['jumphelp']))
check('(b) jumphelp -> menuhelp1, help index 1', m['page'] == 'menuhelp1' and m['help'] == '1', m)
m, ev, err = page_after('swapped on; click ' + centre(TOP['jumphelp']))
check('(b) jumphelp with swapped mouse buttons -> menuhelp1a', m['page'] == 'menuhelp1a' and m['help'] == '1', m)
m, ev, err = page_after('click ' + centre(TOP['exitgame']))
check('(b) exitgame -> the quit question: quitconfirm 1, only yes and no enabled',
      m['page'] == 'menutop' and m['quitconfirm'] == '1' and [n for n, on in m['hot'].items() if on] and
      sorted(n for n, on in m['hot'].items() if on) == ['no', 'yes'], m)
m, ev, err = page_after('click ' + centre(TOP['exitgame']) + '; key N')
check('(b) quit question: N cancels, every button is enabled again, no command is sent',
      m['quitconfirm'] == '0' and m['hot']['newgame'] and not m['hot']['yes'] and 'menucommand 4 0' not in ev, (m, ev[-3:]))
m, ev, err = page_after('click ' + centre(TOP['exitgame']) + '; key Y')
check('(b) quit question: Y sends OnMenuCommand(4, 0)', 'menucommand 4 0' in ev, ev[-4:])
m, ev, err = page_after('click ' + centre(TOP['exitgame']) + '; click ' + centre(TOP['yes']))
check('(b) quit question: the yes hotspot is the Y key', 'menucommand 4 0' in ev, ev[-4:])
m, ev, err = page_after('click ' + centre(TOP['exitgame']) + '; click ' + centre(TOP['no']))
check('(b) quit question: the no hotspot is the N key', m['quitconfirm'] == '0' and 'menucommand 4 0' not in ev, m)
m, ev, err = page_after('click ' + centre(TOP['exitgame']) + '; key ESC')
check('(b) quit question: Escape on the main page closes the menu (OnMenuCommand 0)', not m['open'] and 'menucommand 0 0' in ev, (m, ev[-3:]))
m, ev, err = page_after('click ' + centre(TOP['newgame']))
check('(b) newgame -> OnMenuCommand(1, 0), the engine loads NewGame.sav (event 24 with the blob offset), the menu is closed',
      not m['open'] and 'menucommand 1 0' in ev and 'loadgame NewGame.sav offset=37360' in ev, ev[-8:])
m, ev, err = page_after('key ESC')
d = fresh('hot2')
rc, out, err = run('advance 100; key ESC; click ' + centre(TOP['jumpload']) + '; dump menu; key ESC; dump menu; key ESC; dump menu', d)
mb = menu_blocks(out)
check('(b) Escape on a sub page goes back to the main menu; Escape there closes it',
      [(m['open'], m.get('page')) for m in mb] == [(True, 'menuload'), (True, 'menutop'), (False, None)], mb)
rc, out, err = run('advance 100; key ESC; click ' + centre(TOP['jumpsave']) + '; click ' + centre(SAVE['main']) + '; dump menu; '
                   'click ' + centre(TOP['jumpload']) + '; click ' + centre(LOAD['main']) + '; dump menu', d)
mb = menu_blocks(out)
check('(b) the main hotspot of the save and load pages goes back to menutop', [m['page'] for m in mb] == ['menutop', 'menutop'], mb)
# F5 / F6 open the save / load page directly
rc, out, err = run('advance 100; key F5; dump menu; key ESC; dump menu; key ESC; key F6; dump menu', d)
mb = menu_blocks(out)
check('(b) F5 / F6 (OpenMenu "savegame" / "loadgame") open the save / load page; Escape goes to the main menu first',
      [(m['open'], m.get('page')) for m in mb] == [(True, 'menusave'), (True, 'menutop'), (True, 'menuload')], mb)

# ================================================================= (c) credits and help
d = fresh('cred')
script = 'advance 100; key ESC; click ' + centre(TOP['jumpcredit']) + '; dump menu'
for i in range(2, 12):
    script += '; key SPACE; dump menu'
script += '; key SPACE; dump menu'
rc, out, err = run(script, d)
mb = menu_blocks(out)
check('(c) the credits: jumpcredit, then Space steps through menucred1 .. menucred11, one more Space returns to the main menu',
      [m['page'] for m in mb] == ['menucred%d' % i for i in range(1, 12)] + ['menutop'] and mb[10]['credits'] == '11' and mb[11]['credits'] == '0', [m.get('page') for m in mb])
script = 'advance 100; key ESC; click ' + centre(TOP['jumpcredit']) + ';'
for i in range(1, 11):
    script += ' click ' + centre(CRED[i - 1]['next']) + '; dump menu;'
script += ' click ' + centre(CRED[10]['main']) + '; dump menu'
rc, out, err = run(script, d)
mb = menu_blocks(out)
check('(c) the next hotspots of menucred1 .. 10 lead to the next page, the main hotspot of menucred11 to the main menu',
      [m['page'] for m in mb] == ['menucred%d' % i for i in range(2, 12)] + ['menutop'], [m.get('page') for m in mb])
script = 'advance 100; key ESC; click ' + centre(TOP['jumpcredit']) + '; key SPACE; key SPACE; key SPACE; dump menu; click ' + centre(CRED[3]['back']) + '; dump menu; ' \
         'click ' + centre(CRED[2]['back']) + '; dump menu; click ' + centre(CRED[1]['back']) + '; dump menu'
rc, out, err = run(script, d)
mb = menu_blocks(out)
check('(c) the back hotspots: menucred4 -> 3 -> 2 -> 1', [m['page'] for m in mb] == ['menucred4', 'menucred3', 'menucred2', 'menucred1'], [m.get('page') for m in mb])
rc, out, err = run('advance 100; key ESC; click ' + centre(TOP['jumpcredit']) + '; key ESC; dump menu', d)
check('(c) Escape on a credits page returns to the main menu and clears the credits index',
      menu_blocks(out)[-1]['page'] == 'menutop' and menu_blocks(out)[-1]['credits'] == '0', menu_blocks(out)[-1])
rc, out, err = run('advance 100; key ESC; click ' + centre(TOP['jumphelp']) + '; dump menu; key SPACE; dump menu', d)
mb = menu_blocks(out)
check('(c) help: Space on menuhelp1 (there is no menuhelp2) returns to the main menu', [m['page'] for m in mb] == ['menuhelp1', 'menutop'], mb)
rc, out, err = run('advance 100; key ESC; click ' + centre(TOP['jumphelp']) + '; click ' + centre(HELP['main']) + '; dump menu', d)
check('(c) the main hotspot of the help page goes back to the main menu', menu_blocks(out)[-1]['page'] == 'menutop', menu_blocks(out)[-1])

# ================================================================= (d) save
d = fresh('save')
slot3 = centre(SAVE['savegame3'])
rc, out, err = run('advance 100; key F5; dump menu; click ' + slot3 + '; advance 50; dump menu; dump state', d, '--hotspots')
ev = events(out)
files = sorted(os.listdir(d))
check('(d) saving into slot 3 writes exactly "Saved Game 3.sav" in the save directory', files == ['Saved Game 3.sav'], files)
check('(d) events: 32 for the slots of the page (1 .. 20, flag save), 31 (2, 3), 23, and the menu is closed',
      [e for e in ev if e.startswith('menuquery')][:20] == ['menuquery %d save exists=0' % i for i in range(1, 21)] and
      ev[ev.index('menucommand 2 3'):ev.index('menucommand 2 3') + 2] == ['menucommand 2 3', 'savegame Saved Game 3.sav'] and not menu_blocks(out)[-1]['open'], ev[-6:])
h, raw = parse_sav(d + '/Saved Game 3.sav')
check('(d) header: magic 0xff5e67c0, version 0x12, title "Biosys", page R4, scroll (1290, 308) of the new game, no BS4',
      (h['magic'], h['version'], h['title'], h['page'], h['scroll'], h['bs4']) == (0xff5e67c0, 0x12, 'Biosys', 'R4', (1290, 308), 0), h)
check('(d) the blob offset is where the engine part ends (the end of the objects section) and the blob follows to the end of the file',
      h['blob'] == h['end'] == 0x91f0 and h['size'] > h['blob'] and raw[h['blob']:h['blob'] + 13] == b'JON IS A YOYO', (h['blob'], h['end'], h['size']))
orig, od = parse_sav(SAV)
check('(d) game objects: counts 409 / 409 / 943 / 0 and every record equal to NewGame.sav\'s (it is a new game)',
      (h['n'], h['nmove'], h['nzone'], len(h['bar'])) == (409, 409, 943, 0) and
      all({k: v for k, v in a.items() if k != 'raw'} == {k: v for k, v in b.items() if k != 'raw'} for a, b in zip(h['objs'], orig['objs'])), 'differs')
check('(d) header fields of the viewer state: palette result 1, flags 0, pending struct empty, 0x540 / 0x544 = the engine\'s API 0x6d values',
      (h['grab'], h['f47c'], h['f480'], h['b484'], h['f4cc'], h['f4d0'], h['f4d4'], h['pending'], h['s4dc']) == (1, 0, 0, (0, 0, 0), 0, 0, 0, (0,) * 10, ''), h['b484'])
sv = subprocess.run(['python3', '-I', SAVPY, d + '/Saved Game 3.sav'], capture_output=True, text=True).stdout
check('(d) analysis/scripts/biosys_sav.py reads it: magic OK, objects section ends at the blob offset (MATCH), blob magic and trailer OK',
      'magic=0xff5e67c0 (OK)' in sv and 'MATCH' in sv and "blob magic       : b'JON IS A YOYO' (OK)" in sv and "trailer          : b'THE END' (OK)" in sv, sv[-600:])
# overwrite question
d = fresh('save2')
rc, out, err = run('advance 100; key F5; click ' + slot3 + '; advance 50; key F5; click ' + slot3 + '; dump menu; key N; dump menu; click ' + slot3 +
                   '; key Y; advance 50; dump menu', d)
ev = events(out)
mb = menu_blocks(out)
check('(d) saving into a taken slot asks first (overwrite question: saveconfirm 1, every hotspot of the page disabled; yes / no exist only on menutop, the keys answer); N cancels, Y saves',
      mb[0]['saveconfirm'] == '1' and not any(mb[0]['hot'].values()) and mb[1]['saveconfirm'] == '0' and mb[1]['open'] and all(mb[1]['hot'].values()) and
      ev.count('savegame Saved Game 3.sav') == 2 and not mb[2]['open'], (mb, ev[-5:]))
# a failing save
rc, out, err = run('advance 100; api SaveGame saved game 4.sav', '/nonexistent-biosys-dir')
check('(d) API SaveGame into a directory that does not exist: error 0x1c, logged "Failed to open the saved-game file ... for writing."',
      'api SaveGame rc=28' in out and any('Failed to open the saved-game file' in l and 'for writing.' in l for l in err), (out[-2:], err))

# ================================================================= (e) load restores the state
d = fresh('load')
GOGGLES = 'page fce9cab_o; click 330,321; key SPACE'                 # the goggles of NewGame.sav worn: status 5 on the bar
RFMAP = 'page NbrBIOS4; click 560,376'                                 # the map in the hand: status 2
STATE = 'page R4; scroll 700,250; api SetPalette rgb:40,60,80 1 0; advance 50'
SNAP = 'dump state; dump objects; dump bar; pal'
rc, out, err = run('advance 100; ' + GOGGLES + '; ' + RFMAP + '; ' + STATE + '; ' + SNAP + '; key F5; click ' + slot3 + '; advance 50; ' + SNAP, d)
snaps = [i for i, l in enumerate(out) if l.startswith('held ')]
before, after = out[snaps[0]:snaps[1]], out[snaps[1]:]
check('(e) the state before saving: goggles worn, map held, page R4 at (700, 250)',
      'held rfmap' in before and 'worn goggles' in before and 'page R4 scroll 700,250' in before, before[:3])
check('(e) saving from the menu leaves the game as it was (state and palette)', obj_state(before) == obj_state(after) and [l for l in before if l.startswith('pal ')] == [l for l in after if l.startswith('pal ')], 'changed')
h, raw = parse_sav(d + '/Saved Game 3.sav')
bar_names = h['bar']
st = {o['name']: o['status'] for o in h['objs']}
check('(e) the file: page R4 scroll (700, 250), the palette on display before the menu (40, 60, 80 everywhere: not MENU.PAL), flags byte 0',
      h['page'] == 'R4' and h['scroll'] == (700, 250) and h['grab'] == 1 and all(c == (40, 60, 80) for c in h['palette']) and set(h['pal_flags']) == {0}, (h['page'], h['scroll'], h['palette'][:2]))
check('(e) the file: rfmap status 2, goggles status 5 and the only object on the bar, 43 objects removed as in the new game',
      st['rfmap'] == 2 and st['goggles'] == 5 and bar_names == ['goggles'] and sum(1 for o in h['objs'] if o['status'] == 4) == 43, (st['rfmap'], st['goggles'], bar_names))
# now change everything and load
CHANGE = 'remove goggles; remove rfmap; page PH13; scroll 100,50; api SetPalette rgb:200,10,10 1 0; advance 50'
rc, out, err = run('advance 100; ' + CHANGE + '; ' + SNAP + '; key F6; dump menu; click ' + centre(LOAD['loadgame3']) + '; advance 50; ' + SNAP + '; dump menu', d)
snaps = [i for i, l in enumerate(out) if l.startswith('held ')]
chg, loaded = out[snaps[0]:snaps[1]], out[snaps[1]:]
ev = events(out)
check('(e) after the load: page R4, scroll (700, 250), the palette (40, 60, 80) on display, every object status, the bar order, the held and worn objects as saved',
      'page R4 scroll 700,250' in loaded and 'held rfmap' in loaded and 'worn goggles' in loaded and obj_state(loaded) == obj_state(before) and
      [l for l in loaded if l.startswith('pal ')] == [l for l in before if l.startswith('pal ')] and obj_state(chg) != obj_state(before), 'differs')
check('(e) the load page lists the slot (event 32, flag load, exists=1 for slot 3 only); empty slots are disabled, slot 3 enabled',
      [e for e in ev if e.startswith('menuquery')][:20] == ['menuquery %d load exists=%d' % (i, 1 if i == 3 else 0) for i in range(1, 21)], [e for e in ev if e.startswith('menuquery')][:4])
mb = menu_blocks(out)
check('(e) load page: loadgame3 enabled, the other slots disabled', mb[0]['page'] == 'menuload' and mb[0]['hot'].get('loadgame3') and not mb[0]['hot'].get('loadgame1') and
      not mb[-1]['open'], mb[0]['hot'])
i0 = ev.index('menucommand 3 3')
seq = [e.split()[0] + (' ' + e.split()[1] if e.split()[0] in ('exitpage', 'pageloaded', 'pagedrawn', 'enterpage') else '') for e in ev[i0:]]
want = ['menucommand', 'loadgame', 'exitpage PH13', 'pageloaded R4', 'pagedrawn R4', 'enterpage R4', 'palettechanged']
got = [s for s in seq if s in want or s.split()[0] in ('menucommand', 'loadgame')]
check('(e) events of a load in the order of exe:00452b70: OnMenuCommand 3, OnLoadGame (24), OnExitPage, OnPageLoaded, OnPageDrawn, OnEnterPage, OnPaletteChanged',
      got[:7] == want, got)
check('(e) the load clears the menu and passes the blob offset: loadgame Saved Game 3.sav offset=%d' % h['blob'], 'loadgame Saved Game 3.sav offset=%d' % h['blob'] in ev, ev[i0:i0 + 3])
# empty slot
rc, out, err = run('advance 100; key F6; click ' + centre(LOAD['loadgame5']) + '; dump menu', d)
check('(e) a disabled slot cannot be clicked: no OnMenuCommand, the menu stays', 'menucommand 3 5' not in events(out) and menu_blocks(out)[-1]['open'], events(out)[-2:])

# ================================================================= (f) NewGame.sav and a port-written save load; corrupted files are rejected
d = fresh('files')
shutil.copy(SAV, d + '/orig.sav')
shutil.copy('%s/load/Saved Game 3.sav' % T, d + '/Saved Game 3.sav')
rc, out, err = run('advance 100; page S1; api LoadGame -1 Saved Game 3.sav; dump state; page S1; api LoadGame 2 orig.sav; dump state; api LoadGame -1 missing.sav; dump state', d)
rcs = [l for l in out if l.startswith('api LoadGame')]
check('(f) a save written by the port loads (R4 at (700, 250)), the original NewGame.sav loads (R4 at (1290, 308)), a missing file is 0x1f (31) and changes nothing',
      rcs == ['api LoadGame rc=0', 'api LoadGame rc=0', 'api LoadGame rc=31'] and
      [l for l in out if l.startswith('page ') and 'scroll' in l] == ['page R4 scroll 700,250', 'page R4 scroll 1290,308', 'page R4 scroll 1290,308'], (rcs, out[:6]))


def mutate(name, fn):
    b = bytearray(open(SAV, 'rb').read())
    b = fn(b) or b
    open('%s/%s' % (d, name), 'wb').write(bytes(b))


def setb(off, data):
    def f(b):
        b[off:off + len(data)] = data
    return f


mutate('bad_magic.sav', setb(0, b'\0\0\0\0'))
mutate('bad_version.sav', setb(4, struct.pack('<I', 0x13)))
mutate('bad_title.sav', setb(0x0c, b'Biosyz\0'))
mutate('short_header.sav', lambda b: b[:0x300])
mutate('short_objects.sav', lambda b: b[:0x548 + 0x2000])
mutate('bad_objcount.sav', setb(0x548, struct.pack('<I', 408)))
mutate('bad_zones.sav', setb(0x548 + 8, struct.pack('<I', 942)))
mutate('bad_bar.sav', setb(0x548 + 12, struct.pack('<I', 410)))
mutate('bs4.sav', setb(0x60, struct.pack('<I', 1)))
mutate('bad_blob.sav', setb(0x91f0, b'XXXXXXXXXXXXX'))
mutate('bad_page.sav', setb(0x64, b'NoSuchPage\0'))
MAGIC_MSG = 'is invalid; incorrect magic number and/or incorrect version number and/or publication title string.'
cases = [('bad_magic.sav', 0x22, MAGIC_MSG), ('bad_version.sav', 0x22, MAGIC_MSG), ('bad_title.sav', 0x22, MAGIC_MSG),
         ('short_header.sav', 0x20, 'Failed to read from saved-game file'), ('short_objects.sav', 0x20, 'Failed to read game objects data from saved-game file'),
         ('bad_objcount.sav', 0x22, 'Invalid game objects data in the saved-game file'), ('bad_zones.sav', 0x22, 'Invalid game objects data in the saved-game file'),
         ('bad_bar.sav', 0x22, 'Invalid game objects data in the saved-game file'), ('bs4.sav', 0x22, 'Invalid game objects data in the saved-game file'),   # +0x60 = 1 on a file without terminal data: 0x548 is read as the section size (408), the objects are then misplaced (the engine reads BioSys 4 saves now)
         ('bad_blob.sav', 0x21, None), ('bad_page.sav', 0x21, "Page name 'NoSuchPage' in saved-game file"), ('nofile.sav', 0x1f, 'Failed to open the saved-game file')]
script = 'advance 100; page PH13; scroll 100,50; dump state'
for n, c, m in cases:
    script += '; api LoadGame -1 %s; dump state' % n
rc, out, err = run(script, d)
rcs = [int(re.search(r'rc=(\d+)', l).group(1)) for l in out if l.startswith('api LoadGame')]
check('(f) corrupted files are refused with the original\'s error codes (0x22 invalid, 0x20 short, 0x21 other, 0x1f no file): ' + ' '.join(hex(c) for c in rcs),
      rcs == [c for n, c, m in cases], rcs)
msgs = [l for l in err if l.startswith('biosys: ')]
ok = True
for n, c, m in cases:
    if m and not any(m in l and (n in l or m.startswith('Page name')) for l in msgs):
        ok = False
check('(f) each refusal reports the original\'s message text (exe string table: "Saved-game file %s is invalid; ...", "Failed to read ...")', ok, msgs[:12])
check('(f) a file refused before the game state is touched leaves the page where it was (PH13 at (100, 50)) for all but the late failures (bad blob, bad page)',
      [l for l in out if l.startswith('page ') and 'scroll' in l][:10] == ['page PH13 scroll 100,50'] * 10, [l for l in out if l.startswith('page ') and 'scroll' in l])

# ================================================================= (g) new game = loading NewGame.sav
d = fresh('newgame')
rc, out, err = run('advance 100; ' + GOGGLES + '; ' + RFMAP + '; page S1; key ESC; click ' + centre(TOP['newgame']) + '; advance 50; dump state; dump objects; dump bar; pal;'
                   ' ' + GOGGLES + '; page S1; api LoadGame 2 NewGame.sav; advance 50; dump state; dump objects; dump bar; pal', d)
snaps = [i for i, l in enumerate(out) if l.startswith('held ')]
a, b = out[snaps[0]:snaps[1]], out[snaps[1]:]
check('(g) the menu\'s new game and API LoadGame("NewGame.sav", 2) give the same state (page, scroll, objects, bar, palette)',
      obj_state(a) == obj_state(b) and [l for l in a if l.startswith('pal ')] == [l for l in b if l.startswith('pal ')] and
      'page R4 scroll 1290,308' in a and 'held -' in a and 'worn -' in a, 'differs')
rc, out, err = run('advance 100; dump state; dump objects; dump bar; pal', d)
check('(g) ... and the state of a game started by the viewer (startNewGame)', obj_state(out) == obj_state(a), 'differs')
# the engine's palette after loading NewGame.sav is the saved one
hexpal = ''.join('%02x%02x%02x' % c for c in orig['palette'])
check('(g) the palette on display after the load is the palette saved in NewGame.sav', [l for l in a if l.startswith('pal ')] == ['pal ' + hexpal], 'differs')

# ================================================================= (h) round trip: NewGame.sav -> load -> save
d = fresh('roundtrip')
rc, out, err = run('advance 100; api LoadGame 2 NewGame.sav; advance 50; api SaveGame saved game 7.sav', d)
a_raw = open(SAV, 'rb').read()
b_raw = open(d + '/Saved Game 7.sav', 'rb').read()
strs = string_fields(a_raw)
diffs, ign = diff_bytes(a_raw[:orig['end']], b_raw[:orig['end']], strs)
check('(h) round trip of the engine part (%d bytes): same length, every byte equal except the stack garbage the original leaves behind a string\'s NUL (%d bytes)' % (orig['end'], len(ign)),
      parse_sav(d + '/Saved Game 7.sav')[0]['end'] == orig['end'] and not diffs, diffs[:20])
hb = parse_sav(d + '/Saved Game 7.sav')[0]
check('(h) ... and the port writes zeros there (no stray bytes)', all(b_raw[i] == 0 for i in ign if i < orig['end']), [i for i in ign if i < orig['end'] and b_raw[i]][:10])
check('(h) the fields that come back by value: scroll (1290, 308), palette, filter flag 1 with bytes (0x76, 0x56, 0x57), 0x540 / 0x544 = 1, 1',
      (hb['scroll'], hb['f480'], hb['b484'], hb['a540'], hb['a544']) == ((1290, 308), 1, (0x76, 0x56, 0x57), 1, 1) and hb['palette'] == orig['palette'], hb['b484'])
print('INFO round trip: %d engine bytes, %d padding bytes ignored, fields not modelled (kept as loaded): 0x5c, 0x488.., 0x4cc.., 0x4d8, 0x4dc' % (orig['end'], len(ign)))

# ================================================================= (i) the real game: blob, description, load
d = fresh('game')
rc, out, err = run('advance 2000; key F5; click 100,85; advance 500; key F6; dump menu', d, game=True)
mb = menu_blocks(out)
check('(i) --game: F5 opens the save page, a click on slot 1 saves "Saved Game 1.sav" and closes the menu; F6 then lists it (exit 0)',
      rc == 0 and os.path.exists(d + '/Saved Game 1.sav') and mb and mb[-1]['page'] == 'menuload' and mb[-1]['hot'].get('loadgame1') and not mb[-1]['hot'].get('loadgame2'), (rc, err[:3], mb[-1:]))
gv = subprocess.run(['python3', '-I', SAVPY, d + '/Saved Game 1.sav'], capture_output=True, text=True).stdout
gh, graw = parse_sav(d + '/Saved Game 1.sav')
# the original part (engine part + the game's blob) has the size of NewGame.sav (216377); the port's mode trailer follows it (engine/savetrailer.h)
import struct as _st
_n = _st.unpack('<I', graw[216377 + 12:216377 + 16])[0] if len(graw) >= 216377 + 16 else -1
check('(i) the real game\'s save: the original part has the size of NewGame.sav (216377) and the mode trailer (BSYSMODE, 16 + N + 4 bytes) follows; the engine part ends at 0x91f0, the description reads "Saved Game: Day 1, 08:0x, Rainforest, 0% completed"',
      graw[216377:216385] == b'BSYSMODE' and len(graw) == 216377 + 16 + _n + 4 and gh['blob'] == 0x91f0 and re.search(r"description\[100\] : 'Saved Game: Day 1, 08:0\d, Rainforest, 0% completed'", gv) and 'MATCH' in gv, (len(graw), gv[-300:]))
d = fresh('game2')
rc, out, err = run('advance 2000; game dump; key F5; click 100,85; advance 200; advance 30000; game dump; key F6; click ' + centre(LOAD['loadgame1']) + '; advance 100; game dump; dump state', d, game=True)
dumps = [l for l in out if l.startswith('game time=')]
t = [int(re.search(r'time=(\d+)', l).group(1)) for l in dumps]
ev = events(out)
check('(i) loading the real game\'s save sets the game clock back (the blob is read by the game: time before the load > time after) and fires the events in order',
      len(t) == 3 and t[1] > t[0] and abs(t[2] - t[0]) < 120 and ev.index('loadgame Saved Game 1.sav offset=%d' % gh['blob']) < ev.index('exitpage R4', ev.index('menucommand 3 1')), (t, ev[-12:]))

# ================================================================= (j) restart
d = fresh('restart')
rc, out, err = run('advance 100; ' + GOGGLES + '; api RestartGame; advance 50; dump state; dump objects', d)
ev = events(out)
check('(j) RestartGame: OnRestart (event 30) is sent and the engine jumps to the start page (eintro); the objects are reset; result 0',
      'api RestartGame rc=0' in out and 'restart' in ev and any(l.startswith('page eintro') for l in out) and 'statuscount 4 409' in out, [l for l in out if l.startswith('page ')])
rc, out, err = run('advance 100; key ESC; api RestartGame; dump menu; advance 50; dump state', d)
check('(j) RestartGame is accepted while the menu is up and closes it', 'api RestartGame rc=0' in out and not menu_blocks(out)[0]['open'], out[:4])

# ================================================================= (k) text entry
d = fresh('text')
BOX = '100,100,300,130'
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 8 5 255,255,0; dump text; type ABC123; dump text; key BACKSPACE; dump text; type ZZZZZZZZZ; dump text; '
                   'shot %s/textinput.bmp; key ENTER; dump text' % (BOX, T), d)
tx = [l for l in out if l.startswith('textinput ')]
check('(k) text entry: the keys A-Z 0-9 space append, Backspace removes, the length is limited to the maximum (8)',
      tx == ["textinput on text ''", "textinput on text 'ABC123'", "textinput on text 'ABC12'", "textinput on text 'ABC12ZZZ'", "textinput off text 'ABC12ZZZ'"], tx)
check('(k) Enter ends it: event 28 OnTextInputDone(0, "ABC12ZZZ"), no OnKeyDown reached the game for the typed keys',
      'textinput done \'ABC12ZZZ\'' in events(out) and not [e for e in events(out) if e.startswith('keydown')], events(out))
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 4 0; type ab c; key ESC; dump text; api BeginTextInput %s 4 0; api EndTextInput 0; api EndTextInput 0' % (BOX, BOX), d)
ev = events(out)
check('(k) Escape cancels: OnTextInputDone(1, none); API 0x51 ends it with the text (+04 = 0) or cancelled (1); a second 0x51 does nothing',
      ev.count("textinput cancelled ''") == 1 and ev.count("textinput done ''") == 1 and "textinput done 'AB C'" not in ev, ev)
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 4 0; api BeginTextInput %s 4 0; api BeginTextInput 0,0,9999,10 4 0; api EndTextInput 1' % (BOX, BOX), d)
rcs = [l for l in out if l.startswith('api BeginTextInput')]
check('(k) BeginTextInput twice is 0x11; so are a rectangle outside the view and a length of 0 or 9',
      rcs[0] == 'api BeginTextInput rc=0' and rcs[1] == 'api BeginTextInput rc=17' and rcs[2] == 'api BeginTextInput rc=17', rcs)
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 9 0; api BeginTextInput %s 0 0; page O1; api BeginTextInput %s 4 0' % (BOX, BOX, BOX), d)
rcs = [l for l in out if l.startswith('api BeginTextInput')]
check('(k) ... a length of 9 or 0, and a page larger than the view (O1, a panorama), are refused too', all(l.endswith('rc=17') for l in rcs) and len(rcs) == 3, rcs)
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 4 0; page R4; dump text' % BOX, d)
check('(k) a page load cancels the text entry (event 28 cancelled)', "textinput cancelled ''" in events(out) and 'textinput off' in ' '.join(out), events(out))
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 4 0; api OpenMenu - 0; dump text; dump menu' % BOX, d)
check('(k) OpenMenu while a text entry runs: the entry is cancelled (event 28) and the menu opens', "textinput cancelled ''" in events(out) and menu_blocks(out)[0]['open'], events(out))

# ================================================================= (l) OpenMenu arguments
d = fresh('openmenu')
rc, out, err = run('advance 100; api OpenMenu bogus; api OpenMenu savegame 1; dump menu; api OpenMenu savegame 0; dump menu; key ESC; key ESC; api OpenMenu loadgame; dump menu; '
                   'key ESC; key ESC; api OpenMenu - 1; dump menu', d)
rcs = [l for l in out if l.startswith('api OpenMenu')]
mb = menu_blocks(out)
check('(l) OpenMenu: a bad name is 0x12 (18); "savegame" with flags 1 (no game running) is 0x11 (17) and opens nothing; flags 0 opens menusave; "loadgame" menuload; "" the main menu',
      rcs == ['api OpenMenu rc=18', 'api OpenMenu rc=17', 'api OpenMenu rc=0', 'api OpenMenu rc=0', 'api OpenMenu rc=0'] and not mb[0]['open'] and
      [m.get('page') for m in mb[1:]] == ['menusave', 'menuload', 'menutop'], (rcs, mb))
check('(l) flags 1 greys "save" on the main menu: the jumpsave hotspot is disabled', not mb[-1]['hot']['jumpsave'] and mb[-1]['hot']['jumpload'], mb[-1])
# slot hover images and the cursor are drawn: the highlighted button differs from the plain one
rc, o1, _ = run('advance 100; key ESC; move 600,100; shot %s/plain.bmp; move %s; shot %s/lit.bmp; move 600,100; shot %s/plain2.bmp' % (T, centre(TOP['newgame']), T, T), d)
def pix(p):
    return open(p, 'rb').read()
def ndiff(a, b):
    return sum(1 for x, y in zip(pix(T + '/' + a + '.bmp'), pix(T + '/' + b + '.bmp')) if x != y)
check('(l) hovering a button draws its "<name>1" picture (hundreds of pixels change), leaving it draws "<name>0" again (all but the edge pixels of the translucent picture are back)',
      ndiff('plain', 'lit') > 200 and ndiff('plain', 'plain2') < 60, (ndiff('plain', 'lit'), ndiff('plain', 'plain2')))

# ================================================================= (m) screenshots
d = fresh('shots')
rc, out, err = run('advance 100; key ESC; shot %s/menu_main.bmp; click %s; shot %s/menu_credits1.bmp; key ESC; click %s; shot %s/menu_help.bmp' %
                   (T, centre(TOP['jumpcredit']), T, centre(TOP['jumphelp']), T), d)
gsave = fresh('shots_game')
rc, out, err = run('advance 2000; key F5; click 100,85; advance 300; api OpenMenu loadgame 0 1; advance 50; shot %s/menu_load_slots.bmp' % T, gsave, game=True)
rc, out, err = run('advance 100; page fce9cab_o; api BeginTextInput %s 8 5 255,255,0; type OPEN SESAME; shot %s/textinput_prompt.bmp' % (BOX, T), d)
want = ['menu_main', 'menu_credits1', 'menu_help', 'menu_load_slots', 'textinput_prompt']
check('(m) screenshots saved: ' + ', '.join('%s/%s.bmp' % (T, w) for w in want), all(os.path.getsize('%s/%s.bmp' % (T, w)) > 300000 for w in want), want)
# the slot text is really drawn: the load page with a saved game differs from one without
rc, out, err = run('advance 2000; api OpenMenu loadgame 0 1; shot %s/menu_load_empty.bmp' % T, fresh('shots_empty'), game=True)
check('(m) the load page shows the description of a saved game (differs from the page without one)', pix(T + '/menu_load_slots.bmp') != pix(T + '/menu_load_empty.bmp'), 'same')

# ================================================================= (n) start-up flow with --game --intro
d = fresh('intro')
r = subprocess.run(['build/biosys', '--game', '--intro', '--mute', '--log-events', '--save-dir', d, '--script',
                    'advance 16; shot %s/startup_title.bmp; dump state; advance 1500; dump state; shot %s/startup_intro.bmp' % (T, T)], capture_output=True, text=True)
out = r.stdout.splitlines()
pg = [l for l in out if l.startswith('page ') and 'scroll' in l]
check('(n) --game --intro: the title bitmap is up first, the start page is the intro page (eintro), no game is loaded (no menu command, no NewGame.sav)',
      r.returncode == 0 and pg and pg[0].startswith('page eintro') and not [e for e in events(out) if e.startswith(('menucommand', 'loadgame'))], (r.returncode, pg, r.stderr[:200]))
from_title = open(T + '/startup_title.bmp', 'rb').read()
tbmp = open(D + '/game-cd/files/BIODATA/IMAGES/TITLE.BMP', 'rb').read()
check('(n) the first frame is IMAGES\\TITLE.BMP (same pixels), the second one (after 1.5 s) is not', from_title[1078:] == tbmp[1078:] and open(T + '/startup_intro.bmp', 'rb').read()[1078:] != tbmp[1078:], 'differs')
# the original start-up: events startup then the first page loaded is eintro
ev = events(out)
check('(n) events: startup, then OnPageLoaded for the start page eintro', 'startup' in ev and 'pageloaded eintro' in ev and ev.index('startup') < ev.index('pageloaded eintro'), ev[:6])
r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--mute', '--log-events', '--save-dir', d, '--script', 'advance 100; dump state'], capture_output=True, text=True)
check('(n) --skip-intro: the new game is started at once through the menu command (OnMenuCommand 1, OnLoadGame, page R4)',
      'event menucommand 1 0' in r.stdout and 'event loadgame NewGame.sav offset=37360' in r.stdout and 'page R4 scroll 1290,308' in r.stdout, r.stdout[:300])

print('menu tests: %s' % ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
