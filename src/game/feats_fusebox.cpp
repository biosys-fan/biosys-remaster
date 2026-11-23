// CFuseBox::OnHotspot (dll:10026ff0, vtable +0x30): the hotspots of a fuse box page. "b_connect" reconnects the power of the
// house to the dead biome's circuit (main power flag 0x20000000 -> 0x8000000); "bFUSE_n" is the hole of fuse n: with a fuse in
// the hand it is plugged in (the circuit of the fuse powers on, unless the fuse is burnt), with the fuse in it the fuse is taken
// out (the hand's own action) and the circuit powers off.
#include "game/blobrecords.h"
#include "game/eng.h"
#include "game/feat_power.h"
#include "game/featenv.h"
#include "game/gameobj.h"
#include "game/obj_fuse.h"
#include <cstdio>
#include <cstdlib>

namespace biosys::game {

bool CFuseBox::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    WorldHeader& wh = *world().state().sim.world;
    uint32_t& power = wh.m_dwPowerFlags;             // DAT_100c1138
    if (iequals(hotspot, "b_connect")) {
        if ((power & 0x20000000u) != 0) {
            playSound("sounds\\basement\\recon.wav");
            power &= 0xdfffffffu;
            cworld::powerOffFeatures(world(), 0x20000000);
            cworld::setFXState(world(), 0x2792, 0);
            power |= 0x8000000u;
            cworld::powerOnFeatures(world(), 0x8000000);
            if (unk_60 == 0) {
                unk_64 = cworld::gameTime(world());
                cworld::setFXState(world(), 0x2791, 1);
                unk_60 = 1;
                hotspotEnable("b_connect", 0);
            }
            // the fuses lying in the fuse box room of the basement burn out
            for (Item* it : world().list(Table::Objects)) {
                GameObj* o = dynamic_cast<GameObj*>(it);
                if (!o || (o->flags() & 0x1000000) == 0 || !h || !h->eng()) continue;
                std::array<int, 12> info{};
                h->eng()->GetGameObjectInfo(o->name(), info);
                if (info[2] == 0) continue;
                std::string zonePage, zoneObject;
                int zoneHotspot = 0;
                h->eng()->GetDropZoneInfo(info[11], zonePage, zoneHotspot, zoneObject);
                if (iequals(zonePage, "fbdfuse") && h->eng()->api().ChangeGameObjectVariation(o->name(), "burnt", -2) == 0)
                    o->setVariant("burnt");
            }
            for (int i = 0; i < 0x14; i++) {                    // every circuit of the box powers off, one fuse at a time
                const uint32_t bit = 1u << i;
                m_dwPowerMask = ~bit & m_dwPowerMask;
                cworld::onCircuitPowerOff(world(), (m_dwPowerMask & 0x3f000000u) | bit);
            }
        }
        if (h) h->flagHotspotHandled();                           // DAT_1009f838 = 1
    } else if (hotspot.find("FUSE") != std::string::npos) {
        GameObj* held = h ? h->heldObject() : nullptr;
        if (held && (held->flags() & 0x800) != 0) return true;   // held +0x15 & 8
        const int n = std::atoi(hotspot.c_str() + 6);
        const uint32_t bit = 1u << ((n - 1) & 0x1f);
        if ((static_cast<uint32_t>(unk_5c) & bit) == 0) {         // the hole is empty
            if (held && (held->flags() & 0x1000000) != 0) {      // held +0x17 & 1: a fuse
                const bool notBurnt = !iequals(held->variant(), "burnt");
                const std::string dropAt = hotspot.substr(1);       // Eng_Api48_DropHeldObject(name + 1)
                if (h && h->eng() && h->eng()->api().DropHeldObject(&dropAt) == 0) {
                    playSound("sounds\\objects\\circbrd.wav");
                    unk_5c = static_cast<i32>(static_cast<uint32_t>(unk_5c) | bit);
                    if (notBurnt) {
                        m_dwPowerMask |= bit;
                        cworld::onCircuitPowerOn(world(), (m_dwPowerMask & 0x3f000000u) | bit);
                    }
                }
            }
        } else {                                                  // a fuse sits in the hole: it is taken out
            playSound("sounds\\objects\\fuseplg5.wav");
            if (h && h->eng() && h->eng()->DoHotspotAction(hotspot.substr(1), -1)) {
                unk_5c = static_cast<i32>(static_cast<uint32_t>(unk_5c) & ~bit);
                if ((m_dwPowerMask & bit) != 0) {
                    m_dwPowerMask &= ~bit;
                    cworld::onCircuitPowerOff(world(), (m_dwPowerMask & 0x3f000000u) | bit);
                }
            }
        }
    }
    display(1);
    return true;
}

// dll:10027580 (called by Display only): a fuse that sits in the box (+0x5c) but is not among the working ones (+0x14) is a burnt
// one. The first fuse object of the world's fuse list (world+0xb8) that carries the circuit's bits and this fuse's bit and is not
// "burnt" yet gets the variation "burnt" (FUN_10029440(object, "burnt", -2)).
void CFuseBox::markBurntFuses() {
    FeatureHost* h = host();
    for (int i = 0; i < 0x20; i++) {
        const uint32_t mask = (m_dwPowerMask & 0x3f000000u) | (1u << i);
        if ((mask & ~m_dwPowerMask & static_cast<uint32_t>(unk_5c)) == 0) continue;
        for (Item* o : world().related(Rel::WorldFuse, nullptr)) {
            ObjFuse& f = as<ObjFuse>(o);
            if ((f.fuseFlags() & mask) != mask) continue;
            if (iequals(f.variant(), "burnt")) continue;
            if (h && h->eng() && h->eng()->api().ChangeGameObjectVariation(f.name(), "burnt", -2) == 0) f.setVariant("burnt");
            break;
        }
    }
}

