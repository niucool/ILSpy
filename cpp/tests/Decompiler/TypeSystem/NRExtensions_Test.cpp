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

// Tests for the NRExtensions port (ICSharpCode.Decompiler/NRExtensions.cs): the
// compiler-generated/anonymous-type predicate family the CallBuilder
// anonymous-type arms consult.
//
// The tests pin:
//  (a) IsCompilerGenerated over the [CompilerGenerated] attribute answer and
//      the null-entity guard;
//  (b) HasGeneratedName's two overloads (the IMember '<' prefix, the IType
//      '<'/'$' generated-name classifier through SRMExtensions.IsGeneratedName);
//  (c) IsAnonymousType over the full shape (empty namespace + generated name
//      containing "AnonType"/"AnonymousType" + a [CompilerGenerated]
//      definition with only read-only properties) and every disqualifying arm
//      (a namespace, a non-generated name, a missing definition, a settable
//      property, a non-compiler-generated definition), plus the null guard;
//  (d) IsAnonymousTypeDeclaredAsNamedType (the VB declared-as-named-type arm:
//      the same shape but with a SETTABLE property);
//  (e) ContainsAnonymousType over direct and nested (type-argument) anonymous
//      types and a negative.

#include "Decompiler/NRExtensions.hpp"

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "tests/Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
using namespace ILSpy::Decompiler::TypeSystem;
using ILSpy::Decompiler::IsAnonymousType;
using ILSpy::Decompiler::IsAnonymousTypeDeclaredAsNamedType;
using ILSpy::Decompiler::ContainsAnonymousType;
using ILSpy::Decompiler::HasGeneratedName;
using ILSpy::Decompiler::IsCompilerGenerated;

// A LookupCompilation over a LookupTypeDefinition with the configurable name /
// namespace / attribute set / property list the predicates read.
struct DefinitionFixture {
    TestSupport::LookupCompilation compilation;
    std::shared_ptr<TestSupport::LookupTypeDefinition> definition;
    std::vector<std::shared_ptr<TestSupport::LookupProperty>> properties;

    DefinitionFixture(const char* ns, const char* name, bool compilerGenerated)
    {
        definition = std::make_shared<TestSupport::LookupTypeDefinition>(
            name, ns,
            TS::FullTypeName(TS::TopLevelTypeName(ns, name)), TypeKind::Class,
            Accessibility::Public, compilation, nullptr);
        if (compilerGenerated)
            definition->SetKnownAttributes({KnownAttribute::CompilerGenerated});
    }

    void AddProperty(bool canSet)
    {
        auto property = std::make_shared<TestSupport::LookupProperty>(
            "P" + std::to_string(properties.size()), definition, compilation);
        property->SetCanSet(canSet);
        properties.push_back(std::move(property));
    }

    std::vector<const IProperty*> PropertyPointers() const
    {
        std::vector<const IProperty*> result;
        for (const auto& p : properties)
            result.push_back(p.get());
        return result;
    }
};

} // namespace

TEST(NRExtensionsTest, IsCompilerGeneratedReadsTheAttribute)
{
    DefinitionFixture generated("", "<AnonType>0", true);
    EXPECT_TRUE(IsCompilerGenerated(generated.definition.get()));
    DefinitionFixture plain("NS", "Plain", false);
    EXPECT_FALSE(IsCompilerGenerated(plain.definition.get()));
    EXPECT_FALSE(IsCompilerGenerated(nullptr));
}

TEST(NRExtensionsTest, HasGeneratedNameChecksTheMemberAndTypeForms)
{
    TestSupport::LookupCompilation compilation;
    TestSupport::LookupMethod member("Foo", compilation);
    EXPECT_FALSE(HasGeneratedName(member));
    TestSupport::LookupMethod angleMember("<Foo>d__0", compilation);
    EXPECT_TRUE(HasGeneratedName(angleMember));
    // The IMember overload checks ONLY the '<' prefix: the VB '$'-separated
    // spelling passes the IType classifier (below) but not this one.
    TestSupport::LookupMethod dollarMember("VB$AnonymousType_0", compilation);
    EXPECT_FALSE(HasGeneratedName(dollarMember));

    // The IType overload routes through SRMExtensions.IsGeneratedName.
    DefinitionFixture plain("NS", "Plain", false);
    EXPECT_FALSE(HasGeneratedName(*plain.definition));
    DefinitionFixture generated("", "<AnonType>0", false);
    EXPECT_TRUE(HasGeneratedName(*generated.definition));
}

