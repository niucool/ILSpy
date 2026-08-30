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
// `CSharp::Resolver::Detail` free functions so they are individually unit-testable (TDD): the
// parameter-type/applicability steps (`ResolveParameterTypes`/`MapCorrespondingParameters`/
// `CheckApplicabilityArgumentCounts`/`CheckApplicabilityPassingModeAndConversions`), the better-
// function-member tiebreaks, the best-candidate folding, the constraint-validation region
// (the public 3-arg `ValidateConstraints` overload, `GetSubstitution`, and
// `ValidateMethodConstraints`), the `RunTypeInference` engine step, the `CalculateCandidate`
// composition that wires the steps together in the C# order, and the `AddCandidate` entry that
// calls it plus the `AddMethodLists` scan that feeds it (the derived-type-hides-base-methods
// walk) with the `LogCandidateAddingResult` debug-log helper, the `BestCandidateErrors`
// output property (the lazy `ValidateMethodConstraints` memoization), and the
// `GetArgumentsWithConversions` output-wrapper core (the conversion/named argument wrapping).

#pragma once

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"  // TypeParameterSubstitution (GetSubstitution's by-value return)

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (now ported): the conversion controller. `CheckApplicabilityPassingModeAndConversions`
// reads `CSharpConversions::ImplicitConversion(ResolveResult, IType)` (the D529 public entry).
class CSharpConversions;

// Forward-declared (now ported, in MethodGroupResolveResult.hpp): the method-list bucket type
// `AddMethodLists` takes (`const std::vector<MethodListWithDeclaringType>&` needs only a
// declaration; the .cpp includes the full header).
class MethodListWithDeclaringType;

} // namespace ILSpy::Decompiler::CSharp::Resolver

// `ResolveResult` is forward-declared (the `std::vector<std::shared_ptr<ResolveResult>>` parameter
// needs only a declaration, not the full definition; the .cpp includes the full header).
namespace ILSpy::Decompiler::Semantics { class ResolveResult; }

// `Conversion` is forward-declared (the `std::vector<std::shared_ptr<Conversion>>` parameter needs
// only a declaration; the .cpp includes the full header).
namespace ILSpy::Decompiler::Semantics { class Conversion; }

// `IType` is forward-declared (the `const IType&`/`const IType*&` parameters need only a
// declaration, not the full definition; the .cpp includes the full header).
namespace ILSpy::Decompiler::TypeSystem { class IType; }

// `ITypeParameter` is forward-declared (the `const ITypeParameter&` parameter needs only a
// declaration; the .cpp includes the full header).
namespace ILSpy::Decompiler::TypeSystem { class ITypeParameter; }

// `TypeVisitor` is forward-declared (the nullable `TypeVisitor*` parameter needs only a
// declaration; the .cpp includes the full header).
namespace ILSpy::Decompiler::TypeSystem { class TypeVisitor; }

// `IParameterizedMember` is forward-declared (the `GetBestCandidateWithSubstitutedTypeArguments`
// return type is a nullable non-owning pointer -- a complete pointer type with the class
// incomplete; the .cpp includes the full header).
namespace ILSpy::Decompiler::TypeSystem { class IParameterizedMember; }

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

