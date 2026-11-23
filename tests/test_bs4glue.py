#!/usr/bin/env python3
"""The BioSys 4 terminal inside the running game (src/engine/engine_bs4.cpp, bs4hooks.cpp; spec analysis/notes/bs4.md 1.2, 1.4, 1.8, 1.9,
1.12, 1.13): the real game driven headless through `build/biosys --game --script` with real input events. The geometry of the terminal's pages
comes from the independent page parser (tests/bs4_oracle.py), the saved files are read with struct. Frames: build/shots/bs4glue_*.bmp"""
import os
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bs4_oracle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
T = 'build/test/glue'
SHOTS = 'build/shots'
os.makedirs(T, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)
fails = 0


def check(ok, what, detail=''):
    global fails
    if ok:
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (' -- ' + str(detail)[:900] if detail != '' else ''))


def fresh(name):
    d = '%s/%s' % (T, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def run(script, savedir, *opts):
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--mute', '--seed', '1', '--log-events', '--save-dir', savedir] + list(opts) +
                       ['--script', script], capture_output=True, text=True, timeout=600)
    return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


oracle = bs4_oracle.Oracle()
names = oracle.names


class Sim:
    """the hotspots of a terminal page that are enabled at load, and the verb-0x17 item groups (spec 1.6 / 1.7)"""

    def __init__(self, n):
        self.p = oracle.page(n)
        self.en = [not (h['flags'] & 1) for h in self.p.hotspots]

    def top_at(self, x, y):
        for i in range(len(self.p.hotspots) - 1, -1, -1):
            l, t, r, b = self.p.hotspots[i]['rect']
            if self.en[i] and l <= x <= r and t <= y <= b:
                return i
        return -1

    def point(self, hi):
        l, t, r, b = self.p.hotspots[hi]['rect']
        cx, cy = (l + r) // 2, (t + b) // 2
        if self.top_at(cx, cy) == hi:
            return cx, cy
        for y in range(t, b + 1):
            for x in range(l, r + 1):
                if self.top_at(x, y) == hi:
                    return x, y
        return None

    def named(self, name):
        for i, h in enumerate(self.p.hotspots):
            if h['name'] == name and self.en[i]:
                return i
        return -1

    def apply17(self, h):
        a = h['action']
        for n in range(31):
            for i, hh in enumerate(self.p.hotspots):
                if hh['name'].startswith('%d_' % n):
                    if (a[1] >> n) & 1:
                        self.en[i] = True
                    if (a[4] >> n) & 1:
                        self.en[i] = False


def page_no(name):
    return names.index(name) + 1


def edges(n):
    """(target page, point, hotspot button index) of the verb-0x12 hotspots enabled at load (email1 / synth1 are remapped to the password pages)"""
    s = Sim(n)
    out = []
    for i, h in enumerate(s.p.hotspots):
        if s.en[i] and h['action'][0] == 0x12:
            pt = s.point(i)
            if pt:
                t = h['action'][1]
                if t == page_no('email1'):
                    t = page_no('emailpw')
                if t == page_no('synth1'):
                    t = page_no('synthpw')
                out.append((t, pt, h['button']))
    return out


start = page_no('menuscreen')
paths = {start: []}
queue = [start]
while queue:
    n = queue.pop(0)
    for t, pt, btn in edges(n):
        if t not in paths and 1 <= t <= 45:
            paths[t] = paths[n] + [(pt, t, btn)]
            queue.append(t)


def clicks_to(page):
    """script commands of the clicks that lead from menuscreen to the page, and the click slots they play"""
    cmds, slots = [], []
    for (x, y), t, btn in paths[page_no(page)]:
        cmds += ['click %d,%d' % (x, y), 'advance 50']
        if 1 <= btn <= 14:
            slots.append(btn)
    return cmds, slots


ENTER = ['advance 500', 'page NbrBIOS4', 'advance 500', 'clickhs BUTTON', 'advance 600']       # a game page with a verb-15 hotspot
LOGIN = ['type RUSSELL\\n', 'advance 100']


