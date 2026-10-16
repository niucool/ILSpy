// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
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

// Port of ICSharpCode.Decompiler/Semantics/MemberResolveResult.cs -- the result of a
// member invocation, used for field/property/event access (also the base of
// `InvocationResolveResult`). This is the thirteenth `Semantics` leaf toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker), after
// `ResolveResult` (D424), `TypeResolveResult` (D425), `ThisResolveResult` (D426),
// `TypeOfResolveResult` (D427), `ByReferenceResolveResult` (D428), `SizeOfResolveResult`
// (D430), `TypeIsResolveResult` (D431), `ConstantResolveResult` (D432),
// `NamespaceResolveResult` (D433), `InitializedObjectResolveResult` (D434),
// `ThrowResolveResult` (D435), and `ErrorResolveResult` (D436). All deps are already
// ported: `ResolveResult` (D424), `IMember` (D387), `IField` (D391, for the
// `IsConst`/`GetConstantValue` field-constant check), `ThisResolveResult` (D426, for
// the `CausesNonVirtualInvocation` virtual-call discrimination), `IType` (D271,
// including `ByReferenceType`/`TypeKind`/`UnknownType()`), `SymbolKind` (D372), and
// `IVariable` (D374, for `IsConst`/`GetConstantValue` inherited by `IField`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_MEMBERRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_MEMBERRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class MemberResolveResult : ResolveResult` (NOT `sealed`:
// `InvocationResolveResult` derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is:
//   * Four ctors:
//     1. `(ResolveResult targetResult, IMember member, IType returnTypeOverride = null)`
//        -- the common ctor; computes `isVirtualCall` from `member.IsOverridable` and
//        whether `targetResult` is a `ThisResolveResult` with
//        `CausesNonVirtualInvocation`; computes `isConstant`/`constantValue` from the
//        IField `IsConst`/`GetConstantValue` check.
//     2. `(ResolveResult targetResult, IMember member, bool isVirtualCall,
//        IType returnTypeOverride = null)` -- the explicit-`isVirtualCall` ctor; still
//        computes `isConstant`/`constantValue` from the IField check.
//     3. `(ResolveResult targetResult, IMember member, IType returnType, bool
//        isConstant, object constantValue)` -- the explicit-constant ctor; `isVirtualCall`
//        defaults to `false` (the C# field default).
//     4. `(ResolveResult targetResult, IMember member, IType returnType, bool
//        isConstant, object constantValue, bool isVirtualCall)` -- the full ctor.
//   * `ResolveResult TargetResult` -- the stored target (nullable: some resolver
//     paths pass null, and `GetChildResults` checks for it).
//   * `IMember Member` -- the stored member (never null per the C# doc).
//   * `bool IsVirtualCall` -- whether this is a virtual call.
//   * `override bool IsCompileTimeConstant` -- `isConstant`.
//   * `override object ConstantValue` -- `constantValue` (may be null).
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- the `targetResult`
//     when non-null, else empty.
//   * `override string ToString()` -- `"[{GetType().Name} {member}]"`.
//
// KEY PORT CONVENTIONS:
//  * The C# `IMember member` (a non-null reference the ctor does NOT guard with
//    `ArgumentNullException`; the property doc says "never returns null") ports to a
//    non-owning `const IMember*` raw pointer (the D433 `NamespaceResolveResult`
//    `const INamespace*` precedent). The member is owned by the type system; the
//    `MemberResolveResult` is a temporary resolution outcome that does not outlive the
//    member (the C# GC guarantee). No null guard (the C# does not guard `member`).
//  * The C# `ResolveResult targetResult` (a reference type the ctor does NOT guard)
//    ports to a `std::shared_ptr<ResolveResult>` (the D428 `ByReferenceResolveResult`
//    `elementResult_` precedent). It MAY be null (empty `shared_ptr`): the C#
//    `GetChildResults` checks `if (targetResult != null)`, so the C++ port checks
//    `if (targetResult_)` (the `shared_ptr` `operator bool`).
//  * The C# `object constantValue` ports to `std::any` (the D432
//    `ConstantResolveResult` convention); an empty `any` is the C# `null`.
//  * The C# `IType returnTypeOverride` (nullable reference) ports to `ITypePtr`
//    (nullable `shared_ptr<IType>`); the C# `??` null-coalescing ports to the
//    `shared_ptr` `operator bool` ternary: `returnTypeOverride ? returnTypeOverride :
//    ComputeType(*member)`.
//  * The `ComputeType` static helper (below) returns `ITypePtr` (the base
//    `ResolveResult` ctor takes `ITypePtr`). The `SymbolKind.Constructor` branch
//    returns `member.DeclaringType()` (which IS `ITypePtr`) or `UnknownType()` when
//    the declaring type is null; the `TypeKind.ByReference` branch returns the
//    `ByReferenceType`'s stored `ITypePtr` (via `Element()`); the default branch
//    returns `shared_from_this()` on the `ReturnType` (the `IType` is
//    `shared_ptr`-owned throughout the port, D271/D406, so `shared_from_this` is
//    valid and shares ownership faithfully mirroring the C# GC reference).
//  * The C# `ToString` uses `member.ToString()` (the default `object.ToString` yields
//    the class name; the concrete `IMember` implementations override it to the
//    member's full name). The C++ `IMember` interface does NOT expose a `ToString()`
//    virtual (the D422 `MergedNamespace` non-virtual-`ToString` convention), so the
//    faithful port uses `member_->Name()` (the closest meaningful representation the
//    C# `member.ToString()` includes for a real member) -- a documented deviation.
class MemberResolveResult : public ResolveResult {
public:
    // Ctor 1: the common ctor. Computes `isVirtualCall` from `member->IsOverridable()`
    // and whether `targetResult` is a `ThisResolveResult` with
    // `CausesNonVirtualInvocation`; computes `isConstant`/`constantValue` from the
    // IField `IsConst`/`GetConstantValue` check. The base type is `returnTypeOverride`
    // when non-null, else `ComputeType(*member)`.
    MemberResolveResult(std::shared_ptr<ResolveResult> targetResult,
                        const ILSpy::Decompiler::TypeSystem::IMember* member,
                        ILSpy::Decompiler::TypeSystem::ITypePtr returnTypeOverride = nullptr)
        : ResolveResult(returnTypeOverride ? returnTypeOverride : ComputeType(*member)),
          targetResult_(std::move(targetResult)),
          member_(member) {
        InitVirtualCallFromTarget(member);
        InitConstantFromField(member);
    }

