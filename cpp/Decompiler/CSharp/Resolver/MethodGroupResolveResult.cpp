// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `MethodGroupResolveResult::PerformOverloadResolution` (MethodGroupResolve-
// Result.cs lines 248-307) -- the method group's overload-resolution entry point composing the
// ported `OverloadResolution` engine. See the header for the full class contract. The class was
// header-only until now; this .cpp exists so the method group does not pull the whole
// `OverloadResolution` header into every includer (the `MemberLookup`/`LookupHelpers` include
// graph stays light).

#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"  // CSharpResolver (the GetExtensionMethods fetch back-pointer)

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions (the threaded conversions parameter)
#include "Decompiler/CSharp/Resolver/Log.hpp"  // Log::WriteLine / WriteCollection / Indent / Unindent (the debug trail)
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"  // OverloadResolution (the built resolution)
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"  // OverloadResolutionErrors (AddCandidate's return)

#include <algorithm>  // std::copy
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public IEnumerable<IEnumerable<IMethod>> GetExtensionMethods()` (lines 181-197)
// -- see MethodGroupResolveResult.hpp for the port conventions. The out-of-line definition
// needs the full `CSharpResolver` type for the fetch call (the header can only
// forward-declare it, the CSharpResolver.hpp -> MemberLookup.hpp -> MethodGroupResolveResult.hpp
// header cycle).
std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>
MethodGroupResolveResult::GetExtensionMethods() const
{
    if (resolver_ != nullptr) {
        // C# `Debug.Assert(extensionMethods == null);` -- a resolver-attached group has
        // not yet fetched (the `ResolveMemberAccess` arm sets `extensionMethods`
        // directly and never attaches a resolver).
        assert(!extensionMethods_.has_value());
        // C# `try { extensionMethods = resolver.GetExtensionMethods(methodName,
        // typeArguments); } finally { resolver = null; }` -- the finally detaches the
        // resolver even when the fetch throws; the faithful C++ port re-raises after the
        // detach (a `catch (...)` + rethrow).
        try {
            extensionMethods_ = resolver_->GetExtensionMethods(methodName_, typeArguments_);
        } catch (...) {
            resolver_ = nullptr;
            throw;
        }
        resolver_ = nullptr;
    }
    // C# `return extensionMethods ?? Enumerable.Empty<IEnumerable<IMethod>>();`
    if (extensionMethods_.has_value())
        return *extensionMethods_;
    return {};
}

