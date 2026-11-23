// Water and ocean machines: CWaterPump, CPurifier (derives CWaterPump), CSiltScrubber, CDrainGauge, CTanksCabinet,
// CTideMachine, CWaveMachine, CAlgaeScrubber, COxygenStore, CBubbler. Notes: simulation.md section 8.2.
#pragma once
#include "game/feature.h"

namespace biosys::game {

class CWaterPump : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10005ae0. `srcCode` / `dstCode` are the SetParam codes 0x15 RFTANK, 0x16 STANK, 0x17 DBTANK, 0x18 OTANK;
    // `flow` is the CWaterFlow the pump switches.
    CWaterPump(World& w, const char* name, i32 a1, u32 a2, u16 id, i32 srcCode, i32 dstCode, CWaterFlow* flow);
    void saveSlot(SlotWriter& w) const override;   // dll:10005d60 (CFeature::Save only)
    void loadSlot(SlotReader& r) override;         // dll:10005d80 (CFeature::Load only)

    CWaterTank* srcTank() const { return m_pSrcTank; }
    CWaterTank* dstTank() const { return m_pDstTank; }
    CWaterFlow* flow() const { return m_pFlow; }

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:10005bb0
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10005cf0
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10005db0
    void update(i32 now) override;                                       // +0x58 dll:10005df0 (no timer: the base Update is not called)
    void startRunning() override;                                        // +0x80 dll:10005e20
    void stopRunning() override;                                         // +0x84 dll:10005e50

protected:
    // dll:10005a10 (vtable only) followed by the caller's CFeature__Init
    CWaterPump(World& w, u32 ctorAddr, const char* className, const char* name, u32 flags, u32 powerMask);

    i32 unk_58 = 0;                 // +0x58 set to 0 by the CWaterPump constructor
    CWaterTank* m_pSrcTank = nullptr;   // +0x5c source tank
    CWaterTank* m_pDstTank = nullptr;   // +0x60 destination tank
    i32 m_dwParam = 0;              // +0x64 last code passed to SetParam 0x13 / 0x14
    CWaterFlow* m_pFlow = nullptr;  // +0x68
};

class CPurifier final : public CWaterPump {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10023370. `src` / `dst` are tank names, `purity` the target purity (0.7 reverse osmosis, 1.0 UV).
    CPurifier(World& w, const char* name, u32 a1, i32 a2, u16 id, double purity, const char* src, const char* dst);
    // saved by CFeature::Save / Load through the pump's functions (dll:10005d60 / 10005d80)

    double purity() const { return m_dPurity; }

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10023510
    void startRunning() override;                                        // +0x80 dll:100235a0
    void stopRunning() override;                                         // +0x84 dll:100235d0
    void breakDown() override;                                           // +0x88 dll:10023600 (the flow passes the source purity)
    void repair() override;                                              // +0x8c dll:10023620 (the flow's purity again)

private:
    double m_dPurity;               // +0x70 purity of the output (the arguments the constructor stores at +0x70/+0x74)
};

class CSiltScrubber final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10005e80. `flow` is the drain CWaterFlow it clears.
    CSiltScrubber(World& w, const char* name, i32 a1, u32 a2, CWaterFlow* flow);
    CWaterFlow* flow() const { return m_pFlow; }
    void update(i32 now) override;                                       // +0x58 dll:10005fa0
private:
    CWaterFlow* m_pFlow;            // +0x58
};

class CDrainGauge final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:1002ad70
    CDrainGauge(World& w, const char* name, i32 a1, i32 a2, CWaterFlow* flow);
    CWaterFlow* flow() const { return m_pFlow; }
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:1002ae70
private:
    CWaterFlow* m_pFlow;            // +0x58
};

class CTanksCabinet final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10004620. Four tank names (null-terminated names of CWaterTank items).
    CTanksCabinet(World& w, const char* name, u32 a1, i32 a2, const char* t1, const char* t2, const char* t3,
                  const char* t4);
    CWaterTank* tank(int i) const { return m_pTank[i]; }
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:100056c0 (a drink from the tap)
    void onEnterPage(const std::string& page) override;                  // +0x34 dll:10004760 (the shower page: the mirror's image)
    void onPageEntered(const std::string& page) override;                // +0x3c dll:100058e0 (the shower)
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:10005810 (a container is filled)
    void display(int redraw) override;                                   // +0x5c dll:100047b0 (disp_features.cpp)
private:
    void takeWater();               // the common tail of the three handlers above
    CWaterTank* m_pTank[4];         // +0x58, +0x5c, +0x60, +0x64
};

