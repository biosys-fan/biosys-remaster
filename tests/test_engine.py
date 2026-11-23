"""Engine core tests: objects, zones, bar, key handling, events. Driven through `build/biosys --script`.
All expectations are computed here from the original data via build/biosys-objdump and build/biosys-dump
(independent of the engine): placement rule of spec 1.3, drop search of 1.6.4, event order of 1.13."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import projection_oracle as po

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = _DEV
OBJ = D + '/setup-cd/installed/DATA1/Biodata/OBJECTS.DAT'
PAGES = D + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT'
TCH = D + '/game-cd/files/BIODATA/TOUCHINF.DAT'
SAV = D + '/setup-cd/installed/DATA1/NewGame.sav'
PAL = D + '/game-cd/files/BIODATA/PALETTES/RFOREST/RF_DAY.PAL'
T = 'build/test/eng'
os.makedirs(T, exist_ok=True)

failed = 0


def check(name, ok, detail=''):
    global failed
    if ok:
        print('PASS ' + name)
    else:
        failed += 1
        print('FAIL ' + name + (' -- ' + str(detail) if detail else ''))


def sh(args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('%s failed: %s' % (args[:3], r.stderr.strip()[:300]))
    return r.stdout


def run(script, extra=()):
    cmd = ['build/biosys', '--log-events'] + list(extra) + ['--script', script]
    return sh(cmd).splitlines()


# ---------------------------------------------------------------- BMP helpers
def read_bmp(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    stride = (w + 3) & ~3
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    rows = [d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w] for y in range(h)]
    return w, h, pal, rows


_img_cache = {}


def obj_image(name, no):
    key = (name, no)
    if key not in _img_cache:
        p = '%s/o_%s_%d.bmp' % (T, name, no)
        sh(['build/biosys-objdump', 'export', OBJ, name, str(no), p, PAL])
        _img_cache[key] = read_bmp(p)[3]
    return _img_cache[key]


def page_image(page, idx):
    p = '%s/p_%s_%d.bmp' % (T, page, idx)
    sh(['build/biosys-dump', 'export', PAGES, page, str(idx), p, PAL])
    return read_bmp(p)[3]


# ---------------------------------------------------------------- data parsing
def kv(line):
    return dict(re.findall(r'(\w+)=(\[[^\]]*\]|\S+)', line))


def br(s):
    return s[1:-1] if s.startswith('[') else s


pages = {}
cur = None
for line in sh(['build/biosys-dump', 'dump', PAGES]).splitlines():
    p = line.split()
    if p[0] == 'page':
        k = kv(line)
        w, h = [int(v) for v in k['size'].split('x')]
        cur = dict(num=int(p[1]), name=p[2], flags=int(k['flags'], 16), w=w or 640, h=h or 368, orns=[], hots=[])
        pages[p[2].lower()] = cur
    elif p[0] == 'orn':
        k = kv(line)
        x, y = [int(v) for v in k['pos'].split(',')]
        cur['orns'].append(dict(name=p[2], flags=int(k['flags'], 16), x=x, y=y, image=int(k['image']), type=int(k['type'])))
    elif p[0] == 'hot':
        k = kv(line)
        l, t, r, b = [int(v) for v in k['rect'].split(',')]
        cur['hots'].append(dict(idx=int(p[1]), name=p[2], l=l, t=t, r=r, b=b, verb=int(k['verb']),
                                args=[int(v) for v in k['args'].split(',')]))

zones = []
for line in sh(['build/biosys-objdump', 'touchinf', TCH]).splitlines():
    if line.startswith('zone '):
        k = kv(line)
        zones.append(dict(page=br(k['pagename']), hot=br(k['hotspotname']), type=int(k['type'])))

objs = []
for line in sh(['build/biosys-objdump', 'objects', OBJ]).splitlines():
    p = line.split()
    if p[0] == 'obj':
        k = kv(line)
        objs.append(dict(name=br(k['name']), nvar=int(k['nvar']), normal=[None] * 3, hold=[], vars=[], imgs={}, unk18=int(k['unk18'])))
    elif p[0] == 'normal':
        k = kv(line)
        objs[int(p[1])]['normal'][int(p[2])] = (int(k['image']), int(k['flag']), int(k['transp']))
    elif p[0] == 'icon':
        objs[int(p[1])]['icon'] = int(kv(line)['image'])
    elif p[0] == 'hold':
        k = kv(line)
        objs[int(p[1])]['hold'].append(dict(image=int(k['image']), x=int(k['x']), y=int(k['y']), flag=int(k['flag']),
                                            transp=int(k['transp']), group=int(k['group'])))
    elif p[0] == 'wear':
        k = kv(line)
        objs[int(p[1])]['wear'] = dict(image=int(k['image']), x=int(k['x']), y=int(k['y']), flag=int(k['flag']), transp=int(k['transp']))
    elif p[0] == 'misc':
        k = kv(line)
        o = objs[int(p[1])]
        o.update(short=br(k['short']), fruit=int(k['fruit']), wearable=int(k['wearable']), locked=int(k['unk1a8']),
                 initzone=int(k['initzone']), initvar=br(k['initvar']))
    elif p[0] == 'var':
        oi, vi = p[1].split('.')
        objs[int(oi)]['vars'].append(dict(name=br(kv(line)['name']), normal=[None] * 3))
    elif p[0] == 'varnormal':
        oi, vi = p[1].split('.')
        k = kv(line)
        objs[int(oi)]['vars'][int(vi)]['normal'][int(p[2])] = (int(k['image']), int(k['flag']), int(k['transp']))
    elif p[0] == 'img':
        w, h = [int(v) for v in p[3].split('x')]
        objs[int(p[1])]['imgs'][int(p[2])] = (w, h)
by_name = {o['name'].lower(): o for o in objs}

sobjs = []
for line in sh(['build/biosys-objdump', 'save', SAV]).splitlines():
    if line.startswith('sobj '):
        m = re.match(r'sobj (\d+) name=\[(.*?)\] status=(\d+) .* cur=\[(.*?)\],\[(.*?)\] home=\[(.*?)\],\[(.*?)\] group=\S+ variation=\[(.*?)\]', line)
        sobjs.append(dict(i=int(m[1]), name=m[2], status=int(m[3]), page=m[4], hot=m[5], hpage=m[6], hhot=m[7], var=m[8]))
fruit_zone_keys = set((s['hpage'].lower(), s['hhot'].lower()) for s in sobjs
                      if by_name[s['name'].lower()]['fruit'] == 1 and s['hpage'])
occupied0 = set((s['page'].lower(), s['hot'].lower()) for s in sobjs if s['status'] in (1, 6))


def hot_of(page, name):
    for h in pages[page.lower()]['hots']:
        if h['name'].lower() == name.lower():
            return h
    raise KeyError((page, name))


def zone_type(page, name):
    for z in zones:
        if z['page'].lower() == page.lower() and z['hot'].lower() == name.lower():
            return z['type']
    raise KeyError((page, name))


def filter_of(h):
    b = b''.join(struct.pack('<I', v & 0xffffffff) for v in h['args'][4:7])
    return b.split(b'\0')[0].decode('latin1')


def is_pano(pg):
    return pg['flags'] & 1


def rgb_of(transp):
    return ((transp >> 16) & 255, (transp >> 8) & 255, transp & 255)


def key_index(pal, flag, transp):
    if not flag:
        return None
    c = rgb_of(transp)
    return pal.index(c) if c in pal else None


def choose_entry(normal, ztype):
    for i in range(3):
        e = (ztype - 1 + i) % 3
        if normal[e][0]:
            return e
    return None


def placement(zr, w, h):
    """spec 1.3: dest x,y, source x,y, drawn w,h inside zone rect zr = (l,t,r,b) inclusive."""
    zl, zt, zr_, zb = zr
    zw, zh = zr_ - zl + 1, zb - zt + 1
    if w <= zw:
        dx, sx, dw = zl + (zw - w) // 2, 0, w
    else:
        dx, sx, dw = zl, (w - zw) // 2, zw
    if h <= zh:
        dy, sy, dh = zt + zh - h, 0, h
    else:
        dy, sy, dh = zt, h - zh, zh
    return dx, dy, sx, sy, dw, dh


def view_pos(pg, px, py, scx, scy):
    x = (px - scx) % pg['w'] if is_pano(pg) else px - scx
    return x, py - scy + 56


PROJ = po.Projection(75.0)


def view_is_projection(rows, surf, scroll, skip=None):
    """2560 x 1024 pages are shown through the cylindrical projection (exe:0040cf00): rows 56..423 of a screenshot equal the oracle's render of the page surface."""
    exp = PROJ.render(surf, scroll[0], scroll[1])
    for y in range(368):
        if rows[56 + y] == exp[y * 640:(y + 1) * 640]:
            continue
        for x in range(640):   # skip = (x0, y0, x1, y1) in screen coordinates: layers drawn after the projection (held object image)
            if rows[56 + y][x] != exp[y * 640 + x] and not (skip and skip[0] <= x < skip[2] and skip[1] <= 56 + y < skip[3]):
                return False
    return True


