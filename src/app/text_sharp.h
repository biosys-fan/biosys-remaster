// "Sharp text" (--text sharp): the GDI text of the game drawn at the window's real resolution instead of enlarged in blocks. SDL-free (the
// window uploads the overlay as a texture; the headless capture composes on the CPU).
//
// The engine (Engine::renderText, engine/textplane.h) hands over the frame without its low-resolution text (`free`), and the text runs
// that show in the final frame with the pixels each one owns. A run is drawn again from its own data: the same face at ppem * scale
// (Font::glyphScaled, FreeType anti-aliasing), the pen at the low-resolution pen position times the scale (so line breaks, centring and
// right alignment are the 640 x 480 layout, including its deliberate oddities), the baseline at (y + ascent) * scale. A glyph pixel is
// drawn only inside the run's cell, its clip rectangle and one low-resolution pixel around the run's own visible text pixels, so what the
// original frame draws over text (a sprite, a window, another page layer) stays over it.
#pragma once
#include "engine/engine.h"
#include <cstdint>
#include <vector>

namespace biosys {

// The sharp glyphs of tf.runs alone: premultiplied-free ARGB (0xAARRGGBB, straight alpha) of 640 * scale x 480 * scale; pixels no glyph
// touches are 0. Returns how many pixels got a non-zero alpha.
size_t renderTextOverlay(const Engine::TextFrame& tf, int scale, std::vector<uint32_t>& argb);

// The same for the overlay interface (--interface overlay), whose view scale k is not a whole number: the glyphs of the runs of every layer but
// `skipLayer` (the top bar's text is drawn by the overlay's own words), only for the frame rows [row0, row0 + rows), at scale k. The ARGB has
// outW x outH = (ceil(640 k) + 2) x (ceil(rows k) + 2) pixels; (fx, fy) is where the corner of those rows lies inside pixel (0, 0) of it (the
// view's corner is at a fraction of a window pixel). Returns how many pixels got a non-zero alpha.
size_t renderTextOverlayScaled(const Engine::TextFrame& tf, double k, int row0, int rows, double fx, double fy, const char* skipLayer,
                               std::vector<uint32_t>& argb, int& outW, int& outH);

// The text-free frame enlarged by `scale` (nearest, as the window does) with the overlay blended over it: 0x00RRGGBB, 640 * scale x 480 * scale.
// `base`: the picture to put the text on instead of the enlarged text-free frame (the smooth panorama capture of that frame).
void composeSharpText(const Engine::TextFrame& tf, int scale, std::vector<uint32_t>& rgb, const std::vector<uint32_t>* base = nullptr);

// A number that changes whenever the overlay would: the runs, their visible pixels, their colours, the scale.
uint64_t textOverlaySignature(const Engine::TextFrame& tf, int scale);

// Script commands (app/script.cpp): sharptext on|off | textshot FILE SCALE [PREFIX] [smooth] | textframes PREFIX | textruns FILE | textredraw FILE.
// Returns false if `op` is not one of them.
bool scriptTextCommand(Engine& e, const std::string& op, const std::string& arg);

} // namespace biosys
