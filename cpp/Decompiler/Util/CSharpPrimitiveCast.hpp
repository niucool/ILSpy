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

// The C# `System.ArithmeticException` -- the common base of `OverflowException` and
// `DivideByZeroException`. The CSharpResolver operator-resolution regions wrap the
// constant-evaluation `m.Invoke(...)` call in `catch (ArithmeticException)` (CSharpResolver
// .cs lines 509-514 / 1004-1010) -- the catch must swallow exactly this family and NOT
// `InvalidCastException` (the operand-cast failure the C# lets propagate), so the family
// needs a distinct common base the catch arm can name. The base's ctor is protected
// (the C# never throws a bare `ArithmeticException`; only the family members are
// thrown, each carrying its own exception-kind message).
struct ArithmeticException : std::runtime_error {
protected:
    explicit ArithmeticException(const char* message)
        : std::runtime_error(message) {}
};

// The C# `System.OverflowException` -- thrown by a checked-context conversion whose true
// result leaves the target type's range (and by the decimal conversions in BOTH
// contexts: System.Decimal's op_Explicit operators throw regardless of the caller's
// checked/unchecked context), and by the operator-table bodies at the overflow boundaries
// (the checked unary negation of INT32_MIN/INT64_MIN, the checked binary arithmetic).
// Distinct types (deriving ArithmeticException / std::runtime_error) so the resolver's
// catch arms can discriminate them from InvalidCastException -- the C# nint/nuint cast
// path maps the two exception kinds to DIFFERENT fallbacks.
struct OverflowException : ArithmeticException {
    OverflowException()
        : ArithmeticException("OverflowException")
    {
    }
};

// The C# `System.DivideByZeroException` -- thrown by the binary operator bodies on an
// integer or decimal zero divisor (in BOTH checked and unchecked contexts). A member of
// the ArithmeticException family (the C# inheritance), so the resolver's constant-
// evaluation catch arm swallows it.
struct DivideByZeroException : ArithmeticException {
    DivideByZeroException()
        : ArithmeticException("DivideByZeroException")
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

// The C# `Type.GetTypeCode(input.GetType())` over the port's boxed constant-value types
// (the C# boxed object becomes the std::any holding the C++ counterpart of each C#
// primitive). A held type without a primitive counterpart maps to Object -- the C#
// GetTypeCode fallback. The internal Cast source-type mapping, exposed for the
// CSharpResolver's enum-operator constant-folding arm (`compilation.FindType(expression
// .ConstantValue.GetType())`, CSharpResolver.cs line 451 -- an enum constant holds its
// UNDERLYING primitive value, so the runtime type resolves through its TypeCode via the
// TypeCode-based FindType, ReflectionHelper.cs line 106).
ILSpy::Decompiler::TypeSystem::TypeCode TypeCodeOfBoxedValue(const std::any& value);

} // namespace ILSpy::Decompiler::Util
