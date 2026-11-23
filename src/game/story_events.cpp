// The story: CGame::TriggerEvent (dll:100092d0) sets a milestone bit and does what the milestone means; the sources are the
// pages (CPage::TriggerMilestone dll:1000f480: page +0x68), the objects picked up (CGameObj::PickUp dll:10029000: object
// +0x18), the features (CSeal::Open dll:100249f0 unk_94, the rain machine dll:10026280), the video tapes
// (CVideoPlayer::Play dll:1001a400), the simulation (dead-biome regrowth: CWorld::UpdateBiomeCels dll:10013cc0, in sim.cpp),
// the new day (CSubTechHandler::OnNewDay dll:10022f50), the first synth attack, the BS4 screen and the cheat `MSnn`.
//
// Milestone table (bit: meaning, setter):
//   0x1 R4 flood drained (fR1Rain finished)         0x2 wand (pick up)             0x4 ft3seal opened (CSeal)
//   0x8 russmemo (pick up)                          0x10 page NDB4DEAD             0x20 BS4 screen entered
//   0x40 page "br" with 0x8: basement fuses blow    0x80 page "bp"                 0x100 video1 played
//   0x200 statue (needs 0x400)                      0x400 video2 played            0x800 mikememo: baby synths 1-7
//   0x1000 flop1                                    0x2000 page O1                 0x4000 kukri
//   0x8000 apple / apple2: weather changes          0x10000 page R9: synths stalk the rainforest
//   0x20000 page CE3                                0x40000 page nph13dead         0x80000 poem2
//   0x100000 first synth attack                     0x10000000 dead biome mean plant health > 0.01 (regrowth)
//   0x20000000 ... > 0.3: SubTech arrival in 7 days 0x40000000 ... > 0.4: dead-biome fuses blow
//   0x80000000 first new day after the SubTech arrival time: the helicopter hunt
#include "game/story.h"
#include "game/obj_gear.h"
#include "game/watertank.h"
#include <cstdio>

