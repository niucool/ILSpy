// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files in the "Software", to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the TypeSystemAstBuilder "Convert Type" region
// (ICSharpCode.Decompiler/CSharp/Syntax/TypeSystemAstBuilder.cs lines 266-768):
// ConvertType(IType) / ConvertType(FullTypeName) / ConvertTypeHelper (both
// overloads) / TypeMatches / TypeDefMatches / AddTypeArguments / ConvertNamespace
// (both overloads) / IsValidNamespace / AddTypeAnnotation / MakeSimpleType /
// MakeGlobal / MakeMemberType -- the type-reference renderer that turns an IType
// into an AstType tree (the first Convert* region of the class; the attribute /
// constant-value / parameter / entity regions follow in later slices).
//
// The C# `IType.DeclaringType` / `IType.Namespace` / `IType.TypeArguments` reads
// (on the C# IType interface via INamedElement / the AbstractType defaults) are NOT
// on the port's minimal IType surface -- the file-local DeclaringTypeOf /
// NamespaceOf / TypeArgumentsOf helpers below dispatch to the ported accessors
// (IEntity::DeclaringType / INamedElement::Namespace / IType::TypeParameters) for
// the shapes the region reaches, with the AbstractType defaults (null / "" / the
// definition's own type parameters) for the rest.

#include "TypeSystemAstBuilder.hpp"

#include "ComposedType.hpp"
#include "Expressions/BinaryOperatorExpression.hpp"
#include "Expressions/MemberReferenceExpression.hpp"
#include "Expressions/PrimitiveExpression.hpp"
#include "Expressions/TypeReferenceExpression.hpp"
#include "FunctionPointerAstType.hpp"
#include "ParameterDeclaration.hpp"
#include "PrimitiveType.hpp"
#include "Slots.hpp"
#include "TupleAstType.hpp"
#include "TupleTypeElement.hpp"

#include "Comment.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <any>
#include <charconv>
#include <cmath>
#include <typeinfo>
#include <algorithm>
#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The `UnknownType` class and the `UnknownType()` convenience function share the
// name in `TypeSystem` (the C++ tag-vs-ordinary-namespace distinction). The
// using-declaration imports the whole name set so the elaborated-type-specifier
// (`class UnknownType`) in the dynamic_casts below resolves to the CLASS (the
// CSharpResolver.cpp `new class UnknownType` precedent).
using ILSpy::Decompiler::TypeSystem::UnknownType;

