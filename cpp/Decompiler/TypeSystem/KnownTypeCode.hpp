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

// Port of ICSharpCode.Decompiler/TypeSystem/KnownTypeReference.cs. A known type
// is one of the framework types the decompiler treats specially (primitives,
// System.Object, System.String, System.Void, the collection interfaces, ...).
// KnownTypeCode indexes a fixed table so the type system can compare types by
// code rather than by name. The table is the full set from the C# source.

#pragma once

#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <cstdint>
#include <string_view>

namespace ILSpy::Decompiler::TypeSystem {

enum class KnownTypeCode : std::uint8_t {
    None,
    Object,
    DBNull,
    Boolean,
    Char,
    SByte,
    Byte,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Single,
    Double,
    Decimal,
    DateTime,
    String,
    Void,
    Type,
    Array,
    Attribute,
    ValueType,
    Enum,
    Delegate,
    MulticastDelegate,
    Exception,
    IntPtr,
    UIntPtr,
    IEnumerable,
    IEnumerator,
    IEnumerableOfT,
    IEnumeratorOfT,
    ICollection,
    ICollectionOfT,
    IList,
    IListOfT,
    IReadOnlyCollectionOfT,
    IReadOnlyListOfT,
    Task,
    TaskOfT,
    ValueTask,
    ValueTaskOfT,
    NullableOfT,
    IDisposable,
    IAsyncDisposable,
    INotifyCompletion,
    ICriticalNotifyCompletion,
    TypedReference,
    IFormattable,
    FormattableString,
    DefaultInterpolatedStringHandler,
    SpanOfT,
    ReadOnlySpanOfT,
    MemoryOfT,
    Unsafe,
    IAsyncEnumerableOfT,
    IAsyncEnumeratorOfT,
    Index,
    Range,
};

// Static description of one known type: its code, kind, namespace, name, and the
// number of type parameters. References are by string_view into compile-time
// string literals, so a KnownTypeReference is trivially copyable.
struct KnownTypeReference {
    KnownTypeCode Code;
    TypeKind Kind;
    std::string_view Namespace;
    std::string_view Name;
    int TypeParameterCount;
};

// The full known-type table, indexed by KnownTypeCode. Order matches the enum
// so `KnownTypeTable()[i]` corresponds to `KnownTypeCode(i)`.
const KnownTypeReference* KnownTypeTable();
std::size_t KnownTypeTableSize();

// Look up the reference for a code, or nullptr for None.
const KnownTypeReference* LookupKnownType(KnownTypeCode code);

} // namespace ILSpy::Decompiler::TypeSystem
