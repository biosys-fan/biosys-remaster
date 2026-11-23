// Items that carry a text/file name at +0x40: CPaperItem (dll:1001c470), CAlarmClock (dll:10015f20, derived from
// CPaperItem), CDataDisk (dll:10015af0), CVideoTape (dll:10063360).
#pragma once
#include "game/gameobj.h"

namespace biosys::game {

// CPaperItem: files, maps, letters, memos, poems; m_szText is the name of the page it opens ("x<name>").
// Save dll:1001c520, Load dll:1001c540 are the common ones (FUN_10028d00/d70).
class ObjPaperItem : public GameObj {
public:
    ObjPaperItem(const char* name, i32 nameStringId, u32 flags, const char* text, double weight);   // dll:1001c470
    const std::string& text() const { return m_szText; }

    // vtable 10090ec8: Use +0x2c, key filter +0x58, right click +0x74, page enter +0x80, and the two slots after the common
    // table: +0x98 the paper page was opened, +0x9c it was closed (empty here, CAlarmClock overrides them)
    int use(Self& s) override;                                           // dll:1001c560: opens the page "x<name>"
    int onKey(Self& s, int key, u32* allow) override;                    // dll:1001c790
    int onRightClick(Self& s) override;                                  // dll:1001c840
    void onPageEnter(Self& s, const std::string& page) override;         // dll:1001c8c0
    virtual void onOpened(Self& s) {}                                    // +0x98 dll:1001cc10
    virtual void onClosed(Self& s) {}                                    // +0x9c dll:1001cc20
    void dumpMembers(ObjMembers& out) const override;

protected:
    // FUN_1001c390 dll:1001c390: the CPaperItem constructor chain as run by CAlarmClock (sets nothing but vtables)
    ObjPaperItem(u32 ctorAddr, const char* className, const char* name, i32 nameStringId);
    std::string m_szText;      // +0x40 char[16]
};

// CAlarmClock (item "alarm"). Save dll:10016070, Load dll:100160b0.
class ObjAlarmClock final : public ObjPaperItem {
public:
    ObjAlarmClock(const char* name, i32 nameStringId, const char* text, Item* soundFx);   // dll:10015f20
    void saveSlot(SlotWriter& w) const override;
    void loadSlot(SlotReader& r) override;
    i32 alarmTime() const { return m_dwAlarmTime; }
    Item* soundFx() const { return m_pSoundFx; }

    // vtable 10090590 (CPaperItem's table with these changes)
    void afterLoad(Self& s) override;                                    // dll:100160f0
    void dump(std::vector<std::string>& lines) const override;           // dll:10016190
    int pickUp(Self& s) override;                                        // dll:100161f0
    int onDropped(Self& s, const std::string& hotspot) override;         // dll:10016210
    int update(Self& s, i32 now) override;                               // dll:10016310 (rings, wakes the player)
    int redraw(Self& s, int full) override;                              // dll:100165a0: the clock face
    int onButtonHotspot(Self& s, const std::string& hotspot) override;   // dll:100163c0: bOn / bUp / bDown
    void onPageEnter(Self& s, const std::string& page) override;         // dll:100164c0
    void onPageLeave(Self& s, const std::string& page) override;         // dll:10016570
    void onOpened(Self& s) override;                                     // dll:10016230
    void onClosed(Self& s) override;                                     // dll:100162e0
    void dumpMembers(ObjMembers& out) const override;

private:
    Item* m_pSoundFx = nullptr;   // +0x50 the ringing sound (CLocalFX, "clock.wav")
    i32 m_bBeingCarried = 0;      // +0x54 saved as unk_54
    i32 m_dwAlarmTime = 0;        // +0x58 time of day (23400 = 06:30)
    std::string m_szClock;        // +0x5c char[8] "HH:MM" of the alarm time (not saved)
    i32 m_bAlarmOn = 0;           // +0x64 saved as unk_64
    i32 m_bRinging = 0;           // +0x68 not saved
    i32 m_hFont6c = 0;            // +0x6c font (height 0x48) of the current time on the clock face (not saved)
    i32 m_hFont70 = 0;            // +0x70 font (height 0x30) of the alarm time (not saved)
};

// CDataDisk (dat1..dat4): m_szSound is the wave file played. Save/Load common.
class ObjDataDisk final : public GameObj {
public:
    ObjDataDisk(const char* name, i32 nameStringId, const char* sound);   // dll:10015af0
    const std::string& sound() const { return m_szSound; }
    void dumpMembers(ObjMembers& out) const override;

private:
    std::string m_szSound;     // +0x40
};

// CVideoTape (video1..video6): m_szMovie is the AVI played by the VCR. Save/Load common.
class ObjVideoTape final : public GameObj {
public:
    ObjVideoTape(const char* name, i32 nameStringId, const char* movie);   // dll:10063360
    void poke(u32 offset, unsigned bytes, u32 value, PokeOp op) override;
    u32 peek(u32 offset, unsigned bytes) const override;
    const std::string& movie() const { return m_szMovie; }
    u32 playEvent() const { return m_dwPlayEvent; }
    void dumpMembers(ObjMembers& out) const override;

private:
    std::string m_szMovie;     // +0x40 char[100]
    u32 m_dwPlayEvent = 0;     // +0xa4 CGame::TriggerEvent argument when the tape is played (CVideoPlayer::Play dll:1001a400)
};

} // namespace biosys::game
