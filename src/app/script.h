// Headless scripting of the Engine (no SDL): "--script 'cmd; cmd; ...'". Used by the tests.
//   page NAME|N        go to a page (debug jump)        scroll X,Y        set the view scroll position
//   move X,Y           pointer to screen position       click X,Y         move + left press + release
//   rclick X,Y         same with the right button       key S|D|SPACE|SHIFT|CTRL   key press + release
//                      key also takes a letter / digit, ENTER, ESC, TAB, BACKSPACE, CAPS, F1..F12 or 0xNN (the Windows virtual-key code)
//   postmouse X,Y              a WM_MOUSEMOVE posted to the main window (Engine::postMouseMove; the captured pointer rests at 320,240)
//   verb V A0 A1 ...           a hotspot action of verb V (arguments a1..) on the current page: prints `verb rc=<code>`
//   keydown KEY / keyup KEY   one half of `key`: a key held while other input goes on (the status screens are shown while Tab / Caps Lock is down)
//   type TEXT          one key press + release per character (letters and digits as their virtual keys, a blank as SPACE, the two
//                      characters \n as ENTER): the typed cheat words / passwords reach the game logic as real key events
//   advance MS         advance the injected clock in 16 ms update steps
//   shot FILE.bmp      render a 640x480 frame to an 8-bit BMP
// Smooth panoramas (engine/panomesh.h; the 8-bit frame and the game are unchanged by these):
//   panoinfo                     prints `panoinfo active=0|1 view=X,Y,W,H yaw= pitch= fov= focal= serial= step= now=`: the camera of the frame
//   smoothshot FILE.bmp SCALE [nearest|sdl] [frame=F.bmp] [bare=B.bmp]   the frame through the smooth path at SCALE (24-bit BMP, 640*SCALE x 480*SCALE): the CPU
//                                rasteriser of the mesh (bilinear; nearest = no filter) or `sdl` = SDL's software renderer (nearest only);
//                                frame= / bare= also write the 8-bit frame and the bare projection of the SAME render (a page filter such as
//                                the rain changes with every render)
//   barebmp FILE.bmp             the 640 x 368 projection of the bare page of the last frame (8-bit BMP): a pixel of `shot` that differs
//                                from it was drawn over the panorama
//   smoothtrace DT|off           display clock of DT ms (the engine is updated in steps of DT too); after every step, and every turn of a
//                                blocking animation, prints `smooth t= active= yaw= pitch= ty= tp= fov= snap= serial=`: the camera the
//                                smooth view shows (yaw, pitch), the engine's (ty, tp, yaw unwrapped) and whether it snapped
// Sharp text (app/text_sharp.h, engine/textplane.h; the 8-bit frame and the game are unchanged by these):
//   sharptext on|off             the canvas keeps its text runs (switch on before anything is drawn; --text sharp does it too)
//   textframes PREFIX            renders the frame with and without its low-resolution text: PREFIX_full.bmp, _free.bmp, _mask.bmp (8-bit),
//                                PREFIX_runs.txt (one line per visible text run: position, clip, colour, face, advances, hex text)
//   textruns FILE                only the run list;   textredraw FILE.bmp   the text-free frame with the runs' low-resolution glyphs drawn
//                                again where the mask is (prints `diff=` against the frame: 0 when the list is complete)
//   textshot FILE.bmp SCALE [PREFIX] [smooth]   the sharp-text frame at SCALE (24-bit BMP, 640*SCALE x 480*SCALE, CPU composition); PREFIX also
//                                saves the frames and run list of that very render; smooth: on the smooth panorama as smoothshot makes it
//   dump objects|bar|state|zones|menu|text              text state on stdout (menu: the open menu's page, flags and hotspots; text: the text entry)
//   swapped on|off                                      the system's swapped mouse buttons (the help page of the menu)
//   capture on|off     captured pointer (scroll mode / bar mode of spec 5.2) as in the live window
//   var OBJ VARIATION | remove OBJ | returnobj OBJ    API 0x46 / 0x26 / 0x52, prints 'api <code>'
//   slot day|night|...   time slot       hotspots on|off     hidden on|off     newgame     echo TEXT
// Media (the engine's SoundSystem / SpriteSystem / MovieSystem; files relative to BIOTIME, palette to BIODATA):
//   sound FILE [FLAGS]                          verb-4 style PLAYWAVE (flags 1 page-scoped, 2 loop, 4 new instance)
//   sprite TAN X Y MODE DELAYMS [fixed|page] [loop]   open + play; fixed: X,Y in the view, page: page coordinates
//   movie AVI X Y [PALETTE]                     open + play at page coordinates X,Y
//   useanim TAN WAV|- X Y MODE DELAYMS HIDEHELD REMOVEHELD   API 0x47, prints 'api <code>'
//   api NAME args...                            one EngineApi command (see apiTable in script.cpp), prints
//                                               `api NAME rc=<code> [out=value ...]`; "-" is an empty string argument
//   gdi HANDLE OP args...                       draw on the DC of `api GetWindowDC WINDOW` (Canvas): font F | textcolor C | bkcolor C |
//                                               bkmode 1|2 | textalign N | pen W C [STYLE] | brush C|null | textout X Y TEXT |
//                                               extent TEXT | drawtext L,T,R,B FLAGS TEXT | layout WIDTH FLAGS TEXT | moveto X,Y |
//                                               lineto X,Y | fillrect L,T,R,B C | fillsolid L,T,R,B C | rectangle L,T,R,B |
//                                               ellipse L,T,R,B | pie L,T,R,B XS,YS XE,YE | pixel X,Y C | map C; colours C: R,G,B
//                                               (PALETTERGB), i:N (PALETTEINDEX), rgb:R,G,B. Prints `gdi OP result`.
//   (the tokens lastdc / lastfont stand for the newest DC / font handle that `api GetWindowDC` / `api CreateDeleteLogicalFont` printed)
//   fontinfo FONT | faces                       the logical font made by `api CreateDeleteLogicalFont 1 HEIGHT FACE` / the registered faces
//   apistats CODE...                            prints `apistats 0xNN calls=N notimpl=N` per command code
//   activate on|off                             app activation (silences the media systems, fires the activation events)
//   stopall                                     close every sound, animation and movie
//   mixdump OUT.wav MS                          write the audio mixed during blocking animations since the last
//                                               dump, then MS more milliseconds of the engine mixer (clock advances)
//   pumpstep MS                                 clock step per turn of the blocking animation loop (default 40)
//   animshots PREFIX                            save every frame of the next blocking loops as PREFIXNNN.bmp
//   pumptrace on|off                            print `pump t=<ms>` at every turn of a blocking loop
//   gamelogic on|off                            attach / detach the game logic (ornaments exactly as the page flags say)
//   palettetrace on|off                         print `palette t=<ms> <1536 hex digits>` for every palette the API shows
//   xfade LOOKUP|- PALETTE|current|- [ALT]      what the test game logic answers to event 25 (lookup and palette relative
//                                               to BIODATA, `current` = the palette on display now, ALT = other definition)
//   filtertest on|off                           test OnBlitPage handler: marks the first view rows with a, b, c
//   pal [WINDOW]                                prints the display palette as hex
//   game dump|settime S|seed N|fx|sounds|pals    state / debug commands of the game logic (needs --game)
//   autosave status|minutes N|on|off|now|quit|load [N]   the autosave of the window run on the injected clock (needs --game; MANUAL.md "Saving")
//   menu open|close|key NAME|click X,Y|shot FILE [W H]|state|items|...   the settings menu and the New game screen (needs no window; see menuCommand in main.cpp)
//   activate on|off    (below) and key PAGEUP|PAGEDOWN / wheel up|down: the slot pages of the save and load menus
// More api commands (see apiTable): AddRemoveFontResource FILE ADD, CreateDeleteLogicalFont 1 HEIGHT FACE | 0 FONT,
// GetWindowDC WINDOW, ReleaseWindowDC WINDOW DC, SetPalette PAL STEPS SECONDS, FadeToPalette PAL BACK SPEED PERCENT,
// FadeToColour R G B BACK SPEED KEEP PERCENT, ClearWindow WIN COLOUR, FillScreen COLOUR, DisplayImage WIN NAME [X Y REDRAW
// KEYED WIDTH slot], SetImageVisibility WIN NAME 0|1, ReleasePageImageSlot WIN SLOT, RaisePageImage WIN NAME,
// JigglePage REPEATS FIRST LAST DX DY AX AY..., BeginPageFilter A B C, EndPageFilter, SetPageFilterBuffer LEN.
// PAL = file relative to BIODATA or rgb:R,G,B.
#pragma once
#include "engine/engine.h"
#include <functional>