    // Ctor 2: the explicit-`isVirtualCall` ctor. Takes `isVirtualCall` directly
    // (no auto-computation from the target); still computes `isConstant`/`constantValue`
    // from the IField check. The base type is `returnTypeOverride` when non-null, else
    // `ComputeType(*member)`.
    MemberResolveResult(std::shared_ptr<ResolveResult> targetResult,
                        const ILSpy::Decompiler::TypeSystem::IMember* member,
                        bool isVirtualCall,
                        ILSpy::Decompiler::TypeSystem::ITypePtr returnTypeOverride = nullptr)
        : ResolveResult(returnTypeOverride ? returnTypeOverride : ComputeType(*member)),
          targetResult_(std::move(targetResult)),
          member_(member),
          isVirtualCall_(isVirtualCall) {
        InitConstantFromField(member);
    }

    // Ctor 3: the explicit-constant ctor. Takes `returnType` (non-null, the base
    // `ResolveResult` ctor `assert`-guards it), `isConstant`, and `constantValue`
    // directly. `isVirtualCall` defaults to `false` (the C# field default).
    MemberResolveResult(std::shared_ptr<ResolveResult> targetResult,
                        const ILSpy::Decompiler::TypeSystem::IMember* member,
                        ILSpy::Decompiler::TypeSystem::ITypePtr returnType,
                        bool isConstant,
                        std::any constantValue)
        : ResolveResult(std::move(returnType)),
          targetResult_(std::move(targetResult)),
          member_(member),
          isConstant_(isConstant),
          constantValue_(std::move(constantValue)) {}

    // Ctor 4: the full ctor. Takes everything explicitly.
    MemberResolveResult(std::shared_ptr<ResolveResult> targetResult,
                        const ILSpy::Decompiler::TypeSystem::IMember* member,
                        ILSpy::Decompiler::TypeSystem::ITypePtr returnType,
                        bool isConstant,
                        std::any constantValue,
                        bool isVirtualCall)
        : ResolveResult(std::move(returnType)),
          targetResult_(std::move(targetResult)),
          member_(member),
          isConstant_(isConstant),
          constantValue_(std::move(constantValue)),
          isVirtualCall_(isVirtualCall) {}

    // The C# `ResolveResult TargetResult` -- the stored target (nullable: some
    // resolver paths pass null). Returns a non-owning raw pointer; `nullptr` when
    // the result was constructed with a null target.
    ResolveResult* TargetResult() const noexcept { return targetResult_.get(); }

    // The shared handle behind `TargetResult()` -- the port's shared-ownership
    // accessor for the call sites that must build a new result sharing this
    // target (the `new MemberResolveResult(mrr.TargetResult, ...)` pattern; the
    // C# keeps the reference through the GC, the port through the shared_ptr).
    const std::shared_ptr<ResolveResult>& TargetResultHandle() const noexcept {
        return targetResult_;
    }

    // The C# `IMember Member` -- the stored member (the C# doc says "never returns
    // null"). Returns a non-owning raw pointer (the member is owned by the type
    // system, not by this result).
    const ILSpy::Decompiler::TypeSystem::IMember* Member() const noexcept { return member_; }

    // The C# `bool IsVirtualCall` -- whether this is a virtual call.
    bool IsVirtualCall() const noexcept { return isVirtualCall_; }

    // The C# `override bool IsCompileTimeConstant` -- `isConstant` (true when the
    // member is a const field or the caller explicitly provided a constant value).
    bool IsCompileTimeConstant() const override { return isConstant_; }

