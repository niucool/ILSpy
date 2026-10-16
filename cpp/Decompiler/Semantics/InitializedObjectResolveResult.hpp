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
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/InitializedObjectResolveResult.cs --
// the `ResolveResult` (D424) that refers to the object currently being
// initialized. It is used within `InvocationResolveResult.InitializerStatements`
// (e.g. the `this`/`base` receiver of a constructor `base(...)` / `this(...)`
// call chain, or the object being field-initialized). This is the tenth
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker). The C# source declares only the forwarding
// ctor:
//   * `InitializedObjectResolveResult(IType type) : base(type)` -- forwards the
//     initialized object's type to the `ResolveResult` base ctor.
//
// The C# surface is a forwarding ctor with NO behavioral overrides (no `IsError`,
// `IsCompileTimeConstant`, `ConstantValue`, `GetChildResults`, or `ToString`
// override) -- the subclass relies entirely on the inherited `ResolveResult` base
// defaults. The C++ port additionally overrides `ClassName()` (to faithfully mirror
// the C# polymorphic `GetType().Name` in the inherited `ToString`, the D424
// `ClassName()`-for-polymorphic-class-name convention) and `ShallowClone()` (to
// faithfully mirror the C# `MemberwiseClone` runtime-type preservation, avoiding
// the C++-only slicing a non-overriding base clone would perform -- the documented
// `ResolveResult` convention every subclass port carries). This is structurally
// the simplest possible `ResolveResult` subclass: a forwarding ctor plus the two
// `ClassName()`/`ShallowClone()` convention overrides, with no own state beyond the
// base `type` field.

#ifndef ILSPY_DECOMPILER_SEMANTICS_INITIALIZEDOBJECTRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_INITIALIZEDOBJECTRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class InitializedObjectResolveResult : ResolveResult` (NOT
// `sealed` -- no C# subclass derives from it but it is unsealed) ports to a C++
// subclass (NOT `final`) of `ResolveResult`. The C# surface is just the forwarding
// ctor; the C++ port additionally overrides `ClassName()` and `ShallowClone()` to
// faithfully reproduce the C# `GetType().Name` (polymorphic class name in the
// inherited `ToString`) and `MemberwiseClone` (runtime-type-preserving shallow
// clone) -- the documented `ResolveResult` conventions. A non-overriding C++ base
// clone would slice the subclass to a `ResolveResult` base (diverging from C#
// `MemberwiseClone`), and the inherited `ToString` would report the base class name
// "ResolveResult" (diverging from the C# `GetType().Name` which yields
// "InitializedObjectResolveResult").
class InitializedObjectResolveResult : public ResolveResult {
public:
    // The C# `InitializedObjectResolveResult(IType type) : base(type)` -- forwards
    // the initialized object's type to the `ResolveResult` base ctor (which
    // `assert`-guards the non-null `IType` per the D424 `ArgumentNullException`-to-
    // `assert` convention). The C# does NOT re-state the type (no own field); the
    // inherited base `type` field and `Type()` accessor cover it.
    explicit InitializedObjectResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : ResolveResult(std::move(type)) {}

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "InitializedObjectResolveResult" for an
    // `InitializedObjectResolveResult` instance. The C++ port reproduces this via
    // the `ClassName()` override so the inherited `ToString` reports the subclass
    // name (not the base "ResolveResult" the base `ClassName()` default would
    // yield).
protected:
    std::string ClassName() const override { return "InitializedObjectResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `InitializedObjectResolveResult` stays an `InitializedObjectResolveResult`
    // (not sliced to the `ResolveResult` base). The C++ port reproduces this by
    // overriding `ShallowClone` to construct an `InitializedObjectResolveResult`
    // copy (the default copy ctor shares the `type_` `shared_ptr`, faithful to the
    // C# reference-copy).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<InitializedObjectResolveResult>(*this);
    }
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_INITIALIZEDOBJECTRESOLVERESULT_HPP