// The C# `internal static bool ValidateConstraints(ITypeParameter typeParameter, IType typeArgument,
// TypeVisitor substitution, CSharpConversions conversions)` (OverloadResolution.cs line 585, the
// "Validate Constraints" region) -- whether the type argument satisfies the type parameter's
// constraints. It is the C# spec section 4.4.4 "satisfying constraints" check: `void`/`null`/pointer
// type arguments are rejected outright; the `class` constraint needs a definite
// `IsReferenceType == true` (an indeterminate optional FAILS, not passes); the `struct`/`unmanaged`
// constraint needs a non-nullable value type (`NullableType.IsNonNullableValueType`); the `new()`
// constraint rejects an abstract definition and requires a public parameterless constructor
// (`GetConstructors` with `IgnoreInheritedMembers | ReturnMemberDefinitions`); and every direct base
// type (the declared `where T : Base` constraints) must be constraint-convertible from the type
// argument, after applying the optional `substitution` (the `TypeVisitor` that replaces type
// parameters with type arguments -- the constraint may itself reference another type parameter).
// It is a static method with no `OverloadResolution` instance state, so it lifts to a `Detail::`
// free function (the established convention). `typeParameter` is `const&` (every constraint-flag /
// `DirectBaseTypes` read is const); `typeArgument` is non-const `IType&` because
// `CSharpConversions::IsConstraintConvertible` takes non-const `IType&` (the non-const
// `AcceptVisitor`, D406); `substitution` is a nullable raw pointer (the C# `null` = no
// substitution); `conversions` is a non-const reference (the public `IsConstraintConvertible` is
// non-const). The callers: the public static `ValidateConstraints(ITypeParameter, IType,
// TypeVisitor)` overload below (which fetches the conversions via `CSharpConversions.Get`), the
// `ValidateMethodConstraints` engine step below, and the `ConstraintValidatingSubstitution`
// inside `RunTypeInference` (deferred until the engine step lands).
bool ValidateConstraints(const ILSpy::Decompiler::TypeSystem::ITypeParameter& typeParameter,
                         ILSpy::Decompiler::TypeSystem::IType& typeArgument,
                         ILSpy::Decompiler::TypeSystem::TypeVisitor* substitution,
                         CSharpConversions& conversions);

// The C# `public static bool ValidateConstraints(ITypeParameter typeParameter, IType typeArgument,
// TypeVisitor substitution = null)` (OverloadResolution.cs line 576, the "Validate Constraints"
// region) -- the public 3-arg overload of the internal static above (the 4-arg
// `Detail::ValidateConstraints`). It is the entry point the `ValidateMethodConstraints` engine
// step and the `CSharpResolver` (CSharpResolver.cs line 2161) call: it resolves the
// `CSharpConversions` from the type parameter's own compilation -- `CSharpConversions.Get(
// typeParameter.Owner.Compilation)` -- and delegates to the internal static. The C#
// `ArgumentNullException` guards compile out (the C++ references are non-null by construction,
// the D374 convention); the caller passes the substitution explicitly (no C++ default argument
// -- the C# `= null` default is a call-site convenience, not behavior). SAFE FALLBACK: the C#
// dereferences `typeParameter.Owner` unconditionally, which NREs for the dummy type parameters
// (the `ITypeParameter.Owner` contract is nullable -- "null for the dummy type parameters");
// the port returns `false` (constraints not satisfied) instead of crashing -- a type parameter
// without an owning entity has no compilation to resolve conversions from, so the constraints
// cannot be validated. The `false` verdict is the soft direction in both real callers
// (`ValidateMethodConstraints` records the `MethodConstraintsNotSatisfied` soft error, which
// `IsApplicable` masks out so the candidate stays applicable).
bool ValidateConstraints(const ILSpy::Decompiler::TypeSystem::ITypeParameter& typeParameter,
                         ILSpy::Decompiler::TypeSystem::IType& typeArgument,
                         ILSpy::Decompiler::TypeSystem::TypeVisitor* substitution);

// The C# `TypeParameterSubstitution GetSubstitution(Candidate candidate)` (OverloadResolution.cs
// line 1168) -- the merged substitution for a candidate: the member's CLASS type arguments (from
// `candidate.Member.Substitution.ClassTypeArguments`) combined with the candidate's INFERRED
// METHOD type arguments (`candidate.InferredTypes`). The C# comment "Do not compose the
// substitutions, but merge them. / This is required for InvocationTests.
// SubstituteClassAndMethodTypeParametersAtOnce": composing the member's own substitution with
// the inferred one would double-substitute the class type parameters, so only the class arguments
// are taken from the member and the method arguments from the inference result. It is a private
// `OverloadResolution` instance method, but it reads only candidate state (`Member.Substitution` /
// `InferredTypes` -- no instance fields), so it lifts to a pure `Detail::` free function taking
// the candidate by const ref (the established convention). The C# heap allocation is realized as
// a by-value return (the `TypeParameterSubstitution::Compose` convention). SAFE FALLBACK: the C#
// `IMember.Substitution` contract is "never null" ("Returns `Identity` for not specialized"), but
// the shared test stub returns nullptr; the port falls back to the `Identity` singleton (whose
// `ClassTypeArguments` is `nullopt` -- "keep the class type parameters unmodified"), the
// documented faithful counterpart of the not-specialized contract.
ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution GetSubstitution(const OverloadResolutionCandidate& candidate);

