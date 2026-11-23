// EngineApi: the interface through which the game logic (the port of BiosGame.dll, src/game/) commands the
// engine: the original's 110 API commands, 0x01 .. 0x6e, as one typed virtual method each. In the original a
// command is `SendMessageA(hwnd, 0x8000, code, &block)` with one shared 0xC4-byte parameter block (or a plain
// value / string pointer as lParam for 1, 8, 9, 0x1b, 0x55); the LRESULT is the error code. Reference:
// analysis/notes/interface.md section 3 and engine_runtime.md 1.12, 2.6, 3.1, 4.3 (+ section 6 corrections).
//
// Conventions
//  * Every method returns the original's error code (0 = OK, see ApiError); out-parameters are only written
//    when the call succeeded. kApiNotImplemented (outside the original's range) means "this port's engine has
//    no such feature yet"; the engine logs `api not implemented: <code> <name>` to stderr once per command.
//  * Above each method: the command code and name, the engine implementation (exe:xxxxxxxx; "inline" = in the
//    dispatcher exe:0043da90 itself) and the DLL wrapper that sends it (dll:xxxxxxxx; "-" = Biosys never sends
//    it), then `block:` = the parameter-block offsets the parameters live at (the DLL translation does not need
//    them, they document where each parameter comes from), then what is known about the semantics.
//  * Parameters are named after their role; where the role is unknown they keep their block offset
//    (`unk04`, `unkB0`). Names are at most 10 characters (the DLL wrappers refuse longer ones with error 1
//    without sending; that check is part of the DLL translation, not of this interface); page, transition,
//    image and property names and file names are plain strings.
//  * bool = a flag the engine only compares with 1 / treats as nonzero. Pointers into the parameter block
//    (+c0 palettes, RECTs, LOGFONT) are kept as raw pointers with the layout of the original.
//  * Callbacks (+b0 in the media open commands) are not function pointers: `notify` says that the DLL passed a
//    callback, and the engine then delivers the events through GameLogic::onSoundEvent / onSpriteEvent /
//    onMovieEvent.
//  * Calls are refused with error 10 while an event handler with a restriction mask is running (see
//    controlprogram.h / interface.md 3.2); that is done by the implementation, not by the callers.
#pragma once
#include "engine/common.h"
#include <array>
#include <string>
#include <vector>

namespace biosys {

class Canvas;   // engine/canvas.h

// Error codes of the original (interface.md 3.3) plus the port's own.
enum ApiError : int {
    kApiOk = 0,
    kApiNameTooLong = 1,         // DLL side only: a name longer than 10 characters, nothing sent
    kApiUnknownCommand = 2,      // also: "object manager not initialised" (0x0c)
    kApiBadWindow = 3,
    kApiHotspotNotFound = 4,
    kApiTimerNotFound = 5,
    kApiPageNotFound = 6,
    kApiImageNotFound = 7,
    kApiTransitionNotFound = 8,
    kApiJumpFailed = 9,
    kApiNotAllowed = 10,         // refused by the restriction mask of the running event (or a disabled command)
    kApiActionFailed = 0x0b,
    kApiJumpsDisabled = 0x0c,
    kApiObjectNotFound = 0x0d,
    kApiBadDropZone = 0x0e,
    kApiUnknownProperty = 0x0f,
    kApiBadSoundHandle = 0x10,
    kApiFailure = 0x11,          // general failure / "not a drop zone" / nothing dropped
    kApiBadParameter = 0x12,
    kApiBadAnimHandle = 0x13,
    kApiNotPanorama = 0x14,      // pan-to-page position on a page that is not a 360-degree page
    kApiAppInactive = 0x15,
    kApiRedrawFailed = 0x16,
    kApiModalBlocked = 0x17,     // display-suspended state: only 2 9 0x14..0x19 0x34 0x35 0x44 0x53 0x54 0x59 accepted
    kApiBadMovieHandle = 0x18,
    kApiNotScrollable = 0x19,    // GetPanoramaInfo: not a 360-degree page larger than the view
    kApiNoSuchGroup = 0x1a,      // command-specific failures, see the commands
    kApiNoExtraImage = 0x1b,
    kApiVariationFailed = 0x24,
    kApiHomeOccupied = 0x25,
    kApiMenuOpen = 0x26,         // the menu is up: only 2 9 0x34 0x35 0x53 0x55 0x59 0x6a accepted
    kApiBadImageSlot = 0x27,
    kApiPopupNotOpen = 0x28,
    kApiNotImplemented = 0x10000 // port only: this command is not supported by the engine yet
};

constexpr int kApiFirstCommand = 0x01, kApiLastCommand = 0x6e, kApiCommandCount = 110;

class EngineApi {
public:
    virtual ~EngineApi() = default;

    // The one addition to the original's command set (sanctioned for the GDI text/line drawing, see canvas.h): a device
    // context handle from GetWindowDC (0x3a) is an opaque id, not a Windows HDC; this returns the drawing object it stands
    // for (SelectObject / SetTextColor / TextOut / LineTo ... are Canvas methods) or nullptr if the handle is not an open
    // DC (never obtained, or already released with ReleaseWindowDC). Font handles from CreateDeleteLogicalFont (0x39) are
    // opaque ids as well; Canvas::selectFont takes them. Not virtual: it works through wrappers (LoggingApi) unchanged.
    Canvas* canvas(int dcHandle) const;


    // 0x01 SetTimerInterval   engine inline   DLL wrapper dll:10073fc0
    // block: lParam seconds
    // Period of OnTimer in seconds (lParam is the plain value, not a block). 0 stops OnTimer; otherwise period = seconds * 1000 ms and the timer is started.
    virtual int SetTimerInterval(int seconds) = 0;

    // 0x02 QuitGame   engine exe:00412ae0 exe:00451560   DLL wrapper dll:10073ff0
    // block: -
    // Fade to black, then close the main window.
    virtual int QuitGame() = 0;

