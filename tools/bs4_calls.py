#!/usr/bin/env python3
"""Recover the stack arguments of calls in BioSys.exe that the Ghidra decompile drops (static disassembly with objdump, nothing is run).

The decompile of the BS4 code shows `FUN_0041cab0(this, 0x19)` for BS4Page::ShowItemsByIndex(item, show, &changed): the `show` flag and the
changed pointer are pushed on the stack and the decompiler loses them. This prints, for every call to the given callees inside an address range,
the pushes in front of the call, last pushed first (= first argument first for __cdecl, e.g. ShowItems: n, show, &changed).

usage: python3 -I tools/bs4_calls.py <function start hex> <length in bytes> [callee hex ...]
   e.g. python3 -I tools/bs4_calls.py 424ca0 513 41cab0 4417b0      (the Init of panelah: ShowItems and GetFeatureParameter)
`push ecx` / `push eax` mean a variable: read the surrounding code with analysis/scripts/exedis.sh <start> <len>. A `jmp` that joins two push
sequences makes the list of the later call incomplete (compare with the disassembly).
Callees worth knowing: 41cab0 ShowItemsByIndex(n, show, changed*), 4417b0 OnGetFeatureParameter(name, id, int*, double*),
441720 OnSetFeatureParameter(name, id, a, b, c), 41e040 PaintImage, 4124e0 colour fill, 41ce00 GetVisibleMask, 41cf40 ShowAllItems(mask, changed*)."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import re
import subprocess
import sys

EXE = (_DEV + '/analysis/bin/BioSys.exe')


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    start, length = int(sys.argv[1], 16), int(sys.argv[2])
    targets = [t.lower().lstrip('0') for t in sys.argv[3:]]
    out = subprocess.run(['objdump', '-d', '--no-show-raw-insn', '--x86-asm-syntax=intel', '--start-address=%d' % start,
                          '--stop-address=%d' % (start + length), EXE], capture_output=True, text=True).stdout.splitlines()
    lines = []
    for l in out:
        m = re.match(r'\s*([0-9a-f]+):\s*(.*)', l)
        if m:
            lines.append((m.group(1), ' '.join(m.group(2).split())))
    for i, (addr, text) in enumerate(lines):
        m = re.match(r'call (?:0x)?([0-9a-f]+)', text)
        if not m or (targets and m.group(1).lstrip('0') not in targets):
            continue
        pushes = []
        j = i - 1
        while j >= 0 and len(pushes) < 6 and i - j < 16:
            t = lines[j][1]
            if t.startswith('push'):
                pushes.append(t[5:])
            if t.startswith('call') or t.startswith('j'):
                break
            j -= 1
        print('%s call %s  args (first first): %s' % (addr, m.group(1), pushes))
    return 0


if __name__ == '__main__':
    sys.exit(main())
