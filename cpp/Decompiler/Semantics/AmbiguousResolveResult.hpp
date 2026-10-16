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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/AmbiguousResolveResult.cs -- the two
// `ResolveResult` subclasses that represent an ambiguous resolution (a name
// that resolved to more than one candidate). The C# file declares two classes:
//   * `AmbiguousTypeResolveResult : TypeResolveResult` -- an ambiguous type-name
//     resolution (the ctor forwards the `IType`; the `IsError` override is
//     `true`).
//   * `AmbiguousMemberResolveResult : MemberResolveResult` -- an ambiguous
//     field/property/event access (the ctor forwards the target + member; the
//     `IsError` override is `true`).
//
// This is the eighteenth `Semantics` leaf toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker), after the D424-D441
// `ResolveResult` subclasses. Both deps are already ported: `TypeResolveResult`
// (D425) and `MemberResolveResult` (D437). The C# source declares only the
// forwarding ctor and the `IsError => true` override for each class; the C++ port
// additionally overrides `ClassName()` (the D424 polymorphic-`GetType().Name`
// convention in the inherited `ToString`) and `ShallowClone()` (the D424
// runtime-type-preserving-clone convention, avoiding the C++-only slicing a
// non-overriding base clone would perform).
//
// The `IsError => true` unconditional override is the load-bearing crux
// distinguishing an ambiguous result from its base: `TypeResolveResult` (D425)
// returns `true` only for `TypeKind::Unknown`, and `MemberResolveResult` (D437)
// inherits the base default `false` -- but an ambiguous resolution is ALWAYS an
// error regardless of the stored type or member, mirroring the D436
// `ErrorResolveResult` crux (the unconditional-error `ResolveResult`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_AMBIGUOUSRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_AMBIGUOUSRESOLVERESULT_HPP

#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class AmbiguousTypeResolveResult : TypeResolveResult` (NOT
// `sealed` -- the C# class is unsealed) ports to a C++ subclass (NOT `final`) of
// `TypeResolveResult`. The C# surface is the forwarding ctor plus the
// `IsError => true` override; the C++ port additionally overrides `ClassName()`
// and `ShallowClone()` to faithfully reproduce the C# `GetType().Name`
// (polymorphic class name in the inherited `ToString`) and `MemberwiseClone`
// (runtime-type-preserving shallow clone) -- the documented `ResolveResult`
// conventions every subclass port carries.
class AmbiguousTypeResolveResult : public TypeResolveResult {
public:
    // The C# `AmbiguousTypeResolveResult(IType type) : base(type)` -- forwards the
    // type to the `TypeResolveResult` base ctor (which forwards to the
    // `ResolveResult` base that `assert`-guards the non-null `IType` per the D424
    // `ArgumentNullException`-to-`assert` convention).
    explicit AmbiguousTypeResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : TypeResolveResult(std::move(type)) {}

    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // ambiguous type-name resolution is always an error, unconditionally
    // (regardless of the stored type). This OVERRIDES the `TypeResolveResult`
    // (D425) `IsError` which returns `true` only for `TypeKind::Unknown` -- an
    // ambiguous resolution is an error even when the type is a known, valid type
    // the resolver picked as one of several candidates.
    bool IsError() const override { return true; }

protected:
    // The C# `ToString` (inherited from `ResolveResult` via `TypeResolveResult`)
    // uses `GetType().Name` which is polymorphic and yields
    // "AmbiguousTypeResolveResult". The C++ port reproduces this via the
    // `ClassName()` override so the inherited `ToString` reports the subclass
    // name (not the base "TypeResolveResult" the inherited `ClassName()` would
    // yield).
    std::string ClassName() const override { return "AmbiguousTypeResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult` via
    // `TypeResolveResult`) uses `MemberwiseClone` which preserves the runtime
    // type, so a cloned `AmbiguousTypeResolveResult` stays an
    // `AmbiguousTypeResolveResult` (not sliced to the `TypeResolveResult` or
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct an `AmbiguousTypeResolveResult` copy (the
    // default copy ctor shares the `type_` `shared_ptr`, faithful to the C#
    // reference-copy).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<AmbiguousTypeResolveResult>(*this);
    }
};

// The C# `public class AmbiguousMemberResolveResult : MemberResolveResult` (NOT
// `sealed` -- the C# class is unsealed) ports to a C++ subclass (NOT `final`) of
// `MemberResolveResult`. The C# surface is the forwarding ctor plus the
// `IsError => true` override; the C++ port additionally overrides `ClassName()`
// and `ShallowClone()` to faithfully reproduce the C# `GetType().Name`
// (polymorphic class name in the inherited `ToString`) and `MemberwiseClone`
// (runtime-type-preserving shallow clone).
class AmbiguousMemberResolveResult : public MemberResolveResult {
public:
    // The C# `AmbiguousMemberResolveResult(ResolveResult targetResult, IMember
    // member) : base(targetResult, member)` -- forwards the target and member to
    // the `MemberResolveResult` common ctor (D437 ctor 1), which computes the
    // result type via `ComputeType(*member)` and `isVirtualCall` from the member
    // and target. The target ports to `std::shared_ptr<ResolveResult>` (the D428
    // shared-ownership convention); the member ports to a non-owning
    // `const IMember*` raw pointer (the D437 non-owning-handle convention).
    AmbiguousMemberResolveResult(std::shared_ptr<ResolveResult> targetResult,
                                 const ILSpy::Decompiler::TypeSystem::IMember* member)
        : MemberResolveResult(std::move(targetResult), member) {}

    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // ambiguous field/property/event access is always an error, unconditionally
    // (regardless of the stored member). This OVERRIDES the `MemberResolveResult`
    // (D437) `IsError` which inherits the base default `false` -- an ambiguous
    // member access is an error even when the member is a valid, resolvable member
    // the resolver picked as one of several candidates.
    bool IsError() const override { return true; }

protected:
    // The C# `ToString` (inherited from `ResolveResult` via `MemberResolveResult`)
    // uses `GetType().Name` which is polymorphic and yields
    // "AmbiguousMemberResolveResult". The C++ port reproduces this via the
    // `ClassName()` override so the inherited `ToString` reports the subclass
    // name (not the base "MemberResolveResult" the inherited `ClassName()` would
    // yield).
    std::string ClassName() const override { return "AmbiguousMemberResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult` via
    // `MemberResolveResult`) uses `MemberwiseClone` which preserves the runtime
    // type, so a cloned `AmbiguousMemberResolveResult` stays an
    // `AmbiguousMemberResolveResult` (not sliced to the `MemberResolveResult` or
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct an `AmbiguousMemberResolveResult` copy (the
    // default copy ctor shares the `targetResult_` `shared_ptr`, copies the
    // `member_` raw pointer, and copies the `isConstant_`/`isVirtualCall_` bools
    // and the `constantValue_` `std::any`, faithful to the C# `MemberwiseClone`).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<AmbiguousMemberResolveResult>(*this);
    }
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_AMBIGUOUSRESOLVERESULT_HPP
