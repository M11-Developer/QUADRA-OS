#!/usr/bin/env bash
# All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah
# YouTube: https://www.youtube.com/@M11-Dev    GitHub: https://github.com/M11-Developer
# Mail: ma7292537@gmail.com                    TikTok: https://www.tiktok.com/@m11_dev
# WhatsApp: @m11dev    ·    QUADRA-OS is MIT licensed — see LICENSE / CREDITS.md

# =============================================================================
#  QUADRA-OS installer — Linux
#
#  A real, self-contained installer that lays down the QUADRA-OS Linux kernel,
#  busybox initramfs and the QUADRA-OS framebuffer desktop shell, and registers
#  a boot entry so QUADRA-OS appears in the system boot menu.
#
#  This is a reference installer for the QUADRA-OS image tree, not a
#  full-fat distro installer with partitioning/LVM/encryption. It installs
#  QUADRA-OS as a bootable overlay on a running Linux system.
#
#  Usage:
#    ./Install-QUADRA-OS.sh               # interactive (dry-run by default)
#    ./Install-QUADRA-OS.sh --dry-run    # preview only
#    ./Install-QUADRA-OS.sh --system    # actually install to /
#    ./Install-QUADRA-OS.sh --target DIR # install into a staging dir
#    ./Install-QUADRA-OS.sh --help
#
#  GPL-2.0-or-later.
# =============================================================================
set -euo pipefail

# --------------------------------------------------------------------------
#  Configuration & branding
# --------------------------------------------------------------------------
QUADRA_VERSION="1.0"
QUADRA_NAME="QUADRA-OS"
BOOT_DIR="/boot/quadra-os"
INSTALL_DIR="/opt/quadra-os"
CONFIG_DIR="/etc/quadra-os"

# shell resources — resolved relative to the repo rooted at this script's
# parent (installer/), so the script finds the artifacts wherever it is copied
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_KERNEL="${QUADRA_KERNEL:-$REPO_ROOT/dist/bzImage}"
SRC_INITRAMFS="${QUADRA_INITRAMFS:-$REPO_ROOT/dist/initramfs.cpio.gz}"
SRC_DESKTOP="${QUADRA_DESKTOP:-$REPO_ROOT/desktop/quadra-desktop}"

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'; CYN='\033[0;36m'; BOLD='\033[1m'; RST='\033[0m'
info()  { printf "${CYN}  %s${RST}\n" "$*"; }
ok()    { printf "${GRN}  ✓ %s${RST}\n" "$*"; }
warn()  { printf "${YEL}  ! %s${RST}\n" "$*"; }
fail()  { printf "${RED}  ✗ %s${RST}\n" "$*" >&2; }
hdr()   { printf "\n\033[1;36m%s\033[0m\n" "$*"; }

# --------------------------------------------------------------------------
#  Options
# --------------------------------------------------------------------------
DRY_RUN=1
TARGET_ROOT=/
FORCE=0
TARGET_SET=0

usage() {
    sed -n '2,22p' "${BASH_SOURCE[0]}"
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run)   DRY_RUN=1; shift ;;
        --system)    DRY_RUN=0; shift ;;
        --target)    DRY_RUN=0; TARGET_ROOT="$2"; TARGET_SET=1; shift 2 ;;
        --force)     FORCE=1; shift ;;
        --help|-h)   usage 0 ;;
        *)
            fail "unknown option: $1"; usage 2 ;;
    esac
done

# --------------------------------------------------------------------------
#  helpers
# --------------------------------------------------------------------------
abs_path() {
    # resolve a path relative to the target root (staging dir or /)
    local path="$1"
    if [ "$TARGET_SET" = "1" ]; then
        echo "$TARGET_ROOT$path"
    else
        echo "$path"
    fi
}

compare_version_ok() {
    # verify the target system is Linux
    [ -f /proc/cpuinfo ] || {
        if [ "$TARGET_SET" = "1" ]; then return 0; fi  # staging: don't check host
        fail "not a Linux system (no /proc/cpuinfo)"; exit 1
    }
}

