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

// Tests for the IntroduceUsingDeclarations transform (the port of
// CSharp/Transforms/IntroduceUsingDeclarations.cs) plus its prerequisites:
//   * the `AstType.IsVar` / `AstType.GetNameLookupMode` hand-written reads;
//   * `TypeSystemExtensions.GetNamespaceByFullName`;
//   * the `FindRequiredImports` namespace collection and the `using`-declaration insertion
//     through a full `Run` (the settings gate, the `System`-first ordering);
//   * the `UsingScope` root annotation; and
//   * the `FullyQualifyAmbiguousTypeNamesVisitor` replacement of an annotated `SimpleType`.

#include "Decompiler/CSharp/Transforms/IntroduceUsingDeclarations.hpp"

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/UsingDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/UsingScopeAnnotation.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Transforms = ILSpy::Decompiler::CSharp::Transforms;
using ILSpy::Decompiler::CSharp::GetUsingScope;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

// A minimal namespace tree for the `GetNamespaceByFullName` walk.
class NsStub final : public INamespace {
public:
    NsStub(const ICompilation& compilation, std::string name)
        : compilation_(compilation), name_(std::move(name)) {}

    void AddChild(std::unique_ptr<NsStub> child) { children_.push_back(std::move(child)); }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return name_; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override {
        std::vector<const INamespace*> result;
        for (const auto& child : children_)
            result.push_back(child.get());
        return result;
    }
    std::vector<const ITypeDefinition*> Types() const override { return {}; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const INamespace* GetChildNamespace(const std::string& name) const override {
        for (const auto& child : children_)
            if (child->name_ == name)
                return child.get();
        return nullptr;
    }
    const ITypeDefinition* GetTypeDefinition(const std::string&, int) const override {
        return nullptr;
    }

private:
    const ICompilation& compilation_;
    std::string name_;
    std::vector<std::unique_ptr<NsStub>> children_;
};

// A compilation whose root namespace is the NsStub tree (LookupCompilation's root is a
// child-less stub, so the namespace walk cannot be exercised through it directly).
class RootedCompilation final : public LookupCompilation {
public:
    RootedCompilation() : root_(*this, "Root") {}
    const INamespace& RootNamespace() const override { return root_; }
    NsStub& Root() { return root_; }

private:
    NsStub root_;
};

// The per-test fixture: a fresh LookupCompilation plus the settings and the ast builder
// the TransformContext requires.
struct Fixture {
    LookupCompilation compilation;
    ILSpy::Decompiler::DecompilerSettings settings;
    Syntax::TypeSystemAstBuilder astBuilder;

    std::shared_ptr<LookupTypeDefinition> MakeType(const std::string& ns,
                                                   const std::string& name) const {
        auto def = std::make_shared<LookupTypeDefinition>(
            name, ns, TS::FullTypeName(TS::TopLevelTypeName(ns, name, 0)),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr,
            TS::KnownTypeCode::None);
        return def;
    }

    std::shared_ptr<UsingScope> MakeRootScope() const {
        auto context = std::make_shared<CSharpTypeResolveContext>(compilation.MainModule());
        return std::make_shared<UsingScope>(context, compilation.RootNamespace(),
                                            std::vector<const INamespace*>{});
    }

    void Run(Syntax::AstNode& root) {
        auto scope = MakeRootScope();
        ILSpy::Decompiler::DecompileRun run(&settings, scope);
        CSharpTypeResolveContext decompilationContext(compilation.MainModule());
        Transforms::TransformContext context(compilation, run, decompilationContext, astBuilder);
        Transforms::IntroduceUsingDeclarations transform;
        transform.Run(root, context);
    }
};

Syntax::SimpleType* AnnotatedSimpleType(const std::string& identifier,
                                        const std::shared_ptr<LookupTypeDefinition>& type) {
    auto* simpleType = new Syntax::SimpleType(identifier);
    simpleType->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(type));
    return simpleType;
}

} // namespace

// ===========================================================================
// AstType.IsVar
// ===========================================================================

TEST(IntroduceUsingDeclarationsTest, IsVarTrueForPlainVar)
{
    Syntax::SimpleType type("var");
    EXPECT_TRUE(type.IsVar());
}

