#include "game/feat_story.h"
#include "game/vidmessage.h"
#include "game/world_ctors.gen.h"
#include "game/biomecel.h"
#include "game/gameobj.h"
#include "game/site.h"
#include "game/blobrecords.h"
#include "game/eng.h"
#include <strings.h>

namespace biosys::game {

// ---------------------------------------------------------------------------------------------
// classes saved with the CFeature block only

// dll:100026a0
CPodFeature::CPodFeature(World& w, const char* name, u32 a1, u32 a2, Item* a3, i32 a4)
    : CFeature(w, 0x100026a0, "CPodFeature", name, a1 | 0x203, a2 | 0x8000) {
    unk_5c = a3;                        // +0x5c
    unk_58 = a4;                        // +0x58
}

void CPodFeature::loadSlot(SlotReader& r) {   // dll:10002800
    loadBase(r);
    globals().podGlobal = -1;          // DAT_1009f424
    r.finishPad();
}

// dll:10006460
CPoison::CPoison(World& w, const char* name, i32 a1, i32 a2)
    : CFeature(w, 0x10006460, "CPoison", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    resetStoryGlobals(w);
    m_bOn = 1;                          // +0x1c
}

// dll:10006830
CFire::CFire(World& w, const char* name, u32 a1, const char* site, u16 id, double value)
    : CFeature(w, 0x10006830, "CFire", name, a1 | 0x400, 0x80000000) {
    m_pSite = w.findSite(site);         // +0x58
    m_wId = id;                         // +0x18
    unk_60 = value;                     // +0x60 (low dword = arg 5, high dword = arg 6)
    m_bOn = 1;                          // +0x1c
}

// dll:10006bf0
CFountain::CFountain(World& w, const char* name, i32 a1, i32 a2, u16 id)
    : CFeature(w, 0x10006bf0, "CFountain", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    m_dPowerUsage = dblBits(0x789abcdf, 0x3f323456);   // +0x48
    m_wId = id;                         // +0x18
}

// dll:1000ab70
CSpFeature::CSpFeature(World& w, const char* name, i32 a1, i32 a2)
    : CFeature(w, 0x1000ab70, "CSpFeature", name, static_cast<u32>(a1), static_cast<u32>(a2)) {}

// dll:10016700
CDedFeature::CDedFeature(World& w, const char* name, u32 a1)
    : CFeature(w, 0x10016700, "CDedFeature", name, a1 | 0x400, 0x80000000) {
    m_bActive = 0;                      // +0x34
}

// dll:1005b300
CMicroscope::CMicroscope(World& w, const char* name, i32 a1, i32 a2)
    : CFeature(w, 0x1005b300, "CMicroscope", name, static_cast<u32>(a1), static_cast<u32>(a2)) {}

// dll:1006f5c0
CThermalCooker::CThermalCooker(World& w, const char* name, u32 a1, i32 a2)
    : CFeature(w, 0x1006f5c0, "CThermalCooker", name, a1 | 0x40000000, static_cast<u32>(a2)) {
    m_pObject = nullptr;                // +0x58
}

// dll:10070250
CMirror::CMirror(World& w, const char* name)
    : CFeature(w, 0x10070250, "CMirror", name, 0x80000404, 0x80000000) {
    m_bActive = 0;                      // +0x34
    m_szImage.clear();                  // +0x58 (a byte, 0)
}

// dll:10070ec0
CBloodFeature::CBloodFeature(World& w, const char* name, i32 a1, i32 a2)
    : CFeature(w, 0x10070ec0, "CBloodFeature", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    unk_58 = 0;                         // +0x58
    unk_5c = 0;                         // +0x5c
    unk_5e = 4;                         // +0x5e
}

// dll:1005aff0
CBees::CBees(World& w, const char* name, i32 a1)
    : CFeature(w, 0x1005aff0, "CBees", name, static_cast<u32>(a1), 0x80000000) {
    m_bOn = 0;                          // +0x1c
    m_bActive = 0;                      // +0x34
}

// dll:10023660
CLift::CLift(World& w, const char* name, u32 a1, u32 a2, i32 a3, const char* s1, const char* s2, const char* s3,
             const char* site1, const char* site2, const char* site3)
    : CFeature(w, 0x10023660, "CLift", name, a1 | 0x220, a2 | 0x4000) {
    unk_58 = a3;                                                          // +0x58
    unk_66 = checkedText(s1 ? s1 : "js_1", 10, "CLift +0x66");            // +0x66
    unk_70 = checkedText(s2 ? s2 : "js_2", 10, "CLift +0x70");            // +0x70
    unk_7a = checkedText(s3 ? s3 : "js_3", 14, "CLift +0x7a");            // +0x7a
    const char* sites[3] = {site1, site2, site3};
    for (int i = 0; i < 3; i++)                                           // +0x88, +0x8c, +0x90
        m_pSite[i] = sites[i] ? w.findSite(sites[i]) : nullptr;
}

// ---------------------------------------------------------------------------------------------
// classes with members of their own in the slot

// dll:1000a200
CGate::CGate(World& w, const char* name, u32 a1, const char* t1, const char* t2, const char* t3)
    : CFeature(w, 0x1000a200, "CGate", name, a1 | 0x420, 0x80000000) {
    unk_63 = checkedText(t1, 11, "CGate +0x63");
    unk_6e = checkedText(t2, 11, "CGate +0x6e");
    unk_79 = checkedText(t3, 11, "CGate +0x79");
    unk_84 = 0;                         // +0x84
}

void CGate::saveSlot(SlotWriter& w) const { w.writeI32(unk_84); }   // dll:1000a370 (no CFeature block)

void CGate::loadSlot(SlotReader& r) {                               // dll:1000a390
    unk_84 = r.readI32("unk_84");
    r.finishPad();
}

// dll:10007cd0
CForestFeature::CForestFeature(World& w, const char* name, i32 a1, i32 a2)
    : CFeature(w, 0x10007cd0, "CForestFeature", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    unk_58 = 0;                         // +0x58
}

void CForestFeature::saveSlot(SlotWriter& w) const {   // dll:10007dd0
    saveBase(w);
    w.writeI32(unk_58);
}

void CForestFeature::loadSlot(SlotReader& r) {         // dll:10007e00
    loadBase(r);
    unk_58 = r.readI32("unk_58");
    r.finishPad();
}

// dll:1000b590
CExitDoor::CExitDoor(World& w, const char* name, i32 a1, i32 a2)
    : CFeature(w, 0x1000b590, "CExitDoor", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    unk_58 = 0;
    unk_5c = 0;
    unk_60 = 0;
    unk_64 = 0;
    unk_68 = 0;
    unk_6c = 0;
    m_pCel = w.findBiomeCel("DB6");    // +0x70
}

void CExitDoor::saveSlot(SlotWriter& w) const {   // dll:1000b6b0
    saveBase(w);
    w.writeI32(unk_68);
    w.writeI32(unk_6c);
}

void CExitDoor::loadSlot(SlotReader& r) {         // dll:1000b6e0
    loadBase(r);
    unk_68 = r.readI32("unk_68");
    unk_6c = r.readI32("unk_6c");
    if (strcasecmp(globals().currentSiteName.c_str(), "DB6CAVE") == 0) {   // DAT_100c1e24
        unk_58 = 0;
        unk_5c = 0;
        unk_60 = 0;
        unk_64 = 0;
    }
    r.finishPad();
}

// dll:10010b60
CCoffeeMachine::CCoffeeMachine(World& w, const char* name, u32 a1, i32 a2)
    : CFeature(w, 0x10010b60, "CCoffeeMachine", name, a1 | 0x402, static_cast<u32>(a2)) {
    unk_58 = 0;                         // +0x58
    unk_5c = 5;                         // +0x5c
    m_bOn = 1;                          // +0x1c
    m_bActive = 1;                      // +0x34
    m_bRunning = 1;                     // +0x38
    m_wId = 0xfd2;                      // +0x18
}

void CCoffeeMachine::saveSlot(SlotWriter& w) const {   // dll:10010c70
    saveBase(w);
    w.writeI32(unk_58);
    w.writeI32(unk_5c);
}

void CCoffeeMachine::loadSlot(SlotReader& r) {         // dll:10010ca0
    loadBase(r);
    unk_58 = r.readI32("unk_58");
    unk_5c = r.readI32("unk_5c");
    r.finishPad();
}

// dll:100189b0
CTerminal::CTerminal(World& w, const char* name, u32 a1, const char* text, i32 a3, i32 a4, i32 a5, i32 a6)
    : CFeature(w, 0x100189b0, "CTerminal", name, a1 | 0x80, 0x80000000) {
    resetStoryGlobals(w);
    m_szText = checkedText(text, 12, "CTerminal text");   // +0xb8
    unk_c4 = a3;                        // +0xc4
    unk_c8 = a4;                        // +0xc8
    unk_cc = a5;                        // +0xcc
    unk_d0 = a6;                        // +0xd0
    unk_d4 = 0;                         // +0xd4
    unk_d8 = 0;                         // +0xd8
}

// `*(undefined4 *)(pCVarN + off) = v;` of the builder (`storeDword` calls in world_build.gen.cpp)
void CTerminal::storeDword(u32 off, u32 v) {
    if (off >= 0x58 && off < 0xb8 && (off & 3) == 0) {
        Entry& e = m_aEntry[(off - 0x58) / 12];
        i32* f[3] = {&e.a, &e.b, &e.c};
        *f[((off - 0x58) % 12) / 4] = static_cast<i32>(v);
        return;
    }
    CFeature::storeDword(off, v);
}

u32 CTerminal::peekDword(u32 off) const {
    if (off >= 0x58 && off < 0xb8 && (off & 3) == 0) {
        const Entry& e = m_aEntry[(off - 0x58) / 12];
        const i32* f[3] = {&e.a, &e.b, &e.c};
        return static_cast<u32>(*f[((off - 0x58) % 12) / 4]);
    }
    return CFeature::peekDword(off);
}

void CTerminal::saveSlot(SlotWriter& w) const {   // dll:10018b10
    saveBase(w);
    w.writeI32(unk_d4);
    w.writeI32(unk_d8);
}

void CTerminal::loadSlot(SlotReader& r) {         // dll:10018b50
    loadBase(r);
    unk_d4 = r.readI32("unk_d4");
    unk_d8 = r.readI32("unk_d8");
    r.finishPad();
}

// dll:10019ff0
CVideoPlayer::CVideoPlayer(World& w, const char* name, u32 a1, i32 a2)
    : CFeature(w, 0x10019ff0, "CVideoPlayer", name, a1 | 0x840, static_cast<u32>(a2)) {
    m_szName = "none";                  // +0x5c (DAT_100a1244)
    m_pObject = nullptr;                // +0x58
    unk_68 = 1;                         // +0x68
}

void CVideoPlayer::saveSlot(SlotWriter& w) const {   // dll:1001a130
    saveBase(w);
    w.writeStr(m_szName, 11);
}

void CVideoPlayer::loadSlot(SlotReader& r) {         // dll:1001a170
    loadBase(r);
    m_szName = r.readStr("m_szName", 11);
    m_pObject = world().findObject(m_szName.c_str());   // CWorld::FindObject (NULL for "none")
    r.finishPad();
}

// dll:1005b810
CDataTerminal::CDataTerminal(World& w, const char* name, u32 a1, i32 a2)
    : CFeature(w, 0x1005b810, "CDataTerminal", name, a1 | 0x800, static_cast<u32>(a2)) {
    m_szName = "none";                  // +0x5c (DAT_100a1244)
    unk_68 = -1;                        // +0x68
    m_pObject = nullptr;                // +0x58
}

void CDataTerminal::saveSlot(SlotWriter& w) const {   // dll:1005b950
    saveBase(w);
    w.writeStr(m_szName, 11);
}

void CDataTerminal::loadSlot(SlotReader& r) {         // dll:1005b990
    loadBase(r);
    m_szName = r.readStr("m_szName", 11);
    m_pObject = world().findObject(m_szName.c_str());   // CWorld::FindObject (NULL for "none")
    r.finishPad();
}

// dll:1001a620
CVidLink::CVidLink(World& w, const char* name, i32 a1, i32 a2, u16 id, const char* palette, i32 a5, i32 a6)
    : CFeature(w, 0x1001a620, "CVidLink", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    m_wId = id;                                                    // +0x18
    m_szPalette = checkedText(palette, 100, "CVidLink palette");   // +0x58
    unk_d4 = a5;                        // +0xd4
    unk_d8 = a6;                        // +0xd8
    unk_bc = 0;                         // +0xbc
    unk_dc = 0;                         // +0xdc
}

void CVidLink::saveSlot(SlotWriter& w) const {   // dll:1001a7e0
    saveBase(w);
    w.writeI32(unk_bc);
    for (Item* m : world().related(Rel::VidLinkMessage, this)) {   // the CDWordArray at +0xc0
        const CVidMessage& msg = as<CVidMessage>(m);
        w.writeI32(msg.flagA());      // message +0xec
        w.writeI32(msg.flagB());      // message +0xf0
    }
}

void CVidLink::loadSlot(SlotReader& r) {         // dll:1001a850
    loadBase(r);
    unk_bc = r.readI32("unk_bc");
    size_t i = 0;
    for (Item* m : world().related(Rel::VidLinkMessage, this)) {
        CVidMessage& msg = as<CVidMessage>(m);
        std::string idx = "vidmsg[" + std::to_string(i++) + "].";
        msg.setFlagA(r.readI32((idx + "m_dwFlagA").c_str()));
        msg.setFlagB(r.readI32((idx + "m_dwFlagB").c_str()));
    }
    r.finishPad();
}

// dll:1001b530
CPodTransport::CPodTransport(World& w, const char* name)
    : CFeature(w, 0x1001b530, "CPodTransport", name, 2, 0x80000000) {
    unk_60 = dblBits(0, 0x40100000);    // +0x60 = 4.0
    unk_70 = 0;                         // +0x70
    unk_68 = 0x15;                      // +0x68
    unk_58 = 10;                        // +0x58
    unk_6c = 0x15;                      // +0x6c
    unk_74 = 0;
}

void CPodTransport::saveSlot(SlotWriter& w) const {   // dll:1001b650
    saveBase(w);
    w.writeF64(unk_60);
    w.writeI32(unk_6c);
    w.writeI32(unk_68);
    w.writeI32(unk_74);
    w.writeU16(unk_58);
    w.writeI32(unk_70);
}

void CPodTransport::loadSlot(SlotReader& r) {         // dll:1001b6c0
    loadBase(r);
    unk_60 = r.readF64("unk_60");
    unk_6c = r.readI32("unk_6c");
    unk_68 = r.readI32("unk_68");
    unk_74 = r.readI32("unk_74");
    unk_58 = r.readU16("unk_58");
    unk_70 = r.readI32("unk_70");
    r.finishPad();
}

// dll:1001cc30
CHatch::CHatch(World& w, const char* name, u32 a1, u16 id, const char* text, i32 a4)
    : CFeature(w, 0x1001cc30, "CHatch", name, a1 | 0x620, 0x80000000) {
    unk_5c = checkedText(text, 12, "CHatch text");   // +0x5c (the array's extent is not known; 12 assumed)
    m_wId = id;                         // +0x18
    unk_58 = a4;                        // +0x58
}

void CHatch::saveSlot(SlotWriter& w) const {   // dll:1001cde0
    saveBase(w);
    w.writeI32(unk_58);
}

// ORIGINAL BUG: dll:1001ce10 calls CFeature__Save instead of CFeature__Load, so the original ignores the saved
// base block (it overwrites it in the load buffer with the live state) and only reads +0x58. The port does the
// same unless Fixes::hatchLoadBaseBlock is set (every other class loads its base block with CFeature::Load).
void CHatch::loadSlot(SlotReader& r) {         // dll:1001ce10
    loadBaseOriginalBug(r, fixes().hatchLoadBaseBlock);
    unk_58 = r.readI32("unk_58");
    r.finishPad();
}

// dll:100246e0
CSeal::CSeal(World& w, const char* name, i32 a1, i32 a2, const char* t80, u16 id, i32 a5, i32 a6, const char* t5c,
             const char* t67, Item* fx1, Item* fx2, Item* fx3)
    : CFeature(w, 0x100246e0, "CSeal", name, static_cast<u32>(a1), static_cast<u32>(a2)) {
    unk_80 = checkedText(t80, 16, "CSeal +0x80");   // +0x80
    m_wId = id;                         // +0x18
    unk_58 = a5;                        // +0x58
    unk_90 = a6;                        // +0x90
    unk_5c = checkedText(t5c, 11, "CSeal +0x5c");   // +0x5c
    unk_67 = checkedText(t67, 13, "CSeal +0x67");   // +0x67
    m_pFx[0] = fx1;                     // +0x74
    m_pFx[1] = fx2;                     // +0x78
    m_pFx[2] = fx3;                     // +0x7c
    unk_94 = 0;                         // +0x94
}

void CSeal::storeDword(u32 off, u32 v) {
    // +0x58 is the seal state: Open/Close (dll:100249f0, 10024ae0) write it directly into the other seals (`piVar2[0x16] = 1/0`)
    if (off == 0x58) { unk_58 = static_cast<i32>(v); return; }
    if (off == 0x94) { unk_94 = static_cast<i32>(v); return; }
    CFeature::storeDword(off, v);
}

u32 CSeal::peekDword(u32 off) const {
    if (off == 0x58) return static_cast<u32>(unk_58);
    if (off == 0x94) return static_cast<u32>(unk_94);
    return CFeature::peekDword(off);
}

void CSeal::saveSlot(SlotWriter& w) const {   // dll:10024940
    saveBase(w);
    w.writeI32(unk_58);
    w.writeU32(globals().sealGlobal);  // DAT_100c42cc
}

void CSeal::loadSlot(SlotReader& r) {         // dll:10024980
    loadBase(r);
    unk_58 = r.readI32("unk_58");
    globals().sealGlobal = r.readU32("g_dwSealGlobal");   // DAT_100c42cc
    r.finishPad();
}

// dll:1005b510
CCabinet::CCabinet(World& w, const char* name, u32 a1, const char* text)
    : CFeature(w, 0x1005b510, "CCabinet", name, a1 | 0x20, 0x80000000) {
    unk_58 = checkedText(text, 12, "CCabinet text");   // +0x58
    unk_64 = 1;                         // +0x64
}

void CCabinet::saveSlot(SlotWriter& w) const {   // dll:1005b640
    saveBase(w);
    w.writeI32(unk_64);
}

void CCabinet::loadSlot(SlotReader& r) {         // dll:1005b670
    loadBase(r);
    unk_64 = r.readI32("unk_64");
    r.finishPad();
}

// dll:1005bed0
CMedBox::CMedBox(World& w, const char* name, u32 a1, i32 a2)
    : CFeature(w, 0x1005bed0, "CMedBox", name, a1 | 0x440, static_cast<u32>(a2)) {
    m_bOn = 0;                          // +0x1c
    unk_ = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0};   // +0x58, +0x5c, +0x60, +0x64, +0x68, +0x6c .. +0x7c
}

