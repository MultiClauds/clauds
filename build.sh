#!/bin/zsh
# Builds "Clauds.app" into ./build.
#   ./build.sh              build for this Mac
#   ./build.sh --install    also copy the app to /Applications
#   ./build.sh --universal  build for Apple silicon and Intel
#   ./build.sh --dmg        universal build, and make build/Clauds-<version>.dmg
set -euo pipefail
cd "$(dirname "$0")"

INSTALL=0; DMG=0; UNIVERSAL=0
for arg in "$@"; do
  case "$arg" in
    --install) INSTALL=1 ;;
    --dmg) DMG=1; UNIVERSAL=1 ;;
    --universal) UNIVERSAL=1 ;;
    *) echo "Unknown option: $arg"; exit 2 ;;
  esac
done

VERSION="$(cat VERSION)"
# Self-update: where the app looks for new versions, and the public key that checks their signature.
# The matching secret key is only in the maintainer's Keychain (see docs/RELEASING.md).
FEED_URL="https://raw.githubusercontent.com/codeFi/clauds/main/appcast.xml"
PUBLIC_KEY="MI6m/Me8ipPtzcbsuzr0vRqWoB7EXmAMNCa/4T9oaqs="
BUILD_ARGS=(-c release)
if (( UNIVERSAL )); then BUILD_ARGS+=(--arch arm64 --arch x86_64); fi
swift build "${BUILD_ARGS[@]}"
BIN="$(swift build "${BUILD_ARGS[@]}" --show-bin-path)/Clauds"

APP="build/Clauds.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/Clauds"

# Sparkle (self-update). Copy the framework into the app, with its symlinks.
BIN_DIR="$(dirname "$BIN")"
SPARKLE="$BIN_DIR/Sparkle.framework"
[[ -d "$SPARKLE" ]] || SPARKLE="$BIN_DIR/PackageFrameworks/Sparkle.framework"
mkdir -p "$APP/Contents/Frameworks"
ditto "$SPARKLE" "$APP/Contents/Frameworks/Sparkle.framework"
cp .build/artifacts/sparkle/Sparkle/LICENSE "$APP/Contents/Resources/Sparkle-LICENSE.txt"

# App icon: the app draws its own icon, then iconutil packs it.
rm -rf build/icons
"$BIN" --render-icons build/icons
iconutil -c icns build/icons/AppIcon.iconset -o "$APP/Contents/Resources/AppIcon.icns"

# Layered icon for macOS 26 and later. Without it, macOS redraws the old-style icon and makes its
# background black in dark mode. Needs actool (part of Xcode). Without Xcode, the .icns above is used.
ICON_NAME_KEY=""
ICON_SRC="build/icons/AppIcon.icon"
mkdir -p "$ICON_SRC/Assets" build/icons/compiled
cp Resources/AppIcon.icon/icon.json "$ICON_SRC/icon.json"
cp build/icons/glyph.png "$ICON_SRC/Assets/glyph.png"
if xcrun --find actool >/dev/null 2>&1 && xcrun actool "$ICON_SRC" --compile build/icons/compiled \
     --app-icon AppIcon --platform macosx --minimum-deployment-target 14.0 \
     --output-partial-info-plist build/icons/compiled/partial.plist >/dev/null 2>&1 \
   && [[ -f build/icons/compiled/Assets.car ]]; then
  cp build/icons/compiled/Assets.car "$APP/Contents/Resources/Assets.car"
  ICON_NAME_KEY="<key>CFBundleIconName</key><string>AppIcon</string>"
else
  echo "Note: actool not found or failed. The app uses the plain icon."
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Clauds</string>
  <key>CFBundleDisplayName</key><string>Clauds</string>
  <key>CFBundleIdentifier</key><string>io.github.codefi.clauds</string>
  <key>CFBundleExecutable</key><string>Clauds</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  $ICON_NAME_KEY
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>NSHumanReadableCopyright</key><string>© 2026 Razvan Tanase. MIT License.</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>SUFeedURL</key><string>$FEED_URL</string>
  <key>SUPublicEDKey</key><string>$PUBLIC_KEY</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Ad-hoc signatures, from the inside to the outside. Sparkle must not be signed with --deep.
FW="$APP/Contents/Frameworks/Sparkle.framework"
for part in Versions/B/XPCServices/Installer.xpc Versions/B/XPCServices/Downloader.xpc \
            Versions/B/Autoupdate Versions/B/Updater.app; do
  codesign --force --sign - "$FW/$part"
done
codesign --force --sign - "$FW"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "Built: $PWD/$APP"

if (( INSTALL )); then
  pkill -x Clauds 2>/dev/null || true
  rm -rf "/Applications/Clauds.app"
  cp -R "$APP" /Applications/
  echo "Installed: /Applications/Clauds.app"
fi

# Disk image for a release: the app, a shortcut to Applications, and a short text file.
# The app has an ad-hoc signature. It is NOT notarized by Apple (see README, "Install").
if (( DMG )); then
  STAGE="build/dmg"
  OUT="build/Clauds-$VERSION.dmg"
  rm -rf "$STAGE" "$OUT"
  mkdir -p "$STAGE"
  cp -R "$APP" "$STAGE/"
  ln -s /Applications "$STAGE/Applications"
  cp Resources/dmg-read-me.txt "$STAGE/Read Me First.txt"
  hdiutil create -volname "Clauds $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$OUT" >/dev/null
  rm -rf "$STAGE"
  echo "Disk image: $PWD/$OUT"
  shasum -a 256 "$OUT"
fi
