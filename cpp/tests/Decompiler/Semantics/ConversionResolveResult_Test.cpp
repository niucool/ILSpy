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

// Tests for the `ConversionResolveResult` port (D452) -- the resolve result for an
// implicit or explicit type conversion. It is the last unported `ResolveResult` subclass
// (the thirtieth `Semantics` leaf), deriving directly from `ResolveResult` (D424). The
// `Conversion` abstract base (D451) is the `Semantics` dependency; the concrete
// `Conversion` subclasses and static factories remain deferred, so the tests supply
// minimal test-local concrete `Conversion` stubs (a valid one keeping the `IsValid`
// default of `true` and an invalid one overriding it to `false`, mirroring the C#
// `InvalidConversion` / `None` singleton) to exercise the `IsError => !Conversion.IsValid`
// crux. The tests pin the ctor-stores-all-fields contract, the `IsError` valid-vs-invalid
// crux (direct + virtual dispatch through a base pointer), the `GetChildResults`-returns-
// single-`Input` crux, the `ToString` format, the `ShallowClone` runtime-type /
// shared-ownership / value-copy-of-`CheckForOverflow` preservation, the inherited
// `ResolveResult` defaults, and the class-shape `static_assert`s.

#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

// A minimal concrete `Conversion` keeping the base `IsValid` default of `true` (mirrors
// the C# `BuiltinConversion` / `IdentityConversion` family, all of which inherit the
// base `IsValid` default). Used to exercise the `IsError == false` path.
class TestValidConversion : public Conversion {};

// A minimal concrete `Conversion` overriding `IsValid` to `false` (mirrors the C#
// `InvalidConversion` / `None` singleton). Used to exercise the `IsError == true` path.
class TestInvalidConversion : public Conversion {
public:
    bool IsValid() const override { return false; }
};

ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr StringType() { return std::make_shared<KnownType>(KnownTypeCode::String); }

std::shared_ptr<ResolveResult> Int32Input() {
    return std::make_shared<TypeResolveResult>(Int32Type());
}

std::shared_ptr<Conversion> ValidConversion() {
    return std::make_shared<TestValidConversion>();
}

std::shared_ptr<Conversion> InvalidConversion() {
    return std::make_shared<TestInvalidConversion>();
}

} // namespace

TEST(ConversionResolveResultTest, CtorStoresAllFields) {
    auto input = Int32Input();
    auto conv = ValidConversion();
    ConversionResolveResult rr(StringType(), input, conv);
    EXPECT_EQ(rr.Input(), input.get());
    EXPECT_EQ(rr.ConversionProperty(), conv.get());
    // The 3-arg ctor defaults `CheckForOverflow` to `false` (the C# field default).
    EXPECT_FALSE(rr.CheckForOverflow());
}

TEST(ConversionResolveResultTest, CtorAcceptsStringTargetType) {
    // The `targetType` is forwarded to the `ResolveResult` base; `Type()` returns it.
    auto targetType = StringType();
    ConversionResolveResult rr(targetType, Int32Input(), ValidConversion());
    EXPECT_EQ(&rr.Type(), targetType.get());
}

TEST(ConversionResolveResultTest, FourArgCtorStoresCheckForOverflow) {
    auto input = Int32Input();
    auto conv = ValidConversion();
    ConversionResolveResult rr(StringType(), input, conv, true);
    EXPECT_EQ(rr.Input(), input.get());
    EXPECT_EQ(rr.ConversionProperty(), conv.get());
    EXPECT_TRUE(rr.CheckForOverflow());
}

TEST(ConversionResolveResultTest, FourArgCtorDefaultsCheckForOverflowToFalse) {
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion(), false);
    EXPECT_FALSE(rr.CheckForOverflow());
}

TEST(ConversionResolveResultTest, IsErrorFalseForValidConversion) {
    // A valid conversion (the base `IsValid` default of `true`) yields `IsError == false`.
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion());
    EXPECT_FALSE(rr.IsError());
}

TEST(ConversionResolveResultTest, IsErrorTrueForInvalidConversion) {
    // An invalid conversion (overriding `IsValid` to `false`, mirroring the C#
    // `InvalidConversion` / `None` singleton) yields `IsError == true` -- the
    // `!Conversion.IsValid` crux.
    ConversionResolveResult rr(StringType(), Int32Input(), InvalidConversion());
    EXPECT_TRUE(rr.IsError());
}

TEST(ConversionResolveResultTest, IsErrorDispatchesThroughBasePointer) {
    // The `IsError` virtual override dispatches through a `ResolveResult*` base pointer.
    auto validRR = std::make_unique<ConversionResolveResult>(
        StringType(), Int32Input(), ValidConversion());
    ResolveResult* validBase = validRR.get();
    EXPECT_FALSE(validBase->IsError());

    auto invalidRR = std::make_unique<ConversionResolveResult>(
        StringType(), Int32Input(), InvalidConversion());
    ResolveResult* invalidBase = invalidRR.get();
    EXPECT_TRUE(invalidBase->IsError());
}

