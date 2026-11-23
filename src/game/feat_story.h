// Story and house features (no ecosystem state): pods, doors, hatches, lifts, seals, terminals, video phones,
// cabinets, fire, fountains, ... Member meanings are mostly unknown (names `unk_XX` = object offset); see
// analysis/notes/player.md section 9 for what the story code does with them.
#pragma once
#include "game/feature.h"
#include <array>

namespace biosys::game {

// Features whose slot is the CFeature block only (no members of their own are saved).
class CPodFeature final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:100026a0
    CPodFeature(World& w, const char* name, u32 a1, u32 a2, Item* a3, i32 a4);
    void loadSlot(SlotReader& r) override;         // dll:10002800 (CFeature::Load, then DAT_1009f424 = -1)

    // The pod's console. FEATURE_DESTINATION (0x14) is the destination code of its CPodTransport (+0x5c, member +0x6c)
    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:10002820
    void display(int redraw) override;                                   // +0x5c dll:10002960 (feats_pod.cpp, fork B)
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10002860
    // behaviour (feats_pod.cpp)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10002b70 (b_Call, b_charge)
    void onPageEntered(const std::string& page) override;                // +0x3c dll:10002890 (the pod door opens when the pod stands here)
    void update(i32 now) override;                                       // +0x58 dll:10002c30 (the pod arrived at this station)
    void slot60(const char* page, int arg) override;                     // +0x60 dll:10002b30
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:10002910
private:
    i32 unk_58;           // +0x58 the station's code (0x15 .. 0x19)
    Item* unk_5c;         // +0x5c the CPodTransport
};

class CPoison final : public CFeature {
public:
    // dll:10006460
    CPoison(World& w, const char* name, i32 a1, i32 a2);
    // behaviour (feats_story2.cpp): the poison-gas "POI" information terminal (pages uPOI...)
    void onEnterPage(const std::string& page) override;   // +0x34 dll:10006560
    void onLeavePage() override;                           // +0x40 dll:10006810
    void display(int redraw) override;                     // +0x5c dll:100067f0
    void onPowerOn() override;                             // +0x68 dll:10006770
    void onPowerOff() override;                            // +0x6c dll:100067b0
};

class CFire final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10006830. `value` is the double the original stores as two dwords at +0x60/+0x64.
    CFire(World& w, const char* name, u32 a1, const char* site, u16 id, double value);
    CSite* site() const { return m_pSite; }

    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10006a90 (CFire::OnHotspot: the flame hotspots hurt)
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:10006a00 (an extinguisher used on the fire)
    void update(i32 now) override;                                       // +0x58 dll:10006b00 (burns the cel's oxygen)
    void slot60(const char* page, int arg) override;                     // +0x60 dll:10006960 (the fire / flame hotspots of its page)
    void startRunning() override;                                        // +0x80 dll:10006b70
    void stopRunning() override;                                         // +0x84 dll:10006bb0
private:
    CSite* m_pSite;       // +0x58 the site (room) the fire burns in
    double unk_60;        // +0x60 heat the fire adds to the site's two double members (+0xe8 / +0xf0)
};

class CFountain final : public CFeature {
public:
    // dll:10006bf0
    CFountain(World& w, const char* name, i32 a1, i32 a2, u16 id);
    void startRunning() override;                                        // +0x80 dll:10006d00 (the FX of its id)
    void stopRunning() override;                                         // +0x84 dll:10006d20
};

class CSpFeature final : public CFeature {
public:
    // dll:1000ab70
    CSpFeature(World& w, const char* name, i32 a1, i32 a2);
    // behaviour (feats_story2.cpp): the spirometer
    void onEnterPage(const std::string& page) override;   // +0x34 dll:1000ac70
    void onPageDrawn(const std::string& page) override;   // +0x38 dll:1000ad00
    void onResume() override;                              // +0x54 dll:1000b140
    bool onMediaClosed(int handle, unsigned mask) override;   // +0xa0 dll:1000ae40 (the text of the result)
};

class CDedFeature final : public CFeature {
public:
    // dll:10016700
    CDedFeature(World& w, const char* name, u32 a1);
    // behaviour (feats_story2.cpp): the rock that blocks the way
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:100169a0
    void display(int redraw) override;                     // +0x5c dll:10016800
    void slot60(const char* page, int arg) override;       // +0x60 dll:10016860
};