def state_blocks(out):
    """the `dump bs4` blocks of a run: dicts of what they print"""
    blocks = []
    for l in out:
        m = re.match(r"bs4 page (\S+) panel (\S+) opener '(.*?)' mask (0x[0-9a-f]+) pmask (0x[0-9a-f]+) cursor (\S+)", l)
        if m:
            blocks.append({'page': m.group(1), 'panel': m.group(2), 'opener': m.group(3), 'mask': int(m.group(4), 16),
                           'pmask': int(m.group(5), 16), 'cursor': m.group(6)})
            continue
        m = re.match(r"bs4 session initial=(\d) email=(\d) synth=(\d) mail=(\d+) stamp '(.*)'", l)
        if m and blocks:
            blocks[-1].update(initial=int(m.group(1)), email=int(m.group(2)), synth=int(m.group(3)), mail=int(m.group(4)), stamp=m.group(5))
            continue
        m = re.match(r"bs4 clock '(.*)' '(.*)'", l)
        if m and blocks:
            blocks[-1].update(clockL=m.group(1), clockR=m.group(2))
            continue
        m = re.match(r"bs4 textentry (\d) '(.*)'", l)
        if m and blocks:
            blocks[-1].update(entry=int(m.group(1)), text=m.group(2))
            continue
        m = re.match(r"bs4 return page (\S+) scroll (-?\d+),(-?\d+)", l)
        if m and blocks:
            blocks[-1].update(ret=m.group(1), retscroll=(int(m.group(2)), int(m.group(3))))
            continue
        m = re.match(r'bs4 off sounds (\d+)', l)
        if m:
            blocks.append({'off': True, 'sounds': int(m.group(1))})
            continue
        m = re.match(r'bs4 presents (\d+) sounds (\d+)', l)
        if m and blocks:
            blocks[-1].update(presents=int(m.group(1)), sounds=int(m.group(2)))
    return blocks


def events(out):
    return [l[6:] for l in out if l.startswith('event ') and not l.startswith(('event sound', 'event sprite', 'event movie'))]


def sub(ev, *seq):
    """are the strings of seq found in ev in this order (prefix match)?"""
    i = 0
    for e in ev:
        if i < len(seq) and e.startswith(seq[i]):
            i += 1
    return i == len(seq)


# ----------------------------------------------------------------------------------------------------------------------------------
# 1. entering: verb 15 of a game page, event 18 (0) then event 3, the terminal's first page, the API restriction
d = fresh('s1')
script = ['bs4trace on'] + ENTER[:3] + ['echo mark before', 'clickhs BUTTON', 'echo mark clicked', 'advance 700', 'dump bs4', 'shot %s/bs4glue_initial.bmp' % SHOTS,
                                         'api GetDropZoneCount', 'api WriteLog hello', 'api IsSoundPlaying 0', 'dump state']
rc, out, err = run('; '.join(script), d, '--log-idle')
ev = events(out)
blk = state_blocks(out)
check(rc == 0 and sub(ev, 'hotspot NbrBIOS4 BUTTON', 'bs4screen 0', 'exitpage NbrBIOS4') and not any(e.startswith('pageloaded') for e in ev[ev.index('bs4screen 0'):]),
      'verb 15 (hotspot BUTTON of NbrBIOS4): event 18 (0), then event 3 OnExitPage, and no page is loaded for the terminal', ev[-12:])
check(blk and blk[0].get('page') == 'initial' and blk[0].get('entry') == 1 and blk[0].get('ret') == 'NbrBIOS4' and blk[0].get('initial') == 0,
      'the terminal starts on `initial` in password entry mode and remembers the game page to return to', blk)
