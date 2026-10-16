// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Single include point for the vendored microsoft/winmd headers.
//
// winmd's XLANG_ASSERT (active in _DEBUG builds) encodes WinMD-only invariants
// that do not hold for general .NET assemblies the decompiler reads -- e.g.
// signature.h asserts that ElementType::TypedByRef never appears in a ParamSig,
// but mscorlib has System.TypedReference parameters. To keep the vendored
// headers verbatim (per Ecma335/winmd/README.md), we disable XLANG_ASSERT by
// undefining _DEBUG around the winmd include. _DEBUG only gates XLANG_ASSERT
// in winmd's base.h; the standard-library headers (gated by NDEBUG, not _DEBUG)
// are pre-included first with _DEBUG defined so the debug CRT/iterator-debug
// ABI of the translation unit is unaffected. _DEBUG is restored afterward.

#pragma once

#include <array>
#include <bitset>
#include <filesystem>
#include <fstream>
#include <future>
#include <list>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#if defined(_WIN32)
#  include <windows.h>
#endif

#ifdef _DEBUG
#  define ILSPY_WINMD_HAD_DEBUG 1
#  undef _DEBUG
#endif
#include "winmd_reader.h"
#ifdef ILSPY_WINMD_HAD_DEBUG
#  define _DEBUG 1
#  undef ILSPY_WINMD_HAD_DEBUG
#endif
