// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of `ICSharpCode.Decompiler.TypeSystem.ReflectionHelper` (the `GetTypeCode` + `FindType` leaves, D513). The C#
// `public static class ReflectionHelper` is a namespace of extension methods on `IType`/`ITypeDefinition`;
// this leaf ports `TypeCode GetTypeCode(this IType type)` -- the numeric-type-code lookup used by
// `CSharpConversions`'s numeric-conversion helpers (`ImplicitNumericConversion`/`IsNumericType`/
// `AnyNumericConversion`) and by `NormalizeTypeVisitor`'s `IntPtrToNInt` arms -- and
// `IType FindType(this ICompilation compilation, TypeCode typeCode)` -- the built-in-type lookup the
// `CSharpOperators` parameter tables are built through. The other `ReflectionHelper` members
// (`ParseReflectionName`/`ResolveTypeName`/`ApplyTypeArguments`/...) are deferred.

#pragma once

#include "Decompiler/TypeSystem/KnownTypeCode.hpp"  // KnownTypeCode (the GetTypeCode source domain)

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

// Forward-declared (the FindType declaration below takes a reference; the .cpp includes
// the full header).
class ICompilation;

// The C# `enum TypeCode` -- `System.TypeCode`, the BCL enum the decompiler mirrors. The order of
// `KnownTypeCode`'s first 18 values (None/Object/DBNull/Boolean/Char/SByte/Byte/Int16/UInt16/Int32/UInt32/
// Int64/UInt64/Single/Double/Decimal/DateTime/String) "must correspond to those in System.TypeCode" (the
// `KnownTypeCode` comment), so `(TypeCode)knownTypeCode` is a faithful `static_cast` for those values
// (`None` <-> `Empty`, the rest identity). Values past `String` (Void and the collection-interface/
// Task/etc. codes) have no `TypeCode` counterpart and map to `Empty` (the `GetTypeCode` `<= String`
// guard). `Empty == 0` is the "no matching type code" sentinel.
enum class TypeCode : std::uint8_t {
    Empty = 0,
    Object = 1,
    DBNull = 2,
    Boolean = 3,
    Char = 4,
    SByte = 5,
    Byte = 6,
    Int16 = 7,
    UInt16 = 8,
    Int32 = 9,
    UInt32 = 10,
    Int64 = 11,
    UInt64 = 12,
    Single = 13,
    Double = 14,
    Decimal = 15,
    DateTime = 16,
    String = 17,
};

class IType;

// The C# `public static TypeCode GetTypeCode(this IType type)` -- the type-code lookup. `dynamic_cast`s
// the `IType` to `ITypeDefinition` (the C# `type as ITypeDefinition`); if the definition's `KnownTypeCode`
// is `<= String` and not `Void`, returns `(TypeCode)knownTypeCode` (the numeric cast -- the `KnownTypeCode`
// values 0-17 align with `TypeCode` 0-17); else `Empty`.
TypeCode GetTypeCode(const IType& type);

// The C# `public static IType FindType(this ICompilation compilation, TypeCode typeCode)`
// (ReflectionHelper.cs line 106) -- the built-in-type lookup by `System.TypeCode`: a faithful
// `static_cast<KnownTypeCode>` (the `KnownTypeCode` values 0-17 align with `TypeCode` 0-17,
// `None` <-> `Empty`) forwarded to `ICompilation.FindType(KnownTypeCode)`. The CSharpOperators
// parameter tables (`InitParameterArrays`) are built through this lookup. A NON-NULL
// reference return (the `ICompilation::FindType` contract, mirrored by the delegation).
const IType& FindType(const ICompilation& compilation, TypeCode typeCode);

} // namespace ILSpy::Decompiler::TypeSystem
