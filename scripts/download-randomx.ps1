#=============================================================================
# Download precompiled RandomX binaries for Windows
# Usage: .\scripts\download-randomx.ps1 [-Version TAG]
#=============================================================================
#Requires -Version 5.1

param(
    [string]$Version = "latest"
)

$ErrorActionPreference = "Stop"

$RepoOwner = "eclipserage2025-sketch"
$RepoName  = "AI-monero-miner"
$DestDir   = Join-Path $PSScriptRoot "..\third_party\randomx"
$Platform  = "windows-x64"
$LibName   = "randomx.lib"

function Write-Step($msg)  { Write-Host "`n▸ $msg" -ForegroundColor Cyan }
function Write-Info($msg)  { Write-Host "[✔] $msg" -ForegroundColor Green }
function Write-Warn($msg)  { Write-Host "[!] $msg" -ForegroundColor Yellow }
function Write-Err($msg)   { Write-Host "[✘] $msg" -ForegroundColor Red }

Write-Host ""
Write-Host "  ╔══════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║     RandomX Precompiled Binary Downloader        ║" -ForegroundColor Cyan
Write-Host "  ╚══════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

Write-Info "Platform: $Platform"

# ── Get release URL ────────────────────────────────────────────────────────
Write-Step "Finding precompiled RandomX release..."

if ($Version -eq "latest") {
    $releaseUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/latest"
} else {
    $releaseUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/tags/$Version"
}

$release = $null
try {
    $release = Invoke-RestMethod -Uri $releaseUrl -Headers @{ "User-Agent" = "AI-Monero-Miner" }
} catch {
    Write-Warn "No GitHub release found. Falling back to building from source..."
    Build-FromSource
    exit 0
}

# ── Find asset ─────────────────────────────────────────────────────────────
$assetName = "randomx-${Platform}.zip"
$asset = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1

if (-not $asset) {
    Write-Warn "No precompiled binary found for $Platform. Building from source..."
    Build-FromSource
    exit 0
}

Write-Info "Found: $assetName"

# ── Download and extract ──────────────────────────────────────────────────
Write-Step "Downloading precompiled RandomX..."

$includeDir = Join-Path $DestDir "include"
$libDir     = Join-Path $DestDir "lib\$Platform"

New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
New-Item -ItemType Directory -Path $libDir -Force | Out-Null

$tmpDir = Join-Path $env:TEMP "randomx-download-$(Get-Random)"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    $zipPath = Join-Path $tmpDir $assetName
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -UseBasicParsing

    Write-Step "Extracting..."
    Expand-Archive -Path $zipPath -DestinationPath $tmpDir -Force

    # Copy library
    $libFile = Get-ChildItem -Path $tmpDir -Recurse -Filter $LibName | Select-Object -First 1
    if ($libFile) {
        Copy-Item $libFile.FullName -Destination $libDir -Force
        Write-Info "Library: $libDir\$LibName"
    }

    # Copy headers
    $headerFile = Get-ChildItem -Path $tmpDir -Recurse -Filter "randomx.h" | Select-Object -First 1
    if ($headerFile) {
        Copy-Item $headerFile.FullName -Destination $includeDir -Force
        Write-Info "Header:  $includeDir\randomx.h"
    }
} finally {
    Remove-Item -Path $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}

# ── Fallback: build from source ───────────────────────────────────────────
function Build-FromSource {
    Write-Step "Building RandomX from source..."

    $depsDir = Join-Path $PSScriptRoot "..\deps"
    $randomxDir = Join-Path $depsDir "RandomX"

    if (-not (Test-Path $depsDir)) { New-Item -ItemType Directory -Path $depsDir | Out-Null }

    if (Test-Path $randomxDir) {
        Push-Location $randomxDir
        git pull --ff-only
        Pop-Location
    } else {
        git clone --depth 1 https://github.com/tevador/RandomX.git $randomxDir
    }

    $buildDir = Join-Path $randomxDir "build"
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

    Push-Location $buildDir
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release -j ([Environment]::ProcessorCount)
    Pop-Location

    # Copy to precompiled directory
    $includeDir = Join-Path $DestDir "include"
    $libDir = Join-Path $DestDir "lib\$Platform"
    New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $libDir -Force | Out-Null

    $builtLib = Join-Path $buildDir "Release\randomx.lib"
    if (-not (Test-Path $builtLib)) { $builtLib = Join-Path $buildDir "randomx.lib" }
    Copy-Item $builtLib -Destination $libDir -Force

    Copy-Item (Join-Path $randomxDir "src\randomx.h") -Destination $includeDir -Force

    Write-Info "Built and installed RandomX to $DestDir"
}

# ── Verify ─────────────────────────────────────────────────────────────────
Write-Step "Verifying installation..."
$libPath = Join-Path $libDir $LibName
$headerPath = Join-Path $includeDir "randomx.h"

if ((Test-Path $libPath) -and (Test-Path $headerPath)) {
    $size = (Get-Item $libPath).Length / 1MB
    Write-Info ("RandomX library: {0:N1} MB ({1})" -f $size, $libPath)
    Write-Info "RandomX header:  $headerPath"
    Write-Host ""
    Write-Info "Precompiled RandomX is ready! CMake will auto-detect it."
} else {
    Write-Err "Verification failed. Some files are missing."
    exit 1
}
