// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the TransformFieldAndConstructorInitializers analysis and mutation phases:
//   * `IsGeneratedPrimaryConstructorBackingField` (the `<x>P` + [CompilerGenerated] gate);
//   * `PropertyDeclaration::IsAutomaticProperty` (the previously deferred helper);
//   * `InitializerSequence::Analyze` / `IsMatch` (the leading-member-assignment walk and
//     the cross-constructor sequence match);
//   * `ConstructorInitializerAnalyzer::Analyze` (the constructor classification and the
//     static/instance initializer extraction); and
//   * `MoveConstructorInitializer` (the this/base-ctor call to constructor-initializer move).

#include "Decompiler/CSharp/Transforms/TransformFieldAndConstructorInitializers.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Impl = ILSpy::Decompiler::TypeSystem::Implementation;
namespace Transforms = ILSpy::Decompiler::CSharp::Transforms;
using ILSpy::Decompiler::CSharp::GetSymbol;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

// The per-test fixture: a fresh LookupCompilation plus the settings/ast builder the
// TransformContext requires (the IntroduceUsingDeclarations suite shape).
struct Fixture {
    LookupCompilation compilation;
    ILSpy::Decompiler::DecompilerSettings settings;
    std::shared_ptr<UsingScope> usingScope;
    ILSpy::Decompiler::DecompileRun run;
    std::shared_ptr<CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;
    std::vector<std::shared_ptr<void>> keepAlive;

    Fixture()
        : usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<CSharpTypeResolveContext>(compilation.MainModule())) {}

    std::shared_ptr<UsingScope> MakeScope() {
        return std::make_shared<UsingScope>(
            std::make_shared<CSharpTypeResolveContext>(compilation.MainModule()),
            compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    std::shared_ptr<LookupTypeDefinition> MakeType(const std::string& name) {
        return std::make_shared<LookupTypeDefinition>(
            name, std::string(), TS::FullTypeName(TS::TopLevelTypeName(std::string(), name, 0)),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr,
            TS::KnownTypeCode::None);
    }

    std::shared_ptr<Impl::FakeField> MakeField(const std::string& name) {
        auto field = std::make_shared<Impl::FakeField>(compilation);
        field->SetName(name);
        keepAlive.push_back(field);
        return field;
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A field whose `IsConst` / `HasAttribute` are configurable (FakeField fixes both).
class TestField final : public Impl::FakeField {
public:
    explicit TestField(const TS::ICompilation& compilation) : FakeField(compilation) {}
    bool Const = false;
    bool CompilerGenerated = false;

    bool IsConst() const override { return Const; }
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return CompilerGenerated
            && attribute == TS::KnownAttribute::CompilerGenerated;
    }
};

// Attaches the field as the member-reference's resolve-result symbol. The `FakeField`
// multiple-inheritance diamond makes the `IMember*` conversion ambiguous, so the helper
// takes the `FakeMember` base (the single `IMember` subobject path).
void ResolveField(Syntax::MemberReferenceExpression* memberReference,
                  const Impl::FakeMember* field) {
    memberReference->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, static_cast<const TS::IMember*>(field)));
}

// The `IMember*` view of a `FakeField` (the single-`IMember`-subobject cast).
const TS::IMember* AsMember(const Impl::FakeMember* field) {
    return static_cast<const TS::IMember*>(field);
}

// Builds `this.F = initializer;`.
Syntax::ExpressionStatement* ThisAssignment(const std::string& fieldName,
                                            const Impl::FakeMember* field,
                                            Syntax::Expression* initializer) {
    auto* memberReference = new Syntax::MemberReferenceExpression(
        new Syntax::ThisReferenceExpression(), fieldName);
    if (field != nullptr)
        ResolveField(memberReference, field);
    return new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(memberReference, initializer));
}

Syntax::IdentifierExpression* Ref(const char* name) {
    return new Syntax::IdentifierExpression(name);
}

} // namespace

// ===========================================================================
// IsGeneratedPrimaryConstructorBackingField
// ===========================================================================

