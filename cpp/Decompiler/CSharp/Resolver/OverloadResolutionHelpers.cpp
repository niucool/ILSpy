// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `OverloadResolution` private helpers operating on a `Candidate`. See the header.

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions (ImplicitConversion(ResolveResult, IType) / IsConstraintConvertible)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"  // Detail::IdentityConversion (the BetterParamsCollectionType span arms)
#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"  // ILiftedOperator (the BetterFunctionMember non-lifted-operator tiebreak)
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"  // TooManyPositionalArguments / NoParameterFoundForNamedArgument
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"  // dynamic_cast<ByReferenceResolveResult>
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions::None() (the unmapped-argument conversion)
#include "Decompiler/Semantics/OutVarResolveResult.hpp"  // dynamic_cast<OutVarResolveResult>
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (arguments element)
#include "Decompiler/TypeSystem/Accessibility.hpp"  // Accessibility::Public (the ctor filter)
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // Member->Parameters (specialized)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter::Type / ReferenceKind
#include "Decompiler/TypeSystem/IType.hpp"  // ArrayType / ParameterizedType / ByReferenceType / TypeKind / GetMemberOptions
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition::IsAbstract (the new() constraint)
#include "Decompiler/TypeSystem/ITypeParameter.hpp"  // ITypeParameter (the constraint flags / DirectBaseTypes)
#include "Decompiler/TypeSystem/IMethod.hpp"  // IMethod::Parameters / Accessibility (the ctor filter)
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"  // SpanOfT / ReadOnlySpanOfT
#include "Decompiler/TypeSystem/NullableType.hpp"  // IsNonNullableValueType (the struct constraint)
#include "Decompiler/TypeSystem/ReferenceKind.hpp"  // ReferenceKind
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // IsKnownType / IsArrayInterfaceType / SkipModifiers
#include "Decompiler/TypeSystem/TypeVisitor.hpp"  // TypeVisitor (the substitution)

#include <algorithm>  // std::min (the MoreSpecificFormalParameters Zip-stops-at-shorter)
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

bool ResolveParameterTypes(OverloadResolutionCandidate& candidate, bool useSpecializedParameters) {
    using namespace ILSpy::Decompiler::TypeSystem;
    auto& parameterTypes = candidate.ParameterTypes();
    const auto& parameters = candidate.Parameters();
    for (std::size_t i = 0; i < parameters.size(); i++) {
        ITypePtr type;
        if (useSpecializedParameters) {
            // The parameter type of the specialized non-generic method or indexer.
            // (The C# `Debug.Assert(!candidate.IsGenericMethod)` -- not asserted in the port.)
            type = std::const_pointer_cast<IType>(
                const_cast<IType&>(candidate.Member()->Parameters()[i]->Type()).shared_from_this());
        } else {
            // The type of the original formal parameter (before any substitution).
            type = std::const_pointer_cast<IType>(
                const_cast<IType&>(parameters[i]->Type()).shared_from_this());
        }
        if (candidate.IsExpandedForm() && i == parameters.size() - 1) {
            // Unpack the params-array/Span/array-interface into its element type.
            const auto* arrayType = dynamic_cast<const ArrayType*>(type.get());
            if (arrayType != nullptr && arrayType->Rank() == 1) {
                type = arrayType->Element();  // the C# `arrayType.ElementType`
            } else if (IsKnownType(*type, KnownTypeCode::ReadOnlySpanOfT) ||
                       IsKnownType(*type, KnownTypeCode::SpanOfT)) {
                // `Span<T>` / `ReadOnlySpan<T>` are `ParameterizedType`s; `type.TypeArguments[0]`.
                const auto* pt = dynamic_cast<const ParameterizedType*>(type.get());
                if (pt == nullptr || pt->TypeArguments().empty()) return false;
                type = pt->TypeArguments()[0];
            } else if (IsArrayInterfaceType(*type)) {
                // `IEnumerable<T>` etc. are `ParameterizedType`s; `type.TypeArguments[0]`.
                const auto* pt = dynamic_cast<const ParameterizedType*>(type.get());
                if (pt == nullptr || pt->TypeArguments().empty()) return false;
                type = pt->TypeArguments()[0];
            } else {
                // Error: cannot unpack params-array. Abort considering the expanded form for this candidate.
                return false;
            }
        }
        parameterTypes[i] = type;
    }
    return true;
}

void MapCorrespondingParameters(OverloadResolutionCandidate& candidate,
                                std::size_t argumentCount,
                                const std::vector<std::string>& argumentNames) {
    using namespace ILSpy::Decompiler::TypeSystem;
    auto& map = candidate.ArgumentToParameterMap();
    map.assign(argumentCount, -1);  // the C# `new int[arguments.Length]` (init -1 in the loop)
    const auto& parameterTypes = candidate.ParameterTypes();
    const auto parameterTypesLen = parameterTypes.size();
    bool hasPositionalArgument = false;
    // Go backwards, so `hasPositionalArgument` tells us whether there are non-trailing named args.
    for (std::size_t i = argumentCount; i-- > 0;) {
        map[i] = -1;
        const std::string& argName = (i < argumentNames.size()) ? argumentNames[i] : std::string{};
        if (argName.empty() || hasPositionalArgument) {
            hasPositionalArgument = true;
            if (i < parameterTypesLen) {
                map[i] = static_cast<int>(i);
                if (!argName.empty() && argName != candidate.Parameters()[i]->Name()) {
                    // Non-trailing named argument must match name.
                    candidate.AddError(OverloadResolutionErrors::NoParameterFoundForNamedArgument);
                }
            } else if (candidate.IsExpandedForm()) {
                map[i] = static_cast<int>(parameterTypesLen - 1);
                if (!argName.empty()) {
                    // Can't use a non-trailing named argument here.
                    candidate.AddError(OverloadResolutionErrors::NoParameterFoundForNamedArgument);
                }
            } else {
                candidate.AddError(OverloadResolutionErrors::TooManyPositionalArguments);
            }
        } else {
            // (Trailing) named argument -- scan all parameters for a name match (last match wins).
            int matchedIndex = -1;
            for (std::size_t j = 0; j < candidate.Parameters().size(); j++) {
                if (argName == candidate.Parameters()[j]->Name()) {
                    matchedIndex = static_cast<int>(j);
                }
            }
            map[i] = matchedIndex;
            if (map[i] < 0) {
                candidate.AddError(OverloadResolutionErrors::NoParameterFoundForNamedArgument);
            }
        }
    }
}