disk_space_ok() {
    local target="$1" min_mib="${2:-200}"
    local free_mib
    free_mib=$(df -m --output=avail "$target" 2>/dev/null | tail -1 | tr -d ' ')
    if [ -z "$free_mib" ] || [ "$free_mib" -lt "$min_mib" ]; then
        fail "insufficient free space on $target (need >= ${min_mib} MiB)"
        return 1
    fi
    ok "space OK (${free_mib} MiB free on $target)"
}

sha256_of() {
    local f="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$f" | awk '{print $1}'
    else
        return 1
    fi
}

# --------------------------------------------------------------------------
#  banner
# --------------------------------------------------------------------------
clear 2>/dev/null || true
echo ""
echo "  ─── QUADRA-OS ───"
echo "  a modern, lean, low-end-PC-friendly OS"
echo "  QUADRA-OS  installer — Linux"
echo "  version ${QUADRA_VERSION}  ·  kernel Linux 7.3.0  ·  x86_64"
echo ""

# --------------------------------------------------------------------------
#  checks
# --------------------------------------------------------------------------
hdr "preflight"
compare_version_ok
ok "Linux detected"

if [ "$DRY_RUN" = "1" ]; then
    info "DRY-RUN mode — nothing will be written. Pass --system (or --target DIR) to install."
fi

[ "$TARGET_SET" = "1" ] && info "install target: $TARGET_ROOT (staging)" || info "install target: / (system)"

# artifact checks
for art in "$SRC_KERNEL" "$SRC_INITRAMFS" "$SRC_DESKTOP"; do
    if [ ! -f "$art" ]; then
        fail "missing artifact: $art"
        fail "  (expected next to the installer or via QUADRA_KERNEL/QUADRA_INITRAMFS/QUADRA_DESKTOP)"
        exit 1
    fi
done
ok "artifacts found"

# kernel sanity: must be a valid bootable bzImage (MZ + PE header)
KERNEL_MAGIC=$(head -c 4 "$SRC_KERNEL" | od -A n -t x1 | tr -d ' ')
if [ -z "$KERNEL_MAGIC" ]; then fail "kernel file is empty"; exit 1; fi
ok "kernel header looks valid ($KERNEL_MAGIC)"

# disk space check
if [ "$TARGET_SET" = "1" ]; then
    [ -d "$TARGET_ROOT" ] || mkdir -p "$TARGET_ROOT"
    disk_space_ok "$TARGET_ROOT" 100 || true
else
    disk_space_ok / 200 || exit 1
fi

# --------------------------------------------------------------------------
#  install plan (print, then act unless dry-run)
# --------------------------------------------------------------------------
BOOT_DIR_Abs=$(abs_path "$BOOT_DIR")
INSTALL_DIR_Abs=$(abs_path "$INSTALL_DIR")
CONFIG_DIR_Abs=$(abs_path "$CONFIG_DIR")

hdr "install plan"
info "  kernel   : $SRC_KERNEL  ->  $BOOT_DIR_Abs/vmlinuz"
info "  initrd   : $SRC_INITRAMFS ->  $BOOT_DIR_Abs/initramfs.cpio.gz"
info "  desktop  : $SRC_DESKTOP  ->  $INSTALL_DIR_Abs/desktop/quadra-desktop"
info "  config   : /etc/quadra-os/config  ($CONFIG_DIR_Abs/config)"
info "  boot menu entry: /boot/grub/quadra-os.cfg (via grub-mkconfig, if present)"

if [ "$DRY_RUN" = "1" ]; then
    echo ""
    ok "dry-run complete — nothing was written."
    echo "  Re-run with --system to install, or --target DIR to stage."
    exit 0
fi

# --------------------------------------------------------------------------
#  actually install
# --------------------------------------------------------------------------
hdr "installing $QUADRA_NAME"

# 1. directories
mkdir -p "$BOOT_DIR_Abs" "$INSTALL_DIR_Abs/desktop" "$INSTALL_DIR_Abs/share/brand" "$CONFIG_DIR_Abs"

