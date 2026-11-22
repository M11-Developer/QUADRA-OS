# QUADRA-OS — Reference & Requirement Matrix

**Date:** 2026-11-22
**Branch:** `arena/db69c6e7-quadra-os`
**Status:** Reference doc — not a commit-locked design. Built from the FlowOS reference video, the FlowOS website, and the machine reality in this sandbox.

## 1. What the reference video shows (frame-by-frame audit)

The attached reference video (640×360, 30fps, ~4:18) shows a FlowOS-style operating system experience. Verified on 43 sampled frames:

- Intro / splash screens
- Liquid-glass desktop: wallpaper + **top bar** (clock, status icons)
- Desktop watermark ("FlowOS" style, bottom-right)
- **Control Center** — network, Bluetooth, dark mode, sound, desktops
- **Start menu** — search, pinned apps, recent files
- **Lock screen** — time and date over wallpaper
- **Launchpad** — every installed app, with search
- **Task Manager** — Processes, performance, startup apps (opened with Ctrl+Shift+Esc)
- **Settings** — System, network, personalization, accounts, updates
- **Files** — Home, Documents, Downloads, Pictures
- **Dynamic island** — notifications pill (like macOS)
- **Windows → QUADRA-OS transition** ("This will be great…") — the video transitions from a Windows-ish desktop to the target OS
- Platform platform cards: Windows · Android · macOS · Linux
- App demos: VLC / Notepad++ / SuperTux / Obsidian / Telegram / Signal
- **Store front** — install buttons on app cards
- **Performance page** — RAM / CPU readouts
- "What Windows 11 asks for" — TPM 2.0, SSE4.2, Secure Boot, POPCNT
- "Runs on every PC" — low-end PC fit
- **Installer UI** — steps: welcome → choose install → progress → done
- Closing screens: liquid glass, "Early access opens this December"

## 2. What flowos.veloticstudios.com documents (primary reference)

Site content (fetched 2026-11-22):

- **Core promise:** "Windows, Android, Mac and Linux apps on one desktop."
- **Name/tagline:** "FlowOS — Windows, Android, Mac and Linux apps. One desktop."
- **Desktop:** Liquid glass — top bar + dock, frosted acrylic over the wallpaper
- **Features (liquid glass area):** Desktop (top bar + dock), Start (search, pinned apps, recent files), Control Center (network, Bluetooth, dark mode, sound, desktops), Launchpad (every installed app + search), Lock screen (time + date over wallpaper), Light mode (one click)
- **App worlds:** Windows (.exe · .msi) · Android (.apk · Play Store) · macOS (.app · .dmg) · Linux (.deb · .rpm · Arch · Flatpak/Flathub)
- **Velotic Store:** one store — popular apps with an Install button
- **Coming from Windows:** Start, Settings, Task Manager behave the way hands expect; Ctrl+Shift+Esc opens Task Manager
- **Settings:** System, network, personalization, accounts, updates
- **Files:** Home, Documents, Downloads, Pictures
- **Memory:** Full desktop 2 GB recommended · Light desktop from 256 MB · Tiny mode 128 MB (old BIOS PCs only)
- **Not required:** TPM 2.0, Secure Boot, SSE4.2, POPCNT (desktop) — Android apps need SSE4.2; Adobe apps need SSE4.2 + AVX2
- **Installer:** Install next to Windows (keeps files), choose drive, copy, restart; Secure Boot must be off
- **Requirements:** 64-bit Intel/AMD (x86-64); disk ≥ 20 GB (64 GB suggested); internet for installer + app-world enablement; Windows Engine needs HW virtualization + 40 GB free + 8 GB RAM + own Win11 Pro license
- **Memory usage (full desktop):** ~435–460 MB; light desktop ~230–250 MB
- **Updates:** checked daily, never forced
- **macOS apps:** preview, off by default (double-click .dmg to enable); tested: SuperTux and Platypus
- **Installer:** 4 steps (welcome → choose → progress → done)
- **Support/app list:** tested app list split into Windows / Android / macOS / Linux — with Works and Testing status, and an "Ask us on Discord" channel for apps not listed
- **Roadmap (Dec 2026 → Feb 2027):** December 2026 early access + Windows Engine 1.0 · January 2027 Windows Engine 2.0 + 32-bit/ARM64 phases · February 2027 full ARM support + themes/desktop customization
- **Privacy:** no telemetry; only update checks and the daily license check (PROPRIETARY model, $20 one-time, one PC per license)
- **App list categories (Works, from the /apps page):**
  - Windows: 7-Zip, VLC, Firefox, Discord, Telegram, GIMP, OBS Studio, Audacity, LibreOffice, Notepad++, VS Code, WinRAR, Everything, KeePassXC, PuTTY, Git, Python, Spotify, Minecraft Launcher, LMMS, etc. (100+ vs 40)
  - Android: Telegram, VLC, Organic Maps, VN Video Editor, Termux, F-Droid, NewPipe, Fennec, YouTube, Gmail*, Home Assistant*, Shelter (works); WhatsApp (testing)
  - macOS: SuperTux, Platypus (works); VLC, Chrome (testing)
  - Linux: Chrome, Discord, Obsidian, VS Code, 1Password, Zoom, GIMP, Claude Desktop, LM Studio, DOSBox Staging, PCSX2, Brave, LocalSend, Vesktop, AnyDesk, Signal, RustDesk, FileZilla, EasyEffects, FreeCAD, KeePassXC, OBS Studio, Ferdium, Node.js, Docker, Kodi, Steam, Steam Link, Spotify, Proton VPN, MEGA, Dropbox, FreeFileSync, FreeTube, Zen, ONLYOFFICE, aMule, Tixati, Windscribe*, Kando, Helium — and more (100+)
  - Windows Engine (Dec, via HW virtualization): Word, Excel, PowerPoint, Outlook*, Teams, Photoshop, Acrobat Reader, thinkorswim*, StarMoney Deluxe 15, Directory Opus, VS 2026, MSVC Build Tools, Power BI, Remote Desktop Connection, Radmin VPN, StableBit DrivePool, WO Mic, AutoCAD 2027, paint.net

