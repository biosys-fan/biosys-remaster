#include "game/blobrecords.h"

namespace biosys::game {

std::string BlobHeader::descriptionText() const {
    size_t n = 0;
    while (n < description.size() && description[n]) n++;
    return std::string(description.begin(), description.begin() + static_cast<long>(n));
}

// dll:10008db0
void BlobHeader::saveSlot(SlotWriter& w) const {
    w.writeStr(magic, 13);
    w.writeBytes(description);
    w.writeU32(dllSaveVersion);
    w.writeU32(milestones);
}

// dll:10009080
void BlobHeader::loadSlot(SlotReader& r) {
    magic = r.readStr("magic", 13);
    if (magic != "JON IS A YOYO") throw Error("game blob: bad magic (not 'JON IS A YOYO')");
    description = r.readStrRaw("description", 100);
    dllSaveVersion = r.readU32("dllSaveVersion");
    milestones = r.readHex32("milestones");
    // the original checks the version after reading the milestone dword
    if (dllSaveVersion != kDllSaveVersion)
        throw Error("game blob: tried to load an object of version " + std::to_string(dllSaveVersion) +
                    ", expected " + std::to_string(kDllSaveVersion));
}

// dll:10011710 (CWorld::Save): dword copies from the CWorld members into a malloc(0x100) buffer
void WorldHeader::saveSlot(SlotWriter& w) const {
    w.writeF64(m_dBlueSky);
    w.writeI32(m_dwStamp_1c8);
    w.writeF64(m_dDayMaxExtTemp);
    w.writeF64(m_dDayMaxExtTempTarget);
    w.writeF64(m_dDayMaxExtTempLo);
    w.writeF64(m_dDayMaxExtTempHi);
    w.writeF64(m_dNightMinExtTemp);
    w.writeF64(m_dNightMinExtTempTarget);
    w.writeF64(m_dNightMinExtTempLo);
    w.writeF64(m_dNightMinExtTempHi);
    w.writeF64(m_dExtTemp);
    w.writeF64(m_dPower);
    w.writeU32(m_dwPowerFlags);
    w.writeF64(m_dPowerGeneration);
    w.writeF64(m_dPowerDemand);
    w.writeF64(m_dSunlight);
    w.writeF64(m_dWindSpeed);
    w.writeF64(m_dWindSpeedTarget);
    w.writeF64(m_dWindSpeedLo);
    w.writeF64(m_dWindSpeedHi);
    w.writeI32(m_dwStamp_1c4);
    w.writeI32(m_dwStamp_184);
    w.writeI32(m_dwStamp_164);
    w.writeI32(m_dwStamp_94);
    w.writeI32(m_dwStamp_184);          // world+0x184 is written a second time
    w.writeI32(m_dwStamp_1c0);
    w.writeF64(unk_298);
    w.writeI32(unk_2a0);
    w.writeBytes(uninit_tail);
}

// dll:10012590 (first 0x100 bytes)
void WorldHeader::loadSlot(SlotReader& r) {
    m_dBlueSky = r.readF64("m_dBlueSky");
    m_dwStamp_1c8 = r.readI32("m_dwStamp_1c8");
    m_dDayMaxExtTemp = r.readF64("m_dDayMaxExtTemp");
    m_dDayMaxExtTempTarget = r.readF64("m_dDayMaxExtTempTarget");
    m_dDayMaxExtTempLo = r.readF64("m_dDayMaxExtTempLo");
    m_dDayMaxExtTempHi = r.readF64("m_dDayMaxExtTempHi");
    m_dNightMinExtTemp = r.readF64("m_dNightMinExtTemp");
    m_dNightMinExtTempTarget = r.readF64("m_dNightMinExtTempTarget");
    m_dNightMinExtTempLo = r.readF64("m_dNightMinExtTempLo");
    m_dNightMinExtTempHi = r.readF64("m_dNightMinExtTempHi");
    m_dExtTemp = r.readF64("m_dExtTemp");
    m_dPower = r.readF64("m_dPower");
    m_dwPowerFlags = r.readHex32("m_dwPowerFlags");
    m_dPowerGeneration = r.readF64("m_dPowerGeneration");
    m_dPowerDemand = r.readF64("m_dPowerDemand");
    m_dSunlight = r.readF64("m_dSunlight");
    m_dWindSpeed = r.readF64("m_dWindSpeed");
    m_dWindSpeedTarget = r.readF64("m_dWindSpeedTarget");
    m_dWindSpeedLo = r.readF64("m_dWindSpeedLo");
    m_dWindSpeedHi = r.readF64("m_dWindSpeedHi");
    m_dwStamp_1c4 = r.readI32("m_dwStamp_1c4");
    m_dwStamp_184 = r.readI32("m_dwStamp_184");
    m_dwStamp_164 = r.readI32("m_dwStamp_164");
    m_dwStamp_94 = r.readI32("m_dwStamp_94");
    m_dwStamp_184 = r.readI32("m_dwStamp_184_dup");   // same member, second store wins
    m_dwStamp_1c0 = r.readI32("m_dwStamp_1c0");
    unk_298 = r.readF64("unk_298");
    unk_2a0 = r.readI32("unk_2a0");
    uninit_tail = r.readBytes("uninit_tail", 68);
}

// dll:100151c0
void Timeline::saveSlot(SlotWriter& w) const {
    w.writeI32(m_dwTicksPerGameSec);
    w.writeI32(m_dwTime);
    w.writeI32(m_dwDay);
    w.writeI32(m_dwTimeOfDay);
}

// dll:10015250
void Timeline::loadSlot(SlotReader& r) {
    m_dwTicksPerGameSec = r.readI32("m_dwTicksPerGameSec");
    m_dwTime = r.readI32("m_dwTime");
    m_dwDay = r.readI32("m_dwDay");
    m_dwTimeOfDay = r.readI32("m_dwTimeOfDay");
}

// dll:10022d10
void SubTechHandler::saveSlot(SlotWriter& w) const {
    w.writeI32(m_dwArrivalTime);
    w.skip(12);
}

// dll:10022d90 (the loader ignores the 12 zero bytes; the writer always stores zeros)
void SubTechHandler::loadSlot(SlotReader& r) {
    m_dwArrivalTime = r.readI32("m_dwArrivalTime");
    r.readZeros("zero_fill", 12);
}

// dll:1005ff30
void EngineFx::saveSlot(SlotWriter& w) const {
    w.writeU16(m_wState);
    w.writeBytes(uninit_tail);
}

// dll:1005ffb0
void EngineFx::loadSlot(SlotReader& r) {
    m_wState = r.readU16("m_wState");
    uninit_tail = r.readBytes("uninit_tail", 14);
}

void Trailer::saveSlot(SlotWriter& w) const { w.writeStr("THE END", 7); }

void Trailer::loadSlot(SlotReader& r) {
    if (r.readStr("trailer", 7) != "THE END") throw Error("game blob: trailer 'THE END' not found");
}

} // namespace biosys::game