namespace {

// The C# `IType.DeclaringType` (IType.cs -- `ITypeDefinition? DeclaringType`, the
// AbstractType default null) for the shapes the Convert Type region reaches:
//   * an `ITypeDefinition` reports its `IEntity::DeclaringType()` (the C#
//     `IType.DeclaringType => DeclaringTypeDefinition` for a type definition);
//   * a `ParameterizedType` delegates to its generic (the C#
//     `ParameterizedType.DeclaringType` -- the port passes the RAW declaring type
//     rather than the C# re-parameterized wrapper; every read the region makes
//     over the result (TypeParameterCount / the nested-type recursion / the
//     fresh-ParameterizedType annotation construction) is observationally
//     identical through either form, a documented deviation);
//   * anything else (UnknownType, the decorated types, ...) is null (the
//     AbstractType default).
const TS::IType* DeclaringTypeOf(const TS::IType& type) {
    if (const auto* def = dynamic_cast<const TS::ITypeDefinition*>(&type)) {
        const TS::ITypePtr& declaringType = def->DeclaringType();
        return declaringType ? declaringType.get() : nullptr;
    }
    if (const auto* pt = dynamic_cast<const TS::ParameterizedType*>(&type)) {
        return pt->GenericType() ? DeclaringTypeOf(*pt->GenericType()) : nullptr;
    }
    return nullptr;
}

// The C# `IType.Namespace` (IType.cs -- via `IType : INamedElement`; the
// AbstractType default is the empty string) for the shapes the region reaches:
// an `IEntity`-carrying type (an ITypeDefinition) reports `INamedElement::
// Namespace()`; an `UnknownType` reports its stored full type name's namespace
// (the C# `UnknownType.Namespace => fullTypeName.TopLevelTypeName.Namespace`);
// anything else is the empty default.
std::string NamespaceOf(const TS::IType& type) {
    if (const auto* entity = dynamic_cast<const TS::IEntity*>(&type))
        return entity->Namespace();
    if (const auto* unknown = dynamic_cast<const class UnknownType*>(&type))
        return unknown->FullTypeName().Namespace();
    return std::string();
}

// The C# `IType.TypeArguments` for a definition-shaped type (the
// `MetadataTypeDefinition`/`UnknownType` override `=> TypeParameters`): the
// declared type parameters as owning ITypePtr handles (the D529 shared_from_this +
// const_pointer_cast convention -- every type parameter is shared-managed).
std::vector<TS::ITypePtr> TypeArgumentsOf(const TS::IType& type) {
    std::vector<TS::ITypePtr> result;
    const auto typeParameters = type.TypeParameters();
    result.reserve(typeParameters.size());
    for (const TS::ITypeParameter* tp : typeParameters) {
        if (tp == nullptr)
            continue; // the D516 null-entry guard
        result.push_back(std::const_pointer_cast<TS::IType>(tp->shared_from_this()));
    }
    return result;
}

// The std::any -> PrimitiveValue "boxing bridge": the C# `object constantValue`
// fed to `new PrimitiveExpression(...)` ports as a std::any over the BCL
// primitives (the D374 convention), while `PrimitiveExpression::Value` is the
// PrimitiveValue variant (the D227 object-Value design). The bridge converts a
// boxed primitive to the matching variant alternative:
//   * the exact-alternative primitives (bool / char / string / float / double /
//     int / uint / long / ulong) map directly;
//   * the SMALL integers (sbyte / byte / short / ushort) WIDEN to the int / uint
//     alternatives -- PrimitiveValue carries no narrow-integer alternatives
//     (the C# engine boxes small integers as int/uint before they reach a
//     PrimitiveExpression; the port's `Util::Cast` returns the narrow types, so
//     the bridge widens -- a documented deviation: `Value` reads int32 5 where
//     the C# would hold a boxed sbyte 5, observationally identical for the
//     rendering the output visitor performs);
//   * the `Util::Decimal` stand-in maps onto the faithful 96-bit `DecimalValue`
//     (the 64-bit stand-in mantissa fills lo/mid, hi stays 0 -- every value the
//     stand-in can hold);
//   * an EMPTY any is the C# `null` (std::monostate);
//   * any other held type has no PrimitiveValue alternative -- the C# could box
//     it, the port cannot, so the bridge throws (the degenerate shape never
//     occurs through the constant-value region: the coercing `Util::Cast` at
//     each entry point guarantees a primitive).
PrimitiveValue ToPrimitiveValue(const std::any& value) {
    if (!value.has_value())
        return PrimitiveValue(std::monostate{});
    const auto& t = value.type();
    if (t == typeid(bool))
        return PrimitiveValue(std::any_cast<bool>(value));
    if (t == typeid(char16_t))
        return PrimitiveValue(std::any_cast<char16_t>(value));
    if (t == typeid(std::string))
        return PrimitiveValue(std::any_cast<std::string>(value));
    if (t == typeid(float))
        return PrimitiveValue(std::any_cast<float>(value));
    if (t == typeid(double))
        return PrimitiveValue(std::any_cast<double>(value));
    if (t == typeid(std::int32_t))
        return PrimitiveValue(std::any_cast<std::int32_t>(value));
    if (t == typeid(std::uint32_t))
        return PrimitiveValue(std::any_cast<std::uint32_t>(value));
    if (t == typeid(std::int64_t))
        return PrimitiveValue(std::any_cast<std::int64_t>(value));
    if (t == typeid(std::uint64_t))
        return PrimitiveValue(std::any_cast<std::uint64_t>(value));
    if (t == typeid(std::int8_t))
        return PrimitiveValue(static_cast<std::int32_t>(std::any_cast<std::int8_t>(value)));
    if (t == typeid(std::uint8_t))
        return PrimitiveValue(static_cast<std::uint32_t>(std::any_cast<std::uint8_t>(value)));
    if (t == typeid(std::int16_t))
        return PrimitiveValue(static_cast<std::int32_t>(std::any_cast<std::int16_t>(value)));
    if (t == typeid(std::uint16_t))
        return PrimitiveValue(static_cast<std::uint32_t>(std::any_cast<std::uint16_t>(value)));
    if (t == typeid(ILSpy::Decompiler::Util::Decimal)) {
        const auto d = std::any_cast<ILSpy::Decompiler::Util::Decimal>(value);
        // NormalizeDecimal's invariant: a non-negative magnitude + the authoritative
        // sign flag, so the lo/mid decomposition below is sign-free.
        DecimalValue dv;
        const auto magnitude = static_cast<std::uint64_t>(d.mantissa);
        dv.lo = static_cast<std::uint32_t>(magnitude & 0xFFFFFFFFu);
        dv.mid = static_cast<std::uint32_t>((magnitude >> 32) & 0xFFFFFFFFu);
        dv.hi = 0;
        dv.isNegative = d.isNegative;
        dv.scale = d.scale;
        return PrimitiveValue(dv);
    }
    throw std::runtime_error(
        "PrimitiveExpression cannot hold the boxed constant value type");
}

// The C# `((double)constantValue).ToString("r")` (and the float twin) -- the
// shortest round-trip floating-point form. .NET Core 3.0+ `"r"` IS the
// shortest round-trip representation, which is exactly what C++ `std::to_chars`
// (the general format, no format char) produces: the shortest string that
// parses back to the same value (the DefaultParameter::ToString to_chars
// precedent).
std::string ShortestRoundTrip(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

std::string ShortestRoundTrip(float value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

// The C# `const int MAX_DENOMINATOR_DOUBLE = 1000` / `MAX_DENOMINATOR_FLOAT =
// 360` (TypeSystemAstBuilder.cs lines 1496-1497) -- the fraction-search bounds
// `ConvertFloatingPointLiteral` (and the deferred `TryExtractExpression` PI/E
// extraction) pass to `FractionApprox`.
constexpr int kMaxDenominatorDouble = 1000;
constexpr int kMaxDenominatorFloat = 360;

} // namespace

// The C# `public AstType ConvertType(IType type)` (line 268).
AstType* TypeSystemAstBuilder::ConvertType(TS::IType& type) const {
    AstType* astType = ConvertTypeHelper(type);
    AddTypeAnnotation(*astType, type);
    return astType;
}

// The C# `private void AddTypeAnnotation(AstType astType, IType type)` (line 278).
void TypeSystemAstBuilder::AddTypeAnnotation(AstType& astType, TS::IType& type) const {
    if (AddResolveResultAnnotations())
        astType.AddAnnotation(
            std::make_shared<Sem::TypeResolveResult>(type.shared_from_this()));
}

// The C# `public AstType ConvertType(FullTypeName fullTypeName)` (line 283).
AstType* TypeSystemAstBuilder::ConvertType(const TS::FullTypeName& fullTypeName) const {
    if (resolver_) {
        for (const TS::IModule* assembly : resolver_->Compilation().Modules()) {
            if (assembly == nullptr)
                continue; // the D516 null-entry guard
            const TS::ITypeDefinition* def = TS::GetTypeDefinition(*assembly, fullTypeName);
            if (def != nullptr) {
                // The C# `ConvertType(def)` passes the definition as the IType
                // parameter; the const_cast is the established accessor-contract
                // convention (the underlying type-system objects are mutable).
                return ConvertType(
                    *const_cast<TS::IType*>(static_cast<const TS::IType*>(def)));
            }
        }
    }
    const TS::TopLevelTypeName& top = fullTypeName.GetTopLevelTypeName();
    AstType* type;
    if (top.Namespace().empty()) {
        type = MakeSimpleType(top.Name());
    } else {
        type = MakeMemberType(MakeSimpleType(top.Namespace()), top.Name());
    }
    for (int i = 0; i < fullTypeName.NestingLevel(); i++) {
        type = MakeMemberType(type, fullTypeName.GetNestedTypeName(i));
    }
    return type;
}

// The C# `private AstType ConvertTypeHelper(IType type)` (line 313).
AstType* TypeSystemAstBuilder::ConvertTypeHelper(TS::IType& type) const {
    // The C# `type is TypeWithElementType` dispatch -- the port flattens
    // TypeWithElementType (each concrete leaf carries its own Element() accessor),
    // so the arms dynamic_cast to the concrete leaves; the final else (a custom
    // modifier -- "not supported as type in C#") unwraps to the element.
    if (auto* pointerType = dynamic_cast<TS::PointerType*>(&type)) {
        if (pointerType->Element())
            return ConvertType(*pointerType->Element())->MakePointerType();
        return ConvertType(type); // the D516 degenerate-element fallback
    }
    if (auto* arrayType = dynamic_cast<TS::ArrayType*>(&type)) {
        if (!arrayType->Element())
            return ConvertType(type); // the D516 degenerate-element fallback
        AstType* astType = ConvertType(*arrayType->Element())->MakeArrayType(arrayType->Rank());
        // The port's ArrayType carries no nullability field (the documented
        // IType.hpp divergence -- it inherits the Oblivious default), so this arm
        // never fires through the ported ArrayType; kept faithful for a stub that
        // overrides Nullability().
        if (type.Nullability() == TS::Nullability::Nullable)
            return astType->MakeNullableType();
        return astType;
    }
    if (auto* byReferenceType = dynamic_cast<TS::ByReferenceType*>(&type)) {
        if (byReferenceType->Element())
            return ConvertType(*byReferenceType->Element())->MakeRefType();
        return ConvertType(type); // the D516 degenerate-element fallback
    }
    if (auto* modifiedType = dynamic_cast<TS::ModifiedType*>(&type)) {
        // A custom modifier is not supported as a type in C#: render the element.
        if (modifiedType->Element())
            return ConvertType(*modifiedType->Element());
        return ConvertType(type); // the D516 degenerate-element fallback
    }
    if (auto* annotatedType = dynamic_cast<TS::NullabilityAnnotatedType*>(&type)) {
        if (!annotatedType->TypeWithoutAnnotation())
            return ConvertType(type); // the D516 degenerate fallback
        AstType* astType = ConvertType(*annotatedType->TypeWithoutAnnotation());
        if (annotatedType->Nullability() == TS::Nullability::Nullable)
            astType = astType->MakeNullableType();
        return astType;
    }
    if (auto* tuple = dynamic_cast<TS::TupleType*>(&type)) {
        auto* astType = new TupleAstType();
        const auto& elementTypes = tuple->ElementTypes();
        const auto& elementNames = tuple->ElementNames();
        // The C# `Zip` stops at the shorter sequence.
        const std::size_t count = std::min(elementTypes.size(), elementNames.size());
        for (std::size_t i = 0; i < count; i++) {
            auto* element = new TupleTypeElement();
            element->Type(ConvertType(*elementTypes[i]));
            element->Name(elementNames[i]);
            astType->Elements().Add(element);
        }
        return astType;
    }
    if (auto* functionPointerType = dynamic_cast<TS::FunctionPointerType*>(&type)) {
        auto* astType = new FunctionPointerAstType();
        if (functionPointerType->CallingConvention() == TS::SignatureCallingConvention::Unmanaged) {
            astType->HasUnmanagedCallingConvention(true);
        } else if (functionPointerType->CallingConvention()
                   != TS::SignatureCallingConvention::Default) {
            std::string callconvName;
            switch (functionPointerType->CallingConvention()) {
                case TS::SignatureCallingConvention::CDecl:
                    callconvName = "Cdecl";
                    break;
                case TS::SignatureCallingConvention::StdCall:
                    callconvName = "Stdcall";
                    break;
                case TS::SignatureCallingConvention::ThisCall:
                    callconvName = "Thiscall";
                    break;
                case TS::SignatureCallingConvention::FastCall:
                    callconvName = "Fastcall";
                    break;
                case TS::SignatureCallingConvention::VarArgs:
                    callconvName = "Varargs";
                    break;
                default:
                    // The C# `fpt.CallingConvention.ToString()` (the enum
                    // identifier); the port's enum carries no further values, so
                    // the arm is unreachable -- the empty name stands in.
                    callconvName = std::string();
                    break;
            }
            astType->HasUnmanagedCallingConvention(true);
            astType->CallingConventions().Add(new PrimitiveType(std::move(callconvName)));
        }
        for (const TS::ITypePtr& customCallConv : functionPointerType->CustomCallingConventions()) {
            if (!customCallConv)
                continue; // the D516 null-entry guard
            AstType* callConvSyntax;
            const std::string& name = customCallConv->Name();
            if (NamespaceOf(*customCallConv) == "System.Runtime.CompilerServices"
                && name.rfind("CallConv", 0) == 0 && name.length() > 8) {
                callConvSyntax = new PrimitiveType(name.substr(8));
                if (AddResolveResultAnnotations()) {
                    callConvSyntax->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
                        customCallConv->shared_from_this()));
                }
            } else {
                callConvSyntax = ConvertType(*customCallConv);
            }
            astType->CallingConventions().Add(callConvSyntax);
        }
        const auto& parameterTypes = functionPointerType->ParameterTypes();
        const auto& parameterReferenceKinds = functionPointerType->ParameterReferenceKinds();
        for (std::size_t i = 0; i < parameterTypes.size(); i++) {
            auto* paramDecl = new ParameterDeclaration();
            paramDecl->ParameterModifier(
                i < parameterReferenceKinds.size() ? parameterReferenceKinds[i]
                                                   : TS::ReferenceKind::None);
            if (!parameterTypes[i])
                continue; // the D516 null-entry guard
            TS::IType& parameterType = *parameterTypes[i];
            if (paramDecl->ParameterModifier() != TS::ReferenceKind::None) {
                if (auto* brt = dynamic_cast<TS::ByReferenceType*>(&parameterType)) {
                    if (brt->Element()) {
                        paramDecl->Type(ConvertType(*brt->Element()));
                        astType->Parameters().Add(paramDecl);
                        continue;
                    }
                }
            }
            paramDecl->Type(ConvertType(parameterType));
            astType->Parameters().Add(paramDecl);
        }
        if (functionPointerType->ReturnType())
            astType->ReturnType(ConvertType(*functionPointerType->ReturnType()));
        if (functionPointerType->ReturnIsRefReadOnly()) {
            if (auto* composedType = dynamic_cast<ComposedType*>(astType->ReturnType())) {
                if (composedType->HasRefSpecifier())
                    composedType->HasReadOnlySpecifier(true);
            }
        }
        // The C# `ITypeDefinition treatedAs = fpt.GetDefinition()` arm renders the
        // UIntPtr alias the type acts as when FunctionPointers are disabled in the
        // type-system options. The port's FunctionPointerType inherits the nullptr
        // GetDefinition default (the module-dependent Kind/GetDefinition gate is a
        // documented Phase-2 deferral in IType.hpp), so the arm is unreachable
        // through the ported class -- kept faithful for a stub that overrides
        // GetDefinition. The Comment content deviation: the C#
        // `astType.ToString()` (the output-visitor rendering) is deferred with the
        // CSharpOutputVisitor, so the port attaches an empty comment.
        const TS::ITypeDefinition* treatedAs = functionPointerType->GetDefinition();
        if (treatedAs != nullptr) {
            AstType* result = ConvertTypeHelper(
                *const_cast<TS::IType*>(static_cast<const TS::IType*>(treatedAs)));
            result->AddTrailingTrivia(new Comment(std::string(), CommentType::MultiLine));
            return result;
        }
        return astType;
    }
    // The C# else-branch: the `switch (type)` over ITypeDefinition / UnknownType /
    // ParameterizedType / the by-kind default.
    AstType* astType = nullptr;
    const bool isDefinitionOrUnknown =
        dynamic_cast<const TS::ITypeDefinition*>(&type) != nullptr
        || dynamic_cast<const class UnknownType*>(&type) != nullptr;
    if (isDefinitionOrUnknown) {
        if (TS::IsUnbound(type)) {
            if (ShowTypeParametersForUnboundTypes()) {
                // The C# `type.TypeArguments` (the definition's own type
                // parameters, the MetadataTypeDefinition/UnknownType override).
                astType = ConvertTypeHelper(type, TypeArgumentsOf(type));
            } else {
                std::vector<TS::ITypePtr> typeArguments(
                    static_cast<std::size_t>(type.TypeParameterCount()));
                for (TS::ITypePtr& typeArgument : typeArguments)
                    typeArgument = TS::UnboundTypeArgument();
                astType = ConvertTypeHelper(type, typeArguments);
            }
        } else {
            astType = ConvertTypeHelper(type, std::vector<TS::ITypePtr>{});
        }
    } else if (auto* parameterizedType = dynamic_cast<TS::ParameterizedType*>(&type)) {
        if (UseNullableSpecifierForValueTypes()
            && TS::IsKnownType(*parameterizedType, TS::KnownTypeCode::NullableOfT)) {
            // The C# `pt.TypeArguments[0]` indexes unconditionally; the empty
            // guard is the D516 safe fallback for a degenerate zero-argument
            // ParameterizedType (unreachable for real metadata).
            if (!parameterizedType->TypeArguments().empty())
                return ConvertType(*parameterizedType->TypeArguments()[0])->MakeNullableType();
        }
        if (parameterizedType->GenericType()) {
            astType = ConvertTypeHelper(*parameterizedType->GenericType(),
                                        parameterizedType->TypeArguments());
        } else {
            astType = MakeSimpleType(type.Name()); // the D516 degenerate fallback
        }
    } else {
        switch (type.Kind()) {
            case TS::TypeKind::Dynamic:
            case TS::TypeKind::NInt:
            case TS::TypeKind::NUInt:
                astType = new PrimitiveType(type.Name());
                break;
            default:
                astType = MakeSimpleType(type.Name());
                break;
        }
    }
    if (type.Nullability() == TS::Nullability::Nullable) {
        AddTypeAnnotation(*astType, *type.ChangeNullability(TS::Nullability::Oblivious));
        astType = astType->MakeNullableType();
    }
    return astType;
}

