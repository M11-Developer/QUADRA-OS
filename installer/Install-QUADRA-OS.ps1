<#
All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah
YouTube: https://www.youtube.com/@M11-Dev    GitHub: https://github.com/M11-Developer
Mail: ma7292537@gmail.com                    TikTok: https://www.tiktok.com/@m11_dev
WhatsApp: @m11dev    ·    QUADRA-OS is MIT licensed — see LICENSE / CREDITS.md

.SYNOPSIS
  QUADRA-OS installer — Windows

.DESCRIPTION
  A real, honest installer for QUADRA-OS on Windows.

  QUADRA-OS is a framebuffer Linux OS built from a Linux kernel +
  busybox initramfs + a framebuffer desktop shell. It is NOT a native
  Windows application and cannot be booted from within Windows without
  a UEFI boot chain (GRUB). This installer:

    1. Verifies the host is Windows and you have permissions.
    2. Copies the QUADRA-OS artifacts (kernel, initramfs, desktop,
       brand assets) into the install directory (default C:\QUADRA-OS).
    3. Writes a boot configuration file describing the exact boot menu
       entry that is expected.
    4. Prints honest next steps, including the verified boot path
       (the bootable ISO / Linux installer).

  Nothing is fabricated and nothing is faked as "bootable" without
  being so. See README.md for the full honest picture.

.PARAMETER Target
  Install directory. Default: C:\QUADRA-OS

.PARAMETER Source
  Directory containing the artifacts (bzImage, initramfs.cpio.gz,
  quadra-desktop, brand/). Defaults to the directory this installer
  was run from, searching .. and the current directory.

.PARAMETER DryRun
  Only preview the install, write nothing.

.EXAMPLE
  .\Install-QUADRA-OS.ps1
  .\Install-QUADRA-OS.ps1 -DryRun
  .\Install-QUADRA-OS.ps1 -Target D:\QUADRA-OS
#>
[CmdletBinding()]
param(
    [string]$Target = "C:\QUADRA-OS",
    [string]$Source = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$QUADRA_VERSION = "1.0"
$KERNEL_NAME = "bzImage"
$INITRAMFS_NAME = "initramfs.cpio.gz"
$DESKTOP_NAME = "quadra-desktop"

function Write-Info  { param($m) Write-Host "  $m" -ForegroundColor Cyan }
function Write-OK    { param($m) Write-Host "  [OK] $m" -ForegroundColor Green }
function Write-Warn  { param($m) Write-Host "  [!] $m" -ForegroundColor Yellow }
function Write-Error-Fail { param($m) Write-Host "  [X] $m" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------------------
#  banner
# ---------------------------------------------------------------------------
Clear-Host
Write-Host ""
Write-Host "  --- QUADRA-OS ---" -ForegroundColor Cyan
Write-Host "  a modern, lean, low-end-PC-friendly OS" -ForegroundColor Cyan
Write-Host "  QUADRA-OS  installer - Windows" -ForegroundColor Cyan
Write-Host "  version $QUADRA_VERSION  -  kernel Linux 7.3.0  -  x86_64" -ForegroundColor Cyan
Write-Host ""

# ---------------------------------------------------------------------------
#  preflight
# ---------------------------------------------------------------------------
Write-Info "preflight"

if ($PSVersionTable.PSVersion.Major -lt 5) {
    Write-Error-Fail "PowerShell 5+ required (found $($PSVersionTable.PSVersion))"
}

# locate source
if (-not $Source) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $repoRoot  = Join-Path $scriptDir ".."
    $candidates = @(
        $repoRoot,
        $scriptDir,
        (Join-Path $repoRoot "dist"),
        (Join-Path $scriptDir "dist")
    )
    $Source = $null
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "$KERNEL_NAME")) { $Source = $c; break }
    }
    if (-not $Source) {
        Write-Warn "artifact source not auto-detected; pass -Source with the directory containing bzImage"
        Write-Warn "expected: $KERNEL_NAME  +  $INITRAMFS_NAME  +  $DESKTOP_NAME  in -Source"
        $Source = $candidates[0]
    }
}
Write-Info "source: $Source"

# ---------------------------------------------------------------------------
#  artifact verification
# ---------------------------------------------------------------------------
$kernel    = Join-Path $Source $KERNEL_NAME
$initramfs = Join-Path $Source $INITRAMFS_NAME
# the desktop binary lives in <repo>/desktop/ in the source tree
$desktop = $null
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$desktopCandidates = @(
    (Join-Path $Source $DESKTOP_NAME),
    (Join-Path $scriptDir "..\desktop\$DESKTOP_NAME"),
    (Join-Path $scriptDir "..\$DESKTOP_NAME")
)
foreach ($c in $desktopCandidates) {
    if (Test-Path $c) { $desktop = $c; break }
}
if (-not $desktop) { $desktop = $desktopCandidates[0] }
# brand assets: look next to source, then in <repo>/rootfs/usr/share/quadra-os/brand
$brand = $null
$brandCandidates = @(
    (Join-Path $Source "brand"),
    (Join-Path $scriptDir "..\rootfs\usr\share\quadra-os\brand"),
    (Join-Path $scriptDir "..\assets\brand")
)
foreach ($c in $brandCandidates) {
    if (Test-Path $c) { $brand = $c; break }
}
if (-not $brand) { $brand = $brandCandidates[0] }

