#=============================================================================
# FindPrecompiledRandomX.cmake
#
# Locates precompiled RandomX static libraries and headers.
# Sets:
#   PRECOMPILED_RANDOMX_FOUND   - TRUE if found
#   PRECOMPILED_RANDOMX_INCLUDE - Path to include directory
#   PRECOMPILED_RANDOMX_LIB     - Full path to the static library
#=============================================================================

set(RANDOMX_PRECOMPILED_DIR "${CMAKE_SOURCE_DIR}/third_party/randomx")

# ── Detect platform subdirectory ────────────────────────────────────────────
if(WIN32)
    set(_RANDOMX_PLATFORM "windows-x64")
    set(_RANDOMX_LIB_NAME "randomx.lib")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_RANDOMX_PLATFORM "macos-arm64")
    else()
        set(_RANDOMX_PLATFORM "macos-x64")
    endif()
    set(_RANDOMX_LIB_NAME "librandomx.a")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_RANDOMX_PLATFORM "linux-arm64")
    else()
        set(_RANDOMX_PLATFORM "linux-x64")
    endif()
    set(_RANDOMX_LIB_NAME "librandomx.a")
endif()

set(_RANDOMX_INC_DIR "${RANDOMX_PRECOMPILED_DIR}/include")
set(_RANDOMX_LIB_DIR "${RANDOMX_PRECOMPILED_DIR}/lib/${_RANDOMX_PLATFORM}")
set(_RANDOMX_LIB_PATH "${_RANDOMX_LIB_DIR}/${_RANDOMX_LIB_NAME}")

# ── Check existence ─────────────────────────────────────────────────────────
if(EXISTS "${_RANDOMX_INC_DIR}/randomx.h" AND EXISTS "${_RANDOMX_LIB_PATH}")
    set(PRECOMPILED_RANDOMX_FOUND TRUE)
    set(PRECOMPILED_RANDOMX_INCLUDE "${_RANDOMX_INC_DIR}")
    set(PRECOMPILED_RANDOMX_LIB "${_RANDOMX_LIB_PATH}")

    # Create an imported static library target
    if(NOT TARGET randomx::precompiled)
        add_library(randomx::precompiled STATIC IMPORTED GLOBAL)
        set_target_properties(randomx::precompiled PROPERTIES
            IMPORTED_LOCATION "${PRECOMPILED_RANDOMX_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${PRECOMPILED_RANDOMX_INCLUDE}"
        )
    endif()

    message(STATUS "Found precompiled RandomX: ${_RANDOMX_LIB_PATH}")
    message(STATUS "  Platform: ${_RANDOMX_PLATFORM}")
    message(STATUS "  Headers:  ${_RANDOMX_INC_DIR}")
else()
    set(PRECOMPILED_RANDOMX_FOUND FALSE)
    if(NOT EXISTS "${_RANDOMX_INC_DIR}/randomx.h")
        message(STATUS "Precompiled RandomX: header not found at ${_RANDOMX_INC_DIR}/randomx.h")
    endif()
    if(NOT EXISTS "${_RANDOMX_LIB_PATH}")
        message(STATUS "Precompiled RandomX: library not found at ${_RANDOMX_LIB_PATH}")
    endif()
endif()