// The C# `private AstType ConvertTypeHelper(IType genericType, IReadOnlyList<IType>
// typeArguments)` (line 434).
AstType* TypeSystemAstBuilder::ConvertTypeHelper(
    TS::IType& genericType, const std::vector<TS::ITypePtr>& typeArguments) const {
    const TS::ITypeDefinition* typeDef = genericType.GetDefinition();
    assert(typeDef != nullptr || genericType.Kind() == TS::TypeKind::Unknown);
    assert(static_cast<int>(typeArguments.size()) >= genericType.TypeParameterCount());

    if (UseKeywordsForBuiltinTypes() && typeDef != nullptr) {
        const auto keyword = TS::KnownTypeReference::GetCSharpNameByTypeCode(
            typeDef->KnownTypeCode());
        if (keyword.has_value()) {
            return new PrimitiveType(std::string(*keyword));
        }
    }

    // The number of type parameters belonging to outer classes.
    const TS::IType* declaringType = DeclaringTypeOf(genericType);
    const int outerTypeParameterCount =
        declaringType != nullptr ? declaringType->TypeParameterCount() : 0;

    if (resolver_ && typeDef != nullptr) {
        // Look if there's an alias to the target type.
        if (UseAliases()) {
            for (auto usingScope = resolver_->CurrentUsingScope();
                 usingScope != nullptr;
                 usingScope = usingScope->Parent()) {
                for (const auto& pair : usingScope->UsingAliases()) {
                    auto aliasTypeResolveResult =
                        std::dynamic_pointer_cast<Sem::TypeResolveResult>(pair.second);
                    if (aliasTypeResolveResult) {
                        if (TypeMatches(aliasTypeResolveResult->Type(), *typeDef,
                                        typeArguments))
                            return MakeSimpleType(pair.first);
                    }
                }
            }
        }

        std::vector<TS::ITypePtr> localTypeArguments;
        if (typeDef->TypeParameterCount() > outerTypeParameterCount) {
            const int count = typeDef->TypeParameterCount() - outerTypeParameterCount;
            localTypeArguments.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; i++) {
                // The top-of-function assert guarantees the index is in bounds
                // (typeArguments.Count >= genericType.TypeParameterCount).
                assert(static_cast<std::size_t>(outerTypeParameterCount + i)
                       < typeArguments.size());
                localTypeArguments.push_back(
                    typeArguments[static_cast<std::size_t>(outerTypeParameterCount + i)]);
            }
        }
        auto resolveResult = resolver_->LookupSimpleNameOrTypeName(
            typeDef->Name(), localTypeArguments, NameLookupMode());
        auto typeResolveResult =
            std::dynamic_pointer_cast<Sem::TypeResolveResult>(resolveResult);
        if (!typeResolveResult && localTypeArguments.empty()) {
            resolver_->IsVariableReferenceWithSameType(*resolveResult, typeDef->Name(),
                                                       typeResolveResult);
        }
        if (typeResolveResult) {
            if (!typeResolveResult->IsError()
                && TypeMatches(typeResolveResult->Type(), *typeDef, typeArguments)) {
                // We can use the short type name.
                SimpleType* shortResult = MakeSimpleType(typeDef->Name());
                AddTypeArguments(*shortResult, typeDef->TypeParameters(), typeArguments,
                                 outerTypeParameterCount, typeDef->TypeParameterCount());
                return shortResult;
            }
        }
    }

    if (AlwaysUseShortTypeNames()
        || (typeDef == nullptr && DeclaringTypeOf(genericType) == nullptr)) {
        auto* shortResult = MakeSimpleType(genericType.Name());
        AddTypeArguments(*shortResult, genericType.TypeParameters(), typeArguments,
                         outerTypeParameterCount, genericType.TypeParameterCount());
        return shortResult;
    }
    auto* result = new MemberType();
    if (declaringType != nullptr) {
        // Handle nested types.
        result->Target(ConvertTypeHelper(*const_cast<TS::IType*>(declaringType),
                                         typeArguments));
        // Use correct number of type arguments on the declaring type.
        const TS::IType* declaringTypeForAnnotation = declaringType;
        TS::ITypePtr parameterizedDeclaringType;
        if (outerTypeParameterCount > 0) {
            // The C# `typeArguments.Take(outerTypeParameterCount)` stops at the
            // shorter sequence.
            const std::size_t takeCount = std::min(
                static_cast<std::size_t>(outerTypeParameterCount), typeArguments.size());
            std::vector<TS::ITypePtr> outerTypeArguments(typeArguments.begin(),
                                                         typeArguments.begin() + takeCount);
            parameterizedDeclaringType = std::make_shared<TS::ParameterizedType>(
                std::const_pointer_cast<TS::IType>(declaringType->shared_from_this()),
                std::move(outerTypeArguments));
            declaringTypeForAnnotation = parameterizedDeclaringType.get();
        }
        AddTypeAnnotation(*result->Target(),
                          *const_cast<TS::IType*>(declaringTypeForAnnotation));
    } else {
        // Handle top-level types.
        const std::string namespaceName = NamespaceOf(genericType);
        if (namespaceName.empty()) {
            result->Target(MakeGlobal());
            result->IsDoubleColon(true);
        } else {
            std::shared_ptr<Sem::NamespaceResolveResult> nrr;
            result->Target(ConvertNamespace(
                namespaceName, nrr,
                AlwaysUseGlobal() || namespaceName == genericType.Name()));
        }
    }
    result->MemberName(genericType.Name());
    AddTypeArguments(*result, genericType.TypeParameters(), typeArguments,
                     outerTypeParameterCount, genericType.TypeParameterCount());
    return result;
}

