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
// PURPOSE NONINFRINGEMENT AND AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/InterpolatedStringResolveResult.cs -- the
// result of an interpolated-string expression `$"..."`. It is the twentieth `Semantics`
// leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker),
// deriving directly from `ResolveResult` (D424) and adding the `FormatString` (a `string`)
// and the `Arguments` (a `ResolveResult[]`). All deps are already ported: `ResolveResult`
// (D424) and `IType` (D271, for the `stringType` forwarded to the base). The C# ctor does
// NOT guard `formatString` or `arguments` with `ArgumentNullException` (the fields may be
// null); the `GetChildResults` override returns the `Arguments` directly.

#ifndef ILSPY_DECOMPILER_SEMANTICS_INTERPOLATEDSTRINGRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_INTERPOLATEDSTRINGRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class InterpolatedStringResolveResult : ResolveResult` (NOT `sealed`)
// ports to a C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is:
//   * `readonly string FormatString` -- the format string with the holes (`{0}`, `{1}`,
//     ...); may be null (the ctor does not guard it).
//   * `readonly ResolveResult[] Arguments` -- the interpolated arguments (a `params` array);
//     may be null (the ctor does not guard it).
//   * Ctor `(IType stringType, string formatString, params ResolveResult[] arguments)
//     : base(stringType)` -- forwards the string type to the base and stores the format
//     string and arguments (NO `ArgumentNullException` guards).
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- `return Arguments`.
//
// KEY PORT CONVENTIONS:
//  * The C# `readonly string FormatString` (a reference-type field the ctor does NOT guard
//    with `ArgumentNullException`) ports to a `std::string` value member (the D432
//    `ConstantResolveResult` string-field precedent: a C# `string` ported to `std::string`
//    is a value, never null; the C# null-string state has no faithful `std::string`
//    counterpart and is documented as a deviation). `ShallowClone`'s default copy ctor
//    value-copies it faithfully mirroring the C# `MemberwiseClone` reference-copy of the
//    string.
//  * The C# `readonly ResolveResult[] Arguments` (a `params` array the ctor does NOT guard
//    with `ArgumentNullException`) ports to a `std::vector<std::shared_ptr<ResolveResult>>`
//    member (the D438 `InvocationResolveResult` `Arguments` / D439 `Indexes` precedent for a
//    held `ResolveResult` list: shared ownership so `ShallowClone`'s default copy ctor
//    shares the elements). A C++ `std::vector` passed by value is never "null" -- an empty
//    vector is the faithful equivalent of a non-null empty array (NOT a null array), so the
//    C# null-array state has no C++ counterpart (no assert); only the base `stringType` null
//    guard ports to an `assert` (the inherited D424 convention).
//  * The `FormatString()` accessor returns the stored string by const reference (the
//    faithful exposure of a readonly reference-type field as a value).
//  * The `Arguments()` accessor returns a const reference to the stored shared_ptr vector
//    (the D438 `InvocationResolveResult::Arguments` precedent for a held `ResolveResult`
//    list), exposing the readonly reference-type field faithfully.
//  * `ToString` is inherited (the D425/D426/D427 inherit-`ToString`-override-`ClassName`
//    convention): overriding `ClassName()` to "InterpolatedStringResolveResult" makes the
//    inherited base `ResolveResult::ToString` yield "[InterpolatedStringResolveResult
//    <stringType>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C# inherited
//    `MemberwiseClone` preserves the runtime type, but a non-overriding C++ base clone
//    (`make_unique<ResolveResult>(*this)`) would SLICE an `InterpolatedStringResolveResult`
//    to its base, so the override does `make_unique<InterpolatedStringResolveResult>(*this)`
//    (the default copy ctor value-copies the `formatString_` and shares the `arguments_`
//    shared_ptr vector element-wise faithfully mirroring the C# reference-copy).
class InterpolatedStringResolveResult : public ResolveResult {
public:
    // The C# ctor `(IType stringType, string formatString, params ResolveResult[] arguments)
    // : base(stringType)` -- forwards the string type to the `ResolveResult` base (which
    // asserts it non-null), then stores the `formatString` (a `std::string` value, no null
    // guard per the C# which does not guard it) and the `arguments` (a non-null-by-
    // construction `std::vector`). Only the inherited base `stringType` assert applies.
    InterpolatedStringResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr stringType,
                                    std::string formatString,
                                    std::vector<std::shared_ptr<ResolveResult>> arguments)
        : ResolveResult(std::move(stringType)),
          formatString_(std::move(formatString)),
          arguments_(std::move(arguments)) {}

    // The C# `readonly string FormatString` -- the format string with the holes. Returns a
    // const reference to the stored string (the faithful exposure of a readonly
    // reference-type field as a value).
    const std::string& FormatString() const noexcept { return formatString_; }

    // The C# `readonly ResolveResult[] Arguments` -- the interpolated arguments. Returns a
    // const reference to the stored shared_ptr vector (the faithful exposure of a readonly
    // reference-type list); may be empty (a non-null empty array).
    const std::vector<std::shared_ptr<ResolveResult>>& Arguments() const noexcept {
        return arguments_;
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- `return Arguments`:
    // each interpolated argument, in order. The base default returns empty; the override
    // appends each argument (via `arguments_[i].get()`, the shared_ptr keeping the
    // `ResolveResult` alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        std::vector<const ResolveResult*> children;
        for (const auto& a : arguments_)
            children.push_back(a.get());
        return children;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `formatString_` string is value-copied, the `arguments_`
    // shared_ptr vector is copied element-wise sharing each `ResolveResult`, and the base
    // `type_` shared_ptr is shared). The C++ override reproduces this via the default copy
    // ctor, avoiding the C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<InterpolatedStringResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "InterpolatedStringResolveResult"; }

private:
    std::string formatString_;
    std::vector<std::shared_ptr<ResolveResult>> arguments_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_INTERPOLATEDSTRINGRESOLVERESULT_HPP
