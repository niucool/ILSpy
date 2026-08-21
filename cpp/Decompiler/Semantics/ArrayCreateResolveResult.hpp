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
// PURPOSE NONINFRINGEMENT. AND IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/ArrayCreateResolveResult.cs -- the result of an
// array creation expression `new T[sizeArgs] { initializerElements }`. It is the seventeenth
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining
// blocker), deriving directly from `ResolveResult` (D424) and adding the `SizeArguments`
// (a non-null `IReadOnlyList<ResolveResult>`) and the `InitializerElements` (a NULLABLE
// `IReadOnlyList<ResolveResult>` -- null when no initializer was specified). All deps are
// already ported: `ResolveResult` (D424) and `IType` (D271, for the array type forwarded to
// the base). The C# ctor guards `sizeArguments` with `ArgumentNullException` (the
// `initializerElements` null is the documented "no initializer" state); the
// `GetChildResults` override concats the `SizeArguments` with the `InitializerElements`
// only when the latter is non-null.

#ifndef ILSPY_DECOMPILER_SEMANTICS_ARRAYCREATERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_ARRAYCREATERESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ArrayCreateResolveResult : ResolveResult` (NOT `sealed`) ports to a
// C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is:
//   * `readonly IReadOnlyList<ResolveResult> SizeArguments` -- the size arguments (non-null;
//     the ctor throws `ArgumentNullException` on null; may be an empty list).
//   * `readonly IReadOnlyList<ResolveResult> InitializerElements` -- the initializer
//     elements (NULLABLE: null when no initializer was specified).
//   * Ctor `(IType arrayType, IReadOnlyList<ResolveResult> sizeArguments,
//     IReadOnlyList<ResolveResult> initializerElements) : base(arrayType)` -- forwards the
//     array type to the base, stores the size arguments (guarded with
//     `ArgumentNullException`), and stores the initializer elements (null allowed).
//   * `override IEnumerable<ResolveResult> GetChildResults()` --
//     `InitializerElements != null ? SizeArguments.Concat(InitializerElements)
//     : SizeArguments`.
//
// KEY PORT CONVENTIONS:
//  * The C# `readonly IReadOnlyList<ResolveResult> SizeArguments` (a non-null
//    reference-type list the ctor guards with `ArgumentNullException`) ports to a
//    `std::vector<std::shared_ptr<ResolveResult>>` member (the D438
//    `InvocationResolveResult` `Arguments` / D439 `ArrayAccessResolveResult` `Indexes`
//    precedent for a held `ResolveResult` list: shared ownership so `ShallowClone`'s
//    default copy ctor shares the elements). A C++ `std::vector` passed by value is never
//    "null" -- an empty vector is the faithful equivalent of a non-null empty `IList`
//    (NOT a null `IList`), so the C# `sizeArguments == null` `ArgumentNullException` guard
//    has NO C++ counterpart (no assert).
//  * The C# `readonly IReadOnlyList<ResolveResult> InitializerElements` (a NULLABLE
//    reference-type list -- null when no initializer was specified, distinct from a
//    non-null empty list) ports to a `std::optional<std::vector<std::shared_ptr
//    <ResolveResult>>>` member (the D407 `TypeParameterSubstitution` nullable-
//    `IReadOnlyList`-to-`optional<vector>` precedent: `nullopt` is the C# `null` "no
//    initializer" state, a present empty vector is a non-null EMPTY initializer, and a
//    present non-empty vector is a populated initializer -- the three-way distinction
//    the `GetChildResults` `InitializerElements != null` gate relies on). `ShallowClone`'s
//    default copy ctor copies the `optional` (sharing each present `ResolveResult`).
//  * The C# `IType arrayType` (a non-null reference-type field the base `ResolveResult`
//    ctor guards with `ArgumentNullException`) ports to an `ITypePtr` member guarded with
//    `assert` (the D424 `ArgumentNullException`-to-`assert` precedent, in the base).
//  * The `SizeArguments()` accessor returns a const reference to the stored shared_ptr
//    vector (the D438 `Arguments` / D439 `Indexes` precedent); the `InitializerElements()`
//    accessor returns a const reference to the stored `optional` (exposing the nullable
//    list faithfully -- the caller tests `has_value()` for the C# `!= null` check).
//  * `ToString` is inherited (the D425/D426/D427/D439 inherit-`ToString`-override-
//    `ClassName` convention): overriding `ClassName()` to "ArrayCreateResolveResult" makes
//    the inherited base `ResolveResult::ToString` yield
//    "[ArrayCreateResolveResult <arrayType>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C# inherited
//    `MemberwiseClone` preserves the runtime type, but a non-overriding C++ base clone
//    (`make_unique<ResolveResult>(*this)`) would SLICE an `ArrayCreateResolveResult` to its
//    base, so the override does `make_unique<ArrayCreateResolveResult>(*this)` (the default
//    copy ctor shares the `sizeArguments_` shared_ptr vector, the
//    `initializerElements_` optional, and the base `type_` shared_ptr faithfully mirroring
//    the C# reference-copy).
class ArrayCreateResolveResult : public ResolveResult {
public:
    // The C# ctor `(IType arrayType, IReadOnlyList<ResolveResult> sizeArguments,
    // IReadOnlyList<ResolveResult> initializerElements) : base(arrayType)` -- forwards the
    // array type to the `ResolveResult` base (which asserts it non-null), then stores the
    // `sizeArguments` (a non-null list, ported as a non-null-by-construction `std::vector`)
    // and the `initializerElements` (nullable, ported as a `std::optional`). The base
    // ctor member-init runs first (the `arrayType` assert), then the body moves the lists
    // into the members (the D439 assert-then-move convention; no assert on the lists since
    // the `sizeArguments` null guard has no C++ counterpart and `initializerElements`
    // null is the documented "no initializer" state).
    ArrayCreateResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr arrayType,
                             std::vector<std::shared_ptr<ResolveResult>> sizeArguments,
                             std::optional<std::vector<std::shared_ptr<ResolveResult>>> initializerElements)
        : ResolveResult(std::move(arrayType)) {
        sizeArguments_ = std::move(sizeArguments);
        initializerElements_ = std::move(initializerElements);
    }

    // The C# `readonly IReadOnlyList<ResolveResult> SizeArguments` -- the size arguments.
    // Returns a const reference to the stored shared_ptr vector (the faithful exposure of a
    // readonly reference-type list); may be empty (a non-null empty `IList`).
    const std::vector<std::shared_ptr<ResolveResult>>& SizeArguments() const noexcept {
        return sizeArguments_;
    }

    // The C# `readonly IReadOnlyList<ResolveResult> InitializerElements` -- the initializer
    // elements, NULLABLE (null when no initializer was specified). Returns a const reference
    // to the stored `std::optional`; the caller tests `has_value()` for the C# `!= null`
    // check, and `->` / `*` to reach the present list.
    const std::optional<std::vector<std::shared_ptr<ResolveResult>>>& InitializerElements() const noexcept {
        return initializerElements_;
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` --
    // `InitializerElements != null ? SizeArguments.Concat(InitializerElements)
    // : SizeArguments`: each size argument followed by each initializer element when an
    // initializer is present, else just the size arguments. The base default returns empty;
    // the override appends each size argument (via `sizeArguments_[i].get()`) then, only when
    // the `initializerElements_` optional has a value, appends each initializer element (via
    // `(*initializerElements_)[i].get()`, the shared_ptr keeping each `ResolveResult` alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        std::vector<const ResolveResult*> children;
        for (const auto& s : sizeArguments_)
            children.push_back(s.get());
        if (initializerElements_.has_value()) {
            for (const auto& e : *initializerElements_)
                children.push_back(e.get());
        }
        return children;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `sizeArguments_` shared_ptr vector is copied
    // element-wise sharing each `ResolveResult`, the `initializerElements_` optional is
    // copied sharing each present `ResolveResult`, and the base `type_` shared_ptr is
    // shared). The C++ override reproduces this via the default copy ctor, avoiding the
    // C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ArrayCreateResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "ArrayCreateResolveResult"; }

private:
    std::vector<std::shared_ptr<ResolveResult>> sizeArguments_;
    std::optional<std::vector<std::shared_ptr<ResolveResult>>> initializerElements_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_ARRAYCREATERESOLVERESULT_HPP
