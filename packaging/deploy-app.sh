#!/bin/bash
# Deploy the freshly built SUPERNOVA.app to /Applications with a STABLE code
# signing identity, so macOS TCC (Screen Recording) keeps its grant across
# rebuilds instead of re-prompting on every cdhash change.
#
# Why a stable cert matters:
#   ad-hoc signing (--sign -) → designated requirement = cdhash (changes every
#   build) → TCC invalidates the Screen Recording grant + spawns ghost rows.
#   A cert-based identity → requirement = `identifier ... and certificate leaf`
#   (STABLE across rebuilds) → grant sticks. The cert is local & self-signed
#   (free); the paid Apple Developer cert is only needed to DISTRIBUTE to others.
#
# One-time setup:  ./packaging/make-signing-cert.sh   (creates "SUPERNOVA Local")
# Then every deploy:  ./packaging/deploy-app.sh
set -euo pipefail

CERT_NAME="${SNV_SIGN_IDENTITY:-SUPERNOVA Local}"
BUNDLE_ID="com.ovni.supernova.app"
DEST="/Applications/SUPERNOVA.app"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${SNV_BUILD_DIR:-$REPO_ROOT/build}"
SRC="$BUILD_DIR/plugins/supernova/app/supernova_app_artefacts.noindex/Release/SUPERNOVA.app"

if ! security find-certificate -c "$CERT_NAME" >/dev/null 2>&1; then
  echo "ERROR: signing identity '$CERT_NAME' not found in your keychain." >&2
  echo "       Run ./packaging/make-signing-cert.sh once to create it." >&2
  exit 1
fi

if [ ! -d "$SRC" ]; then
  echo "ERROR: built app not found at:" >&2
  echo "       $SRC" >&2
  echo "       Build it first (Release), or set SNV_BUILD_DIR=/path/to/build." >&2
  exit 1
fi

echo "==> replacing $DEST"
rm -rf "$DEST"
cp -R "$SRC" "$DEST"

echo "==> signing with stable identity: $CERT_NAME"
codesign --force --sign "$CERT_NAME" --identifier "$BUNDLE_ID" --timestamp=none "$DEST"

echo "==> verifying"
codesign --verify --verbose=2 "$DEST"
codesign -dv --verbose=2 "$DEST" 2>&1 | grep -Ei "Authority|flags"
codesign -d --requirements - "$DEST" 2>&1 | grep -i designated

echo "==> refreshing Launch Services"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$DEST"

cat <<'NOTE'

Done. Launch ONLY with:  open /Applications/SUPERNOVA.app
(never the inner binary, never from the build tree — that spawns TCC ghost rows.)

First launch after switching from ad-hoc to the cert, macOS re-asks Screen
Recording ONCE. Grant it. From then on the cert requirement is stable, so the
grant persists across every future rebuild+deploy.
NOTE
