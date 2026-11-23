// biosys-apitest: drives the engine's game-logic interface from a test GameLogic (needs the game data).
//   biosys-apitest mask      for every restriction mask: which API commands a handler running under it is refused
//   biosys-apitest real      the same through events the engine fires itself (hotspot click, page load / enter)
// Prints `key=value` lines; tests/test_api.py checks them against the table of interface.md 3.2.
#include "engine/devroot.h"
#include <cstdio>
#include <sys/stat.h>
#include "engine/engine.h"

using namespace biosys;

namespace {

std::string dataDir(const char* argv0) {
    std::string a = argv0 ? argv0 : "";
    size_t slash = a.rfind('/');
    std::string cands[] = {slash == std::string::npos ? "../data" : a.substr(0, slash) + "/../data", "data",
                           BIOSYS_DEV_ROOT "/port/data"};
    for (const std::string& c : cands) {
        struct stat st;
        if (stat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return c;
    }
    return cands[2];
}

// Calls a fixed set of commands from inside every event it overrides and prints the error codes.
struct ProbeLogic : GameLogic {
    Engine* e = nullptr;
    bool probing = false;
    std::string jumpOnEnter;       // OnEnterPage jumps there once (the nested jump must work: mask 0)
    std::string jumpResult;

    void probe(const char* event) {
        if (probing) return;
        probing = true;
        EngineApi& a = e->api();
        auto row = [&](const char* name, int rc) {
            std::printf("probe event=%s mask=%u cmd=%s rc=%d\n", event, e->control().mask(), name, rc);
        };
        int n = 0;
        std::string s;
        row("JumpToPage", a.JumpToPage("R4", "none", true, 0, 0, 0, 0));
        row("EnableHotspot", a.EnableHotspot(-1, "nothere", true, false, nullptr));
        row("DoHotspotAction", a.DoHotspotAction(-1, "nothere"));
        row("EnablePageTimer", a.EnablePageTimer(-1, "nothere", true));
        row("RedrawPage", a.RedrawPage());
        row("DisplayPopupPage", a.DisplayPopupPage("nothere"));
        row("GetHotspotNameByIndex", a.GetHotspotNameByIndex(-1, 100000, s));
        row("LoadGame", a.LoadGame("x", 0));
        row("RestartGame", a.RestartGame());
        row("ReleasePageImageSlot", a.ReleasePageImageSlot(-1, 0, 0));
        row("GetGameObjectCount", a.GetGameObjectCount(n));
        row("WriteLog", a.WriteLog("probe"));
        row("PlaySound", a.PlaySound(99, true));
        probing = false;
    }
    void onBS4Screen(int) override { probe("BS4Screen"); }                      // mask 1
    void onExitPage() override { probe("ExitPage"); }                           // mask 6
    void onPageLoaded(const std::string&, int w) override { if (w == -1) probe("PageLoaded"); }   // mask 4
    int onHotspot(const std::string&, int, const std::string&) override { probe("Hotspot"); return 1; }   // mask 8
    void onTimer() override { probe("Timer"); }                                 // mask 0
    void onEnterPage(const std::string& page) override {                       // mask 0
        if (!jumpOnEnter.empty()) {
            std::string to = jumpOnEnter;
            jumpOnEnter.clear();
            int rc = e->api().JumpToPage(to, "none", true, 0, 0, 0, 0);
            std::printf("enterjump from=%s to=%s rc=%d now=%s\n", page.c_str(), to.c_str(), rc, e->view().page().name.c_str());
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    try {
        std::string mode = argc > 1 ? argv[1] : "";
        ProbeLogic logic;
        Engine e(DataFiles::fromEnvironment(), dataDir(argv[0]), &logic, nullptr);
        logic.e = &e;
        e.startup();
        e.startNewGame();
        if (mode == "mask") {
            e.update(1000);
            e.control().onBS4Screen(0);
            std::printf("mask-after-event=%u\n", e.control().mask());
            e.control().onHotspot("h", -1, "R4");
            e.control().onPageLoaded("R4", -1);
            e.control().onExitPage();
            e.update(1000 + 5000);       // OnTimer after 5 s (mask 0)
            e.update(1000 + 5600);
            std::printf("mask-after-all=%u\n", e.control().mask());
        } else if (mode == "real") {
            // OnPageLoaded of a real page jump: the jump itself is refused (mask 4), the page stays
            int before = e.view().pageNumber();
            e.goToPage(e.view().findPage("O1"));
            std::printf("page-after-load-probe=%s (was %d)\n", e.view().page().name.c_str(), before);
            // a real click on a hotspot with a jump verb: OnHotspot (mask 8) cannot start a jump
            int pn = 0;
            for (int p = 1; p <= e.view().pageCount() && !pn; p++) {
                e.goToPage(p, false);
                for (int y = 60; y < 420 && !pn; y += 8)
                    for (int x = 4; x < 640 && !pn; x += 8) {
                        HitInfo h = e.view().hit(x, y);
                        if (h && h.hotspot->action.verb == VERB_JUMP) {
                            pn = p;
                            e.mouseMove(x, y, 0, 0);
                            std::string from = e.view().page().name;
                            e.mouseDown(Button::Left);
                            e.mouseUp(Button::Left);
                            std::printf("clicked page=%s hotspot=%s landed=%s\n", from.c_str(), h.hotspot->name.c_str(),
                                        e.view().page().name.c_str());
                        }
                    }
            }
            // OnEnterPage runs with mask 0: a nested jump works
            logic.jumpOnEnter = "R4";
            e.goToPage(e.view().findPage("O1"));
            std::printf("final=%s\n", e.view().page().name.c_str());
        } else {
            std::fprintf(stderr, "usage: biosys-apitest mask|real\n");
            return 2;
        }
        return 0;
    } catch (const Error& err) {
        std::fprintf(stderr, "biosys-apitest: %s\n", err.what());
        return 1;
    }
}