void CheckApplicabilityArgumentCounts(OverloadResolutionCandidate& candidate,
                                      bool allowOptionalParameters) {
    // C# 4.0 spec section 7.5.3.1 "Applicable function member" -- test whether parameters were mapped
    // the correct number of arguments.
    const auto& parameterTypes = candidate.ParameterTypes();
    const std::size_t paramCount = parameterTypes.size();
    std::vector<int> argumentCountPerParameter(paramCount, 0);
    for (int parameterIndex : candidate.ArgumentToParameterMap()) {
        if (parameterIndex >= 0) {
            argumentCountPerParameter[static_cast<std::size_t>(parameterIndex)]++;
        }
    }
    for (std::size_t i = 0; i < argumentCountPerParameter.size(); i++) {
        if (candidate.IsExpandedForm() && i == argumentCountPerParameter.size() - 1) {
            continue;  // any number of arguments is fine for the params-array
        }
        if (argumentCountPerParameter[i] == 0) {
            if (allowOptionalParameters && candidate.Parameters()[i]->IsOptional()) {
                candidate.HasUnmappedOptionalParameters() = true;
            } else {
                candidate.AddError(OverloadResolutionErrors::MissingArgumentForRequiredParameter);
            }
        } else if (argumentCountPerParameter[i] > 1) {
            candidate.AddError(OverloadResolutionErrors::MultipleArgumentsForSingleParameter);
        }
    }
}

void CheckApplicabilityPassingModeAndConversions(
    OverloadResolutionCandidate& candidate,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    CSharpConversions& conversions,
    bool allowImplicitIn,
    bool isExtensionMethodInvocation) {
    using namespace ILSpy::Decompiler::TypeSystem;
    using ILSpy::Decompiler::Semantics::ByReferenceResolveResult;
    using ILSpy::Decompiler::Semantics::Conversion;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::Semantics::OutVarResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    // The C# `candidate.ArgumentConversions = new Conversion[arguments.Length]` -- sized, null entries
    // filled below (the unmapped-argument early-out sets `None`).
    auto& argumentConversions = candidate.ArgumentConversions();
    argumentConversions.assign(arguments.size(), nullptr);

    for (std::size_t i = 0; i < arguments.size(); i++) {
        const auto& argumentMap = candidate.ArgumentToParameterMap();
        int parameterIndex = argumentMap[i];
        if (parameterIndex < 0) {
            argumentConversions[i] = Conversions::None();
            continue;
        }
        auto paramIdx = static_cast<std::size_t>(parameterIndex);

        ReferenceKind paramRefKind = candidate.Parameters()[paramIdx]->ReferenceKind();
        const auto* brrr = dynamic_cast<const ByReferenceResolveResult*>(arguments[i].get());
        const auto* outVar = dynamic_cast<const OutVarResolveResult*>(arguments[i].get());

        if (brrr != nullptr) {
            // The argument is a `ref`/`out`/`in` directionExpression -- its `ReferenceKind` must match
            // the parameter's.
            if (brrr->ReferenceKind() != paramRefKind)
                candidate.AddError(OverloadResolutionErrors::ParameterPassingModeMismatch);
        } else if (outVar != nullptr) {
            // `out var decl` arguments are compatible with any `out` parameter; the conversion is NOT
            // checked (the `out var` carries no type to convert).
            if (paramRefKind != ReferenceKind::Out)
                candidate.AddError(OverloadResolutionErrors::ParameterPassingModeMismatch);
            continue;
        } else {
            // AllowImplicitIn: `in` parameters can be filled implicitly without `in` DirectionExpression.
            // IsExtensionMethodInvocation: `this ref` / `this in` parameters can be filled implicitly.
            bool inOrRefReadOnly = (paramRefKind == ReferenceKind::In || paramRefKind == ReferenceKind::RefReadOnly);
            bool extThisRef = (isExtensionMethodInvocation && parameterIndex == 0 &&
                               (paramRefKind == ReferenceKind::In ||
                                paramRefKind == ReferenceKind::Ref ||
                                paramRefKind == ReferenceKind::RefReadOnly));
            if ((inOrRefReadOnly && allowImplicitIn) || extThisRef) {
                // The C# `candidate.ParameterTypes[parameterIndex].SkipModifiers() is ByReferenceType brt`
                // -- strip the modopt/modreq decorators, then unwrap a `ByReferenceType` to its element so
                // the parameter is treated as not declared `in` for the following steps.
                const IType* stripped = SkipModifiers(*candidate.ParameterTypes()[paramIdx]);
                const auto* brt = dynamic_cast<const ByReferenceType*>(stripped);
                if (brt != nullptr) {
                    candidate.ParameterTypes()[paramIdx] = brt->Element();
                } else if (paramRefKind != ReferenceKind::None) {
                    candidate.AddError(OverloadResolutionErrors::ParameterPassingModeMismatch);
                }
            } else if (paramRefKind != ReferenceKind::None) {
                candidate.AddError(OverloadResolutionErrors::ParameterPassingModeMismatch);
            }
        }

        ITypePtr& parameterTypePtr = candidate.ParameterTypes()[paramIdx];
        IType& parameterType = *parameterTypePtr;
        std::shared_ptr<Conversion> c = conversions.ImplicitConversion(*arguments[i], parameterType);
        argumentConversions[i] = c;
        if (isExtensionMethodInvocation && parameterIndex == 0) {
            // First parameter to extension method must be an identity, reference, boxing or span
            // conversion -- pointer-identity against the four singletons (the C# reference-equality
            // `c == Conversion.IdentityConversion || ...`).
            if (!(c.get() == Conversions::IdentityConversion().get() ||
                  c.get() == Conversions::ImplicitReferenceConversion().get() ||
                  c.get() == Conversions::BoxingConversion().get() ||
                  c.get() == Conversions::ImplicitSpanConversion().get()))
                candidate.AddError(OverloadResolutionErrors::ArgumentTypeMismatch);
        } else {
            if ((!c->IsValid() && !c->IsUserDefined() && !c->IsMethodGroupConversion()) &&
                parameterType.Kind() != TypeKind::Unknown) {
                candidate.AddError(OverloadResolutionErrors::ArgumentTypeMismatch);
            }
        }
    }
}

