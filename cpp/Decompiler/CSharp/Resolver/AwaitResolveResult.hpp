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

// Port of ICSharpCode.Decompiler/CSharp/Resolver/AwaitResolveResult.cs -- the result of an
// `await` expression. This is the sixth `cpp/Decompiler/CSharp/Resolver/` leaf toward the
// `CSharpResolver` leaf deps (the long-pole remaining blocker of `TypeSystemAstBuilder` /
// `CSharpAmbience`), after the D467 twin Alias `ResolveResult` subclasses, the D468 twin
// enum leaves (`NameLookupMode` + `OverloadResolutionErrors`), and the D469
// `DynamicMemberResolveResult`. All deps are already ported: `ResolveResult` (D424, the
// base), `IType` (D271, for the `resultType` forwarded to the base and the non-null
// `AwaiterType`), `IProperty` (D390, for the nullable `IsCompletedProperty`), and `IMethod`
// (D389, for the nullable `OnCompletedMethod`/`GetResultMethod`). The C# surface is:
//   * `AwaitResolveResult(IType resultType, ResolveResult getAwaiterInvocation,
//      IType awaiterType, IProperty isCompletedProperty, IMethod onCompletedMethod,
//      IMethod getResultMethod) : base(resultType)` -- forwards `resultType` to the
//     `ResolveResult` base; `throw`s `ArgumentNullException` on null `awaiterType` or
//     `getAwaiterInvocation`; stores the five fields.
//   * `readonly ResolveResult GetAwaiterInvocation` -- the `GetAwaiter()` call (an
//     `InvocationResolveResult` or `DynamicInvocationResolveResult`); non-null (guarded).
//   * `readonly IType AwaiterType` -- the awaiter type; non-null (guarded) but may be
//     `UnknownType`.
//   * `readonly IProperty IsCompletedProperty` -- the `IsCompleted` property on the
//     awaiter; nullable (not found / dynamic).
//   * `readonly IMethod OnCompletedMethod` -- the `OnCompleted`/`UnsafeOnCompleted`
//     method on the awaiter; nullable (not found / dynamic).
//   * `readonly IMethod GetResultMethod` -- the `GetResult` method on the awaiter;
//     nullable (not found / dynamic).
//   * `override bool IsError` =>
//     `GetAwaiterInvocation.IsError || (AwaiterType.Kind != TypeKind.Dynamic &&
//      (IsCompletedProperty == null || OnCompletedMethod == null || GetResultMethod == null))`.
//   * `override IEnumerable<ResolveResult> GetChildResults()` => `new[] { GetAwaiterInvocation }`.
//
// KEY PORT CONVENTIONS:
//  * The C# `IType resultType` (forwarded to the `ResolveResult` base, which the base
//    ctor `throw`s `ArgumentNullException` on for null) ports to an `ITypePtr`
//    (`shared_ptr<IType>`) forwarded to the `ResolveResult(ITypePtr)` base, which
//    `assert`-guards it non-null (the D424 base-ctor convention).
//  * The C# `ResolveResult getAwaiterInvocation` (a non-null reference the ctor
//    `throw`s `ArgumentNullException` on for null) ports to a
//    `std::shared_ptr<ResolveResult>` (the D428 `ByReferenceResolveResult`
//    `elementResult_` / D445 `ForEachResolveResult` `getEnumeratorCall_` precedent:
//    shared ownership so `ShallowClone`'s default copy ctor shares it). The `assert`
//    ports the C# `ArgumentNullException` guard (the D424/D439 assert-then-move
//    convention).
//  * The C# `IType awaiterType` (a non-null reference the ctor `throw`s
//    `ArgumentNullException` on for null) ports to an `ITypePtr` (`shared_ptr<IType>`)
//    member guarded with `assert` (the D427 `SizeOfResolveResult` / D445
//    `ForEachResolveResult` ITypePtr-asserted precedent); the awaiter type may be
//    `UnknownType()` but never a null `shared_ptr`.
//  * The C# `IProperty isCompletedProperty` / `IMethod onCompletedMethod` /
//    `IMethod getResultMethod` (three NULLABLE reference-type fields the ctor does NOT
//    guard -- they may be `null` when the awaiter type or the member was not found, or
//    when awaiting a dynamic expression) port to non-owning `const IProperty*` /
//    `const IMethod*` raw pointers defaulting to `nullptr` (the D445
//    `ForEachResolveResult` nullable `CurrentProperty`/`MoveNextMethod` precedent; the
//    D437 `MemberResolveResult` non-owning-pointer convention). The members are owned by
//    the type system (the C# GC guarantee); the `AwaitResolveResult` is a temporary
//    resolution outcome that does not outlive them. `IProperty` and `IMethod` are
//    forward-declared (not included) in the header -- pointer members and
//    pointer-returning accessors to incomplete types need only forward declarations (the
//    D445 IProperty/IMethod forward-declaration precedent), keeping the include graph
//    minimal.
//  * The C# `override bool IsError` is the load-bearing crux: the await is an error when
//    the `GetAwaiterInvocation` is itself an error OR the awaiter type is NOT dynamic
//    (the `TypeKind.Dynamic` short-circuit -- a dynamic await skips the member-presence
//    check) AND any of the three awaiter-pattern members (`IsCompletedProperty`,
//    `OnCompletedMethod`, `GetResultMethod`) is null (not found). The C++ port ports it
//    verbatim: `getAwaiterInvocation_->IsError() || (awaiterType_->Kind() !=
//    TypeKind::Dynamic && (!isCompletedProperty_ || !onCompletedMethod_ ||
//    !getResultMethod_))`. The `GetAwaiterInvocation.IsError` virtual dispatches through
//    the held `ResolveResult` (the `shared_ptr` keeps it alive); the `AwaiterType.Kind`
//    is the `IType::Kind()` virtual; the `== null` ports to the raw-pointer `!`
//    (`operator bool`).
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` returns
//    `new[] { GetAwaiterInvocation }` -- a one-element snapshot. The C++ port returns
//    `std::vector<const ResolveResult*>` (non-owning pointers, the D424 convention) with
//    the single element `getAwaiterInvocation_.get()` (the `shared_ptr` keeps the
//    `ResolveResult` alive).
//  * `ToString` is inherited (the D424/D425/D427 inherit-`ToString`-override-`ClassName`
//    convention): the C# does NOT override `ToString`, so overriding `ClassName()` to
//    "AwaitResolveResult" makes the inherited `ResolveResult::ToString` yield
//    "[AwaitResolveResult <resultType ReflectionName>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention):
//    `make_unique<AwaitResolveResult>(*this)` (the default copy ctor shares the
//    `getAwaiterInvocation_` shared_ptr, shares the `awaiterType_` ITypePtr, copies the
//    three raw pointers, and shares the base `type_` shared_ptr, faithfully mirroring the
//    C# `MemberwiseClone` reference-copy).
//  * The class is NOT `final` (the C# is unsealed -- no C# subclass derives but it is
//    unsealed), pinned by `static_assert(!std::is_final_v)`.

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_AWAITRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_AWAITRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
class IProperty;
class IMethod;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public class AwaitResolveResult : ResolveResult` (NOT `sealed`) ports to a C++
// subclass (NOT `final`) of `Semantics::ResolveResult`. The ctor forwards `resultType` to
// the base (which `assert`-guards it non-null), then `assert`s `getAwaiterInvocation` and
// `awaiterType` non-null (the C# `ArgumentNullException` guards) before storing all five
// fields. The three awaiter-pattern members (`isCompletedProperty`/`onCompletedMethod`/
// `getResultMethod`) are nullable (no assert -- the C# does not guard them).
class AwaitResolveResult : public ILSpy::Decompiler::Semantics::ResolveResult {
public:
    AwaitResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr resultType,
                       std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> getAwaiterInvocation,
                       ILSpy::Decompiler::TypeSystem::ITypePtr awaiterType,
                       const ILSpy::Decompiler::TypeSystem::IProperty* isCompletedProperty,
                       const ILSpy::Decompiler::TypeSystem::IMethod* onCompletedMethod,
                       const ILSpy::Decompiler::TypeSystem::IMethod* getResultMethod)
        : ResolveResult(std::move(resultType)) {
        assert(getAwaiterInvocation && "AwaitResolveResult: getAwaiterInvocation must not be null");
        assert(awaiterType && "AwaitResolveResult: awaiterType must not be null");
        getAwaiterInvocation_ = std::move(getAwaiterInvocation);
        awaiterType_ = std::move(awaiterType);
        isCompletedProperty_ = isCompletedProperty;
        onCompletedMethod_ = onCompletedMethod;
        getResultMethod_ = getResultMethod;
    }

    // The C# `ResolveResult GetAwaiterInvocation` -- the `GetAwaiter()` call. Returns a
    // non-owning raw pointer (`getAwaiterInvocation_.get()`); never null (the ctor
    // `assert`s it).
    ILSpy::Decompiler::Semantics::ResolveResult* GetAwaiterInvocation() const noexcept {
        return getAwaiterInvocation_.get();
    }

    // The C# `IType AwaiterType` -- the awaiter type (may be `UnknownType`, never null).
    // Returns a non-null reference (the held `shared_ptr` keeps the `IType` alive).
    const ILSpy::Decompiler::TypeSystem::IType& AwaiterType() const { return *awaiterType_; }

    // The C# `IProperty IsCompletedProperty` -- the `IsCompleted` property on the
    // awaiter, or null if not found / dynamic. Returns a nullable non-owning raw pointer.
    const ILSpy::Decompiler::TypeSystem::IProperty* IsCompletedProperty() const noexcept {
        return isCompletedProperty_;
    }

    // The C# `IMethod OnCompletedMethod` -- the `OnCompleted`/`UnsafeOnCompleted` method
    // on the awaiter, or null if not found / dynamic. Returns a nullable non-owning raw
    // pointer.
    const ILSpy::Decompiler::TypeSystem::IMethod* OnCompletedMethod() const noexcept {
        return onCompletedMethod_;
    }

    // The C# `IMethod GetResultMethod` -- the `GetResult` method on the awaiter, or null
    // if not found / dynamic. Returns a nullable non-owning raw pointer.
    const ILSpy::Decompiler::TypeSystem::IMethod* GetResultMethod() const noexcept {
        return getResultMethod_;
    }

    // The C# `override bool IsError` =>
    // `GetAwaiterInvocation.IsError || (AwaiterType.Kind != TypeKind.Dynamic &&
    //  (IsCompletedProperty == null || OnCompletedMethod == null || GetResultMethod == null))`.
    // The await is an error when the `GetAwaiter()` call is itself an error, OR the awaiter
    // type is NOT dynamic (a dynamic await skips the member-presence check) AND any of the
    // three awaiter-pattern members is null (not found). The `GetAwaiterInvocation.IsError`
    // virtual-dispatches through the held `ResolveResult`; `AwaiterType.Kind()` is the
    // `IType::Kind()` virtual; the C# `== null` ports to the raw-pointer `!`.
    bool IsError() const override {
        return getAwaiterInvocation_->IsError() ||
               (awaiterType_->Kind() != ILSpy::Decompiler::TypeSystem::TypeKind::Dynamic &&
                (!isCompletedProperty_ || !onCompletedMethod_ || !getResultMethod_));
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- returns
    // `new[] { GetAwaiterInvocation }`, a one-element snapshot. The snapshot is a
    // non-owning pointer (the `shared_ptr` keeps the `ResolveResult` alive).
    std::vector<const ILSpy::Decompiler::Semantics::ResolveResult*> GetChildResults() const override
    {
        return { getAwaiterInvocation_.get() };
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `getAwaiterInvocation_` shared_ptr is shared, the
    // `awaiterType_` ITypePtr is shared, the three raw pointers are copied, the base
    // `type_` shared_ptr is shared). The C++ override reproduces this via the default
    // copy ctor, avoiding the C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<AwaitResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString` (the C# does NOT override `ToString`).
    std::string ClassName() const override { return "AwaitResolveResult"; }

private:
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> getAwaiterInvocation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr awaiterType_;
    const ILSpy::Decompiler::TypeSystem::IProperty* isCompletedProperty_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IMethod* onCompletedMethod_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IMethod* getResultMethod_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_AWAITRESOLVERESULT_HPP