// The C# `OverloadResolutionErrors ValidateMethodConstraints(Candidate candidate)`
// (OverloadResolution.cs line 548, the "Validate Constraints" region) -- validates the generic
// method candidate's type-argument constraints after type inference. If type inference already
// failed, the constraints are NOT checked (the `TypeInferenceFailed` mask bit short-circuits to
// `None` -- the inferred types are unusable); a non-generic candidate (no method type parameters)
// short-circuits to `None` as well. Otherwise every method type parameter is validated against
// the corresponding inferred type argument (the `GetSubstitution` merge supplies both the
// arguments and the substitution visitor applied to constraints that reference type parameters)
// via the public 3-arg `ValidateConstraints` overload above; the first violation yields
// `MethodConstraintsNotSatisfied` (a soft error -- `IsApplicable` masks it out, so the candidate
// stays applicable; it still shows up in the final `BestCandidateErrors` and makes the created
// `CSharpInvocationResolveResult` an error). It is a private `OverloadResolution` instance method,
// but it reads only candidate state (`Errors` / `TypeParameters` / `Member` / `InferredTypes` --
// the conversions are resolved inside the 3-arg overload from the type parameter's own
// compilation, not from the instance), so it lifts to a pure `Detail::` free function taking the
// candidate by const ref (the established convention). SAFE FALLBACKS for the degenerate
// pre-inference candidate (the C# indexes `substitution.MethodTypeArguments[i]` unconditionally,
// which throws for a null/short `InferredTypes` array -- unreachable in the real engine flow,
// where `ValidateMethodConstraints` only runs after `RunTypeInference` populated the array): an
// out-of-range index or a null inferred-type entry yields `MethodConstraintsNotSatisfied` (the
// soft unverifiable verdict, the same masked-out direction as a genuine constraint violation).
ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors
ValidateMethodConstraints(const OverloadResolutionCandidate& candidate);

// The C# `public IParameterizedMember GetBestCandidateWithSubstitutedTypeArguments()`
// (OverloadResolution.cs line 1153) -- the best candidate with the inferred type arguments
// substituted into its generic method definition: a null best candidate yields null; a member
// that is not a generic `IMethod` (a non-method parameterized member such as an indexer, or a
// method with no type parameters) is returned as-is; a GENERIC method is re-specialized through
// its MEMBER DEFINITION -- `((IMethod)method.MemberDefinition).Specialize(GetSubstitution(
// bestCandidate))` -- so the returned member carries the merged substitution (the member's own
// class type arguments + the candidate's inferred method type arguments, the `GetSubstitution`
// merge-not-compose contract; the definition is re-specialized, NOT the already-specialized
// member, so the inferred arguments replace the member's own method arguments). It is a public
// `OverloadResolution` instance method reading only the `bestCandidate` field, so the port lifts
// it to a `Detail::` free function taking that state by const reference (the
// `ConsiderIfNewCandidateIsBest` state-threading convention; nullable -- a null `shared_ptr` is
// the C# `bestCandidate == null`; the thin member method lands when the engine wires). The
// return is a non-owning nullable `const IParameterizedMember*` (the candidate owns the member;
// the `Specialize` result is owned by the type system, the `IMember::Specialize` contract).
// SAFE FALLBACK for the degenerate shapes the C# hard cast `((IMethod)method.MemberDefinition)`
// would throw `InvalidCastException` on (a definition that is not an `IMethod`, or a null
// definition -- a real method's definition is always a method, "Returns `this` if this is not a
// specialized member"): the member is returned as-is (the D516 safe-fallback convention).
const ILSpy::Decompiler::TypeSystem::IParameterizedMember* GetBestCandidateWithSubstitutedTypeArguments(
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidate);