void CMedBox::saveSlot(SlotWriter& w) const {   // dll:1005bff0
    saveBase(w);
    for (i32 v : unk_) w.writeI32(v);
}

void CMedBox::loadSlot(SlotReader& r) {         // dll:1005c080
    loadBase(r);
    static const char* const labels[10] = {"unk_58", "unk_5c", "unk_60", "unk_64", "unk_68",
                                           "unk_6c", "unk_70", "unk_74", "unk_78", "unk_7c"};
    for (int i = 0; i < 10; i++) unk_[i] = r.readI32(labels[i]);
    r.finishPad();
}

// ---------------------------------------------------------------------------------------------
// behaviour

// CPodFeature dll:10002820 / 10002860: id 0x14 is the destination of the pod transport
bool CPodFeature::setParam(i32 id, i32 value, double) {
    if (id != 0x14) return baseSetParam(id, value);
    as<CPodTransport>(unk_5c).setDestination(value);
    return true;
}
bool CPodFeature::getParam(i32 id, i32* pi, double* pd) const {
    if (id != 0x14) return baseGetParam(id, pi, pd);
    *pi = as<CPodTransport>(unk_5c).destination();
    return true;
}

// CPodTransport dll:1001c2b0 / 1001c2f0: charging draws 1/720 of a unit per game second from the world
void CPodTransport::startCharging() {
    cworld::addPowerDemand(world(), 0.001388888888888889);
    unk_58 = 0x1e;
    unk_74 = cworld::gameTime(world());
    cworld::setFXState(world(), 0x100e, 1);
}
void CPodTransport::stopCharging() {
    cworld::removePowerDemand(world(), 0.001388888888888889);
    unk_74 = 0;
    unk_58 = 10;
    cworld::setFXState(world(), 0x100e, 0);
}
// dll:1001b730: 0x8f starts charging, 0x90 stops it
bool CPodTransport::setParam(i32 id, i32 value, double) {
    if (id == 0x8f) { startCharging(); return true; }
    if (id == 0x90) { stopCharging(); return true; }
    return baseSetParam(id, value);
}
// dll:1001b780: 0x85 position, 0x86 charging, 0x8d the stored charge x 5
bool CPodTransport::getParam(i32 id, i32* pi, double* pd) const {
    if (id == 0x85) { *pi = unk_68; return true; }
    if (id == 0x86) { *pi = unk_58 == 0x1e ? 1 : 0; return true; }
    if (id == 0x8d) { *pd = unk_60 * 5.0; return true; }
    return baseGetParam(id, pi, pd);
}
// dll:1001c1f0 CPodTransport::Update (no base update). Charging adds 1/720 per second to 20 and stops when the world's stored
// energy drops below 3; a pod that arrived (0x28) settles into its destination 60 s later, spending 2 units of charge
void CPodTransport::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if (unk_58 == 0x1e) {
        if (cworld::header(world()).m_dPower < 3.0) {
            stopCharging();
            return;
        }
        unk_60 = static_cast<double>(static_cast<u32>(now - last)) * 0.001388888888888889 + unk_60;
        if (20.0 < unk_60) {
            unk_60 = 20.0;
            stopCharging();
            return;
        }
    } else {
        if (unk_58 != 0x28) return;
        if (0x3b < static_cast<u32>(cworld::gameTime(world()) - unk_74)) {
            unk_74 = 0;
            unk_68 = unk_6c;
            unk_60 = unk_60 - 2.0;
        }
    }
}

