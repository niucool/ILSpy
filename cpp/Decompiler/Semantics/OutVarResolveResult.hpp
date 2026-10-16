// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation, rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/OutVarResolveResult.cs -- the
// `ResolveResult` (D424) for an implicitly-typed `out var` argument. It is
// special-cased in overload resolution to be compatible with any out-parameter
// (the resolver matches it against every `out` parameter regardless of type), and
// carries the `OriginalVariableType` -- the type of the variable originally used in
// IL, which the resolver falls back to when `out var` cannot be used. This is the
// sixteenth `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker). The C# source declares a 1-arg ctor and one field:
//   * `public readonly IType OriginalVariableType` -- the original variable type
//     (nullable in C#: the ctor does NOT guard it with `ArgumentNullException`).
//   * `OutVarResolveResult(IType originalVariableType) : base(SpecialType.NoType)
//     { OriginalVariableType = originalVariableType; }` -- forwards
//     `SpecialType.NoType` (the `TypeKind::None` null object, no type at all) to
//     the `ResolveResult` base ctor, then stores the original variable type in the
//     readonly field.
//
// The C# `SpecialType.NoType` dependency (the `TypeKind::None` special-type null
// object) was the prerequisite the D427/D432 next-in-order analysis flagged: the
// D433 `NamespaceResolveResult` port added the `NoType()` convenience to
// `IType.hpp`, unblocking the `SpecialType.NoType`-dependent subclasses
// (`NamespaceResolveResult` D433, `ThrowResolveResult` D435, and this leaf). This
// leaf is the third consumer of the D433 `NoType()` prerequisite.
//
// The C# surface is a 1-arg forwarding ctor with NO behavioral overrides (no
// `IsError`, `IsCompileTimeConstant`, `ConstantValue`, `GetChildResults`, or
// `ToString` override) -- the subclass relies entirely on the inherited
// `ResolveResult` base defaults. The C++ port additionally overrides `ClassName()`
// (to faithfully mirror the C# polymorphic `GetType().Name` in the inherited
// `ToString`, the D424 `ClassName()`-for-polymorphic-class-name convention) and
// `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention every subclass port
// carries). This is structurally the D435 `ThrowResolveResult` NoType-forwarding
// precedent plus one additional nullable `IType` field (`OriginalVariableType`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_OUTVARRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_OUTVARRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `class OutVarResolveResult : ResolveResult` (NO access modifier -- the C#
// default is `internal`, assembly-private; NOT `sealed` -- no C# subclass derives
// from it but it is unsealed) ports to a PUBLIC C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is the 1-arg forwarding ctor plus the
// `OriginalVariableType` readonly field; the C++ port additionally overrides
// `ClassName()` and `ShallowClone()` to faithfully reproduce the C# `GetType().Name`
// (polymorphic class name in the inherited `ToString`) and `MemberwiseClone`
// (runtime-type-preserving shallow clone) -- the documented `ResolveResult`
// conventions. A non-overriding C++ base clone would slice the subclass to a
// `ResolveResult` base (diverging from C# `MemberwiseClone`), and the inherited
// `ToString` would report the base class name "ResolveResult" (diverging from the
// C# `GetType().Name` which yields "OutVarResolveResult").
class OutVarResolveResult : public ResolveResult {
public:
    // The C# `OutVarResolveResult(IType originalVariableType) : base(SpecialType.NoType)
    // { OriginalVariableType = originalVariableType; }` -- forwards
    // `SpecialType.NoType` (the `TypeKind::None` null object, no type at all) to the
    // `ResolveResult` base ctor, then stores the original variable type. The C++ port
    // forwards `ILSpy::Decompiler::TypeSystem::NoType()` (the D433 additive `IType.hpp`
    // convenience) to the base and stores `originalVariableType` in the member-init list
    // (idiomatic C++; the base ctor runs first, then the member initializes, matching
    // the C# base-init-then-body-assign order). Distinct from `UnknownType()`
    // (`TypeKind::Unknown`, the error-type null object): an `out var` expression has NO
    // type at all (the resolver special-cases it to match any out-parameter), not an
    // unknown one. The C# ctor does NOT guard `originalVariableType` with
    // `ArgumentNullException` (the field may be null), so the C++ port does NOT `assert`
    // on it -- the faithful null-handling distinct from the D424 base ctor which DOES
    // assert on its `type` parameter. The `ITypePtr` (shared_ptr) member may be empty
    // (the C# `null`).
    explicit OutVarResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr originalVariableType)
        : ResolveResult(ILSpy::Decompiler::TypeSystem::NoType()),
          originalVariableType_(std::move(originalVariableType)) {}

    // The C# `public readonly IType OriginalVariableType` field (a public readonly
    // reference-type field, nullable in C#). The faithful C++ mirror is a public
    // accessor returning the owning `ITypePtr` handle by const reference (the
    // `ResolveResult::TypePtr()` D428 precedent for exposing an owning handle as a
    // non-rebindable view): an empty `shared_ptr` is the C# `null`, and the caller can
    // check `.get() != nullptr` and call `IType` methods on a non-null handle. The
    // field is NOT `assert`-guarded (the C# ctor allows null), so the accessor returns
    // the handle directly without a null check.
    const ILSpy::Decompiler::TypeSystem::ITypePtr& OriginalVariableType() const {
        return originalVariableType_;
    }

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name` which
    // is polymorphic and yields "OutVarResolveResult" for an `OutVarResolveResult`
    // instance. The C++ port reproduces this via the `ClassName()` override so the
    // inherited `ToString` reports the subclass name (not the base "ResolveResult" the
    // base `ClassName()` default would yield).
protected:
    std::string ClassName() const override { return "OutVarResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses `MemberwiseClone`
    // which preserves the runtime type, so a cloned `OutVarResolveResult` stays an
    // `OutVarResolveResult` (not sliced to the `ResolveResult` base). The C++ port
    // reproduces this by overriding `ShallowClone` to construct an
    // `OutVarResolveResult` copy (the default copy ctor shares the `type_`
    // `shared_ptr` -- both the original and the clone hold the same `NoType()` null
    // object -- AND shares the `originalVariableType_` `shared_ptr` -- both hold the
    // same original variable type, faithful to the C# reference-copy of both `IType`
    // fields).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<OutVarResolveResult>(*this);
    }

private:
    // The C# `public readonly IType OriginalVariableType` -- the original variable
    // type, nullable (the C# ctor allows null). The `ITypePtr` (shared_ptr) member is
    // the owning handle; an empty `shared_ptr` is the C# `null`.
    ILSpy::Decompiler::TypeSystem::ITypePtr originalVariableType_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_OUTVARRESOLVERESULT_HPP