// The C# `TypeWithElementType` abstract base (Array/ByReference/Pointer/ModOpt/ModReq -- PinnedType
// is not ported) is flattened in the minimal port: each concrete leaf carries its own `Element()`.
// This dispatches on `Kind()` and returns the element type for the TypeWithElementType leaves, else
// `nullptr` -- the faithful C# `t as TypeWithElementType` + `.ElementType` (a non-`TypeWithElementType`
// kind, or a degenerate leaf with a null element, yields `nullptr`, matching the C# `as` returning
// `null` and avoiding a UB deref of a null element the real type system never produces).
static const ILSpy::Decompiler::TypeSystem::IType* ElementTypeOf(
    const ILSpy::Decompiler::TypeSystem::IType& type) {
    using namespace ILSpy::Decompiler::TypeSystem;
    switch (type.Kind()) {
        case TypeKind::Array:
            if (const auto* a = dynamic_cast<const ArrayType*>(&type)) {
                const ITypePtr& e = a->Element();
                return e ? e.get() : nullptr;
            }
            return nullptr;
        case TypeKind::ByReference:
            if (const auto* b = dynamic_cast<const ByReferenceType*>(&type)) {
                const ITypePtr& e = b->Element();
                return e ? e.get() : nullptr;
            }
            return nullptr;
        case TypeKind::Pointer:
            if (const auto* p = dynamic_cast<const PointerType*>(&type)) {
                const ITypePtr& e = p->Element();
                return e ? e.get() : nullptr;
            }
            return nullptr;
        case TypeKind::ModOpt:
        case TypeKind::ModReq:
            if (const auto* m = dynamic_cast<const ModifiedType*>(&type)) {
                const ITypePtr& e = m->Element();
                return e ? e.get() : nullptr;
            }
            return nullptr;
        default:
            return nullptr;
    }
}

bool IsArrayOrArrayInterfaceType(const ILSpy::Decompiler::TypeSystem::IType& type,
                                 const ILSpy::Decompiler::TypeSystem::IType*& elementType) {
    using namespace ILSpy::Decompiler::TypeSystem;
    elementType = nullptr;
    // C# `if (type is ArrayType arrayType) { elementType = arrayType.ElementType; return true; }`.
    if (const auto* arrayType = dynamic_cast<const ArrayType*>(&type)) {
        elementType = arrayType->Element().get();
        return true;
    }
    // C# `if (type.IsArrayInterfaceType()) { elementType = type.TypeArguments[0]; return true; }`.
    // `TypeArguments` is `ParameterizedType`-specific in the port (not on the `IType` surface), so
    // dynamic-cast + guard. `IsArrayInterfaceType` requires `TypeParameterCount == 1`, so a real
    // array-interface always has exactly one type argument; the guard defends a degenerate stub.
    if (IsArrayInterfaceType(type)) {
        const auto* pt = dynamic_cast<const ParameterizedType*>(&type);
        if (pt != nullptr && !pt->TypeArguments().empty()) {
            elementType = pt->TypeArguments()[0].get();
            return true;
        }
        return false;
    }
    return false;
}

int MoreSpecificFormalParameter(const ILSpy::Decompiler::TypeSystem::IType& t1,
                                const ILSpy::Decompiler::TypeSystem::IType& t2) {
    using namespace ILSpy::Decompiler::TypeSystem;
    // C# `if ((t1 is ITypeParameter) && !(t2 is ITypeParameter)) return 2;` -- a type parameter is
    // LESS specific than a non-type-parameter, so the NON-type-parameter side (t2) wins (return 2).
    if (t1.Kind() == TypeKind::TypeParameter && t2.Kind() != TypeKind::TypeParameter)
        return 2;
    // C# `if ((t2 is ITypeParameter) && !(t1 is ITypeParameter)) return 1;`
    if (t2.Kind() == TypeKind::TypeParameter && t1.Kind() != TypeKind::TypeParameter)
        return 1;

    // C# `ParameterizedType p1 = t1 as ParameterizedType; ParameterizedType p2 = t2 as ParameterizedType;`
    const auto* p1 = dynamic_cast<const ParameterizedType*>(&t1);
    const auto* p2 = dynamic_cast<const ParameterizedType*>(&t2);
    if (p1 != nullptr && p2 != nullptr && p1->TypeParameterCount() == p2->TypeParameterCount()) {
        // C# `int r = MoreSpecificFormalParameters(p1.TypeArguments, p2.TypeArguments); if (r > 0) return r;`
        // -- falls through to the TypeWithElementType check when `r == 0` (a ParameterizedType is not a
        // TypeWithElementType, so the fall-through is harmless).
        const auto& ta1 = p1->TypeArguments();
        const auto& ta2 = p2->TypeArguments();
        std::vector<const IType*> v1;
        std::vector<const IType*> v2;
        v1.reserve(ta1.size());
        v2.reserve(ta2.size());
        for (const auto& a : ta1) v1.push_back(a.get());
        for (const auto& b : ta2) v2.push_back(b.get());
        int r = MoreSpecificFormalParameters(v1, v2);
        if (r > 0)
            return r;
    }
    // C# `TypeWithElementType tew1 = t1 as TypeWithElementType; ... if (tew1 != null && tew2 != null)
    // return MoreSpecificFormalParameter(tew1.ElementType, tew2.ElementType);`
    const IType* e1 = ElementTypeOf(t1);
    const IType* e2 = ElementTypeOf(t2);
    if (e1 != nullptr && e2 != nullptr) {
        return MoreSpecificFormalParameter(*e1, *e2);
    }
    return 0;
}

