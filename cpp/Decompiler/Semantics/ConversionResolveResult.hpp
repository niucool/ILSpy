// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/Semantics/ConversionResolveResult.cs -- the resolve
// result for an implicit or explicit type conversion. It is the last unported
// `ResolveResult` subclass (the thirtieth `Semantics` leaf), deriving directly from
// `ResolveResult` (D424). The `Conversion` abstract base (D451) is the `Semantics`
// dependency; `ConversionResolveResult.IsError` derives from `!Conversion.IsValid`
// (the D451 load-bearing crux the `Conversion` base's `IsValid` default of `true`
// serves). The concrete `Conversion` subclasses and static factories (the six nested
// sealed subclasses `InvalidConversion` / `NumericOrEnumerationConversion` /
// `BuiltinConversion` / `UserDefinedConv` / `MethodGroupConv` / `TupleConv` and the
// `None` / `IdentityConversion` / ... fields) remain deferred to a follow-up
// iteration; the tests supply minimal test-local concrete `Conversion` stubs (a valid
// one keeping the `IsValid` default and an invalid one overriding it to `false`) to
// exercise the `IsError` crux. The long-pole `CSharpAmbience` blocker remains
// `TypeSystemAstBuilder` (2782 C# lines plus `CSharpResolver` plus `Semantics`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_CONVERSIONRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_CONVERSIONRESOLVERESULT_HPP

#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ConversionResolveResult : ResolveResult` -- the resolve result for
// an implicit or explicit type conversion. The `Conversion` field describes the kind of
// conversion (numeric / reference / user-defined / ...); the `Input` field is the source
// expression's resolve result; the `CheckForOverflow` flag gates the numeric overflow
// check. The `IsError` override derives from `!Conversion.IsValid` (the D451 crux); the
// `GetChildResults` override returns the single `Input` operand.
class ConversionResolveResult : public ResolveResult {
public:
    // The common ctor (the C# 3-arg ctor): forwards `targetType` to the `ResolveResult`
    // base and stores the `input` and `conversion` fields. Both `input` and `conversion`
    // are guarded with `assert` per the C# `ArgumentNullException` (the D424
    // `ArgumentNullException`-to-`assert` convention; a `shared_ptr` is never null by
    // construction but the assert documents the contract and catches a misuse).
    ConversionResolveResult(
        ILSpy::Decompiler::TypeSystem::ITypePtr targetType,
        std::shared_ptr<ResolveResult> input,
        std::shared_ptr<Conversion> conversion)
        : ResolveResult(std::move(targetType))
        , input_(std::move(input))
        , conversion_(std::move(conversion)) {
        assert(input_ && "ConversionResolveResult: input must not be null");
        assert(conversion_ && "ConversionResolveResult: conversion must not be null");
    }

    // The 4-arg ctor (the C# 5-arg-ctor-with-`checkForOverflow`): forwards to the common
    // ctor and stores the `checkForOverflow` flag. The C# delegates to the 3-arg ctor
    // then sets the field; the C++ port mirrors this via a delegating ctor.
    ConversionResolveResult(
        ILSpy::Decompiler::TypeSystem::ITypePtr targetType,
        std::shared_ptr<ResolveResult> input,
        std::shared_ptr<Conversion> conversion,
        bool checkForOverflow)
        : ConversionResolveResult(std::move(targetType), std::move(input), std::move(conversion)) {
        checkForOverflow_ = checkForOverflow;
    }

    // The source expression's resolve result. A non-null `shared_ptr<ResolveResult>`
    // (the C# `readonly ResolveResult Input` field is guarded with `ArgumentNullException`
    // in the ctor). Exposed as a raw non-owning pointer (the `ConversionResolveResult`
    // owns the `input_` via the `shared_ptr`; the caller may share the handle via
    // `InputShared()` if it needs to keep the input alive).
    const ResolveResult* Input() const { return input_.get(); }

    // The owning `shared_ptr<ResolveResult>` handle behind `Input()` -- exposed so a
    // consumer (or a test) can share ownership of the input operand.
    const std::shared_ptr<ResolveResult>& InputShared() const { return input_; }

    // The conversion description. A non-null `shared_ptr<Conversion>` (the C# `readonly
    // Conversion Conversion` field is guarded with `ArgumentNullException` in the ctor).
    // Exposed as a raw non-owning pointer for the common read-only case.
    const Conversion* ConversionProperty() const { return conversion_.get(); }

    // The owning `shared_ptr<Conversion>` handle behind `ConversionProperty()`.
    const std::shared_ptr<Conversion>& ConversionShared() const { return conversion_; }

    // For numeric conversions, specifies whether overflow checking is enabled. Defaults
    // to `false` (the C# `bool CheckForOverflow` field defaults to `false` via the 3-arg
    // ctor, set to the configured value by the 4-arg ctor).
    bool CheckForOverflow() const { return checkForOverflow_; }

    // The C# `override bool IsError => !Conversion.IsValid` -- the load-bearing crux.
    // A valid conversion (e.g. the C# `IdentityConversion` / `BuiltinConversion` family,
    // all keeping the base `IsValid` default of `true`) yields `IsError == false`; an
    // invalid conversion (the C# `None` singleton / `InvalidConversion` overriding
    // `IsValid` to `false`) yields `IsError == true`.
    bool IsError() const override { return !conversion_->IsValid(); }

    // The C# `override IEnumerable<ResolveResult> GetChildResults() => new[] { Input }`
    // -- the single `Input` operand is the only child result.
    std::vector<const ResolveResult*> GetChildResults() const override {
        return { input_.get() };
    }

protected:
    // The runtime class name the C# `GetType().Name` yields in the inherited `ToString`.
    std::string ClassName() const override { return "ConversionResolveResult"; }

public:
    // The C# `ShallowClone` uses `MemberwiseClone` (shallow-copy preserving the runtime
    // type). The C++ port mirrors it via `std::make_unique<ConversionResolveResult>(*this)`
    // (the default copy ctor shares the `input_` / `conversion_` `shared_ptr` members and
    // copies the `checkForOverflow_` bool, faithful to the C# `MemberwiseClone`
    // reference-copy of the `Input` / `Conversion` fields and value-copy of the
    // `CheckForOverflow` field). A non-overriding C++ base clone would slice the subclass
    // to a `ResolveResult` base (diverging from the C# `MemberwiseClone` which preserves
    // the runtime type), so the override is required.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ConversionResolveResult>(*this);
    }

private:
    std::shared_ptr<ResolveResult> input_;
    std::shared_ptr<Conversion> conversion_;
    bool checkForOverflow_ = false;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_CONVERSIONRESOLVERESULT_HPP