TEST(TransformFieldAndConstructorInitializersTest, BackingFieldNeedsNameAndAttribute)
{
    Fixture fixture;
    TestField field(fixture.compilation);
    field.SetName("<x>P");

    field.CompilerGenerated = false;
    EXPECT_FALSE(
        Transforms::TransformFieldAndConstructorInitializers::IsGeneratedPrimaryConstructorBackingField(field));

    field.CompilerGenerated = true;
    EXPECT_TRUE(
        Transforms::TransformFieldAndConstructorInitializers::IsGeneratedPrimaryConstructorBackingField(field));
}

TEST(TransformFieldAndConstructorInitializersTest, BackingFieldRejectsOtherNames)
{
    Fixture fixture;
    TestField field(fixture.compilation);
    field.CompilerGenerated = true;

    field.SetName("xP");
    EXPECT_FALSE(
        Transforms::TransformFieldAndConstructorInitializers::IsGeneratedPrimaryConstructorBackingField(field));

    field.SetName("<x>Q");
    EXPECT_FALSE(
        Transforms::TransformFieldAndConstructorInitializers::IsGeneratedPrimaryConstructorBackingField(field));

    field.SetName("<x");
    EXPECT_FALSE(
        Transforms::TransformFieldAndConstructorInitializers::IsGeneratedPrimaryConstructorBackingField(field));
}

// ===========================================================================
// PropertyDeclaration.IsAutomaticProperty
// ===========================================================================

TEST(TransformFieldAndConstructorInitializersTest, PropertyWithoutAccessorBodiesIsAutomatic)
{
    Syntax::PropertyDeclaration property;
    EXPECT_TRUE(property.IsAutomaticProperty());

    property.Getter(new Syntax::Accessor());
    property.Setter(new Syntax::Accessor());
    EXPECT_TRUE(property.IsAutomaticProperty());
}

TEST(TransformFieldAndConstructorInitializersTest, PropertyWithAccessorBodyIsNotAutomatic)
{
    Syntax::PropertyDeclaration withGetterBody;
    auto* getter = new Syntax::Accessor();
    getter->Body(new Syntax::BlockStatement());
    withGetterBody.Getter(getter);
    EXPECT_FALSE(withGetterBody.IsAutomaticProperty());

    Syntax::PropertyDeclaration withSetterBody;
    auto* setter = new Syntax::Accessor();
    setter->Body(new Syntax::BlockStatement());
    withSetterBody.Setter(setter);
    EXPECT_FALSE(withSetterBody.IsAutomaticProperty());
}

// ===========================================================================
// InitializerSequence.Analyze / IsMatch
// ===========================================================================

TEST(TransformFieldAndConstructorInitializersTest, AnalyzeCollectsLeadingMemberAssignments)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto otherField = fixture.MakeField("G");
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    body->Statements().Add(ThisAssignment("G", otherField.get(), Ref("y")));

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;

    // The analyzer requires a symbol for the constructor; the sequence analysis itself only
    // needs the body and the field resolves.
    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);
    ctor.AddAnnotation(std::make_shared<Sem::MemberResolveResult>(nullptr, AsMember(ctorSym.get())));

    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, ctor, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    ASSERT_EQ(sequence->Statements.size(), 2u);
    EXPECT_EQ(sequence->Statements[0].Member, AsMember(field.get()));
    EXPECT_EQ(sequence->Statements[1].Member, AsMember(otherField.get()));
    // Two more statements (the second G assignment plus nothing) -- the body is exhausted
    // after the two assignments, so the sequence covers the full body.
    EXPECT_TRUE(sequence->CoversFullBody);
    EXPECT_FALSE(sequence->HasDuplicateAssignments);
}

TEST(TransformFieldAndConstructorInitializersTest, AnalyzeStopsAtNonAssignment)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    body->Statements().Add(new Syntax::ExpressionStatement(Ref("Other")));

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, ctor, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    ASSERT_EQ(sequence->Statements.size(), 1u);
    EXPECT_FALSE(sequence->CoversFullBody);
}