    // 0x03 EnableHotspot   engine exe:00402860 exe:00402ac0   DLL wrapper dll:10074010
    // block: +00 window, +2f hotspot, +04 enable, +08 prefixWildcard, +c0 previousState
    // Enable / disable a hotspot of the page in `window`. prefixWildcard == 1 and a name ending in "*": every hotspot whose name starts with the rest.
    // previousState (+c0, may be NULL, not used with the wildcard) receives the old state (1 enabled). Errors: 3 bad window, 4 not found.
    virtual int EnableHotspot(int window, const std::string& hotspot, bool enable, bool prefixWildcard, int* previousState) = 0;

    // 0x04 EnablePageTimer   engine exe:00402b50   DLL wrapper -
    // block: +00 window, +3a timer, +04 enable
    // Errors: 3 bad window, 5 timer not found.
    virtual int EnablePageTimer(int window, const std::string& timer, bool enable) = 0;

    // 0x05 JumpToPage   engine exe:00411350 exe:00445490   DLL wrapper dll:100740d0
    // block: +24 page, +5b transition, +08 pushHistory, +0c x, +10 y, +04 bigPageMode, +ac noFlip
    // Page jump (OnExitPage -> load -> OnPageLoaded / OnPageDrawn -> OnEnterPage). transition: none, wipeleft, wiperight, wipeup, wipedown,
    // pushwipeleft, pushwiperight, pushwipeup, pushwipedown, zoomtohotspot (8 if unknown). pushHistory (+08) == 1 remembers the old page for
    // the back history. (x, y) = screen position the pointer is moved to in scroll mode (DLL: (0,0) if it passes no point).
    // bigPageMode (+04) == 1 and a 0xa00 x 0x400 page: the engine uses its second jump routine (exe:00445490), else the ordinary one with
    // noFlip (+ac, only honoured when bigPageMode != 1): do not flip the screen after the jump. Errors: 6 page, 8 transition, 9 jump failed, 0xc jumps disabled, 0x15 app inactive.
    virtual int JumpToPage(const std::string& page, const std::string& transition, bool pushHistory, int x, int y, int bigPageMode, int noFlip) = 0;

    // 0x06 DoHotspotAction   engine exe:00402bd0 exe:00443e60 exe:004449f0 exe:00444ca0 exe:00450950   DLL wrapper dll:10074200
    // block: +00 window, +2f hotspot
    // Runs the action stored on the hotspot (jump, zoom-in, cross-fade jump...). Errors: 3, 4, 0xb dispatch failed, 0x15.
    virtual int DoHotspotAction(int window, const std::string& hotspot) = 0;

    // 0x07 DoPageTimerAction   engine exe:00402c80 exe:00443e60 exe:00450950   DLL wrapper -
    // block: +00 window, +3a timer
    // Runs the action stored on a page timer. Errors: 3, 5, 0xb, 0x15.
    virtual int DoPageTimerAction(int window, const std::string& timer) = 0;

    // 0x08 ShowMessage   engine exe:004513c0   DLL wrapper -
    // block: lParam text
    // Message box with the text (lParam is the string, not a block).
    virtual int ShowMessage(const std::string& text) = 0;

    // 0x09 WriteLog   engine exe:004513c0   DLL wrapper dll:100742a0
    // block: lParam text
    // Appends the text to the engine log file (lParam is the string, not a block).
    virtual int WriteLog(const std::string& text) = 0;

    // 0x0a DisplayImage   engine exe:00402e00   DLL wrapper dll:100742d0
    // block: +00 window, +45 image, +0c x, +10 y, +04 unk04, +08 unk08, +20 unk20, +c0 unkOut
    // TCPage::DisplayImage: paints the named type-1 ornament onto the page surface NOW (hidden or not; the hidden flag is not touched),
    // objects lifted off the surface around it. (x, y) = (-1, -1) means "its own position". Read off the exe: unk04 = redraw the screen
    // afterwards, unk20 = at most that many pixels wide (-1 = all, 0 = nothing), unkOut (+c0) = optional out slot: the covered
    // rectangle is saved in one of 16 slots (-1 if none free) for ReleasePageImageSlot; unk08 == 1 with a transparent image: the slot only
    // restores the pixels the image changed. Errors: 3 bad window, 7 image not found / not paintable.
    virtual int DisplayImage(int window, const std::string& image, int x, int y, int unk04, int unk08, int unk20, int* unkOut) = 0;

    // 0x0b SetImageVisibility   engine exe:00403390   DLL wrapper dll:10074440
    // block: +00 window, +45 image, +04 visible
    // Only flips ornament flag bit0 (hidden); visible == 1 clears it, anything else sets it. The picture changes when the page is painted
    // again (RedrawPage paints the visible ornaments over the surface; a page load clears it first). The flags are run-time state of the
    // loaded page: entering the page again reloads them from the file. Errors: 3, 7.
    virtual int SetImageVisibility(int window, const std::string& image, bool visible) = 0;

    // 0x0c GetGameObjectCount   engine exe:004490d0   DLL wrapper -
    // block: +14 count
    // Number of game objects. 2 if the object manager is not initialised.
    virtual int GetGameObjectCount(int& count) = 0;

    // 0x0d GetGameObjectName   engine exe:004490f0   DLL wrapper -
    // block: +18 index, +50 name
    // Name of object `index`. 0xd if out of range.
    virtual int GetGameObjectName(int index, std::string& name) = 0;

    // 0x0e GetGameObjectInfo   engine exe:00449140   DLL wrapper dll:100744f0
    // block: +50 name, +78 info[12]
    // info[0] moveable, [1] wearable, [2] in zone (status 1, or not moveable), [3] fruit lying in a zone (6), [4] held, [5] on bar (3 or 5),
    // [6] removed (4), [7] worn (5), [8] weight, [9] usage count, [10] fruit flag, [11] zone number (status 1/6, else -1). 0xd if unknown.
    virtual int GetGameObjectInfo(const std::string& name, std::array<int, 12>& info) = 0;

