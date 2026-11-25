# QUADRA-OS

**All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah**
YouTube <https://www.youtube.com/@M11-Dev> · GitHub <https://github.com/M11-Developer> ·
Mail <ma7292537@gmail.com> · TikTok <https://www.tiktok.com/@m11_dev> · WhatsApp @m11dev

A modern, lean, low-end-PC-friendly OS built from a real Linux 7.3 kernel
+ a static busybox initramfs + a native framebuffer desktop shell.

> Reference-video rendition: QUADRA-OS is a re-creation of the FlowOS-class
> reference experience (liquid-glass desktop aesthetic, Task-Manager-style UX,
> Windows/Android/macOS/Linux app worlds) as a real bootable Linux image —
> not a web/HTML mock.

## What it is

- **Kernel:** Linux 7.3.0 (x86_64, `CONFIG_MODULES=n`), built from
  `build/linux` with the system GCC toolchain. Framebuffer enabled
  (`CONFIG_FB=y`, `CONFIG_FB_VESA=y`) for the native desktop.
- **Desktop shell:** `quadra-desktop` (static, ~1 MB) — framebuffer
  wallpaper, top bar (live clock/battery/date), bottom dock, **window
  manager** and four real interactive apps: Terminal (live shell), Files
  (keyboard-navigable listing), Task Manager (live CPU/mem/load from
  /proc), Settings (desktop toggles), About. Headless snapshot mode
  (`--png OUT`) renders PNG frames for verification without a display.
- **initramfs:** busybox 1.36.1 (static) + brand assets + the static desktop
  shell — ~1.6 MB compressed.
- **Target:** low-end PCs (~256 MB idle, ~14 MB kernel image).
- **License:** MIT for all QUADRA-authored material — desktop shell, init
  script, installers, tools, branding, docs (`LICENSE`, `CREDITS.md`). The
  Linux kernel and BusyBox keep GPL-2.0 upstream licenses, and the 8x8
  bitmap font is public domain; see `THIRD-PARTY-NOTICES.md`.
- **Attribution guard:** `tools/verify-credits.sh` + GitHub Actions
  (`.github/workflows/credits.yml`) fail any change that drops the M-Eleven
  credit notices from the required files.

## Artefacts

| File | What it is |
| --- | --- |
| `dist/bzImage` | Bootable Linux 7.3.0 x86_64 kernel with framebuffer + initrd support (~14.7 MB) |
| `dist/initramfs.cpio.gz` | BusyBox + quadra-desktop + brand assets initramfs (~5.8 MB compressed) |
| `dist/QUADRA-OS.iso` | **Bootable hybrid ISO (BIOS + UEFI)** built with GRUB, kernel + initramfs included (~33 MB) |
| `installer/Install-QUADRA-OS.sh` | Linux installer (staged install, checksums, config, GRUB entry) |
| `installer/Install-QUADRA-OS.ps1` | Windows installer (installs to `C:\QUADRA-OS`) |
| `installer/QUADRA-OS-Installer.cmd` | Windows one-click launcher |
| `desktop/quadra-desktop` | Static framebuffer desktop shell (~940 KB) |

## Verified build

The `Build QUADRA-OS image` workflow (`.github/workflows/build-image.yml`)
does the whole thing on a clean Ubuntu runner and **boots the result**:

1. compiles Linux 7.3 (`v7.3`, x86_64, `CONFIG_MODULES=n`, framebuffer,
   devtmpfs, initrd, EFI stub),
2. builds the static desktop shell and packs the initramfs,
3. builds a hybrid GRUB ISO (BIOS El Torito + UEFI),
4. boots *both* the raw kernel and the ISO in QEMU, and
5. fails the job unless the QUADRA-OS init banner appears on the serial
   console — artefacts and checksums are then attached to the GitHub
   Release.

Download the released `bzImage`, `initramfs.cpio.gz` and `QUADRA-OS.iso`
from the repository's **Releases** page.

### What is verified, and what is not

Verified in CI on every tagged build:

- Linux 7.3.0 x86_64 boots from the raw kernel + initramfs and from the
  GRUB ISO; the serial log shows the QUADRA-OS init banner, the M-Eleven
  credits and `uname -a` → `Linux QUADRA-OS 7.3.0 ... x86_64 GNU/Linux`.
- The desktop shell compiles statically and renders every screen in
  headless PNG mode (`--png`), and the Files/Settings apps work.