int MoreSpecificFormalParameters(const std::vector<const ILSpy::Decompiler::TypeSystem::IType*>& t1,
                                 const std::vector<const ILSpy::Decompiler::TypeSystem::IType*>& t2) {
    // C# `t1.Zip(t2, ...)` stops at the shorter sequence (an `IEnumerable` `Zip` yields
    // `min(count1, count2)` pairs).
    bool c1IsBetter = false;
    bool c2IsBetter = false;
    const std::size_t n = std::min(t1.size(), t2.size());
    for (std::size_t i = 0; i < n; i++) {
        switch (MoreSpecificFormalParameter(*t1[i], *t2[i])) {
            case 1: c1IsBetter = true; break;
            case 2: c2IsBetter = true; break;
            default: break;
        }
    }
    if (c1IsBetter && !c2IsBetter) return 1;
    if (!c1IsBetter && c2IsBetter) return 2;
    return 0;
}

int MoreSpecificFormalParameters(const OverloadResolutionCandidate& c1,
                                 const OverloadResolutionCandidate& c2) {
    using namespace ILSpy::Decompiler::TypeSystem;
    // C# `int r = c1.Parameters.Count.CompareTo(c2.Parameters.Count);` -- prefer the member with MORE
    // formal parameters (in case both have different numbers of optional parameters).
    const auto& params1 = c1.Parameters();
    const auto& params2 = c2.Parameters();
    if (params1.size() > params2.size())
        return 1;
    if (params1.size() < params2.size())
        return 2;
    // C# `c1.Parameters.Select(p => p.Type)` -- build the two type sequences (non-owning `const IType*`
    // per parameter, the `MoreSpecificFormalParameters(IEnumerable<IType>)` overload convention) and
    // delegate to the type-sequence tiebreak.
    std::vector<const IType*> t1;
    std::vector<const IType*> t2;
    t1.reserve(params1.size());
    t2.reserve(params2.size());
    for (const auto* p : params1) t1.push_back(&p->Type());
    for (const auto* p : params2) t2.push_back(&p->Type());
    return MoreSpecificFormalParameters(t1, t2);
}

int BetterParameterPassingChoice(const OverloadResolutionCandidate& c1,
                                 const OverloadResolutionCandidate& c2) {
    using namespace ILSpy::Decompiler::TypeSystem;
    // C# `Debug.Assert(c1.Parameters.Count == c2.Parameters.Count, ...)` -- the port assumes the equal
    // count (the caller `BetterFunctionMember` reaches this only after the formal-parameter tiebreaks
    // established equal arity); the loop bounds at the smaller count to avoid an out-of-range read if a
    // degenerate stub violates the invariant (the C# `Debug.Assert` is a debug-only precondition).
    bool c1IsBetter = false;
    bool c2IsBetter = false;
    const auto& params1 = c1.Parameters();
    const auto& params2 = c2.Parameters();
    const std::size_t n = std::min(params1.size(), params2.size());
    for (std::size_t i = 0; i < n; i++) {
        ReferenceKind refKind1 = params1[i]->ReferenceKind();
        ReferenceKind refKind2 = params2[i]->ReferenceKind();
        // by-value (`None`) is better than `in`; the two `if`s (NOT `else if`) mirror the C# so a
        // position where c1 is by-value/in (c2 in/by-value) sets BOTH flags (a mixed verdict).
        if (refKind1 == ReferenceKind::None && refKind2 == ReferenceKind::In)
            c1IsBetter = true;
        if (refKind1 == ReferenceKind::In && refKind2 == ReferenceKind::None)
            c2IsBetter = true;
    }
    if (c1IsBetter && !c2IsBetter) return 1;
    if (!c1IsBetter && c2IsBetter) return 2;
    return 0;
}

