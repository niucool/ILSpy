// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, without limitation the rights to use, copy,
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

// Port of ICSharpCode.Decompiler/Semantics/ThrowResolveResult.cs -- the
// `ResolveResult` (D424) for a `throw` expression (the C# 7 `throw`-expression
// form usable in a conditional or assignment context). This is the eleventh
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker). The C# source declares only the parameterless
// ctor:
//   * `ThrowResolveResult() : base(SpecialType.NoType)` -- forwards
//     `SpecialType.NoType` (the `TypeKind::None` null object, no type at all) to
//     the `ResolveResult` base ctor.
//
// The C# `SpecialType.NoType` dependency (the `TypeKind::None` special-type null
// object) was the prerequisite the D427/D432 next-in-order analysis flagged: the
// C++ minimal `IType` port had only the `UnknownType()` convenience
// (`SpecialType(TypeKind::Unknown)`), NOT a `NoType()` (`SpecialType(TypeKind::None)`).
// The D433 `NamespaceResolveResult` port added the `NoType()` convenience to
// `IType.hpp`, unblocking `ThrowResolveResult` (the other
// `SpecialType.NoType`-dependent subclass). This leaf consumes it.
//
// The C# surface is a parameterless forwarding ctor with NO behavioral overrides
// (no `IsError`, `IsCompileTimeConstant`, `ConstantValue`, `GetChildResults`, or
// `ToString` override) -- the subclass relies entirely on the inherited
// `ResolveResult` base defaults. The C++ port additionally overrides `ClassName()`
// (to faithfully mirror the C# polymorphic `GetType().Name` in the inherited
// `ToString`, the D424 `ClassName()`-for-polymorphic-class-name convention) and
// `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention every subclass port
// carries). This is structurally the simplest possible `ResolveResult` subclass:
// a parameterless forwarding ctor plus the two `ClassName()`/`ShallowClone()`
// convention overrides, with no own state beyond the base `type` field.
//
// KEY PORT CONVENTIONS:
//  * The C# `class ThrowResolveResult` (NO access modifier -- the C# default is
//    `internal`, assembly-private) ports to a PUBLIC C++ class (no assembly
//    boundary in C++; the D428 `internal`-ctor-to-`public` precedent applied to a
//    whole class, documented here as the assembly-internal gate the C# source
//    restricts but the C++ port has no counterpart for).
//  * The C# `base(SpecialType.NoType)` forwards the `TypeKind::None` null object;
//    the C++ port forwards `ILSpy::Decompiler::TypeSystem::NoType()` (the D433
//    additive `IType.hpp` convenience). Distinct from `UnknownType()`
//    (`TypeKind::Unknown`, the error-type null object) -- a `throw` expression
//    has NO type at all (it never yields a value), not an unknown one.

#ifndef ILSPY_DECOMPILER_SEMANTICS_THROWRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_THROWRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `class ThrowResolveResult : ResolveResult` (internal, NOT `sealed` -- no
// C# subclass derives from it but it is unsealed) ports to a PUBLIC C++ subclass
// (NOT `final`) of `ResolveResult`. The C# surface is just the parameterless
// forwarding ctor; the C++ port additionally overrides `ClassName()` and
// `ShallowClone()` to faithfully reproduce the C# `GetType().Name` (polymorphic
// class name in the inherited `ToString`) and `MemberwiseClone`
// (runtime-type-preserving shallow clone) -- the documented `ResolveResult`
// conventions. A non-overriding C++ base clone would slice the subclass to a
// `ResolveResult` base (diverging from C# `MemberwiseClone`), and the inherited
// `ToString` would report the base class name "ResolveResult" (diverging from the
// C# `GetType().Name` which yields "ThrowResolveResult").
class ThrowResolveResult : public ResolveResult {
public:
    // The C# `ThrowResolveResult() : base(SpecialType.NoType)` -- forwards
    // `SpecialType.NoType` (the `TypeKind::None` null object, no type at all) to
    // the `ResolveResult` base ctor. The C++ port forwards
    // `ILSpy::Decompiler::TypeSystem::NoType()` (the D433 additive `IType.hpp`
    // convenience). Distinct from `UnknownType()` (`TypeKind::Unknown`, the
    // error-type null object): a `throw` expression has NO type at all (it never
    // yields a value), not an unknown one.
    ThrowResolveResult()
        : ResolveResult(ILSpy::Decompiler::TypeSystem::NoType()) {}

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "ThrowResolveResult" for a
    // `ThrowResolveResult` instance. The C++ port reproduces this via the
    // `ClassName()` override so the inherited `ToString` reports the subclass name
    // (not the base "ResolveResult" the base `ClassName()` default would yield).
protected:
    std::string ClassName() const override { return "ThrowResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `ThrowResolveResult` stays a `ThrowResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct a `ThrowResolveResult` copy (the default copy
    // ctor shares the `type_` `shared_ptr` -- both the original and the clone hold
    // the same `NoType()` null object, faithful to the C# reference-copy).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ThrowResolveResult>(*this);
    }
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_THROWRESOLVERESULT_HPP
