"""Shared reader of the two sides of the audit: the decompiler listing of the original BiosGame.dll (static text, nothing is executed) and
the port's sources. Used by tests/test_parity.py and the audit reports.
  original():  {address: Fn}  with the strings a function refers to and the functions it calls
  port():      Port           with, per original address, the port functions that carry it in a `dll:ADDRESS` comment"""
import collections, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = os.path.dirname(ROOT)
DEC = os.path.join(BASE, 'analysis', 'decompiled')
LISTING = os.path.join(DEC, 'BiosGame.dll.c')
FUNCS = os.path.join(DEC, 'BiosGame.dll.functions.tsv')
STRINGS = os.path.join(DEC, 'BiosGame.dll.strings.tsv')
GAME_END = 0x10078000          # from here on: the C runtime and MFC part of the DLL


def available():
    return all(os.path.exists(p) for p in (LISTING, FUNCS, STRINGS))


class Fn:
    def __init__(self, addr, name, size):
        self.addr, self.name, self.size = addr, name, size
        self.strs = []          # [(address, text)] in order of first use
        self.calls = collections.Counter()      # callee address -> number of call sites
        self.virt = collections.Counter()       # vtable offset -> number of virtual call sites
        self.lines = 0


def original():
    strs = {}
    for l in open(STRINGS, errors='replace').read().split('\n')[1:]:
        p = l.split('\t')
        if len(p) >= 3:
            strs[int(p[0], 16)] = p[2]
    names = {}
    for l in open(FUNCS, errors='replace').read().split('\n')[1:]:
        p = l.split('\t')
        if len(p) >= 3:
            names[p[1]] = int(p[0], 16)
    hdr = re.compile(r'^// ===== ([0-9a-f]{8}) (\S+) \(size (\d+), callers (\d+)\)')
    call = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
    sref = re.compile(r'\bs_\w*?_(100[0-9a-f]{5})\b')
    virt = re.compile(r'\(\*\*\(code \*\*\)\(\*[^()]*?\+ (0x[0-9a-f]+|\d+)\)\)')
    fns, cur, seen = {}, None, None
    for l in open(LISTING, errors='replace'):
        m = hdr.match(l)
        if m:
            cur = Fn(int(m.group(1), 16), m.group(2), int(m.group(3)))
            fns[cur.addr] = cur
            seen = set()
            first = True
            continue
        if cur is None:
            continue
        cur.lines += 1
        for m in sref.finditer(l):
            a = int(m.group(1), 16)
            if a not in seen and a in strs:
                seen.add(a)
                cur.strs.append((a, strs[a]))
        for m in call.finditer(l):
            a = names.get(m.group(1))
            if a is not None and not (a == cur.addr and l.startswith(('void', 'undefined', 'int', 'uint', 'bool', 'char', 'double', 'float'))):
                cur.calls[a] += 1
        for m in virt.finditer(l):
            cur.virt[int(m.group(1), 0)] += 1
    for f in fns.values():          # the signature line names the function itself
        if f.calls.get(f.addr):
            f.calls[f.addr] -= 1
            if f.calls[f.addr] <= 0:
                del f.calls[f.addr]
    return fns


# ---------------------------------------------------------------------------------------------------------------- the port's sources

class Body:
    def __init__(self, path, name, start, end, text, line):
        self.path, self.name, self.start, self.end, self.text, self.line = path, name, start, end, text, line
        self.bare = name.split('::')[-1]

    def __repr__(self):
        return '%s:%d %s' % (os.path.relpath(self.path, ROOT), self.line, self.name)