int BetterParamsCollectionType(CSharpConversions& conversions,
                               ILSpy::Decompiler::TypeSystem::IType& paramsCollectionType1,
                               ILSpy::Decompiler::TypeSystem::IType& paramsCollectionType2) {
    using namespace ILSpy::Decompiler::TypeSystem;
    using ILSpy::Decompiler::CSharp::Resolver::Detail::IdentityConversion;

    // C# `bool isSpan1 = ...IsKnownType(SpanOfT) || ...IsKnownType(ReadOnlySpanOfT);` etc. -- the free
    // `IsKnownType` reads the type's own `GetDefinition()?.KnownTypeCode` (no `FindType`).
    bool isSpan1 = IsKnownType(paramsCollectionType1, KnownTypeCode::SpanOfT) ||
                  IsKnownType(paramsCollectionType1, KnownTypeCode::ReadOnlySpanOfT);
    bool isSpan2 = IsKnownType(paramsCollectionType2, KnownTypeCode::SpanOfT) ||
                  IsKnownType(paramsCollectionType2, KnownTypeCode::ReadOnlySpanOfT);

    // A small helper to extract the first type argument of a `Span<T>`/`ReadOnlySpan<T>` -- the C#
    // `t.TypeArguments[0]` where `t` is a `ParameterizedType`. The port's `TypeArguments()` is
    // `ParameterizedType`-specific (not on the `IType` surface), so the `dynamic_cast` + guard avoids
    // UB on a degenerate stub (the D516 null-guard precedent). Returns null when `t` is not a
    // parameterized type or carries no type arguments; the caller guards before dereferencing.
    auto firstTypeArg = [](IType& t) -> IType* {
        auto* pt = dynamic_cast<ParameterizedType*>(&t);
        if (pt == nullptr || pt->TypeArguments().empty())
            return nullptr;
        return pt->TypeArguments()[0].get();
    };

    // The C# `out var elementType2`/`out var elementType1` locals, declared before the `else if`
    // chain (C++ `else if` conditions cannot declare inline like C# `out var`).
    // `IsArrayOrArrayInterfaceType` resets the out param to `nullptr` at the top, so reusing the
    // locals across the chain is safe (no stale value).
    const IType* elementType2 = nullptr;
    const IType* elementType1 = nullptr;

    if (!isSpan1 && !isSpan2) {
        // C# `conversions.ImplicitConversion(p1, p2).IsValid` -- the cached public entry. The cache
        // is local to the `CSharpConversions` instance (the `OverloadResolution.conversions` field);
        // a test-constructed instance's cache dies with the instance (no cross-test dangling, unlike
        // `CSharpConversions::Get`, the D528/D540 caveat).
        bool implicitConversion1to2 = conversions.ImplicitConversion(
            paramsCollectionType1, paramsCollectionType2)->IsValid();
        bool implicitConversion2to1 = conversions.ImplicitConversion(
            paramsCollectionType2, paramsCollectionType1)->IsValid();
        if (implicitConversion1to2 && !implicitConversion2to1)
            return 1;
        if (!implicitConversion1to2 && implicitConversion2to1)
            return 2;
    }
    else if (IsKnownType(paramsCollectionType1, KnownTypeCode::ReadOnlySpanOfT) &&
             IsKnownType(paramsCollectionType2, KnownTypeCode::SpanOfT)) {
        // ReadOnlySpan<T> is better than Span<T> if the element types identity-match.
        IType* a1 = firstTypeArg(paramsCollectionType1);
        IType* a2 = firstTypeArg(paramsCollectionType2);
        if (a1 != nullptr && a2 != nullptr && IdentityConversion(*a1, *a2))
            return 1;
    }
    else if (IsKnownType(paramsCollectionType2, KnownTypeCode::ReadOnlySpanOfT) &&
             IsKnownType(paramsCollectionType1, KnownTypeCode::SpanOfT)) {
        // The mirror: Span<T> vs ReadOnlySpan<T> -> ReadOnlySpan<T> (the second arg) is better.
        IType* a1 = firstTypeArg(paramsCollectionType1);
        IType* a2 = firstTypeArg(paramsCollectionType2);
        if (a1 != nullptr && a2 != nullptr && IdentityConversion(*a2, *a1))
            return 2;
    }
    else if (isSpan1 && IsArrayOrArrayInterfaceType(paramsCollectionType2, elementType2)) {
        // Span<T>/ReadOnlySpan<T> is better than an array/array-interface if the element types
        // identity-match.
        IType* a1 = firstTypeArg(paramsCollectionType1);
        if (a1 != nullptr && elementType2 != nullptr &&
            IdentityConversion(*a1, const_cast<IType&>(*elementType2)))
            return 1;
    }
    else if (isSpan2 && IsArrayOrArrayInterfaceType(paramsCollectionType1, elementType1)) {
        // The mirror: the array is the first arg, the span is the second -> the span wins (return 2).
        IType* a2 = firstTypeArg(paramsCollectionType2);
        if (a2 != nullptr && elementType1 != nullptr &&
            IdentityConversion(*a2, const_cast<IType&>(*elementType1)))
            return 2;
    }
    return 0;
}

