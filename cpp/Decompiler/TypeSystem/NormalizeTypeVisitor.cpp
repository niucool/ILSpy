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

// Out-of-line members of `NormalizeTypeVisitor` (see the header). Defined here
// because the bodies name members of `ITypeDefinition` / `ITypeParameter` /
// `Implementation::DummyTypeParameter` (which this header forward-declares via
// `TypeVisitor.hpp`) -- the same include-cycle convention as `TypeVisitor.cpp`.

#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"

#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"

namespace ILSpy::Decompiler::TypeSystem {

NormalizeTypeVisitor& NormalizeTypeVisitor::TypeErasure()
{
    // The C# object-initializer: type erasure (object->dynamic; tuple->underlying;
    // modopt/modreq/nullability/intptr removal) WITHOUT type-parameter replacement.
    static NormalizeTypeVisitor instance = [] {
        NormalizeTypeVisitor v;
        v.ReplaceClassTypeParametersWithDummy = false;
        v.ReplaceMethodTypeParametersWithDummy = false;
        return v;
    }();
    return instance;
}

NormalizeTypeVisitor& NormalizeTypeVisitor::IgnoreNullabilityAndTuples()
{
    static NormalizeTypeVisitor instance = [] {
        NormalizeTypeVisitor v;
        v.ReplaceClassTypeParametersWithDummy = false;
        v.ReplaceMethodTypeParametersWithDummy = false;
        v.DynamicAndObject = false;
        v.IntPtrToNInt = false;
        return v;
    }();
    return instance;
}

NormalizeTypeVisitor& NormalizeTypeVisitor::IgnoreNullability()
{
    static NormalizeTypeVisitor instance = [] {
        NormalizeTypeVisitor v;
        v.ReplaceClassTypeParametersWithDummy = false;
        v.ReplaceMethodTypeParametersWithDummy = false;
        v.DynamicAndObject = false;
        v.IntPtrToNInt = false;
        v.TupleToUnderlyingType = false;
        return v;
    }();
    return instance;
}

bool NormalizeTypeVisitor::EquivalentTypes(IType& a, IType& b)
{
    ITypePtr an = a.AcceptVisitor(*this);
    ITypePtr bn = b.AcceptVisitor(*this);
    return an->Equals(*bn);
}

ITypePtr NormalizeTypeVisitor::VisitTypeParameter(ITypeParameter& type)
{
    if (type.OwnerType() == SymbolKind::Method && ReplaceMethodTypeParametersWithDummy) {
        return Implementation::DummyTypeParameter::GetMethodTypeParameter(type.Index());
    }
    if (type.OwnerType() == SymbolKind::TypeDefinition && ReplaceClassTypeParametersWithDummy) {
        return Implementation::DummyTypeParameter::GetClassTypeParameter(type.Index());
    }
    if (RemoveNullability) {
        // The C# `type is NullabilityAnnotatedTypeParameter natp` arm (reached when
        // an annotated type parameter is visited directly, e.g. through a caller
        // passing the ITypeParameter statically -- an annotated parameter visited
        // via `AcceptVisitor` unwraps in VisitNullabilityAnnotatedType instead).
        if (auto* natp = dynamic_cast<NullabilityAnnotatedTypeParameter*>(&type)) {
            return natp->TypeWithoutAnnotation()->AcceptVisitor(*this);
        }
    }
    return TypeVisitor::VisitTypeParameter(type);
}

ITypePtr NormalizeTypeVisitor::VisitTypeDefinition(ITypeDefinition& type)
{
    switch (type.KnownTypeCode()) {
        case KnownTypeCode::Object:
            if (DynamicAndObject) {
                // The C# normalizes in the opposite direction (object -> dynamic)
                // so no compilation is needed to find the object type.
                auto dynamic = std::make_shared<SpecialType>(TypeKind::Dynamic,
                                                             /*isReferenceType=*/true);
                if (RemoveNullability) {
                    return dynamic;
                }
                return dynamic->ChangeNullability(type.Nullability());
            }
            break;
        case KnownTypeCode::IntPtr:
            if (IntPtrToNInt) {
                return std::make_shared<SpecialType>(TypeKind::NInt, /*isReferenceType=*/false);
            }
            break;
        case KnownTypeCode::UIntPtr:
            if (IntPtrToNInt) {
                return std::make_shared<SpecialType>(TypeKind::NUInt, /*isReferenceType=*/false);
            }
            break;
        default:
            break;
    }
    return TypeVisitor::VisitTypeDefinition(type);
}

ITypePtr NormalizeTypeVisitor::VisitTupleType(TupleType& type)
{
    if (TupleToUnderlyingType) {
        return type.UnderlyingType()->AcceptVisitor(*this);
    }
    return TypeVisitor::VisitTupleType(type);
}

ITypePtr NormalizeTypeVisitor::VisitNullabilityAnnotatedType(NullabilityAnnotatedType& type)
{
    if (RemoveNullability) {
        return type.TypeWithoutAnnotation()->AcceptVisitor(*this);
    }
    return TypeVisitor::VisitNullabilityAnnotatedType(type);
}

ITypePtr NormalizeTypeVisitor::VisitArrayType(ArrayType& type)
{
    if (RemoveNullability) {
        // The C# `base.VisitArrayType(type).ChangeNullability(Nullability.Oblivious)`
        // -- visit the element, then drop any residual annotation.
        return TypeVisitor::VisitArrayType(type)->ChangeNullability(Nullability::Oblivious);
    }
    return TypeVisitor::VisitArrayType(type);
}

ITypePtr NormalizeTypeVisitor::VisitModOpt(ModifiedType& type)
{
    if (RemoveModOpt) {
        return type.Element()->AcceptVisitor(*this);
    }
    return TypeVisitor::VisitModOpt(type);
}

ITypePtr NormalizeTypeVisitor::VisitModReq(ModifiedType& type)
{
    if (RemoveModReq) {
        return type.Element()->AcceptVisitor(*this);
    }
    return TypeVisitor::VisitModReq(type);
}

} // namespace ILSpy::Decompiler::TypeSystem