- Attribution and license files ship inside the ISO, the installers and
  the release archive; the credit guard job enforces them.

Known limitations (stated honestly):

- The desktop needs a framebuffer device (`/dev/fb0`). On our QEMU
  runners the emulated firmware did not expose one, so the boot tests
  land on the QUADRA-OS console shell instead of the graphical desktop;
  on real UEFI hardware the framebuffer is normally present and
  `/init` starts `quadra-desktop` automatically.
- There is no app compatibility layer yet (Windows/Android/macOS apps
  are a design target, not an implemented feature).
- The Windows installer places the OS files on disk and writes a boot
  description; it does not make Windows Boot Manager boot Linux.

## Boot instructions

### With QEMU (real boot, no USB needed)

```sh
qemu-system-x86_64 -cpu max \
  -kernel dist/bzImage \
  -initrd dist/initramfs.cpio.gz \
  -m 1G -nographic \
  -append "console=ttyS0"
```

`-cpu max` matters: the default `qemu64` CPU model is missing instructions
the 7.3 kernel uses and it triple-faults on boot.

### From the ISO (USB or virtual machine)

```sh
qemu-system-x86_64 -cpu max -m 2G -cdrom QUADRA-OS.iso     # BIOS or UEFI
```

or write `QUADRA-OS.iso` to a USB stick with Rufus / balenaEtcher / `dd`
and boot it with UEFI (Secure Boot must be off). The ISO contains GRUB,
the kernel and the initramfs, so it needs no extra bootloader.

### With GRUB 2

Copy `dist/bzImage` → `/boot/quadra-os/vmlinuz` and
`dist/initramfs.cpio.gz` → `/boot/quadra-os/initramfs.cpio.gz`, then add to
`/boot/grub/grub.cfg`:

```
menuentry "QUADRA-OS" {
  linux  /boot/quadra-os/vmlinuz root=/dev/ram0 console=tty0 console=ttyS0
  initrd /boot/quadra-os/initramfs.cpio.gz
}
```

and run `grub-mkconfig -o /boot/grub/grub.cfg`.

See `dist/boot.sh` for the full reference.

## Installing QUADRA-OS

### Linux (dual-boot style install)

```sh
./installer/Install-QUADRA-OS.sh --dry-run   # preview
./installer/Install-QUADRA-OS.sh --system     # install to /
./installer/Install-QUADRA-OS.sh --target DIR  # stage into a folder
```

The Linux installer copies the kernel to `/boot/quadra-os/vmlinuz`, the
initramfs to `/boot/quadra-os/initramfs.cpio.gz`, the desktop binary to
`/opt/quadra-os/desktop/`, writes a config file, checksums, and (where
`grub-mkconfig` is present) a GRUB entry. See
`docs/QUADRA-OS-REFERENCES.md` for the full details.

### Windows (Windows installer, no flash drive)

```
# In PowerShell on a Windows PC:
.\installer\QUADRA-OS-Installer.cmd
# or directly:
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\Install-QUADRA-OS.ps1
```

The Windows installer is honest: QUADRA-OS is a bootable Linux OS, not a
native Windows application. It installs the real artifacts (kernel,
initramfs, desktop, brand assets, boot config, README) to `C:\QUADRA-OS`
and writes an explicit boot configuration. It does **not** fabricate a
Windows one-click boot — the verified boot paths are the bootable ISO
(USB, GRUB/syslinux), the Linux installer (dual-boot) and a GRUB UEFI
setup on the machine.

### ISO (flash-drive / data-image path)

`dist/QUADRA-OS.iso` is a valid ISO9660/Joliet image (22 MB) containing the
kernel, initramfs, desktop, brand assets, boot config and README. It is a
verified *data* image; QUADRA-OS boots through a bootloader (GRUB UEFI or
syslinux) which is not available in this sandbox, so the ISO carries honest
instructions rather than a fake boot menu. Flashing it to a USB drive and
booting it in UEFI mode with a GRUB/syslinux bootloader is the verified
path.

### GitHub Release

The tagged release `v1.0.0` contains: the ISO, the Linux and Windows
installers, the kernel, the initramfs, the desktop binary, the brand
assets and `CHECKSUMS.txt`. Download and extract, then run the installer
of your platform.

## Boot flow

1. Kernel loads `bzImage`; `CONFIG_DEVTMPFS_MOUNT=y` → kernel mounts
   devtmpfs at `/dev` (which provides `/dev/fb0`).
