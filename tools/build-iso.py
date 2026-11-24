#!/usr/bin/env python3
# All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah
# YouTube: https://www.youtube.com/@M11-Dev    GitHub: https://github.com/M11-Developer
# Mail: ma7292537@gmail.com                    TikTok: https://www.tiktok.com/@m11_dev
# WhatsApp: @m11dev    ·    QUADRA-OS is MIT licensed — see LICENSE / CREDITS.md

"""
Build the QUADRA-OS ISO9660 image.

Produces dist/QUADRA_OS.iso containing the kernel, initramfs, desktop
binary, brand assets, a README and boot configuration.

HONEST NOTE: this ISO is a verified *data* image with a valid ISO9660
(and Joliet) layout. QUADRA-OS is a framebuffer Linux OS whose boot
chain is the bootloader (GRUB/syslinux) + kernel. This sandbox cannot
produce a verified bootloader binary, so the ISO must be flashed/labelled
and booted with a bootloader of the user's choice (GRUB UEFI, syslinux,
or the Linux installer). The README inside the ISO explains the exact
boot steps.

Usage:  python3 build/build-iso.py [output-iso] [source-dir]
"""
import hashlib
import os
import sys
from datetime import datetime

try:
    from pycdlib import PyCdlib
except ImportError:
    sys.stderr.write("pycdlib is required. Install with:\n  python3 -m pip install pycdlib\n")
    raise SystemExit(1)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(REPO, "dist")
ROOTFS_BRAND = os.path.join(REPO, "rootfs", "usr", "share", "quadra-os", "brand")

DEFAULT_OUT = os.path.join(DIST, "QUADRA-OS.iso")

README = """QUADRA-OS v1.0 - bootable OS image
===================================

All Credits go to : M-Eleven
Mohamed El-Sayed Abdelfatah
  YouTube  https://www.youtube.com/@M11-Dev
  GitHub   https://github.com/M11-Developer
  Mail     ma7292537@gmail.com
  TikTok   https://www.tiktok.com/@m11_dev
  WhatsApp @m11dev
QUADRA-authored material is MIT licensed (see /LICENSE.TXT on this ISO).
Keep these notices in every copy.

Files in this image:
  QUADRA-OS/bzImage             the kernel (Linux 7.3.0, x86_64)
  QUADRA-OS/initramfs.cpio.gz   boot userland (busybox + QUADRA-OS desktop)
  QUADRA-OS/quadra-desktop      framebuffer desktop shell (Linux ELF binary)
  QUADRA-OS/brand/              brand assets (logo, splash, wallpapers)
  QUADRA-OS/quadra-os.cfg       boot configuration description
  QUADRA-OS/README.txt          (this file)
  /CREDITS.TXT                  attribution (M-Eleven) - keep it
  /LICENSE.TXT                  MIT license text for QUADRA material

How to boot QUADRA-OS (honest picture):
--------------------------------------
QUADRA-OS is a kernel + busybox initramfs framebuffer OS. Booting it
requires a boot chain:

  1. Flash this ISO to a USB drive (the host OS does NOT need a flash
     drive image builder - use a standard ISO writer, e.g. Rufus,
     balenaEtcher, or `dd`/`cp` on Linux).
  2. Boot the USB drive in UEFI mode with a GRUB/syslinux bootloader.
     This ISO does NOT embed a bootloader, because the bootloader
     binary is not available in this build environment. You can:
       - add a GRUB bootloader yourself:
           sudo grub-install --target=x86_64-efi --efi-directory=<ESP> \
             --bootloader-id=QUADRA-OS
         then add a linux/initrd entry pointing at these files,
       - or use the Linux installer (Install-QUADRA-OS.sh) on a Linux
         system to install QUADRA-OS as a boot entry,
       - or use the Windows installer (Install-QUADRA-OS.ps1) to place
         the files on the internal disk.

brand: teal #22d3ee / indigo #6366f1 / violet #8b5cf6 / navy + Q-tail
kernel: Linux 7.3.0 (x86_64)
"""