// The C# `sealed class ConstraintValidatingSubstitution : TypeParameterSubstitution` (Overload-
// Resolution.cs lines 503-521, nested in the `RunTypeInference` region) -- the substitution
// `RunTypeInference` applies to the formal parameter types after type inference, which additionally
// VALIDATES the constructed generic types: whenever `VisitParameterizedType` produces a changed
// parameterized type (some type argument was substituted), every type argument is checked against
// the corresponding declared type parameter's constraints (C# 4.0 spec section 4.4.4 "Satisfying
// constraints") through the already-ported internal static `ValidateConstraints` above, with the
// CONSTRUCTED type's own `GetSubstitution()` applied to constraints that reference type parameters;
// the first violation flips `ConstraintsValid` to false (`RunTypeInference` then adds
// `ConstructedTypeDoesNotSatisfyConstraint` to the candidate).
//
// The C# `base.VisitParameterizedType(type)` resolves to the `TypeVisitor` default
// (`type.VisitChildren(this)`, visiting the generic type and the type arguments through THIS
// substitution) because `TypeParameterSubstitution` does not override `VisitParameterizedType` --
// neither in the C# nor in the port; the port's qualified `TypeVisitor::VisitParameterizedType`
// call mirrors that resolution exactly. `ConstraintsValid` is a public mutable FIELD in the C#
// (`public bool ConstraintsValid = true;`), so the port keeps it a public data member (the `TP`
// struct precedent) rather than an accessor pair. SAFE FALLBACKS for the degenerate shapes the C#
// would throw on (`typeArguments[index]` indexes without a bounds check; a constructed stub could
// carry fewer type arguments than declared type parameters, or a null entry): the loop is bounded
// to the constructed type's actual type-argument count and a null type argument counts as a
// VIOLATION (constraints cannot be satisfied by a missing argument), the D516 convention for
// shapes unreachable through the real type system.
class ConstraintValidatingSubstitution : public ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution {
public:
    // The C# `ConstraintValidatingSubstitution(IReadOnlyList<IType> classTypeArguments,
    // IReadOnlyList<IType> methodTypeArguments, OverloadResolution overloadResolution)` -- the
    // C# reads the conversions out of the `OverloadResolution` instance; the port threads them
    // as a parameter (the instance-state-threading convention).
    ConstraintValidatingSubstitution(
        std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> classTypeArguments,
        std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> methodTypeArguments,
        CSharpConversions& conversions)
        : TypeParameterSubstitution(std::move(classTypeArguments), std::move(methodTypeArguments)),
          conversions_(conversions) {}

    // The C# `public bool ConstraintsValid = true;` -- the public mutable field the outer
    // `RunTypeInference` reads after the visitor ran.
    bool ConstraintsValid = true;

    // The C# `public override IType VisitParameterizedType(ParameterizedType type)` --
    // out-of-line (the body calls `ValidateConstraints`, whose full `CSharpConversions&`
    // parameter needs the .cpp's include; the header keeps only the forward declaration).
    ILSpy::Decompiler::TypeSystem::ITypePtr VisitParameterizedType(
        ILSpy::Decompiler::TypeSystem::ParameterizedType& type) override;

private:
    CSharpConversions& conversions_;
};

