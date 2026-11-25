@echo off
REM All Credits go to : M-Eleven — Mohamed El-Sayed Abdelfatah
REM YouTube: https://www.youtube.com/@M11-Dev    GitHub: https://github.com/M11-Developer
REM Mail: ma7292537@gmail.com                    TikTok: https://www.tiktok.com/@m11_dev
REM WhatsApp: @m11dev    ·    QUADRA-OS is MIT licensed — see LICENSE / CREDITS.md

REM =============================================================================
REM  QUADRA-OS installer -- Windows
REM
REM  Double-click to install QUADRA-OS on this machine.
REM  Installs:
REM    - bzImage              (Linux kernel 7.3.0, x86_64)
REM    - initramfs.cpio.gz    (busybox + QUADRA-OS framebuffer desktop)
REM    - quadra-desktop      (Linux ELF framebuffer desktop shell)
REM    - brand assets + boot configuration + README
REM
REM  QUADRA-OS is a bootable Linux OS, not a native Windows application.
REM  See README.txt in the install directory for the exact boot steps.
REM =============================================================================

setlocal

REM Change to the directory this file lives in.
cd /d "%~dp0"

REM Prefer the Windows PowerShell 5+ launcher bundled with Windows 10/11.
where powershell.exe >nul 2>nul
if errorlevel 1 (
    REM Fall back to pwsh (PowerShell 7+).
    where pwsh.exe >nul 2>nul
    if errorlevel 1 (
        echo [X] PowerShell was not found. Install Windows PowerShell 5+ or PowerShell 7+.
        pause
        exit /b 1
    )
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-QUADRA-OS.ps1" %*
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-QUADRA-OS.ps1" %*
)

set ERR=%ERRORLEVEL%
echo.
if not "%ERR%"=="0" (
    echo [X] Installer exited with error code %ERR%.
) else (
    echo [OK] QUADRA-OS installer finished.
)
echo.
pause
exit /b %ERR%