// dll:10026d10 CFuseBox::Display (vtable +0x5c, one argument: `cmp dword ptr [esp + 0x1c], 0` after `sub esp, 0xc` and three pushes,
// `ret 4`). The images of the fuse box page: "ce_power" / "db_power" by the main power flag 0x20000000, and for n = 0..31 the images
// "fuse_NN" (a working fuse in hole n), "blown_NN" (a blown one) and "light_NN" (the lamp under the hole: on when the fuse works, the
// main power flag 0x40000000 is set and the power flag of the box's own circuit is set). The hole of fuse n is bit n - 1 (n = 0 names
// bit 31 and images no page has, as in the original). PORT: missing until 2026-11-22, so the holes and lamps never changed.
void CFuseBox::display(int redraw) {
    markBurntFuses();
    const uint32_t power = world().state().sim.world->m_dwPowerFlags;      // DAT_100c1138
    const bool connected = (power & 0x20000000u) != 0;
    imgVis("ce_power", connected ? 1 : 0);
    imgVis("db_power", connected ? 0 : 1);
    int lit = (power & 0x40000000u) != 0 ? 1 : 0;
    const uint32_t m = m_dwPowerMask;
    if ((m & 0x1000000u) != 0) { if ((power & 0x1000000u) == 0) lit = 0; }
    else if ((m & 0x2000000u) != 0) { if ((power & 0x2000000u) == 0) lit = 0; }
    else if ((m & 0x4000000u) != 0) { if ((power & 0x4000000u) == 0) lit = 0; }
    else if ((m & 0x8000000u) != 0) { if ((power & 0x8000000u) == 0) lit = 0; }
    else if ((m & 0x10000000u) != 0) { if ((power & 0x10000000u) == 0) lit = 0; }
    for (int n = 0; n < 0x20; n++) {
        char fuse[16], blown[16], light[16];
        std::snprintf(fuse, sizeof fuse, "fuse_%02d", n);
        std::snprintf(blown, sizeof blown, "blown_%02d", n);
        std::snprintf(light, sizeof light, "light_%02d", n);
        const uint32_t bit = 1u << ((n - 1) & 0x1f);
        int lamp = 0;
        if ((static_cast<uint32_t>(unk_5c) & bit) == 0) {            // the hole is empty
            imgVis(blown, 0);
            imgVis(fuse, 0);
        } else if ((m_dwPowerMask & bit) == 0) {                     // a blown fuse sits in it
            imgVis(fuse, 0);
            imgVis(blown, 1);
        } else {                                                     // a working fuse
            imgVis(fuse, 1);
            imgVis(blown, 0);
            lamp = lit;
        }
        imgVis(light, lamp);
    }
    if (redraw != 0) redrawPage();                                   // FUN_10060de0
}

// dll:10026ed0 (vtable +0x60, two arguments, `ret 8`; the second is tested at `[esp + 0x18]` after four pushes): the power image
// ("db_power" without the house connection 0x20000000, else "ce_power"), the hotspots into the fuse room of the dead biome
// ("fbdfuse_x", "fbdfuse": enabled while no fire burns, +0x60 == 0), the four fire hotspots "y<name from its 2nd letter>_1".."_4"
// (stage bits 2, 4, 8, 0x10 of +0x60), and with a non-zero second argument the image "podoff" while fuse 16 (bit 0x8000 of +0x14:
// the pod controls) does not work.
void CFuseBox::slot60(const char*, int arg) {
    FeatureHost* h = host();
    Eng* e = h ? h->eng() : nullptr;
    const uint32_t power = world().state().sim.world->m_dwPowerFlags;
    const bool connected = (power & 0x20000000u) != 0;
    if (!connected) {
        if (e) e->DisplayImage("db_power", -1, nullptr, 0, -1, nullptr, 0);
        imgVis("ce_power", 0);
    } else {
        if (e) e->DisplayImage("ce_power", -1, nullptr, 0, -1, nullptr, 0);
        imgVis("ce_power", 1);
    }
    imgVis("db_power", connected ? 0 : 1);
    hotspotEnable("fbdfuse_x", unk_60 == 0 ? 1 : 0);
    hotspotEnable("fbdfuse", unk_60 == 0 ? 1 : 0);
    const std::string tail = name().size() > 1 ? name().substr(1) : std::string();
    for (int i = 0; i < 4; i++) {
        const std::string spot = "y" + tail + "_" + std::to_string(i + 1);          // sprintf("y%s_%d", name + 1, i + 1)
        hotspotEnable(spot.c_str(), ((2u << i) & static_cast<uint32_t>(unk_60)) != 0 ? 1 : 0);
    }
    if (arg != 0 && (m_dwPowerMask & 0x8000u) == 0 && e) {           // CEngineFX::DisplayImage("podoff", -1, 0, 0, -1, 0) dll:10060bf0
        const int xy[2] = {0, 0};
        e->DisplayImage("podoff", -1, xy, 0, -1, nullptr, 0);
    }
}

} // namespace biosys::game
