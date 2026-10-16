// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software, including without limitation, the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/InvocationResolveResult.cs -- the result of a
// method, constructor or indexer invocation. It is the fourteenth `Semantics` leaf toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker), deriving from
// `MemberResolveResult` (D437) and adding the call's `Arguments` and `InitializerStatements`.
// All deps are already ported: `MemberResolveResult` (D437), `IParameterizedMember` (D388,
// for the `new Member` downcast), `IType` (D271, for the optional `returnTypeOverride`),
// and `ResolveResult` (D424, for the argument/initializer element type).

#ifndef ILSPY_DECOMPILER_SEMANTICS_INVOCATIONRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_INVOCATIONRESOLVERESULT_HPP

#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class InvocationResolveResult : MemberResolveResult` (NOT `sealed`)
// ports to a C++ subclass (NOT `final`) of `MemberResolveResult`. The C# surface is:
//   * `readonly IList<ResolveResult> Arguments` -- the call's arguments (defaults to
//     `EmptyList<ResolveResult>.Instance` when the ctor is passed null).
//   * `readonly IList<ResolveResult> InitializerStatements` -- the object/collection
//     initializer statements applied to the call's result (defaults to
//     `EmptyList<ResolveResult>.Instance`).
//   * Ctor `(ResolveResult targetResult, IParameterizedMember member,
//     IList<ResolveResult> arguments = null, IList<ResolveResult> initializerStatements
//     = null, IType returnTypeOverride = null)` -- delegates to the `MemberResolveResult`
//     common ctor (ctor 1) which computes `isVirtualCall`/`isConstant`/`constantValue`.
//   * `new IParameterizedMember Member { get { return (IParameterizedMember)base.Member; } }`
//     -- a `new` (hiding) property downcasting the inherited `IMember Member` to the
//     `IParameterizedMember` the ctor was built with.
//   * `virtual IList<ResolveResult> GetArgumentsForCall()` -- returns `Arguments` (a
//     derived class overrides it to rebuild the list for `params`-array calls).
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- the base's target
//     result followed by `Arguments` then `InitializerStatements`.
//   * `ToString` is inherited from `MemberResolveResult` (it uses the polymorphic
//     `ClassName()`, so overriding `ClassName()` is enough).
//
// KEY PORT CONVENTIONS:
//  * The C# `IList<ResolveResult> Arguments` / `InitializerStatements` (reference-type
//    lists the ctor guards with `?? EmptyList<ResolveResult>.Instance`) port to
//    `std::vector<std::shared_ptr<ResolveResult>>` members (the D428
//    `ByReferenceResolveResult` `shared_ptr<ResolveResult>` precedent for a held
//    `ResolveResult` list: shared ownership so `ShallowClone`'s default copy ctor shares
//    the elements faithfully mirroring the C# `MemberwiseClone` reference-copy of the
//    list). The `?? EmptyList` null-default ports to a defaulted `{}` empty vector (the
//    D-TypeConstraint `?? EmptyList`-to-empty-vector convention); the ctor stores the
//    vectors by value, so a null C# call (an empty C++ vector) and a populated call both
//    round-trip.
//  * The C# `new IParameterizedMember Member` (a hiding property downcasting the
//    inherited `IMember Member`) ports to a NON-VIRTUAL `const IParameterizedMember*
//    Member()` that HIDES the base `MemberResolveResult::Member()` (the base accessor is
//    non-virtual, so a same-name derived accessor hides it, mirroring the C# `new`). The
//    `(IParameterizedMember)base.Member` downcast ports to a `static_cast` (the ctor
//    invariant -- `member` is always an `IParameterizedMember` -- guarantees the cast is
//    valid, so no runtime `dynamic_cast` is needed; the C# cast would throw
//    `InvalidCastException` on a wrong type but the ctor never accepts a non-parameterized
//    member). Through an `InvocationResolveResult*` the accessor yields
//    `const IParameterizedMember*`; through a `MemberResolveResult*` / `ResolveResult*`
//    the base `Member()` (non-virtual) still yields `const IMember*` -- the faithful C#
//    `new`-hiding semantics.
//  * The C# `virtual IList<ResolveResult> GetArgumentsForCall()` (the base returns
//    `Arguments`; a derived class rebuilds the list for `params` arrays) ports to a
//    `virtual` returning `std::vector<std::shared_ptr<ResolveResult>>` BY VALUE -- the
//    base returns a copy of `arguments_` (behaviorally identical to the C# reference for
//    read-only iteration), and a derived class can return a freshly-built list. Returning
//    by value (not by reference) is the faithful choice because a derived override builds
//    a NEW list each call, which a reference return cannot express.
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` concats the base's
//    target result with `Arguments` then `InitializerStatements`. The base
//    `MemberResolveResult::GetChildResults()` returns `std::vector<const ResolveResult*>`
//    (non-owning pointers, the D424/D437 convention); the override appends each argument
//    and initializer statement via `.get()` (the shared_ptr keeps the `ResolveResult`
//    alive).
//  * `ToString` is inherited (the D425/D426/D427 inherit-`ToString`-override-`ClassName`
//    convention): overriding `ClassName()` to "InvocationResolveResult" makes the
//    inherited `MemberResolveResult::ToString` yield "[InvocationResolveResult <member>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C#
//    inherited `MemberwiseClone` preserves the runtime type, but a non-overriding C++
//    base clone (`make_unique<MemberResolveResult>(*this)`) would SLICE an
//    `InvocationResolveResult` to its base, so the override does
//    `make_unique<InvocationResolveResult>(*this)` (the default copy ctor shares the
//    `arguments_` / `initializerStatements_` shared_ptr vectors faithfully mirroring the
//    C# reference-copy).
class InvocationResolveResult : public MemberResolveResult {
public:
    // The C# ctor: delegates to `MemberResolveResult` ctor 1 (the common ctor), then
    // stores `arguments ?? EmptyList<ResolveResult>.Instance` and
    // `initializerStatements ?? EmptyList<ResolveResult>.Instance`. The C++ port defaults
    // the two lists to empty vectors (the `?? EmptyList`-to-empty-vector convention) and
    // forwards `targetResult` / `member` / `returnTypeOverride` to the base common ctor
    // (which computes `isVirtualCall`/`isConstant`/`constantValue`). The `member` is
    // `const IParameterizedMember*` which binds to the base's `const IMember*` parameter
    // via the derived-to-base pointer conversion.
    InvocationResolveResult(std::shared_ptr<ResolveResult> targetResult,
                            const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member,
                            std::vector<std::shared_ptr<ResolveResult>> arguments = {},
                            std::vector<std::shared_ptr<ResolveResult>> initializerStatements = {},
                            ILSpy::Decompiler::TypeSystem::ITypePtr returnTypeOverride = nullptr)
        : MemberResolveResult(std::move(targetResult), member, std::move(returnTypeOverride)),
          arguments_(std::move(arguments)),
          initializerStatements_(std::move(initializerStatements)) {}

    // The C# `new IParameterizedMember Member { get { return
    // (IParameterizedMember)base.Member; } }` -- a hiding accessor downcasting the
    // inherited `IMember Member` to the `IParameterizedMember` the ctor was built with.
    // Non-virtual (the base `Member()` is non-virtual), so it hides the base through an
    // `InvocationResolveResult*` while the base still dispatches through a
    // `MemberResolveResult*` / `ResolveResult*` (the faithful C# `new` semantics). The
    // `static_cast` is safe under the ctor invariant (`member` is always an
    // `IParameterizedMember`).
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Member() const noexcept {
        return static_cast<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>(
            MemberResolveResult::Member());
    }

    // The C# `readonly IList<ResolveResult> Arguments` -- the call's arguments. Returns a
    // const reference to the stored shared_ptr vector (the faithful exposure of a readonly
    // reference-type field; the shared_ptr elements keep the `ResolveResult`s alive and
    // are shared with the caller's list).
    const std::vector<std::shared_ptr<ResolveResult>>& Arguments() const noexcept {
        return arguments_;
    }

    // The C# `readonly IList<ResolveResult> InitializerStatements` -- the object/collection
    // initializer statements applied to the call's result. Same convention as `Arguments`.
    const std::vector<std::shared_ptr<ResolveResult>>& InitializerStatements() const noexcept {
        return initializerStatements_;
    }

    // The C# `virtual IList<ResolveResult> GetArgumentsForCall()` -- returns `Arguments`
    // (a derived class overrides it to rebuild the list for `params`-array calls, e.g.
    // wrapping the trailing arguments in an `ArrayCreateResolveResult`). Returns by value
    // so a derived override can build a new list each call; the base returns a copy of
    // `arguments_`.
    virtual std::vector<std::shared_ptr<ResolveResult>> GetArgumentsForCall() const {
        return arguments_;
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- the base's target
    // result followed by `Arguments` then `InitializerStatements`. The base returns the
    // non-owning target snapshot (`std::vector<const ResolveResult*>`); the override
    // appends each argument and initializer statement via `.get()` (the shared_ptr keeps
    // the `ResolveResult` alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        auto children = MemberResolveResult::GetChildResults();
        for (const auto& a : arguments_)
            children.push_back(a.get());
        for (const auto& i : initializerStatements_)
            children.push_back(i.get());
        return children;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `arguments_` / `initializerStatements_` shared_ptr
    // vectors are shared, the base members are copied). The C++ override reproduces this
    // via the default copy ctor, avoiding the C++-only slicing a non-overriding base clone
    // would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<InvocationResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `MemberResolveResult::ToString`.
    std::string ClassName() const override { return "InvocationResolveResult"; }

private:
    std::vector<std::shared_ptr<ResolveResult>> arguments_;
    std::vector<std::shared_ptr<ResolveResult>> initializerStatements_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_INVOCATIONRESOLVERESULT_HPP
