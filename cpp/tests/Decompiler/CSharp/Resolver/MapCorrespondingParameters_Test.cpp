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

// Tests for `OverloadResolution`'s `MapCorrespondingParameters` helper (D509) -- the `Detail::` free
// function that maps each argument to a parameter (by position or by name), implementing the C# spec
// (draft-v11 section 12.6.2.2 "Corresponding parameters", incl. the non-trailing named-argument rule
// from C# 7.2). The C# `void MapCorrespondingParameters(Candidate candidate)`:
//   ArgumentToParameterMap = new int[arguments.Length];
//   hasPositionalArgument = false;
//   for (i = arguments.Length - 1; i >= 0; i--) {  // backwards
//     ArgumentToParameterMap[i] = -1;
//     if (argumentNames[i] == null || hasPositionalArgument) {
//       hasPositionalArgument = true;
//       if (i < ParameterTypes.Length) {
//         ArgumentToParameterMap[i] = i;
//         if (argumentNames[i] != null && argumentNames[i] != Parameters[i].Name)
//           AddError(NoParameterFoundForNamedArgument);
//       } else if (IsExpandedForm) {
//         ArgumentToParameterMap[i] = ParameterTypes.Length - 1;
//         if (argumentNames[i] != null) AddError(NoParameterFoundForNamedArgument);
//       } else { AddError(TooManyPositionalArguments); }
//     } else {  // trailing named argument
//       for j in Parameters: if (argumentNames[i] == Parameters[j].Name) ArgumentToParameterMap[i] = j;
//       if (ArgumentToParameterMap[i] < 0) AddError(NoParameterFoundForNamedArgument);
//     }
//   }
// The `arguments`/`argumentNames` are `OverloadResolution` ctor fields; the free function takes them
// as parameters (`argumentCount` + `argumentNames`, empty-string == positional).
//
// The tests pin:
//  (a) all-positional, count == ParameterTypes: map is [0,1,2,...]; no errors.
//  (b) too many positional args (count > ParameterTypes, not expanded): the overflow gets -1 and
//      `TooManyPositionalArguments` is set (ErrorCount == 1).
//  (c) expanded form, count > ParameterTypes: the overflow maps to the last param index.
//  (d) a trailing named arg matching a param name: maps to that param index.
//  (e) a trailing named arg with no matching param: maps to -1, sets `NoParameterFoundForNamedArgument`.
//  (f) a non-trailing named arg (followed by positional) whose name doesn't match: still maps by
//      position (index i), but sets `NoParameterFoundForNamedArgument`.

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

using ILSpy::Decompiler::CSharp::Resolver::Detail::MapCorrespondingParameters;
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

// A minimal `IParameter` stub with a configurable `Type`/`IsParams`/`Name`.
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p", bool isParams = false)
        : name_(std::move(name)), type_(std::move(type)), isParams_(isParams) {}

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
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
};

// A `LookupMethod` subclass with a configurable `Parameters` vector (read via `MemberDefinition()`).
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

// Empty argumentNames (all positional) of the given count.
std::vector<std::string> Positional(std::size_t n) { return std::vector<std::string>(n, std::string{}); }

} // namespace

