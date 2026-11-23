// Event_OnBlitPage (dll:10022530): the rain overlay. The engine calls it after every page blit while the page filter is on
// (API 0x32, begun by the rain FX with the colours 0x76 / 0x56 / 0x57). 217 thin drops (a streak of 5 pixels, colour a) and 15 long
// drops (16 rows: 3 pixels wide in the middle, 1 pixel wide at the ends, colours c / b) fall through the view, each at its own speed;
// a drop that leaves the view starts again at the top. The original keeps the drops in DLL statics (DAT_100a1f88 .. DAT_100a2a7c);
// here they are members of BiosGame, filled on the first call with _rand() exactly as the original does (so the first picture
// depends on the generator's state).
#include "game/biosgame.h"
#include <cstring>

namespace biosys::game {

namespace {
// the x start positions of the 217 thin drops: DLL .data, first short of each 12-byte entry from 0x100a1f90
const int16_t kThinX[217] = {
    10, 15, 18, 22, 24, 26, 30, 35, 45, 47, 50, 52, 57, 60, 62, 63,
    65, 68, 70, 75, 77, 80, 82, 84, 86, 89, 91, 93, 94, 98, 104, 107,
    110, 113, 115, 118, 120, 124, 129, 131, 135, 137, 140, 142, 147, 150, 152, 157,
    160, 162, 163, 165, 168, 170, 175, 177, 180, 182, 184, 186, 189, 191, 193, 194,
    198, 204, 207, 210, 213, 215, 218, 220, 224, 229, 231, 235, 237, 240, 242, 247,
    250, 252, 257, 260, 262, 263, 265, 268, 270, 275, 277, 280, 282, 284, 286, 289,
    291, 293, 294, 298, 304, 307, 310, 313, 315, 318, 320, 324, 329, 331, 335, 337,
    340, 342, 347, 350, 352, 357, 360, 362, 363, 365, 368, 370, 375, 377, 380, 382,
    384, 386, 389, 391, 393, 394, 398, 404, 407, 410, 413, 415, 418, 420, 424, 429,
    431, 435, 437, 440, 442, 447, 450, 452, 457, 460, 462, 463, 465, 468, 470, 475,
    477, 480, 482, 484, 486, 489, 491, 493, 494, 498, 504, 507, 510, 513, 515, 518,
    520, 524, 529, 531, 535, 537, 540, 542, 547, 550, 552, 557, 560, 562, 563, 565,
    568, 570, 575, 577, 580, 582, 584, 586, 589, 591, 593, 594, 598, 604, 607, 610,
    613, 615, 618, 620, 624, 629, 631, 635, 637
};
constexpr int kRow = 0x280;   // the view is 640 bytes wide
}

// dll:10022530. Returns true (the filter handled the picture). With a rectangle list (never set by the DLL, API 0x5b) a pixel is only
// drawn inside one of the rectangles; `bufLen` is taken as their number (the original's param_4).
bool BiosGame::onBlitPage(u8* dst, u8* dstEnd, void* buf, int bufLen, u8 p5, u8 p6, u8 p7) {
    RainDrops& r = rain_;
    if (!r.started) {
        for (int k = 0; k < 217; k++) {
            r.thin[k][0] = kThinX[k];
            r.thin[k][2] = 0;
            r.thin[k][1] = static_cast<int16_t>(crand() % 100);
            r.thin[k][3] = static_cast<int16_t>((crand() & 0xf) + 10);
            r.thin[k][4] = r.thin[k][5] = -1;
        }
        for (int k = 0; k < 15; k++) {
            r.tall[k][0] = static_cast<int16_t>(crand() % 0x276);
            r.tall[k][1] = static_cast<int16_t>(crand() % 100);
            r.tall[k][2] = 0;
            r.tall[k][3] = static_cast<int16_t>(crand() % 0x1f + 0x1e);
            r.tall[k][4] = r.tall[k][5] = -1;
        }
        r.started = true;                                    // DAT_100a1f88
    }
    for (auto& d : r.thin) if (d[4] == -1) { d[4] = d[0]; d[5] = d[1]; }
    for (auto& d : r.tall) if (d[4] == -1) { d[4] = d[0]; d[5] = d[1]; }

    auto inRects = [&](int x, int y) {                       // PtInRect over the list: any rectangle holds the point
        for (int i = 0; i < bufLen; i++) {
            int32_t rc[4];
            std::memcpy(rc, static_cast<const char*>(buf) + 16 * static_cast<size_t>(i), 16);
            if (x >= rc[0] && x < rc[2] && y >= rc[1] && y < rc[3]) return true;
        }
        return false;
    };
    for (auto& d : r.thin) {
        const int x = d[4], y = d[5];
        u8* p = dst + static_cast<ptrdiff_t>(y) * kRow + x;
        if (p < dstEnd) {
            if (!buf) {
                for (int i = 0; i < 5; i++) {
                    *p = p5;
                    if (dstEnd <= p + kRow) break;
                    p += kRow;
                }
            } else {
                int yy = y;
                for (int i = 0; i < 5; i++) {
                    if (inRects(x, yy)) *p = p5;
                    p += kRow;
                    if (dstEnd <= p) break;
                    yy++;
                }
            }
            d[5] = static_cast<int16_t>(d[5] + d[3]);
        } else {
            d[4] = d[5] = -1;
            d[1] = static_cast<int16_t>(crand() % 100);
        }
    }
    for (auto& d : r.tall) {
        const int x = d[4], y = d[5];
        u8* p = dst + static_cast<ptrdiff_t>(y) * kRow + x;
        if (p < dstEnd) {
            auto draw = [&](int i) {
                if (i < 4 || i > 0xe) p[1] = p7;
                else { p[0] = p6; p[1] = p7; p[2] = p6; }
            };
            if (!buf) {
                for (int i = 0; i < 0x10;) {
                    draw(i);
                    p += kRow;
                    if (!(p < dstEnd)) break;
                    i++;
                }
            } else {
                int yy = y;
                for (int i = 0; i < 0x10; i++) {
                    if (inRects(x + 1, yy)) draw(i);
                    p += kRow;
                    if (dstEnd <= p) break;
                    yy++;
                }
            }
            d[5] = static_cast<int16_t>(d[5] + d[3]);
        } else {
            d[4] = d[5] = -1;
            d[0] = static_cast<int16_t>(crand() % 0x276);
            d[1] = static_cast<int16_t>(crand() % 100);
        }
    }
    return true;
}

} // namespace biosys::game