class CMicroscope final : public CFeature {
public:
    // dll:1005b300
    CMicroscope(World& w, const char* name, i32 a1, i32 a2);
    // behaviour (feats_story2.cpp)
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1005b4b0
    void onEnterPage(const std::string& page) override;   // +0x34 dll:1005b400
    void onLeavePage() override;                           // +0x40 dll:1005b4f0
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:1005b4d0
};

class CThermalCooker final : public CFeature {
public:
    // dll:1006f5c0
    CThermalCooker(World& w, const char* name, u32 a1, i32 a2);
    // behaviour (feats_story2.cpp): the thermal cooker of the house
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1006f6c0 (CCooker::OnButton)
    void onPageDrawn(const std::string& page) override;   // +0x38 dll:1006f7d0
    void onPageEntered(const std::string& page) override; // +0x3c dll:1006f840
    void onClickAway() override;                           // +0x48 dll:1006f7c0
    void onLeave() override;                               // +0x4c dll:1006f940
    bool onMediaClosed(int handle, unsigned mask) override;   // +0xa0 dll:1006f870 (CCooker::OnSoundEnd)
    // GameObj::onDropped (dll:100290a0): *(feature + 0x58) = the object put in the cooker
    void setObject(GameObj* o) { m_pObject = o; }
    GameObj* object() const { return m_pObject; }
private:
    void cookObject();    // the tail shared by dll:1006f940 and dll:1006f870
    GameObj* m_pObject;   // +0x58 the food in the cooker (not saved)
    i32 m_hSound = -1;    // DAT_100bd7cc: handle of the bubbling sound
};

class CMirror final : public CFeature {
public:
    // dll:10070250
    CMirror(World& w, const char* name);
    // behaviour (feats_story2.cpp): the mirror (G / M / P: goggles, mask, poncho in the reflection)
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:10070400 (it breaks)
    void display(int redraw) override;                     // +0x5c dll:10070350
    void slot60(const char* page, int arg) override;       // +0x60 dll:100704c0
private:
    std::string m_szImage;   // +0x58 the name of the reflection image shown last
};

class CBloodFeature final : public CFeature {
public:
    // dll:10070ec0
    CBloodFeature(World& w, const char* name, i32 a1, i32 a2);
    // behaviour (feats_story2.cpp): the blood scanner (ECG / blood pressure / blood test)
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:10071080
    void onEnterPage(const std::string& page) override;   // +0x34 dll:10070fd0
    void onPageDrawn(const std::string& page) override;   // +0x38 dll:10071020
    void onResume() override;                              // +0x54 dll:10071ce0
    void display(int redraw) override;                     // +0x5c dll:10071170
    bool onMediaClosed(int handle, unsigned mask) override;   // +0xa0 dll:10071050
    void onIdle() override;                                // +0xa4 dll:10071b10
private:
    void startMode();                 // dll:10071310 (the movie / sound of the mode)
    void drawFindings(int redraw);    // dll:10071450 (the text of the result)
    void drawTitle(int redraw);       // dll:10071b40
    i32 unk_58;           // +0x58 tick the ECG movie's text is due (0 = none)
    u16 unk_5c;           // +0x5c mode: 1 ECG, 2 blood pressure, 3 blood test
    u16 unk_5e;           // +0x5e blood test page 4..7
    i32 m_hSound = -1;    // DAT_100bd9b0
};

class CBees final : public CFeature {
public:
    // dll:1005aff0
    CBees(World& w, const char* name, i32 a1);
    // behaviour (feats_story2.cpp): the bees' nest
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1005b100
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:1005b1e0 (honey)
    void update(i32 now) override;                         // +0x58 dll:1005b250
    void display(int redraw) override;                     // +0x5c dll:1005b1b0
};

