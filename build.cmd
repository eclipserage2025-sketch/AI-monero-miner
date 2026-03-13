@echo off
setlocal enabledelayedexpansion

:: ============================================================================
::  AI Monero Miner — Windows Build Script (CMD)
::  Usage:
::    build.cmd                    Build Release (precompiled RandomX)
::    build.cmd debug              Build Debug
::    build.cmd release            Build Release (explicit)
::    build.cmd clean              Remove build directory
::    build.cmd rebuild            Clean + Build Release
::    build.cmd --from-source      Force build RandomX from source
::    build.cmd --no-hwloc         Disable hwloc support
::    build.cmd --generator ninja  Use Ninja instead of default
:: ============================================================================

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "BUILD_TYPE=Release"
set "GENERATOR="
set "USE_PRECOMPILED=ON"
set "USE_HWLOC=ON"
set "DO_CLEAN=0"
set "JOBS=%NUMBER_OF_PROCESSORS%"

:: ── Parse Arguments ─────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="debug"          set "BUILD_TYPE=Debug"        & shift & goto :parse_args
if /i "%~1"=="release"        set "BUILD_TYPE=Release"      & shift & goto :parse_args
if /i "%~1"=="relwithdebinfo" set "BUILD_TYPE=RelWithDebInfo"& shift & goto :parse_args
if /i "%~1"=="clean"          set "DO_CLEAN=1"              & shift & goto :parse_args
if /i "%~1"=="rebuild"        set "DO_CLEAN=1"              & shift & goto :parse_args
if /i "%~1"=="--from-source"  set "USE_PRECOMPILED=OFF"     & shift & goto :parse_args
if /i "%~1"=="--no-hwloc"     set "USE_HWLOC=OFF"           & shift & goto :parse_args
if /i "%~1"=="--generator" (
    set "GENERATOR=%~2"
    shift & shift & goto :parse_args
)
if /i "%~1"=="--jobs" (
    set "JOBS=%~2"
    shift & shift & goto :parse_args
)
echo [WARNING] Unknown argument: %~1
shift
goto :parse_args
:done_args

:: ── Title ───────────────────────────────────────────────────────────────────
echo.
echo  ======================================================
echo    AI Monero Miner — Windows CMake Build
echo  ======================================================
echo    Build Type       : %BUILD_TYPE%
echo    Precompiled RX   : %USE_PRECOMPILED%
echo    hwloc            : %USE_HWLOC%
echo    Parallel Jobs    : %JOBS%
if defined GENERATOR echo    Generator        : %GENERATOR%
echo  ======================================================
echo.

:: ── Clean ───────────────────────────────────────────────────────────────────
if %DO_CLEAN%==1 (
    echo [1/4] Cleaning build directory...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
        echo       Removed %BUILD_DIR%
    ) else (
        echo       Nothing to clean.
    )
    echo.
)

:: ── Check Prerequisites ─────────────────────────────────────────────────────
echo [2/4] Checking prerequisites...

where cmake >nul 2>&1
if errorlevel 1 (
    echo.
    echo  [ERROR] CMake not found!
    echo  Install CMake from https://cmake.org/download/
    echo  Or run: winget install Kitware.CMake
    echo.
    exit /b 1
)
for /f "tokens=3" %%v in ('cmake --version 2^>nul ^| findstr /i "version"') do (
    echo       CMake %%v found.
)

:: Check for Visual Studio / MSVC
set "VS_FOUND=0"
where cl >nul 2>&1
if not errorlevel 1 (
    set "VS_FOUND=1"
    echo       MSVC compiler (cl.exe) found.
)

if %VS_FOUND%==0 (
    :: Try to find and activate VS environment
    for %%y in (2022 2019) do (
        for %%e in (Enterprise Professional Community BuildTools) do (
            set "VSDIR=C:\Program Files\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build"
            if exist "!VSDIR!\vcvarsall.bat" (
                echo       Activating Visual Studio %%y %%e environment...
                call "!VSDIR!\vcvarsall.bat" x64 >nul 2>&1
                set "VS_FOUND=1"
                goto :vs_done
            )
            set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build"
            if exist "!VSDIR!\vcvarsall.bat" (
                echo       Activating Visual Studio %%y %%e environment...
                call "!VSDIR!\vcvarsall.bat" x64 >nul 2>&1
                set "VS_FOUND=1"
                goto :vs_done
            )
        )
    )
)
:vs_done

