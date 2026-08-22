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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.cs -- the
// result of an invocation of a member of a `dynamic` object. This is the seventh
// `cpp/Decompiler/CSharp/Resolver/` leaf toward the `CSharpResolver` leaf deps (the
// long-pole remaining blocker of `TypeSystemAstBuilder` / `CSharpAmbience`), after the
// D467 twin Alias `ResolveResult` subclasses, the D468 twin enum leaves, the D469
// `DynamicMemberResolveResult`, and the D470 `AwaitResolveResult`. The C# source declares
// the `DynamicInvocationType` enum (Invocation / Indexing / ObjectCreation) in the same
// file, ahead of the class -- the C++ port co-locates it here (the C# file structure).
// All deps are already ported: `ResolveResult` (D424, the base), `SpecialType` /
// `TypeKind::Dynamic` (the `SpecialType.Dynamic` singleton the C# `: base(SpecialType.
// Dynamic)` forwards), and `IMember` (D387, for the nullable `Symbol`). The C# surface is:
//   * `DynamicInvocationType` -- a plain (non-`[Flags]`) `enum` with three members:
//     `Invocation` (0, 'a(b)'), `Indexing` (1, 'a[b]'), `ObjectCreation` (2, 'new a(b)').
//   * `DynamicInvocationResolveResult(ResolveResult target, DynamicInvocationType
//      invocationType, IList<ResolveResult> arguments, IList<ResolveResult>
//      initializerStatements = null, IMember symbol = null) : base(SpecialType.Dynamic)`
//      -- forwards the dynamic special type to the `ResolveResult` base; does NOT guard
//     `target` (may be null); stores `arguments ?? EmptyList<ResolveResult>.Instance` and
//     `initializerStatements ?? EmptyList<ResolveResult>.Instance` and `symbol`.
//   * `readonly ResolveResult Target` -- the invocation target (a dynamic expression or a
//     `MethodGroupResolveResult`); NOT a child per `GetChildResults` (the C# does NOT
//     override `GetChildResults`, so the inherited base default returns empty -- the
//     `Target` is held but not exposed as a child).
//   * `readonly DynamicInvocationType InvocationType` -- the invocation kind.
//   * `readonly IList<ResolveResult> Arguments` -- the call's arguments (named arguments
//     are `NamedArgumentResolveResult` instances); defaults to empty.
//   * `readonly IList<ResolveResult> InitializerStatements` -- the object/collection
//     initializer statements applied to the result (only for `ObjectCreation`); defaults
//     to empty.
//   * `readonly IMember Symbol` -- a synthesized member (a `dynamic` method on the
//     `dynamic` type) representing the invoked member; only set for an invoke-member
//     (`a.Method(b)`); null for a plain invoke or an indexer; may be null.
//   * `override string ToString()` -- `"[Dynamic invocation ]"` (a custom format, NOT the
//     inherited `ResolveResult::ToString` bracket form).
//
// KEY PORT CONVENTIONS:
//  * The C# `: base(SpecialType.Dynamic)` forwards the C# `SpecialType.Dynamic`
//    singleton to the `ResolveResult(IType)` base; the C++ port constructs
//    `std::make_shared<SpecialType>(TypeKind::Dynamic, true)` inline (the D469
//    `DynamicMemberResolveResult` precedent -- the `true` is the `isReferenceType: true`;
//    the name "dynamic" comes from `SpecialType::Name()`). The `ResolveResult` base ctor
//    `assert`-guards the type is non-null; `make_shared` returns a non-null `shared_ptr`.
//  * The C# `ResolveResult Target` (a reference-type field the ctor does NOT guard --
//    `target` may be null) ports to a `std::shared_ptr<ResolveResult>` (the D428
//    `ByReferenceResolveResult` / D469 `DynamicMemberResolveResult` `target_` precedent:
//    shared ownership so `ShallowClone`'s default copy ctor shares it; no assert since the
//    C# does not guard it).
//  * The C# `DynamicInvocationType InvocationType` (a plain `enum` value) ports to a
//    value member of the C++ `enum class DynamicInvocationType` (scoped, `int32`-backed,
//    the C# `int` default). Held by value and returned by value.
//  * The C# `IList<ResolveResult> Arguments` / `InitializerStatements` (reference-type
//    lists the ctor defaults to `EmptyList<ResolveResult>.Instance` via `??`) port to
//    `std::vector<std::shared_ptr<ResolveResult>>` members defaulted to `{}` (empty, the
//    D438 `InvocationResolveResult` `?? EmptyList`-to-empty-vector convention). The C++
//    `std::vector` is never "null" -- an empty vector is the faithful equivalent of a
//    non-null empty `IList` (NOT a null `IList`). The accessors return const references to
//    the stored shared_ptr vectors (the D438 convention).
//  * The C# `IMember Symbol` (a NULLABLE reference-type field the ctor defaults to
//    `null`) ports to a non-owning `const IMember*` raw pointer defaulting to `nullptr`
//    (the D469 `DynamicMemberResolveResult` `symbol_` precedent; the D437
//    `MemberResolveResult` non-owning-pointer convention). The member is owned by the
//    type system (the C# GC guarantee); `Symbol()` is never dereferenced by
//    `DynamicInvocationResolveResult` itself. `IMember` is forward-declared (not included)
//    in the header (the D445/D469 forward-declaration precedent).
//  * The C# does NOT override `GetChildResults` -- the inherited `ResolveResult` base
//    default returns empty. The C++ port faithfully does NOT override `GetChildResults`
//    (the `Target` is held but not exposed as a child; the base default returns `{}`). This
//    diverges from the `OperatorResolveResult` (D448) / `InvocationResolveResult` (D438)
//    which DO override `GetChildResults` to expose their held children -- the
//    `DynamicInvocationResolveResult` deliberately does not (the C# source does not).
//  * The C# `override string ToString()` uses a custom format
//    `string.Format(CultureInfo.InvariantCulture, "[Dynamic invocation ]")` -- NOT
//    `GetType().Name`. The C++ port overrides `ToString()` directly
//    (`return "[Dynamic invocation ]";`) rather than overriding only `ClassName()` (the
//    D424/D425 inherit-`ToString` convention does NOT apply since the C# overrides
//    `ToString` with a custom format; the D437 `MemberResolveResult` / D469
//    `DynamicMemberResolveResult` custom-`ToString` precedent). `ClassName()` is still
//    overridden to "DynamicInvocationResolveResult" for RTTI consistency, though the custom
//    `ToString` does not consult it.
//  * The C# does NOT override `IsError` -- the inherited base default returns `false`. The
//    C++ port faithfully does NOT override `IsError` (a dynamic invocation is never an
//    error per the `ResolveResult` base).
//  * The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
//    shallow-copies the fields. The C++ override reproduces this via the default copy ctor
//    (`make_unique<DynamicInvocationResolveResult>(*this)`), avoiding the C++-only slicing
//    a non-overriding base clone would perform (the D424 slicing-prevention convention).
//  * The class is NOT `final` (the C# is unsealed), pinned by `static_assert(!is_final)`.

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_DYNAMICINVOCATIONRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_DYNAMICINVOCATIONRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem { class IMember; }

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public enum DynamicInvocationType` (an `int`-backed enum, no `[Flags]`). The
// `enum class` (scoped, no implicit conversion to/from `int`, matching the C# enum's typed
// usage) is backed by `std::int32_t` (the C# `int` default). The member order and values
// mirror the C# exactly (each member's numeric value is its declaration index, 0..2).
enum class DynamicInvocationType : std::int32_t {
	Invocation,
	Indexing,
	ObjectCreation,
};

// The C# `public class DynamicInvocationResolveResult : ResolveResult` (NOT `sealed`)
// ports to a C++ subclass (NOT `final`) of `Semantics::ResolveResult`. The ctor forwards
// `std::make_shared<SpecialType>(TypeKind::Dynamic, true)` (the C# `SpecialType.Dynamic`
// singleton) to the `ResolveResult` base and stores the target (a `shared_ptr<ResolveResult>`
// child), the invocation type (the enum value), the arguments list, the initializer-
// statements list, and the optional symbol (a non-owning nullable `const IMember*`).
class DynamicInvocationResolveResult : public ILSpy::Decompiler::Semantics::ResolveResult {
public:
    DynamicInvocationResolveResult(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
                                   DynamicInvocationType invocationType,
                                   std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments = {},
                                   std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> initializerStatements = {},
                                   const ILSpy::Decompiler::TypeSystem::IMember* symbol = nullptr)
        : ResolveResult(std::make_shared<ILSpy::Decompiler::TypeSystem::SpecialType>(
              ILSpy::Decompiler::TypeSystem::TypeKind::Dynamic, true)),
          target_(std::move(target)),
          invocationType_(invocationType),
          arguments_(std::move(arguments)),
          initializerStatements_(std::move(initializerStatements)),
          symbol_(symbol) {}

    // The C# `ResolveResult Target` -- the invocation target (a dynamic expression or a
    // `MethodGroupResolveResult`). Returns a non-owning raw pointer (`target_.get()`);
    // `nullptr` when an empty `shared_ptr` was passed (the C# does not guard `target`).
    ILSpy::Decompiler::Semantics::ResolveResult* Target() const noexcept { return target_.get(); }

    // The C# `DynamicInvocationType InvocationType` -- the invocation kind. Returned by
    // value (a plain `enum class` value).
    DynamicInvocationType InvocationType() const noexcept { return invocationType_; }

    // The C# `IList<ResolveResult> Arguments` -- the call's arguments. Returns a const
    // reference to the stored shared_ptr vector (the faithful exposure of a readonly
    // reference-type field; the shared_ptr elements keep the `ResolveResult`s alive).
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& Arguments() const noexcept
    {
        return arguments_;
    }

    // The C# `IList<ResolveResult> InitializerStatements` -- the object/collection
    // initializer statements (only for `ObjectCreation`). Returns a const reference to the
    // stored shared_ptr vector; empty when no initializer statements were specified.
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& InitializerStatements() const noexcept
    {
        return initializerStatements_;
    }

    // The C# `IMember Symbol` -- a synthesized member representing the invoked member;
    // only set for an invoke-member (`a.Method(b)`); null for a plain invoke or an
    // indexer; may be null. Returns a non-owning raw pointer; `nullptr` when no symbol was
    // provided (the default). Never dereferenced by `DynamicInvocationResolveResult` itself.
    const ILSpy::Decompiler::TypeSystem::IMember* Symbol() const noexcept { return symbol_; }

    // The C# `override string ToString()` =>
    // `string.Format(CultureInfo.InvariantCulture, "[Dynamic invocation ]")`. A custom
    // format (NOT the inherited `ResolveResult::ToString` bracket form): the literal
    // `"[Dynamic invocation ]"` (a fixed string with no member-name substitution).
    std::string ToString() const override {
        return "[Dynamic invocation ]";
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `target_` shared_ptr is shared, the `invocationType_`
    // value is copied, the `arguments_`/`initializerStatements_` shared_ptr vectors are
    // shared element-wise, the `symbol_` raw pointer is copied, the base `type_`
    // shared_ptr is shared). The C++ override reproduces this via the default copy ctor,
    // avoiding the C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<DynamicInvocationResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields. Overridden for RTTI
    // consistency (the polymorphic-class-name convention); the custom `ToString` above does
    // NOT consult it (the C# `ToString` uses a fixed format, not `GetType().Name`).
    std::string ClassName() const override { return "DynamicInvocationResolveResult"; }

private:
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target_;
    DynamicInvocationType invocationType_;
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments_;
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> initializerStatements_;
    const ILSpy::Decompiler::TypeSystem::IMember* symbol_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_DYNAMICINVOCATIONRESOLVERESULT_HPP