namespace biosys {

// Test hooks into the game logic of the host (main.cpp); any may be empty.
struct ScriptHooks {
    std::function<void(const CrossFadeSpec&)> setCrossFade;
    std::function<void(bool)> setFilterTest;
    // `smoothshot FILE SCALE sdl`: the smooth capture through SDL's software renderer (app/panorama_smooth.h); absent without SDL
    std::function<bool(const Engine::PanoramaFrame&, const Frame&, int, std::vector<uint32_t>&, std::string&)> smoothCaptureSdl;
    // `window ...`: the window's real-clock waits (the journey's busy wait) on / off; absent without the game logic
    std::function<void(bool)> realtimeWaits;
    std::function<std::string(const std::string&)> game;   // `game ARGS`: debug commands of the game logic (--game)
    // Autosave (main.cpp): `tick(ms)` is called after every update step of `advance` with the injected time that has passed since the last
    // call (the play time of a script); `autosave ARGS` is the script command of that name: status | minutes N | on | off | now | quit | load [N]
    std::function<void(uint32_t)> tick;
    std::function<std::string(const std::string&)> autosave;
    // `menu ARGS`: the settings menu (F10) and the New game screen, driven without a window (main.cpp menuCommand; MANUAL.md "The settings menu")
    std::function<std::string(const std::string&)> menu;
};

// Runs the commands in order; throws Error on a bad command. Output goes to stdout.
void runScript(Engine& engine, const std::string& script, ScriptHooks* hooks = nullptr);

} // namespace biosys