def _strip(src):
    """the source with comments and string / character literals blanked (same length), for brace matching"""
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif c == 'R' and src.startswith('R"', i) and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == '_')):
            p = src.find('(', i)
            delim = src[i + 2:p]
            j = src.find(')' + delim + '"', p)
            j = n if j < 0 else j + len(delim) + 2
            for k in range(i + 1, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif c == '"' or c == "'":
            if c == "'" and i > 0 and src[i - 1].isalnum():       # digit separator 1'000
                i += 1
                continue
            j = i + 1
            while j < n and src[j] != c and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                out[k] = ' '
            i = j + 1
        else:
            i += 1
    return ''.join(out)


_SCOPE = re.compile(r'^\s*(?:(?:inline|export|typedef|static|template\s*<[^{}]*>)\s+)*(namespace|class|struct|union|enum|extern)\b')
_NAME = re.compile(r'((?:[A-Za-z_]\w*\s*::\s*)*(?:~?[A-Za-z_]\w*|operator\s*[^\s(]+))\s*\($')


def _bodies(path, src):
    """the function bodies of one source file (also methods defined inside classes)"""
    s = _strip(src)
    res = []
    stack = []            # (kind, start) with kind 'scope' or 'fn' or 'other'
    last = 0              # start of the current declaration (after the last ; { } at scope level)
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '{':
            in_fn = any(k == 'fn' for k, _, _ in stack)
            head = s[last:i]
            kind = 'other'
            name = None
            if not in_fn:
                if _SCOPE.match(head) and '(' not in head.split('{')[0].split(':')[0]:
                    kind = 'scope'
                elif _SCOPE.match(head):
                    kind = 'scope'
                else:
                    # a function: the head has a parameter list at paren depth 0 and no `=` before it
                    d, first = 0, -1
                    for k, ch in enumerate(head):
                        if ch == '(':
                            if d == 0 and first < 0:
                                first = k
                            d += 1
                        elif ch == ')':
                            d -= 1
                    if first >= 0 and '=' not in head[:first].replace('==', '').replace('operator=', ''):
                        m = _NAME.search(head[:first + 1])
                        if m:
                            kind, name = 'fn', re.sub(r'\s+', '', m.group(1))
            stack.append((kind, i, name))
            if kind != 'fn' and not in_fn:
                last = i + 1
        elif c == '}':
            if stack:
                kind, st, name = stack.pop()
                if kind == 'fn':
                    hs = last
                    while hs < st and s[hs] in ' \t\r\n':
                        hs += 1
                    res.append(Body(path, name, hs, i + 1, src[hs:i + 1], src.count('\n', 0, hs) + 1))
                if not any(k == 'fn' for k, _, _ in stack):
                    last = i + 1
        elif c == ';' and not any(k == 'fn' for k, _, _ in stack):
            last = i + 1
        i += 1
    return res, s


class Port:
    def __init__(self):
        self.bodies = []                                   # every function body of the port
        self.by_addr = collections.defaultdict(list)       # original address -> [Body]
        self.names = collections.defaultdict(set)          # original address -> bare port names (also from declarations)
        self.mentioned = set()                             # every address written as dll:ADDRESS anywhere under src/
        self.by_name = collections.defaultdict(list)       # bare name -> [Body]
        self.text = ''                                     # all sources, lower case (for 'anywhere' searches)


_MENTION = re.compile(r'(?:dll:|FUN_)(1[0-9a-f]{7})')      # the port writes an original address as dll:ADDRESS or FUN_ADDRESS
_DECL = re.compile(r'(~?[A-Za-z_]\w*)\s*\([^;{}]*\)[^;{}]*;\s*(?://.*)?$')


def port(subdirs=('src',)):
    P = Port()
    texts = []
    for sub in subdirs:
        for r, _, fs in sorted(os.walk(os.path.join(ROOT, sub))):
            for f in sorted(fs):
                if not f.endswith(('.cpp', '.h', '.inc')):
                    continue
                path = os.path.join(r, f)
                src = open(path, errors='replace').read()
                texts.append(src)
                bodies, stripped = _bodies(path, src)
                bodies.sort(key=lambda b: b.start)
                P.bodies.extend(bodies)
                for b in bodies:
                    P.by_name[b.bare].append(b)
                starts = [b.start for b in bodies]
                for m in _MENTION.finditer(src):
                    a = int(m.group(1), 16)
                    P.mentioned.add(a)
                    pos = m.start()
                    inside = [b for b in bodies if b.start <= pos < b.end]
                    if inside:
                        b = min(inside, key=lambda b: b.end - b.start)
                        # a comment in the first lines of a body names the body; deeper inside it names a part of it
                        P.by_addr[a].append(b)
                        if src.count('\n', b.start, pos) <= 3 or src[b.start:pos].count('{') <= 1:
                            P.names[a].add(b.bare)
                        continue
                    ls = src.rfind('\n', 0, pos) + 1
                    line = src[ls:pos]
                    d = _DECL.search(stripped[ls:pos].rstrip() + ';') if '(' in line else None
                    dm = re.search(r'(~?[A-Za-z_]\w*)\s*\(', stripped[ls:pos])
                    if dm and ';' in stripped[ls:pos]:
                        P.names[a].add(dm.group(1))          # a declaration with the comment behind it
                        for b in P.by_name.get(dm.group(1), []):
                            pass
                        continue
                    # a comment above a function: the next body that starts within 8 lines
                    nxt = [b for b in bodies if b.start >= pos and src.count('\n', pos, b.start) <= 8]
                    if nxt:
                        P.by_addr[a].append(nxt[0])
                        P.names[a].add(nxt[0].bare)
    P.text = '\n'.join(texts).lower()
    return P