TEST(IntroduceUsingDeclarationsTest, IsVarFalseForVarWithTypeArguments)
{
    Syntax::SimpleType type("var");
    type.TypeArguments().Add(new Syntax::SimpleType("T"));
    EXPECT_FALSE(type.IsVar());
}

TEST(IntroduceUsingDeclarationsTest, IsVarFalseForOtherName)
{
    Syntax::SimpleType type("Int32");
    EXPECT_FALSE(type.IsVar());
}

// ===========================================================================
// AstType.GetNameLookupMode
// ===========================================================================

TEST(IntroduceUsingDeclarationsTest, NameLookupModePlainTypeIsType)
{
    Syntax::SimpleType type("C");
    EXPECT_EQ(type.GetNameLookupMode(), ILSpy::Decompiler::CSharp::Resolver::NameLookupMode::Type);
}

TEST(IntroduceUsingDeclarationsTest, NameLookupModeUsingImport)
{
    Syntax::UsingDeclaration usingDeclaration;
    auto* type = new Syntax::SimpleType("System");
    usingDeclaration.Import(type);
    EXPECT_EQ(type->GetNameLookupMode(),
              ILSpy::Decompiler::CSharp::Resolver::NameLookupMode::TypeInUsingDeclaration);
}

TEST(IntroduceUsingDeclarationsTest, NameLookupModeTypeBaseType)
{
    Syntax::TypeDeclaration typeDeclaration;
    auto* type = new Syntax::SimpleType("Base");
    typeDeclaration.BaseTypes().Add(type);
    EXPECT_EQ(type->GetNameLookupMode(),
              ILSpy::Decompiler::CSharp::Resolver::NameLookupMode::BaseTypeReference);
}

TEST(IntroduceUsingDeclarationsTest, NameLookupModeTypeConstraintBaseType)
{
    Syntax::TypeDeclaration typeDeclaration;
    auto* constraint = new Syntax::Constraint();
    auto* type = new Syntax::SimpleType("Base");
    constraint->BaseTypes().Add(type);
    typeDeclaration.Constraints().Add(constraint);
    EXPECT_EQ(type->GetNameLookupMode(),
              ILSpy::Decompiler::CSharp::Resolver::NameLookupMode::BaseTypeReference);
}

TEST(IntroduceUsingDeclarationsTest, NameLookupModeMethodConstraintBaseTypeIsType)
{
    Syntax::MethodDeclaration methodDeclaration;
    auto* constraint = new Syntax::Constraint();
    auto* type = new Syntax::SimpleType("Base");
    constraint->BaseTypes().Add(type);
    methodDeclaration.Constraints().Add(constraint);
    EXPECT_EQ(type->GetNameLookupMode(),
              ILSpy::Decompiler::CSharp::Resolver::NameLookupMode::Type);
}

TEST(IntroduceUsingDeclarationsTest, NameLookupModeInTypeReferenceExpressionIsType)
{
    auto* reference = new Syntax::TypeReferenceExpression(new Syntax::SimpleType("C"));
    auto* type = dynamic_cast<Syntax::SimpleType*>(reference->Type());
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->GetNameLookupMode(),
              ILSpy::Decompiler::CSharp::Resolver::NameLookupMode::Type);
}

// ===========================================================================
// TypeSystemExtensions.GetNamespaceByFullName
// ===========================================================================

TEST(IntroduceUsingDeclarationsTest, GetNamespaceByFullNameEmptyReturnsRoot)
{
    RootedCompilation compilation;
    EXPECT_EQ(TS::GetNamespaceByFullName(compilation, ""), &compilation.RootNamespace());
}

TEST(IntroduceUsingDeclarationsTest, GetNamespaceByFullNameWalksChildren)
{
    RootedCompilation compilation;
    auto a = std::make_unique<NsStub>(compilation, "A");
    NsStub* aPtr = a.get();
    a->AddChild(std::make_unique<NsStub>(compilation, "B"));
    compilation.Root().AddChild(std::move(a));
    const INamespace* result = TS::GetNamespaceByFullName(compilation, "A.B");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Name(), "B");
    (void)aPtr;
}

