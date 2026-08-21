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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/ThisResolveResult.cs -- the resolved
// expression is the 'this' reference (also used for the 'base' reference). It is
// the third `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker), after `ResolveResult` (D424) and
// `TypeResolveResult` (D425). The C# source declares only the forwarding ctor
// (with a defaulted `causesNonVirtualInvocation` bool) and the
// `CausesNonVirtualInvocation` property getter; it does NOT override any of the
// `ResolveResult` virtuals. The C++ port additionally overrides `ClassName()` (to
// faithfully mirror the C# polymorphic `GetType().Name` in the inherited
// `ToString`, the D424 `ClassName()`-for-polymorphic-class-name convention) and
// `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention every subclass port
// carries).

#ifndef ILSPY_DECOMPILER_SEMANTICS_THISRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_THISRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ThisResolveResult : ResolveResult` (NOT `sealed`, and no
// C# subclass derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is:
//   * `ThisResolveResult(IType type, bool causesNonVirtualInvocation = false)
//     : base(type)` -- the forwarding ctor with a defaulted bool.
//   * `bool CausesNonVirtualInvocation { get; }` -- the plain (non-virtual)
//     property getter returning the ctor-stored bool.
//
// The C# does NOT override `ToString` or `ShallowClone` (nor any other
// `ResolveResult` virtual), but the C++ port overrides both `ClassName()` and
// `ShallowClone()` to faithfully reproduce the C# `GetType().Name` (polymorphic
// class name in the inherited `ToString`) and `MemberwiseClone` (runtime-type-
// preserving shallow clone) -- the documented `ResolveResult` conventions. A
// non-overriding C++ base clone would slice the subclass to a `ResolveResult`
// base (diverging from C# `MemberwiseClone`), and the inherited `ToString` would
// report the base class name "ResolveResult" (diverging from the C#
// `GetType().Name` which yields "ThisResolveResult").
class ThisResolveResult : public ResolveResult {
public:
    // The C# `ThisResolveResult(IType type, bool causesNonVirtualInvocation = false)
    // : base(type)`: the forwarding ctor with a defaulted bool. The C++ port
    // mirrors the default argument (`= false`) so callers may construct a
    // `ThisResolveResult(type)` (the common case -- virtual invocation) or pass
    // `true` (the `base`-reference case -- non-virtual invocation). The `type`
    // null-guard is inherited from the `ResolveResult` base ctor (the D424
    // `assert`-on-null-`ITypePtr` convention).
    ThisResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type,
                      bool causesNonVirtualInvocation = false)
        : ResolveResult(std::move(type)),
          causesNonVirtualInvocation_(causesNonVirtualInvocation) {}

    // The C# `bool CausesNonVirtualInvocation { get; }`: a plain (non-virtual)
    // property getter returning the ctor-stored bool. Gets whether this resolve
    // result causes member invocations to be non-virtual (the `base` reference
    // case). Non-virtual: it is a `ThisResolveResult`-own accessor reachable only
    // through a `ThisResolveResult*` (not a `ResolveResult*` base pointer).
    bool CausesNonVirtualInvocation() const { return causesNonVirtualInvocation_; }

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "ThisResolveResult" for a `ThisResolveResult`
    // instance. The C++ port reproduces this via the `ClassName()` override so the
    // inherited `ToString` reports the subclass name (not the base "ResolveResult"
    // the base `ClassName()` default would yield).
protected:
    std::string ClassName() const override { return "ThisResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `ThisResolveResult` stays a `ThisResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct a `ThisResolveResult` copy (the default copy
    // ctor shares the `type_` `shared_ptr`, faithful to the C# reference-copy,
    // and copies the `causesNonVirtualInvocation_` bool by value).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ThisResolveResult>(*this);
    }

private:
    bool causesNonVirtualInvocation_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_THISRESOLVERESULT_HPP
