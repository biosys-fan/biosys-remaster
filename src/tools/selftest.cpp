// biosys-selftest: runs the player model (Self) and the game objects headlessly on a saved game, driven by a script.
//   biosys-selftest run <file.sav> <script> [--seed N]
// The script language and the dump format are in tests/self_protocol.txt; tests/self_oracle.py reads the same scripts.
// The world is frozen: only the player and the objects update (no Sim).
#include "game/biomecel.h"
#include "game/featenv.h"
#include "game/gameobj.h"
#include "game/page.h"
#include "game/savegame.h"
#include "game/self.h"
#include "game/self_display.h"
#include "game/self_text.gen.h"
#include "game/site.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace biosys;
using namespace biosys::game;

namespace {

struct Host : PlayerHost {
    Self* self = nullptr;
    World* world = nullptr;
    Timeline* tl = nullptr;
    u32 tick = 0;
    u32 getTickCount() override { return tick; }
    bool ChangeGameObjectVariation(const char*, const char*, int) override { return true; }
    bool playSoundHandle(const char*, int*) override { return true; }
    int jumpToPage(const char*, int, int, int, int) override { return 1; }
    void CEngineFX_PlayRandomSound(const char* stem, unsigned n) override {}
    void CTimeline_SetTicksPerGameSec(int t) override { tl->m_dwTicksPerGameSec = t; }
    // dll:100154b0 CTimeline::Advance with the world simulation left out (tests/self_protocol.txt)
    void advanceTime(unsigned seconds, int updateSelf) override {
        if (tl->m_dwTicksPerGameSec == -1) return;
        auto stepTime = [&](unsigned dt) {
            tl->m_dwTime += static_cast<i32>(dt);
            tl->m_dwTimeOfDay = static_cast<i32>(static_cast<u32>(tl->m_dwTime) % 0x15180);
        };
        unsigned n = (seconds + 0x5a) / 0x5a, left = n;
        do {
            cworld::updateAllObjects(*world, *self, tl->m_dwTime);
            if (updateSelf) self->Update(0);
            stepTime(0x5a);
        } while (--left != 0);
        if (seconds < n * 0x5a) {
            // the original adds (seconds - n*90) as unsigned: a wrap-around subtraction of the overshoot
            tl->m_dwTime = tl->m_dwTime + static_cast<i32>(seconds) - static_cast<i32>(n * 0x5a);
            tl->m_dwTimeOfDay = static_cast<i32>(static_cast<u32>(tl->m_dwTime) % 0x15180);
        }
    }
};

std::vector<std::string> split(const std::string& l) {
    std::vector<std::string> t;
    std::istringstream is(l);
    std::string w;
    while (is >> w) t.push_back(w);
    return t;
}

double num(const std::string& s) { return std::strtod(s.c_str(), nullptr); }
long long inum(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 0); }

std::string upper(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); return s; }

struct Runner {
    SaveGame sg;
    Host host;
    RecordingPlayerDisplay disp;
    size_t shown = 0;

    explicit Runner(const SaveGame::Options& o) : sg(o) {}

    void setMember(const std::string& name, const std::string& v) {
        Self& s = sg.self;
#define M(T, N) if (name == #N) { s.N = static_cast<T>(std::is_floating_point<T>::value ? num(v) : static_cast<double>(inum(v))); return; }
#include "selftest_members.inc"
#undef M
        throw Error("self: unknown or non-scalar member " + name);
    }

    void dump(const std::vector<std::string>& t) {
        std::printf("== t=%d seed=%u\n", sg.timeline.m_dwTime, sg.world.state().rng.state());
        for (auto& kv : sg.self.dumpAll()) std::printf("self.%s = %s\n", kv.first.c_str(), kv.second.c_str());
        std::printf("global.dead = %d\n", host.dead());
        for (size_t i = 1; i + 1 < t.size(); i++) {
            if (t[i] != "obj") continue;
            GameObj* o = sg.world.findObject(t[i + 1].c_str());
            if (!o) throw Error("dump: no object " + t[i + 1]);
            ObjMembers m;
            o->dumpMembers(m);
            for (auto& kv : m) std::printf("obj.%s.%s = %s\n", o->name().c_str(), kv.first.c_str(), kv.second.c_str());
        }
        const auto& msgs = disp.messages();
        for (; shown < msgs.size(); shown++) std::printf("msg %zu %d %s\n", shown, msgs[shown].stringId, msgs[shown].text.c_str());
    }

