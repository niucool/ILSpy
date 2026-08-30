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

// Port of the `OverloadResolution` constructor (the validation + field init + the input-property
// defaults) and the `AddCandidate` engine entry. See the header for the deferred engine steps and
// output properties.

#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions::Get (the lazy ctor-default resolution)
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"  // Detail::AddCandidate

namespace ILSpy::Decompiler::CSharp::Resolver {

OverloadResolution::OverloadResolution(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
    std::optional<std::vector<std::string>> argumentNames,
    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> typeArguments,
    const CSharpConversions* conversions)
    : compilation_(&compilation),
      arguments_(std::move(arguments)),
      conversions_(conversions)
{
    // The C# throws `ArgumentNullException` on null `compilation`/`arguments`. The port's `const`
    // reference + owning-vector params cannot be null at the type level (a reference can't bind to
    // null), so those two checks are compiled out (the D374 reference-not-null convention); the C#
    // `ArgumentException` on `argumentNames.Length != arguments.Length` is preserved.
    if (argumentNames.has_value()) {
        if (argumentNames->size() != arguments_.size()) {
            throw std::invalid_argument(
                "OverloadResolution: argumentNames.Length must be equal to arguments.Length");
        }
        argumentNames_ = std::move(*argumentNames);
    } else {
        // The C# `argumentNames = new string[arguments.Length]` (all-null == all-positional).
        // The port uses empty-string for the C# `null` entry.
        argumentNames_.assign(arguments_.size(), std::string{});
    }
    // The C# `if (typeArguments != null && typeArguments.Length > 0) this.explicitlyGivenTypeArguments
    // = typeArguments` -- keep `nullopt` when no type arguments were specified (an empty present vector
    // leaves it `nullopt`, faithful to the `Length > 0` guard).
    if (typeArguments.has_value() && !typeArguments->empty()) {
        explicitlyGivenTypeArguments_ = std::move(*typeArguments);
    }
    // `AllowExpandingParams = true` / `AllowOptionalParameters = true` are the in-class initializers
    // (the C# auto-property defaults set in the ctor body); `AllowImplicitIn = true` likewise.
}

OverloadResolutionErrors OverloadResolution::AddCandidate(
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember& member,
    OverloadResolutionErrors additionalErrors) {
    // The C# ctor default `conversions ?? CSharpConversions.Get(compilation)` resolved lazily at
    // the first engine call (the ctor stores the nullable pointer as-is; `Get` is the
    // per-compilation cached factory, so repeated resolutions return the same instance).
    CSharpConversions& conversions = conversions_ != nullptr
        ? const_cast<CSharpConversions&>(*conversions_)
        : CSharpConversions::Get(*compilation_);
    return Detail::AddCandidate(member, additionalErrors, *compilation_, conversions, arguments_,
                                argumentNames_, explicitlyGivenTypeArguments_, allowExpandingParams_,
                                allowOptionalParameters_, allowImplicitIn_,
                                isExtensionMethodInvocation_, bestCandidate_,
                                bestCandidateWasValidated_, bestCandidateAmbiguousWith_);
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