// The C# `private bool TypeMatches(IType type, ITypeDefinition typeDef,
// IReadOnlyList<IType> typeArguments)` (line 596).
bool TypeSystemAstBuilder::TypeMatches(
    const TS::IType& type, const TS::ITypeDefinition& typeDef,
    const std::vector<TS::ITypePtr>& typeArguments) const {
    if (typeDef.TypeParameterCount() == 0) {
        return TypeDefMatches(typeDef, &type);
    }
    if (!TypeDefMatches(typeDef, type.GetDefinition()))
        return false;
    const auto* parameterizedType = dynamic_cast<const TS::ParameterizedType*>(&type);
    if (parameterizedType == nullptr) {
        // The C# `typeArguments.All(t => t.Kind == TypeKind.UnboundTypeArgument)`
        // (an All over an empty list is true).
        for (const TS::ITypePtr& typeArgument : typeArguments) {
            if (!typeArgument || typeArgument->Kind() != TS::TypeKind::UnboundTypeArgument)
                return false;
        }
        return true;
    }
    const auto& typeArgumentsOfParameterized = parameterizedType->TypeArguments();
    for (std::size_t i = 0; i < typeArgumentsOfParameterized.size(); i++) {
        // The C# indexes typeArguments[i] unconditionally (an out-of-range index
        // throws IndexOutOfRangeException); the bounds guard is the D516 safe
        // fallback for a degenerate shorter list.
        if (i >= typeArguments.size() || !typeArguments[i])
            return false;
        if (!typeArgumentsOfParameterized[i]->Equals(*typeArguments[i]))
            return false;
    }
    return true;
}