## 3. What QUADRA-OS must have (options matrix)

A living checklist. "Must have" = baseline goal for the OS; "Should have" = high-value; "Nice to have" = later. Evidence/notes in parentheses.

### Desktop & shell
- [ ] **Liquid-glass desktop** — frosted acrylic panels over a wallpaper (reference video + FlowOS site)
- [ ] **Top bar** — clock, date, network, battery/status (reference video)
- [ ] **Dock** — pinned apps, running-app indicators (reference video)
- [ ] **Start menu** — search + pinned apps + recent files (site, video)
- [ ] **Launchpad** — all installed apps, with search (site)
- [ ] **Lock screen** — time + date over wallpaper (site)
- [ ] **Light mode** — whole system, one click (site)
- [ ] **Dynamic island** — notification pill (video)
- [ ] **Desktop watermark** — branded corner badge (video)
- [ ] **Desktop switching / virtual desktops** — via Control Center (site)
- [ ] **Task Manager** — processes, performance, startup apps; Ctrl+Shift+Esc (site, video)
- [ ] **Settings** — System · Network · Personalization · Accounts · Updates (site)
- [ ] **Files** — Home · Documents · Downloads · Pictures (site)
- [ ] **Window controls** — draggable/resizable windows with the reference window chrome (video)
- [ ] **Dark mode** — one-click toggle (Control Center)
- [ ] **Themes/customization** (roadmap Feb 2027)

### App worlds & apps
- [ ] **Linux app world** — native apps (.deb · .rpm · Arch · Flathub/Flatpak-style) — main world, must work on day one
- [ ] **Windows app world** — Wine/Proton-style compatibility (.exe · .msi), onboarding for users coming from Windows; Steam + games through Proton (site / apps page)
- [ ] **Android app world** — Waydroid-style (.apk · Play Store), each app in its own window (site)
- [ ] **macOS app world** — Darling-style (.app · .dmg), preview + OFF by default (site)
- [ ] **Velotic Store → QUADRA Store** — one store, popular apps, one-click install (site, video)
- [ ] **Install/upgrade** — updates every day, never forced (site)
- [ ] **Drivers app** — NVIDIA own driver (GTX 16 / RTX 20+), AMD extras (site)
- [ ] **Performance page** — RAM/CPU/disk view (video)
- [ ] **Game support** — Steam + Proton games; kernel anti-cheat in phase 2 (site)

### Performance & hardware fit
- [ ] **128 MB tiny mode** — boots in 128 MB on older BIOS PCs (site, video)
- [ ] **Light desktop** — 256 MB minimum (site)
- [ ] **Full desktop** — 2 GB recommended; ~435–460 MB idle usage (site)
- [ ] **No TPM 2.0 required** (site, video)
- [ ] **No Secure Boot required** (site, video)
- [ ] **No SSE4.2 / POPCNT required for the desktop** (site)
- [ ] **x86-64 baseline** (site)
- [ ] **≥ 20 GB disk; 64 GB suggested** (site)
- [ ] **Installs next to Windows, keeps files** (site, video)
- [ ] **4-step installer** — welcome → drive choice → copy → done (site)
- [ ] **16-day "early access" gate** — user-visible status (site)

### Privacy & trust
- [ ] **No telemetry** — only update checks + daily license-style check (site)
- [ ] **No accounts/login wall** (advisory)
- [ ] **License / activation check** like FlowOS daily license check (site — privacy policy)

