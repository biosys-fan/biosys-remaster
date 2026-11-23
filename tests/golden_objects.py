#!/usr/bin/env python3
"""Canonical dumps from the Python reference parsers (same format as `biosys-objdump`).
usage: golden_objects.py objects|touchinf|save FILE
Objects come straight from biosys_objects.Objects. The touchinf and sav reference scripts only print,
so their printed self-checks (MATCH/MISMATCH, OK/BAD) are run and asserted, and the fields are
re-read with struct for the canonical dump."""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import contextlib, importlib.util, io, struct, sys, zlib

SCRIPTS = (_DEV + '/analysis/scripts/')

def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS + name + '.py')
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, SCRIPTS)      # the scripts import biosys_pages from their own directory
    spec.loader.exec_module(m)
    return m

out = io.BytesIO()
def w(s):
    out.write(s.encode('latin-1') + b'\n')

def hold_line(tag, ident, k, h):
    w('%s %s %d image=%u x=%d y=%d flag=%u transp=%d group=%u' % ((tag, ident, k) + tuple(h)))

def normal_line(tag, ident, k, n):
    w('%s %s %d image=%u flag=%u transp=%d' % ((tag, ident, k) + tuple(n)))

def cs(d, off, n):
    b = d[off:off + n]
    z = b.find(b'\0')
    return (b if z < 0 else b[:z]).decode('latin-1')

def dump_objects(path):
    bo = load('biosys_objects')
    O = bo.Objects(path)
    O.classify()
    w('objects count=%d residentBase=%d residentSize=%d fileSize=%d' % (O.count, O.resident_img_base, O.resident_size, len(O.d)))
    for ob in O.objs:
        i = ob['index']
        w('obj %d name=[%s] tablename=[%s] off=%d size=%d nimg=%d nanim=%d nvar=%d oimg=%d oanim=%d ovar=%d unk18=%d' % (
            i, ob['name'], ob['table_name'], ob['offset'], ob['size'], ob['n_images'], ob['n_anims'], ob['n_vars'],
            ob['o_images'], ob['o_anims'], ob['o_vars'], ob['unk18']))
        for k, n in enumerate(ob['normal']): normal_line('normal', i, k, n)
        w('icon %d image=%d unk44=%d' % (i, ob['icon_image'], ob['unk44']))
        for k, h in enumerate(ob['holding']): hold_line('hold', i, k, h)
        w('holddelay %d %d' % (i, ob['hold_delay']))
        hold_line('wear', i, 0, ob['wearing'])
        for k, (img, nm) in enumerate(ob['named_images']): w('named %d %d image=%d name=[%s]' % (i, k, img, nm))
        w('misc %d short=[%s] moveable=%d randomzone=%d unk198=%d unk19c=%d wearable=%d fruit=%d unk1a8=%d initzone=%d initzonetype=%d initvar=[%s]' % (
            i, ob['name184'], ob['moveable'], ob['random_zone'], ob['unk198'], ob['unk19c'], ob['wearable'], ob['fruit'],
            ob['unk1a8'], ob['init_zone'], ob['init_zone_type'], ob['init_variation']))
        for k, a in enumerate(ob['anims']):
            e, x, y, u, dl = a['words']
            ao = ob['offset'] + ob['o_anims'] + 0x228 * k
            wav, avar = (cs(O.d, ao + 0x118, 0x104), cs(O.d, ao + 0x21c, 12))
            w('anim %d %d event=%d x=%d y=%d unk0c=%d delay=%d file=[%s] wav=[%s] var=[%s]' % (i, k, e, x, y, u, dl, a['file'], wav, avar))
        for k, v in enumerate(ob['vars']):
            vid = '%d.%d' % (i, k)
            unk28 = struct.unpack_from('<I', O.d, ob['offset'] + ob['o_vars'] + 0x120 * k + 0x28)[0]
            vshort = cs(O.d, ob['offset'] + ob['o_vars'] + 0x120 * k + 0x108, 12)
            w('var %s name=[%s] short=[%s] icon=%d unk28=%d holddelay=%d' % (vid, v['name'], vshort, v['icon_image'], unk28, v['hold_delay']))
            for j, n in enumerate(v['normal']): normal_line('varnormal', vid, j, n)
            for j, h in enumerate(v['holding']): hold_line('varhold', vid, j, h)
            hold_line('varwear', vid, 0, v['wearing'])
        for im in ob['images']:
            wd, ht, st, raw = O.pixels(ob, im['index'])
            px = b''.join(raw[y * st:y * st + wd] for y in range(ht - 1, -1, -1))
            fileoff = im['inline'] + ob['offset'] if im['data_off'] == -1 else im['file_off']
            w('img %d %d %dx%d stride=%d comp=%d bpp=%d dataoff=%d ptr=%d area=%s fileoff=%d crc=%08x' % (
                i, im['index'] + 1, wd, ht, st, im['compressed'], im['bpp'], im['data_off'], im['ptr'], im['area'],
                fileoff, zlib.crc32(px)))

