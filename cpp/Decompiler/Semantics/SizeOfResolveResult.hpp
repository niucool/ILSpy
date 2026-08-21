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

// Port of ICSharpCode.Decompiler/Semantics/SizeOfResolveResult.cs -- the resolved
// expression is the `sizeof` operator. `SizeOfResolveResult` is the sixth
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker), after `ResolveResult` (D424), `TypeResolveResult` (D425),
// `ThisResolveResult` (D426), `TypeOfResolveResult` (D427), and
// `ByReferenceResolveResult` (D428). The C# source declares a forwarding ctor taking
// two `IType` arguments (the `int32` forwarded to the base, and the `referencedType`
// stored with an `ArgumentNullException` null guard) plus an `int?` `constantValue`
// (the compile-time-known size, null when the size is not a compile-time constant).
// It exposes `ReferencedType` (a plain non-virtual getter, like `TypeOfResolveResult`)
// and overrides three `ResolveResult` virtuals: `IsCompileTimeConstant` (true when
// `constantValue` is non-null), `ConstantValue` (returns the boxed `constantValue`),
// and `IsError` (`referencedType.IsReferenceType != false` -- a `sizeof` of a
// reference type or a type of unknown reference-ness is an error, since `sizeof`
// only applies to value types). The `IsError` override is the consumer the D429
// `IsReferenceType` (the `bool? IsReferenceType` virtual on `IType`) leaf was ported
// for: the C# `bool? != bool` lifted comparison returns `bool` where `null != false`
// is `true`, faithfully mirrored by the C++ `std::optional<bool> != false` (the D429
// `ConsumerPattern*` tests pin this exact shape). The C++ port additionally overrides
// `ClassName()` (to faithfully mirror the C# polymorphic `GetType().Name` in the
// inherited `ToString`, the D424 `ClassName()`-for-polymorphic-class-name convention)
// and `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention every subclass port
// carries).

