#!/usr/bin/env python3
"""Regenerate data/palette_sets.tsv and data/page_palettes.tsv from the original game files.

Static parsing only (nothing is executed):
  * BiosGame.dll (PE image)       - the 27 palette sets and the constructor calls that bind a set
                                    to every location (x86 push sequences before `call 1000c270`
                                    and `call 10024f60`);
  * BiosGame.dll.c (decompiled)   - World__BuildFeatures 1004cf20: which feature is attached to
                                    which location (CSite::AddFeature FUN_1000c880);
  * PAGES.DAT                     - the page names.

usage: gen_palette_tables.py [--dll F] [--decomp F] [--pages F] [--out DIR]
(defaults: the analysis tree, game CD, <port>/data)
"""
import argparse, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
ROOT = os.path.dirname(PORT)

TABLE_FIRST = b'palettes\\angel\\eangel1.pal'
SLOT = 100
SLOT_NAMES = ['night', 'dawn', 'day', 'dusk', 'cloud', 'gogs']
CTOR_SITE = 0x1000c270      # CSite::CSite(name, ..., palette-set pointer as 10th argument, ...)
CTOR_EVENT = 0x10024f60     # constructor of the special pages EAngel1/EAngel2/EPeyote1/EDeath/EGameEnd
BUILD_FEATURES = (51950, 54275)   # line range of World__BuildFeatures (1004cf20) in the decompilation


# ---------------------------------------------------------------- PE helpers
class Pe:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
        self.secs = []
        for i in range(nsec):
            s = pe + 24 + opt + 40 * i
            name = d[s:s + 8].rstrip(b'\0').decode()
            vsize, va, rsize, raw = struct.unpack_from('<4I', d, s + 8)
            self.secs.append((name, self.base + va, vsize, raw, rsize))

    def off(self, va):
        for _, a, _, raw, rs in self.secs:
            if a <= va < a + rs:
                return raw + va - a
        return None

    def va(self, off):
        for _, a, _, raw, rs in self.secs:
            if raw <= off < raw + rs:
                return a + off - raw
        return None

    def cstr(self, va, maxlen=SLOT):
        o = self.off(va)
        if o is None:
            return None
        return self.d[o:o + maxlen].split(b'\0')[0].decode('latin-1')

    def calls_to(self, target):
        name, a, vs, raw, rs = [s for s in self.secs if s[0] == '.text'][0]
        out = []
        d = self.d
        for i in range(rs - 5):
            if d[raw + i] == 0xE8:
                rel = struct.unpack_from('<i', d, raw + i + 1)[0]
                if (a + i + 5 + rel) & 0xffffffff == target:
                    out.append(a + i)
        return out

    def pushes_before_call(self, call_va, nargs):
        """Decode the argument pushes (and register moves) that precede a call. Returns the list
        of arguments in C order (first argument first); registers are ('reg', n)."""
        for k in range(8, 300):
            p, end, pushes = call_va - k, call_va, []
            ok = True
            while p < end:
                o = self.off(p)
                b = self.d[o]
                if b == 0x68:
                    pushes.append(struct.unpack_from('<I', self.d, o + 1)[0]); p += 5
                elif b == 0x6a:
                    v = self.d[o + 1]
                    pushes.append(v if v < 0x80 else v - 256); p += 2
                elif 0x50 <= b <= 0x57:
                    pushes.append(('reg', b - 0x50)); p += 1
                elif b == 0x8b and self.d[o + 1] & 0xc0 == 0xc0:       # mov reg, reg (this -> ecx)
                    p += 2
                else:
                    ok = False; break
            if ok and p == end and len(pushes) == nargs:
                return pushes[::-1]
        raise SystemExit('cannot decode call at %#x' % call_va)


