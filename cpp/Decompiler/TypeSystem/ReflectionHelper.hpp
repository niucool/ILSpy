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

// Port of `ICSharpCode.Decompiler.TypeSystem.ReflectionHelper` (the `GetTypeCode` + `FindType` +
// `ParseReflectionName` leaves, D513). The C# `public static class ReflectionHelper` is a
// namespace of extension methods and reflection-name helpers on `IType`/`ITypeDefinition`/
// `ICompilation`; the leaves ported here are `TypeCode GetTypeCode(this IType type)` -- the
// numeric-type-code lookup used by `CSharpConversions`'s numeric-conversion helpers
// (`ImplicitNumericConversion`/`IsNumericType`/`AnyNumericConversion`) and by
// `NormalizeTypeVisitor`'s `IntPtrToNInt` arms -- `IType FindType(this ICompilation compilation,
// TypeCode typeCode)` -- the built-in-type lookup the `CSharpOperators` parameter tables are
// built through -- the two `SplitTypeParameterCountFromReflectionName` overloads -- and
// `ParseReflectionName`/`ResolveTypeName` (ReflectionHelper.cs lines 131-243): the reflection-name
// parser/resolver that turns a (possibly assembly-qualified, possibly nested/decorated)
// `System.Reflection.Metadata` `TypeName` into an `IType` against an `ITypeResolveContext`, plus
// the `ReadTypeParameterCount` scan helper the same `#region` declares (its `IdStringProvider`
// consumer stays deferred with the Documentation tree). The remaining members
// (`FindType(Type)`/`FindType(StackType, Sign)` -- the `System.Type`/`IL` stack-type inputs) are
// deferred with their consumers.

#pragma once

#include "Decompiler/TypeSystem/KnownTypeCode.hpp"  // KnownTypeCode (the GetTypeCode source domain)

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::TypeSystem {

// Forward-declared (the declarations below take references/return a shared_ptr; the
// .cpp includes the full headers).
class ICompilation;
class IType;
class ITypeResolveContext;

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
    // The real System.TypeCode has NO member with value 17 (probed against
    // .NET 10: String=18, and (TypeCode)17 renders the bare decimal). The hole
    // is what makes the C# GetTypeCode's numeric cast `(TypeCode)typeCode` the
    // identity over the KnownTypeCode range (KnownTypeCode.String is ALSO 18
    // -- the TypeCode numbering is why the KnownTypeCode hole exists).
    String = 18,
};

class IType;

// The C# `public static TypeCode GetTypeCode(this IType type)` -- the type-code lookup. `dynamic_cast`s
// the `IType` to `ITypeDefinition` (the C# `type as ITypeDefinition`); if the definition's `KnownTypeCode`
// is `<= String` and not `Void`, returns `(TypeCode)knownTypeCode` (the numeric cast -- the identity,
// since both enums continue System.TypeCode's numbering: String=18 in both, no member at 17);
// else `Empty`.
TypeCode GetTypeCode(const IType& type);

// The C# `public static IType FindType(this ICompilation compilation, TypeCode typeCode)`
// (ReflectionHelper.cs line 106) -- the built-in-type lookup by `System.TypeCode`: a faithful
// `static_cast<KnownTypeCode>` (the identity cast -- both enums continue System.TypeCode's
// numbering, String=18 in both, no member at 17; `None` <-> `Empty`) forwarded to
// `ICompilation.FindType(KnownTypeCode)`. The CSharpOperators
// parameter tables (`InitParameterArrays`) are built through this lookup. A NON-NULL
// reference return (the `ICompilation::FindType` contract, mirrored by the delegation).
const IType& FindType(const ICompilation& compilation, TypeCode typeCode);

// The C# `public static string SplitTypeParameterCountFromReflectionName(string reflectionName)`
// (ReflectionHelper.cs line 66) -- strip everything from the LAST '`' onward, unconditionally
// (no digits check: "A`1`B" -> "A`1", "Foo`bar" -> "Foo").
std::string SplitTypeParameterCountFromReflectionName(std::string_view reflectionName);