int BetterFunctionMember(CSharpConversions& conversions,
                        const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
                        const OverloadResolutionCandidate& c1,
                        const OverloadResolutionCandidate& c2) {
    using namespace ILSpy::Decompiler::TypeSystem;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    // C# `if (c1.ErrorCount == 0 && c2.ErrorCount > 0) return 1;` etc. -- the "prefer applicable
    // members" heuristic (produces a best candidate even if none is applicable).
    if (c1.ErrorCount() == 0 && c2.ErrorCount() > 0)
        return 1;
    if (c1.ErrorCount() > 0 && c2.ErrorCount() == 0)
        return 2;

    // C# 4.0 spec section 7.5.3.2 "Better function member" -- the per-argument better-conversion
    // loop. `c1IsBetter`/`c2IsBetter` are the direction-exclusive flags; `parameterTypesEqual`
    // tracks whether every mapped-argument pair has identity-convertible formal parameter types
    // (the gate for the tie-breaking rules below).
    bool c1IsBetter = false;
    bool c2IsBetter = false;
    bool parameterTypesEqual = true;
    const auto& map1 = c1.ArgumentToParameterMap();
    const auto& map2 = c2.ArgumentToParameterMap();
    const auto& paramTypes1 = c1.ParameterTypes();
    const auto& paramTypes2 = c2.ParameterTypes();
    for (std::size_t i = 0; i < arguments.size(); i++) {
        // C# `int p1 = c1.ArgumentToParameterMap[i]; int p2 = c2.ArgumentToParameterMap[i];`.
        int p1 = (i < map1.size()) ? map1[i] : -1;
        int p2 = (i < map2.size()) ? map2[i] : -1;
        if (p1 >= 0 && p2 < 0) {
            // c1 maps the argument, c2 does not -> c1 is better for this argument.
            c1IsBetter = true;
        } else if (p1 < 0 && p2 >= 0) {
            // The mirror.
            c2IsBetter = true;
        } else if (p1 >= 0 && p2 >= 0) {
            // Both map the argument -- compare the formal parameter types and the argument's
            // better conversion to the two target types. `ParameterTypes[p]` is sized by the
            // candidate and filled by `ResolveParameterTypes`/`RunTypeInference` before
            // `BetterFunctionMember` runs (after `CheckApplicability`); a defensive null-guard
            // skips the comparison for a degenerate null entry (the C# would NRE on a null
            // `ParameterTypes[p]`, which the real pipeline never produces -- the D516 null-guard
            // precedent), avoiding UB and leaving `parameterTypesEqual`/the better flags unchanged
            // for this argument.
            auto idx1 = static_cast<std::size_t>(p1);
            auto idx2 = static_cast<std::size_t>(p2);
            const ITypePtr& t1 = (idx1 < paramTypes1.size()) ? paramTypes1[idx1] : nullptr;
            const ITypePtr& t2 = (idx2 < paramTypes2.size()) ? paramTypes2[idx2] : nullptr;
            if (t1 && t2) {
                // C# `if (!conversions.IdentityConversion(c1.ParameterTypes[p1], c2.ParameterTypes[p2]))
                // parameterTypesEqual = false;` -- the C# public `IdentityConversion` ports to the
                // `Detail::IdentityConversion` free function (the port has no public method, the
                // D547 precedent). The `ITypePtr` derefs yield non-const `IType&` (the shared_ptr
                // owns a mutable `IType`, the accessor's const is the contract) for `IdentityConversion`'s
                // non-const `IType&` (the non-const `AcceptVisitor`, D406).
                if (!IdentityConversion(*t1, *t2))
                    parameterTypesEqual = false;
                // C# `switch (conversions.BetterConversion(arguments[i], c1.ParameterTypes[p1],
                // c2.ParameterTypes[p2]))` -- the public `BetterConversion(ResolveResult, IType, IType)`
                // entry (D544). `*arguments[i]` is a `ResolveResult&` (the public method takes a const
                // ref); the derefs feed the non-const `IType&` targets.
                switch (conversions.BetterConversion(*arguments[i], *t1, *t2)) {
                    case 1: c1IsBetter = true; break;
                    case 2: c2IsBetter = true; break;
                    default: break;
                }
            } else {
                // A null `ParameterTypes` entry -- the degenerate state; treat the types as not
                // equal so the tie-breaking rules do not fire on incomparable types.
                parameterTypesEqual = false;
            }
        }
    }
    if (c1IsBetter && !c2IsBetter)
        return 1;
    if (!c1IsBetter && c2IsBetter)
        return 2;

    // C# `if (c1.ErrorCount < c2.ErrorCount) return 1;` etc. -- the "prefer members with less
    // errors" heuristic.
    if (c1.ErrorCount() < c2.ErrorCount())
        return 1;
    if (c1.ErrorCount() > c2.ErrorCount())
        return 2;

    if (!c1IsBetter && !c2IsBetter && parameterTypesEqual) {
        // C# `// we need the tie-breaking rules`.

        // Non-generic methods are better.
        if (!c1.IsGenericMethod() && c2.IsGenericMethod())
            return 1;
        else if (c1.IsGenericMethod() && !c2.IsGenericMethod())
            return 2;

        // Non-expanded members are better.
        if (!c1.IsExpandedForm() && c2.IsExpandedForm())
            return 1;
        else if (c1.IsExpandedForm() && !c2.IsExpandedForm())
            return 2;

        // C# `int r = c1.ArgumentsPassedToParams.CompareTo(c2.ArgumentsPassedToParams);` -- prefer
        // the member with FEWER arguments mapped to the params-collection.
        int aptp1 = c1.ArgumentsPassedToParams();
        int aptp2 = c2.ArgumentsPassedToParams();
        if (aptp1 < aptp2)
            return 1;
        else if (aptp1 > aptp2)
            return 2;

        // Prefer the member where no default values need to be substituted.
        if (!c1.HasUnmappedOptionalParameters() && c2.HasUnmappedOptionalParameters())
            return 1;
        else if (c1.HasUnmappedOptionalParameters() && !c2.HasUnmappedOptionalParameters())
            return 2;

        // Compare the formal parameters.
        int r = MoreSpecificFormalParameters(c1, c2);
        if (r != 0)
            return r;

        // C# `ILiftedOperator lift1 = c1.Member as ILiftedOperator;` -- prefer non-lifted operators.
        // The `Member()` accessor returns a `const IParameterizedMember*`; the `dynamic_cast` cross-
        // casts to the standalone `ILiftedOperator` base (the C# `as ILiftedOperator`, the D549
        // interface). A non-lifted member (no `ILiftedOperator` base) yields nullptr.
        const ILiftedOperator* lift1 = dynamic_cast<const ILiftedOperator*>(c1.Member());
        const ILiftedOperator* lift2 = dynamic_cast<const ILiftedOperator*>(c2.Member());
        if (lift1 == nullptr && lift2 != nullptr)
            return 1;
        if (lift1 != nullptr && lift2 == nullptr)
            return 2;

        // Prefer by-value parameters over in-parameters.
        r = BetterParameterPassingChoice(c1, c2);
        if (r != 0)
            return r;

        if (c1.IsExpandedForm()) {
            // C# `Debug.Assert(c2.IsExpandedForm);` -- not asserted in the port (a degenerate
            // mismatched pair skips the tiebreak rather than aborting).
            r = BetterParamsCollectionType(conversions, *c1.ParamsCollectionType(), *c2.ParamsCollectionType());
            if (r != 0)
                return r;
        }
    }
    return 0;
}

void ConsiderIfNewCandidateIsBest(
    CSharpConversions& conversions,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    bool& bestCandidateWasValidated,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith,
    const std::shared_ptr<OverloadResolutionCandidate>& candidate) {
    // C# `if (bestCandidate == null) { bestCandidate = candidate; bestCandidateWasValidated = false; }`
    // -- the first candidate becomes the best. A default-constructed `std::shared_ptr` is empty (the
    // C# `null`); the shared_ptr copy ports the C# reference assignment (the candidate is shared).
    if (!bestCandidate) {
        bestCandidate = candidate;
        bestCandidateWasValidated = false;
        return;
    }
    // C# `switch (BetterFunctionMember(candidate, bestCandidate))` -- the NEW candidate is `c1`, the
    // existing best is `c2` (note the argument order: `candidate` first, `bestCandidate` second). Both
    // are non-null here (the `!bestCandidate` guard above and the caller's non-null candidate), so the
    // derefs are safe.
    switch (BetterFunctionMember(conversions, arguments, *candidate, *bestCandidate)) {
        case 0:
            // C# `// Overwrite 'bestCandidateAmbiguousWith' so that API users can detect the set of all
            // ambiguous methods if they look at bestCandidateAmbiguousWith after each step.` -- the
            // candidate is recorded as ambiguous WITH the current best (the best itself stays).
            bestCandidateAmbiguousWith = candidate;
            break;
        case 1:
            // The new candidate is better -> promote it to best, reset the validation flag, and clear
            // any previously-recorded ambiguous partner.
            bestCandidate = candidate;
            bestCandidateWasValidated = false;
            bestCandidateAmbiguousWith.reset();
            break;
        // C# `// case 2: best candidate stays best` -- no state change.
        default:
            break;
    }
}

