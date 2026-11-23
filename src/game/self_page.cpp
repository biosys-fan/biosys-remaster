// CSelf: the page-related parts (dll:1006e220, 1006ec20, 1006eca0, 1006ede0, 1006ef60..1006f220). The page events of
// the engine (OnEnterPage dll:1006e390, OnPageDrawn dll:1006e900) are BiosGame members (bg_page.cpp); these are the
// pieces they and the idle / sleep code call.
#include "game/feature.h"
#include "game/page.h"
#include "game/plant.h"
#include "game/self.h"
#include "game/site.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace biosys::game {

// dll:1006efa0 FUN_1006efa0: Eng_GetPanoramaInfo(&a, &b, &CSelf+0x754); +0x754 -= 0x140
void Self::UpdatePanoramaOffset() {
    int a, b, x;
    host().Eng_GetPanoramaInfo(a, b, x);
    g.panoramaX = x - 0x140;
}

// dll:1006f090 FUN_1006f090
void Self::OnFireBurn() {
    if (m_pFeature.item == nullptr) {                            // DAT_100c1f9c == 0
        host().sendKeyUp(0x53, 0xc0000001u);                     // SendMessageA(DAT_100c4324, WM_KEYUP, 'S', -0x3fffffff)
        const int r = world().state().rng.rand();
        char wav[64];
        std::snprintf(wav, sizeof wav, "sounds\\russell\\pain\\pain%d.wav", r % 5 + 1);
        host().playObjectUseAnimation("anims\\obj_use\\burnt.tan", wav, 1, 0, 200, 0x45, 3, 0x42, nullptr, -2);
    }
}

// dll:1006f100 FUN_1006f100: Eng_GetWindowDC(CSelf+0x328) and a black FillSolidRect(0, 0, 0x27f, 0x37)
void Self::RefreshForPage() { host().blankTopBar(m_lTopBarHandle); }

// dll:1006f220 FUN_1006f220(hotspot, &stringId, &unused): the help text of the hotspot named `hotspot` (first letter
// selects the kind). The third argument is never touched.
void Self::HotspotHelpText(const char* hotspot, int* stringId, int*) {
    switch (hotspot[0]) {
    case 'A': *stringId = 0x16b; return;
    case 'D': *stringId = 0x16d; return;
    case 'F': *stringId = 0x16a; return;
    case 'L': *stringId = 0x16c; return;
    case 'O': *stringId = 0x16f; return;
    case 'R': {
        const CPlant* plant = dynamic_cast<const CPlant*>(m_pPlant.item);   // DAT_100c1f98
        if (plant && (plant->flags() & 8) == 0) *stringId = 0x16e;
        return;
    }
    case 'Y':
        if (std::strstr(hotspot, "WATER") != nullptr || std::strstr(hotspot, "WORMS") != nullptr) *stringId = 0x170;
        return;
    default:
        return;
    }
}

// dll:1006e220 FUN_1006e220: one step of a journey; L = path, U = ... (sets breathing bit 4), S = stairs; the cost
// factor is 1.0 for L / U and 0.1 for S and unknown letters
int Self::Journey(u8 typeLetter) {
    double factor = 0.1;
    if (typeLetter != 'L') {
        if (typeLetter == 'S') {
            // factor 0.1
        } else if (typeLetter != 'U') {
            const std::string text = std::string("Unknown journey type letter - ") + static_cast<char>(typeLetter);
            world().state().message = text;
            host().writeLog(text);
        } else {
            m_wBreathing = static_cast<u16>(m_wBreathing | 4);
            factor = 1.0;
        }
    } else {
        factor = 1.0;
    }
    const double energy = (m_dWeightCarried * 0.05 + 3.0) * factor;
    host().CEngineFX_StopSound(m_hUnk730);
    m_hUnk730 = -1;
    LoseEnergy(energy);
    LoseNutrition(factor * 2.0);
    host().advanceTime(static_cast<unsigned>(static_cast<int64_t>(factor * 1080.0)), 1);
    host().CTimeline_UpdateDiurnal(0, 0, 0, 1);
    // FUN_10014ed0 (empty)
    m_dwJourneyTime = now();
    return 1;
}

