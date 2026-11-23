"""Which functions of the game logic (src/game/*.cpp) do the sweeps execute at all?

  python3 -I tests/audit/coverage.py [--quick] [--no-build] [--merge-only] [--variants a,b,...] [--top N]

Builds an instrumented game into build-cov/ (clang source-based coverage; the normal build/ is not touched), lets every sweep variant
run with it (tests/audit/sweep.py takes the binary from BIOSYS_BIN; every run writes into a pool of LLVM_PROFILE_FILE files that
merge on exit), merges the profiles and writes build/audit/coverage_functions.tsv (file, function, executed count, lines covered,
lines total). Prints the share of functions and lines reached and the never-executed functions grouped by source file, largest first.
It is a report, not a test: the exit code is 0.

  --quick       leave out the `apply` matrix (about 12 minutes of the roughly 15)
  --variants    default: base,power,take,use,look,apply,apply+power:B
  --no-build    use build-cov/biosys as it is
  --merge-only  do not run the sweeps, evaluate the profiles of the last run
  --top N       print the N largest never-executed functions (default 30)
Only functions defined in src/game/*.cpp count (inline functions of headers and templates are left out)."""
import collections, glob, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
COV = os.path.join(ROOT, 'build-cov')
OUT = os.path.join(ROOT, 'build', 'audit')
PROF = os.path.join(OUT, 'cov')
BIN = os.path.join(COV, 'biosys')
VARIANTS = ['base', 'power', 'take', 'use', 'look', 'apply', 'apply+power']


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors='replace', **kw)


def build():
    mk = open(os.path.join(ROOT, 'Makefile')).read()
    cxx = re.search(r'^CXXFLAGS\s*\?=\s*(.*)$', mk, re.M).group(1).replace('-O2', '-O1')
    ld = re.search(r'^LDFLAGS\s*\?=\s*(.*)$', mk, re.M).group(1)
    r = sh(['make', '-j%d' % (os.cpu_count() or 4), 'B=build-cov', 'CXXFLAGS=%s -fprofile-instr-generate -fcoverage-mapping' % cxx,
            'LDFLAGS=%s -fprofile-instr-generate' % ld, 'build-cov/biosys'])
    if r.returncode != 0:
        print(r.stdout[-2000:] + r.stderr[-3000:])
        raise SystemExit('the instrumented build failed')


def run_sweeps(variants):
    shutil.rmtree(PROF, ignore_errors=True)
    os.makedirs(os.path.join(PROF, 'raw'))
    env = dict(os.environ, BIOSYS_BIN=BIN, LLVM_PROFILE_FILE=os.path.join(PROF, 'raw', 'p%8m.profraw'))
    for v in variants:
        kinds = []
        if ':' in v and v.startswith('apply'):
            v, k = v.split(':')
            kinds = ['--kinds', k]
        r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'sweep.py'), '--variant', v, '--out', os.path.join(PROF, 'sweeps')] + kinds,
                           cwd=ROOT, env=env, capture_output=True, text=True)
        print((r.stdout.strip().splitlines() or ['sweep %s: no output ' % v + r.stderr[-300:]])[-1], flush=True)


def demangle(names):
    p = subprocess.run(['xcrun', 'c++filt'], input='\n'.join(names) + '\n', capture_output=True, text=True)
    out = p.stdout.splitlines()
    return out if len(out) == len(names) else names