// ---------------------------------------------------------------------------
// MapCorrespondingParameters: all-positional, count == ParameterTypes -> identity map, no errors.
// ---------------------------------------------------------------------------
TEST(MapCorrespondingParametersTest, AllPositionalIdentityMap) {
    auto p1 = std::make_shared<TestParameter>(Object(), "a");
    auto p2 = std::make_shared<TestParameter>(Object(), "b");
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    std::vector<std::string> names = Positional(2);
    MapCorrespondingParameters(c, 2, names);
    ASSERT_EQ(c.ArgumentToParameterMap().size(), 2u);
    EXPECT_EQ(c.ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c.ArgumentToParameterMap()[1], 1);
    EXPECT_EQ(c.ErrorCount(), 0);
    EXPECT_EQ(c.Errors(), OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// MapCorrespondingParameters: too many positional args, not expanded -> overflow gets -1 and
// `TooManyPositionalArguments` is set (ErrorCount == 1).
// ---------------------------------------------------------------------------
TEST(MapCorrespondingParametersTest, TooManyPositionalArgsNotExpanded) {
    auto p1 = std::make_shared<TestParameter>(Object(), "a");
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    std::vector<std::string> names = Positional(3);
    MapCorrespondingParameters(c, 3, names);
    ASSERT_EQ(c.ArgumentToParameterMap().size(), 3u);
    EXPECT_EQ(c.ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c.ArgumentToParameterMap()[1], -1);
    EXPECT_EQ(c.ArgumentToParameterMap()[2], -1);
    // `AddError(TooManyPositionalArguments)` is called for both overflow args (i=1, i=2).
    EXPECT_EQ(c.ErrorCount(), 2);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::TooManyPositionalArguments) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// MapCorrespondingParameters: expanded form, count > ParameterTypes -> overflow maps to the last
// param index.
// ---------------------------------------------------------------------------
TEST(MapCorrespondingParametersTest, ExpandedOverflowMapsToLastParam) {
    auto p1 = std::make_shared<TestParameter>(Object(), "a");
    auto p2 = std::make_shared<TestParameter>(Object(), "b", /*isParams=*/true);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/true);
    std::vector<std::string> names = Positional(4);
    MapCorrespondingParameters(c, 4, names);
    ASSERT_EQ(c.ArgumentToParameterMap().size(), 4u);
    EXPECT_EQ(c.ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c.ArgumentToParameterMap()[1], 1);
    EXPECT_EQ(c.ArgumentToParameterMap()[2], 1);  // overflow -> last param index (1)
    EXPECT_EQ(c.ArgumentToParameterMap()[3], 1);
    EXPECT_EQ(c.ErrorCount(), 0);
}

// ---------------------------------------------------------------------------
// MapCorrespondingParameters: a trailing named arg matching a param name -> maps to that index.
// ---------------------------------------------------------------------------
TEST(MapCorrespondingParametersTest, TrailingNamedMatchesParameter) {
    auto p1 = std::make_shared<TestParameter>(Object(), "a");
    auto p2 = std::make_shared<TestParameter>(Object(), "b");
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    // First arg positional, second arg named "b".
    std::vector<std::string> names{"", "b"};
    MapCorrespondingParameters(c, 2, names);
    ASSERT_EQ(c.ArgumentToParameterMap().size(), 2u);
    EXPECT_EQ(c.ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c.ArgumentToParameterMap()[1], 1);
    EXPECT_EQ(c.ErrorCount(), 0);
}

// ---------------------------------------------------------------------------
// MapCorrespondingParameters: a trailing named arg with no matching param -> maps to -1, sets
// `NoParameterFoundForNamedArgument`.
// ---------------------------------------------------------------------------
TEST(MapCorrespondingParametersTest, TrailingNamedNoMatch) {
    auto p1 = std::make_shared<TestParameter>(Object(), "a");
    auto p2 = std::make_shared<TestParameter>(Object(), "b");
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    std::vector<std::string> names{"", "x"};  // "x" matches no parameter
    MapCorrespondingParameters(c, 2, names);
    ASSERT_EQ(c.ArgumentToParameterMap().size(), 2u);
    EXPECT_EQ(c.ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c.ArgumentToParameterMap()[1], -1);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::NoParameterFoundForNamedArgument) !=
                OverloadResolutionErrors::None);
}

// ---------------------------------------------------------------------------
// MapCorrespondingParameters: a non-trailing named arg (followed by positional) whose name doesn't
// match its positional parameter -> still maps by position (index i), but sets
// `NoParameterFoundForNamedArgument` (the non-trailing-named-arg must-match rule).
// ---------------------------------------------------------------------------
TEST(MapCorrespondingParametersTest, NonTrailingNamedMismatchedName) {
    auto p1 = std::make_shared<TestParameter>(Object(), "a");
    auto p2 = std::make_shared<TestParameter>(Object(), "b");
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    // First arg named "z" (mismatched), second arg positional (makes "z" non-trailing-named).
    std::vector<std::string> names{"z", ""};
    MapCorrespondingParameters(c, 2, names);
    ASSERT_EQ(c.ArgumentToParameterMap().size(), 2u);
    EXPECT_EQ(c.ArgumentToParameterMap()[0], 0);  // maps by position (i < ParameterTypes)
    EXPECT_EQ(c.ArgumentToParameterMap()[1], 1);
    EXPECT_EQ(c.ErrorCount(), 1);
    EXPECT_TRUE((c.Errors() & OverloadResolutionErrors::NoParameterFoundForNamedArgument) !=
                OverloadResolutionErrors::None);
}