// CHatch dll:1001ceb0 / 1001cef0
void CHatch::open() {
    unk_58 = 1;
    playSound("sounds\\features\\hatopen.wav");
    cworld::switchAirFlows(world(), static_cast<i16>(m_wId), 1);
}
void CHatch::close() {
    playSound("sounds\\features\\hatclose.wav");
    unk_58 = 0;
    cworld::switchAirFlows(world(), static_cast<i16>(m_wId), 0);
}
bool CHatch::setParam(i32 id, i32 value, double) {   // dll:1001cd60
    if (id != 10) return baseSetParam(id, value);
    if (value == 0) close(); else open();
    return true;
}
bool CHatch::getParam(i32 id, i32* pi, double* pd) const {   // dll:1001cdb0
    if (id != 10) return baseGetParam(id, pi, pd);
    *pi = unk_58;
    return true;
}
// dll:1001ce40: the "b_open" / "b_close" hotspots; any other is not handled
bool CHatch::onHotspot(const std::string& hotspot) {
    if (host()) host()->flagHotspotHandled();   // DAT_1009f838 = 1
    if (strcasecmp(hotspot.c_str(), "b_open") == 0) { open(); return true; }
    if (strcasecmp(hotspot.c_str(), "b_close") == 0) { close(); return true; }
    return false;
}
// dll:1001cf30 CHatch::display: the hotspot of the open hatch is on while it is open; "b_open" / "b_close" mirror it
void CHatch::display(int redraw) {
    FeatureHost* h = host();
    if (!globals().currentSiteName.empty() && globals().currentSiteName[0] == 'T') {   // DAT_100c1e24 == 'T': a tunnel page: just the state
        unk_58 = 1;
        return;
    }
    if (strcasecmp(name().c_str(), "fo1htch") == 0) {
        const bool five = h && h->currentSite() && h->currentSite()->state() == 5;
        imgVis("high", five ? 1 : 0);      // DAT_100a16cc / DAT_100a16c8: the two water-level images of the ocean hatch
        imgVis("low", five ? 0 : 1);
    } else {
        siteStateImages();
    }
    const bool closed = unk_58 == 0;
    hotspotEnable(unk_5c.c_str(), closed ? 0 : 1);
    hotspotEnable("b_open", closed ? 1 : 0);
    hotspotEnable("b_close", closed ? 0 : 1);
    if (redraw != 0) redrawPage();
}