// The C# `void RunTypeInference(Candidate candidate)` (OverloadResolution.cs lines 438-486, the
// "RunTypeInference" region) -- the type-inference engine step of `CalculateCandidate`: resolves
// the candidate's method type arguments (either the explicitly given type arguments, or the
// `TypeInference` engine over the argument/parameter-type pairs built from
// `ArgumentToParameterMap`), then substitutes them into the formal parameter types through the
// `ConstraintValidatingSubstitution` (which additionally flags `ConstructedTypeDoesNotSatisfy`
// `Constraint` when a constructed generic type violates its type-parameter constraints).
//
// The decision tree:
//   * a NON-GENERIC candidate (`candidate.TypeParameters == null` in the C#, the empty vector in
//     the port) with explicitly given type arguments adds `WrongNumberOfTypeArguments` (the method
//     does not expect type arguments, but was given some); either way it re-grabs the parameter
//     types from the SPECIALIZED member (`ResolveParameterTypes(candidate, true)`) and returns.
//   * a generic candidate: the member's declaring type contributes the class type arguments when
//     it is a `ParameterizedType` (the `C<int>` in `c.M<U>(...)`); explicit type arguments of the
//     matching count become the inferred types as-is, a mismatched count adds
//     `WrongNumberOfTypeArguments` and truncates/pads the list to the type-parameter count (the
//     pad entries are `UnknownType`); with NO explicit type arguments, the ported `Detail::
//     InferTypeArguments` engine runs over the projected parameter types (`ArgumentToParameterMap`
//     entry -> the mapped `ParameterTypes` entry, `UnknownType` for an unmapped argument -- the C#
//     `SelectReadOnlyArray` projection), adding `TypeInferenceFailed` when it reports failure.
//   * finally the merged substitution (class + inferred method type arguments) is applied to every
//     formal parameter type, and a `ConstraintsValid == false` adds
//     `ConstructedTypeDoesNotSatisfyConstraint`.
//
// The C# is a private instance method reading `compilation`/`conversions`/`arguments`/
// `explicitlyGivenTypeArguments`; the port lifts it to a `Detail::` free function taking those as
// parameters (the D536 `CheckApplicabilityPassingModeAndConversions` state-threading convention),
// individually unit-testable ahead of the `CalculateCandidate`/`AddCandidate` composition steps
// that call it. `candidate` is non-const (the inference + substitution MUTATE the candidate's
// `InferredTypes`/`ParameterTypes`/`Errors`); `conversions` is a non-const reference (the
// `TypeInference` engine's `Fix`/`FindTypesInBounds` and the `ValidateConstraints` constraint
// check call the non-const public conversion entries). SAFE FALLBACK: a never-filled `Parameter
// Types` entry (null -- the C# would NRE on `AcceptVisitor`) is skipped, the D516 convention for
// the degenerate pre-`ResolveParameterTypes` shape unreachable through the real engine flow.
void RunTypeInference(OverloadResolutionCandidate& candidate,
                      const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                      CSharpConversions& conversions,
                      const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
                      const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& explicitlyGivenTypeArguments);

// The C# `bool CalculateCandidate(Candidate candidate)` (OverloadResolution.cs line 278) -- the
// engine step that composes the whole ported pipeline in the C# order:
//   1. `ResolveParameterTypes(candidate, false)` -- a candidate whose params-collection cannot
//      be unpacked (the expanded form's last parameter is not an array/Span/array-interface)
//      aborts with `return false` (the candidate is removed WITHOUT reporting an error; every
//      later step, including the best-candidate folding, is skipped);
//   2. `MapCorrespondingParameters` -- the argument-to-parameter map (the `arguments`/
//      `argumentNames` ctor fields threaded as parameters);
//   3. `RunTypeInference` -- the inferred method type arguments + the substituted formal
//      parameter types (the `explicitlyGivenTypeArguments` ctor field threaded);
//   4. `CheckApplicability` -- the single C# method split into the two ported halves (the
//      argument-count check, then the passing-mode + conversion check; the
//      `AllowOptionalParameters`/`AllowImplicitIn`/`IsExtensionMethodInvocation` input
//      properties threaded);
//   5. `ConsiderIfNewCandidateIsBest` -- the best-candidate folding (the `bestCandidate`/
//      `bestCandidateWasValidated`/`bestCandidateAmbiguousWith` instance fields threaded by
//      reference, the D550 state-threading convention).
// Returns true when the calculation ran (the candidate stays in the resolution regardless of
// applicability -- the errors are reported later through `BestCandidateErrors`).
//
// The C# is a private instance method reading the `OverloadResolution` ctor fields and input
// properties; the port lifts it to a `Detail::` free function taking those as parameters (the
// D536 `CheckApplicabilityPassingModeAndConversions` state-threading convention), individually
// unit-testable ahead of the `AddCandidate` entry that calls it. `candidate` is a shared handle
// (the C# `AddCandidate` allocates the candidate and hands the SAME instance to the folding
// step, which stores it in `bestCandidate`/`bestCandidateAmbiguousWith`; the port mirrors the
// shared ownership so the folding's `bestCandidate = candidate` reference assignment is a
// `shared_ptr` copy). The steps mutate the candidate, so the shared handle is passed by const
// reference and dereferenced non-const through it (the object is shared, not const).
bool CalculateCandidate(
    const std::shared_ptr<OverloadResolutionCandidate>& candidate,
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    CSharpConversions& conversions,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<std::string>& argumentNames,
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& explicitlyGivenTypeArguments,
    bool allowOptionalParameters,
    bool allowImplicitIn,
    bool isExtensionMethodInvocation,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    bool& bestCandidateWasValidated,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith);

