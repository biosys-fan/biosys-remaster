"""Coverage of the original's virtual methods. Three times a machine of the game did not behave as in the original because one of its
virtual methods had never been ported (the lift's redraw, the fuse box's display, the water tanks' tap and shower): nothing referred to the
missing function, so nothing failed. This test reads the virtual tables of BiosGame.dll (the tables a constructor in the decompiler listing
assigns), and requires that every game function in them is mentioned by address in port/src, i.e. somebody ported it or wrote down why not.
Excluded: slot +4 (the deleting destructor of every class), functions of 12 bytes or less (they return a constant), the C runtime / MFC part
of the DLL (from 0x10078000), and the methods listed in LOG_ONLY, which only write the object's members to the debug log (Eng_WriteLog).
Skipped (exit 0, with a note) when the analysis files are not there."""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.dirname(ROOT)
DLL = None
for root, _, fs in os.walk(os.path.join(BASE, 'analysis', 'bin')):
    for f in fs:
        if f.lower() == 'biosgame.dll':
            DLL = os.path.join(root, f)
LISTING = os.path.join(BASE, 'analysis', 'decompiled', 'BiosGame.dll.c')
FUNCS = os.path.join(BASE, 'analysis', 'decompiled', 'BiosGame.dll.functions.tsv')
if not DLL or not os.path.exists(LISTING) or not os.path.exists(FUNCS):
    print('vtables: SKIPPED (the analysis files are not present)')
    sys.exit(0)

# methods that only dump members to the log (CFeature::Dump and the like, vtable +0x1c / +0x2c of their classes) and are not ported
LOG_ONLY = {
    0x10006140: 'FX dump (m_bOn ...)', 0x1000aac0: 'SpotFX / SpotWav / SpotAnim / SpotAvi dump', 0x1000eee0: 'CWaterTank::Dump',
    0x10016dc0: 'CHeater::Dump', 0x10026630: 'CTankSensor dump', 0x10027b50: 'COceanProbe::Dump', 0x100297d0: 'COcean::Dump',
    0x1005ac10: 'CAirFlow::Dump', 0x1005cf60: 'CWaterFlow::Dump', 0x1005dc10: 'CBiomeSensor::Dump', 0x10062900: 'CVent dump',
}
# small methods looked at one by one on 2026-11-22
REVIEWED = {
    0x100061b0: 'FX: clears its on-flag when the id matches (the port switches FX by id in cworld::setFXState)',
    0x100061d0: 'FX: 13 bytes, returns a member',
    0x1000f350: 'page classes: 27 bytes, vtable +0x18',
    0x10007cc0: 'CExtinguisher +0x54: only calls the base method dll:10028f10',
    0x10028080: 'COceanProbe +0x38: only calls CFeature::OnPageDrawn dll:10019cd0',
    0x10078180: 'CRainFX / dll:10077e40 +0x20: in the C runtime range boundary, 53 bytes',
}

d = open(DLL, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def off(t):
    for vs, va, rs, ro in secs:
        if base + va <= t < base + va + max(vs, rs):
            return ro + t - base - va
    return None


funcs = {}
for l in open(FUNCS):
    p = l.rstrip('\n').split('\t')
    try:
        funcs[int(p[0], 16)] = (p[1], int(p[2]))
    except (ValueError, IndexError):
        pass
vts, cur = {}, None
for l in open(LISTING, errors='replace'):
    m = re.match(r'// ===== ([0-9a-f]{8}) (\S+)', l)
    if m:
        cur = m.group(2)
        continue
    m = re.search(r'= &PTR_(?:FUN|LAB)_([0-9a-f]{8});', l)
    if m and cur:
        vts.setdefault(int(m.group(1), 16), cur)
port = ''
for root, _, fs in os.walk(os.path.join(ROOT, 'src')):
    for f in fs:
        if f.endswith(('.cpp', '.h', '.inc', '.md')):
            port += open(os.path.join(root, f), errors='replace').read().lower()
missing, checked = {}, 0
for vt, owner in sorted(vts.items()):
    o = off(vt)
    if o is None:
        continue
    for i in range(64):
        a = struct.unpack_from('<I', d, o + 4 * i)[0]
        if a not in funcs or (i > 0 and (vt + 4 * i) in vts):
            break
        size = funcs[a][1]
        if i == 1 or size <= 12 or a >= 0x10078000 or a in LOG_ONLY or a in REVIEWED:
            continue
        checked += 1
        if ('%08x' % a) not in port:
            missing.setdefault(a, []).append('%s +0x%x (%d bytes)' % (owner, 4 * i, size))
for a, us in sorted(missing.items()):
    print('FAIL dll:%08x is in a virtual table and nowhere in port/src: %s' % (a, ', '.join(us[:3])))
print('vtables: %d tables, %d method slots checked, %s' % (len(vts), checked, 'all mentioned' if not missing else '%d methods MISSING' % len(missing)))
sys.exit(1 if missing else 0)
