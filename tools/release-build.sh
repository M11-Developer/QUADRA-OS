#!/bin/bash
# All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah
# YouTube: https://www.youtube.com/@M11-Dev    GitHub: https://github.com/M11-Developer
# Mail: ma7292537@gmail.com                    TikTok: https://www.tiktok.com/@m11_dev
# WhatsApp: @m11dev    ·    QUADRA-OS is MIT licensed — see LICENSE / CREDITS.md

# =============================================================================
#  QUADRA-OS release builder
#
#  Stages the release zip and a CHECKSUMS.txt, and prints the release manifest.
#  Usage: ./build/release-build.sh [tag]
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TAG="${1:-v1.0.0}"
DIST_DIR="$REPO_ROOT/dist"
STAGE="$REPO_ROOT/dist/release-$TAG"

cd "$REPO_ROOT"

echo "=== QUADRA-OS release builder ==="
echo "tag: $TAG"
echo ""

# --- sanity checks ----------------------------------------------------------
for f in dist/bzImage dist/initramfs.cpio.gz dist/QUADRA-OS.iso desktop/quadra-desktop installer/Install-QUADRA-OS.sh installer/Install-QUADRA-OS.ps1 installer/QUADRA-OS-Installer.cmd; do
    if [ ! -f "$f" ]; then
        echo "MISSING: $f"
        echo "Run the build steps first (see README.md#building-from-source)."
        exit 1
    fi
done

# --- staging ----------------------------------------------------------------
rm -rf "$STAGE"
mkdir -p "$STAGE/installer" "$STAGE/dist" "$STAGE/desktop" "$STAGE/assets/brand" "$STAGE/docs"

cp "dist/bzImage" "$STAGE/dist/bzImage"
cp "dist/initramfs.cpio.gz" "$STAGE/dist/initramfs.cpio.gz"
cp "dist/QUADRA-OS.iso" "$STAGE/dist/QUADRA-OS.iso"
cp "desktop/quadra-desktop" "$STAGE/desktop/quadra-desktop"
cp "installer/Install-QUADRA-OS.sh" "$STAGE/installer/Install-QUADRA-OS.sh"
cp "installer/Install-QUADRA-OS.ps1" "$STAGE/installer/Install-QUADRA-OS.ps1"
cp "installer/QUADRA-OS-Installer.cmd" "$STAGE/installer/QUADRA-OS-Installer.cmd"
cp assets/brand/* "$STAGE/assets/brand/"
cp README.md "$STAGE/README.md"
cp docs/QUADRA-OS-REFERENCES.md "$STAGE/docs/QUADRA-OS-REFERENCES.md"
cp LICENSE "$STAGE/LICENSE"
cp CREDITS.md "$STAGE/CREDITS.md"
cp THIRD-PARTY-NOTICES.md "$STAGE/THIRD-PARTY-NOTICES.md"
chmod +x "$STAGE/installer/Install-QUADRA-OS.sh"

# --- checksums ----------------------------------------------------------------
(
    cd "$STAGE"
    find . -type f ! -name "CHECKSUMS.txt" | sort | xargs sha256sum > CHECKSUMS.txt
)

# --- zip ---------------------------------------------------------------------
ZIP="dist/QUADRA-OS-$TAG.zip"
rm -f "$ZIP"
(
    cd "$STAGE"
    zip -qr "$REPO_ROOT/$ZIP" .
)

echo ""
echo "=== release built ==="
echo "zip      : $ZIP"
echo "  size    : $(du -h "$ZIP" | cut -f1)"
echo "  sha256  : $(sha256sum "$ZIP" | cut -d' ' -f1)"
echo ""
echo "=== checksums ==="
(cd "$STAGE" && cat CHECKSUMS.txt)

echo ""
echo "=== artifacts ==="
find "$STAGE" -type f | sort | sed 's|^|  |'