### Components & licenses
- [ ] **Linux kernel (upstream)** — the reference video/sites show Arch Linux-derived desktops; QUADRA-OS should boot a real kernel
- [ ] **Open-source base** — Linux kernel + open-source stack (Wine/Proton, Waydroid, Darling, KWin-style window manager) — FlowOS model
- [ ] **Its own work:** desktop, apps, installer, and the integration that makes them one system

## 4. Reference hardware & requirements (from the site)

| Concern | Requirement |
|---|---|
| Processor | 64-bit Intel/AMD (x86-64). Desktop needs no SSE4.2/POPCNT |
| Memory (full) | 2 GB recommended; ~435–460 MB used idle |
| Memory (light) | runs from 256 MB; ~230–250 MB used |
| Memory (tiny) | 128 MB on older BIOS PCs (not UEFI) |
| Disk | ≥ 20 GB; installer suggests 64 GB |
| Install | next to Windows; Secure Boot OFF; internet for installer + app-world enablement |
| Windows Engine | HW virtualization + 40 GB free disk + 8 GB RAM + own Win11 Pro license |
| Updates | daily check, never forced |
| License | $20 one-time ($25 post-Nov-30); one license per PC; no subscription |

## 5. What the reference does NOT do (and QUADRA-OS should not copy)

- Telemetry-laden behavior — reference explicitly avoids it
- Forcing updates — reference never forces
- Login walls / accounts at first boot — avoid
- Phone/tablet-only focus — reference is for 64-bit PCs (tablets supported as 64-bit Intel/AMD tablets)
- Subscription model — reference is a one-time license; QUADRA-OS should keep that simple model

## 6. Reference app install model (what running an app must do)

1. Pick an app from an installed list (pinned, recent, or via launchpad search)
2. Double-click → opens in its own window (no fake emulator full-screen)
3. Taskbar/dock shows running-app indicators
4. Closing the window leaves the app available to re-launch (normal desktop semantics)

## 7. QUADRA-OS build status (2026-11-22)

The reference video/site promise a full GUI desktop. What has been built
so far (in this sandbox — no QEMU host available, but the desktop shell
is verified via headless PNG snapshots):

### Built
- [x] **Linux 7.3.0** x86_64 kernel (`bzImage`, `CONFIG_MODULES=n`) —
  `dist/bzImage` (~14 MB). Full compile + objtool pass + link succeeded;
  **framebuffer enabled** (`CONFIG_FB=y`, `CONFIG_FB_VESA=y`) for the
  native desktop.
- [x] **quadra-desktop** — static C framebuffer desktop shell (~1 MB),
  drawn with the Linux kernel's 8x8 bitmap font (extracted from
  `lib/fonts/font_8x8.c` — GPL-2.0). Wallpaper (gradient + aurora blobs
  + subtle grid), top bar (QUADRA-OS logo, live clock, battery, date),
  bottom dock (6 apps), window manager (open/focus/close/Tab cycles),
  and real interactive apps:
  - **Terminal** — live shell (fork/exec + pipe-capture + scrollback)
  - **Files** — keyboard-navigable listing with breadcrumb + status bar
  - **Task Manager** — live memory/load/CPU from /proc with bars
  - **Settings** — grid toggle, brightness, interface scale
  - **About** — brand + build info
  Headless mode (`--png OUT [STATE]`) renders static 800×480 snapshots;
  verified to produce correct PNG output for all six states.
- [x] **BusyBox 1.36.1 initramfs** (~1.6 MB compressed) — `/init` script
  (execs `/bin/quadra-desktop`, falls back to `/bin/sh -l`), ~90 applet
  symlinks, QUADRA-OS branding, static `quadra-desktop` — `dist/initramfs.cpio.gz`.
- [x] **Boot reference** — `dist/boot.sh` (QEMU/GRUB/EFI paths) +
  `README.md`.

### Still missing (needs QEMU / larger toolchain)
- [ ] **Live framebuffer boot** — the fb0 code path (mmap + bitfield
  blit + evdev keyboard) is written but cannot be exercised in this
  sandbox (no display, no QEMU). Must be verified in a real boot.
- [ ] **App worlds** — Windows (Wine/Proton), Android (Waydroid),
  macOS (Darling) — still design targets; need a much larger toolchain.
- [ ] **Persistent rootfs** — boots via initramfs; a real rootfs can be
  layered on later.
- [ ] **Installer image** — not produced yet.
- [ ] **Start menu search filtering** — currently accepts typing but
  does not filter the grid (keyboard-only).
- [ ] **Lock screen, Control Center, Launchpad 3D-style grid, Dynamic
  island, desktop watermark** — design targets from the reference video.

### Known build limitations
- Signature enforcement is stubbed (`certs/system_certificates.o` is an
  empty stub, no openssl in this environment). Disable or re-generate
  real certs for production.
- The desktop shell is written for /dev/fb0 + evdev; it is not
  exercised against a live framebuffer in this environment.
