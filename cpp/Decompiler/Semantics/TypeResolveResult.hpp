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

// Port of ICSharpCode.Decompiler/Semantics/TypeResolveResult.cs -- the resolved
// expression refers to a type name. `TypeResolveResult` is the simplest
// `ResolveResult` (D424) subclass: it adds only the `IsError` override (a type
// name resolves to an error when the type is `TypeKind.Unknown`) and is the
// second `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker). The C# source declares only the ctor and
// the `IsError` override; the C++ port additionally overrides `ClassName()` (to
// faithfully mirror the C# polymorphic `GetType().Name` in the inherited
// `ToString`, the D424 `ClassName()`-for-polymorphic-class-name convention) and
// `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention the first subclass port
// carries).

#ifndef ILSPY_DECOMPILER_SEMANTICS_TYPERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_TYPERESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class TypeResolveResult : ResolveResult` (NOT `sealed` -- the
// `AmbiguousTypeResolveResult` subclass in `Semantics/AmbiguousResolveResult.cs`
// derives from it) ports to a C++ subclass (NOT `final`) of `ResolveResult`.
// The C# surface is:
//   * `TypeResolveResult(IType type) : base(type)` -- the forwarding ctor.
//   * `public override bool IsError => this.Type.Kind == TypeKind.Unknown` -- the
//     sole behavioral override; a type-name expression is an error when the type
//     is the unknown null object.
//
// The C# does NOT override `ToString` or `ShallowClone`, but the C++ port
// overrides both `ClassName()` and `ShallowClone()` to faithfully reproduce the
// C# `GetType().Name` (polymorphic class name in the inherited `ToString`) and
// `MemberwiseClone` (runtime-type-preserving shallow clone) -- the documented
// `ResolveResult` conventions. A non-overriding C++ base clone would slice the
// subclass to a `ResolveResult` base (diverging from C# `MemberwiseClone`), and
// the inherited `ToString` would report the base class name "ResolveResult"
// (diverging from the C# `GetType().Name` which yields "TypeResolveResult").
class TypeResolveResult : public ResolveResult {
public:
    explicit TypeResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : ResolveResult(std::move(type)) {}

    // The C# `public override bool IsError => this.Type.Kind == TypeKind.Unknown`:
    // a type-name expression resolves to an error when the type is the unknown
    // null object (`TypeKind::Unknown` -- the `UnknownType()` convenience null
    // object, the `UnknownType` class, or any `IType` whose `Kind()` returns
    // `Unknown`). The base default returns `false`; this override flips it to
    // `true` for the unknown kind only.
    bool IsError() const override {
        return Type().Kind() == ILSpy::Decompiler::TypeSystem::TypeKind::Unknown;
    }

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "TypeResolveResult" for a `TypeResolveResult`
    // instance. The C++ port reproduces this via the `ClassName()` override so the
    // inherited `ToString` reports the subclass name (not the base "ResolveResult"
    // the base `ClassName()` default would yield).
protected:
    std::string ClassName() const override { return "TypeResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `TypeResolveResult` stays a `TypeResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct a `TypeResolveResult` copy (the default copy
    // ctor shares the `type_` `shared_ptr`, faithful to the C# reference-copy).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<TypeResolveResult>(*this);
    }
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_TYPERESOLVERESULT_HPP
