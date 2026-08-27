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

// Port of `ICSharpCode.Decompiler.CSharp.Resolver.OverloadResolution` -- the C# overload resolution
// (C# spec draft-v11 section 12.6.4). This header ports the class SKELETON: the constructor (validation
// + field init + the `AllowExpandingParams`/`AllowOptionalParameters` defaults), the input properties
// (`IsExtensionMethodInvocation`/`AllowExpandingParams`/`AllowOptionalParameters`/`AllowImplicitIn`/
// `CheckForOverflow`/`Arguments`), and the member fields. The engine steps (`AddCandidate`/
// `AddMethodLists`/`CalculateCandidate`/`RunTypeInference`/`CheckApplicability`/
// `ConsiderIfNewCandidateIsBest`/`BetterFunctionMember`) and the output properties (`BestCandidate`/
// `CreateResolveResult`/`GetArgumentsWithConversions`/etc.) are deferred -- they need the unported
// `CSharpConversions` (~1757 lines) and `TypeInference` (~1188 lines), and the `ResolveResult`/
// `CSharpResolver` composition.
//
// The C# `Candidate` nested class is ported as the separate `OverloadResolutionCandidate` (D506); the
// pure-transform engine steps that do NOT need `CSharpConversions`/`TypeInference` are ported as
// `Detail::` free functions in `OverloadResolutionHelpers.{hpp,cpp}` (`ResolveParameterTypes` D508,
// `MapCorrespondingParameters` D509, `CheckApplicabilityArgumentCounts` D510) -- they take the
// `arguments`/`argumentNames`/`allowOptionalParameters` they need as parameters since the free function
// has no instance state. This skeleton gives those helpers an owning `OverloadResolution` instance to
// compose into once the engine steps land.
//
// Field ownership (the C# is a class with GC-owned reference fields):
//  * `compilation` -> non-owning `const ICompilation*` (the C# holds a reference; the caller owns the
//    compilation, which outlives the resolver).
//  * `arguments` -> owning `std::vector<std::shared_ptr<ResolveResult>>` (the C# `ResolveResult[]`; the
//    `ResolveResult` hierarchy is fully ported in `Semantics/`).
//  * `argumentNames` -> owning `std::vector<std::string>` (the C# `string[]`; `null` normalized to
//    all-empty in the ctor).
//  * `conversions` -> non-owning `const CSharpConversions*` (forward-declared; unported). The C# ctor
//    default `conversions ?? CSharpConversions.Get(compilation)` is deferred -- the port ctor takes a
//    nullable pointer and stores it as-is (the `?? Get(compilation)` fallback lands with the
//    `CSharpConversions` port).
//  * `bestCandidate`/`bestCandidateAmbiguousWith` -> owning `std::shared_ptr<OverloadResolutionCandidate>`
//    (nullable; the C# holds the class instance by reference).
//  * `explicitlyGivenTypeArguments` -> owning `std::optional<std::vector<ITypePtr>>` (the C# `IType[]`,
//    `null` when no type arguments were specified).
//  * `bestCandidateWasValidated`/`bestCandidateValidationResult` -> plain bool / enum (mutable state for
//    the deferred `BestCandidateErrors` getter).

#pragma once

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"  // OverloadResolutionCandidate (bestCandidate)
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"  // OverloadResolutionErrors
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (arguments)
#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (compilation)
#include "Decompiler/TypeSystem/IType.hpp"  // ITypePtr (explicitlyGivenTypeArguments)

#include <memory>
#include <optional>
#include <stdexcept>  // invalid_argument / runtime_error
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (unported): the conversion controller. The C# ctor default
// `conversions ?? CSharpConversions.Get(compilation)` is deferred.
class CSharpConversions;

