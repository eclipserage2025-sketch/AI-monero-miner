#!/usr/bin/env bash
#=============================================================================
# AI Monero Miner — Auto Install Script (Linux / macOS)
# Usage:  chmod +x install.sh && ./install.sh [--from-source]
#=============================================================================
set -euo pipefail

FROM_SOURCE=false
for arg in "$@"; do
  case "$arg" in
    --from-source) FROM_SOURCE=true ;;
  esac
done

# ── Colors ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

banner() {
  echo -e "${CYAN}"
  echo "  ╔══════════════════════════════════════════════════╗"
  echo "  ║       AI Monero Miner — Auto Installer v0.3.0   ║"
  echo "  ╚══════════════════════════════════════════════════╝"
  echo -e "${NC}"
}

info()  { echo -e "${GREEN}[✔]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
err()   { echo -e "${RED}[✘]${NC} $*"; }
step()  { echo -e "\n${BOLD}${CYAN}▸ $*${NC}"; }

check_root() {
  if [[ $EUID -eq 0 ]]; then
    warn "Running as root. Dependencies will be installed system-wide."
  fi
}

# ── Detect OS & Package Manager ────────────────────────────────────────────
detect_os() {
  step "Detecting operating system..."
  OS="$(uname -s)"
  ARCH="$(uname -m)"
  info "OS: $OS | Arch: $ARCH"

  case "$OS" in
    Linux*)
      if command -v apt-get &>/dev/null; then
        PKG_MGR="apt"
      elif command -v dnf &>/dev/null; then
        PKG_MGR="dnf"
      elif command -v yum &>/dev/null; then
        PKG_MGR="yum"
      elif command -v pacman &>/dev/null; then
        PKG_MGR="pacman"
      elif command -v apk &>/dev/null; then
        PKG_MGR="apk"
      elif command -v zypper &>/dev/null; then
        PKG_MGR="zypper"
      else
        err "Unsupported Linux distribution. Install dependencies manually."
        exit 1
      fi
      info "Package manager: $PKG_MGR"
      ;;
    Darwin*)
      if ! command -v brew &>/dev/null; then
        warn "Homebrew not found. Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
      fi
      PKG_MGR="brew"
      info "Package manager: brew"
      ;;
    *)
      err "Unsupported OS: $OS. Use install.ps1 for Windows."
      exit 1
      ;;
  esac
}

# ── Install System Dependencies ────────────────────────────────────────────
install_deps() {
  step "Installing build dependencies..."

  SUDO=""
  if [[ $EUID -ne 0 ]] && [[ "$PKG_MGR" != "brew" ]]; then
    SUDO="sudo"
  fi

  case "$PKG_MGR" in
    apt)
      $SUDO apt-get update -qq
      $SUDO apt-get install -y --no-install-recommends \
        build-essential cmake git pkg-config \
        nlohmann-json3-dev libhwloc-dev \
        libssl-dev ca-certificates curl
      ;;
    dnf|yum)
      $SUDO $PKG_MGR install -y \
        gcc gcc-c++ cmake make git pkg-config \
        json-devel hwloc-devel \
        openssl-devel ca-certificates curl
      ;;
    pacman)
      $SUDO pacman -Syu --noconfirm --needed \
        base-devel cmake git pkg-config \
        nlohmann-json hwloc \
        openssl curl
      ;;
    apk)
      $SUDO apk add --no-cache \
        build-base cmake git pkgconfig \
        nlohmann-json hwloc-dev \
        openssl-dev curl
      ;;
    zypper)
      $SUDO zypper install -y \
        gcc gcc-c++ cmake make git pkg-config \
        nlohmann_json-devel hwloc-devel \
        libopenssl-devel ca-certificates curl
      ;;
    brew)
      brew install cmake nlohmann-json hwloc openssl pkg-config
      ;;
  esac

  info "System dependencies installed."
}

# ── Setup RandomX ──────────────────────────────────────────────────────────
setup_randomx() {
  SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
  PRECOMPILED_DIR="$SCRIPT_DIR/third_party/randomx"

  # Detect platform for precompiled check
  case "$OS" in
    Linux*)
      case "$ARCH" in
        x86_64|amd64)   PLATFORM="linux-x64"   ; LIB_NAME="librandomx.a" ;;
        aarch64|arm64)  PLATFORM="linux-arm64"  ; LIB_NAME="librandomx.a" ;;
        *)              PLATFORM="unknown"      ; LIB_NAME="librandomx.a" ;;
      esac
      ;;
    Darwin*)
      case "$ARCH" in
        x86_64|amd64)   PLATFORM="macos-x64"   ; LIB_NAME="librandomx.a" ;;
        arm64|aarch64)  PLATFORM="macos-arm64"  ; LIB_NAME="librandomx.a" ;;
        *)              PLATFORM="unknown"      ; LIB_NAME="librandomx.a" ;;
      esac
      ;;
  esac

  if [[ "$FROM_SOURCE" == "true" ]]; then
    step "Building RandomX from source (--from-source)..."
    build_randomx_from_source
    return
  fi

  # Check if precompiled binary already exists
  if [[ -f "$PRECOMPILED_DIR/lib/$PLATFORM/$LIB_NAME" ]] && [[ -f "$PRECOMPILED_DIR/include/randomx.h" ]]; then
    step "Using precompiled RandomX..."
    info "Precompiled RandomX found at $PRECOMPILED_DIR/lib/$PLATFORM/$LIB_NAME"
    USE_PRECOMPILED=true
    return
  fi

  # Try downloading precompiled binary
  step "Downloading precompiled RandomX..."
  if [[ -x "$SCRIPT_DIR/scripts/download-randomx.sh" ]]; then
    if "$SCRIPT_DIR/scripts/download-randomx.sh"; then
      if [[ -f "$PRECOMPILED_DIR/lib/$PLATFORM/$LIB_NAME" ]]; then
        info "Precompiled RandomX downloaded successfully."
        USE_PRECOMPILED=true
        return
      fi
    fi
  fi

  # Fallback: build from source
  warn "Precompiled RandomX not available. Building from source..."
  build_randomx_from_source
}