    // The C# `override object ConstantValue` -- `constantValue` (an empty `any` is
    // the C# `null`; a non-const field's value is null/empty by default). Returns a
    // copy of the stored `std::any` (the D432 `ConstantResolveResult` convention).
    std::any ConstantValue() const override { return constantValue_; }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()`: returns the
    // `targetResult` (a one-element snapshot) when non-null, else empty. The snapshot
    // is a non-owning pointer (the `shared_ptr` keeps the `ResolveResult` alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        if (targetResult_)
            return { targetResult_.get() };
        return {};
    }

    // The C# `override string ToString() =>
    // string.Format(CultureInfo.InvariantCulture, "[{0} {1}]", GetType().Name, member)`.
    // The format is `"[MemberResolveResult <member name>]"` where the member name is
    // `member_->Name()` (the C# uses `member.ToString()` which the C++ `IMember`
    // interface does not expose; `Name()` is the closest meaningful representation).
    std::string ToString() const override {
        return "[" + ClassName() + " " + member_->Name() + "]";
    }

    // The C# `ShallowClone` (inherited from `ResolveResult`) uses `MemberwiseClone`
    // which preserves the runtime type and shallow-copies the fields (the `targetResult_`
    // `shared_ptr` is shared, the `member_` raw pointer is copied, the `isConstant_`/
    // `isVirtualCall_` bools are copied, and the `constantValue_` `std::any` is
    // value-copied). The C++ override reproduces this via the default copy ctor,
    // avoiding the C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<MemberResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by `ToString`.
    std::string ClassName() const override { return "MemberResolveResult"; }

private:
    // The C# `static IType ComputeType(IMember member)`: computes the
    // `MemberResolveResult`'s type from the member when no `returnTypeOverride` is
    // given. The `SymbolKind.Constructor` branch returns the declaring type (or
    // `UnknownType` when null); the `SymbolKind.Field` branch falls through (the
    // commented-out `IsFixed` -> `PointerType` path is NOT ported); the
    // `TypeKind.ByReference` return type unwraps to the element type; the default
    // returns the member's return type. Returns `ITypePtr` (the base `ResolveResult`
    // ctor takes `ITypePtr`).
    //
    // The `DeclaringType()` IS `ITypePtr` (D381 `IEntity`), so the Constructor branch
    // returns it directly (or `UnknownType()` for null). The `ByReference` branch
    // returns `ByReferenceType::Element()` (the stored `ITypePtr`, shared). The
    // default branch calls `shared_from_this()` on the `ReturnType` (the `IType` is
    // `shared_ptr`-owned throughout the port, D271/D406) to share ownership faithfully
    // mirroring the C# GC reference.
    static ILSpy::Decompiler::TypeSystem::ITypePtr ComputeType(
        const ILSpy::Decompiler::TypeSystem::IMember& member) {
        namespace TS = ILSpy::Decompiler::TypeSystem;
        switch (member.SymbolKind()) {
            case TS::SymbolKind::Constructor:
                if (auto dt = member.DeclaringType())
                    return dt;
                return TS::UnknownType();
            case TS::SymbolKind::Field:
                break;
            default:
                break;
        }
        const TS::IType& returnType = member.ReturnType();
        if (returnType.Kind() == TS::TypeKind::ByReference)
            return static_cast<const TS::ByReferenceType&>(returnType).Element();
        return const_cast<TS::IType&>(returnType).shared_from_this();
    }

    // The C# ctor 1 body: `var thisRR = targetResult as ThisResolveResult;
    // this.isVirtualCall = member.IsOverridable && !(thisRR != null &&
    // thisRR.CausesNonVirtualInvocation)`. The `as` ports to `dynamic_cast`; the
    // `&&` and `!` are the faithful short-circuiting conjunction and negation.
    void InitVirtualCallFromTarget(const ILSpy::Decompiler::TypeSystem::IMember* member) {
        auto thisRR = dynamic_cast<ThisResolveResult*>(targetResult_.get());
        isVirtualCall_ = member->IsOverridable() &&
                         !(thisRR != nullptr && thisRR->CausesNonVirtualInvocation());
    }

    // The C# ctor 1/2 body: `IField field = member as IField; if (field != null)
    // { isConstant = field.IsConst; if (isConstant) constantValue =
    // field.GetConstantValue(); }`. The `as` ports to `dynamic_cast`; the IField
    // `IsConst`/`GetConstantValue` are inherited from `IVariable` (D374).
    void InitConstantFromField(const ILSpy::Decompiler::TypeSystem::IMember* member) {
        if (auto field = dynamic_cast<const ILSpy::Decompiler::TypeSystem::IField*>(member)) {
            isConstant_ = field->IsConst();
            if (isConstant_)
                constantValue_ = field->GetConstantValue();
        }
    }

    std::shared_ptr<ResolveResult> targetResult_;
    const ILSpy::Decompiler::TypeSystem::IMember* member_;
    bool isConstant_ = false;
    std::any constantValue_;
    bool isVirtualCall_ = false;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_MEMBERRESOLVERESULT_HPP