TEST(ConversionResolveResultTest, GetChildResultsReturnsSingleInput) {
    auto input = Int32Input();
    ConversionResolveResult rr(StringType(), input, ValidConversion());
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    // The single child is the `Input` operand (pointer-identity).
    EXPECT_EQ(children[0], input.get());
}

TEST(ConversionResolveResultTest, GetChildResultsDispatchesThroughBasePointer) {
    auto input = Int32Input();
    auto rr = std::make_unique<ConversionResolveResult>(
        StringType(), input, ValidConversion());
    ResolveResult* base = rr.get();
    // The `GetChildResults` virtual override dispatches through the base pointer (the
    // base default is empty; the `ConversionResolveResult` override returns the single
    // `Input`).
    auto children = base->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], input.get());
}

TEST(ConversionResolveResultTest, ToStringReportsSubclassClassName) {
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion());
    // The inherited `ResolveResult::ToString` yields "[ConversionResolveResult
    // <ReflectionName>]" via the `ClassName()` override.
    auto str = rr.ToString();
    EXPECT_NE(str.find("ConversionResolveResult"), std::string::npos) << str;
    EXPECT_EQ(str.front(), '[');
    EXPECT_EQ(str.back(), ']');
}

TEST(ConversionResolveResultTest, ShallowClonePreservesRuntimeType) {
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion());
    auto clone = rr.ShallowClone();
    EXPECT_NE(dynamic_cast<ConversionResolveResult*>(clone.get()), nullptr);
}

TEST(ConversionResolveResultTest, ShallowCloneSharesInput) {
    auto input = Int32Input();
    ConversionResolveResult rr(StringType(), input, ValidConversion());
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<ConversionResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    // The `input_` `shared_ptr` is shared through the default copy ctor (faithfully
    // mirroring the C# `MemberwiseClone` reference-copy).
    EXPECT_EQ(cloned->Input(), input.get());
}

TEST(ConversionResolveResultTest, ShallowCloneSharesConversion) {
    auto conv = ValidConversion();
    ConversionResolveResult rr(StringType(), Int32Input(), conv);
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<ConversionResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    // The `conversion_` `shared_ptr` is shared through the default copy ctor.
    EXPECT_EQ(cloned->ConversionProperty(), conv.get());
}

TEST(ConversionResolveResultTest, ShallowCloneCopiesCheckForOverflow) {
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion(), true);
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<ConversionResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    // The `checkForOverflow_` bool is value-copied through the default copy ctor.
    EXPECT_TRUE(cloned->CheckForOverflow());
}

TEST(ConversionResolveResultTest, ShallowCloneIsDistinctInstance) {
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion());
    auto clone = rr.ShallowClone();
    EXPECT_NE(clone.get(), &rr);
}

TEST(ConversionResolveResultTest, ShallowCloneSharesTargetType) {
    auto targetType = StringType();
    ConversionResolveResult rr(targetType, Int32Input(), ValidConversion());
    auto clone = rr.ShallowClone();
    // The base `type_` `shared_ptr` is shared through the default copy ctor.
    EXPECT_EQ(&clone->Type(), targetType.get());
}

TEST(ConversionResolveResultTest, VirtualDispatchThroughBasePointer) {
    auto input = Int32Input();
    auto rr = std::make_unique<ConversionResolveResult>(
        StringType(), input, ValidConversion());
    ResolveResult* base = rr.get();
    // The `GetChildResults` virtual dispatches through the base pointer (returns the
    // single `Input`, not the base empty default).
    EXPECT_EQ(base->GetChildResults().size(), 1u);
    // The `ClassName` virtual dispatches to "ConversionResolveResult".
    auto str = base->ToString();
    EXPECT_NE(str.find("ConversionResolveResult"), std::string::npos) << str;
}

TEST(ConversionResolveResultTest, InheritedResolveResultDefaultsArePreserved) {
    ConversionResolveResult rr(StringType(), Int32Input(), ValidConversion());
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    // The `ConstantValue` default is an empty `std::any` (the C# `null`).
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

TEST(ConversionResolveResultTest, IsResolveResultSubclassAndNotFinal) {
    static_assert(std::is_base_of_v<ResolveResult, ConversionResolveResult>);
    static_assert(std::has_virtual_destructor_v<ConversionResolveResult>);
    static_assert(std::is_polymorphic_v<ConversionResolveResult>);
    // The C# `class ConversionResolveResult` (NOT `sealed`) ports to a C++ subclass (NOT
    // `final`); a future subclass may derive from it.
    static_assert(!std::is_final_v<ConversionResolveResult>);
}
