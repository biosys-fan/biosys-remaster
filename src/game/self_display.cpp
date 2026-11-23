#include "game/self_display.h"

namespace biosys::game {

void LoggingPlayerDisplay::showMessage(int stringId, const std::string& text) {
    if (!sink_) return;
    std::string line = "player-message " + std::to_string(stringId) + " ";
    for (char c : text) {
        if (c == '\n') line += "\\n";
        else line += c;
    }
    sink_(line);
}

} // namespace biosys::game