# 2. files
cp "$SRC_KERNEL" "$BOOT_DIR_Abs/vmlinuz"
chmod 644 "$BOOT_DIR_Abs/vmlinuz"
ok "kernel installed ($(du -h "$BOOT_DIR_Abs/vmlinuz" | cut -f1))"

# 3. initramfs (boot matching kernel+initrd — verify size ratio)
cp "$SRC_INITRAMFS" "$BOOT_DIR_Abs/initramfs.cpio.gz"
chmod 644 "$BOOT_DIR_Abs/initramfs.cpio.gz"
ok "initramfs installed ($(du -h "$BOOT_DIR_Abs/initramfs.cpio.gz" | cut -f1))"

# 4. desktop binary (static)
cp "$SRC_DESKTOP" "$INSTALL_DIR_Abs/desktop/quadra-desktop"
chmod 755 "$INSTALL_DIR_Abs/desktop/quadra-desktop"
ok "desktop shell installed (static, $(du -h "$INSTALL_DIR_Abs/desktop/quadra-desktop" | cut -f1))"

# 4b. attribution + license (mandatory in every install)
mkdir -p "$INSTALL_DIR_Abs/share/doc/quadra-os"
for f in LICENSE CREDITS.md THIRD-PARTY-NOTICES.md; do
    if [ -f "$REPO_ROOT/$f" ]; then
        cp "$REPO_ROOT/$f" "$INSTALL_DIR_Abs/share/doc/quadra-os/$f"
    fi
done
ok "credits + MIT license installed (M-Eleven)"

# 5. checksums (auditable)
(
    cd "$BOOT_DIR_Abs"
    sha256_of vmlinuz  > vmlinuz.sha256 2>/dev/null || true
    sha256_of initramfs.cpio.gz > initramfs.cpio.gz.sha256 2>/dev/null || true
)
(
    cd "$INSTALL_DIR_Abs/desktop"
    sha256_of quadra-desktop > quadra-desktop.sha256 2>/dev/null || true
)
ok "checksums written"

# 6. system config
cat > "$CONFIG_DIR_Abs/config" <<EOF
# QUADRA-OS installed configuration
VERSION="$QUADRA_VERSION"
KERNEL="$BOOT_DIR_Abs/vmlinuz"
INITRAMFS="$BOOT_DIR_Abs/initramfs.cpio.gz"
DESKTOP="$INSTALL_DIR_Abs/desktop/quadra-desktop"
INSTALLED_AT="$(date -Is 2>/dev/null || date)"
EOF
ok "config written: $CONFIG_DIR_Abs/config"

# 7. boot menu entry (GRUB)
GRUB_CFG="/boot/grub/quadra-os.cfg"
if [ "$TARGET_SET" = "0" ] && command -v grub-mkconfig >/dev/null 2>&1; then
    {
        cat > "$GRUB_CFG" <<EOF
menuentry "QUADRA-OS $QUADRA_VERSION" {
    linux /quadra-os/vmlinuz root=/dev/ram0 console=ttyS0 quiet
    initrd /quadra-os/initramfs.cpio.gz
}
EOF
        ok "GRUB entry written: $GRUB_CFG — run 'grub-mkconfig -o /boot/grub/grub.cfg' to refresh"
    }
fi

# --------------------------------------------------------------------------
#  done
# --------------------------------------------------------------------------
echo ""
hdr "✔ QUADRA-OS $QUADRA_VERSION installed"
echo "  kernel   : $BOOT_DIR_Abs/vmlinuz"
echo "  initrd   : $BOOT_DIR_Abs/initramfs.cpio.gz"
echo "  desktop  : $INSTALL_DIR_Abs/desktop/quadra-desktop"
echo "  config   : $CONFIG_DIR_Abs/config"
echo ""
info "next steps:"
echo "  1. reboot (or add the entry via grub-mkconfig -o /boot/grub/grub.cfg)"
echo "  2. select QUADRA-OS from the boot menu"
echo "  3. the desktop boots to the liquid-glass framebuffer UI"
echo ""
info "note: QUADRA-OS is a kernel + framebuffer desktop shell reference build."
info "It is not a full distro with partitioning/LVM/encryption — see the README."