class CLift final : public CFeature {
public:
    // dll:10023660. Strings may be null (defaults "js_1", "js_2", "js_3" for the three sound names); the three
    // site names may be null.
    CLift(World& w, const char* name, u32 a1, u32 a2, i32 a3, const char* s1, const char* s2, const char* s3,
          const char* site1, const char* site2, const char* site3);
    CSite* site(int i) const { return m_pSite[i]; }
    // behaviour (feats_lift.cpp)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10023e40 (b_Unlock, b_Call, the floor buttons)
    void onPageDrawn(const std::string& page) override;                  // +0x38 dll:10023e10
    void display(int redraw) override;                                   // +0x5c dll:10023860 (the panel page / the lift's inside)
    void slot60(const char* page, int arg) override;                     // +0x60 dll:10024230 (the display light)
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:100241f0 (the lift arrived)
    void onTextInput(bool cancelled, const std::string& text);           // +0x90 dll:10024050 (the password was typed)
    void beginPassword();                                                // dll:10023fd0
    i32 state() const { return unk_58; }
private:
    i32 unk_58;                  // +0x58 the password bit this lift needs (0 = none); world +0x298 holds the ones entered
    std::string unk_66;          // +0x66 char[10]
    std::string unk_70;          // +0x70 char[10]
    std::string unk_7a;          // +0x7a char[14]
    CSite* m_pSite[3];           // +0x88, +0x8c, +0x90, null when the name was null
};

// Features with a few saved members of their own
class CGate final : public CFeature {
public:
    // dll:1000a200. NOTE: not saved with the CFeature block, only the dword at +0x84.
    CGate(World& w, const char* name, u32 a1, const char* t1, const char* t2, const char* t3);
    void saveSlot(SlotWriter& w) const override;   // dll:1000a370
    void loadSlot(SlotReader& r) override;         // dll:1000a390
    // behaviour (feats_gate.cpp)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1000a3b0 (a closed gate rattles)
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:1000a3f0 (the key / apple opens it)
    void display(int redraw) override;                                   // +0x5c dll:1000a470
    void slot60(const char* page, int arg) override;                     // +0x60 dll:1000a4a0 (the gate image of its state)
private:
    std::string unk_63;          // +0x63 char[11]
    std::string unk_6e;          // +0x6e char[11]
    std::string unk_79;          // +0x79 char[11]
    i32 unk_84;                  // +0x84
};

