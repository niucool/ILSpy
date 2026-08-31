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

// The out-of-line bodies of the `AstType` `Make*` builders (the hand-written partial in
// ICSharpCode.Decompiler/CSharp/Syntax/AstType.cs lines 75-105, declared in
// cpp/.../Syntax/AstType.hpp). They construct `ComposedType` wrappers, so they live in this
// .cpp (which can include `ComposedType.hpp`): `ComposedType` derives `AstType`, so
// `AstType.hpp` cannot include `ComposedType.hpp` back (the `AstNode.cpp` out-of-line
// precedent for a base-class member needing a derived type).
//
// Ownership: the C# `new ComposedType { ... }` reference return (GC-owned) ports to a raw
// `new`-ed pointer returned to the caller (the D223 non-owning leak model, the
// `Identifier::Create` factory precedent): the caller attaches the returned node to the
// tree via a slot setter (which re-parents but does not take ownership).

#include "Decompiler/CSharp/Syntax/AstType.hpp"

#include "Decompiler/CSharp/Syntax/ComposedType.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public virtual AstType MakePointerType()` (AstType.cs line 75): a pointer type
// is a fresh `ComposedType` wrapping this type, with the pointer run applied through the
// (virtual) `ComposedType.MakePointerType` -- the fresh wrapper carries no array
// specifiers, so the override extends its `PointerRank` in place to 1.
AstType* AstType::MakePointerType() {
    auto* composed = new ComposedType();
    composed->BaseType(this);
    return composed->MakePointerType();
}

// The C# `public virtual AstType MakeArrayType(int rank = 1)` (AstType.cs line 85): an
// array type is a fresh `ComposedType` wrapping this type, with the rank specifier applied
// through the (virtual) `ComposedType.MakeArrayType` -- the fresh wrapper's specifier
// collection is empty, so the override's InsertBefore(null, ...) appends the first
// specifier.
AstType* AstType::MakeArrayType(int rank) {
    auto* composed = new ComposedType();
    composed->BaseType(this);
    return composed->MakeArrayType(rank);
}

// The C# `public AstType MakeNullableType()` (AstType.cs line 93): a nullable type is a
// fresh `ComposedType` wrapping this type with `HasNullableSpecifier = true`. Unlike the
// other three builders this sets the flag on the fresh wrapper DIRECTLY (the C# object
// initializer) and does not dispatch through a virtual -- there is no
// `ComposedType.MakeNullableType` override in the C#, so a `?` always re-wraps.
AstType* AstType::MakeNullableType() {
    auto* composed = new ComposedType();
    composed->BaseType(this);
    composed->HasNullableSpecifier(true);
    return composed;
}

// The C# `public virtual AstType MakeRefType()` (AstType.cs line 101): a C# 7 ref type is
// a fresh `ComposedType` wrapping this type with `HasRefSpecifier = true`. Like
// `MakeNullableType`, the base sets the flag on the fresh wrapper directly (the C# object
// initializer); the IN-PLACE variant is the `ComposedType.MakeRefType` override, reached
// only when the receiver already is the composed node.
AstType* AstType::MakeRefType() {
    auto* composed = new ComposedType();
    composed->BaseType(this);
    composed->HasRefSpecifier(true);
    return composed;
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
