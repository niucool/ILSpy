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

// Port of ICSharpCode.Decompiler/TypeSystem/IParameter.cs (the `VarianceModifier` enum). The
// variance of a type parameter (a plain `Invariant` type parameter, a `out` covariant one, or an
// `in` contravariant one); the C# `TypeParameterDeclaration.Variance` scalar carries one of these.
// The C# `: byte` underlying type is a faithful `std::uint8_t`. The member order and values mirror
// the C# exactly (each member's numeric value is its declaration index).
//
// `VarianceModifier` declares NO `Any` member, so the generator's `DoMatchTerm` `hasAny` path
// (which detects an `Any` member by name and emits the `== Any || == o.Field` wildcard) does NOT
// fire for `TypeParameterDeclaration.Variance` -- the `DoMatch` term is the PLAIN
// `this.Variance == o.Variance` (the `DirectionExpression.FieldDirection` D235 / `ReferenceKind`
// D278 no-`Any` precedent applied to a `TypeSystem` enum scalar).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

enum class VarianceModifier : std::uint8_t {
    // The type parameter is not variant (no `in`/`out` modifier) -- the zero value (the C# default
    // for an uninitialized `Variance` property is `VarianceModifier.Invariant`).
    Invariant,
    // The type parameter is covariant (used in output position, declared `out`).
    Covariant,
    // The type parameter is contravariant (used in input position, declared `in`).
    Contravariant,
};

} // namespace ILSpy::Decompiler::TypeSystem
