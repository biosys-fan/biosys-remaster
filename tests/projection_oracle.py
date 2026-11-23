#!/usr/bin/env python3
"""Independent oracle of the BioSys.exe cylindrical panorama projection.

Written only from the Ghidra text and the disassembly of BioSys.exe
(exe:00445a70 setup, 0040cf00 tables, 0041a03c line renderer, 00443f30 blit,
00403470 inverse, 00403850 heading).  Pure stdlib; run with `python3 -I`.

Findings ([V] = read in the code, [G] = guessed):
 Setup (exe:00445a70, window fields this+0x2e8..0x2f8)
  [V] 2e8 = fov as double; 2f0 = ftol(tan(fov*0.5*0.0174532925)*407.4)  (yaw offset
      added to the scroll x); 2f4 = ftol(fov) << 10 (int, fov in 1/1024 deg, the
      fraction of fov is dropped); 2f8 = ftol(maxpitch*1024*(1/328)) with
      maxpitch = max(0, 51.48 - atan(368/(2*scale))*57.29577957855229),
      scale = max(320/tan(fov*0.00872664625), 184/tan(0.8984954979)).
 Blit (exe:00443f30, 2560x1024 branch) [V]
  yaw = 2f0 + scrollx ; pitch = 2f8 * (scrolly - 0x148) (32-bit imul) ; fov = 2f4;
  param block 0x499108 = {src surface, dst view, yaw, pitch, fovint}; 0x40cf00 called.
 Tables (exe:0040cf00) [V]
  yaw: one wrap only (if > 0x500: -0xa00; if <= -0x500: +0xa00), written back to
  param[2]; pitchdeg = pitch/1024 clamped to +-maxpitch (same formula as above, scale
  from fov/1024), written back ftol(pitchdeg*1024) to param[3] (these are globals
  0x499110/0x499114 used by the inverse).  A = cos(p)*scale, B = sin(p)*scale
  (p = pitchdeg*0.0174532925, cos/sin stored as doubles).
  13 key rows k=0..12 <-> r = -184+32k, 21 columns j=0..20 <-> c = -320+32j.
   D = A - sin*r (stored as double), E = D*D (double), Fi = ftol(r*cos + B),
   G = Fi*407.4366547808163 (double)
   X = ftol((atan2(c, D)*407.4366547808163 + yaw)*65536)       (16.16 page x)
   Y = ftol((G/sqrt(c*c + E) + 512.0)*65536)                   (16.16 page y)
  ftol = fistp with truncation (toward zero), low 32 bits kept.
  Band k (k=0..11) renders screen line 32k from key row k, and lines 32k+i,
  i=1..min(367-32k,31), from interpolated row i: v0 + i*((v1-v0)>>5) per entry
  (32-bit wrap, arithmetic shift, X and Y separately).  368 lines in all.
 Line renderer (exe:0041a03c) [V]
  20 segments, j=19 first, segment j produces pixels 32j..32j+31 between table
  columns j (left, X0,Y0) and j+1 (right, X1,Y1) of the row; pixels are fetched
  right to left (fetch n -> pixel 32j+31-n); 4 fetches are packed per dword b0<<24..
  X1' = X1+0xa000000 if signed (X1+0xa000000) < 0xa000000 (i.e. X1 < 0: a whole
  page wrap), else X1 and slow flag = (X0 < 0).
  st = (X0-X1)>>5; dx = st>>16 ; S = ((st<<16)&0xff000000) + (((Y0-Y1)>>5>>7)&0x7ffff)
  P0 = ((X1'<<16)&0xff000000) + ((Y1>>7)&0x7ffff);  column ecx0 = X1'>>16
  Q0 = P0, Q1 = P0 + (S&0x7ffff) (pre-step), Q(n+1) = Q(n)+S (32 bit)
  fetch n reads surface[ecx_n + ((Q_n & 0x7fe00)*5)]  (row = bits 9..18 of Q, mod 1024)
  ecx_n = ecx_(n-1) + dx + carry(Q_n + S)  (adc; n>=1).  The x fraction (top byte)
  of Q carries into ecx; bits 19..23 of Q are garbage from y wraps and can carry into
  the x fraction after many steps (reproduced by exact 32-bit arithmetic).
  Fast path (flag 0): x is never wrapped (x<0 / x>=2560 reads neighbouring rows).
  Slow path (flag 1, X0<0<=X1: segment straddles page x=0), 8 dwords of 4 fetches:
   ecx_(4d+1..4d+3) are wrapped (+0xa00 when < 0) BEFORE their fetch, fetch 4d uses
   the already wrapped ecx; QUIRK: fetch 4d+3 uses ecx_(4d+4) (advanced one more step,
   not yet wrapped) with the row of Q_(4d+3); ecx_(4d+4) is wrapped afterwards.
   Reads outside the surface return 0 here [G: the original reads raw memory].
 Inverse (exe:00403470, 2560x1024 branch) [V]
  rp = pitch_global*1.7044230957031252e-05, Sc = 320/tan(fov*0.00872664625) (NO
  max() with the 184 term here), v = sy-184, hx = sx-320, cp=cos rp, sp=sin rp
  Fv = v*cp + sp*Sc, Dv = cp*Sc - sp*v  (both stored as doubles)
  px = ftol(atan(hx/Dv)*407.4 + yaw_global) (note atan, not atan2; const 407.4, not
  407.4366547808163); if px<0: px+=2560; if px>2560: px-=2560
  py = ftol(Fv/sqrt(hx*hx+Dv*Dv)*407.4 + 512.0)
  heading (exe:00403850) = px of inverse(320, 0)   [V]
 Not modelled [G]: x87 80-bit intermediates (everything is double); a trig result
  differing by 1 ulp may flip a truncation.  Globals start as yaw=pitch=0.
"""
import math
import sys