    // 0x0f GetDropZoneCount   engine exe:0040ef50   DLL wrapper -
    // block: +14 count
    // Number of drop zones.
    virtual int GetDropZoneCount(int& count) = 0;

    // 0x10 GetDropZoneInfo   engine exe:0040ef70 exe:004518c0 exe:00449300   DLL wrapper dll:100745b0
    // block: +18 index, +24 page, +18 hotspotIndex, +50 object
    // Drop zone `index`: page name, hotspot index within the page, name of the object lying in it ("" if free). 0xe if index >= count.
    virtual int GetDropZoneInfo(int index, std::string& page, int& hotspotIndex, std::string& object) = 0;

    // 0x11 GetHotspotNameByIndex   engine exe:00403670   DLL wrapper -
    // block: +00 window, +18 index, +2f hotspot
    // Errors: 3 bad window, 4 no such hotspot.
    virtual int GetHotspotNameByIndex(int window, int index, std::string& hotspot) = 0;

    // 0x12 SetGameObjectProperty   engine exe:004493f0   DLL wrapper dll:10074670
    // block: +50 object, +6a property, +18 value
    // property "weight" (clamped to 100) or "usagecount" (-1 = unlimited). 0xd unknown object, 0xf unknown property.
    virtual int SetGameObjectProperty(const std::string& object, const std::string& property, int value) = 0;

    // 0x13 NoOp13   engine -   DLL wrapper dll:10074770
    // block: +a8 text, +18 number
    // Does nothing, returns 0 (the DLL still calls it with a string and a number).
    virtual int NoOp13(const std::string& text, int number) = 0;

    // 0x14 OpenSound   engine exe:0043cbf0 exe:0043d060   DLL wrapper dll:100747d0
    // block: +a8 file, +ac loop, +b0 notify, +b4 frequency, +b8 pan, +bc volume, +18 handle
    // Opens a wav (relative to BIOTIME), max 40 sounds, lock 1. loop: only the value 1 loops. frequency Hz (0 = file rate), pan and volume in
    // 1/100 dB (volume -10000..0). notify = the callback pointer at +b0 was non-NULL: the engine then fires onSoundEvent. (interface.md calls +b4 a
    // callback argument; engine_runtime.md 4.3 and the DLL call sites show it is the frequency.) 0x11 on any failure.
    virtual int OpenSound(const std::string& file, int loop, bool notify, int frequency, int pan, int volume, int& handle) = 0;

    // 0x15 PlaySound   engine exe:0043c3b0   DLL wrapper dll:10074880
    // block: +18 handle, +04 restart
    // restart == 1 starts from the beginning, else resumes. 0x10 bad handle.
    virtual int PlaySound(int handle, bool restart) = 0;

    // 0x16 StopSound   engine exe:0043c450   DLL wrapper dll:100748e0
    // block: +18 handle
    // Stop, position kept. 0x10 bad handle.
    virtual int StopSound(int handle) = 0;

    // 0x17 IsSoundPlaying   engine exe:0043c4d0   DLL wrapper dll:10074930
    // block: +18 handle, +04 playing
    // 0x10 bad handle.
    virtual int IsSoundPlaying(int handle, int& playing) = 0;

    // 0x18 SetSoundProperty   engine exe:0043c5b0 exe:0043c4f0 exe:0043c530 exe:0043c570   DLL wrapper dll:100749a0
    // block: +18 handle, +6a property, +14 value
    // property "loop" / "frequency" / "pan" / "volume" (case-insensitive). 0xf unknown property, 0x10 bad handle, 0x11 failure.
    virtual int SetSoundProperty(int handle, const std::string& property, int value) = 0;

    // 0x19 CloseSound   engine exe:0043d090 exe:0043d0e0   DLL wrapper dll:10074b50
    // block: +18 handle
    // Unlock and free. 0x11 if still locked, 0x10 bad handle.
    virtual int CloseSound(int handle) = 0;

    // 0x1a SetPalette   engine exe:00412630   DLL wrapper dll:10074ba0
    // block: +c0 palette, +14 steps, +18 duration
    // palette = 256 entries of 4 bytes (R, G, B, flags). duration (+18) 0 = immediately; otherwise steps (+14) must be nonzero (0x12 else).
    // Timed: steps are limited to 2 per second (they are taken from the 500 ms timer tick, the first one when `duration * 1000 / steps` ms
    // have passed); palette k = trunc(delta * k + start) per channel with float delta = (target - start) / steps. Fires OnPaletteChanged
    // (event 13) after the immediate set and after the last timed step. From the first call on the engine shows the logic's palette.
    // 0x15 app inactive, 0x11 failure.
    virtual int SetPalette(const u8* palette, int steps, int duration) = 0;

    // 0x1b GetDataDirectory   engine inline   DLL wrapper dll:10074c10
    // block: lParam directory
    // Copies the data directory (the "Data directory = %s" of the engine log) into the buffer.
    virtual int GetDataDirectory(std::string& directory) = 0;

    // 0x1c GetPalette   engine exe:00412a00   DLL wrapper -
    // block: +c0 palette
    // Receives the current 256 x 4 byte palette. 0x15 inactive, 0x11 failure.
    virtual int GetPalette(u8* palette) = 0;

    // 0x1d FadeToColour   engine exe:00412ae0   DLL wrapper dll:10074c40
    // block: +18 rgb, +04 unk04, +14 speed, +1c unk1c, +00 percent
    // Blocking fade of the screen to the colour rgb = r | g << 8 | b << 16 (the wrapper packs three bytes). speed 0..3 (30/60/100/100 steps
    // at 0/1/16/32 ms), percent 1..100 of the way; unk04 == 1: fade back afterwards; unk1c < 256: that palette index keeps its colour.
    // 0x15 inactive, 0x11 failure.
    virtual int FadeToColour(int rgb, int unk04, int speed, int unk1c, int percent) = 0;