// The C# `private bool TypeDefMatches(ITypeDefinition typeDef, IType? type)`
// (line 617).
bool TypeSystemAstBuilder::TypeDefMatches(const TS::ITypeDefinition& typeDef,
                                          const TS::IType* type) const {
    if (type == nullptr || type->Name() != typeDef.Name()
        || NamespaceOf(*type) != typeDef.Namespace()
        || type->TypeParameterCount() != typeDef.TypeParameterCount())
        return false;
    const bool defIsNested = typeDef.DeclaringTypeDefinition() != nullptr;
    const TS::IType* typeDeclaringType = DeclaringTypeOf(*type);
    const bool typeIsNested = typeDeclaringType != nullptr;
    if (defIsNested && typeIsNested)
        return TypeDefMatches(*typeDef.DeclaringTypeDefinition(), typeDeclaringType);
    return defIsNested == typeIsNested;
}

// The C# `private void AddTypeArguments(AstType result, IReadOnlyList<ITypeParameter>
// typeParameters, IReadOnlyList<IType> typeArguments, int startIndex, int endIndex)`
// (line 636).
void TypeSystemAstBuilder::AddTypeArguments(
    AstType& result, const std::vector<const TS::ITypeParameter*>& typeParameters,
    const std::vector<TS::ITypePtr>& typeArguments, int startIndex, int endIndex) const {
    assert(endIndex <= static_cast<int>(typeParameters.size()));
    for (int i = startIndex; i < endIndex; i++) {
        assert(static_cast<std::size_t>(i) < typeParameters.size());
        assert(static_cast<std::size_t>(i) < typeArguments.size());
        const TS::ITypePtr& typeArgument = typeArguments[static_cast<std::size_t>(i)];
        if (ConvertUnboundTypeArguments() && typeArgument
            && typeArgument->Kind() == TS::TypeKind::UnboundTypeArgument) {
            result.AddChild(
                MakeSimpleType(typeParameters[static_cast<std::size_t>(i)]->Name()),
                &Slots::TypeArgument);
        } else {
            result.AddChild(ConvertType(*typeArgument), &Slots::TypeArgument);
        }
    }
}