TEST(IntroduceUsingDeclarationsTest, GetNamespaceByFullNameMissingReturnsNull)
{
    RootedCompilation compilation;
    compilation.Root().AddChild(std::make_unique<NsStub>(compilation, "A"));
    EXPECT_EQ(TS::GetNamespaceByFullName(compilation, "A.C"), nullptr);
}

// ===========================================================================
// IntroduceUsingDeclarations.Run
// ===========================================================================

TEST(IntroduceUsingDeclarationsTest, RunInsertsUsingDeclarationsSystemFirst)
{
    Fixture fixture;
    fixture.settings.SetUsingDeclarations(true);
    auto myType = fixture.MakeType("My.Ns", "Widget");
    auto sysType = fixture.MakeType("System.Text", "StringBuilder");

    Syntax::SyntaxTree root;
    auto* first = new Syntax::TypeDeclaration();
    first->BaseTypes().Add(AnnotatedSimpleType("Widget", myType));
    root.Members().Add(first);
    auto* second = new Syntax::TypeDeclaration();
    second->BaseTypes().Add(AnnotatedSimpleType("StringBuilder", sysType));
    root.Members().Add(second);

    fixture.Run(root);

    auto& members = root.Members();
    ASSERT_GE(members.Count(), 2);
    auto* using0 = dynamic_cast<Syntax::UsingDeclaration*>(members[0]);
    auto* using1 = dynamic_cast<Syntax::UsingDeclaration*>(members[1]);
    ASSERT_NE(using0, nullptr);
    ASSERT_NE(using1, nullptr);
    // The System import comes first; the other namespace follows.
    auto* import0 = dynamic_cast<Syntax::MemberType*>(using0->Import());
    ASSERT_NE(import0, nullptr);
    EXPECT_EQ(import0->MemberName(), "Text");
    auto* simple0 = dynamic_cast<Syntax::SimpleType*>(import0->Target());
    ASSERT_NE(simple0, nullptr);
    EXPECT_EQ(simple0->Identifier(), std::optional<std::string>("System"));
    auto* import1 = dynamic_cast<Syntax::MemberType*>(using1->Import());
    ASSERT_NE(import1, nullptr);
    EXPECT_EQ(import1->MemberName(), "Ns");
}

TEST(IntroduceUsingDeclarationsTest, RunAttachesUsingScopeAnnotation)
{
    Fixture fixture;
    fixture.settings.SetUsingDeclarations(true);
    Syntax::SyntaxTree root;
    fixture.Run(root);

    auto scope = GetUsingScope(root);
    ASSERT_NE(scope, nullptr);
    EXPECT_TRUE(scope->Usings().empty());  // LookupCompilation's root has no child namespaces
}

TEST(IntroduceUsingDeclarationsTest, RunSettingsOffInsertsNoUsingDeclarations)
{
    Fixture fixture;
    fixture.settings.SetUsingDeclarations(false);
    auto myType = fixture.MakeType("My.Ns", "Widget");

    Syntax::SyntaxTree root;
    auto* first = new Syntax::TypeDeclaration();
    first->BaseTypes().Add(AnnotatedSimpleType("Widget", myType));
    root.Members().Add(first);

    fixture.Run(root);

    EXPECT_EQ(dynamic_cast<Syntax::UsingDeclaration*>(root.Members()[0]), nullptr);
    EXPECT_NE(GetUsingScope(root), nullptr);
}

TEST(IntroduceUsingDeclarationsTest, RunFullyQualifiesAnnotatedSimpleType)
{
    Fixture fixture;
    fixture.settings.SetUsingDeclarations(false);
    auto type = fixture.MakeType("System", "Int32");

    Syntax::SyntaxTree root;
    auto* declaration = new Syntax::TypeDeclaration();
    auto* simpleType = AnnotatedSimpleType("Int32", type);
    declaration->BaseTypes().Add(simpleType);
    root.Members().Add(declaration);

    fixture.Run(root);

    // The visitor replaces the annotated type with the builder's rendering, so the
    // original node is detached from the declaration's base-type list.
    EXPECT_EQ(declaration->BaseTypes().Count(), 1);
    EXPECT_NE(declaration->BaseTypes()[0], simpleType);
}
