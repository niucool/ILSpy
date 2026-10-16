// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/TranslationContext.cs -- the context struct
// passed in to `ExpressionBuilder.Visit()` methods. The single `IType TypeHint` field
// carries the expected type during ILAst->C# translation; the C# docs state it is
// `SpecialType.Unknown` when no specific type is expected (the value callers pass;
// the struct's own default is the CLR null -- a nullable-reference lie the nullable
// context does not flag because the struct is consumed only through the visitor
// plumbing that always assigns it).
//
// Port convention: the C# `IType` field (non-null by the doc contract, null by the
// CLR default) ports to a non-owning `const IType*` defaulting to nullptr -- the
// unset state is the pointer-null (the type system owns the types; the C# GC-owned
// reference is a non-owning handle). A caller that wants the documented "no specific
// type expected" value passes the `SpecialType::UnknownType()` null object's address
// (the C# `SpecialType.UnknownType` singleton).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::CSharp {

/// The expected type during ILAst->C# translation; or the `SpecialType::UnknownType`
/// null object when no specific type is expected. Null only while unset.
struct TranslationContext {
    const ILSpy::Decompiler::TypeSystem::IType* TypeHint = nullptr;
};

}  // namespace ILSpy::Decompiler::CSharp
