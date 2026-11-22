# QUADRA-OS

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
- **License:** Kernel bzImage + quadra-desktop are GPL-2.0 (Linux kernel /
  project source). Branding, init script and userland content are
  released under the project MIT license. See `docs/QUADRA-OS-REFERENCES.md`.

## Artefacts

| File | What it is |
| --- | --- |
| `dist/bzImage` | Bootable Linux 7.3.0 x86_64 kernel with framebuffer (~14 MB) |
| `dist/initramfs.cpio.gz` | BusyBox + quadra-desktop initramfs (~1.6 MB compressed) |
| `dist/boot.sh` | Reference boot launcher (not a bootloader) |

## Boot instructions

### With QEMU (real boot)

```sh
qemu-system-x86_64 \
  -kernel dist/bzImage \
  -initrd dist/initramfs.cpio.gz \
  -m 1G \
  -nographic \
  -append "console=ttyS0"
```

### With GRUB 2

Copy `dist/bzImage` → `/boot/quadra-os/vmlinuz` and
`dist/initramfs.cpio.gz` → `/boot/quadra-os/initramfs.cpio.gz`, then add to
`/boot/grub/grub.cfg`:

```
menuentry "QUADRA-OS" {
  linux  /vmlinuz root=/dev/ram0 console=ttyS0 fb=y
  initrd /initramfs.cpio.gz
}
```

and run `grub-mkconfig -o /boot/grub/grub.cfg`.

See `dist/boot.sh` for the full reference.

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

# 3. Initramfs
cd ..
cp desktop/quadra-desktop rootfs/bin/quadra-desktop
cd rootfs
find . | /usr/bin/busybox cpio -H newc -o | gzip -9 > ../dist/initramfs.cpio.gz

# 4. Copy the kernel
cp build/linux/arch/x86/boot/bzImage dist/bzImage
```

## What's NOT in this build (yet)

- **App worlds** — Windows (Wine/Proton), Android (Waydroid),
  macOS (Darling) — still design targets; needs a much larger toolchain.
- **Persistent rootfs** — boots via initramfs; a real rootfs can be
  layered on later.
- **Installer image** — not produced yet.
- **QEMU test** — the sandbox has no QEMU; the desktop is verified via
  headless PNG snapshots, not against a live framebuffer.

## Known issues / limitations

- **Signature enforcement is stubbed** — the kernel is built with
  `CONFIG_SYSTEM_DATA_VERIFICATION=y` but no certificate is embedded
  (`certs/system_certificates.o` is an empty stub) to keep the sandbox
  build environment buildable without openssl. In a production build the
  real certificate blob must be regenerated with `scripts/extract-cert`
  + openssl.
- **No live framebuffer test** — the desktop is verified via
  `quadra-desktop --png` snapshots (PNG output at 800×480); the
  framebuffer code path (`/dev/fb0` mmap + bitfield blit) is written but
  not exercised in this sandbox.
- See `docs/QUADRA-OS-REFERENCES.md` for the full feature checklist.
