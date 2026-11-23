// GdiPlayerDisplay: the player's text output drawn through the engine's DC (CSelf::ShowMessage dll:1006bc40, FUN_1006c700), with
// every string also recorded (LoggingPlayerDisplay's "player-message" lines and GdiSession's record of the drawn text).
#pragma once
#include "game/biosgame.h"
#include "game/self_display.h"

namespace biosys::game {

class GdiPlayerDisplay final : public LoggingPlayerDisplay {
public:
    GdiPlayerDisplay(BiosGame& g, Sink sink) : LoggingPlayerDisplay(std::move(sink)), g_(g) {}
    void showMessage(int stringId, const std::string& text) override;
    void redrawMessage(int stringId, const std::string& text) override;
    void showStatusWord(const char* stat, int state, const std::string& text) override;
    // The two status screens on the sim-status window's DC (dll:10068f90, dll:1006aa80), see bg_display.cpp.
    bool statusScreenOpen(const char* screen, bool withPlant) override;
    void showText(const char* area, const std::string& text) override;
    void statusTextColour(unsigned colorRef) override;
    void statusScreenGauge(const char* name, double value, int pixels) override;
    void statusScreenClock(int timeOfDay) override;
    void statusScreenHotspot(const char* name) override;
    void statusScreenDone(const char* screen) override;
    void statusScreenClose(const char* screen) override;

private:
    void drawLine(int stringId, const std::string& text);
    void selectPenBrush(u32 colour);
    BiosGame& g_;
    std::string lastDrawn_;
    // the status screen being drawn
    int simDc_ = 0;
    Canvas* sc_ = nullptr;
    Pen oldPen_;
    Brush oldBrush_;
    int oldFont_ = 0;
    int energyX_ = 0, symptomY_ = 0x85, problems_ = 0;
};

} // namespace biosys::game
