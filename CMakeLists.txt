cmake_minimum_required(VERSION 3.16)
project(ai-monero-miner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(USE_HWLOC   "Enable hwloc for NUMA / topology awareness" ON)
option(BUILD_TESTS "Build unit tests"                           OFF)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# RandomX (submodule expected at third_party/RandomX)
add_subdirectory(third_party/RandomX EXCLUDE_FROM_ALL)

if(USE_HWLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HWLOC IMPORTED_TARGET hwloc)
    if(HWLOC_FOUND)
        add_compile_definitions(HAS_HWLOC)
    endif()
endif()

# nlohmann/json (header-only, fetched via FetchContent)
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

target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    randomx
    nlohmann_json::nlohmann_json
)

if(HWLOC_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::HWLOC)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
