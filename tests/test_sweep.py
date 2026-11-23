"""The interaction sweep (tests/audit/sweep.py) against its recorded state: every page entered, every hotspot clicked.

tests/audit/sweep_<variant>.tsv holds, per interaction: page, hotspot, class, digest of what happened, verdict.
  verdict for the classes crash / stuck / dead / unreached:   open (not looked at yet)   or   ok: REASON
Rows are keyed by (where, what): (page, hotspot) for the page sweeps, (page, OBJECT@zone) for `take`, (OBJECT, use|stash|drop) for `use`,
(PAGE|OBJECT, rclick|use+rclick) for `look`, (page, hotspot|OBJECT) for `apply` (on demand, only the rows that differ from the empty-handed click).
The test fails when an interaction's class or digest differs from the record (a machine now behaves differently: look, then --record),
when an interaction is missing from the record, or when a verdict is malformed. Open verdicts are counted, they do not fail.
`python3 -I tests/test_sweep.py --record [VARIANT...]` rewrites the record from a fresh sweep, keeping the verdicts."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SUSPECT = ('crash', 'stuck', 'dead', 'unreached')


def load(path, verdicts=False):
    d = {}
    if os.path.exists(path):
        for l in open(path, errors='replace'):
            if l.startswith('#') or not l.strip():
                continue
            p = l.rstrip('\n').split('\t')
            d[(p[0], p[1])] = p
    return d


ON_DEMAND = ('apply',)        # variants whose sweep takes minutes: only run when named (`python3 -I tests/test_sweep.py apply`)


def variants():
    v = [f[len('sweep_'):-len('.tsv')] for f in sorted(os.listdir(os.path.join(HERE, 'audit'))) if f.startswith('sweep_') and f.endswith('.tsv')]
    v = [x for x in v if not x.startswith(ON_DEMAND)]
    return v or ['base']


# verdicts written by whoever looked at an interaction: tests/audit/verdicts/*.tsv, lines `S <tab> page <tab> hotspot <tab> verdict`
# (they hold for every variant in which that interaction is suspect, and override the record's own verdict)
over = {}
VD = os.path.join(HERE, 'audit', 'verdicts')
for fn in sorted(os.listdir(VD)) if os.path.isdir(VD) else []:
    for l in open(os.path.join(VD, fn), errors='replace'):
        p = l.rstrip('\n').split('\t')
        if len(p) >= 4 and p[0] == 'S':
            over[(p[1], p[2])] = p[3]

record = '--record' in sys.argv
names = [a for a in sys.argv[1:] if not a.startswith('--')] or variants()
fail = 0
for v in names:
    out = os.path.join(ROOT, 'build', 'audit')
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'audit', 'sweep.py'), '--variant', v.replace('hold_', 'hold:'), '--out', out], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        print('sweep %s: FAILED to run' % v)
        fail = 1
        continue
    fresh = load(os.path.join(out, 'sweep_%s.tsv' % v))          # page, hotspot, class, digest, note, failed, signature
    gpath = os.path.join(HERE, 'audit', 'sweep_%s.tsv' % v)
    gold = load(gpath)                                            # page, hotspot, class, digest, verdict, note, signature
    if record:
        with open(gpath, 'w') as f:
            f.write('# page\thotspot\tclass\tdigest\tverdict\tnote\tsignature (shortened)      -- see tests/test_sweep.py\n')
            for k in sorted(fresh):
                p = fresh[k]
                old = gold.get(k)
                verdict = ''
                if p[2] in SUSPECT:
                    verdict = over.get(k) or (old[4] if old and old[2] == p[2] and old[4] else 'open')
                f.write('\t'.join([p[0], p[1], p[2], p[3], verdict, p[4] + ('; FAILED OPEN: ' + p[5] if p[5] else ''), p[6][:240]]) + '\n')
        print('sweep %s: recorded %d interactions' % (v, len(fresh)))
        continue
    diff, n_open, n_fail_open = 0, 0, 0
    for k in sorted(fresh):
        p, g = fresh[k], gold.get(k)
        if p[5]:
            n_fail_open += 1
            print('FAILED OPEN %s %s: %s' % (p[0], p[1], p[5]))
        if g is None:
            diff += 1
            print('NOT IN THE RECORD %s %s: %s %s' % (p[0], p[1], p[2], p[6][:160]))
        elif g[2] != p[2] or g[3] != p[3]:
            diff += 1
            if diff <= 60:
                print('CHANGED %s %s: was %s, now %s\n    was: %s\n    now: %s' % (p[0], p[1], g[2], p[2], g[6][:200], p[6][:200]))
        elif p[2] in SUSPECT:
            vd = over.get(k, g[4])
            if vd == 'open':
                n_open += 1
            elif not vd.startswith('ok:'):
                diff += 1
                print('BAD VERDICT %s %s: "%s" (use `open` or `ok: reason`)' % (p[0], p[1], vd))
    gone = [k for k in gold if k not in fresh]
    for k in gone[:20]:
        print('GONE %s %s' % k)
    cls = {}
    for p in fresh.values():
        cls[p[2]] = cls.get(p[2], 0) + 1
    print('sweep %s: %d interactions (%s); %d differ from the record, %d gone, %d failed file opens, %d suspect ones still open' % (
        v, len(fresh), ', '.join('%s %d' % kv for kv in sorted(cls.items())), diff, len(gone), n_fail_open, n_open))
    if diff or gone or n_fail_open:
        fail = 1
sys.exit(fail)