    // 0x1e OpenSpriteAnimationVariant   engine exe:00443e80 exe:00459dd0   DLL wrapper -
    // block: as 0x1f
    // Same as 0x1f, but first leaves the drag-scroll state (exe:00443e80, a no-op in Biosys).
    virtual int OpenSpriteAnimationVariant(const std::string& file, int window, int x, int y, int playMode, int frameDelayMs, bool notify, bool loop, bool keepPainted, const std::string& wav, int maxPlayMs, int soundMode, bool pageRelative, int visibleWidth, bool eraseOnStop, int& handle) = 0;

    // 0x1f OpenSpriteAnimation   engine exe:00459dd0 exe:0045a060   DLL wrapper dll:10074ce0
    // block: +a8 file, +00 window, +0c x, +10 y, +18 playMode, +14 frameDelayMs, +b0 notify, +08 loop, +04 keepPainted, +c0 wav, +1c maxPlayMs, +ac soundMode, +b8 pageRelative, +20 visibleWidth, +bc eraseOnStop, +18 handle
    // Opens a .TAN (relative to BIOTIME), max 10 animations, API lock 1. window -1 main / 0..19 popup. playMode 1..4, frameDelayMs per frame,
    // wav = optional sound file ("" = none), maxPlayMs 0 = unlimited, soundMode 1 = looping wav spans the whole animation, visibleWidth -1 = full.
    // Never autoplays. notify as for 0x14. 0x15 inactive, 0x11 failure.
    virtual int OpenSpriteAnimation(const std::string& file, int window, int x, int y, int playMode, int frameDelayMs, bool notify, bool loop, bool keepPainted, const std::string& wav, int maxPlayMs, int soundMode, bool pageRelative, int visibleWidth, bool eraseOnStop, int& handle) = 0;

    // 0x20 PlaySpriteAnimation   engine exe:00458840   DLL wrapper dll:10074e20
    // block: +18 handle, +04 restart, +08 priority
    // priority == 1: redraw the page first, make it the priority animation and run one update at once. 0x13 bad handle, 0x15 inactive.
    virtual int PlaySpriteAnimation(int handle, bool restart, bool priority) = 0;

    // 0x21 StopSpriteAnimation   engine exe:00458970   DLL wrapper dll:10074e90
    // block: +18 handle
    // 0x13 bad handle.
    virtual int StopSpriteAnimation(int handle) = 0;

    // 0x22 IsSpriteAnimationPlaying   engine exe:00458aa0   DLL wrapper dll:10074ee0
    // block: +18 handle, +04 playing
    // 0x13 bad handle.
    virtual int IsSpriteAnimationPlaying(int handle, int& playing) = 0;

    // 0x23 CloseSpriteAnimation   engine exe:0045a090 exe:0045a0e0   DLL wrapper dll:10074f50
    // block: +18 handle
    // Releases the API lock; 0x11 if still locked, 0x13 bad handle.
    virtual int CloseSpriteAnimation(int handle) = 0;

    // 0x24 FadeToPalette   engine exe:00413140   DLL wrapper dll:10074fa0
    // block: +c0 palette, +04 unk04, +14 speed, +18 percent
    // Blocking fade to a 256 x 4 byte palette (same steps as FadeToColour; unk04 == 1: back to the start palette). 0x15 inactive, 0x11 failure.
    virtual int FadeToPalette(const u8* palette, int unk04, int speed, int percent) = 0;

    // 0x25 GetDropZoneForHotspot   engine exe:00403790 exe:0040efb0   DLL wrapper dll:10075010
    // block: +2f hotspot, +18 zone
    // Drop zone number of a hotspot of the current main page. 4 hotspot not found, 0x11 if it is not a zone.
    virtual int GetDropZoneForHotspot(const std::string& hotspot, int& zone) = 0;

    // 0x26 RemoveGameObject   engine exe:00449830   DLL wrapper dll:100750c0
    // block: +50 object
    // Takes the object out of the world / hand / status bar (status 4). 0xd unknown, 0x15 inactive.
    virtual int RemoveGameObject(const std::string& object) = 0;

    // 0x27 SetSoundPanToPagePosition   engine exe:0043c6e0   DLL wrapper dll:10074a50
    // block: +18 handle, +04 enable, +14 x, +2f hotspot
    // Pan the sound to a page position: x, or the centre of `hotspot` ("" = use x). 360-degree pages only (0x14 else). 0x10 bad handle.
    virtual int SetSoundPanToPagePosition(int handle, bool enable, int x, const std::string& hotspot) = 0;

    // 0x28 RedrawPage   engine exe:00411760   DLL wrapper dll:100752a0
    // block: -
    // Re-render the main page and its objects and flip. 0x16 failure, 0x15 inactive.
    virtual int RedrawPage() = 0;

    // 0x29 OpenAVIMovie   engine exe:004574d0 exe:00457770   DLL wrapper dll:100752d0
    // block: +a8 file, +00 window, +0c x, +10 y, +b0 notify, +08 loop, +04 unk04, +c0 palette, +18 handle
    // Opens an AVI (relative to BIOTIME) at page position (x, y); max 4 movies. palette = 256 x 4 bytes, used to resolve the colour key.
    // Never autoplays. 0x15 inactive, 0x11 failure.
    virtual int OpenAVIMovie(const std::string& file, int window, int x, int y, bool notify, bool loop, int unk04, const u8* palette, int& handle) = 0;

    // 0x2a PlayAVIMovie   engine exe:00455a50   DLL wrapper dll:10075650
    // block: +18 handle, +04 restart
    // 0x18 bad handle; a failing Play destroys the movie (0x11).
    virtual int PlayAVIMovie(int handle, bool restart) = 0;

    // 0x2b StopAVIMovie   engine exe:00455f40   DLL wrapper dll:100756b0
    // block: +18 handle
    // 0x18 bad handle.
    virtual int StopAVIMovie(int handle) = 0;

