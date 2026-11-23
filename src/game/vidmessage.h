// CVidMessage (dll:1001acd0, vtable 10090cf0, size 0xf4): one video message of a CVidLink (the Club Eden
// videophone "fce9vidpho" and the house VCR panel "fph8vid"); added to the link with CVidLink::AddMessage
// dll:1001a8d0. Not saved.
#pragma once
#include "game/world.h"

namespace biosys::game {

class CVidMessage final : public Item {
public:
    CVidMessage(const char* movie, const char* imageName, const char* palette, int32_t x, int32_t y,
                int32_t milestoneMask, int32_t played);
    bool typed() const override { return true; }
    void saveSlot(SlotWriter&) const override;   // not a slot item: throws
    void loadSlot(SlotReader&) override;

    const std::string& movie() const { return m_szMovie; }
    const std::string& imageName() const { return m_szImage; }
    const std::string& palette() const { return m_szPalette; }
    int32_t x() const { return m_dwX; }
    int32_t y() const { return m_dwY; }
    uint32_t milestoneMask() const { return m_dwMilestoneMask; }
    // The two flags the owning CVidLink saves and loads (dll:1001a7e0 / 1001a850), by object offset.
    int32_t flagA() const { return m_dwFlagA; }            // +0xec
    void setFlagA(int32_t v) { m_dwFlagA = v; }
    int32_t flagB() const { return m_dwFlagB; }            // +0xf0
    void setFlagB(int32_t v) { m_dwFlagB = v; }
    bool available() const { return m_dwFlagA != 0; }
    bool played() const { return m_dwFlagB != 0; }
    void setPlayed(bool v) { m_dwFlagB = v ? 1 : 0; }
    // CVidMessage::CheckMilestone dll:1001af00 needs the global milestone mask (DAT_100c272c): behaviour code
    void markAvailable() { m_dwFlagA = 1; }

private:
    // +0x00 vtable
    int32_t m_dwX;                 // +0x04 movie position
    int32_t m_dwY;                 // +0x08
    std::string m_szMovie;         // +0x0c char[100] avi file
    std::string m_szImage;         // +0x70 char[20] name of the page image shown for the message ("mike1")
    std::string m_szPalette;       // +0x84 char[100] .pal file
    uint32_t m_dwMilestoneMask;    // +0xe8 milestone bits (DAT_100c272c) that make it available; 0 = always
    int32_t m_dwFlagA;             // +0xec = (mask == 0): available
    int32_t m_dwFlagB;             // +0xf0 already played (constructor argument 6)
};

} // namespace biosys::game
