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

// Port of the `OverloadResolution.Candidate` nested class (ICSharpCode.Decompiler/CSharp/Resolver/
// OverloadResolution.cs, the C# overload resolution, C# spec draft-v11 section 12.6.4). The C# `sealed class
// Candidate` (nested in `OverloadResolution`) is the per-candidate value type the resolution builds per
// candidate method: it holds the candidate's `Member` (readonly `IParameterizedMember`), `IsExpandedForm`
// (readonly bool -- the params-expanded form), the sized `ParameterTypes` (`IType[]` -- without substitution
// initially; `RunTypeInference` substitutes), the mutable `ArgumentToParameterMap` (`int[]`), the
// `Errors`/`ErrorCount`/`HasUnmappedOptionalParameters` flags, the mutable `InferredTypes` (`IType[]`),
// the readonly `Parameters` (the member DEFINITION's parameters -- non-owning `const IParameter*`),
// the readonly `TypeParameters` (the method definition's type parameters, for a generic method --
// non-owning `const ITypeParameter*`), and the mutable `ArgumentConversions` (`Conversion[]`). The
// computed properties `ParamsCollectionType` / `IsGenericMethod` / `ArgumentsPassedToParams`; the
// `AddError` method accumulates the error mask and increments `ErrorCount` if the error makes the
// candidate inapplicable. The static `IsApplicable` returns whether the error mask (minus the
// `AmbiguousMatch` | `MethodConstraintsNotSatisfied` flags that "do not matter for applicability") is
// `None`.
//
// The C# nests `Candidate` inside `OverloadResolution`; the port makes it a separate class
// `OverloadResolutionCandidate` in the `CSharp::Resolver` namespace (the C++-nested-class-in-header-only
// convention is awkward, and the later `OverloadResolution` leaf will include this header). It is NOT
// `final` (the C# is `sealed` but the port does not seal value types).
//
// KEY PORT CONVENTIONS:
//  (a) OWNING MODEL: the C# `IType[]`/`Conversion[]`-GC model maps to owning `std::vector<ITypePtr>` /
//      `std::vector<std::shared_ptr<Conversion>>` (the candidate builds the `ParameterTypes` sized, the
//      `InferredTypes`/`ArgumentConversions` fresh during inference/applicability). `Member` is a
//      NON-OWNING `const IParameterizedMember*` (the type system owns the member; the candidate observes
//      it -- the resolution's caller keeps the `MethodListWithDeclaringType` buckets' `DeclaringType`
//      alive, which keeps the methods alive). `Parameters`/`TypeParameters` are NON-OWNING
//      `const IParameter*`/`const ITypeParameter*` (the member DEFINITION owns them -- the ctor reads
//      `member.MemberDefinition()->Parameters()`).
//  (b) The ctor reads `member.MemberDefinition()` (returns `const IMember*`), downcasts to
//      `IParameterizedMember` (`dynamic_cast` -- the C# `(IParameterizedMember)member.MemberDefinition`
//      cast; a `MemberDefinition` is always a `IParameterizedMember` for a parameterized member, but the
//      `dynamic_cast` is the safe faithful port). The `Parameters` come from the definition (NOT the
//      specialized member) -- the C# goes "back to the original parameters (without any type parameter
//      substitution)" so `RunTypeInference` can re-substitute. The `TypeParameters` come from the method
//      definition (only if `TypeParameters.Count > 0`).
//  (c) `ParamsCollectionType` returns `UnknownType()` (a fresh `SpecialType(TypeKind::Unknown)`, the C#
//      `SpecialType.UnknownType` singleton) for the not-params / not-expanded arm; for the params arm,
//      it returns the last parameter's `Type()` aliased via `shared_from_this` (the `const IType&` from
//      `IParameter::Type()` is `enable_shared_from_this`, so the aliasing `ITypePtr` keeps the
//      parameter's type alive -- the parameter owns it).
//  (d) `IsGenericMethod` uses `dynamic_cast<const IMethod*>(Member_)` (the C# `Member as IMethod`).
//  (e) `ArgumentsPassedToParams` iterates `ArgumentToParameterMap` counting entries equal to the params
//      parameter index (`Parameters.size() - 1`), only if `IsExpandedForm`.
//  (f) `IsApplicable` is a free function (the C# `public static bool IsApplicable(OverloadResolutionErrors)`):
//      `(errors & ~(AmbiguousMatch | MethodConstraintsNotSatisfied)) == None`. The "errors that do not
//      matter for applicability" are the two overall errors (ambiguity, constraints) -- they don't make a
//      single candidate inapplicable. `Candidate::AddError` calls `IsApplicable(newError)` -- if the new
//      error makes the candidate inapplicable, `ErrorCount` increments.

