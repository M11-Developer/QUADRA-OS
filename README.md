# QUADRA-OS

A modern, lean, low-end-PC-friendly OS built from a real Linux 7.3 kernel
+ minimal busybox initramfs.

> Reference-video rendition: QUADRA-OS is a re-creation of the
> FlowOS-class reference experience (liquid-glass desktop aesthetic,
> Task-Manager-style UX, Windows/Android/macOS/Linux app worlds) as a
> real bootable Linux image — not a web/HTML mock.

## What it is

- **Kernel:** Linux 7.3.0 (x86_64, `CONFIG_MODULES=n`), built from
  `build/linux` with the system GCC toolchain.
- **initramfs:** busybox 1.36.1 (static) + brand assets — ~1.1 MB compressed.
- **Target:** low-end PCs (~256 MB idle, ~14 MB kernel image).
- **License:** Kernel bzImage is GPL-2.0 (Linux kernel). Branding,
  init script, and userland content are released under the project MIT
  license. See `docs/QUADRA-OS-REFERENCES.md` for the full
  requirements matrix.

## Artefacts

| File | What it is |
| --- | --- |
| `dist/bzImage` | Bootable Linux 7.3.0 x86_64 kernel (~14 MB) |
| `dist/initramfs.cpio.gz` | BusyBox initramfs with `/init` (~1.1 MB, only rootfs files) |
| `dist/boot.sh` | Reference boot launcher (not a bootloader) |

## Boot instructions

### With QEMU

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
  linux  /vmlinuz root=/dev/ram0 console=ttyS0
  initrd /initramfs.cpio.gz
}
```

and run `grub-mkconfig -o /boot/grub/grub.cfg`.

See `dist/boot.sh` for the full reference.

## Boot flow

1. Kernel loads `bzImage`; `CONFIG_DEVTMPFS_MOUNT=y` → kernel mounts
   devtmpfs at `/dev`.
2. Kernel runs `/init` from the initramfs.
3. `/init` mounts `/proc`, `/sys`, `/tmp` (tmpfs), sets the hostname,
   prints the QUADRA-OS banner + motd, summarises resources.
4. `exec /bin/sh -l` keeps a root shell on the console.

## What's NOT in this build (yet)

- A window manager / full GUI desktop. QUADRA-OS uses the kernel +
  initramfs but is not a distro with a graphical desktop; the kernel
  provides a console shell (getty/tty1).
- Persistent rootfs (this boots via initramfs; a real rootfs target can
  be layered on later).
- The FlowOS-style apps (Files, Terminal, Photos, Store) are design
  targets derived from the reference video — see
  `docs/QUADRA-OS-REFERENCES.md`.

## Repository layout

- `build/linux/` — Linux 7.3 source tree + build (gzImage artifacts)
- `build/tools/` — m4, flex, bison, libelf toolchain (installed under
  `build/tools/install`)
- `rootfs/` — initramfs skeleton (`init`, `bin/`, `etc/`, `usr/share/brand/`)
- `dist/` — the bootable artefacts (`bzImage`, `initramfs.cpio.gz`,
  `boot.sh`)
- `docs/QUADRA-OS-REFERENCES.md` — full requirements matrix
- `assets/` — wallpapers, covers, photos for branding

## Building from source

See `docs/QUADRA-OS-REFERENCES.md` and the `build/linux/` tree. Invoke:

```sh
cd build/linux
export PATH=/home/user/QUADRA-OS/build/tools/install/bin:$PATH
export HOSTCFLAGS="-I/home/user/QUADRA-OS/build/tools/install/include"
export HOSTLDFLAGS="-L/home/user/QUADRA-OS/build/tools/install/lib"
export LD_LIBRARY_PATH="/home/user/QUADRA-OS/build/tools/install/lib"
make -j2 bzImage
```

Then rebuild the initramfs:

```sh
cd rootfs
find . | /usr/bin/busybox cpio -H newc -o | gzip -9 > /home/user/QUADRA-OS/dist/initramfs.cpio.gz
cp build/linux/arch/x86/boot/bzImage dist/bzImage
```

## Known issues / limitations

- **Signature enforcement is stubbed** — the kernel is built with
  `CONFIG_SYSTEM_DATA_VERIFICATION=y` but no certificate is embedded
  (`certs/system_certificates.o` is an empty stub) to keep the
  sandbox build environment buildable without openssl. In a production
  build the real certificate blob must be regenerated with
  `scripts/extract-cert` + openssl.
- **No QEMU in this environment** — the kernel has not been booted;
  `bzImage` and `initramfs` are produced and verified structurally.
- See `docs/QUADRA-OS-REFERENCES.md` for the full feature checklist.