check(any(l == 'api GetDropZoneCount rc=23' for l in out) and 'api WriteLog rc=0' in out and 'api IsSoundPlaying rc=16' in out,
      'while the terminal is up the API answers 0x17 to commands outside 2 9 0x14..0x19 0x34 0x35 0x44 0x53 0x54 0x59 (the display-suspended gate of exe:0043da90)',
      [l for l in out if l.startswith('api ')])
check(any(e.startswith('timer') for e in ev) and any(e.startswith('idle') for e in ev[ev.index('bs4screen 0'):]),
      'the game logic still gets event 4 (500 ms timer) and event 17 (idle) while the terminal is up')

# ----------------------------------------------------------------------------------------------------------------------------------
# 2. hover and cursor, the click sounds, the passwords (RUSSELL / wrong), the first page after the login
d = fresh('s2')
script = ['bs4trace on'] + ENTER + ['move 300,300', 'dump bs4', 'move 560,58', 'dump bs4', 'type ABC\\n', 'advance 100', 'dump bs4',
                                     'shot %s/bs4glue_wrong.bmp' % SHOTS] + LOGIN + ['dump bs4', 'shot %s/bs4glue_menuscreen.bmp' % SHOTS]
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ev = events(out)
check(len(blk) >= 4 and blk[0]['cursor'] == 'arrow' and blk[1]['cursor'] == 'hand',
      'the pointer over a hotspot of the terminal shows the hand cursor (BS4HAND), elsewhere the arrow (BS4ARROW)', [b.get('cursor') for b in blk])
check(len(blk) >= 4 and blk[2]['page'] == 'initial' and blk[2]['entry'] == 1 and blk[2]['text'] == '' and 'bs4click 4' in ev,
      'a wrong password plays click slot 4 (Synth10), clears the text and keeps the entry on `initial`', (blk[2:3], ev[-8:]))
check(len(blk) >= 4 and blk[3]['page'] == 'menuscreen' and blk[3]['initial'] == 1 and 'bs4welcome' in ev,
      'RUSSELL: welcome.wav starts, the first-password flag is set and the terminal goes to menuscreen', blk[3:4])

# the same keys typed on a page without text entry reach the game logic as events 14 / 15 (Enter on menuscreen opens `tech`, spec 1.8)
script = ['bs4trace on'] + ENTER + LOGIN + ['key ENTER', 'advance 100', 'dump bs4', 'shot %s/bs4glue_tech.bmp' % SHOTS]
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ev = events(out)
check(blk and blk[0]['page'] == 'tech' and 'keydown vk13' in ev, 'Enter on menuscreen opens `tech` and the key is passed to the game logic (event 14)', (blk, ev[-5:]))

# the loop sounds (exe:0040c4b0 / exe:0040c660 behind the panels' active indicators) and the welcome sound are real sounds of the engine's mixer;
# leaving closes all of them
script = ['bs4trace on'] + ENTER + ['dump bs4', 'bs4loop 2 on', 'bs4loop 2 on', 'advance 100', 'dump bs4', 'bs4loop 2 off', 'bs4loop 7 on', 'advance 100',
                                    'dump bs4', 'click 560,58', 'advance 200', 'dump bs4']
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ev = events(out)
check(len(blk) == 4 and [b.get('sounds') for b in blk] == [0, 1, 2, 0] and ev.count('bs4loop 2 start') == 1 and 'bs4loop 2 stop' in ev and 'bs4loop 7 start' in ev,
      'a loop starts once (a second start while it plays does nothing), stops on request, and leaving the terminal closes every sound of it', ([b.get('sounds') for b in blk], ev))

