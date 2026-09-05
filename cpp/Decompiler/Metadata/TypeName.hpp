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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of System.Reflection.Metadata's `TypeName`, `TypeNameParseOptions` and
// the internal `TypeNameParser`/`TypeNameParserHelpers` (all decompiled from
// the .NET 10.0.8 runtime's System.Reflection.Metadata.dll). `TypeName` is the
// .NET 8+ assembly-qualified type-name parser -- the ECMA-335 reflection-name
// grammar with nested types ('+'), array/pointer/byref decorators
// ('[]'/'[*]'/'[,'/'*'/'&'), constructed generics (the single- and
// double-bracket argument forms) and the escape character backslash.
//
// Consumer: `ReflectionHelper.ParseReflectionName` (ICSharpCode.Decompiler/
// TypeSystem/ReflectionHelper.cs) parses every
// BAML TypeInfoRecord's type name through `TryParse` and resolves the parsed
// tree against the compilation -- lifting the `XamlContext.ResolveType`
// deferral. `TypeName.Unescape` and the assembly-part rendering also feed the
// resolution chain.
//
// Every behavior pinned against the real .NET 10 classes through the
// C:\temp-probe\TypeNameProbe public-API probe: the full parse matrix (the
// simple/nested/array/pointer/byref/generic shapes, both bracket forms, the
// escaped-delimiter handling that keeps the backslash inside the stored name,
// the trailing-whitespace-inside-the-name quirk), the accessor contracts and
// exception messages, the MaxNodes bound (the dive counter equals the node
// count, so a parse that would need more than MaxNodes nodes returns false),
// and the Make*/WithAssemblyName renders.
//
// Port conventions (documented divergences from the C#):
//  * The C# GC-shared tree (the `_elementOrGenericType`/`_declaringType`/
//    `_genericArguments` references and the one `AssemblyNameInfo` shared by
//    every node of a parse) ports to `std::shared_ptr` links; a `TypeName`
//    hands out shared ownership of its children.
//  * Exception mapping (the standing repo convention): ArgumentException and
//    ArgumentNullException -> `std::invalid_argument`, InvalidOperationException
//    -> `std::runtime_error`, ArgumentOutOfRangeException -> `std::out_of_range`,
//    OverflowException -> `std::overflow_error`, each carrying the exact .NET
//    message text (the separate `paramName` argument is dropped -- C++
//    exceptions have no parameter-name channel).
//  * The public surface takes and returns UTF-8 `std::string`; parsing and
//    rendering run over UTF-16 code units internally so the .NET unit view
//    (the char.IsWhiteSpace TrimStart, the unit-based `_nestedNameLength`
//    arithmetic) is reproduced exactly. A C# string can carry lone surrogate
//    halves, which the port's UTF-8 boundary cannot express (the standing
//    boundary divergence for every string-taking ported API).
//  * The lazily-materialized string caches (`_name`/`_namespace`/`_fullName`/
//    `_assemblyQualifiedName`) are kept, mirroring the C# mutation order, but
//    their C# reference-identity side effects are not portable: only the
//    null-vs-materialized distinction that feeds the append algorithms is.

#pragma once

#include "Decompiler/Metadata/AssemblyNameInfo.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The C# `public sealed class TypeNameParseOptions` -- the node budget of one
// parse. `MaxNodes` defaults to 20 and must be positive (the setter's
// ArgumentOutOfRangeException, mapped to `std::out_of_range` with the exact
// .NET text).
class TypeNameParseOptions final {
public:
    TypeNameParseOptions() = default;

    std::int32_t MaxNodes() const { return maxNodes_; }
    void SetMaxNodes(std::int32_t value);

private:
    std::int32_t maxNodes_ = 20;
};