if %VS_FOUND%==0 (
    :: Check for MinGW as fallback
    where g++ >nul 2>&1
    if not errorlevel 1 (
        echo       MinGW g++ found (will use MinGW Makefiles).
        if not defined GENERATOR set "GENERATOR=MinGW Makefiles"
    ) else (
        echo.
        echo  [ERROR] No C++ compiler found!
        echo  Install one of:
        echo    - Visual Studio Build Tools: winget install Microsoft.VisualStudio.2022.BuildTools
        echo    - MinGW-w64: winget install -e --id MSYS2.MSYS2
        echo.
        exit /b 1
    )
)

:: Check for Git (needed for FetchContent)
where git >nul 2>&1
if errorlevel 1 (
    echo       [WARNING] Git not found — FetchContent may fail.
    echo       Install Git: winget install Git.Git
) else (
    echo       Git found.
)

echo.

:: ── Download Precompiled RandomX (if needed) ────────────────────────────────
if /i "%USE_PRECOMPILED%"=="ON" (
    set "RX_LIB=%ROOT%third_party\randomx\lib\windows-x64\randomx.lib"
    if not exist "!RX_LIB!" (
        echo [*] Precompiled RandomX not found locally. Attempting download...
        if exist "%ROOT%scripts\download-randomx.ps1" (
            powershell -ExecutionPolicy Bypass -File "%ROOT%scripts\download-randomx.ps1"
            if errorlevel 1 (
                echo       Download failed — will build from source.
                set "USE_PRECOMPILED=OFF"
            )
        ) else (
            echo       Download script not found — will build from source.
            set "USE_PRECOMPILED=OFF"
        )
        echo.
    ) else (
        echo [*] Precompiled RandomX found at !RX_LIB!
        echo.
    )
)

:: ── Configure (CMake) ───────────────────────────────────────────────────────
echo [3/4] Configuring with CMake...

set "CMAKE_ARGS=-S "%ROOT%" -B "%BUILD_DIR%""
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DUSE_PRECOMPILED_RANDOMX=%USE_PRECOMPILED%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DUSE_HWLOC=%USE_HWLOC%"

if defined GENERATOR (
    set "CMAKE_ARGS=%CMAKE_ARGS% -G "%GENERATOR%""
)

cmake %CMAKE_ARGS%
if errorlevel 1 (
    echo.
    echo  [ERROR] CMake configuration failed!
    echo  Check the output above for details.
    exit /b 1
)
echo       Configuration successful.
echo.

:: ── Build ───────────────────────────────────────────────────────────────────
echo [4/4] Building (%BUILD_TYPE%, %JOBS% jobs)...

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %JOBS%
if errorlevel 1 (
    echo.
    echo  [ERROR] Build failed!
    echo  Check the output above for details.
    exit /b 1
)
echo.

:: ── Copy Config ─────────────────────────────────────────────────────────────
if not exist "%ROOT%config\miner_config.json" (
    if exist "%ROOT%config\default_config.json" (
        copy "%ROOT%config\default_config.json" "%ROOT%config\miner_config.json" >nul
        echo [+] Copied default config to config\miner_config.json
    )
)

:: ── Find Output Binary ──────────────────────────────────────────────────────
set "EXE_PATH="
if exist "%BUILD_DIR%\%BUILD_TYPE%\ai-monero-miner.exe" (
    set "EXE_PATH=%BUILD_DIR%\%BUILD_TYPE%\ai-monero-miner.exe"
) else if exist "%BUILD_DIR%\ai-monero-miner.exe" (
    set "EXE_PATH=%BUILD_DIR%\ai-monero-miner.exe"
)

:: ── Done ────────────────────────────────────────────────────────────────────
echo.
echo  ======================================================
echo    BUILD SUCCESSFUL!
echo  ======================================================
if defined EXE_PATH (
    echo    Binary: %EXE_PATH%
) else (
    echo    Binary: check %BUILD_DIR%\
)
echo    Config: %ROOT%config\miner_config.json
echo.
echo    Run:
if defined EXE_PATH (
    echo      %EXE_PATH%
) else (
    echo      build\ai-monero-miner.exe
)
echo.
echo    Edit config\miner_config.json to set your pool
echo    and wallet address before mining.
echo  ======================================================
echo.

endlocal
exit /b 0