// dll:1006ec20 FUN_1006ec20 (Event_OnExitPage)
void Self::OnExitPage() {
    if (m_pPath == nullptr || m_szLocation[0] != 'L') {
        if (m_pPage) {
            host().CPage_Exit(m_pPage);                          // vtable +0x28
            m_pPage = nullptr;
        }
    } else {
        host().CPath_PageLeft();                                 // FUN_10002380
    }
    if (m_bAwake != 0) {
        if (m_bStatusScreenVisible != 0) CloseStatusScreen();    // FUN_1006c7b0
        if (CFeature* f = dynamic_cast<CFeature*>(m_pFeature.item)) f->onLeavePage();   // vtable +0x40
        m_dwUnk298 = 0;
        // +0x29c (the text of the second message line) is not a member of Self
        m_dwUnk2a0 = 0;
    }
}

// dll:1006eca0 FUN_1006eca0 (Event_OnEnterPage: the engine finished the page jump to `page`)
void Self::OnEnterPageDone(const char* page) {
    if (m_bAwake == 0) return;
    if (!host().gameEnded()) {                                   // DAT_100c2724
        if (host().dead() == 0 && page[0] != 'L') {
            m_wBreathing = static_cast<u16>(m_wBreathing & 0xfffb);
            if (m_pPage) host().CPage_OnTrigger(m_pPage);        // vtable +0x24
            CFeature* feat = dynamic_cast<CFeature*>(m_pFeature.item);
            if (feat) feat->onPageEntered(page);                 // vtable +0x3c
            if (m_pObjHolding.item) host().objectVirtual(m_pObjHolding.item, 0x88, page);
            if (host().paletteOn()) {                            // DAT_100c1864
                host().clearWindow(m_lTopBarHandle, 0);          // FUN_1006c790
                ShowMessage(1, 0, 0, 0);
            }
            switch (m_szLocation[0]) {
            case 'A': case 'C': case 'D': case 'F': case 'O': case 'R': case 'S':
                if (host().rainFilter() && (feat == nullptr || feat->unk54() == 0)) {   // DAT_1009f848
                    host().Eng_BeginPageFilter(0x76, 0x56, 0x57);
                    return;
                }
                break;
            case 'M':
                host().Eng_DoHotspotAction("xTopBar", -1);
                break;
            default:
                break;
            }
        }
    } else if (m_pPage) {
        host().CPage_OnTrigger(m_pPage);                         // vtable +0x24 (CGameEndPage dll:1006ffc0)
    }
}

// dll:1006ede0 FUN_1006ede0 (Event_OnReturnToSavedPage): make `page` the current page without a page jump
void Self::ReturnToSavedPage(const char* page) {
    std::string up(page);
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    m_szLocation.fill(0);
    std::memcpy(m_szLocation.data(), up.c_str(), std::min(up.size(), m_szLocation.size() - 1));
    if (m_pPlant.item) {
        host().CPlant_ReleasePageImages(m_pPlant.item);          // FUN_10073a60
        m_pPlant.clear();
    }
    if (CFeature* f = dynamic_cast<CFeature*>(m_pFeature.item)) f->onClickAway();   // vtable +0x48
    CSite* site = dynamic_cast<CSite*>(m_pSite.item);
    if (!site) throw Error("CSelf::ReturnToSavedPage: the player has no site (crash in the original)");
    CFeature* cur = dynamic_cast<CFeature*>(m_pFeature.item);
    CFeature* siteFeature = site->feature();                     // site +0xc
    if (cur != nullptr && siteFeature != cur) cur->onLeave();    // vtable +0x4c
    if (siteFeature != nullptr && cur != siteFeature) siteFeature->onClickAway();
    m_pFeature.set(siteFeature);
    m_pPage = world().findPage(page);
    host().CSite_OnPageEntered(site, up.c_str());                // site vtable +0x24
    if (m_pPage) host().CPage_EnterAndUpdate(m_pPage);           // FUN_1000f540
    if ((m_dwChampagne != 0 && static_cast<double>(static_cast<u32>(m_dwChampagne)) < 3600.0) ||
        (m_dwMescal != 0 && static_cast<double>(static_cast<u32>(m_dwMescal)) < 3600.0))
        host().enableMouseDeltaEvent(1);
}

} // namespace biosys::game