std::unique_ptr<OverloadResolution> MethodGroupResolveResult::PerformOverloadResolution(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::optional<std::vector<std::string>>& argumentNames,
    bool allowExtensionMethods,
    bool allowExpandingParams,
    bool allowOptionalParameters,
    bool allowImplicitIn,
    bool checkForOverflow,
    const CSharpConversions* conversions) const
{
    // C# `Log.WriteLine("Performing overload resolution for " + this);` -- the string
    // concatenation ports to the pre-formatted single argument (a no-op while
    // `Log::IsEnabled == false`).
    Log::WriteLine(std::string("Performing overload resolution for ") + ToString());
    Log::WriteCollection("  Arguments: ", arguments);

    // C# `var typeArgumentArray = this.TypeArguments.ToArray();` -- a non-null snapshot of
    // the group's explicitly provided type arguments. An empty list yields an EMPTY array,
    // which the `OverloadResolution` ctor treats the same as null (the
    // `typeArguments.Length > 0` guard), so the port passes `nullopt` for the empty shape
    // (behavior-identical; the array is shared with the extension-method resolution below).
    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> typeArgumentArray;
    {
        std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments = TypeArguments();
        if (!typeArguments.empty())
            typeArgumentArray = std::move(typeArguments);
    }

    // C# `OverloadResolution or = new OverloadResolution(compilation, arguments,
    // argumentNames, typeArgumentArray, conversions);` followed by the four input-property
    // assignments (the port's `bool&` accessors are the C# property setters).
    auto resolution = std::make_unique<OverloadResolution>(
        compilation, arguments, argumentNames, typeArgumentArray, conversions);
    resolution->AllowExpandingParams() = allowExpandingParams;
    resolution->AllowOptionalParameters() = allowOptionalParameters;
    resolution->CheckForOverflow() = checkForOverflow;
    resolution->AllowImplicitIn() = allowImplicitIn;

    // C# `or.AddMethodLists(methodLists);` -- the group's own per-declaring-type buckets
    // (base types first, the derived-type-hides-base-methods scan inside).
    resolution->AddMethodLists(methodLists_);

    // C# `if (allowExtensionMethods && !or.FoundApplicableCandidate) { ... }` -- with
    // no resolver attached and no directly-set `extensionMethods`, `GetExtensionMethods()`
    // yields empty (the resolver-less state), so the `extensionMethods.Any()` guard
    // keeps the whole extension-method block inert; with a resolver attached (the
    // `CSharpResolver::ResolveMemberAccess` arm) or a directly-set list (its fallback
    // construction), the block is LIVE.
    if (allowExtensionMethods && !resolution->FoundApplicableCandidate()) {
        // No applicable match found, so let's try extension methods.
        auto extensionMethods = GetExtensionMethods();

        if (!extensionMethods.empty()) {
            Log::WriteLine("No candidate is applicable, trying {0} extension methods groups...",
                           extensionMethods.size());
            // C# `ResolveResult[] extArguments = new ResolveResult[arguments.Length + 1];
            // extArguments[0] = new ResolveResult(this.TargetType); arguments.CopyTo(
            // extArguments, 1);` -- the receiver is prepended as the first argument.
            std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> extArguments(
                arguments.size() + 1);
            {
                // C# `new ResolveResult(this.TargetType)` -- the base `ResolveResult` over the
                // group's target type. `TargetType()` returns `const IType&` (the target
                // result's type, or the cached `UnknownType` for a null target -- both
                // shared-managed), so the owning `ITypePtr` the base ctor takes comes from
                // `shared_from_this` + `const_pointer_cast` (the accessor-const contract; the
                // D515/D529 convention).
                const ILSpy::Decompiler::TypeSystem::IType& targetType = TargetType();
                extArguments[0] = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(
                    std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
                        std::static_pointer_cast<const ILSpy::Decompiler::TypeSystem::IType>(
                            targetType.shared_from_this())));
            }
            std::copy(arguments.begin(), arguments.end(), extArguments.begin() + 1);
            // C# `string[] extArgumentNames = null; if (argumentNames != null) { ... }` --
            // the receiver's entry [0] stays null (the port's empty-string normalization,
            // the `OverloadResolution` ctor convention).
            std::optional<std::vector<std::string>> extArgumentNames = std::nullopt;
            if (argumentNames.has_value()) {
                std::vector<std::string> names(argumentNames->size() + 1, std::string{});
                std::copy(argumentNames->begin(), argumentNames->end(), names.begin() + 1);
                extArgumentNames = std::move(names);
            }
            auto extOr = std::make_unique<OverloadResolution>(
                compilation, extArguments, extArgumentNames, typeArgumentArray, conversions);
            extOr->AllowExpandingParams() = allowExpandingParams;
            extOr->AllowOptionalParameters() = allowOptionalParameters;
            extOr->IsExtensionMethodInvocation() = true;
            extOr->CheckForOverflow() = checkForOverflow;
            extOr->AllowImplicitIn() = allowImplicitIn;

            for (const auto& g : extensionMethods) {
                for (const ILSpy::Decompiler::TypeSystem::IMethod* method : g) {
                    Log::Indent();
                    OverloadResolutionErrors errors = extOr->AddCandidate(*method);
                    Log::Unindent();
                    // C# `or.LogCandidateAddingResult("  Extension", method, errors);` -- NOTE
                    // the OUTER resolution's best-candidate state, not extOr's (the log's
                    // " (best candidate so far)" suffix refers to the non-extension
                    // candidates).
                    resolution->LogCandidateAddingResult("  Extension", *method, errors);
                }
                if (extOr->FoundApplicableCandidate())
                    break;
            }
            // C# `if (extOr.FoundApplicableCandidate || or.BestCandidate == null) { or =
            // extOr; }` -- "Consider an extension method result better than the normal result
            // only if it's applicable; or if there is no normal result."
            if (extOr->FoundApplicableCandidate() || resolution->BestCandidate() == nullptr) {
                resolution = std::move(extOr);
            }
        }
    }
    // C# `Log.WriteLine("Overload resolution finished, best candidate is {0}.",
    // or.GetBestCandidateWithSubstitutedTypeArguments());` -- the C# `[Conditional]` elides
    // the call AND its argument evaluation while logging is disabled, so the port wraps the
    // (potentially expensive: `Specialize` on a generic best candidate) argument in
    // `if constexpr (Log::IsEnabled)` for full elision parity (the Log.hpp caller-side
    // remedy for the arguments-still-evaluated divergence).
    if constexpr (Log::IsEnabled) {
        const ILSpy::Decompiler::TypeSystem::IParameterizedMember* best =
            resolution->GetBestCandidateWithSubstitutedTypeArguments();
        Log::WriteLine("Overload resolution finished, best candidate is {0}.",
                       best != nullptr ? std::string(best->Name()) : std::string("<null>"));
    }
    return resolution;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
