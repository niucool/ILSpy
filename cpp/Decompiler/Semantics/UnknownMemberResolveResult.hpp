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

// Port of ICSharpCode.Decompiler/Semantics/UnknownMemberResolveResult.cs -- the
// three `ResolveResult` subclasses that represent an unresolvable member /
// method / identifier. The C# file declares three classes:
//   * `UnknownMemberResolveResult : ResolveResult` -- an unknown member access
//     on a known target type (carries the target type, the member name, and the
//     type arguments); the `IsError` override is `true`.
//   * `UnknownMethodResolveResult : UnknownMemberResolveResult` -- an unknown
//     method call (adds the parameter list); inherits the `IsError => true`.
//   * `UnknownIdentifierResolveResult : ResolveResult` -- an unknown identifier
//     reference (carries the identifier string and the type-argument count); the
//     `IsError` override is `true`.
//
// This is the twenty-third `Semantics` leaf toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker), after the D424-D446
// `ResolveResult` subclasses. All deps are already ported: `ResolveResult`
// (D424), `IType` (D271, for the target type and type arguments), `IParameter`
// (D382, for the `UnknownMethodResolveResult` parameter list), and
// `SpecialType.UnknownType` (the `UnknownType()` convenience in `IType.hpp`,
// the D433 additive helper -- the C# `base(SpecialType.UnknownType)` forwards
// the `TypeKind::Unknown` null object to the `ResolveResult` base). The C#
// source declares the ctor plus the `IsError => true` override for the
// `UnknownMemberResolveResult` / `UnknownIdentifierResolveResult` classes and a
// forwarding ctor (no `IsError` override -- it inherits the base `true`) for
// `UnknownMethodResolveResult`; the C++ port additionally overrides
// `ClassName()` (the D424 polymorphic-`GetType().Name` convention the custom
// `ToString` reuses) and `ShallowClone()` (the D424 runtime-type-preserving-clone
// convention, avoiding the C++-only slicing a non-overriding base clone would
// perform).
//
// The `IsError => true` unconditional override is the load-bearing crux
// distinguishing an unknown member/identifier from a resolved one: the
// `ResolveResult` base default is `false`, but an unresolvable name is ALWAYS an
// error regardless of the stored target type or identifier, mirroring the D436
// `ErrorResolveResult` / D442 `AmbiguousResolveResult` crux (the unconditional-error
// `ResolveResult`). The two `ToString` overrides diverge from the base
// `"[<class-name> <type reflection name>]"` format because the member/identifier
// name must appear (the base `Type()` is the `UnknownType` null object, not the
// target), so the C# source overrides `ToString` directly -- the D432
// `ConstantResolveResult` precedent (override `ToString` directly AND keep the
// `ClassName()` override so a future subclass that inherits the custom `ToString`
// reports its own name).