M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def ftol(v):
    """exe:00461968: fistp with round-to-zero, eax = low 32 bits (signed here)."""
    try:
        return s32(int(v))
    except (OverflowError, ValueError):
        return -0x80000000


# constants, .rdata addresses in comments
K_1_1024 = 0.0009765625            # 0x47b040
K_HALFRAD = 0.008726646250000001   # 0x47b048 / 0x47b5d0 / 0x47b020
K_320 = 320.0                      # 0x47b050
K_ATAN_D = 0.8984954979            # 0x47b058
K_184 = 184.0                      # 0x47b060
K_368 = 368.0                      # 0x47b070
K_RAD2DEG_NEG = -57.29577957855229  # 0x47b078
K_RAD2DEG = 57.29577957855229      # 0x47b600
K_51_48 = 51.48                    # 0x47b080
K_1024 = 1024.0                    # 0x47b090
K_DEGRAD = 0.017453292500000002    # 0x47b098 / 0x47b5c0
K_2560_2PI = 407.4366547808163     # 0x47b0a0
K_65536 = 65536.0                  # 0x47b0a8
K_512 = 512.0                      # 0x47b0b0
K_407_4 = 407.4                    # 0x47b030 / 0x47b5c8
K_PITCH_RAD = 1.7044230957031252e-05  # 0x47b018
K_INV_328 = 0.003048780487804878   # 0x47b620
K_HALF = 0.5                       # 0x47b5b8

ROWS_KEY = 13
COLS = 21
VIEW_W, VIEW_H = 640, 368
PAGE_W, PAGE_H = 2560, 1024


def _scale_and_maxpitch(fovdeg):
    """320/tan(..) vs 184/tan(..) max, then the pitch limit (same in 445a70 / 40cf00)."""
    s1 = K_320 / math.tan(fovdeg * K_HALFRAD)
    d2 = K_184 / math.tan(K_ATAN_D)
    scale = s1
    if not (d2 <= s1):
        scale = d2
    maxp = math.atan2(K_368 / (scale * 2.0), 1.0) * K_RAD2DEG_NEG + K_51_48
    if maxp < 0.0:
        maxp = 0.0
    return scale, maxp


def build_tables(yaw, pitch, fovint):
    """exe:0040cf00 first half.  Returns (yaw, pitch, key_rows[13][21] of (X,Y))."""
    if yaw > 0x500:
        yaw -= 0xA00
    if yaw < -0x4FF:           # cmp eax,0xfffffb00 ; jg skip  => yaw <= -0x500
        yaw += 0xA00
    fovdeg = s32(fovint) * K_1_1024
    pitchdeg = s32(pitch) * K_1_1024
    scale, maxp = _scale_and_maxpitch(fovdeg)
    if maxp < pitchdeg:
        pitchdeg = maxp
    if not (-maxp <= pitchdeg):
        pitchdeg = -maxp
    pitch_out = ftol(pitchdeg * K_1024)
    r = pitchdeg * K_DEGRAD
    cosv = math.cos(r)
    sinv = math.sin(r)
    A = cosv * scale
    B = sinv * scale
    keys = []
    for k in range(ROWS_KEY):
        rr = -184 + 32 * k
        D = A - sinv * rr
        E = D * D
        G = ftol(rr * cosv + B) * K_2560_2PI
        row = []
        for j in range(COLS):
            c = -320 + 32 * j
            X = ftol((math.atan2(c, D) * K_2560_2PI + yaw) * K_65536)
            Y = ftol((G / math.sqrt(c * c + E) + K_512) * K_65536)
            row.append((X, Y))
        keys.append(row)
    return yaw, pitch_out, keys