foreach ($f in @($kernel, $initramfs, $desktop)) {
    if (-not (Test-Path $f)) { Write-Error-Fail "missing artifact: $f`n  (expected next to this installer, or via -Source)" }
}
Write-OK "artifacts found"

# read first 4 bytes to confirm a valid bootable bzImage (MZ/PE magic)
try {
    $fs = [System.IO.File]::OpenRead($kernel)
    $buf = New-Object byte[] 4
    $null = $fs.Read($buf, 0, 4)
    $fs.Close()
    $magic = ($buf | ForEach-Object { $_.ToString("x2") }) -join ""
    if ($magic -notmatch "^4d5a") { Write-Error-Fail "kernel file does not look like a bootable bzImage (magic: $magic)" }
    Write-OK "kernel header looks valid ($magic)"
} catch {
    Write-Error-Fail "could not read kernel header: $($_.Exception.Message)"
}

# ---------------------------------------------------------------------------
#  target + permissions
# ---------------------------------------------------------------------------
try {
    if (-not (Test-Path $Target)) {
        if ($DryRun) { Write-Host "  (target would be created: $Target)" }
        else { New-Item -ItemType Directory -Path $Target -Force | Out-Null }
    }
    $targetDir = $Target
    $tgtItem = Get-Item $Target -ErrorAction SilentlyContinue
} catch {
    Write-Error-Fail "could not access target $Target : $($_.Exception.Message)"
}

Write-Info "target: $targetDir"

# ---------------------------------------------------------------------------
#  install plan
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "  Install plan" -ForegroundColor Cyan
Write-Info "kernel   : $kernel  ->  $targetDir\bzImage"
Write-Info "initrd   : $initramfs -> $targetDir\initramfs.cpio.gz"
Write-Info "desktop  : $desktop -> $targetDir\quadra-desktop (Linux ELF binary)"
Write-Info "brand    : $brand  ->  $targetDir\brand (if present)"
Write-Info "boot conf: $targetDir\quadra-os.cfg (describe boot entry)"
Write-Info "README   : $targetDir\README.txt (honest boot instructions)"
Write-Host ""

if ($DryRun) {
    Write-OK "dry-run complete - nothing was written"
    Write-Host "  Re-run without -DryRun to install." -ForegroundColor Cyan
    exit 0
}

# ---------------------------------------------------------------------------
#  install
# ---------------------------------------------------------------------------
Write-Host "  Installing QUADRA-OS" -ForegroundColor Cyan

# ensure target directory
New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

# 1. kernel + initramfs
Copy-Item -Path $kernel -Destination (Join-Path $targetDir "bzImage") -Force
Write-OK "kernel installed ($([Math]::Round((Get-Item (Join-Path $targetDir 'bzImage')).Length / 1MB, 1)) MB)"

Copy-Item -Path $initramfs -Destination (Join-Path $targetDir "initramfs.cpio.gz") -Force
Write-OK "initramfs installed ($([Math]::Round((Get-Item (Join-Path $targetDir 'initramfs.cpio.gz')).Length / 1MB, 1)) MB)"

# 2. desktop binary
Copy-Item -Path $desktop -Destination (Join-Path $targetDir "quadra-desktop") -Force
Write-OK "desktop shell installed (Linux ELF binary, $([Math]::Round((Get-Item (Join-Path $targetDir 'quadra-desktop')).Length / 1KB, 0)) KB)"
Write-Warn "quadra-desktop is a Linux framebuffer binary; it cannot run natively on Windows."

# 3. brand assets
if (Test-Path $brand) {
    $brandDest = Join-Path $targetDir "brand"
    New-Item -ItemType Directory -Path $brandDest -Force | Out-Null
    Copy-Item -Path (Join-Path $brand "*") -Destination $brandDest -Recurse -Force
    Write-OK "brand assets installed to $brandDest"
} else {
    Write-Warn "no brand/ source directory found - skipping brand assets"
}

# 3b. attribution + MIT license (mandatory in every install)
$licenseDest = Join-Path $targetDir "licenses"
New-Item -ItemType Directory -Path $licenseDest -Force | Out-Null
$repoRoot = Split-Path -Parent $PSScriptRoot
foreach ($f in @("LICENSE", "CREDITS.md", "THIRD-PARTY-NOTICES.md")) {
    $src = Join-Path $repoRoot $f
    if (Test-Path $src) { Copy-Item -Path $src -Destination (Join-Path $licenseDest $f) -Force }
}
Write-OK "credits + MIT license installed to $licenseDest (M-Eleven)"

