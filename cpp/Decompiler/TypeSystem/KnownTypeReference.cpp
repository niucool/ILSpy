// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `KnownTypeReference` (see KnownTypeReference.hpp). The static
// table (`Table()`) is a function-local `static const std::array` mirroring the
// C# `static readonly KnownTypeReference?[] knownTypeReferences` -- 60 entries
// indexed by `KnownTypeCode` (sequential, `String == 17`, matching the D271
// `KnownTypeCode` minimal port), with the `None` sentinel at index 0 that `Get`
// never returns. The string literals here are the same metadata the
// `KnownTypeReferenceEntry` table in `KnownTypeCode.cpp` carries (the
// `SignatureDecoder` / `KnownType` CLI path uses that table; this class table
// serves the `ITypeReference`-resolution path and is dead in the CLI call graph).

#include "Decompiler/TypeSystem/KnownTypeReference.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <cassert>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `private KnownTypeReference(...)` ctor. Applies the struct->`ValueType`
// base-type adjustment: a struct's direct base is `System.ValueType`, not
// `System.Object`, so the default `baseType = KnownTypeCode.Object` is corrected
// to `ValueType` for structs (unless an explicit non-`Object` base was given).
KnownTypeReference::KnownTypeReference(KnownTypeCode knownTypeCode, TypeKind typeKind,
                                       std::string_view namespaceName, std::string_view name,
                                       int typeParameterCount, KnownTypeCode baseType)
    : knownTypeCode_(knownTypeCode),
      namespaceName_(namespaceName),
      name_(name),
      typeParameterCount_(typeParameterCount),
      baseType_(baseType),
      typeKind_(typeKind)
{
    if (typeKind == TypeKind::Struct && baseType == KnownTypeCode::Object)
        baseType_ = KnownTypeCode::ValueType;
}