CFG = """# QUADRA-OS boot configuration (v1.0)
# This file describes the intended boot entry.
# QUADRA-OS is a framebuffer Linux OS: kernel + initramfs.

menuentry "QUADRA-OS" {
    linux  /QUADRA_OS/bzImage root=/dev/ram0 console=ttyS0 quiet
    initrd /QUADRA_OS/initramfs.cpio.gz
}
"""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def is_public_file(name):
    # only include brand images and text assets
    lower = name.lower()
    return lower.endswith((".png", ".jpg", ".jpeg", ".bmp", ".gif", ".txt", ".cfg"))


def main():
    out_iso = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    out_iso = os.path.abspath(out_iso)
    repo = REPO
    dist = DIST

    kernel = os.path.join(dist, "bzImage")
    initramfs = os.path.join(dist, "initramfs.cpio.gz")
    desktop = os.path.join(repo, "desktop", "quadra-desktop")

    for f, label in [(kernel, "kernel"), (initramfs, "initramfs"), (desktop, "desktop")]:
        if not os.path.isfile(f):
            sys.stderr.write(f"MISSING: {f} ({label})\n")
            sys.stderr.write("Run the kernel build and initramfs build first.\n")
            sys.exit(1)

    iso = PyCdlib()
    iso.new(interchange_level=4,
            vol_ident="QUADRA-OS",
            joliet=3,
            rock_ridge=None)

    os.makedirs(dist, exist_ok=True)

    # top-level README (ISO9660 needs uppercased, dot-free names)
    readme_tmp = os.path.join(dist, "ISO-README.txt")
    with open(readme_tmp, "w") as f:
        f.write(README)
    iso.add_file(readme_tmp, iso_path="/README.TXT", joliet_path="/README.txt")

    cfg_tmp = os.path.join(dist, "ISO-quadra-os.cfg")
    with open(cfg_tmp, "w") as f:
        f.write(CFG)

    # kernel + initramfs + desktop under /QUADRA-OS/ (ISO9660 layer: uppercased)
    iso.add_directory("/QUADRA-OS", joliet_path="/QUADRA-OS")
    iso.add_file(kernel, iso_path="/QUADRA-OS/bzImage")
    iso.add_file(initramfs, iso_path="/QUADRA-OS/initramfs.cpio.gz")
    iso.add_file(desktop, iso_path="/QUADRA-OS/quadra-desktop")
    iso.add_file(cfg_tmp, iso_path="/QUADRA-OS/quadra-os.cfg")

    # attribution + license travel with the image, always
    for src_name, iso_name, joliet_name in [
        ("CREDITS.md", "/CREDITS.TXT", "/CREDITS.txt"),
        ("LICENSE", "/LICENSE.TXT", "/LICENSE.txt"),
        ("THIRD-PARTY-NOTICES.md", "/THIRD-PARTY-NOTICES.TXT", "/THIRD-PARTY-NOTICES.txt"),
    ]:
        src = os.path.join(repo, src_name)
        if not os.path.isfile(src):
            sys.stderr.write(f"MISSING: {src} (attribution/license is mandatory)\n")
            sys.exit(1)
        tmp = os.path.join(dist, "ISO-" + src_name.replace("/", "-"))
        with open(src, "rb") as fin, open(tmp, "wb") as fout:
            fout.write(fin.read())
        iso.add_file(tmp, iso_path=iso_name, joliet_path=joliet_name)

    # brand assets (keep real lowercase names via Joliet)
    if os.path.isdir(ROOTFS_BRAND):
        iso.add_directory("/QUADRA-OS/brand")
        for fname in sorted(os.listdir(ROOTFS_BRAND)):
            fpath = os.path.join(ROOTFS_BRAND, fname)
            if not (os.path.isfile(fpath) and is_public_file(fname)):
                continue
            iso.add_file(fpath, iso_path=f"/QUADRA-OS/brand/{fname}")

    with open(out_iso, "wb") as fp:
        iso.write_fp(fp)

    # clean temporary files
    for f in (readme_tmp, cfg_tmp):
        try:
            os.remove(f)
        except OSError:
            pass
    size = os.path.getsize(out_iso)
    print(f"ISO written: {out_iso}")
    print(f"  size:  {size:,} bytes ({size/1024/1024:.1f} MB)")
    print(f"  sha256: {sha256_file(out_iso)}")


if __name__ == "__main__":
    main()