class CForestFeature final : public CFeature {
public:
    // dll:10007cd0
    CForestFeature(World& w, const char* name, i32 a1, i32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:10007dd0
    void loadSlot(SlotReader& r) override;         // dll:10007e00
    // behaviour (feats_story2.cpp): the Club Eden forest terminal (pages uCE9...)
    void onEnterPage(const std::string& page) override;   // +0x34 dll:10007e30
    void onLeavePage() override;                           // +0x40 dll:10007ff0
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:10007fb0 (the disk)
private:
    i32 unk_58;                  // +0x58
};

class CExitDoor final : public CFeature {
public:
    // dll:1000b590
    CExitDoor(World& w, const char* name, i32 a1, i32 a2);
    // behaviour = CSynthCave (feats_gate.cpp)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1000b9a0 CSynthCave::OnButton (the four switches)
    void onEnterPage(const std::string& page) override;                  // +0x34 dll:1000b740 CSynthCave::OnEnter
    void onPageDrawn(const std::string& page) override;                  // +0x38 dll:1000b800 (the explosion movie after the blast)
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:1000bfd0
    void update(i32 now) override;                                       // +0x58 dll:1000bf00 (the blast relaxes the cave's air)
    void display(int redraw) override;                                   // +0x5c dll:1000b860 CSynthCave::Display
    bool onMediaStopped(int handle, unsigned mask) override;             // +0x9c dll:1000bd20 CSynthCave::OnMediaEnd
    void onIdle() override;                                              // +0xa4 dll:1000bb90 CSynthCave::OnIdle (40 s in the cave)
    i32 blown() const { return unk_68; }
    i32 unk68() const { return unk_68; }
    i32 unk6c() const { return unk_6c; }
    void saveSlot(SlotWriter& w) const override;   // dll:1000b6b0
    void loadSlot(SlotReader& r) override;         // dll:1000b6e0
private:
    u16 unk_58;                  // +0x58
    i32 unk_5c, unk_60, unk_64;  // +0x5c, +0x60, +0x64
    i32 unk_68, unk_6c;          // +0x68, +0x6c
    CBiomeCel* m_pCel;           // +0x70 "DB6"
};

class CCoffeeMachine final : public CFeature {
public:
    // dll:10010b60
    CCoffeeMachine(World& w, const char* name, u32 a1, i32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:10010c70
    void loadSlot(SlotReader& r) override;         // dll:10010ca0
    // behaviour (feats_story2.cpp)
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:10010d80
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:10010f20 (a mug is filled)
    void update(i32 now) override;                         // +0x58 dll:10010ea0
    void display(int redraw) override;                     // +0x5c dll:10010cd0
    void slot60(const char* page, int arg) override;       // +0x60 dll:10010d40
    void startRunning() override;                          // +0x80 dll:10010e50
    void stopRunning() override;                           // +0x84 dll:10010e80
private:
    i32 unk_58, unk_5c;          // +0x58, +0x5c
};

class CTerminal final : public CFeature {
public:
    // dll:100189b0
    CTerminal(World& w, const char* name, u32 a1, const char* text, i32 a3, i32 a4, i32 a5, i32 a6);
    void saveSlot(SlotWriter& w) const override;   // dll:10018b10
    void loadSlot(SlotReader& r) override;         // dll:10018b50
    void storeDword(u32 objOffset, u32 v) override;
    u32 peekDword(u32 objOffset) const override;
    // behaviour (feats_story2.cpp): the termite mounds ("grub" / "term": insects crawl on the page, a click kills one)
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:10018b90
    void onEnterPage(const std::string& page) override;   // +0x34 dll:10018c60
    void onLeavePage() override;                           // +0x40 dll:10018fd0
    bool onMediaStopped(int handle, unsigned mask) override;   // +0x9c dll:100190e0
    void onIdle() override;                                // +0xa4 dll:10019000

private:
    struct Entry { i32 a = 0, b = 0, c = 0; };
    // +0x58..+0xb7: eight 12-byte entries. The constructor leaves them alone; the builder stores two dwords into
    // seven of them afterwards (`as<CFeature>(pCVar5).storeDword(0x64, ...)` in world_build.gen.cpp).
    std::array<Entry, 8> m_aEntry{};
    std::string m_szText;        // +0xb8 char[12] ("grub", "term")
    i32 unk_c4, unk_c8, unk_cc, unk_d0;   // +0xc4, +0xc8, +0xcc, +0xd0 (constructor arguments 3..6)
    i32 unk_d4, unk_d8;          // +0xd4, +0xd8 (saved)
};

class CVideoPlayer final : public CFeature {
public:
    // dll:10019ff0
    CVideoPlayer(World& w, const char* name, u32 a1, i32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:1001a130
    void loadSlot(SlotReader& r) override;         // dll:1001a170
    GameObj* object() const { return m_pObject; }
    // behaviour (feats_video.cpp)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1001a240 (bPlay, bStop, bRew, bEject)
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:1001a1c0 (a tape is put in)
    void onClickAway() override;                                         // +0x48 dll:1001a5c0 (the noise on the screen)
    void onLeave() override;                                             // +0x4c dll:1001a600
    void display(int redraw) override;                                   // +0x5c dll:1001a520
    void startRunning() override;                                        // +0x80 dll:1001a400 CVideoPlayer::Play
    void stopRunning() override;                                         // +0x84 dll:1001a4f0
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:1001a5a0 (the tape ended)
private:
    GameObj* m_pObject;          // +0x58 the tape object found by name on Load (CWorld::FindObject)
    std::string m_szName;        // +0x5c char[11], "none"
    i32 unk_68;                  // +0x68
};

class CDataTerminal final : public CFeature {
public:
    // dll:1005b810
    CDataTerminal(World& w, const char* name, u32 a1, i32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:1005b950
    void loadSlot(SlotReader& r) override;         // dll:1005b990
    GameObj* object() const { return m_pObject; }
    void setObject(GameObj* o) { m_pObject = o; }
    // behaviour (feats_story2.cpp): the data disk reader of the house
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1005b9e0
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:1005bb40
    void onLeave() override;                               // +0x4c dll:1005bc50
    void display(int redraw) override;                     // +0x5c dll:1005bc10
    void startRunning() override;                          // +0x80 dll:1005bbc0
    void stopRunning() override;                           // +0x84 dll:1005bbf0
private:
    GameObj* m_pObject;          // +0x58 the DAT object found by name on Load
    std::string m_szName;        // +0x5c char[11], "none"
    i32 unk_68;                  // +0x68
};

class CVidLink final : public CFeature {
public:
    // dll:1001a620. The messages (CVidMessage items) are added afterwards with Rel::VidLinkMessage
    // (CVidLink::AddMessage dll:1001a8d0 appends them to the CDWordArray at +0xc0).
    CVidLink(World& w, const char* name, i32 a1, i32 a2, u16 id, const char* palette, i32 a5, i32 a6);
    void saveSlot(SlotWriter& w) const override;   // dll:1001a7e0
    void loadSlot(SlotReader& r) override;         // dll:1001a850
    // behaviour (feats_story2.cpp): the videophone / message viewer
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1001aa20 (CVidLink::OnButton)
    void onResume() override;                              // +0x54 dll:1001acc0
    void update(i32 now) override;                         // +0x58 dll:1001abf0
    void display(int redraw) override;                     // +0x5c dll:1001a920
    bool onMediaClosed(int handle, unsigned mask) override;   // +0xa0 dll:1001aca0

private:
    std::string m_szPalette;     // +0x58 char[100]
    i32 unk_bc;                  // +0xbc
    // +0xc0 CDWordArray of the messages = World::related(Rel::VidLinkMessage, this)
    i32 unk_d4, unk_d8;          // +0xd4, +0xd8 (constructor arguments 5, 6)
    i32 unk_dc;                  // +0xdc
    // The per-message flags (message +0xec / +0xf0) live in the CVidMessage objects, as in the original.
};

class CPodTransport final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:1001b530
    CPodTransport(World& w, const char* name);
    void saveSlot(SlotWriter& w) const override;   // dll:1001b650
    void loadSlot(SlotReader& r) override;         // dll:1001b6c0