// dll:1001d040 CHatch +0x60: the feature's name + "_c" (closed) / "_o" (open) is the hotspot and image of the state, the other one is hidden
void CHatch::slot60(const char*, int) {
    std::string cur = name() + (unk_58 == 0 ? "_c" : "_o");
    hotspotEnable(cur.c_str(), 1);
    if ((m_dwFlags & 0x100u) != 0) {      // the page has a variant image: D (dead) or H (healthy) by the site's state
        CSite* site = host() ? host()->currentSite() : nullptr;
        cur[0] = 'D';
        if (site && site->state() != 1) cur[0] = 'H';
    }
    imgVis(cur.c_str(), 1);
    if (host() && host()->eng()) host()->eng()->DisplayImage(cur, -1, nullptr, 0, -1, nullptr, 0);
    std::string other = name() + (unk_58 == 0 ? "_o" : "_c");
    imgVis(other.c_str(), 0);
    hotspotEnable(other.c_str(), 0);
}

// CSeal dll:100249f0: opening the seal switches its air flows, plays its movie, tells the story, and opens the other seals of
// the same group (flag 0x10000000) too; dll:10024ae0 closes
void CSeal::open() {
    FeatureHost* h = host();
    unk_58 = 1;
    cworld::switchAirFlows(world(), static_cast<i16>(m_wId), 1);
    if (isCurrentFeature()) {
        const u32 milestones = world().state().sim.story ? world().state().sim.story->milestones : 0;
        bool played;
        if (unk_94 == 0 || (milestones & static_cast<u32>(unk_94)) != 0 || !m_pFx[2]) {
            played = m_pFx[0] && h->fxStart(m_pFx[0]) && h->fxPlay(m_pFx[0]);
        } else {
            played = h->fxStart(m_pFx[2]) && h->fxPlay(m_pFx[2]);
        }
        if (!played) display(1);
    }
    if (h) h->triggerEvent(static_cast<u32>(unk_94));          // CGame::TriggerEvent(&story, +0x94)
    if ((m_dwFlags & 0x10000000u) != 0) {
        world().state().features.sealGlobal = 1;                // DAT_100c42cc
        for (Item* it : world().list(Table::Features)) {
            CFeature& f = as<CFeature>(it);
            if (&f != this && (f.flags() & 0x10000000u) != 0) {
                f.storeDword(0x58, 1);                           // the other seals: state = open
                cworld::switchAirFlows(world(), static_cast<i16>(f.id()), 1);
            }
        }
    }
}
void CSeal::close() {
    FeatureHost* h = host();
    unk_58 = 0;
    cworld::switchAirFlows(world(), static_cast<i16>(m_wId), 0);
    if (isCurrentFeature()) {
        bool played = false;
        // ORIGINAL BUG: dll:10024ae0 tests the first movie (+0x74) for NULL but starts the second one (+0x78)
        // fix: test the movie that is started (the sibling code of CSeal::Open tests and starts the same one)
        if (fixes().sealMovieNullCheck ? m_pFx[1] != nullptr : m_pFx[0] != nullptr) {
            if (!m_pFx[1]) throw Error("CSeal::Close: the second movie is NULL (the original dereferences it)");
            if (h->fxStart(m_pFx[1])) {
                hotspotEnable(unk_5c.c_str(), 0);
                played = h->fxPlay(m_pFx[1]);
            }
        }
        if (!played) display(1);
    }
    if ((m_dwFlags & 0x10000000u) != 0) {
        world().state().features.sealGlobal = 0;
        for (Item* it : world().list(Table::Features)) {
            CFeature& f = as<CFeature>(it);
            if (&f != this && (f.flags() & 0x10000000u) != 0) {
                f.storeDword(0x58, 0);
                cworld::switchAirFlows(world(), static_cast<i16>(f.id()), 0);
            }
        }
    }
}
bool CSeal::setParam(i32 id, i32 value, double) {   // dll:100248c0
    if (id != 10) return baseSetParam(id, value);
    if (value == 0) close(); else open();
    return true;
}
bool CSeal::getParam(i32 id, i32* pi, double* pd) const {   // dll:10024910
    if (id != 10) return baseGetParam(id, pi, pd);
    *pi = unk_58;
    return true;
}
// dll:100249c0: on a closed seal the player is told (message 0xd9)
bool CSeal::onHotspot(const std::string&) {
    if (unk_58 == 0 && host()) host()->showMessage(0xd9);
    return true;
}
void CSeal::onApply(GameObj&, const std::string&) {   // dll:10024db0 (no arguments used)
    if (unk_58 == 0) open(); else close();
}
// dll:10024ba0 CSeal::display: unk_90 = "the player stands in the seal's own room" (the name of the room in +0x80)
void CSeal::display(int redraw) {
    unk_90 = strcasecmp(globals().currentSiteName.c_str(), unk_80.c_str()) == 0 ? 1 : 0;   // __strcmpi(DAT_100c1e24, +0x80)
    const char* spot = unk_90 == 0 ? unk_5c.c_str() : unk_67.c_str();
    if (unk_58 == 0) {
        hotspotEnable(spot, 0);
        hotspotEnable("b_open", 1);
        hotspotEnable("b_close", 0);
        imgVis("i_open", 0);
        imgVis("i_open2", 0);
        imgVis("i_closed", 1);
        imgVis("i_closed2", 1);
    } else {
        hotspotEnable(spot, 1);
        hotspotEnable("b_open", 0);
        const bool inRoom = unk_90 != 0;
        hotspotEnable(inRoom ? "b_close" : "yDoor", inRoom ? 1 : 0);
        imgVis("i_open", 1);
        imgVis("i_open2", 1);
        imgVis("i_closed", 0);
        imgVis("i_closed2", 0);
    }
    if (redraw != 0) redrawPage();
}
// dll:10024dd0: a movie / sprite started: the movies of the seal's current state get to answer first
bool CSeal::onMediaStarted(int handle, unsigned mask) {
    FeatureHost* h = host();
    if (!h) return false;
    if (unk_58 == 0) return m_pFx[1] && h->fxOnStarted(m_pFx[1], handle, mask);
    if (m_pFx[0] && h->fxOnStarted(m_pFx[0], handle, mask)) return true;
    return m_pFx[2] && h->fxOnStarted(m_pFx[2], handle, mask);
}

