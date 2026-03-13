#!/usr/bin/env bash
#=============================================================================
# Download precompiled RandomX binaries for the current platform
# Usage: ./scripts/download-randomx.sh [--version TAG]
#=============================================================================
set -euo pipefail

# ── Config ─────────────────────────────────────────────────────────────────
REPO_OWNER="eclipserage2025-sketch"
REPO_NAME="AI-monero-miner"
RANDOMX_VERSION="${1:-latest}"
DEST_DIR="$(cd "$(dirname "$0")/.." && pwd)/third_party/randomx"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

info()  { echo -e "${GREEN}[✔]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
err()   { echo -e "${RED}[✘]${NC} $*"; }
step()  { echo -e "\n${CYAN}▸ $*${NC}"; }

# ── Detect platform ────────────────────────────────────────────────────────
detect_platform() {
    OS="$(uname -s)"
    ARCH="$(uname -m)"

    case "$OS" in
        Linux*)
            case "$ARCH" in
                x86_64|amd64)   PLATFORM="linux-x64"   ;;
                aarch64|arm64)  PLATFORM="linux-arm64"  ;;
                *)              err "Unsupported arch: $ARCH"; exit 1 ;;
            esac
            LIB_NAME="librandomx.a"
            ;;
        Darwin*)
            case "$ARCH" in
                x86_64|amd64)   PLATFORM="macos-x64"   ;;
                arm64|aarch64)  PLATFORM="macos-arm64"  ;;
                *)              err "Unsupported arch: $ARCH"; exit 1 ;;
            esac
            LIB_NAME="librandomx.a"
            ;;
        *)
            err "Unsupported OS: $OS. Use download-randomx.ps1 for Windows."
            exit 1
            ;;
    esac

    info "Platform: $PLATFORM ($OS $ARCH)"
}

# ── Get download URL from GitHub releases ──────────────────────────────────
get_download_url() {
    step "Finding precompiled RandomX release..."

    if [[ "$RANDOMX_VERSION" == "latest" ]]; then
        RELEASE_URL="https://api.github.com/repos/$REPO_OWNER/$REPO_NAME/releases/latest"
    else
        RELEASE_URL="https://api.github.com/repos/$REPO_OWNER/$REPO_NAME/releases/tags/$RANDOMX_VERSION"
    fi

    # Fetch release data
    RELEASE_JSON=$(curl -sL "$RELEASE_URL" 2>/dev/null || true)

    if echo "$RELEASE_JSON" | grep -q '"message": "Not Found"'; then
        warn "No GitHub release found. Falling back to building from source..."
        build_from_source
        exit 0
    fi

    # Find the asset for our platform
    ASSET_NAME="randomx-${PLATFORM}.tar.gz"
    DOWNLOAD_URL=$(echo "$RELEASE_JSON" | grep -o "\"browser_download_url\"[[:space:]]*:[[:space:]]*\"[^\"]*${ASSET_NAME}\"" | head -1 | cut -d'"' -f4)

    if [[ -z "$DOWNLOAD_URL" ]]; then
        warn "No precompiled binary found for $PLATFORM. Building from source..."
        build_from_source
        exit 0
    fi

    info "Found: $ASSET_NAME"
}

# ── Download and extract ───────────────────────────────────────────────────
download_and_extract() {
    step "Downloading precompiled RandomX for $PLATFORM..."

    mkdir -p "$DEST_DIR/include"
    mkdir -p "$DEST_DIR/lib/$PLATFORM"

    TMP_DIR=$(mktemp -d)
    trap "rm -rf $TMP_DIR" EXIT

    curl -L --progress-bar "$DOWNLOAD_URL" -o "$TMP_DIR/$ASSET_NAME"

    step "Extracting..."
    tar -xzf "$TMP_DIR/$ASSET_NAME" -C "$TMP_DIR"

    # Copy library
    if [[ -f "$TMP_DIR/$LIB_NAME" ]]; then
        cp "$TMP_DIR/$LIB_NAME" "$DEST_DIR/lib/$PLATFORM/"
    elif [[ -f "$TMP_DIR/lib/$LIB_NAME" ]]; then
        cp "$TMP_DIR/lib/$LIB_NAME" "$DEST_DIR/lib/$PLATFORM/"
    fi

    # Copy headers
    if [[ -f "$TMP_DIR/randomx.h" ]]; then
        cp "$TMP_DIR/randomx.h" "$DEST_DIR/include/"
    elif [[ -d "$TMP_DIR/include" ]]; then
        cp "$TMP_DIR/include/"*.h "$DEST_DIR/include/"
    fi

    info "Installed: $DEST_DIR/lib/$PLATFORM/$LIB_NAME"
    info "Headers:   $DEST_DIR/include/"
}

# ── Fallback: build from source ────────────────────────────────────────────
build_from_source() {
    step "Building RandomX from source..."

    DEPS_DIR="$(cd "$(dirname "$0")/.." && pwd)/deps"
    RANDOMX_SRC="$DEPS_DIR/RandomX"

    mkdir -p "$DEPS_DIR"

    if [[ -d "$RANDOMX_SRC" ]]; then
        warn "RandomX source exists. Pulling latest..."
        cd "$RANDOMX_SRC" && git pull --ff-only && cd -
    else
        git clone --depth 1 https://github.com/tevador/RandomX.git "$RANDOMX_SRC"
    fi

    mkdir -p "$RANDOMX_SRC/build"
    cd "$RANDOMX_SRC/build"
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build . --config Release -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
    cd - >/dev/null

    # Copy to precompiled directory
    mkdir -p "$DEST_DIR/include"
    mkdir -p "$DEST_DIR/lib/$PLATFORM"

    cp "$RANDOMX_SRC/build/$LIB_NAME" "$DEST_DIR/lib/$PLATFORM/"
    cp "$RANDOMX_SRC/src/randomx.h" "$DEST_DIR/include/"

    info "Built and installed RandomX to $DEST_DIR/"
}

# ── Verify ─────────────────────────────────────────────────────────────────
verify_install() {
    step "Verifying installation..."

    if [[ -f "$DEST_DIR/lib/$PLATFORM/$LIB_NAME" ]] && [[ -f "$DEST_DIR/include/randomx.h" ]]; then
        LIB_SIZE=$(du -h "$DEST_DIR/lib/$PLATFORM/$LIB_NAME" | cut -f1)
        info "RandomX library: $LIB_SIZE ($DEST_DIR/lib/$PLATFORM/$LIB_NAME)"
        info "RandomX header:  $DEST_DIR/include/randomx.h"
        echo ""
        info "Precompiled RandomX is ready! CMake will auto-detect it."
    else
        err "Verification failed. Some files are missing."
        exit 1
    fi
}

# ── Main ───────────────────────────────────────────────────────────────────
main() {
    echo -e "${CYAN}"
    echo "  ╔══════════════════════════════════════════════════╗"
    echo "  ║     RandomX Precompiled Binary Downloader        ║"
    echo "  ╚══════════════════════════════════════════════════╝"
    echo -e "${NC}"

    detect_platform
    get_download_url
    download_and_extract
    verify_install
}

main "$@"
