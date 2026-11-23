#!/usr/bin/env python3
"""gen_world.py: generate src/game/world_build.gen.cpp, world_ctors.gen.h and world_ctors.gen.cpp.

Inputs (read only):
  analysis/work/sb/events.jsonl      replay of the world-building code (order, kinds, resolved arguments)
  analysis/decompiled (via dllpack)  the eleven builder functions, to recover what the replay lost:
                                     which variable each constructor result lives in, which arguments are
                                     references to earlier items, owners, and post-construction statements
  (post-constructor statements of the builders -- stores into fresh items, string copies, virtual calls, stores to
   globals -- are emitted as calls of the class hooks; see emit_notes)
  analysis/bin/BiosGame.dll          static data only, to read the strings behind FindSite arguments
  analysis/scripts/biosys_savblob.py class names of the constructors

Run:  python3 -I tools/gen/gen_world.py          (from the port root; writes the three files)
      python3 -I tools/gen/gen_world.py --spec   (print the inferred constructor signatures only)
The generated files are checked in and must never be edited by hand: change this script instead
(argument type corrections go into ARG_OVERRIDES below).
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..', '..'))
import csv, json, os, re, struct, subprocess, sys
from collections import OrderedDict

PORT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AN = (_DEV + '/analysis')
sys.path.insert(0, AN + '/scripts')
import calls            # noqa: E402  (static DLL string reader)
import biosys_savblob   # noqa: E402  (class names)

# ---------------------------------------------------------------------------------------------
# tables that encode knowledge of the original

BUILDERS = [0x1002b700, 0x10030eb0, 0x10039130, 0x1002b630, 0x10037a90, 0x1004cf20, 0x1003e700,
            0x100413a0, 0x10051a10, 0x10044ae0, 0x10057f30]
BUILDER_NAMES = {}   # filled from the tsv

# world table adders: dll address -> (World method, table) ; the item's list is the one in saveblob.md sec 3
# Add functions that also register the item in an owner list are described by RELATIONS below.
TABLE_ADDERS = {
    0x10013aa0: ('addBiomeCel', 'cel'), 0x10013980: ('addSite', 'site'), 0x10013f50: ('addWaterTank', 'tank'),
    0x10013dd0: ('addAirFlow', 'air'), 0x10014080: ('addWaterFlow', 'water'), 0x10014790: ('addFeature', 'feature'),
    0x10014570: ('addObject', 'object'), 0x100138a0: ('addFX', 'fx'), 0x10014250: ('addLocalFX', 'localfx'),
}
# owner-taking adders: address -> (relation enum name, table the item additionally goes into or None, kind)
RELATIONS = {
    0x1000c880: ('SiteFeature', 'addFeature', 'feature'),   # CSite::AddFeature -> CObList site+0x28, CWorld::AddFeature
    0x1000c9b0: ('SiteFx', 'addFX', 'fx'),                  # CSite::AddFX (site+0x98) + CWorld::AddFX
    0x1000f6e0: ('PageFx', 'addFX', 'fx'),                  # CPage::AddFX (page+0x10) + CWorld::AddFX
    0x1000f7a0: ('PageOccFx', 'addFX', 'fx'),               # CPage::AddOccFX (page+0x2c) + CWorld::AddFX
    0x1000f890: ('PageOccLoopedFx', 'addFX', 'fx'),         # CPage::AddOccLoopedFX (page+0x4c) + CWorld::AddFX
    0x1000c8c0: ('SitePlant', 'addPlant', 'plant'),         # CSite::AddPlant -> FUN_100143d0 (CWorld plants list)
    0x1000c930: ('SiteLink', None, 'link'),                 # FUN_1000c930: site+0x7c
    0x1000c900: ('SiteList60', None, 'site60'),             # FUN_1000c900: site+0x60
    0x100146d0: ('WorldPlantFood', 'addObject', 'object'),  # FUN_100146d0: world+0xd4 list + CWorld::AddObject
    0x10014710: ('WorldList_f0', None, 'wf0'),              # FUN_10014710: world+0xf0
    0x10014f40: ('WorldFuse', None, 'wfuse'),               # FUN_10014f40: world+0xb8
    0x100148e0: ('WorldSensor', None, 'sensor'),            # CWorld::AddSensor: world+0x12c
    0x10013820: ('WorldPage', None, 'page'),                # CWorld::AddPage
}
# CVidLink__AddMessage is found by name; CBiome adders take a global biome as owner
BIOME_ADDERS = {'CBiome__AddCel': 'BiomeCel', 'CBiome__AddSensor': 'BiomeSensor'}
# the adders the replay (sb_build.py) turned into events; every other registration call is a side effect only
EVENT_ADDERS = {0x10013aa0, 0x10013980, 0x10013f50, 0x10013dd0, 0x10014080, 0x10014790, 0x1000c880, 0x10014570,
                0x100146d0, 0x100138a0, 0x1000c9b0, 0x1000f6e0, 0x1000f7a0, 0x1000f890, 0x10014250, 0x1000c8c0,
                0x100148e0, 0x10013820, 0x1000c930}

# the nine static CBiome objects (analysis/notes/simulation.md section 3; names from scripts/sb_mk.py)
BIOMES = {0x100c1c50: 'rainforest', 0x100c0ee8: 'savannah', 0x100c1280: 'ocean', 0x100c2768: 'undersea',
          0x100c24e8: 'deadbiome', 0x100c2560: 'clubeden', 0x100c1208: 'house', 0x100c1ba0: 'basement',
          0x100c25d8: 'tunnels'}

# explicit argument type corrections: (ctor address, event-argument index) -> letter
#   g = pointer to DLL global data (kept as address)
ARG_OVERRIDES = {(0x1000c270, i): 'g' for i in (13, 14, 15, 16)}   # CSite p14..p17: pointers to four shared "count" buffers
ARG_OVERRIDES[(0x10024f60, 4)] = 'g'                                 # CSiteVariant: the same kind of pointer
ARG_OVERRIDES[(0x1005d300, 6)] = 'g'; ARG_OVERRIDES[(0x1000fe10, 7)] = 'g'   # CBiomeSensor / CLung: pointer to the alarm threshold table (DAT_100b7948..)
ARG_OVERRIDES[(0x10024280, 7)] = 'd'; ARG_OVERRIDES[(0x10024280, 8)] = '-'   # CLungAirFlow: wind speed double = p8,p9

# ctor naming rule for the item name used in dumps ("construction order name" of saveblob.md section 2):
#  default = first argument; air/water flows "a>b#id"; sound localFX "file@site"; rain localFX "file@biome"
NAME_RULE = {'air': 'flow', 'water': 'flow', 'localfx': 'localfx'}

# ---------------------------------------------------------------------------------------------

def load_tsv():
    name2addr, sig = {}, {}
    for r in csv.DictReader(open(AN + '/decompiled/BiosGame.dll.functions.tsv'), delimiter='\t'):
        a = int(r['address'], 16)
        name2addr.setdefault(r['name'], a)
        sig[a] = (r['name'], r['signature'])
    return name2addr, sig

NAME2ADDR, SIG = load_tsv()

def builder_text(addr):
    out = subprocess.run(['python3', '-I', AN + '/scripts/dllpack.py', 'show', '%x' % addr],
                         capture_output=True, text=True, check=True).stdout
    return out

def stmts(text):
    out = []; cur = ''
    for l in text.split('\n'):
        s = l.strip()
        if not s or s.startswith('//') or s.startswith('/*'):
            continue
        cur += (' ' if cur else '') + s
        if s.endswith(';') or s.endswith('{') or s.endswith('}'):
            out.append(re.sub(r'\s+;$', ';', re.sub(r'\s+', ' ', cur))); cur = ''
    return out

def split_args(s):
    out, cur, q, depth = [], '', False, 0
    for ch in s:
        if ch == '"': q = not q
        if not q and ch in '([': depth += 1
        if not q and ch in ')]': depth -= 1
        if ch == ',' and not q and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    out.append(cur.strip())
    return out

VAR = re.compile(r'^(pvVar\d+|pCVar\d+|puVar\d+|local_18|this)$')

def strip_cast(t):
    t = t.strip()
    t = re.sub(r'^\((?:char|void|int|undefined4|CObject)\s*\*?\)\s*', '', t).strip()
    return t

def token_string(tok):
    """string behind a string/DAT token in the DLL image, else None"""
    t = strip_cast(tok)
    m = re.match(r'^&?(?:s_.*_|_?DAT_)([0-9a-f]{8})$', t) or re.match(r'^0x(100[0-9a-f]{5})$', t)
    if not m:
        return None
    return calls.cstr(int(m.group(1), 16))

def token_value(tok):
    """python value of a constructor argument token: ('str',s) ('int',n) ('f64',x) ('var',name)
    ('sitecel',var) ('biome',addr) ('ptr',addr)"""
    t0 = tok.strip()
    t = strip_cast(t0)
    if VAR.match(t):
        return ('var', t)
    m = re.match(r'^\*\(undefined4 \*\) ?\(\(int\)(pvVar\d+) \+ 8\)$', t)
    if m:
        return ('sitecel', m.group(1))
    m = re.match(r'^&_?DAT_([0-9a-f]{8})$', t)
    if m:
        a = int(m.group(1), 16)
        st = calls.cstr(a)
        if st is not None:
            return ('str', st)
        return ('biome', a) if a in BIOMES else ('ptr', a)
    m = re.match(r'^&?s_.*_([0-9a-f]{8})$', t)
    if m:
        st = calls.cstr(int(m.group(1), 16))
        if st is None:
            raise SystemExit('unresolvable string token ' + tok)
        return ('str', st)
    m = re.match(r'^0x([0-9a-f]+)$', t)
    if m:
        v = int(m.group(1), 16)
        if 0x100a0000 <= v < 0x100c0000:
            st = calls.cstr(v)
            if st is not None and st.isprintable():
                return ('str', st)
        return ('int', v)
    if re.match(r'^-?\d+$', t):
        return ('int', int(t) & 0xffffffff)
    try:
        return ('f64', float(t))
    except ValueError:
        pass
    raise SystemExit('cannot resolve argument token: ' + tok)

# ---------------------------------------------------------------------------------------------
# walking the builders

class Call(object):
    """one constructor call in a builder"""
    def __init__(self, addr, name, tokens, var, builder, idx):
        self.addr, self.name, self.tokens, self.var, self.builder, self.idx = addr, name, tokens, var, builder, idx
        self.event = None        # index into events.jsonl (set when the item is added to a list)
        self.args = None         # event args (resolved), when listed
        self.vals = [token_value(t) for t in tokens]

def walk(events):
    ops = []          # per builder: list of ('ctor', Call) | ('add', ...) | ('find', ...) | ('note', text)
    allcalls = []
    ev_i = 0
    per_builder = OrderedDict()
    for b in BUILDERS:
        text = builder_text(b)
        body = stmts(text)
        bops = []
        var = {}      # variable -> Call | ('site', name) | ('feature', name)
        used = set()
        for s in body:
            m = re.match(r'^(\w+) = (?:\([\w ]+\*\))?(\w+)\s*\((.*)\);$', s)
            if m:
                v, f, a = m.groups()
                args = split_args(a)
                if f == 'operator_new':
                    continue
                if f in ('CWorld__FindSite', 'CWorld__FindFeature'):
                    nm = token_string(args[1])
                    if nm is None:
                        raise SystemExit('FindSite arg not a string: ' + s)
                    kind = 'site' if f == 'CWorld__FindSite' else 'feature'
                    var[v] = (kind, nm); used.add(v)
                    bops.append(('find', kind, v, nm))
                    continue
                if f in NAME2ADDR and args and re.match(r'^(pvVar\d+|this(_\d+)?|puVar\d+|\(void \*\)pvVar\d+)$', args[0]):
                    c = Call(NAME2ADDR[f], f, args[1:], v, b, len(allcalls))
                    allcalls.append(c)
                    var[v] = c; used.add(v)
                    bops.append(('ctor', c))
                    continue
            m = re.match(r'^(\w+)\s*\((.*)\);$', s)
            if m and m.group(1) in NAME2ADDR:
                f = m.group(1); a = NAME2ADDR[f]
                args = split_args(m.group(2))
                if len(args) == 2 and (a in TABLE_ADDERS or a in RELATIONS or f in BIOME_ADDERS or f == 'CVidLink__AddMessage'):
                    item = var.get(strip_cast(args[1]))
                    if item is None or not isinstance(item, Call):
                        raise SystemExit('add of unknown item: ' + s)
                    owner = None
                    if not args[0].startswith('&') :
                        owner = strip_cast(args[0])
                        if owner not in var: raise SystemExit('unknown owner var: ' + s)
                    elif a in RELATIONS or f in BIOME_ADDERS:
                        m2 = re.match(r'^&_?DAT_([0-9a-f]{8})$', args[0])
                        owner = ('global', int(m2.group(1), 16)) if m2 else None
                    bops.append(('add', a, f, owner, strip_cast(args[1]), item, s))
                    continue
            # side effects of interest: keep what each variable holds at this point (its class decides the code)
            if is_note(s, var):
                names = set(re.findall(r'\b(?:pvVar\d+|pCVar\d+|puVar\d+|local_18)\b', s))
                bops.append(('note', s, {n: var.get(n) for n in names}))
        per_builder[b] = (bops, sorted(used | {x for x in re.findall(r'\b(?:pvVar\d+|pCVar\d+|puVar\d+|local_18)\b', ' '.join(body))}))
    return per_builder, allcalls

SKIP = re.compile(r'^(void|CObject|undefined4|undefined2|code|char|uint|int)\b[^=(]*;$|^local_|^pcStack_|^\*unaff_|^return|^if |^\}|^else|^do |^while|^for |^goto|^LAB_|^\w+ = \((?:CObject|void) \*\)0x0;$|^Eng_WriteLog|^void \w+\(void\)')
def is_note(s, var):
    if SKIP.match(s):
        return False
    return bool(re.search(r'\b(pvVar\d+|pCVar\d+|puVar\d+|local_18|pcVar\d+|uVar\d+)\b|DAT_', s))

# ---------------------------------------------------------------------------------------------
# signatures

def plausible_hi(v):
    if not isinstance(v, int) or isinstance(v, bool): return False
    ex = (v >> 20) & 0x7ff
    return 0x380 <= ex <= 0x440

def infer(cl):
    """letters per constructor argument: s string (0 = NULL), i int, d double stored as lo,hi dwords (the hi
    position gets '-'), D double already one argument, r item reference, c the cel of a site item,
    b biome global, g pointer to other DLL data"""
    a0 = cl[0].addr
    n = max(len(c.vals) for c in cl)
    out = []
    for i in range(n):
        vs = [c.vals[i] for c in cl if len(c.vals) > i]
        ks = set(v[0] for v in vs)
        if (a0, i) in ARG_OVERRIDES: k = ARG_OVERRIDES[(a0, i)]
        elif 'f64' in ks: k = 'D'
        elif 'sitecel' in ks: k = 'c'
        elif 'var' in ks: k = 'r'
        elif 'biome' in ks: k = 'b'
        elif 'ptr' in ks: k = 'g'
        elif 'str' in ks: k = 's'
        else: k = 'i'
        out.append(k)
    # hi-word anchors -> doubles
    for j in range(1, n):
        if out[j] == 'i' and out[j - 1] == 'i':
            vs = [c.vals[j][1] for c in cl if len(c.vals) > j]
            nz = [v for v in vs if v]
            if nz and all(plausible_hi(v) for v in nz) and all(v > 0xfffff for v in nz):
                out[j - 1] = 'd'; out[j] = '-'
    return out

# ---------------------------------------------------------------------------------------------
# verification against the replay

def norm(x):
    return re.sub(r'\s+', '', x) if isinstance(x, str) else x

def verify(per_builder, events):
    ei = 0
    for b, (bops, used) in per_builder.items():
        for op in bops:
            if op[0] != 'add':
                continue
            _, a, f, owner, vname, item, st = op
            if not (a in EVENT_ADDERS or f == 'CVidLink__AddMessage'):
                continue
            e = events[ei]
            if e[1] != item.addr or int(e[4], 16) != b:
                raise SystemExit('event %d mismatch: %s vs %s' % (ei, e[:3], item.name))
            if item.event is None:
                item.event = ei
                if len(e[3]) != len(item.vals):
                    raise SystemExit('event %d: argument count %d vs %d' % (ei, len(e[3]), len(item.vals)))
                for k, (ea, v) in enumerate(zip(e[3], item.vals)):
                    if v[0] in ('str', 'int', 'f64'):
                        ok = (ea == v[1]) or (v[0] == 'f64' and isinstance(ea, float) and ea == v[1])
                    else:
                        ok = norm(ea) == norm(item.tokens[k]) or (v[0] == 'var' and norm(ea) == v[1]) or \
                             (v[0] == 'ptr' and norm(ea) == '&DAT_%08x' % v[1]) or (v[0] == 'biome' and norm(ea) == '&DAT_%08x' % v[1])
                    if not ok:
                        raise SystemExit('event %d arg %d: replay %r vs builder %r' % (ei, k, ea, item.tokens[k]))
            ei += 1
    if ei != len(events):
        raise SystemExit('replay has %d events, builders produced %d' % (len(events), ei))

# ---------------------------------------------------------------------------------------------
# C++ emission

TABLE_OF_KIND = {'cel': 'cels', 'site': 'sites', 'tank': 'tanks', 'air': 'airflows', 'water': 'waterflows',
                 'localfx': 'localfx', 'fx': 'fx', 'plant': 'plants', 'feature': 'features', 'object': 'objects'}

def cstr_lit(s):
    out = '"'
    for ch in s:
        o = ord(ch)
        if ch in '\\"': out += '\\' + ch
        elif ch == '?': out += '\\?'
        elif 32 <= o < 127: out += ch
        else: out += '\\%03o' % o
    return out + '"'

def int_lit(v, typ):
    if typ == 'u' or typ == 'h':
        return '0x%x' % v if v > 9 else str(v)
    if v >= 0x80000000:
        return 'INT32_MIN' if v == 0x80000000 else '-%d' % (0x100000000 - v)
    return '0x%x' % v if v >= 0x10000 else str(v)

def dbl_lit(lo, hi=None):
    if hi is None:
        x = lo
    else:
        x = struct.unpack('<d', struct.pack('<II', lo, hi))[0]
    if x != x or x in (float('inf'), float('-inf')):
        raise SystemExit('non-finite double constant %08x%08x' % (hi, lo))
    r = repr(x)
    if 'e' not in r and '.' not in r:
        r += '.0'
    return r

def sig_params(addr):
    name, sg = SIG[addr]
    inner = sg[sg.index('(') + 1:sg.rindex(')')]
    ps = [x.strip() for x in split_args(inner)]
    ps = ps[1:]           # this
    kinds = []
    for p in ps:
        t = p.rsplit(' ', 1)[0]
        kinds.append('u' if t == 'uint' else 'h' if t == 'undefined2' else 'i')
    return kinds

# The save format calls the constructor dll:10028c20 "CMiscObject"; the code shows it to be CObject itself (it writes the
# same vtables as FUN_10028b90, the CObject constructor part of every object).
CLASS_NAME_OVERRIDES = {0x10028c20: 'CObject'}

def class_name(addr, fname):
    if addr in CLASS_NAME_OVERRIDES:
        return CLASS_NAME_OVERRIDES[addr]
    for cn, c in biosys_savblob.DATA['classes'].items():
        if int(c['ctor'], 16) == addr:
            return cn
    if fname.startswith('FUN_'):
        return fname
    return fname.split('__')[0]

class CtorSpec(object):
    def __init__(self, addr, cl, events):
        self.addr, self.calls = addr, cl
        self.fname = cl[0].name
        self.letters = infer(cl)
        n = len(self.letters)
        # integer widths from the decompiler's signature (uint -> unsigned, undefined2 -> 16 bit)
        gk = sig_params(addr)
        self.itype = ['i'] * n
        if len(gk) == n:
            for k in range(n):
                if self.letters[k] == 'i':
                    self.itype[k] = gk[k]
        for k in range(n):
            if self.itype[k] == 'h' and any(len(c.vals) > k and c.vals[k][1] > 0xffff for c in cl):
                self.itype[k] = 'i'
        self.kinds = []
        for c in cl:
            if c.event is not None:
                self.kinds.append(events[c.event][0])
        self.cls = class_name(addr, self.fname)
        tabs = []
        for k in self.kinds:
            t = TABLE_OF_KIND.get(k, '(' + k + ')')
            if t not in tabs: tabs.append(t)
        self.lists = tabs if tabs else ['(unlisted)']
        first = self.kinds[0] if self.kinds else ''
        self.namerule = 'default'
        if first in ('air', 'water'): self.namerule = 'flow'
        elif first == 'localfx': self.namerule = 'rain' if self.fname == 'CRainFX__CRainFX' else 'localfx'

    def params(self):
        out = []
        for k, l in enumerate(self.letters):
            nm = 'name' if k == 0 and l == 's' else 'a%d' % k
            if l == '-': continue
            t = {'s': 'const char*', 'D': 'double', 'd': 'double', 'r': 'Item*', 'c': 'Item*', 'b': 'Biome*',
                 'g': 'DllPtr'}.get(l)
            if l == 'i':
                t = {'i': 'int32_t', 'u': 'uint32_t', 'h': 'uint16_t'}[self.itype[k]]
            out.append((t, nm, l))
        return out

    def sigtext(self):
        out = ''
        for k, l in enumerate(self.letters):
            if l == '-': continue
            out += self.itype[k] if l == 'i' else l
        return out

    def arg_expr(self, c, k):
        l = self.letters[k]
        v = c.vals[k]
        t = self.itype[k]
        if l == 's':
            if v[0] == 'str': return cstr_lit(v[1])
            if v == ('int', 0): return 'nullptr'
        elif l == 'i':
            if v[0] == 'int': return int_lit(v[1], t)
        elif l == 'd':
            if v[0] == 'int' and c.vals[k + 1][0] == 'int': return dbl_lit(v[1], c.vals[k + 1][1])
        elif l == 'D':
            if v[0] == 'f64': return dbl_lit(v[1])
        elif l == 'r':
            if v[0] == 'var': return v[1]
            if v == ('int', 0): return 'nullptr'
        elif l == 'c':
            if v[0] == 'sitecel': return '%s->siteCel()' % v[1]
        elif l == 'b':
            if v[0] == 'biome': return 'w.biome(0x%x)' % v[1]
            if v == ('int', 0): return 'nullptr'
        elif l == 'g':
            m = re.search(r'([0-9a-f]{8})$', strip_cast(c.tokens[k]))
            if m: return 'DllPtr{0x%s}' % m.group(1)
        raise SystemExit('ctor %x argument %d: letter %s does not fit %r' % (self.addr, k, l, v))

    def call_args(self, c):
        out = []
        for k, l in enumerate(self.letters):
            if l == '-': continue
            out.append(self.arg_expr(c, k))
        return out

    def name_expr(self):
        L = self.letters
        pn = {k: ('name' if k == 0 and L[0] == 's' else 'a%d' % k) for k in range(len(L))}
        if self.namerule == 'flow':
            third = 'a2' if L[2] == 's' else 'std::to_string(a2)'
            return 'std::string(name) + ">" + a1 + "#" + %s' % third
        if self.namerule == 'localfx':
            return 'std::string(name) + "@" + a2'
        if self.namerule == 'rain':
            return 'std::string(name) + "@" + a2->name'
        for k, l in enumerate(L):
            if l == 's':
                return 'std::string(%s ? %s : "")' % (pn[k], pn[k])
        return 'std::string()'

    def raw_arg(self, k):
        l = self.letters[k]
        nm = 'name' if k == 0 and l == 's' else 'a%d' % k
        return {'s': 'RawArg::ofStr(%s)', 'i': 'RawArg::ofInt(static_cast<int32_t>(%s))', 'd': 'RawArg::ofDouble(%s)',
                'D': 'RawArg::ofDouble(%s)', 'r': 'RawArg::ofItem(%s)', 'c': 'RawArg::ofItem(%s)',
                'b': 'RawArg::ofBiome(%s)', 'g': 'RawArg::ofPtr(%s)'}[l] % nm

def emit_header(specs):
    o = ['// GENERATED by tools/gen/gen_world.py from analysis/work/sb/events.jsonl -- do not edit.',
         '// One constructor factory per distinct constructor address of the original world-building code.',
         '// A factory is a replaceable slot: world_ctors.gen.cpp installs a RawItem placeholder; a ported class',
         '// replaces it with BIOSYS_REGISTER_CTOR (see src/game/README.md).',
         '#pragma once', '#include "game/world.h"', '#include <cstdint>', '', 'namespace biosys::game {', '']
    for sp in specs:
        ps = sp.params()
        decl = ', '.join('%s %s' % (t, n) for t, n, _ in ps)
        types = ', '.join(t for t, _, _ in ps)
        names = ', '.join(n for _, n, _ in ps)
        a = '%x' % sp.addr
        o.append('// dll:%s %s  [%s]  %d call%s  signature "%s"' % (a, sp.fname, ', '.join(sp.lists), len(sp.calls),
                                                              '' if len(sp.calls) == 1 else 's', sp.sigtext()))
        o.append('using CtorFn_%s = Item* (*)(World&%s);' % (a, (', ' + types) if types else ''))
        o.append('CtorFn_%s& ctorSlot_%s();' % (a, a))
        o.append('inline Item* ctor_%s(World& w%s) { return ctorSlot_%s()(w%s); }' %
                 (a, (', ' + decl) if decl else '', a, (', ' + names) if names else ''))
        o.append('')
    o += ['// Install a ported constructor: BIOSYS_REGISTER_CTOR(1000ec80, [](World& w, const char* name, ...) -> Item* { ... });',
          '#define BIOSYS_REGISTER_CTOR(addr, ...) \\',
          '    static const ::biosys::game::CtorRegistrar<::biosys::game::CtorFn_##addr> biosysReg_##addr(::biosys::game::ctorSlot_##addr(), __VA_ARGS__)',
          '', 'template <class Fn> struct CtorRegistrar {', '    CtorRegistrar(Fn& slot, Fn fn) { slot = fn; }', '};', '',
          'struct CtorInfo {', '    uint32_t addr;', '    const char* ctorName;     // name in the decompilation', '    const char* className;    // class in the save format (analysis/notes/saveblob.md)',
          '    const char* lists;        // world list(s) the constructed items end up in', '    int calls;                // calls in the replayed world build',
          '    const char* signature;    // s string, i/u/h int32/uint32/uint16, d/D double, r item, c site cel, b biome, g DLL pointer',
          '};', 'const std::vector<CtorInfo>& allCtors();', '', '} // namespace biosys::game', '']
    return '\n'.join(o)

def emit_ctors_cpp(specs):
    o = ['// GENERATED by tools/gen/gen_world.py -- do not edit.',
         '// Default (RawItem) implementation of every constructor factory, the factory slots and the constructor table.',
         '#include "game/world_ctors.gen.h"', '#include <string>', '', 'namespace biosys::game {', '']
    o.append('const char* biomeName(uint32_t addr) {')
    o.append('    switch (addr) {')
    for a, n in BIOMES.items():
        o.append('    case 0x%x: return "%s";' % (a, n))
    o.append('    default: throw Error("unknown biome global " + std::to_string(addr));')
    o += ['    }', '}', '', 'namespace {', '']
    for sp in specs:
        a = '%x' % sp.addr
        ps = sp.params()
        decl = ', '.join('%s %s' % (t, n) for t, n, _ in ps)
        o.append('Item* raw_%s(World& w%s) {' % (a, (', ' + decl) if decl else ''))
        o.append('    std::vector<RawArg> args;')
        for k, l in enumerate(sp.letters):
            if l == '-': continue
            o.append('    args.push_back(%s);' % sp.raw_arg(k))
        o.append('    return w.adopt(std::make_unique<RawItem>(0x%su, "%s", %s, std::move(args)));' % (a, sp.cls, sp.name_expr()))
        o.append('}')
        o.append('')
    o += ['} // namespace', '']
    for sp in specs:
        a = '%x' % sp.addr
        o.append('CtorFn_%s& ctorSlot_%s() {' % (a, a))
        o.append('    static CtorFn_%s fn = raw_%s;' % (a, a))
        o.append('    return fn;')
        o.append('}')
        o.append('')
    o.append('const std::vector<CtorInfo>& allCtors() {')
    o.append('    static const std::vector<CtorInfo> t = {')
    for sp in specs:
        o.append('        {0x%su, "%s", "%s", "%s", %d, "%s"},' % ('%x' % sp.addr, sp.fname, sp.cls, ','.join(sp.lists), len(sp.calls), sp.sigtext()))
    o += ['    };', '    return t;', '}', '', '} // namespace biosys::game', '']
    return '\n'.join(o)

def one_line(st):
    return re.sub(r'\s+', ' ', st)

# ---------------------------------------------------------------------------------------------
# builder statements that are not constructor calls ("post-constructor statements")

INT_TOKEN = r'(0x[0-9a-f]+|\d+)'
STORE = re.compile(r'^\*\((undefined4|undefined2|undefined1|uint|int|ushort|short|byte|char) \*\)\((\w+) \+ ' + INT_TOKEN + r'\) = (.+);$')
STORE_OR = re.compile(r'^\*\(uint \*\)\((\w+) \+ ' + INT_TOKEN + r'\) = \*\(uint \*\)\((\w+) \+ ' + INT_TOKEN + r'\) \| ' + INT_TOKEN + r';$')
PTR_STORE = re.compile(r'^\*\(CObject \*\*\)\(\(int\)(\w+) \+ ' + INT_TOKEN + r'\) = (\w+);$')
GLOBAL_STORE = re.compile(r'^_?DAT_([0-9a-f]{8}) = (\w+);$')
VCALL = re.compile(r'^\(\*\*\(code \*\*\)\(\*\(int \*\)(\w+) \+ ' + INT_TOKEN + r'\)\)\(\);$')
# the inlined strcpy of the original (a do/while strlen followed by dword and byte copies), one statement per line
STRCPY_PART = [re.compile(x) for x in (
    r'^uVar\d+ = (0xffffffff|uVar\d+ - 1|~uVar\d+);$', r'^pcVar\d+ = (pcVar\d+( \+ 1)?|s_\w+);$', r'^cVar\d+ = \*pcVar\d+;$',
    r'^pCVar\d+ = \(CObject \*\)\(pcVar\d+ \+ -uVar\d+\);$', r'^pCVar\d+ = pCVar\d+ \+ ' + INT_TOKEN + r';$',
    r'^\*\(undefined4 \*\)pCVar\d+ = \*\(undefined4 \*\)pCVar\d+;$', r'^\*pCVar\d+ = \*pCVar\d+;$')]
STRCPY_START = re.compile(r'^uVar\d+ = 0xffffffff;$')
STRCPY_DWORD = re.compile(r'^\*\(undefined4 \*\)(pCVar\d+) = \*\(undefined4 \*\)(pCVar\d+);$')
STRCPY_SRC = re.compile(r'^pcVar\d+ = (s_\w+_[0-9a-f]{8});$')
STRCPY_ADV = re.compile(r'^(pCVar\d+) = (pCVar\d+) \+ ' + INT_TOKEN + r';$')

VCALL_NAMES = {0x78: 'Activate', 0x70: 'TurnOn', 0x94: 'RemoveFromScene'}
VCALL_METHODS = {0x78: 'activate', 0x70: 'turnOn'}   # CFeature virtuals (feature.h) the builders call
STATS = {'emitted': 0, 'deferred': [], 'virtual': []}      # filled while emitting; printed by main()

def num(tok):
    return int(tok, 16) if tok.lower().startswith('0x') else int(tok)

def hexlit(v):
    return '0x%x' % v if v > 9 else str(v)

def ckind(c, events):
    """table kind of the object a variable holds: object, feature, plant, site, page, ... (None: not in a list)"""
    if isinstance(c, Call):
        return events[c.event][0] if c.event is not None else None
    if isinstance(c, tuple):
        return c[0]
    return None

def cname(c):
    return c.vals[0][1] if isinstance(c, Call) and c.vals and c.vals[0][0] == 'str' else '?'

def emit_notes(o, notes, events, calls_by_name):
    """code for a run of consecutive builder statements; `notes` is a list of (text, {variable: what it holds})"""
    i = 0
    pending_site = {}                    # (var, base) -> {off: value}: halves of the doubles stored dword by dword

    def flush_site():
        for (var, base), halves in sorted(pending_site.items()):
            if sorted(halves) != [base, base + 4]:
                raise SystemExit('site %s: only half of the double at +0x%x is stored' % (var, base))
            member = {0xe8: 'setUnk0e8', 0xf0: 'setUnk0f0'}.get(base)
            if not member:
                raise SystemExit('site store at +0x%x has no member' % base)
            o.append('    as<CSite>(%s).%s(%s);   // was: +0x%x = %s, +0x%x = %s' % (
                var, member, dbl_lit(halves[base], halves[base + 4]), base, hexlit(halves[base]), base + 4,
                hexlit(halves[base + 4])))
            STATS['emitted'] += 2
        pending_site.clear()

    while i < len(notes):
        text, snap = notes[i]
        # the inlined strcpy of a string constant into an object
        if STRCPY_START.match(text) and i + 1 < len(notes) and STRCPY_SRC.match(notes[i + 1][0]):
            j = i
            while j < len(notes) and any(r.match(notes[j][0]) for r in STRCPY_PART):
                j += 1
            run = [t for t, _ in notes[i:j]]
            src = STRCPY_SRC.match(run[1]).group(1)
            cp = [STRCPY_DWORD.match(t) for t in run if STRCPY_DWORD.match(t)]
            if len(cp) != 1:
                raise SystemExit('strcpy statement group: dword copy not found in %r' % run)
            var = cp[0].group(1)
            adds = [m for m in (STRCPY_ADV.match(t) for t in run) if m and m.group(1) == var and m.group(2) == var]
            if not adds:
                raise SystemExit('strcpy statement group: destination offset not found in %r' % run)
            off = num(adds[0].group(3))              # the first advance of the destination pointer is the member offset
            what = None
            for t, sn in notes[i:j]:
                if sn.get(var) is not None:
                    what = sn[var]
                    break
            if ckind(what, events) != 'object' or off != 0x34:
                raise SystemExit('strcpy into %s +0x%x: unsupported target %r' % (var, off, what))
            st = calls.cstr(int(re.search(r'([0-9a-f]{8})$', src).group(1), 16))
            if st is None:
                raise SystemExit('strcpy source %s is not a string' % src)
            flush_site()
            o.append('    objectPokeString(%s, 0x%x, %s);   // was: inlined strcpy(obj + 0x%x, %s), %d statements' % (
                var, off, cstr_lit(st), off, src, j - i))
            STATS['emitted'] += j - i
            i = j
            continue
        m = STORE_OR.match(text)
        if m:
            v1, o1, v2, o2, val = m.groups()
            if v1 != v2 or num(o1) != num(o2):
                raise SystemExit('read-modify-write of another member: ' + text)
            k = ckind(snap.get(v1), events)
            flush_site()
            if k != 'object':
                raise SystemExit('|= store into a %s: %s' % (k, text))
            o.append('    objectPoke(%s, 0x%x, 4, %s, PokeOp::Or);' % (v1, num(o1), hexlit(num(val))))
            STATS['emitted'] += 1
            i += 1
            continue
        m = STORE.match(text)
        if m:
            typ, var, off, val = m.groups()
            if not re.match('^' + INT_TOKEN + '$', val):
                raise SystemExit('store of a non-constant: ' + text)
            width = {'undefined4': 4, 'uint': 4, 'int': 4, 'undefined2': 2, 'ushort': 2, 'short': 2, 'undefined1': 1,
                     'byte': 1, 'char': 1}[typ]
            off, val = num(off), num(val)
            what = snap.get(var)
            k = ckind(what, events)
            if k == 'site':
                if width != 4:
                    raise SystemExit('site store of %d bytes: %s' % (width, text))
                pending_site.setdefault((var, off & ~7), {})[off] = val
                i += 1
                continue
            flush_site()
            if k == 'page':
                if width != 4 or off != 0x68:
                    raise SystemExit('page store not supported: ' + text)
                o.append('    as<CPage>(%s).setMilestone(%s);' % (var, hexlit(val)))
            elif k == 'plant':
                if width != 4:
                    raise SystemExit('plant store of %d bytes: %s' % (width, text))
                o.append('    as<CPlant>(%s).poke32(0x%x, %s);' % (var, off, hexlit(val)))
            elif k == 'object':
                o.append('    objectPoke(%s, 0x%x, %d, %s, PokeOp::Set);' % (var, off, width, hexlit(val)))
            elif k == 'feature':
                if width != 4:
                    raise SystemExit('feature store of %d bytes: %s' % (width, text))
                o.append('    as<CFeature>(%s).storeDword(0x%x, %s);' % (var, off, hexlit(val)))
            else:
                raise SystemExit('store into %r (%s): %s' % (what, k, text))
            STATS['emitted'] += 1
            i += 1
            continue
        m = PTR_STORE.match(text)
        if m:
            var, off, src = m.group(1), num(m.group(2)), m.group(3)
            what, srcwhat = snap.get(var), snap.get(src)
            flush_site()
            if ckind(srcwhat, events) != 'feature':
                raise SystemExit('pointer store of a non-feature: ' + text)
            target = calls_by_name.get(what[1].lower()) if isinstance(what, tuple) and what[0] == 'feature' else None
            if isinstance(what, tuple) and what[0] == 'site' and off == 0xc:
                o.append('    as<CSite>(%s).setFeature(&as<CFeature>(%s));' % (var, src))
            elif target is not None and off == 0x5c and class_name(target.addr, target.name) == 'CBubbler':
                o.append('    as<CBubbler>(%s).setOxygenStore(&as<CFeature>(%s));' % (var, src))
            else:
                raise SystemExit('pointer store not supported: %s (%r)' % (text, what))
            STATS['emitted'] += 1
            i += 1
            continue
        m = GLOBAL_STORE.match(text)
        if m:
            flush_site()
            o.append('    w.state().setPointer(0x%x, %s);' % (int(m.group(1), 16), m.group(2)))
            STATS['emitted'] += 1
            i += 1
            continue
        m = VCALL.match(text)
        if m:
            var, off = m.group(1), num(m.group(2))
            what = snap.get(var)
            k = ckind(what, events)
            flush_site()
            if k == 'object' and off == 0x94:
                o.append('    objectVirtualCall(%s, 0x94);   // vtable +0x94: remove from scene' % var)
                STATS['emitted'] += 1
            elif k == 'feature':
                tag = "%s '%s' vtable+0x%x %s" % (class_name(what.addr, what.name), cname(what), off, VCALL_NAMES[off])
                # the features have their behaviour now: the virtual call is made (CFeature::activate / turnOn)
                o.append('    as<CFeature>(%s).%s();   // %s: %s' % (var, VCALL_METHODS[off], tag, text))
                STATS['virtual'].append(tag)
                STATS['emitted'] += 1
            else:
                raise SystemExit('virtual call not supported: %s (%s)' % (text, k))
            i += 1
            continue
        raise SystemExit('builder statement not recognised: ' + text)
    flush_site()

def emit_build(per_builder, specs_by_addr, events):
    o = ['// GENERATED by tools/gen/gen_world.py from analysis/work/sb/events.jsonl and the decompiled builders -- do not edit.',
         '// Replay of the original world construction (Event_OnStartup dll:1001d7b0): one function per builder, one',
         '// factory call per constructor call, the original variable names kept. The statements of the builders that',
         '// are not constructor calls (stores into fresh items, string copies, stores to globals) are emitted as code',
         '// through the hooks of the classes; the virtual calls on features (vtable +0x78 Activate, +0x70 TurnOn) are real',
         '// calls of CFeature::activate / turnOn, each with a comment naming the feature and the decompiled statement.',
         '#include "game/world_ctors.gen.h"', '#include "game/feat_water.h"', '#include "game/feature.h"',
         '#include "game/gameobj.h"', '#include "game/page.h"', '#include "game/plant.h"', '#include "game/site.h"', '',
         'namespace biosys::game {', '', 'namespace {', '']
    calls_by_name = {}
    for b, (bops, used) in per_builder.items():
        for op in bops:
            if op[0] == 'ctor' and op[1].vals and op[1].vals[0][0] == 'str':
                calls_by_name.setdefault(op[1].vals[0][1].lower(), op[1])
    names = []
    for b, (bops, used) in per_builder.items():
        bn = NAME2ADDR_REV.get(b, 'FUN_%x' % b)
        names.append(b)
        o.append('// dll:%x %s' % (b, bn))
        o.append('void build_%x(World& w) {' % b)
        vs = sorted(v for v in used if v != 'this' and VAR.match(v))
        for v in vs:
            o.append('    [[maybe_unused]] Item* %s = nullptr;' % v)
        k = 0
        while k < len(bops):
            op = bops[k]
            k += 1
            if op[0] == 'find':
                _, kind, v, nm = op
                o.append('    %s = &w.require%s(%s);' % (v, 'Site' if kind == 'site' else 'Feature', cstr_lit(nm)))
            elif op[0] == 'ctor':
                c = op[1]
                sp = specs_by_addr[c.addr]
                args = ', '.join(['w'] + sp.call_args(c))
                tail = '' if c.event is not None else '   // not added to any list'
                o.append('    %s = ctor_%x(%s);%s' % (c.var, c.addr, args, tail))
            elif op[0] == 'add':
                _, a, f, owner, vname, item, st = op
                emit_add(o, a, f, owner, vname)
            elif op[0] == 'note':
                m = re.sub(r'\s+', '', op[1])
                mm = re.match(r'^CObList::AddTail\(\(CObList\*\)\(\(int\)(\w+)\+0x28\),(\w+)\);$', m)
                if mm:
                    o.append('    w.relate(Rel::SiteFeature, %s, %s);   // bare CObList::AddTail(site+0x28): not in the world features list' % (mm.group(1), mm.group(2)))
                else:
                    run = [(one_line(op[1]), op[2])]
                    while k < len(bops) and bops[k][0] == 'note' and not re.match(r'^CObList::AddTail', re.sub(r'\s+', '', bops[k][1])):
                        run.append((one_line(bops[k][1]), bops[k][2]))
                        k += 1
                    emit_notes(o, run, events, calls_by_name)
        o += ['}', '']
    o += ['} // namespace', '', 'void buildWorld(World& w) {']
    for b in names:
        o.append('    build_%x(w);' % b)
    o += ['}', '', '} // namespace biosys::game', '']
    return '\n'.join(o)

def emit_add(o, a, f, owner, item):
    own = 'nullptr'
    if isinstance(owner, str): own = owner
    if f in BIOME_ADDERS:
        fn = 'biomeAddCel' if f == 'CBiome__AddCel' else 'biomeAddSensor'
        o.append('    w.%s(w.biome(0x%x), %s);   // %s' % (fn, owner[1], item, f))
        return
    if f == 'CVidLink__AddMessage':
        o.append('    w.relate(Rel::VidLinkMessage, %s, %s);   // CVidLink::AddMessage' % (own, item))
        return
    if a in TABLE_ADDERS:
        o.append('    w.%s(%s);   // dll:%x' % (TABLE_ADDERS[a][0], item, a))
        return
    rel, tab, _ = RELATIONS[a]
    o.append('    w.relate(Rel::%s, %s, %s);   // dll:%x %s' % (rel, own, item, a, f))
    if tab:
        o.append('    w.%s(%s);' % (tab, item))

NAME2ADDR_REV = {}

def main():
    spec_only = '--spec' in sys.argv
    events = [json.loads(l) for l in open(AN + '/work/sb/events.jsonl')]
    per_builder, allcalls = walk(events)
    verify(per_builder, events)
    byctor = OrderedDict()
    for c in allcalls: byctor.setdefault(c.addr, []).append(c)
    for a, (n, _) in SIG.items(): NAME2ADDR_REV[a] = n
    specs = [CtorSpec(a, cl, events) for a, cl in byctor.items()]
    specs.sort(key=lambda sp: sp.addr)
    if spec_only:
        for sp in specs:
            print('%x %-34s %-14s %4d  %s' % (sp.addr, sp.fname, ','.join(sp.lists), len(sp.calls), sp.sigtext()))
        return
    by = {sp.addr: sp for sp in specs}
    for name, text in (('world_ctors.gen.h', emit_header(specs)), ('world_ctors.gen.cpp', emit_ctors_cpp(specs)),
                       ('world_build.gen.cpp', emit_build(per_builder, by, events))):
        with open(os.path.join(PORT, 'src/game', name), 'w') as f:
            f.write(text)
    print('wrote %d constructor factories, %d constructor calls' % (len(specs), len(allcalls)), file=sys.stderr)
    print('builder statements: %d emitted as code (%d of them virtual calls on features), %d deferred (DEFERRED(behaviour)):' %
          (STATS['emitted'], len(STATS['virtual']), len(STATS['deferred'])), file=sys.stderr)
    for d in STATS['deferred']:
        print('  ' + d, file=sys.stderr)

if __name__ == '__main__':
    main()
