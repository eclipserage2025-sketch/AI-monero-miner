@echo off
REM ==========================================================================
REM AI Monero Miner — Quick Install Launcher for Windows
REM Double-click this file or run from Command Prompt
REM ==========================================================================

echo.
echo   ╔══════════════════════════════════════════════════╗
echo   ║       AI Monero Miner — Windows Installer        ║
echo   ╚══════════════════════════════════════════════════╝
echo.

REM Check for PowerShell
where powershell >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [!] PowerShell is required but not found.
    echo     Please install PowerShell or run install.ps1 manually.
    pause
    exit /b 1
)

echo [*] Launching PowerShell installer...
echo.

powershell -ExecutionPolicy Bypass -File "%~dp0install.ps1"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [!] Installation encountered errors. Check the output above.
) else (
    echo.
    echo [✔] Installation complete!
)

pause