def at_page(pg, px, py, scroll, verb='click'):
    """script command for the screen position that shows page point (px, py): flat pages by arithmetic, panoramas through the engine's inverse"""
    if is_pano(pg):
        return '%sp %d,%d' % (verb, px, py)
    x, y = view_pos(pg, px, py, scroll[0], scroll[1])
    return '%s %d,%d' % (verb, x, y)


def scroll_for(pg, cx, cy, viewx=None):
    """scroll that brings page point (cx,cy) to view x=viewx (default centre), y centre."""
    if not is_pano(pg):
        return 0, 0
    sx = (cx - (320 if viewx is None else viewx)) % pg['w']
    sy = min(max(cy - 184, 0), pg['h'] - 368)
    return sx, sy


def check_drawn(shot, pg, oname, image_no, flag, transp, zr, scroll, label, surf=None):
    """Does the screenshot show image `image_no` of the object placed in zone rect zr (spec 1.3)?"""
    w, h, pal, rows = read_bmp(shot)
    img = obj_image(oname, image_no)
    iw, ih = len(img[0]), len(img)
    key = key_index(pal, flag, transp)
    dx, dy, sx, sy, dw, dh = placement(zr, iw, ih)
    total = bad = nontrans = 0
    for iy in range(sy, sy + dh):
        for ix in range(sx, sx + dw):
            v = img[iy][ix]
            if key is not None and v == key:
                continue
            nontrans += 1
            if is_pano(pg):
                # the object is baked into the page surface (compared there); the view must be the projection of that surface
                total += 1
                bad += surf[(dy + iy - sy) * 2560 + dx + ix - sx] != v
                continue
            X, Y = view_pos(pg, dx + ix - sx, dy + iy - sy, scroll[0], scroll[1])
            if 0 <= X < 640 and 56 <= Y < 424:
                total += 1
                if rows[Y][X] != v:
                    bad += 1
    if is_pano(pg) and not view_is_projection(rows, surf, scroll):
        bad += 1
    ok = total > 0 and bad == 0 and total >= nontrans // 2
    check(label, ok, 'compared %d of %d opaque pixels, %d wrong' % (total, nontrans, bad))
    return ok