    void command(const std::vector<std::string>& t) {
        Self& s = sg.self;
        const std::string& c = t[0];
        if (c == "seed") sg.world.state().rng.seed(static_cast<u32>(inum(t.at(1))));
        else if (c == "tick") host.tick = static_cast<u32>(inum(t.at(1)));
        else if (c == "time") {
            sg.timeline.m_dwTime = static_cast<i32>(inum(t.at(1)));
            sg.timeline.m_dwTimeOfDay = static_cast<i32>(static_cast<u32>(sg.timeline.m_dwTime) % 0x15180);
        } else if (c == "ticks") sg.timeline.m_dwTicksPerGameSec = static_cast<i32>(inum(t.at(1)));
        else if (c == "self") setMember(t.at(1), t.at(2));
        else if (c == "cel") {
            CBiomeCel& cl = sg.world.requireBiomeCel(t.at(1).c_str());
            for (size_t i = 2; i < t.size(); i++) {
                size_t e = t[i].find('=');
                std::string k = t[i].substr(0, e);
                double v = num(t[i].substr(e + 1));
                if (k == "airTemp") cl.setAirTemp(v);
                else if (k == "humidity") cl.setHumidity(v);
                else if (k == "oxygen") cl.setOxygen(v);
                else if (k == "co2") cl.setCO2(v);
                else if (k == "soilWater") cl.setSoilWater(v);
                else if (k == "wind") cl.setWindSpeed(v);
                else if (k == "pollution") cl.setPollution(v);
                else if (k == "plantHealth") cl.setPlantHealthAvg(v);
                else if (k == "flags") cl.setFlags(static_cast<u32>(inum(t[i].substr(e + 1))));
                else throw Error("cel: unknown key " + k);
            }
        } else if (c == "goto") {
            CSite& site = sg.world.requireSite(t.at(1).c_str());
            s.m_pSite.set(&site);
            s.m_pCel = site.cel();
            s.m_pBiome = site.biome();
            if (t.size() > 2) {
                std::string p = upper(t[2]);
                s.m_szLocation.fill(0);
                std::memcpy(s.m_szLocation.data(), p.data(), p.size());
                s.m_pPage = sg.world.findPage(p.c_str());
            }
        } else if (c == "update") s.Update(t.size() > 1 ? static_cast<int>(inum(t[1])) : 1);
        else if (c == "run") {
            i32 secs = static_cast<i32>(inum(t.at(1))), step = static_cast<i32>(inum(t.at(2)));
            int p = t.size() > 3 ? static_cast<int>(inum(t[3])) : 1;
            i32 elapsed = 0;
            do {
                sg.timeline.m_dwTime += step;
                sg.timeline.m_dwTimeOfDay = static_cast<i32>(static_cast<u32>(sg.timeline.m_dwTime) % 0x15180);
                elapsed += step;
                s.Update(p);
            } while (elapsed < secs && !host.dead());
        } else if (c == "advance") host.advanceTime(static_cast<unsigned>(inum(t.at(1))), t.size() > 2 ? static_cast<int>(inum(t[2])) : 1);
        else if (c == "consume") s.Consume(static_cast<u16>(inum(t.at(1))));
        else if (c == "drink") s.Drink(static_cast<i16>(inum(t.at(1))));
        else if (c == "event") dispatchObjectEvent(sg.world, s, t.at(1), static_cast<int>(inum(t.at(2))), t.size() > 3 ? t[3] : "");
        else if (c == "sleep") {
            int r = s.Sleep(static_cast<int>(inum(t.at(1))), static_cast<int>(inum(t.at(2))), static_cast<int>(inum(t.at(3))));
            if (r) {
                s.m_bRunSleepOnIdle = 1;
                for (int i = 0; i < 10; i++) {
                    int rc = s.OnIdle();
                    if (rc == 0 && (s.m_bAwake || host.dead())) break;
                }
            }
        } else if (c == "idle") {
            int n = t.size() > 1 ? static_cast<int>(inum(t[1])) : 1;
            for (int i = 0; i < n; i++) s.OnIdle();
        } else if (c == "die") s.Die(static_cast<u16>(inum(t.at(1))));
        else if (c == "symptoms") {   // string id and text of every symptom bit (CSelf::GetSymptomStringID dll:1006a8e0)
            for (int b = 0; b < 32; b++) {
                int id = Self::GetSymptomStringID(1u << b);
                const char* text = id ? dllString(id) : "";
                std::printf("symptom %d %d %s\n", b, id, text ? text : "MISSING");
            }
        }
        else if (c == "roundtrip") {   // save after the actions, load into a fresh game, save again: the bytes must be equal
            std::vector<u8> a = sg.save();
            SaveGame::Options o;
            o.repairOriginalLoadBugs = true;
            SaveGame again(o);
            again.load(a);
            std::vector<u8> b = again.save();
            std::printf("roundtrip %s %zu\n", a == b ? "ok" : "DIFFERENT", a.size());
        }
        else if (c == "dump") dump(t);
        else throw Error("unknown command " + c);
    }
};

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 4 || std::string(argv[1]) != "run") throw Error("usage: biosys-selftest run <file.sav> <script> [--seed N]");
        SaveGame::Options o;
        long seed = 1;
        for (int i = 4; i + 1 < argc; i++)
            if (std::string(argv[i]) == "--seed") seed = std::atol(argv[i + 1]);
        auto r = std::make_unique<Runner>(o);
        r->sg.load(SaveGame::blobOf(readFile(argv[2])));
        r->host.self = &r->sg.self;
        r->host.world = &r->sg.world;
        r->host.tl = &r->sg.timeline;
        r->sg.self.bind(r->sg.world, &r->host, &r->disp);
        r->sg.world.state().rng.seed(static_cast<u32>(seed));
        std::ifstream f(argv[3]);
        if (!f) throw Error(std::string("cannot open ") + argv[3]);
        std::string line;
        int n = 0;
        while (std::getline(f, line)) {
            n++;
            size_t h = line.find('#');
            if (h != std::string::npos) line.resize(h);
            auto t = split(line);
            if (t.empty()) continue;
            try { r->command(t); }
            catch (const Error& e) { std::fprintf(stderr, "%s:%d: %s\n", argv[3], n, e.what()); return 1; }
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "biosys-selftest: %s\n", e.what());
        return 1;
    }
}