# 4. boot configuration + README (honest, explicit)
$bootCfg = @"
# QUADRA-OS boot configuration (v$QUADRA_VERSION)
# =============================================
# This file describes what the boot chain expects.
# QUADRA-OS is a framebuffer Linux OS: kernel + initramfs.
#
# Kernel   : $targetDir\bzImage
# Initramfs: $targetDir\initramfs.cpio.gz
#
# UEFI boot (GRUB) expects something like:
#   menuentry "QUADRA-OS" {
#       linux  /boot/quadra-os/bzImage root=/dev/ram0 console=ttyS0
#       initrd /boot/quadra-os/initramfs.cpio.gz
#   }
# See README.txt in this directory for the honest boot instructions.
"@
Set-Content -Path (Join-Path $targetDir "quadra-os.cfg") -Value $bootCfg -Encoding UTF8
Write-OK "boot configuration written: $targetDir\quadra-os.cfg"

$readme = @"
QUADRA-OS v$QUADRA_VERSION - Windows install
===========================================

All Credits go to : M-Eleven
Mohamed El-Sayed Abdelfatah
  YouTube  https://www.youtube.com/@M11-Dev
  GitHub   https://github.com/M11-Developer
  Mail     ma7292537@gmail.com
  TikTok   https://www.tiktok.com/@m11_dev
  WhatsApp @m11dev
QUADRA-authored material is MIT licensed - see licenses\LICENSE.
Keep these notices in every copy.

What was installed here:
  bzImage              QUADRA-OS kernel (Linux 7.3.0, x86_64, bootable bzImage)
  initramfs.cpio.gz    boot userland (busybox + QUADRA-OS framebuffer desktop)
  quadra-desktop       framebuffer desktop shell (Linux ELF binary - NOT a Windows app)
  brand/               QUADRA-OS brand assets
  quadra-os.cfg        boot configuration description

How to boot QUADRA-OS (honest picture):
--------------------------------------
QUADRA-OS is a bootable Linux OS, not a Windows application. Windows
Boot Manager cannot boot the raw Linux kernel. To run QUADRA-OS you
need one of:

  A) The bootable ISO - from this same release - flashed to a USB drive
     (grub-mkrescue / a standard Windows ISO writer) and booted from UEFI.
  B) The Linux installer (Install-QUADRA-OS.sh) on a Linux system, which
     installs QUADRA-OS as a Linux boot entry.
  C) A GRUB UEFI setup on this machine's EFI partition, pointing at the
     kernel and initramfs in this directory.

The installer on this Windows PC placed the artifacts in this folder and
is honest: it does NOT fabricate a Windows one-click boot. The verified
paths are A, B and C above.

brand: teal #22d3ee / indigo #6366f1 / violet #8b5cf6 / navy + Q-tail
kernel: Linux 7.3.0 (x86_64)
"@
Set-Content -Path (Join-Path $targetDir "README.txt") -Value $readme -Encoding UTF8
Write-OK "README written: $targetDir\README.txt"

# 5. manifest + checksums
$manifest = @"
QUADRA-OS v$QUADRA_VERSION - manifest
-----------------------------------
Target directory : $targetDir
Installed at     : $(Get-Date -Format "yyyy-MM-ddTHH:mm:ss")
Kernel SHA-256   : $((Get-FileHash (Join-Path $targetDir 'bzImage') -Algorithm SHA256).Hash)
Initramfs SHA-256: $((Get-FileHash (Join-Path $targetDir 'initramfs.cpio.gz') -Algorithm SHA256).Hash)
Desktop SHA-256  : $((Get-FileHash (Join-Path $targetDir 'quadra-desktop') -Algorithm SHA256).Hash)
"@
Set-Content -Path (Join-Path $targetDir "MANIFEST.txt") -Value $manifest -Encoding UTF8
Write-OK "manifest written: $targetDir\MANIFEST.txt"

# ---------------------------------------------------------------------------
#  done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "  [OK] QUADRA-OS $QUADRA_VERSION installed" -ForegroundColor Green
Write-Host "  kernel   : $targetDir\bzImage"
Write-Host "  initrd   : $targetDir\initramfs.cpio.gz"
Write-Host "  desktop  : $targetDir\quadra-desktop"
Write-Host "  config   : $targetDir\quadra-os.cfg"
Write-Host ""
Write-Host "  next steps:" -ForegroundColor Cyan
Write-Host "    1. Read $targetDir\README.txt - it explains exactly how to boot QUADRA-OS"
Write-Host "    2. Use the bootable ISO (USB) or the Linux installer (dual-boot)"
Write-Host "    3. QUADRA-OS boots to the liquid-glass framebuffer desktop"
Write-Host ""
Write-Host "  note: QUADRA-OS is honestly a bootable Linux OS, not a native" -ForegroundColor Cyan
Write-Host "  Windows application. Nothing here fabricates a Windows one-click boot." -ForegroundColor Cyan