// The C# `static readonly KnownTypeReference?[] knownTypeReferences`. A
// function-local `static const` array (Meyers singleton) built at first use.
// The class is polymorphic (virtual destructor via `ITypeReference`), so the
// array is not `constexpr`; the function-local static is the faithful
// runtime-initialized counterpart. Each entry is constructed via the private
// ctor (accessible from this member function). Index 0 (`None`) is a sentinel
// that `Get` skips (returns `nullptr` for `None`).
const std::array<KnownTypeReference, KnownTypeCodeCount>&
KnownTypeReference::Table()
{
    static const std::array<KnownTypeReference, KnownTypeCodeCount> kTable = {{
        KnownTypeReference(KnownTypeCode::None,    TypeKind::Unknown,  "", "", 0),
        KnownTypeReference(KnownTypeCode::Object,  TypeKind::Class,    "System", "Object", 0, KnownTypeCode::None),
        KnownTypeReference(KnownTypeCode::DBNull,  TypeKind::Class,    "System", "DBNull"),
        KnownTypeReference(KnownTypeCode::Boolean, TypeKind::Struct,   "System", "Boolean"),
        KnownTypeReference(KnownTypeCode::Char,    TypeKind::Struct,   "System", "Char"),
        KnownTypeReference(KnownTypeCode::SByte,   TypeKind::Struct,   "System", "SByte"),
        KnownTypeReference(KnownTypeCode::Byte,    TypeKind::Struct,   "System", "Byte"),
        KnownTypeReference(KnownTypeCode::Int16,   TypeKind::Struct,   "System", "Int16"),
        KnownTypeReference(KnownTypeCode::UInt16,  TypeKind::Struct,   "System", "UInt16"),
        KnownTypeReference(KnownTypeCode::Int32,   TypeKind::Struct,   "System", "Int32"),
        KnownTypeReference(KnownTypeCode::UInt32,  TypeKind::Struct,   "System", "UInt32"),
        KnownTypeReference(KnownTypeCode::Int64,   TypeKind::Struct,   "System", "Int64"),
        KnownTypeReference(KnownTypeCode::UInt64,  TypeKind::Struct,   "System", "UInt64"),
        KnownTypeReference(KnownTypeCode::Single,  TypeKind::Struct,   "System", "Single"),
        KnownTypeReference(KnownTypeCode::Double,  TypeKind::Struct,   "System", "Double"),
        KnownTypeReference(KnownTypeCode::Decimal, TypeKind::Struct,   "System", "Decimal"),
        KnownTypeReference(KnownTypeCode::DateTime, TypeKind::Struct,  "System", "DateTime"),
        KnownTypeReference(KnownTypeCode::String,  TypeKind::Class,    "System", "String"),
        KnownTypeReference(KnownTypeCode::Void,    TypeKind::Void,     "System", "Void", 0, KnownTypeCode::ValueType),
        KnownTypeReference(KnownTypeCode::Type,    TypeKind::Class,    "System", "Type"),
        KnownTypeReference(KnownTypeCode::Array,   TypeKind::Class,    "System", "Array"),
        KnownTypeReference(KnownTypeCode::Attribute, TypeKind::Class,  "System", "Attribute"),
        KnownTypeReference(KnownTypeCode::ValueType, TypeKind::Class,  "System", "ValueType"),
        KnownTypeReference(KnownTypeCode::Enum,   TypeKind::Class,    "System", "Enum", 0, KnownTypeCode::ValueType),
        KnownTypeReference(KnownTypeCode::Delegate, TypeKind::Class,   "System", "Delegate"),
        KnownTypeReference(KnownTypeCode::MulticastDelegate, TypeKind::Class, "System", "MulticastDelegate", 0, KnownTypeCode::Delegate),
        KnownTypeReference(KnownTypeCode::Exception, TypeKind::Class,  "System", "Exception"),
        KnownTypeReference(KnownTypeCode::IntPtr,  TypeKind::Struct,   "System", "IntPtr"),
        KnownTypeReference(KnownTypeCode::UIntPtr, TypeKind::Struct,   "System", "UIntPtr"),
        KnownTypeReference(KnownTypeCode::IEnumerable, TypeKind::Interface, "System.Collections", "IEnumerable"),
        KnownTypeReference(KnownTypeCode::IEnumerator, TypeKind::Interface, "System.Collections", "IEnumerator"),
        KnownTypeReference(KnownTypeCode::IEnumerableOfT, TypeKind::Interface, "System.Collections.Generic", "IEnumerable", 1),
        KnownTypeReference(KnownTypeCode::IEnumeratorOfT, TypeKind::Interface, "System.Collections.Generic", "IEnumerator", 1),
        KnownTypeReference(KnownTypeCode::ICollection, TypeKind::Interface, "System.Collections", "ICollection"),
        KnownTypeReference(KnownTypeCode::ICollectionOfT, TypeKind::Interface, "System.Collections.Generic", "ICollection", 1),
        KnownTypeReference(KnownTypeCode::IList, TypeKind::Interface, "System.Collections", "IList"),
        KnownTypeReference(KnownTypeCode::IListOfT, TypeKind::Interface, "System.Collections.Generic", "IList", 1),
        KnownTypeReference(KnownTypeCode::IReadOnlyCollectionOfT, TypeKind::Interface, "System.Collections.Generic", "IReadOnlyCollection", 1),
        KnownTypeReference(KnownTypeCode::IReadOnlyListOfT, TypeKind::Interface, "System.Collections.Generic", "IReadOnlyList", 1),
        KnownTypeReference(KnownTypeCode::Task, TypeKind::Class, "System.Threading.Tasks", "Task"),
        KnownTypeReference(KnownTypeCode::TaskOfT, TypeKind::Class, "System.Threading.Tasks", "Task", 1, KnownTypeCode::Task),
        KnownTypeReference(KnownTypeCode::ValueTask, TypeKind::Struct, "System.Threading.Tasks", "ValueTask"),
        KnownTypeReference(KnownTypeCode::ValueTaskOfT, TypeKind::Struct, "System.Threading.Tasks", "ValueTask", 1),
        KnownTypeReference(KnownTypeCode::NullableOfT, TypeKind::Struct, "System", "Nullable", 1),
        KnownTypeReference(KnownTypeCode::IDisposable, TypeKind::Interface, "System", "IDisposable"),
        KnownTypeReference(KnownTypeCode::IAsyncDisposable, TypeKind::Interface, "System", "IAsyncDisposable"),
        KnownTypeReference(KnownTypeCode::INotifyCompletion, TypeKind::Interface, "System.Runtime.CompilerServices", "INotifyCompletion"),
        KnownTypeReference(KnownTypeCode::ICriticalNotifyCompletion, TypeKind::Interface, "System.Runtime.CompilerServices", "ICriticalNotifyCompletion"),
        KnownTypeReference(KnownTypeCode::TypedReference, TypeKind::Struct, "System", "TypedReference"),
        KnownTypeReference(KnownTypeCode::IFormattable, TypeKind::Interface, "System", "IFormattable"),
        KnownTypeReference(KnownTypeCode::FormattableString, TypeKind::Class, "System", "FormattableString", 0, KnownTypeCode::IFormattable),
        KnownTypeReference(KnownTypeCode::DefaultInterpolatedStringHandler, TypeKind::Struct, "System.Runtime.CompilerServices", "DefaultInterpolatedStringHandler"),
        KnownTypeReference(KnownTypeCode::SpanOfT, TypeKind::Struct, "System", "Span", 1),
        KnownTypeReference(KnownTypeCode::ReadOnlySpanOfT, TypeKind::Struct, "System", "ReadOnlySpan", 1),
        KnownTypeReference(KnownTypeCode::MemoryOfT, TypeKind::Struct, "System", "Memory", 1),
        KnownTypeReference(KnownTypeCode::Unsafe, TypeKind::Class, "System.Runtime.CompilerServices", "Unsafe"),
        KnownTypeReference(KnownTypeCode::IAsyncEnumerableOfT, TypeKind::Interface, "System.Collections.Generic", "IAsyncEnumerable", 1),
        KnownTypeReference(KnownTypeCode::IAsyncEnumeratorOfT, TypeKind::Interface, "System.Collections.Generic", "IAsyncEnumerator", 1),
        KnownTypeReference(KnownTypeCode::Index, TypeKind::Struct, "System", "Index"),
        KnownTypeReference(KnownTypeCode::Range, TypeKind::Struct, "System", "Range"),
    }};
    return kTable;
}

