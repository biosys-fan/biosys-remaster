"""The interaction sweep: every page of the game is entered and every hotspot on it is clicked, each in its own headless run of
build/biosys (about 0.1 s each), and what the game did is written down as one line per interaction.

  python3 -I tests/audit/sweep.py [--jobs N] [--pages A,B,...] [--variant NAME] [--out DIR]

Variants (the state the click is made in):
  base      a new game, nothing in the hand, left click
  power     the same after `game fuses`: every fuse plugged in and every circuit powered (most machines are dead in a new game)
  right     the same, right click (look at)
  hold:OBJ  the object OBJ in the hand (script command `hold`), left click
  take      every object picked up from the zone where the new game puts it (rows: page, OBJECT@zone)
  use       every object in the hand on the outdoor page R4: Space (use), S (stash), D (drop), one run each (rows: OBJECT, use|stash|drop)
  look      right click: on every page, and with every object in the hand, also after Space has opened it (rows: PAGE, rclick / OBJECT, rclick | use+rclick)
            (the engine's right click is not hotspot-specific: it goes to the held object (a paper closes) and to the page's feature)
  apply     on demand (about 42 000 runs, 13 min): one representative object per object class in the hand, left click on every button (B*)
            and drop zone (D*) hotspot; only what differs from the empty-handed click (the record sweep_base / sweep_power) is written,
            crash and stuck always; per hotspot the most common outcome is one row `HOTSPOT|*`, the other objects get their own rows
            (every run is also written to sweep_apply_full.tsv). `--kinds B,D` the hotspot kinds, `--objects a,b` own representatives.  `apply+power` after `game fuses`.
  (+power can be added to every variant: `take+power`)
Environment: BIOSYS_BIN the game binary (default build/biosys; tests/audit/coverage.py points it at the instrumented one).

Per interaction the result has
  class     crash | stuck | off | unreached | moved | react | dead
              crash  the run ended with an error
              stuck  18 s after the click objects and keys are still switched off, on the same page
              off    the hotspot is switched off on the freshly entered page (it needs a state the new game does not have)
              unreached  the hotspot is on but the sweep found no point of the view that hits it
              moved  the click led to another page
              react  something visible or audible happened (picture, sound, animation, film, message, object)
              dead   the game was told about the click and did nothing a player could notice
  failed    engine commands that opened a file and failed (a sound, animation, film, palette that is not there)
  sig       the visible / audible commands in order, shortened
Entering the page itself is the interaction with the hotspot name `-` (what the page shows on entry, and whether the still picture is
stable from frame to frame).

The results go to OUT/sweep_<variant>.tsv (default OUT = build/audit). tests/test_sweep.py compares them with the accepted state."""
import collections, hashlib, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import play_lib

BIN = os.environ.get('BIOSYS_BIN') or os.path.join(ROOT, 'build', 'biosys')
VIEW_W, VIEW_H = 640, 368

# engine commands that a player cannot notice (queries, drawing-context bookkeeping, log lines) are left out of the signature
QUIET = {'GetWindowDC', 'ReleaseWindowDC', 'GetGameObjectCount', 'GetGameObjectName', 'GetGameObjectInfo', 'GetDropZoneCount',
         'GetDropZoneInfo', 'GetHotspotNameByIndex', 'IsSoundPlaying', 'IsSpriteAnimationPlaying', 'IsAVIMoviePlaying', 'GetPalette',
         'GetSpriteAnimationSound', 'GetDropZoneForHotspot', 'GetPanoramaInfo', 'GetStatusBarObjectUnderCursor', 'GetReturnPageName',
         'GetSaveDirectory', 'SetTimerInterval', 'EnablePageTimer', 'NoOp13', 'NoOp44', 'NoOp59', 'Disabled62', 'Disabled63',
         'EnableMouseDeltaEvent', 'ShowCursor', 'SetSoundProperty', 'SetSoundPanToPagePosition', 'RedrawPage', 'RedrawScreen',
         'EnableUserInput', 'SetScrollMode', 'CloseSound', 'StopSound', 'CloseSpriteAnimation', 'StopSpriteAnimation', 'CloseAVIMovie',
         'StopAVIMovie', 'SetGameObjectProperty', 'AddRemoveFontResource', 'CreateDeleteLogicalFont', 'SetAVIMovieVolume',
         'ClearWindow', 'EnableHotspot', 'ReleasePageImageSlot', 'SetPageFilterBuffer'}
