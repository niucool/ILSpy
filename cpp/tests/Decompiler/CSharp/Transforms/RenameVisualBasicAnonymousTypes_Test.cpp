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

// Tests for the `RenameVisualBasicAnonymousTypes` transform: the '$'-to-'_' identifier
// rename (gated on the resolved entity belonging to a VB anonymous type that keeps its own
// declaration) and the three-line leading comment on such a type declaration.
//
// The resolve results are supplied directly through the port's `ResolveResult` annotation
// channel: a `MemberResolveResult` over a field stub for the field-name arm, and a
// `TypeResolveResult` for the type-reference / type-declaration arms. The anonymous-type
// definitions come from the shared `LookupTypeDefinition` stub with a configurable
// namespace / generated name / [CompilerGenerated] attribute / settable property.

#include "Decompiler/CSharp/Transforms/RenameVisualBasicAnonymousTypes.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the RemoveCLSCompliantAttribute suite shape): a real
// compilation over MinimalCorlib plus the resolve/ast-builder state a context composes.
// RenameVisualBasicAnonymousTypes ignores the context, but the Run contract requires one.
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A VB anonymous type definition retaining its declaration: an empty namespace, a
// '$'-separated generated name containing "AnonymousType", [CompilerGenerated], and (by
// default) one settable property so it cannot be written as a C# anonymous type.
struct AnonymousTypeFixture {
    std::shared_ptr<TestSupport::LookupTypeDefinition> definition;
    std::vector<std::shared_ptr<TestSupport::LookupProperty>> properties;

    AnonymousTypeFixture(const TS::ICompilation& compilation,
                         std::string name = "VB$AnonymousType_0",
                         std::string ns = "")
    {
        const std::string fullName = name;
        definition = std::make_shared<TestSupport::LookupTypeDefinition>(
            std::move(name), std::move(ns),
            TS::FullTypeName(TS::TopLevelTypeName("", fullName)),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr);
        definition->SetKnownAttributes({TS::KnownAttribute::CompilerGenerated});
        AddProperty(true);
    }

    void AddProperty(bool canSet) {
        auto property = std::make_shared<TestSupport::LookupProperty>(
            "$P" + std::to_string(properties.size()), TS::ITypePtr{}, definition->Compilation());
        property->SetCanSet(canSet);
        properties.push_back(std::move(property));
        std::vector<const TS::IProperty*> pointers;
        for (const auto& p : properties)
            pointers.push_back(p.get());
        definition->SetProperties(std::move(pointers));
    }
};

// A field stub whose declaring type the transform's member arm reads. It subclasses the
// shared LookupField stub and overrides the two accessors (`Name`, `DeclaringTypeDefinition`)
// the rename needs.
class FieldStub : public TestSupport::LookupField {
public:
    FieldStub(std::string name, const TS::ICompilation& compilation)
        : LookupField(std::move(name), TS::ITypePtr{}, compilation) {}

    void SetDeclaringTypeDefinition(const TS::ITypeDefinition* declaring) {
        declaring_ = declaring;
    }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override { return declaring_; }

private:
    const TS::ITypeDefinition* declaring_ = nullptr;
};

// A `[Type]`-style name node (`SimpleType`) whose identifier token carries the given name
// and whose resolved type is attached as a `TypeResolveResult`.
Syntax::SimpleType* MakeTypeReference(const std::string& name,
                                      std::shared_ptr<Sem::TypeResolveResult> resolved) {
    auto* type = new Syntax::SimpleType(name);
    if (resolved)
        type->AddAnnotation(std::move(resolved));
    return type;
}

std::shared_ptr<Sem::TypeResolveResult> ResolveToType(
    const std::shared_ptr<TestSupport::LookupTypeDefinition>& definition) {
    return std::make_shared<Sem::TypeResolveResult>(
        std::static_pointer_cast<TS::IType>(definition));
}

// The identifier token a `VariableInitializer` was constructed with.
Syntax::Identifier* InitializerName(Syntax::VariableInitializer& initializer) {
    return initializer.NameToken();
}

// The number of leading comments on a node.
int LeadingCommentCount(Syntax::AstNode& node) {
    int count = 0;
    for (Syntax::Trivia* trivia : node.LeadingTrivia()) {
        if (dynamic_cast<Syntax::Comment*>(trivia) != nullptr)
            count++;
    }
    return count;
}

} // namespace

