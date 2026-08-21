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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/TypeOfResolveResult.cs -- the resolved
// expression is the `typeof` operator. `TypeOfResolveResult` is the fourth
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker), after `ResolveResult` (D424), `TypeResolveResult` (D425), and
// `ThisResolveResult` (D426). The C# source declares a forwarding ctor taking two
// `IType` arguments (the `systemType` forwarded to the base, and the
// `referencedType` stored with an `ArgumentNullException` null guard) and a
// non-virtual `ReferencedType` property getter; it does NOT override any of the
// `ResolveResult` virtuals. The C++ port additionally overrides `ClassName()` (to
// faithfully mirror the C# polymorphic `GetType().Name` in the inherited
// `ToString`, the D424 `ClassName()`-for-polymorphic-class-name convention) and
// `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention every subclass port
// carries). The load-bearing behavior this subclass adds beyond the base is the
// SECOND `IType` field (`referencedType`) exposed via `ReferencedType()`, distinct
// from the base `Type()` (the `systemType`): a `typeof(T)` expression's own type is
// `System.Type` (the `systemType`) while the type it names is `T` (the
// `referencedType`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_TYPEOFRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_TYPEOFRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class TypeOfResolveResult : ResolveResult` (NOT `sealed`, and no
// C# subclass derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is:
//   * `TypeOfResolveResult(IType systemType, IType referencedType)
//     : base(systemType)` -- the ctor forwards `systemType` to the base and
//     stores `referencedType` (throwing `ArgumentNullException` on null).
//   * `IType ReferencedType { get; }` -- the plain (non-virtual) property getter
//     returning the ctor-stored `referencedType`.
//
// The C# does NOT override `ToString` or `ShallowClone` (nor any other
// `ResolveResult` virtual), but the C++ port overrides both `ClassName()` and
// `ShallowClone()` to faithfully reproduce the C# `GetType().Name` (polymorphic
// class name in the inherited `ToString`) and `MemberwiseClone` (runtime-type-
// preserving shallow clone) -- the documented `ResolveResult` conventions. A
// non-overriding C++ base clone would slice the subclass to a `ResolveResult`
// base (diverging from C# `MemberwiseClone`), and the inherited `ToString` would
// report the base class name "ResolveResult" (diverging from the C#
// `GetType().Name` which yields "TypeOfResolveResult").
class TypeOfResolveResult : public ResolveResult {
public:
    // The C# `TypeOfResolveResult(IType systemType, IType referencedType)
    // : base(systemType)`: forwards `systemType` to the `ResolveResult` base
    // (whose ctor guards it with `ArgumentNullException` -> `assert`, the D424
    // convention) and stores `referencedType` (the C#
    // `if (referencedType == null) throw ArgumentNullException` ports to an
    // `assert`, the D401 `Debug.Assert`-to-`assert` / D424
    // `ArgumentNullException`-to-`assert` convention). Both `IType` arguments are
    // held as `ITypePtr` (the D271 `shared_ptr<IType>` reference-handle model),
    // sharing ownership with the caller (the C# GC-owned references); the base
    // `type_` and the own `referencedType_` are distinct `shared_ptr` members.
    TypeOfResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr systemType,
                        ILSpy::Decompiler::TypeSystem::ITypePtr referencedType)
        : ResolveResult(std::move(systemType)) {
        assert(referencedType && "TypeOfResolveResult: referencedType must not be null");
        referencedType_ = std::move(referencedType);
    }

    // The C# `IType ReferencedType { get; }`: a plain (non-virtual) property
    // getter returning the ctor-stored `referencedType`. The non-null reference
    // return (the D374 `IVariable::Type()` non-null-reference convention; the
    // ctor `assert` pins non-null) returns `*referencedType_` -- the held
    // `shared_ptr` keeps the `IType` alive for the `TypeOfResolveResult`'s
    // lifetime. Non-virtual: it is a `TypeOfResolveResult`-own accessor reachable
    // only through a `TypeOfResolveResult*` (not a `ResolveResult*` base pointer),
    // distinct from the inherited `Type()` which returns the `systemType`.
    const ILSpy::Decompiler::TypeSystem::IType& ReferencedType() const {
        return *referencedType_;
    }

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "TypeOfResolveResult" for a
    // `TypeOfResolveResult` instance. The C++ port reproduces this via the
    // `ClassName()` override so the inherited `ToString` reports the subclass
    // name (not the base "ResolveResult" the base `ClassName()` default would
    // yield).
protected:
    std::string ClassName() const override { return "TypeOfResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `TypeOfResolveResult` stays a `TypeOfResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct a `TypeOfResolveResult` copy (the default copy
    // ctor shares BOTH the base `type_` and the own `referencedType_`
    // `shared_ptr` members, faithful to the C# reference-copy of both `IType`
    // fields).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<TypeOfResolveResult>(*this);
    }

private:
    ILSpy::Decompiler::TypeSystem::ITypePtr referencedType_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_TYPEOFRESOLVERESULT_HPP