TEST(NRExtensionsTest, NullTypeIsNeverAnonymous)
{
    EXPECT_FALSE(IsAnonymousType(nullptr));
}

TEST(NRExtensionsTest, AnonymousTypeWithNamespaceIsRejected)
{
    DefinitionFixture fixture("NS", "<AnonType>0", true);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    EXPECT_FALSE(IsAnonymousType(fixture.definition.get()));
}

TEST(NRExtensionsTest, AnonymousTypeWithoutAGeneratedNameIsRejected)
{
    DefinitionFixture fixture("", "Anon", true);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    EXPECT_FALSE(IsAnonymousType(fixture.definition.get()));
}

TEST(NRExtensionsTest, AnonymousTypeWithoutTheAnonTokenIsRejected)
{
    DefinitionFixture fixture("", "<Generated>0", true);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    EXPECT_FALSE(IsAnonymousType(fixture.definition.get()));
}

TEST(NRExtensionsTest, AnonymousTypeWithSettablePropertyIsNotAnonymous)
{
    DefinitionFixture fixture("", "<AnonType>0", true);
    fixture.AddProperty(/*canSet=*/true);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    // Only the VB declared-as-named-type arm accepts the settable shape.
    EXPECT_FALSE(IsAnonymousType(fixture.definition.get()));
    EXPECT_TRUE(IsAnonymousTypeDeclaredAsNamedType(*fixture.definition));
}

TEST(NRExtensionsTest, AnonymousTypeWithoutCompilerGeneratedIsRejected)
{
    DefinitionFixture fixture("", "<AnonType>0", false);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    EXPECT_FALSE(IsAnonymousType(fixture.definition.get()));
    EXPECT_FALSE(IsAnonymousTypeDeclaredAsNamedType(*fixture.definition));
}

TEST(NRExtensionsTest, AnonymousTypeFullShapeAnswersTrue)
{
    DefinitionFixture fixture("", "<AnonType>0", true);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    EXPECT_TRUE(IsAnonymousType(fixture.definition.get()));
    EXPECT_TRUE(ContainsAnonymousType(*fixture.definition));
}

TEST(NRExtensionsTest, AnonymousTypeVBSpellingIsAccepted)
{
    DefinitionFixture fixture("", "VB$AnonymousType_0", true);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());
    EXPECT_TRUE(IsAnonymousType(fixture.definition.get()));
}

TEST(NRExtensionsTest, ContainsAnonymousTypeFindsDirectAndNested)
{
    DefinitionFixture fixture("", "<AnonType>0", true);
    fixture.AddProperty(/*canSet=*/false);
    fixture.definition->SetProperties(fixture.PropertyPointers());

    // A nested type argument: IEnumerable<AnonType> contains the anonymous
    // type through the type-argument recursion.
    TS::ITypePtr anonType = fixture.definition;
    TestSupport::LookupTypeDefinition enumerableDef(
        "IEnumerable`1", "System.Collections.Generic",
        TS::FullTypeName(TS::TopLevelTypeName(
            "System.Collections.Generic", "IEnumerable`1", 1)),
        TypeKind::Class, Accessibility::Public, fixture.compilation, nullptr);
    TS::ITypePtr listOfAnon = std::make_shared<TS::ParameterizedType>(
        TS::ITypePtr(&enumerableDef, [](TS::IType*) {}),
        std::vector<TS::ITypePtr>{anonType});
    EXPECT_TRUE(ContainsAnonymousType(*listOfAnon));

    // A negative: the wrapper over a non-anonymous type.
    TestSupport::LookupTypeDefinition intDef(
        "Int32", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "Int32")),
        TypeKind::Class, Accessibility::Public, fixture.compilation, nullptr);
    TS::ITypePtr listOfInt = std::make_shared<TS::ParameterizedType>(
        TS::ITypePtr(&enumerableDef, [](TS::IType*) {}),
        std::vector<TS::ITypePtr>{TS::ITypePtr(&intDef, [](TS::IType*) {})});
    EXPECT_FALSE(ContainsAnonymousType(*listOfInt));
}