// The C# `public OverloadResolutionErrors AddCandidate(IParameterizedMember member,
// OverloadResolutionErrors additionalErrors)` (OverloadResolution.cs line 231) -- the engine
// entry that adds a candidate member to the resolution: it allocates the NORMAL-form candidate
// (`new Candidate(member, false)`), applies the `additionalErrors` (the errors that apply to the
// candidate from the member lookup, e.g. `Inaccessible`), and calculates it (the `CalculateCandidate`
// composition above -- the candidate is folded into the best-candidate state regardless of
// applicability); then, when `AllowExpandingParams` is set and the member's LAST parameter is a
// `params` parameter, it allocates and calculates the EXPANDED-form candidate (`new Candidate(
// member, true)` -- the params collection unpacked so each argument fills the element type), and
// when the expanded form ran (not aborted) and has a strictly LOWER `ErrorCount` than the normal
// form, its errors are returned instead. Otherwise the normal form's errors are returned.
// "Note: this method does not return errors that do not affect applicability" -- the returned mask
// is the winning form's accumulated mask (the `AmbiguousMatch`/`MethodConstraintsNotSatisfied`
// soft bits may be present; the `AddMethodLists` caller filters with `IsApplicable`).
//
// The C# `ArgumentNullException` on a null member compiles out (the C++ reference is non-null by
// construction, the D374 convention). The C# is a public instance method reading the ctor fields
// and input properties plus mutating the `bestCandidate` state; the port lifts it to a `Detail::`
// free function taking those as parameters (the D574 `CalculateCandidate` state-threading
// convention), individually unit-testable. `allowExpandingParams` is the `AllowExpandingParams`
// input property (default true). The member's OWN parameter list is read for the `params` check
// (`member.Parameters` -- the possibly-specialized member, NOT the definition's parameters the
// candidate ctor reads). SAFE FALLBACK: the C# indexes `member.Parameters[Count - 1]` without a
// null-entry guard (a null parameter entry is impossible through the real type system); the
// port treats a null last parameter as not-params (the expanded form is skipped), the D516
// convention for the degenerate shape.
OverloadResolutionErrors AddCandidate(
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember& member,
    OverloadResolutionErrors additionalErrors,
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    CSharpConversions& conversions,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<std::string>& argumentNames,
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& explicitlyGivenTypeArguments,
    bool allowExpandingParams,
    bool allowOptionalParameters,
    bool allowImplicitIn,
    bool isExtensionMethodInvocation,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    bool& bestCandidateWasValidated,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith);

// The C# `internal void LogCandidateAddingResult(string text, IParameterizedMember method,
// OverloadResolutionErrors errors)` (OverloadResolution.cs line 377) -- the per-candidate debug
// log line: `"{text} {method} = {Success|errors}{ (best candidate so far)| (ambiguous)}"`.
// The C# is `[Conditional("DEBUG")]` (the CALL is elided in release builds); the port keeps
// every call site compiled and relies on `Log::IsEnabled == false` making the body a no-op
// (the Log.hpp [Conditional]-analogue convention -- the message pieces are still rendered
// before the discarded write, the documented arguments-still-evaluated divergence).
// The C# is an instance method reading `this.BestCandidate`/`this.BestCandidateAmbiguousWith`
// (reference equality against `method`); the port threads the best-candidate state so BOTH
// the `Detail::AddMethodLists` free function below AND the later `CallBuilder` /
// `MethodGroupResolveResult.PerformOverloadResolution` call sites (CallBuilder.cs line 1585,
// MethodGroupResolveResult.cs line 299 -- they hold an `OverloadResolution` instance and will
// pass its state) can use the one implementation. DIVERGENCE (logging-only, output is compiled
// out): the C# `{1}` placeholder renders `IParameterizedMember.ToString()` (the full member
// signature); the ported type system has no `ToString` on members, so the member NAME is
// rendered instead.
void LogCandidateAddingResult(
    const char* text,
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember& method,
    OverloadResolutionErrors errors,
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith);

