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

#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <array>
#include <cstddef>

namespace ILSpy::Decompiler::TypeSystem {

namespace {
// Ordered exactly as KnownTypeCode so the table is indexable by the enum.
constexpr std::array<KnownTypeReference, 60> kTable = {{
    { KnownTypeCode::None,    TypeKind::Unknown,  "", "", 0 },
    { KnownTypeCode::Object,   TypeKind::Class,    "System", "Object", 0 },
    { KnownTypeCode::DBNull,   TypeKind::Class,    "System", "DBNull", 0 },
    { KnownTypeCode::Boolean,  TypeKind::Struct,   "System", "Boolean", 0 },
    { KnownTypeCode::Char,     TypeKind::Struct,   "System", "Char", 0 },
    { KnownTypeCode::SByte,    TypeKind::Struct,   "System", "SByte", 0 },
    { KnownTypeCode::Byte,     TypeKind::Struct,   "System", "Byte", 0 },
    { KnownTypeCode::Int16,    TypeKind::Struct,   "System", "Int16", 0 },
    { KnownTypeCode::UInt16,   TypeKind::Struct,   "System", "UInt16", 0 },
    { KnownTypeCode::Int32,    TypeKind::Struct,   "System", "Int32", 0 },
    { KnownTypeCode::UInt32,   TypeKind::Struct,   "System", "UInt32", 0 },
    { KnownTypeCode::Int64,    TypeKind::Struct,   "System", "Int64", 0 },
    { KnownTypeCode::UInt64,   TypeKind::Struct,   "System", "UInt64", 0 },
    { KnownTypeCode::Single,   TypeKind::Struct,   "System", "Single", 0 },
    { KnownTypeCode::Double,   TypeKind::Struct,   "System", "Double", 0 },
    { KnownTypeCode::Decimal,  TypeKind::Struct,   "System", "Decimal", 0 },
    { KnownTypeCode::DateTime, TypeKind::Struct,   "System", "DateTime", 0 },
    { KnownTypeCode::String,   TypeKind::Class,    "System", "String", 0 },
    { KnownTypeCode::Void,     TypeKind::Void,     "System", "Void", 0 },
    { KnownTypeCode::Type,     TypeKind::Class,    "System", "Type", 0 },
    { KnownTypeCode::Array,    TypeKind::Class,    "System", "Array", 0 },
    { KnownTypeCode::Attribute,TypeKind::Class,    "System", "Attribute", 0 },
    { KnownTypeCode::ValueType,TypeKind::Class,    "System", "ValueType", 0 },
    { KnownTypeCode::Enum,     TypeKind::Class,    "System", "Enum", 0 },
    { KnownTypeCode::Delegate, TypeKind::Class,    "System", "Delegate", 0 },
    { KnownTypeCode::MulticastDelegate, TypeKind::Class, "System", "MulticastDelegate", 0 },
    { KnownTypeCode::Exception,TypeKind::Class,    "System", "Exception", 0 },
    { KnownTypeCode::IntPtr,   TypeKind::Struct,   "System", "IntPtr", 0 },
    { KnownTypeCode::UIntPtr,  TypeKind::Struct,   "System", "UIntPtr", 0 },
    { KnownTypeCode::IEnumerable, TypeKind::Interface, "System.Collections", "IEnumerable", 0 },
    { KnownTypeCode::IEnumerator, TypeKind::Interface, "System.Collections", "IEnumerator", 0 },
    { KnownTypeCode::IEnumerableOfT, TypeKind::Interface, "System.Collections.Generic", "IEnumerable", 1 },
    { KnownTypeCode::IEnumeratorOfT, TypeKind::Interface, "System.Collections.Generic", "IEnumerator", 1 },
    { KnownTypeCode::ICollection,    TypeKind::Interface, "System.Collections", "ICollection", 0 },
    { KnownTypeCode::ICollectionOfT, TypeKind::Interface, "System.Collections.Generic", "ICollection", 1 },
    { KnownTypeCode::IList,          TypeKind::Interface, "System.Collections", "IList", 0 },
    { KnownTypeCode::IListOfT,       TypeKind::Interface, "System.Collections.Generic", "IList", 1 },
    { KnownTypeCode::IReadOnlyCollectionOfT, TypeKind::Interface, "System.Collections.Generic", "IReadOnlyCollection", 1 },
    { KnownTypeCode::IReadOnlyListOfT,       TypeKind::Interface, "System.Collections.Generic", "IReadOnlyList", 1 },
    { KnownTypeCode::Task,         TypeKind::Class,  "System.Threading.Tasks", "Task", 0 },
    { KnownTypeCode::TaskOfT,      TypeKind::Class,  "System.Threading.Tasks", "Task", 1 },
    { KnownTypeCode::ValueTask,    TypeKind::Struct, "System.Threading.Tasks", "ValueTask", 0 },
    { KnownTypeCode::ValueTaskOfT, TypeKind::Struct, "System.Threading.Tasks", "ValueTask", 1 },
    { KnownTypeCode::NullableOfT,  TypeKind::Struct, "System", "Nullable", 1 },
    { KnownTypeCode::IDisposable,  TypeKind::Interface, "System", "IDisposable", 0 },
    { KnownTypeCode::IAsyncDisposable, TypeKind::Interface, "System", "IAsyncDisposable", 0 },
    { KnownTypeCode::INotifyCompletion, TypeKind::Interface, "System.Runtime.CompilerServices", "INotifyCompletion", 0 },
    { KnownTypeCode::ICriticalNotifyCompletion, TypeKind::Interface, "System.Runtime.CompilerServices", "ICriticalNotifyCompletion", 0 },
    { KnownTypeCode::TypedReference, TypeKind::Struct, "System", "TypedReference", 0 },
    { KnownTypeCode::IFormattable,  TypeKind::Interface, "System", "IFormattable", 0 },
    { KnownTypeCode::FormattableString, TypeKind::Class, "System", "FormattableString", 0 },
    { KnownTypeCode::DefaultInterpolatedStringHandler, TypeKind::Struct, "System.Runtime.CompilerServices", "DefaultInterpolatedStringHandler", 0 },
    { KnownTypeCode::SpanOfT,   TypeKind::Struct, "System", "Span", 1 },
    { KnownTypeCode::ReadOnlySpanOfT, TypeKind::Struct, "System", "ReadOnlySpan", 1 },
    { KnownTypeCode::MemoryOfT, TypeKind::Struct, "System", "Memory", 1 },
    { KnownTypeCode::Unsafe,    TypeKind::Class,  "System.Runtime.CompilerServices", "Unsafe", 0 },
    { KnownTypeCode::IAsyncEnumerableOfT, TypeKind::Interface, "System.Collections.Generic", "IAsyncEnumerable", 1 },
    { KnownTypeCode::IAsyncEnumeratorOfT, TypeKind::Interface, "System.Collections.Generic", "IAsyncEnumerator", 1 },
    { KnownTypeCode::Index,    TypeKind::Struct, "System", "Index", 0 },
    { KnownTypeCode::Range,    TypeKind::Struct, "System", "Range", 0 },
}};
} // namespace

const KnownTypeReference* KnownTypeTable() { return kTable.data(); }
std::size_t KnownTypeTableSize() { return kTable.size(); }

const KnownTypeReference* LookupKnownType(KnownTypeCode code) {
    auto i = static_cast<std::size_t>(code);
    if (i == 0 || i >= kTable.size()) return nullptr; // None has no entry
    return &kTable[i];
}

} // namespace ILSpy::Decompiler::TypeSystem