# ---------------------------------------------------------------- palette sets
def read_sets(pe):
    d = pe.d
    first = d.find(TABLE_FIRST)
    if first < 0:
        raise SystemExit('palette table not found')

    def slot(o):
        return d[o:o + SLOT].split(b'\0')[0].decode('latin-1')

    def is_slot(o):
        t = slot(o).lower()
        return t.startswith('palettes\\') or t.startswith('lookups\\')
    start = first
    while is_slot(start - SLOT) or is_slot(start - 2 * SLOT):
        start -= SLOT
    end = first
    while is_slot(end) or is_slot(end + SLOT) or is_slot(end + 2 * SLOT):
        end += SLOT
    n = (end - start) // SLOT
    if n % 24:
        raise SystemExit('palette table size %d is not a multiple of 24' % n)
    base_va = pe.va(start)
    sets = []
    for s in range(n // 24):
        rec = {}
        for g, gname in enumerate('AB'):
            rec[gname] = ([slot(start + (s * 24 + g * 12 + i) * SLOT) for i in range(6)],
                          [slot(start + (s * 24 + g * 12 + 6 + i) * SLOT) for i in range(6)])
        sets.append(rec)
    return base_va, sets


def set_index(base_va, nsets, va):
    if va is None or isinstance(va, tuple) or va == 0:
        return None
    rel = va - base_va
    if 0 <= rel < nsets * 2400 and rel % 2400 == 0:
        return rel // 2400
    return None


# ---------------------------------------------------------------- location constructors
def read_ctors(pe, base_va, nsets):
    """site name (lower case) -> set index, from the constructor calls in World__BuildCelsSitesTanks."""
    sites = {}
    for c in pe.calls_to(CTOR_SITE):
        a = pe.pushes_before_call(c, 17)
        name = pe.cstr(a[0]); idx = set_index(base_va, nsets, a[9])
        if name is None or idx is None:
            raise SystemExit('unexpected constructor call at %#x' % c)
        sites[name.lower()] = idx
    for c in pe.calls_to(CTOR_EVENT):
        a = pe.pushes_before_call(c, 5)
        name = pe.cstr(a[0]); idx = set_index(base_va, nsets, a[1])
        sites[name.lower()] = idx if idx is not None else -1
    return sites


# ---------------------------------------------------------------- feature -> site (BuildFeatures)
def read_features(pe, decomp):
    lines = open(decomp, encoding='latin-1').read().split('\n')[BUILD_FEATURES[0] - 1:BUILD_FEATURES[1]]
    stmts = [s.strip() for s in re.split(r'[;{}]', ' '.join(l.strip() for l in lines))]

    def addr(a):
        m = re.search(r'(?:DAT_|s_\w*?_)([0-9a-f]{8})$', a.strip())
        return int(m.group(1), 16) if m else None

    def site_arg(a):
        a = a.strip().replace('(char *)', '')
        if a.startswith('"'):
            return a.strip('"')
        v = addr(a)
        return pe.cstr(v) if v else None
    siteof, last, pairs = {}, None, {}
    for s in stmts:
        m = re.match(r'(pvVar\d+) = CWorld__FindSite\(&DAT_100c0f60,\s*(.*)\)$', s)
        if m:
            siteof[m.group(1)] = site_arg(m.group(2)); continue
        m = re.match(r'(?:pCVar\d+|pvVar\d+) = (\w+)\s*\(\s*(pvVar\d+),\s*(s_\w+_[0-9a-f]{8})(.*)', s)
        if m and 'FindSite' not in s:
            last = pe.cstr(addr(m.group(3)))
            if m.group(1) == 'FUN_10023660':           # lifts: explicit list of the sites they link
                args = [x.strip() for x in m.group(4).rstrip(') ').split(',')][-3:]
                lifts = [site_arg(x) if x not in ('(char *)0x0', '0') else None for x in args]
                pairs['lift:' + last.lower()] = lifts
            continue
        m = re.match(r'FUN_1000c880\((pvVar\d+),\s*(pCVar\d+)\)', s)      # CSite::AddFeature
        if m and last and siteof.get(m.group(1)):
            pairs.setdefault(last.lower(), siteof[m.group(1)])
    return pairs


# ---------------------------------------------------------------- page list
def read_pages(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<I', d, 0)[0]
    return [d[4 + 28 * i + 16:4 + 28 * i + 28].split(b'\0')[0].decode('latin-1') for i in range(n)]


# ---------------------------------------------------------------- fixed palettes
# page-name match (lower case regex) -> (palette, reference to the code that selects it)
FIXED_RULES = [
    (r'^menu', 'menu.pal'),                                                   # engine: MENU.PAL
    # dll:100014e0 (travel/transition pages "L*"): trans palette chosen from the site-type flag
    (r'^lr$', r'palettes\trans\rftrans.pal'), (r'^ls$', r'palettes\trans\strans.pal'),
    (r'^ld$', r'palettes\trans\dbtrans.pal'), (r'^lt$', r'palettes\trans\ttrans.pal'),
    (r'^lt2$', r'palettes\trans\t2trans.pal'), (r'^lp[12]$', r'palettes\trans\phtrans.pal'),
    (r'^lp3$', r'palettes\trans\comtrans.pal'), (r'^lb$', r'palettes\trans\btrans.pal'),
    (r'^lo$', r'palettes\trans\otrans.pal'), (r'^lw$', r'palettes\trans\wtrans.pal'),
    (r'^lc$', r'palettes\trans\cetrans.pal'),
    # dll:10006560 (POI terminal popups)
    (r'^upoititle$', r'palettes\poi\poititle.pal'), (r'^upoiintro', r'palettes\poi\poiintro.pal'),
    (r'^upoiprof', r'palettes\poi\poiprof.pal'), (r'^upoihist', r'palettes\poi\poihist.pal'),
    (r'^uce9for_', r'palettes\clubeden\ce9for.pal'),                          # dll:10007e30
    (r'^uph12mic_', r'palettes\house\mscope.pal'),                            # dll:1005b400
    (r'^f\w+prob\w*_o$', r'palettes\sensor\sensor.pal'),                      # dll:1005e000 (probe handler)
    (r'^status$', r'palettes\status\status.pal'),                             # dll:1005a0c0
    (r'^edeath$', r'palettes\death\death.pal'),                               # dll:1002b0b0
    (r'^eintro$', r'palettes\intro\title.pal'),                               # dll:10061ec0
    (r'^egameend_', r'palettes\misc\gameend.pal'),                            # dll:1006ff60, 10070040
    (r'^zlandofnod$', r'palettes\misc\sleep.pal'),                            # dll:1006e900 case 'Z'
]
# POI pages that never load a palette themselves: the handler only changes it on the first page of
# each section, so they keep the palette of the section they are reached from (history).
POI_GUESS = (r'^upoi(space|fols|b)\d', r'palettes\poi\poihist.pal',
             'guess:poi-subpage-keeps-previous-palette(hist)')

# lift pages: feature name -> level -> site list index (FUN_10023660 args: basement, upper..., )
LIFT_RE = re.compile(r'^f([rsdop]|o)lift_(\d)_[co]$')
LIFT_FEATURE = {'r': 'frlift', 's': 'fslift', 'd': 'fdlift', 'o': 'folift', 'p': 'fplift'}

# name prefix -> site name for pages with no code link (name-derived guesses)
NAME_RULES = [
    (r'^[fun](r|s|o)(\d+)', lambda m: m.group(1).upper() + m.group(2)),
    (r'^[fun]db(\d+)', lambda m: 'DB' + m.group(1)),
    (r'^[fun]ce(\d+)', lambda m: 'CE' + m.group(1)),
    (r'^[fun]ph(\d+)', lambda m: 'PH' + m.group(1)),
    (r'^[fn]b([rsdop])', lambda m: 'B' + m.group(1).upper()),
    (r'^a(r|s)(\d+)', lambda m: m.group(1).upper() + m.group(2)),
    (r'^f([rsdop])pod_[co]$', lambda m: 'B' + m.group(1).upper()),
]
# pages that no rule above covers (reason strings must not contain tabs)
SPECIAL = {
    'topbar': (-1, '', 'guess:overlay-uses-current-location-palette'),
    'nhammock': ('RHUT', None, 'guess:hammock-is-in-the-rainforest-hut'),
    'frfheater': ('R1', None, 'guess:biome-heater-not-attached-to-a-site(rainforest,R1 is first)'),
    'fsheater': ('S1', None, 'guess:biome-heater-not-attached-to-a-site(savannah,S1 is first)'),
    'foheater': ('O1', None, 'guess:biome-heater-not-attached-to-a-site(ocean,O1 is first)'),
    'fdbheater': ('DB1', None, 'guess:biome-heater-not-attached-to-a-site(deadbiome,DB1 is first)'),
    'fdbmixer1': ('DB5', None, 'guess:mixer-links-DB4/DB5/DB6(middle)'),
    'fdbmixer2': ('DB3', None, 'guess:mixer-links-DB1/DB3/DB9(middle)'),
    'fomixer1': ('O1', None, 'guess:mixer-argument-is-O1'),
    'fdbrain3': ('DB3', None, 'guess:rain-feature-not-attached-to-a-site(deadbiome)'),
    'fdb3fan': ('DB3', None, 'guess:name'),
    'fph1mask': ('PH1', None, 'guess:name'),
    'xrfmap': ('R1', None, 'guess:item-page-shown-over-current-location(rainforest map)'),
    'xsavmap': ('S1', None, 'guess:item-page-shown-over-current-location(savannah map)'),
    'xceblue': ('CE3', None, 'guess:item-page-shown-over-current-location(club eden blueprint)'),
}
XPAGE_REASON = 'guess:item-page-shown-over-current-location(no static parent)'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dll', default=os.path.join(ROOT, 'analysis/bin/BiosGame.dll'))
    ap.add_argument('--decomp', default=os.path.join(ROOT, 'analysis/decompiled/BiosGame.dll.c'))
    ap.add_argument('--pages', default=os.path.join(ROOT, 'game-cd/files/BIODATA/PAGES.DAT'))
    ap.add_argument('--out', default=os.path.join(PORT, 'data'))
    a = ap.parse_args()

    pe = Pe(a.dll)
    base_va, sets = read_sets(pe)
    ctors = read_ctors(pe, base_va, len(sets))
    feats = read_features(pe, a.decomp)
    pages = read_pages(a.pages)
    by_lower = {p.lower(): p for p in pages}

    # ---- palette_sets.tsv
    with open(os.path.join(a.out, 'palette_sets.tsv'), 'w', newline='\n') as f:
        f.write('# set\tgroup\tslot\tpal\txft   (generated by tools/gen/gen_palette_tables.py from BiosGame.dll; '
                'slots: 0 night 1 dawn 2 day 3 dusk 4 cloud 5 gogs)\n')
        for si, rec in enumerate(sets):
            for g in 'AB':
                pal, xft = rec[g]
                for i in range(6):
                    f.write('%d\t%s\t%d\t%s\t%s\n' % (si, g, i, pal[i].lower(), xft[i].lower()))

    # ---- page_palettes.tsv
    rows = {}            # lower name -> [page, set, fixed, source]
    pending = []         # (page, parent page, reason prefix) resolved after the first pass

    def inherit(page, parent_site, why=None):
        parent = by_lower.get(parent_site.lower())
        if parent is None:
            raise SystemExit('%s: parent site %s is not a page' % (page, parent_site))
        pending.append((page, parent, why))

    def lift_site(page, m):
        fe = LIFT_FEATURE[m.group(1)]
        return feats['lift:' + fe][int(m.group(2)) - 1]

    for p in pages:
        k = p.lower()
        base = k.split('_')[0]
        fixed = next((pal for rx, pal in FIXED_RULES if re.search(rx, k)), None)
        if fixed:
            rows[k] = [p, -1, fixed, 'fixed']; continue
        if re.search(POI_GUESS[0], k):
            rows[k] = [p, -1, POI_GUESS[1], POI_GUESS[2]]; continue
        if k in ctors:
            rows[k] = [p, ctors[k], '', 'ctor']; continue
        if base in ctors and base != k:              # CWorld::FindSite strips "_suffix" (dll:100139b0)
            rows[k] = [p, ctors[base], '', 'ctor']; continue
        if k in SPECIAL:
            s, fx, src = SPECIAL[k]
            if fx is None:
                inherit(p, s, src)
            else:
                rows[k] = [p, s, fx, src]
            continue
        m = LIFT_RE.match(k)
        if m:
            inherit(p, lift_site(p, m)); continue
        # feature attached to a location in World__BuildFeatures (CSite::AddFeature)
        cand = [k, base, 'f' + k[1:], 'f' + base[1:]]
        site = None
        for c in cand:
            if c in feats:
                site = feats[c]; break
        if site is None:
            for fe, st in feats.items():
                if not fe.startswith('lift:') and (k.startswith(fe) or (len(k) > 5 and fe.startswith(k))):
                    site = st; break
        if site:
            inherit(p, site); continue
        for rx, fn in NAME_RULES:
            m = re.match(rx, k)
            if m:
                inherit(p, fn(m), 'guess:name'); break
        else:
            if k[0] == 'x':
                rows[k] = [p, -1, '', XPAGE_REASON]
            else:
                raise SystemExit('unclassified page: ' + p)

    for page, parent, why in pending:
        # follow chains (a parent may itself be inherited)
        cur, seen = parent, set()
        while True:
            r = rows.get(cur.lower())
            if r is not None:
                break
            raise SystemExit('%s: parent %s has no palette row' % (page, cur))
        rows[page.lower()] = [page, r[1], r[2], why if why else 'inherit:' + parent]

    with open(os.path.join(a.out, 'page_palettes.tsv'), 'w', newline='\n') as f:
        f.write('# page\tset\tfixed\tsource   (generated by tools/gen/gen_palette_tables.py; set -1 = none)\n')
        for p in pages:
            r = rows[p.lower()]
            f.write('%s\t%d\t%s\t%s\n' % (r[0], r[1], r[2], r[3]))
    kinds = {}
    for r in rows.values():
        kk = r[3].split(':')[0]
        kinds[kk] = kinds.get(kk, 0) + 1
    print('sets=%d pages=%d %s' % (len(sets), len(pages), kinds))


if __name__ == '__main__':
    main()