#pragma once

#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"  // OverloadResolutionErrors + the | & ~ operators
#include "Decompiler/Semantics/Conversion.hpp"  // Conversion (the ArgumentConversions element)
#include "Decompiler/TypeSystem/IMethod.hpp"  // dynamic_cast<IMethod> (IsGenericMethod / TypeParameters)
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // IParameterizedMember (Member / Parameters)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter (Parameters element -- IsParams / Type)
#include "Decompiler/TypeSystem/IType.hpp"  // IType + ITypePtr + UnknownType()
#include "Decompiler/TypeSystem/ITypeParameter.hpp"  // ITypeParameter (TypeParameters element)

#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public static bool IsApplicable(OverloadResolutionErrors errors)` -- whether the error mask
// (minus the two "do not matter for applicability" overall errors) is `None`. A free function (the C#
// `static` method) so `Candidate::AddError` and the later `OverloadResolution` resolution both call it.
inline bool IsApplicable(ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors errors) {
    using E = ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
    // The "errors that do not matter for applicability" -- ambiguity (an overall resolution error, not a
    // single-candidate error) and method-constraints (checked separately, not a per-candidate applicability
    // failure). NOT `constexpr` (the `operator|` on the `[Flags]` enum is `inline`, not `constexpr`).
    E errorsThatDoNotMatterForApplicability =
        E::AmbiguousMatch | E::MethodConstraintsNotSatisfied;
    return (errors & ~errorsThatDoNotMatterForApplicability) == E::None;
}

// The `OverloadResolution.Candidate` nested class -- the per-candidate value type. See the file header.
class OverloadResolutionCandidate {
public:
    // The C# `Candidate(IParameterizedMember member, bool isExpanded)`.
    OverloadResolutionCandidate(const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member,
                                bool isExpanded)
        : member_(member), isExpandedForm_(isExpanded)
    {
        using namespace ILSpy::Decompiler::TypeSystem;
        // For specialized methods, go back to the original parameters (without any type parameter
        // substitution, not even class type parameters) -- RunTypeInference re-substitutes them.
        const IParameterizedMember* memberDefinition =
            dynamic_cast<const IParameterizedMember*>(member->MemberDefinition());
        if (memberDefinition != nullptr) {
            parameters_ = memberDefinition->Parameters();
            const IMethod* methodDefinition = dynamic_cast<const IMethod*>(memberDefinition);
            if (methodDefinition != nullptr && !methodDefinition->TypeParameters().empty()) {
                typeParameters_ = methodDefinition->TypeParameters();
            }
        }
        parameterTypes_.resize(parameters_.size());  // sized, null entries
    }

    // The C# `public readonly IParameterizedMember Member`.
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Member() const { return member_; }

    // The C# `public readonly bool IsExpandedForm`.
    bool IsExpandedForm() const { return isExpandedForm_; }

    // The C# `public IType[] ParameterTypes` (sized; RunTypeInference substitutes). Returns a non-const
    // ref so the inference/applicability steps can write the types.
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& ParameterTypes() { return parameterTypes_; }
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& ParameterTypes() const { return parameterTypes_; }

    // The C# `public int[] ArgumentToParameterMap` (mutable; argument index -> parameter index, -1 unmapped).
    std::vector<int>& ArgumentToParameterMap() { return argumentToParameterMap_; }
    const std::vector<int>& ArgumentToParameterMap() const { return argumentToParameterMap_; }

