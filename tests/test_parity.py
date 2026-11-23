"""Static comparison of the original game DLL with the port (tests/audit/parity.py): every game routine is ported or accounted for, and
every port routine uses the picture / sound / animation names and calls the routines its original does.

State of each finding, in tests/audit/parity_state.tsv (kind, address, text, verdict); a line in tests/audit/verdicts/*.tsv
(`P`, kind, address, text, verdict) overrides it:
  open            found, not looked at yet           -> counted, does not fail
  ok: REASON      looked at: the port is right       -> silent
A finding that is not in the file fails the test (something new is missing); a line whose finding no longer exists is reported as stale.
`python3 -I tests/test_parity.py --record` adds new findings as `open` and drops stale lines (verdicts are kept)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'audit'))
import portmap as pm
import parity

STATE = os.path.join(HERE, 'audit', 'parity_state.tsv')
if not pm.available():
    print('parity: SKIPPED (the analysis files are not present)')
    sys.exit(0)

state = {}
if os.path.exists(STATE):
    for l in open(STATE, errors='replace'):
        l = l.rstrip('\n')
        if l and not l.startswith('#'):
            p = l.split('\t')
            if len(p) >= 4:
                state['\t'.join(p[:3])] = p[3]
recorded = set(state)
# verdicts written by whoever looked at a finding: tests/audit/verdicts/*.tsv, lines `P <tab> kind <tab> address <tab> finding <tab> verdict`
VD = os.path.join(HERE, 'audit', 'verdicts')
for fn in sorted(os.listdir(VD)) if os.path.isdir(VD) else []:
    for l in open(os.path.join(VD, fn), errors='replace'):
        p = l.rstrip('\n').split('\t')
        if len(p) >= 5 and p[0] == 'P':
            state['\t'.join(p[1:4])] = p[4]
found = {}
for r in parity.findings():
    found[parity.key(r)] = r

if '--record' in sys.argv:
    with open(STATE, 'w') as f:
        f.write('# kind\taddress\tfinding\tverdict (open | ok: reason)        -- see tests/test_parity.py\n')
        for k in sorted(found, key=lambda k: (k.split('\t')[1], k)):
            f.write('%s\t%s\n' % (k, state.get(k, 'open')))
    print('parity: recorded %d findings (%d new, %d stale dropped)' % (len(found), len([k for k in found if k not in state]), len([k for k in state if k not in found])))
    sys.exit(0)

new = [k for k in found if k not in state]
stale = [k for k in recorded if k not in found]      # (a verdict file may keep lines for findings that are gone)
n_open = len([k for k in found if state.get(k) == 'open'])
bad = [k for k in found if k in state and state[k] != 'open' and not state[k].startswith('ok:')]
for k in new:
    r = found[k]
    print('NEW %s %08x %s: %s' % (r[0], r[1], r[2], r[3]))
for k in bad:
    print('BAD VERDICT (use `open` or `ok: reason`): %s' % k)
for k in stale[:40]:
    print('stale (fixed or changed; run --record): %s' % k.replace('\t', ' '))
print('parity: %d findings: %d accounted for, %d open, %d new, %d stale' % (len(found), len(found) - n_open - len(new), n_open, len(new), len(stale)))
sys.exit(1 if new or bad else 0)
