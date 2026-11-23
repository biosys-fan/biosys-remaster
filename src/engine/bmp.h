// Minimal reader for the 8-bit uncompressed BMP files of the game (BIODATA/CURSORS/*.BMP).
#pragma once
#include "engine/image.h"

namespace biosys {

struct Bitmap8 {
    Image8 image;      // top-down, palette indices
    Palette palette;   // the file's own colour table
};

// Throws Error unless the file is an uncompressed 8-bit BMP (bottom-up or top-down) with sane sizes.
Bitmap8 readBmp8(const std::string& path);

} // namespace biosys
