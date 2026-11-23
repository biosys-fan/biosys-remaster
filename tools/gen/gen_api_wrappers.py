#!/usr/bin/env python3
"""Generates data/dll_api_wrappers.tsv: for every thin wrapper of BiosGame.dll that sends an engine API
command (`SendMessageA(DAT_100c4324, 0x8000, <code>, ...)`) which EngineApi method it corresponds to and how
the wrapper's arguments map to that method's parameters.

Inputs (read only):
  <dev>/analysis/decompiled/BiosGame.dll.c   the decompiled DLL
  src/engine/engineapi.h                                      the EngineApi contract (command codes, method
                                                              names, parameter names and `// block:` lines)
Output columns (tab separated, header line starts with '#'):
  dll_address  ghidra_name  api_code  EngineApi method  argument mapping
Argument mapping, space separated tokens, wrapper arguments are p1, p2, ... in declaration order:
  pN->name      the argument is stored in the block field the EngineApi parameter `name` stands for
  pN->x,y       a pointer to {x, y}: pN[0] -> x, pN[1] -> y
  pN<-name      the argument is a pointer the wrapper fills from the reply (out-parameter `name`)
  pN->lParam    the argument is passed as lParam itself (commands 1, 8, 9, 0x1b, 0x55: no block)
  pN->name!=0   the wrapper sends 1 if the argument is nonzero, else 0
  pN->bytes(r,g,b)  three bytes packed into one colour value (FadeToColour)
After ` ; ` come notes: maximum name lengths the wrapper checks (error 1 without sending), constants and
conditions the wrapper applies, and the return convention (ret=bool: returns rc == 0; ret=int: 1 on success,
0 on failure with the out-parameters written only on success; ret=void). Every wrapper also stores the
reply in the DLL global DAT_100be424 (Eng_GetLastError, dll:10073fb0), except the void ones which store 0.
Run:  python3 -I tools/gen/gen_api_wrappers.py   (from the port root)
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..', '..'))
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DLL = (_DEV + '/analysis/decompiled/BiosGame.dll.c')
HEADER = os.path.join(ROOT, 'src/engine/engineapi.h')
OUT = os.path.join(ROOT, 'data/dll_api_wrappers.tsv')


def block_offset(name):
    """Ghidra stack names -> offset in the 0xC4-byte block (the block starts at local_c4)."""
    m = re.match(r'local_c4\[(\w+)\]', name)
    if m:
        return int(m.group(1), 0) * 4
    m = re.match(r'(?:local|uStack|iStack|puStack|pcStack|auStack|acStack)_([0-9a-f]+)', name)
    if m:
        return 0xc4 - int(m.group(1), 16)
    return None


def parse_header():
    """code -> dict(name, block: list of (offset, name), params: [names])"""
    cmds = {}
    lines = open(HEADER).read().split('\n')
    cur = None
    for l in lines:
        m = re.match(r'\s*// (0x[0-9a-f]{2}) (\w+)   engine (.*?)   DLL wrapper (\S+)', l)
        if m:
            cur = {'code': int(m.group(1), 16), 'name': m.group(2), 'block': [], 'dll': m.group(4)}
            continue
        m = re.match(r'\s*// block: (.*)', l)
        if m and cur is not None:
            for part in m.group(1).split(', '):
                part = part.strip()
                mm = re.match(r'\+([0-9a-f]{2}) (\w+)$', part)
                if mm:
                    cur['block'].append((int(mm.group(1), 16), mm.group(2)))
                else:
                    mm = re.match(r'lParam (\w+)$', part)
                    if mm:
                        cur['block'].append(('lParam', mm.group(1)))
            continue
        m = re.match(r'\s*virtual int (\w+)\((.*)\) = 0;', l)
        if m and cur is not None:
            assert m.group(1) == cur['name'], (m.group(1), cur['name'])
            plist = [p.strip() for p in m.group(2).split(',')] if m.group(2).strip() else []
            cur['params'] = [re.split(r'[ &*]+', p)[-1] for p in plist]
            cur['ptypes'] = dict((re.split(r'[ &*]+', p)[-1], p) for p in plist)
            cmds[cur['code']] = cur
            cur = None
    return cmds


def first_name(cmd, off):
    for o, n in cmd['block']:
        if o == off:
            return n
    return None


def last_name(cmd, off):
    r = None
    for o, n in cmd['block']:
        if o == off:
            r = n
    return r


def lp_to_p(p):
    return 'p' + p.split('_')[1]


def cond_text(line):
    """`if (param_4 == (char *)0x0) {` -> 'p4==NULL'; other conditions -> None."""
    m = re.match(r'if \((param_\d+) (==|!=) (.*)\) \{$', line)
    if not m:
        return None
    rhs = m.group(3)
    rhs = 'NULL' if re.fullmatch(r'\(.*\*\)0x0', rhs) else rhs
    return '%s%s%s' % (lp_to_p(m.group(1)), m.group(2), rhs)


def negate(c):
    if c is None:
        return None
    return c.replace('==', '!=') if '==' in c else c.replace('!=', '==')


# Replies that are strings / arrays copied out of the block by loops the generic parser does not follow.
OUT_OVERRIDES = {
    0x0e: ['p2<-info[12]'],
    0x10: ['p2<-page', 'p4<-object'],
    0x65: ['p2<-object'],
}


def analyse(body, cmd):
    """One wrapper body (list of lines) -> (tokens, notes, name-length limits, out tokens)."""
    tokens, notes, lens, outs = [], [], [], []
    stack = []                      # one entry per open '{': condition text or None
    curparam = None
    loop_params = []
    last_popped = None
    len_param = None
    send_seen = False
    n = len(body)
    i = 0
    while i < n:
        s = body[i].strip()
        i += 1
        if not s:
            continue
        if s.startswith('do {'):                     # strlen / copy loops are balanced, skip them
            txt = ''
            while i < n:
                t = body[i]
                i += 1
                txt += t
                if t.strip().startswith('} while'):
                    break
            mm = re.search(r'cVar1 = \*(param_\d+);', txt)
            if mm and not send_seen:
                curparam = mm.group(1)
                loop_params.append(curparam)
            continue
        if s.startswith('for ('):
            depth = s.count('{') - s.count('}')
            while depth > 0 and i < n:
                depth += body[i].count('{') - body[i].count('}')
                i += 1
            continue
        mm = re.fullmatch(r'(?:pcVar\d+|puVar\d+) = (param_\d+);', s)
        if mm and not send_seen:
            len_param = mm.group(1)           # start of a strlen loop over this argument
            continue
        if s.startswith('}'):
            last_popped = stack.pop() if stack else None
            if 'else' in s:
                stack.append(negate(last_popped))
            continue
        if s.endswith('{'):
            mm = re.match(r'if \((0x[0-9a-f]+|\d+) < ~uVar\d+ - 1\) \{', s)
            if mm and len_param:
                lens.append((len_param, int(mm.group(1), 0)))
                stack.append('len')
            elif s == 'else {':
                stack.append(negate(last_popped))
            else:
                stack.append(cond_text(s))
            continue
        if 'SendMessageA(DAT_100c4324' in s:
            send_seen = True
            mm = re.search(r'SendMessageA\(DAT_100c4324,0x8000,(?:0x[0-9a-f]+|\d+),(.*)\);', s)
            if re.fullmatch(r'param_\d+', mm.group(1)):
                tokens.append('%s->%s' % (lp_to_p(mm.group(1)), cmd['block'][0][1]))
            continue
        conds = [c for c in stack if c not in (None, 'len')]
        cond = ' && '.join(conds)
        if send_seen:
            mm = re.match(r'\*(param_\d+) = (local_c4\[\w+\]|(?:local|uStack|iStack)_[0-9a-f]+);', s)
            if mm:
                off = block_offset(mm.group(2))
                nm = last_name(cmd, off) or '?+%02x' % off
                outs.append('%s<-%s' % (lp_to_p(mm.group(1)), nm) + (' (if %s)' % cond if cond else ''))
            continue
        # --- before the send ---
        mm = re.match(r'(?:pcVar\d+|puVar\d+) = \(?(?:undefined4 \*|char \*)?\)?&?(\w+);', s)
        if mm and block_offset(mm.group(1)) is not None and curparam and 'local_c4' not in mm.group(1):
            nm = first_name(cmd, block_offset(mm.group(1)))
            tokens.append('%s->%s' % (lp_to_p(curparam), nm) + (' (if %s)' % cond if cond else ''))
            continue
        mm = re.match(r'(\w+(?:\[\w+\])?)(?:\._\d+_\d+_)? = (.*);', s)
        if mm and block_offset(mm.group(1)) is not None:
            off = block_offset(mm.group(1))
            rhs = mm.group(2)
            nm = first_name(cmd, off) or '?+%02x' % off
            ifs = ' (if %s)' % cond if cond else ''
            m1 = re.fullmatch(r'(?:\(uint\))?(param_\d+)', rhs)
            m2 = re.fullmatch(r'\(uint\)\((param_\d+) != 0\)', rhs)
            m3 = re.fullmatch(r'\*(param_\d+)', rhs)
            m4 = re.fullmatch(r'(param_\d+)\[1\]', rhs)
            m5 = re.fullmatch(r'\(uint\)(param_\d+) << 8 \| \(uint\)(param_\d+) << 0x10 \| \(uint\)(param_\d+)', rhs)
            if m1:
                tokens.append('%s->%s%s' % (lp_to_p(m1.group(1)), nm, ifs))
            elif m2:
                tokens.append('%s->%s!=0' % (lp_to_p(m2.group(1)), nm))
            elif m3 and off == 0x0c:
                tokens.append('%s->%s,%s' % (lp_to_p(m3.group(1)), nm, first_name(cmd, 0x10)))
            elif m3:
                tokens.append('%s->%s%s' % (lp_to_p(m3.group(1)), nm, ifs))
            elif m4:
                pass                                    # second element of a point
            elif m5:
                tokens.append('p1,p2,p3->%s(r=p1 g=p2 b=p3, packed r|g<<8|b<<16)' % nm)
            else:
                notes.append('%s=%s%s' % (nm, rhs.replace('0xffffffff', '-1'), ifs))
    return tokens, notes, lens, outs


EXTRA_NOTES = {
    0x65: 'p2 may be NULL (no name is copied)',
    0x6c: 'p2 and p3 are written only when p1 == 1, p4 only when p3 == 1',
    0x27: 'p4 an empty string -> error 4 without sending',
    0x14: 'p3 is the callback pointer: nonzero -> notify',
    0x1f: 'p7 is the callback pointer: nonzero -> notify; p10 NULL -> empty string',
    0x29: 'p5 is the callback pointer: nonzero -> notify',
}


def fix_tokens(tokens, notes, cmd):
    out = []
    for t in tokens:
        m = re.match(r'(p\d+)->notify$', t)
        if m:
            t = m.group(1) + '->notify!=0'
        out.append(t)
    # `+04 = 0 (if pK==0)` / `+04 = 1 (if pK!=0)` is just `field = (pK != 0)`
    notes = list(notes)
    for n in list(notes):
        m = re.fullmatch(r'(\w+)=0 \(if (p\d+)==0\)', n)
        if m and '%s=1 (if %s!=0)' % (m.group(1), m.group(2)) in notes:
            notes.remove(n)
            notes.remove('%s=1 (if %s!=0)' % (m.group(1), m.group(2)))
            out.append('%s->%s!=0' % (m.group(2), m.group(1)))
    out.sort(key=lambda t: int(re.match(r'p(\d+)', t).group(1)))
    return out, notes


def main():
    cmds = parse_header()
    src = open(DLL, errors='replace').read()
    parts = re.split(r'(?m)^// ===== ', src)
    rows = []
    for f in parts:
        m = re.match(r'([0-9a-f]{8}) (\S+) \(size (\d+), callers (\d+)\)', f)
        if not m:
            continue
        mm = re.search(r'SendMessageA\(DAT_100c4324,0x8000,(0x[0-9a-f]+|\d+),', f)
        if not mm:
            continue
        code = int(mm.group(1), 0)
        cmd = cmds[code]
        lines = f.split('\n')
        # signature = text up to the first line that is exactly '{'
        k = 1
        sig = []
        while lines[k].strip() != '{':
            if lines[k].strip():
                sig.append(lines[k].strip())
            k += 1
        sig = ' '.join(sig)
        body = lines[k + 1:]
        tokens, notes, lens, outs = analyse(body, cmd)
        outs += OUT_OVERRIDES.get(code, [])
        if code in (0x1b, 0x55):                  # lParam is the caller's buffer: the reply is written into it
            tokens = [t.replace('->', '<-') + ' (the wrapper passes its argument as lParam; the engine writes the string into it)' for t in tokens]
        tokens, notes = fix_tokens(tokens, notes, cmd)
        outs.sort(key=lambda t: int(re.match(r'p(\d+)', t).group(1)))
        if code in EXTRA_NOTES:
            notes.append(EXTRA_NOTES[code])
        # return convention
        rt = re.match(r'(?:/\*.*?\*/\s*)?(\w+)', sig).group(1)
        ret = {'bool': 'ret=bool(rc==0)', 'void': 'ret=void', 'undefined4': 'ret=int(1 ok, 0 error)'}.get(rt, 'ret=' + rt)
        rows.append({'addr': m.group(1), 'name': m.group(2), 'code': code, 'cmd': cmd, 'tokens': tokens + outs,
                     'notes': notes, 'lens': lens, 'ret': ret, 'sig': sig})
    rows.sort(key=lambda r: (r['code'], r['addr']))
    with open(OUT, 'w') as o:
        o.write('# dll_address\tghidra_name\tapi_code\tEngineApi method\targument mapping (generated by tools/gen/gen_api_wrappers.py)\n')
        for r in rows:
            note = list(r['notes'])
            for p, lim in r['lens']:
                note.append('len(%s)<=%d else error 1, nothing sent' % (lp_to_p(p), lim))
            note.append(r['ret'])
            mapping = ' '.join(r['tokens'])
            o.write('%s\t%s\t0x%02x\t%s\t%s ; %s\n' % (r['addr'], r['name'], r['code'], r['cmd']['name'], mapping, '; '.join(note)))
    print('%d wrappers -> %s' % (len(rows), OUT))


if __name__ == '__main__':
    main()
