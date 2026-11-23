// What the story and hotspot code needs of the player (CSelf, dll:10063ca0..1006f330) and of the objects' virtual methods:
// the player and object classes: each is a thin forward ("story calls CSelf"); the text of a message goes to the display
// interface. Next to them: the display interface (what GDI text and CSelf::ShowMessage would show) and a few port helpers.
#include "game/story.h"
#include "game/obj_gear.h"
#include "game/plant.h"
#include "game/watertank.h"
#include <cstdio>

namespace biosys::game {

// ---------------------------------------------------------------------------------------------------------------
// display interface

void StoryLogic::uiMessage(int id) {
    char t[64];
    std::snprintf(t, sizeof t, "message id=0x%x", id);
    ui.record(t);
}
void StoryLogic::uiRecordText(int x, int y, const std::string& text) {
    char t[160];
    std::snprintf(t, sizeof t, "text x=%d y=%d '%s'", x, y, text.c_str());
    ui.record(t);
}
void StoryLogic::uiText(int x, int y, const std::string& text) {
    if (!b.CEngineFX_DrawText(text, x, y, 0)) uiRecordText(x, y, text);     // no DC: the original draws nothing; the port still records
}
void StoryLogic::uiTextRight(int x, int y, const std::string& text) {
    char t[160];
    std::snprintf(t, sizeof t, "text right=%d y=%d '%s'", x, y, text.c_str());
    ui.record(t);
}
void StoryLogic::uiClearHint() { ui.record("hint cleared"); }      // FUN_1006c700: the hint line 0,0x24..0x27f,0x37 is painted over

// ---------------------------------------------------------------------------------------------------------------
// CSelf (self.h: the player worker's translation)

// dll:1006bc40 CSelf::ShowMessage(&CSelf, 1, id, 0xffff, 0): the status bar text. The player's display records it; the story's own
// interface does too, so that `game ui` and `--log-events` show it.
void StoryLogic::Self_ShowMessage(int id, int a, int bb) {
    uiMessage(id);
    me().ShowMessage(1, id, a, bb);
}
void StoryLogic::Self_QueueMessage(int id) { me().ShowMessageDeferred(id); }          // dll:1006f330
void StoryLogic::Self_Consume(int id) { me().Consume(static_cast<u16>(id)); }          // dll:1006cf40
int StoryLogic::Self_Drink(int amount) { return me().Drink(static_cast<i16>(amount)); }   // dll:1006ccd0
int StoryLogic::Self_CanPickUp(double weight) { return me().CanPickUp(weight); }       // dll:1006def0
void StoryLogic::Self_LoseEnergy(double v) { me().LoseEnergy(v); }                     // dll:10066a30
void StoryLogic::Self_LoseNutrition(double v) { me().LoseNutrition(v); }               // dll:100669d0
void StoryLogic::Self_CloseStatusScreen() {              // dll:1006c790 FUN_1006c790: Eng_ClearWindow(CSelf+0x328, 0)
    b.eng_.ClearWindow(me().m_lTopBarHandle, 0);
}
void StoryLogic::Self_RefreshPage() { me().RefreshForPage(); }                         // dll:1006f100
void StoryLogic::Self_OnSynthAttack() { me().OnSynthAttack(); }                        // dll:1006efd0
void StoryLogic::Self_ShowStatus(int open) { me().ShowStatus(open); }                  // dll:10068f90
void StoryLogic::Self_ShowEnvironmentStatus(int open) { me().ShowEnvironmentStatus(open); }   // dll:1006aa80
void StoryLogic::Self_Rest() { me().Rest(); }                                          // dll:10067bb0
bool StoryLogic::Self_CanSleep() { return me().CanSleep() != 0; }                      // dll:10067540
void StoryLogic::Self_Sleep(int a, int bb, int c) { me().Sleep(a, bb, c); }            // dll:10067600
void StoryLogic::Self_ReturnToSavedPage(const std::string& page) { me().ReturnToSavedPage(page.c_str()); }   // dll:1006ede0
void StoryLogic::Self_Die() {}                           // (unused: the idle event calls BiosGame::CSelf_Die)
void StoryLogic::Self_Eat() { me().Eat(20.0, 0.1, 0.0, 1); }   // CSelf::Eat(&CSelf, 0, 20.0, 0.1, 0, 0, 1) of the cheat BANANA
void StoryLogic::Self_CatchMalaria() { me().CatchMalaria(); }   // dll:1006df90
void StoryLogic::Self_SetGogglesPower(int v) {           // the cheat GOGGLESnn: CGoggles::SetPower dll:1001d780 (obj_gear.h)
    if (auto* g = dynamic_cast<ObjGoggles*>(world().findObject("goggles"))) g->setPower(v);
}

// the objects' virtual methods (gameobj.h, the objects worker's)
int StoryLogic::heldHotspot(const std::string& hotspot) { GameObj* h = held(); return h ? h->onButtonHotspot(me(), hotspot) : 0; }   // vtable +0x70
void StoryLogic::heldDropOn(const std::string& hotspot) { if (GameObj* h = held()) h->onDropZoneClicked(me(), hotspot); }          // vtable +0x90
void StoryLogic::heldRightClick() { if (GameObj* h = held()) h->onRightClick(me()); }                                              // vtable +0x74
void StoryLogic::heldStatusBar() {                                                                                                  // vtable +0x28
    if (GameObj* o = dynamic_cast<GameObj*>(me().m_pObjTarget)) o->describe(me());
}

// plant functions of the fruit picking: the plant worker's
void StoryLogic::plantPickFruit(CPlant& p, const std::string& hotspot) { b.CPlant_Interact(p, nullptr, hotspot); }    // dll:100731b0 FUN_100731b0(plant, NULL, hotspot)

// dll:10073090 FUN_10073090 (bg_plant.cpp)
void StoryLogic::plantRefreshFruitImages(CPlant& p) { b.CPlant_RefreshFruitImages(p); }

// ---------------------------------------------------------------------------------------------------------------
// port helpers

void StoryLogic::setOceanHealth(double v) {              // `DAT_100c1cc8[0x22..0x23] = v` (the ocean's +0x88)
    if (COcean* oc = dynamic_cast<COcean*>(world().state().ocean)) oc->setHealth(v);
}

} // namespace biosys::game