// The C# `static KnownTypeReference? Get(KnownTypeCode typeCode)` -- the table
// entry for `typeCode`, or `nullptr` for `None` (the C# `null` at index 0). The
// C++ table carries a `None` sentinel at index 0 (no `null`), so `None` is
// special-cased to `nullptr`.
const KnownTypeReference* KnownTypeReference::Get(KnownTypeCode typeCode)
{
    if (typeCode == KnownTypeCode::None)
        return nullptr;
    const auto i = static_cast<std::size_t>(typeCode);
    assert(i < KnownTypeCodeCount);
    return &Table()[i];
}

// The C# `static IEnumerable<KnownTypeReference> AllKnownTypes` -- every
// non-`None` table entry (the 59 known types). The C++ table has no `null` slots
// (the `None` sentinel at index 0 is the only skipped entry), so this yields
// indices 1..59.
std::vector<const KnownTypeReference*> KnownTypeReference::AllKnownTypes()
{
    std::vector<const KnownTypeReference*> result;
    result.reserve(KnownTypeCodeCount - 1);
    for (std::size_t i = 1; i < KnownTypeCodeCount; ++i)
        result.push_back(&Table()[i]);
    return result;
}

// The C# `static string? GetCSharpNameByTypeCode(KnownTypeCode)` -- the C#
// primitive keyword for a primitive known type, or `null` for a non-primitive.
// The C++ port returns `std::optional<std::string_view>` (a view into a
// compile-time literal, or `std::nullopt`).
std::optional<std::string_view>
KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode knownTypeCode)
{
    switch (knownTypeCode)
    {
        case KnownTypeCode::Object:  return "object";
        case KnownTypeCode::Boolean: return "bool";
        case KnownTypeCode::Char:    return "char";
        case KnownTypeCode::SByte:   return "sbyte";
        case KnownTypeCode::Byte:    return "byte";
        case KnownTypeCode::Int16:   return "short";
        case KnownTypeCode::UInt16:  return "ushort";
        case KnownTypeCode::Int32:   return "int";
        case KnownTypeCode::UInt32:  return "uint";
        case KnownTypeCode::Int64:   return "long";
        case KnownTypeCode::UInt64:  return "ulong";
        case KnownTypeCode::Single:  return "float";
        case KnownTypeCode::Double:  return "double";
        case KnownTypeCode::Decimal: return "decimal";
        case KnownTypeCode::String:  return "string";
        case KnownTypeCode::Void:    return "void";
        default: return std::nullopt;
    }
}

// The C# `TopLevelTypeName TypeName => new TopLevelTypeName(namespaceName, name,
// typeParameterCount)` -- the full top-level type name, owning its strings.
TopLevelTypeName KnownTypeReference::TypeName() const
{
    return TopLevelTypeName(std::string(namespaceName_), std::string(name_),
                            typeParameterCount_);
}

// The C# `IType Resolve(ITypeResolveContext context)` -- resolves this reference
// to the `IType` the compilation holds for `knownTypeCode`. Never null (a
// non-null reference return, the `ICompilation::FindType` precedent).
const IType& KnownTypeReference::Resolve(const ITypeResolveContext& context) const
{
    return context.Compilation().FindType(knownTypeCode_);
}

// The C# `override string ToString()` -- the C# primitive keyword for a
// primitive, otherwise `Namespace + "." + Name` (the metadata full name).
std::string KnownTypeReference::ToString() const
{
    if (auto primitive = GetCSharpNameByTypeCode(knownTypeCode_))
        return std::string(*primitive);
    std::string result(namespaceName_);
    result += '.';
    result += std::string(name_);
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem
