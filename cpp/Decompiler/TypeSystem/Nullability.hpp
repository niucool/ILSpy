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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Nullability.cs -- the Nullability
// enum, the three-state annotation the type system carries for every type and
// member once nullable reference types are in play: Oblivious (no annotation,
// the pre-C#-8 default), NotNullable (a `!`-annotated or implicitly non-nullable
// reference), and Nullable (a `?`-annotated reference).
//
// Nullability is a leaf dependency of TypeSystemAstBuilder (the direct
// CSharpAmbience dependency): it compares `type.Nullability` to decide whether
// to append a `?` to a rendered type (TypeSystemAstBuilder lines 323/475), walks
// the NullabilityConstraint on type parameters (lines 2622/2642), and recurses
// through NullabilityAnnotatedType (line 2725). It is also the type of
// ITypeDefinition.NullableContext and ITypeParameter.NullabilityConstraint.
//
// The C# enum is `: byte` with three members in declaration order
// (Oblivious=0, NotNullable=1, Nullable=2). Unlike Accessibility the integer
// order carries no partial-order semantics, so no relational operators or
// helpers are needed -- the consumers compare for equality against the three
// named values, never order them. The port keeps the `: byte` backing and the
// declaration-order values pinned (do not reorder them: ITypeDefinition and
// ITypeParameter default-construct to Oblivious=0, and the consumers switch on
// the named values).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

enum class Nullability : std::uint8_t {
    Oblivious = 0,
    NotNullable = 1,
    Nullable = 2,
};

} // namespace ILSpy::Decompiler::TypeSystem