// dll:10024d20 (vtable +0x60, PORT: missing until 2026-11-22): on a page whose name starts with 'C' the box "fce7box": open (+0x58),
// its open picture "fce7box_o" is displayed unless milestone 0x10000000 is reached, the box's hotspot is off and the way
// "l_ce7ce7a" on; closed, the other way round.
void CSeal::slot60(const char* page, int) {
    if (!page || page[0] != 'C') return;
    if (unk_58 != 0) {
        const u32 milestones = world().state().sim.story ? world().state().sim.story->milestones : 0;      // DAT_100c272c
        if ((milestones & 0x10000000u) == 0 && host() && host()->eng()) host()->eng()->DisplayImage("fce7box_o", -1, nullptr, 0, -1, nullptr, 0);
        hotspotEnable("fce7box", 0);
        hotspotEnable("l_ce7ce7a", 1);
        return;
    }
    hotspotEnable("fce7box", 1);
    hotspotEnable("l_ce7ce7a", 0);
}

// dll:10024e60 (vtable +0xa0, PORT: missing until 2026-11-22): the engine closed a movie / sprite / sound. The FX of the seal's
// state get it (their vtable +0x38); when one took it and it was no sound (mask 4) and the player is at the seal, the seal is
// displayed again (the box "fce7box" plays the door's sound first). The closed seal's one FX is not asked for its answer.
bool CSeal::onMediaClosed(int handle, unsigned mask) {
    FeatureHost* h = host();
    if (!h) return false;
    const bool here = mask != 4 && h->isCurrentFeature(this);
    if (unk_58 == 0) {
        if (!m_pFx[1]) return false;
        h->fxOnClosed(m_pFx[1], handle, mask);
        if (here) display(1);
        return true;
    }
    if (m_pFx[0] && h->fxOnClosed(m_pFx[0], handle, mask)) {
        if (here) {
            if (strcasecmp(name().c_str(), "fce7box") == 0) playSound("sounds\\features\\burndoor.wav");
            display(1);
        }
        return true;
    }
    if (!m_pFx[2] || !h->fxOnClosed(m_pFx[2], handle, mask)) return false;
    if (here) display(1);
    return true;
}

