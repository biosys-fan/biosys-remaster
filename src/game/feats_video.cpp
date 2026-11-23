// CVideoPlayer (dll:10019ff0, vtable 10090ba0): the VCR "fph1vid" of the house. A tape object (ObjVideoTape: its movie and the
// milestone its playing sets) is put in the slot (dropped on "DropSlot"), played on the TV screen of page ph1 (movie at 0x91a,0x19b);
// bPlay / bStop / bRew / bEject are its buttons.
#include "game/eng.h"
#include "game/feat_story.h"
#include "game/featenv.h"
#include "game/gameobj.h"
#include "game/obj_paper.h"

namespace biosys::game {

// dll:1001a240 CVideoPlayer::OnHotspot
bool CVideoPlayer::onHotspot(const std::string& hotspot) {
    FeatureHost* h = host();
    if (iequals(hotspot, "bPlay")) {
        playSound("sounds\\features\\click.wav");
        if (m_pObject && m_bActive == 0) { activate(); return true; }          // vtable +0x78
    } else if (iequals(hotspot, "bStop")) {
        playSound("sounds\\features\\click.wav");
        unk_68 = 0;
        if (m_pObject && m_bActive != 0) { deactivate(); return true; }        // vtable +0x7c
    } else if (iequals(hotspot, "bRew")) {
        unk_68 = 1;
        if (!m_pObject || m_bActive == 0) {
            playSound("sounds\\features\\click.wav");
            return true;
        }
        playSound("sounds\\features\\rewind.wav");
        deactivate();
        return true;
    } else if (iequals(hotspot, "bEject")) {
        playSound("sounds\\features\\click.wav");
        if (m_pObject) {
            playSound("sounds\\features\\eject.wav");
            if (m_bActive != 0) deactivate();
            m_pObject = nullptr;
            m_szName = "none";
            unk_68 = 1;
            if (h && h->eng()) h->eng()->DoHotspotAction("DropSlot", -1);
            display(1);
        }
    }
    return true;
}

// dll:1001a1c0 CVideoPlayer::OnApply (+0x44): a tape dropped on the slot
void CVideoPlayer::onApply(GameObj& object, const std::string&) {
    if (m_pObject) {
        playSound("sounds\\features\\vid_doh.wav");
        return;
    }
    m_pObject = &object;
    m_szName = object.name();
    playSound("sounds\\features\\vid.wav");
    if (host() && host()->eng()) {
        const std::string slot = "DropSlot";
        host()->eng()->api().DropHeldObject(&slot);
    }
    display(1);
}

// dll:1001a400 CVideoPlayer::Play (vtable +0x80): the base switches the feature on, then the tape's movie starts on the TV and the
// tape's milestone is set (video1 = 0x100, video2 = 0x400)
void CVideoPlayer::startRunning() {
    CFeature::startRunning();
    if (!m_pObject || !host()) return;
    auto* tape = dynamic_cast<ObjVideoTape*>(m_pObject);
    if (!tape) return;
    int handle = -1;
    if (host()->startTapeMovie(tape->movie().c_str(), &handle)) host()->triggerEvent(tape->playEvent());
}

// dll:1001a4f0 (+0x84)
void CVideoPlayer::stopRunning() {
    CFeature::stopRunning();
    if (isCurrentFeature() && host()) host()->flagHotspotHandled();            // DAT_1009f838 = 1
    if (host()) {
        host()->stopLoopedAvi();                                               // FUN_10061120
        host()->moviePageHandle() = -1;                                        // DAT_100c18f0
    }
}

// dll:1001a520 (+0x5c): on the feature's own page the "InUse" image shows a tape in the slot; elsewhere (the TV page) the noise runs
void CVideoPlayer::display(int redraw) {
    FeatureHost* h = host();
    if (!h) return;
    const std::string loc = h->currentLocation();
    if (!loc.empty() && loc[0] == 'F') {
        imgVis("InUse", m_pObject != nullptr);
        if (redraw != 0) redrawPage();
    } else if ((m_bRunning == 0 || m_pObject == nullptr) && h->moviePageHandle() == -1) {
        h->playLoopedAvi("movies\\house\\fph1vid\\noise.avi", 0x91a, 0x19b, 0, 1, "ph1", 0, 0, 0);
    }
}

// dll:1001a5c0 (+0x48): the noise on the screen when the player clicks away
void CVideoPlayer::onClickAway() {
    FeatureHost* h = host();
    if (h && (m_bRunning == 0 || m_pObject == nullptr) && h->moviePageHandle() == -1)
        h->playLoopedAvi("movies\\house\\fph1vid\\noise.avi", 0x91a, 0x19b, 0, 1, "ph1", 0, 0, 0);
}

// dll:1001a600 (+0x4c): the player leaves the feature: a running tape is stopped (not on the feature's own pages)
void CVideoPlayer::onLeave() {
    if (m_bRunning == 0 || (host() && host()->currentLocation().size() && host()->currentLocation()[0] == 'F')) return;
    deactivate();                                                              // vtable +0x7c
}

// dll:1001a5a0 (+0xa0): the movie ended (kind 1): the tape stops
bool CVideoPlayer::onMediaClosed(int, unsigned mask) {
    if (mask == 1 && m_bRunning != 0) {
        m_bRunning = 0;
        display(0);
    }
    return false;
}

} // namespace biosys::game
