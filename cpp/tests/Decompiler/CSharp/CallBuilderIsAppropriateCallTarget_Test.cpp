// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The CallBuilder::IsAppropriateCallTarget gate the overload-resolution helpers
// consult: the type-erasure identity arm, the CallVirt-override base-chain walk,
// the boxing-on-non-reference-type early rejection, and the non-override /
// non-CallVirt fall-throughs -- over name+declaring-type-equal method stubs (a
// FakeMethod reaches IMember through two subobjects, so the port's default
// pointer-identity Equals cannot be used for the base-chain walk) and a
// hand-wired base/derived type graph.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace IL = ::ILSpy::Decompiler::IL;
namespace CSharp = ::ILSpy::Decompiler::CSharp;
using ExpectedTargetDetails = CSharp::CallBuilder::ExpectedTargetDetails;

// A method stub with a settable `IsOverride` and a name+declaring-type equality
// (the C# `IMethod.Equals` semantics; a FakeMethod's default `Equals` is
// pointer-identity, which does not survive the base-chain walk's IMethod-path
// re-view).
class TestMethod : public Impl::FakeMethod {
public:
    explicit TestMethod(const TS::ICompilation& compilation)
        : FakeMethod(compilation, TS::SymbolKind::Method) {}

    void SetOverride(bool value) { isOverride_ = value; }
    bool IsOverride() const override { return isOverride_; }

    bool Equals(const TS::IMember* obj, const TS::TypeVisitor* typeNormalization)
        const override
    {
        (void)typeNormalization;
        return obj != nullptr && obj->Name() == Name()
            && obj->DeclaringType() == DeclaringType();
    }

private:
    bool isOverride_ = false;
};

// A LookupTypeDefinition whose `GetMethods` returns its wired method list (the
// stub's base `Methods()` surface, reachable through the `IType::GetMethods`
// virtual `GetBaseMembers` walks).
class MethodsTypeDefinition : public TS::TestSupport::LookupTypeDefinition {
public:
    MethodsTypeDefinition(const std::string& name, const TS::ICompilation& compilation,
                          const TS::IModule* parentModule)
        : LookupTypeDefinition(name, "N", TS::FullTypeName(
                                             TS::TopLevelTypeName("N", name, 0)),
                               TS::TypeKind::Class, TS::Accessibility::Public,
                               compilation, parentModule) {}

    std::vector<const TS::IMethod*> GetMethods(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override
    {
        (void)options;
        std::vector<const TS::IMethod*> result;
        for (const TS::IMethod* m : Methods()) {
            if (!filter || filter(m))
                result.push_back(m);
        }
        return result;
    }
};

struct Fixture {
    TS::SimpleCompilation compilation;
    Fixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    std::shared_ptr<TestMethod> MakeMethod(TS::ITypePtr declaringType, bool isOverride)
    {
        auto method = std::make_shared<TestMethod>(compilation);
        method->SetName("M");
        method->SetDeclaringType(std::move(declaringType));
        method->SetOverride(isOverride);
        return method;
    }

    std::shared_ptr<MethodsTypeDefinition> MakeType(const std::string& name)
    {
        return std::make_shared<MethodsTypeDefinition>(name, compilation,
                                                       &compilation.MainModule());
    }
};

// The IMember view of a test method (the FakeMember path).
const TS::IMember& AsMember(const Impl::FakeMember& member) { return member; }

} // namespace

TEST(CallBuilderIsAppropriateCallTargetTest, SameMemberIsAppropriate)
{
    Fixture fixture;
    auto method = fixture.MakeMethod(fixture.TypePtr(TS::KnownTypeCode::String), false);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;

    EXPECT_TRUE(CSharp::CallBuilder::IsAppropriateCallTarget(
        details, AsMember(*method), AsMember(*method)));
}

TEST(CallBuilderIsAppropriateCallTargetTest, WalkFindsTheBaseMember)
{
    Fixture fixture;
    auto expected = fixture.MakeMethod(fixture.TypePtr(TS::KnownTypeCode::String), false);
    auto baseType = fixture.MakeType("Base");
    baseType->SetMethods({expected.get()});
    auto derivedType = fixture.MakeType("Derived");
    derivedType->AddDirectBaseType(baseType);
    auto actual = fixture.MakeMethod(derivedType, /*isOverride=*/true);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;
    details.NeedsBoxingConversion = false;

    EXPECT_TRUE(CSharp::CallBuilder::IsAppropriateCallTarget(
        details, AsMember(*expected), AsMember(*actual)));
}

TEST(CallBuilderIsAppropriateCallTargetTest, BoxingConversionOnNonReferenceTypeIsRejected)
{
    Fixture fixture;
    auto expected = fixture.MakeMethod(fixture.TypePtr(TS::KnownTypeCode::String), false);
    auto baseType = fixture.MakeType("Base");
    baseType->SetMethods({expected.get()});
    auto derivedType = fixture.MakeType("Derived");
    derivedType->AddDirectBaseType(baseType);
    auto actual = fixture.MakeMethod(derivedType, /*isOverride=*/true);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;
    details.NeedsBoxingConversion = true;

    // The base walk would match, but the boxing gate rejects first (the declaring
    // type's `IsReferenceType()` is null, not `true`).
    EXPECT_FALSE(CSharp::CallBuilder::IsAppropriateCallTarget(
        details, AsMember(*expected), AsMember(*actual)));
}

TEST(CallBuilderIsAppropriateCallTargetTest, NonOverrideIsRejected)
{
    Fixture fixture;
    auto expected = fixture.MakeMethod(fixture.TypePtr(TS::KnownTypeCode::String), false);
    auto derivedType = fixture.MakeType("Derived");
    auto actual = fixture.MakeMethod(derivedType, /*isOverride=*/false);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;

    EXPECT_FALSE(CSharp::CallBuilder::IsAppropriateCallTarget(
        details, AsMember(*expected), AsMember(*actual)));
}

TEST(CallBuilderIsAppropriateCallTargetTest, NonCallVirtOverrideIsRejected)
{
    Fixture fixture;
    auto expected = fixture.MakeMethod(fixture.TypePtr(TS::KnownTypeCode::String), false);
    auto derivedType = fixture.MakeType("Derived");
    auto actual = fixture.MakeMethod(derivedType, /*isOverride=*/true);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::Call;

    EXPECT_FALSE(CSharp::CallBuilder::IsAppropriateCallTarget(
        details, AsMember(*expected), AsMember(*actual)));
}

TEST(CallBuilderIsAppropriateCallTargetTest, OverrideWithNoMatchingBaseMemberIsRejected)
{
    Fixture fixture;
    auto expected = fixture.MakeMethod(fixture.TypePtr(TS::KnownTypeCode::String), false);
    auto derivedType = fixture.MakeType("Derived");
    auto actual = fixture.MakeMethod(derivedType, /*isOverride=*/true);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;
    details.NeedsBoxingConversion = false;

    // The base-member walk finds no member named "M", so the call is rejected.
    EXPECT_FALSE(CSharp::CallBuilder::IsAppropriateCallTarget(
        details, AsMember(*expected), AsMember(*actual)));
}

} // namespace ILSpy::Tests