# commands that open a file: a failure is a missing or misnamed asset
OPENERS = {'OpenSound', 'OpenSpriteAnimation', 'OpenAVIMovie', 'PrepareSynthAVIMovie', 'SetPalette', 'FadeToPalette',
           'PlayObjectUseAnimation', 'DisplayPopupPage', 'JumpToPage', 'BeginPageFilter'}
# (DisplayImage / SetImageVisibility fail routinely and harmlessly: the game shows a site's pictures on whatever page is up)
# commands that count as "the player notices something"
VISIBLE = {'OpenSound', 'PlaySound', 'OpenSpriteAnimation', 'PlaySpriteAnimation', 'OpenAVIMovie', 'PlayAVIMovie', 'PrepareSynthAVIMovie',
           'SetPalette', 'FadeToPalette', 'FadeToColour', 'FillScreen', 'DisplayImage', 'SetImageVisibility', 'RaisePageImage',
           'JigglePage', 'BeginPageFilter', 'EndPageFilter', 'PlayObjectUseAnimation', 'DropHeldObject', 'ReturnGameObject',
           'ReturnGameObjectHidden', 'PlaceGameObject', 'RemoveGameObject', 'ChangeGameObjectVariation', 'ChangeGameObjectVariation1',
           'ChangeGameObjectHoldingImagesGroup', 'DisplayPopupPage', 'ClosePopupWindow', 'JumpToPage', 'DoHotspotAction', 'ShowMessage',
           'OpenMenu', 'QuitGame', 'RestartGame', 'SaveGame', 'LoadGame', 'BeginTextInput', 'EndTextInput', 'DoPageTimerAction'}

_API = re.compile(r'^apicall (\w+)(.*?) -> (-?\d+)(.*)$')
_ARG = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)')


def short_call(name, args):
    a = dict((k, v.strip('"').replace('\\\\', '\\')) for k, v in _ARG.findall(args))
    if name in ('OpenSound', 'OpenSpriteAnimation', 'OpenAVIMovie', 'PrepareSynthAVIMovie'):
        return '%s %s' % (name, a.get('file', '?').lower())
    if name == 'SetImageVisibility':
        return 'img %s=%s' % (a.get('image', '?').lower(), a.get('visible', '?'))
    if name == 'DisplayImage':
        return 'DisplayImage %s' % a.get('image', a.get('name', '?')).lower()
    if name in ('JumpToPage', 'DisplayPopupPage'):
        return '%s %s' % (name, a.get('page', a.get('name', '?')).lower())
    if name == 'DoHotspotAction':
        return 'DoHotspotAction %s' % a.get('hotspot', '?').lower()
    if name in ('PlaySound', 'PlaySpriteAnimation', 'PlayAVIMovie'):
        return name
    if name == 'PlayObjectUseAnimation':
        return 'UseAnim %s %s' % (a.get('anim', a.get('file', '?')).lower(), a.get('sound', a.get('wav', '')).lower())
    keep = ' '.join('%s=%s' % (k, v.lower()) for k, v in a.items() if k not in ('window', 'handle', 'dc'))
    return ('%s %s' % (name, keep)).strip()[:90]


def summarise(lines):
    """(signature list, failed list, visible count) of the log lines of one interaction"""
    sig, failed, visible = [], [], 0
    last = None
    for l in lines:
        if l.startswith('apicall '):
            m = _API.match(l)
            if not m:
                continue
            name, args, rc = m.group(1), m.group(2), int(m.group(3))
            if name == 'WriteLog':
                pm = re.search(r'text="player-message (\d+)', l)
                if pm:
                    sig.append('message %s' % pm.group(1))
                    visible += 1
                continue
            if name in OPENERS and rc != 0:
                failed.append('%s rc=%d' % (short_call(name, args), rc))
            if name in QUIET:
                continue
            s = short_call(name, args)
            if name == 'SetImageVisibility' and rc != 0:
                continue                        # the page has no picture of that name (loops over every possible name are normal)
            if name in VISIBLE:
                visible += 1
            if s != last:
                sig.append(s)
            last = s
        elif l.startswith('event '):
            p = l.split()
            kind = p[1] if len(p) > 1 else ''
            if kind in ('anim', 'gameobject', 'page', 'enterpage', 'pagedrawn', 'pageentered', 'exitpage', 'popup', 'avi', 'movie', 'key', 'drop'):
                s = ' '.join(p[1:5]).lower()
                if kind in ('anim', 'gameobject', 'avi', 'movie'):
                    visible += 1
                if s != last:
                    sig.append('ev ' + s)
                last = s
    return sig, failed, visible