def line_rows(keys):
    """Per screen line (0..367) the 21 (X,Y) entries the line renderer is given."""
    lines = []
    for k in range(ROWS_KEY - 1):
        r0, r1 = keys[k], keys[k + 1]
        lines.append(r0)
        n = min(367 - 32 * k, 31)
        steps = [((s32(r1[j][0] - r0[j][0]) >> 5), (s32(r1[j][1] - r0[j][1]) >> 5))
                 for j in range(COLS)]
        for i in range(1, n + 1):
            lines.append([(s32(r0[j][0] + i * steps[j][0]), s32(r0[j][1] + i * steps[j][1]))
                          for j in range(COLS)])
    return lines


def render_line(surf, row, out, off):
    """exe:0041a03c for one line; out[off..off+639] receives the pixels."""
    n_surf = len(surf)
    for j in range(19, -1, -1):
        X0, Y0 = row[j]
        X1, Y1 = row[j + 1]
        eax = s32(X1 + 0xA000000)
        slow = False
        if eax >= 0xA000000:
            eax = s32(eax - 0xA000000)
            slow = X0 < 0
        st = s32(X0 - X1) >> 5
        dx = st >> 16
        S = (((st << 16) & 0xFF000000) + ((s32(Y0 - Y1) >> 5 >> 7) & 0x7FFFF)) & M32
        P0 = (((eax << 16) & 0xFF000000) + ((Y1 >> 7) & 0x7FFFF)) & M32
        ecx0 = eax >> 16
        Q = [0] * 34
        C = [0] * 34
        Q[0] = P0
        Q[1] = (P0 + (S & 0x7FFFF)) & M32
        for n in range(1, 33):
            t = Q[n] + S
            C[n] = t >> 32
            Q[n + 1] = t & M32
        px = [0] * 32

        def get(ecx, q):
            i = ecx + (q & 0x7FE00) * 5
            return surf[i] if 0 <= i < n_surf else 0

        if not slow:
            ecx = ecx0
            px[0] = get(ecx, Q[0])
            for n in range(1, 32):
                ecx += dx + C[n]
                px[n] = get(ecx, Q[n])
        else:
            ecx = ecx0
            for d in range(8):
                n = 4 * d
                px[n] = get(ecx, Q[n])
                ecx += dx + C[n + 1]
                if ecx < 0:
                    ecx += 0xA00
                px[n + 1] = get(ecx, Q[n + 1])
                ecx += dx + C[n + 2]
                if ecx < 0:
                    ecx += 0xA00
                px[n + 2] = get(ecx, Q[n + 2])
                ecx += dx + C[n + 3]
                if ecx < 0:
                    ecx += 0xA00
                ecx += dx + C[n + 4]          # adc happens BEFORE the 4th fetch
                px[n + 3] = get(ecx, Q[n + 3])  # quirk: column of fetch n+4, row of n+3
                if ecx < 0:
                    ecx += 0xA00
        base = off + 32 * j + 31
        for n in range(32):
            out[base - n] = px[n]


class Projection:
    def __init__(self, fov=75.0):
        self.yaw = 0      # global 0x499110 (after wrap)
        self.pitch = 0    # global 0x499114 (after clamp, 1/1024 deg)
        self.set_fov(fov)

    def set_fov(self, fov):
        """exe:00445a70."""
        self.fov = float(fov)                                                # this+0x2e8
        self.yaw_offset = ftol(math.tan(self.fov * K_HALF * K_DEGRAD) * K_407_4)  # +0x2f0
        self.fov_int = s32(ftol(self.fov) << 10)                             # +0x2f4
        # 445a70 uses the double fov directly (not the truncated one)
        scale, maxp = _scale_and_maxpitch(self.fov)
        self.pitch_step = ftol(maxp * K_1024 * K_INV_328)                    # +0x2f8

    def _prepare(self, x, y):
        yaw = s32(self.yaw_offset + x)
        pitch = s32(self.pitch_step * (y - 0x148))
        yaw, pitch, keys = build_tables(yaw, pitch, self.fov_int)
        self.yaw, self.pitch = yaw, pitch
        return keys

    def render(self, surface, x, y):
        keys = self._prepare(x, y)
        lines = line_rows(keys)
        out = bytearray(VIEW_W * VIEW_H)
        for ln in range(VIEW_H):
            render_line(surface, lines[ln], out, ln * VIEW_W)
        return bytes(out)

    def inverse(self, sx, sy):
        """exe:00403470, 2560x1024 branch."""
        rp = self.pitch * K_PITCH_RAD
        v = float(sy - 0xB8)
        hx = float(sx - 0x140)
        Sc = K_320 / math.tan(self.fov * K_HALFRAD)
        cp, sp = math.cos(rp), math.sin(rp)
        Fv = v * cp + sp * Sc
        Dv = cp * Sc - sp * v
        if Dv == 0.0:
            ratio = math.copysign(math.inf, hx) if hx != 0.0 else 0.0
        else:
            ratio = hx / Dv
        px = ftol(math.atan(ratio) * K_407_4 + self.yaw)
        if px < 0:
            px += 0xA00
        if px > 0xA00:
            px -= 0xA00
        den = math.sqrt(hx * hx + Dv * Dv)
        py = ftol(Fv / den * K_407_4 + K_512) if den != 0.0 else -0x80000000
        return px, py

    def heading(self):
        """exe:00403850: page x of the view point (320, 0)."""
        return self.inverse(320, 0)[0]


