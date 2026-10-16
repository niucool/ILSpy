Here's the updated document with C++17 explicitly specified as the language standard:

# ILSpy Mirror Layout Reorganization

## Overview

This document defines the complete mirror-layout mapping between the C# projects and the C++ `cpp/` port. 
Only core components and cli are needed, GUI/Addin/installer are not needed. 

## Build System & Dependencies

### Platform Support
- **Target Platforms**: Windows and Linux (x64)
- **Compiler Support**: 
  - Windows: MSVC (Visual Studio 2019/2022)
  - Linux: GCC 9+ or Clang 10+
- **Language Standard**: C++17 (ISO/IEC 14882:2017)
- **Build System**: CMake (minimum version 3.15)
- **Package Manager**: vcpkg for dependency management

### Third-Party Libraries
The following libraries are required and managed via vcpkg:

| Library | Purpose | vcpkg Package |
|---------|---------|---------------|
| nlohmann-json | JSON parsing and serialization | `nlohmann-json` |
| plog | Lightweight logging framework | `plog` |
| pe-parse | Portable Executable parsing library | `pe-parse` |
| Google Test | Unit testing framework | `gtest` |

**Setup Instructions**:

1. Configure CMake to use vcpkg toolchain:
   ```bash
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
   ```

**CMake Configuration Example**:
```cmake
# Minimum CMake version
cmake_minimum_required(VERSION 3.15)

# Project definition with C++17
project(ilspy VERSION 1.0.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages via vcpkg
find_package(nlohmann_json CONFIG REQUIRED)
find_package(plog CONFIG REQUIRED)
find_package(pe-parse CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
```

### Build Outputs
The project produces **two primary build artifacts**:

| Artifact | Type | Description |
|----------|------|-------------|
| **Static Library** | `.lib` (Windows) / `.a` (Linux) | Core ilspy parsing library (`ilspy.lib` / `libilspy.a`) for integration into other projects |
| **CLI Tool** | `.exe` (Windows) / executable (Linux) | Command-line interface for direct PE file analysis and testing |

**CMake Build Targets**:
```bash
# Build both static library and CLI
cmake --build build --config Release

# Build only the static library
cmake --build build --target ilspy_static --config Release

# Build only the CLI tool
cmake --build build --target ilspy_cli --config Release
```

### Testing

**Test Framework**: Google Test (gtest) is used for all unit tests

**Test Structure**:
- Test files mirror the source directory structure (e.g., `tests/PE/PE_Test.cpp` for `cpp/PE/PE.cpp`)
- Test executables run all unit tests and report results via Google Test framework

**Running Tests**:
```bash
# Run all tests
./build/tests/ilspy_tests

# Run specific test suite
./build/tests/ilspy_tests --gtest_filter=PE_Test.*

# Run with verbose output
./build/tests/ilspy_tests --gtest_output=json:test_results.json
```

**Test Coverage Requirements**:
- All public API functions must have corresponding unit tests
- Edge cases and error conditions must be covered
- Integration tests for complex ilspy parsing scenarios

## Mirror Layout Rules

1. **File Basename Matching**: C# `.cs` files → C++ `.hpp` + `.cpp` pair with **identical basenames**
2. **Partial Classes**: C# partial classes are merged into a single C++ class definition within the primary `.hpp`/`.cpp` pair
3. **Namespace to Directory Mapping**: C# namespaces map to C++ directory structures (e.g., `PE.Utils` → `cpp/PE/Utils/`)
4. **Directory Structure**: C++ directories **exactly mirror** C# project folder structure
5. **C#-Specific Constructs**: 
   - Properties → C++ getter/setter methods
   - Events → C++ callback/delegate patterns
   - LINQ → C++ STL algorithms or range-based loops
   - `async`/`await` → C++ coroutines or callback-based async patterns
6. **C++-Only Files**: Documented with a note and placed logically for infrastructure purposes

## Status Legend

- **Ported** — File exists and matches the C# source
- **Audit** — File exists but needs review/fixes for spec compliance
- **NEW** — File does not yet exist; planned per `PORT_PLAN.md`
- **C++-only** — No C# counterpart; C++ infrastructure file

## Key Migration Considerations

- **Memory Management**: C# garbage collection → C++ RAII/smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- **Exception Handling**: C# `try-catch-finally` → C++ `try-catch` with RAII for finally patterns
- **Interfaces**: C# interfaces → C++ abstract base classes with pure virtual functions
- **Generics**: C# generics → C++ templates
- **Extension Methods**: C# extension methods → C++ free functions or static methods
- **String Handling**: C# `System.String` → C++ `std::string`/`std::wstring` with proper encoding handling
- **Collections**: C# `List<T>`, `Dictionary<T>` → C++ `std::vector`, `std::unordered_map`
- **C++17 Features Leveraged**: 
  - `std::optional` for nullable return types
  - `std::variant` for union-like types
  - `std::filesystem` for file system operations
  - Structured bindings for cleaner code
  - `if constexpr` for compile-time conditionals
  - `std::string_view` for efficient string handling