// A field whose name contains '$' and resolves to a member of a VB anonymous type that
// retains its declaration is renamed in place ('$' -> '_').
TEST(RenameVisualBasicAnonymousTypesTest, RenamesFieldIdentifierOfAnonymousDeclaredType)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    AnonymousTypeFixture anonymous(fixture.compilation);
    FieldStub field("VB$Foo", fixture.compilation);
    field.SetDeclaringTypeDefinition(anonymous.definition.get());

    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* initializer = new Syntax::VariableInitializer("VB$Foo");
    fieldDecl->Variables().Add(initializer);
    fieldDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, &field, std::static_pointer_cast<TS::IType>(anonymous.definition)));

    Syntax::SyntaxTree tree;
    tree.Members().Add(fieldDecl);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(InitializerName(*initializer)->Name(), "VB_Foo");
}

// An identifier whose name has no '$' is never touched, even when its symbol belongs to an
// anonymous declared type.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsIdentifierWithoutDollar)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    AnonymousTypeFixture anonymous(fixture.compilation);
    FieldStub field("Plain", fixture.compilation);
    field.SetDeclaringTypeDefinition(anonymous.definition.get());

    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* initializer = new Syntax::VariableInitializer("Plain");
    fieldDecl->Variables().Add(initializer);
    fieldDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, &field, std::static_pointer_cast<TS::IType>(anonymous.definition)));

    Syntax::SyntaxTree tree;
    tree.Members().Add(fieldDecl);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(InitializerName(*initializer)->Name(), "Plain");
}

// An identifier with no symbol (neither on its holder nor on the parent) is left alone.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsIdentifierWithoutSymbol)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* initializer = new Syntax::VariableInitializer("VB$Foo");
    fieldDecl->Variables().Add(initializer);

    Syntax::SyntaxTree tree;
    tree.Members().Add(fieldDecl);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(InitializerName(*initializer)->Name(), "VB$Foo");
}

// The resolved entity must carry the SAME name as the identifier; a reference through a
// differently named symbol is left alone.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsIdentifierWhenSymbolNameDiffers)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    AnonymousTypeFixture anonymous(fixture.compilation);
    FieldStub field("Other", fixture.compilation);
    field.SetDeclaringTypeDefinition(anonymous.definition.get());

    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* initializer = new Syntax::VariableInitializer("VB$Foo");
    fieldDecl->Variables().Add(initializer);
    fieldDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, &field, std::static_pointer_cast<TS::IType>(anonymous.definition)));

    Syntax::SyntaxTree tree;
    tree.Members().Add(fieldDecl);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(InitializerName(*initializer)->Name(), "VB$Foo");
}

// A member whose declaring type is a plain (non-anonymous) type is left alone: the type
// name contains '$' and the '[CompilerGenerated]' attribute is absent.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsIdentifierWhenDeclaringTypeNotAnonymous)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    auto plain = std::make_shared<TestSupport::LookupTypeDefinition>(
        "VB$Foo", "", TS::FullTypeName(TS::TopLevelTypeName("", "VB$Foo")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation, nullptr);
    FieldStub field("VB$Foo", fixture.compilation);
    field.SetDeclaringTypeDefinition(plain.get());

    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* initializer = new Syntax::VariableInitializer("VB$Foo");
    fieldDecl->Variables().Add(initializer);
    fieldDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, &field, std::static_pointer_cast<TS::IType>(plain)));

    Syntax::SyntaxTree tree;
    tree.Members().Add(fieldDecl);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(InitializerName(*initializer)->Name(), "VB$Foo");
}

// A member whose declaring type has ONLY read-only properties is a real C# anonymous type
// shape (IsAnonymousType), not one that keeps its declaration, so it is left alone.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsIdentifierWhenAnonymousTypeHasOnlyReadOnlyProperties)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    auto definition = std::make_shared<TestSupport::LookupTypeDefinition>(
        "VB$AnonymousType_0", "",
        TS::FullTypeName(TS::TopLevelTypeName("", "VB$AnonymousType_0")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation, nullptr);
    definition->SetKnownAttributes({TS::KnownAttribute::CompilerGenerated});
    auto property = std::make_shared<TestSupport::LookupProperty>(
        "$P", TS::ITypePtr{}, fixture.compilation);
    property->SetCanSet(false);
    definition->SetProperties({property.get()});

    FieldStub field("VB$Foo", fixture.compilation);
    field.SetDeclaringTypeDefinition(definition.get());

    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* initializer = new Syntax::VariableInitializer("VB$Foo");
    fieldDecl->Variables().Add(initializer);
    fieldDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, &field, std::static_pointer_cast<TS::IType>(definition)));

    Syntax::SyntaxTree tree;
    tree.Members().Add(fieldDecl);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(InitializerName(*initializer)->Name(), "VB$Foo");
}