def object_rect_view(pg, oname, entry_img, zr, scroll):
    img = obj_image(oname, entry_img)
    dx, dy, sx, sy, dw, dh = placement(zr, len(img[0]), len(img))
    X, Y = view_pos(pg, dx, dy, scroll[0], scroll[1])
    return dx, dy, dw, dh, X, Y


def events(lines, start, end=None):
    """gameobject events between echo markers #start and #end: list of (name, code, extra)."""
    out, on = [], False
    for l in lines:
        if l == '#' + start:
            on = True
        elif end and l == '#' + end:
            break
        elif on and l.startswith('event gameobject '):
            p = l.split()
            out.append((p[2], int(p[3]), p[4] if len(p) > 4 else ''))
    return out


def section(lines, start, end=None):
    out, on = [], False
    for l in lines:
        if l == '#' + start:
            on = True
        elif l.startswith('#') and on and (end is None or l == '#' + end):
            break
        elif on:
            out.append(l)
    return out


def objline(lines, name):
    for l in lines:
        p = l.split()
        if len(p) > 2 and p[0] == 'obj' and p[2].lower() == name.lower():
            return dict(kv(l), _=l)
    return None


# ---------------------------------------------------------------- candidates
def candidate(s, allow_var=False):
    o = by_name[s['name'].lower()]
    if s['status'] != 1 or o['fruit'] or (o['nvar'] and not allow_var) or o['locked'] == 1:
        return None
    if s['page'].lower() not in pages:
        return None
    pg = pages[s['page'].lower()]
    if pg['w'] > 640 and not is_pano(pg) or pg['h'] not in (368, 1024):
        return None
    try:
        h = hot_of(s['page'], s['hot'])
    except KeyError:
        return None
    # a zone filter only changes the picture when the object has a short name (spec 1.4): keep those out
    if (filter_of(h) and (o.get('short') or allow_var)) or h['args'][1] & 1 or h['verb'] != 13:
        return None
    zt = zone_type(s['page'], s['hot'])
    normal = o['normal']
    if allow_var:
        v = next((v for v in o['vars'] if v['name'].lower() == s['var'].lower()), None)
        if v is None:
            return None
        normal = v['normal']
    e = choose_entry(normal, zt)
    if e is None:
        return None
    img_no, flag, transp = normal[e]
    w, hh = o['imgs'][img_no]
    zr = (h['l'], h['t'], h['r'], h['b'])
    return dict(s=s, o=o, pg=pg, hot=h, zr=zr, zt=zt, img=img_no, flag=flag, transp=transp, w=w, h=hh,
                crop=(w > zr[2] - zr[0] + 1 or hh > zr[3] - zr[1] + 1))


cands = [c for c in (candidate(s) for s in sobjs) if c]
print('candidates:', len(cands))


def pick_cands():
    pano = [c for c in cands if is_pano(c['pg'])]
    flat = [c for c in cands if not is_pano(c['pg'])]
    crop = [c for c in cands if c['crop']]
    chosen = [pano[0], flat[0]]
    third = next((c for c in crop if c not in chosen), None) or next(c for c in pano if c['pg'] is not pano[0]['pg'])
    chosen.append(third)
    return chosen


three = pick_cands()
print('chosen:', ', '.join('%s@%s/%s%s' % (c['o']['name'], c['pg']['name'], c['hot']['name'], ' (crop)' if c['crop'] else '') for c in three))


def obj_center_page(c):
    dx, dy, sx, sy, dw, dh = placement(c['zr'], c['w'], c['h'])
    return dx + dw // 2, dy + dh // 2


def click_view(c, scroll, verb='click'):
    cx, cy = obj_center_page(c)
    return at_page(c['pg'], cx, cy, scroll, verb)


def scroll_obj(c, viewx=None):
    cx, cy = obj_center_page(c)
    return scroll_for(c['pg'], cx, cy, viewx)


# ================================================================= (a) new game
lines = run('dump objects')
counts = {int(l.split()[1]): int(l.split()[2]) for l in lines if l.startswith('statuscount')}
check('(a) new game status counts 160 / 206 / 43', counts.get(1) == 160 and counts.get(6) == 206 and counts.get(4) == 43
      and counts.get(2) == 0 and counts.get(3) == 0 and counts.get(5) == 0, counts)
check('(a) every object line matches NewGame.sav status',
      all((objline(lines, s['name']) or {}).get('status') == str(s['status']) for s in sobjs))