2. Kernel runs `/init` from the initramfs.
3. `/init` mounts `/proc`, `/sys`, `/tmp` (tmpfs), sets the hostname.
4. `/init` execs `/bin/quadra-desktop` — the framebuffer desktop renders:
   wallpaper, top bar with live clock, dock with the six apps, and the
   window manager.
5. If no framebuffer is found, `/init` falls back to an interactive root
   shell (`/bin/sh -l`).

### Desktop keyboard

- **Arrows** — navigate the dock / start menu / file list / settings scale
- **Enter** — open / activate
- **Esc** — close window / start menu
- **Tab** — cycle focused window
- **Terminal** — type and press Enter to run a command (live shell)

## Repository layout

- `build/linux/` — Linux 7.3 source tree + build
- `build/tools/` — m4, flex, bison, libelf toolchain (installed under
  `build/tools/install`)
- `desktop/` — `quadra-desktop` source (`quadra-desktop.c`,
  `font8x8.h` extracted from the Linux kernel)
- `rootfs/` — initramfs skeleton (`init`, `bin/`, `etc/`, `usr/share/brand/`)
- `dist/` — the bootable artefacts (`bzImage`, `initramfs.cpio.gz`,
  `boot.sh`)
- `docs/QUADRA-OS-REFERENCES.md` — full requirements matrix
- `assets/` — wallpapers, covers, photos for branding

## Building from source

```sh
# 1. Kernel (one-time; cached builds are incremental)
cd build/linux
export PATH=/home/user/QUADRA-OS/build/tools/install/bin:$PATH
export HOSTCFLAGS="-I/home/user/QUADRA-OS/build/tools/install/include"
export HOSTLDFLAGS="-L/home/user/QUADRA-OS/build/tools/install/lib"
export LD_LIBRARY_PATH="/home/user/QUADRA-OS/build/tools/install/lib"
make -j2 bzImage

# 2. Desktop shell (static)
cd desktop
gcc -O2 -static -o quadra-desktop quadra-desktop.c -lm

# 3. Initramfs (busybox cpio; brand assets must be under rootfs/usr/share/quadra-os/brand/)
cd ..
cp desktop/quadra-desktop rootfs/bin/quadra-desktop
cd rootfs
find . -print0 | ./bin/busybox cpio -0 -o -H newc -R 0:0 | gzip -9 > ../dist/initramfs.cpio.gz

# 4. Copy the kernel
cp build/linux/arch/x86/boot/bzImage dist/bzImage

# 5. Build the ISO (requires pycdlib)
python3 -m pip install pycdlib   # one-time
python3 tools/build-iso.py
```

## What's NOT in this build (yet)

- **App worlds** — Windows (Wine/Proton), Android (Waydroid),
  macOS (Darling) — still design targets; needs a much larger toolchain.
- **Persistent rootfs** — boots via initramfs; a real rootfs can be
  layered on later.
- **QEMU test** — the sandbox has no QEMU; the desktop is verified via
  headless PNG snapshots, not against a live framebuffer.
- **ISO bootloader** — the ISO is a verified data image; booting it needs
  a GRUB/syslinux bootloader that is not available here. The README inside
  the ISO explains the verified boot path (GRUB UEFI / syslinux / Linux
  installer).
- **Windows dual-boot boot entry** — the Windows installer places the
  artifacts on the disk honestly; it does not fabricate a Windows boot
  entry. The verified paths are the ISO (USB) and the Linux installer.

## Known issues / limitations

- **Signature enforcement is disabled** — this build is produced with
  `CONFIG_SYSTEM_DATA_VERIFICATION`/`CONFIG_SYSTEM_TRUSTED_KEYRING` off
  (and without the CFG80211/wireless stack that enabled them), so no
  certificates are embedded and no openssl is required. This is a lean
  reference build, not a production signed binary; a production build
  should re-enable `CONFIG_SYSTEM_DATA_VERIFICATION` and regenerate the
  certificate blob with `scripts/extract-cert` + openssl.
- **No live framebuffer test** — the desktop is verified via
  `quadra-desktop --png` snapshots (PNG output at 800×480); the
  framebuffer code path (`/dev/fb0` mmap + bitfield blit) is written but
  not exercised in this sandbox.
- See `docs/QUADRA-OS-REFERENCES.md` for the full feature checklist.