// CFire dll:10006a90: the flames ('Y' hotspots) hurt
bool CFire::onHotspot(const std::string& hotspot) {
    if (!hotspot.empty() && hotspot[0] == 'Y' && host()) host()->fireHurts();
    return true;
}
// dll:10006a00 CFire::OnApply: an extinguisher (H2O type 1, CO2 type 2) used on the fire puts it out
void CFire::onApply(GameObj& object, const std::string&) {
    if ((object.flags() & 0x100000u) == 0) return;
    u16 type = static_cast<u16>(object.peek(0x40, 2));
    FeatureHost* h = host();
    const char* tan = nullptr;
    const char* wav = nullptr;
    int x = 0, y = 0;
    if (type == 1) { tan = "anims\\obj_use\\h2ouse.tan"; wav = "sounds\\objects\\exting2.wav"; x = 0xfa; y = 0x91; }
    else if (type == 2) { tan = "anims\\obj_use\\co2use.tan"; wav = "sounds\\objects\\exting1.wav"; x = 0x112; y = 0x98; }
    if (tan && h && h->eng()) h->eng()->api().PlayObjectUseAnimation(tan, wav, true, false, x, y, 1, 0x42, "", -2);
    m_bActive = 0;
    stopRunning();
    slot60(globals().currentSiteName.c_str(), 1);   // (+0x60)(DAT_100c1e24 = where the player is, 1)
}
// dll:10006b00 CFire::Update (no base update): while burning in a cel with more than 13.8 % oxygen the fire turns oxygen into CO2
void CFire::update(i32 now) {
    i32 last = m_dwTimeLastUpdate;
    m_dwTimeLastUpdate = now;
    if (m_bActive != 0) {
        CBiomeCel* cel = m_pSite->cel();     // *(site + 8)
        if (13.8 < cel->oxygen()) {
            double d = static_cast<double>(static_cast<u32>(now - last)) * 2.8935185185185185e-07;
            cel->setNewCO2(cel->newCO2() + d);
            cel->setNewOxygen(cel->newOxygen() - d);
        }
    }
}
// dll:10006960: the flame hotspot (the feature's name) is on while the fire is out; the 'Y' one (hurts) while it burns
void CFire::slot60(const char*, int) {
    std::string n = name();
    if (m_bActive == 0) {
        hotspotEnable(n.c_str(), 1);
        n[0] = 'Y';
        hotspotEnable(n.c_str(), 0);
        return;
    }
    hotspotEnable(n.c_str(), 0);
    n[0] = 'Y';
    hotspotEnable(n.c_str(), 1);
}
// dll:10006b70 / 10006bb0: the FX of the id; a burning fire heats its room (the site's two doubles)
void CFire::startRunning() {
    CFeature::startRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
    m_pSite->setUnk0e8(m_pSite->unk0e8() + unk_60);
    m_pSite->setUnk0f0(m_pSite->unk0f0() + unk_60);
}
void CFire::stopRunning() {
    CFeature::stopRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
    m_pSite->setUnk0e8(m_pSite->unk0e8() - unk_60);
    m_pSite->setUnk0f0(m_pSite->unk0f0() - unk_60);
}