class CTideMachine final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10003d40
    CTideMachine(World& w, const char* name, u32 a1, u32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:10003ee0
    void loadSlot(SlotReader& r) override;         // dll:10003f10

    CWaterTank* tideTank() const { return m_pTideTank; }

    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10003e80
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10003f40
    void update(i32 now) override;                                       // +0x58 dll:10004330
    void display(int redraw) override;                                   // +0x5c dll:10003f70 (the big page: dll:10003f70)
    void slot60(const char* page, int arg) override;                     // +0x60 dll:100042b0 (tide on / off image)
    void startRunning() override;                                        // +0x80 dll:100043f0
    void stopRunning() override;                                         // +0x84 dll:10004440
private:
    CWaterTank* m_pTideTank;        // +0x58 "TIDE"
    i32 m_dwLastToggle = 0;         // +0x5c time of the last direction toggle (guess), saved as unk_5c
    i32 m_dwDirection = 0;          // +0x60 direction flag (guess), saved as unk_60
};

class CWaveMachine final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10070990
    CWaveMachine(World& w, const char* name, u32 a1, u32 a2);
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10070b90
    void display(int redraw) override;                                   // +0x5c dll:10070ab0
    void slot60(const char* page, int arg) override;                     // +0x60 dll:10070b10 (wave on / off image)
    void startRunning() override;                                        // +0x80 dll:10070be0 (ocean wave machine flag)
    void stopRunning() override;                                         // +0x84 dll:10070c10
};

class CAlgaeScrubber final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:100036c0
    CAlgaeScrubber(World& w, const char* name, u32 a1, u32 a2);
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10003a80
    void update(i32 now) override;                                       // +0x58 dll:10003ae0
    void display(int redraw) override;                                   // +0x5c dll:100037e0 (disp_features.cpp)
    void startRunning() override;                                        // +0x80 dll:10003b60
    void stopRunning() override;                                         // +0x84 dll:10003b80
};

class COxygenStore final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10025320
    COxygenStore(World& w, const char* name, u32 a1, u32 a2);
    void saveSlot(SlotWriter& w) const override;   // dll:10025500
    void loadSlot(SlotReader& r) override;         // dll:10025540

    double store() const { return m_dStore; }
    void setStore(double v) { m_dStore = v; }     // CBubbler::Update takes oxygen out of the store (+0x60)
    u16 mode() const { return m_wMode; }

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:100254b0
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10025450
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10025a70
    void onApply(GameObj& object, const std::string& hotspot) override;  // +0x44 dll:10025bc0 (not an apply: refreshes the page)
    void update(i32 now) override;                                       // +0x58 dll:10025bf0
    void display(int redraw) override;                                   // +0x5c dll:10025580 (disp_features.cpp)
    void startRunning() override;                                        // +0x80 dll:10025b10
    void stopRunning() override;                                         // +0x84 dll:10025b60
private:
    CBiomeCel* m_pCel;              // +0x58 "TO"
    double m_dStore;                // +0x60 stored oxygen (0..10)
    u16 m_wMode;                    // +0x68 saved as unk_68; the constructor sets 1
};

class CBubbler final : public CFeature {
public:
    void members(MemberList& out) const override;   // port only (tests)
    // dll:10077750
    CBubbler(World& w, const char* name, u32 a1, u32 a2, i32 a3);
    void saveSlot(SlotWriter& w) const override;   // dll:10077890
    void loadSlot(SlotReader& r) override;         // dll:100778c0

    u16 gasSource() const { return m_wGasSource; }
    // The builder statement `*(CObject **)(bubbler + 0x5c) = oxygenStore` (world_build.gen.cpp)
    void setOxygenStore(CFeature* s) { m_pOxygenStore = s; }
    CFeature* oxygenStore() const { return m_pOxygenStore; }

    bool setParam(i32 id, i32 value, double d) override;                 // +0x18 dll:100778f0
    bool getParam(i32 id, i32* outInt, double* outDbl) const override;   // +0x1c dll:10077980
    bool onHotspot(const std::string& hotspot) override;                 // +0x30 dll:10077b30
    void display(int redraw) override;                                   // +0x5c dll:10077a40
    void update(i32 now) override;                                       // +0x58 dll:10077d10
    void statusMessage(i32 id) override;                                 // +0x64 dll:100779f0
    void startRunning() override;                                        // +0x80 dll:10077c80
    void stopRunning() override;                                         // +0x84 dll:10077cd0
private:
    u16 m_wGasSource;               // +0x58 id of the bubble flow: 0xc1f air, 0xc20 stored oxygen
    CFeature* m_pOxygenStore;       // +0x5c the constructor stores its fourth argument (always 0); the builder then stores the COxygenStore
    CBiomeCel* m_pCel;              // +0x60 "O1"
};

} // namespace biosys::game
