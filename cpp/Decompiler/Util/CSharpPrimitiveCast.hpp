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

// CSharpPrimitiveCast: static helper for converting between primitive types.
// Port of the C# Util/CSharpPrimitiveCast.cs (the C# `ICSharpCode.Decompiler.Util`
// namespace; the file lives next to the other Util primitives). The C# entry point is
// `Cast(TypeCode, object, bool checkForOverflow)` -- the constant-value converter the
// CSharpResolver.Convert region (ResolveCast / the deferred operator-method Invoke
// constant evaluation) consumes.
//
// The C# boxed `object` ports to `std::any` holding the C++ counterpart of each C#
// primitive (the D374/D424 constant-value convention): `bool`, `char16_t` (the C#
// `char`), the fixed-width integrals `std::int8_t`..`std::uint64_t`, `float`, `double`,
// `std::string`, and the `Decimal` stand-in (Util/Decimal.hpp). An empty `any` is the
// C# `null`.
//
// The C# `System.TypeCode` ports to `ILSpy::Decompiler::TypeSystem::TypeCode` (the port's
// TypeCode lives in TypeSystem/ReflectionHelper.hpp -- a light enum header; this is the
// one TypeSystem include a Util header makes, mirroring how the C# Util class consumes
// the BCL enum).

#pragma once

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // TypeCode (the C# System.TypeCode)

#include <any>
#include <stdexcept>

namespace ILSpy::Decompiler::Util {

// The C# `System.OverflowException` -- thrown by a checked-context conversion whose true
// result leaves the target type's range (and by the decimal conversions in BOTH
// contexts: System.Decimal's op_Explicit operators throw regardless of the caller's
// checked/unchecked context). A distinct type (deriving std::runtime_error) so the
// ResolveCast catch arms can discriminate it from InvalidCastException -- the C#
// nint/nuint cast path maps the two exceptions to DIFFERENT fallbacks.
struct OverflowException : std::runtime_error {
    OverflowException()
        : std::runtime_error("OverflowException")
    {
    }
};

// The C# `System.InvalidCastException` -- thrown when no conversion exists between the
// pair (e.g. a string operand, or a target that is not one of the 12 primitive target
// codes).
struct InvalidCastException : std::runtime_error {
    InvalidCastException()
        : std::runtime_error("InvalidCastException")
    {
    }
};

// Performs a conversion between primitive types.
// Unfortunately we cannot use Convert.ChangeType because it has different semantics
// (e.g. rounding behavior for floats, overflow, etc.), so we write down every possible
// primitive C# cast and let the compiler figure out the exact semantics.
// And we have to do everything twice, once in a checked-block, once in an unchecked-block.
//
// <exception cref="OverflowException">Overflow checking is enabled and an overflow
// occurred.</exception>
// <exception cref="InvalidCastException">The cast is invalid, e.g. casting a boolean to
// an integer.</exception>
std::any Cast(ILSpy::Decompiler::TypeSystem::TypeCode targetType, const std::any& input,
              bool checkForOverflow);

} // namespace ILSpy::Decompiler::Util