class OverloadResolution {
public:
    // The C# `public OverloadResolution(ICompilation compilation, ResolveResult[] arguments,
    // string[] argumentNames = null, IType[] typeArguments = null, CSharpConversions conversions = null)`.
    //
    // `argumentNames` is `std::nullopt` for the C# `null` default (normalized to all-empty, length ==
    // `arguments.size()`); a present-but-mismatched-length vector throws (the C# `ArgumentException`).
    // `typeArguments` is `std::nullopt` for the C# `null` default; a present non-empty vector sets
    // `explicitlyGivenTypeArguments` (an empty vector leaves it `nullopt`, matching the C#
    // `typeArguments != null && typeArguments.Length > 0` guard). `conversions` is a nullable
    // non-owning pointer (the C# `?? CSharpConversions.Get(compilation)` fallback is deferred).
    OverloadResolution(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                       std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
                       std::optional<std::vector<std::string>> argumentNames = std::nullopt,
                       std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> typeArguments = std::nullopt,
                       const CSharpConversions* conversions = nullptr);

    // --- Input Properties (the C# `public bool ... { get; set; }`) ---

    // The C# `IsExtensionMethodInvocation` (default false). Setting true restricts the first-argument
    // conversions to identity/reference/boxing.
    bool IsExtensionMethodInvocation() const { return isExtensionMethodInvocation_; }
    bool& IsExtensionMethodInvocation() { return isExtensionMethodInvocation_; }

    // The C# `AllowExpandingParams` (default true) -- whether expanding `params` into individual
    // elements is allowed.
    bool AllowExpandingParams() const { return allowExpandingParams_; }
    bool& AllowExpandingParams() { return allowExpandingParams_; }

    // The C# `AllowOptionalParameters` (default true) -- whether optional parameters may be left at
    // their default value.
    bool AllowOptionalParameters() const { return allowOptionalParameters_; }
    bool& AllowOptionalParameters() { return allowOptionalParameters_; }

    // The C# `AllowImplicitIn` (default true) -- whether a value argument can be passed to an `in`
    // reference parameter.
    bool AllowImplicitIn() const { return allowImplicitIn_; }
    bool& AllowImplicitIn() { return allowImplicitIn_; }

    // The C# `CheckForOverflow` (default false) -- whether `ConversionResolveResult`s apply overflow
    // checking.
    bool CheckForOverflow() const { return checkForOverflow_; }
    bool& CheckForOverflow() { return checkForOverflow_; }

    // The C# `public IList<ResolveResult> Arguments { get { return arguments; } }`.
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& Arguments() const {
        return arguments_;
    }

    // The C# `argumentNames` field (not public in C#, but the engine helpers take it). Empty-string ==
    // positional (the C# `null` entry).
    const std::vector<std::string>& ArgumentNames() const { return argumentNames_; }

    // The C# `compilation` field (not public in C#; exposed for the engine helpers / deferred output
    // properties).
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const { return *compilation_; }

    // The C# `conversions` field (not public in C#; exposed for the deferred engine steps). Non-owning;
    // nullable until the `CSharpConversions` port.
    const CSharpConversions* Conversions() const { return conversions_; }

    // The C# `explicitlyGivenTypeArguments` field -- `nullopt` when no type arguments were specified.
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& ExplicitlyGivenTypeArguments() const {
        return explicitlyGivenTypeArguments_;
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation* compilation_;
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments_;
    std::vector<std::string> argumentNames_;
    const CSharpConversions* conversions_;
    // The `bestCandidate`/`bestCandidateAmbiguousWith`/`bestCandidateWasValidated`/
    // `bestCandidateValidationResult` fields are deferred (the engine steps that set them need
    // `CSharpConversions`/`TypeInference`). Declared here so the field layout is complete.
    std::shared_ptr<OverloadResolutionCandidate> bestCandidate_;
    std::shared_ptr<OverloadResolutionCandidate> bestCandidateAmbiguousWith_;
    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> explicitlyGivenTypeArguments_;
    bool bestCandidateWasValidated_ = false;
    ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors bestCandidateValidationResult_ =
        ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors::None;

    // Input-property backing fields (the C# auto-properties' defaults).
    bool isExtensionMethodInvocation_ = false;
    bool allowExpandingParams_ = true;
    bool allowOptionalParameters_ = true;
    bool allowImplicitIn_ = true;
    bool checkForOverflow_ = false;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
