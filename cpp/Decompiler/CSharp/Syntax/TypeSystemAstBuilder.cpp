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
#include "Constraint.hpp"
#include "Expressions/ArrayCreateExpression.hpp"
#include "Expressions/ArrayInitializerExpression.hpp"
#include "Expressions/BinaryOperatorExpression.hpp"
#include "Expressions/CastExpression.hpp"
#include "Expressions/DefaultValueExpression.hpp"
#include "Expressions/ErrorExpression.hpp"
#include "Expressions/IdentifierExpression.hpp"
#include "Expressions/MemberReferenceExpression.hpp"
#include "Expressions/NullReferenceExpression.hpp"
#include "Expressions/ObjectCreateExpression.hpp"
#include "Expressions/PrimitiveExpression.hpp"
#include "Expressions/TypeOfExpression.hpp"
#include "Expressions/TypeReferenceExpression.hpp"
#include "Expressions/UnaryOperatorExpression.hpp"
#include "FunctionPointerAstType.hpp"
#include "MethodDeclaration.hpp"
#include "ParameterDeclaration.hpp"
#include "PrimitiveType.hpp"
#include "Slots.hpp"
#include "Statements/BlockStatement.hpp"
#include "Statements/ThrowStatement.hpp"
#include "TupleAstType.hpp"
#include "TupleTypeElement.hpp"

#include "Comment.hpp"
#include "ConstructorDeclaration.hpp"
#include "CustomEventDeclaration.hpp"
#include "DelegateDeclaration.hpp"
#include "DestructorDeclaration.hpp"
#include "EventDeclaration.hpp"
#include "FieldDeclaration.hpp"
#include "IndexerDeclaration.hpp"
#include "OperatorDeclaration.hpp"
#include "PropertyDeclaration.hpp"
#include "TypeDeclaration.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/Util/Decimal.hpp"
#include "Attribute.hpp"
#include "AttributeSection.hpp"
#include "Constraint.hpp"
#include "ExtensionDeclaration.hpp"
#include "NamespaceDeclaration.hpp"
#include "TypeParameterDeclaration.hpp"
#include "VariableInitializer.hpp"
#include "Statements/VariableDeclarationStatement.hpp"
#include "Expressions/NamedExpression.hpp"
#include "Decompiler/Semantics/InitializedObjectResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"

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
            if (customCallConv->Namespace() == "System.Runtime.CompilerServices"
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
        // GetDefinition. The trailing Comment carries the FUNCTION-POINTER AST's
        // rendering (`astType.ToString()`, the fully-built `astType` above -- NOT the
        // treated-as `result`), the C# shape exactly (the AstNode ToString
        // output-visitor rendering, now landed).
        const TS::ITypeDefinition* treatedAs = functionPointerType->GetDefinition();
        if (treatedAs != nullptr) {
            AstType* result = ConvertTypeHelper(
                *const_cast<TS::IType*>(static_cast<const TS::IType*>(treatedAs)));
            result->AddTrailingTrivia(
                new Comment(astType->ToString(), CommentType::MultiLine));
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
        const std::string namespaceName = genericType.Namespace();
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
        || type->Namespace() != typeDef.Namespace()
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


// ---------------------------------------------------------------------------
// The "Convert Constant Value" CORE region (C# lines 998-1078 + 1306-1480)
// ---------------------------------------------------------------------------

namespace {

// The C# `MetadataTokens.GetRowNumber(entity.MetadataToken)` -- the
// System.Reflection.Metadata helper over the `EntityHandle` the C# reads off
// `IEntity.MetadataToken`: the row number is the LOW 24 BITS of the raw
// metadata token (the high byte carries the metadata-table id; the D381
// raw-token convention IEntity.MetadataToken documents). A sort key only --
// ConvertEnumValue compares row numbers for the declaration order.
constexpr int GetMetadataRowNumber(std::uint32_t metadataToken) {
    return static_cast<int>(metadataToken & 0x00FFFFFFu);
}

// The C# `ErrorResolveResult.UnknownError` as an owning handle for the
// MakeEnumMemberReference target (the empty-owner aliasing shared_ptr over the
// program-lifetime singleton -- the CSharpResolver ErrorResultSingleton
// convention; no deleter runs so the static is never destroyed).
std::shared_ptr<Sem::ResolveResult> UnknownErrorSingleton() {
    return std::shared_ptr<Sem::ResolveResult>(
        std::shared_ptr<Sem::ResolveResult>(),
        const_cast<Sem::ErrorResolveResult*>(&Sem::ErrorResolveResult::UnknownError()));
}

// The C# `internal static IMember MemberForNamedArgument(IType attributeType,
// CustomAttributeNamedArgument<IType> namedArgument)` (CustomAttribute.cs line 113)
// -- the named-argument member lookup: the kind dispatches to the attribute
// type's fields or properties filtered by name, keeping the LAST match (the C#
// `LastOrDefault`, the last-declaration-wins convention). The class the C#
// helper lives on (`CustomAttribute`, the attribute-blob decoder) is otherwise
// unported, so the helper lands file-local next to its only consumer
// (`ConvertAttribute`, the MetadataTokens.GetRowNumber precedent). The return
// is a non-owning pointer (the type system owns the member).
const TS::IMember* MemberForNamedArgument(
    const TS::IType& attributeType, const TS::CustomAttributeNamedArgument& namedArgument) {
    switch (namedArgument.Kind()) {
    case TS::CustomAttributeNamedArgumentKind::Field: {
        const std::string& name = namedArgument.Name();
        std::vector<const TS::IField*> fields = attributeType.GetFields(
            [&name](const TS::IField* field) { return field->Name() == name; });
        return fields.empty() ? nullptr : fields.back();
    }
    case TS::CustomAttributeNamedArgumentKind::Property: {
        const std::string& name = namedArgument.Name();
        std::vector<const TS::IProperty*> properties = attributeType.GetProperties(
            [&name](const TS::IProperty* property) { return property->Name() == name; });
        return properties.empty() ? nullptr : properties.back();
    }
    default:
        return nullptr;
    }
}

// The C# `public static ResolveResult GetResolveResult(this AstNode node)`
// (Annotations.cs line 153) -- the resolve-result annotation lookup with the
// `ErrorResolveResult.UnknownError` singleton fallback (the
// `node.Annotation<ResolveResult>() ?? ErrorResolveResult.UnknownError` shape).
// Only the `ApplyShortAttributeNameIfPossible` namespace-target read needs the
// extension here (the Annotations.cs file itself is unported), so it lands
// file-local (the MemberForNamedArgument convention); the fallback keeps the
// fallback's non-NamespaceResolveResult behavior (the singleton is not a
// NamespaceResolveResult, so the namespace arm simply does not fire).
const Sem::ResolveResult* GetResolveResultOf(const AstNode& node) {
    const Sem::ResolveResult* rr = node.Annotation<Sem::ResolveResult>();
    return rr != nullptr ? rr : UnknownErrorSingleton().get();
}

} // namespace

// The C# `public Expression ConvertConstantValue(ResolveResult rr)` (line 998).
Expression* TypeSystemAstBuilder::ConvertConstantValue(
    std::shared_ptr<Sem::ResolveResult> rr) const {
    using ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType;
    using ILSpy::Decompiler::TypeSystem::IsCSharpSmallIntegerType;
    if (!rr)
        throw std::invalid_argument("TypeSystemAstBuilder::ConvertConstantValue: rr is null");
    bool isBoxing = false;
    if (auto* crr = dynamic_cast<Sem::ConversionResolveResult*>(rr.get())) {
        // unpack ConversionResolveResult if necessary
        // (e.g. a boxing conversion or string->object reference conversion)
        rr = crr->InputShared();
        isBoxing = crr->ConversionProperty() != nullptr
            && crr->ConversionProperty()->IsBoxingConversion();
    }

    if (dynamic_cast<Sem::TypeOfResolveResult*>(rr.get()) != nullptr) {
        auto& torr = static_cast<Sem::TypeOfResolveResult&>(*rr);
        auto* expr =
            new TypeOfExpression(ConvertType(const_cast<TS::IType&>(torr.ReferencedType())));
        if (AddResolveResultAnnotations())
            expr->AddAnnotation(std::move(rr));
        return expr;
    }
    if (auto* acrr = dynamic_cast<Sem::ArrayCreateResolveResult*>(rr.get())) {
        auto* ace = new ArrayCreateExpression();
        ace->Type(ConvertType(const_cast<TS::IType&>(acrr->Type())));
        if (auto* composedType = dynamic_cast<ComposedType*>(ace->Type())) {
            composedType->ArraySpecifiers().MoveTo(ace->AdditionalArraySpecifiers());
            if (!composedType->HasNullableSpecifier() && composedType->PointerRank() == 0)
                ace->Type(composedType->BaseType());
        }

        // The C# `acrr.SizeArguments != null` is provably satisfied (the
        // ArrayCreateResolveResult ctor throws on a null list; the port's vector
        // is non-null by construction), so the port runs the arm on the
        // InitializerElements-absent side alone.
        if (!acrr->InitializerElements().has_value()) {
            if (ArraySpecifier* first = ace->AdditionalArraySpecifiers().FirstOrNull())
                first->Remove();
            std::vector<Expression*> sizeArguments;
            sizeArguments.reserve(acrr->SizeArguments().size());
            for (const auto& sizeArgument : acrr->SizeArguments())
                sizeArguments.push_back(ConvertConstantValue(sizeArgument));
            ace->Arguments().AddRange(sizeArguments);
        }
        if (acrr->InitializerElements().has_value()) {
            std::vector<Expression*> elements;
            elements.reserve(acrr->InitializerElements()->size());
            for (const auto& element : *acrr->InitializerElements())
                elements.push_back(ConvertConstantValue(element));
            auto* initializer = new ArrayInitializerExpression();
            initializer->Elements().AddRange(elements);
            ace->Initializer(initializer);
        }
        if (AddResolveResultAnnotations())
            ace->AddAnnotation(std::move(rr));
        return ace;
    }
    if (rr->IsCompileTimeConstant()) {
        Expression* expr =
            ConvertConstantValue(const_cast<TS::IType&>(rr->Type()), rr->ConstantValue());
        if (isBoxing
            && (IsCSharpSmallIntegerType(&rr->Type())
                || IsCSharpNativeIntegerType(&rr->Type()))) {
            // C# does not have small integer literal types.
            // We need to add a cast so that the integer literal gets boxed as the correct type.
            expr = new CastExpression(ConvertType(const_cast<TS::IType&>(rr->Type())), expr);
            if (AddResolveResultAnnotations())
                expr->AddAnnotation(std::move(rr));
        }
        return expr;
    } else {
        return new ErrorExpression();
    }
}

// The C# `public Expression ConvertConstantValue(IType type, object? constantValue)`
// (line 1073).
Expression* TypeSystemAstBuilder::ConvertConstantValue(TS::IType& type,
                                                       const std::any& constantValue) const {
    return ConvertConstantValue(type, type, constantValue);
}

// The C# `public Expression ConvertConstantValue(IType expectedType, IType type,
// object? constantValue)` (line 1081).
Expression* TypeSystemAstBuilder::ConvertConstantValue(TS::IType& expectedType, TS::IType& type,
                                                       const std::any& constantValue) const {
    using ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType;
    using ILSpy::Decompiler::TypeSystem::IsCSharpPrimitiveIntegerType;
    using ILSpy::Decompiler::TypeSystem::IsCSharpSmallIntegerType;
    // The C# `throw new ArgumentNullException(nameof(type))` is N/A (a reference
    // cannot be null, the D374 convention).
    if (!constantValue.has_value()) {
        // The C# `type.IsReferenceType == true` is the definite check (a lifted
        // `bool? == true` is falsy for an indeterminate null).
        const std::optional<bool> isReferenceType = type.IsReferenceType();
        if ((isReferenceType.has_value() && *isReferenceType)
            || IsKnownType(type, TS::KnownTypeCode::NullableOfT)
            || IsAnyPointer(type.Kind())) {
            auto* expr = new NullReferenceExpression();
            if (AddResolveResultAnnotations())
                expr->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    std::make_shared<TS::SpecialType>(TS::TypeKind::Null, true),
                    std::any{}));
            return expr;
        } else {
            auto* expr = new DefaultValueExpression(ConvertType(type));
            if (AddResolveResultAnnotations())
                expr->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    type.shared_from_this(), std::any{}));
            return expr;
        }
    } else if (const auto* typeofType = std::any_cast<TS::ITypePtr>(&constantValue)) {
        auto* expr = new TypeOfExpression(ConvertType(const_cast<TS::IType&>(**typeofType)));
        if (AddResolveResultAnnotations())
            expr->AddAnnotation(std::make_shared<Sem::TypeOfResolveResult>(
                type.shared_from_this(), *typeofType));
        return expr;
    } else if (const auto* arr =
                   std::any_cast<std::vector<TS::CustomAttributeTypedArgument>>(&constantValue)) {
        // The C# `ImmutableArray<CustomAttributeTypedArgument<IType>>` (a params-array
        // fixed argument of a custom attribute) ports to the any-held
        // `std::vector<CustomAttributeTypedArgument>` (the CustomAttributeTypedArgument.hpp
        // array-case convention).
        const TS::IType* elementType = nullptr;
        TS::ITypePtr unknownElementType;
        if (const auto* arrayType = dynamic_cast<const TS::ArrayType*>(&type)) {
            if (arrayType->Element())
                elementType = arrayType->Element().get();
        }
        if (elementType == nullptr) {
            // The C# `?? SpecialType.UnknownType` null object. Held by the local
            // handle (the UnknownType() convenience allocates per call).
            unknownElementType = TS::UnknownType();
            elementType = unknownElementType.get();
        }
        auto* expr = new ArrayCreateExpression();
        expr->Type(ConvertType(type));
        if (auto* composedType = dynamic_cast<ComposedType*>(expr->Type())) {
            composedType->ArraySpecifiers().MoveTo(expr->AdditionalArraySpecifiers());
            if (!composedType->HasNullableSpecifier() && composedType->PointerRank() == 0)
                expr->Type(composedType->BaseType());
        }
        std::vector<Expression*> elements;
        elements.reserve(arr->size());
        for (const TS::CustomAttributeTypedArgument& e : *arr) {
            // The C# passes `e.Type` into the 3-arg entry whose null check throws
            // ArgumentNullException; the port guards the nullable handle at the
            // call site with the same throw (the D424 convention).
            if (!e.Type())
                throw std::invalid_argument(
                    "TypeSystemAstBuilder::ConvertConstantValue: a typed argument has no "
                    "type");
            // The C# threads the array's element type as the EXPECTED type of
            // each element conversion (the 3-arg argument order: elementType,
            // e.Type, e.Value).
            elements.push_back(ConvertConstantValue(const_cast<TS::IType&>(*elementType),
                                                    const_cast<TS::IType&>(*e.Type()),
                                                    e.Value()));
        }
        auto* initializer = new ArrayInitializerExpression();
        initializer->Elements().AddRange(elements);
        expr->Initializer(initializer);
        return expr;
    } else {
        // The C# rebinds the `constantValue` parameter through the small/native-integer
        // remap below; the port keeps a rebindable local copy (the parameter is a const
        // reference and cannot be rebound).
        std::any boxedValue = constantValue;
        const TS::IType& underlyingType = TS::GetUnderlyingType(type);
        if (underlyingType.Kind() == TS::TypeKind::Enum) {
            return ConvertEnumValue(
                const_cast<TS::IType&>(underlyingType),
                std::any_cast<std::int64_t>(ILSpy::Decompiler::Util::Cast(
                    TS::TypeCode::Int64, boxedValue, /*checkForOverflow*/ false)));
        } else {
            Expression* expr = nullptr;
            if (!(PrintIntegralValuesAsHex() && IsCSharpPrimitiveIntegerType(&underlyingType))
                && IsSpecialConstant(const_cast<TS::IType&>(underlyingType), boxedValue,
                                     expr)) {
                return expr;
            }
            if (IsKnownType(underlyingType, TS::KnownTypeCode::Double)
                || IsKnownType(underlyingType, TS::KnownTypeCode::Single))
                return ConvertFloatingPointLiteral(const_cast<TS::IType&>(underlyingType),
                                                   boxedValue);
            // The C# `IType? literalType = underlyingType` is a lazy reference copy;
            // the port keeps the non-owning pointer and materializes the owning
            // handle only at the annotation site (a stack-local SpecialType would
            // throw bad_weak_ptr on an eager shared_from_this).
            const TS::IType* literalType = &underlyingType;
            const bool integerTypeMismatch = IsCSharpSmallIntegerType(&underlyingType)
                || IsCSharpNativeIntegerType(&underlyingType);
            if (integerTypeMismatch) {
                // C# does not have integer literals of small integer types,
                // use `int` literal instead.
                // It also doesn't have native integer literals, those also use `int`
                // (or `uint` for `nuint`).
                // The C# local is named `unsigned` (a C# keyword-free identifier);
                // the port renames it (`unsigned` is a C++ type keyword).
                const bool isUnsigned = underlyingType.Kind() == TS::TypeKind::NUInt;
                boxedValue = ILSpy::Decompiler::Util::Cast(
                    isUnsigned ? TS::TypeCode::UInt32 : TS::TypeCode::Int32, boxedValue,
                    /*checkForOverflow*/ false);
                const TS::ICompilation* compilation = nullptr;
                if (resolver_)
                    compilation = &resolver_->Compilation();
                else if (const TS::ITypeDefinition* definition = expectedType.GetDefinition())
                    compilation = &definition->Compilation();
                literalType = compilation != nullptr
                    ? &compilation->FindType(
                          isUnsigned ? TS::KnownTypeCode::UInt32 : TS::KnownTypeCode::Int32)
                    : nullptr;
            }
            LiteralFormat format = LiteralFormat::None;
            if (PrintIntegralValuesAsHex()) {
                format = LiteralFormat::HexadecimalNumber;
            }
            expr = new PrimitiveExpression(ToPrimitiveValue(boxedValue), format);
            if (AddResolveResultAnnotations() && literalType != nullptr)
                expr->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
                    const_cast<TS::IType*>(literalType)->shared_from_this(), boxedValue));
            // The C# precedence: `(integerTypeMismatch && !type.Equals(expectedType))
            // || underlyingType.Kind == TypeKind.Unknown`.
            if ((integerTypeMismatch && !type.Equals(expectedType))
                || underlyingType.Kind() == TS::TypeKind::Unknown) {
                expr = new CastExpression(ConvertType(type), expr);
            }
            return expr;
        }
    }
}

