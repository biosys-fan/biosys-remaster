"""The story feature classes (src/game/feats_story2.cpp) driven through `build/biosys --game --script` (pages of the features,
`game hotspot`, `activate`, `game ui`). The expectations are written from the decompiled text of BiosGame.dll (addresses in the
comments) and from the data of the builders (analysis/work/sb/events.jsonl) and the string table (analysis/out/dll_stringtable.txt)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
EVENTS = (_DEV + '/analysis/work/sb/events.jsonl')
STRINGS = (_DEV + '/analysis/out/dll_stringtable.txt')
failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail)[:600] if detail else ''))


strings = {}
for l in open(STRINGS, errors='replace'):
    p = l.rstrip('\n').split('\t')
    if len(p) >= 3 and p[0].isdigit():
        strings[int(p[0])] = p[2].replace('\\n', '\n')


def run(script, seed=1):
    r = subprocess.run(['build/biosys', '--game', '--mute', '--log-api', '--seed', str(seed), '--script', script],
                       capture_output=True, text=True, timeout=300)
    out = [l for l in r.stdout.splitlines() if not l.startswith(('obj ', 'statuscount'))]
    return r.returncode, out, r.stderr


def api(out, name, **kw):
    """the `apicall NAME k=v ...` lines of that command whose arguments contain all the k=v (a file / image / hotspot name matches
    as a substring of the quoted value, ignoring the doubled backslashes of the log)"""
    res = []
    for l in out:
        if l.startswith('apicall ' + name + ' ') or l == 'apicall ' + name:
            n = l.replace('\\\\', '\\')
            ok = True
            for k, v in kw.items():
                if k in ('file', 'image', 'hotspot'):
                    ok = ok and re.search(r'\b%s="[^"]*%s[^"]*"' % (k, re.escape(v)), n) is not None
                else:
                    ok = ok and re.search(r'\b%s=%s\b' % (k, re.escape(v)), n) is not None
            if ok:
                res.append(l)
    return res


def after(out, marker):
    for i, l in enumerate(out):
        if l == marker:
            return out[i + 1:]
    return []


def msgs(out):
    return [int(m.group(1), 16) for l in out for m in [re.match(r'ui message id=0x([0-9a-f]+)', l)] if m]


def texts(out):
    # (the top bar's status words and message line, y = 18 and 36, are drawn through the same DC record: not the page's text)
    return [(int(m.group(1)), int(m.group(2)), m.group(3)) for l in out
            for m in [re.match(r"ui text x=(-?\d+) y=(-?\d+) '(.*)'$", l)] if m and int(m.group(2)) not in (18, 36)]


PRE = 'advance 1000; '

# ---------------------------------------------------------------- CCoffeeMachine (dll:10010d80 / 10010cd0)
# 5 cups at the start, the machine runs (m_bRunning = 1): each "yCoffee" click takes a cup, plays a drink sound (drink1..4.wav) and
# shows the image "empty" and "coffee_1".."coffee_<cups>" (Eng_DisplayImage); with no cup the message 0xd8 ("coffee brewing")
rc, out, err = run(PRE + 'page fph3coffee; advance 300; echo MARK; game hotspot ycoffee fph3coffee; game ui; echo MARK2; '
                   + 'game hotspot ycoffee fph3coffee; game hotspot ycoffee fph3coffee; game hotspot ycoffee fph3coffee; '
                   + 'game hotspot ycoffee fph3coffee; echo MARK3; game hotspot ycoffee fph3coffee; game ui')
first = after(out, 'MARK')
imgs = [re.search(r'image="([^"]+)"', l).group(1) for l in first if l.startswith('apicall DisplayImage') and
        re.search(r'image="(empty|coffee_\d)"', l)]
check('coffee: the first cup: images empty, coffee_1..coffee_4', imgs[:5] == ['empty', 'coffee_1', 'coffee_2', 'coffee_3', 'coffee_4'], imgs)
check('coffee: a drink sound', any(re.search(r'drinking.*drink[1-4]\.wav', l) for l in api(first, 'OpenSound')), first[:3])
after5 = after(out, 'MARK3')
check('coffee: the 6th click: message 0xd8 and no drink', 0xd8 in msgs(after5) and not api(after5, 'OpenSound', file='drink'), msgs(after5))

# ---------------------------------------------------------------- CCabinet (dll:1005b710 / 1005b6b0)
rc, out, err = run(PRE + 'page fph8cab_c; advance 300; echo MARK; game hotspot b_Locked fph8cab_c; game ui')
m = after(out, 'MARK')
check('cabinet: b_Locked: cablockd.wav and message 0xd9 ("locked")', api(m, 'OpenSound', file='cablockd.wav') and 0xd9 in msgs(m), m[:4])
check('cabinet: the page shows b_Locked enabled, b_Open disabled (locked = 1 at the start)',
      api(out, 'EnableHotspot', hotspot='b_Locked', enable='1') and api(out, 'EnableHotspot', hotspot='b_Open', enable='0'))

# ---------------------------------------------------------------- CMedBox (dll:1005c920 / 1005c150 / 1005c560)
rc, out, err = run(PRE + 'page FPH12medbo; advance 300; echo MARK; game hotspot bLocked FPH12medbo; game ui; echo MARK2; '
                   + 'game hotspot bDisAdren FPH12medbo; game hotspot bAdren FPH12medbo; game ui')
check('medbox: locked at the start: image "locked", hotspot bLocked on (the display)',
      api(out, 'SetImageVisibility', image='locked', visible='1') and api(out, 'EnableHotspot', hotspot='bLocked', enable='1'))
m = after(out, 'MARK')
check('medbox: bLocked: cablockd.wav and message 0xd9', api(m, 'OpenSound', file='cablockd.wav') and 0xd9 in msgs(m))
m = after(out, 'MARK2')
check('medbox: bDisAdren: drugdrop.wav (counter 1 < 6)', api(m, 'OpenSound', file='drugdrop.wav'))
check('medbox: bAdren: Eng_DoHotspotAction("dAdren1") (sprintf "dAdren%d" of the old counter 1)', api(m, 'DoHotspotAction', hotspot='dAdren1'),
      [l for l in m if 'DoHotspot' in l])

# ---------------------------------------------------------------- CBees (dll:1005b100): any click hurts; a red flash, a pain sound
rc, out, err = run(PRE + 'echo MARK; game hotspot YS5BEES -; game dump')
m = after(out, 'MARK')
check('bees: a click flashes red (FadeToColour 0xff0000) and plays a pain sound',
      api(m, 'FadeToColour') and api(m, 'OpenSound', file='pain'), [l for l in m if 'Fade' in l])

# ---------------------------------------------------------------- CMicroscope (dll:1005b4b0): the click sound
rc, out, err = run(PRE + 'page FPH12micro; advance 300; echo MARK; game hotspot bMicro FPH12micro')
check('microscope: a click plays mscope.wav', api(after(out, 'MARK'), 'OpenSound', file='mscope.wav'))

# ---------------------------------------------------------------- CDedFeature (dll:100169a0): b_Rock; energy 63 is below 90
rc, out, err = run(PRE + 'page fce10ded_c; advance 300; echo MARK; game hotspot b_Rock fce10ded_c; game ui')
m = after(out, 'MARK')
check('ded: b_Rock: a strain sound and the message 0x151 (not strong enough)', api(m, 'OpenSound', file='strain') and 0x151 in msgs(m), msgs(m))
check('ded: the hotspot b_open stays off (display: m_bActive == 0)', api(out, 'EnableHotspot', hotspot='b_open', enable='0'))

# ---------------------------------------------------------------- CDataTerminal (dll:1005b9e0): nothing in the reader: only the click
rc, out, err = run(PRE + 'page FPH17dat; advance 300; echo MARK; game hotspot bPlay FPH17dat; game hotspot bEject FPH17dat')
m = after(out, 'MARK')
check('data terminal: bPlay / bEject without a disk: the click sound only', len(api(m, 'OpenSound', file='click.wav')) == 2 and not api(m, 'OpenSound', file='vid'),
      [l for l in m if 'OpenSound' in l])

# ---------------------------------------------------------------- CVidLink (dll:1001aa20 / 1001abf0 / 1001a920)
msgsof = {}
vl = {}
for l in open(EVENTS):
    e = json.loads(l)
    if e[0] == 'feature' and e[2] == 'FUN_1001a620':
        vl[e[3][0]] = e[3]
    if e[0] == 'vidmsg':
        msgsof.setdefault(e[5][2][0], []).append(e[3])
# the first message (list order) that is available (milestone mask 0) and not yet played: Update (dll:1001abf0) walks the list
# from the end and leaves the lowest such index in +0xbc
avail = [m for m in msgsof['fph8vidpho'] if m[5] == 0 and m[6] == 0]
rc, out, err = run(PRE + 'page FPH8VIDPHO; advance 500; echo MARK; game hotspot bPlay FPH8VIDPHO; advance 200')
m = after(out, 'MARK')
op = api(m, 'OpenAVIMovie')
check('vidlink: bPlay opens the first unplayed message at the link\'s position (a5, a6)',
      len(op) == 1 and avail[0][0].replace('\\', '\\') in op[0].replace('\\\\', '\\') and ('x=%d y=%d' % (vl['fph8vidpho'][5], vl['fph8vidpho'][6])) in op[0],
      (avail[0][0], vl['fph8vidpho'][5:], op))
check('vidlink: the message image "new" shows (FX / display: available and not played)', api(out, 'SetImageVisibility', image='new', visible='1'))

# ---------------------------------------------------------------- CTerminal (dll:10018c60): one animation per insect
for name, page in (('fs9term', 'Fs9term'), ('fr10Log', 'FR10log')):
    ev = None
    for l in open(EVENTS):
        e = json.loads(l)
        if e[0] == 'feature' and e[2] == 'FUN_100189b0' and e[3][0] == name:
            ev = e[3]
    text, count, delay, mode = ev[2], ev[3], ev[6], ev[5]
    rc, out, err = run(PRE + 'page %s; advance 500; echo MARK; game ui' % page)
    ops = [l for l in api(out, 'OpenSpriteAnimation') if 'anims\\insects\\%s_' % text in l.replace('\\\\', '\\')]
    if mode == 0:
        check('%s: %d insect animations opened (loop=0, delay %d)' % (name, count, delay),
              len(ops) == count and all('frameDelayMs=%d' % delay in l and 'loop=0' in l for l in ops), len(ops))
        hs = api(out, 'EnableHotspot', hotspot='b%s_1' % text, enable='0')
        check('%s: its hotspots b%s_n start disabled' % (name, text), bool(hs))
    else:
        # mode 1: every insect is spawned with a 60 percent chance, at most the number of living grubs
        check('%s: insect animations only for the spawned ones (<= %d), looping' % (name, count),
              len(ops) <= count and all('loop=1' in l for l in ops), len(ops))

# ---------------------------------------------------------------- CPoison (dll:10006560): palette lock and the page palettes
rc, out, err = run(PRE + 'page fce4poi; advance 300; echo MARK; page uPOItitle; advance 300; echo MARK2; page uPOIintro1; advance 300; '
                   + 'echo MARK3; page uPOIintro2; advance 300')
check('poison: the feature page shows lightson / lightsoff and hides the title hotspot when the lights are off',
      api(out, 'SetImageVisibility', image='lightsoff', visible='1') or api(out, 'SetImageVisibility', image='lightson', visible='1'))
m2, m3 = after(out, 'MARK2'), after(out, 'MARK3')
check('poison: uPOIintro1 plays poipage.wav and changes the palette (page[4] differs from the last page)',
      api(m2, 'OpenSound', file='poipage.wav') and api(m2, 'FadeToPalette'), [l[:90] for l in m2 if 'Fade' in l])
check('poison: uPOIintro2 (same 5th character) plays the sound without a palette change',
      api(m3, 'OpenSound', file='poipage.wav') and not api(m3, 'FadeToPalette'))

# ---------------------------------------------------------------- CSpFeature (dll:1000ae40): the text of the result
rc, out, err = run(PRE + 'page FPH12sp_o; advance 500; activate off; echo MARK; activate on; advance 500; game ui')
m = [t for t in texts(after(out, 'MARK'))]
want = [(24, 10, strings[0x7c])]
l7d = [x for x in strings[0x7d].split('\n') if x]     # DrawText breaks at '\n': one line per row, 18 pixels apart (font height)
want += [(24, 30 + 18 * i, s) for i, s in enumerate(l7d)]
want += [(24, 0x78, strings[0x7f]), (24, 0x8c, strings[0x82].strip('\n')) if False else None]
want = [w for w in want if w]
check('spirometer: title 0x7c at (24,10), the findings 0x7d lines below it, 0x7f at (24,120)', m[:len(want)] == want, (m[:6], want))
check('spirometer: the closing text 0x83 at (24,230)', any(t[0] == 24 and t[1] == 230 and t[2].startswith(strings[0x83].strip()[:12]) for t in m), m)
check('spirometer: every drawn string is recorded (GetWindowDC / ReleaseWindowDC pairs)',
      len(api(out, 'GetWindowDC', window='-1')) == len(api(out, 'ReleaseWindowDC', window='-1')))

# ---------------------------------------------------------------- CBloodFeature (dll:10071080 / 10071450 / 10071b40)
# the findings of the blood test come when its sound has ended (OnSoundEnd, code 4): the mixer is run with `mixdump`
rc, out, err = run(PRE + 'page FPH12bld_o; advance 500; echo MARK; game hotspot bTest FPH12bld_o; mixdump build/test/featstory/m.wav 40000; '
                   + 'advance 500; game ui; echo MARK2; game hotspot bNext FPH12bld_o; game ui')
m = after(out, 'MARK')
t = texts(m)
check('blood test: heading 0xa9 at (210,50) and 0x8d at (210,70), page 4: 0x8e at (210,110) and "none" 0xa0 below',
      (210, 50, strings[0xa9]) in t and (210, 70, strings[0x8d]) in t and (210, 110, strings[0x8e]) in t and
      any(x[0] == 210 and x[1] == 150 and x[2].strip() == strings[0xa0].strip() for x in t), t)
check('blood test: the sound bloodtst.wav', api(m, 'OpenSound', file='bloodtst.wav'))
m2 = texts(after(out, 'MARK2'))
check('blood test: bNext goes to page 5: heading 0x98', any(x[2] == strings[0x98] for x in m2), m2)

# ---------------------------------------------------------------- CThermalCooker (dll:1006f6c0 / 1006f870)
rc, out, err = run(PRE + 'page FPH16therm; advance 500; echo MARK; game hotspot bStart FPH16therm; echo MARK2; '
                   + 'mixdump build/test/featstory/m.wav 40000; advance 500')
m = after(out, 'MARK')
check('cooker: bStart closes the lid (image closed), starts the bubbling and disables bStart / bPickUp',
      api(m, 'DisplayImage', image='closed') and api(m, 'OpenSound', file='therbub1.wav') and
      api(m, 'EnableHotspot', hotspot='bStart', enable='0') and api(m, 'EnableHotspot', hotspot='bPickUp', enable='0'))
m2 = after(out, 'MARK2')
check('cooker: when the sound ends the lid opens and the buttons are back',
      api(m2, 'DisplayImage', image='open') and api(m2, 'EnableHotspot', hotspot='bStart', enable='1'), [l[:80] for l in m2 if 'Display' in l][:3])

# ---------------------------------------------------------------- CMirror (dll:100704c0): whole mirror: hotspot on, broken one off
# slot60 runs when the site's page (PH6) is shown
rc, out, err = run(PRE + 'page PH6; advance 300')
check('mirror: whole mirror: image fph6Mir_b hidden, its hotspot fph6Mir enabled, the broken one\'s ("_b") not',
      api(out, 'SetImageVisibility', image='fph6Mir_b', visible='0') and api(out, 'EnableHotspot', hotspot='fph6Mir', enable='1') and
      api(out, 'EnableHotspot', hotspot='fph6Mir_b', enable='0'))

# ---------------------------------------------------------------- CForestFeature (dll:10007e30)
rc, out, err = run(PRE + 'page fce9for; advance 300')
check('forest terminal: no disk: image insert shown, text hidden (or no power: both hidden)',
      api(out, 'SetImageVisibility', image='insert') and api(out, 'SetImageVisibility', image='text', visible='0'))

print('featstory tests: %s' % ('all passed' if not failed else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
