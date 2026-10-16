// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution`'s `CheckApplicability` first half (D510) -- the argument-count-per-
// parameter check, a `Detail::` free function. The C# `CheckApplicability(Candidate candidate)` (C# 4.0
// spec section 7.5.3.1 "Applicable function member") builds a per-parameter argument count from
// `candidate.ArgumentToParameterMap`, then for each parameter:
//   if (IsExpandedForm && i == last) continue;  // any number of args is fine for the params-array
//   if (count == 0) {
//     if (AllowOptionalParameters && Parameters[i].IsOptional) HasUnmappedOptionalParameters = true;
//     else AddError(MissingArgumentForRequiredParameter);
//   } else if (count > 1) {
//     AddError(MultipleArgumentsForSingleParameter);
//   }
// This is the self-contained half (no `CSharpConversions`/`TypeInference`); the second half (passing-mode
// + conversion check) is blocked on `CSharpConversions.ImplicitConversion` and deferred.
//
// The tests pin:
//  (a) every parameter gets exactly one argument -> no errors.
//  (b) a parameter gets no argument (not optional) -> `MissingArgumentForRequiredParameter`.
//  (c) a parameter gets no argument, but it IS optional, AllowOptional=true -> `HasUnmappedOptionalParameters`.
//  (d) a parameter gets no argument, optional, but AllowOptional=false -> `MissingArgumentForRequiredParameter`.
//  (e) two arguments map to the same parameter -> `MultipleArgumentsForSingleParameter`.
//  (f) the expanded form's last params-array param gets many args -> no error (skipped).

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::CheckApplicabilityArgumentCounts;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
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
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A minimal `IParameter` stub with configurable `IsOptional`/`IsParams`.
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, bool isOptional = false, bool isParams = false,
                            std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), isOptional_(isOptional), isParams_(isParams) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None;
    }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    std::string name_;
    ITypePtr type_;
    bool isOptional_;
    bool isParams_;
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

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

} // namespace

// ---------------------------------------------------------------------------
// Every parameter gets exactly one argument -> no errors.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityArgumentCountsTest, EachParamOneArgument) {
    auto p1 = std::make_shared<TestParameter>(Object());
    auto p2 = std::make_shared<TestParameter>(Object());
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0, 1};
    CheckApplicabilityArgumentCounts(c, /*allowOptionalParameters=*/true);
    EXPECT_EQ(c.ErrorCount(), 0);
    EXPECT_EQ(c.Errors(), OverloadResolutionErrors::None);
    EXPECT_FALSE(c.HasUnmappedOptionalParameters());
}

// ---------------------------------------------------------------------------
// A parameter gets no argument (not optional) -> `MissingArgumentForRequiredParameter`.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityArgumentCountsTest, MissingRequiredArgument) {
    auto p1 = std::make_shared<TestParameter>(Object());
    auto p2 = std::make_shared<TestParameter>(Object());
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};  // only arg 0 -> param 0; param 1 unmapped
    CheckApplicabilityArgumentCounts(c, /*allowOptionalParameters=*/true);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::MissingArgumentForRequiredParameter) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// A parameter gets no argument, IS optional, AllowOptional=true -> HasUnmappedOptionalParameters.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityArgumentCountsTest, OptionalUnmappedSetsFlag) {
    auto p1 = std::make_shared<TestParameter>(Object());
    auto p2 = std::make_shared<TestParameter>(Object(), /*isOptional=*/true);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};  // param 1 (optional) unmapped
    CheckApplicabilityArgumentCounts(c, /*allowOptionalParameters=*/true);
    EXPECT_EQ(c.ErrorCount(), 0);
    EXPECT_TRUE(c.HasUnmappedOptionalParameters());
}

// ---------------------------------------------------------------------------
// A parameter gets no argument, optional, but AllowOptional=false -> MissingArgumentForRequiredParameter.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityArgumentCountsTest, OptionalUnmappedDisallowOptionalIsError) {
    auto p1 = std::make_shared<TestParameter>(Object());
    auto p2 = std::make_shared<TestParameter>(Object(), /*isOptional=*/true);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0};  // param 1 (optional) unmapped
    CheckApplicabilityArgumentCounts(c, /*allowOptionalParameters=*/false);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::MissingArgumentForRequiredParameter) !=
                OverloadResolutionErrors::None);
    EXPECT_FALSE(c.HasUnmappedOptionalParameters());
}

// ---------------------------------------------------------------------------
// Two arguments map to the same parameter -> `MultipleArgumentsForSingleParameter`.
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityArgumentCountsTest, MultipleArgsOneParameter) {
    auto p1 = std::make_shared<TestParameter>(Object());
    auto p2 = std::make_shared<TestParameter>(Object());
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0, 0};  // both args -> param 0; param 1 unmapped
    CheckApplicabilityArgumentCounts(c, /*allowOptionalParameters=*/true);
    // Both errors fire: param 0 count 2 -> MultipleArgumentsForSingleParameter; param 1 count 0 ->
    // MissingArgumentForRequiredParameter. `ErrorCount` is 2 (each AddError makes it inapplicable).
    EXPECT_EQ(c.ErrorCount(), 2);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::MultipleArgumentsForSingleParameter) !=
                OverloadResolutionErrors::None);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::MissingArgumentForRequiredParameter) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// The expanded form's last params-array param gets many args -> no error (skipped).
// ---------------------------------------------------------------------------
TEST(CheckApplicabilityArgumentCountsTest, ExpandedParamsArrayAnyCount) {
    auto p1 = std::make_shared<TestParameter>(Object());
    auto p2 = std::make_shared<TestParameter>(Object(), /*isOptional=*/false, /*isParams=*/true);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/true);
    // 4 args: arg0->param0, args1-3->param1 (the params-array). param1 gets count 3 (>1 normally an
    // error), but the expanded form skips the last param, so no MultipleArgumentsForSingleParameter.
    c.ArgumentToParameterMap() = {0, 1, 1, 1};
    CheckApplicabilityArgumentCounts(c, /*allowOptionalParameters=*/true);
    EXPECT_EQ(c.ErrorCount(), 0);
    EXPECT_EQ(c.Errors(), OverloadResolutionErrors::None);
}