// The C# `internal Expression ConvertEnumValue(IType type, long val,
// IField? declaringEnumMember = null)` (line 1306).
Expression* TypeSystemAstBuilder::ConvertEnumValue(TS::IType& type, std::int64_t val,
                                                   const TS::IField* declaringEnumMember) const {
    const TS::ITypeDefinition* enumDefinition = type.GetDefinition();
    const TS::ITypePtr underlyingType =
        enumDefinition != nullptr ? enumDefinition->EnumUnderlyingType() : nullptr;
    if (enumDefinition == nullptr || !underlyingType) {
        // The C# `type.GetDefinition()!` / `enumDefinition.EnumUnderlyingType!`
        // null-forgiving derefs NRE for a definitionless enum-kind type or an enum
        // definition without its underlying primitive (degenerate stub shapes; real
        // metadata always carries both). The port's documented safe fallback (the
        // EnumUnderlyingOrUnknown convention) renders the plain numeric cast directly
        // over the raw value -- the C# tail's `new CastExpression(ConvertType(type),
        // numericExpression)` shape with the unconverted value (the C#
        // unreachable-for-real-metadata path).
        return new CastExpression(ConvertType(type),
                                  new PrimitiveExpression(ToPrimitiveValue(std::any(val))));
    }
    const TS::TypeCode enumBaseTypeCode = TS::GetTypeCode(*underlyingType);
    const bool isFlags = IsFlagsEnum(*enumDefinition);

    // The C# local function `(long value, IField field, int weight)? PrepareConstant(IField)`:
    // the (value, field, weight) triple for every const field with a constant value, in
    // declaration order (a non-const field or a null constant value yields null and is
    // dropped). The Hamming weight is inlined (the C# local function
    // CalculateHammingWeight, see https://en.wikipedia.org/wiki/Hamming_weight).
    struct PreparedConstant {
        std::int64_t value;
        const TS::IField* field;
        int weight;
    };
    std::vector<PreparedConstant> fields;
    for (const TS::IField* field : enumDefinition->Fields()) {
        if (!field->IsConst())
            continue;
        const std::any constantValue = field->GetConstantValue();
        if (!constantValue.has_value())
            continue;
        const std::int64_t value = std::any_cast<std::int64_t>(ILSpy::Decompiler::Util::Cast(
            TS::TypeCode::Int64, constantValue, /*checkForOverflow*/ false));
        std::uint64_t x = static_cast<std::uint64_t>(value);
        x = x - ((x >> 1) & 0x5555555555555555ull);   // put count of each 2 bits into those 2 bits
        x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
        x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0full;   // put count of each 8 bits into those 8 bits
        const int weight = static_cast<int>((x * 0x0101010101010101ull) >> 56);
        fields.push_back({value, field, weight});
    }

    // The C# local function `Expression MakeEnumMemberReference(IField field)` -- the
    // qualified `EnumType.Member` outside an enum member initializer, the unqualified
    // `Member` inside one.
    auto makeEnumMemberReference = [&](const TS::IField* field) -> Expression* {
        if (declaringEnumMember == nullptr) {
            auto* mre = new MemberReferenceExpression(
                new TypeReferenceExpression(ConvertType(type)), field->Name());
            if (AddResolveResultAnnotations())
                // The C# `mre.Target.GetResolveResult()` reads the (un-annotated)
                // target's resolve-result annotation with the UnknownError fallback
                // (Annotations.cs GetResolveResult) -- the fresh TypeReferenceExpression
                // carries no annotation on this path.
                mre->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
                    UnknownErrorSingleton(), field));
            return mre;
        } else {
            auto* ie = new IdentifierExpression(field->Name());
            if (AddResolveResultAnnotations())
                ie->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
                    std::shared_ptr<Sem::ResolveResult>(), field));
            return ie;
        }
    };

    const int declaringTokenRowNumber =
        declaringEnumMember == nullptr
            ? std::numeric_limits<int>::max()
            : GetMetadataRowNumber(declaringEnumMember->MetadataToken());
    for (const PreparedConstant& prepared : fields) {
        // In a [Flags] enum declaration, only reference single-bit members directly:
        // combined values are built from their flag components below (so that
        // e.g. All = Item1 | Item2 | Item3), and zero members stay numeric, because
        // mask-style enums routinely contain several unrelated zero members
        // (e.g. MethodAttributes.PrivateScope/ReuseSlot).
        if (prepared.value == val
            && (declaringEnumMember == nullptr || !isFlags || prepared.weight == 1)) {
            if (prepared.field == declaringEnumMember
                || declaringTokenRowNumber
                       < GetMetadataRowNumber(prepared.field->MetadataToken())) {
                return ConvertConstantValue(const_cast<TS::IType&>(*underlyingType),
                                             ILSpy::Decompiler::Util::Cast(
                                                 enumBaseTypeCode, std::any(val),
                                                 /*checkForOverflow*/ false));
            }
            return makeEnumMemberReference(prepared.field);
        }
    }
    if (isFlags) {
        // The complement of a byte- or ushort-based enum member is computed in int and
        // therefore negative, which an enum member initializer cannot implicitly convert
        // back to the underlying type -- the ~X form would not compile there.
        const bool complementCompiles =
            declaringEnumMember == nullptr
            || (enumBaseTypeCode != TS::TypeCode::Byte
                && enumBaseTypeCode != TS::TypeCode::UInt16);
        std::int64_t enumValue = val;
        Expression* expr = nullptr;
        std::int64_t negatedEnumValue = ~val;
        // limit negatedEnumValue to the appropriate range
        switch (enumBaseTypeCode) {
            case TS::TypeCode::Byte:
            case TS::TypeCode::SByte:
                negatedEnumValue &= static_cast<std::int64_t>(0xFFull);
                break;
            case TS::TypeCode::Int16:
            case TS::TypeCode::UInt16:
                negatedEnumValue &= static_cast<std::int64_t>(0xFFFFull);
                break;
            case TS::TypeCode::Int32:
            case TS::TypeCode::UInt32:
                negatedEnumValue &= static_cast<std::int64_t>(0xFFFFFFFFull);
                break;
            default:
                break;
        }
        Expression* negatedExpr = nullptr;
        // The C# `fields.OrderByDescending(f => f.weight)` -- LINQ OrderBy is STABLE,
        // so equal weights keep their declaration order (the stable_sort with the
        // descending comparator).
        std::vector<const PreparedConstant*> ordered;
        ordered.reserve(fields.size());
        for (const PreparedConstant& f : fields)
            ordered.push_back(&f);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const PreparedConstant* a, const PreparedConstant* b) {
                             return a->weight > b->weight;
                         });
        for (const PreparedConstant* entry : ordered) {
            const std::int64_t fieldValue = entry->value;
            const TS::IField* field = entry->field;
            if (fieldValue == 0 || field == declaringEnumMember)
                continue;   // skip None enum value

            if (declaringTokenRowNumber < GetMetadataRowNumber(field->MetadataToken()))
                continue;

            if ((fieldValue & enumValue) == fieldValue) {
                Expression* fieldExpression = makeEnumMemberReference(field);
                if (expr == nullptr)
                    expr = fieldExpression;
                else
                    expr = new BinaryOperatorExpression(
                        expr, BinaryOperatorType::BitwiseOr, fieldExpression);

                enumValue &= ~fieldValue;
            }
            if (complementCompiles && (fieldValue & negatedEnumValue) == fieldValue) {
                Expression* fieldExpression = makeEnumMemberReference(field);
                if (negatedExpr == nullptr)
                    negatedExpr = fieldExpression;
                else
                    negatedExpr = new BinaryOperatorExpression(
                        negatedExpr, BinaryOperatorType::BitwiseOr, fieldExpression);

                negatedEnumValue &= ~fieldValue;
            }
        }
        // A multi-bit value that lies entirely within a larger, previously declared member
        // is usually a field encoding inside that mask (e.g. TypeAttributes.NestedPrivate
        // within VisibilityMask), not a union of independent flags; keep it numeric.
        const bool isEncodedInEarlierMask =
            declaringEnumMember != nullptr
            && std::any_of(fields.begin(), fields.end(), [&](const PreparedConstant& f) {
                   return f.field != declaringEnumMember
                       && GetMetadataRowNumber(f.field->MetadataToken()) < declaringTokenRowNumber
                       && (f.value & val) == val && f.value != val;
               });
        if (enumValue == 0 && expr != nullptr && !isEncodedInEarlierMask) {
            if (!(negatedEnumValue == 0 && negatedExpr != nullptr
                  && negatedExpr->Descendants().size() < expr->Descendants().size())) {
                return expr;
            }
        }
        if (complementCompiles && negatedEnumValue == 0 && negatedExpr != nullptr) {
            return new UnaryOperatorExpression(negatedExpr, UnaryOperatorType::BitNot);
        }
    }

    Expression* numericExpression = ConvertConstantValue(
        const_cast<TS::IType&>(*underlyingType),
        ILSpy::Decompiler::Util::Cast(enumBaseTypeCode, std::any(val),
                                      /*checkForOverflow*/ false));
    if (declaringEnumMember != nullptr) {
        return numericExpression;
    }
    return new CastExpression(ConvertType(type), numericExpression);
}

