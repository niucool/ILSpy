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

// `IType` is forward-declared (the `const IType&`/`const IType*&` parameters need only a
// declaration, not the full definition; the .cpp includes the full header).
namespace ILSpy::Decompiler::TypeSystem { class IType; }

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

// The C# `bool IsArrayOrArrayInterfaceType(IType type, out IType elementType)` (OverloadResolution.cs,
// the private helper the expanded-form `BetterParamsCollectionType` tiebreak calls). Returns true and
// sets `elementType` when `type` is an `ArrayType` (the element is the array's element type) or an
// array-interface type (`IEnumerable<T>`/`ICollection<T>`/`IList<T>`/`IReadOnlyCollection<T>`/
// `IReadOnlyList<T>` -- the element is the single type argument, via `IsArrayInterfaceType`). Pure
// (reads only the type's kind / `Element()` / `TypeArguments`); no `OverloadResolution` instance state.
bool IsArrayOrArrayInterfaceType(const ILSpy::Decompiler::TypeSystem::IType& type,
                                 const ILSpy::Decompiler::TypeSystem::IType*& elementType);

// The C# `static int MoreSpecificFormalParameter(IType t1, IType t2)` (OverloadResolution.cs) -- the
// recursive "more specific formal parameter" tiebreak (C# spec section 7.5.3.3 / draft-v11 12.6.4.4):
// returns 1 if `t1` is more specific, 2 if `t2` is, 0 if neither. A type parameter is less specific
// than a non-type-parameter; two `ParameterizedType`s of equal arity recurse on the type arguments;
// two `TypeWithElementType` types (Array/ByReference/Pointer/ModOpt/ModReq -- PinnedType is not
// ported) recurse on the element types. Pure and recursive; no `OverloadResolution` instance state.
int MoreSpecificFormalParameter(const ILSpy::Decompiler::TypeSystem::IType& t1,
                                const ILSpy::Decompiler::TypeSystem::IType& t2);

// The C# `static int MoreSpecificFormalParameters(IEnumerable<IType> t1, IEnumerable<IType> t2)`
// (OverloadResolution.cs) -- zips the two type sequences (stopping at the shorter, the C# `Zip`
// semantics) and reduces the per-pair `MoreSpecificFormalParameter` verdicts: 1 if every decisive pair
// favours `t1`, 2 if every decisive pair favours `t2`, 0 otherwise (mixed or no decisive pairs). Pure.
int MoreSpecificFormalParameters(const std::vector<const ILSpy::Decompiler::TypeSystem::IType*>& t1,
                                 const std::vector<const ILSpy::Decompiler::TypeSystem::IType*>& t2);

// The C# `int MoreSpecificFormalParameters(Candidate c1, Candidate c2)` (OverloadResolution.cs) -- the
// Candidate-taking entry the `BetterFunctionMember` tiebreak calls. It first prefers the member with
// MORE formal parameters (in case both have different numbers of optional parameters), then falls back
// to the type-sequence overload over the two candidates' parameter types (`c.Parameters.Select(p => p.Type)`).
// Pure (reads only the candidates' `Parameters()`); no `OverloadResolution` instance state.
int MoreSpecificFormalParameters(const OverloadResolutionCandidate& c1,
                                 const OverloadResolutionCandidate& c2);

// The C# `int BetterParameterPassingChoice(Candidate c1, Candidate c2)` (OverloadResolution.cs) -- the
// C# 7.2 "prefer by-value parameters over in-parameters" tiebreak (`BetterFunctionMember` calls it after
// the lifted-operator tiebreak). For each parameter position (the two candidates have the same parameter
// count), a by-value (`ReferenceKind::None`) parameter beats an `in` (`ReferenceKind::In`) parameter; the
// direction-exclusive reduction (`c1IsBetter && !c2IsBetter` -> 1, the mirror -> 2, else 0) follows the
// `MoreSpecificFormalParameters` convention. Pure (reads only the candidates' `Parameters()`); no
// `OverloadResolution` instance state.
int BetterParameterPassingChoice(const OverloadResolutionCandidate& c1,
                                 const OverloadResolutionCandidate& c2);

