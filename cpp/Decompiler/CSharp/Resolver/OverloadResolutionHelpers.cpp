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

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions (ImplicitConversion(ResolveResult, IType))
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"  // TooManyPositionalArguments / NoParameterFoundForNamedArgument
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"  // dynamic_cast<ByReferenceResolveResult>
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions::None() (the unmapped-argument conversion)
#include "Decompiler/Semantics/OutVarResolveResult.hpp"  // dynamic_cast<OutVarResolveResult>
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (arguments element)
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // Member->Parameters (specialized)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter::Type / ReferenceKind
#include "Decompiler/TypeSystem/IType.hpp"  // ArrayType / ParameterizedType / ByReferenceType / TypeKind
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"  // SpanOfT / ReadOnlySpanOfT
#include "Decompiler/TypeSystem/ReferenceKind.hpp"  // ReferenceKind
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // IsKnownType / IsArrayInterfaceType / SkipModifiers

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
