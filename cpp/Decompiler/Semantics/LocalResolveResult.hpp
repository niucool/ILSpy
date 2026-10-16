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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/LocalResolveResult.cs -- the resolved
// expression is a local variable or parameter. It is the twenty-first `Semantics`
// leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining
// blocker), after `ResolveResult` (D424) and the twenty concrete subclasses ported
// since. All deps are already ported: `ResolveResult` (D424), `IVariable` (D374, for
// `IsConst`/`GetConstantValue`/`Type`/`Name`), `IParameter` (D382, for the
// `ByReference`-unwrap `ReferenceKind` check and the `IsParameter` discrimination),
// `ByReferenceType`/`TypeKind`/`ReferenceKind` (D271). The C# surface is:
//   * `LocalResolveResult(IVariable variable) : base(UnpackTypeIfByRefParameter(variable))
//     { this.variable = variable; }` -- the forwarding ctor: the result type is the
//     variable's type, UNWRAPPED one level when the variable is a non-`None`-ref
//     `IParameter` of `ByReference` type (an `out`/`ref`/`in`/`ref readonly`
//     parameter's local resolve result exposes the element type, not the ref type).
//   * `IVariable Variable { get; }` -- the stored variable.
//   * `bool IsParameter { get => variable is IParameter; }` -- whether the variable
//     is a parameter.
//   * `override bool IsCompileTimeConstant { get => variable.IsConst; }` -- the
//     variable's const-ness (a `const local` is a compile-time constant).
//   * `override object ConstantValue { get => IsParameter ? null :
//     variable.GetConstantValue(); }` -- the boxed constant value for a local, or
//     null for a parameter (a parameter has no compile-time constant value even
//     when it has a default).
//   * `override string ToString() => "[LocalResolveResult {variable}]"`.
//
// KEY PORT CONVENTIONS:
//  * The C# `IVariable variable` (a non-null reference-type field the ctor guards
//    with `ArgumentNullException`) ports to a non-owning `const IVariable*` raw
//    pointer guarded with `assert` (the D424 `assert`-on-null convention, compiled
//    out with NDEBUG). The variable is owned by the type system / resolver (the
//    C# GC guarantee); the `LocalResolveResult` is a temporary resolution outcome
//    that does not outlive the variable, mirroring the D437 `MemberResolveResult`
//    non-owning `const IMember* member_`.
//  * The `UnpackTypeIfByRefParameter` static helper returns `ITypePtr` (the base
//    `ResolveResult` ctor takes `ITypePtr`). The `ByReference` branch returns the
//    `ByReferenceType`'s stored `ITypePtr` (via `Element()`); the default branch
//    returns `shared_from_this()` on the variable's type (the `IType` is
//    `shared_ptr`-owned throughout the port, D271/D406, so `shared_from_this` is
//    valid and shares ownership faithfully mirroring the C# GC reference -- the
//    D437 `ComputeType` default-branch precedent). The C# `variable as IParameter`
//    downcast ports to `dynamic_cast<const IParameter*>(variable)` (the D437
//    `member as IField` precedent); the C# `((ByReferenceType)type)` cast is
//    `static_cast<const ByReferenceType&>` (safe under the `Kind() == ByReference`
//    gate, the D437 `ComputeType` ByReference-branch precedent).
//  * The C# `ToString` uses `variable.ToString()` (the default `object.ToString`
//    yields the class name; the concrete variable implementations override it).
//    The C++ `IVariable` interface does NOT expose a `ToString()` virtual (the
//    D422 `MergedNamespace` non-virtual-`ToString` convention), so the faithful
//    port uses `variable_->Name()` (the closest meaningful representation the C#
//    `variable.ToString()` includes for a real variable) -- a documented
//    deviation, the D437 `MemberResolveResult` `member_->Name()` precedent.
//  * The C++ port additionally overrides `ClassName()` (to faithfully mirror the
//    C# polymorphic `GetType().Name` in the custom `ToString`) and `ShallowClone()`
//    (to faithfully mirror the C# `MemberwiseClone` runtime-type preservation,
//    avoiding the C++-only slicing a non-overriding base clone would perform --
//    the documented `ResolveResult` convention every subclass port carries).

#ifndef ILSPY_DECOMPILER_SEMANTICS_LOCALRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_LOCALRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"

#include <any>
#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class LocalResolveResult : ResolveResult` (NOT `sealed` -- no C#
// subclass derives from it but it is unsealed) ports to a PUBLIC C++ subclass (NOT
// `final`) of `ResolveResult`. The C# surface is the forwarding ctor plus the
// `Variable` field and the `IsParameter` / `IsCompileTimeConstant` / `ConstantValue`
// / `ToString` overrides; the C++ port additionally overrides `ClassName()` and
// `ShallowClone()` to faithfully reproduce the C# `GetType().Name` (polymorphic
// class name in the custom `ToString`) and `MemberwiseClone` (runtime-type-
// preserving shallow clone) -- the documented `ResolveResult` conventions. A
// non-overriding C++ base clone would slice the subclass to a `ResolveResult` base
// (diverging from C# `MemberwiseClone`).
class LocalResolveResult : public ResolveResult {
public:
    // The C# `LocalResolveResult(IVariable variable)
    // : base(UnpackTypeIfByRefParameter(variable)) { this.variable = variable; }`:
    // the forwarding ctor. The result type is the variable's type, unwrapped one
    // level when the variable is a non-`None`-ref `IParameter` of `ByReference` type.
    // The C# null-guard ports to an `assert` (the D424 convention). The
    // `UnpackTypeIfByRefParameter` static helper (declared below, in the private
    // section; mem-initializer expressions are complete-class contexts so a
    // later-declared member is visible) returns the `ITypePtr` the base ctor takes.
    explicit LocalResolveResult(const ILSpy::Decompiler::TypeSystem::IVariable* variable)
        : ResolveResult(UnpackTypeIfByRefParameter(*variable)),
          variable_(variable) {
        assert(variable != nullptr && "LocalResolveResult: variable must not be null");
    }