// ---------------------------------------------------------------------------
// The "Convert Attribute" + "Convert Attribute Type" regions (C# lines 770-988)
// ---------------------------------------------------------------------------

// The C# `public Attribute ConvertAttribute(IAttribute attribute)` (line 771).
Attribute* TypeSystemAstBuilder::ConvertAttribute(const TS::IAttribute& attribute) const {
    auto* attr = new Attribute();
    // The C# `ConvertAttributeType(attribute.AttributeType)` receives the
    // non-const `IType&` the region's `shared_from_this`-based annotation path
    // needs; the attribute's type is the type-system-owned mutable object behind
    // the interface's const reference (the established const_cast convention).
    auto& attributeType = const_cast<TS::IType&>(attribute.AttributeType());
    attr->Type(ConvertAttributeType(attributeType));

    // The C# `switch (attr.Type)` over the rendered type: the trailing
    // "Attribute" suffix is stripped off the identifier (the C# `id is { } id`
    // non-null pattern ports to `has_value()`; the suffix check implies the
    // length is at least 9, so `Substring(0, Length - 9)` never underflows).
    if (auto* st = dynamic_cast<SimpleType*>(attr->Type())) {
        const std::optional<std::string> id = st->Identifier();
        if (id.has_value() && id->size() >= 9
            && id->compare(id->size() - 9, 9, "Attribute") == 0) {
            st->Identifier(id->substr(0, id->size() - 9));
        }
    } else if (auto* mt = dynamic_cast<MemberType*>(attr->Type())) {
        const std::string memberName = mt->MemberName();
        if (memberName.size() >= 9
            && memberName.compare(memberName.size() - 9, 9, "Attribute") == 0) {
            mt->MemberName(memberName.substr(0, memberName.size() - 9));
        }
    }

    if (AddResolveResultAnnotations() && attribute.Constructor() != nullptr) {
        attr->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), attribute.Constructor()));
    }

    // The C# `attribute.Constructor?.Parameters ?? EmptyList<IParameter>.Instance`.
    const TS::IMethod* constructor = attribute.Constructor();
    const std::vector<const TS::IParameter*> parameters =
        constructor != nullptr ? constructor->Parameters()
                               : std::vector<const TS::IParameter*>{};
    const std::vector<TS::CustomAttributeTypedArgument> fixedArguments =
        attribute.FixedArguments();
    for (std::size_t i = 0; i < fixedArguments.size(); i++) {
        const TS::CustomAttributeTypedArgument& arg = fixedArguments[i];
        const TS::IParameter* p = i < parameters.size() ? parameters[i] : nullptr;
        // The C# `p?.Type ?? arg.Type`: the constructor parameter's type when the
        // position has one, else the argument's own type. Both feeds are non-const
        // `IType&` through the established const_cast convention; `arg.Type` is
        // non-null by the decoder contract (the D354 unguarded-deref convention).
        TS::IType& expectedType = p != nullptr ? const_cast<TS::IType&>(p->Type())
                                               : *arg.Type();
        attr->Arguments().Add(ConvertConstantValue(expectedType, *arg.Type(), arg.Value()));
    }

    const std::vector<TS::CustomAttributeNamedArgument> namedArguments =
        attribute.NamedArguments();
    if (!namedArguments.empty()) {
        auto targetResult = std::make_shared<Sem::InitializedObjectResolveResult>(
            attributeType.shared_from_this());
        for (const TS::CustomAttributeNamedArgument& namedArg : namedArguments) {
            auto* namedArgument = new NamedExpression(
                namedArg.Name(), ConvertConstantValue(*namedArg.Type(), namedArg.Value()));
            if (AddResolveResultAnnotations()) {
                const TS::IMember* member = MemberForNamedArgument(attributeType, namedArg);
                if (member != nullptr) {
                    namedArgument->AddAnnotation(
                        std::make_shared<Sem::MemberResolveResult>(targetResult, member));
                }
            }
            attr->Arguments().Add(namedArgument);
        }
    }

    if (attribute.HasDecodeErrors()) {
        attr->HasArgumentList(true);
        // An ErrorExpression renders purely as its comment, so it appears inside the
        // parentheses without an explicit closing-paren token to anchor a comment child.
        attr->Arguments().Add(new ErrorExpression("Could not decode attribute arguments."));
    }
    return attr;
}

// The C# `internal IEnumerable<AttributeSection> ConvertAttributes(
// IEnumerable<IAttribute> attributes, string? target = null)` (line 824).
std::vector<AttributeSection*> TypeSystemAstBuilder::ConvertAttributes(
    const std::vector<const TS::IAttribute*>& attributes,
    const std::optional<std::string>& target) const {
    std::vector<const TS::IAttribute*> ordered = attributes;
    if (SortAttributes()) {
        // The C# `OrderBy(a => a, new DelegateComparer<IAttribute>((a, b) =>
        // CompareAttribute(a, b)))` is a STABLE sort over the pre-staged
        // `CompareAttribute` free function (TypeSystemAstBuilder.cs line 835, the
        // CompareType / CompareAny statics): `std::stable_sort` preserves the
        // input order for equal keys exactly as `OrderBy` does.
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const TS::IAttribute* a, const TS::IAttribute* b) {
                             return CompareAttribute(*a, *b) < 0;
                         });
    }
    std::vector<AttributeSection*> result;
    result.reserve(ordered.size());
    for (const TS::IAttribute* attribute : ordered) {
        auto* section = new AttributeSection(ConvertAttribute(*attribute));
        if (target.has_value())
            section->AttributeTarget(*target);
        result.push_back(section);
    }
    return result;
}

// The C# `public AstType ConvertAttributeType(IType type)` (line 894).
AstType* TypeSystemAstBuilder::ConvertAttributeType(TS::IType& type) const {
    // The C# `if (type == null) throw new ArgumentNullException` is structurally
    // unreachable through the reference parameter (the D374 convention).
    AstType* astType = ConvertTypeHelper(type);

    std::optional<std::string> shortName;
    const std::string name = type.Name();
    if (name.size() > 9 && name.compare(name.size() - 9, 9, "Attribute") == 0) {
        shortName = name.substr(0, name.size() - 9);
    }
    if (AlwaysUseShortTypeNames()) {
        if (auto* st = dynamic_cast<SimpleType*>(astType)) {
            // The C# `st.Identifier = shortName` assigns a possibly-null short name;
            // `Identifier.CreateIfNotEmpty(null)` clears the token, so the port
            // maps the null to the empty string (the setter's clearing path).
            st->Identifier(shortName.value_or(""));
        } else if (auto* mt = dynamic_cast<MemberType*>(astType)) {
            // The C# `mt.MemberName = shortName!`: the null-forgiving operator lies
            // for a suffix-less qualified name; `MemberName`'s setter always creates
            // a token (an empty name yields an empty-Name token), so the port maps
            // the null to the empty string (the nearest reachable edge).
            mt->MemberName(shortName.value_or(""));
        }
    } else if (resolver_ != nullptr) {
        ApplyShortAttributeNameIfPossible(type, *astType, shortName);
    }
    AddTypeAnnotation(*astType, type);

    return astType;
}

// The C# `private void ApplyShortAttributeNameIfPossible(IType type, AstType
// astType, string? shortName)` (line 926).
void TypeSystemAstBuilder::ApplyShortAttributeNameIfPossible(
    TS::IType& type, AstType& astType, const std::optional<std::string>& shortName) const {
    if (auto* st = dynamic_cast<SimpleType*>(&astType)) {
        std::shared_ptr<Sem::ResolveResult> shortRR;
        // The C# `resolver!` deref: the method is reached only through
        // ConvertAttributeType's `resolver != null` arm.
        std::shared_ptr<Sem::ResolveResult> withExtraAttrSuffix =
            resolver_->LookupSimpleNameOrTypeName(type.Name() + "Attribute",
                                                  std::vector<TS::ITypePtr>{}, NLM::Type);
        if (shortName.has_value()) {
            shortRR = resolver_->LookupSimpleNameOrTypeName(*shortName,
                                                            std::vector<TS::ITypePtr>{},
                                                            NLM::Type);
        }
        // short type is either unknown or not an attribute type -> we can use the short name.
        if (shortRR != nullptr
            && (dynamic_cast<const Sem::UnknownIdentifierResolveResult*>(shortRR.get()) != nullptr
                || !IsAttributeType(*shortRR))) {
            st->Identifier(*shortName);
        } else if (IsAttributeType(*withExtraAttrSuffix)) {
            // typeName + "Attribute" is an attribute type -> we cannot use long type name,
            // add '@' to disable implicit "Attribute" suffix. The C# `'@' + st.Identifier`
            // over a null identifier yields just "@" (the null maps to the empty string).
            st->Identifier("@" + st->Identifier().value_or(""));
        }
    } else if (auto* mt = dynamic_cast<MemberType*>(&astType)) {
        const TS::IType* declaringType = DeclaringTypeOf(type);
        if (declaringType != nullptr) {
            const TS::ITypeDefinition* declaringTypeDef = declaringType->GetDefinition();
            if (declaringTypeDef != nullptr) {
                // The C# `declaringTypeDef.GetNestedTypes(t => t.TypeParameterCount == 0
                // && t.Name == X).Any(IsAttributeType)` -- the same shapeless filter
                // over the two names the two arms read.
                auto anyNestedTypeIsAttributeType = [&](const std::string& nestedName) {
                    const std::vector<TS::ITypePtr> nestedTypes =
                        declaringTypeDef->GetNestedTypes([&nestedName](
                                                             const TS::ITypeDefinition* t) {
                            return t->TypeParameterCount() == 0 && t->Name() == nestedName;
                        });
                    for (const TS::ITypePtr& nestedType : nestedTypes) {
                        if (IsAttributeType(nestedType.get()))
                            return true;
                    }
                    return false;
                };
                if (shortName.has_value()
                    && !anyNestedTypeIsAttributeType(*shortName)) {
                    mt->MemberName(*shortName);
                } else if (anyNestedTypeIsAttributeType(type.Name() + "Attribute")) {
                    mt->MemberName("@" + mt->MemberName());
                }
            }
        } else if (const auto* nrr = dynamic_cast<const Sem::NamespaceResolveResult*>(
                       GetResolveResultOf(*mt->Target()))) {
            // The C# `mt.Target.GetResolveResult()` reads the target's
            // resolve-result annotation with the UnknownError fallback (the
            // GetResolveResultOf file-local helper); the MemberType target is
            // non-null by construction (the unguarded deref, the D354 convention).
            if (shortName.has_value()
                && !IsAttributeType(nrr->Namespace()->GetTypeDefinition(*shortName, 0))) {
                mt->MemberName(*shortName);
            } else if (IsAttributeType(
                           nrr->Namespace()->GetTypeDefinition(type.Name() + "Attribute", 0))) {
                mt->MemberName("@" + mt->MemberName());
            }
        }
    }
}

// The C# `private bool IsAttributeType(IType? type)` (line 979).
bool TypeSystemAstBuilder::IsAttributeType(const TS::IType* type) const {
    if (type == nullptr)
        return false;
    for (const TS::IType* baseType : TS::GetNonInterfaceBaseTypes(*type)) {
        if (TS::IsKnownType(*baseType, TS::KnownTypeCode::Attribute))
            return true;
    }
    return false;
}

// The C# `private bool IsAttributeType(ResolveResult rr)` (line 984).
bool TypeSystemAstBuilder::IsAttributeType(const Sem::ResolveResult& rr) const {
    const auto* trr = dynamic_cast<const Sem::TypeResolveResult*>(&rr);
    return trr != nullptr && IsAttributeType(&trr->Type());
}