# ----------------------------------------------------------------------------------------------------------------------------------
# 3. navigation with real clicks: the page, the click sounds of the hotspot buttons, hover cursor, the clock fields ticking
cmds, slots = clicks_to('pollut')
script = ['bs4trace on'] + ENTER + LOGIN + cmds + ['dump bs4', 'shot %s/bs4glue_pollut.bmp' % SHOTS]
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ev = events(out)
played = [int(e.split()[1]) for e in ev if e.startswith('bs4click ')]
check(blk and blk[-1]['page'] == 'pollut' and played == slots,
      'navigation menuscreen -> pollut with %d real clicks; each hotspot button 1..14 plays its click slot %r' % (len(cmds) // 2, slots), (blk[-1:], played))

# a variant panel: the hotspot `techah` opens the spec sheet by the name rule (exe:0040a760), the group hotspot 5_ahs2 opens panelah and the kind-3 hook
# (exe:00409720) picks variant 3 from the opening hotspot
s = Sim(page_no('pollut'))
i = s.named('techah')
pt1 = s.point(i)
s.apply17(s.p.hotspots[i])
j = s.named('5_ahs2')
pt2 = s.point(j) if j >= 0 else None
script = ['bs4trace on'] + ENTER + LOGIN + cmds + ['click %d,%d' % pt1, 'advance 50', 'dump bs4', 'shot %s/bs4glue_spec.bmp' % SHOTS,
                                                    'click %d,%d' % pt2, 'advance 50', 'dump bs4', 'shot %s/bs4glue_variant.bmp' % SHOTS]
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
check(len(blk) >= 2 and blk[-2]['panel'] == 'specah' and blk[-1]['panel'] == 'panelah' and blk[-1]['opener'] == '5_ahs2' and (blk[-1]['pmask'] & 8)
      and not (blk[-1]['pmask'] & 0x10 and blk[-1]['pmask'] & 0x40),
      'a hotspot `tech<x>` opens the spec sheet `spec<x>`; the hotspot 5_ahs2 opens panelah and the hook selects its variant (item 3)', blk[-2:])
check(len(blk) >= 2 and blk[-1]['cursor'] in ('arrow', 'hand'), 'the panel page is shown over the main page (frame saved as bs4glue_variant.bmp)')

# ----------------------------------------------------------------------------------------------------------------------------------
# 4. the other two passwords and their gated pages
for target, word, flag in (('emailpw', 'CYBERSPHERES', 'email'), ('synthpw', 'GREENKING', 'synth')):
    c, sl = clicks_to(target)
    script = ['bs4trace on'] + ENTER + LOGIN + c + ['dump bs4', 'type WRONG\\n', 'advance 50', 'dump bs4', 'type %s\\n' % word, 'advance 100', 'dump bs4',
                                                     'shot %s/bs4glue_%s.bmp' % (SHOTS, target.replace('pw', '1'))]
    rc, out, err = run('; '.join(script), d)
    blk = state_blocks(out)
    ev = events(out)
    want = 'email1' if target == 'emailpw' else 'synth1'
    check(len(blk) >= 3 and blk[-3]['page'] == target and blk[-3]['entry'] == 1 and blk[-2]['page'] == target and blk[-2]['entry'] == 1
          and blk[-1]['page'] == want and blk[-1][flag] == 1 and ev.count('bs4click 5') == 1 and ev.count('bs4click 4') == 1,
          '%s: the link to the gated page ends in `%s`; WRONG stays (click 4), %s opens `%s` (click 5) and sets the flag' % (word, target, word, want), (blk[-3:], ev[-6:]))

# ----------------------------------------------------------------------------------------------------------------------------------
# 5. the clock fields: left HH:MM (query 7), right the date (query 0xb) of dll:10021b50, ticking with the game time
script = ['bs4trace on'] + ENTER + ['dump bs4', 'advance 6000', 'dump bs4']
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ok = len(blk) >= 2 and re.fullmatch(r'\d\d:\d\d', blk[0]['clockL']) and re.fullmatch(r'[A-Z][a-z]{2} \d{1,2} [A-Z][a-z]{2} \d{4}', blk[0]['clockR'])
check(ok and blk[0]['clockR'] == 'Thu 27 Dec 2007' and blk[1]['clockL'] != blk[0]['clockL'],
      'the clock fields differ (time / date), start at 08:00 on Thu 27 Dec 2007 and tick with the game time: %r -> %r' % (blk[0].get('clockL'), blk[1].get('clockL')) if blk else 'no state', blk[:2])
check(ok and blk[0]['clockL'] != blk[0]['clockR'], 'the two clock fields no longer show the same text (spec 7 item 9: query 7 = %H:%M, query 0xb = %a %#d %b %Y)')

# ----------------------------------------------------------------------------------------------------------------------------------
# 6. save while the terminal is up, restart, load: the same page / panel / flags; the file layout; the byte round trip
d = fresh('s6')
s = Sim(page_no('pollut'))
cmds, slots = clicks_to('pollut')
script = ['bs4trace on'] + ENTER + LOGIN + cmds + ['click %d,%d' % pt1, 'advance 50', 'click %d,%d' % pt2, 'advance 50', 'dump bs4',
                                                    'api SaveGame saved game 1.sav', 'api RestartGame', 'advance 300', 'dump bs4', 'dump state',
                                                    'api LoadGame 2 saved game 1.sav', 'advance 300', 'dump bs4', 'shot %s/bs4glue_loaded.bmp' % SHOTS,
                                                    'api SaveGame saved game 2.sav']
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ev = events(out)
check(rc == 0 and len(blk) == 3 and blk[1].get('off'), 'restart destroys the terminal (no event 18 leave, page eintro again)', blk)
a = blk[0]
b = blk[2] if len(blk) > 2 else {}
keys = ('page', 'panel', 'opener', 'mask', 'pmask', 'initial', 'email', 'synth', 'mail', 'ret', 'retscroll')
check(all(a.get(k) == b.get(k) for k in keys) and a['panel'] == 'panelah',
      'load of that save: the same page, panel, opener, item masks, flags and return page', ([(k, a.get(k), b.get(k)) for k in keys if a.get(k) != b.get(k)]))
check(sub(ev, 'restart', 'loadgame Saved Game 1.sav', 'bs4screen 0', 'exitpage eintro'), 'a load from a game page enters the terminal: event 18 (0), then event 3', ev[-8:])
f1 = open(d + '/Saved Game 1.sav', 'rb').read()
f2 = open(d + '/Saved Game 2.sav', 'rb').read()
blob_off = struct.unpack_from('<I', f1, 8)[0]
bs4_flag, bs4_size = struct.unpack_from('<II', f1, 0x60)[0], struct.unpack_from('<I', f1, 0x548)[0]
check(struct.unpack_from('<I', f1, 0x5c)[0] == 1 and bs4_flag == 1 and bs4_size == 0x480 and f1[0x548 + 4:0x548 + 4 + 12].rstrip(b'\0') == b'pollut'
      and f1[0x548 + 4 + 0x18:0x548 + 4 + 0x18 + 12].rstrip(b'\0') == b'panelah' and struct.unpack_from('<I', f1, 0x4cc)[0] == 0x80b71f04,
      'the save has header +0x5c = 1, +0x60 = 1, u32 0x480, the section (page pollut, panel panelah), the flag magic 0x80b71f04 and then the objects section',
      (struct.unpack_from('<III', f1, 0x5c), f1[0x54c:0x558]))
check(f1[:blob_off] == f2[:blob_off] and len(f1) == len(f2),
      'load -> save again of the terminal save: the engine part (header, terminal section, objects) is byte identical (%d bytes; the DLL blob is not idempotent, as for any save)' % blob_off)

# ----------------------------------------------------------------------------------------------------------------------------------
# 7. leaving with QUIT: the game page again with its events; saving afterwards is the normal save (compared with a run that never opened the terminal)
d = fresh('s7')
script = ['bs4trace on'] + ENTER + ['click 560,58', 'advance 300', 'dump bs4', 'dump state', 'api GetDropZoneCount', 'api SaveGame saved game 1.sav',
                                     'shot %s/bs4glue_back.bmp' % SHOTS]
rc, out, err = run('; '.join(script), d)
ev = events(out)
tail = ev[ev.index('bs4screen 0'):] if 'bs4screen 0' in ev else []
check(sub(tail, 'bs4screen 0', 'exitpage NbrBIOS4', 'pageloaded NbrBIOS4', 'pagedrawn NbrBIOS4', 'enterpage NbrBIOS4', 'palettechanged', 'bs4leave')
      and sum(1 for e in tail if e.startswith('exitpage')) == 1 and 'page NbrBIOS4 scroll 0,0' in out and 'bs4 off sounds 0' in out and any(l.startswith('api GetDropZoneCount rc=0') for l in out),
      'QUIT leaves: the page is reloaded (events 7, 8, 2, then 13), no second event 3, the API accepts commands again', tail)
d2 = fresh('s7b')
rc2, out2, err2 = run('; '.join(['advance 500', 'page NbrBIOS4', 'advance 1500', 'api SaveGame saved game 1.sav']), d2)
g1 = open(d + '/Saved Game 1.sav', 'rb').read()
g2 = open(d2 + '/Saved Game 1.sav', 'rb').read()
bo = struct.unpack_from('<I', g1, 8)[0]
bo2 = struct.unpack_from('<I', g2, 8)[0]
check(struct.unpack_from('<II', g1, 0x5c) == (0, 0) and bo == bo2 and g1[:bo] == g2[:bo],
      'a save made after leaving the terminal is the normal one: +0x5c = +0x60 = 0 and the engine part equals that of a run that never opened it (%d bytes)' % bo,
      (struct.unpack_from('<II', g1, 0x5c), bo, bo2))

# ----------------------------------------------------------------------------------------------------------------------------------
# 8. load while the terminal is up: a save without a terminal leaves it for the saved page, a save with one restores it in place
d = fresh('s8')
script = (['bs4trace on'] + ENTER + ['api SaveGame saved game 1.sav', 'click 560,58', 'advance 300', 'api SaveGame saved game 2.sav'] +
          ENTER[:1] + ['clickhs BUTTON', 'advance 600'] + LOGIN + ['api SaveGame saved game 3.sav', 'dump bs4', 'api LoadGame 2 saved game 2.sav', 'advance 300', 'dump bs4',
                                                                    'dump state', 'api LoadGame 2 saved game 3.sav', 'advance 300', 'dump bs4',
                                                                    'api LoadGame 2 saved game 1.sav', 'advance 300', 'dump bs4'])
rc, out, err = run('; '.join(script), d)
blk = state_blocks(out)
ev = events(out)
sav = [struct.unpack_from('<II', open('%s/Saved Game %d.sav' % (d, n), 'rb').read(), 0x5c) for n in (1, 2, 3)]
check(rc == 0 and sav == [(1, 1), (0, 0), (1, 1)] and len(blk) == 4, 'the three saves: with the terminal up (1, 1), after leaving (0, 0), with the terminal up again', (sav, blk))
if len(blk) == 4:
    check(blk[1].get('off') and sub(ev, 'loadgame Saved Game 2.sav', 'pageloaded NbrBIOS4', 'enterpage NbrBIOS4', 'palettechanged', 'bs4leave'),
          'loading a save without the terminal while it is up: the terminal is left (events 7, 8, 2, 13, 18 leave)', ev[-14:])
    check(blk[0].get('page') == 'menuscreen' and blk[2].get('page') == 'menuscreen' and blk[2].get('initial') == 1 and sub(ev, 'loadgame Saved Game 3.sav', 'bs4screen 0'),
          'loading a terminal save from a game page enters the terminal on its saved page with its flags', blk[2])
    check(blk[3].get('page') == 'initial' and blk[3].get('initial') == 0 and 'loadgame Saved Game 1.sav' in ev[-3:] + ['loadgame Saved Game 1.sav'],
          'loading a terminal save into a running terminal restores its page (`initial`, from before the login) and the unset flag in place', blk[3])

print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