// CFountain dll:10006d00 / 10006d20
void CFountain::startRunning() {
    CFeature::startRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 1);
}
void CFountain::stopRunning() {
    CFeature::stopRunning();
    cworld::setFXState(world(), static_cast<i16>(m_wId), 0);
}

void CPodFeature::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "unk_58", unk_58);
    putP(out, "unk_5c", unk_5c);
}

void CFire::members(MemberList& out) const {
    CFeature::members(out);
    putP(out, "m_pSite", m_pSite);
    putD(out, "unk_60", unk_60);
}

void CPodTransport::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "unk_58", unk_58);
    putD(out, "unk_60", unk_60);
    putI(out, "unk_68", unk_68);
    putI(out, "unk_6c", unk_6c);
    putI(out, "unk_70", unk_70);
    putI(out, "unk_74", unk_74);
}

void CHatch::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "unk_58", unk_58);
    putS(out, "unk_5c", unk_5c);
}

void CSeal::members(MemberList& out) const {
    CFeature::members(out);
    putI(out, "unk_58", unk_58);
    putS(out, "unk_5c", unk_5c);
    putS(out, "unk_67", unk_67);
    putP(out, "m_pFx[0]", m_pFx[0]);
    putP(out, "m_pFx[1]", m_pFx[1]);
    putP(out, "m_pFx[2]", m_pFx[2]);
    putS(out, "unk_80", unk_80);
    putI(out, "unk_90", unk_90);
    putI(out, "unk_94", unk_94);
}