    i32 position() const { return unk_68; }
    i32 destination() const { return unk_6c; }
    void setDestination(i32 d) { unk_6c = d; }
    u16 podState() const { return unk_58; }
    double charge() const { return unk_60; }
    void setPodState(u16 s) { unk_58 = s; }
    void callTo(i32 station) {                                           // `+0x6c = station; FUN_1001c330`: the pod starts when it has 2.0 charge
        unk_6c = station;
        if (2.0 <= unk_60) { unk_58 = 0x28; unk_74 = timeStampNow_; }
    }
    i32 timeStampNow_ = 0;                                               // port: DAT_100c1c1c as the caller last saw it
    void startCharging();                                                // dll:1001c2b0
    void stopCharging();                                                 // dll:1001c2f0

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:1001b730 (0x8f start / 0x90 stop charging)
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:1001b780
    void update(i32 now) override;                                       // +0x58 dll:1001c1f0
    void display(int redraw) override;                                   // +0x5c dll:1001b800 (feats_pod.cpp, fork B)
    // behaviour (feats_pod.cpp)
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1001bda0 (b_Go, the destination buttons b_bR / S / D / O / P)
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:1001c030 (the pod movie ended: arrived)
    void slot60(const char* page, int arg) override;                     // +0x60 dll:1001c350 (the display light)
    void onTextInput(bool cancelled, const std::string& text);           // +0x90 dll:1001c070 CPodConsole::OnPasswordEntered
    void beginPassword();                                                // dll:1001bfa0
private:
    // +0x58 state: 10 idle, 0x14 travelling, 0x1e charging, 0x28 arrived (settles after 60 s); +0x60 charge 0..20 (units of
    // FEATURE_POWER_AVAILABLE / 5); +0x68 position, +0x6c destination (codes 0x15..0x19); +0x70 enabled; +0x74 time stamp
    u16 unk_58;                  // +0x58
    double unk_60;               // +0x60
    i32 unk_68, unk_6c, unk_70;  // +0x68, +0x6c, +0x70
    i32 unk_74;                  // +0x74 not set by the constructor
};

class CHatch final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:1001cc30
    CHatch(World& w, const char* name, u32 a1, u16 id, const char* text, i32 a4);
    void saveSlot(SlotWriter& w) const override;   // dll:1001cde0
    void loadSlot(SlotReader& r) override;         // dll:1001ce10