TEST(TransformFieldAndConstructorInitializersTest, AnalyzeDetectsDuplicateAssignments)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("y")));

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, ctor, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    ASSERT_EQ(sequence->Statements.size(), 2u);
    EXPECT_TRUE(sequence->HasDuplicateAssignments);
}

TEST(TransformFieldAndConstructorInitializersTest, AnalyzeCoversFullBodyWithCtorCallTail)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::InvocationExpression(new Syntax::MemberReferenceExpression(
            new Syntax::ThisReferenceExpression(), std::string(".ctor")))));

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, ctor, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    ASSERT_EQ(sequence->Statements.size(), 1u);
    EXPECT_TRUE(sequence->CoversFullBody);
}

TEST(TransformFieldAndConstructorInitializersTest, AnalyzeSkipsNonConstForStaticConstructor)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    ctorSym->SetIsStatic(true);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, ctor, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    // The non-const field assignment is skipped, so the sequence stays empty and does not
    // cover the body.
    EXPECT_TRUE(sequence->Statements.empty());
    EXPECT_FALSE(sequence->CoversFullBody);
}

TEST(TransformFieldAndConstructorInitializersTest, IsMatchAcceptsEqualSequences)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    auto makeCtor = [&](Syntax::ConstructorDeclaration& ctor) {
        auto* body = new Syntax::BlockStatement();
        ctor.Body(body);
        body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    };

    Syntax::ConstructorDeclaration first;
    makeCtor(first);
    Syntax::ConstructorDeclaration second;
    makeCtor(second);

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, first, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    EXPECT_TRUE(sequence->IsMatch(second));
}

TEST(TransformFieldAndConstructorInitializersTest, IsMatchRejectsDifferentField)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto other = fixture.MakeField("G");
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration first;
    {
        auto* body = new Syntax::BlockStatement();
        first.Body(body);
        body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    }
    Syntax::ConstructorDeclaration second;
    {
        auto* body = new Syntax::BlockStatement();
        second.Body(body);
        body->Statements().Add(ThisAssignment("G", other.get(), Ref("x")));
    }

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    auto sequence = Transforms::TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
        analyzer, first, *ctorSym);
    ASSERT_TRUE(sequence.has_value());
    EXPECT_FALSE(sequence->IsMatch(second));
}

// ===========================================================================
// ConstructorInitializerAnalyzer.Analyze
// ===========================================================================

TEST(TransformFieldAndConstructorInitializersTest, AnalyzerFindsInstanceInitializers)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    auto* ctor = new Syntax::ConstructorDeclaration();
    auto* body = new Syntax::BlockStatement();
    ctor->Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);
    ctor->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(nullptr, AsMember(ctorSym.get())));

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    ASSERT_TRUE(analyzer.Analyze({ctor}));
    ASSERT_EQ(analyzer.InstanceConstructors.size(), 1u);
    EXPECT_TRUE(analyzer.InstanceInitializers.has_value());
    EXPECT_FALSE(analyzer.StaticInitializers.has_value());
}

TEST(TransformFieldAndConstructorInitializersTest, AnalyzerFindsStaticInitializers)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto field = fixture.MakeField("F");
    auto type = fixture.MakeType("C");

    auto* ctor = new Syntax::ConstructorDeclaration();
    auto* body = new Syntax::BlockStatement();
    ctor->Body(body);
    body->Statements().Add(ThisAssignment("F", field.get(), Ref("x")));
    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    ctorSym->SetIsStatic(true);
    fixture.keepAlive.push_back(ctorSym);
    ctor->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(nullptr, AsMember(ctorSym.get())));

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    ASSERT_TRUE(analyzer.Analyze({ctor}));
    EXPECT_TRUE(analyzer.StaticInitializers.has_value());
    EXPECT_FALSE(analyzer.InstanceInitializers.has_value());
}

// ===========================================================================
// MoveConstructorInitializer
// ===========================================================================

