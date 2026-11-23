#include "game/feature.h"
#include "game/world_ctors.gen.h"
#include "game/eng.h"
#include "game/feat_hostext.h"
#include "game/site.h"
#include <cstdio>

namespace biosys::game {

std::string checkedText(const char* s, size_t arrayBytes, const char* what) {
    std::string t = s ? s : "";
    if (t.size() + 1 > arrayBytes)
        throw Error(std::string(what) + ": text '" + t + "' does not fit in char[" + std::to_string(arrayBytes) + "]");
    return t;
}

// The part of the derived-class constructors that is CFeature::CFeature + CFeature__Init (dll:100193b0, 10019500).
CFeature::CFeature(World& w, u32 ctorAddr, const char* className, const char* name, u32 flags, u32 powerMask)
    : Item(ctorAddr, className, name ? name : ""), world_(&w) {
    m_szName = checkedText(name, 12, "CFeature::Init name");   // char[12] at +0x04; the next member is at +0x10
    m_dwFlags = flags;                    // +0x10
    m_dwPowerMask = powerMask;            // +0x14
    m_dwTimeLimit = -1;                   // +0x2c
    m_dwTimerStart = -1;                  // +0x24
    m_dwTimerEnd = -1;                    // +0x28
    unk_20 = -1;                          // +0x20
    m_bOn = 0;                            // +0x1c
    m_bRunning = 0;                       // +0x38
    m_bActive = 0;                        // +0x34
    m_wId = 0;                            // +0x18
    unk_54 = 0;                           // +0x54
    m_dMalfunctionProb = 0;               // +0x40
    m_dPowerUsage = 0;                    // +0x48
    m_bWorking = 1;                       // +0x30
    m_dwTimeLastUpdate = 0x7080;          // +0x50
}

// dll:10019480 FUN_10019480 (CFeature used directly; the original's constructor is a copy of the base one)
CFeature::CFeature(World& w, const char* name, i32 flags, i32 powerMask)
    : CFeature(w, 0x10019480, "CFeature", name, static_cast<u32>(flags), static_cast<u32>(powerMask)) {}

void CFeature::storeDword(u32 off, u32 v) {
    switch (off) {
    case 0x1c: m_bOn = static_cast<i32>(v); return;
    case 0x24: m_dwTimerStart = static_cast<i32>(v); return;
    case 0x28: m_dwTimerEnd = static_cast<i32>(v); return;
    case 0x2c: m_dwTimeLimit = static_cast<i32>(v); return;
    case 0x30: m_bWorking = static_cast<i32>(v); return;
    case 0x34: m_bActive = static_cast<i32>(v); return;
    case 0x38: m_bRunning = static_cast<i32>(v); return;
    case 0x50: m_dwTimeLastUpdate = static_cast<i32>(v); return;
    }
    throw Error(className() + " '" + name() + "': no member at object offset " + std::to_string(off));
}

u32 CFeature::peekDword(u32 off) const {
    switch (off) {
    case 0x1c: return static_cast<u32>(m_bOn);
    case 0x24: return static_cast<u32>(m_dwTimerStart);
    case 0x28: return static_cast<u32>(m_dwTimerEnd);
    case 0x2c: return static_cast<u32>(m_dwTimeLimit);
    case 0x30: return static_cast<u32>(m_bWorking);
    case 0x34: return static_cast<u32>(m_bActive);
    case 0x38: return static_cast<u32>(m_bRunning);
    case 0x50: return static_cast<u32>(m_dwTimeLastUpdate);
    }
    throw Error(className() + " '" + name() + "': no member at object offset " + std::to_string(off));
}

// dll:10019800 CFeature__Save
void CFeature::saveBase(SlotWriter& w) const {
    w.writeI32(m_bOn);
    w.writeI32(m_bWorking);
    w.writeI32(m_bActive);
    w.writeI32(m_bRunning);
    w.writeF64(m_dPowerUsage);
    w.writeI32(m_dwTimeLimit);
    w.writeI32(m_dwTimerStart);
    w.writeI32(m_dwTimerEnd);
    w.writeI32(m_dwTimeLastUpdate);
}

// dll:10019890 CFeature__Load
void CFeature::loadBase(SlotReader& r) {
    m_bOn = r.readI32("m_bOn");
    m_bWorking = r.readI32("m_bWorking");
    m_bActive = r.readI32("m_bActive");
    m_bRunning = r.readI32("m_bRunning");
    m_dPowerUsage = r.readF64("m_dPowerUsage");
    m_dwTimeLimit = r.readI32("m_dwTimeLimit");
    m_dwTimerStart = r.readI32("m_dwTimerStart");
    m_dwTimerEnd = r.readI32("m_dwTimerEnd");
    m_dwTimeLastUpdate = r.readI32("m_dwTimeLastUpdate");
}

void CFeature::loadBaseOriginalBug(SlotReader& r, bool fixed) {
    if (fixed) {
        loadBase(r);
        return;
    }
    r.readBytes("_ignored_base_block", 40);   // CFeature::Save writes the same 40 bytes over the buffer
}

void CFeature::saveSlot(SlotWriter& w) const {   // dll:10019800
    saveBase(w);
    if (carryTail_) w.writeBytes(tail_);
}

void CFeature::loadSlot(SlotReader& r) {         // dll:10019890
    loadBase(r);
    if (carryTail_) tail_ = r.takeRest("_class_specific");
    else r.finishPad();
}

// ---------------------------------------------------------------------------------------------------------------
// Behaviour of the base class (vtable 10090ae0)

namespace {
inline u32 u(i32 v) { return static_cast<u32>(v); }
}

void CFeature::putD(MemberList& out, const char* name, double v) { char b[40]; std::snprintf(b, sizeof b, "%.17g", v); out.emplace_back(name, b); }
void CFeature::putI(MemberList& out, const char* name, long long v) { out.emplace_back(name, std::to_string(v)); }
void CFeature::putX(MemberList& out, const char* name, u32 v) { char b[16]; std::snprintf(b, sizeof b, "0x%08x", v); out.emplace_back(name, b); }
void CFeature::putS(MemberList& out, const char* name, const std::string& v) { out.emplace_back(name, v); }
void CFeature::putP(MemberList& out, const char* name, const Item* v) { out.emplace_back(name, v ? v->name() : std::string("null")); }
void CFeature::members(MemberList& out) const {
    putS(out, "class", className());
    putS(out, "name", m_szName);
    putX(out, "m_dwFlags", m_dwFlags);
    putX(out, "m_dwPowerMask", m_dwPowerMask);
    putI(out, "m_wId", m_wId);
    putI(out, "m_bOn", m_bOn);
    putI(out, "unk_20", unk_20);
    putI(out, "m_dwTimerStart", m_dwTimerStart);
    putI(out, "m_dwTimerEnd", m_dwTimerEnd);
    putI(out, "m_dwTimeLimit", m_dwTimeLimit);
    putI(out, "m_bWorking", m_bWorking);
    putI(out, "m_bActive", m_bActive);
    putI(out, "m_bRunning", m_bRunning);
    putD(out, "m_dMalfunctionProb", m_dMalfunctionProb);
    putD(out, "m_dPowerUsage", m_dPowerUsage);
    putI(out, "m_dwTimeLastUpdate", m_dwTimeLastUpdate);
    putI(out, "unk_54", unk_54);
}

// dll:10019aa0 CFeature::InTimerWindow: is the time of day inside (start, end)? A window that wraps midnight has end <= start.
bool CFeature::inTimerWindow(u32 tod) const {
    u32 start = u(m_dwTimerStart), end = u(m_dwTimerEnd);
    if (end <= start) return start < tod || tod < end;
    return start < tod && tod < end;
}

// dll:10019580 CFeature::SetParam
bool CFeature::baseSetParam(i32 id, i32 value) {
    switch (id) {
    case 1:     // FEATURE_ON
        if (value == 0) turnOff(); else turnOn();
        break;
    case 2:     // FEATURE_ACTIVE
        if (value == 0) deactivate(); else activate();
        break;
    case 3: m_dwTimerStart = value; break;                 // FEATURE_TIME_ACTIVATE
    case 5: m_dwTimeLimit = value; break;                  // FEATURE_TIME_LIMIT
    case 6:     // FEATURE_DAILY_TIME_LIMIT: the window is `value` seconds long
        if (value == 0x15180 && m_dwTimerStart == 0) {
            m_dwTimerEnd = 0x15180;
        } else {
            u32 e = u(m_dwTimerStart) + u(value);
            m_dwTimerEnd = static_cast<i32>(e);
            m_dwTimerEnd = static_cast<i32>(e % 0x15180u);
        }
        break;
    default:
        return false;
    }
    update(cworld::gameTime(world()));                     // (**(vtable + 0x58))(DAT_100c1c1c)
    return true;
}

bool CFeature::setParam(i32 id, i32 value, double) { return baseSetParam(id, value); }

// dll:10019640 CFeature::GetParam
bool CFeature::baseGetParam(i32 id, i32* pi, double* pd) const {
    switch (id) {
    case 1: *pi = m_bOn; return true;
    case 2: *pi = m_bRunning; return true;                 // FEATURE_ACTIVE reads the running flag (+0x38)
    case 3: *pi = m_dwTimerStart; return true;
    case 5: *pi = m_dwTimeLimit; return true;
    case 6: {
        u32 start = u(m_dwTimerStart), end = u(m_dwTimerEnd);
        *pi = static_cast<i32>(end < start ? (end - start) + 0x15180u : end - start);
        return true;
    }
    case 7:     // FEATURE_MALFUNCTION
        *pi = (m_bWorking == 0 || cworld::hasPower(const_cast<World&>(world()), m_dwPowerMask) == 0) ? 1 : 0;
        return true;
    case 0x8e:  // FEATURE_POWER_USAGE: the one that is switched on (+0x34, not +0x38)
        *pd = m_bActive == 0 ? 0.0 : m_dPowerUsage * 3600.0;
        return true;
    default:
        return false;
    }
}

bool CFeature::getParam(i32 id, i32* pi, double* pd) const { return baseGetParam(id, pi, pd); }

// dll:10019930 CFeature::Dump
void CFeature::baseDump(std::vector<std::string>& lines) const {
    char buf[96];
    lines.push_back("\n");
    lines.push_back(m_szName);
    lines.push_back(m_bOn == 0 ? "m_bOn \t\tFALSE" : "m_bOn \t\tTRUE");
    lines.push_back(m_bWorking == 0 ? "m_bWorking \t\tFALSE" : "m_bWorking \t\tTRUE");
    lines.push_back(m_bActive == 0 ? "m_bActive \t\tFALSE" : "m_bActive \t\tTRUE");
    std::snprintf(buf, sizeof buf, "m_dMalfunctionProb \t\t%f", m_dMalfunctionProb);
    lines.push_back(buf);
    std::snprintf(buf, sizeof buf, "m_dPowerUsage \t\t%f", m_dPowerUsage);
    lines.push_back(buf);
    std::snprintf(buf, sizeof buf, "m_dwTimeLastUpdate \t\t%d", m_dwTimeLastUpdate);
    lines.push_back(buf);
}
void CFeature::dump(std::vector<std::string>& lines) const { baseDump(lines); }

// dll:10019a10 CFeature::Update (the daily timer)
void CFeature::update(i32 now) {
    u32 dt = u(now) - u(m_dwTimeLastUpdate);
    m_dwTimeLastUpdate = now;
    if (m_bActive == 0) {
        if (m_bOn != 0 && m_dwTimerStart != -1) {
            if (inTimerWindow(cworld::timeOfDay(world()))) activate();
        }
    } else {
        if (m_dwTimerEnd == -1) {
            if (dt < u(m_dwTimeLimit)) {
                m_dwTimeLimit = static_cast<i32>(u(m_dwTimeLimit) - dt);
                return;
            }
            m_dwTimeLimit = 0;
            deactivate();
            return;
        }
        if (!inTimerWindow(cworld::timeOfDay(world()))) {
            deactivate();
            return;
        }
    }
}

// dll:10019dc0 / 10019de0
void CFeature::turnOn() {
    m_bOn = 1;
    update(cworld::gameTime(world()));
}
void CFeature::turnOff() {
    m_bOn = 0;
    deactivate();
}

// dll:10019df0 / 10019e20
void CFeature::onPowerOn() {
    if (m_bActive != 0 && cworld::hasPower(world(), m_dwPowerMask) != 0) startRunning();
}
void CFeature::onPowerOff() {
    if (m_bRunning == 0 || (m_dwPowerMask & 0x80000000u) != 0) return;
    stopRunning();
}

// dll:10019e40 CFeature::Activate: unu_20 receives the activation time (the only place that writes it besides Init)
void CFeature::activate() {
    m_bActive = 1;
    if (m_bWorking != 0) {
        unk_20 = cworld::gameTime(world());   // param_1[8] = DAT_100c1c1c
        if (m_bRunning == 0) {
            if (cworld::hasPower(world(), m_dwPowerMask) != 0 || (m_dwPowerMask & 0x80000000u) != 0) startRunning();
        }
    }
}
// dll:10019e90
void CFeature::deactivate() {
    m_bActive = 0;
    if (m_bRunning != 0) stopRunning();
}
// dll:10019eb0 / 10019ed0
void CFeature::startRunning() {
    m_bRunning = 1;
    cworld::addPowerDemand(world(), m_dPowerUsage);
}
void CFeature::stopRunning() {
    cworld::removePowerDemand(world(), m_dPowerUsage);
    m_bRunning = 0;
}
// dll:10019ef0
void CFeature::breakDown() { m_bWorking = 0; }

// dll:10019b10: the message the player gets for a feature (id) unless the circuit has no power: then "no power" (0xbe)
void CFeature::statusMessage(i32 id) {
    if ((m_dwPowerMask & 0x80000000u) == 0 && cworld::hasPower(world(), m_dwPowerMask) == 0 &&
        (m_dwFlags & 0x8000000u) == 0)
        id = 0xbe;
    if (id != 0 && host()) host()->showMessage(id);
}

// CEngineFX::SetImageVisibility(image, visible, -1) dll:10060c70: true when the page has the image
bool CFeature::imgVis(const char* image, int visible) {
    Eng* e = host() ? host()->eng() : nullptr;
    return e && e->SetImageVisibility(image, -1, visible);
}
// FUN_10060c90: Eng_EnableHotspot(name, window -1, enable)
void CFeature::hotspotEnable(const char* name, int enable) {
    Eng* e = host() ? host()->eng() : nullptr;
    if (e) e->EnableHotspot(name, -1, enable, nullptr, 0);
}
// FUN_10060de0 (a jump to Eng_RedrawPage dll:100752a0, API 0x28): the visible images of the page are painted again.
// (Until 2026-11-22 the port called RedrawScreen here, API 0x49 dll:10076220, which only shows the page as it already is: a feature
// that changed its images after the page was drawn kept its old picture, e.g. the lift after a ride.)
void CFeature::redrawPage() {
    Eng* e = host() ? host()->eng() : nullptr;
    if (e) e->api().RedrawPage();
}

// dll:10019b70: the page's state images of the site the player is in
void CFeature::siteStateImages() {
    imgVis("XH", 0);
    imgVis("XU", 0);
    imgVis("XD", 0);
    imgVis("HEALTHY", 0);
    imgVis("DEAD", 0);
    CSite* site = host() ? host()->currentSite() : nullptr;
    if (!site) return;      // the original dereferences DAT_100c1f84
    switch (site->state()) {
    case 1:
        if (!imgVis("XD", 1)) imgVis("DEAD", 1);
        break;
    case 2:
        if (!imgVis("XH", 1)) imgVis("HEALTHY", 1);
        break;
    case 3:
    case 4:
        imgVis("XU", 1);
    }
}

// dll:10019c90 CFeature::EnterPage
void CFeature::onEnterPage(const std::string&) {
    if ((m_dwFlags & 2) == 0) display(0);
    if ((m_dwFlags & 8) != 0) siteStateImages();
    if ((m_dwFlags & 0x200) != 0 && host() && host()->eng()) host()->eng()->EnableUserInput(0, 1);
    statusMessage(0);
}

// dll:10019cd0 CFeature::OnPageDrawn
void CFeature::onPageDrawn(const std::string& page) {
    if (!page.empty() && page[0] == 'F' && (m_dwFlags & 0x20) != 0 && host()) host()->resetDiurnal();
    if ((m_dwFlags & 2) != 0) display(0);
}

// dll:10019d20 CFeature::OnPageEntered
void CFeature::onPageEntered(const std::string& page) {
    FeatureHost* h = host();
    if (!h) return;
    if ((m_dwFlags & 0x200000u) != 0) {
        if (cworld::hasPower(world(), m_dwPowerMask) != 0) {
            h->showMessage(0x140);
            return;
        }
    }
    const std::string& loc = globals().currentSiteName;           // DAT_100c1e24: where the player is (a feature page: 'F...')
    if (!loc.empty() && loc[0] == 'F') {
        if (h->helpTextsOn()) h->showMessage(0x171);
    }
}

// dll:10019d90
void CFeature::onLeavePage() {
    if ((m_dwFlags & 0x30) != 0 && host()) host()->forcePalette();
}

// dll:10019db0 (empty), 10019f80, 10019fc0 (empty)
void CFeature::onApply(GameObj&, const std::string&) {}
void CFeature::onClickAway() {
    if ((m_dwFlags & 0x400) != 0) {
        slot60(host() && host()->currentSite() ? host()->currentSite()->name().c_str() : nullptr, 1);   // (**(vtable + 0x60))(site name, 1)
        return;
    }
    if (unk_54 != 0) {
        if (host() && host()->eng()) host()->eng()->ClearWindow(-1, 0);   // CEngineFX::ClearWindow dll:10060df0
        if (host()) host()->forcePalette();
    }
}
void CFeature::onLeave() {}
bool CFeature::onHotspot(const std::string&) { return true; }   // dll:10019c70

// dll:10019f20 -> CEngineFX::JumpToPage(&CEngineFX, name, 0, 0, 1, 0)
void CFeature::jumpToOwnPage() {
    if (FeatureHostExt* x = hostExt(host())) x->jumpToPage(m_szName.c_str(), 0, 0, 1, 0);   // the transition "none" and the failure message are in the host's jump
}

BIOSYS_REGISTER_CTOR(10019480, [](World& w, const char* name, int32_t a1, int32_t a2) -> Item* {
    return w.adopt(std::make_unique<CFeature>(w, name, a1, a2));
});

} // namespace biosys::game
