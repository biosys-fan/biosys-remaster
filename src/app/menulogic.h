// A test game logic for the menu tests (--menu-logic): what BiosGame.dll does around the menu and the saved games, reduced
// to the file work, so that the engine's menu, save and load can be driven without the game. Escape opens the main menu,
// F5 the save page, F6 the load page (dll:1001e6b0); the menu commands do what dll:10008010 does; the blob after the
// engine's part is the container of the real game: "JON IS A YOYO", 100 bytes description, a word, "THE END".
#pragma once
#include "engine/engine.h"
#include <cstdio>
#include <cstring>

namespace biosys {

class MenuTestLogic : public GameLogic {
public:
    explicit MenuTestLogic(Engine& e) : e_(e) {}
    int loads = 0, saves = 0;                  // OnLoadGame / OnSaveGame calls that succeeded
    std::string lastText;                      // the last text entry result
    bool lastCancelled = false;

    int onStartup() override { return 0; }
    void onKeyDown(int vkey, int, int, int& doDefault) override {
        static const u8 colour[4] = {0x73, 0x52, 0, 0};   // DAT_1009f844: the slot description colour the DLL passes
        const char* menu = vkey == 0x1b ? "" : vkey == 0x74 ? "savegame" : vkey == 0x75 ? "loadgame" : nullptr;
        if (!menu) return;
        e_.api().OpenMenu(menu, colour, 0);
        doDefault = 0;
    }
    void onMenuCommand(int code, int arg) override {
        const std::string slot = "saved game " + std::to_string(arg) + ".sav";
        switch (code) {
        case 1: e_.api().LoadGame("newgame.sav", 2); break;
        case 2: e_.api().SaveGame(slot); break;
        case 3: e_.api().LoadGame(slot, -1); break;
        case 4: e_.api().QuitGame(); break;
        default: break;
        }
    }
    bool onSaveGame(const std::string& file) override {
        std::FILE* f = std::fopen(file.c_str(), "ab");
        if (!f) return false;
        char desc[100] = {};
        std::snprintf(desc, sizeof desc, "Saved Game: %s test", e_.view().pageNumber() ? e_.view().page().name.c_str() : "-");
        const u32 version = 0x23;
        bool ok = std::fwrite("JON IS A YOYO", 13, 1, f) == 1 && std::fwrite(desc, 100, 1, f) == 1 && std::fwrite(&version, 4, 1, f) == 1 &&
                  std::fwrite("THE END", 7, 1, f) == 1;
        std::fclose(f);
        if (ok) saves++;
        return ok;
    }
    bool onLoadGame(const std::string& file, long offset) override {
        std::FILE* f = std::fopen(file.c_str(), "rb");
        if (!f) return false;
        char magic[13] = {};
        bool ok = std::fseek(f, offset, SEEK_SET) == 0 && std::fread(magic, 13, 1, f) == 1 && std::memcmp(magic, "JON IS A YOYO", 13) == 0;
        std::fclose(f);
        if (ok) loads++;
        return ok;
    }
    // dll:10008350: the description is in the blob of the file
    void onMenuQuerySavedGame(int slot, int& exists, std::string& description, int) override {
        const std::string dir = e_.saveDirectory();
        const std::string path = dir + "/Saved Game " + std::to_string(slot) + ".sav";
        std::FILE* f = std::fopen(path.c_str(), "rb");
        exists = f != nullptr;
        if (!f) { description = "-- empty --"; return; }
        u32 off = 0;
        char desc[101] = {};
        if (std::fseek(f, 8, SEEK_SET) == 0 && std::fread(&off, 4, 1, f) == 1 && std::fseek(f, static_cast<long>(off) + 13, SEEK_SET) == 0 &&
            std::fread(desc, 100, 1, f) == 1)
            description = desc;
        else
            description = "Saved Game " + std::to_string(slot);
        std::fclose(f);
    }
    void onTextInputDone(bool cancelled, const std::string& text) override {
        lastCancelled = cancelled;
        lastText = text;
    }

private:
    Engine& e_;
};

} // namespace biosys