// The C# `public AstType ConvertNamespace(string namespaceName, out
// NamespaceResolveResult? nrr)` (line 663).
AstType* TypeSystemAstBuilder::ConvertNamespace(
    const std::string& namespaceName,
    std::shared_ptr<Sem::NamespaceResolveResult>& nrr) const {
    return ConvertNamespace(namespaceName, nrr, /*requiresGlobalPrefix*/ false);
}

// The C# `private AstType ConvertNamespace(string namespaceName, out
// NamespaceResolveResult? nrr, bool requiresGlobalPrefix)` (line 668).
AstType* TypeSystemAstBuilder::ConvertNamespace(
    const std::string& namespaceName,
    std::shared_ptr<Sem::NamespaceResolveResult>& nrr, bool requiresGlobalPrefix) const {
    nrr = nullptr;
    if (resolver_) {
        // Look if there's an alias to the target namespace.
        if (UseAliases()) {
            for (auto usingScope = resolver_->CurrentUsingScope();
                 usingScope != nullptr;
                 usingScope = usingScope->Parent()) {
                for (const auto& pair : usingScope->UsingAliases()) {
                    nrr = std::dynamic_pointer_cast<Sem::NamespaceResolveResult>(pair.second);
                    if (nrr && nrr->NamespaceName() == namespaceName) {
                        auto* ns = MakeSimpleType(pair.first);
                        if (AddResolveResultAnnotations())
                            ns->AddAnnotation(nrr);
                        return ns;
                    }
                }
            }
        }
    }

    const std::size_t pos = namespaceName.rfind('.');
    if (pos == std::string::npos) {
        if (IsValidNamespace(namespaceName, nrr)) {
            AstType* ns;
            if (requiresGlobalPrefix) {
                ns = new MemberType();
                static_cast<MemberType*>(ns)->Target(MakeGlobal());
                static_cast<MemberType*>(ns)->IsDoubleColon(true);
                static_cast<MemberType*>(ns)->MemberName(namespaceName);
            } else {
                ns = MakeSimpleType(namespaceName);
            }
            if (AddResolveResultAnnotations() && nrr)
                ns->AddAnnotation(nrr);
            return ns;
        } else {
            auto* ns = new MemberType();
            ns->Target(MakeGlobal());
            ns->IsDoubleColon(true);
            ns->MemberName(namespaceName);
            if (AddResolveResultAnnotations() && resolver_) {
                const TS::INamespace* childNamespace =
                    resolver_->Compilation().RootNamespace().GetChildNamespace(namespaceName);
                if (childNamespace != nullptr) {
                    nrr = std::make_shared<Sem::NamespaceResolveResult>(childNamespace);
                    ns->AddAnnotation(nrr);
                }
            }
            return ns;
        }
    } else {
        std::string parentNamespace = namespaceName.substr(0, pos);
        std::string localNamespace = namespaceName.substr(pos + 1);
        std::shared_ptr<Sem::NamespaceResolveResult> parentNRR;
        AstType* parentNS = ConvertNamespace(parentNamespace, parentNRR, requiresGlobalPrefix);
        auto* ns = new MemberType();
        ns->Target(parentNS);
        ns->MemberName(localNamespace);
        nrr = nullptr;
        if (AddResolveResultAnnotations() && parentNRR && parentNRR->Namespace()) {
            const TS::INamespace* newNamespace =
                parentNRR->Namespace()->GetChildNamespace(localNamespace);
            if (newNamespace != nullptr) {
                nrr = std::make_shared<Sem::NamespaceResolveResult>(newNamespace);
                ns->AddAnnotation(nrr);
            }
        }
        return ns;
    }
}

// The C# `private bool IsValidNamespace(string firstNamespacePart, out
// NamespaceResolveResult? nrr)` (line 733).
bool TypeSystemAstBuilder::IsValidNamespace(
    const std::string& firstNamespacePart,
    std::shared_ptr<Sem::NamespaceResolveResult>& nrr) const {
    nrr = nullptr;
    if (!resolver_)
        return true; // just assume namespaces are valid if we don't have a resolver
    nrr = std::dynamic_pointer_cast<Sem::NamespaceResolveResult>(
        resolver_->ResolveSimpleName(firstNamespacePart, std::vector<TS::ITypePtr>{},
                                     /*isInvocationTarget*/ false));
    return nrr != nullptr && !nrr->IsError() && nrr->NamespaceName() == firstNamespacePart;
}