#ifndef ILSPY_DECOMPILER_SEMANTICS_SIZEOFRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_SIZEOFRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <any>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class SizeOfResolveResult : ResolveResult` (NOT `sealed`, and no
// C# subclass derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is:
//   * `SizeOfResolveResult(IType int32, IType referencedType, int? constantValue)
//     : base(int32)` -- the ctor forwards `int32` (the `sizeof` expression's own
//     type, `System.Int32`) to the base, stores `referencedType` (throwing
//     `ArgumentNullException` on null) and `constantValue` (the compile-time-known
//     size, null when not known).
//   * `IType ReferencedType { get; }` -- the plain (non-virtual) property getter
//     returning the ctor-stored `referencedType` (the type the `sizeof` names).
//   * `override bool IsCompileTimeConstant => constantValue != null` -- true when
//     the size is a compile-time constant.
//   * `override object? ConstantValue => constantValue` -- the boxed `int?`
//     (null when `constantValue` is null).
//   * `override bool IsError => referencedType.IsReferenceType != false` -- a
//     `sizeof` of a reference type or a type of unknown reference-ness is an error.
//
// The C# does NOT override `ToString` or `ShallowClone`, but the C++ port overrides
// both `ClassName()` and `ShallowClone()` to faithfully reproduce the C#
// `GetType().Name` (polymorphic class name in the inherited `ToString`) and
// `MemberwiseClone` (runtime-type-preserving shallow clone) -- the documented
// `ResolveResult` conventions. A non-overriding C++ base clone would slice the
// subclass to a `ResolveResult` base (diverging from C# `MemberwiseClone`), and the
// inherited `ToString` would report the base class name "ResolveResult" (diverging
// from the C# `GetType().Name` which yields "SizeOfResolveResult").
class SizeOfResolveResult : public ResolveResult {
public:
    // The C# `SizeOfResolveResult(IType int32, IType referencedType, int?
    // constantValue) : base(int32)`: forwards `int32` to the `ResolveResult` base
    // (whose ctor guards it with `ArgumentNullException` -> `assert`, the D424
    // convention) and stores `referencedType` (the C#
    // `if (referencedType == null) throw ArgumentNullException` ports to an
    // `assert`, the D401 `Debug.Assert`-to-`assert` / D427
    // `ArgumentNullException`-to-`assert` convention) and `constantValue` (the
    // C# `int?` ports to `std::optional<int>`, nullopt = C# null). Both `IType`
    // arguments are held as `ITypePtr` (the D271 `shared_ptr<IType>`
    // reference-handle model), sharing ownership with the caller (the C# GC-owned
    // references); the base `type_` and the own `referencedType_` are distinct
    // `shared_ptr` members, and `constantValue_` is a value member.
    SizeOfResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr int32,
                        ILSpy::Decompiler::TypeSystem::ITypePtr referencedType,
                        std::optional<int> constantValue)
        : ResolveResult(std::move(int32)),
          constantValue_(std::move(constantValue)) {
        assert(referencedType && "SizeOfResolveResult: referencedType must not be null");
        referencedType_ = std::move(referencedType);
    }

    // The C# `IType ReferencedType { get; }`: a plain (non-virtual) property
    // getter returning the ctor-stored `referencedType`. The non-null reference
    // return (the D374 `IVariable::Type()` non-null-reference convention; the
    // ctor `assert` pins non-null) returns `*referencedType_` -- the held
    // `shared_ptr` keeps the `IType` alive for the `SizeOfResolveResult`'s
    // lifetime. Non-virtual: it is a `SizeOfResolveResult`-own accessor reachable
    // only through a `SizeOfResolveResult*` (not a `ResolveResult*` base pointer),
    // distinct from the inherited `Type()` which returns the `int32` forwarded to
    // the base (the D427 `ReferencedType` precedent).
    const ILSpy::Decompiler::TypeSystem::IType& ReferencedType() const {
        return *referencedType_;
    }

    // The C# `override bool IsCompileTimeConstant => constantValue != null`: true
    // when the size is a compile-time constant. The C# `int? != null` ports to
    // `std::optional<int>::has_value()` -- a non-empty optional is a non-null
    // `int?`, an empty optional is a null `int?`.
    bool IsCompileTimeConstant() const override {
        return constantValue_.has_value();
    }

    // The C# `override object? ConstantValue => constantValue`: the boxed `int?`.
    // A non-null `int?` boxes to a boxed `int` (the D374 `std::any`-for-`object?`
    // convention; a non-null `int` is `std::any(*constantValue_)`); a null `int?`
    // boxes to `null` (the empty `std::any`, matching the base default). The
    // `value()` extracts the `int` which the `std::any` ctor copies.
    std::any ConstantValue() const override {
        if (constantValue_)
            return std::any(*constantValue_);
        return {};
    }

    // The C# `override bool IsError => referencedType.IsReferenceType != false`: a
    // `sizeof` of a reference type (`IsReferenceType == true`, so `true != false`
    // is `true`) or a type of unknown reference-ness (`IsReferenceType == null`,
    // so the C# `bool? != bool` lifted comparison yields `null != false` = `true`)
    // is an error; a `sizeof` of a value type (`IsReferenceType == false`, so
    // `false != false` is `false`) is not. The C++ `std::optional<bool> != false`
    // mirrors the C# lifted comparison faithfully: `std::optional<bool>(true) !=
    // false` is `true`; `std::optional<bool>(false) != false` is `false`;
    // `std::nullopt != false` is `true` (the D429 `IsReferenceType` leaf and its
    // `ConsumerPattern*` tests pin this exact `!= false` shape). The `false` is a
    // `bool` literal the `std::optional<bool>` comparison operator accepts.
    bool IsError() const override {
        return referencedType_->IsReferenceType() != false;
    }

    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "SizeOfResolveResult" for a
    // `SizeOfResolveResult` instance. The C++ port reproduces this via the
    // `ClassName()` override so the inherited `ToString` reports the subclass
    // name (not the base "ResolveResult" the base `ClassName()` default would
    // yield).
protected:
    std::string ClassName() const override { return "SizeOfResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `SizeOfResolveResult` stays a `SizeOfResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct a `SizeOfResolveResult` copy (the default copy
    // ctor shares the base `type_` and the own `referencedType_` `shared_ptr`
    // members, and copies the `constantValue_` value member, faithful to the C#
    // reference-copy of both `IType` fields and the value-copy of the `int?`).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<SizeOfResolveResult>(*this);
    }

private:
    ILSpy::Decompiler::TypeSystem::ITypePtr referencedType_;
    std::optional<int> constantValue_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_SIZEOFRESOLVERESULT_HPP
