// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without, including without limitation, rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
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

// Port of ICSharpCode.Decompiler/Semantics/TypeIsResolveResult.cs -- the resolved
// expression is a C# `is` expression ("Input is TargetType"). `TypeIsResolveResult`
// is the seventh `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker), after `ResolveResult` (D424),
// `TypeResolveResult` (D425), `ThisResolveResult` (D426), `TypeOfResolveResult`
// (D427), `ByReferenceResolveResult` (D428), and `SizeOfResolveResult` (D430). The
// C# source declares a forwarding ctor taking a `ResolveResult input`, an `IType
// targetType`, and an `IType booleanType` (the latter forwarded to the base as the
// expression's own type, conventionally `bool`), with `ArgumentNullException` null
// guards on `input` and `targetType`; it exposes the two stored fields as
// `public readonly` fields (`Input` and `TargetType`) and does NOT override any of
// the `ResolveResult` virtuals (`IsError` / `IsCompileTimeConstant` /
// `ConstantValue` / `GetChildResults` / `ToString` / `ShallowClone`). The C++ port
// additionally overrides `ClassName()` (to faithfully mirror the C# polymorphic
// `GetType().Name` in the inherited `ToString`, the D424 `ClassName()`-for-
// polymorphic-class-name convention) and `ShallowClone()` (to faithfully mirror
// the C# `MemberwiseClone` runtime-type preservation, avoiding the C++-only
// slicing a non-overriding base clone would perform -- the documented
// `ResolveResult` convention every subclass port carries). The load-bearing
// behavior this subclass adds beyond the base is the `Input` (`ResolveResult`)
// and `TargetType` (a second `IType` beyond the base `booleanType`) members: an
// `is` expression's own type is `bool` (the base `Type()`), while the `Input` is
// the resolved left-hand operand and the `TargetType` is the type the operand is
// compared against.

#ifndef ILSPY_DECOMPILER_SEMANTICS_TYPEISRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_TYPEISRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class TypeIsResolveResult : ResolveResult` (NOT `sealed`, and no
// C# subclass derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is:
//   * `TypeIsResolveResult(ResolveResult input, IType targetType, IType
//     booleanType) : base(booleanType)` -- the ctor forwards `booleanType` to the
//     base (the expression's own type, conventionally `bool`), and stores `input`
//     and `targetType` (both throwing `ArgumentNullException` on null).
//   * `public readonly ResolveResult Input` -- the stored left-hand operand.
//   * `public readonly IType TargetType` -- the stored type compared against.
//
// The C# does NOT override `ToString` or `ShallowClone` (nor any other
// `ResolveResult` virtual), but the C++ port overrides both `ClassName()` and
// `ShallowClone()` to faithfully reproduce the C# `GetType().Name` (polymorphic
// class name in the inherited `ToString`) and `MemberwiseClone` (runtime-type-
// preserving shallow clone) -- the documented `ResolveResult` conventions. A
// non-overriding C++ base clone would slice the subclass to a `ResolveResult`
// base (diverging from C# `MemberwiseClone`), and the inherited `ToString` would
// report the base class name "ResolveResult" (diverging from the C#
// `GetType().Name` which yields "TypeIsResolveResult").
class TypeIsResolveResult : public ResolveResult {
public:
    // The C# `TypeIsResolveResult(ResolveResult input, IType targetType, IType
    // booleanType) : base(booleanType)`: forwards `booleanType` to the
    // `ResolveResult` base (whose ctor guards it with `ArgumentNullException` ->
    // `assert`, the D424 convention) and stores `input` and `targetType` (the C#
    // `if (input == null) throw ArgumentNullException` and `if (targetType ==
    // null) throw ArgumentNullException` both port to `assert`, the D401
    // `Debug.Assert`-to-`assert` / D424 `ArgumentNullException`-to-`assert`
    // convention). The `input` is held as a `shared_ptr<ResolveResult>` (the
    // faithful C# GC-reference model -- the caller and the `TypeIsResolveResult`
    // share the `ResolveResult`, and `ShallowClone`'s default copy ctor shares
    // it too, mirroring `MemberwiseClone`'s reference-copy, the D428
    // `ByReferenceResolveResult::elementResult_` precedent); the `targetType` is
    // held as an `ITypePtr` (the D271 `shared_ptr<IType>` reference-handle model),
    // distinct from the base `type_` (the `booleanType`).
    TypeIsResolveResult(std::shared_ptr<ResolveResult> input,
                       ILSpy::Decompiler::TypeSystem::ITypePtr targetType,
                       ILSpy::Decompiler::TypeSystem::ITypePtr booleanType)
        : ResolveResult(std::move(booleanType)) {
        assert(input && "TypeIsResolveResult: input must not be null");
        assert(targetType && "TypeIsResolveResult: targetType must not be null");
        input_ = std::move(input);
        targetType_ = std::move(targetType);
    }

    // The C# `public readonly ResolveResult Input` -- the stored left-hand
    // operand. Returns a non-owning raw pointer (the `shared_ptr` keeps the
    // `ResolveResult` alive for the `TypeIsResolveResult`'s lifetime); never null
    // (the ctor `assert` pins non-null), matching the D428
    // `ByReferenceResolveResult::ElementResult()` accessor shape for a
    // `shared_ptr<ResolveResult>` member.
    ResolveResult* Input() const noexcept { return input_.get(); }

    // The C# `public readonly IType TargetType` -- the stored type compared
    // against. The non-null reference return (the D374 `IVariable::Type()`
    // non-null-reference convention; the ctor `assert` pins non-null) returns
    // `*targetType_` -- the held `shared_ptr` keeps the `IType` alive for the
    // `TypeIsResolveResult`'s lifetime, matching the D427
    // `TypeOfResolveResult::ReferencedType()` accessor shape for a second
    // `ITypePtr` member.
    const ILSpy::Decompiler::TypeSystem::IType& TargetType() const {
        return *targetType_;
    }

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "TypeIsResolveResult" for a
    // `TypeIsResolveResult` instance. The C++ port reproduces this via the
    // `ClassName()` override so the inherited `ToString` reports the subclass
    // name (not the base "ResolveResult" the base `ClassName()` default would
    // yield). The format is "[TypeIsResolveResult <booleanType reflection name>]"
    // -- the base `Type()` is the `booleanType` (conventionally `bool`).
protected:
    std::string ClassName() const override { return "TypeIsResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `TypeIsResolveResult` stays a `TypeIsResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct a `TypeIsResolveResult` copy (the default copy
    // ctor shares the base `type_` `shared_ptr` (the `booleanType`), the
    // `input_` `shared_ptr<ResolveResult>`, and the `targetType_` `ITypePtr`,
    // faithful to the C# reference-copy of all three fields).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<TypeIsResolveResult>(*this);
    }

private:
    std::shared_ptr<ResolveResult> input_;
    ILSpy::Decompiler::TypeSystem::ITypePtr targetType_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_TYPEISRESOLVERESULT_HPP
