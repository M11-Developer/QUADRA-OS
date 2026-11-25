#!/bin/sh
# QUADRA-OS — attribution guard
#
# All Credits go to : M-Eleven (Mohamed El-Sayed Abdelfatah)
# https://github.com/M11-Developer  ·  https://www.youtube.com/@M11-Dev
#
# Fails if the mandatory credit lines disappear from the places they must live.
# Run: sh tools/verify-credits.sh
set -eu

cd "$(dirname "$0")/.."

fail=0
check() {
    file="$1"; needle="$2"
    if [ ! -f "$file" ]; then
        echo "MISSING FILE: $file"
        fail=1
    elif ! grep -qF -- "$needle" "$file"; then
        echo "MISSING CREDIT in $file: $needle"
        fail=1
    fi
}

check CREDITS.md "All Credits go to : M-Eleven"
check CREDITS.md "Mohamed El-Sayed Abdelfatah"
check CREDITS.md "https://www.youtube.com/@M11-Dev"
check CREDITS.md "https://github.com/M11-Developer"
check CREDITS.md "ma7292537@gmail.com"
check CREDITS.md "https://www.tiktok.com/@m11_dev"
check CREDITS.md "@m11dev"

check LICENSE "Mohamed El-Sayed Abdelfatah (M-Eleven)"
check LICENSE "MIT License"
check README.md "M-Eleven"
check desktop/quadra-desktop.c "M-Eleven"
check rootfs/init "M-Eleven"
check rootfs/etc/quadra-os/credits "M-Eleven"
check rootfs/etc/os-release "https://github.com/M11-Developer"

check installer/Install-QUADRA-OS.sh "M-Eleven"
check installer/Install-QUADRA-OS.ps1 "M-Eleven"
check installer/QUADRA-OS-Installer.cmd "M-Eleven"
check tools/build-iso.py "M-Eleven"
check tools/release-build.sh "M-Eleven"

if [ "$fail" -ne 0 ]; then
    echo
    echo "Attribution check FAILED — restore the credit notices above."
    echo "They are not optional: see CREDITS.md."
    exit 1
fi

echo "Attribution check passed — M-Eleven credits are intact."