for n, c in enumerate(three):
    scroll = scroll_obj(c)
    shot = '%s/a%d.bmp' % (T, n)
    run('page %s; scroll %d,%d; surfdump %s/a%d.raw; shot %s' % (c['pg']['name'], scroll[0], scroll[1], T, n, shot))
    surf_a = open('%s/a%d.raw' % (T, n), 'rb').read() if is_pano(c['pg']) else None
    check_drawn(shot, c['pg'], c['o']['name'], c['img'], c['flag'], c['transp'], c['zr'], scroll,
               '(a) %s lies in %s/%s: normal image %d at the placement rule position%s' %
               (c['o']['name'], c['pg']['name'], c['hot']['name'], c['img'], ' (cropped)' if c['crop'] else ''), surf_a)

# ================================================================= (b) pick up, (c) stash
c0 = three[0]
sc = scroll_obj(c0, 120)
base = '%s/b_base.bmp' % T
sh(['build/biosys', '--no-objects', '--page', c0['pg']['name'], '--scroll', '%d,%d' % sc, '--screenshot', base])
sh(['build/biosys', '--no-objects', '--script', 'page %s; scroll %d,%d; surfdump %s/b_base.raw' % (c0['pg']['name'], sc[0], sc[1], T)])
cc = click_view(c0, sc)
script = ('page %s; scroll %d,%d; echo #before; dump state; echo #e1; %s; echo #picked; shot %s/b1.bmp; surfdump %s/b1.raw; dump objects; dump state; '
          'echo #held; move 320,450; key S; echo #stashed; shot %s/c1.bmp; dump objects; dump bar; dump state; echo #end') % (
    c0['pg']['name'], sc[0], sc[1], cc, T, T, T)
out = run(script)
ev = events(out, 'e1', 'picked')
check('(b) pick-up events 0x0a then 0x01', ev == [(c0['o']['name'], 10, ''), (c0['o']['name'], 1, '')], ev)
ol = objline(section(out, 'picked', 'held'), c0['o']['name'])
check('(b) object status 2 (held)', ol and ol['status'] == '2', ol)
st = dict(l.split(' ', 1) for l in section(out, 'picked', 'held') if l.startswith('held '))
check('(b) state: held object is %s' % c0['o']['name'], st.get('held') == c0['o']['name'], st)
w, h, pal, rows = read_bmp(T + '/b1.bmp')
# held image: view origin + (x,y) of its first holding entry
o = c0['o']
gmin = min(hd['group'] for hd in o['hold'] if hd['image'])
hd = next(hd for hd in o['hold'] if hd['image'] and hd['group'] == gmin)
himg = obj_image(o['name'], hd['image'])
hkey = key_index(pal, hd['flag'], hd['transp'])
tot = bad = 0
for iy in range(len(himg)):
    for ix in range(len(himg[0])):
        v = himg[iy][ix]
        if hkey is not None and v == hkey:
            continue
        X, Y = hd['x'] + ix, 56 + hd['y'] + iy
        if 0 <= X < 640 and 56 <= Y < 424:
            tot += 1
            bad += rows[Y][X] != v
check('(b) held image drawn at view origin + (%d,%d)' % (hd['x'], hd['y']), tot > 100 and bad == 0, (tot, bad))
# erased: the object's rect equals the page without any object (outside the held image)
bw, bh, bpal, brows = read_bmp(base)
dx, dy, dw, dh, X0, Y0 = object_rect_view(c0['pg'], o['name'], c0['img'], c0['zr'], sc)
cmp_n = mis = 0
if is_pano(c0['pg']):   # baked into the page surface: compare the surface, and the view must be its projection
    sb0 = open(T + '/b_base.raw', 'rb').read()
    sb1 = open(T + '/b1.raw', 'rb').read()
    for yy in range(dy, dy + dh):
        for xx in range(dx, dx + dw):
            cmp_n += 1
            mis += sb1[yy * 2560 + xx] != sb0[yy * 2560 + xx]
    mis += not view_is_projection(rows, sb1, sc, (hd['x'], 56 + hd['y'], hd['x'] + len(himg[0]), 56 + hd['y'] + len(himg)))
    Y0 = 10 ** 6   # skip the flat loop below
for yy in range(Y0, Y0 + dh):
    for xx in range(X0, X0 + dw):
        if not (0 <= xx < 640 and 56 <= yy < 424):
            continue
        if hd['x'] <= xx < hd['x'] + len(himg[0]) and 56 + hd['y'] <= yy < 56 + hd['y'] + len(himg):
            continue
        cmp_n += 1
        mis += rows[yy][xx] != brows[yy][xx]
check('(b) object erased from the page (pixels equal the page without it)', cmp_n > 50 and mis == 0, (cmp_n, mis))
ol = objline(section(out, 'picked', 'held'), c0['o']['name'])

sec = section(out, 'stashed', 'end')
ev = events(out, 'held', 'stashed')
check('(c) S stashes: events 0x0b then 0x02', ev == [(o['name'], 11, ''), (o['name'], 2, '')], ev)
check('(c) status 3 and slot 0 of the bar', (objline(sec, o['name']) or {}).get('status') == '3' and
      any(l.startswith('slot 0 %s ' % o['name']) for l in sec), sec[:3])