    // 0x2c IsAVIMoviePlaying   engine exe:00455ff0   DLL wrapper -
    // block: +18 handle, +04 playing
    // 0x18 bad handle.
    virtual int IsAVIMoviePlaying(int handle, int& playing) = 0;

    // 0x2d CloseAVIMovie   engine exe:004577a0 exe:004577f0   DLL wrapper dll:10075700
    // block: +18 handle
    // 0x11 only if still locked, 0x18 bad handle.
    virtual int CloseAVIMovie(int handle) = 0;

    // 0x2e EnableUserInput   engine inline exe:00444890   DLL wrapper dll:10075750
    // block: +04 enable, +08 unk08
    // Disable (enable == 0) ignores keys, mouse and object handlers until re-enabled. Both values are normalised to 0 / 1. (guess at name)
    virtual int EnableUserInput(bool enable, int unk08) = 0;

    // 0x2f GetPanoramaInfo   engine exe:00403850   DLL wrapper dll:100757b0
    // block: +14 pageWidth, +18 startX, +1c centreX
    // Main page must be a 360-degree page larger than the view: pageWidth in pixels, startX = initial scroll x of the page record, centreX =
    // page x at the centre of the view. 10 if no main page, 0x19 if it is not such a page.
    virtual int GetPanoramaInfo(int& pageWidth, int& startX, int& centreX) = 0;

    // 0x30 ChangeGameObjectHoldingImagesGroup   engine exe:00449d90   DLL wrapper dll:10075830
    // block: +50 object, +18 group
    // 0xd unknown object, 0x1a no such group.
    virtual int ChangeGameObjectHoldingImagesGroup(const std::string& object, int group) = 0;

    // 0x31 PlaceGameObject   engine exe:0044a030   DLL wrapper dll:100758d0
    // block: +50 srcObject, +24 extraImage, +2f dstObject, +3a imageKind, +1c holdIndex, +18 group, +0c x, +10 y
    // Copies the named extra image of srcObject into the image of dstObject selected by imageKind (normal1..normal4, icon1, holding, wearing;
    // holdIndex within hold group `group` for "holding") at (x, y) and repaints. 0xd / 0x12 / 2 / 0x1b see engine_runtime.md 1.12.
    virtual int PlaceGameObject(const std::string& srcObject, const std::string& extraImage, const std::string& dstObject, const std::string& imageKind, int holdIndex, int group, int x, int y) = 0;

    // 0x32 BeginPageFilter   engine inline   DLL wrapper dll:10075ab0
    // block: +18 a, +1c b, +14 c
    // Starts the page filter: three bytes handed to event 42 (OnBlitPage) as (a, c, b) after every page blit until 0x33 (or a page load).
    virtual int BeginPageFilter(int a, int b, int c) = 0;

    // 0x33 EndPageFilter   engine inline exe:00443f30   DLL wrapper dll:10075b20
    // block: -
    // Ends the page filter and redraws.
    virtual int EndPageFilter() = 0;

    // 0x34 SaveGame   engine exe:00452410   DLL wrapper dll:10075b40
    // block: +a8 fileName
    // The engine writes its part, then fires OnSaveGame.
    virtual int SaveGame(const std::string& fileName) = 0;

    // 0x35 LoadGame   engine exe:00452b70   DLL wrapper dll:10075b90
    // block: +a8 fileName, +00 mode
    // Fires OnLoadGame after the engine read its part.
    virtual int LoadGame(const std::string& fileName, int mode) = 0;

    // 0x36 SetSpriteAnimationValue   engine exe:00458f90   DLL wrapper -
    // block: +18 handle, +20 value
    // Sets the visible width (capped by the animation header); effective from the next frame. 0x13 bad handle.
    virtual int SetSpriteAnimationValue(int handle, int value) = 0;

    // 0x37 SetAVIMovieParam   engine exe:00456000   DLL wrapper dll:10075390
    // block: +18 handle, +c0 colourKey
    // Colour key on: colourKey points to a dword whose bytes are R, G, B, unused (the DLL sends cyan). 0x18 bad handle.
    virtual int SetAVIMovieParam(int handle, const u8* colourKey) = 0;

    // 0x38 AddRemoveFontResource   engine AddFontResourceA RemoveFontResourceA   DLL wrapper dll:10075bf0
    // block: +a8 file, +04 add
    // Add (1) or remove a font file. `file` = the data directory (API 0x1b) + "\\fonts\\" + name, as the DLL builds it. Error 0x11 if the
    // call fails (file not found / not a TrueType font / removing a file that was never added). The port reads the face name
    // from the font's name table; no font is registered system-wide.
    virtual int AddRemoveFontResource(const std::string& file, bool add) = 0;

    // 0x39 CreateDeleteLogicalFont   engine CreateFontIndirectA DeleteObject   DLL wrapper dll:10075c50
    // block: +04 create, +c0 logFont, +18 font
    // create == 1: make a font from the LOGFONT at logFont (the 60-byte LOGFONTA), result in font (an opaque id, > 0); otherwise delete the
    // font passed in `font`. Error 0x11 on failure (deleting an unknown font, or no LOGFONT). Like CreateFontIndirectA it succeeds
    // for any face name: an unregistered face falls back to the first registered font.
    virtual int CreateDeleteLogicalFont(bool create, const void* logFont, int& font) = 0;

    // 0x3a GetWindowDC   engine exe:00413770   DLL wrapper dll:10075ce0
    // block: +00 window, +18 dc, +20 size20, +14 size14
    // Device context of the window's page surface; size20 / size14 = the page size: TCPage::GetSize(width, height) -> +20 / +14 (so size20 = width,
    // size14 = height). 10 if mask. Error 0x11 if the window has no page, is -3, or its DC is already out (DirectDraw hands out one DC per
    // surface). `dc` is an opaque id: use `canvas(dc)` to draw. Text and lines drawn go into the window's page surface and stay
    // until the page is repainted over them (page load, DisplayImage / RedrawPage ornaments over the same pixels).
    virtual int GetWindowDC(int window, int& dc, int& size20, int& size14) = 0;

