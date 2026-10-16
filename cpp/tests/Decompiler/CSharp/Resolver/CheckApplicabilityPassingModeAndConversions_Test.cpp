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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution`'s `CheckApplicability` second half (D536) -- the passing-mode +
// conversion check, a `Detail::` free function. The C# `CheckApplicability(Candidate candidate)`
// (C# 4.0 spec section 7.5.3.1 "Applicable function member") second half tests, for each argument:
//   * unmapped (parameterIndex < 0): `ArgumentConversions[i] = None`, continue.
//   * a `ByReferenceResolveResult`: its `ReferenceKind` must match the parameter's.
//   * an `OutVarResolveResult`: the parameter must be `Out` (else mismatch); `out var` skips the
//     conversion check (`continue`).
//   * by-value: the `AllowImplicitIn` / `IsExtensionMethodInvocation` implicit-`in` unwrap (a
//     `ByReferenceType` parameter type is unwrapped to its element); else a non-`None` `ReferenceKind`
//     is a `ParameterPassingModeMismatch`.
//   * then `conversions.ImplicitConversion(arguments[i], parameterType)` is stored; for an extension
//     method's first parameter, the conversion must be identity / implicit-reference / boxing /
//     implicit-span; otherwise an invalid non-user-defined non-method-group conversion to a
//     non-`Unknown` type is an `ArgumentTypeMismatch`.
// This half is now landable because `CSharpConversions.ImplicitConversion(ResolveResult, IType)`
// (D529) is ported; the first half (argument counts) landed as D510.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"  // ByReferenceType / KnownType / TypeKind
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::CheckApplicabilityPassingModeAndConversions;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::Semantics::ByReferenceResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::OutVarResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A minimal `IParameter` with a configurable type and `ReferenceKind` (the D524/D531
// `TestParameter` precedent; the cross-scope name-hiding crux means the enum references are
// fully-qualified with `::ILSpy::Decompiler::TypeSystem::`).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type,
                           ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                           std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::vector<const IParameter*> params, std::string name = "M")
        : LookupMethod(std::move(name), Compilation()), params_(std::move(params)) {}
    std::vector<const IParameter*> Parameters() const override { return params_; }
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
private:
    std::vector<const IParameter*> params_;
};

ITypePtr Def(KnownTypeCode ktc) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<KnownType>(ktc);
}

ITypePtr Int() { return Def(KnownTypeCode::Int32); }
ITypePtr Long() { return Def(KnownTypeCode::Int64); }
ITypePtr String() { return Def(KnownTypeCode::String); }

// A by-value `ResolveResult` over the supplied type (the faithful plain-ResolveResult argument;
// `IsCompileTimeConstant` is false, so the D528 dispatch falls through to the IType-based
// `ImplicitConversion` for the identity / numeric / None verdicts).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// A `ByReferenceType` over the supplied element (the `in`/`ref`/`ref readonly` parameter type the
// implicit-`in` unwrap strips).
ITypePtr ByRef(ITypePtr element) {
    return std::make_shared<ByReferenceType>(std::move(element));
}

} // namespace

// ---------------------------------------------------------------------------
// An unmapped argument (parameterIndex < 0) sets ArgumentConversions[i] = None and continues --
// no passing-mode check, no conversion call, no error.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, UnmappedArgumentSetsNoneAndContinues) {
    auto p = std::make_shared<TestParameter>(Int());
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {-1};  // unmapped
    c.ParameterTypes() = {Int()};
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    EXPECT_EQ(c.ArgumentConversions()[0].get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// A `ByReferenceResolveResult` whose `ReferenceKind` matches the parameter's -> no mismatch; the
// conversion is called (identity `ref int`->`ref int`) and stored.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ByReferenceMatchingReferenceKindNoError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::Ref);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    // The `ref int` parameter's type is `ByReferenceType(int)` (matching the argument's type).
    c.ParameterTypes() = {ByRef(Int())};
    CSharpConversions conversions(Compilation());
    auto arg = std::make_shared<ByReferenceResolveResult>(Int(), ReferenceKind::Ref);
    CheckApplicabilityPassingModeAndConversions(c, {arg}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    // The conversion IS called (identity `ref int`->`ref int`) and stored.
    EXPECT_NE(c.ArgumentConversions()[0].get(), Conversions::None().get());
    EXPECT_TRUE(c.ArgumentConversions()[0]->IsIdentityConversion());
}

// ---------------------------------------------------------------------------
// A `ByReferenceResolveResult` whose `ReferenceKind` does NOT match the parameter's ->
// `ParameterPassingModeMismatch` (the conversion is identity `ref int`->`ref int`, so no
// `ArgumentTypeMismatch` fires alongside).
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ByReferenceMismatchedReferenceKindIsError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::Out);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    // The `out int` parameter's type is `ByReferenceType(int)` (matching the argument's type so the
    // conversion is identity, isolating the passing-mode mismatch as the sole error).
    c.ParameterTypes() = {ByRef(Int())};
    CSharpConversions conversions(Compilation());
    auto arg = std::make_shared<ByReferenceResolveResult>(Int(), ReferenceKind::Ref);
    CheckApplicabilityPassingModeAndConversions(c, {arg}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::ParameterPassingModeMismatch) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// An `OutVarResolveResult` to an `Out` parameter -> no mismatch, no conversion check (continue).
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, OutVarWithOutParamNoError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::Out);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {Int()};
    CSharpConversions conversions(Compilation());
    auto arg = std::make_shared<OutVarResolveResult>(Int());
    CheckApplicabilityPassingModeAndConversions(c, {arg}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 0);
    // The `continue` means the conversion is NOT called -- ArgumentConversions[i] stays null.
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    EXPECT_EQ(c.ArgumentConversions()[0].get(), nullptr);
}

// ---------------------------------------------------------------------------
// An `OutVarResolveResult` to a non-`Out` parameter (`Ref`) -> `ParameterPassingModeMismatch`.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, OutVarWithNonOutParamIsError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::Ref);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {Int()};
    CSharpConversions conversions(Compilation());
    auto arg = std::make_shared<OutVarResolveResult>(Int());
    CheckApplicabilityPassingModeAndConversions(c, {arg}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::ParameterPassingModeMismatch) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// A by-value argument to an `in` parameter (with AllowImplicitIn) unwraps the `ByReferenceType`
// to its element, then calls the conversion on the element type (identity int->int) -> no error.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ImplicitInUnwrapsByReferenceType) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::In);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {ByRef(Int())};  // the `in` parameter's type is `ByReferenceType(int)`
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 0);
    // The unwrap rewrote ParameterTypes[0] to the element (int).
    ASSERT_FALSE(c.ParameterTypes().empty());
    EXPECT_EQ(c.ParameterTypes()[0]->Kind(), TypeKind::Struct);
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    EXPECT_TRUE(c.ArgumentConversions()[0]->IsIdentityConversion());
}

// ---------------------------------------------------------------------------
// A by-value argument to a `Ref` parameter (AllowImplicitIn does NOT apply to `Ref`) ->
// `ParameterPassingModeMismatch`.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ByValueToRefParamIsError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::Ref);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {Int()};
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::ParameterPassingModeMismatch) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// A by-value argument to a `None` parameter with an identity conversion (int->int) -> no error;
// the conversion is stored.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ByValueToNoneParamIdentityNoError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::None);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {Int()};
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    EXPECT_TRUE(c.ArgumentConversions()[0]->IsIdentityConversion());
}

// ---------------------------------------------------------------------------
// A by-value argument whose type is not implicitly convertible to the parameter type (int ->
// string) and the parameter type is not `Unknown` -> `ArgumentTypeMismatch`.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, IncompatibleTypeIsArgumentTypeMismatch) {
    auto p = std::make_shared<TestParameter>(String(), ReferenceKind::None);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {String()};  // the parameter type is `string` (a Class, not Unknown)
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::ArgumentTypeMismatch) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// A by-value argument whose conversion is invalid BUT the parameter type is `Unknown` -> no
// `ArgumentTypeMismatch` (the `Kind != Unknown` guard suppresses the error).
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, UnknownTypeSuppressesArgumentTypeMismatch) {
    auto unknown = ILSpy::Decompiler::TypeSystem::UnknownType();
    auto p = std::make_shared<TestParameter>(unknown, ReferenceKind::None);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {unknown};  // the parameter type is `Unknown`
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/false);
    // The conversion int->Unknown is invalid (None), but the Unknown-type guard suppresses the
    // ArgumentTypeMismatch -- no error.
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    EXPECT_EQ(c.ArgumentConversions()[0].get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// An extension method's first parameter with an identity conversion (int->int) -> no error (the
// extension-method first-param check accepts identity).
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ExtensionMethodFirstParamIdentityNoError) {
    auto p = std::make_shared<TestParameter>(Int(), ReferenceKind::None);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {Int()};
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/true);
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_EQ(c.ArgumentConversions().size(), 1u);
    EXPECT_TRUE(c.ArgumentConversions()[0]->IsIdentityConversion());
}

// ---------------------------------------------------------------------------
// An extension method's first parameter with a numeric-widening conversion (int->long, NOT
// identity/reference/boxing/span) -> `ArgumentTypeMismatch` (the extension-method first-param
// check is stricter than the normal check).
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityPassingModeAndConversionsTest, ExtensionMethodFirstParamNumericIsError) {
    auto p = std::make_shared<TestParameter>(Long(), ReferenceKind::None);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};
    c.ParameterTypes() = {Long()};
    CSharpConversions conversions(Compilation());
    CheckApplicabilityPassingModeAndConversions(c, {Arg(Int())}, conversions,
                                                /*allowImplicitIn=*/true,
                                                /*isExtensionMethodInvocation=*/true);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::ArgumentTypeMismatch) !=
                OverloadResolutionErrors::None);
}