w, h, pal, rows = read_bmp(T + '/c1.bmp')
icon = obj_image(o['name'], o['icon'])
okp = all(rows[424 + y][285 + x] == icon[y][x] for y in range(56) for x in range(70))
check('(c) bar icon drawn at x=285 (slot 0 centred), y=424', okp)
check('(c) held cleared after stash', 'held -' in sec)

# ================================================================= (d) swap, (e) drop
c1 = three[1]
sc1 = scroll_obj(c0, 160)
sc2 = scroll_obj(c1, 160)
p1 = click_view(c0, sc1)
p2 = click_view(c1, sc2)
script = ('page %s; scroll %d,%d; %s; page %s; scroll %d,%d; echo #swap; %s; echo #swapped; dump objects; dump bar; dump state; '
          'echo #drop; key D; echo #dropped; dump objects; dump state; scroll %d,%d; shot %s/e1.bmp; echo #end') % (
    c0['pg']['name'], sc1[0], sc1[1], p1, c1['pg']['name'], sc2[0], sc2[1], p2, 0, 0, T)
# find where the drop will go: nearest free matching zone of c1's page from the view at sc2 (spec 1.6.4)
pg1 = c1['pg']
freed = {(c0['pg']['name'].lower(), c0['hot']['name'].lower())}   # c0's zone is on another page anyway