    bool isOpen() const { return unk_58 != 0; }
    void open();                                                         // dll:1001ceb0
    void close();                                                        // dll:1001cef0
    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:1001cd60 (FEATURE_OPEN)
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:1001cdb0
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:1001ce40 (b_open / b_close)
    void display(int redraw) override;                                   // +0x5c dll:1001cf30
    void slot60(const char* page, int arg) override;                     // +0x60 dll:1001d040 (the door image / hotspot of the state)
private:
    i32 unk_58;                  // +0x58 1 = open (the air flows of its id are on)
    std::string unk_5c;          // +0x5c char[], the name of the hotspot that is enabled while the hatch is open
};

class CSeal final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:100246e0
    CSeal(World& w, const char* name, i32 a1, i32 a2, const char* t80, u16 id, i32 a5, i32 a6, const char* t5c,
          const char* t67, Item* fx1, Item* fx2, Item* fx3);
    void saveSlot(SlotWriter& w) const override;   // dll:10024940
    void loadSlot(SlotReader& r) override;         // dll:10024980
    void storeDword(u32 objOffset, u32 v) override;
    u32 peekDword(u32 objOffset) const override;

    bool isOpen() const { return unk_58 != 0; }
    void open();                                                         // dll:100249f0
    void close();                                                        // dll:10024ae0
    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:100248c0 (FEATURE_OPEN)
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10024910
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:100249c0 (a closed seal: the player is told)
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:10024db0 (toggles)
    void display(int redraw) override;                                   // +0x5c dll:10024ba0
    void slot60(const char* page, int arg) override;                     // +0x60 dll:10024d20 (the cave's box: its open picture and the way through)
    bool onMediaStarted(int handle, unsigned mask) override;             // +0x98 dll:10024dd0 (the FX of the seal's state)
    bool onMediaClosed(int handle, unsigned mask) override;              // +0xa0 dll:10024e60 (the same FX, then the display)
private:
    i32 unk_58;                  // +0x58 seal state: 1 open (the air flows of its id are on)
    std::string unk_5c;          // +0x5c char[11]
    std::string unk_67;          // +0x67 char[13]
    Item* m_pFx[3];              // +0x74, +0x78, +0x7c CMovieFX (null allowed)
    std::string unk_80;          // +0x80 char[16]
    i32 unk_90;                  // +0x90
    i32 unk_94;                  // +0x94
};

class CCabinet final : public CFeature {
public:
    // dll:1005b510
    CCabinet(World& w, const char* name, u32 a1, const char* text);
    void saveSlot(SlotWriter& w) const override;   // dll:1005b640
    void loadSlot(SlotReader& r) override;         // dll:1005b670
    // behaviour (feats_story2.cpp)
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1005b710
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:1005b7d0 (the key)
    void display(int redraw) override;                     // +0x5c dll:1005b6b0
private:
    std::string unk_58;          // +0x58 char[12]
    i32 unk_64;                  // +0x64
};

class CMedBox final : public CFeature {
public:
    // dll:1005bed0
    CMedBox(World& w, const char* name, u32 a1, i32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:1005bff0
    void loadSlot(SlotReader& r) override;         // dll:1005c080
    // behaviour (feats_story2.cpp): the drug dispenser (CDrugDispenser)
    bool onHotspot(const std::string& hotspot) override;   // +0x30 dll:1005c920
    void onEnterPage(const std::string& page) override;   // +0x34 dll:1005c110
    void onApply(GameObj& object, const std::string& hotspot) override;   // +0x44 dll:1005c120 (the key)
    void display(int redraw) override;                     // +0x5c dll:1005c150
    void slot60(const char* page, int arg) override;       // +0x60 dll:1005c560
private:
    std::array<i32, 10> unk_;    // +0x58 .. +0x7c, ten dwords
};

// GameObj::onDropped (dll:100290a0): `*(feature + 0x58) = object` for a feature flagged 0x40000000 (the thermal cooker, the
// data terminal): the classes that own that member take it. Returns false for any other class.
bool storyFeatureSetObject(Item* feature, GameObj* object);
// A new world: the DLL globals of the POI terminal and the termite terminals (feats_story2.cpp) start empty
void resetStoryGlobals(World& w);

} // namespace biosys::game
