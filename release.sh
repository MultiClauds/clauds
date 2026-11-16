#!/bin/zsh
# Makes the files for a release: the DMG, and the signed update feed (appcast.xml).
# Read docs/RELEASING.md first. This script publishes nothing.
set -euo pipefail
cd "$(dirname "$0")"

VERSION="$(cat VERSION)"
TOOLS=".build/artifacts/sparkle/Sparkle/bin"
ACCOUNT="claude-switcher"          # name of the secret key in the maintainer's Keychain
NOTES="release-notes/$VERSION.html"
REPO_URL="https://github.com/codeFi/clauds"

[[ -f "$NOTES" ]] || { echo "Missing $NOTES. Write the release notes first."; exit 1; }
if [[ -n "$(git status --porcelain -- . ':!appcast.xml')" ]]; then
  echo "Commit your changes first. The DMG must come from a commit."; exit 1
fi

./build.sh --dmg

# The feed tool reads a folder: the DMG, and its notes with the same file name.
STAGE="build/release"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "build/Clauds-$VERSION.dmg" "$STAGE/"
cp "$NOTES" "$STAGE/Clauds-$VERSION.html"

# Signs the DMG with the secret key and adds the new version to appcast.xml.
"$TOOLS/generate_appcast" --account "$ACCOUNT" \
  --download-url-prefix "$REPO_URL/releases/download/v$VERSION/" \
  --embed-release-notes --link "$REPO_URL" --maximum-deltas 0 \
  -o appcast.xml "$STAGE"

# Check: the signature in the feed must match the DMG.
SIGNATURE="$(grep -o 'sparkle:edSignature="[^"]*"' appcast.xml | head -1 | cut -d'"' -f2)"
"$TOOLS/sign_update" --account "$ACCOUNT" --verify "build/Clauds-$VERSION.dmg" "$SIGNATURE"
echo "Signature in appcast.xml is valid."
echo "SHA-256: $(shasum -a 256 "build/Clauds-$VERSION.dmg" | awk '{print $1}')"
echo "Next: commit appcast.xml, tag v$VERSION, push, and make the GitHub release (docs/RELEASING.md)."