// The C# `public ParameterDeclaration ConvertParameter(IParameter parameter)`
// (line 1786).
ParameterDeclaration* TypeSystemAstBuilder::ConvertParameter(
    const TS::IParameter& parameter) const {
    // The C# `throw new ArgumentNullException(nameof(parameter))` is N/A (a
    // reference cannot be null, the D374 convention).
    auto* decl = new ParameterDeclaration();
    decl->ParameterModifier(parameter.ReferenceKind());
    decl->IsParams(parameter.IsParams());
    decl->IsScopedRef(parameter.Lifetime().ScopedRef());
    if (ShowAttributes()) {
        // The C# `decl.Attributes.AddRange(ConvertAttributes(parameter.GetAttributes()))`
        // -- the `AddRange` convenience ports to element-wise `Add` (the D222
        // convention).
        for (AttributeSection* section : ConvertAttributes(parameter.GetAttributes()))
            decl->Attributes().Add(section);
    }
    // The C# rebinds the local `parameterType`: a by-reference parameter type is
    // unwrapped to its element first ("avoid 'out ref'" -- `out ref int x` is not
    // valid C#, the modifier already carries the ref-ness). The C# hard cast
    // `(ByReferenceType)parameter.Type` (after the `Kind == TypeKind.ByReference`
    // gate) ports to the `dynamic_cast<const TS::ByReferenceType&>` -- a
    // Kind-mismatched type would throw `std::bad_cast` exactly like the C#
    // `InvalidCastException` (the UnderlyingTypeForConversion precedent). A
    // degenerate null element (does not occur in practice) keeps the parameter's
    // own type as the safe fallback (the D516 convention; the C# would pass null
    // on to `ConvertType(null)` and throw).
    const TS::IType* parameterType = &parameter.Type();
    if (parameter.Type().Kind() == TS::TypeKind::ByReference) {
        const TS::ByReferenceType& byRef =
            dynamic_cast<const TS::ByReferenceType&>(parameter.Type());
        if (byRef.Element())
            parameterType = byRef.Element().get();
    }
    // `ConvertType` takes `TS::IType&` non-const (the `shared_from_this`-based
    // annotation path, the D529 convention); the parameter's type accessor is
    // const, so the cast (the D515/D517 precedent).
    decl->Type(ConvertType(const_cast<TS::IType&>(*parameterType)));
    if (ShowParameterNames()) {
        decl->Name(parameter.Name());
    }
    if (TS::IsDefaultValueAssignmentAllowed(parameter) && ShowConstantValues()) {
        // The C# `catch (BadImageFormatException ex)` guards the metadata
        // decoder inside `GetConstantValue(throwOnInvalidMetadata: true)` (the
        // `MetadataParameter` blob decode); the port has no
        // `BadImageFormatException` analog and no port-side `GetConstantValue`
        // throws (the `DefaultParameter`/`SpecializedParameter` shapes return the
        // boxed value verbatim), so the arm catches `std::exception` -- the
        // closest message-carrying base -- keeping the structure in place for a
        // future metadata-backed `IParameter` whose `GetConstantValue` throws on
        // invalid metadata (`ErrorExpression(ex.what())` is the `ex.Message`
        // shape).
        try {
            decl->DefaultExpression(ConvertConstantValue(
                const_cast<TS::IType&>(*parameterType),
                parameter.GetConstantValue(/*throwOnInvalidMetadata:*/ true)));
        } catch (const std::exception& ex) {
            decl->DefaultExpression(new ErrorExpression(ex.what()));
        }
    }
    return decl;
}

// The C# `internal TypeParameterDeclaration ConvertTypeParameter(ITypeParameter tp)`
// (line 2602).
TypeParameterDeclaration* TypeSystemAstBuilder::ConvertTypeParameter(
    const TS::ITypeParameter& tp) const {
    auto* decl = new TypeParameterDeclaration();
    decl->Variance(tp.Variance());
    decl->Name(tp.Name());
    if (ShowAttributes()) {
        // The C# `decl.Attributes.AddRange(ConvertAttributes(tp.GetAttributes()))`
        // -- the `AddRange` convenience ports to element-wise `Add` (the D222
        // convention, the ConvertParameter precedent).
        for (AttributeSection* section : ConvertAttributes(tp.GetAttributes()))
            decl->Attributes().Add(section);
    }
    return decl;
}

// The C# `internal Constraint? ConvertTypeParameterConstraint(ITypeParameter tp)`
// (line 2612).
Constraint* TypeSystemAstBuilder::ConvertTypeParameterConstraint(
    const TS::ITypeParameter& tp) const {
    // The C# no-constraint early out: every flag false, the nullability not
    // `NotNullable`, and every direct base type an object/valuetype (the
    // `DirectBaseTypes.All(IsObjectOrValueType)` LINQ quantifier ports to
    // `std::all_of` over the shared_ptr entries -- the vector accessor returns
    // BY VALUE, so a local binds the one snapshot both iterators range over;
    // two chained calls would hand `all_of` iterators from different
    // containers).
    const std::vector<TS::ITypePtr> directBaseTypes = tp.DirectBaseTypes();
    if (!tp.HasDefaultConstructorConstraint() && !tp.HasReferenceTypeConstraint()
        && !tp.HasValueTypeConstraint() && !tp.AllowsRefLikeType()
        && tp.NullabilityConstraint() != TS::Nullability::NotNullable
        && std::all_of(directBaseTypes.begin(), directBaseTypes.end(),
                       [](const TS::ITypePtr& t) { return IsObjectOrValueType(*t); })) {
        return nullptr;
    }
    auto* c = new Constraint();
    c->TypeParameter(MakeSimpleType(tp.Name()));
    if (tp.HasReferenceTypeConstraint()) {
        if (tp.NullabilityConstraint() == TS::Nullability::Nullable) {
            // `where T : class?` -- the `class` keyword wrapped in a trailing `?`
            // (the `MakeNullableType` non-virtual wrap, the D223 leak model).
            c->BaseTypes().Add((new PrimitiveType("class"))->MakeNullableType());
        } else {
            c->BaseTypes().Add(new PrimitiveType("class"));
        }
    } else if (tp.HasValueTypeConstraint()) {
        if (tp.HasUnmanagedConstraint()) {
            c->BaseTypes().Add(new PrimitiveType("unmanaged"));
        } else {
            c->BaseTypes().Add(new PrimitiveType("struct"));
        }
    } else if (tp.NullabilityConstraint() == TS::Nullability::NotNullable) {
        c->BaseTypes().Add(new PrimitiveType("notnull"));
    }
    for (const TS::TypeConstraint& t : tp.TypeConstraints()) {
        // The C# `t.Type` is non-null (the `TypeConstraint` ctor throws on null;
        // the port asserts), so the deref needs no guard.
        if (!IsObjectOrValueType(*t.Type()) || !t.Attributes().empty()) {
            AstType* astType = ConvertType(*t.Type());
            if (!t.Attributes().empty()) {
                // A constraint carrying attributes (the C# 8.5 `[Attr] Base` form)
                // wraps the rendered type in a `ComposedType` holding the
                // attribute section (the object-initializer collection-add ports
                // to `Add`, the D222 convention).
                auto* attrSection = new AttributeSection();
                for (const TS::IAttribute* attribute : t.Attributes())
                    attrSection->Attributes().Add(ConvertAttribute(*attribute));
                auto* composed = new ComposedType();
                composed->Attributes().Add(attrSection);
                composed->BaseType(astType);
                astType = composed;
            }
            c->BaseTypes().Add(astType);
        }
    }
    if (tp.HasDefaultConstructorConstraint() && !tp.HasValueTypeConstraint()) {
        c->BaseTypes().Add(new PrimitiveType("new"));
    }
    if (tp.AllowsRefLikeType()) {
        c->BaseTypes().Add(new PrimitiveType("allows ref struct"));
    }
    return c;
}

// The C# `public VariableDeclarationStatement ConvertVariable(IVariable v)`
// (line 2744).
VariableDeclarationStatement* TypeSystemAstBuilder::ConvertVariable(
    const TS::IVariable& v) const {
    auto* decl = new VariableDeclarationStatement();
    decl->Modifiers(v.IsConst() ? Modifiers::Const : Modifiers::None);
    // `ConvertType` takes `TS::IType&` non-const (the `shared_from_this`-based
    // annotation path, the D529 convention); the variable's type accessor is
    // const, so the cast (the D515/D517 precedent, the ConvertParameter
    // precedent).
    decl->Type(ConvertType(const_cast<TS::IType&>(v.Type())));
    Expression* initializer = nullptr;
    if (v.IsConst()) {
        // The C# `catch (BadImageFormatException ex)` guards the metadata
        // decoder inside `GetConstantValue(throwOnInvalidMetadata: true)`; the
        // port catches `std::exception` (the ConvertParameter catch-arm
        // convention -- no port-side `GetConstantValue` throws today, the arm
        // keeps the structure for a future metadata-backed `IVariable`).
        try {
            initializer = ConvertConstantValue(
                const_cast<TS::IType&>(v.Type()),
                v.GetConstantValue(/*throwOnInvalidMetadata:*/ true));
        } catch (const std::exception& ex) {
            initializer = new ErrorExpression(ex.what());
        }
    }
    decl->Variables().Add(new VariableInitializer(v.Name(), initializer));
    return decl;
}

// The C# `void AddNullabilityDisambiguatingConstraints(MethodDeclaration decl,
// IMethod method)` (line 2686) -- see the header declaration for the full
// contract. A disambiguator is required only where the type parameter itself
// carries a nullable annotation (`T?`) in the signature: without it the compiler
// reads `T?` as `Nullable<T>` (the C# comment block above the method).
void TypeSystemAstBuilder::AddNullabilityDisambiguatingConstraints(
    MethodDeclaration& decl, const TS::IMethod& method) const {
    if (method.TypeParameters().empty())
        return;
    NullableTypeParameterCollector collector(method.TypeParameters());
    // `AcceptVisitor` is non-const (the D406 contract): the const accessors'
    // results are const_cast for the visits (the D515/D517 convention -- the
    // underlying type-system objects are mutable, the accessor's const is the
    // contract).
    const_cast<TS::IType&>(method.ReturnType()).AcceptVisitor(collector);
    for (const TS::IParameter* p : method.Parameters()) {
        if (p == nullptr)
            continue;
        const_cast<TS::IType&>(p->Type()).AcceptVisitor(collector);
    }
    if (collector.NullableTypeParameters.empty())
        return;
    for (const TS::ITypeParameter* tp : method.TypeParameters()) {
        if (tp == nullptr)
            continue;
        // The C# `!collector.NullableTypeParameters.Contains(tp) ||
        // GetNullabilityDisambiguator(tp) is not string keyword` -- the recorded
        // set is scanned by pointer identity (the C# HashSet reference equality)
        // and the disambiguator is the already-ported free function above.
        if (std::find(collector.NullableTypeParameters.begin(),
                      collector.NullableTypeParameters.end(),
                      tp) == collector.NullableTypeParameters.end()) {
            continue;
        }
        std::optional<std::string> keyword = GetNullabilityDisambiguator(*tp);
        if (!keyword.has_value())
            continue;
        auto* c = new Constraint();
        c->TypeParameter(MakeSimpleType(tp->Name()));
        c->BaseTypes().Add(new PrimitiveType(*keyword));
        decl.Constraints().Add(c);
    }
}

// The C# `bool NeedsAccessibility(IMember member)` (line 2518) -- see the
// header declaration for the full contract.
bool TypeSystemAstBuilder::NeedsAccessibility(const TS::IMember& member) const {
    TS::ITypePtr declaringType = member.DeclaringType();
    if (member.IsExplicitInterfaceImplementation())
        return false;
    switch (member.SymbolKind()) {
        case TS::SymbolKind::Constructor:
            return !member.IsStatic();
        case TS::SymbolKind::Destructor:
            return false;
        default:
            // The C# `declaringType?.Kind == TypeKind.Interface` -- the
            // null-conditional reads a null declaring type as not-an-interface
            // (a top-level member stub shape).
            if (declaringType && declaringType->Kind() == TS::TypeKind::Interface) {
                return member.Accessibility() != TS::Accessibility::Public;
            }
            // The C# `member is not IMethod method || !method.IsLocalFunction`:
            // a non-method member needs accessibility; a method does unless it
            // is a local function.
            return dynamic_cast<const TS::IMethod*>(&member) == nullptr
                || !static_cast<const TS::IMethod&>(member).IsLocalFunction();
    }
}

// The C# `Modifiers GetMemberModifiers(IMember member)` (line 2538) -- see the
// header declaration for the full contract.
Modifiers TypeSystemAstBuilder::GetMemberModifiers(const TS::IMember& member) const {
    Modifiers m = Modifiers::None;
    if (ShowAccessibility() && NeedsAccessibility(member)) {
        m = m | ModifierFromAccessibility(member.Accessibility(),
                                          UsePrivateProtectedAccessibility());
    }
    if (ShowModifiers()) {
        // The C# `member is LocalFunctionMethod localFunction` -- the pattern
        // match against the concrete wrapper class (the RTTI test the port
        // dynamic_casts; only the wrapper, not every IMethod).
        if (const auto* localFunction =
                dynamic_cast<const TS::Implementation::LocalFunctionMethod*>(&member)) {
            // Only the source-level flag decides; the wrapper's unconditionally
            // true `IsStatic` is deliberately NOT read (a non-static local
            // function carries no `static` modifier even though the wrapper
            // reports `IsStatic == true`).
            if (localFunction->IsStaticLocalFunction()) {
                m = m | Modifiers::Static;
            }
        } else {
            if (member.IsStatic()) {
                m = m | Modifiers::Static;
            }
            const auto* method = dynamic_cast<const TS::IMethod*>(&member);
            if (method != nullptr && method->ThisIsRefReadOnly()) {
                // The C# `method.DeclaringTypeDefinition?.IsReadOnly == false`:
                // the lifted `==` over the nullable bool is true ONLY for a
                // definite false, so a null definition (or a readonly one)
                // yields no bit.
                const TS::ITypeDefinition* declaringTypeDef =
                    method->DeclaringTypeDefinition();
                if (declaringTypeDef != nullptr && !declaringTypeDef->IsReadOnly()) {
                    m = m | Modifiers::Readonly;
                }
            }

            TS::ITypePtr declaringType = member.DeclaringType();
            // The C# derefs `declaringType.Kind` unconditionally (a real member
            // always has a declaring type); the port guards the degenerate
            // null-declaring-type stub shape, reading it as not-an-interface
            // (the D516 safe-fallback convention).
            if (declaringType && declaringType->Kind() == TS::TypeKind::Interface) {
                if (!member.IsStatic() && !member.IsVirtual() && !member.IsAbstract()
                    && !member.IsOverride()
                    && member.Accessibility() != TS::Accessibility::Private
                    && method != nullptr && method->HasBody()) {
                    m = m | Modifiers::Sealed;
                }
                if (member.IsStatic()) {
                    // Modifiers of static members in interfaces:
                    if (member.IsAbstract()) {
                        m = m | Modifiers::Abstract;
                    } else if (member.IsVirtual() && !member.IsOverride()) {
                        m = m | Modifiers::Virtual;
                    }
                }
            } else {
                if (member.IsAbstract()) {
                    m = m | Modifiers::Abstract;
                } else if (member.IsVirtual() && !member.IsOverride()) {
                    m = m | Modifiers::Virtual;
                }
                if (member.IsOverride() && !member.IsExplicitInterfaceImplementation()) {
                    m = m | Modifiers::Override;
                }
                if (member.IsSealed() && !member.IsExplicitInterfaceImplementation()) {
                    m = m | Modifiers::Sealed;
                }
            }
        }
    }
    return m;
}

