# Precompiled RandomX Libraries

This directory holds precompiled RandomX static libraries and headers for all supported platforms.

## Directory Structure

```
third_party/randomx/
├── include/
│   └── randomx.h          # RandomX public API header
├── lib/
│   ├── linux-x64/
│   │   └── librandomx.a
│   ├── linux-arm64/
│   │   └── librandomx.a
│   ├── macos-x64/
│   │   └── librandomx.a
│   ├── macos-arm64/
│   │   └── librandomx.a
│   └── windows-x64/
│       └── randomx.lib
└── README.md
```

## How Binaries Are Built

Precompiled binaries are built automatically via GitHub Actions (`.github/workflows/build-randomx.yml`)
and published as release assets. The download scripts (`scripts/download-randomx.*`) fetch the correct
binary for your platform.

## Updating

To update to a new RandomX version:
1. Update `RANDOMX_VERSION` in `.github/workflows/build-randomx.yml`
2. Push to trigger a new CI build
3. Create a new release to publish the artifacts
4. Run `scripts/download-randomx.sh` or `scripts/download-randomx.ps1` to fetch updated binaries

## Building From Source

If you prefer to build from source instead of using precompiled binaries:
```bash
cmake -DUSE_PRECOMPILED_RANDOMX=OFF ..
```
This will use CMake FetchContent to download and build RandomX from source.
