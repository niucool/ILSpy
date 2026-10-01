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
# miniz (raw deflate) is the embedded-Portable-PDB decompressor (the MPDB
# debug-directory entry blob, the PeImagePortablePdb.cpp decode of the
# `ReadEmbeddedPortablePdbDebugDirectoryData` data) and later the
# -genpdb/--dump-package deflate call sites (PORT_PLAN.md section 5.8). It was
# optional during initial bring-up because the vcpkg miniz source download hit
# an environment SSL/proxy issue; the first deflate call site has landed, so it
# is REQUIRED now.
find_package(miniz CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# miniz's exported target name differs across upstream releases; normalise it.
if(TARGET miniz::miniz)
    set(ILSPY_MINIZ_LIB miniz::miniz)
elseif(TARGET miniz)
    set(ILSPY_MINIZ_LIB miniz)
else()
    message(FATAL_ERROR "ilspy: miniz target not found after find_package succeeded")
endif()

# GCC 15 enables -Wchanges-meaning as an error by default. The port's
# accessor-named-after-its-enum-type convention (the C# property shape,
# e.g. ISymbol::SymbolKind()) trips it in every TU that includes the
# TypeSystem headers, so disable it for every target; older GCC and Clang
# silently ignore the unknown negative form.
#
# GCC < 13 predates the -Wchanges-meaning spelling entirely, so -Wno-* is
# silently ignored there and the diagnostic stays a hard error gated by
# -fpermissive (the box's stock GCC 11.4 hits exactly this). Use -fpermissive
# on those — same convention the sibling dnlib/dnscope C++ ports use — and
# the surgical -Wno-changes-meaning where it exists.
if(MSVC)
    # nothing to do
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    add_compile_options(-fpermissive)
else()
    add_compile_options(-Wno-changes-meaning)
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
    # /utf-8 is PUBLIC: the sources are UTF-8 (decision D2) and every
    # consumer (the tests carry UTF-8 fixture raw strings) must compile
    # them as UTF-8 too -- MSVC's default source charset is the system
    # codepage, which mangles multi-byte literals into '?' bytes.
    target_compile_options(ilspy PUBLIC /utf-8)
    target_compile_options(ilspy PRIVATE /W3)
else()
    target_compile_options(ilspy PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_subdirectory(Decompiler)
add_subdirectory(ILSpyX)
add_subdirectory(BamlDecompiler)
add_subdirectory(ILSpyCmd)

# bennu-umbrella compatibility (same convention as the capa/floss/vivisect
# subprojects, see bennu/CMakeLists.txt): the umbrella sets BUILD_TESTING=OFF
# (CACHE FORCE) and adds this project with EXCLUDE_FROM_ALL, so skip the test
# tree there — otherwise ilspy_tests registers into the umbrella's ctest suite
# unbuildable. Standalone builds default BUILD_TESTING to ON and keep tests.
option(BUILD_TESTING "Build the testing tree." ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