// -- The "Convert Entity" accessor-support cluster (C# lines 2188-2297 +
// 2771-2782) --

// The C# `BlockStatement? GenerateBodyBlock()` (line 2188) -- see the header.
BlockStatement* TypeSystemAstBuilder::GenerateBodyBlock() const {
    if (GenerateBody()) {
        // The C# object initializer `new BlockStatement {
        // new ThrowStatement(new ObjectCreateExpression(ConvertType(
        // new TopLevelTypeName("System", "NotImplementedException", 0)))) }` --
        // the statement element ports to a `Statements().Add` (the
        // object-collection-initializer convention). `TopLevelTypeName`
        // converts implicitly to `FullTypeName` in C#; the port spells the
        // conversion (the explicit `FullTypeName` ctor).
        auto* block = new BlockStatement();
        block->Statements().Add(new ThrowStatement(new ObjectCreateExpression(
            ConvertType(TS::FullTypeName(
                TS::TopLevelTypeName("System", "NotImplementedException", 0))))));
        return block;
    }
    return nullptr;
}

// The C# `Accessor? ConvertAccessor(IMethod? accessor,
// MethodSemanticsAttributes kind, Accessibility ownerAccessibility, bool
// addParameterAttribute)` (line 2206) -- see the header.
Accessor* TypeSystemAstBuilder::ConvertAccessor(
    const TS::IMethod* accessor, TS::MethodSemanticsAttributes kind,
    TS::Accessibility ownerAccessibility, bool addParameterAttribute) const {
    // The C# `if (accessor == null) return null` -- the nullable method
    // parameter (the caller-owned accessor method is absent).
    if (accessor == nullptr)
        return nullptr;

    auto* decl = new Accessor();
    if (ShowAttributes()) {
        // The C# `decl.Attributes.AddRange(ConvertAttributes(...))` -- the
        // AddRange convenience ports to element-wise Add (the D222 convention,
        // the ConvertParameter precedent).
        for (AttributeSection* section : ConvertAttributes(accessor->GetAttributes()))
            decl->Attributes().Add(section);
        for (AttributeSection* section : ConvertAttributes(
                 accessor->GetReturnTypeAttributes(), "return"))
            decl->Attributes().Add(section);
        // The C# `accessor.Parameters.Last()` -- the port's parameter snapshot
        // is a vector, so `.back()` (the empty list cannot reach here: the
        // `Parameters.Count > 0` gate guards it).
        if (addParameterAttribute && !accessor->Parameters().empty()) {
            for (AttributeSection* section : ConvertAttributes(
                     accessor->Parameters().back()->GetAttributes(), "param"))
                decl->Attributes().Add(section);
        }
    }
    // The C# `accessor.Accessibility != ownerAccessibility` -- the accessibility
    // modifier renders only when the accessor's differs from the property's
    // (an accessor inheriting its owner's accessibility carries no modifier).
    if (ShowAccessibility() && accessor->Accessibility() != ownerAccessibility)
        decl->Modifiers(
            ModifierFromAccessibility(accessor->Accessibility(),
                                      UsePrivateProtectedAccessibility()));
    if (ShowModifiers() && TS::HasReadonlyModifier(*accessor))
        decl->Modifiers(decl->Modifiers() | Modifiers::Readonly);

    // The C# switch expression `kind switch { Getter => ..., Setter => ...,
    // Adder => ..., Remover => ..., _ => AccessorKind.Any }` -- a plain switch
    // (the C# `_` arm folds every other / combined value to Any).
    AccessorKind accessorKind;
    switch (kind) {
        case TS::MethodSemanticsAttributes::Getter:
            accessorKind = AccessorKind::Getter;
            break;
        case TS::MethodSemanticsAttributes::Setter:
            accessorKind = AccessorKind::Setter;
            break;
        case TS::MethodSemanticsAttributes::Adder:
            accessorKind = AccessorKind::Adder;
            break;
        case TS::MethodSemanticsAttributes::Remover:
            accessorKind = AccessorKind::Remover;
            break;
        default:
            accessorKind = AccessorKind::Any;
            break;
    }
    // The C# `kind == MethodSemanticsAttributes.Setter && SupportInitAccessors
    // && accessor.IsInitOnly` -- only a SETTER upgrades to the init accessor.
    if (kind == TS::MethodSemanticsAttributes::Setter && SupportInitAccessors()
        && accessor->IsInitOnly()) {
        accessorKind = AccessorKind::Init;
    }
    decl->Kind(accessorKind);
    // The C# `accessor.IsInitOnly && accessorKind != AccessorKind.Init` -- an
    // init-only accessor that did NOT become an init accessor (the flag off,
    // or a non-setter kind) keeps the init-ness as a trailing `/* init */`
    // comment (the fallback for output without init accessors).
    if (accessor->IsInitOnly() && accessorKind != AccessorKind::Init) {
        decl->AddTrailingTrivia(new Comment("init", CommentType::MultiLine));
    }
    if (AddResolveResultAnnotations()) {
        // The C# `new MemberResolveResult(null, accessor)` -- the null target
        // is the empty shared_ptr (the ConvertAttribute annotation precedent);
        // `ComputeType` reads the accessor's return type (which must be
        // shared-managed, the D271/D406 model).
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), accessor));
    }
    if (GenerateBody()) {
        decl->Body(GenerateBodyBlock());
    }
    return decl;
}

// The C# `static void MergeReadOnlyModifiers(EntityDeclaration decl, Accessor?
// accessor1, Accessor? accessor2)` (line 2286) -- see the header.
void TypeSystemAstBuilder::MergeReadOnlyModifiers(EntityDeclaration& decl,
                                                   Accessor* accessor1,
                                                   Accessor* accessor2) {
    if (accessor1 == nullptr)
        return;
    if (accessor1->HasModifier(Modifiers::Readonly) && accessor2 == nullptr) {
        accessor1->Modifiers(accessor1->Modifiers() & ~Modifiers::Readonly);
        decl.Modifiers(decl.Modifiers() | Modifiers::Readonly);
    } else if (accessor1->HasModifier(Modifiers::Readonly)
               && accessor2->HasModifier(Modifiers::Readonly)) {
        accessor1->Modifiers(accessor1->Modifiers() & ~Modifiers::Readonly);
        accessor2->Modifiers(accessor2->Modifiers() & ~Modifiers::Readonly);
        decl.Modifiers(decl.Modifiers() | Modifiers::Readonly);
    }
}

// The C# `AstType? GetExplicitInterfaceType(IMember member)` (line 2771) --
// see the header.
AstType* TypeSystemAstBuilder::GetExplicitInterfaceType(
    const TS::IMember& member) const {
    if (member.IsExplicitInterfaceImplementation()) {
        // The C# `member.ExplicitlyImplementedInterfaceMembers
        // .FirstOrDefault()` -- an empty list yields null (the C# default
        // of a nullable reference), so the empty vector takes the fallback.
        std::vector<const TS::IMember*> baseMembers =
            member.ExplicitlyImplementedInterfaceMembers();
        if (!baseMembers.empty()) {
            const TS::IMember* baseMember = baseMembers.front();
            // The C# `baseMember.DeclaringType` (an `IType?` -- null for a
            // top-level entity, which cannot be an interface member's declaring
            // type in practice); the port guards the degenerate null handle
            // (the D516 safe-fallback convention: the C# would pass null on
            // to `ConvertType(null)` and throw).
            TS::ITypePtr declaringType = baseMember->DeclaringType();
            if (declaringType)
                return ConvertType(*declaringType);
        }
    }
    return nullptr;
}

// -- The "Convert Entity" member renderers (C# lines 2143-2186 + 2252-2275 +
// 2294-2320 + 2322-2360) --

// The C# `FieldDeclaration ConvertField(IField field)` (line 2143) -- see the
// header declaration for the full contract.
FieldDeclaration* TypeSystemAstBuilder::ConvertField(
    const TS::IField& field) const {
    auto* decl = new FieldDeclaration();
    if (ShowModifiers()) {
        Modifiers m = GetMemberModifiers(field);
        if (field.IsConst()) {
            // The C# `m &= ~Modifiers.Static; m |= Modifiers.Const;` -- a C#
            // constant is never rendered `static const` (a `static const` field
            // in metadata is a C# `const`), so the static bit is REPLACED by the
            // const bit.
            m = m & ~Modifiers::Static;
            m = m | Modifiers::Const;
        } else if (field.IsReadOnly()) {
            m = m | Modifiers::Readonly;
        } else if (field.IsVolatile()) {
            m = m | Modifiers::Volatile;
        }
        decl->Modifiers(m);
    }
    if (ShowAttributes()) {
        // The C# `decl.Attributes.AddRange(ConvertAttributes(...))` -- the
        // AddRange convenience ports to element-wise Add (the D222 convention,
        // the ConvertParameter precedent).
        for (AttributeSection* section : ConvertAttributes(field.GetAttributes()))
            decl->Attributes().Add(section);
    }
    if (AddResolveResultAnnotations()) {
        // The C# `new MemberResolveResult(null, field)` -- the null target is
        // the empty shared_ptr (the ConvertAccessor annotation precedent);
        // `ComputeType` reads the field's return type (which must be
        // shared-managed, the D271/D406 model) and `InitConstantFromField`
        // reads the field's own `IsConst` / `GetConstantValue`.
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &field));
    }
    // `ConvertType` takes `TS::IType&` non-const (the `shared_from_this`-based
    // annotation path, the D529 convention); the member's type accessor is
    // const, so the cast (the D515/D517 precedent, the ConvertVariable
    // precedent).
    decl->ReturnType(ConvertType(const_cast<TS::IType&>(field.ReturnType())));
    // The C# `if (decl.ReturnType is ComposedType ct && ct.HasRefSpecifier &&
    // field.ReturnTypeIsRefReadOnly) ct.HasReadOnlySpecifier = true;` -- a
    // `ref readonly` field renders `ref readonly` (the ref comes from the
    // ByReferenceType unwrap inside ConvertType; the readonly is promoted
    // here onto the rendered ComposedType).
    if (auto* ct = dynamic_cast<ComposedType*>(decl->ReturnType());
        ct != nullptr && ct->HasRefSpecifier() && field.ReturnTypeIsRefReadOnly()) {
        ct->HasReadOnlySpecifier(true);
    }
    Expression* initializer = nullptr;
    if (field.IsConst() && ShowConstantValues()) {
        // The C# `catch (BadImageFormatException ex)` guards the metadata
        // decoder inside `GetConstantValue(throwOnInvalidMetadata: true)`; the
        // port catches `std::exception` (the ConvertParameter / ConvertVariable
        // catch-arm convention -- no port-side `GetConstantValue` throws today,
        // the arm keeps the structure for a future metadata-backed `IField`)
        // and renders the message as an `ErrorExpression`'s trailing comment.
        try {
            // The C# initializer call passes `field.Type` (the `IVariable`
            // surface), NOT `field.ReturnType` -- faithful to the call site.
            initializer = ConvertConstantValue(
                const_cast<TS::IType&>(field.Type()),
                field.GetConstantValue(/*throwOnInvalidMetadata:*/ true));
        } catch (const std::exception& ex) {
            initializer = new ErrorExpression(ex.what());
        }
    }
    decl->Variables().Add(new VariableInitializer(field.Name(), initializer));
    return decl;
}

