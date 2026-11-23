// CTimeline (dll:10015070..10015ab0): the game clock. Game time runs on a tick source (GetTickCount in the original,
// the injected engine clock here): `m_dwTicksPerGameSec` (0x28 = 40) real milliseconds make one game second, i.e. the
// game runs 25 times faster than real time; -1 freezes it. Time of day phases drive the site's palette and sounds.
#include "game/biosgame.h"
#include "game/feat_sensors.h"
#include <cstdio>

namespace biosys::game {

// dll:10015070 (the constructor of the static object; Timeline::Load overwrites everything but the diurnal state)
void BiosGame::CTimeline_Constructor() {
    Timeline& t = save_->timeline;
    t.m_dwTime = 1;
    t.m_dwDay = 1;
    t.m_dwTicksPerGameSec = 0x28;
    tl.lastTick = 0;
    tl.diurnal = 0x28;
    tl.paused = false;
}

// dll:100155e0
void BiosGame::CTimeline_UpdateTimeOfDay() {
    Timeline& t = save_->timeline;
    uint32_t tod = static_cast<uint32_t>(t.m_dwTime) % 0x15180;
    t.m_dwTimeOfDay = static_cast<i32>(tod);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", static_cast<int>(tod / 0xe10), static_cast<int>((tod % 0xe10) / 0x3c));
    tl.clock = b;
}

// dll:100153b0. Real time since the last call is turned into game seconds, unless it is 10 s or more (the machine was
// suspended): then the clock only restarts.
void BiosGame::CTimeline_AdvanceRealTime() {
    Timeline& t = save_->timeline;
    u32 now = tick();
    u32 elapsed = now - tl.lastTick;
    if (t.m_dwTicksPerGameSec == 0) throw Error("CTimeline::AdvanceRealTime: ticks per game second is 0 (division by zero in the original)");
    if (elapsed < 10000) t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + elapsed / static_cast<u32>(t.m_dwTicksPerGameSec));
    tl.lastTick = now;
    int day = static_cast<int>(static_cast<u32>(t.m_dwTime) / 0x15180 + 1);
    if (t.m_dwDay != day) {
        t.m_dwDay = day;
        CWorld_OnNewDay();
        CSubTechHandler_OnNewDay();
    }
    CTimeline_UpdateTimeOfDay();
    // FUN_10015630 (dll:10015630): stores the game time into a CTime member; no observable effect
}

// dll:10015410
void BiosGame::CTimeline_Start(uint32_t t) {
    if (t == 0 || t > 0x15180) log("Invalid start time: " + std::to_string(t));
    Timeline& rec = save_->timeline;
    rec.m_dwDay = 0;
    tl.lastTick = tick();
    rec.m_dwTime = 0x7080;
    rec.m_dwTimeOfDay = 0x7080;
}

// dll:10015470. Called by OnIdle (idle count 1) and OnTimer phase 0; at most once per 5000 ms.
void BiosGame::CTimeline_RealTimeTick() {
    if (tl.paused) return;
    u32 now = tick();
    if (now - g.lastRealTick > 4999) {
        g.lastRealTick = now;
        CTimeline_AdvanceRealTime();
        CTimeline_UpdateDiurnal(1, 1, 5, 1);
    }
}

// dll:100154b0: jumps the clock forward by `seconds` game seconds, running the simulation in 90 s steps.
void BiosGame::CTimeline_Advance(uint32_t seconds, int updateSelf) {
    Timeline& t = save_->timeline;
    CEngineFX_LockSounds(2);
    g.silent = true;
    if (t.m_dwTicksPerGameSec != -1) {
        uint32_t steps = (seconds + 0x5a) / 0x5a;
        uint32_t n = steps;
        do {
            CWorld_UpdateWeather();
            COcean_UpdateState();
            CWorld_UpdateAirFlows();
            CWorld_UpdateWaterFlows();
            CWorld_UpdateAllObjects();
            CWorld_UpdateAllFeatures();
            CWorld_UpdateAllPlants();
            if (updateSelf != 0) CSelf_Update(false);
            t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + 0x5a);
            CTimeline_UpdateTimeOfDay();
            CTimeline_UpdateDiurnal(0, 0, 0, 1);
            n--;
        } while (n != 0);
        if (seconds < steps * 0x5a) {
            t.m_dwTime = static_cast<i32>(static_cast<u32>(t.m_dwTime) + seconds - steps * 0x5a);
            CTimeline_UpdateTimeOfDay();
            CTimeline_UpdateDiurnal(0, 0, 0, 1);
        }
    }
    tl.lastTick = tick();
    CEngineFX_UnlockSounds(2);
    g.silent = false;
}

