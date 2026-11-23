// The events BiosGame hands to StoryLogic (biosgame.h declares them; the handlers are in story_*.cpp), the idle step of a
// journey and the `game ...` debug commands of the story code.
#include "game/story.h"
#include "game/feat_story.h"
#include "game/feat_power.h"
#include "game/featenv.h"
#include <cstdio>
#include <cstring>
#include <sstream>

namespace biosys::game {

int BiosGame::onHotspot(const std::string& name, int window, const std::string& page) { return story_->onHotspot(name, window, page); }
void BiosGame::onKeyDown(int vkey, int repeat, int flags, int& doDefault) { story_->onKeyDown(vkey, repeat, flags, doDefault); }
void BiosGame::onKeyUp(int vkey, int repeat, int flags, int& doDefault) { story_->onKeyUp(vkey, repeat, flags, doDefault); }
void BiosGame::onBS4Screen(int leaving) { story_->onBS4Screen(leaving); }
bool BiosGame::onBS4Query(int id, void* buffer) { return story_->onBS4Query(id, buffer); }
void BiosGame::onTextInputDone(bool cancelled, const std::string& text) { story_->onTextInputDone(cancelled, text); }
void BiosGame::onHotspotHover(const std::string& name, int window, bool entered) { story_->onHotspotHover(name, window, entered); }
void BiosGame::onSimStatusHotspot(const std::string& name) { story_->onSimStatusHotspot(name); }
bool BiosGame::onEvent35(const std::string& page) { return story_->onEvent35(page); }
void BiosGame::onReturnToSavedPage(const std::string& page) { story_->onReturnToSavedPage(page); }
void BiosGame::onRightClick() { story_->onRightClick(); }
void BiosGame::onMouseDelta(int& dx, int& dy) { story_->onMouseDelta(dx, dy); }
void BiosGame::onStatusBarObject(int index, const std::string& name, int arg) { story_->onStatusBarObject(index, name, arg); }
bool BiosGame::onEvent44() { return story_->onEvent44(); }

// dll:10021890, the journey branch: the idle event hands over to CPath::TravelStep
int StoryLogic::idleTravel() {
    CPath* p = path();
    return p ? (CPath_TravelStep(*p) ? 1 : 0) : 0;
}

int StoryLogic::gameEndPageStage() {
    CGameEndPage* p = dynamic_cast<CGameEndPage*>(world().findPage("eGameEnd"));
    return p ? p->stage() : -1;
}

int StoryLogic::podState(CFeature& f) {
    auto* p = dynamic_cast<CPodTransport*>(&f);
    return p ? p->podState() : -1;
}
double StoryLogic::podCharge(CFeature& f) {
    auto* p = dynamic_cast<CPodTransport*>(&f);
    return p ? p->charge() : 0.0;
}

// feature vtable +0x90 (Event_OnTextInputDone): the classes that take text (the lift's password panel) override it; the base
// (dll:10019f10) does nothing
void StoryLogic::featureTextInput(CFeature& f, bool cancelled, const std::string& text) {
    if (auto* lift = dynamic_cast<CLift*>(&f)) lift->onTextInput(cancelled, text);
    else if (auto* pod = dynamic_cast<CPodTransport*>(&f)) pod->onTextInput(cancelled, text);
}

// port: the `game ...` commands of the story code (BiosGame::debugCommand hands over the ones it does not know)
std::string StoryLogic::debugCommand(const std::string& args) {
    std::istringstream in(args);
    std::string cmd;
    in >> cmd;
    std::ostringstream out;
    if (cmd == "hotspot") {      // game hotspot NAME [PAGE]: what the engine does on a click: the event, then the hotspot's own action
        std::string name, pageName, opt;
        in >> name >> pageName >> opt;       // PAGE "-" = the current location; OPT "noaction": do not perform the hotspot's own action
        if (pageName.empty() || pageName == "-") pageName = loc();
        const size_t before = trace.size();
        int rc = b.onHotspot(name, -1, pageName);
        out << "game hotspot " << name << " rc=" << rc << " kind=" << (trace.size() > before ? trace.back().substr(name.size() + 1) : "-");
        if (rc == 1 && opt != "noaction") {
            bool ok = b.eng_.DoHotspotAction(name, -1);
            out << " action=" << (ok ? "ok" : "failed");
        }
        out << "\n";
    } else if (cmd == "pod") {      // game pod BUTTON [WORD]: a destination button of the pod console, then the typed password
        std::string button, word;
        in >> button >> word;
        CPodTransport* pod = dynamic_cast<CPodTransport*>(world().findFeature("fPod"));
        if (!pod) throw Error("game pod: no fPod feature");
        pod->onHotspot(button);
        if (!word.empty()) featureTextInput(*pod, false, word);
        uint64_t bits;
        std::memcpy(&bits, &b.save_->worldHeader.unk_298, 8);
        out << "game pod dest=0x" << std::hex << pod->destination() << " state=0x" << pod->podState() << " bits=" << std::dec
            << static_cast<uint32_t>(bits) << " position=0x" << std::hex << pod->position() << "\n";
    } else if (cmd == "lift") {     // game lift NAME [WORD]: the unlock text of a lift panel
        std::string name, word;
        in >> name >> word;
        CLift* lift = dynamic_cast<CLift*>(world().findFeature(name.c_str()));
        if (!lift) throw Error("game lift: no such lift " + name);
        if (!word.empty()) featureTextInput(*lift, false, word);
        uint64_t bits;
        std::memcpy(&bits, &b.save_->worldHeader.unk_298, 8);
        out << "game lift " << name << " state=" << lift->state() << " bits=" << static_cast<uint32_t>(bits) << " textfail="
            << world().state().features.lift.textFail << "\n";
    } else if (cmd == "hover") {
        std::string name, dir;
        in >> name >> dir;
        b.onHotspotHover(name, -1, dir == "in");
        out << "game hover " << name << " " << dir << "\n";
    } else if (cmd == "rclick") {
        b.onRightClick();
        out << "game rclick\n";
    } else if (cmd == "type") {  // game type WORD [enter]
        std::string w, e;
        in >> w >> e;
        typeWord(w, e == "enter");
        out << "game type " << w << " cheats=" << cheatFlags << " last=" << lastCheat << "\n";
    } else if (cmd == "key") {
        int vk = 0;
        in >> vk;
        int dd = 1;
        b.onKeyDown(vk, 1, 0, dd);
        out << "game key " << vk << " doDefault=" << dd << "\n";
    } else if (cmd == "milestone") {
        unsigned long bit = 0;
        in >> std::hex >> bit;
        CGame_TriggerEvent(static_cast<uint32_t>(bit));
        out << "game milestone mask=0x" << std::hex << milestones() << "\n";
    } else if (cmd == "idle") {
        int n = 1, rc = 0;
        in >> n;
        for (int i = 0; i < n; i++) rc = b.onIdle(i < 2 ? i : 2);
        out << "game idle rc=" << rc << " path=" << (me().m_pPath ? me().m_pPath->name() : "-") << "\n";
    } else if (cmd == "story") {
        CPath* p = path();
        out << "game story milestones=0x" << std::hex << milestones() << std::dec << " site=" << (site() ? site()->name() : "-")
            << " page=" << (page() ? page()->name() : "-") << " loc=" << loc() << " path=" << (p ? p->pathName() : "-")
            << " journey=" << me().m_dwJourneySeconds << "s cost=" << me().m_dJourneyEnergy << " cheats=" << cheatFlags
            << " subtech=" << subtech.state << " arrival=" << b.save_->subTech.m_dwArrivalTime << " synth=" << b.g.synthState
            << " gameend=" << b.g.gameEnd << " dead=" << b.g.dead << " modal=" << b.g.noPalette << " score=" << score
            << " cause=" << me().m_wCauseOfDeath << " endstage=" << gameEndPageStage() << " lift=" << world().state().features.lift.current << "\n";
    } else if (cmd == "fuses") {     // port, tests only: every fuse of every fuse box plugged in and its circuit powered on (what the player does by hand)
        for (Item* it : world().state().features.fuseBox) {
            CFuseBox* box = dynamic_cast<CFuseBox*>(it);
            if (!box) continue;
            const uint32_t circuit = box->fuseMask() & 0x3f000000u;
            box->setFuseMask(box->fuseMask() | 0xfffffu);
            for (int i = 0; i < 20; i++) cworld::onCircuitPowerOn(world(), circuit | (1u << i));
        }
        out << "game fuses done\n";
    } else if (cmd == "ui") {
        for (const std::string& l : ui.lines) out << "ui " << l << "\n";
        ui.lines.clear();
    } else if (cmd == "trace") {
        for (const std::string& l : trace) out << "trace " << l << "\n";
        trace.clear();
    } else {
        throw Error("game: unknown subcommand '" + cmd + "' (dump settime seed fx sounds pals hotspot hover rclick type key milestone idle story ui trace)");
    }
    return out.str();
}

} // namespace biosys::game