// The C# `PropertyDeclaration ConvertProperty(IProperty property)` (line
// 2252) -- see the header declaration for the full contract.
PropertyDeclaration* TypeSystemAstBuilder::ConvertProperty(
    const TS::IProperty& property) const {
    auto* decl = new PropertyDeclaration();
    decl->Modifiers(GetMemberModifiers(property));
    if (ShowAttributes()) {
        for (AttributeSection* section : ConvertAttributes(property.GetAttributes()))
            decl->Attributes().Add(section);
    }
    if (AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &property));
    }
    decl->ReturnType(ConvertType(const_cast<TS::IType&>(property.ReturnType())));
    // The C# `if (property.ReturnTypeIsRefReadOnly && decl.ReturnType is
    // ComposedType ct && ct.HasRefSpecifier) ct.HasReadOnlySpecifier = true;`
    // -- note the FLAG check comes first here (the mirror of ConvertField's
    // node-first order; both are the same conjunction).
    if (property.ReturnTypeIsRefReadOnly()) {
        if (auto* ct = dynamic_cast<ComposedType*>(decl->ReturnType());
            ct != nullptr && ct->HasRefSpecifier()) {
            ct->HasReadOnlySpecifier(true);
        }
    }
    decl->Name(property.Name());
    // The C# `decl.Getter = ConvertAccessor(property.Getter, Getter,
    // property.Accessibility, false); decl.Setter = ConvertAccessor(
    // property.Setter, Setter, property.Accessibility, true);` -- only the
    // SETTER arm passes `addParameterAttribute: true` (the `[param: ...]`
    // section belongs to the setter's `value` parameter).
    decl->Getter(ConvertAccessor(property.Getter(),
                                  TS::MethodSemanticsAttributes::Getter,
                                  property.Accessibility(), false));
    decl->Setter(ConvertAccessor(property.Setter(),
                                 TS::MethodSemanticsAttributes::Setter,
                                 property.Accessibility(), true));
    decl->PrivateImplementationType(GetExplicitInterfaceType(property));
    MergeReadOnlyModifiers(*decl, decl->Getter(), decl->Setter());
    return decl;
}

// The C# `IndexerDeclaration ConvertIndexer(IProperty indexer)` (line 2294) --
// see the header declaration for the full contract.
IndexerDeclaration* TypeSystemAstBuilder::ConvertIndexer(
    const TS::IProperty& indexer) const {
    auto* decl = new IndexerDeclaration();
    decl->Modifiers(GetMemberModifiers(indexer));
    if (ShowAttributes()) {
        for (AttributeSection* section : ConvertAttributes(indexer.GetAttributes()))
            decl->Attributes().Add(section);
    }
    if (AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &indexer));
    }
    decl->ReturnType(ConvertType(const_cast<TS::IType&>(indexer.ReturnType())));
    if (indexer.ReturnTypeIsRefReadOnly()) {
        if (auto* ct = dynamic_cast<ComposedType*>(decl->ReturnType());
            ct != nullptr && ct->HasRefSpecifier()) {
            ct->HasReadOnlySpecifier(true);
        }
    }
    // The C# `foreach (IParameter p in indexer.Parameters) decl.Parameters.Add(
    // ConvertParameter(p));` -- the parameter loop in place of the property's
    // name assignment (an indexer names itself `this[...]`; the node's `Name`
    // setter deliberately throws).
    for (const TS::IParameter* p : indexer.Parameters()) {
        if (p == nullptr)
            continue; // the D516 null-entry guard
        decl->Parameters().Add(ConvertParameter(*p));
    }
    decl->Getter(ConvertAccessor(indexer.Getter(),
                                  TS::MethodSemanticsAttributes::Getter,
                                  indexer.Accessibility(), false));
    decl->Setter(ConvertAccessor(indexer.Setter(),
                                 TS::MethodSemanticsAttributes::Setter,
                                 indexer.Accessibility(), true));
    decl->PrivateImplementationType(GetExplicitInterfaceType(indexer));
    MergeReadOnlyModifiers(*decl, decl->Getter(), decl->Setter());
    return decl;
}

// The C# `EntityDeclaration ConvertEvent(IEvent ev)` (line 2322) -- see the
// header declaration for the full contract.
EntityDeclaration* TypeSystemAstBuilder::ConvertEvent(const TS::IEvent& ev) const {
    if (UseCustomEvents()) {
        auto* decl = new CustomEventDeclaration();
        decl->Modifiers(GetMemberModifiers(ev));
        if (ShowAttributes()) {
            for (AttributeSection* section : ConvertAttributes(ev.GetAttributes()))
                decl->Attributes().Add(section);
        }
        if (AddResolveResultAnnotations()) {
            decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
                std::shared_ptr<Sem::ResolveResult>(), &ev));
        }
        decl->ReturnType(ConvertType(const_cast<TS::IType&>(ev.ReturnType())));
        decl->Name(ev.Name());
        // The C# passes `addParameterAttribute: true` for BOTH event accessors
        // (an add/remove accessor's `[param: ...]` section belongs to its
        // `value` parameter).
        decl->AddAccessor(ConvertAccessor(ev.AddAccessor(),
                                           TS::MethodSemanticsAttributes::Adder,
                                           ev.Accessibility(), true));
        decl->RemoveAccessor(ConvertAccessor(ev.RemoveAccessor(),
                                              TS::MethodSemanticsAttributes::Remover,
                                              ev.Accessibility(), true));
        decl->PrivateImplementationType(GetExplicitInterfaceType(ev));
        MergeReadOnlyModifiers(*decl, decl->AddAccessor(), decl->RemoveAccessor());
        return decl;
    } else {
        auto* decl = new EventDeclaration();
        decl->Modifiers(GetMemberModifiers(ev));
        if (ShowAttributes()) {
            for (AttributeSection* section : ConvertAttributes(ev.GetAttributes()))
                decl->Attributes().Add(section);
        }
        if (AddResolveResultAnnotations()) {
            decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
                std::shared_ptr<Sem::ResolveResult>(), &ev));
        }
        decl->ReturnType(ConvertType(const_cast<TS::IType&>(ev.ReturnType())));
        // The field-like event shape: the name lives in the sole
        // `VariableInitializer` and there are NO accessors (the C#
        // `decl.Variables.Add(new VariableInitializer(ev.Name))`).
        decl->Variables().Add(new VariableInitializer(ev.Name()));
        return decl;
    }
}

// -- The "Convert Entity" method renderers (C# lines 2362-2500) --

// The C# `MethodDeclaration ConvertMethod(IMethod method)` (line 2362) -- see
// the header declaration for the full contract.
MethodDeclaration* TypeSystemAstBuilder::ConvertMethod(
    const TS::IMethod& method) const {
    auto* decl = new MethodDeclaration();
    decl->Modifiers(GetMemberModifiers(method));
    if (ShowAttributes()) {
        // The C# `decl.Attributes.AddRange(ConvertAttributes(method.GetAttributes()));
        // decl.Attributes.AddRange(ConvertAttributes(method.GetReturnTypeAttributes(),
        // "return"));` -- the method renders BOTH its own attribute sections and
        // the `[return: ...]` sections over the return-type attributes (the
        // AddRange convenience ports to element-wise Add, the D222 convention).
        for (AttributeSection* section : ConvertAttributes(method.GetAttributes()))
            decl->Attributes().Add(section);
        for (AttributeSection* section : ConvertAttributes(
                 method.GetReturnTypeAttributes(), "return"))
            decl->Attributes().Add(section);
    }
    if (AddResolveResultAnnotations()) {
        // The C# `new MemberResolveResult(null, method)` -- the null target is
        // the empty shared_ptr (the ConvertAccessor annotation precedent);
        // `ComputeType` reads the method's return type (which must be
        // shared-managed, the D271/D406 model).
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &method));
    }
    // `ConvertType` takes `TS::IType&` non-const (the `shared_from_this`-based
    // annotation path, the D529 convention); the accessor's const is the
    // contract (the D515/D517 const_cast precedent).
    decl->ReturnType(ConvertType(const_cast<TS::IType&>(method.ReturnType())));
    // The C# `if (method.ReturnTypeIsRefReadOnly && decl.ReturnType is
    // ComposedType ct && ct.HasRefSpecifier) ct.HasReadOnlySpecifier = true;`
    // -- the flag-check-first order (the ConvertProperty mirror; the C#
    // `is`-pattern has no short-circuit-visible ordering).
    if (method.ReturnTypeIsRefReadOnly()) {
        if (auto* ct = dynamic_cast<ComposedType*>(decl->ReturnType());
            ct != nullptr && ct->HasRefSpecifier()) {
            ct->HasReadOnlySpecifier(true);
        }
    }
    decl->Name(method.Name());
    if (ShowTypeParameters()) {
        for (const TS::ITypeParameter* tp : method.TypeParameters()) {
            if (tp == nullptr)
                continue; // the D516 null-entry guard
            decl->TypeParameters().Add(ConvertTypeParameter(*tp));
        }
    }
    for (const TS::IParameter* p : method.Parameters()) {
        if (p == nullptr)
            continue; // the D516 null-entry guard
        decl->Parameters().Add(ConvertParameter(*p));
    }
    // The C# `if (method.IsExtensionMethod && method.ReducedFrom == null &&
    // decl.Parameters.Any()) decl.Parameters.First().HasThisModifier = true;`
    // -- the `this` modifier goes on the FIRST parameter of a NON-reduced
    // extension method (a reduced method's receiver parameter is already
    // gone, so re-adding `this` would point at a wrong parameter). The C#
    // `Any()` ports to `Count() > 0` (the FirstOrNull null-check convention).
    if (method.IsExtensionMethod() && method.ReducedFrom() == nullptr
        && decl->Parameters().Count() > 0) {
        decl->Parameters()[0]->HasThisModifier(true);
    }
    if (ShowTypeParameters() && ShowTypeParameterConstraints()) {
        // The C# override / explicit-interface split: C# inherits the
        // constraints of an override or explicit interface implementation from
        // the base member and forbids restating them, with a single exception --
        // a `class`, `struct`, or `default` constraint may be given to
        // disambiguate whether `T?` denotes a nullable annotation or
        // `Nullable<T>` (the C# comment at line 2403).
        if (method.IsOverride() || method.IsExplicitInterfaceImplementation()) {
            AddNullabilityDisambiguatingConstraints(*decl, method);
        } else {
            for (const TS::ITypeParameter* tp : method.TypeParameters()) {
                if (tp == nullptr)
                    continue; // the D516 null-entry guard
                Constraint* constraint = ConvertTypeParameterConstraint(*tp);
                if (constraint != nullptr)
                    decl->Constraints().Add(constraint);
            }
        }
    }
    // The C# `decl.Body = GenerateBodyBlock();` -- UNCONDITIONAL (unlike the
    // `ConvertAccessor` `if (GenerateBody)` guard): a null body is the slot's
    // default state, so assigning it is a no-op.
    decl->Body(GenerateBodyBlock());
    decl->PrivateImplementationType(GetExplicitInterfaceType(method));
    return decl;
}

// The C# `EntityDeclaration ConvertOperator(IMethod op)` (line 2405) -- see
// the header declaration for the full contract.
EntityDeclaration* TypeSystemAstBuilder::ConvertOperator(
    const TS::IMethod& op) const {
    // The C# `int dot = op.Name.LastIndexOf('.'); string name =
    // op.Name.Substring(dot + 1);` -- an explicit-interface operator name is
    // `Namespace.Iface.op_Addition`, so the operator token is looked up from
    // the tail after the LAST '.'. The C# `LastIndexOf` returns -1 when no dot
    // exists (so `Substring(dot + 1)` yields the whole name); the port's
    // `rfind` returns `npos`, mapped back to the whole-name start offset 0.
    const std::string& opName = op.Name();
    std::string::size_type dot = opName.rfind('.');
    std::string name = opName.substr(dot == std::string::npos ? 0 : dot + 1);
    std::optional<OperatorType> opType = OperatorDeclaration::GetOperatorType(name);
    if (!opType.has_value())
        return ConvertMethod(op);
    // The C# `if (opType == OperatorType.UnsignedRightShift &&
    // !SupportUnsignedRightShift) return ConvertMethod(op);` -- the C# 11
    // `>>>` operator falls back to the method syntax when the output language
    // level does not support it.
    if (*opType == OperatorType::UnsignedRightShift && !SupportUnsignedRightShift())
        return ConvertMethod(op);
    // The C# `if (!SupportOperatorChecked && OperatorDeclaration.IsChecked(
    // opType.Value)) return ConvertMethod(op);` -- the C# 11 `checked` operators
    // fall back to the method syntax when unsupported.
    if (!SupportOperatorChecked() && OperatorDeclaration::IsChecked(*opType))
        return ConvertMethod(op);

    auto* decl = new OperatorDeclaration();
    decl->Modifiers(GetMemberModifiers(op));
    // The C# `decl.OperatorType = opType.Value;` -- the operator's `Name` is
    // DERIVED from this scalar (the `GetName(this.OperatorType)` override), so
    // no name is rendered here.
    decl->OperatorType(*opType);
    decl->ReturnType(ConvertType(const_cast<TS::IType&>(op.ReturnType())));
    // The C# `if (op.ReturnTypeIsRefReadOnly && decl.ReturnType is ComposedType ct
    // && ct.HasRefSpecifier) ct.HasReadOnlySpecifier = true;` -- the
    // flag-check-first order (the ConvertMethod mirror).
    if (op.ReturnTypeIsRefReadOnly()) {
        if (auto* ct = dynamic_cast<ComposedType*>(decl->ReturnType());
            ct != nullptr && ct->HasRefSpecifier()) {
            ct->HasReadOnlySpecifier(true);
        }
    }
    for (const TS::IParameter* p : op.Parameters()) {
        if (p == nullptr)
            continue; // the D516 null-entry guard
        decl->Parameters().Add(ConvertParameter(*p));
    }
    if (ShowAttributes()) {
        for (AttributeSection* section : ConvertAttributes(op.GetAttributes()))
            decl->Attributes().Add(section);
        for (AttributeSection* section : ConvertAttributes(
                 op.GetReturnTypeAttributes(), "return"))
            decl->Attributes().Add(section);
    }
    if (AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &op));
    }
    // The C# `decl.Body = GenerateBodyBlock();` -- unconditional (the
    // ConvertMethod convention).
    decl->Body(GenerateBodyBlock());
    decl->PrivateImplementationType(GetExplicitInterfaceType(op));
    return decl;
}