bool ValidateConstraints(const ILSpy::Decompiler::TypeSystem::ITypeParameter& typeParameter,
                         ILSpy::Decompiler::TypeSystem::IType& typeArgument,
                         ILSpy::Decompiler::TypeSystem::TypeVisitor* substitution,
                         CSharpConversions& conversions)
{
    using ILSpy::Decompiler::TypeSystem::Accessibility;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::IsNonNullableValueType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // C# `switch (typeArgument.Kind) { // void, null, and pointers cannot be used as type arguments
    // case TypeKind.Void: case TypeKind.Null: case TypeKind.Pointer: return false; }` -- the
    // outright rejections before any constraint flag is consulted.
    switch (typeArgument.Kind()) {
        case TypeKind::Void:
        case TypeKind::Null:
        case TypeKind::Pointer:
            return false;
        default:
            break;
    }
    // C# `if (typeParameter.HasReferenceTypeConstraint) { if (typeArgument.IsReferenceType != true)
    // return false; }` -- the `bool? != true` check is false ONLY for a definite `true` (a lifted
    // `!=` yields true when the optional holds `false` OR is empty, the D517 `bool?` relational
    // semantics), so an indeterminate `IsReferenceType` FAILS the `class` constraint.
    if (typeParameter.HasReferenceTypeConstraint()) {
        auto isReferenceType = typeArgument.IsReferenceType();
        if (!(isReferenceType.has_value() && *isReferenceType == true))
            return false;
    }
    // C# `if (typeParameter.HasValueTypeConstraint) { if (!NullableType.IsNonNullableValueType(
    // typeArgument)) return false; }` -- the `struct`/`unmanaged` constraint needs a non-nullable
    // value type (a `Nullable<T>` is a value type but nullable, so it FAILS).
    if (typeParameter.HasValueTypeConstraint()) {
        if (!IsNonNullableValueType(typeArgument))
            return false;
    }
    // C# `if (typeParameter.HasDefaultConstructorConstraint) { ITypeDefinition def =
    // typeArgument.GetDefinition(); if (def != null && def.IsAbstract) return false; var ctors =
    // typeArgument.GetConstructors(m => m.Parameters.Count == 0 && m.Accessibility ==
    // Accessibility.Public, IgnoreInheritedMembers | ReturnMemberDefinitions); if (!ctors.Any())
    // return false; }` -- the `new()` constraint: an abstract type has no creatable parameterless
    // instances, and the type must declare its OWN public parameterless constructor (the
    // IgnoreInheritedMembers option; ReturnMemberDefinitions skips member specialization).
    if (typeParameter.HasDefaultConstructorConstraint()) {
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* definition = typeArgument.GetDefinition();
        if (definition != nullptr && definition->IsAbstract())
            return false;
        auto constructors = typeArgument.GetConstructors(
            [](const ILSpy::Decompiler::TypeSystem::IMethod* m) {
                return m->Parameters().size() == 0 &&
                       m->Accessibility() == Accessibility::Public;
            },
            ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers |
                ILSpy::Decompiler::TypeSystem::GetMemberOptions::ReturnMemberDefinitions);
        if (constructors.empty())
            return false;
    }
    // C# `foreach (IType constraintType in typeParameter.DirectBaseTypes) { IType c =
    // constraintType; if (substitution != null) c = c.AcceptVisitor(substitution); if
    // (!conversions.IsConstraintConvertible(typeArgument, c)) return false; }` -- each declared
    // base-type constraint (`where T : Base`), after the optional substitution replaces type
    // parameters inside the constraint (a constraint may reference another type parameter, or
    // recursively the same one). The `c` handle is null-guarded before the deref: the C# visitor
    // contract never returns null, but the port's `ITypePtr` return could be empty for a
    // degenerate visitor, and the guard yields false as the safe faithful fallback (the D543
    // IsExactlyMatching null-guard precedent) instead of dereferencing an empty handle.
    for (const ITypePtr& constraintType : typeParameter.DirectBaseTypes()) {
        ITypePtr c = constraintType;
        if (!c)
            return false;
        if (substitution != nullptr) {
            c = c->AcceptVisitor(*substitution);
            if (!c)
                return false;
        }
        if (!conversions.IsConstraintConvertible(typeArgument, *c))
            return false;
    }
    return true;
}