    // 0x3b ReleaseWindowDC   engine exe:004137e0   DLL wrapper dll:10075d60
    // block: +00 window, +18 dc, +04 unk04, +08 unk08
    // Re-blits the window and flips (exe:004137e0; the page surface is composed on every frame here, so this closes the DC, drops a ClearWindow
    // fill and counts a redraw: Engine::redrawCount). There is NO redraw flag: the engine reads the window (+00) and the HDC (+18) only, +04 and
    // +08 are not looked at; the flip happens for any non-null HDC (at once, or at the end of the idle pass). The top bar, window 0, is not
    // re-blitted. Error 0x11 for a DC that is not open for that window; 10 if mask.
    virtual int ReleaseWindowDC(int window, int dc, int unk04, int unk08) = 0;

    // 0x3c ClearWindow   engine exe:004138b0   DLL wrapper dll:10075dd0
    // block: +00 window, +18 colour
    // Fills the window rectangle of the screen (not the page surface) with palette index `colour`; it stays until the next page blit.
    // window -1 main view, -3 status bar, 0 top bar, 1 popup; unknown window 0x11; an inactive app answers 0.
    virtual int ClearWindow(int window, int colour) = 0;

    // 0x3d SetAVIMovieVolume   engine exe:00456020   DLL wrapper dll:100753f0
    // block: +18 handle, +bc volume
    // 1/100 dB, -10000..0. 0x18 bad handle.
    virtual int SetAVIMovieVolume(int handle, int volume) = 0;

    // 0x3e JigglePage   engine exe:004139f0   DLL wrapper dll:10075e30
    // block: +c0 records, +18 count, +14 unk14, +1c first, +20 last
    // Scrolls the main view by the sum of the records {dx, dy, stepX, stepY} (4 ints) first..last, in steps of at most stepX / stepY
    // (0 = in one step), repeated unk14 + 1 times; stops early when the view does not move any more. Only moves in scroll mode.
    // 10 no page, 0x12 no records / last < first; an inactive app or a page that does not scroll answer 0.
    virtual int JigglePage(const void* records, int count, int unk14, int first, int last) = 0;

    // 0x3f SetAVIMoviePage   engine exe:00456060   DLL wrapper dll:10075450
    // block: +18 handle, +24 page
    // Binds the movie to a page (visible only there). 6 unknown page, 0x11, 0x18.
    virtual int SetAVIMoviePage(int handle, const std::string& page) = 0;

    // 0x40 ShowCursor   engine inline   DLL wrapper dll:10075eb0
    // block: +04 show
    // show == 1 shows the cursor (DAT_00499070), then redraws. (guess at name)
    virtual int ShowCursor(bool show) = 0;

    // 0x41 AviOption41   engine exe:00456160   DLL wrapper dll:100754f0
    // block: +18 handle
    // Line doubling: every second destination row is filled with 0, height 2x. Needs an open, never-played, non-synth movie (0x11 else).
    virtual int AviOption41(int handle) = 0;

    // 0x42 AviOption42   engine exe:00456200   DLL wrapper dll:100755b0
    // block: +18 handle
    // Save-background surface. Same preconditions as 0x41.
    virtual int AviOption42(int handle) = 0;

    // 0x43 PrepareSynthAVIMovie   engine exe:00456240   DLL wrapper dll:10075600
    // block: +18 handle
    // Synth mode: frames decompressed to memory, drawn on the back buffer. Same preconditions as 0x41.
    virtual int PrepareSynthAVIMovie(int handle) = 0;

    // 0x44 NoOp44   engine -   DLL wrapper -
    // block: -
    // Always returns 0 (the DLL sends WM_KEYUP with this code number to the window, not through this API).
    virtual int NoOp44() = 0;

    // 0x45 ReturnGameObjectHidden   engine exe:0044bee0   DLL wrapper dll:10075160
    // block: +50 object
    // A removed fruit lies in its home zone again, ornament shown. 0xd unknown, 0x12.
    virtual int ReturnGameObjectHidden(const std::string& object) = 0;

    // 0x46 ChangeGameObjectVariation   engine exe:0044c370   DLL wrapper dll:10075f00
    // block: +50 object, +a8 variation, +18 group
    // Refused (10) when called inside OnGameObject for the same object; 0xd unknown object, 0x24 failure.
    virtual int ChangeGameObjectVariation(const std::string& object, const std::string& variation, int group) = 0;

    // 0x47 PlayObjectUseAnimation   engine exe:0044ced0   DLL wrapper dll:10076060
    // block: +a8 tanFile, +c0 wavFile, +04 hideHeld, +08 removeHeldAfter, +0c x, +10 y, +18 playMode, +14 frameDelayMs, +24 variation, +1c holdGroup
    // Plays an arbitrary animation with the blocking loop. hideHeld == 1 removes the held image meanwhile; removeHeldAfter == 1 runs 0x26 on the held
    // object afterwards, else a non-empty `variation` switches the held object to it (group holdGroup). (corrected vs interface.md.) 0x15, 0x11.
    virtual int PlayObjectUseAnimation(const std::string& tanFile, const std::string& wavFile, bool hideHeld, bool removeHeldAfter, int x, int y, int playMode, int frameDelayMs, const std::string& variation, int holdGroup) = 0;

    // 0x48 DropHeldObject   engine exe:0044d030   DLL wrapper dll:10076170
    // block: +04 atHotspot, +2f hotspot
    // hotspot == nullptr: default place (+04 = 0); else drop at that hotspot (+04 = 1). 0x11 nothing dropped.
    virtual int DropHeldObject(const std::string* hotspot) = 0;

    // 0x49 RedrawScreen   engine exe:00411b00   DLL wrapper dll:10076220
    // block: -
    // Redraws everything (menu / BS4 / page / status bar) and flips.
    virtual int RedrawScreen() = 0;