def ambient_key(item):
    """a signature item with what varies from run to run removed (which of the numbered moans, which frame of a looping film)"""
    if item.startswith('OpenSound '):
        return re.sub(r'\d+', '#', item)
    if item.startswith('ev movie') or item.startswith('ev avi'):
        return 'ev movie'
    if item.startswith('SetPalette') or item.startswith('FadeToPalette'):
        return item.split()[0]
    return item


_INVISIBLE_ITEMS = ('ev page', 'ev enterpage', 'ev pagedrawn', 'ev pageentered', 'ev exitpage', 'ev key', 'ev drop', 'ev popup')


def is_visible(item):
    return not item.startswith(_INVISIBLE_ITEMS)


def state_of(lines):
    st = {}
    for l in lines:
        m = re.match(r'page (\S+) scroll', l)
        if m:
            st['page'] = m.group(1).upper()
        m = re.match(r'held (\S+)', l)
        if m:
            st['held'] = m.group(1)
        m = re.match(r'worn (\S+)', l)
        if m:
            st['worn'] = m.group(1)
        m = re.match(r'input (\d+) simstatus (\d+)', l)
        if m:
            st['input'] = int(m.group(1))
        m = re.match(r'framehash (\S+)', l)
        if m:
            st.setdefault('hashes', []).append(m.group(1))
    return st


def run_script(script, tmp, extra=()):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    cmd = [BIN, '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', tmp, '--log-events', '--log-api'] + list(extra) + ['--script', script]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, timeout=120, cwd=ROOT)
        return r.returncode, r.stdout.splitlines(), r.stderr.strip().splitlines()
    except subprocess.TimeoutExpired:
        return -99, [], ['timeout after 120 s']


def split_marks(lines):
    parts, cur = {'': []}, ''
    for l in lines:
        if l.startswith('##'):
            cur = l[2:].strip()
            parts[cur] = []
        else:
            parts[cur].append(l)
    return parts


USE_PAGE = 'R4'                         # the neutral outdoor page of the object sweeps (where a new game starts)
LOOK_AT = '320,240'                     # a click point of the view for the right click (it is not hotspot-specific)
SPOT_EXTRA = {'plain': ['CObject', 'CSurvivalBag', 'CCompass']}


def objects_table():
    """[(name, zone page, zone hotspot, status, usage)] of `dump objects` of a new game"""
    rc, out, err = run_script('advance 100; dump objects', tempfile.mkdtemp(prefix='biosys-sweep-o-'))
    res = []
    for l in out:
        m = re.match(r'obj \d+ (\S+) status=(\d+)(?: zone=(\S+?)/(\S+))? ', l + ' ')
        if m:
            res.append((m.group(1), (m.group(3) or '').upper(), m.group(4) or '', int(m.group(2))))
    return res


def object_classes(names):
    """{name: class key}: objects of one class have the same members (`game obj`), for food / consumables / extinguishers also the same
    code. Objects whose members are the plain item set (the CObject family: bag, compass, key ...) are each their own class."""
    cmd = 'advance 100; ' + '; '.join('echo ##%s; game obj %s' % (n, n) for n in names)
    rc, out, err = run_script(cmd, tempfile.mkdtemp(prefix='biosys-sweep-o-'))
    mem, val, cur = {}, {}, None
    for l in out:
        if l.startswith('##'):
            cur = l[2:].strip()
            continue
        m = re.match(r'obj\.(\S+?)\.(\w+) = (.*)', l)
        if m and cur:
            mem.setdefault(cur, []).append(m.group(2))
            val[(cur, m.group(2))] = m.group(3)
    base = ['name', 'm_dwNameStringId', 'm_dwFlags', 'm_dwPickupEvent', 'm_dWeight', 'm_dwTimeStamp', 'm_bInScene', 'm_bHeld', 'm_szVariant']
    cls = {}
    for n, m in mem.items():
        key = ','.join(x for x in m if x not in base) or 'plain:' + n
        for f in ('m_wConsumableCode', 'm_wContentCode', 'm_wType'):
            if (n, f) in val:
                key += '/' + val[(n, f)]
        cls[n] = key
    return cls