def zoom_out_schedule(x0, y0, dx, dy, fov0=72, step=3, steps=12):
    """exe:00445f80 (the return from a close-up page): [(x, y, fov)] of the frames in drawing order, written from the decompiled text.
    dx, dy are the float32 values of the return data (exe:00445cc0). Start: x0 + ftol(steps * dx) at fov (1 - steps) * step + fov0; the first
    picture is drawn by the page change; then k = steps-1 .. 1: scroll x0 + ftol(k * dx), fov start + step * (steps - k); the last picture is at (x0, y0)
    with fov 75 (the yaw offset of the zoom is kept for all but the last, which is set up by exe:00445a70 again)."""
    start = (1 - steps) * step + fov0
    frames = [((x0 + ftol(steps * dx)) % 0xA00, y0 + ftol(steps * dy), start)]
    fov = start + step
    for k in range(steps - 1, 0, -1):
        frames.append(((x0 + ftol(k * dx)) % 0xA00, y0 + ftol(k * dy), fov))
        fov += step
    frames.append((x0, y0, 75.0))
    return frames


def compass_sector(heading, width, start_x):
    """exe:004038f0 / exe:00401050: the sector 1..8 announced for the page x `heading` of the view centre (event 22)."""
    f = ((heading - (start_x - width * 0.0625)) / width) * 8.0
    if f < 0.0:
        f += 8.0
    if f >= 8.0:
        f -= 8.0
    return ftol(f) + 1


def _selfcheck():
    surf = bytearray(PAGE_W * PAGE_H)
    for r in range(PAGE_H):
        base = r * PAGE_W
        for c in range(PAGE_W):
            surf[base + c] = (c // 16 + r // 8) % 251
    surf = bytes(surf)
    import time
    for fov in (75.0, 128.25, 100.0):
        p = Projection(fov)
        print("fov", fov, "fields: yaw_off", p.yaw_offset, "fov_int", p.fov_int,
              "pitch_step", p.pitch_step)
        for x in (0, 700, 1279, 1280, 2559):
            for y in (0, 328, 656):
                t = time.time()
                out = p.render(surf, x, y)
                assert len(out) == VIEW_W * VIEW_H
                nz = sum(1 for b in out if b)
                print("  x=%4d y=%3d ok  yaw=%5d pitch=%6d heading=%4d nonzero=%d inv(320,184)=%s %.2fs"
                      % (x, y, p.yaw, p.pitch, p.heading(), nz, p.inverse(320, 184),
                         time.time() - t))
    # geometric sanity: the view centre shows page column ~ yaw (pitch 0)
    p = Projection(75.0)
    out = p.render(surf, 1000, 328)
    cx = out[184 * VIEW_W + 320]
    px, py = p.inverse(320, 184)
    print("centre pixel", cx, "inverse", (px, py), "expected col//16 + row//8 ~",
          (px // 16 + py // 8) % 251)
    assert abs((px - p.yaw) % PAGE_W) <= 1 and abs(py - 512) <= 2
    print("selfcheck OK")


def _main(argv):
    if argv[0] == "render":
        src, x, y, dst = argv[1], int(argv[2]), int(argv[3]), argv[4]
        fov = float(argv[5]) if len(argv) > 5 else 75.0
        with open(src, "rb") as f:
            surf = f.read()
        out = Projection(fov).render(surf, x, y)
        with open(dst, "wb") as f:
            f.write(out)
    elif argv[0] == "inverse":
        x, y = int(argv[1]), int(argv[2])
        rest = argv[3:]
        fov = 75.0
        if rest and "," not in rest[0]:
            fov = float(rest[0])
            rest = rest[1:]
        p = Projection(fov)
        p._prepare(x, y)   # same state as after a render at (x, y)
        for tok in rest:
            sx, sy = (int(t) for t in tok.split(","))
            px, py = p.inverse(sx, sy)
            print("%d,%d -> %d,%d" % (sx, sy, px, py))
    else:
        raise SystemExit("usage: render SURF X Y OUT [FOV] | inverse X Y [FOV] SX,SY ...")


if __name__ == "__main__" and len(sys.argv) == 1:
    _selfcheck()
elif __name__ == "__main__":
    _main(sys.argv[1:])
