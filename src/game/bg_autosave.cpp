// The saves the front end makes without the menu (autosave, F9): the conditions of the original's save and load keys and the
// menu commands 2 and 3 of Event_OnMenuCommand (dll:10008010) with a file name of the front end's choosing. Port code, no original.
#include "game/biosgame.h"
#include "game/story.h"

namespace biosys::game {

std::string BiosGame::loadBlocker() {
    Self& s = self();
    if (g.loading) return "a game is loading";
    if (s.m_bAwake == 0) return "the player is asleep";
    if (s.m_bInsideAngelSequence != 0 || s.m_bInsidePeyoteSequence != 0) return "a vision is running";
    if (g.synthState == 6 || story_->subtech.state == 6) return "a sequence is running";
    const char c = story_->locChar();
    if (c == 'U' || c == 'X') return "the player is in a closed place";
    return "";
}

std::string BiosGame::saveBlocker() {
    if (!g.gameRunning) return "no game is running";
    if (g.dead) return "the player is dead";
    if (g.gameEnd) return "the game has ended";
    return loadBlocker();
}

bool BiosGame::saveToFile(const std::string& fileName) {
    g.soundsOff = true;
    const bool ok = eng_.SaveGame(fileName);
    g.soundsOff = false;
    return ok;
}

bool BiosGame::loadFromFile(const std::string& fileName) {
    if (CSite* s = curSite()) {
        CSite_Exit(*s);
        CSite_CloseAllAmbient(*s);
    }
    if (CPage* p = curPage()) pageExit(*p);
    CWorld_LocalFxStop();
    self().ResetPlayer();
    g.loading = true;
    g.soundsOff = true;
    const bool ok = eng_.LoadGame(fileName, -1);
    g.soundsOff = false;
    g.loading = false;
    if (ok) {
        g.gameRunning = true;
        afterNewGameLoaded();
    }
    return ok;
}

} // namespace biosys::game