#ifndef ILSPY_DECOMPILER_SEMANTICS_UNKNOWNMEMBERRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_UNKNOWNMEMBERRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
// Forward declaration: `IParameter` is held only as a non-owning pointer element
// of the `UnknownMethodResolveResult` parameter snapshot (never dereferenced in
// this header), so a forward declaration suffices and keeps the include graph
// minimal -- the real `IParameter.hpp` is pulled only by a TU that iterates the
// parameters (the `IEntity::ParentModule` / `ForEachResolveResult` nullable-
// /pointer-only-dep precedent applied to a vector-of-pointers element type).
class IParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::Semantics {

// The C# `public class UnknownMemberResolveResult : ResolveResult` (NOT
// `sealed` -- the C# class is unsealed, and `UnknownMethodResolveResult`
// derives from it) ports to a C++ subclass (NOT `final`) of `ResolveResult`.
// The C# surface is:
//   * `UnknownMemberResolveResult(IType targetType, string memberName,
//     IEnumerable<IType> typeArguments) : base(SpecialType.UnknownType)` --
//     forwards the `UnknownType` null object to the base (whose ctor guards it
//     with `ArgumentNullException` -> `assert`, the D424 convention) and stores
//     the `targetType` (assert-guarded -- the C# ctor guards it with
//     `ArgumentNullException`), the `memberName`, and the `typeArguments`
//     (snapshot via `ReadOnlyCollection`).
//   * `IType TargetType` / `string MemberName` / `ReadOnlyCollection<IType>
//     TypeArguments` -- the getters.
//   * `override bool IsError => true` -- the load-bearing crux.
//   * `override string ToString() => "[<class-name> <targetType>.<memberName>]"`.
class UnknownMemberResolveResult : public ResolveResult {
public:
    // The C# `UnknownMemberResolveResult(IType targetType, string memberName,
    // IEnumerable<IType> typeArguments) : base(SpecialType.UnknownType)`:
    // forwards the `UnknownType` null object (`TypeKind::Unknown`) to the
    // `ResolveResult` base (whose ctor asserts it non-null -- `UnknownType()` is
    // always non-null) and stores the three fields. The C# `targetType` (a
    // non-null `IType` the ctor guards with `ArgumentNullException`) ports to an
    // `ITypePtr` (the D271 shared-handle model) member guarded with `assert`
    // (the D424 `ArgumentNullException`-to-`assert` convention). The C# `string
    // memberName` ports to a `std::string` value (the D432/D443 string-field
    // precedent; never null, no guard). The C# `IEnumerable<IType>
    // typeArguments` (snapshotted into a `ReadOnlyCollection<IType>` via
    // `ToArray()`) ports to a `std::vector<ITypePtr>` taken by value (the
    // snapshot is copied into the member, the D438/D443 list-to-vector
    // convention).
    UnknownMemberResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr targetType,
                                std::string memberName,
                                std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments)
        : ResolveResult(ILSpy::Decompiler::TypeSystem::UnknownType()),
          targetType_(std::move(targetType)),
          memberName_(std::move(memberName)),
          typeArguments_(std::move(typeArguments)) {
        assert(targetType_ && "UnknownMemberResolveResult: targetType must not be null");
    }

    // The C# `IType TargetType` (non-null reference) ports to `const IType&`
    // returning `*targetType_` (the D374 non-null-reference convention). The
    // held `shared_ptr` keeps the `IType` alive for the result's lifetime. This
    // is DISTINCT from the inherited `Type()` which returns the base
    // `UnknownType` null object (the C# `this.Type` is `SpecialType.UnknownType`,
    // while `TargetType` is the configured target type) -- the
    // `TargetTypeIsDistinctFromBaseType` test pins this distinction.
    const ILSpy::Decompiler::TypeSystem::IType& TargetType() const noexcept { return *targetType_; }

    // The C# `string MemberName` ports to a `const std::string&` accessor (the
    // D446 `ParameterName` precedent); a `std::string` is a value, never null.
    const std::string& MemberName() const noexcept { return memberName_; }

    // The C# `ReadOnlyCollection<IType> TypeArguments` (a snapshot of the ctor's
    // `IEnumerable<IType>`) ports to `const std::vector<ITypePtr>&` (the D438
    // list-to-shared-ptr-vector convention); the snapshot shares ownership of
    // each type argument with the caller.
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& TypeArguments() const noexcept {
        return typeArguments_;
    }

    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // unknown member access is always an error, unconditionally (regardless of
    // the target type). This OVERRIDES the `ResolveResult` base default `false`.
    bool IsError() const override { return true; }

    // The C# `override string ToString() => string.Format(InvariantCulture,
    // "[{0} {1}.{2}]", GetType().Name, targetType, memberName)`: the custom debug
    // format including the target type and member name. The C++ port reproduces
    // it: `"[<class-name> <targetType reflection name>.<memberName>]"`. The
    // `ClassName()` helper (overridden below) supplies the polymorphic class
    // name (the C# `GetType().Name`); `targetType_->ReflectionName()` is the C#
    // `targetType.ToString()` (the `AbstractType.ToString` -> `ReflectionName`
    // convention). This OVERRIDES the base `ToString` directly (the custom
    // format diverges from the base `"[<class-name> <type reflection name>]"`
    // because the TARGET type, not the base `UnknownType`, must appear) -- the
    // D432 `ConstantResolveResult` precedent.
    std::string ToString() const override {
        return "[" + ClassName() + " " + targetType_->ReflectionName() + "." + memberName_ + "]";
    }

protected:
    // The C# `ToString` uses `GetType().Name` (polymorphic class name); the
    // inherited base `ToString` is overridden directly here (the custom format
    // diverges from the base), but the `ClassName()` override is kept so a future
    // subclass that inherits this `ToString` reports its own name (the D424
    // convention), and so the custom `ToString` reuses it without duplicating
    // the class-name string. `UnknownMethodResolveResult` overrides this to
    // report its own name in the inherited `ToString`.
    std::string ClassName() const override { return "UnknownMemberResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `UnknownMemberResolveResult` stays an
    // `UnknownMemberResolveResult` (not sliced to the `ResolveResult` base). The
    // C++ override reproduces this by constructing an `UnknownMemberResolveResult`
    // copy (the default copy ctor shares the base `type_` `shared_ptr` (the
    // `UnknownType` null object) and the own `targetType_` `shared_ptr`,
    // value-copies the `memberName_` string, and copies the `typeArguments_`
    // vector -- sharing each element `shared_ptr`, faithful to the C#
    // `MemberwiseClone`).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<UnknownMemberResolveResult>(*this);
    }

private:
    ILSpy::Decompiler::TypeSystem::ITypePtr targetType_;
    std::string memberName_;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments_;
};

