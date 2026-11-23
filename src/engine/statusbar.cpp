#include "engine/statusbar.h"
#include <algorithm>

namespace biosys {

namespace {
// Tiny 5x7 digit face for the stack counts (the original face is unknown, spec 1.8).
const char* kDigits[10][7] = {
    {"01110", "10001", "10011", "10101", "11001", "10001", "01110"},
    {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
    {"01110", "10001", "00001", "00010", "00100", "01000", "11111"},
    {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
    {"00010", "00110", "01010", "10010", "11111", "00010", "00010"},
    {"11111", "10000", "11110", "00001", "00001", "10001", "01110"},
    {"00110", "01000", "10000", "11110", "10001", "10001", "01110"},
    {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
    {"01110", "10001", "10001", "01110", "10001", "10001", "01110"},
    {"01110", "10001", "10001", "01111", "00001", "00010", "01100"},
};

u8 nearest(const Palette& pal, int r, int g, int b) {
    int best = 0;
    long bestD = -1;
    for (int i = 0; i < 256; i++) {
        long dr = pal.c[i].r - r, dg = pal.c[i].g - g, db = pal.c[i].b - b;
        long d = dr * dr + dg * dg + db * db;
        if (bestD < 0 || d < bestD) { bestD = d; best = i; }
    }
    return static_cast<u8>(best);
}

void put(Frame& f, int x, int y, u8 c, const Rect& clip) {
    if (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h) return;
    f.px[static_cast<size_t>(y) * Frame::W + x] = c;
}

// Right-aligned number whose right edge is at x (exclusive), top at y; 1-px shadow for contrast.
void drawCount(Frame& f, int n, int xRight, int y, u8 fg, u8 shadow, const Rect& clip) {
    std::string s = std::to_string(n);
    int x = xRight - static_cast<int>(s.size()) * 6 + 1;
    for (char ch : s) {
        const char* const* g = kDigits[ch - '0'];
        for (int pass = 0; pass < 2; pass++)
            for (int gy = 0; gy < 7; gy++)
                for (int gx = 0; gx < 5; gx++)
                    if (g[gy][gx] == '1') put(f, x + gx + (pass == 0), y + gy + (pass == 0), pass == 0 ? shadow : fg, clip);
        x += 6;
    }
}
} // namespace

Image8 StatusBar::normalise(const Image8& src) {
    Image8 out;
    out.w = IconW; out.h = Height;
    out.px.assign(static_cast<size_t>(IconW) * Height, 0);
    if (src.w == IconW && src.h == Height) { out.px = src.px; return out; }
    int ox = (IconW - src.w) / 2, oy = (Height - src.h) / 2;
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            int dx = x + ox, dy = y + oy;
            if (dx >= 0 && dx < IconW && dy >= 0 && dy < Height)
                out.px[static_cast<size_t>(dy) * IconW + dx] = src.px[static_cast<size_t>(y) * src.w + x];
        }
    return out;
}

void StatusBar::rebuild() {
    slots_.clear();
    for (size_t i = 0; i < nodes_.size(); i++) {
        int key = nodes_[i].key;
        bool joined = false;
        if (key != -1)
            for (auto& s : slots_)
                if (nodes_[static_cast<size_t>(s[0])].key == key) { s.push_back(static_cast<int>(i)); joined = true; break; }
        if (!joined) slots_.push_back({static_cast<int>(i)});
    }
    int hi = std::max(35, slotCount() * SlotW - 37);
    scroll_ = std::clamp(scroll_, 35, hi);
}

bool StatusBar::insert(int position, const Image8& icon, int obj, int key) {
    if (icon.px.empty()) return false;
    Node n{normalise(icon), obj, key};
    size_t at = nodes_.size();
    if (position >= 0) at = std::min(static_cast<size_t>(position), nodes_.size());
    else if (key != -1)
        for (size_t i = nodes_.size(); i-- > 0;)
            if (nodes_[i].key == key) { at = i + 1; break; }
    nodes_.insert(nodes_.begin() + static_cast<long>(at), std::move(n));
    rebuild();
    centreOnSlot(slotOfObject(obj));
    if (onChanged) onChanged();
    return true;
}

bool StatusBar::remove(int obj) {
    for (size_t i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].obj != obj) continue;
        int slot = slotOfObject(obj);
        nodes_.erase(nodes_.begin() + static_cast<long>(i));
        rebuild();
        if (slotCount() > 0) centreOnSlot(std::min(slot, slotCount() - 1));
        if (onChanged) onChanged();
        return true;
    }
    return false;
}

bool StatusBar::changeImage(int obj, const Image8& icon) {
    if (icon.px.empty()) return false;
    for (Node& n : nodes_)
        if (n.obj == obj) {
            n.icon = normalise(icon);
            if (onChanged) onChanged();
            return true;
        }
    return false;
}

void StatusBar::clear() {
    nodes_.clear();
    slots_.clear();
    scroll_ = 35;
}

bool StatusBar::contains(int obj) const {
    for (const Node& n : nodes_) if (n.obj == obj) return true;
    return false;
}

std::vector<int> StatusBar::objects() const {
    std::vector<int> v;
    for (const Node& n : nodes_) v.push_back(n.obj);
    return v;
}

int StatusBar::firstObjectInSlot(int slot) const {
    if (slot < 0 || slot >= slotCount()) return -1;
    return nodes_[static_cast<size_t>(slots_[static_cast<size_t>(slot)][0])].obj;
}

int StatusBar::slotMemberCount(int slot) const {
    return slot < 0 || slot >= slotCount() ? 0 : static_cast<int>(slots_[static_cast<size_t>(slot)].size());
}

int StatusBar::slotOfObject(int obj) const {
    for (int s = 0; s < slotCount(); s++)
        for (int ni : slots_[static_cast<size_t>(s)])
            if (nodes_[static_cast<size_t>(ni)].obj == obj) return s;
    return -1;
}

int StatusBar::slotAt(int barX) const {
    if (slots_.empty()) return -1;
    int sx = barX - 320 + scroll_;        // strip coordinate
    if (sx < 0 || sx >= slotCount() * SlotW - 2) return -1;
    return sx / SlotW;
}

void StatusBar::scrollBy(int dx) {
    scroll_ += dx;
    int hi = std::max(35, slotCount() * SlotW - 37);
    scroll_ = std::clamp(scroll_, 35, hi);
}

void StatusBar::centreOnSlot(int slot) {
    if (slot < 0) return;
    scroll_ = slot * SlotW + IconW / 2;
    scrollBy(0);
}

void StatusBar::render(Frame& f, const Palette& pal, bool vis) const {
    const u8 black = nearest(pal, 0, 0, 0);
    Rect bar{0, ScreenY, Width, Height};
    for (int y = 0; y < Height; y++)
        std::fill_n(f.px.begin() + static_cast<long>(ScreenY + y) * Frame::W, Width, black);
    if (!vis) return;
    const u8 fg = nearest(pal, 225, 220, 210);
    for (int s = 0; s < slotCount(); s++) {
        const Node& n = nodes_[static_cast<size_t>(slots_[static_cast<size_t>(s)][0])];
        int x = s * SlotW - scroll_ + 320;
        blit(f, n.icon, x, ScreenY, bar, -1);
        int cnt = slotMemberCount(s);
        if (cnt > 1) drawCount(f, cnt, x + 0x45, ScreenY, fg, black, bar);
    }
}

} // namespace biosys