def holdable(names):
    """the names `hold` can put in the hand in a new game (a plant's fruit cannot: it is eaten at pick-up)"""
    def test(n):
        rc, out, err = run_script('advance 100; hold %s' % n, tempfile.mkdtemp(prefix='biosys-sweep-o-'))
        return n if any(l == 'hold %s 0' % n for l in out) else None
    with ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 4) - 2)) as ex:
        return {n for n in ex.map(test, names) if n}


def representatives(table):
    """one object per object class that can be held; (list in table order, {name: class})"""
    names = [t[0] for t in table]
    cls = object_classes(names)
    ok = holdable(names)
    reps = {}
    for n in names:
        if n in cls and n in ok:
            reps.setdefault(cls[n], n)
    return sorted(reps.values(), key=names.index), cls


class Sweep:
    def __init__(self, variant='base', kinds='B,D', objects=None):
        self.variant = variant
        parts = variant.split('+')
        self.power = 'power' in parts
        rest = [p for p in parts if p != 'power']
        self.kind = rest[0].split(':')[0] if rest else 'base'
        self.hold = rest[0][5:] if rest and rest[0].startswith('hold:') else ''
        self.kinds = tuple(k.strip().upper() for k in kinds.split(',') if k.strip())
        self.objects = objects
        self.pages = play_lib.load_pages()
        self.tmp = tempfile.mkdtemp(prefix='biosys-sweep-')
        self.ambient = {}                  # page -> what happens there in 6 s without a click (normalised signature items)
        self.table = None

    def scroll_for(self, page, rect):
        if page not in self.pages or not rect:
            return 0, 0
        w, h = self.pages[page]['size']
        l, t, r, b = rect
        cx, cy = (l + r) // 2, (t + b) // 2
        sx = max(0, min(max(0, w - VIEW_W), cx - VIEW_W // 2)) if w > VIEW_W else 0
        sy = max(0, min(max(0, h - VIEW_H), cy - VIEW_H // 2)) if h > VIEW_H else 0
        return sx, sy

    def rect_of(self, page, hotspot):
        for name, rect, flags, button, verb in self.pages.get(page, {}).get('hot', []):
            if name.lower() == hotspot.lower():
                return rect
        return None

    def hotspot_jobs(self, only):
        out = []
        for page in sorted(self.pages):
            if only and page not in only:
                continue
            seen = set()
            for name, rect, flags, button, verb in self.pages[page]['hot']:
                if name.lower() in seen:
                    continue
                seen.add(name.lower())
                out.append((page, name, rect))
        return out

    def jobs(self, only=None):
        """[(page, hotspot or '-', rect or None, spec or None)]; spec = {where, what, setup, act, click, scroll} of the object sweeps"""
        k, out = self.kind, []
        if k in ('base', 'power', 'right'):
            for page, name, rect in self.hotspot_jobs(only):
                out.append((page, name, rect, None))
            if k != 'right':
                out = [(p, '-', None, None) for p in sorted(self.pages) if not only or p in only] + out
            return out
        if k == 'hold':
            return [(page, name, rect, dict(where=page, what=name, setup=['hold %s' % self.hold], act=['clickhs %s' % name], click=True, scroll=rect))
                    for page, name, rect in self.hotspot_jobs(only)]
        if self.table is None:
            self.table = objects_table()
        names = [t[0] for t in self.table]
        if k == 'take':
            for name, zpage, zhot, status in self.table:
                if only and zpage not in only:
                    continue
                if not zpage:
                    continue
                rect = self.rect_of(zpage, zhot)
                out.append((zpage, zhot, rect, dict(where=zpage, what='%s@%s' % (name, zhot), setup=[], act=['clickhs %s' % zhot],
                                                     click=True, scroll=rect, hold_expect=name, note='status %d' % status)))
        elif k == 'use':
            for n in names:
                for what, keys in (('use', ['key SPACE']), ('stash', ['key S']), ('drop', ['key D'])):
                    out.append((USE_PAGE, '', None, dict(where=n, what=what, setup=['hold %s' % n], act=keys, click=False, scroll=None, obj=n)))
        elif k == 'look':
            for page in sorted(self.pages):
                if not only or page in only:
                    out.append((page, 'rclick', None, dict(where=page, what='rclick', setup=[], act=['rclick ' + LOOK_AT], click=False, scroll=None)))
            for n in names:
                out.append((USE_PAGE, '', None, dict(where=n, what='rclick', setup=['hold %s' % n], act=['rclick ' + LOOK_AT], click=False, scroll=None, obj=n)))
                out.append((USE_PAGE, '', None, dict(where=n, what='use+rclick', setup=['hold %s' % n],
                                                       act=['key SPACE', 'advance 3000', 'rclick ' + LOOK_AT], click=False, scroll=None, obj=n)))
        elif k == 'apply':
            if self.objects:
                reps = self.objects
            else:
                reps, _ = representatives(self.table)
            for page, name, rect in self.hotspot_jobs(only):
                if name[:1].upper() not in self.kinds:
                    continue
                for o in reps:
                    out.append((page, name, rect, dict(where=page, what='%s|%s' % (name, o), setup=['hold %s' % o], act=['clickhs %s' % name],
                                                       click=True, scroll=rect, obj=o)))
        else:
            raise SystemExit('unknown variant ' + self.variant)
        return out

    def script(self, page, name, rect, spec=None):
        s = ['advance 1500']
        if self.power:
            s += ['game fuses', 'advance 500']
        s += ['echo ##enter', 'page %s' % page, 'advance 800', 'echo ##begin', 'dump state']
        if name == '-' and spec is None:
            s += ['echo ##act', 'advance 6000', 'echo ##settle']
            for i in range(6):
                s += ['framehash', 'advance 100']
            s += ['echo ##end', 'dump state']
            return '; '.join(s)
        if spec is not None:
            sx, sy = self.scroll_for(page, spec.get('scroll'))
            if page in self.pages and (self.pages[page]['size'][0] > VIEW_W or self.pages[page]['size'][1] > VIEW_H) and spec.get('scroll'):
                s.append('scroll %d,%d' % (sx, sy))
            s += spec['setup']
            s += ['echo ##begin2', 'dump state', 'echo ##act'] + spec['act']
        else:
            sx, sy = self.scroll_for(page, rect)
            if page in self.pages and (self.pages[page]['size'][0] > VIEW_W or self.pages[page]['size'][1] > VIEW_H):
                s.append('scroll %d,%d' % (sx, sy))
            s.append('echo ##act')
            if self.kind == 'right':
                s.append('game rclick %s' % name)
            else:
                s.append('clickhs %s' % name)
        s += ['advance 6000', 'echo ##end', 'dump state', 'echo ##late', 'advance 12000', 'dump state']
        return '; '.join(s)

    def one(self, job):
        page, name, rect, spec = job
        what = name if spec is None else spec['what']
        d = os.path.join(self.tmp, '%s_%s' % (page, hashlib.md5((what + str(spec and spec['where'])).encode()).hexdigest()[:8]))
        os.makedirs(d, exist_ok=True)
        rc, out, err = run_script(self.script(page, name, rect, spec), d)
        parts = split_marks(out)
        res = {'page': page if spec is None else spec['where'], 'hot': what, 'failed': [], 'sig': [], 'cls': '', 'note': ''}
        entry_sig, entry_failed, _ = summarise(parts.get('enter', []) + parts.get('begin', []))
        if name == '-' and spec is None:
            res['sig'], res['failed'] = entry_sig, entry_failed
        if rc != 0 or 'end' not in parts:
            res['cls'] = 'crash'
            res['note'] = (err[-1] if err else 'exit %d' % rc)[:160]
            if 'act' in parts:
                res['sig'] = summarise(parts['act'])[0]
            return res
        before = state_of(parts.get('begin2', parts['begin']))
        act = parts['act'] + parts.get('settle', [])
        sig, failed, visible = summarise(act)
        if name == '-' and spec is None:
            self.ambient[page] = [ambient_key(x) for x in summarise(parts['act'])[0]]
        else:                               # what the page does by itself in the same 6 s (time of day, thirst, looping films) is not a reaction
            left = collections.Counter(self.ambient.get(page, []))
            kept = []
            for x in sig:
                k = ambient_key(x)
                if left[k] > 0:
                    left[k] -= 1
                else:
                    kept.append(x)
            sig = kept
            visible = sum(1 for x in sig if is_visible(x))
        after = state_of(parts['end'])
        if name == '-' and spec is None:
            hashes = state_of(parts.get('settle', [])).get('hashes', [])
            running = [l for l in parts.get('enter', []) + parts['begin'] + act if re.match(r'apicall (PlaySpriteAnimation|PlayAVIMovie|BeginPageFilter|JigglePage)', l)]
            res['cls'] = 'page'
            if len(set(hashes)) > 1:
                res['note'] = 'picture changes while idle (%d different frames of %d)%s' % (len(set(hashes)), len(hashes), ', animation running' if running else ', NO animation running')
            res['sig'] = entry_sig + sig
            res['failed'] = entry_failed + failed
            if after.get('page') != page:
                res['note'] = ('left for %s by itself; ' % after.get('page')) + res['note']
            return res
        if spec is not None:                # (the frame events of a looping film depend on when the object was taken into the hand: noise)
            sig = [x for x in sig if not x.startswith(('ev movie', 'ev avi'))]
            visible = sum(1 for x in sig if is_visible(x))
        res['sig'], res['failed'] = sig, failed
        if spec is not None:                # what the hand holds / the body wears afterwards is part of the outcome
            for key, tag in (('held', 'held'), ('worn', 'worn')):
                if after.get(key) != before.get(key):
                    res['sig'].append('%s->%s' % (tag, after.get(key)))
            if spec.get('note'):
                res['note'] = spec['note']
            for l in parts.get('begin', []):
                m = re.match(r'hold (\S+) (-?\d+)', l)
                if m and m.group(2) != '0':
                    res['cls'] = 'off' if m.group(2) == '18' else 'unreached'   # rc 18: a plant's fruit (GameObjects::debugHold), eaten at pick-up, never held
                    res['note'] = 'the object cannot be put in the hand (hold rc=%s%s)' % (m.group(2), ': fruit' if m.group(2) == '18' else '')
                    return res
        if spec is None or spec['click']:
            hit = any(re.match(r'clickhs \S+ at ', l) for l in act) or self.kind == 'right'
        else:
            hit = True
        why = ''
        for l in act:
            m = re.match(r'clickhs \S+ not found \((.*)\)', l)
            if m:
                why = m.group(1)
        late = state_of(parts.get('late', []))
        if not hit:
            res['cls'] = 'off' if why == 'disabled' else 'unreached'
            res['note'] = why
            if spec is not None:            # nothing was clicked: whatever the page did meanwhile (a message about the held object) is not an outcome
                res['sig'] = []
        elif before.get('input', 1) == 1 and late.get('input', 1) == 0 and late.get('page') == before.get('page'):
            res['cls'] = 'stuck'
            res['note'] = 'objects and keys still switched off 18 s after the click, on the same page'
        elif after.get('page') != before.get('page'):
            res['cls'] = 'moved'
            res['note'] = '-> %s' % after.get('page')
        elif visible > 0 or after.get('held') != before.get('held') or after.get('worn') != before.get('worn'):
            res['cls'] = 'react'
        else:
            res['cls'] = 'quiet' if self.kind == 'look' else 'dead'     # (look: a right click acts on a paper in the hand and on the page's feature only)
            if spec is None and not any(l.startswith('event hotspot') for l in act):
                res['note'] = 'the game logic got no hotspot event'
        if spec is not None and spec.get('hold_expect') and hit and after.get('held') != spec['hold_expect']:
            h = after.get('held')
            if h not in (None, '-') or 'status 1' in res['note']:      # (status 6 = plants: a picked fruit is eaten on the spot)
                res['note'] = (res['note'] + '; ' if res['note'] else '') + 'hand holds %s, not the object' % h
        return res


def line_of(r):
    sig = ' | '.join(r['sig'])
    digest = hashlib.md5(sig.encode()).hexdigest()[:10]
    return '\t'.join([r['page'], r['hot'], r['cls'], digest, r['note'], '; '.join(r['failed']), sig[:700]])


def record_of(path):
    d = {}
    if os.path.exists(path):
        for l in open(path, errors='replace'):
            if l.startswith('#') or not l.strip():
                continue
            p = l.rstrip('\n').split('\t')
            d[(p[0], p[1])] = p
    return d


def main():
    a = sys.argv[1:]
    jobs, only, variant, out = max(2, (os.cpu_count() or 4) - 2), None, 'base', os.path.join(ROOT, 'build', 'audit')
    kinds, objects, quiet = 'B,D', None, False
    i = 0
    while i < len(a):
        if a[i] == '--jobs':
            jobs = int(a[i + 1]); i += 2
        elif a[i] == '--pages':
            only = set(x.upper() for x in a[i + 1].split(',')); i += 2
        elif a[i] == '--variant':
            variant = a[i + 1]; i += 2
        elif a[i] == '--out':
            out = a[i + 1]; i += 2
        elif a[i] == '--kinds':
            kinds = a[i + 1]; i += 2
        elif a[i] == '--objects':
            objects = [x for x in a[i + 1].split(',') if x]; i += 2
        elif a[i] == '--quiet':
            quiet = True; i += 1
        else:
            raise SystemExit('unknown argument ' + a[i])
    sw = Sweep(variant, kinds, objects)
    todo = sw.jobs(only)
    pages_job = sorted({j[0] for j in todo})
    with ThreadPoolExecutor(max_workers=jobs) as ex:           # the pages first: their runs give what each page does by itself
        first = [j for j in todo if j[1] == '-' and j[3] is None]
        extra = [(p, '-', None, None) for p in pages_job if p not in {j[0] for j in first}]
        pre = list(ex.map(sw.one, first + extra))
        res = pre if sw.kind in ('base', 'power') else []
        rest = [j for j in todo if not (j[1] == '-' and j[3] is None)]
        done = 0
        for r in ex.map(sw.one, rest):
            res.append(r)
            done += 1
            if not quiet and len(rest) > 3000 and done % 2000 == 0:
                print('  %d / %d runs' % (done, len(rest)), file=sys.stderr, flush=True)
    total = len(res)
    if sw.kind == 'apply':
        # The record holds what an object in the hand changes compared with the empty-handed click, kept short: per hotspot the most
        # common outcome among the objects is the row `HOTSPOT|*` (only if it differs from the empty-handed click, in sweep_base /
        # sweep_power), and every object whose outcome differs from that gets its own row `HOTSPOT|OBJECT`. Crash and stuck always.
        ref = record_of(os.path.join(HERE, 'sweep_%s.tsv' % ('power' if sw.power else 'base')))
        full = os.path.join(out, 'sweep_%s_full.tsv' % variant.replace(':', '_'))
        os.makedirs(out, exist_ok=True)
        with open(full, 'w') as f:                       # every run, for whoever wants to look (not a record)
            for r in sorted(res, key=lambda r: (r['page'], r['hot'].lower())):
                f.write(line_of(r) + '\n')
        groups = collections.defaultdict(list)
        for r in res:
            groups[(r['page'], r['hot'].split('|')[0])].append(r)
        keep = []
        for (page, hot), rs in sorted(groups.items()):
            outcome = lambda r: (r['cls'], hashlib.md5(' | '.join(r['sig']).encode()).hexdigest()[:10])
            modal, n = collections.Counter(outcome(r) for r in rs).most_common(1)[0]
            first = [r for r in rs if outcome(r) == modal][0]
            g = ref.get((page, hot))
            if g is None or (g[2], g[3]) != modal and not (modal[0] in ('off', 'unreached') and g[2] == modal[0]):
                star = dict(first, hot=hot + '|*', note='%d of %d objects%s' % (n, len(rs), '; ' + first['note'] if first['note'] else ''))
                keep.append(star)
            for r in rs:
                if outcome(r) != modal or r['cls'] in ('crash', 'stuck'):
                    keep.append(r)
        res = keep
    res.sort(key=lambda r: (r['page'], r['hot'] != '-', r['hot'].lower()))
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, 'sweep_%s.tsv' % variant.replace(':', '_'))
    with open(path, 'w') as f:
        f.write('# page\thotspot\tclass\tdigest\tnote\tfailed opens\tsignature\n')
        for r in res:
            f.write(line_of(r) + '\n')
    n = {}
    for r in res:
        n[r['cls']] = n.get(r['cls'], 0) + 1
    print('sweep %s: %d interactions%s on %d pages: %s -> %s' % (variant, len(res), (' kept of %d runs' % total) if total != len(res) else '',
                                                                 len({r['page'] for r in res}),
                                                                 ', '.join('%s %d' % kv for kv in sorted(n.items())), os.path.relpath(path, ROOT)))
    subprocess.run(['rm', '-rf', sw.tmp])


if __name__ == '__main__':
    main()