// The C# `public class UnknownMethodResolveResult : UnknownMemberResolveResult`
// (NOT `sealed` -- the C# class is unsealed) ports to a C++ subclass (NOT
// `final`) of `UnknownMemberResolveResult`. The C# surface is a forwarding ctor
// (delegating to the base `UnknownMemberResolveResult` ctor) plus a
// `ReadOnlyCollection<IParameter> Parameters` getter; it does NOT override
// `IsError` (it inherits the base `true`) or `ToString` (it inherits the base
// custom format, reporting the method name via the inherited `ClassName()`).
// The C++ port additionally overrides `ClassName()` (so the inherited `ToString`
// reports "UnknownMethodResolveResult", not the base "UnknownMemberResolveResult")
// and `ShallowClone()` (the runtime-type-preservation convention).
class UnknownMethodResolveResult : public UnknownMemberResolveResult {
public:
    // The C# `UnknownMethodResolveResult(IType targetType, string methodName,
    // IEnumerable<IType> typeArguments, IEnumerable<IParameter> parameters) :
    // base(targetType, methodName, typeArguments)`: forwards the target type,
    // method name, and type arguments to the `UnknownMemberResolveResult` base
    // ctor (which forwards `UnknownType` to the `ResolveResult` base and
    // assert-guards the target type) and stores the `parameters`. The C#
    // `IEnumerable<IParameter> parameters` (snapshotted into a
    // `ReadOnlyCollection<IParameter>` via `ToArray()`) ports to a
    // `std::vector<const IParameter*>` (non-owning pointers, the D437/D445
    // non-owning-handle convention -- the type system owns the parameters, the
    // caller holds raw pointers) taken by value (the snapshot is copied into
    // the member).
    UnknownMethodResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr targetType,
                               std::string methodName,
                               std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
                               std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters)
        : UnknownMemberResolveResult(std::move(targetType), std::move(methodName),
                                     std::move(typeArguments)),
          parameters_(std::move(parameters)) {
    }

    // The C# `ReadOnlyCollection<IParameter> Parameters` (a snapshot of the
    // ctor's `IEnumerable<IParameter>`) ports to `const std::vector<const
    // IParameter*>&` (the D438 list-to-pointer-vector convention); the snapshot
    // is a non-owning view (the type system owns the parameters).
    const std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>& Parameters() const noexcept {
        return parameters_;
    }

protected:
    // The C# `ToString` is INHERITED from `UnknownMemberResolveResult` (the
    // custom `"[<class-name> <targetType>.<memberName>]"` format), but uses
    // `GetType().Name` which is polymorphic and yields "UnknownMethodResolveResult".
    // The C++ port reproduces this via the `ClassName()` override so the
    // inherited `ToString` reports the subclass name (not the base
    // "UnknownMemberResolveResult" the inherited `ClassName()` would yield).
    std::string ClassName() const override { return "UnknownMethodResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `UnknownMethodResolveResult` stays an
    // `UnknownMethodResolveResult` (not sliced to the `UnknownMemberResolveResult`
    // or `ResolveResult` base). The C++ override reproduces this by constructing
    // an `UnknownMethodResolveResult` copy (the default copy ctor shares the
    // base and target `shared_ptr`s and the `typeArguments_` vector elements,
    // value-copies the `memberName_` string, and copies the `parameters_`
    // non-owning pointer vector, faithful to the C# `MemberwiseClone`).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<UnknownMethodResolveResult>(*this);
    }