def expected_drop_zone(pg, name, scroll, occupied):
    best, bestd = None, None
    for h in pg['hots']:
        if h['verb'] != 13 or h['args'][1] & 1:
            continue
        key = (pg['name'].lower(), h['name'].lower())
        if key in occupied or key in fruit_zone_keys:
            continue
        f = filter_of(h)
        if f:
            if f.endswith('*'):
                if not name.lower().startswith(f[:-1].lower()):
                    continue
            elif f.lower() != name.lower():
                continue
        cx, cy = (h['l'] + h['r'] + 1) // 2, (h['t'] + h['b'] + 1) // 2
        dx = cx - (scroll[0] + 320)
        if is_pano(pg):
            dx = (dx + pg['w'] // 2) % pg['w'] - pg['w'] // 2
        dy = cy - (scroll[1] + 184)
        d = dx * dx + dy * dy
        if bestd is None or d < bestd:
            best, bestd = h, d
    return best


occ = set(occupied0)
occ.discard((c1['pg']['name'].lower(), c1['hot']['name'].lower()))
occ.discard((c0['pg']['name'].lower(), c0['hot']['name'].lower()))
# drop with a scroll offset so that a different zone may be nearest than the old one
exp_hot = expected_drop_zone(pg1, c1['o']['name'], (sc2[0], sc2[1]), occ)
out = run(script)
ev = events(out, 'swap', 'swapped')
n0, n1 = c0['o']['name'], c1['o']['name']
check('(d) swap: held object stashed (0x0b,0x02) then second picked up (0x0a,0x01)',
      ev == [(n0, 11, ''), (n0, 2, ''), (n1, 10, ''), (n1, 1, '')], ev)
sec = section(out, 'swapped', 'drop')
check('(d) first object on the bar, second held',
      (objline(sec, n0) or {}).get('status') == '3' and (objline(sec, n1) or {}).get('status') == '2' and
      any(l.startswith('slot 0 %s ' % n0) for l in sec) and 'held ' + n1 in sec, sec[:4])
ev = events(out, 'drop', 'dropped')
check('(e) D drops into the nearest free matching zone (%s): events 0x0d, 0x06' % (exp_hot['name'] if exp_hot else None),
      exp_hot is not None and ev == [(n1, 13, ''), (n1, 6, exp_hot['name'])], ev)
sec = section(out, 'dropped', 'end')
ol = objline(sec, n1)
check('(e) dropped object lies in %s/%s with status 1' % (pg1['name'], exp_hot['name'] if exp_hot else '?'),
      ol and ol['status'] == '1' and ol.get('zone', '').lower() == (pg1['name'] + '/' + exp_hot['name']).lower(), ol and ol['_'])
if exp_hot:
    zt = zone_type(pg1['name'], exp_hot['name'])
    e = choose_entry(by_name[n1.lower()]['normal'], zt)
    img_no, flag, transp = by_name[n1.lower()]['normal'][e]
    zr = (exp_hot['l'], exp_hot['t'], exp_hot['r'], exp_hot['b'])
    scd = scroll_for(pg1, (zr[0] + zr[2]) // 2, (zr[1] + zr[3]) // 2)
    # redo the whole sequence ending in a screenshot scrolled to the drop zone
    script2 = script.replace('scroll 0,0; shot %s/e1.bmp' % T, 'scroll %d,%d; surfdump %s/e1.raw; shot %s/e1.bmp' % (scd[0], scd[1], T, T))
    run(script2)
    check_drawn('%s/e1.bmp' % T, pg1, n1, img_no, flag, transp, zr, scd, '(e) dropped object is drawn in its new zone',
                open(T + '/e1.raw', 'rb').read() if is_pano(pg1) else None)

# drop with no candidate zone -> event 0x10; find a normal page without drop zones
nozone = next(p for p in pages.values() if p['h'] == 368 and p['w'] == 640 and not any(h['verb'] == 13 for h in p['hots'])
              and p['name'].lower() != 'topbar' and not p['flags'] & 2)
out = run('page %s; scroll %d,%d; %s; page %s; echo #d; key D; echo #end; dump state' % (
    c0['pg']['name'], sc[0], sc[1], cc, nozone['name']))
ev = events(out, 'd', 'end')
check('(e) D with no candidate zone fires 0x10 only', ev == [(c0['o']['name'], 16, '')], ev)
check('(e) object still held after the failed drop', 'held ' + c0['o']['name'] in out)

# ================================================================= (f) wearing goggles
g = by_name['goggles']
gs = next(s for s in sobjs if s['name'] == 'goggles')
gpg = pages[gs['page'].lower()]
gh = hot_of(gs['page'], gs['hot'])
zr = (gh['l'], gh['t'], gh['r'], gh['b'])
gt = zone_type(gs['page'], gs['hot'])
ge = choose_entry(g['normal'], gt)
gimg = g['normal'][ge][0]
gw, gh_ = g['imgs'][gimg]
gc = dict(pg=gpg, zr=zr, w=gw, h=gh_)
gsc = scroll_for(gpg, *obj_center_page(gc), 120)
gcmd = at_page(gpg, *obj_center_page(gc), gsc)
script = ('page %s; scroll %d,%d; echo #p; %s; echo #held; shot %s/f0.bmp; advance 260; shot %s/f0b.bmp; advance 260; shot %s/f0c.bmp; '
          'echo #wear; key SPACE; echo #worn; dump objects; dump bar; dump state; shot %s/f1.bmp; '
          'echo #t; move 320,452; click 320,452; echo #off; dump objects; dump state; echo #end') % (
    gpg['name'], gsc[0], gsc[1], gcmd, T, T, T, T)
out = run(script)
n = 'goggles'
check('(f) pick-up goggles', events(out, 'p', 'held') == [(n, 10, ''), (n, 1, '')], events(out, 'p', 'held'))
ev = events(out, 'wear', 'worn')
check('(f) Space wears goggles: events 0x0e then 0x08', ev == [(n, 14, ''), (n, 8, '')], ev)
sec = section(out, 'worn', 'off')
check('(f) status 5, on the bar, listed as worn', (objline(sec, n) or {}).get('status') == '5' and
      any(l.startswith('slot 0 goggles ') for l in sec) and 'worn goggles' in sec and 'held -' in sec, sec[:4])
w, h, pal, rows = read_bmp(T + '/f1.bmp')
wr = g['wear']
wimg = obj_image(n, wr['image'])
wkey = key_index(pal, wr['flag'], wr['transp'])
tot = bad = 0
for iy in range(len(wimg)):
    for ix in range(len(wimg[0])):
        v = wimg[iy][ix]
        if wkey is not None and v == wkey:
            continue
        X, Y = wr['x'] + ix, 56 + wr['y'] + iy
        if 0 <= X < 640 and 56 <= Y < 424:
            tot += 1
            bad += rows[Y][X] != v
check('(f) wearing overlay (640x368 at the view origin) drawn', tot > 1000 and bad == 0, (tot, bad))
ev = events(out, 't', 'off')
check('(f) clicking the worn bar icon takes it off: events 0x0f then 0x09', ev == [(n, 15, ''), (n, 9, '')], ev)
sec = section(out, 'off', 'end')
check('(f) after take-off: status 3, nothing worn', (objline(sec, n) or {}).get('status') == '3' and 'worn -' in sec, sec[:3])

# holding-image frame animation with the injected clock (spec 1.6.1): 250 ms delay, images 10,11,12,11
seq = [hd['image'] for hd in g['hold'] if hd['image']]


def held_matches(shot, no):
    w, h, pal, rows = read_bmp(shot)
    hd = next(x for x in g['hold'] if x['image'] == no)
    im = obj_image(n, no)
    key = key_index(pal, hd['flag'], hd['transp'])
    for iy in range(len(im)):
        for ix in range(len(im[0])):
            v = im[iy][ix]
            if key is not None and v == key:
                continue
            X, Y = hd['x'] + ix, 56 + hd['y'] + iy
            if 0 <= X < 640 and 56 <= Y < 424 and rows[Y][X] != v:
                return False
    return True


check('(f) holding animation: frame 0, then after 260 ms and 520 ms the next frames',
      held_matches(T + '/f0.bmp', seq[0]) and held_matches(T + '/f0b.bmp', seq[1]) and held_matches(T + '/f0c.bmp', seq[2]),
      seq)

# ================================================================= (g) variations
vc = None
for s in sobjs:
    c = candidate(s, allow_var=True)
    if not c or len(c['o']['vars']) < 2:
        continue
    cur_v = next(v for v in c['o']['vars'] if v['name'].lower() == s['var'].lower())
    for nv in c['o']['vars']:
        if nv is cur_v:
            continue
        e = choose_entry(nv['normal'], c['zt'])
        if e is not None and nv['normal'][e][0] != c['img']:
            vc = (c, nv, nv['normal'][e])
            break
    if vc:
        break
c, nv, ne = vc
vo = c['o']
vsc = scroll_obj(c)
sh(['build/biosys', '--no-objects', '--page', c['pg']['name'], '--scroll', '%d,%d' % vsc, '--screenshot', T + '/g_base.bmp'])
out = run('page %s; scroll %d,%d; surfdump %s/g0.raw; shot %s/g0.bmp; echo #v; var %s %s; echo #w; surfdump %s/g1.raw; shot %s/g1.bmp; dump objects; echo #end' % (
    c['pg']['name'], vsc[0], vsc[1], T, T, vo['name'], nv['name'], T, T))
g0s = open(T + '/g0.raw', 'rb').read() if is_pano(c['pg']) else None
g1s = open(T + '/g1.raw', 'rb').read() if is_pano(c['pg']) else None
check('(g) variation of %s changed to %s (API result 0)' % (vo['name'], nv['name']), 'api 0' in section(out, 'v', 'w'))
ol = objline(section(out, 'w', 'end'), vo['name'])
check('(g) object state shows the new variation and image', ol and ol.get('var', '').lower() == nv['name'].lower() and
      ol.get('image') == str(ne[0]), ol and ol['_'])
check_drawn(T + '/g0.bmp', c['pg'], vo['name'], c['img'], c['flag'], c['transp'], c['zr'], vsc, '(g) old variation image drawn before the change', g0s)
check_drawn(T + '/g1.bmp', c['pg'], vo['name'], ne[0], ne[1], ne[2], c['zr'], vsc, '(g) new variation image drawn after the change (page redrawn)', g1s)
w, h, pal, rows = read_bmp(T + '/g1.bmp')
bw, bh, bpal, brows = read_bmp(T + '/g_base.bmp')
nimg = obj_image(vo['name'], ne[0])
dx, dy, sx_, sy_, dw, dh = placement(c['zr'], len(nimg[0]), len(nimg))
odx, ody, osx, osy, odw, odh = placement(c['zr'], c['w'], c['h'])
cmp_n = mis = 0
if is_pano(c['pg']):
    sh(['build/biosys', '--no-objects', '--script', 'page %s; scroll %d,%d; surfdump %s/g_base.raw' % (c['pg']['name'], vsc[0], vsc[1], T)])
    gbs = open(T + '/g_base.raw', 'rb').read()
for yy in range(ody, ody + odh):
    for xx in range(odx, odx + odw):
        if dx <= xx < dx + dw and dy <= yy < dy + dh:
            continue
        X, Y = view_pos(c['pg'], xx, yy, vsc[0], vsc[1])
        if is_pano(c['pg']):
            cmp_n += 1
            mis += g1s[yy * 2560 + xx] != gbs[yy * 2560 + xx]
            continue
        if 0 <= X < 640 and 56 <= Y < 424:
            cmp_n += 1
            mis += rows[Y][X] != brows[Y][X]
check('(g) area of the old image outside the new one is restored to the page', mis == 0, (cmp_n, mis))

# ================================================================= (h) fruit
fr = None
for s in sobjs:
    o = by_name[s['name'].lower()]
    if s['status'] != 6 or s['page'].lower() not in pages:
        continue
    pg = pages[s['page'].lower()]
    if not is_pano(pg):
        continue
    h = hot_of(s['page'], s['hot'])
    for orn in pg['orns']:
        if orn['flags'] & 4 and orn['type'] == 1:
            im = page_image(pg['name'], orn['image'])
            ocx, ocy = orn['x'] + len(im[0]) // 2, orn['y'] + len(im) // 2
            if h['l'] <= ocx <= h['r'] and h['t'] <= ocy <= h['b']:
                fr = (s, o, pg, h, orn, im)
                break
    if fr:
        break
check('(h) found a fruit object with a fruit ornament inside its zone', fr is not None)
s, o, pg, h, orn, im = fr
fsc = scroll_for(pg, (h['l'] + h['r']) // 2, (h['t'] + h['b']) // 2)
fcmd = at_page(pg, (h['l'] + h['r']) // 2, (h['t'] + h['b']) // 2, fsc)
mapping = '%s=%s' % (orn['name'], o['name'])
sh(['build/biosys', '--no-objects', '--page', pg['name'], '--scroll', '%d,%d' % fsc, '--screenshot', T + '/h_base.bmp'])
sh(['build/biosys', '--no-objects', '--script', 'page %s; scroll %d,%d; surfdump %s/h_base.raw' % (pg['name'], fsc[0], fsc[1], T)])
out = run('page %s; scroll %d,%d; surfdump %s/h0.raw; shot %s/h0.bmp; dump zones; echo #c; %s; echo #d; surfdump %s/h1.raw; shot %s/h1.bmp; dump objects; dump bar; echo #end' % (
    pg['name'], fsc[0], fsc[1], T, T, fcmd, T, T), ['--test-fruit', mapping])
sh0 = open(T + '/h0.raw', 'rb').read()
sh1 = open(T + '/h1.raw', 'rb').read()
shb = open(T + '/h_base.raw', 'rb').read()
w, h_, pal, rows = read_bmp(T + '/h0.bmp')
key = pal.index((0, 255, 255)) if orn['flags'] & 0x10 else None
tot = bad = 0
for iy in range(len(im)):
    for ix in range(len(im[0])):
        v = im[iy][ix]
        if key is not None and v == key:
            continue
        X, Y = view_pos(pg, orn['x'] + ix, orn['y'] + iy, *fsc)
        if is_pano(pg):   # painted into the page surface; the view is its projection
            tot += 1
            bad += sh0[(orn['y'] + iy) * 2560 + orn['x'] + ix] != v
            continue
        if 0 <= X < 640 and 56 <= Y < 424:
            tot += 1
            bad += rows[Y][X] != v
if is_pano(pg):
    bad += not view_is_projection(rows, sh0, fsc)
check('(h) fruit ornament %s painted over the page (event 29 -> %s)' % (orn['name'], o['name']), tot > 20 and bad == 0, (tot, bad))
n = o['name']
ev = events(out, 'c', 'd')
check('(h) clicking the fruit: events 0x0a, 0x01, 0x0b, 0x02', ev == [(n, 10, ''), (n, 1, ''), (n, 11, ''), (n, 2, '')], ev)
sec = section(out, 'd', 'end')
check('(h) fruit on the bar with status 3', (objline(sec, n) or {}).get('status') == '3' and any(l.startswith('slot 0 %s ' % n) for l in sec), sec[:3])
w, h_, pal, rows = read_bmp(T + '/h1.bmp')
bw, bh, bpal, brows = read_bmp(T + '/h_base.bmp')
mis = cmpn = 0
for iy in range(len(im)):
    for ix in range(len(im[0])):
        X, Y = view_pos(pg, orn['x'] + ix, orn['y'] + iy, *fsc)
        if is_pano(pg):
            cmpn += 1
            mis += sh1[(orn['y'] + iy) * 2560 + orn['x'] + ix] != shb[(orn['y'] + iy) * 2560 + orn['x'] + ix]
            continue
        if 0 <= X < 640 and 56 <= Y < 424:
            cmpn += 1
            mis += rows[Y][X] != brows[Y][X]
check('(h) page background restored under the taken fruit', cmpn > 20 and mis == 0, (cmpn, mis))
icon = obj_image(n, o['icon'])
check('(h) bar flashes visible after the pick-up', all(rows[424 + y][285 + x] == icon[y][x] for y in range(56) for x in range(70)))

# ================================================================= stacking and cursors
by_key = {}
for c in [c for c in (candidate(s, allow_var=True) for s in sobjs) if c]:
    if c['o']['unk18'] != -1:
        by_key.setdefault(c['o']['unk18'], []).append(c)
pair = next((v[:2] for v in by_key.values() if len(v) >= 2), None)
if pair:
    a, b = pair
    sa, sb = scroll_obj(a, 160), scroll_obj(b, 160)
    pa, pb = click_view(a, sa), click_view(b, sb)
    out = run('page %s; scroll %d,%d; %s; page %s; scroll %d,%d; %s; move 320,450; key S; dump bar; shot %s/k1.bmp' % (
        a['pg']['name'], sa[0], sa[1], pa, b['pg']['name'], sb[0], sb[1], pb, T))
    barl = [l for l in out if l.startswith('bar ') or l.startswith('slot ')]
    check('(k) objects with the same stacking key share one bar slot with count 2',
          barl and 'objects=2 slots=1' in barl[0] and any('count=2' in l for l in barl[1:]), barl)
    w, h, pal, rows = read_bmp(T + '/k1.bmp')
    ink = sum(1 for y in range(7) for x in range(285 + 70 - 12, 285 + 70) if rows[424 + y][x] != obj_image(a['o']['name'], a['o']['icon'])[y][x - 285])
    check('(k) stack count digits drawn in the slot corner', ink > 5, ink)
else:
    check('(k) stacking pair available', False)

mv0 = click_view(c0, sc, 'move')
out = run('page %s; scroll %d,%d; %s; dump state; echo #a; move 5,300; dump state; echo #b; %s; %s; dump state; move 320,450; dump state' % (
    c0['pg']['name'], sc[0], sc[1], mv0, cc, mv0))
curs = [l for l in out if l.startswith('cursor ')]
check('(j) cursor over an occupied object zone is OBJECT (0x19); over the bar STATUS (0x1a); picked-up zone no longer active',
      len(curs) == 4 and curs[0].split()[1] == '25' and curs[3].split()[1] == '26' and curs[2].split()[1] != '25', curs)

# ================================================================= scrolling (spec 5.2)
out = run('page R4; scroll 1000,308; capture on; move 330,240; advance 400; dump state; capture off; key SHIFT; dump state')
st = [l for l in out if l.startswith('page R4 scroll')]
check('(m) captured pointer: delta 10 is doubled and decays (20+10+5+2+1): scroll 1000 -> 1038', st and st[0] == 'page R4 scroll 1038,308', st)
out = run('page R4; scroll 2550,308; capture on; move 330,240; advance 400; dump state')
st = [l for l in out if l.startswith('page R4 scroll')]
check('(m) panorama scroll wraps around the page width', st and st[0] == 'page R4 scroll 28,308', st)
out = run('page R4; capture on; key SHIFT; dump state; key SHIFT; dump state')
modes = [l for l in out if l.startswith('scrollmode')]
check('(m) Shift toggles the bar mode', modes == ['scrollmode 1 barmode 1', 'scrollmode 1 barmode 0'], modes)

print('engine tests:', 'FAILED %d' % failed if failed else 'all passed')
sys.exit(1 if failed else 0)