    // 0x4a CreateSimStatusWindow   engine exe:00410130   DLL wrapper dll:10076240
    // block: +24 page
    // Creates the modal "sim status" window from a page.
    virtual int CreateSimStatusWindow(const std::string& page) = 0;

    // 0x4b ShowSimStatusWindow   engine exe:00410290 exe:004102d0   DLL wrapper dll:100762e0
    // block: +04 show, +c0 unkC0, +08 unk08, +1c unk1c
    // show == 1 (exe:0040f700): unkC0 = a palette (256 x R, G, B, flags) the window puts on display (the opaque status screen) or null (the
    // palette stays and the window is laid over the page with palette index unk1c transparent), unk08 != 0 = the window's hotspots take the
    // mouse (a click fires event 34). 0x11 if the window does not exist, no page is loaded, or the palette has no (0, 255, 255). Hiding
    // (exe:0040f930) puts the old palette back. See engine_simstatus.cpp for what the window suppresses while it is up.
    virtual int ShowSimStatusWindow(bool show, const void* unkC0, int unk08, int unk1c) = 0;

    // 0x4c GetReleaseSimStatusDC   engine exe:004102f0 exe:00410330   DLL wrapper dll:10076350
    // block: +04 get, +18 dc
    // get == 1: dc receives the HDC of the window's surface (one at a time; 0x11 without the window); else releases the HDC passed in `dc`.
    virtual int GetReleaseSimStatusDC(bool get, int& dc) = 0;

    // 0x4d DisplaySimStatusImage   engine exe:004103a0   DLL wrapper dll:100763d0
    // block: +45 image, +0c x, +10 y
    // (x, y) = (-1, -1): its own position.
    virtual int DisplaySimStatusImage(const std::string& image, int x, int y) = 0;

    // 0x4e DestroySimStatusWindow   engine exe:00410410   DLL wrapper -
    // block: -
    // Hides the window (exe:0040f930) and frees it. The game made it once at start-up; a restart (API 0x53) destroys it too.
    virtual int DestroySimStatusWindow() = 0;

    // 0x4f AviOption4f   engine exe:004561b0   DLL wrapper dll:10075540
    // block: +18 handle, +04 restoreOnStop, +08 restoreOnClose
    // Restore the page under the movie when it stops / closes. Same preconditions as 0x41.
    virtual int AviOption4f(int handle, int restoreOnStop, int restoreOnClose) = 0;

    // 0x50 BeginTextInput   engine exe:00414720   DLL wrapper dll:10076530
    // block: +c0 rect, +14 maxLength, +18 unk18, +b0 unkB0
    // Starts the text-entry mode in the RECT (maxLength 1..8); ends with OnTextInputDone.
    virtual int BeginTextInput(const void* rect, int maxLength, int unk18, void* unkB0) = 0;

    // 0x51 EndTextInput   engine exe:004148f0   DLL wrapper dll:100765b0
    // block: +04 cancel
    // 
    virtual int EndTextInput(bool cancel) = 0;

    // 0x52 ReturnGameObject   engine exe:0044c040   DLL wrapper dll:10075200
    // block: +50 object
    // A removed object returns to its home drop zone. 0x25 if occupied, 0x12, 0xd.
    virtual int ReturnGameObject(const std::string& object) = 0;

    // 0x53 RestartGame   engine exe:00453da0   DLL wrapper -
    // block: -
    // Resets the engine state, fires OnRestart and jumps to the start page.
    virtual int RestartGame() = 0;

    // 0x54 OpenMenu   engine exe:004468d0   DLL wrapper dll:10076600
    // block: +a8 menu, +c0 unkC0, +04 unk04
    // menu = "top" / "savegame" / "loadgame" (the DLL may pass NULL = "").
    virtual int OpenMenu(const std::string& menu, const void* unkC0, int unk04) = 0;

    // 0x55 GetSaveDirectory   engine inline   DLL wrapper dll:10076670
    // block: lParam directory
    // Save directory (DAT_00497a28), "C:\" if empty. (guess at name)
    virtual int GetSaveDirectory(std::string& directory) = 0;

    // 0x56 EnableSimStatusHotspot   engine exe:0040fda0 exe:00402860   DLL wrapper dll:10076490
    // block: +2f hotspot, +04 enable
    // 
    virtual int EnableSimStatusHotspot(const std::string& hotspot, bool enable) = 0;

    // 0x57 ChangeGameObjectVariation1   engine exe:0044ccb0   DLL wrapper dll:10075fe0
    // block: +a8 variation, +18 group
    // Deferred variant: the object currently handling an OnGameObject event takes the variation after the handler ("" cancels). 0x24 if none / no free slot.
    virtual int ChangeGameObjectVariation1(const std::string& variation, int group) = 0;

    // 0x58 DumpFileStats   engine exe:00454040   DLL wrapper dll:100766a0
    // block: +c0 unkC0, +04 unk04
    // Debug: "Dumping time-based file stats".
    virtual int DumpFileStats(const void* unkC0, int unk04) = 0;

    // 0x59 NoOp59   engine -   DLL wrapper dll:10076700
    // block: +a8 unkA8
    // Always returns 0.
    virtual int NoOp59(const std::string& unkA8) = 0;

    // 0x5a SetPanoramaScrollLimits   engine exe:00454120   DLL wrapper dll:10076750
    // block: +14 minX, +18 maxX, +04 unk04
    // minX..maxX limit the horizontal scroll (-1, -1 = none); 0x12 if maxX < minX, 0x11 failure.
    virtual int SetPanoramaScrollLimits(int minX, int maxX, int unk04) = 0;

    // 0x5b SetPageFilterBuffer   engine inline   DLL wrapper -
    // block: +c0 buffer, +18 length
    // Buffer handed to OnBlitPage; only while 0x32 is active (0x11 else; length 0 clears it, a null buffer is 0x12).
    virtual int SetPageFilterBuffer(void* buffer, int length) = 0;

