"""Helper of the playthrough tests: builds scripts of *real input* (move / click / key / type) for `build/biosys --game --script`,
from the page file's hotspot rectangles (build/biosys-dump), and parses the output of the run."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGES = (_DEV + '/setup-cd/installed/DATA1/Biodata/PAGES.DAT')
VIEW_W, VIEW_H, VIEW_TOP = 640, 368, 56      # the main window's view inside the 640x480 screen


def load_pages():
    """{PAGE: {'size': (w, h), 'hot': [(name, (l, t, r, b), flags, button, verb)]}}"""
    r = subprocess.run([ROOT + '/build/biosys-dump', 'dump', PAGES], capture_output=True, text=True, cwd=ROOT)
    pages, cur = {}, None
    for l in r.stdout.splitlines():
        m = re.match(r'page \d+ (\S+) flags=\S+ size=(\d+)x(\d+)', l)
        if m:
            cur = m.group(1).upper()
            pages[cur] = {'size': (int(m.group(2)), int(m.group(3))), 'hot': []}
            continue
        m = re.match(r'hot \d+ (\S+) rect=(\d+),(\d+),(\d+),(\d+) flags=(\S+) button=(\d+) verb=(\d+)', l)
        if m and cur:
            pages[cur]['hot'].append((m.group(1), tuple(int(m.group(i)) for i in range(2, 6)), int(m.group(6), 16),
                                      int(m.group(7)), int(m.group(8))))
    return pages


class Play:
    """A growing script of real input. `page` tracks the page the script has led to (for the hotspot rectangles)."""

    def __init__(self, start='R4'):
        self.pages = load_pages()
        self.cmds = []
        self.page = start
        self.marks = []                   # (index in cmds, label): `mark` commands are printed as `echo mark LABEL`

    def raw(self, *c):
        self.cmds.extend(c)

    def advance(self, ms):
        self.raw('advance %d' % ms)

    def mark(self, label):
        self.raw('echo mark ' + label)

    def shot(self, path):
        self.raw('shot ' + path)

    def hotspot_point(self, page, name, index=0):
        """scroll position and screen point that hit the hotspot (nth hotspot of that name) of the page"""
        p = self.pages[page.upper()]
        hs = [h for h in p['hot'] if h[0].lower() == name.lower()]
        if len(hs) <= index:
            raise KeyError('no hotspot %s on %s' % (name, page))
        l, t, r, b = hs[index][1]
        cx, cy = (l + r) // 2, (t + b) // 2
        w, h = p['size']
        sx = max(0, min(max(0, w - VIEW_W), cx - VIEW_W // 2)) if w > VIEW_W else 0
        sy = max(0, min(max(0, h - VIEW_H), cy - VIEW_H // 2)) if h > VIEW_H else 0
        return (sx, sy), (cx - sx, cy - sy + VIEW_TOP)

    def point_cmd(self, page, name, index=0, dy=0, verb='click'):
        """(scroll, script command) that moves / clicks the hotspot point: 2560 x 1024 pages are shown through the cylindrical projection
        (exe:0040cf00), so the engine's `clickp` / `movep` find the screen point that shows the page point"""
        (sx, sy), (x, y) = self.hotspot_point(page, name, index)
        if self.pages[page.upper()]['size'] == (2560, 1024):
            return (sx, sy), '%sp %d,%d' % (verb, x + sx, y - VIEW_TOP + sy + dy)
        return (sx, sy), '%s %d,%d' % (verb, x, y + dy)

    def click_hotspot(self, name, page=None, index=0, wait=300, rclick=False, dy=0):
        (sx, sy), cmd = self.point_cmd(page or self.page, name, index, dy, 'click')
        if rclick:
            cmd = 'r' + cmd if cmd.startswith('click') else cmd
        self.raw('scroll %d,%d' % (sx, sy), cmd)
        if wait:
            self.advance(wait)

    def goto(self, name, dest, wait=9000):
        """click the path hotspot `name` of the current page and wait for the arrival"""
        self.click_hotspot(name, wait=wait)
        self.page = dest

    def key(self, k, wait=100):
        self.raw('key ' + k)
        if wait:
            self.advance(wait)

    def type(self, text, wait=100):
        self.raw('type ' + text)
        if wait:
            self.advance(wait)

    def run(self, seed=1, extra=(), timeout=600, game_args=()):
        args = [ROOT + '/build/biosys', '--game', '--mute', '--seed', str(seed)] + list(extra) + \
               ['--script', '; '.join(self.cmds)]
        r = subprocess.run(args, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        return r.returncode, r.stdout.splitlines(), r.stderr.splitlines()


_zones = {}


def object_zones():
    """{object name (lower): (page, hotspot)} of the new game (one probe run of `dump objects`)"""
    if not _zones:
        r = subprocess.run([ROOT + '/build/biosys', '--game', '--mute', '--script', 'advance 100; dump objects'],
                           capture_output=True, text=True, cwd=ROOT)
        for l in r.stdout.splitlines():
            m = re.match(r'obj \d+ (\S+) status=\d+ zone=([^/\s]+)/(\S+)', l)
            if m:
                _zones[m.group(1).lower()] = (m.group(2).upper(), m.group(3))
    return _zones


def click_object(self, obj, wait=300):
    """click the zone the object sits in (pick it up)"""
    page, hs = object_zones()[obj.lower()]
    # a thing lying on the ground is drawn at the bottom of its drop zone ("dropzoneN"), a fruit hangs in its own zone
    if hs.lower().startswith('dropzone'):
        l, t, r, b = [h for h in self.pages[page]['hot'] if h[0] == hs][0][1]
        dy = (b - min(22, (b - t) // 2)) - (t + b) // 2
    else:
        dy = 0
    self.click_hotspot(hs, page=page, wait=wait, dy=dy)


Play.click_object = click_object