    // The C# `IVariable Variable { get; }` -- the stored variable. Returns a
    // non-owning raw pointer (the variable is owned by the type system / resolver,
    // not by this result); never null (the ctor asserts it).
    const ILSpy::Decompiler::TypeSystem::IVariable* Variable() const noexcept {
        return variable_;
    }

    // The C# `bool IsParameter { get => variable is IParameter; }` -- whether the
    // variable is a parameter. The C# `is` type-test ports to a `dynamic_cast`
    // null test (the D437 `member as IField` precedent).
    bool IsParameter() const {
        return dynamic_cast<const ILSpy::Decompiler::TypeSystem::IParameter*>(variable_) != nullptr;
    }

    // The C# `override bool IsCompileTimeConstant { get => variable.IsConst; }` --
    // the variable's const-ness (a `const local` is a compile-time constant). A
    // parameter is never a compile-time constant (a parameter's `IsConst` is
    // false in practice, but the override faithfully reads `variable.IsConst`
    // regardless, mirroring the C#).
    bool IsCompileTimeConstant() const override {
        return variable_->IsConst();
    }

    // The C# `override object ConstantValue { get => IsParameter ? null :
    // variable.GetConstantValue(); }` -- the boxed constant value for a local, or
    // null for a parameter (a parameter has no compile-time constant value even
    // when it has a default). An empty `std::any` is the C# `null` (the D424
    // convention); `GetConstantValue` returns `std::any` (the D374
    // `IVariable::GetConstantValue` convention).
    std::any ConstantValue() const override {
        if (IsParameter())
            return {};
        return variable_->GetConstantValue();
    }

    // The C# `override string ToString() =>
    // string.Format(CultureInfo.InvariantCulture, "[LocalResolveResult {0}]", variable)`.
    // The format is `"[LocalResolveResult <variable name>]"` where the variable
    // name is `variable_->Name()` (the C# uses `variable.ToString()` which the C++
    // `IVariable` interface does not expose; `Name()` is the closest meaningful
    // representation -- the D437 `MemberResolveResult` `member_->Name()` precedent).
    std::string ToString() const override {
        return "[" + ClassName() + " " + variable_->Name() + "]";
    }

    // The C# `ShallowClone` (inherited from `ResolveResult`) uses `MemberwiseClone`
    // which preserves the runtime type and shallow-copies the fields (the
    // `variable_` raw pointer is copied by value). The C++ override reproduces this
    // via the default copy ctor, avoiding the C++-only slicing a non-overriding
    // base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<LocalResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by `ToString`.
    std::string ClassName() const override { return "LocalResolveResult"; }

private:
    // The C# `static IType UnpackTypeIfByRefParameter(IVariable variable)`:
    // computes the `LocalResolveResult`'s type from the variable. When the
    // variable's type is `ByReference` AND the variable is a non-`None`-ref
    // `IParameter` (an `out`/`ref`/`in`/`ref readonly` parameter), the element type
    // is returned (unwrapping one ref level); otherwise the variable's type is
    // returned as-is. Returns `ITypePtr` (the base `ResolveResult` ctor takes
    // `ITypePtr`).
    //
    // The `ByReference` branch returns `static_cast<const ByReferenceType&>(type)
    // .Element()` (the stored `ITypePtr`, shared) -- safe under the
    // `Kind() == ByReference` gate (the D437 `ComputeType` ByReference-branch
    // precedent). The default branch returns `shared_from_this()` on the
    // variable's type (the `IType` is `shared_ptr`-owned throughout the port,
    // D271/D406) to share ownership faithfully mirroring the C# GC reference (the
    // D437 `ComputeType` default-branch precedent). The C# `variable as IParameter`
    // ports to `dynamic_cast<const IParameter*>(variable)`; the C#
    // `p.ReferenceKind != ReferenceKind.None` ports verbatim.
    static ILSpy::Decompiler::TypeSystem::ITypePtr UnpackTypeIfByRefParameter(
        const ILSpy::Decompiler::TypeSystem::IVariable& variable) {
        namespace TS = ILSpy::Decompiler::TypeSystem;
        const TS::IType& type = variable.Type();
        if (type.Kind() == TS::TypeKind::ByReference) {
            auto p = dynamic_cast<const TS::IParameter*>(&variable);
            if (p != nullptr && p->ReferenceKind() != TS::ReferenceKind::None)
                return static_cast<const TS::ByReferenceType&>(type).Element();
        }
        return const_cast<TS::IType&>(type).shared_from_this();
    }

    // The C# `readonly IVariable variable` -- the stored variable. A non-owning
    // raw pointer (the variable is owned by the type system / resolver; the C# GC
    // guarantee); never null (the ctor asserts it).
    const ILSpy::Decompiler::TypeSystem::IVariable* variable_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_LOCALRESOLVERESULT_HPP