// The C# `private static SimpleType MakeSimpleType(string name)` (line 746).
SimpleType* TypeSystemAstBuilder::MakeSimpleType(std::string_view name) {
    if (name == "_")
        return new SimpleType("@_");
    return new SimpleType(std::string(name));
}

// The C# `private SimpleType MakeGlobal()` (line 753).
SimpleType* TypeSystemAstBuilder::MakeGlobal() const {
    auto* global = new SimpleType("global");
    if (AddResolveResultAnnotations() && resolver_) {
        global->AddAnnotation(std::make_shared<Sem::NamespaceResolveResult>(
            &resolver_->Compilation().RootNamespace()));
    }
    return global;
}

// The C# `private static MemberType MakeMemberType(AstType target, string name)`
// (line 760).
MemberType* TypeSystemAstBuilder::MakeMemberType(AstType* target, std::string_view name) {
    if (name == "_")
        return new MemberType(target, "@_");
    return new MemberType(target, std::string(name));
}

// ---------------------------------------------------------------------------
// The "Convert Constant Value" SUPPORT region (C# lines 1168-1249 + 1496-1582)
// ---------------------------------------------------------------------------

// The C# `bool IsSpecialConstant(IType expectedType, object constant,
// [NotNullWhen(true)] out Expression? expression)` (line 1168).
bool TypeSystemAstBuilder::IsSpecialConstant(TS::IType& expectedType, const std::any& constant,
                                            Expression*& expression) const {
    expression = nullptr;
    const auto info = TryGetSpecialConstant(constant);
    if (!info.has_value())
        return false;
    // find IType of constant in compilation.
    TS::IType* constantType = &expectedType;
    if (!IsKnownType(expectedType, info->first)) {
        const TS::ICompilation* compilation = nullptr;
        if (resolver_)
            compilation = &resolver_->Compilation();
        else if (const TS::ITypeDefinition* definition = expectedType.GetDefinition())
            compilation = &definition->Compilation();
        if (compilation == nullptr)
            return false;
        // The C# rebinds the local to the FindType result; the port rebinds a
        // non-const pointer (the FindType const-reference accessor feeding the
        // non-const ConvertType / shared_from_this surface, the D515/D517
        // const_cast convention).
        constantType = const_cast<TS::IType*>(&compilation->FindType(info->first));
    }
    // if the field definition cannot be found, do not generate a reference to the field.
    const TS::IField* field = nullptr;
    {
        const std::string& memberName = info->second;
        const auto fields = constantType->GetFields(
            [&memberName](const TS::IField* f) { return f->Name() == memberName; });
        // The C# `.SingleOrDefault()` throws InvalidOperationException when more
        // than one field matches; the port throws std::runtime_error (the
        // GetInlineArrayElementType SingleOrDefault-throw precedent).
        if (fields.size() > 1)
            throw std::runtime_error("IsSpecialConstant: more than one field named '" +
                                     memberName + "' (SingleOrDefault)");
        if (!fields.empty())
            field = fields.front();
    }
    if (!UseSpecialConstants() || field == nullptr) {
        // +Infty, -Infty and NaN, cannot be represented in their encoded form.
        // Use an equivalent arithmetic expression instead.
        // The C# `switch ((double)constant)` over the three table keys ports to the
        // isnan/isinf value comparisons (the TryGetSpecialConstant dispatch
        // convention); the other table keys (MinValue/MaxValue/Epsilon) fall out of
        // the switch and reach the `return false`, faithfully.
        if (info->first == TS::KnownTypeCode::Double) {
            const double v = std::any_cast<double>(constant);
            if (std::isinf(v) && v < 0) {
                // (-1.0 / 0.0)
                auto* left = new PrimitiveExpression(-1.0);
                left->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), -1.0));
                auto* right = new PrimitiveExpression(0.0);
                right->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0));
                expression = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
                expression->AddAnnotation(
                    std::make_shared<Sem::ConstantResolveResult>(
                        constantType->shared_from_this(),
                        -std::numeric_limits<double>::infinity()));
                return true;
            }
            if (std::isinf(v) && v > 0) {
                // (1.0 / 0.0)
                auto* left = new PrimitiveExpression(1.0);
                left->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 1.0));
                auto* right = new PrimitiveExpression(0.0);
                right->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0));
                expression = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
                expression->AddAnnotation(
                    std::make_shared<Sem::ConstantResolveResult>(
                        constantType->shared_from_this(),
                        std::numeric_limits<double>::infinity()));
                return true;
            }
            if (std::isnan(v)) {
                // (0.0 / 0.0)
                auto* left = new PrimitiveExpression(0.0);
                left->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0));
                auto* right = new PrimitiveExpression(0.0);
                right->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0));
                expression = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
                expression->AddAnnotation(
                    std::make_shared<Sem::ConstantResolveResult>(
                        constantType->shared_from_this(),
                        std::numeric_limits<double>::quiet_NaN()));
                return true;
            }
        }
        if (info->first == TS::KnownTypeCode::Single) {
            const float v = std::any_cast<float>(constant);
            if (std::isinf(v) && v < 0) {
                // (-1.0f / 0.0f)
                auto* left = new PrimitiveExpression(-1.0f);
                left->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), -1.0f));
                auto* right = new PrimitiveExpression(0.0f);
                right->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0f));
                expression = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
                expression->AddAnnotation(
                    std::make_shared<Sem::ConstantResolveResult>(
                        constantType->shared_from_this(),
                        -std::numeric_limits<float>::infinity()));
                return true;
            }
            if (std::isinf(v) && v > 0) {
                // (1.0f / 0.0f)
                auto* left = new PrimitiveExpression(1.0f);
                left->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 1.0f));
                auto* right = new PrimitiveExpression(0.0f);
                right->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0f));
                expression = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
                expression->AddAnnotation(
                    std::make_shared<Sem::ConstantResolveResult>(
                        constantType->shared_from_this(),
                        std::numeric_limits<float>::infinity()));
                return true;
            }
            if (std::isnan(v)) {
                // (0.0f / 0.0f)
                auto* left = new PrimitiveExpression(0.0f);
                left->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0f));
                auto* right = new PrimitiveExpression(0.0f);
                right->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    constantType->shared_from_this(), 0.0f));
                expression = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
                expression->AddAnnotation(
                    std::make_shared<Sem::ConstantResolveResult>(
                        constantType->shared_from_this(),
                        std::numeric_limits<float>::quiet_NaN()));
                return true;
            }
        }
        return false;
    }

    // `TypeReferenceExpression.ConvertType(constantType) . info.Member` -- the
    // member reference the caller emits in place of the literal.
    auto* typeReference = new TypeReferenceExpression(ConvertType(*constantType));
    if (AddResolveResultAnnotations())
        typeReference->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
            constantType->shared_from_this()));
    // The C# `WithoutILInstruction()` is the IL-layer fluent wrapper with NO AST
    // effect (the wrapper type is not modeled); `WithRR(rr)` is the
    // `AddAnnotation(rr)` side effect it carries.
    auto* memberReference = new MemberReferenceExpression(typeReference, info->second);
    if (AddResolveResultAnnotations())
        memberReference->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::make_shared<Sem::TypeResolveResult>(constantType->shared_from_this()), field));
    expression = memberReference;
    return true;
}

