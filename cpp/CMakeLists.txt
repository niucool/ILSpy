# Top-level CMake for the C++ port of ILSpy (core + CLI).
#
# Produces three targets (see PORT_PLAN.md section 8):
#   ilspy       -- aggregate static library (ilspy.lib / libilspy.a)
#   ilspy_cli   -- the ilspycmd executable
#   ilspy_tests -- the Google Test driver
#
# Each subdirectory (Decompiler/, ILSpyX/, BamlDecompiler/, ILSpyCmd/) adds its
# own translation units to the single `ilspy` library via target_sources(), so
# the on-disk artifact is one static library, matching the mirror layout's
# "static library + CLI" build outputs.

cmake_minimum_required(VERSION 3.15)

project(ilspy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators default to Debug; multi-config (VS) ignores this.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Third-party dependencies (vcpkg manifest mode, see vcpkg.json) ---
# VCPKG_ROOT must point at a vcpkg checkout; the presets wire the toolchain.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(plog CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
# miniz (deflate) is optional for now: it is needed only from Phase 7/8 (Portable
# PDB writes, --dump-package). It lives behind the `deflate` vcpkg manifest
# feature (see vcpkg.json); the default bring-up does not pull it because the
# miniz source download hit an environment SSL/proxy issue. Make this REQUIRED
# when the first deflate call site lands (PORT_PLAN.md section 5.8).
find_package(miniz CONFIG QUIET)
find_package(cxxopts CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# miniz's exported target name differs across upstream releases; normalise it.
if(TARGET miniz::miniz)
    set(ILSPY_MINIZ_LIB miniz::miniz)
elseif(TARGET miniz)
    set(ILSPY_MINIZ_LIB miniz)
else()
    set(ILSPY_MINIZ_LIB "")
    message(STATUS "ilspy: miniz not found (optional for now); deflate features deferred to Phase 7/8.")
endif()

# The single aggregate static library. Subdirectories attach sources to it.
add_library(ilspy STATIC)
add_library(ILSpy::ilspy ALIAS ilspy)

target_include_directories(ilspy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# JSON and logging are part of the public ABI; compression is an impl detail.
target_link_libraries(ilspy
    PUBLIC  nlohmann_json::nlohmann_json plog::plog
    PRIVATE lz4::lz4)
if(ILSPY_MINIZ_LIB)
    target_link_libraries(ilspy PRIVATE ${ILSPY_MINIZ_LIB})
endif()

# The vendored microsoft/winmd headers (Decompiler/Metadata/Ecma335/winmd/)
# pull in <windows.h> on Windows. Keep its macros out of the rest of the tree.
if(WIN32)
    target_compile_definitions(ilspy PUBLIC
        WIN32_LEAN_AND_MEAN NOMINMAX NOGDI NOSOUND NOSERVICE)
endif()

# Plan decision D6: no -Werror until Phase 11 (no C++ analogue of the C#
# `nullable` WarningsAsErrors). Keep warnings informative, not fatal.
if(MSVC)
    target_compile_options(ilspy PRIVATE /W3 /utf-8)
else()
    target_compile_options(ilspy PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_subdirectory(Decompiler)
add_subdirectory(ILSpyX)
add_subdirectory(BamlDecompiler)
add_subdirectory(ILSpyCmd)

enable_testing()
add_subdirectory(tests)