// The C# `public void AddMethodLists(IReadOnlyList<MethodListWithDeclaringType> methodLists)`
// (OverloadResolution.cs line 330) -- "Adds all candidates from the method lists. This method
// implements the logic that causes applicable methods in derived types to hide all methods in
// base types." Base types come FIRST in the list, so the list is walked BACKWARDS (derived
// types first); every candidate of each non-hidden list is added through `AddCandidate`
// (additionalErrors None), and once a list produced an APPLICABLE candidate, every earlier
// (more-base) list `j < i` whose `DeclaringType` equals a base type of the current list's
// declaring type (the `GetAllBaseTypes` reflexive-transitive closure -- the declaring type
// itself, its direct bases, and their bases) is marked hidden and skipped entirely.
//
// The C# `ArgumentNullException` on a null `methodLists` compiles out (the reference is
// non-null by construction, the D374 convention). The `isHiddenByDerivedType` array is only
// allocated for more than one list (the C# keeps it null otherwise); the port models the null
// with an empty vector plus a `methodLists.size() > 1` guard -- exactly the C# `!= null` check,
// and every `isHidden[j]` read happens on a live array because the hiding block requires
// `i > 0`, which implies at least two lists. SAFE FALLBACKS for the degenerate shapes the C#
// would throw on / NRE on (both impossible through the real member-lookup construction of the
// lists): a null method entry is skipped (the C# `AddCandidate` throws `ArgumentNullException`
// on it -- the D516 convention), and a null `GetAllBaseTypes` entry is skipped rather than
// dereferenced. The `MethodListWithDeclaringType` buckets' `DeclaringType()` is a non-null
// reference contract (the D374 convention; a null `ITypePtr` ctor arg is UB-by-contract).
//
// The C# is a public instance method reading the ctor fields and input properties plus
// mutating the `bestCandidate` state through `AddCandidate`; the port lifts it to a `Detail::`
// free function taking those as parameters (the D574/D575 state-threading convention),
// individually unit-testable. `AllowExpandingParams` and the other input properties are
// threaded the same way `Detail::AddCandidate` threads them.
void AddMethodLists(
    const std::vector<MethodListWithDeclaringType>& methodLists,
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    CSharpConversions& conversions,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<std::string>& argumentNames,
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& explicitlyGivenTypeArguments,
    bool allowExpandingParams,
    bool allowOptionalParameters,
    bool allowImplicitIn,
    bool isExtensionMethodInvocation,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    bool& bestCandidateWasValidated,
    std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith);

// The C# `public OverloadResolutionErrors BestCandidateErrors` getter (OverloadResolution.cs
// line 1015) -- "the errors that apply to the best candidate. This includes additional
// errors that do not affect applicability (e.g. AmbiguousMatch, MethodConstraintsNotSatisfied)".
// The lazy `ValidateMethodConstraints` memoization: when `bestCandidateWasValidated` is false
// (the initial state, and the state `ConsiderIfNewCandidateIsBest` restores whenever a new best
// is promoted), the constraint check runs and its result is memoized into
// `bestCandidateValidationResult` with the flag set; a repeat read reuses the memoized result
// without re-running the check. The returned mask is `bestCandidate.Errors |
// bestCandidateValidationResult`, plus `AmbiguousMatch` when `bestCandidateAmbiguousWith` is
// non-null. A null best candidate short-circuits to `None` BEFORE the memoization state is
// touched (the C# early return leaves both fields as-is -- load-bearing: had the flag been set
// on this path, a later read after a best IS added would skip validation with a stale result).
//
// The C# property getter MUTATES the two memoization fields, so the port lifts it to a
// `Detail::` free function taking them by reference (the `ConsiderIfNewCandidateIsBest`
// state-threading convention); the public `OverloadResolution::BestCandidateErrors()` method
// (non-const -- it mutates the instance state through this call) delegates with the instance
// fields threaded.
OverloadResolutionErrors BestCandidateErrors(
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    bool& bestCandidateWasValidated,
    OverloadResolutionErrors& bestCandidateValidationResult,
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidateAmbiguousWith);

