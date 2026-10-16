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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.cs -- the
// result of a method/constructor/indexer invocation carrying the C#-specific information
// `InvocationResolveResult` (D438) does not. This is the eighth `cpp/Decompiler/CSharp/
// Resolver/` leaf toward the `CSharpResolver` leaf deps (the long-pole remaining blocker of
// `TypeSystemAstBuilder` / `CSharpAmbience`), after the D467 twin Alias `ResolveResult`
// subclasses, the D468 twin enum leaves, the D469 `DynamicMemberResolveResult`, the D470
// `AwaitResolveResult`, and the D471 `DynamicInvocationResolveResult`. It derives from
// `Semantics::InvocationResolveResult` (D438, which itself derives from `MemberResolveResult`
// D437) and adds the `OverloadResolutionErrors` (D468) mask plus three C#-specific bools and
// the optional argument-to-parameter map. All deps are already ported: `InvocationResolveResult`
// (D438, the base), `OverloadResolutionErrors` (D468), and `IParameterizedMember` (D388,
// the `Member` the base ctor takes). The C# surface is:
//   * `CSharpInvocationResolveResult(ResolveResult targetResult, IParameterizedMember
//      member, IList<ResolveResult> arguments, OverloadResolutionErrors
//      overloadResolutionErrors = None, bool isExtensionMethodInvocation = false,
//      bool isExpandedForm = false, bool isDelegateInvocation = false,
//      IReadOnlyList<int> argumentToParameterMap = null, IList<ResolveResult>
//      initializerStatements = null, IType returnTypeOverride = null) : base(targetResult,
//      member, arguments, initializerStatements, returnTypeOverride)` -- forwards to the
//     `InvocationResolveResult` base ctor and stores the C#-specific fields.
//   * `readonly OverloadResolutionErrors OverloadResolutionErrors` -- the per-invocation
//     error mask.
//   * `readonly bool IsExtensionMethodInvocation` -- calling an extension method using
//     extension-method syntax.
//   * `readonly bool IsDelegateInvocation` -- calling a delegate without `.Invoke()`.
//   * `readonly bool IsExpandedForm` -- a params-array used in its expanded form.
//   * `readonly IReadOnlyList<int> argumentToParameterMap` -- nullable; the map from
//     argument index to parameter index (-1 for unmapped).
//   * `override bool IsError => OverloadResolutionErrors != OverloadResolutionErrors.None`.
//   * `IReadOnlyList<int> GetArgumentToParameterMap()` -- returns the stored map.
//   * `override IList<ResolveResult> GetArgumentsForCall()` -- rebuilds the argument list
//     for params-array calls (DEFERRED here; the base `InvocationResolveResult::GetArgumentsForCall`
//     default returns `Arguments` directly -- the non-C#-specific behavior -- until the
//     `NamedArgumentResolveResult.Argument` shared-ownership exposure + the
//     `IParameter.IsOptional`/`GetConstantValue` + `Member.Compilation.FindType` surface
//     the full override needs are in place).
//
// KEY PORT CONVENTIONS:
//  * The ctor forwards `targetResult` / `member` / `arguments` / `initializerStatements` /
//    `returnTypeOverride` to the `InvocationResolveResult` base ctor (the D438 ctor shape,
//    which itself forwards to the `MemberResolveResult` common ctor that computes
//    `isVirtualCall`/`isConstant`/`constantValue`). The five C#-specific fields are stored
//    in the body. The C# `IList<ResolveResult> arguments`/`initializerStatements` `??`
//    empty defaults port to the `std::vector` defaulted-to-`{}` (the D438 convention); the
//    C# `IType returnTypeOverride = null` ports to `ITypePtr = nullptr`; the C#
//    `IReadOnlyList<int> argumentToParameterMap = null` (nullable) ports to
//    `std::optional<std::vector<int>>` (empty `optional` = the C# `null`).
//  * The C# `OverloadResolutionErrors OverloadResolutionErrors` (the D468 `[Flags]` enum
//    value) ports to a value member of the C++ `enum class OverloadResolutionErrors`
//    (`std::int32_t`-backed, the D468 port). Held by value and returned by value. The
//    `!= OverloadResolutionErrors::None` comparison in `IsError` is the faithful C++
//    `!=` on the scoped enum (no implicit `int` conversion needed).
//  * The three C# `bool` fields (`IsExtensionMethodInvocation`/`IsDelegateInvocation`/
//    `IsExpandedForm`) port to plain `bool` value members with defaulted `= false` member
//    initializers (the C# ctor-omission defaults via the `= false` params). Returned by
//    value.
//  * The C# `readonly IReadOnlyList<int> argumentToParameterMap` (a NULLABLE
//    reference-type list; the ctor takes it `= null`) ports to a
//    `std::optional<std::vector<int>>` member (empty `optional` = the C# `null`; a
//    present-but-empty vector = a non-null empty `IReadOnlyList`, distinct from null --
//    the D441 `ArrayCreateResolveResult::InitializerElements` `optional<vector>` precedent
//    for a nullable list). `GetArgumentToParameterMap()` returns the `optional` by value
//    (the caller inspects `.has_value()` / `*`); the C# returns `IReadOnlyList<int>` (the
//    nullable reference) so the `optional` is the faithful nullable-by-value port.
//  * The C# `override bool IsError => OverloadResolutionErrors != None` is the load-bearing
//    crux -- the invocation is an error when the overload-resolution error mask is non-empty
//    (NOT the base `MemberResolveResult::IsError` default which returns `false` and NOT the
//    `MemberResolveResult` `IsCompileTimeConstant`/`ConstantValue` machinery). The C++ port
//    overrides `IsError()` to `overloadResolutionErrors_ != OverloadResolutionErrors::None`.
//    This exercises the D468 `OverloadResolutionErrors` `[Flags]` enum directly (the
//    `CSharpInvocationResolveResult` is the primary consumer of the mask).
//  * `GetArgumentsForCall()` is DEFERRED (the override rebuilds the argument list for
//    params-array calls): it needs the `NamedArgumentResolveResult::Argument()` exposed as a
//    `shared_ptr` (the D446 port returns a raw `ResolveResult*`, not the shared handle, so
//    the unwrapped inner argument cannot be placed into the `vector<shared_ptr<ResolveResult>>`
//    the C# `results[]` maps to), the `IParameter.IsOptional`/`Type`/`GetConstantValue`
//    surface (for the optional-parameter fill), the `Member.Compilation.FindType` (for the
//    `Int32` size-argument constant), and the `ArrayCreateResolveResult`/`ConstantResolveResult`/
//    `ErrorResolveResult` constructors. The base `InvocationResolveResult::GetArgumentsForCall()`
//    default returns `Arguments` directly (the non-C#-specific behavior), so not overriding
//    it is the faithful deferred state (the C# override just rebuilds for params-arrays;
//    without it the base default applies). The `GetArgumentsForCall` override is the next
//    in-order piece once the `NamedArgumentResolveResult` shared-ownership exposure is in
//    place.
//  * `ToString` is inherited (the D424/D425 inherit-`ToString`-override-`ClassName`
//    convention): the C# does NOT override `ToString`, so overriding `ClassName()` to
//    "CSharpInvocationResolveResult" makes the inherited `ResolveResult::ToString` yield
//    "[CSharpInvocationResolveResult <resultType ReflectionName>]". Note the base
//    `InvocationResolveResult` also does NOT override `ToString` (only `ClassName`), so the
//    inherited chain is `ResolveResult::ToString` using the most-derived `ClassName()`.
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C#
//    inherited `MemberwiseClone` preserves the runtime type, so a non-overriding C++ base
//    clone (`make_unique<InvocationResolveResult>(*this)`) would slice a
//    `CSharpInvocationResolveResult` to its `InvocationResolveResult` base, diverging from
//    the C# `MemberwiseClone`. The override does `make_unique<CSharpInvocationResolveResult>(*this)`
//    (the default copy ctor shares the `arguments_`/`initializerStatements_` shared_ptr
//    vectors, copies the `OverloadResolutionErrors` value + the three bools + the
//    `argumentToParameterMap_` optional, and shares the base fields).
//  * The class is NOT `final` (the C# is unsealed), pinned by `static_assert(!is_final)`.
//
// DEFERRED vs the C#: the `GetArgumentsForCall()` override (rebuilds the argument list for
// params-array calls, unwrapping named arguments and synthesizing the params-array
// `ArrayCreateResolveResult`); it needs the `NamedArgumentResolveResult::Argument()`
// shared-ownership exposure + the `IParameter.IsOptional`/`GetConstantValue` + the
// `Member.Compilation.FindType` surface + the `ArrayCreateResolveResult`/`ConstantResolveResult`/
// `ErrorResolveResult` constructors. The base `InvocationResolveResult::GetArgumentsForCall()`
// default returns `Arguments` directly, so the deferred state is faithful (the override
// only rebuilds for params-arrays).

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_CSHARPINVOCATIONRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_CSHARPINVOCATIONRESOLVERESULT_HPP

