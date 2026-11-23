// CFeature (dll:10019480 ctor of the bare class, 10 items of the features table) and the helpers every feature
// class shares. The 48 classes derived from it are in feat_*.h. Base members by object offset: simulation.md
// section 8.1, slot layout: saveblob.md 5.0 / 5.9.
#pragma once
#include "game/featenv.h"
#include "game/world.h"
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace biosys::game {

// A double given as the two halves the decompiler shows (low dword, high dword).
inline double dblBits(u32 lo, u32 hi) {
    uint64_t b = (static_cast<uint64_t>(hi) << 32) | lo;
    double d;
    std::memcpy(&d, &b, sizeof d);
    return d;
}

// Copies a C string into a fixed char[] member the way the original's inlined strcpy does (strlen+1 bytes, no
// check). The original overruns the neighbouring member when the text is too long; the port refuses.
std::string checkedText(const char* s, size_t arrayBytes, const char* what);

class CFeature : public Item {
public:
    // dll:10019480 (bare CFeature: charge points and sprinklers)
    CFeature(World& w, const char* name, i32 flags, i32 powerMask);
    bool typed() const override { return true; }
    void saveSlot(SlotWriter& w) const override;   // dll:10019800
    void loadSlot(SlotReader& r) override;         // dll:10019890

    // Applies `*(undefined4 *)(pCVarN + off) = v;` of a builder (`storeDword` call in
    // world_build.gen.cpp). Base members only here; classes with more override it. Throws for an offset that
    // holds no member.
    virtual void storeDword(u32 objOffset, u32 v);
    // The member storeDword writes at that offset (tests of the builder statements); throws for an offset that holds none.
    virtual u32 peekDword(u32 objOffset) const;

    // ---- the virtual interface of CFeature (vtable 10090ae0; the slot is in the comment) ----------------------------
    // SetParam / GetParam: the BS4 control panels' parameter interface (Event_OnSet/GetFeatureParameter, ids in
    // featparam.cpp). `d` is the double the original passes as two dwords. false = the id is not a parameter of the class.
    virtual bool setParam(i32 id, i32 value, double d);                  // +0x18 dll:10019580
    virtual bool getParam(i32 id, i32* outInt, double* outDbl) const;    // +0x1c dll:10019640
    virtual void afterLoad() {}                                          // +0x28 dll:10019920 (empty in the base; after a game is loaded)
    virtual void dump(std::vector<std::string>& lines) const;            // +0x2c dll:10019930 (the lines Eng_WriteLog gets)
    virtual bool onHotspot(const std::string& hotspot);                  // +0x30 dll:10019c70 (returns 1: handled)
    virtual void onEnterPage(const std::string& page);                   // +0x34 dll:10019c90
    virtual void onPageDrawn(const std::string& page);                   // +0x38 dll:10019cd0
    virtual void onPageEntered(const std::string& page);                 // +0x3c dll:10019d20 (Event_OnEnterPage)
    virtual void onLeavePage();                                          // +0x40 dll:10019d90 (Event_OnExitPage)
    virtual void onApply(GameObj& object, const std::string& hotspot);   // +0x44 dll:10019db0 (an object used on a hotspot)
    virtual void onClickAway();                                          // +0x48 dll:10019f80
    virtual void onLeave();                                              // +0x4c dll:10019fc0 (the player's feature changes)
    virtual void slot50() {}                                             // +0x50 dll:10019fd0 (empty; Event_OnRightClick)
    virtual void onResume() {}                                           // +0x54 dll:10019fe0 (empty; app activated)
    virtual void update(i32 now);                                        // +0x58 dll:10019a10
    virtual void display(int redraw) {}                                  // +0x5c dll:10019b00 (empty in the base)
    virtual void slot60(const char* page, int arg) {}                    // +0x60 dll:10019b60 (empty in the base; display of the feature on page `page`)
    virtual void statusMessage(int id);                                  // +0x64 dll:10019b10
    virtual void onPowerOn();                                            // +0x68 dll:10019df0
    virtual void onPowerOff();                                           // +0x6c dll:10019e20
    virtual void turnOn();                                               // +0x70 dll:10019dc0
    virtual void turnOff();                                              // +0x74 dll:10019de0
    virtual void activate();                                             // +0x78 dll:10019e40
    virtual void deactivate();                                           // +0x7c dll:10019e90
    virtual void startRunning();                                         // +0x80 dll:10019eb0
    virtual void stopRunning();                                          // +0x84 dll:10019ed0
    virtual void breakDown();                                            // +0x88 dll:10019ef0 (CFeature::Break)
    virtual void repair() {}                                             // +0x8c dll:10019f00 (empty in the base)
    virtual void slot90() {}                                             // +0x90 dll:10019f10 (empty in the base)
    virtual void jumpToOwnPage();                                        // +0x94 dll:10019f20
    virtual bool onMediaStarted(int handle, unsigned mask) { return false; }   // +0x98 dll:10019f40
    virtual bool onMediaStopped(int handle, unsigned mask) { return false; }   // +0x9c dll:10019f50
    virtual bool onMediaClosed(int handle, unsigned mask) { return false; }    // +0xa0 dll:10019f60
    virtual void onIdle() {}                                             // +0xa4 dll:10019f70

    // Port only (tests, tools/feattest): every member of the object as name / text, base block first. Doubles as %.17g.
    using MemberList = std::vector<std::pair<std::string, std::string>>;
    virtual void members(MemberList& out) const;

    const std::string& featureName() const { return m_szName; }
    u32 flags() const { return m_dwFlags; }
    u32 powerMask() const { return m_dwPowerMask; }
    u16 id() const { return m_wId; }
    bool on() const { return m_bOn != 0; }
    bool working() const { return m_bWorking != 0; }
    bool active() const { return m_bActive != 0; }
    bool running() const { return m_bRunning != 0; }
    double powerUsage() const { return m_dPowerUsage; }
    i32 timerStart() const { return m_dwTimerStart; }
    i32 timerEnd() const { return m_dwTimerEnd; }
    i32 timeLimit() const { return m_dwTimeLimit; }
    i32 timeLastUpdate() const { return m_dwTimeLastUpdate; }
    i32 unk54() const { return unk_54; }   // +0x54 (CRainFX::Update / CSelf::OnEnterPage test it)

protected:
    static void putD(MemberList& out, const char* name, double v);
    static void putI(MemberList& out, const char* name, long long v);
    static void putX(MemberList& out, const char* name, u32 v);
    static void putS(MemberList& out, const char* name, const std::string& v);
    static void putP(MemberList& out, const char* name, const Item* v);   // the item's name or "null"
    FeatureHost* host() const { return world_->state().host; }
    bool isCurrentFeature() const { return host() && host()->isCurrentFeature(this); }   // DAT_100c1f9c == this
    bool inTimerWindow(u32 timeOfDay) const;                            // dll:10019aa0
    bool imgVis(const char* image, int visible);                        // CEngineFX::SetImageVisibility(image, visible, -1)
    void hotspotEnable(const char* name, int enable);                   // FUN_10060c90
    void redrawPage();                                                  // Eng_RedrawPage
    void siteStateImages();                                             // FUN_10019b70
    void playSound(const char* file) { if (host()) host()->playSound(file); }
    // CFeature::SetParam / GetParam for the ids every class has (the original classes end in `return CFeature__SetParam(...)`)
    bool baseSetParam(i32 id, i32 value);
    bool baseGetParam(i32 id, i32* outInt, double* outDbl) const;
    void baseDump(std::vector<std::string>& lines) const;
    // Subclasses call the derived-class form of the original constructor: CFeature::CFeature (vtable only) and
    // then CFeature__Init dll:10019500 with the flags and power mask they computed.
    CFeature(World& w, u32 ctorAddr, const char* className, const char* name, u32 flags, u32 powerMask);

    World& world() const { return *world_; }
    FeatureGlobals& globals() const { return world_->state().features; }   // DLL globals of the feature classes
    void saveBase(SlotWriter& w) const;   // CFeature::Save dll:10019800 (slot+0x00..0x27)
    void loadBase(SlotReader& r);         // CFeature::Load dll:10019890
    // What the two ORIGINAL BUG loads do (CLung dll:100102d0, CHatch dll:1001ce10): they call CFeature::Save on the
    // load cursor instead of CFeature::Load, so the saved base block is skipped (40 bytes) and the live state stays.
    // With `fixed` (Fixes::lungLoadBaseBlock / hatchLoadBaseBlock) the block is loaded instead.
    void loadBaseOriginalBug(SlotReader& r, bool fixed);
    const Fixes& fixes() const { return world_->state().fixes; }

    // Base-only mechanism: a class whose own members are not typed yet carries its slot bytes after the base
    // block verbatim (Feature::saveSlot / loadSlot do it when this is set). No class uses it any more.
    void carryClassBytes() { carryTail_ = true; }

    // Members of CFeature, by object offset
    // +0x00 vtable
    // +0x04 char[12] name (copy of the first constructor argument, = Item::name())
    u32 m_dwFlags = 0;               // +0x10 feature flags (UI/behaviour bits)
    u32 m_dwPowerMask = 0;           // +0x14 circuit + fuse bits; 0x80000000 = needs no power
    u16 m_wId = 0;                   // +0x18 flow id / FX id it switches
    // +0x1a padding
    i32 m_bOn = 0;                   // +0x1c master switch (FEATURE_ON)
    i32 unk_20 = 0;                  // +0x20 set to -1 by Init, never read by the code seen so far
    i32 m_dwTimerStart = 0;          // +0x24 daily timer start, seconds of day (-1 none)
    i32 m_dwTimerEnd = 0;            // +0x28 daily timer end
    i32 m_dwTimeLimit = 0;           // +0x2c remaining run time in seconds (-1 none)
    i32 m_bWorking = 0;              // +0x30 not broken
    i32 m_bActive = 0;               // +0x34 wants to run
    i32 m_bRunning = 0;              // +0x38 powered and drawing power
    // +0x3c not touched
    double m_dMalfunctionProb = 0;   // +0x40 always 0
    double m_dPowerUsage = 0;        // +0x48 per game second
    i32 m_dwTimeLastUpdate = 0;      // +0x50 game time of the last Update (28800 = 08:00)
    i32 unk_54 = 0;                  // +0x54

private:
    World* world_;
    std::string m_szName;
    bool carryTail_ = false;
    std::vector<u8> tail_;
};

} // namespace biosys::game
