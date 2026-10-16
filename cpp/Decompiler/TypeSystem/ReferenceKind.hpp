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
// OTHERWISE, ARISING FROM OR CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IParameter.cs (the `ReferenceKind` enum). The
// reference kind of an `IParameter` (a plain value, an `out`/`ref`/`in`/`ref readonly`
// parameter); the C# `ParameterDeclaration.ParameterModifier` scalar carries one of these. The
// C# `: byte` underlying type is a faithful `std::uint8_t`. The member order and values mirror the
// C# exactly (each member's numeric value is its declaration index). The C# comment on the enum
// notes the order should match `CSharp.Syntax.FieldDirection` (the ported `FieldDirection` enum in
// `Expressions/DirectionExpression.hpp`), but the values diverge after `In` (`FieldDirection` has
// no `RefReadOnly`), so the port keeps the C# `ReferenceKind` order verbatim.
//
// `ReferenceKind` declares NO `Any` member, so the generator's `DoMatchTerm` `hasAny` path (which
// detects an `Any` member by name and emits the `== Any || == o.Field` wildcard) does NOT fire for
// `ParameterDeclaration.ParameterModifier` -- the `DoMatch` term is the PLAIN
// `this.ParameterModifier == o.ParameterModifier` (the `DirectionExpression.FieldDirection` D235
// no-`Any` precedent applied to a `TypeSystem` enum scalar).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

enum class ReferenceKind : std::uint8_t {
    // A plain value parameter (no modifier) -- the zero value (the C# default for an
    // uninitialized `ParameterModifier` property is `ReferenceKind.None`).
    None,
    // An `out` parameter.
    Out,
    // A `ref` parameter.
    Ref,
    // An `in` parameter.
    In,
    // A `ref readonly` parameter (C# 7.2).
    RefReadOnly,
};

} // namespace ILSpy::Decompiler::TypeSystem
