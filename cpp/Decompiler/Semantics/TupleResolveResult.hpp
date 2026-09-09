// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/Semantics/TupleResolveResult.cs -- the resolve result
// for a C# 7 tuple literal `(int, string)` / `(int x, string y)`. It is the twenty-ninth
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker), deriving directly from `ResolveResult` (D424). All deps are
// already ported: `ResolveResult` (D424), `TupleType` (D405, the concrete `IType` the
// `GetTupleType` helper builds), `IType` (D271), `NoType()` (D433, the `SpecialType.NoType`
// singleton the None/Null-branch returns), `TypeKind` (D271, the `None`/`Null`
// discriminator).
//
// The C# ctor takes an `ICompilation` (threaded to the C# `TupleType` ctor's
// `CreateUnderlyingType` / `FindValueTupleType` recursion which resolves the
// `System.ValueTuple<...>` chain through the `valueTupleAssembly` module first and the
// compilation-wide `ICompilation.FindType` fallback second), a nullable
// `ImmutableArray<string> elementNames`, and a nullable `IModule valueTupleAssembly`.
// The D405 minimal `TupleType` port had DEFERRED that `ICompilation`-driven
// construction by accepting an ALREADY-BUILT `UnderlyingType`; the compilation-driven
// `CreateTupleType` factory has since landed, so the deferral is LIFTED and the ctor
// takes the full C# signature. The `GetTupleType` helper's None/Null early-return
// (which yields `SpecialType.NoType` when any element's type is `TypeKind.None` or
// `TypeKind.Null`) ports faithfully on top of it.

