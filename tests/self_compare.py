"""Runs a script through build/biosys-selftest and tests/self_oracle.py and compares the dumps.
usage: self_compare.py <selftest binary> <sav> <script> [--seed N]   exit 0 = equal (names present in both, 1e-9 relative)"""
import subprocess, sys, math, os
def parse(text):
    blocks, cur = [], None
    for ln in text.splitlines():
        if ln.startswith('== '):
            cur = {}; blocks.append(cur)
        elif cur is not None and ' = ' in ln and not ln.startswith('msg '):
            k, v = ln.split(' = ', 1); cur[k] = v
    return blocks
def same(a, b):
    if a == b: return True
    try:
        x = float(int(a, 0)) if a.startswith('0x') else float(a)
        y = float(int(b, 0)) if b.startswith('0x') else float(b)
        return abs(x - y) <= 1e-9 * max(1.0, abs(x), abs(y))
    except ValueError:
        return False
def main():
    exe, sav, script = sys.argv[1:4]
    extra = sys.argv[4:]
    here = os.path.dirname(os.path.abspath(__file__))
    a = subprocess.run([exe, 'run', sav, script] + extra, capture_output=True, text=True)
    b = subprocess.run([sys.executable, '-I', os.path.join(here, 'self_oracle.py'), sav, script] + extra, capture_output=True, text=True)
    if a.returncode or b.returncode:
        print('FAIL run', script, '\n C++:', a.stderr.strip()[:300], '\n PY :', b.stderr.strip()[-300:]); return 1
    A, B = parse(a.stdout), parse(b.stdout)
    bad = 0
    if len(A) != len(B): print('FAIL block count', len(A), len(B)); return 1
    for i, (x, y) in enumerate(zip(A, B)):
        for k in sorted(set(x) & set(y)):
            if k == 'self.m_pPage': continue   # pages that do not exist as items print empty in C++
            if not same(x[k], y[k]):
                print('DIFF %s block %d %s: C++ %s  PY %s' % (os.path.basename(script), i, k, x[k], y[k])); bad += 1
    return 1 if bad else 0
sys.exit(main())