BIOSYS_REGISTER_CTOR(100026a0, [](World& w, const char* name, uint32_t a1, uint32_t a2, Item* a3, int32_t a4) -> Item* {
    return w.adopt(std::make_unique<CPodFeature>(w, name, a1, a2, a3, a4));
});
BIOSYS_REGISTER_CTOR(10006460, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CPoison>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10006830, [](World& w, const char* name, uint32_t a1, const char* a2, uint16_t a3, double a4) -> Item* {
    return w.adopt(std::make_unique<CFire>(w, name, a1, a2, a3, a4));
});
BIOSYS_REGISTER_CTOR(10006bf0, [](World& w, const char* name, int32_t a1, int32_t a2, uint16_t a3) -> Item* {
    return w.adopt(std::make_unique<CFountain>(w, name, a1, a2, a3));
});
BIOSYS_REGISTER_CTOR(1000ab70, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CSpFeature>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10016700, [](World& w, const char* name, uint32_t a1) -> Item* {
    return w.adopt(std::make_unique<CDedFeature>(w, name, a1));
});
BIOSYS_REGISTER_CTOR(1005b300, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CMicroscope>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(1006f5c0, [](World& w, const char* name, uint32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CThermalCooker>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10070250, [](World& w, const char* name) -> Item* {
    return w.adopt(std::make_unique<CMirror>(w, name));
});
BIOSYS_REGISTER_CTOR(10070ec0, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CBloodFeature>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(1005aff0, [](World& w, const char* name, int32_t a1) -> Item* {
    return w.adopt(std::make_unique<CBees>(w, name, a1));
});
BIOSYS_REGISTER_CTOR(10023660, [](World& w, const char* name, uint32_t a1, uint32_t a2, int32_t a3, const char* a4,
                                  const char* a5, const char* a6, const char* a7, const char* a8,
                                  const char* a9) -> Item* {
    return w.adopt(std::make_unique<CLift>(w, name, a1, a2, a3, a4, a5, a6, a7, a8, a9));
});
BIOSYS_REGISTER_CTOR(1000a200, [](World& w, const char* name, uint32_t a1, const char* a2, const char* a3,
                                  const char* a4) -> Item* {
    return w.adopt(std::make_unique<CGate>(w, name, a1, a2, a3, a4));
});
BIOSYS_REGISTER_CTOR(10007cd0, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CForestFeature>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(1000b590, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CExitDoor>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(10010b60, [](World& w, const char* name, uint32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CCoffeeMachine>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(100189b0, [](World& w, const char* name, uint32_t a1, const char* a2, int32_t a3, int32_t a4,
                                  int32_t a5, int32_t a6) -> Item* {
    return w.adopt(std::make_unique<CTerminal>(w, name, a1, a2, a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(10019ff0, [](World& w, const char* name, uint32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CVideoPlayer>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(1005b810, [](World& w, const char* name, uint32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CDataTerminal>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(1001a620, [](World& w, const char* name, int32_t a1, int32_t a2, uint16_t a3, const char* a4,
                                  int32_t a5, int32_t a6) -> Item* {
    return w.adopt(std::make_unique<CVidLink>(w, name, a1, a2, a3, a4, a5, a6));
});
BIOSYS_REGISTER_CTOR(1001b530, [](World& w, const char* name) -> Item* {
    return w.adopt(std::make_unique<CPodTransport>(w, name));
});
BIOSYS_REGISTER_CTOR(1001cc30, [](World& w, const char* name, uint32_t a1, uint16_t a2, const char* a3, int32_t a4) -> Item* {
    return w.adopt(std::make_unique<CHatch>(w, name, a1, a2, a3, a4));
});
BIOSYS_REGISTER_CTOR(100246e0, [](World& w, const char* name, int32_t a1, int32_t a2, const char* a3, uint16_t a4,
                                  int32_t a5, int32_t a6, const char* a7, const char* a8, Item* a9, Item* a10,
                                  Item* a11) -> Item* {
    return w.adopt(std::make_unique<CSeal>(w, name, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11));
});
BIOSYS_REGISTER_CTOR(1005b510, [](World& w, const char* name, uint32_t a1, const char* a2) -> Item* {
    return w.adopt(std::make_unique<CCabinet>(w, name, a1, a2));
});
BIOSYS_REGISTER_CTOR(1005bed0, [](World& w, const char* name, uint32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CMedBox>(w, name, a1, a2));
});

} // namespace biosys::game