build_randomx_from_source() {
  DEPS_DIR="$(pwd)/deps"
  RANDOMX_DIR="$DEPS_DIR/RandomX"

  mkdir -p "$DEPS_DIR"

  if [[ -d "$RANDOMX_DIR" ]]; then
    warn "RandomX directory exists. Pulling latest..."
    cd "$RANDOMX_DIR" && git pull --ff-only && cd -
  else
    git clone --depth 1 https://github.com/tevador/RandomX.git "$RANDOMX_DIR"
  fi

  mkdir -p "$RANDOMX_DIR/build"
  cd "$RANDOMX_DIR/build"
  cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  cmake --build . --config Release -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
  cd - >/dev/null

  # Also install into precompiled directory for future use
  SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
  PRECOMPILED_DIR="$SCRIPT_DIR/third_party/randomx"
  mkdir -p "$PRECOMPILED_DIR/include"
  mkdir -p "$PRECOMPILED_DIR/lib/$PLATFORM"

  cp "$RANDOMX_DIR/build/$LIB_NAME" "$PRECOMPILED_DIR/lib/$PLATFORM/" 2>/dev/null || true
  cp "$RANDOMX_DIR/src/randomx.h" "$PRECOMPILED_DIR/include/" 2>/dev/null || true

  USE_PRECOMPILED=true
  info "RandomX built and cached in $PRECOMPILED_DIR/"
}

# ── Build the Miner ───────────────────────────────────────────────────────
build_miner() {
  step "Building AI Monero Miner..."

  BUILD_DIR="$(pwd)/build"
  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"

  CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
  )

  if [[ "${USE_PRECOMPILED:-false}" == "true" ]]; then
    CMAKE_ARGS+=(-DUSE_PRECOMPILED_RANDOMX=ON)
    info "Using precompiled RandomX libraries."
  else
    CMAKE_ARGS+=(-DUSE_PRECOMPILED_RANDOMX=OFF)
    info "RandomX will be built via FetchContent."
  fi

  # macOS: help find OpenSSL from Homebrew
  if [[ "$OS" == "Darwin" ]]; then
    OPENSSL_PREFIX="$(brew --prefix openssl 2>/dev/null || true)"
    if [[ -n "$OPENSSL_PREFIX" ]]; then
      CMAKE_ARGS+=(-DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX")
    fi
  fi

  cmake .. "${CMAKE_ARGS[@]}"
  cmake --build . --config Release -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

  cd - >/dev/null
  info "Build complete!"
}

# ── Setup Config ───────────────────────────────────────────────────────────
setup_config() {
  step "Setting up configuration..."

  CONFIG_FILE="$(pwd)/config/miner_config.json"
  DEFAULT_CONFIG="$(pwd)/config/default_config.json"

  if [[ -f "$CONFIG_FILE" ]]; then
    warn "Config file already exists at $CONFIG_FILE — skipping."
  elif [[ -f "$DEFAULT_CONFIG" ]]; then
    cp "$DEFAULT_CONFIG" "$CONFIG_FILE"
    info "Config created at $CONFIG_FILE"
    warn "Edit this file to set your wallet address and pool before mining!"
  else
    warn "No default config found. You'll need to create config/miner_config.json manually."
  fi
}

# ── Post-Install Summary ──────────────────────────────────────────────────
summary() {
  echo ""
  echo -e "${CYAN}══════════════════════════════════════════════════════${NC}"
  echo -e "${GREEN}${BOLD}  ✅ Installation Complete!${NC}"
  echo -e "${CYAN}══════════════════════════════════════════════════════${NC}"
  echo ""
  echo -e "  ${BOLD}Binary:${NC}     ./build/ai-monero-miner"
  echo -e "  ${BOLD}Config:${NC}     ./config/miner_config.json"
  echo ""
  echo -e "  ${YELLOW}Before mining, edit the config file:${NC}"
  echo -e "    • Set your ${BOLD}wallet_address${NC}"
  echo -e "    • Set your ${BOLD}pool_url${NC} and ${BOLD}pool_port${NC}"
  echo ""
  echo -e "  ${BOLD}Run:${NC}"
  echo -e "    ./build/ai-monero-miner"
  echo -e "    ./build/ai-monero-miner --config /path/to/config.json"
  echo ""
  echo -e "  ${BOLD}Options used:${NC}"
  if [[ "${USE_PRECOMPILED:-false}" == "true" ]]; then
    echo -e "    ⚡ RandomX: precompiled (fast install)"
  else
    echo -e "    🔧 RandomX: built from source"
  fi
  echo ""
  echo -e "${CYAN}══════════════════════════════════════════════════════${NC}"
}

# ── Main ───────────────────────────────────────────────────────────────────
main() {
  banner
  check_root
  detect_os
  install_deps
  setup_randomx
  build_miner
  setup_config
  summary
}

main "$@"
