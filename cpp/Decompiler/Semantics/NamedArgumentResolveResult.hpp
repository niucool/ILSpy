// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE NONINFRINGEMENT AND IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/NamedArgumentResolveResult.cs -- the result
// of a named argument expression (`name: argument`). It is the twenty-third `Semantics`
// leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker),
// deriving directly from `ResolveResult` (D424) and carrying the owning
// `IParameterizedMember` (nullable), the `IParameter` (nullable -- set only by the first
// ctor), the `ParameterName` (a `string`), and the `Argument` (a non-null `ResolveResult`).
// All deps are already ported: `ResolveResult` (D424), `IParameter` (D382),
// `IParameterizedMember` (D388), and `IType` (D271, for the argument's type forwarded to
// the base). The C# declares two ctors and overrides `GetChildResults` to return the single
// `Argument`.

#ifndef ILSPY_DECOMPILER_SEMANTICS_NAMEDARGUMENTRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_NAMEDARGUMENTRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// Forward declaration of `IParameterizedMember` (D388): the header holds it only as a
// nullable non-owning raw pointer (the `member_` field, the ctor param, the `Member()`
// accessor return) -- never dereferenced -- so a forward declaration suffices and keeps
// the include graph minimal (the `IEntity::ParentModule` forward-declared-pointer
// precedent). `IParameter` (D382) IS dereferenced (`parameter->Name()` in the first ctor
// body), so `IParameter.hpp` is included directly.
class IParameterizedMember;

// The C# `public class NamedArgumentResolveResult : ResolveResult` (NOT `sealed`, and no
// C# subclass derives from it) ports to a C++ subclass (NOT `final`) of `ResolveResult`.
// The C# surface is:
//   * `readonly IParameterizedMember Member` -- the member to which the parameter belongs;
//     the field CAN be null (the first ctor's `member` param defaults to null).
//   * `readonly IParameter Parameter` -- the parameter; the field CAN be null (the second
//     ctor does not set it, leaving the C# default null).
//   * `readonly string ParameterName` -- the parameter name (set by both ctors: the first
//     derives it from `parameter.Name`, the second takes it directly).
//   * `readonly ResolveResult Argument` -- the argument passed to the parameter (non-null;
//     both ctors guard it with `ArgumentNullException`).
//   * Ctor 1 `(IParameter parameter, ResolveResult argument, IParameterizedMember member
//     = null) : base(argument.Type)` -- guards `parameter` and `argument` with
//     `ArgumentNullException`; stores `member`, `parameter`, `ParameterName = parameter.Name`,
//     `argument`.
//   * Ctor 2 `(string parameterName, ResolveResult argument) : base(argument.Type)` --
//     guards `parameterName` and `argument` with `ArgumentNullException`; stores
//     `ParameterName` and `argument` (leaving `Member` and `Parameter` at the C# default null).
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- `return new[] { Argument }`.
//
// KEY PORT CONVENTIONS:
//  * The C# `IParameterizedMember Member` (a nullable reference-type field, the first
//    ctor's `member` param defaulting to null) ports to a non-owning `const
//    IParameterizedMember*` raw pointer member defaulting to `nullptr` (the D437
//    `MemberResolveResult::member_` non-owning-raw-pointer precedent). The member is owned
//    by the type system; the `NamedArgumentResolveResult` is a temporary resolution
//    outcome that does not outlive it (the C# GC guarantee). No null guard (the C# field
//    may be null).
//  * The C# `IParameter Parameter` (a reference-type field that ctor 1 guards with
//    `ArgumentNullException` but ctor 2 leaves at the C# default null) ports to a non-owning
//    `const IParameter*` raw pointer member defaulting to `nullptr`. Ctor 1 `assert`-guards
//    it non-null (the D424 `ArgumentNullException`-to-`assert` convention) and dereferences it
//    (`parameter->Name()`); ctor 2 leaves it `nullptr` (the `member_`/`parameter_` default
//    member initializers cover the unset fields). The asymmetry -- a guarded ctor that
//    requires it non-null and an unguarded ctor that leaves it null -- is faithful to the
//    C# (the field is effectively nullable because ctor 2 never sets it).
//  * The C# `string ParameterName` (set by both ctors; ctor 2 guards `parameterName` with
//    `ArgumentNullException`) ports to a `std::string` value member (the D432
//    `ConstantResolveResult` / D443 `InterpolatedStringResolveResult` string-field
//    precedent: a C# `string` ported to `std::string` is a value, never null, so the C#
//    null-string state has no faithful `std::string` counterpart -- the ctor 2
//    `ArgumentNullException` on `parameterName` therefore has no C++ assert counterpart;
//    only the base `argument.Type` and the ctor 1 `parameter` guards port to asserts).
//  * The C# `ResolveResult Argument` (non-null, both ctors guard it with
//    `ArgumentNullException`) ports to a `std::shared_ptr<ResolveResult>` (the D428
//    `ByReferenceResolveResult::elementResult_` / D431 `TypeIsResolveResult::input_`
//    shared-ownership precedent), `assert`-guarded non-null (the D424 convention). The
//    argument's type is forwarded to the `ResolveResult` base via the protected
//    `ResolveResult::TypePtr()` accessor (`argument->TypePtr()` in the member-init list,
//    the D428 delegating-ctor pattern -- evaluated before the `argument` is moved into the
//    member).
//  * `ToString` is inherited (the D425/D426/D427/D443 inherit-`ToString`-override-
//    `ClassName` convention): overriding `ClassName()` to "NamedArgumentResolveResult"
//    makes the inherited base `ResolveResult::ToString` yield
//    "[NamedArgumentResolveResult <argument-type reflection name>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C# inherited
//    `MemberwiseClone` preserves the runtime type, but a non-overriding C++ base clone
//    (`make_unique<ResolveResult>(*this)`) would SLICE a `NamedArgumentResolveResult` to
//    its base, so the override does `make_unique<NamedArgumentResolveResult>(*this)` (the
//    default copy ctor copies the two non-owning raw pointers, value-copies the
//    `parameterName_` string, and shares the `argument_` shared_ptr and the base `type_`
//    shared_ptr faithfully mirroring the C# reference-copy of the `Argument` and `Type`).
class NamedArgumentResolveResult : public ResolveResult {
public:
    // Ctor 1: `(IParameter parameter, ResolveResult argument, IParameterizedMember member
    // = null) : base(argument.Type)`. Forwards the argument's type to the `ResolveResult`
    // base (extracted as an `ITypePtr` via the protected `ResolveResult::TypePtr()`
    // accessor, the D428 delegating-ctor pattern -- evaluated before `argument` is moved),
    // then stores the nullable `member` and the `parameter`, and derives
    // `ParameterName` from `parameter->Name()`. Both `parameter` and `argument` are
    // `assert`-guarded non-null (the D424 `ArgumentNullException`-to-`assert` convention);
    // `member` is NOT guarded (the C# field may be null).
    NamedArgumentResolveResult(const ILSpy::Decompiler::TypeSystem::IParameter* parameter,
                               std::shared_ptr<ResolveResult> argument,
                               const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member = nullptr)
        : ResolveResult(argument->TypePtr()),
          member_(member),
          parameter_(parameter) {
        assert(parameter && "NamedArgumentResolveResult: parameter must not be null");
        assert(argument && "NamedArgumentResolveResult: argument must not be null");
        parameterName_ = parameter->Name();
        argument_ = std::move(argument);
    }