def run_reference(fn, *args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        fn(*args)
    return buf.getvalue()

def dump_touchinf(path):
    bt = load('biosys_touchinf')
    cstr = bt.cstr
    sys.argv = ['biosys_touchinf.py', path]
    ref = run_reference(bt.main)
    if 'MISMATCH' in ref or ref.count('MATCH') < 2: w('REFERENCE SELF-CHECK FAILED')
    d = open(path, 'rb').read()
    w('touchinf size=%d' % len(d))
    w('tm sec=%d min=%d hour=%d mday=%d mon=%d year=%d wday=%d yday=%d isdst=%d' % struct.unpack_from('<9i', d, 0))
    o_info, o_pub, o_btn = struct.unpack_from('<3I', d, 0x24)
    w('offsets pubinfo=%d pubdata=%d buttons=%d' % (o_info, o_pub, o_btn))
    b = o_info
    v = struct.unpack_from('<13I', d, b)
    w('pubinfo version=%d book=%dx%d unk0c=%d release=%d firstpage=%d unk18=%d topy=%d unk20=%d unk24=%d statusbar=%dx%d pos=%d' % v)
    for tag, a, z in (('title', 0x34, 0x84), ('copyright', 0x84, 0xfc), ('font', 0xfc, 0x1fd), ('control', 0x1fd, 0x2fe), ('titlebmp', 0x2fe, 0x3ff)):
        w('%s [%s]' % (tag, cstr(d[b + a:b + z])))
    b = o_pub
    nz, ns, oz, os_ = struct.unpack_from('<4I', d, b)
    w('pubdata zones=%d styles=%d zoneoff=%d styleoff=%d' % (nz, ns, oz, os_))
    for i in range(nz):
        r = d[b + oz + 0x24 * i:b + oz + 0x24 * (i + 1)]
        pg, hs = struct.unpack_from('<II', r, 0)
        w('zone %d page=%d hotspot=%d pagename=[%s] hotspotname=[%s] type=%d' % (i, pg, hs, cstr(r[8:19]), cstr(r[19:32]), struct.unpack_from('<I', r, 32)[0]))
    for i in range(ns):
        r = d[b + os_ + 0x2c * i:b + os_ + 0x2c * (i + 1)]
        w('style %d height=%d rgb=%d,%d,%d face=[%s]' % (i, struct.unpack_from('<i', r, 0)[0], r[4], r[5], r[6], cstr(r[8:])))
    b = o_btn
    nb, ng, oi, si = struct.unpack_from('<4I', d, b)
    w('buttons count=%d backgrounds=%d imagedataoff=%d imagedatasize=%d' % (nb, ng, oi, si))
    p = b + 16
    for i in range(nb):
        w('button %d %s' % (i + 1, ' '.join('%x' % x for x in struct.unpack_from('<13I', d, p)))); p += 0x34
    for i in range(ng):
        w('background %d %s' % (i + 1, ' '.join('%x' % x for x in struct.unpack_from('<36I', d, p)))); p += 0x90

def dump_save(path):
    bs = load('biosys_sav')
    cstr = bs.cstr
    ref = run_reference(bs.parse, path)
    if 'MISMATCH' in ref or 'BAD' in ref: w('REFERENCE SELF-CHECK FAILED')
    d = open(path, 'rb').read()
    magic, version, blob = struct.unpack_from('<3I', d, 0)
    w('save size=%d magic=%08x version=%d blobOffset=%d blobSize=%d' % (len(d), magic, version, blob, len(d) - blob))
    unk5c, bs4 = struct.unpack_from('<II', d, 0x5c)
    w('title [%s]\nunk5c=%d bs4=%d\nprimarypage [%s]' % (cstr(d[0x0c:0x5c]), unk5c, bs4, cstr(d[0x64:0x70])))
    w('scroll %d,%d\npalettegrab=%d' % (struct.unpack_from('<ii', d, 0x70) + struct.unpack_from('<I', d, 0x78)))
    for i in range(256): w('pal %d %d,%d,%d,%d' % ((i,) + tuple(d[0x7c + 4 * i:0x80 + 4 * i])))
    w('unk47c=%d unk480=%d bytes484=%d,%d,%d,%d' % (struct.unpack_from('<II', d, 0x47c) + tuple(d[0x484:0x488])))
    w('pending words=%s page=[%s] hotspot=[%s] flag=%d' % (','.join(str(x) for x in struct.unpack_from('<10I', d, 0x488)),
      cstr(d[0x4b0:0x4bb]), cstr(d[0x4bb:0x4c8]), struct.unpack_from('<i', d, 0x4c8)[0]))
    f1, f2, f3, u = struct.unpack_from('<4I', d, 0x4cc)
    w('flags4cc=%08x flags4d0=%08x flags4d4=%08x unk4d8=%d' % (f1, f2, f3, u))
    w('string4dc [%s]\nunk540=%d unk544=%d' % ((cstr(d[0x4dc:0x540]),) + struct.unpack_from('<II', d, 0x540)))
    p = 0x548
    if bs4:
        w('objects section absent (bs4)')
    else:
        n = struct.unpack_from('<I', d, p)[0]; p += 4
        nm = unk = nbar = 0
        if n: nm, unk, nbar = struct.unpack_from('<3I', d, p); p += 12
        w('objects count=%d moveable=%d zones=%d statusbar=%d' % (n, nm, unk, nbar))
        for i in range(n):
            r = d[p:p + 0x58]; p += 0x58
            st, u198, u19c = struct.unpack_from('<IIi', r, 0x0c)
            g1, g2 = struct.unpack_from('<II', r, 0x44)
            w('sobj %d name=[%s] status=%d u198=%d u19c=%d cur=[%s],[%s] home=[%s],[%s] group=%d/%d variation=[%s]' % (
                i, cstr(r[0:12]), st, u198, u19c, cstr(r[0x18:0x23]), cstr(r[0x23:0x2e]), cstr(r[0x2e:0x39]),
                cstr(r[0x39:0x44]), g1, g2, cstr(r[0x4c:0x58])))
        for i in range(nbar):
            w('bar %d [%s]' % (i, cstr(d[p:p + 11]))); p += 11
    w('sectionsEnd=%d' % p)

if __name__ == '__main__':
    {'objects': dump_objects, 'touchinf': dump_touchinf, 'save': dump_save}[sys.argv[1]](sys.argv[2])
    sys.stdout.buffer.write(out.getvalue())