namespace biosys::game {

// dll:100092d0 CGame::TriggerEvent
void StoryLogic::CGame_TriggerEvent(uint32_t param) {
    if (param == 0) return;
    uint32_t& mask = b.save_->header.milestones;           // story +0x0c = DAT_100c272c
    const uint32_t old = mask;
    uint32_t now = old | param;
    mask = now;
    if (old == now) return;
    Self& s = me();
    const bool modal = b.g.noPalette;                       // story +0x3c = DAT_100c275c
    const bool silent = b.g.silent;                         // DAT_1009f83c
    bool chime = false;
    auto sound = [&](const char* f) { b.CEngineFX_PlaySound(f, 0, 0); };
    auto setBaby = [&](GameObj* o) {
        objectPoke(o, 0x40, 2, 1, PokeOp::Set);              // *(u16*)(obj + 0x40) = 1
        objectPoke(o, 0x2c, 4, 1, PokeOp::Set);              // *(int*)(obj + 0x2c) = 1
    };
    auto finish = [&]() { if (chime) sound("sounds\\misc\\mileston.wav"); };
    switch (param) {
    case 8: chime = true; break;
    case 1: {
        if (CWaterTank* tank = world().findWaterTank("R4Tank")) tank->setWaterVol(tank->volume() * 1.01);
        if (!modal) Self_ShowMessage(1);
        chime = true;
        break;
    }
    case 2:
        s.m_dwDreamsAvailable |= 0x40;
        s.m_dwNextDream = 0x40;
        s.m_dwDreamsSeen &= 0x7fffffffu;                     // the help texts go off
        chime = true;
        break;
    case 0x10:
        s.m_dwDreamsAvailable |= 3;
        s.m_dwNextDream = 2;
        break;
    case 0x40:
        if ((now & 8) != 0) {
            if (b.CWorld_HasPower(0x1000004) || b.CWorld_HasPower(0x1000002) || b.CWorld_HasPower(0x1000008) ||
                b.CWorld_HasPower(0x1000010)) {
                b.CEngineFX_PlayAvi("movies\\basement\\sprks_BR.avi", 0x3de, 0x1e6, 0, 1, "nowhere", 0, 0, 0, 1);
                sound("sounds\\basement\\fuseblow.wav");
            }
            cworld::blowFuse(world(), 0x1000004);
            cworld::blowFuse(world(), 0x1000002);
            cworld::blowFuse(world(), 0x1000008);
            cworld::blowFuse(world(), 0x1000010);
        } else {
            mask = now & 0xffffffbfu;                        // without russmemo the bit is taken back
        }
        break;
    case 0x100:
        s.m_dwDreamsAvailable |= 8;
        s.m_dwNextDream = 8;
        break;
    case 0x80:
        s.m_dwDreamsAvailable |= 0x20;
        s.m_dwNextDream = 0x20;
        break;
    case 0x400:
        s.m_dwDreamsAvailable |= 4;
        s.m_dwNextDream = 4;
        break;
    case 0x200:
        if ((now & 0x400) != 0 && s.m_dwNextDream != 4) {
            sound("sounds\\objects\\statue.wav");
        } else {
            mask = now & 0xfffffdffu;
        }
        break;
    case 0x1000: chime = true; break;
    case 0x800:
        s.m_dwDreamsSeen |= 4;
        chime = true;
        for (int i = 1; i < 8; i++) {
            char n[16];
            std::snprintf(n, sizeof n, "baby%d", i);
            if (GameObj* o = world().findObject(n)) {
                setBaby(o);
                b.eng_.ReturnGameObject(o->name());
            }
        }
        break;
    case 0x4000: chime = true; break;
    case 0x2000:
        s.m_dwDreamsAvailable |= 0x10;
        s.m_dwNextDream = 0x10;
        break;
    case 0x10000:
        b.CWorld_SetFXState(-0x63c0, 1);
        break;
    case 0x8000: {
        chime = true;
        WorldHeader& wh = b.save_->worldHeader;       // _DAT_100c11c0 = 0, _DAT_100c11c8 = 20.0
        wh.m_dWindSpeedLo = 0;
        wh.m_dWindSpeedHi = 20.0;
        b.sim_->blueSkyLo = 0.5;                       // _DAT_100c11e0 = 0.5
        b.CWorld_SetFXState(-0x3ca6, 0);
        break;
    }
    case 0x40000:
        chime = true;
        s.m_dwNextDream = 0x80;
        s.m_dwDreamsAvailable |= 0x280;
        break;
    case 0x20000: chime = true; break;
    case 0x80000:
        chime = true;
        b.sim_->blueSkyLo = 0;                         // _DAT_100c11e0 = 0
        break;
    case 0x100000:
        s.m_dwDreamsAvailable |= 0x100;
        s.m_dwNextDream = 0x100;
        break;
    case 0x20000000:
        if (!modal && !silent) {
            sound("sounds\\misc\\helicopt.wav");
            b.save_->subTech.m_dwArrivalTime = b.save_->timeline.m_dwTime + static_cast<i32>(world().state().tuning.heliDelaySeconds);     // _DAT_100c24e0: in 7 game days
        } else {
            mask = now & 0xdfffffffu;
        }
        break;
    case 0x10000000:
        if (!modal && !silent) {
            chime = true;
            Self_ShowMessage(0x26);
            for (int i = 1; i < 7; i++) {
                char n[16];
                std::snprintf(n, sizeof n, "dbaby%d", i);
                GameObj* o = world().findObject(n);
                if (o && b.eng_.ReturnGameObject(o->name())) setBaby(o);
            }
            // wifelet / pamphlet are taken out of Club Eden's reception pages
            auto removeFrom = [&](const char* object, const char* pageName) {
                std::array<int, 12> info{};
                b.eng_.GetGameObjectInfo(object, info);
                if (info[2] == 0) return;
                std::string zonePage, zoneObject;
                int zoneHotspot = 0;
                b.eng_.GetDropZoneInfo(info[11], zonePage, zoneHotspot, zoneObject);
                if (iequals(zonePage, pageName)) {
                    if (GameObj* o = world().findObject(object))
                        if (b.eng_.RemoveGameObject(o->name())) o->removeFromScene();     // vtable +0x94
                }
            };
            removeFrom("wifelet", "nce3dead");
            removeFrom("pamphlet", "nce4recep");
            for (const char* f : {"fce7fire1", "fce7fire2", "fce7fire3", "fce7fire4", "fce3fount"})
                if (CFeature* cf = world().findFeature(f)) cf->turnOff();                 // vtable +0x74
        } else {
            mask = now & 0xefffffffu;
        }
        break;
    case 0x40000000:
        if (!modal && !silent) {
            cworld::blowFuse(world(), 0x8001000);
            cworld::blowFuse(world(), 0x8000800);
            cworld::blowFuse(world(), 0x8000400);
            cworld::blowFuse(world(), 0x8002000);
        } else {
            mask = now & 0xbfffffffu;
        }
        break;
    case 0x80000000u:
        if (!modal && !silent) sound("sounds\\misc\\helicopt.wav");
        else mask = now & 0x7fffffffu;
        break;
    default:
        break;      // the other bits (0x4, 0x20, 0x2000...) only count for the score
    }
    finish();
}

// dll:10009a10 CStory::CalcScore: score = 50 * (visited sites / sites) + 2 per milestone bit; the health of the biosphere
// (mean plant health of the counted cels, the ocean counting nine times) in percent
void StoryLogic::CStory_CalcScore() {
    uint32_t visited = 0;
    const auto& sites = world().list(Table::Sites);
    for (Item* it : sites)
        if ((as<CSite>(it).flags() & 0x80000000u) != 0) visited++;
    score = (static_cast<double>(visited) / static_cast<double>(sites.size())) * 50.0;
    for (int i = 0; i < 32; i++)
        if ((milestones() & (1u << i)) != 0) score += 2.0;
    int n = 0;
    double sum = 0.0;
    for (Item* it : world().list(Table::Cels)) {
        CBiomeCel& c = as<CBiomeCel>(it);
        const Biome* bio = c.biome();
        if (!bio) continue;
        // the biome's flag word +0x60 bit 2 (set by dll:1002b560 for rainforest, savannah, ocean and the dead biome)
        if (bio->addr == 0x100c1c50 || bio->addr == 0x100c0ee8 || bio->addr == 0x100c1280 || bio->addr == 0x100c24e8) {
            n++;
            b.CBiomeCel_UpdatePlantHealthAvg(c);
            sum = c.plantHealthAvg() + sum;
        }
    }
    const COcean* oc = dynamic_cast<const COcean*>(world().state().ocean);
    const double oceanHealth = oc ? oc->health() : 0.0;                  // COcean +0x88
    biosphereHealth = ((oceanHealth * 9.0 + sum) / static_cast<double>(n + 9)) * 100.0;
}

// dll:1000f480 CPage::TriggerMilestone: the page's milestone (+0x68) is set when it is entered
void StoryLogic::CPage_TriggerMilestone(CPage& p) {
    CGame_TriggerEvent(p.milestone());
    p.setUnk48(0);
}

// the page's vtable slot +0x24, called by Event_OnEnterPage (dll:1006eca0) for the page the player is on: the generic page
// and the death page trigger their milestone (dll:1000f480), the intro page opens its movies (dll:10061db0), the game end
// page does nothing (dll:1006ffc0)
void StoryLogic::pageOnEntered(CPage& p) {
    if (auto* ip = dynamic_cast<CIntroPage*>(&p)) { CIntroPage_OnEnterPage(*ip); return; }
    if (dynamic_cast<CGameEndPage*>(&p)) return;
    CPage_TriggerMilestone(p);
}

} // namespace biosys::game