// The C# `public static string SplitTypeParameterCountFromReflectionName(string reflectionName,
// out int typeParameterCount)` (ReflectionHelper.cs line 83) -- strip the trailing "`N"
// ONLY when the text after the last '`' parses as an integer (then the count is that
// integer, possibly negative); otherwise the name is returned whole and the count is 0
// (the C# int.TryParse sets the out parameter to 0 on failure). Delegates to the existing
// TopLevelTypeName::SplitTypeParameterCount (the same semantics, mirrored for the
// reflection-name ctor); "A`1`B" -> whole + 0 (the tail "B" does not parse).
std::string SplitTypeParameterCountFromReflectionName(std::string_view reflectionName,
                                                       int& typeParameterCount);

// The C# `public static IType ParseReflectionName(string reflectionTypeName, ITypeResolveContext
// resolveContext)` (ReflectionHelper.cs lines 131-155) -- parses a reflection name into a type
// reference and resolves it against the context: `TypeName.TryParse` over the name (the
// iteration-48 `System.Reflection.Metadata` parser port), and on failure a
// `ReflectionNameParseException(0, "Invalid type name: " + name)`; then the private
// `ResolveTypeName` arm chain over the parsed tree:
//   * array -> an `ArrayType` of the parsed rank over the resolved element
//     (`GetArrayRank`: 1 for the SZ form, the comma count + 1 otherwise);
//   * byref -> a `ByReferenceType` over the resolved element;
//   * constructed generic -> resolve `GetGenericTypeDefinition()`, return it AS-IS when its
//     `TypeParameterCount` is 0 (the degenerate arm: a non-generic named with args), else a
//     `ParameterizedType` over the resolved arguments (a NULL TAIL entry when the parse carried
//     FEWER arguments than the resolved generic's arity -- the C# binds the internal unchecked
//     `params IType[]` ctor, so no ctor check fires and the null flows into the type; writing
//     PAST the arity instead throws, mapped from the C# `IndexOutOfRangeException`);
//   * nested -> resolve `DeclaringType()` and take its `GetDefinition()`, then scan the
//     declaring type's `NestedTypes` for `Name == plainName && TypeParameterCount ==
//     tpc + declaringType.TypeParameterCount`; the miss (or a null definition) falls back to an
//     `UnknownType(new FullTypeName(result.FullName))` (the nested chain preserved in the name);
//   * pointer -> a `PointerType` over the resolved element;
//   * simple -> the `N / ``N type-parameter arms (the wired `CurrentMember`/`CurrentTypeDefinition`
//     slots when the index is in range, `DummyTypeParameter.GetMethodTypeParameter` /
//     `GetClassTypeParameter` otherwise), then `new TopLevelTypeName(result.FullName)`: the
//     assembly-qualified arm (`FindModuleByAssemblyNameInfo` over `result.AssemblyName()`, an
//     in-module hit returning the definition or the `?? new UnknownType(topLevelTypeName)`
//     fallback, and a null module falling through to) the plain walk over
//     `Compilation.Modules()` (first `GetTypeDefinition` hit wins), and the final
//     `new UnknownType(topLevelTypeName)` fallback.
// The returned handle owns the freshly built composites (`make_shared`) and
// snapshot-aliases the definitions the modules hand back (the no-op-deleter
// convention -- the type system owns the entities); the alias is spelled out as
// `std::shared_ptr<IType>` because this header forward-declares `IType` only (the
// `ITypePtr` alias lives in `IType.hpp`). The C# `ArgumentNullException` on a null
// `reflectionTypeName` is N/A in the port (a `string_view` has no null state).
std::shared_ptr<IType> ParseReflectionName(std::string_view reflectionTypeName,
                                            const ITypeResolveContext& resolveContext);

// The C# `internal static int ReadTypeParameterCount(string reflectionTypeName, ref int pos)`
// (ReflectionHelper.cs lines 225-243) -- the digit scan behind the documentation-tree ID-string
// parser (`IdStringProvider.ReadTypeParameterCountFromIdString`, deferred with its consumer):
// consumes the run of ASCII digits at `pos`, then `int.TryParse`s the slice; a failed parse
// (no digits consumed, or an overflow such as a 12-digit run) throws
// `ReflectionNameParseException(pos, "Expected type parameter count")` at the position the
// scan stopped at. On success `pos` is advanced past the digits and the count returned
// (leading zeros allowed: "007" -> 7).
int ReadTypeParameterCount(std::string_view reflectionTypeName, int& pos);

} // namespace ILSpy::Decompiler::TypeSystem
