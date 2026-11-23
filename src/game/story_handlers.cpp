// The two hunters: CSubTechHandler (the helicopter, dll:10022930..10022f50) and CSynthHandler (the synth stalker,
// dll:100084c0..10008c10), and the cursor filter of CEngineFX that belongs to them (dll:100617d0, 10061820, 100618a0,
// 10061a30). Both handlers keep one record at DAT_100c24b8 / DAT_100c27e0 (Handler in story.h): +4 state (1 idle, 3 following /
// chase, 4 attack, 6 attack movie / shot), +8 horizontal position of the pursuer (0..2560), +0x10 time left, +0x18 the sound,
// +0x1c / +0x20 / +0x24 volume, frequency and pan of that sound. The SubTech arrival time (+0x28) is the saved member.
#include "game/story.h"
#include <cstdio>
#include <cstdlib>

namespace biosys::game {

// ---------------------------------------------------------------------------------------------------------------
// shared

// dll:10022e90 FUN_10022e90 (SubTech) / dll:10008c10 FUN_10008c10 (synth): volume, pan and frequency of the pursuer's sound
// from its distance (+0x10 remaining seconds) and its position (+0x8) relative to the panorama centre
void StoryLogic::handlerSoundParams(Handler& h) {
    h.volume = static_cast<int>(static_cast<int64_t>(h.timer * 0.05 * -500.0));
    int pageWidth = 0, startX = 0, centreX = 0;
    b.eng_.GetPanoramaInfo(pageWidth, startX, centreX);
    int32_t d = h.x - centreX;                    // local_8, the third output of the call
    (void)startX;
    int32_t alt = d + 0xa00;
    if (std::abs(d) > std::abs(alt)) d = alt;
    uint32_t a = static_cast<uint32_t>(std::abs(d));
    if (a > 0x280) a = 0x280 - a % 0x280;
    h.pan = static_cast<int>(static_cast<int64_t>(a * 1.5625));
    if (d < 0) h.pan = -h.pan;
    h.frequency = static_cast<int>(static_cast<int64_t>((20.0 - h.timer) * 1100.0 + 22000.0));
}

// dll:10022c10 FUN_10022c10 / dll:10008890 FUN_10008890: stop and close the pursuer's sound
void StoryLogic::closeHandlerSound(Handler& h) {
    if (h.sound == -1) return;
    int playing = 0;
    b.eng_.IsSoundPlaying(h.sound, playing);
    if (playing != 0) b.eng_.StopSound(h.sound);
    b.eng_.CloseSound(h.sound);
    h.sound = -1;
}
void StoryLogic::CSubTechHandler_CloseSound() { closeHandlerSound(subtech); }
void StoryLogic::CSynthHandler_CloseSound() { closeHandlerSound(synth); }

// ---------------------------------------------------------------------------------------------------------------
// CSubTechHandler

// dll:10022c00 FUN_10022c00: a page was entered
void StoryLogic::CSubTechHandler_OnEnterPage() {
    subtech.x = 0;
    subtech.state = 1;
}

// dll:10022f50 CSubTechHandler::OnNewDay: the arrival time has passed
void StoryLogic::CSubTechHandler_OnNewDay() {
    if (static_cast<uint32_t>(b.save_->subTech.m_dwArrivalTime) < static_cast<uint32_t>(b.save_->timeline.m_dwTime))
        CGame_TriggerEvent(0x80000000u);
}

// dll:10022930 CSubTechHandler::Update (idle, every 500 ms): once milestone 0x80000000 is set, on an unsheltered site of the
// pages C, D, R, S, at most every 300 s a roll (2 %) starts the helicopter chase; the chase sound follows the pursuer, after
// 10..20 s of chase the helicopter shoots
void StoryLogic::CSubTechHandler_Update() {
    const uint32_t now = b.tick();
    if (now - subtechLastUpdate < 500) return;
    subtechLastUpdate = now;
    const char c = locChar();
    if (c != 'C' && c != 'D' && c != 'R' && c != 'S') return;
    if ((milestones() & 0x80000000u) == 0) return;
    switch (subtech.state) {
    case 1: {
        const Tuning& tu = world().state().tuning;   // Tuning::huntCooldownSeconds / huntChancePercent
        if (!(now - subtechLastChase > static_cast<uint32_t>(tu.huntCooldownSeconds * 1000.0))) break;
        if (!b.chancePercent(tu.huntChancePercent)) break;
        CSite* s = site();
        if (!s || (s->flags() & 0x800000) != 0) break;                   // site +0x12 & 0x80: sheltered
        if (s->name().empty() || s->name()[0] != c) break;
        b.CEngineFX_PlaySound("sounds\\misc\\helicopt.wav", 0, 0);
        subtechLastChase = now;
        subtech.state = 3;
        subtech.x = static_cast<int>(static_cast<int64_t>(b.randRange(0.0, 2560.0)));
        subtech.timer = b.randRange(10.0, 20.0);
        handlerSoundParams(subtech);
        b.eng_.OpenSound("sounds\\misc\\chase1.wav", 0, true, subtech.frequency, subtech.pan, subtech.volume, subtech.sound);
        b.eng_.PlaySound(subtech.sound, 1);
        return;
    }
    case 3: {
        subtech.timer = subtech.timer - 0.2;
        if (subtech.timer < 0.0) {
            subtech.state = 4;
            int playing = 0;
            b.eng_.IsSoundPlaying(subtech.sound, playing);
            if (playing != 0) b.eng_.StopSound(subtech.sound);
            b.eng_.CloseSound(subtech.sound);
            subtech.sound = -1;
            handlerSoundParams(subtech);
            b.eng_.OpenSound("sounds\\misc\\chase2.wav", 0, true, 0, subtech.pan, 0, subtech.sound);
            b.eng_.PlaySound(subtech.sound, 1);
            subtechChase2 = now;
            return;
        }
        int playing = 0;
        b.eng_.IsSoundPlaying(subtech.sound, playing);
        if (playing == 0 && b.chancePercent(30.0)) {
            handlerSoundParams(subtech);
            b.eng_.SetSoundProperty(subtech.sound, "frequency", subtech.frequency);
            b.eng_.SetSoundProperty(subtech.sound, "volume", subtech.volume);
            b.eng_.SetSoundProperty(subtech.sound, "pan", subtech.pan);
            b.eng_.PlaySound(subtech.sound, 1);
            return;
        }
        break;
    }
    case 4:
        if (now - subtechChase2 > 2000) CSubTechHandler_Shoot();
        break;
    default:
        break;
    }
}

// dll:10022e00 CSubTechHandler::Shoot: the shot (gunkill.wav); the effects of the page stop
void StoryLogic::CSubTechHandler_Shoot() {
    if (subtech.state == 6) return;
    subtech.state = 6;
    (void)b.crand();      // _rand() * 3 / 0x7fff is the argument of a format string that has no conversion
    if (CPage* pg = page()) pageCancelFx(*pg);
    if (!b.eng_.OpenSound("sounds\\misc\\gunkill.wav", 0, true, 0, 0, 0, subtechGunSound)) {
        subtechGunSound = -1;
        return;
    }
    b.eng_.PlaySound(subtechGunSound, 1);
}

// dll:10022c60 CSubTechHandler::OnMediaEnd. Returns true when the event was the handler's (the caller then does not close the
// sound itself). kind 4 = sound.
bool StoryLogic::CSubTechHandler_OnMediaEnd(int handle, int kind) {
    if (subtechGunSound == handle && kind == 4) {         // the shot ended: the player is dead
        subtech.state = 1;
        b.CEngineFX_PlayRandomSound("sounds\\russell\\screams\\scream", 5);
        b.CEngineFX_FadeToColour(0xff, 0, 0, 0, 0x50, 0x36, 0);        // FUN_10060620
        b.CEngineFX_FadeToColour(0, 0, 0, 1, 100, 0x36, 0);            // FUN_100605f0
        b.eng_.CloseSound(subtechGunSound);
        b.g.dieOnUpdate = true;                                        // DAT_100c249c
        me().m_wCauseOfDeath = 0x14;                                   // DAT_100c24a0: subtech
        return true;
    }
    if (subtech.sound != handle || kind != 4) return false;
    if (subtech.state == 4) {
        subtech.sound = -1;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// CSynthHandler

// dll:100089a0 CSynthHandler::OnHotspot: a click on "SYNTH_GRR" provokes the attack at once (unless the hand holds something
// that scares them); while a synth movie is open every hotspot is swallowed
bool StoryLogic::CSynthHandler_OnHotspot(const std::string& upperName) {
    if (b.g.synthState == 1) return false;
    if (!iequals(upperName, "SYNTH_GRR")) return synthMovie != -1;
    GameObj* h = held();
    if (!h || (h->flags() & 0x400) == 0) {                // +0x15 & 4
        synth.timer = -1.0;                               // +0x10/+0x14 = 0xbff00000_00000000
        b.g.synthState = 3;
    }
    return true;
}

// dll:10008830 CSynthHandler::OnEnterPage: a page was entered. The pages F and A (feature and plant pages) keep an attack going
void StoryLogic::CSynthHandler_OnEnterPage(const std::string& pageName) {
    const char c = pageName.empty() ? '\0' : pageName[0];
    if (c != 'F' && c != 'A') {
        synth.x = 0;
        b.g.synthState = 1;
        b.CEngineFX_HotspotEnable("SYNTH", 0, -1);
        b.CEngineFX_HotspotEnable("SYNTH_GRR", 0, -1);
        return;
    }
    if (b.g.synthState == 4) CSynthHandler_Attack();
}

// dll:100084c0 CSynthHandler::Update (idle, every 500 ms): in the rainforest after milestone 0x10000, in the dead biome after
// 0x20000000, on an unsheltered site, a roll (the darker the likelier) starts the synth following the player; 10..20 half-
// second steps later it attacks
void StoryLogic::CSynthHandler_Update() {
    const uint32_t now = b.tick();
    if (now - synthLastUpdate < 500) return;
    synthLastUpdate = now;
    const char c = locChar();
    if (c == 'D') {
        if ((milestones() & 0x20000000u) == 0) return;
    } else {
        if (c != 'R') return;
        if ((milestones() & 0x10000u) == 0) return;
    }
    switch (b.g.synthState) {
    case 1: {
        if (!(now - synthLastFollow > 60000)) break;
        if (!b.chancePercent(1.0 - b.tl.daylight)) break;
        CSite* s = site();
        if (!s || (s->flags() & 0x800000) != 0) break;
        if (s->name().empty() || s->name()[0] != c) break;
        synthLastFollow = now;
        b.g.synthState = 3;
        synth.x = static_cast<int>(static_cast<int64_t>(b.randRange(0.0, 2560.0)));
        synth.timer = b.randRange(10.0, 20.0);
        handlerSoundParams(synth);
        b.eng_.OpenSound("sounds\\synth\\chase\\synfolw1.wav", 0, true, synth.frequency, synth.pan, synth.volume, synth.sound);
        b.eng_.PlaySound(synth.sound, 1);
        return;
    }
    case 2: {
        if (synth.sound == -1) {
            int n = static_cast<int>(static_cast<int64_t>(b.randRange(1.0, 7.0)));
            char f[64];
            std::snprintf(f, sizeof f, "sounds\\synth\\photo%d.wav", n);
            handlerSoundParams(synth);
            b.eng_.OpenSound(f, 0, true, synth.frequency, synth.pan, synth.volume, synth.sound);
        } else {
            handlerSoundParams(synth);
            b.eng_.SetSoundProperty(synth.sound, "pan", synth.pan);
        }
        int playing = 0;
        b.eng_.IsSoundPlaying(synth.sound, playing);
        if (playing == 0 && b.chancePercent(20.0)) {
            b.eng_.PlaySound(synth.sound, 1);
            return;
        }
        break;
    }
    case 3: {
        synth.timer = synth.timer - 1.0;
        if (synth.timer < 0.0) {
            b.g.synthState = 4;
            int playing = 0;
            b.eng_.IsSoundPlaying(synth.sound, playing);
            if (playing != 0) b.eng_.StopSound(synth.sound);
            b.eng_.CloseSound(synth.sound);
            synth.sound = -1;
            handlerSoundParams(synth);
            b.eng_.OpenSound("sounds\\synth\\chase\\synatak4.wav", 0, true, 0, synth.pan, 0, synth.sound);
            b.eng_.PlaySound(synth.sound, 1);
            synthAttackStart = now;
            return;
        }
        int playing = 0;
        b.eng_.IsSoundPlaying(synth.sound, playing);
        if (playing == 0 && b.chancePercent(30.0)) {
            handlerSoundParams(synth);
            b.eng_.SetSoundProperty(synth.sound, "frequency", synth.frequency);
            b.eng_.SetSoundProperty(synth.sound, "volume", synth.volume);
            b.eng_.SetSoundProperty(synth.sound, "pan", synth.pan);
            b.eng_.PlaySound(synth.sound, 1);
            return;
        }
        break;
    }
    case 4:
        if (now - synthAttackStart > 4000) CSynthHandler_Attack();
        break;
    default:
        break;
    }
}

// the part of the attacks (dll:10008a30, CSynthCave dll:1000b9a0 / 1000bb90) after the movie file is chosen: the movie is opened
// (colour key 0,255,255), prepared, the page's synth FX switched off, the hand's object dropped and put away (PORT: engine gap), the
// input blocked and the movie played. false = it could not be opened (the handle is -1).
bool StoryLogic::startAttackMovie(const std::string& file, int& handle) {
    if (!b.eng_.OpenAVIMovie(file, -1, 0, 0, true, 0, 0, b.fx.palette.data(), handle)) {
        handle = -1;
        return false;
    }
    const u8 key[4] = {0, 0xff, 0xff, 0};
    b.eng_.SetAVIMovieParam(handle, key);
    b.eng_.AviOption42(handle);
    b.eng_.PrepareSynthAVIMovie(handle);
    if (CPage* pg = page()) pageDisableFx(*pg, -0x63c0);
    // SendMessageA(hwnd, WM_KEYUP, 'D', 0xc0000001), then 'S' if something is still in the hand: the engine's key handler drops / puts away
    // the held object (the front end's Config::sendKeyUp puts the release into the engine); if it is still held the input is blocked
    if (held() && b.config_.sendKeyUp) b.config_.sendKeyUp(0x44, 0xc0000001u);
    if (held()) {
        if (b.config_.sendKeyUp) b.config_.sendKeyUp(0x53, 0xc0000001u);
        if (held()) b.eng_.EnableUserInput(0, 0);
    }
    b.eng_.PlayAVIMovie(handle, 1);
    return true;
}

// dll:10008a30 CSynthHandler::Attack: the attack movie (rainforest: attack0..3.avi, or attackr.avi on a feature / plant page;
// dead biome: attack.avi). Wearing the oxygen mask (breathing bit 0) cancels it.
void StoryLogic::CSynthHandler_Attack() {
    Self& s = me();
    if ((s.m_wBreathing & 1) != 0) {
        b.g.synthState = 1;
        return;
    }
    if (b.g.synthState == 6) return;
    b.g.synthState = 6;
    std::string file;
    const char c = locChar();
    if (s.m_pBiome && s.m_pBiome->addr == 0x100c1c50) {
        if (c != 'F' && c != 'A') {
            int r = b.crand();
            char f[64];
            std::snprintf(f, sizeof f, "movies\\synth\\rforest\\attack%d.avi", r & 3);
            file = f;
        } else {
            file = "movies\\synth\\rforest\\attackr.avi";
        }
    } else if (s.m_pBiome && s.m_pBiome->addr == 0x100c24e8) {
        file = "movies\\synth\\deadBiom\\attack.avi";
    } else {
        return;
    }
    if (CPage* pg = page()) pageCancelFx(*pg);
    if (!startAttackMovie(file, synthMovie)) return;
    CGame_TriggerEvent(0x100000);
}

// dll:100088e0 CSynthHandler::OnMediaEnd: kind 1 = movie, 4 = sound
bool StoryLogic::CSynthHandler_OnMediaEnd(int handle, int kind) {
    if (synthMovie == handle && kind == 1) {
        b.g.synthState = 1;
        b.CEngineFX_PlayRandomSound("sounds\\russell\\screams\\scream", 5);
        b.CEngineFX_FadeToColour(0xff, 0, 0, 0, 0x50, 0x36, 0);
        b.CEngineFX_FadeToColour(0, 0, 0, 1, 100, 0x36, 0);
        b.eng_.CloseAVIMovie(synthMovie);
        synthMovie = -1;
        Self_OnSynthAttack();
        b.eng_.RedrawScreen();
        return true;
    }
    if (synth.sound != handle || kind != 4) return false;
    if (b.g.synthState == 4) {
        synth.sound = -1;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// the cursor filter (CEngineFX +0x6c8..+0x8a4)

// dll:100617d0: gain B, feedback A and the window length N (at most 50 samples)
void StoryLogic::mouseFilterSet(double gain, double feedback, int window) {
    if (mf.window < 0x32) {
        mf.window = window;
        mf.gain = gain;
        mf.feedback = feedback;
        return;
    }
    b.eng_.WriteLog("CEngineFX::SetMouseFilter - too many samples");     // the text at dll:100bc464
}

// dll:10061820: forget the history
void StoryLogic::mouseFilterReset() {
    mf.sumX = mf.sumY = 0;
    mf.prevSumX = mf.prevSumY = 0;
    mf.lastX = mf.lastY = 0;
    mf.idxA = mf.window;
    mf.idxB = 0;
    for (int i = 0; i < 0x32; i++) mf.ringX[i] = mf.ringY[i] = 0;
    mf.active = 0;
}

// dll:100618a0 FUN_100618a0 (Event_OnMouseDelta): the smoothed cursor of the drunk player. The filter keeps the last N
// deltas; the output is gain * (sum - feedback * previous sum) / N
void StoryLogic::synthMouseDelta(int& dx, int& dy) {
    if (mf.skipNext) {                       // DAT_100bc0f0: the event our own posted move caused
        mf.skipNext = false;
        return;
    }
    mf.sumX += static_cast<double>(dx - mf.ringX[mf.idxB]);
    mf.sumY += static_cast<double>(dy - mf.ringY[mf.idxB]);
    mf.lastX = dx;
    mf.lastY = dy;
    dx = static_cast<int>(static_cast<int64_t>(((mf.sumX - mf.feedback * mf.prevSumX) / mf.window) * mf.gain));
    dy = static_cast<int>(static_cast<int64_t>(((mf.sumY - mf.prevSumY * mf.feedback) / mf.window) * mf.gain));
    mf.ringX[mf.idxB] = mf.lastX;
    mf.ringY[mf.idxB] = mf.lastY;
    mf.idxA = mf.idxA < mf.window ? mf.idxA + 1 : 0;
    mf.idxB = mf.idxB < mf.window ? mf.idxB + 1 : 0;
    mf.prevSumX = mf.sumX;
    mf.prevSumY = mf.sumY;
    mf.active = (dx == 0 && dy == 0) ? 0 : 1;
}

// dll:10061a30 FUN_10061a30 (idle): while the filter still has momentum a zero delta is fed through it every 34 ms
int StoryLogic::synthMouseIdle() {
    const uint32_t now = b.tick();
    if (mf.active != 0 && now - mf.lastStep > 0x21) {
        mf.lastStep = now;
        int dx = 0, dy = 0;
        mf.sumX += static_cast<double>(-mf.ringX[mf.idxB]);
        mf.sumY += static_cast<double>(dy - mf.ringY[mf.idxB]);
        mf.lastX = dx;
        mf.lastY = dy;
        dx = static_cast<int>(static_cast<int64_t>(((mf.sumX - mf.feedback * mf.prevSumX) / mf.window) * mf.gain));
        dy = static_cast<int>(static_cast<int64_t>(((mf.sumY - mf.prevSumY * mf.feedback) / mf.window) * mf.gain));
        mf.ringX[mf.idxB] = mf.lastX;
        mf.ringY[mf.idxB] = mf.lastY;
        mf.idxA = mf.idxA < mf.window ? mf.idxA + 1 : 0;
        mf.idxB = mf.idxB < mf.window ? mf.idxB + 1 : 0;
        mf.prevSumX = mf.sumX;
        mf.prevSumY = mf.sumY;
        if (dx == 0 && dy == 0) mouseFilterReset();
        else mf.active = 1;
        // GetScrollState, GetCursorPos, PostMessageA(hwnd, WM_MOUSEMOVE, 0, (y << 16) | (x + 0x140)); DAT_100bc0f0 = 1: the filtered delta goes
        // into the engine as a move from the centre of the main view (320, 240). The original queues the message and sets the flag at once, so
        // the event the message causes skips the filter; here the message is handled on the spot, so the flag goes up first (it stays up if
        // no event came, as it does in the original).
        int active = 0, deltaEvent = 0, released = 0, top = 0;   // (released is read uninitialised by the original when the state is not active)
        if (b.eng_.GetScrollState(active, deltaEvent, released, top) && b.config_.postMouseMove) {
            mf.skipNext = true;
            b.config_.postMouseMove(static_cast<int16_t>(dx) + 0x140, static_cast<int16_t>(dy) + 0xf0, released != 0);
        }
    }
    return mf.active;
}

} // namespace biosys::game