    // 0x5c EnableMouseDeltaEvent   engine inline   DLL wrapper dll:100767c0
    // block: +04 enable
    // Gates OnMouseDelta (event 40).
    virtual int EnableMouseDeltaEvent(bool enable) = 0;

    // 0x5d MoveMainWindowView   engine exe:00414c20 exe:00445810   DLL wrapper -
    // block: +0c x, +10 y
    // Offset of the main view inside 640 x 480.
    virtual int MoveMainWindowView(int x, int y) = 0;

    // 0x5e ReleasePageImageSlot   engine exe:00403250   DLL wrapper dll:100743d0
    // block: +00 window, +04 unk04, +1c slot
    // Puts the picture saved by DisplayImage's slot back and frees the slot. 0x27 invalid / free slot, 3 bad window.
    virtual int ReleasePageImageSlot(int window, int unk04, int slot) = 0;

    // 0x5f SetScrollMode   engine exe:00445970   DLL wrapper -
    // block: +04 enable
    // Scroll mode on (only if the page is larger than the view) or off.
    virtual int SetScrollMode(bool enable) = 0;

    // 0x60 DisplayPopupPage   engine exe:00450bd0   DLL wrapper -
    // block: +24 page
    // Opens a popup page (max 20). 6 unknown page, 0x11 failure.
    virtual int DisplayPopupPage(const std::string& page) = 0;

    // 0x61 ClosePopupWindow   engine exe:004502e0   DLL wrapper -
    // block: +00 window
    // 3 bad window, 0x28 popup not open.
    virtual int ClosePopupWindow(int window) = 0;

    // 0x62 Disabled62   engine -   DLL wrapper -
    // block: -
    // Always returns 10 (disabled).
    virtual int Disabled62() = 0;

    // 0x63 Disabled63   engine -   DLL wrapper -
    // block: -
    // Always returns 10 (disabled).
    virtual int Disabled63() = 0;

    // 0x64 GetSpriteAnimationSound   engine exe:004594c0   DLL wrapper -
    // block: +18 handle, +18 soundHandle
    // Sound handle of the animation's wav (-1 = none). 0x13 bad handle, 0x11 not created.
    virtual int GetSpriteAnimationSound(int handle, int& soundHandle) = 0;

    // 0x65 GetStatusBarObjectUnderCursor   engine exe:0045b4a0 exe:004490f0   DLL wrapper dll:10076810
    // block: +04 overBar, +50 object
    // overBar = 1 if the pointer is over the objects bar; object = the one under it ("" none). 0x11 if there is no bar.
    virtual int GetStatusBarObjectUnderCursor(int& overBar, std::string& object) = 0;

    // 0x66 SetCursorOverride   engine exe:00451d50   DLL wrapper -
    // block: +04 enable
    // Stores the flag in DAT_004979e0 and updates the cursor. (guess at name)
    virtual int SetCursorOverride(bool enable) = 0;

    // 0x67 FillScreen   engine exe:00411cb0   DLL wrapper -
    // block: +1c colour
    // Fills the whole 640 x 480 screen with a palette index (like ClearWindow, until the next page blit).
    virtual int FillScreen(int colour) = 0;

    // 0x68 SetCursorClipRect   engine exe:00411860 exe:00452020   DLL wrapper -
    // block: +04 manual, +c0 rect
    // manual == 1: clip the cursor to the RECT (NULL = none); else automatic.
    virtual int SetCursorClipRect(bool manual, const void* rect) = 0;

    // 0x69 GetReturnPageName   engine inline   DLL wrapper -
    // block: +24 page
    // Copies DAT_00499bf8 to +24: the name of the page the jump in progress goes to, written by the jump routine (exe:00411350, and
    // exe:00445f80 for the right-click return) just before OnExitPage; it keeps its value afterwards, empty until the first jump.
    virtual int GetReturnPageName(std::string& page) = 0;

    // 0x6a EnumPageHotspots   engine exe:00403e80 exe:00402820   DLL wrapper -
    // block: +00 window
    // Result is delivered through event slot 45 (onEnumPageHotspots). 3 bad window, 0x11.
    virtual int EnumPageHotspots(int window) = 0;

    // 0x6b Unresolved6b   engine exe:00404030   DLL wrapper -
    // block: +00 window, +14 count, +c0 names, +18 unk18, +b0 unkB0
    // Unresolved: window, a count, an array of names and two more values.
    virtual int Unresolved6b(int window, int count, void* names, int unk18, void* unkB0) = 0;

    // 0x6c GetScrollState   engine inline   DLL wrapper dll:100768a0
    // block: +04 active, +08 unk08, +ac unkAc, +14 unk14
    // active (+04) = scroll mode with a captured pointer and no modal window; the other three are only set when active (+14 only if unkAc == 1).
    virtual int GetScrollState(int& active, int& unk08, int& unkAc, int& unk14) = 0;

    // 0x6d SetScrollOptions   engine inline   DLL wrapper dll:10076930
    // block: +04 unk04, +08 unk08
    // Stored in DAT_00499bdc / DAT_00497a10; may release the captured pointer.
    virtual int SetScrollOptions(int unk04, int unk08) = 0;

    // 0x6e RaisePageImage   engine exe:00402d20   DLL wrapper -
    // block: +00 window, +45 image
    // Moves the ornament record to the end of the table: the next full paint (RedrawPage) draws it last. 3, 7.
    virtual int RaisePageImage(int window, const std::string& image) = 0;

};

// One row of the table of commands the engine implements (engineapi_impl.cpp registers every command once).
struct ApiCommandInfo {
    int code;
    const char* name;
    bool implemented;           // false: the engine answers kApiNotImplemented
    const char* category;       // stubs: the engine feature the command needs
};
const std::vector<ApiCommandInfo>& apiCommandTable();   // sorted by code, exactly 110 entries

} // namespace biosys
