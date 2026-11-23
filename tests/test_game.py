#!/usr/bin/env python3
"""Helper of tests/test_game.sh: compares biosys-gametest output with the Python oracle
(analysis/scripts/biosys_savblob.py, read only) and writes corrupted copies of a saved game."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os, re, struct, subprocess, sys
sys.path.insert(0, (_DEV + '/analysis/scripts'))
import biosys_savblob as ref
import calls            # static reader of the strings of the DLL image

EXPECT = {'cels': 38, 'sites': 93, 'tanks': 66, 'airflows': 107, 'waterflows': 125, 'localfx': 84,
          'fx': 224, 'plants': 67, 'features': 210, 'objects': 423}

def oracle(sav):
    d, off = ref.read_sav(sav)
    return d, off, ref.parse_blob(d[off:])

def cmd_names(items_tsv, sav):
    _, _, segs = oracle(sav)
    got = {}
    for line in open(items_tsv):
        sec, idx, name, cls, typed = line.rstrip('\n').split('\t')
        got.setdefault(sec, []).append(name)
    bad = 0
    for sec, n in EXPECT.items():
        want = [it.name for s, items, _ in segs if s == sec for it in items]
        if len(want) != n:
            print('oracle has %d %s, expected %d' % (len(want), sec, n)); bad += 1
        g = got.get(sec, [])
        if len(g) != n:
            print('FAIL %s: %d items, expected %d' % (sec, len(g), n)); bad += 1; continue
        for i, (a, b) in enumerate(zip(g, want)):
            if a != b:
                print('FAIL %s[%d]: name %r, oracle %r' % (sec, i, a, b)); bad += 1; break
        else:
            print('PASS %s: %d items, names equal the oracle' % (sec, n))
    return 1 if bad else 0

def cmd_dump(cpp_dump, sav):
    _, _, segs = oracle(sav)
    want = {}
    for nm, items, pad in segs:
        for it in items:
            for f in it.fields:
                want.setdefault('%s/%s/' % (nm, it.key), []).append(
                    '%s/%s/%s = %s' % (nm, it.key, f.name if not f.name == '_pad' else '_pad@%#x' % f.off, f.show()))
    got = {}
    order = []
    for line in open(cpp_dump):
        line = line.rstrip('\n')
        sec, key, rest = line.split('/', 2)
        p = '%s/%s/' % (sec, key)
        if p not in got: order.append(p)
        got.setdefault(p, []).append(line)
    bad = 0
    for p in order:
        if p not in want:
            print('FAIL: typed item %s is not in the oracle dump' % p); bad += 1; continue
        if got[p] != want[p]:
            for a, b in zip(got[p] + [''] * 3, want[p] + [''] * 3):
                if a != b:
                    print('FAIL %s\n  C++:    %s\n  oracle: %s' % (p, a, b)); break
            bad += 1
    nl = sum(len(v) for v in got.values())
    print('%s: %d typed items, %d dump lines compared with the oracle' % ('FAIL' if bad else 'PASS', len(order), nl))
    if not order:
        print('FAIL: no typed items'); return 1
    return 1 if bad else 0

def cmd_corrupt(sav, outdir):
    d, off = ref.read_sav(sav)
    os.makedirs(outdir, exist_ok=True)
    blob = off
    def w(name, data):
        open(os.path.join(outdir, name), 'wb').write(bytes(data))
    w('trunc_1.sav', d[:-1])
    w('trunc_half_blob.sav', d[:off + (len(d) - off) // 2])
    w('trunc_header_only.sav', d[:off + 20])
    w('trunc_no_blob.sav', d[:off])
    w('extra_byte.sav', d + b'\0')
    def patch(at, new):
        x = bytearray(d); x[at:at + len(new)] = new; return x
    w('bad_magic.sav', patch(blob, b'JON IS A YOYX'))
    w('bad_version.sav', patch(blob + 113, struct.pack('<I', 0x24)))
    w('bad_trailer.sav', patch(len(d) - 7, b'THE ENX'))
    w('bad_self_tag.sav', patch(blob + 178378, b'SELX'))
    w('bad_blob_offset.sav', patch(8, struct.pack('<I', len(d) + 100)))
    w('bad_nonzero_pad.sav', patch(blob + 178978 + 8, b'\x01'))   # CSubTechHandler zero_fill
    return 0

# ---------------------------------------------------------------------------------------------------------------
# (e) the post-constructor statements of the original builders, read from the decompiled text, against a fresh world

AN = (_DEV + '/analysis')
BUILDERS = [0x1002b700, 0x10030eb0, 0x10039130, 0x1002b630, 0x10037a90, 0x1004cf20, 0x1003e700, 0x100413a0, 0x10051a10,
            0x10044ae0, 0x10057f30]          # the eleven functions Event_OnStartup (dll:1001d7b0) calls to build the world
# what the builders' stores to DLL globals are: address -> name of the query
GLOBALS = {0x100c1cc8: 'ocean', 0x100c4274: 'dataTerminal', 0x100c1150: 'fuseBox0', 0x100c1154: 'fuseBox1',
           0x100c1158: 'fuseBox2', 0x100c115c: 'fuseBox3', 0x100c1160: 'fuseBox4'}
INT = r'(0x[0-9a-f]+|\d+)'

def builder_text(addr):
    return subprocess.run(['python3', '-I', AN + '/scripts/dllpack.py', 'show', '%x' % addr], capture_output=True,
                          text=True, check=True).stdout

def statements(text):
    out, cur = [], ''
    for l in text.split('\n'):
        t = l.strip()
        if not t or t.startswith('//') or t.startswith('/*'):
            continue
        cur += (' ' if cur else '') + t
        if t.endswith(';') or t.endswith('{') or t.endswith('}'):
            out.append(re.sub(r'\s+;$', ';', re.sub(r'\s+', ' ', cur))); cur = ''
    return out

def split_args(a):
    out, cur, depth, q = [], '', 0, False
    for ch in a:
        if ch == '"': q = not q
        if not q and ch in '([': depth += 1
        if not q and ch in ')]': depth -= 1
        if ch == ',' and depth == 0 and not q:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    out.append(cur.strip())
    return out

def string_of(tok):
    m = re.search(r'(?:s_\w+_|DAT_)([0-9a-f]{8})$', tok.strip())
    return calls.cstr(int(m.group(1), 16)) if m else None

def ctor_lists(gametest):
    """constructor address -> first world list, from `biosys-gametest ctors`"""
    out = {}
    for line in subprocess.run([gametest, 'ctors'], capture_output=True, text=True, check=True).stdout.split('\n')[1:]:
        f = line.split()
        if len(f) >= 6:
            out[int(f[0], 16)] = f[3].split(',')[0]
    return out

def builder_facts(gametest):
    """[(query, how, expected, source statement)] for every post-constructor statement; deferred virtual calls separately"""
    name2addr = {}
    for line in open(AN + '/decompiled/BiosGame.dll.functions.tsv').read().split('\n')[1:]:
        f = line.split('\t')
        if len(f) > 2: name2addr.setdefault(f[1], int(f[0], 16))
    lists = ctor_lists(gametest)
    facts, deferred = [], []
    for b in BUILDERS:
        var = {}                       # variable -> (ctor address | None, item name)
        sts = statements(builder_text(b))
        sites = {}                     # (site name, base) -> {offset: dword}
        i = 0
        def flush_sites():
            for (nm, base), halves in sorted(sites.items()):
                lo, hi = halves[base], halves[base + 4]
                d = struct.unpack('<d', struct.pack('<II', lo, hi))[0]
                facts.append(('site %s %s' % (nm, 'unk0e8' if base == 0xe8 else 'unk0f0'), 'eq', repr(d),
                              'site +0x%x/+0x%x' % (base, base + 4)))
            sites.clear()
        while i < len(sts):
            st = sts[i]; i += 1
            if not (re.match(r'^\*\((undefined4|undefined2|uint|int) \*\)\(\w+ \+ ', st)):
                flush_sites()
            m = re.match(r'^(\w+) = (?:\(\w+ \*\))?(\w+) ?\((\w+),(.*)\);$', st)
            if m and m.group(2) in name2addr and re.match(r'^(pvVar\d+|this)$', m.group(3)) and m.group(2) != 'operator_new':
                args = split_args(m.group(4))
                nm = string_of(args[0]) if args else None
                var[m.group(1)] = (name2addr[m.group(2)], nm)
                continue
            m = re.match(r'^(\w+) = CWorld__Find(Site|Feature) ?\(&DAT_100c0f60,(.*)\);$', st)
            if m:
                var[m.group(1)] = ('find' + m.group(2).lower(), string_of(m.group(3)))
                continue
            m = re.match(r'^\*\((?:undefined4|uint|int) \*\)\((\w+) \+ ' + INT + r'\) = \*\((?:uint) \*\)\(\1 \+ \2\) \| ' + INT + ';$', st)
            if m:
                v, off, val = m.group(1), int(m.group(2), 0), int(m.group(3), 0)
                assert lists[var[v][0]] == 'objects', st
                facts.append(('object %s peek 0x%x 4' % (var[v][1], off), 'mask', val, st))
                continue
            m = re.match(r'^\*\((undefined4|undefined2) \*\)\((\w+) \+ ' + INT + r'\) = ' + INT + ';$', st)
            if m:
                typ, v, off, val = m.group(1), m.group(2), int(m.group(3), 0), int(m.group(4), 0)
                width = 4 if typ == 'undefined4' else 2
                table = lists.get(var[v][0])
                nm = var[v][1]
                if table == '(page)':
                    assert off == 0x68
                    facts.append(('page %s milestone' % nm, 'eq', '0x%x' % val, st))
                elif table == 'sites':
                    sites.setdefault((nm, off & ~7), {})[off] = val
                elif table == 'plants':
                    facts.append(('plant %s peek32 0x%x' % (nm, off), 'eq', '0x%x' % val, st))
                elif table == 'objects':
                    facts.append(('object %s peek 0x%x %d' % (nm, off, width), 'eq', '0x%x' % val, st))
                elif table == 'features':
                    facts.append(('feature %s peek 0x%x' % (nm, off), 'eq', '0x%x' % val, st))
                else:
                    raise SystemExit('store into %s (%s): %s' % (nm, table, st))
                continue
            m = re.match(r'^\*\(CObject \*\*\)\(\(int\)(\w+) \+ ' + INT + r'\) = (\w+);$', st)
            if m:
                v, off, src = m.group(1), int(m.group(2), 0), m.group(3)
                if var[v][0] == 'findsite':
                    assert off == 0xc
                    facts.append(('site %s feature' % var[v][1], 'eqi', var[src][1], st))
                else:
                    assert var[v][0] == 'findfeature' and off == 0x5c
                    facts.append(('feature %s oxygenStore' % var[v][1], 'eqi', var[src][1], st))
                continue
            m = re.match(r'^_?DAT_([0-9a-f]{8}) = (\w+);$', st)
            if m and int(m.group(1), 16) in GLOBALS:
                facts.append(('global %s' % GLOBALS[int(m.group(1), 16)], 'eqi', var[m.group(2)][1], st))
                continue
            m = re.match(r'^\(\*\*\(code \*\*\)\(\*\(int \*\)(\w+) \+ ' + INT + r'\)\)\(\);$', st)
            if m:
                v, off = m.group(1), int(m.group(2), 0)
                table = lists.get(var[v][0])
                if table == 'features':
                    deferred.append((var[v][1].lower(), off))
                else:
                    assert table == 'objects' and off == 0x94, st
                    facts.append(('object %s inScene' % var[v][1], 'eq', '0', st))
                continue
            if st == 'uVar5 = 0xffffffff;' or re.match(r'^uVar\d+ = 0xffffffff;$', st):
                # the inlined strcpy of a string constant into the object: find the source string and the destination
                j = i
                src = dst = None
                while j < len(sts) and re.match(r'^(uVar|pcVar|cVar|pCVar)\d+ = |^\*(\(undefined4 \*\))?pCVar\d+ = ', sts[j]):
                    ms = re.match(r'^pcVar\d+ = (s_\w+_[0-9a-f]{8});$', sts[j])
                    if ms and src is None: src = string_of(ms.group(1))
                    md = re.match(r'^(pCVar\d+) = (pCVar\d+) \+ ' + INT + ';$', sts[j])
                    if md and md.group(1) == md.group(2) and dst is None and int(md.group(3), 0) == 0x34: dst = md.group(1)
                    j += 1
                if src is not None and dst is not None:
                    facts.append(('object %s variant' % var[dst][1], 'eq', src, 'strcpy(+0x34, "%s")' % src))
                    i = j
                    continue
        flush_sites()
    return facts, deferred

def cmd_builder_queries(gametest, queries_out, expect_out):
    facts, deferred = builder_facts(gametest)
    with open(queries_out, 'w') as q, open(expect_out, 'w') as e:
        for query, how, exp, src in facts:
            q.write(query + '\n')
            e.write('%s\t%s\t%s\t%s\n' % (query, how, exp, src))
    print('%d post-constructor statements give %d checks; %d virtual calls on features are deferred' % (
        sum(1 for _ in facts), len(facts), len(deferred)))
    return 0

def cmd_builder_compare(answers, expect):
    got = {}
    for line in open(answers):
        k, _, v = line.rstrip('\n').rpartition(' = ')
        got[k] = v
    bad = n = 0
    kinds = {}
    for line in open(expect):
        query, how, exp, src = line.rstrip('\n').split('\t')
        if how == 'mask': exp = int(exp)
        a = got[query]
        n += 1
        kinds[query.split()[0]] = kinds.get(query.split()[0], 0) + 1
        ok = ((int(a, 16) & exp) == exp) if how == 'mask' else (a.lower() == exp.lower() if how == 'eqi' else a == exp)
        if not ok:
            print('FAIL %s: fresh world says %r, decompiled builder (%s) says %r%s' % (query, a, src, exp, ' (mask)' if how == 'mask' else ''))
            bad += 1
    print('%s: %d post-constructor statements checked against the fresh world (%s)' % (
        'FAIL' if bad else 'PASS', n, ', '.join('%s %d' % kv for kv in sorted(kinds.items()))))
    return 1 if bad else 0

def cmd_builder_deferred(gametest, gen_cpp):
    """the virtual calls on features of the generated builder (activate / turnOn, formerly DEFERRED(behaviour)) are exactly
    the virtual calls on features of the decompiled builders, and no builder statement is left as an untagged comment"""
    _, deferred = builder_facts(gametest)
    text = open(gen_cpp).read()
    tags = re.findall(r"as<CFeature>\((\w+)\)\.(activate|turnOn)\(\);   // (\w+) '([^']+)' vtable\+(0x[0-9a-f]+) (\w+): ", text)
    got = sorted((n.lower(), int(o, 16)) for _, _, _, n, o, _ in tags)
    want = sorted(deferred)
    bad = 0
    if got != want:
        print('FAIL: feature virtual calls %r differ from the builders\' %r' % (got, want)); bad += 1
    if 'DEFERRED(behaviour)' in text.replace('"DEFERRED(behaviour)"', ''):
        print('FAIL: a DEFERRED(behaviour) comment remains'); bad += 1
    untagged = [l for l in text.split('\n') if 'ORIGINAL:' in l]
    if untagged:
        print('FAIL: %d untagged ORIGINAL lines remain, first: %s' % (len(untagged), untagged[0])); bad += 1
    for var, meth, c, n, o, what in tags:
        if (int(o, 16), what, meth) not in ((0x78, 'Activate', 'activate'), (0x70, 'TurnOn', 'turnOn')):
            print('FAIL: call %s %s has an unknown vtable meaning' % (n, o)); bad += 1
    print('%s: %d feature virtual calls (activate / turnOn) = the %d virtual calls on features in the builders; %d untagged ORIGINAL lines' % (
        'FAIL' if bad else 'PASS', len(tags), len(want), len(untagged)))
    return 1 if bad else 0

# the first sensor built (fr1probe): its first readings are the cel's values plus RandPlusMinus(error) drawn from the
# generator seeded with 1 (srand not called: the C runtime starts at seed 1), in the order of the decompiled constructor
def cmd_sensor(answers_line):
    line = open(answers_line).read().strip().split(' = ', 1)[1]
    rd, cel = line.split(' | ')
    r = {k: float(v) for k, v in (x.split('=') for x in rd.split())}
    c = {k: float(v) for k, v in (x.split('=') for x in cel.split())}
    seed = 1
    def rnd():
        nonlocal seed
        seed = (seed * 214013 + 2531011) & 0xffffffff
        return (seed >> 16) & 0x7fff
    def pm(x): return x * 2.0 * rnd() * 3.051850947599719e-05 - x
    d = lambda lo, hi: struct.unpack('<d', struct.pack('<II', lo, hi))[0]
    e_air, e_co2, e_hum = d(0x9999999a, 0x3fb99999), d(0xeb1c432d, 0x3f1a36e2), d(0x9999999a, 0x3fb99999)
    e_light, e_ox, e_pol = d(0xd2f1a9fc, 0x3f50624d), d(0xd2f1a9fc, 0x3f50624d), d(0x47ae147b, 0x3f847ae1)
    e_st, e_ph, e_sw, e_wind = d(0x9999999a, 0x3fb99999), d(0x47ae147b, 0x3f847ae1), d(0x47ae147b, 0x3f847ae1), d(0x47ae147b, 0x3f847ae1)
    want = {}
    want['pressure'] = c['airVolume'] / c['airCapacity'] * 1000.0 + pm(0.1)
    want['airTemp'] = pm(e_air) + c['airTemp']
    want['co2'] = pm(e_co2) + c['co2']
    want['humidity'] = pm(e_hum) + c['humidity']
    want['light'] = (pm(e_light) + c['light']) * 100.0
    want['oxygen'] = pm(e_ox) + c['oxygen']
    want['pollution'] = pm(e_pol) + c['pollution']
    want['soilPH'] = pm(e_ph) + c['soilPH']          # +0xa8 is drawn before +0xa0
    want['soilTemp'] = pm(e_st) + c['soilTemp']
    want['soilWater'] = pm(e_sw) + c['soilWater']
    want['windSpeed'] = pm(e_wind) + c['windSpeed']
    bad = 0
    for k, v in want.items():
        if abs(v - r[k]) > 1e-9 * max(1.0, abs(v)):
            print('FAIL sensor reading %s: %r, expected %r' % (k, r[k], v)); bad += 1
    print('%s: the 11 first readings of fr1probe = cel value + RandPlusMinus(error), draws %s' % ('FAIL' if bad else 'PASS', 'in the original order'))
    return 1 if bad else 0

# CFood (dll:10063520): a food that starts on its plant (flag 0x8000 clear) does plant+0x10 += 1 and plant+0x14 |= 1 << (slot-1)
def cmd_fruit(gametest, outdir):
    name2addr = {}
    for line in open(AN + '/decompiled/BiosGame.dll.functions.tsv').read().split('\n')[1:]:
        f = line.split('\t')
        if len(f) > 2: name2addr.setdefault(f[1], int(f[0], 16))
    want = {}                              # plant name -> [count, mask]
    for b in BUILDERS:
        var = {}
        for st in statements(builder_text(b)):
            m = re.match(r'^(\w+) = (?:\(\w+ \*\))?(\w+) ?\((\w+),(.*)\);$', st)
            if not (m and m.group(2) in name2addr and re.match(r'^pvVar\d+$', m.group(3))):
                continue
            args = split_args(m.group(4))
            plant = re.sub(r'^\(int\)', '', args[3]) if len(args) > 3 else ''
            if name2addr[m.group(2)] == 0x10063520 and plant in var and var[plant][0] == 0x10072210:
                flags, slot = int(args[1], 0), int(args[4], 0)
                if flags & 0x8000 == 0:
                    w = want.setdefault(var[plant][1], [0, 0])     # looked up before the result is assigned to a variable
                    w[0] += 1
                    w[1] |= 1 << (slot - 1)
            var[m.group(1)] = (name2addr[m.group(2)], string_of(args[0]))
    q = os.path.join(outdir, 'fruit_q.txt')
    with open(q, 'w') as f:
        for n in sorted(want):
            f.write('plant %s fruitCount\nplant %s fruitMask\n' % (n, n))
    got = {}
    for line in subprocess.run([gametest, 'fresh', q], capture_output=True, text=True, check=True).stdout.split('\n'):
        if ' = ' in line:
            k, _, v = line.rpartition(' = ')
            got[k] = v
    bad = 0
    for n, (cnt, mask) in sorted(want.items()):
        if got['plant %s fruitCount' % n] != str(cnt) or int(got['plant %s fruitMask' % n], 16) != mask:
            print('FAIL plant %s: fruit count/mask %s/%s, builder says %d/0x%x' % (n, got['plant %s fruitCount' % n], got['plant %s fruitMask' % n], cnt, mask)); bad += 1
    print('%s: %d plants carry the fruit of the food objects built on them (%d fruit)' % ('FAIL' if bad else 'PASS', len(want), sum(c for c, _ in want.values())))
    return 1 if bad or not want else 0

if __name__ == '__main__':
    cmds = {'names': (cmd_names, 2), 'dump': (cmd_dump, 2), 'corrupt': (cmd_corrupt, 2), 'builder-queries': (cmd_builder_queries, 3),
            'builder-compare': (cmd_builder_compare, 2), 'builder-deferred': (cmd_builder_deferred, 2), 'sensor': (cmd_sensor, 1), 'fruit': (cmd_fruit, 2)}
    c, n = cmds[sys.argv[1]]
    sys.exit(c(*sys.argv[2:2 + n]))
