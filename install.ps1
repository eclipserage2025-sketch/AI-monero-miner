#=============================================================================
# AI Monero Miner — Auto Install Script (Windows PowerShell)
# Usage:  Set-ExecutionPolicy Bypass -Scope Process; .\install.ps1 [-FromSource]
#=============================================================================
#Requires -Version 5.1

param(
    [string]$BuildType = "Release",
    [switch]$SkipDeps,
    [switch]$FromSource
)

$ErrorActionPreference = "Stop"

# ── Colors & Helpers ───────────────────────────────────────────────────────
function Write-Banner {
    Write-Host ""
    Write-Host "  ╔══════════════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "  ║       AI Monero Miner — Auto Installer v0.3.0   ║" -ForegroundColor Cyan
    Write-Host "  ╚══════════════════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step($msg)  { Write-Host "`n▸ $msg" -ForegroundColor Cyan }
function Write-Info($msg)  { Write-Host "[✔] $msg" -ForegroundColor Green }
function Write-Warn($msg)  { Write-Host "[!] $msg" -ForegroundColor Yellow }
function Write-Err($msg)   { Write-Host "[✘] $msg" -ForegroundColor Red }

function Test-Command($cmd) {
    return [bool](Get-Command $cmd -ErrorAction SilentlyContinue)
}

function Get-CpuCount {
    return [Environment]::ProcessorCount
}

$script:UsePrecompiled = $false

# ── Check Prerequisites ───────────────────────────────────────────────────
function Test-Prerequisites {
    Write-Step "Checking prerequisites..."
    $missing = @()

    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $hasVS = $false
    if (Test-Path $vsWhere) {
        $vsInstalls = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsInstalls) { $hasVS = $true }
    }
    if (-not $hasVS -and -not (Test-Command "cl")) {
        $missing += "Visual Studio 2022 Build Tools (with C++ workload)"
    }

    if (-not (Test-Command "cmake")) { $missing += "CMake" }
    if (-not (Test-Command "git"))   { $missing += "Git" }

    if ($missing.Count -gt 0) {
        Write-Err "Missing required tools:"
        foreach ($m in $missing) { Write-Host "   • $m" -ForegroundColor Red }
        Write-Host ""
        Write-Host "Install options:" -ForegroundColor Yellow
        Write-Host "  1. Install Visual Studio 2022 Community (free) with 'Desktop development with C++' workload"
        Write-Host "     https://visualstudio.microsoft.com/downloads/"
        Write-Host "  2. Or install Build Tools for Visual Studio 2022:"
        Write-Host "     https://visualstudio.microsoft.com/visual-cpp-build-tools/"
        Write-Host ""

        if (-not (Test-Command "cmake")) {
            Write-Host "  CMake: https://cmake.org/download/" -ForegroundColor Yellow
            Write-Host "    Or: winget install Kitware.CMake" -ForegroundColor Yellow
        }
        if (-not (Test-Command "git")) {
            Write-Host "  Git:   https://git-scm.com/download/win" -ForegroundColor Yellow
            Write-Host "    Or: winget install Git.Git" -ForegroundColor Yellow
        }

        Write-Host ""
        $install = Read-Host "Try to install missing tools via winget? (y/N)"
        if ($install -eq "y" -or $install -eq "Y") {
            Install-WithWinget $missing
        } else {
            Write-Err "Please install missing tools and re-run the script."
            exit 1
        }
    }

    Write-Info "All prerequisites found."
}

function Install-WithWinget($missing) {
    if (-not (Test-Command "winget")) {
        Write-Err "winget not found. Please install tools manually."
        exit 1
    }
    foreach ($m in $missing) {
        switch -Wildcard ($m) {
            "*Visual Studio*" {
                Write-Warn "Installing Visual Studio Build Tools..."
                winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
            }
            "*CMake*" {
                Write-Warn "Installing CMake..."
                winget install --id Kitware.CMake --accept-package-agreements --accept-source-agreements
            }
            "*Git*" {
                Write-Warn "Installing Git..."
                winget install --id Git.Git --accept-package-agreements --accept-source-agreements
            }
        }
    }
    Write-Info "Installations triggered. You may need to restart your terminal and re-run this script."
}

# ── Install vcpkg & Dependencies ──────────────────────────────────────────
function Install-Dependencies {
    if ($SkipDeps) {
        Write-Warn "Skipping dependency installation (-SkipDeps)."
        return
    }

    Write-Step "Installing C++ dependencies via vcpkg..."

    $vcpkgDir = Join-Path $PSScriptRoot "deps\vcpkg"

    if (-not (Test-Path $vcpkgDir)) {
        git clone https://github.com/microsoft/vcpkg.git $vcpkgDir
    }

    $bootstrapScript = Join-Path $vcpkgDir "bootstrap-vcpkg.bat"
    if (-not (Test-Path (Join-Path $vcpkgDir "vcpkg.exe"))) {
        Write-Info "Bootstrapping vcpkg..."
        & $bootstrapScript -disableMetrics
    }

    $vcpkg = Join-Path $vcpkgDir "vcpkg.exe"

    $triplet = "x64-windows"
    Write-Info "Installing packages for triplet: $triplet"

    $packages = @(
        "nlohmann-json:$triplet",
        "openssl:$triplet"
    )

    foreach ($pkg in $packages) {
        Write-Info "Installing $pkg..."
        & $vcpkg install $pkg
    }

    Write-Info "Dependencies installed."
    return $vcpkgDir
}

# ── Setup RandomX ─────────────────────────────────────────────────────────
function Setup-RandomX {
    $precompiledDir = Join-Path $PSScriptRoot "third_party\randomx"
    $platform = "windows-x64"
    $libName = "randomx.lib"
    $libPath = Join-Path $precompiledDir "lib\$platform\$libName"
    $headerPath = Join-Path $precompiledDir "include\randomx.h"

    if ($FromSource) {
        Write-Step "Building RandomX from source (-FromSource)..."
        Build-RandomXFromSource
        return
    }

    # Check if precompiled binary already exists
    if ((Test-Path $libPath) -and (Test-Path $headerPath)) {
        Write-Step "Using precompiled RandomX..."
        Write-Info "Found precompiled RandomX at $libPath"
        $script:UsePrecompiled = $true
        return
    }

    # Try downloading precompiled binary
    Write-Step "Downloading precompiled RandomX..."
    $downloadScript = Join-Path $PSScriptRoot "scripts\download-randomx.ps1"
    if (Test-Path $downloadScript) {
        try {
            & $downloadScript
            if ((Test-Path $libPath) -and (Test-Path $headerPath)) {
                Write-Info "Precompiled RandomX downloaded successfully."
                $script:UsePrecompiled = $true
                return
            }
        } catch {
            Write-Warn "Download failed: $_"
        }
    }

    # Fallback: build from source
    Write-Warn "Precompiled RandomX not available. Building from source..."
    Build-RandomXFromSource
}

function Build-RandomXFromSource {
    $depsDir = Join-Path $PSScriptRoot "deps"
    $randomxDir = Join-Path $depsDir "RandomX"

    if (-not (Test-Path $depsDir)) { New-Item -ItemType Directory -Path $depsDir | Out-Null }

    if (Test-Path $randomxDir) {
        Write-Warn "RandomX directory exists. Pulling latest..."
        Push-Location $randomxDir
        git pull --ff-only
        Pop-Location
    } else {
        git clone --depth 1 https://github.com/tevador/RandomX.git $randomxDir
    }

    $buildDir = Join-Path $randomxDir "build"
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

    Push-Location $buildDir
    cmake .. -DCMAKE_BUILD_TYPE=$BuildType
    cmake --build . --config $BuildType -j (Get-CpuCount)
    Pop-Location

    # Cache into precompiled directory for future use
    $precompiledDir = Join-Path $PSScriptRoot "third_party\randomx"
    $platform = "windows-x64"
    $libName = "randomx.lib"

    $includeDir = Join-Path $precompiledDir "include"
    $libDir = Join-Path $precompiledDir "lib\$platform"
    New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $libDir -Force | Out-Null

    $builtLib = Join-Path $buildDir "$BuildType\$libName"
    if (-not (Test-Path $builtLib)) { $builtLib = Join-Path $buildDir $libName }
    if (Test-Path $builtLib) { Copy-Item $builtLib -Destination $libDir -Force }
    $headerSrc = Join-Path $randomxDir "src\randomx.h"
    if (Test-Path $headerSrc) { Copy-Item $headerSrc -Destination $includeDir -Force }

    $script:UsePrecompiled = $true
    Write-Info "RandomX built and cached in $precompiledDir"
}

# ── Build the Miner ──────────────────────────────────────────────────────
function Build-Miner {
    param($VcpkgDir)

    Write-Step "Building AI Monero Miner..."

    $buildDir = Join-Path $PSScriptRoot "build"
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

    Push-Location $buildDir

    $cmakeArgs = @(
        "..",
        "-DCMAKE_BUILD_TYPE=$BuildType"
    )

    # Precompiled RandomX
    if ($script:UsePrecompiled) {
        $cmakeArgs += "-DUSE_PRECOMPILED_RANDOMX=ON"
        Write-Info "Using precompiled RandomX libraries."
    } else {
        $cmakeArgs += "-DUSE_PRECOMPILED_RANDOMX=OFF"
        Write-Info "RandomX will be built via FetchContent."
    }

    # vcpkg toolchain
    if ($VcpkgDir) {
        $toolchain = Join-Path $VcpkgDir "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $toolchain) {
            $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        }
    }

    cmake @cmakeArgs
    cmake --build . --config $BuildType -j (Get-CpuCount)

    Pop-Location

    Write-Info "Build complete!"
}

# ── Setup Config ──────────────────────────────────────────────────────────
function Setup-Config {
    Write-Step "Setting up configuration..."

    $configFile  = Join-Path $PSScriptRoot "config\miner_config.json"
    $defaultFile = Join-Path $PSScriptRoot "config\default_config.json"

    if (Test-Path $configFile) {
        Write-Warn "Config file already exists — skipping."
    } elseif (Test-Path $defaultFile) {
        Copy-Item $defaultFile $configFile
        Write-Info "Config created at $configFile"
        Write-Warn "Edit this file to set your wallet address and pool before mining!"
    } else {
        Write-Warn "No default config found. Create config\miner_config.json manually."
    }
}

# ── Summary ───────────────────────────────────────────────────────────────
function Write-Summary {
    Write-Host ""
    Write-Host "══════════════════════════════════════════════════════" -ForegroundColor Cyan
    Write-Host "  ✅ Installation Complete!" -ForegroundColor Green
    Write-Host "══════════════════════════════════════════════════════" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  Binary:  .\build\$BuildType\ai-monero-miner.exe" -ForegroundColor White
    Write-Host "  Config:  .\config\miner_config.json" -ForegroundColor White
    Write-Host ""
    Write-Host "  Before mining, edit the config file:" -ForegroundColor Yellow
    Write-Host "    • Set your wallet_address"
    Write-Host "    • Set your pool_url and pool_port"
    Write-Host ""
    Write-Host "  Run:" -ForegroundColor White
    Write-Host "    .\build\$BuildType\ai-monero-miner.exe"
    Write-Host "    .\build\$BuildType\ai-monero-miner.exe --config C:\path\to\config.json"
    Write-Host ""
    if ($script:UsePrecompiled) {
        Write-Host "  ⚡ RandomX: precompiled (fast install)" -ForegroundColor Green
    } else {
        Write-Host "  🔧 RandomX: built via FetchContent" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "══════════════════════════════════════════════════════" -ForegroundColor Cyan
}

# ── Main ──────────────────────────────────────────────────────────────────
function Main {
    Write-Banner
    Test-Prerequisites
    $vcpkgDir = Install-Dependencies
    Setup-RandomX
    Build-Miner -VcpkgDir $vcpkgDir
    Setup-Config
    Write-Summary
}

Main
