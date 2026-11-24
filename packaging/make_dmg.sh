#!/bin/bash
# Packs dist/Biosys Remaster.app into dist/Biosys-Remaster-<version>-macOS.dmg: a compressed, read-only disk image
# with the usual window (the app, an arrow, the Applications folder). The window layout is set through Finder;
# if Finder may not be scripted here, the image is still built, with a plain window.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(cat VERSION)"
APP="dist/Biosys Remaster.app"
VOL="Biosys Remaster"
OUT="dist/Biosys-Remaster-$VERSION-macOS.dmg"
STAGE="dist/dmg-stage"
# Finder stores the path of the image it laid out inside the image (the background picture's alias), so the
# working image lives under a neutral name outside anyone's home folder.
WORK="/tmp/biosys-remaster-dmg"
RW="$WORK/rw.dmg"
ART="dist/art"

[ -d "$APP" ] || { echo "run packaging/make_app.sh first" >&2; exit 1; }

rm -rf "$STAGE" "$WORK" "$OUT"
mkdir -p "$WORK"
mkdir -p "$STAGE/.background"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp "$ART/dmg_background.png" "$ART/dmg_background@2x.png" "$STAGE/.background/"
# one multi-resolution picture, so the window is sharp on every display
tiffutil -cathidpicheck "$ART/dmg_background.png" "$ART/dmg_background@2x.png" -out "$STAGE/.background/background.tiff" >/dev/null 2>&1
rm "$STAGE/.background/dmg_background.png" "$STAGE/.background/dmg_background@2x.png"

hdiutil create -quiet -srcfolder "$STAGE" -volname "$VOL" -fs HFS+ -format UDRW -ov "$RW"
MNT="$(hdiutil attach -readwrite -noverify -noautoopen "$RW" | awk -F'\t' '/\/Volumes\//{print $NF}' | tail -1)"

layout_ok=1
osascript <<OSA || layout_ok=0
tell application "Finder"
    tell disk "$VOL"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 860, 568}
        set opts to the icon view options of container window
        set arrangement of opts to not arranged
        set icon size of opts to 128
        set text size of opts to 13
        set background picture of opts to file ".background:background.tiff"
        set position of item "Biosys Remaster.app" of container window to {165, 200}
        set position of item "Applications" of container window to {495, 200}
        update without registering applications
        delay 1
        close
    end tell
end tell
OSA
[ "$layout_ok" = 1 ] || echo "note: Finder could not be scripted; the disk image gets a plain window" >&2

rm -rf "$MNT/.fseventsd" "$MNT/.Trashes"
sync
hdiutil detach -quiet "$MNT" || { sleep 2; hdiutil detach -quiet -force "$MNT"; }
hdiutil convert -quiet "$RW" -format ULMO -o "$OUT"     # lzma, read-only (macOS 10.15+)
rm -rf "$WORK" "$STAGE"
# The image itself is NOT signed. macOS checks a signed disk image when a downloaded copy is opened, and an ad-hoc signature (all
# this project has) fails that check: the image of 0.1.0 and 0.1.1 did not open after a download (macOS 26: the open is refused),
# while an unsigned image opens and only the app inside goes through the "Open Anyway" step. The app inside stays ad-hoc signed.
if codesign -dv "$OUT" 2>&1 | grep -q 'Signature='; then echo "the disk image must not be signed" >&2; exit 1; fi
hdiutil verify -quiet "$OUT"
echo "built $OUT ($(du -h "$OUT" | cut -f1)), sha256 $(shasum -a 256 "$OUT" | cut -d' ' -f1)"
