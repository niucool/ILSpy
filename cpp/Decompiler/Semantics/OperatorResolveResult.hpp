// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/OperatorResolveResult.cs -- the result of a
// unary / binary / ternary operator invocation. It is the twenty-seventh `Semantics` leaf
// toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker),
// deriving directly from `ResolveResult` (D424). All deps are already ported:
// `ResolveResult` (D424), `ExpressionType` (D448, the BCL
// `System.Linq.Expressions.ExpressionType` enum `OperatorType` returns), `IMethod` (D389,
// for the nullable `UserDefinedOperatorMethod`), and `IType` (D271, for the result type
// forwarded to the base).

#ifndef ILSPY_DECOMPILER_SEMANTICS_OPERATORRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_OPERATORRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem { class IMethod; }

namespace ILSpy::Decompiler::Semantics {

// The C# `public class OperatorResolveResult : ResolveResult` (NOT `sealed`) ports to a
// C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is:
//   * `readonly ExpressionType operatorType` -- the operator kind (the BCL enum).
//   * `readonly IMethod userDefinedOperatorMethod` -- the user-defined operator method
//     (`null` when this is a predefined operator).
//   * `readonly IList<ResolveResult> operands` -- the operator's operands.
//   * `readonly bool isLiftedOperator` -- whether this is a lifted operator (a nullable
//     overload applied to the underlying value type).
//   * Ctor 1 `(IType resultType, ExpressionType operatorType, params ResolveResult[] operands)`
//     -- the common ctor for a predefined operator; forwards `resultType` to the base,
//     stores the `operatorType` and the `operands`, and leaves `userDefinedOperatorMethod`
//     `null` and `isLiftedOperator` `false` (the C# field defaults).
//   * Ctor 2 `(IType resultType, ExpressionType operatorType, IMethod userDefinedOperatorMethod,
//     bool isLiftedOperator, IList<ResolveResult> operands)` -- the full ctor for a
//     user-defined / lifted operator.
//   * `ExpressionType OperatorType` / `IList<ResolveResult> Operands` /
//     `IMethod UserDefinedOperatorMethod` / `bool IsLiftedOperator` -- the getters.
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- returns `operands`.
//
// KEY PORT CONVENTIONS:
//  * The C# `ExpressionType operatorType` (the BCL `int`-backed enum, D448) ports to a
//    value member of the C++ `enum class ExpressionType` (a plain value, never null; the
//    D384/D403/D448 BCL-enum-absorption precedent). It is held by value and returned by
//    value from `OperatorType()`.
//  * The C# `IMethod userDefinedOperatorMethod` (a NULLABLE reference-type field the
//    ctor does NOT guard -- it returns `null` for a predefined operator) ports to a
//    non-owning `const IMethod*` raw pointer member defaulting to `nullptr` (the D437
//    `MemberResolveResult` non-owning `const IMember*` precedent; the D445
//    `ForEachResolveResult` nullable `CurrentProperty`/`MoveNextMethod` precedent). The
//    method is owned by the type system (the C# GC guarantee); the `OperatorResolveResult`
//    is a temporary resolution outcome that does not outlive it. No null guard (the C#
//    does not guard `userDefinedOperatorMethod`). `IMethod` is forward-declared (not
//    included) in the header -- a pointer member and a pointer-returning accessor to an
//    incomplete type need only a forward declaration (the D445 IProperty/IMethod
//    forward-declaration precedent).
//  * The C# `IList<ResolveResult> operands` (a non-null reference-type list both ctors
//    guard with `ArgumentNullException`) ports to a `std::vector<std::shared_ptr<ResolveResult>>`
//    member (the D438 `InvocationResolveResult` `Arguments` / D443
//    `InterpolatedStringResolveResult` `Arguments` precedent for a held `ResolveResult`
//    list: shared ownership so `ShallowClone`'s default copy ctor shares the elements
//    faithfully mirroring the C# `MemberwiseClone` reference-copy). A C++ `std::vector`
//    is NEVER "null" -- an empty vector is the faithful equivalent of a non-null EMPTY
//    `IList` (NOT a null `IList`) -- so the C# `operands == null` `ArgumentNullException`
//    guard has NO C++ counterpart (no assert; the D441 `ArrayCreateResolveResult` precedent).
//    The `Operands()` accessor returns a const reference to the stored shared_ptr vector
//    (the D438/D443 convention).
//  * The C# `bool isLiftedOperator` (a value field the C# defaults to `false` via the
//    ctor-1 omission) ports to a plain `bool` value member with a defaulted `= false`
//    member initializer so ctor 1 (which does not take it) leaves it `false` (the C445
//    `ForEachResolveResult` value-field precedent). `IsLiftedOperator()` returns it by
//    value.
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` returns `operands`
//    directly (a unary operator yields 1 child, a binary 2, a ternary 3). The base default
//    returns empty, but this override returns the operands. The snapshot is
//    `std::vector<const ResolveResult*>` (non-owning pointers, the D424 convention);
//    each operand is reached via `.get()` (the shared_ptr keeps the `ResolveResult` alive).
//  * `ToString` is inherited (the D425/D426/D427/D443 inherit-`ToString`-override-`ClassName`
//    convention): the C# does NOT override `ToString`, so overriding `ClassName()` to
//    "OperatorResolveResult" makes the inherited `ResolveResult::ToString` yield
//    "[OperatorResolveResult <resultType ReflectionName>]".
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C#
//    inherited `MemberwiseClone` preserves the runtime type, but a non-overriding C++
//    base clone (`make_unique<ResolveResult>(*this)`) would SLICE an `OperatorResolveResult`
//    to its base, so the override does `make_unique<OperatorResolveResult>(*this)` (the
//    default copy ctor copies the `operatorType_` value, copies the `userDefinedOperatorMethod_`
//    raw pointer, shares the `operands_` shared_ptr vector element-wise, and copies the
//    `isLiftedOperator_` bool, faithfully mirroring the C# reference-copy).
class OperatorResolveResult : public ResolveResult {
public:
    // Ctor 1: the common ctor for a predefined operator. Forwards `resultType` to the base
    // (the base `ResolveResult` ctor `assert`-guards it), stores the `operatorType` and
    // the `operands`, and leaves `userDefinedOperatorMethod_` `nullptr` and
    // `isLiftedOperator_` `false` (the C# field defaults via the ctor-1 omission). The C#
    // `operands == null` `ArgumentNullException` guard has NO C++ counterpart (a
    // `std::vector` is never null, the D441 precedent).
    OperatorResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr resultType,
                          ILSpy::Decompiler::TypeSystem::ExpressionType operatorType,
                          std::vector<std::shared_ptr<ResolveResult>> operands)
        : ResolveResult(std::move(resultType)),
          operatorType_(operatorType),
          operands_(std::move(operands)) {}

    // Ctor 2: the full ctor for a user-defined / lifted operator. Forwards `resultType`
    // to the base, stores all four fields. The C# `operands == null` guard has no C++
    // counterpart (the D441 precedent); `userDefinedOperatorMethod` is NOT guarded (the
    // C# does not guard it -- it may be `null` for a predefined operator).
    //
    // The trailing `methodKeepAlive` (defaulted null) is the port's lifetime bridge for
    // a RESOLUTION-TRANSIENT method: the C# `UserDefinedOperatorMethod` field is a GC
    // reference that keeps the method alive for the result's lifetime, but the port's
    // raw may point at a freshly-built `LiftedUserDefinedOperator` wrapper whose only
    // owner is the resolver's candidate list -- a shared holder over that list rides
    // the result, keeping the raw valid. The module-cached methods need no keep-alive
    // (the D515 borrow discipline -- the type system owns them).
    OperatorResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr resultType,
                          ILSpy::Decompiler::TypeSystem::ExpressionType operatorType,
                          const ILSpy::Decompiler::TypeSystem::IMethod* userDefinedOperatorMethod,
                          bool isLiftedOperator,
                          std::vector<std::shared_ptr<ResolveResult>> operands,
                          std::shared_ptr<const void> methodKeepAlive = nullptr)
        : ResolveResult(std::move(resultType)),
          operatorType_(operatorType),
          userDefinedOperatorMethod_(userDefinedOperatorMethod),
          isLiftedOperator_(isLiftedOperator),
          operands_(std::move(operands)),
          methodKeepAlive_(std::move(methodKeepAlive)) {}

    // The C# `ExpressionType OperatorType` -- the operator kind (the BCL enum). Returned by
    // value (a plain `enum class` value, never null).
    ILSpy::Decompiler::TypeSystem::ExpressionType OperatorType() const noexcept {
        return operatorType_;
    }

    // The C# `IList<ResolveResult> Operands` -- the operator's operands. Returns a const
    // reference to the stored shared_ptr vector (the faithful exposure of a readonly
    // reference-type field; the shared_ptr elements keep the `ResolveResult`s alive and are
    // shared with the caller's list).
    const std::vector<std::shared_ptr<ResolveResult>>& Operands() const noexcept {
        return operands_;
    }

    // The C# `IMethod UserDefinedOperatorMethod` -- the user-defined operator method
    // (`null` when this is a predefined operator). Returns a non-owning raw pointer; `nullptr`
    // for a predefined operator (ctor 1) or when ctor 2 was passed `nullptr`.
    const ILSpy::Decompiler::TypeSystem::IMethod* UserDefinedOperatorMethod() const noexcept {
        return userDefinedOperatorMethod_;
    }

    // The C# `bool IsLiftedOperator` -- whether this is a lifted operator.
    bool IsLiftedOperator() const noexcept { return isLiftedOperator_; }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- returns `operands`
    // directly (a unary operator yields 1 child, a binary 2, a ternary 3). The snapshot is
    // non-owning pointers (the shared_ptr keeps each `ResolveResult` alive).
    std::vector<const ResolveResult*> GetChildResults() const override {
        std::vector<const ResolveResult*> children;
        children.reserve(operands_.size());
        for (const auto& o : operands_)
            children.push_back(o.get());
        return children;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `operands_` shared_ptr vector is shared, the
    // `operatorType_`/`isLiftedOperator_` values are copied, the
    // `userDefinedOperatorMethod_` raw pointer is copied, the base `type_` shared_ptr is
    // shared). The C++ override reproduces this via the default copy ctor, avoiding the
    // C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<OperatorResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "OperatorResolveResult"; }

private:
    ILSpy::Decompiler::TypeSystem::ExpressionType operatorType_;
    const ILSpy::Decompiler::TypeSystem::IMethod* userDefinedOperatorMethod_ = nullptr;
    bool isLiftedOperator_ = false;
    std::vector<std::shared_ptr<ResolveResult>> operands_;
    // The ctor-2 lifetime bridge (null when the method is module-cached).
    std::shared_ptr<const void> methodKeepAlive_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_OPERATORRESOLVERESULT_HPP
