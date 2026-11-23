#!/bin/bash
# Fetches and builds the two libraries the released app carries inside it, for Intel and Apple Silicon, macOS 11+:
#   SDL3      the official release (SDL3.framework, universal), from github.com/libsdl-org/SDL
#   FreeType  built from source as a static library, from download.savannah.gnu.org
# FreeType is built without its optional helpers (png, harfbuzz, brotli, bzip2) and without the Apple HVF module, which
# would tie the app to the newest macOS. Both downloads are checked against the SHA-256 written here. Result: dist/deps/sdl3, dist/deps/freetype.
set -euo pipefail
cd "$(dirname "$0")/.."

SDL_VERSION=3.4.20
SDL_SHA256=36b7e4cefcf7d55d227b3df5e686f8f33ab34193173a976445d1b57c80009e44
FT_VERSION=2.15.0
FT_SHA256=93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
MIN_MACOS=11.0
ARCHS="-arch arm64 -arch x86_64"

D="$PWD/dist/deps"
mkdir -p "$D/sdl3-dl" "$D/freetype-dl"

fetch() {   # url file sha256
    if [ ! -f "$2" ]; then curl -fsSL -o "$2" "$1"; fi
    echo "$3  $2" | shasum -a 256 -c - >/dev/null || { echo "checksum mismatch: $2" >&2; exit 1; }
}

fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.dmg" "$D/sdl3-dl/SDL3-$SDL_VERSION.dmg" "$SDL_SHA256"
fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FT_VERSION.tar.xz" "$D/freetype-dl/freetype-$FT_VERSION.tar.xz" "$FT_SHA256"

if [ ! -d "$D/sdl3/SDL3.framework" ]; then
    hdiutil attach -nobrowse -readonly -mountpoint "$D/sdl3-mnt" "$D/sdl3-dl/SDL3-$SDL_VERSION.dmg" >/dev/null
    mkdir -p "$D/sdl3"
    cp -R "$D/sdl3-mnt/SDL3.xcframework/macos-arm64_x86_64/SDL3.framework" "$D/sdl3/"
    cp "$D/sdl3-mnt/LICENSE.txt" "$D/sdl3/"
    hdiutil detach "$D/sdl3-mnt" >/dev/null
fi

if [ ! -f "$D/freetype/lib/libfreetype.a" ]; then
    rm -rf "$D/freetype-src"
    mkdir -p "$D/freetype-src"
    tar -xJf "$D/freetype-dl/freetype-$FT_VERSION.tar.xz" -C "$D/freetype-src" --strip-components 1
    ( cd "$D/freetype-src"
      ./configure --prefix="$D/freetype" --enable-static --disable-shared \
          --with-zlib=yes --with-bzip2=no --with-png=no --with-harfbuzz=no --with-brotli=no --with-librsvg=no \
          ac_cv_header_hvf_Scaler_h=no \
          CC=clang CFLAGS="-O2 $ARCHS -mmacosx-version-min=$MIN_MACOS" LDFLAGS="$ARCHS -mmacosx-version-min=$MIN_MACOS" > "$D/freetype-configure.log" 2>&1
      make -j8 > "$D/freetype-make.log" 2>&1
      make install > "$D/freetype-install.log" 2>&1 )
    cp "$D/freetype-src/LICENSE.TXT" "$D/freetype-src/docs/FTL.TXT" "$D/freetype/"
fi
echo "dependencies ready in $D"