// dll:10015ab0
void BiosGame::CTimeline_SetTicksPerGameSec(int ticks) {
    CTimeline_AdvanceRealTime();
    save_->timeline.m_dwTicksPerGameSec = ticks;
}

// dll:100156e0 (called by OnAppActiveChanged): the clock stops while the window is inactive
void BiosGame::CTimeline_SetPaused(int active) {
    if (active == 0) {
        CTimeline_AdvanceRealTime();
        tl.paused = true;
        return;
    }
    tl.paused = false;
    tl.lastTick = tick();
}

// dll:10015710. p1: apply the sounds, p2: change the palette, p3/p4: fade steps / immediate flag handed to the site.
// The six time-of-day phases (state codes of CTimeline +0x2c): 0x28 night, 5 dawn (03:00-06:00), 0xf morning
// (06:00-08:00), 0x14 day (08:00-16:00), 0x1e evening (16:00-18:00), 0x23 dusk (18:00-21:00). `frac` is the progress
// inside the phase; +0x20 is the daylight level (0 night .. 1 day).
void BiosGame::CTimeline_UpdateDiurnal(int p1, int p2, int p3, int p4) {
    uint32_t tod = static_cast<uint32_t>(save_->timeline.m_dwTimeOfDay);
    CSite* site = curSite();   // the original dereferences DAT_100c1f84 unchecked
    auto change = [&](int state, double frac) { if (site) CSite_OnTimeOfDayChange(*site, p1, p2, state, p3, p4, frac); };
    auto ambient = [&](int slot, double w) { if (site) CSite_RandomAmbient(*site, slot, w); };
    if (tod < 0x2a30 || tod > 0x1274f) {
        tl.daylight = 0.0;
        tl.diurnal = 0x28;
        change(0x28, 1.0);
        if (p1 != 0) ambient(0x28, 1.0);
    } else if (tod < 0x5460) {
        double f = static_cast<double>(tod - 0x2a30) * 9.259259259259259e-05;
        tl.diurnal = 5;
        tl.daylight = f * 0.5;
        change(5, f);
        if (p1 != 0) {
            ambient(10, f);
            ambient(0x28, 1.0 - f);
        }
    } else if (tod < 0x7080) {
        double f = static_cast<double>(tod - 0x5460) * 0.0001388888888888889;
        tl.diurnal = 0xf;
        tl.daylight = f * 0.5 + 0.5;
        change(0xf, f);
        if (p1 != 0) {
            ambient(0x14, f);
            ambient(10, 1.0 - f);
        }
    } else if (tod < 0xe100) {
        tl.daylight = 1.0;
        tl.diurnal = 0x14;
        change(0x14, 1.0);
        if (p1 != 0) ambient(0x14, 1.0);
    } else if (tod < 0xfd20) {
        double f = static_cast<double>(tod - 0xe100) * 0.0001388888888888889;
        tl.diurnal = 0x1e;
        tl.daylight = f * -0.5 + 1.0;
        change(0x1e, f);
        if (p1 != 0) {
            ambient(0x14, 1.0 - f);
            ambient(0x20, f);
        }
    } else {
        double f = static_cast<double>(tod - 0xfd20) * 9.259259259259259e-05;
        tl.diurnal = 0x23;
        tl.daylight = f * -0.5 + 0.5;
        change(0x23, f);
        if (p1 != 0) {
            ambient(0x20, 1.0 - f);
            ambient(0x28, f);
        }
    }
    gs().sim.diurnal = tl.diurnal;      // the machines read DAT_100c1c44 / timeline+0x20 (CHeater::Thermostat, CSolarGen)
    gs().sim.daylight = tl.daylight;
}

// dll:10014ee0 CWorld::OnNewDay: CBiomeSensor::OnNewDay (dll:1005e860) for every sensor (world+0x12c), CBiomeCel::OnNewDay
// (dll:100189a0: clears flag 8) for every cel (world+0x14c)
void BiosGame::CWorld_OnNewDay() {
    for (Item* it : world().related(Rel::WorldSensor, nullptr)) as<CBiomeSensor>(it).onNewDay();
    for (Item* it : world().list(Table::Cels)) {
        CBiomeCel& c = as<CBiomeCel>(it);
        c.setFlags(c.flags() & 0xfffffff7u);
    }
}

// dll:10022f50 CSubTechHandler::OnNewDay: `if (arrivalTime < gameTime) CGame::TriggerEvent(0x80000000)`
void BiosGame::CSubTechHandler_OnNewDay() {
    if (static_cast<uint32_t>(save_->subTech.m_dwArrivalTime) < static_cast<uint32_t>(save_->timeline.m_dwTime))
        CGame_TriggerEvent(0x80000000u);
}

} // namespace biosys::game
