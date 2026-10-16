// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/NormalizeTypeVisitor.cs -- the type-rewriting
// `TypeVisitor` that normalizes away differences between types that should compare
// EQUAL for signature purposes: method type parameter identity (replaced with
// `DummyTypeParameter`s by index), object-vs-dynamic (in the OPPOSITE direction,
// object -> dynamic, so no compilation is needed to find the object type),
// tuple-vs-underlying `ValueTuple`, custom modifiers, nint/nuint, and nullability
// annotations. Its main consumer is `ParameterListComparer` (which folds two
// parameters' normalized types with `IType::Equals`); the three named singletons
// (`TypeErasure` / `IgnoreNullabilityAndTuples` / `IgnoreNullability`) serve the
// attribute-argument / type-comparison call sites.
//
// KEY PORT CONVENTIONS:
//  (a) The eight public mutable `bool` option fields port verbatim (public fields,
//      default-initialized to `true`, matching the C# field initializers; the named
//      singletons and `ParameterListComparer` adjust individual fields).
//  (b) The C# `internal static readonly NormalizeTypeVisitor TypeErasure / ...`
//      singletons port to Meyers-singleton accessors (the D398 precedent) returning a
//      non-const reference -- the C# fields stay mutable after construction
//      (`readonly` freezes only the reference).
//  (c) `VisitTypeParameter`'s `type is NullabilityAnnotatedTypeParameter` arm and the
//      `Dynamic.ChangeNullability(type.Nullability)` arm exercise the
//      `NullabilityAnnotatedTypeParameter` landed in `ITypeParameter.hpp` and the
//      `IType::ChangeNullability` virtual landed with this visitor.

#pragma once

#include "Decompiler/TypeSystem/TypeVisitor.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// The normalizing visitor (the C# `sealed class NormalizeTypeVisitor : TypeVisitor`).
// All overrides are out-of-line in the .cpp (they name `ITypeDefinition`,
// `ITypeParameter`, and `Implementation::DummyTypeParameter` members, which this
// header keeps to forward declarations via `TypeVisitor.hpp`).
class NormalizeTypeVisitor final : public TypeVisitor {
public:
    NormalizeTypeVisitor() = default;

    // NormalizeTypeVisitor that does not normalize type parameters, but performs
    // type erasure (object->dynamic; tuple->underlying type). The C#
    // `internal static readonly NormalizeTypeVisitor TypeErasure`.
    static NormalizeTypeVisitor& TypeErasure();
    // The C# `internal static readonly NormalizeTypeVisitor IgnoreNullabilityAndTuples`.
    static NormalizeTypeVisitor& IgnoreNullabilityAndTuples();
    // The C# `internal static readonly NormalizeTypeVisitor IgnoreNullability`.
    static NormalizeTypeVisitor& IgnoreNullability();

    // The C# `public bool EquivalentTypes(IType a, IType b)` -- normalizes both
    // types through this visitor and compares structurally (`IType::Equals`).
    bool EquivalentTypes(IType& a, IType& b);

    // The eight C# option fields (all default `true`). No accessors; the C#
    // fields are public.
    bool RemoveModOpt = true;
    bool RemoveModReq = true;
    bool ReplaceClassTypeParametersWithDummy = true;
    bool ReplaceMethodTypeParametersWithDummy = true;
    bool DynamicAndObject = true;
    bool IntPtrToNInt = true;
    bool TupleToUnderlyingType = true;
    bool RemoveNullability = true;

    // The C# overrides (see the .cpp):
    // VisitTypeParameter    -- dummy replacement by (owner kind, index); unwrap a
    //                          `NullabilityAnnotatedTypeParameter` when removing
    //                          nullability.
    // VisitTypeDefinition   -- object -> dynamic, IntPtr -> nint, UIntPtr -> nuint.
    // VisitTupleType        -- tuple -> underlying ValueTuple type.
    // VisitNullabilityAnnotatedType / VisitArrayType / VisitModOpt / VisitModReq --
    //                          strip annotations/modifiers when removing.
    ITypePtr VisitTypeParameter(ITypeParameter& type) override;
    ITypePtr VisitTypeDefinition(ITypeDefinition& type) override;
    ITypePtr VisitTupleType(TupleType& type) override;
    ITypePtr VisitNullabilityAnnotatedType(NullabilityAnnotatedType& type) override;
    ITypePtr VisitArrayType(ArrayType& type) override;
    ITypePtr VisitModOpt(ModifiedType& type) override;
    ITypePtr VisitModReq(ModifiedType& type) override;
};

} // namespace ILSpy::Decompiler::TypeSystem