// The C# `int BetterParamsCollectionType(IType paramsCollectionType1, IType paramsCollectionType2)`
// (OverloadResolution.cs, the C# 13.0 params-collection "better function member" tiebreak -- see
// https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/proposals/csharp-13.0/params-collections#better-function-member).
// Returns 1 if `paramsCollectionType1` is the better params-collection type, 2 if
// `paramsCollectionType2` is, 0 if neither. The two types are the candidates' `ParamsCollectionType`
// (the expanded form's last parameter type). The non-span arm prefers the type that implicitly
// converts to the other but not vice versa (`conversions.ImplicitConversion(IType, IType).IsValid`);
// the span arms prefer `ReadOnlySpan<T>` over `Span<T>` and a `Span<T>`/`ReadOnlySpan<T>` over an
// array/array-interface when the element types identity-match. `conversions.IdentityConversion`
// (C# public) ports to the `Detail::IdentityConversion` free function (the port has no public
// `IdentityConversion` method); `conversions.ImplicitConversion` is the cached public entry (threaded
// via `CSharpConversions&`, the `OverloadResolution.conversions` field). Takes `IType&` non-const
// (the implicit-conversion dispatch and `IdentityConversion` take non-const `IType&`, the non-const
// `AcceptVisitor`, D406).
int BetterParamsCollectionType(CSharpConversions& conversions,
                               ILSpy::Decompiler::TypeSystem::IType& paramsCollectionType1,
                               ILSpy::Decompiler::TypeSystem::IType& paramsCollectionType2);

// The C# `int BetterFunctionMember(Candidate c1, Candidate c2)` (OverloadResolution.cs line 730,
// C# spec draft-v11 section 12.6.4.3 "better function member") -- returns 1 if `c1` is the better
// function member, 2 if `c2` is, 0 if neither. The full decision: prefer the applicable candidate
// (the `ErrorCount==0` heuristic), then the per-argument better-conversion loop (the argument-to-
// parameter map + `conversions.IdentityConversion` over the formal parameter types +
// `conversions.BetterConversion` of each argument to the two target parameter types -- the
// direction-exclusive `c1IsBetter`/`c2IsBetter` reduction), then the less-errors heuristic, then the
// tie-breaking rules (only when neither is better AND the parameter types are all equal): non-
// generic beats generic, non-expanded beats expanded, fewer arguments-to-params, no-unmapped-
// optional-parameters, `MoreSpecificFormalParameters`, non-lifted operators (a `dynamic_cast` to
// `ILiftedOperator` -- the C# `Member as ILiftedOperator`), `BetterParameterPassingChoice`, and
// `BetterParamsCollectionType` (when both are expanded). `conversions.IdentityConversion` (C#
// public) ports to `Detail::IdentityConversion` (the port has no public `IdentityConversion`
// method, the D547 precedent); `conversions.BetterConversion` is the public `BetterConversion(
// ResolveResult, IType, IType)` entry (D544). The `arguments` and `conversions` are the
// `OverloadResolution` instance fields (the free function takes them as parameters, the
// D536 `CheckApplicabilityPassingModeAndConversions` precedent); the two candidates are passed by
// const ref (every candidate accessor read is const). The `Member()` accessor returns a
// `const IParameterizedMember*`; the lifted-operator check is a `dynamic_cast<const
// ILiftedOperator*>` cross-cast (the standalone `ILiftedOperator` base, the D549 interface).
int BetterFunctionMember(CSharpConversions& conversions,
                        const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
                        const OverloadResolutionCandidate& c1,
                        const OverloadResolutionCandidate& c2);

// The C# `void ConsiderIfNewCandidateIsBest(Candidate candidate)` (OverloadResolution.cs line 978) --
// the engine step that folds a freshly-calculated candidate into the running best-candidate state.
// It is an `OverloadResolution` instance method that reads/writes the `bestCandidate`/
// `bestCandidateWasValidated`/`bestCandidateAmbiguousWith` fields; the port lifts it to a `Detail::`
// free function taking those fields by reference (the D536 `CheckApplicabilityPassingModeAndConversions`
// precedent -- the instance fields are threaded as parameters since the free function has no instance
// state), so it is individually unit-testable. The decision: if `bestCandidate` is null, the candidate
// becomes the new best (and `bestCandidateWasValidated` resets to false); otherwise `BetterFunctionMember(
// candidate, bestCandidate)` decides -- 0 (neither better) overwrites `bestCandidateAmbiguousWith` with
// the candidate (so API users can track the set of ambiguous methods after each step), 1 (the new
// candidate is better) promotes it to best (resetting `bestCandidateWasValidated` and clearing
// `bestCandidateAmbiguousWith`), 2 (the existing best stays best) changes nothing. `conversions` and
// `arguments` are the `OverloadResolution` instance fields the `BetterFunctionMember` call needs (the
// D549 precedent); the candidate and the best state are shared handles (`std::shared_ptr<
// OverloadResolutionCandidate>`) -- the C# `bestCandidate = candidate` reference assignment ports to a
// shared_ptr copy (the candidate is shared, not moved).
void ConsiderIfNewCandidateIsBest(
    CSharpConversions& conversions,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    bool& bestCandidateWasValidated,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith,
    const std::shared_ptr<OverloadResolutionCandidate>& candidate);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
