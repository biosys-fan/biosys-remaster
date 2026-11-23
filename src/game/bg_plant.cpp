// CPlant's page display and the player's dealings with a plant (dll:10072910..10073a60, 10009ce0). The plant's image names are
// built from the characters of the cel's / plant's names; the engine slots of the fruit images live in the plant (+0x18 + 4 n).
#include "game/biosgame.h"
#include "game/obj_food.h"
#include "game/story.h"
#include <cstdio>
#include <cstring>

namespace biosys::game {

namespace {
bool contains(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }
}

// dll:10014740 FUN_10014740(&CWorld, plant): the plant's fruit that hangs on it (state 2) leave the scene (object vtable +0x94)
void BiosGame::CWorld_RemovePlantFood(CPlant& p) {
    for (Item* it : world().related(Rel::WorldPlantFood, nullptr)) {
        auto* f = dynamic_cast<ObjFood*>(it);
        if (f && f->plant() == &p && f->state() == 2) f->removeFromScene(&self());
    }
}

// dll:10072fb0 (the fruit images "<plant>_<n>" of the plant (name + 1) for the bits of the fruit mask)
void BiosGame::CPlant_ShowFruitImages(CPlant& p) {
    uint16_t bit = 1;
    for (int n = 1; n < 9; n++, bit = static_cast<uint16_t>(bit * 2)) {
        char img[24];
        std::snprintf(img, sizeof img, "%s_%d", p.plantName().c_str() + 1, n);
        if ((p.fruitMask() & bit) == 0) {
            eng_.SetImageVisibility(img, -1, 0);
            p.imageSlot(n) = -1;
            std::snprintf(img, sizeof img, "o%s_%d", p.plantName().c_str() + 1, n);
            CEngineFX_HotspotEnable(img, 0, -1);
        } else {
            int slot = p.imageSlot(n);
            eng_.DisplayImage(img, -1, nullptr, 0, -1, &slot, (p.flags() & 0x100000) >> 0x14);
            p.imageSlot(n) = slot;
            eng_.SetImageVisibility(img, -1, 1);
        }
    }
}

// dll:10072d70
void BiosGame::CPlant_DisplayInDistance(CPlant& p) {
    p.setShownMask(p.fruitMask());                                       // +0x16 = +0x14
    if ((p.flags() & 0x10000) != 0) return;
    if (p.healthState() == 0) p.update(*sim_, cworld::gameTime(world()));
    if ((p.flags() & 0x100) == 0) return;
    CSite* site = p.site();
    char name[9] = {};
    // the characters: the cel's name (2), the plant's type (2), then c / l / r, health, fruit, site state
    name[0] = p.cel()->name().size() > 0 ? p.cel()->name()[0] : 0;
    name[1] = p.cel()->name().size() > 1 ? p.cel()->name()[1] : 0;
    name[2] = p.type().size() > 0 ? p.type()[0] : 0;
    name[3] = p.type().size() > 1 ? p.type()[1] : 0;
    char siteState = 0;
    switch (site->state()) {
    case 1:
        eng_.SetImageVisibility(p.image(), -1, 0);
        if (p.image().size() > 4 && p.image()[4] == 'l') {
            std::string img = p.image();
            img[4] = 'r';
            p.setImage(img);
            eng_.SetImageVisibility(img, -1, 0);
        }
        return;
    case 2: siteState = 'H'; break;
    case 3: siteState = 'H'; break;
    case 4: siteState = 'U'; break;
    default:
        gs().message = "BiosGame.dll: unrecognised site state in CPlant::DisplayInDistance for " + p.plantName();
        siteState = 'H';
        break;
    }
    char fruit;
    if ((p.flags() & 0xc00) == 0) fruit = p.fruitCount() != 0 ? 'F' : 'N';
    else fruit = 'N';
    char health;
    if ((p.healthState() & 4) == 0) {
        health = 'H';
        if ((p.healthState() & 0x802) != 0) health = 'U';
    } else {
        health = 'D';
        fruit = 'N';
    }
    name[5] = health;
    name[6] = fruit;
    name[7] = siteState;
    bool ok;
    if ((p.flags() & 0x200) == 0) {
        name[4] = 'c';
        eng_.DisplayImage(name, -1, nullptr, 0, -1, nullptr, 0);
        ok = eng_.SetImageVisibility(name, -1, 1);
    } else {
        name[4] = 'r';
        eng_.DisplayImage(name, -1, nullptr, 0, -1, nullptr, 0);
        eng_.SetImageVisibility(name, -1, 1);
        name[4] = 'l';
        eng_.DisplayImage(name, -1, nullptr, 0, -1, nullptr, 0);
        ok = eng_.SetImageVisibility(name, -1, 1);
    }
    if (!ok) gs().message = "BiosGame.dll: SetImageVisibility failed in CPlant::DisplayInDistance for " + p.plantName() + ", image name " + name;
    if (!iequals(name, p.image())) {
        if (!iequals(p.image(), "noimage")) {
            eng_.SetImageVisibility(p.image(), -1, 0);
            if (p.image().size() > 4 && p.image()[4] == 'l') {
                std::string img = p.image();
                img[4] = 'r';
                p.setImage(img);
                eng_.SetImageVisibility(img, -1, 0);
            }
        }
        p.setImage(name);
    }
}

// dll:10072910
void BiosGame::CPlant_DisplayAll(CPlant& p) {
    CPlant_DisplayInDistance(p);
    if ((p.flags() & 0x800) != 0) CPlant_ShowFruitImages(p);
}

// dll:10009ce0
void BiosGame::CPlantPair_DisplayInDistance(CPlantPair& pr, int redraw) {
    CPlant& a = *pr.plantA();
    CPlant& b = *pr.plantB();
    const i32 now = cworld::gameTime(world());
    if (a.healthState() == 0) a.update(*sim_, now);
    if (b.healthState() == 0) b.update(*sim_, now);
    if (a.site()->state() == 1) {
        eng_.SetImageVisibility(pr.image(), -1, 0);
        if (pr.image().size() > 2 && pr.image()[2] == 'l') {
            std::string img = pr.image();
            img[2] = 'r';
            pr.setImage(img);
            eng_.SetImageVisibility(img, -1, 0);
        }
        return;
    }
    auto health = [](const CPlant& p) { return (p.healthState() & 4) == 0 ? ((p.healthState() & 0x802) != 0 ? 'U' : 'H') : 'D'; };
    // the original's buffer: [0..1] cel, [2] c/l/r, [3..4] plant A's type, [5] its health, [6..7] plant B's type, [8] its health
    char buf[10] = {};
    buf[0] = a.cel()->name().size() > 0 ? a.cel()->name()[0] : 0;
    buf[1] = a.cel()->name().size() > 1 ? a.cel()->name()[1] : 0;
    buf[3] = a.type().size() > 0 ? a.type()[0] : 0;
    buf[4] = a.type().size() > 1 ? a.type()[1] : 0;
    buf[5] = health(a);
    buf[6] = b.type().size() > 0 ? b.type()[0] : 0;
    buf[7] = b.type().size() > 1 ? b.type()[1] : 0;
    buf[8] = health(b);
    bool ok;
    auto warn = [&]() {
        gs().message = "BiosGame.dll: SetImageVisibility failed in CPlantPair::DisplayInDistance for " + a.plantName() + " & " +
                       b.plantName() + ", image name " + buf;
    };
    if ((pr.flags() & 0x200) == 0) {
        buf[2] = 'c';
        eng_.DisplayImage(buf, -1, nullptr, 0, -1, nullptr, 0);
        ok = eng_.SetImageVisibility(buf, -1, 1);
    } else {
        buf[2] = 'r';
        eng_.DisplayImage(buf, -1, nullptr, 0, -1, nullptr, 0);
        ok = eng_.SetImageVisibility(buf, -1, 1);
        if (!ok) warn();
        buf[2] = 'l';
        eng_.DisplayImage(buf, -1, nullptr, 0, -1, nullptr, 0);
        ok = eng_.SetImageVisibility(buf, -1, 1);
    }
    if (!ok) warn();
    if (!iequals(buf, pr.image())) {
        if (!iequals(pr.image(), "noimage")) {
            eng_.SetImageVisibility(pr.image(), -1, 0);
            if (pr.image().size() > 2 && pr.image()[2] == 'l') {
                std::string img = pr.image();
                img[2] = 'r';
                pr.setImage(img);
                eng_.SetImageVisibility(img, -1, 0);
            }
        }
        pr.setImage(buf);
    }
    if (redraw != 0) eng_.api().RedrawPage();
}

// dll:10072aa0
void BiosGame::CPlant_Display(CPlant& p, int redraw) {
    if ((p.flags() & 8) == 0) {
        if (p.healthState() == 0) p.update(*sim_, cworld::gameTime(world()));
        for (const char* n : {"HH", "HU", "HX", "UH", "UU", "UX", "DH", "DU", "DX"}) eng_.SetImageVisibility(n, -1, 0);
        eng_.SetImageVisibility("dead", -1, 0);
        eng_.SetImageVisibility("healthy", -1, 0);
        eng_.SetImageVisibility("unhealthy", -1, 0);
        char name[3] = {'X', 'X', 0};
        if ((p.flags() & 0x4000) == 0) {
            switch (p.site()->state()) {
            case 1: name[1] = 'D'; break;
            case 2: name[1] = 'H'; break;
            case 3: case 4: name[1] = 'U'; break;
            default: break;
            }
        }
        if ((p.flags() & 0x2000) == 0) {
            const uint32_t st = p.healthState();
            if ((st & 4) == 0) {
                if ((st & 1) == 0) {
                    if ((st & 0x800) == 0) {
                        if ((st & 2) != 0) {
                            name[0] = 'U';
                            eng_.SetImageVisibility("unhealthy", -1, 1);
                            if (p.fruitCount() != 0) CWorld_RemovePlantFood(p);
                        }
                    } else {
                        name[0] = 'U';
                        eng_.SetImageVisibility("unhealthy", -1, 1);
                    }
                } else {
                    name[0] = 'H';
                    eng_.SetImageVisibility("healthy", -1, 1);
                }
            } else {
                name[0] = 'U';
                eng_.SetImageVisibility("unhealthy", -1, 1);
                log("CPlant::Display plant dead");
                // dll:CPlant::Dump is a log of the members (not translated: Eng_WriteLog only)
                if (p.fruitCount() != 0) CWorld_RemovePlantFood(p);
            }
        } else {
            name[0] = 'X';
        }
        eng_.SetImageVisibility(name, -1, 1);
    }
    if ((p.flags() & 0x1000) != 0) {
        uint32_t bit = 1;
        for (int n = 1; n < 9; n++, bit *= 2) {
            if ((p.fruitMask() & bit) != 0) {
                char img[16];
                std::snprintf(img, sizeof img, "fruit%d", n);
                eng_.SetImageVisibility(img, -1, 1);
            }
        }
    }
    if ((p.flags() & 0x80) != 0 && static_cast<uint32_t>(cworld::gameTime(world()) - p.timeLastPicked()) < 0x3840 &&
        p.timeLastPicked() != 0)
        eng_.SetImageVisibility("cut", -1, 1);
    if (redraw != 0) eng_.api().RedrawPage();
}

// dll:10072930 (the plant page was entered)
void BiosGame::CPlant_OnEnter(CPlant& p) {
    if (p.healthState() == 0) p.update(*sim_, cworld::gameTime(world()));
    if ((p.flags() & 0x80) != 0) {
        if (static_cast<uint32_t>(p.waterUntil()) < static_cast<uint32_t>(cworld::gameTime(world()))) {
            CEngineFX_HotspotEnable("yWater", 0, -1);
            if (p.waterUntil() != 0) {
                CWorld_SetFXState(21000, 0);
                p.setWaterUntil(0);
            }
        } else {
            CEngineFX_HotspotEnable("yWater", 1, -1);
            CEngineFX_HotspotEnable("y_Kukri", 0, -1);
        }
    }
    CPlant_Display(p, 0);
    const bool help = (self().m_dwDreamsSeen & 0x80000000u) != 0;                  // FUN_1006f320
    self().ShowMessage(1, p.speciesId(), 0xffff, help ? 0x171 : 0);
}

// dll:10073090 (the fruit images of the plant: those of the mask shown, the others hidden with their hotspots)
void BiosGame::CPlant_RefreshFruitImages(CPlant& p) {
    uint16_t bit = 1;
    for (int n = 1; n < 9; n++, bit = static_cast<uint16_t>(bit * 2)) {
        char img[24];
        std::snprintf(img, sizeof img, "%s_%d", p.plantName().c_str() + 1, n);
        if ((p.fruitMask() & bit) == 0) {
            eng_.SetImageVisibility(img, -1, 0);
            p.imageSlot(n) = -1;
            std::snprintf(img, sizeof img, "o%s_%d", p.plantName().c_str() + 1, n);
            CEngineFX_HotspotEnable(img, 0, -1);
        } else {
            eng_.SetImageVisibility(img, -1, 1);
        }
    }
}

// dll:10073a60 (the page is left: the saved rectangles of the fruit that was picked meanwhile are released)
void BiosGame::CPlant_ReleasePageImages(CPlant& p) {
    uint16_t bit = 1;
    for (int n = 1; n < 9; n++, bit = static_cast<uint16_t>(bit * 2)) {
        if ((p.shownMask() & bit) != 0 && (p.fruitMask() & bit) == 0) {
            char img[24];
            std::snprintf(img, sizeof img, "o%s_%d", p.plantName().c_str() + 1, n);
            eng_.api().ReleasePageImageSlot(-1, 0, p.imageSlot(n));
            CEngineFX_HotspotEnable(img, 0, -1);
        }
    }
}

// dll:10072a00 (an object used on a hotspot of the plant's page)
void BiosGame::CPlant_UseObject(CPlant& p, GameObj& o, const std::string& hotspot) {
    if (o.nameStringId() == 0xf9 && (p.flags() & 0x80) != 0) {                // the kukri on the water vine
        if ((p.healthState() & 1) != 0) {
            eng_.DisplayImage("cut", -1, nullptr, 1, -1, nullptr, 0);
            if (static_cast<uint32_t>(p.numPicked()) < 3) {
                p.setWaterUntil(cworld::gameTime(world()) + 0x708);
                CWorld_SetFXState(21000, 1);
                CEngineFX_HotspotEnable("yWater", 1, -1);
                CEngineFX_HotspotEnable("y_Kukri", 0, -1);
                return;
            }
        }
    } else if ((p.flags() & 0x10) != 0) {
        CPlant_Interact(p, &o, hotspot);
    }
}

// dll:100731b0 (drink at / eat from the plant: the hotspot's name says what it gives; o = the object in hand or none)
void BiosGame::CPlant_Interact(CPlant& p, GameObj* o, const std::string& hotspot) {
    Self& me = self();
    i16 code;
    if (contains(hotspot, "WATER")) code = 0xbe;
    else if (contains(hotspot, "WORMS")) code = 200;
    else if (contains(hotspot, "CACTUS")) code = 0xc3;
    else if (contains(hotspot, "POISON")) code = 0x10e;
    else if (contains(hotspot, "LOMB")) code = 0x82;
    else {
        if (p.speciesId() != 6) return;
        if (static_cast<uint32_t>(p.numPicked()) < 3) {
            CEngineFX_PlaySound("sounds\\rforest\\slosh.wav", 0, 0);
            me.ShowMessage(1, 0x155, 0xffff, 0);
            return;
        }
        me.ShowMessage(1, 0x157, 0xffff, 0);
        return;
    }
    if (2 < static_cast<uint32_t>(p.numPicked())) {
        if (code == 0xbe || code == 0xc3) me.ShowMessage(1, 0x157, 0xffff, 0);
        return;
    }
    bool done = false;
    if (!o || ((o->flags() & 0x1000) == 0 && (o->nameStringId() != 0xdb || code != 0xbe))) {
        if (code == 1) done = true;
        else {
            if (99.0 < me.m_dHydration) {
                me.ShowMessage(1, 0xc1, 0xffff, 0);
                return;
            }
            char wav[64];
            std::snprintf(wav, sizeof wav, "\\sounds\\russell\\drinking\\drink%d.wav", (crand() & 3) + 1);
            eng_.api().PlayObjectUseAnimation("anims\\obj_use\\water.tan", wav, true, false, 200, 0xda, 1, 0x42, "", -2);
            me.Drink(code);
        }
    } else {
        o->fill(me, static_cast<u16>(code), -1);                                // vtable +0x60
        if (code == 1) done = true;
    }
    if (!done) {
        p.setNumPicked(p.numPicked() + 1);
        p.setTimeLastPicked(cworld::gameTime(world()));
    }
    if ((p.flags() & 0x80) == 0 || static_cast<uint32_t>(p.numPicked()) < 3) return;
    CWorld_SetFXState(21000, 0);
    p.setWaterUntil(0);
}

} // namespace biosys::game