// The C# `public sealed class TypeName` -- one parsed (or synthesized) type
// name. One instance represents either a simple/nested name (the
// `_fullName`-backed form), a decorated name (array/pointer/byref: the
// element type plus a rank-or-modifier), or a constructed generic (the
// generic-type-definition plus its arguments). The instance is immutable;
// the Make* family links back to `this`, so the class derives from
// `enable_shared_from_this` (the shared-ownership stand-in for the C# GC
// reference the C# stores directly).
class TypeName final : public std::enable_shared_from_this<TypeName> {
public:
    // The C# `static TypeName Parse(ReadOnlySpan<char>, TypeNameParseOptions?)` --
    // ArgumentException ("The name of the type is invalid.") on failure, or
    // InvalidOperationException ("Maximum node count of {MaxNodes} exceeded.")
    // when the failure was the node bound.
    static std::shared_ptr<TypeName> Parse(std::string_view typeName,
                                           const TypeNameParseOptions* options = nullptr);

    // The C# `static bool TryParse(ReadOnlySpan<char>, out TypeName?,
    // TypeNameParseOptions?)` -- the silent parse; `result` untouched on
    // false. The consumer `ReflectionHelper.ParseReflectionName` uses this
    // form.
    static bool TryParse(std::string_view typeName, std::shared_ptr<TypeName>& result,
                         const TypeNameParseOptions* options = nullptr);

    // The C# `static string Unescape(string name)` -- collapse every escape:
    // a backslash before any character is dropped ("a\\+b" -> "a+b"), a
    // double backslash collapses to one ("a\\\\b" -> "a\\b"), and a trailing
    // lone backslash is kept. (The parser itself does NOT unescape: the
    // stored Name/FullName keep their backslashes, and this member is the
    // separate public conversion.) ArgumentNullException for null maps to
    // `std::invalid_argument` ("Value cannot be null. (Parameter 'name')").
    static std::string Unescape(std::string_view name);

    // The `IsArray`/`IsSZArray`/`IsVariableBoundArrayType` group -- the
    // `_rankOrModifier` field decoded (-1 = SZ array, >= 1 = variable-bound,
    // with the "[*]" form a rank-1 variable-bound array).
    bool IsArray() const;
    bool IsSZArray() const { return rankOrModifier_ == -1; }
    bool IsVariableBoundArrayType() const { return rankOrModifier_ >= 1; }
    bool IsByRef() const { return rankOrModifier_ == -3; }
    bool IsPointer() const { return rankOrModifier_ == -2; }
    bool IsConstructedGenericType() const { return !genericArguments_.empty(); }
    bool IsNested() const { return declaringType_ != nullptr; }
    bool IsSimple() const { return elementOrGenericType_ == nullptr; }

    // The `Name` property -- the name without namespace, assembly name and
    // declaring type, but WITH the decorators ("Int32[]") and WITH any escape
    // backslashes; lazily computed and cached.
    std::string Name() const;

    // The `Namespace` property -- the namespace of the simple element type;
    // InvalidOperationException ("Cannot retrieve the namespace of a nested
    // type.") when the simple element is nested (mapped to
    // `std::runtime_error`).
    std::string Namespace() const;

    // The `FullName` property -- namespace and nested chain ('+') but no
    // assembly name, EXCEPT that generic arguments carry their own assembly
    // names inside the argument brackets (the ECMA reflection-name form).
    // Lazily computed and cached.
    std::string FullName() const;

    // The `AssemblyQualifiedName` property -- the full name plus ", " plus
    // the assembly display name when one is present.
    std::string AssemblyQualifiedName() const;

    // The `AssemblyName` property -- the parsed `AssemblyNameInfo` shared by
    // every node of the parse, or a null pointer when the name was not
    // assembly-qualified.
    const std::shared_ptr<AssemblyNameInfo>& AssemblyName() const { return assemblyName_; }

    // The `DeclaringType` property -- InvalidOperationException ("This
    // operation is only valid on nested types.") when not nested.
    std::shared_ptr<TypeName> DeclaringType() const;

    // The `GetArrayRank` property -- 1 for the SZ form, the parsed rank
    // otherwise; InvalidOperationException ("Must be an array type.") when
    // not an array.
    std::int32_t GetArrayRank() const;

    // The `GetElementType` property -- InvalidOperationException ("This
    // operation is only valid on arrays, pointers and references.") when
    // simple (including nested and constructed-generic names).
    std::shared_ptr<TypeName> GetElementType() const;

