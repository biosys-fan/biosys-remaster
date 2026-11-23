"""Static comparison of every routine of the original BiosGame.dll with its translation in the port (nothing of the original is executed:
the decompiler listing is read as text).

  A  coverage   every game routine of the original is mentioned in the port (`dll:ADDRESS`) or is in a class of routines that need no port
  B  names      every picture / sound / film / page-item name a routine uses appears in the port routines that carry its address
  C  calls      every game routine it calls is also called from the port routines that carry its address

B and C compare text, so they find a translation that left a part out (the fuse box that drew no fuses), not one that computes differently.
`python3 -I tests/audit/parity.py [--all]` prints the findings that are not in tests/audit/parity_accepted.tsv."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portmap as pm

ACCEPTED = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'parity_accepted.tsv')
_DEBUG = re.compile(r'BiosGame\.dll|biosgame\.dll|constructor|\\t\\t|::|^\\n|WARNING|benchmark|^m_|duff|invalid|Invalid|failed|mystery|%0\.3f|NULL', re.I)


def classify_unported(f, O):
    """why an original routine that the port never mentions needs no port, or None"""
    callee = [O[c].name for c in f.calls if c in O]
    game_calls = [c for c in f.calls if c < pm.GAME_END]
    if f.name.startswith(('Catch', 'Unwind')) or f.size <= 12:
        return 'exception funclet / constant'
    if not f.strs and '_atexit' in callee and f.size <= 40:
        return 'static initialiser (registers a destructor with atexit)'
    if not f.strs and 'FUN_10087fb8' in callee and f.size <= 40:
        return 'scalar deleting destructor (calls the destructor, then operator delete)'
    if not f.strs and f.size <= 30 and callee == ['__strcmpi']:
        return 'name comparison helper'
    if f.size in (46, 49, 54, 55, 59, 61, 73, 76, 78, 79, 83, 85) and not f.strs and len(game_calls) <= 1:
        return 'compiler-made destructor / array helper'
    if f.strs and all(_DEBUG.search(t) for _, t in f.strs) and any('WriteLog' in n for n in callee):
        return 'debug dump'
    return None


def is_debug_string(t):
    return bool(_DEBUG.search(t)) or len(t) < 2


def variants(t):
    t = t.lower()
    t1 = t.replace('\\\\', '\\')
    return {t, t1, t1.replace('\\', '\\\\'), t1.replace('\\', '/')}


_files = {}


def file_text(path):
    t = _files.get(path)
    if t is None:
        t = _files[path] = open(path, errors='replace').read().lower()
    return t


def body_text(P, bodies, depth=1):
    """the text of the bodies plus the bodies of the port-only helpers they call (helpers that carry no original address)"""
    seen, out, todo = set(), [], list(bodies)
    for level in range(depth + 1):
        nxt = []
        for b in todo:
            if id(b) in seen:
                continue
            seen.add(id(b))
            out.append(b.text)
            if level < depth:
                for name in set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', b.text)):
                    for h in P.by_name.get(name, []):
                        if h.path.startswith(os.path.join(pm.ROOT, 'src', 'game')) and len(P.by_name[name]) <= 3:
                            nxt.append(h)
        todo = nxt
    return '\n'.join(out)

_IDENT = re.compile(r'\b([A-Za-z_]\w*)\b')
_idents = {}


def idents(b):
    r = _idents.get(id(b))
    if r is None:
        r = _idents[id(b)] = set(_IDENT.findall(b.text))
    return r


def reach_names(P, bodies, depth=3, short=14):
    """the identifiers the bodies use, plus those of what they call: one level through any port function, further levels through
    short ones (wrappers such as imgVis -> host -> CEngineFX_ImageVisibility)"""
    out, seen, todo = set(), set(), list(bodies)
    for level in range(depth + 1):
        nxt = []
        for b in todo:
            if id(b) in seen:
                continue
            seen.add(id(b))
            ids = idents(b)
            out |= ids
            if level < depth:
                for name in ids:
                    for h in P.by_name.get(name, ()):
                        if level == 0 or h.text.count('\n') <= short:
                            nxt.append(h)
        todo = nxt
    return out


def findings():
    O, P = pm.original(), pm.port()
    addr_has_name = {a for a, n in P.names.items() if n}
    res = []
    for a in sorted(O):
        f = O[a]
        if a >= pm.GAME_END:
            continue
        if a not in P.mentioned:
            why = classify_unported(f, O)
            if why is None:
                res.append(('A', a, f.name, 'not ported, not mentioned (size %d; calls %s; strings %s)' % (
                    f.size, ','.join(O[c].name for c in list(f.calls)[:4] if c in O), ' | '.join(t[:30] for _, t in f.strs[:3]))))
            continue
        bodies = P.by_addr.get(a)
        if not bodies:
            continue                        # mentioned in prose only (a note why it is not needed, or a declaration)
        text = body_text(P, bodies)
        low = text.lower()
        done = set()
        for sa, t in f.strs:
            if is_debug_string(t) or t in done:
                continue
            done.add(t)
            if not any(v in low for v in variants(t)):
                if any(v in file_text(b.path) for b in bodies for v in variants(t)):
                    continue                # kept in a constant or table of the same source file
                where = 'elsewhere in the port' if any(v in P.text for v in variants(t)) else 'NOWHERE in the port'
                res.append(('B', a, f.name, 'string "%s" not in %s (%s)' % (t, ', '.join(sorted({b.name for b in bodies})), where)))
        called = reach_names(P, bodies)
        for c, n in f.calls.items():
            if c >= pm.GAME_END or c == a or c not in O or O[c].name in ('Eng_WriteLog',):
                continue
            names = set(P.names.get(c) or ())
            m = re.match(r'Eng_(?:Api[0-9A-Fa-f]{2}_)?(\w+)$', O[c].name)
            if m and names:
                names.add(m.group(1))       # an engine command: the port also calls it as api().Name(...)
            if not names:
                continue                    # the callee has no port routine of its own (inlined, or reported under A)
            if names & called:
                continue
            cb = P.by_addr.get(c, [])
            if any(b in cb for b in bodies) or any(h in cb for b in bodies for nm in idents(b) for h in P.by_name.get(nm, ())):
                continue                    # the callee's address is written in the caller (merged, or a comment at the call) or in a helper it calls
            res.append(('C', a, f.name, 'calls %s (%08x, port: %s) %dx, not called from %s' % (
                O[c].name, c, '/'.join(sorted(names)), n, ', '.join(sorted({b.name for b in bodies})))))
    return res


def key(r):
    return '%s\t%08x\t%s' % (r[0], r[1], re.sub(r'\s+', ' ', r[3])[:90])


def accepted():
    acc = {}
    if os.path.exists(ACCEPTED):
        for l in open(ACCEPTED, errors='replace'):
            l = l.rstrip('\n')
            if not l or l.startswith('#'):
                continue
            p = l.split('\t')
            if len(p) >= 4:
                acc['\t'.join(p[:3])] = p[3]
    return acc


if __name__ == '__main__':
    if not pm.available():
        print('parity: SKIPPED (the analysis files are not present)')
        sys.exit(0)
    res = findings()
    acc = accepted()
    show_all = '--all' in sys.argv
    n = {'A': 0, 'B': 0, 'C': 0}
    open_ = 0
    for r in res:
        n[r[0]] += 1
        k = key(r)
        if k in acc and not show_all:
            continue
        open_ += 1
        print('%s %08x %-34s %s' % (r[0], r[1], r[2][:34], r[3]))
    print('parity: %d findings (coverage %d, names %d, calls %d), %d accepted, %d open' % (len(res), n['A'], n['B'], n['C'], len(res) - open_ if not show_all else len(acc), open_))