    // Ctor 2: `(string parameterName, ResolveResult argument) : base(argument.Type)`.
    // Forwards the argument's type to the `ResolveResult` base, then stores the
    // `parameterName`. `Member` and `Parameter` are left at the `nullptr` default (their
    // default member initializers). The `argument` is `assert`-guarded non-null (the D424
    // convention); `parameterName` is a `std::string` (never null, so the C#
    // `ArgumentNullException` on it has no C++ assert counterpart -- the D432/D443
    // string-field convention).
    NamedArgumentResolveResult(std::string parameterName,
                               std::shared_ptr<ResolveResult> argument)
        : ResolveResult(argument->TypePtr()),
          parameterName_(std::move(parameterName)) {
        assert(argument && "NamedArgumentResolveResult: argument must not be null");
        argument_ = std::move(argument);
    }

    // The C# `readonly IParameterizedMember Member` -- the member to which the parameter
    // belongs. Returns a non-owning raw pointer; `nullptr` when the result was constructed
    // without a member (ctor 2, or ctor 1 with the default `member = null`).
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Member() const noexcept {
        return member_;
    }

    // The C# `readonly IParameter Parameter` -- the parameter. Returns a non-owning raw
    // pointer; `nullptr` when the result was constructed by ctor 2 (which does not set it).
    const ILSpy::Decompiler::TypeSystem::IParameter* Parameter() const noexcept {
        return parameter_;
    }

    // The C# `readonly string ParameterName` -- the parameter name. Returns a const
    // reference to the stored string (the faithful exposure of a readonly reference-type
    // field as a value).
    const std::string& ParameterName() const noexcept { return parameterName_; }

    // The C# `readonly ResolveResult Argument` -- the argument passed to the parameter.
    // Returns a non-owning raw pointer (the `shared_ptr` keeps the `ResolveResult` alive
    // for the `NamedArgumentResolveResult`'s lifetime); never null (the ctor `assert` pins
    // non-null), matching the D428 `ByReferenceResolveResult::ElementResult()` /
    // D431 `TypeIsResolveResult::Input()` accessor shape for a `shared_ptr<ResolveResult>`
    // member.
    ResolveResult* Argument() const noexcept { return argument_.get(); }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- `return new[]
    // { Argument }`: the single argument. The base default returns empty; the override
    // returns a one-element snapshot (the non-owning pointer, the `shared_ptr` keeping
    // the `ResolveResult` alive), the D443 `InterpolatedStringResolveResult` single-list
    // precedent applied to a one-element case.
    std::vector<const ResolveResult*> GetChildResults() const override {
        return { argument_.get() };
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the two non-owning raw pointers are copied, the
    // `parameterName_` string is value-copied, the `argument_` shared_ptr is shared, and
    // the base `type_` shared_ptr is shared). The C++ override reproduces this via the
    // default copy ctor, avoiding the C++-only slicing a non-overriding base clone would
    // perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<NamedArgumentResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "NamedArgumentResolveResult"; }

private:
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IParameter* parameter_ = nullptr;
    std::string parameterName_;
    std::shared_ptr<ResolveResult> argument_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_NAMEDARGUMENTRESOLVERESULT_HPP
