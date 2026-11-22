#!/bin/sh
# QUADRA-OS — boot launcher
#
# This script is a reference boot path for the QUADRA-OS kernel + initramfs.
#
# It is NOT a bootloader — it cannot be executed as a program by the CPU.
# It describes how to boot the artefacts in this directory.
#
# Requirements (host):
#   - Linux x86_64 host (kernel 6.x+)
#   - QEMU (qemu-system-x86_64), or GRUB 2, or a BIOS/UEFI boot path
#
# QUICK BOOT (with QEMU):
#   qemu-system-x86_64 \
#     -kernel dist/bzImage \
#     -initrd dist/initramfs.cpio.gz \
#     -m 1G \
#     -nographic \
#     -append "console=ttyS0"
#
# QUICK BOOT (with GRUB 2):
#   copy dist/bzImage   -> /boot/quadra-os/vmlinuz
#   copy dist/initramfs.cpio.gz -> /boot/quadra-os/initramfs.cpio.gz
#   edit /boot/grub/grub.cfg — add:
#     menuentry "QUADRA-OS" {
#       linux  /vmlinuz root=/dev/ram0 console=ttyS0
#       initrd /initramfs.cpio.gz
#     }
#   run: grub-mkconfig -o /boot/grub/grub.cfg
#   reboot
#
# QUICK BOOT (BIOS/UEFI via EFI System Partition):
#   place dist/bzImage as /EFI/QUADRA-OS/vmlinuz
#   place dist/initramfs.cpio.gz as /EFI/QUADRA-OS/initramfs.cpio.gz
#   use a UEFI boot manager that supports initramfs
#
# Kernel: Linux 7.3.0  x86_64 (CONFIG_MODULES=n)
# initramfs: ~1.1 MB compressed, busybox 1.36.1 + brand assets
# Target: low-end PC friendly (~256 MB idle, ~14 MB kernel)
set -e

QEMU="${QEMU:-qemu-system-x86_64}"
KERNEL="$(dirname "$0")/bzImage"
INITRD="$(dirname "$0")/initramfs.cpio.gz"

echo "QUADRA-OS boot launcher — checking artefacts"
test -f "$KERNEL"  && echo "  ✓ kernel  $KERNEL"  || { echo "  ✗ kernel  $KERNEL  (missing)"; exit 1; }
test -f "$INITRD"  && echo "  ✓ initrd  $INITRD"  || { echo "  ✗ initrd  $INITRD  (missing)"; exit 1; }

echo
echo "Launching with: $QEMU"
exec "$QEMU" -kernel "$KERNEL" -initrd "$INITRD" -m 1G -nographic -append "console=ttyS0"