// The C# `IList<ResolveResult> GetArgumentsWithConversions(ResolveResult targetResolveResult,
// IParameterizedMember bestCandidateForNamedArguments)` (OverloadResolution.cs line 1110) --
// the PRIVATE core the public output wrappers build on: "Returns the arguments for the method
// call in the order they were provided (not in the order of the parameters). Arguments are
// wrapped in a `ConversionResolveResult` if an implicit conversion is being applied to them
// when calling the method", and -- when `bestCandidateForNamedArguments` is non-null and the
// argument was passed with an explicit name -- in a `NamedArgumentResolveResult` (composed
// AROUND the conversion wrap). The C# instance method reads the `IsExtensionMethodInvocation`/
// `CheckForOverflow` input properties and the `arguments`/`argumentNames`/`bestCandidate` ctor
// fields plus the `ArgumentConversions` property; the port threads them as parameters (the
// `CalculateCandidate`/`BestCandidateErrors` state-threading convention) -- `conversions` is
// the `ArgumentConversions` snapshot the CALLER takes (the public methods pass
// `OverloadResolution::ArgumentConversions()`; the C# reads `this.ArgumentConversions`).
//
// The per-argument steps, in the C# order:
//  1. the extension-method receiver swap: `IsExtensionMethodInvocation && i == 0 &&
//     targetResolveResult != null` replaces the first argument with the target (the resolved
//     receiver; only the first argument, and only when a target was given);
//  2. the conversion wrap: a MAPPED argument (`ArgumentToParameterMap[i] >= 0`) under a
//     NON-identity conversion is wrapped in a `ConversionResolveResult` carrying the best
//     candidate's (substituted) parameter type, the original argument, the applied
//     conversion, and the `CheckForOverflow` flag -- but NOT when the parameter type is
//     `TypeKind.Unknown` (the unresolved shape stays unwrapped). The identity-conversion
//     check is POINTER IDENTITY against the `IdentityConversion` singleton (the C#
//     `!= Conversion.IdentityConversion` reference comparison, the `CheckApplicability`
//     convention). DEFERRED: the C# constant arm (`arguments[i].IsCompileTimeConstant &&
//     conversions[i].IsValid && !conversions[i].IsUserDefined` ->
//     `new CSharpResolver(compilation).WithCheckForOverflow(CheckForOverflow).ResolveCast(...)`
//     -- re-resolving a compile-time constant through the target type) needs the unported
//     `CSharpResolver.ResolveCast`; the faithful fallback wraps the constant in the
//     `ConversionResolveResult` too (the C# else branch), preserving the wrapper structure
//     (target type + applied conversion) -- only the constant is not re-folded;
//  3. the named wrap: when `bestCandidateForNamedArguments` is non-null and the argument was
//     passed with an explicit name (the C# `argumentNames[i] != null`; the port normalizes
//     the null entry to the empty string), the argument is wrapped in a
//     `NamedArgumentResolveResult` -- carrying the parameter AND the member when the argument
//     is mapped (`bestCandidateForNamedArguments.Parameters[parameterIndex]`), or the name
//     only when unmapped.
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> GetArgumentsWithConversions(
    const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& targetResolveResult,
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* bestCandidateForNamedArguments,
    bool isExtensionMethodInvocation,
    bool checkForOverflow,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<std::string>& argumentNames,
    const std::shared_ptr<OverloadResolutionCandidate>& bestCandidate,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>>& conversions);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