// The C# `ConstructorDeclaration ConvertConstructor(IMethod ctor)` (line
// 2443) -- see the header declaration for the full contract.
ConstructorDeclaration* TypeSystemAstBuilder::ConvertConstructor(
    const TS::IMethod& ctor) const {
    auto* decl = new ConstructorDeclaration();
    decl->Modifiers(GetMemberModifiers(ctor));
    if (ShowAttributes()) {
        // The C# renders ONLY the constructor's own attribute sections -- no
        // `[return: ...]` (a constructor has no return type).
        for (AttributeSection* section : ConvertAttributes(ctor.GetAttributes()))
            decl->Attributes().Add(section);
    }
    // The C# `if (ctor.DeclaringTypeDefinition != null) decl.Name =
    // ctor.DeclaringTypeDefinition.Name;` -- a constructor's name IS its
    // declaring type's name, rendered through the inherited
    // `EntityDeclaration::Name(std::string_view)` setter (SetChildByKind finds
    // the node's own `NameToken` slot, the iteration-128 property precedent).
    if (ctor.DeclaringTypeDefinition() != nullptr)
        decl->Name(ctor.DeclaringTypeDefinition()->Name());
    for (const TS::IParameter* p : ctor.Parameters()) {
        if (p == nullptr)
            continue; // the D516 null-entry guard
        decl->Parameters().Add(ConvertParameter(*p));
    }
    if (AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &ctor));
    }
    // The C# `decl.Body = GenerateBodyBlock();` -- unconditional (the
    // ConvertMethod convention).
    decl->Body(GenerateBodyBlock());
    return decl;
}

// The C# `DestructorDeclaration ConvertDestructor(IMethod dtor)` (line 2460)
// -- see the header declaration for the full contract.
DestructorDeclaration* TypeSystemAstBuilder::ConvertDestructor(
    const TS::IMethod& dtor) const {
    auto* decl = new DestructorDeclaration();
    // The C# renders NO modifiers for a destructor (never accessibility or
    // static -- the `NeedsAccessibility` destructor case), NO parameters, and
    // NO explicit-interface type: only the attributes, the name, the
    // annotation, and the body.
    if (ShowAttributes()) {
        for (AttributeSection* section : ConvertAttributes(dtor.GetAttributes()))
            decl->Attributes().Add(section);
    }
    if (dtor.DeclaringTypeDefinition() != nullptr)
        decl->Name(dtor.DeclaringTypeDefinition()->Name());
    if (AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
            std::shared_ptr<Sem::ResolveResult>(), &dtor));
    }
    // The C# `decl.Body = GenerateBodyBlock();` -- unconditional (the
    // ConvertMethod convention).
    decl->Body(GenerateBodyBlock());
    return decl;
}

namespace {

// The C# `sealed class BaseListNameabilityVisitor(ITypeDefinition currentType,
// MemberLookup lookup) : TypeVisitor` (TypeSystemAstBuilder.cs lines 2061-2069)
// -- the private nested visitor `BaseTypeAccessibleFrom` drives over the
// base-list reference: every `ITypeDefinition` the traversal reaches (the named
// type itself and, through the children walk the base `VisitTypeDefinition`
// performs, every nested type argument) must be nameable. The `AllNameable`
// field starts true and is AND-ed (non-short-circuit, the C# `&=`) with each
// visited definition's nameability. File-local (its only consumer is
// `BaseTypeAccessibleFrom`; the composed member is the testable surface).
class BaseListNameabilityVisitor final : public TS::TypeVisitor {
public:
    // The C# `public bool AllNameable = true;`.
    bool AllNameable = true;

    BaseListNameabilityVisitor(const TS::ITypeDefinition& currentType,
                               const Resolver::MemberLookup& lookup)
        : currentType_(&currentType), lookup_(&lookup) {}

    TS::ITypePtr VisitTypeDefinition(TS::ITypeDefinition& type) override {
        AllNameable = AllNameable
            & TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
                &type, *currentType_, *lookup_);
        // The C# `return base.VisitTypeDefinition(type);` -- the TypeVisitor
        // default visits the children (a ParameterizedType's generic and type
        // arguments, so `IWrap<F.IFoo>` reaches the nested `F.IFoo`).
        return TS::TypeVisitor::VisitTypeDefinition(type);
    }

private:
    const TS::ITypeDefinition* currentType_;
    const Resolver::MemberLookup* lookup_;
};

// The C# `baseType.TypeArguments.Count == 1 &&
// baseType.TypeArguments[0].Equals(typeDefinition.AsParameterizedType())` -- the
// record-IEquatable omission's type-arguments conjunction. The C#
// `IType.TypeArguments` is ParameterizedType-specific on the port's minimal
// surface (the iteration-82 convention): a non-parameterized base type does not
// take the omission (for the bare `IEquatable`1` definition the C# reads its own
// type parameters, count 1, and the comparison against the record type still
// fails -- the same observable outcome through the port's guard).
bool RecordIEquatableOmitted(const TS::IType& baseType,
                             const TS::ITypeDefinition& typeDefinition) {
    const auto* pt = dynamic_cast<const TS::ParameterizedType*>(&baseType);
    if (pt == nullptr || pt->TypeArguments().size() != 1)
        return false;
    const TS::ITypePtr selfParameterized = AsParameterizedType(typeDefinition);
    return pt->TypeArguments()[0]->Equals(*selfParameterized);
}

} // namespace

// The C# `static bool TypeDefinitionNameableInBaseList(ITypeDefinition? td,
// ITypeDefinition currentType, MemberLookup lookup)` (line 2072) -- see the
// header declaration for the full contract.
bool TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
    const TS::ITypeDefinition* td,
    const TS::ITypeDefinition& currentType,
    const Resolver::MemberLookup& lookup) {
    if (td == nullptr)
        return true;
    // The C# `for (var t = currentType; t != null; t = t.DeclaringTypeDefinition)
    // { if (td.DeclaringTypeDefinition?.Equals(t) == true) return true; }` -- a
    // type may name its own nested types (and those of its enclosing types) in
    // its base list regardless of accessibility, e.g. 'class F : F.IFoo'.
    for (const TS::ITypeDefinition* t = &currentType; t != nullptr;
         t = t->DeclaringTypeDefinition()) {
        const TS::ITypeDefinition* tdDeclaring = td->DeclaringTypeDefinition();
        // The C# `?.Equals(t) == true` -- a null declaring type definition is
        // not a match (the lifted == over null yields false).
        if (tdDeclaring != nullptr && tdDeclaring->Equals(*t))
            return true;
    }
    // The C# `if (!lookup.IsAccessible(td, false)) return false;` -- everything
    // else resolves in the enclosing scope: 'class SubF : F, F.IFoo' is CS0122
    // even though F.IFoo is accessible inside SubF's body.
    if (!lookup.IsAccessible(*td, /*allowProtectedAccess:*/ false))
        return false;
    // The C# recursion -- naming 'A.I' also requires 'A' to be nameable.
    return TypeDefinitionNameableInBaseList(td->DeclaringTypeDefinition(),
                                            currentType, lookup);
}

// The C# `bool BaseTypeAccessibleFrom(IType baseType, ITypeDefinition
// currentType, MemberLookup lookup)` (line 2053) -- see the header declaration
// for the full contract.
bool TypeSystemAstBuilder::BaseTypeAccessibleFrom(
    TS::IType& baseType,
    const TS::ITypeDefinition& currentType,
    const Resolver::MemberLookup& lookup) const {
    BaseListNameabilityVisitor visitor(currentType, lookup);
    baseType.AcceptVisitor(visitor);
    return visitor.AllNameable;
}

// The C# `DelegateDeclaration ConvertDelegate(IMethod invokeMethod, Modifiers
// modifiers)` (line 2094) -- see the header declaration for the full contract.
DelegateDeclaration* TypeSystemAstBuilder::ConvertDelegate(
    const TS::IMethod& invokeMethod, Modifiers modifiers) const {
    // The C# `ITypeDefinition d = invokeMethod.DeclaringTypeDefinition!;` -- the
    // hard non-null assertion (a real delegate's invoke method always carries
    // its declaring type definition). The port guards the degenerate stub shape
    // (a null definition skips the d-reading arms, the D516 safe-fallback
    // convention).
    const TS::ITypeDefinition* d = invokeMethod.DeclaringTypeDefinition();
    auto* decl = new DelegateDeclaration();
    // The C# `decl.Modifiers = modifiers & ~Modifiers.Sealed;` -- a delegate is
    // never rendered `sealed`.
    decl->Modifiers(modifiers & ~Modifiers::Sealed);
    if (d != nullptr && ShowAttributes()) {
        // The delegate's OWN attribute sections come from the DEFINITION; the
        // `[return: ...]` sections from the invoke method's return-type
        // attributes (the `target: "return"` overload, the ConvertMethod
        // precedent).
        for (AttributeSection* section : ConvertAttributes(d->GetAttributes()))
            decl->Attributes().Add(section);
        for (AttributeSection* section : ConvertAttributes(
                 invokeMethod.GetReturnTypeAttributes(), "return"))
            decl->Attributes().Add(section);
    }
    if (d != nullptr && AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
            std::const_pointer_cast<TS::IType>(d->shared_from_this())));
    }
    // `ConvertType` takes `TS::IType&` non-const (the D529 annotation path); the
    // accessor's const is the contract (the D515/D517 const_cast precedent, the
    // ConvertMethod precedent).
    decl->ReturnType(ConvertType(const_cast<TS::IType&>(invokeMethod.ReturnType())));
    // The C# `if (invokeMethod.ReturnTypeIsRefReadOnly && decl.ReturnType is
    // ComposedType ct && ct.HasRefSpecifier) ct.HasReadOnlySpecifier = true;`
    // -- the node-first order (the ConvertField mirror).
    if (invokeMethod.ReturnTypeIsRefReadOnly()) {
        if (auto* ct = dynamic_cast<ComposedType*>(decl->ReturnType());
            ct != nullptr && ct->HasRefSpecifier()) {
            ct->HasReadOnlySpecifier(true);
        }
    }
    if (d != nullptr)
        decl->Name(d->Name());

    const int outerTypeParameterCount =
        d == nullptr || d->DeclaringTypeDefinition() == nullptr
            ? 0
            : d->DeclaringTypeDefinition()->TypeParameterCount();

    if (d != nullptr && ShowTypeParameters()) {
        // The C# `d.TypeParameters.Skip(outerTypeParameterCount)` -- the outer
        // type's parameters are not redeclared (the nested-type convention); the
        // skip is an index loop (a start beyond the end yields nothing, the C#
        // Skip semantics).
        const std::vector<const TS::ITypeParameter*> typeParameters =
            d->TypeParameters();
        for (size_t i = static_cast<size_t>(outerTypeParameterCount);
             i < typeParameters.size(); i++) {
            if (typeParameters[i] == nullptr)
                continue; // the D516 null-entry guard
            decl->TypeParameters().Add(ConvertTypeParameter(*typeParameters[i]));
        }
    }
    for (const TS::IParameter* p : invokeMethod.Parameters()) {
        if (p == nullptr)
            continue; // the D516 null-entry guard
        decl->Parameters().Add(ConvertParameter(*p));
    }
    if (d != nullptr && ShowTypeParameters() && ShowTypeParameterConstraints()) {
        const std::vector<const TS::ITypeParameter*> typeParameters =
            d->TypeParameters();
        for (size_t i = static_cast<size_t>(outerTypeParameterCount);
             i < typeParameters.size(); i++) {
            if (typeParameters[i] == nullptr)
                continue; // the D516 null-entry guard
            Constraint* constraint = ConvertTypeParameterConstraint(*typeParameters[i]);
            if (constraint != nullptr)
                decl->Constraints().Add(constraint);
        }
    }
    return decl;
}