bool ValidateConstraints(const ILSpy::Decompiler::TypeSystem::ITypeParameter& typeParameter,
                         ILSpy::Decompiler::TypeSystem::IType& typeArgument,
                         ILSpy::Decompiler::TypeSystem::TypeVisitor* substitution)
{
    using ILSpy::Decompiler::TypeSystem::IEntity;
    // C# `return ValidateConstraints(typeParameter, typeArgument, substitution,
    // CSharpConversions.Get(typeParameter.Owner.Compilation));` -- the public overload resolves
    // the conversions from the TYPE PARAMETER'S OWN compilation (the owner entity's compilation,
    // not the caller's). The C# `ArgumentNullException` guards for null `typeParameter` /
    // `typeArgument` compile out (the C++ references are non-null by construction, the D374
    // convention).
    const IEntity* owner = typeParameter.Owner();
    if (owner == nullptr) {
        // SAFE FALLBACK: the C# dereferences `typeParameter.Owner` unconditionally (an NRE for the
        // dummy type parameters, whose `Owner` contract is nullable); a type parameter without an
        // owning entity has no compilation to resolve the conversions from, so the constraints
        // cannot be validated. Return `false` (not satisfied) -- the soft direction in both real
        // callers (`ValidateMethodConstraints` records the masked-out `MethodConstraintsNotSatisfied`
        // soft error; the `CSharpResolver` call site behaves likewise). The D516
        // guard-instead-of-crash precedent.
        return false;
    }
    return ValidateConstraints(typeParameter, typeArgument, substitution,
                               CSharpConversions::Get(owner->Compilation()));
}

ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution GetSubstitution(const OverloadResolutionCandidate& candidate)
{
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
    // C# `// Do not compose the substitutions, but merge them.` / `// This is required for
    // InvocationTests.SubstituteClassAndMethodTypeParametersAtOnce` / `return new
    // TypeParameterSubstitution(candidate.Member.Substitution.ClassTypeArguments,
    // candidate.InferredTypes);` -- the class type arguments come from the member's own
    // substitution, the method type arguments from the inference result. The member's METHOD type
    // arguments are deliberately DISCARDED (composing would double-substitute the class
    // parameters); only the class list is merged in.
    const TypeParameterSubstitution* memberSubstitution = candidate.Member()->Substitution();
    if (memberSubstitution == nullptr) {
        // SAFE FALLBACK: the C# `IMember.Substitution` contract is "never null" ("Returns
        // `Identity` for not specialized"), but the shared test stub returns nullptr; the Identity
        // singleton's `ClassTypeArguments` is `nullopt` ("keep the class type parameters
        // unmodified") -- the documented faithful counterpart of the not-specialized contract.
        memberSubstitution = &TypeParameterSubstitution::Identity();
    }
    // `candidate.InferredTypes` (the C# `IType[]`, null before inference runs) -- the port's
    // candidate carries a never-null `std::vector<ITypePtr>`, so the method list is always
    // present (an empty vector substitutes every index out of range, per the
    // `TypeParameterSubstitution` semantics); the copy realizes the C# reference pass-through.
    return TypeParameterSubstitution(memberSubstitution->ClassTypeArguments(),
                                      std::optional<std::vector<ITypePtr>>(candidate.InferredTypes()));
}

ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors
ValidateMethodConstraints(const OverloadResolutionCandidate& candidate)
{
    using Errors = ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
    using ILSpy::Decompiler::TypeSystem::ITypeParameter;
    using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
    // C# `// If type inference already failed, we won't check the constraints:` / `if
    // ((candidate.Errors & OverloadResolutionErrors.TypeInferenceFailed) != 0) return
    // OverloadResolutionErrors.None;` -- the inferred types are unusable after a failed
    // inference, so the constraints are skipped entirely (the soft `None`, NOT an error).
    if ((candidate.Errors() & Errors::TypeInferenceFailed) != Errors::None)
        return Errors::None;
    // C# `if (candidate.TypeParameters == null || candidate.TypeParameters.Count == 0) return
    // OverloadResolutionErrors.None; // the method isn't generic` -- the port's `TypeParameters()`
    // vector is never null (the C# `== null` half is N/A), so only the emptiness check remains.
    const std::vector<const ITypeParameter*>& typeParameters = candidate.TypeParameters();
    if (typeParameters.empty())
        return Errors::None; // the method isn't generic
    // C# `var substitution = GetSubstitution(candidate);` -- the merged substitution supplies both
    // the per-parameter type arguments (`MethodTypeArguments[i]`, the candidate's inferred types)
    // and the visitor applied to constraints that reference type parameters.
    TypeParameterSubstitution substitution = GetSubstitution(candidate);
    // C# `for (int i = 0; i < candidate.TypeParameters.Count; i++) { if (!ValidateConstraints(
    // candidate.TypeParameters[i], substitution.MethodTypeArguments[i], substitution)) return
    // OverloadResolutionErrors.MethodConstraintsNotSatisfied; }` -- the PUBLIC 3-arg overload
    // (the conversions resolved from each type parameter's own compilation inside it).
    const auto& methodTypeArguments = substitution.MethodTypeArguments();
    for (std::size_t i = 0; i < typeParameters.size(); i++) {
        // SAFE FALLBACK: the C# indexes `substitution.MethodTypeArguments[i]` (the candidate's
        // `InferredTypes` array) unconditionally, which throws for a null/short array -- the
        // degenerate pre-inference candidate state, unreachable in the real engine flow (this step
        // only runs after `RunTypeInference` populated the array, and the `TypeInferenceFailed`
        // guard above covers the failed case). The port treats an out-of-range index or a null
        // entry as the unverifiable soft verdict `MethodConstraintsNotSatisfied` (masked out by
        // `IsApplicable`, so the candidate stays applicable) instead of the UB.
        if (!methodTypeArguments.has_value() || i >= methodTypeArguments->size())
            return Errors::MethodConstraintsNotSatisfied;
        const ILSpy::Decompiler::TypeSystem::ITypePtr& typeArgument = (*methodTypeArguments)[i];
        if (!typeArgument)
            return Errors::MethodConstraintsNotSatisfied;
        if (!ValidateConstraints(*typeParameters[i], *typeArgument, &substitution))
            return Errors::MethodConstraintsNotSatisfied;
    }
    return Errors::None;
}

// The C# `public IParameterizedMember GetBestCandidateWithSubstitutedTypeArguments()`
// (OverloadResolution.cs line 1153). See the header comment for the port conventions.
const ILSpy::Decompiler::TypeSystem::IParameterizedMember* GetBestCandidateWithSubstitutedTypeArguments(
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidate)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    // C# `if (bestCandidate == null) return null;` -- a null `shared_ptr` is the C# `null`.
    if (!bestCandidate)
        return nullptr;
    // C# `IMethod method = bestCandidate.Member as IMethod;`
    const IParameterizedMember* member = bestCandidate->Member();
    const IMethod* method = dynamic_cast<const IMethod*>(member);
    // C# `if (method != null && method.TypeParameters.Count > 0)`.
    if (method != nullptr && !method->TypeParameters().empty()) {
        // C# `return ((IMethod)method.MemberDefinition).Specialize(GetSubstitution(bestCandidate));`
        // -- the MEMBER DEFINITION (not the already-specialized member) is re-specialized with
        // the merged substitution. The C# hard cast is a `dynamic_cast` + safe fallback: a
        // definition that is not an `IMethod` (or a null definition) cannot occur for a real
        // method; the member is returned as-is instead of the C# `InvalidCastException` (the
        // D516 safe-fallback convention).
        const IMethod* methodDefinition = dynamic_cast<const IMethod*>(method->MemberDefinition());
        if (methodDefinition != nullptr) {
            TypeParameterSubstitution substitution = GetSubstitution(*bestCandidate);
            return methodDefinition->Specialize(&substitution);
        }
    }
    // C# `else { return bestCandidate.Member; }` (also the safe-fallback arm above).
    return member;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