    // The C# `public OverloadResolutionErrors Errors` (mutable; the accumulated applicability mask).
    ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors& Errors() { return errors_; }
    ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors Errors() const { return errors_; }

    // The C# `public int ErrorCount` (mutable; the count of inapplicability-making errors).
    int& ErrorCount() { return errorCount_; }
    int ErrorCount() const { return errorCount_; }

    // The C# `public bool HasUnmappedOptionalParameters` (mutable).
    bool& HasUnmappedOptionalParameters() { return hasUnmappedOptionalParameters_; }
    bool HasUnmappedOptionalParameters() const { return hasUnmappedOptionalParameters_; }

    // The C# `public IType[] InferredTypes` (mutable; the per-method-type-parameter inferred types).
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& InferredTypes() { return inferredTypes_; }
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& InferredTypes() const { return inferredTypes_; }

    // The C# `public readonly IReadOnlyList<IParameter> Parameters` (the member DEFINITION's parameters;
    // non-owning `const IParameter*` -- the member definition owns them).
    const std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>& Parameters() const { return parameters_; }

    // The C# `public readonly IReadOnlyList<ITypeParameter> TypeParameters` (the method definition's type
    // parameters; empty for a non-generic method; non-owning `const ITypeParameter*`).
    const std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*>& TypeParameters() const { return typeParameters_; }

    // The C# `public Conversion[] ArgumentConversions` (mutable; the conversions applied to the arguments;
    // owning `shared_ptr<Conversion>` -- the CheckApplicability step builds them fresh).
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>>& ArgumentConversions() {
        return argumentConversions_;
    }
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>>& ArgumentConversions() const {
        return argumentConversions_;
    }

    // The C# `public IType ParamsCollectionType { get; }` -- the type of the collection used for the
    // 'params' parameter (before any substitution), or `UnknownType` if not a params-expanded candidate.
    ILSpy::Decompiler::TypeSystem::ITypePtr ParamsCollectionType() const {
        using namespace ILSpy::Decompiler::TypeSystem;
        if (isExpandedForm_ && !parameters_.empty()) {
            const IParameter* lastParameter = parameters_.back();
            if (lastParameter != nullptr && lastParameter->IsParams()) {
                // `lastParameter.Type` (the C# `IType` reference) -- the parameter owns the type; alias
                // via `shared_from_this` (the `const IType&` from `Type()` is `enable_shared_from_this`).
                return std::const_pointer_cast<IType>(const_cast<IType&>(lastParameter->Type()).shared_from_this());
            }
        }
        return UnknownType();
    }

    // The C# `public bool IsGenericMethod { get; }` -- `Member as IMethod` with `TypeParameters.Count > 0`.
    bool IsGenericMethod() const {
        const auto* method = dynamic_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(member_);
        return method != nullptr && !method->TypeParameters().empty();
    }

    // The C# `public int ArgumentsPassedToParams { get; }` -- the count of arguments mapped to the params
    // parameter (only if expanded). The params parameter index is `Parameters.size() - 1`.
    int ArgumentsPassedToParams() const {
        int count = 0;
        if (isExpandedForm_ && !parameters_.empty()) {
            int paramsParameterIndex = static_cast<int>(parameters_.size()) - 1;
            for (int parameterIndex : argumentToParameterMap_) {
                if (parameterIndex == paramsParameterIndex)
                    count++;
            }
        }
        return count;
    }

    // The C# `public void AddError(OverloadResolutionErrors newError)` -- accumulate the error mask, and
    // increment `ErrorCount` if the new error makes the candidate inapplicable.
    void AddError(ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors newError) {
        errors_ = errors_ | newError;
        if (!IsApplicable(newError))
            errorCount_++;
    }

private:
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member_;
    bool isExpandedForm_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters_;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> typeParameters_;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> parameterTypes_;
    std::vector<int> argumentToParameterMap_;
    ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors errors_{};
    int errorCount_ = 0;
    bool hasUnmappedOptionalParameters_ = false;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> inferredTypes_;
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>> argumentConversions_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