private:
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters_;
};

// The C# `public class UnknownIdentifierResolveResult : ResolveResult` (NOT
// `sealed` -- the C# class is unsealed) ports to a C++ subclass (NOT `final`)
// of `ResolveResult`. The C# surface is:
//   * `UnknownIdentifierResolveResult(string identifier, int typeArgumentCount =
//     0) : base(SpecialType.UnknownType)` -- forwards the `UnknownType` null
//     object to the base and stores the identifier and type-argument count.
//   * `string Identifier` / `int TypeArgumentCount` -- the getters.
//   * `override bool IsError => true` -- the load-bearing crux.
//   * `override string ToString() => "[<class-name> <identifier>]"`.
class UnknownIdentifierResolveResult : public ResolveResult {
public:
    // The C# `UnknownIdentifierResolveResult(string identifier, int
    // typeArgumentCount = 0) : base(SpecialType.UnknownType)`: forwards the
    // `UnknownType` null object (`TypeKind::Unknown`) to the `ResolveResult` base
    // and stores the two fields. The C# `string identifier` ports to a
    // `std::string` value (never null, no guard -- the D432/D443 string-field
    // precedent). The C# `int typeArgumentCount` (with the default `0`) ports to
    // an `int` with a C++ default arg `= 0` (the D426
    // `ThisResolveResult` defaulted-ctor-arg convention). The C# ctor does NOT
    // guard `identifier` with `ArgumentNullException` (the C# `string` may be
    // `null`), so the C++ port does NOT `assert` -- a `std::string` is never
    // null (the faithful null-handling distinct from the
    // `UnknownMemberResolveResult` target-type guard).
    UnknownIdentifierResolveResult(std::string identifier, int typeArgumentCount = 0)
        : ResolveResult(ILSpy::Decompiler::TypeSystem::UnknownType()),
          identifier_(std::move(identifier)),
          typeArgumentCount_(typeArgumentCount) {
    }

    // The C# `string Identifier` ports to a `const std::string&` accessor (the
    // D446 `ParameterName` precedent); a `std::string` is a value, never null.
    const std::string& Identifier() const noexcept { return identifier_; }

    // The C# `int TypeArgumentCount` ports to an `int` accessor (a scalar return
    // by value, the `TypeResolveResult` scalar precedent).
    int TypeArgumentCount() const noexcept { return typeArgumentCount_; }

    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // unknown identifier is always an error, unconditionally (regardless of the
    // identifier string). This OVERRIDES the `ResolveResult` base default `false`.
    bool IsError() const override { return true; }

    // The C# `override string ToString() => string.Format(InvariantCulture,
    // "[{0} {1}]", GetType().Name, identifier)`: the custom debug format
    // including the identifier. The C++ port reproduces it:
    // `"[<class-name> <identifier>]"`. The `ClassName()` helper (overridden
    // below) supplies the polymorphic class name (the C# `GetType().Name`). This
    // OVERRIDES the base `ToString` directly (the custom format diverges from
    // the base `"[<class-name> <type reflection name>]"` because the identifier,
    // not the base `UnknownType`, must appear) -- the D432
    // `ConstantResolveResult` precedent.
    std::string ToString() const override {
        return "[" + ClassName() + " " + identifier_ + "]";
    }

protected:
    // The C# `ToString` uses `GetType().Name` (polymorphic class name); the
    // inherited base `ToString` is overridden directly here (the custom format
    // diverges from the base), but the `ClassName()` override is kept so a future
    // subclass that inherits this `ToString` reports its own name (the D424
    // convention), and so the custom `ToString` reuses it without duplicating
    // the class-name string.
    std::string ClassName() const override { return "UnknownIdentifierResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `UnknownIdentifierResolveResult` stays an
    // `UnknownIdentifierResolveResult` (not sliced to the `ResolveResult` base).
    // The C++ override reproduces this by constructing an
    // `UnknownIdentifierResolveResult` copy (the default copy ctor shares the
    // base `type_` `shared_ptr` (the `UnknownType` null object) and
    // value-copies the `identifier_` string and the `typeArgumentCount_` int,
    // faithful to the C# `MemberwiseClone`).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<UnknownIdentifierResolveResult>(*this);
    }

private:
    std::string identifier_;
    int typeArgumentCount_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_UNKNOWNMEMBERRESOLVERESULT_HPP