#ifndef ILSPY_DECOMPILER_SEMANTICS_TUPLERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_TUPLERESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"  // CreateTupleType (the lifted D405 deferral)

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class TupleResolveResult : ResolveResult` (NOT `sealed`) ports to a
// C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is:
//   * `readonly ImmutableArray<ResolveResult> Elements` -- the tuple's element results.
//   * Ctor `(ICompilation compilation, ImmutableArray<ResolveResult> elements,
//     ImmutableArray<string> elementNames = default, IModule valueTupleAssembly = null)`
//     -- forwards `GetTupleType(...)` to the base, stores the `elements`.
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- returns `Elements`.
//   * `static IType GetTupleType(...)` -- `SpecialType.NoType` when any element type is
//     `TypeKind.None` or `TypeKind.Null`, else `new TupleType(...)`.
//
// KEY PORT CONVENTIONS:
//  * The C# `ICompilation compilation` / `IModule valueTupleAssembly` pair (threaded
//    by `GetTupleType` to the `CreateTupleType` factory, which resolves the
//    `System.ValueTuple<...>` chain through the value-tuple-assembly definition first
//    and the compilation-wide `FindType` fallback second). The former D405 deferral
//    (a pre-built `underlyingType` ctor parameter) was lifted when `CreateTupleType`
//    landed; the None/Null early-return discards the pair exactly as before.
//  * The C# `ImmutableArray<ResolveResult> elements` (a non-null value-type array the
//    ctor does NOT guard with `ArgumentNullException` -- an `ImmutableArray` is a value
//    type, never null as a reference) ports to a `std::vector<std::shared_ptr<ResolveResult>>`
//    member (the D438 `InvocationResolveResult` `Arguments` / D443
//    `InterpolatedStringResolveResult` `Arguments` precedent for a held `ResolveResult`
//    list: shared ownership so `ShallowClone`'s default copy ctor shares the elements
//    faithfully mirroring the C# `MemberwiseClone` reference-copy). A `std::vector` is
//    never "null" -- an empty vector is the faithful equivalent of a non-null EMPTY
//    `ImmutableArray` -- so the C# has no null guard to port. The `Elements()` accessor
//    returns a const reference to the stored shared_ptr vector (the D438/D443 convention).
//  * The C# `ImmutableArray<string> elementNames = default(ImmutableArray<string>)` (a
//    NULLABLE value-type array; `default` is the uninitialized sentinel) ports to
//    `std::optional<std::vector<std::string>>` (`std::nullopt` = the C# `default` -- "not
//    provided"; a present vector = the C# non-null array -- the D441
//    `ArrayCreateResolveResult` nullable-`IReadOnlyList`-to-`optional-vector` precedent).
//    The `elementNames` is NOT stored (the C# stores only `Elements`); it is threaded to
//    `GetTupleType` which hands it to the `TupleType` ctor (a present vector is passed
//    through; `nullopt` is converted to an empty vector which the `TupleType` ctor fills
//    with empty strings matching the element count -- the D405 "not provided" sentinel).
//  * The `GetTupleType` static helper ports the C# `elements.Any(e => e.Type.Kind ==
//    TypeKind.None || e.Type.Kind == TypeKind.Null)` early-return as a loop over the
//    elements' `Type().Kind()`; the `SpecialType.NoType` return ports to the `NoType()`
//    inline helper (D433, `SpecialType(TypeKind::None)`). The else-branch builds the
//    `elementTypes` from the elements' `Type()` via the `shared_from_this` bridge (the
//    D437 `MemberResolveResult::ComputeType` pattern: `const_cast<IType&>(type).shared_from_this()`
//    -- the `IType` is `shared_ptr`-owned throughout the port, D271/D406) and constructs
//    the `TupleType` through `CreateTupleType` (the compilation-driven factory).
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` returns `Elements`
//    directly (a 2-tuple yields 2 children, a 3-tuple 3). The base default returns empty,
//    but this override returns the elements. The snapshot is
//    `std::vector<const ResolveResult*>` (non-owning pointers, the D424 convention);
//    each element is reached via `.get()` (the shared_ptr keeps the `ResolveResult` alive).
//  * `ToString` is inherited (the D425/D426/D427/D443 inherit-`ToString`-override-`ClassName`
//    convention): the C# does NOT override `ToString`, so overriding `ClassName()` to
//    "TupleResolveResult" makes the inherited `ResolveResult::ToString` yield
//    "[TupleResolveResult <tupleType ReflectionName>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C#
//    inherited `MemberwiseClone` preserves the runtime type, but a non-overriding C++
//    base clone (`make_unique<ResolveResult>(*this)`) would SLICE a `TupleResolveResult`
//    to its base, so the override does `make_unique<TupleResolveResult>(*this)` (the
//    default copy ctor shares the `elements_` shared_ptr vector element-wise and shares
//    the base `type_` shared_ptr, faithfully mirroring the C# reference-copy).
class TupleResolveResult : public ResolveResult {
public:
    // The C++ ctor: the C#-faithful signature `TupleResolveResult(ICompilation compilation,
    // ImmutableArray<ResolveResult> elements, ImmutableArray<string> elementNames = default,
    // IModule valueTupleAssembly = null)`: the compilation and the module (the assembly
    // defining `System.ValueTuple`, nullable) are threaded to `GetTupleType` which resolves
    // the underlying `System.ValueTuple<...>` chain through the ported `CreateTupleType`
    // factory (the value-tuple-assembly definition first, the compilation-wide lookup
    // fallback second -- the `FindValueTupleType` order). The `elementNames` defaults to
    // `std::nullopt` (the C# `default(ImmutableArray<string>)` "not provided" sentinel).
    // The `elements` vector is stored (shared ownership, the C# GC references the
    // caller's resolve results).
    TupleResolveResult(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                       std::vector<std::shared_ptr<ResolveResult>> elements,
                       std::optional<std::vector<std::string>> elementNames = std::nullopt,
                       const ILSpy::Decompiler::TypeSystem::IModule* valueTupleAssembly = nullptr)
        : ResolveResult(GetTupleType(compilation, elements, elementNames, valueTupleAssembly)),
          elements_(std::move(elements)) {}

    // The C# `ImmutableArray<ResolveResult> Elements` -- the tuple's element results.
    // Returns a const reference to the stored shared_ptr vector (the faithful exposure of
    // a readonly value-type field; the shared_ptr elements keep the `ResolveResult`s
    // alive and are shared with the caller's list).
    const std::vector<std::shared_ptr<ResolveResult>>& Elements() const noexcept {
        return elements_;
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- returns `Elements`
    // directly (a 2-tuple yields 2 children, a 3-tuple 3). The snapshot is non-owning
    // pointers (the shared_ptr keeps each `ResolveResult` alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        std::vector<const ResolveResult*> children;
        children.reserve(elements_.size());
        for (const auto& e : elements_)
            children.push_back(e.get());
        return children;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `elements_` shared_ptr vector is shared element-wise,
    // the base `type_` shared_ptr is shared). The C++ override reproduces this via the
    // default copy ctor, avoiding the C++-only slicing a non-overriding base clone would
    // perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<TupleResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "TupleResolveResult"; }

private:
    // The C# `static IType GetTupleType(ICompilation compilation,
    // ImmutableArray<ResolveResult> elements, ImmutableArray<string> elementNames,
    // IModule valueTupleAssembly)`: returns `SpecialType.NoType` when any element's type
    // is `TypeKind.None` or `TypeKind.Null`, else `new TupleType(...)`. The
    // `ICompilation` / `valueTupleAssembly` pair resolves the underlying
    // `System.ValueTuple<...>` chain through the ported `CreateTupleType` factory (the
    // value-tuple-assembly definition first, the compilation-wide lookup fallback --
    // the `FindValueTupleType` order the C# `TupleType` ctor spells).
    static ILSpy::Decompiler::TypeSystem::ITypePtr GetTupleType(
        const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
        const std::vector<std::shared_ptr<ResolveResult>>& elements,
        const std::optional<std::vector<std::string>>& elementNames,
        const ILSpy::Decompiler::TypeSystem::IModule* valueTupleAssembly) {
        namespace TS = ILSpy::Decompiler::TypeSystem;
        // The C# `elements.Any(e => e.Type.Kind == TypeKind.None || e.Type.Kind == TypeKind.Null)`.
        for (const auto& e : elements) {
            const auto kind = e->Type().Kind();
            if (kind == TS::TypeKind::None || kind == TS::TypeKind::Null)
                return TS::NoType();
        }
        // The C# `new TupleType(compilation, elements.Select(e => e.Type).ToImmutableArray(),
        // elementNames, valueTupleAssembly)` -- the compilation-driven construction
        // building the `System.ValueTuple<...>` chain at runtime.
        std::vector<TS::ITypePtr> elementTypes;
        elementTypes.reserve(elements.size());
        for (const auto& e : elements) {
            elementTypes.push_back(const_cast<TS::IType&>(e->Type()).shared_from_this());
        }
        return TS::CreateTupleType(compilation, std::move(elementTypes),
                                   elementNames, valueTupleAssembly);
    }

    std::vector<std::shared_ptr<ResolveResult>> elements_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_TUPLERESOLVERESULT_HPP