// The C# `EntityDeclaration ConvertTypeDefinition(ITypeDefinition typeDefinition)`
// (line 1900) -- see the header declaration for the full contract.
EntityDeclaration* TypeSystemAstBuilder::ConvertTypeDefinition(
    const TS::ITypeDefinition& typeDefinition) const {
    Modifiers modifiers = Modifiers::None;
    if (ShowAccessibility()) {
        modifiers = modifiers | ModifierFromAccessibility(
            typeDefinition.Accessibility(), UsePrivateProtectedAccessibility());
    }
    if (ShowModifiers()) {
        // The C# if/else-if chain -- `static` wins over `abstract`, which wins
        // over `sealed`.
        if (typeDefinition.IsStatic()) {
            modifiers = modifiers | Modifiers::Static;
        } else if (typeDefinition.IsAbstract()) {
            modifiers = modifiers | Modifiers::Abstract;
        } else if (typeDefinition.IsSealed()) {
            modifiers = modifiers | Modifiers::Sealed;
        }
    }

    ClassType classType;
    switch (typeDefinition.Kind()) {
        case TS::TypeKind::Struct:
        case TS::TypeKind::Void:
            classType = ClassType::Struct;
            // The C# `modifiers &= ~Modifiers.Sealed;` -- a struct is never
            // rendered `sealed` (it is implicitly sealed).
            modifiers = modifiers & ~Modifiers::Sealed;
            if (ShowModifiers()) {
                if (typeDefinition.IsReadOnly())
                    modifiers = modifiers | Modifiers::Readonly;
                if (typeDefinition.IsByRefLike())
                    modifiers = modifiers | Modifiers::Ref;
            }
            if (SupportRecordStructs() && typeDefinition.IsRecord())
                classType = ClassType::RecordStruct;
            break;
        case TS::TypeKind::Enum:
            classType = ClassType::Enum;
            modifiers = modifiers & ~Modifiers::Sealed;
            break;
        case TS::TypeKind::Interface:
            classType = ClassType::Interface;
            // The C# `modifiers &= ~Modifiers.Abstract;` -- an interface is
            // never rendered `abstract` (it is implicitly abstract).
            modifiers = modifiers & ~Modifiers::Abstract;
            break;
        case TS::TypeKind::Delegate: {
            const TS::IMethod* invoke = GetDelegateInvokeMethod(typeDefinition);
            if (invoke != nullptr)
                return ConvertDelegate(*invoke, modifiers);
            // The C# `goto default;` (a delegate-kind definition whose Invoke
            // did not resolve) -- C++ has no `goto default`, so the default
            // arm's body repeats inline (the goto-case-Plus fallthrough
            // convention).
            classType = ClassType::Class;
            if (SupportRecordClasses() && typeDefinition.IsRecord())
                classType = ClassType::RecordClass;
            break;
        }
        default:
            classType = ClassType::Class;
            if (SupportRecordClasses() && typeDefinition.IsRecord())
                classType = ClassType::RecordClass;
            break;
    }

    auto* decl = new TypeDeclaration();
    decl->ClassType(classType);
    decl->Modifiers(modifiers);
    if (ShowAttributes()) {
        for (AttributeSection* section : ConvertAttributes(typeDefinition.GetAttributes()))
            decl->Attributes().Add(section);
    }
    if (AddResolveResultAnnotations()) {
        decl->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
            std::const_pointer_cast<TS::IType>(typeDefinition.shared_from_this())));
    }
    // The C# `decl.Name = typeDefinition.Name == "_" ? "@_" :
    // typeDefinition.Name;` -- the `_` discard identifier renders escaped; the
    // `Name` setter routes through `Identifier::Create`, which strips the `@`
    // into the token's verbatim flag.
    decl->Name(typeDefinition.Name() == "_" ? "@_" : typeDefinition.Name());

    const int outerTypeParameterCount =
        typeDefinition.DeclaringTypeDefinition() == nullptr
            ? 0
            : typeDefinition.DeclaringTypeDefinition()->TypeParameterCount();

    if (ShowTypeParameters()) {
        // The C# `typeDefinition.TypeParameters.Skip(outerTypeParameterCount)`
        // -- the outer type's parameters are not redeclared.
        const std::vector<const TS::ITypeParameter*> typeParameters =
            typeDefinition.TypeParameters();
        for (size_t i = static_cast<size_t>(outerTypeParameterCount);
             i < typeParameters.size(); i++) {
            if (typeParameters[i] == nullptr)
                continue; // the D516 null-entry guard
            decl->TypeParameters().Add(ConvertTypeParameter(*typeParameters[i]));
        }
    }

    if (ShowBaseTypes()) {
        // The C# `MemberLookup baseListLookup = new MemberLookup(
        // typeDefinition.DeclaringTypeDefinition, typeDefinition.ParentModule);`.
        Resolver::MemberLookup baseListLookup(typeDefinition.DeclaringTypeDefinition(),
                                              typeDefinition.ParentModule());
        // `DirectBaseTypes()` returns the vector BY VALUE; bind the snapshot
        // before the loop (the by-value-accessor hazard).
        const std::vector<TS::ITypePtr> baseTypes = typeDefinition.DirectBaseTypes();
        for (const TS::ITypePtr& baseType : baseTypes) {
            if (!baseType)
                continue; // the D516 null-entry guard
            // Interfaces enter the interface-impl metadata transitively, so
            // entries the base list cannot name can be dropped; a base class
            // was always written explicitly and stays even if C# could not
            // name it (the C# comment).
            if (baseType->Kind() == TS::TypeKind::Interface
                && !BaseTypeAccessibleFrom(*baseType, typeDefinition,
                                           baseListLookup)) {
                continue;
            }
            if (typeDefinition.Kind() == TS::TypeKind::Enum
                && IsKnownType(*baseType, TS::KnownTypeCode::Enum)) {
                // If the declared type is an enum, replace all references to
                // System.Enum with the enum-underlying type (the default int
                // underlying renders nothing).
                const TS::ITypePtr& underlying = typeDefinition.EnumUnderlyingType();
                // The C# derefs the non-null-asserted `EnumUnderlyingType` on
                // the enum shape; the port guards the degenerate stub (a null
                // underlying renders nothing, the D516 safe-fallback
                // convention).
                if (underlying && !IsKnownType(*underlying,
                                               TS::KnownTypeCode::Int32)) {
                    decl->BaseTypes().Add(ConvertType(*underlying));
                }
            } else if ((typeDefinition.Kind() == TS::TypeKind::Struct
                        || typeDefinition.Kind() == TS::TypeKind::Void)
                       && IsKnownType(*baseType, TS::KnownTypeCode::ValueType)) {
                // If the declared type is a struct, ignore System.ValueType.
                continue;
            } else if (IsKnownType(*baseType, TS::KnownTypeCode::Object)) {
                // Always ignore System.Object.
                continue;
            } else if (SupportRecordClasses() && typeDefinition.IsRecord()
                       && baseType->Name() == "IEquatable"
                       && baseType->Namespace() == "System"
                       && RecordIEquatableOmitted(*baseType, typeDefinition)) {
                // Omit "IEquatable<R>" in records.
                continue;
            } else {
                decl->BaseTypes().Add(ConvertType(*baseType));
            }
        }
    }

    if (ShowTypeParameters() && ShowTypeParameterConstraints()) {
        const std::vector<const TS::ITypeParameter*> typeParameters =
            typeDefinition.TypeParameters();
        for (size_t i = static_cast<size_t>(outerTypeParameterCount);
             i < typeParameters.size(); i++) {
            if (typeParameters[i] == nullptr)
                continue; // the D516 null-entry guard
            Constraint* constraint = ConvertTypeParameterConstraint(*typeParameters[i]);
            if (constraint != nullptr)
                decl->Constraints().Add(constraint);
        }
    }
    return decl;
}

// ---------------------------------------------------------------------------
// The "Convert Entity" dispatch entries (C# lines 1829-1901 + 2766-2769) --
// see the header for the port conventions.
// ---------------------------------------------------------------------------

// The C# `public AstNode ConvertSymbol(ISymbol symbol)` (line 1829).
AstNode* TypeSystemAstBuilder::ConvertSymbol(const TS::ISymbol& symbol) const {
    switch (symbol.SymbolKind()) {
        case TS::SymbolKind::Namespace:
            // The C# `(INamespace)symbol` hard cast: the reference-form
            // dynamic_cast throws std::bad_cast on a mismatch (the C#
            // InvalidCastException analog; unreachable for a real symbol whose
            // SymbolKind is authoritative).
            return ConvertNamespaceDeclaration(dynamic_cast<const TS::INamespace&>(symbol));
        case TS::SymbolKind::Variable:
            return ConvertVariable(dynamic_cast<const TS::IVariable&>(symbol));
        case TS::SymbolKind::Parameter:
            return ConvertParameter(dynamic_cast<const TS::IParameter&>(symbol));
        case TS::SymbolKind::TypeParameter:
            return ConvertTypeParameter(dynamic_cast<const TS::ITypeParameter&>(symbol));
        default: {
            // The C# `symbol as IEntity` (null on a non-entity) ports to a
            // pointer-form dynamic_cast.
            const auto* entity = dynamic_cast<const TS::IEntity*>(&symbol);
            if (entity != nullptr)
                return ConvertEntity(*entity);
            throw std::invalid_argument(
                "Invalid value for SymbolKind: "
                + std::to_string(static_cast<int>(symbol.SymbolKind())));
        }
    }
}

// The C# `public EntityDeclaration ConvertEntity(IEntity entity)` (line 1851).
EntityDeclaration* TypeSystemAstBuilder::ConvertEntity(const TS::IEntity& entity) const {
    switch (entity.SymbolKind()) {
        case TS::SymbolKind::TypeDefinition:
            return ConvertTypeDefinition(dynamic_cast<const TS::ITypeDefinition&>(entity));
        case TS::SymbolKind::Field:
            return ConvertField(dynamic_cast<const TS::IField&>(entity));
        case TS::SymbolKind::Property:
            return ConvertProperty(dynamic_cast<const TS::IProperty&>(entity));
        case TS::SymbolKind::Indexer:
            return ConvertIndexer(dynamic_cast<const TS::IProperty&>(entity));
        case TS::SymbolKind::Event:
            return ConvertEvent(dynamic_cast<const TS::IEvent&>(entity));
        case TS::SymbolKind::Method:
            return ConvertMethod(dynamic_cast<const TS::IMethod&>(entity));
        case TS::SymbolKind::Operator:
            return ConvertOperator(dynamic_cast<const TS::IMethod&>(entity));
        case TS::SymbolKind::Constructor:
            return ConvertConstructor(dynamic_cast<const TS::IMethod&>(entity));
        case TS::SymbolKind::Destructor:
            return ConvertDestructor(dynamic_cast<const TS::IMethod&>(entity));
        case TS::SymbolKind::Accessor: {
            const TS::IMethod& accessor = dynamic_cast<const TS::IMethod&>(entity);
            // The C# `accessor.AccessorOwner is IProperty owner &&
            // owner.IsParameterizedProperty()`: C# cannot represent the
            // parameterized property itself, so its accessors are declared as
            // ordinary methods.
            const TS::IMember* owner = accessor.AccessorOwner();
            const auto* propertyOwner = dynamic_cast<const TS::IProperty*>(owner);
            if (propertyOwner != nullptr && TS::IsParameterizedProperty(*propertyOwner))
                return ConvertMethod(accessor);
            // The C# `accessor.AccessorOwner?.Accessibility ?? Accessibility.None`.
            const TS::Accessibility ownerAccessibility =
                owner != nullptr ? owner->Accessibility() : TS::Accessibility::None;
            // The C# null-forgiving `!`: the accessor is non-null here, so
            // ConvertAccessor returns a non-null node (its null return happens
            // only for a null accessor parameter).
            return ConvertAccessor(&accessor, accessor.AccessorKind(), ownerAccessibility, false);
        }
        default:
            throw std::invalid_argument(
                "Invalid value for SymbolKind: "
                + std::to_string(static_cast<int>(entity.SymbolKind())));
    }
}

// The C# `public EntityDeclaration ConvertExtension((IMethod MarkerMethod,
// IReadOnlyList<ITypeParameter> TypeParameters) group)` (line 1890).
EntityDeclaration* TypeSystemAstBuilder::ConvertExtension(const ExtensionGroup& group) const {
    auto* ext = new ExtensionDeclaration();
    // The C# `var subst = new TypeParameterSubstitution(group.TypeParameters, [])` -- the
    // group's type parameters as the CLASS type arguments (the marker method's parameter
    // types reference container-owned type parameters; the specialization re-points them
    // at the group's freshly declared ones) and a PRESENT-but-EMPTY method list (every
    // method-owned index is out of range, faithfully matching the C# empty-array
    // `IReadOnlyList<IType>`). The owning handles for the substitution list come from the
    // non-const shared_from_this + const_pointer_cast pair (the D529 convention -- every
    // type parameter is shared-managed).
    std::vector<TS::ITypePtr> classTypeArguments;
    classTypeArguments.reserve(group.second.size());
    for (const TS::ITypeParameter* tp : group.second) {
        if (tp == nullptr)
            continue; // the D516 null-entry guard
        classTypeArguments.push_back(
            std::const_pointer_cast<TS::IType>(tp->shared_from_this()));
    }
    const TS::TypeParameterSubstitution substitution(std::move(classTypeArguments),
                                                     std::vector<TS::ITypePtr>{});
    for (const TS::ITypeParameter* tp : group.second) {
        if (tp == nullptr)
            continue; // the D516 null-entry guard
        ext->TypeParameters().Add(ConvertTypeParameter(*tp));
    }
    // The C# `group.MarkerMethod.Specialize(subst).Parameters.Single()` -- the
    // `Single()` throws InvalidOperationException on anything but exactly one entry,
    // ported as std::runtime_error (the CreateResolveResult InvalidOperationException
    // precedent).
    const TS::IMethod* specialized = group.first->Specialize(&substitution);
    const std::vector<const TS::IParameter*> parameters = specialized->Parameters();
    if (parameters.size() != 1)
        throw std::runtime_error(
            "ConvertExtension: the marker method must have exactly one parameter");
    ext->ReceiverParameters().Add(ConvertParameter(*parameters[0]));
    // The C# `.OfType<Constraint>()` over the nullable ConvertTypeParameterConstraint
    // results: every result is a Constraint or null, so the filter is the null check.
    for (const TS::ITypeParameter* tp : group.second) {
        if (tp == nullptr)
            continue; // the D516 null-entry guard
        Constraint* constraint = ConvertTypeParameterConstraint(*tp);
        if (constraint != nullptr)
            ext->Constraints().Add(constraint);
    }
    return ext;
}

// The C# `NamespaceDeclaration ConvertNamespaceDeclaration(INamespace ns)` (line 2766).
NamespaceDeclaration* TypeSystemAstBuilder::ConvertNamespaceDeclaration(
    const TS::INamespace& ns) const {
    return new NamespaceDeclaration(ns.FullName());
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
