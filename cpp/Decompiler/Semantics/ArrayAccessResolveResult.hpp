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

// Port of ICSharpCode.Decompiler/Semantics/ArrayAccessResolveResult.cs -- the result of
// an array access expression `array[indexes]`. It is the fifteenth `Semantics` leaf toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker), deriving
// directly from `ResolveResult` (D424) and adding the accessed `Array` (a `ResolveResult`)
// and the `Indexes` (an `IList<ResolveResult>`). All deps are already ported: `ResolveResult`
// (D424) and `IType` (D271, for the element type forwarded to the base). The C# ctor guards
// `array` and `indexes` with `ArgumentNullException`; the `GetChildResults` override concats
// the `Array` with the `Indexes` (`new[] { Array }.Concat(Indexes)`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_ARRAYACCESSRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_ARRAYACCESSRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ArrayAccessResolveResult : ResolveResult` (NOT `sealed`) ports to a
// C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is:
//   * `readonly ResolveResult Array` -- the accessed array expression (non-null; the ctor
//     throws `ArgumentNullException` on null).
//   * `readonly IList<ResolveResult> Indexes` -- the index expressions (non-null; the ctor
//     throws `ArgumentNullException` on null; may be an empty list).
//   * Ctor `(IType elementType, ResolveResult array, IList<ResolveResult> indexes)
//     : base(elementType)` -- forwards the element type to the base and stores the array
//     and indexes (guarded with `ArgumentNullException`).
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- `new[] { Array
//     }.Concat(Indexes)`, i.e. the `Array` followed by each `Index`.
//
// KEY PORT CONVENTIONS:
//  * The C# `readonly ResolveResult Array` (a non-null reference-type field the ctor guards
//    with `ArgumentNullException`) ports to a `std::shared_ptr<ResolveResult>` member guarded
//    with `assert` (the D424 `Debug.Assert`/`ArgumentNullException`-to-`assert` precedent,
//    compiled out with NDEBUG). The `shared_ptr` shares ownership of the array expression
//    with the caller (the C# GC-owned reference), and `ShallowClone`'s default copy ctor
//    shares it faithfully mirroring the C# `MemberwiseClone` reference-copy.
//  * The C# `readonly IList<ResolveResult> Indexes` (a non-null reference-type list the ctor
//    guards with `ArgumentNullException`) ports to a `std::vector<std::shared_ptr<ResolveResult>>`
//    member (the D438 `InvocationResolveResult` `Arguments` precedent for a held
//    `ResolveResult` list: shared ownership so `ShallowClone`'s default copy ctor shares the
//    elements). A C++ `std::vector` passed by value is never "null" -- an empty vector is the
//    faithful equivalent of a non-null empty `IList` (NOT a null `IList`), so the C# `indexes
//    == null` `ArgumentNullException` guard has NO C++ counterpart (no assert); only the
//    `array` null guard ports to an `assert`.
//  * The `Array()` accessor returns a non-owning raw pointer (the D428
//    `ByReferenceResolveResult::ElementResult` / D431 `TypeIsResolveResult::Input` precedent
//    for a single held `ResolveResult` field); the `shared_ptr` keeps the `ResolveResult`
//    alive for the `ArrayAccessResolveResult`'s lifetime.
//  * The `Indexes()` accessor returns a const reference to the stored shared_ptr vector (the
//    D438 `InvocationResolveResult::Arguments` precedent for a held `ResolveResult` list),
//    exposing the readonly reference-type field faithfully.
//  * `ToString` is inherited (the D425/D426/D427 inherit-`ToString`-override-`ClassName`
//    convention): overriding `ClassName()` to "ArrayAccessResolveResult" makes the inherited
//    base `ResolveResult::ToString` yield "[ArrayAccessResolveResult <elementType>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C# inherited
//    `MemberwiseClone` preserves the runtime type, but a non-overriding C++ base clone
//    (`make_unique<ResolveResult>(*this)`) would SLICE an `ArrayAccessResolveResult` to its
//    base, so the override does `make_unique<ArrayAccessResolveResult>(*this)` (the default
//    copy ctor shares the `array_` shared_ptr and the `indexes_` shared_ptr vector faithfully
//    mirroring the C# reference-copy).
class ArrayAccessResolveResult : public ResolveResult {
public:
    // The C# ctor `(IType elementType, ResolveResult array, IList<ResolveResult> indexes)
    // : base(elementType)` -- forwards the element type to the `ResolveResult` base (which
    // asserts it non-null), then stores the `array` (asserted non-null) and `indexes` (a
    // non-null list, ported as a non-null-by-construction `std::vector`). The `array` assert
    // is in the body (the D424 assert-then-move convention: the base `ResolveResult` ctor
    // member-init runs first, then the body asserts `array` before the move into `array_`).
    ArrayAccessResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr elementType,
                             std::shared_ptr<ResolveResult> array,
                             std::vector<std::shared_ptr<ResolveResult>> indexes)
        : ResolveResult(std::move(elementType)) {
        assert(array && "ArrayAccessResolveResult: array must not be null");
        array_ = std::move(array);
        indexes_ = std::move(indexes);
    }

    // The C# `readonly ResolveResult Array` -- the accessed array expression. Returns a
    // non-owning raw pointer (the `shared_ptr` keeps the `ResolveResult` alive); never
    // null (the ctor asserts it).
    ResolveResult* Array() const noexcept { return array_.get(); }

    // The C# `readonly IList<ResolveResult> Indexes` -- the index expressions. Returns a
    // const reference to the stored shared_ptr vector (the faithful exposure of a readonly
    // reference-type list); may be empty (a non-null empty `IList`).
    const std::vector<std::shared_ptr<ResolveResult>>& Indexes() const noexcept {
        return indexes_;
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` --
    // `new[] { Array }.Concat(Indexes)`: the `Array` followed by each `Index`. The base
    // default returns empty; the override prepends the `Array` (via `array_.get()`) then
    // appends each index (via `indexes_[i].get()`, the shared_ptr keeping the `ResolveResult`
    // alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        std::vector<const ResolveResult*> children;
        children.push_back(array_.get());
        for (const auto& i : indexes_)
            children.push_back(i.get());
        return children;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `array_` shared_ptr is shared, the `indexes_` shared_ptr
    // vector is copied element-wise sharing each `ResolveResult`, and the base `type_`
    // shared_ptr is shared). The C++ override reproduces this via the default copy ctor,
    // avoiding the C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ArrayAccessResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "ArrayAccessResolveResult"; }

private:
    std::shared_ptr<ResolveResult> array_;
    std::vector<std::shared_ptr<ResolveResult>> indexes_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_ARRAYACCESSRESOLVERESULT_HPP
