#!/bin/bash
# Builds dist/Biosys Remaster.app: a universal (Intel + Apple Silicon) release build for macOS 11+, with SDL3 inside
# the bundle and FreeType linked statically. Run packaging/build_deps.sh first. No original game file goes in.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(cat VERSION)"
D="dist/deps"
B="dist/build-release"
APP="dist/Biosys Remaster.app"
MIN_MACOS=11.0
ARCHS="-arch arm64 -arch x86_64"

[ -d "$D/sdl3/SDL3.framework" ] && [ -f "$D/freetype/lib/libfreetype.a" ] || { echo "run packaging/build_deps.sh first" >&2; exit 1; }

# -ffile-prefix-map keeps the build machine's folder names out of the program
make -j8 B="$B" DEVFLAGS= \
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -I$D/freetype/include/freetype2 -F$D/sdl3 $ARCHS -mmacosx-version-min=$MIN_MACOS -ffile-prefix-map=$PWD=. -DBIOSYS_VERSION=\\\"$VERSION\\\"" \
    LDFLAGS="$ARCHS -mmacosx-version-min=$MIN_MACOS" \
    LIBS="-lz $D/freetype/lib/libfreetype.a" \
    SDL_LIBS="-F$D/sdl3 -framework SDL3 -Wl,-rpath,@executable_path/../Frameworks" \
    "$B/biosys"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources/data" "$APP/Contents/Resources/licenses"
cp "$B/biosys" "$APP/Contents/MacOS/Biosys"
strip -S -x "$APP/Contents/MacOS/Biosys"
sed "s/@VERSION@/$VERSION/g" packaging/Info.plist.in > "$APP/Contents/Info.plist"

# SDL3.framework without its headers
cp -R "$D/sdl3/SDL3.framework" "$APP/Contents/Frameworks/"
rm -rf "$APP/Contents/Frameworks/SDL3.framework/Headers" "$APP/Contents/Frameworks/SDL3.framework/Versions/A/Headers"

# the port's own tables (generated from analysis, see data/README.md), the manual and the licences
cp data/*.tsv "$APP/Contents/Resources/data/"
cp LICENSE "$APP/Contents/Resources/licenses/LICENSE-Biosys-Remaster.txt"
cp DISCLAIMER.md "$APP/Contents/Resources/licenses/DISCLAIMER.txt"
cp "$D/sdl3/LICENSE.txt" "$APP/Contents/Resources/licenses/LICENSE-SDL3.txt"
cp "$D/freetype/FTL.TXT" "$APP/Contents/Resources/licenses/LICENSE-FreeType.txt"

# icon
ART="dist/art"
mkdir -p "$ART"
swift packaging/make_art.swift "$ART" >/dev/null
ICONSET="$ART/Biosys.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z $s $s "$ART/icon_1024.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$ART/icon_1024.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/Biosys.icns"

# No extended attributes, then an ad-hoc signature (there is no paid Apple developer identity behind this project;
# Apple Silicon needs at least this to start the program at all).
xattr -cr "$APP"
codesign --force --sign - --timestamp=none "$APP/Contents/Frameworks/SDL3.framework"
codesign --force --sign - --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"

echo "built $APP ($VERSION): $(lipo -archs "$APP/Contents/MacOS/Biosys")"