#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public class CSharpInvocationResolveResult : InvocationResolveResult` (NOT
// `sealed`) ports to a C++ subclass (NOT `final`) of `Semantics::InvocationResolveResult`.
// The ctor forwards the five base args to the `InvocationResolveResult` base ctor and
// stores the C#-specific fields in the body.
class CSharpInvocationResolveResult : public ILSpy::Decompiler::Semantics::InvocationResolveResult {
public:
    // A type alias for the `OverloadResolutionErrors` enum -- the member function
    // `OverloadResolutionErrors()` (below) shadows the enum type name in the class scope
    // (a C# property-name-shares-enum-type idiom that C++ does not support without
    // qualification: a member function and an enclosing-namespace enum type cannot share
    // a name in the class body, since the function name hides the type). This alias
    // provides an unshadowed reference for the ctor parameter default, the member
    // declaration, and the `IsError` body. The alias IS the fully-qualified enum type.
    using Errors = ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;

    CSharpInvocationResolveResult(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> targetResult,
                                  const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member,
                                  std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments = {},
                                  Errors overloadResolutionErrors = Errors::None,
                                  bool isExtensionMethodInvocation = false,
                                  bool isExpandedForm = false,
                                  bool isDelegateInvocation = false,
                                  std::optional<std::vector<int>> argumentToParameterMap = std::nullopt,
                                  std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> initializerStatements = {},
                                  ILSpy::Decompiler::TypeSystem::ITypePtr returnTypeOverride = nullptr)
        : InvocationResolveResult(std::move(targetResult), member, std::move(arguments),
                                  std::move(initializerStatements), std::move(returnTypeOverride)),
          overloadResolutionErrors_(overloadResolutionErrors),
          isExtensionMethodInvocation_(isExtensionMethodInvocation),
          isExpandedForm_(isExpandedForm),
          isDelegateInvocation_(isDelegateInvocation),
          argumentToParameterMap_(std::move(argumentToParameterMap)) {}

