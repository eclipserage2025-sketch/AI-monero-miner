cmake_minimum_required(VERSION 3.16)
project(ai-monero-miner VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(USE_PRECOMPILED_RANDOMX "Use precompiled RandomX libraries"        ON)
option(USE_HWLOC               "Enable hwloc for NUMA / topology awareness" ON)
option(BUILD_TESTS             "Build unit tests"                           OFF)

# ── Platform-specific compile definitions & flags ────────────────────────────
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
    add_compile_options(/W3 /permissive- /Zc:__cplusplus /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ── RandomX ──────────────────────────────────────────────────────────────────
# Strategy:
#   1. USE_PRECOMPILED_RANDOMX=ON  → look for precompiled libs in third_party/
#   2. Fallback: FetchContent to download & build from source
#   3. Last resort: expect third_party/RandomX submodule

if(USE_PRECOMPILED_RANDOMX)
    include(${CMAKE_SOURCE_DIR}/cmake/FindPrecompiledRandomX.cmake)
endif()

if(PRECOMPILED_RANDOMX_FOUND)
    message(STATUS "Using PRECOMPILED RandomX")
    set(RANDOMX_TARGET randomx::precompiled)
else()
    message(STATUS "Precompiled RandomX not found — building from source via FetchContent")

    include(FetchContent)
    FetchContent_Declare(randomx
        GIT_REPOSITORY https://github.com/tevador/RandomX.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(randomx)
    set(RANDOMX_TARGET randomx)
endif()

# ── hwloc (optional) ────────────────────────────────────────────────────────
if(USE_HWLOC)
    if(NOT WIN32)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(HWLOC IMPORTED_TARGET hwloc)
        if(HWLOC_FOUND)
            add_compile_definitions(HAS_HWLOC)
        endif()
    endif()
endif()

# ── nlohmann/json (header-only, fetched via FetchContent) ────────────────────
include(FetchContent)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# ── Sources ──────────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# If using precompiled, also add include path explicitly for non-target usage
if(PRECOMPILED_RANDOMX_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${PRECOMPILED_RANDOMX_INCLUDE}
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    ${RANDOMX_TARGET}
    nlohmann_json::nlohmann_json
)

if(HWLOC_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::HWLOC)
endif()

# ── Windows-specific libraries ───────────────────────────────────────────────
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 pdh iphlpapi ole32 oleaut32 wbemuuid)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