// A type reference whose resolved symbol IS the anonymous type definition (the
// `entity as ITypeDefinition` arm) is renamed when the names agree.
TEST(RenameVisualBasicAnonymousTypesTest, RenamesTypeReferenceWhoseEntityIsTheAnonymousType)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    AnonymousTypeFixture anonymous(fixture.compilation, "VB$AnonymousType_0");
    auto* reference = MakeTypeReference("VB$AnonymousType_0", ResolveToType(anonymous.definition));

    Syntax::SyntaxTree tree;
    tree.Members().Add(reference);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->IdentifierToken()->Name(), "VB_AnonymousType_0");
}

// A type reference whose resolved symbol is a plain type (same '$'-name, no
// [CompilerGenerated] attribute) is left alone.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsTypeReferenceWhoseEntityIsNotAnonymous)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    auto plain = std::make_shared<TestSupport::LookupTypeDefinition>(
        "VB$Plain_0", "", TS::FullTypeName(TS::TopLevelTypeName("", "VB$Plain_0")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation, nullptr);
    auto* reference = MakeTypeReference("VB$Plain_0", ResolveToType(plain));

    Syntax::SyntaxTree tree;
    tree.Members().Add(reference);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->IdentifierToken()->Name(), "VB$Plain_0");
}

// Every '$' in the identifier name is replaced, not just the first.
TEST(RenameVisualBasicAnonymousTypesTest, ReplacesEveryDollar)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    AnonymousTypeFixture anonymous(fixture.compilation, "VB$AnonType$1");
    // The declaration name carries two '$'s but the name-mismatch gate compares the
    // identifier against the ENTITY name, so the reference must match it exactly.
    auto* reference = MakeTypeReference("VB$AnonType$1", ResolveToType(anonymous.definition));

    Syntax::SyntaxTree tree;
    tree.Members().Add(reference);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->IdentifierToken()->Name(), "VB_AnonType_1");
}

// An anonymous type that retains its declaration gets the three explanatory leading
// comments, in order.
TEST(RenameVisualBasicAnonymousTypesTest, AddsLeadingCommentsToAnonymousTypeDeclaration)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    AnonymousTypeFixture anonymous(fixture.compilation);
    auto* declaration = new Syntax::TypeDeclaration();
    declaration->AddAnnotation(ResolveToType(anonymous.definition));

    Syntax::SyntaxTree tree;
    tree.Members().Add(declaration);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    std::vector<Syntax::Comment*> comments;
    for (Syntax::Trivia* trivia : declaration->LeadingTrivia()) {
        if (auto* comment = dynamic_cast<Syntax::Comment*>(trivia))
            comments.push_back(comment);
    }
    ASSERT_EQ(comments.size(), 3u);
    EXPECT_EQ(comments[0]->Content(),
              " A VB anonymous type. Its properties are settable and only those declared 'Key'");
    EXPECT_EQ(comments[1]->Content(),
              " take part in Equals and GetHashCode, so it cannot be written as a C# anonymous");
    EXPECT_EQ(comments[2]->Content(), " type and is declared here instead.");
}

// A type declaration with no symbol is left uncommented.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsTypeDeclarationWithoutSymbol)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    auto* declaration = new Syntax::TypeDeclaration();

    Syntax::SyntaxTree tree;
    tree.Members().Add(declaration);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(LeadingCommentCount(*declaration), 0);
}

// A type declaration resolving to a non-anonymous type is left uncommented.
TEST(RenameVisualBasicAnonymousTypesTest, KeepsTypeDeclarationOfNonAnonymousType)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    auto plain = std::make_shared<TestSupport::LookupTypeDefinition>(
        "Plain", "NS", TS::FullTypeName(TS::TopLevelTypeName("NS", "Plain")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation, nullptr);
    auto* declaration = new Syntax::TypeDeclaration();
    declaration->AddAnnotation(ResolveToType(plain));

    Syntax::SyntaxTree tree;
    tree.Members().Add(declaration);

    Transforms::RenameVisualBasicAnonymousTypes transform;
    transform.Run(tree, context);

    EXPECT_EQ(LeadingCommentCount(*declaration), 0);
}