// The C# `Expression ConvertFloatingPointLiteral(IType type, object constantValue)`
// (line 1499).
Expression* TypeSystemAstBuilder::ConvertFloatingPointLiteral(TS::IType& type,
                                                              const std::any& constantValue) const {
    // Coerce constantValue to either float or double:
    // There are compilers that embed 0 (and possible other values) as int into constant value
    // signatures, even if the expected type is float or double.
    std::any coerced =
        ILSpy::Decompiler::Util::Cast(TS::GetTypeCode(type), constantValue, /*checkForOverflow*/ false);
    const bool isDouble = IsKnownType(type, TS::KnownTypeCode::Double);
    // The C# `ICompilation compilation = type.GetDefinition()!.Compilation;` -- the
    // null-forgiving deref NREs for a definitionless type; the compilation is
    // consumed only by the DEFERRED Math.PI/E extraction arm below, so the port
    // guards the read and lets the fraction/primitive arms proceed (the D516
    // safe-fallback convention -- a definitionless float/double type is a degenerate
    // stub shape the real type system never produces).
    const TS::ICompilation* compilation = nullptr;
    if (const TS::ITypeDefinition* definition = type.GetDefinition())
        compilation = &definition->Compilation();
    (void)compilation; // the sole consumer is the deferred arm

    Expression* expr = nullptr;

    std::string str;
    if (isDouble) {
        const double v = std::any_cast<double>(coerced);
        if (std::floor(v) == v)
            expr = new PrimitiveExpression(ToPrimitiveValue(coerced));
        str = ShortestRoundTrip(v);
    } else {
        const float v = std::any_cast<float>(coerced);
        if (std::floor(v) == v)
            expr = new PrimitiveExpression(ToPrimitiveValue(coerced));
        str = ShortestRoundTrip(v);
    }

    const bool useFraction = static_cast<std::ptrdiff_t>(str.size())
        - (!str.empty() && str[0] == '-' ? 2 : 1) > 5;

    if (useFraction && expr == nullptr) {
        // For fractions not involving PI, use a smaller MAX_DENOMINATOR
        // to avoid coincidences such as (1f/MathF.PI) == (113f/355f)
        const auto [num, den] = isDouble
            ? FractionApprox(std::any_cast<double>(coerced), kMaxDenominatorDouble)
            : FractionApprox(static_cast<double>(std::any_cast<float>(coerced)), 200);
        if (IsValidFraction(num, den) && IsEqual(num, den, coerced, isDouble)
            && std::llabs(den) != 1) {
            auto* left = MakeConstant(type, num);
            auto* right = MakeConstant(type, den);
            expr = new BinaryOperatorExpression(left, BinaryOperatorType::Divide, right);
        }
    }

    // DEFERRED: the `useFraction && expr == nullptr && UseSpecialConstants` arm
    // (C# lines 1542-1557) -- the System.Math / System.MathF PI / E extraction
    // (`TryExtractExpression`, C# lines 1559-1723). Its dependencies are not yet
    // portable: `compilation.FindType(typeof(Math))` resolves through
    // `ParseReflectionName` (the reflection-name parsing machinery),
    // `compilation.FindType(new TopLevelTypeName("System", "MathF"))` through the
    // unported `FindType(ICompilation, FullTypeName)` extension, and the MathF
    // eligibility check through the unported `IsDirectImportOf(IModule)` (the
    // assembly-reference walk). The C# itself falls through to the plain
    // PrimitiveExpression when `TryExtractExpression` yields null, so the
    // deferral is observationally identical for every value except a PI/E
    // rational multiple with a long decimal form (e.g. `Math.PI` itself), a
    // documented divergence pinned by the PiRendersAsPlainPrimitiveExpression
    // test.

    if (expr == nullptr)
        expr = new PrimitiveExpression(ToPrimitiveValue(coerced));

    if (AddResolveResultAnnotations())
        expr->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
            type.shared_from_this(), coerced));

    return expr;
}

// The C# `Expression MakeConstant(IType type, long c)` (line 1578).
Expression* TypeSystemAstBuilder::MakeConstant(TS::IType& type, std::int64_t c) const {
    // The C# boxes the `long` argument and casts it through the type's TypeCode with
    // overflow CHECKING (a fraction term outside the target's range throws, the C#
    // OverflowException -> Util::OverflowException).
    return new PrimitiveExpression(ToPrimitiveValue(
        ILSpy::Decompiler::Util::Cast(TS::GetTypeCode(type), std::any(c), /*checkForOverflow*/ true)));
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
