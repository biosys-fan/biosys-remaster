// biosys-firstruntest: unit cases of the first-run logic (src/app/firstrunlogic.h), SDL-free.
#include "app/firstrunlogic.h"
#include <cstdio>
#include <map>

using namespace biosys;
using namespace biosys::firstrun;

static int g_fail = 0, g_pass = 0;
static void check(const char* name, bool ok) {
    if (ok) { g_pass++; std::printf("PASS %s\n", name); }
    else { g_fail++; std::printf("FAIL %s\n", name); }
}

int main() {
    std::map<std::string, DiscKind> known = {{"setup.zip", DiscKind::SetupCd}, {"game.zip", DiscKind::GameCd}, {"setup2.iso", DiscKind::SetupCd}};
    auto ident = [&](const std::string& p, std::string* why) {
        auto it = known.find(p);
        if (it != known.end()) return it->second;
        if (why) *why = "This is a text file, not one of the two Biosys CDs.";
        return DiscKind::Unknown;
    };
    {
        Logic L(ident, {});
        check("start: both missing, no message", L.view(DiscKind::SetupCd).slot == Slot::Missing && L.view(DiscKind::GameCd).slot == Slot::Missing && L.message().empty());
        check("start: log line", L.logLine() == "firstrun: setup=missing game=missing msg=\"\"");
        L.drop("notes.txt");
        check("unknown drop: why sentence, nothing queued", L.message() == "This is a text file, not one of the two Biosys CDs." && !L.busy());
        check("unknown drop in the log", L.logLine().find("text file") != std::string::npos);
        L.drop("game.zip");
        check("known drop is queued and clears the message", L.view(DiscKind::GameCd).slot == Slot::Queued && L.message().empty());
        L.drop("setup.zip");
        check("two drops: both queued", L.view(DiscKind::SetupCd).slot == Slot::Queued && L.busy());
        L.drop("game.zip");
        check("same disc again while queued: says so", L.message().find("already") != std::string::npos && L.view(DiscKind::GameCd).slot == Slot::Queued);
        Job j;
        check("first job is the first drop", L.takeJob(j) && j.kind == DiscKind::GameCd && j.path == "game.zip");
        check("one at a time", !L.takeJob(j));
        check("job taken: unpacking 0", L.view(DiscKind::GameCd).slot == Slot::Unpack && L.view(DiscKind::GameCd).pct == 0 && L.view(DiscKind::SetupCd).slot == Slot::Queued);
        L.progress(DiscKind::GameCd, "unpack", 43, 100);
        check("unpack 43 %", L.view(DiscKind::GameCd).pct == 43 && L.stateText(L.view(DiscKind::GameCd)) == "Unpacking\xE2\x80\xA6 43 %");
        check("log: unpack", L.logLine() == "firstrun: setup=queued game=unpack 43 msg=\"\"");
        L.progress(DiscKind::GameCd, "extract", 1, 4);
        check("extract 25 %", L.view(DiscKind::GameCd).slot == Slot::Extract && L.stateText(L.view(DiscKind::GameCd)) == "Copying\xE2\x80\xA6 25 %");
        L.progress(DiscKind::GameCd, "extract", 5, 4);
        check("progress is capped at 100", L.view(DiscKind::GameCd).pct == 100);
        L.progress(DiscKind::SetupCd, "extract", 1, 2);
        check("progress of a disc that is not running is ignored", L.view(DiscKind::SetupCd).slot == Slot::Queued);
        L.finished(DiscKind::GameCd);
        check("finished: done, bar full", L.view(DiscKind::GameCd).slot == Slot::Done && L.barFraction(L.view(DiscKind::GameCd)) == 1.0 && !L.allSet());
        L.drop("game.zip");
        check("dropping a done disc says so", L.message().find("already imported") != std::string::npos);
        check("next job", L.takeJob(j) && j.kind == DiscKind::SetupCd);
        L.failed(DiscKind::SetupCd, "not enough space");
        check("error: back to missing, message shown", L.view(DiscKind::SetupCd).slot == Slot::Missing && L.message().find("not enough space") != std::string::npos && !L.busy());
        L.drop("setup2.iso");
        check("retry after an error", L.view(DiscKind::SetupCd).slot == Slot::Queued && L.message().empty());
        check("retry job", L.takeJob(j) && j.path == "setup2.iso");
        L.cancelled(DiscKind::SetupCd);
        check("cancel: missing again, no message", L.view(DiscKind::SetupCd).slot == Slot::Missing && L.message().find("The Game CD is done") == 0);
        L.drop("setup.zip");
        L.takeJob(j);
        L.finished(DiscKind::SetupCd);
        check("both done: all set", L.allSet() && L.message() == "All set. Starting the game\xE2\x80\xA6");
        L.drop("game.zip");
        check("nothing is queued once all is set", !L.busy());
    }
    {
        Logic L(ident, install::Status{true, false});
        check("earlier run: setup is done from the start", L.view(DiscKind::SetupCd).slot == Slot::Done && L.view(DiscKind::GameCd).slot == Slot::Missing && !L.allSet());
        check("hint names the other disc", L.message() == "The Setup CD is done. Now drop the Game CD.");
        L.drop("setup.zip");
        check("dropping the imported disc again: told so", L.message().find("already imported") != std::string::npos && !L.busy());
        L.drop("game.zip");
        check("hint gone while something is queued", L.message().empty());
        Logic M(ident, install::Status{true, true});
        check("both imported before: all set", M.allSet());
    }
    {
        Logic L(ident, {});
        L.setDragging(true);
        check("dragging flag", L.dragging());
        L.setDragging(false);
        L.note("hello");
        check("note", L.message() == "hello");
        check("dragging flag off", !L.dragging());
        check("state texts", Logic::stateText(DiscView{Slot::Missing, 0}) == "Not here yet" && Logic::stateText(DiscView{Slot::Queued, 0}) == "Waiting\xE2\x80\xA6" &&
                                 Logic::stateText(DiscView{Slot::Done, 100}) == "Done");
    }
    {
        check("hit: download button", hitTest(700, 180) == Hit::Download);
        check("hit: choose button", hitTest(700, 260) == Hit::Choose);
        check("hit: outside", hitTest(10, 10) == Hit::None && hitTest(579, 180) == Hit::None && hitTest(860, 180) == Hit::None);
        Fit f = fitDesign(960, 600);
        check("fit: 1:1", f.scale == 1 && f.offX == 0 && f.offY == 0);
        f = fitDesign(1920, 1200);
        check("fit: 2x", f.scale == 2 && f.toDesignX(1920) == 960);
        f = fitDesign(1440, 600);
        check("fit: wide window is centred", f.scale == 1 && f.offX == 240 && f.toDesignX(240) == 0 && f.toDesignX(1200) == 960);
        f = fitDesign(960, 1000);
        check("fit: tall window is centred", f.scale == 1 && f.offY == 200 && f.toDesignY(200) == 0);
        check("layout: cards and buttons inside the design", kSetupCard.x + kSetupCard.w < kGameCard.x && kGameCard.x + kGameCard.w <= kDesignW &&
                                                               kDownloadButton.y + kDownloadButton.h < kChooseButton.y && kMessageArea.y > kSetupCard.y + kSetupCard.h);
    }
    std::printf("%d checks, %d failed\n", g_pass + g_fail, g_fail);
    if (g_fail == 0) std::printf("all checks passed\n");
    return g_fail ? 1 : 0;
}