    // The C# `readonly OverloadResolutionErrors OverloadResolutionErrors` -- the
    // per-invocation error mask (the D468 `[Flags]` enum). Returned by value. The return
    // type uses the `Errors` alias (the enum type) since the member function name shadows
    // the unqualified enum type in the class scope.
    Errors OverloadResolutionErrors() const noexcept {
        return overloadResolutionErrors_;
    }

    // The C# `readonly bool IsExtensionMethodInvocation` -- calling an extension method
    // using extension-method syntax.
    bool IsExtensionMethodInvocation() const noexcept { return isExtensionMethodInvocation_; }

    // The C# `readonly bool IsDelegateInvocation` -- calling a delegate without `.Invoke()`.
    bool IsDelegateInvocation() const noexcept { return isDelegateInvocation_; }

    // The C# `readonly bool IsExpandedForm` -- a params-array used in its expanded form.
    bool IsExpandedForm() const noexcept { return isExpandedForm_; }

    // The C# `override bool IsError => OverloadResolutionErrors != OverloadResolutionErrors.None`.
    // The invocation is an error when the overload-resolution error mask is non-empty (NOT
    // the base `MemberResolveResult::IsError` default of `false`). This is the load-bearing
    // crux exercising the D468 `OverloadResolutionErrors` leaf.
    bool IsError() const override {
        return overloadResolutionErrors_ != Errors::None;
    }

    // The C# `IReadOnlyList<int> GetArgumentToParameterMap()` -- the map from argument
    // index to parameter index (-1 for unmapped). Returns the `optional<vector<int>>` by
    // value; an empty `optional` is the C# `null` (no map), a present vector is the C#
    // non-null `IReadOnlyList<int>`.
    std::optional<std::vector<int>> GetArgumentToParameterMap() const {
        return argumentToParameterMap_;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the base `arguments_`/`initializerStatements_` shared_ptr
    // vectors are shared, the `OverloadResolutionErrors` value + the three bools + the
    // `argumentToParameterMap_` optional are copied, and the base fields are shared). The
    // C++ override reproduces this via the default copy ctor, avoiding the C++-only slicing
    // a non-overriding base clone (`make_unique<InvocationResolveResult>(*this)`) would
    // perform.
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<CSharpInvocationResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString` (the C# does NOT override `ToString`; the base
    // `InvocationResolveResult` also does NOT, so the inherited chain uses this
    // most-derived `ClassName`).
    std::string ClassName() const override { return "CSharpInvocationResolveResult"; }

private:
    Errors overloadResolutionErrors_ = Errors::None;
    bool isExtensionMethodInvocation_ = false;
    bool isExpandedForm_ = false;
    bool isDelegateInvocation_ = false;
    std::optional<std::vector<int>> argumentToParameterMap_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_CSHARPINVOCATIONRESOLVERESULT_HPP
