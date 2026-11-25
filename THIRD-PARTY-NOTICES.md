# QUADRA-OS — Third-Party Notices

QUADRA-OS is built on top of upstream components that keep their own licenses.
This file exists so the project never claims MIT over someone else's work.

| Component | Where | License | Upstream |
| --- | --- | --- | --- |
| Linux kernel (bzImage) | `dist/bzImage`, `rootfs/` boot chain | **GPL-2.0** | <https://www.kernel.org/> |
| BusyBox 1.36.1 (static) | `rootfs/bin/`, `rootfs/sbin/` | **GPL-2.0** | <https://busybox.net/> |
| 8x8 bitmap font | `desktop/font8x8.h` | **Public domain** (Daniel Hepper, based on the IBM PC BIOS 8x8 charset) | <https://github.com/dhepper/font8x8> |

## What this means for redistribution

- You may redistribute QUADRA-OS unmodified.
- The Linux kernel and BusyBox binaries are GPL-2.0: if you redistribute a
  modified kernel or BusyBox, you must follow GPL-2.0 (including offering the
  corresponding source).
- `desktop/quadra-desktop` is a QUADRA-authored work released under MIT, and
  the only bundled third-party material in it (the bitmap font) is public
  domain, so the desktop binary itself contains no GPL code.
- The combined OS image is a distribution that contains GPL-2.0 components;
  that does not relicense QUADRA-authored material, but it does mean a
  redistributor must honour GPL-2.0 for the kernel and BusyBox parts.

All credit for QUADRA-OS itself: **M-Eleven — Mohamed El-Sayed Abdelfatah**.
See [`CREDITS.md`](CREDITS.md) and [`LICENSE`](LICENSE).