    // The `GetGenericTypeDefinition` property -- InvalidOperationException
    // ("This operation is only valid on generic types.") when not a
    // constructed generic.
    std::shared_ptr<TypeName> GetGenericTypeDefinition() const;

    // The `GetGenericArguments` property -- the argument list (empty when not
    // a constructed generic).
    const std::vector<std::shared_ptr<TypeName>>& GetGenericArguments() const
    {
        return genericArguments_;
    }

    // The `GetNodeCount` property -- the total number of TypeName instances
    // describing this name (OverflowException -> `std::overflow_error` past
    // int32 range; only the unbounded Make* chain can reach that).
    std::int32_t GetNodeCount() const;

    // `WithAssemblyName` -- a new simple/nested name carrying the given
    // assembly name (null clears it); InvalidOperationException ("'{FullName}'
    // is not a simple TypeName.") for decorated and constructed names.
    std::shared_ptr<TypeName> WithAssemblyName(
        std::shared_ptr<AssemblyNameInfo> assemblyName) const;

    // The Make* family -- new names decorating this one. `MakeArrayTypeName`
    // throws ArgumentOutOfRangeException for a rank <= 0 (mapped to
    // `std::out_of_range` with the exact .NET text; the documented "less than
    // or equal to 32" bound does not exist in the implementation).
    std::shared_ptr<TypeName> MakeSZArrayTypeName() const;
    std::shared_ptr<TypeName> MakeArrayTypeName(std::int32_t rank) const;
    std::shared_ptr<TypeName> MakePointerTypeName() const;
    std::shared_ptr<TypeName> MakeByRefTypeName() const;
    // InvalidOperationException ("'{FullName}' is not a simple TypeName.")
    // for non-simple names.
    std::shared_ptr<TypeName> MakeGenericTypeName(
        std::vector<std::shared_ptr<TypeName>> typeArguments) const;

private:
    // The C# `private static TypeName MakeElementTypeName(int rankOrModifier)` --
    // the shared body of the four decorator factories (the element link plus
    // the rank-or-modifier).
    std::shared_ptr<TypeName> MakeElementTypeName(std::int32_t rankOrModifier) const;

    // The C# internal ctor (`TypeName(string? fullName, AssemblyNameInfo?
    // assemblyName, TypeName? elementOrGenericType, TypeName? declaringType,
    // Builder? genericTypeArguments, int rankOrModifier, int nestedNameLength)`)
    // -- the single construction point behind every factory below.
    TypeName(std::optional<std::u16string> fullName,
             std::shared_ptr<AssemblyNameInfo> assemblyName,
             std::shared_ptr<TypeName> elementOrGenericType,
             std::shared_ptr<TypeName> declaringType,
             std::vector<std::shared_ptr<TypeName>> genericArguments,
             std::int32_t rankOrModifier = 0, std::int32_t nestedNameLength = -1);

    // The append algorithms (the ValueStringBuilder stand-in operates on
    // UTF-16 units).
    void AppendFullName(std::u16string& builder) const;
    void AppendName(std::u16string& builder) const;

    // The C# readonly fields.
    std::shared_ptr<AssemblyNameInfo> assemblyName_;
    std::shared_ptr<TypeName> elementOrGenericType_;
    std::shared_ptr<TypeName> declaringType_;
    std::vector<std::shared_ptr<TypeName>> genericArguments_;
    std::int32_t rankOrModifier_ = 0;
    std::int32_t nestedNameLength_ = -1;

    // The C# lazy string caches -- `_fullName` is NOT readonly in the C# (the
    // FullName/AssemblyQualifiedName getters assign it), so all four are
    // mutable here too.
    mutable std::optional<std::u16string> fullName_;
    mutable std::optional<std::u16string> name_;
    mutable std::optional<std::u16string> namespace_;
    mutable std::optional<std::u16string> assemblyQualifiedName_;

    friend class TypeNameParser;
};

} // namespace ILSpy::Decompiler::Metadata