def evaluate(top):
    raw = glob.glob(os.path.join(PROF, 'raw', '*.profraw'))
    if not raw:
        raise SystemExit('no profiles in %s: run without --merge-only' % PROF)
    data = os.path.join(PROF, 'all.profdata')
    r = sh(['xcrun', 'llvm-profdata', 'merge', '-o', data] + raw)
    if r.returncode:
        raise SystemExit(r.stderr)
    srcs = sorted(glob.glob(os.path.join(ROOT, 'src', 'game', '*.cpp')))
    ex = sh(['xcrun', 'llvm-cov', 'export', '-format=text', '-instr-profile=' + data, BIN] + srcs)
    if ex.returncode:
        raise SystemExit(ex.stderr[-2000:])
    counts = {}                                                    # (file, mangled name) -> executed count
    for fn in json.loads(ex.stdout)['data'][0]['functions']:
        f = os.path.relpath(fn['filenames'][0], ROOT)
        counts[(f, fn['name'].split(':', 1)[-1] if ':' in fn['name'] and not fn['name'].startswith('_') else fn['name'])] = fn['count']
    rep = sh(['xcrun', 'llvm-cov', 'report', '-show-functions', '-instr-profile=' + data, BIN] + srcs)
    rows, cur = [], None
    for l in rep.stdout.splitlines():
        m = re.match(r"File '(.*)':", l)
        if m:
            cur = os.path.relpath(m.group(1), ROOT)
            continue
        if cur is None or l.startswith(('Name', '---', 'TOTAL')) or not l.strip():
            continue
        t = l.split()
        if len(t) < 10:
            continue
        name = t[0].split(':', 1)[-1] if not t[0].startswith('_') and ':' in t[0] else t[0]
        lines, miss = int(t[4]), int(t[5])
        rows.append((cur, name, lines, lines - miss))
    dem = demangle([r[1] for r in rows])
    out = []
    for (f, mangled, lines, cov), d in zip(rows, dem):
        out.append((f, d, counts.get((f, mangled), -1), cov, lines))
    out.sort()
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, 'coverage_functions.tsv')
    with open(path, 'w') as f:
        f.write('# file\tfunction\texecuted\tlines covered\tlines total\n')
        for r in out:
            f.write('%s\t%s\t%d\t%d\t%d\n' % r)
    nf, ne = len(out), sum(1 for r in out if r[2] > 0)
    tl, cl = sum(r[4] for r in out), sum(r[3] for r in out)
    print('\nsrc/game/*.cpp: %d functions, %d executed (%.1f%%); %d lines, %d covered (%.1f%%)  -> %s' % (
        nf, ne, 100.0 * ne / nf, tl, cl, 100.0 * cl / tl, os.path.relpath(path, ROOT)))
    never = [r for r in out if r[2] <= 0]
    byfile = collections.defaultdict(list)
    for r in never:
        byfile[r[0]].append(r)
    print('\nnever executed: %d functions, %d lines, by file (largest first)' % (len(never), sum(r[4] for r in never)))
    for f, rs in sorted(byfile.items(), key=lambda kv: -sum(r[4] for r in kv[1])):
        allf = [r for r in out if r[0] == f]
        print('%s: %d of %d functions never executed, %d lines' % (f, len(rs), len(allf), sum(r[4] for r in rs)))
        for r in sorted(rs, key=lambda r: -r[4])[:8]:
            print('    %4d lines  %s' % (r[4], r[1][:110]))
        if len(rs) > 8:
            print('    ... %d more' % (len(rs) - 8))
    print('\nthe %d largest never-executed functions:' % top)
    for r in sorted(never, key=lambda r: -r[4])[:top]:
        print('%5d lines  %s  %s' % (r[4], os.path.basename(r[0]), r[1][:120]))


def main():
    a = sys.argv[1:]
    variants, top, do_build, do_run = list(VARIANTS), 30, True, True
    i = 0
    while i < len(a):
        if a[i] == '--quick':
            variants = [v for v in variants if not v.startswith('apply')]; i += 1
        elif a[i] == '--variants':
            variants = a[i + 1].split(','); i += 2
        elif a[i] == '--top':
            top = int(a[i + 1]); i += 2
        elif a[i] == '--no-build':
            do_build = False; i += 1
        elif a[i] == '--merge-only':
            do_build = do_run = False; i += 1
        else:
            raise SystemExit('unknown argument ' + a[i])
    if do_build:
        build()
    if do_run:
        run_sweeps(variants)
    evaluate(top)


if __name__ == '__main__':
    main()