TEST(TransformFieldAndConstructorInitializersTest, MoveThisConstructorCallToInitializer)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(new Syntax::ThisReferenceExpression(),
                                              std::string(".ctor")));
    auto* statement = new Syntax::ExpressionStatement(invocation);
    body->Statements().Add(statement);

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);
    invocation->AddAnnotation(std::make_shared<Sem::InvocationResolveResult>(nullptr,
                                                                              ctorSym.get()));

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    ASSERT_TRUE(analyzer.MoveConstructorInitializer(ctor, *ctorSym));
    ASSERT_NE(ctor.Initializer(), nullptr);
    EXPECT_EQ(ctor.Initializer()->ConstructorInitializerType(),
              Syntax::ConstructorInitializerType::This);
    EXPECT_EQ(body->Statements().Count(), 0);
}

TEST(TransformFieldAndConstructorInitializersTest, MoveBaseConstructorCallWithArguments)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto type = fixture.MakeType("Derived");
    auto baseType = fixture.MakeType("Base");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(new Syntax::BaseReferenceExpression(),
                                              std::string(".ctor")));
    invocation->Arguments().Add(Ref("arg"));
    auto* statement = new Syntax::ExpressionStatement(invocation);
    body->Statements().Add(statement);

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    auto baseCtorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    baseCtorSym->SetDeclaringType(baseType);
    fixture.keepAlive.push_back(ctorSym);
    fixture.keepAlive.push_back(baseCtorSym);
    invocation->AddAnnotation(std::make_shared<Sem::InvocationResolveResult>(nullptr,
                                                                              baseCtorSym.get()));

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    ASSERT_TRUE(analyzer.MoveConstructorInitializer(ctor, *ctorSym));
    ASSERT_NE(ctor.Initializer(), nullptr);
    EXPECT_EQ(ctor.Initializer()->ConstructorInitializerType(),
              Syntax::ConstructorInitializerType::Base);
    EXPECT_EQ(ctor.Initializer()->Arguments().Count(), 1);
    EXPECT_EQ(body->Statements().Count(), 0);
}

TEST(TransformFieldAndConstructorInitializersTest, MoveDefaultBaseConstructorLeavesNoInitializer)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto type = fixture.MakeType("Derived");
    auto baseType = fixture.MakeType("Base");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(new Syntax::BaseReferenceExpression(),
                                              std::string(".ctor")));
    body->Statements().Add(new Syntax::ExpressionStatement(invocation));

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    auto baseCtorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    baseCtorSym->SetDeclaringType(baseType);
    fixture.keepAlive.push_back(ctorSym);
    fixture.keepAlive.push_back(baseCtorSym);
    invocation->AddAnnotation(std::make_shared<Sem::InvocationResolveResult>(nullptr,
                                                                              baseCtorSym.get()));

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    ASSERT_TRUE(analyzer.MoveConstructorInitializer(ctor, *ctorSym));
    EXPECT_EQ(ctor.Initializer(), nullptr);
    EXPECT_EQ(body->Statements().Count(), 0);
}

TEST(TransformFieldAndConstructorInitializersTest, MoveLeavesNonCtorCallAlone)
{
    Fixture fixture;
    auto context = fixture.MakeContext();
    auto type = fixture.MakeType("C");

    Syntax::ConstructorDeclaration ctor;
    auto* body = new Syntax::BlockStatement();
    ctor.Body(body);
    auto* statement = new Syntax::ExpressionStatement(Ref("Call"));
    body->Statements().Add(statement);

    auto ctorSym = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    ctorSym->SetDeclaringType(type);
    fixture.keepAlive.push_back(ctorSym);

    Transforms::TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer analyzer(
        context, *type, nullptr);
    analyzer.HasMemberMap = true;
    EXPECT_FALSE(analyzer.MoveConstructorInitializer(ctor, *ctorSym));
    EXPECT_EQ(ctor.Initializer(), nullptr);
    EXPECT_EQ(body->Statements().Count(), 1);
}
