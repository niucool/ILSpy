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

// Port of the `OverloadResolution` private helpers that operate on a `Candidate` but are pure
// transformations (no `OverloadResolution` instance state) -- the C# private methods lifted to
// `CSharp::Resolver::Detail` free functions so they are individually unit-testable (TDD). The first is
// `ResolveParameterTypes` (ICSharpCode.Decompiler/CSharp/Resolver/OverloadResolution.cs); the later
// `MapCorrespondingParameters`/`RunTypeInference`/`CheckApplicability`/`ConsiderIfNewCandidateIsBest`
// helpers will land as they become feasible.

#pragma once

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (now ported): the conversion controller. `CheckApplicabilityPassingModeAndConversions`
// reads `CSharpConversions::ImplicitConversion(ResolveResult, IType)` (the D529 public entry).
class CSharpConversions;

} // namespace ILSpy::Decompiler::CSharp::Resolver

// `ResolveResult` is forward-declared (the `std::vector<std::shared_ptr<ResolveResult>>` parameter
// needs only a declaration, not the full definition; the .cpp includes the full header).
namespace ILSpy::Decompiler::Semantics { class ResolveResult; }

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

// The C# `bool ResolveParameterTypes(Candidate candidate, bool useSpecializedParameters)`. Reads the
// candidate's parameter types (the formal parameters, or the specialized member's parameters if
// `useSpecializedParameters`), and -- for the expanded form's last parameter -- unpacks a single-dim
// array (`Element`), a `Span<T>`/`ReadOnlySpan<T>` (`TypeArguments[0]`), or an array-interface
// (`IEnumerable<T>` etc., `TypeArguments[0]`); if the last param's type is not unpackable, returns false
// (abort the expanded-form candidate). Writes the resolved types into `candidate.ParameterTypes`.
// Returns true on success. `useSpecializedParameters` is only for a non-generic method's specialized
// non-generic indexer (`candidate.Member.Parameters[i].Type`); the common `useSpecializedParameters=false`
// path reads the original formal parameters (`candidate.Parameters[i].Type`).
bool ResolveParameterTypes(OverloadResolutionCandidate& candidate, bool useSpecializedParameters);

// The C# `void MapCorrespondingParameters(Candidate candidate)` -- the C# spec (draft-v11 section 12.6.2.2)
// "Corresponding parameters" (incl. the non-trailing named-argument rule from C# 7.2). Maps each argument
// to a parameter (by position, or by name for trailing named args), writing
// `candidate.ArgumentToParameterMap` (argument index -> parameter index, -1 unmapped). The `arguments`/
// `argumentNames` are `OverloadResolution` ctor fields; the free function takes them as parameters:
// `argumentCount` (the `arguments.Length`) and `argumentNames` (the per-arg name, empty-string == positional
// -- the C# `null` entry). The C# goes backwards (`i` from `arguments.Length - 1` down) so
// `hasPositionalArgument` detects non-trailing named args; the port mirrors that exactly.
void MapCorrespondingParameters(OverloadResolutionCandidate& candidate,
                                std::size_t argumentCount,
                                const std::vector<std::string>& argumentNames);

// The first half of the C# `CheckApplicability(Candidate candidate)` (C# 4.0 spec section 7.5.3.1
// "Applicable function member") -- the argument-count-per-parameter check. Builds a per-parameter
// argument count from `candidate.ArgumentToParameterMap`, then for each parameter: skips the expanded
// form's last params-array param (any count is fine); if count==0 and the param is optional and
// `allowOptionalParameters`, sets `HasUnmappedOptionalParameters`, else `MissingArgumentForRequiredParameter`;
// if count>1, `MultipleArgumentsForSingleParameter`. `allowOptionalParameters` is the `OverloadResolution`
// `AllowOptionalParameters` input property. The second half (passing-mode + conversion check) needs
// `CSharpConversions.ImplicitConversion` and is deferred.
void CheckApplicabilityArgumentCounts(OverloadResolutionCandidate& candidate,
                                      bool allowOptionalParameters);

// The second half of the C# `CheckApplicability(Candidate candidate)` (C# 4.0 spec section
// 7.5.3.1 "Applicable function member") -- the passing-mode + conversion check. For each argument:
//   * unmapped (parameterIndex < 0): `ArgumentConversions[i] = None`, continue.
//   * a `ByReferenceResolveResult`: the argument's `ReferenceKind` must match the parameter's, else
//     `ParameterPassingModeMismatch`.
//   * an `OutVarResolveResult`: the parameter must be `Out`, else `ParameterPassingModeMismatch`;
//     `out var` is compatible with any `out` parameter (the conversion is NOT checked -- `continue`).
//   * otherwise (by-value): the `AllowImplicitIn` / `IsExtensionMethodInvocation` implicit-`in` unwrap
//     (a `ByReferenceType` parameter type stripped via `SkipModifiers` is unwrapped to its element so
//     `in`/`ref`/`ref readonly` parameters can be filled implicitly); else a non-`None` `ReferenceKind`
//     is a `ParameterPassingModeMismatch`.
//   * then `conversions.ImplicitConversion(arguments[i], parameterType)` is called and stored in
//     `ArgumentConversions[i]`; for an extension method's first parameter, the conversion must be an
//     identity / implicit-reference / boxing / implicit-span conversion, else `ArgumentTypeMismatch`;
//     otherwise, an invalid non-user-defined non-method-group conversion to a non-`Unknown` type is an
//     `ArgumentTypeMismatch`.
// `arguments` is the `OverloadResolution` ctor field; `conversions` is the instance's
// `CSharpConversions`; `allowImplicitIn`/`isExtensionMethodInvocation` are the `AllowImplicitIn`/
// `IsExtensionMethodInvocation` input properties.
void CheckApplicabilityPassingModeAndConversions(
    OverloadResolutionCandidate& candidate,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    CSharpConversions& conversions,
    bool allowImplicitIn,
    bool isExtensionMethodInvocation);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
