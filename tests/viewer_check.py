"""Helper for test_viewer.sh: compare 8-bit BMPs by palette index."""
import struct, sys

def read_bmp(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    bpp, = struct.unpack_from('<H', d, 28)
    assert d[:2] == b'BM' and bpp == 8, path
    stride = (w + 3) & ~3
    rows = [d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w] for y in range(h)]
    return w, h, rows

def main():
    cmd, shot, ref = sys.argv[1:4]
    sw, sh, srows = read_bmp(shot)
    rw, rh, rrows = read_bmp(ref)
    if (sw, sh) != (640, 480):
        print("screenshot size %dx%d" % (sw, sh)); return 1
    if cmd == "crop":        # view rows 56..423 == ref crop starting at ref x0 (wrapped), y0
        x0, y0 = int(sys.argv[4]), int(sys.argv[5])
        for y in range(368):
            exp = bytes(rrows[y0 + y][(x0 + x) % rw] for x in range(640))
            if srows[56 + y] != exp:
                x = next(i for i in range(640) if srows[56 + y][i] != exp[i])
                print("mismatch at view row %d col %d" % (y, x)); return 1
        return 0
    if cmd == "proj":        # 2560 x 1024 page: the view is the cylindrical projection (exe:0040cf00) of the reference image at scroll x0, y0
        sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
        import projection_oracle as po
        x0, y0 = int(sys.argv[4]), int(sys.argv[5])
        exp = po.Projection(75.0).render(b''.join(rrows), x0, y0)
        for y in range(368):
            if srows[56 + y] != exp[y * 640:(y + 1) * 640]:
                print("mismatch at view row %d" % y); return 1
        return 0
    if cmd == "full":
        if (rw, rh) != (640, 480): print("ref size %dx%d" % (rw, rh)); return 1
        return 0 if srows == rrows else 1
    return 2

sys.exit(main())
