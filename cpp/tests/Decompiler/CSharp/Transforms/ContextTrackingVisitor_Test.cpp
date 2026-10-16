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

// Tests for the `<AstNode>`-returning visitor infrastructure (the `IAstVisitorAstNode`
// dispatch and the `DepthFirstAstVisitor<AstNode>` default walk, reached through the
// `AstNode::AcceptVisitorAstNode` adapter) and for `ContextTrackingVisitor` (the
// current-type/current-method context slots the pattern-based transforms need), the
// prerequisites for `PatternStatementTransform`.

#include "Decompiler/CSharp/Transforms/ContextTrackingVisitor.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorAstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorAstNode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace PM = ::ILSpy::Decompiler::CSharp::Syntax::PatternMatching;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// A minimal named `IType` for the resolve-result return slots.
class StubType : public TS::IType {
public:
    explicit StubType(std::string name) : name_(std::move(name)) {}
    TS::TypeKind Kind() const override { return TS::TypeKind::Unknown; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    bool StructuralEquals(const TS::IType& other) const override { return &other == this; }

private:
    std::string name_;
};

// The TransformContext fixture (the RenameVisualBasicAnonymousTypes suite shape).
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

std::shared_ptr<TestSupport::LookupTypeDefinition> MakeTypeDef(const TS::ICompilation& compilation,
                                                              const std::string& name) {
    return std::make_shared<TestSupport::LookupTypeDefinition>(
        name, "", TS::FullTypeName(TS::TopLevelTypeName("", name)), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr);
}

// A `TypeDeclaration` named `name` whose resolved symbol is `definition`.
Syntax::TypeDeclaration* MakeTypeDecl(const std::string& name,
                                      const std::shared_ptr<TestSupport::LookupTypeDefinition>& definition) {
    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name(name);
    typeDecl->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
        std::static_pointer_cast<TS::IType>(definition)));
    return typeDecl;
}

// A `MethodDeclaration` named `name` whose resolved symbol is `method`.
Syntax::MethodDeclaration* MakeMethod(const std::string& name, const TS::IMethod* method) {
    auto* methodDecl = new Syntax::MethodDeclaration();
    methodDecl->Name(name);
    methodDecl->AddAnnotation(
        std::make_shared<Sem::MemberResolveResult>(nullptr, method, std::make_shared<StubType>("T")));
    return methodDecl;
}

// A visitor overriding `VisitIdentifierExpression` to return a fixed replacement and count
// the visits.
class ReplacementVisitor : public Syntax::DepthFirstAstVisitorAstNode {
public:
    Syntax::Expression* replacement = new Syntax::IdentifierExpression("replacement");
    int visits = 0;

    Syntax::AstNode* VisitIdentifierExpression(Syntax::IdentifierExpression*) override {
        visits++;
        return replacement;
    }
};

// A visitor overriding `VisitIdentifierExpression` only to count.
class CountingVisitor : public Syntax::DepthFirstAstVisitorAstNode {
public:
    int visits = 0;
    Syntax::AstNode* VisitMethodDeclaration(Syntax::MethodDeclaration*) override {
        visits++;
        return nullptr;
    }
};

// A visitor overriding the pattern-placeholder arm.
class PlaceholderVisitor : public Syntax::DepthFirstAstVisitorAstNode {
public:
    Syntax::AstNode* placeholder = nullptr;
    PM::Pattern* pattern = nullptr;
    int visits = 0;

    Syntax::AstNode* VisitPatternPlaceholder(Syntax::AstNode* p, PM::Pattern& pat) override {
        visits++;
        placeholder = p;
        pattern = &pat;
        return p;
    }
};

// The ContextTrackingVisitor test visitor. The context slots are set by the base's per-node
// `Visit<Declaration>` arms before they call `VisitChildren`, so the visitor records in the
// overridden `VisitChildren` (the point during the walk where the slots are live), keyed on
// the declaration node being walked.
class TrackingVisitor : public Transforms::ContextTrackingVisitor {
public:
    std::vector<std::pair<const TS::ITypeDefinition*, const TS::IMethod*>> hits;
    std::vector<std::string> names;

    const TS::ITypeDefinition* CurrentType() const { return currentTypeDefinition; }
    const TS::IMethod* CurrentMethod() const { return currentMethod; }

    void Seed(const Transforms::TransformContext& context) { Initialize(context); }
    void Clear() { Uninitialize(); }

protected:
    Syntax::AstNode* VisitChildren(Syntax::AstNode* node) override {
        if (auto* methodDeclaration = dynamic_cast<Syntax::MethodDeclaration*>(node)) {
            hits.push_back({currentTypeDefinition, currentMethod});
            names.push_back(methodDeclaration->Name());
        }
        return Syntax::DepthFirstAstVisitorAstNode::VisitChildren(node);
    }
};

} // namespace

// ---- IAstVisitorAstNode dispatch -----------------------------------------------------

// An overridden per-node visit returns its `AstNode*` result through `AcceptVisitorAstNode`.
TEST(DepthFirstAstVisitorAstNodeTest, OverrideReturnValueIsPropagated)
{
    ReplacementVisitor visitor;
    auto* identifier = new Syntax::IdentifierExpression("x");

    Syntax::AstNode* result = identifier->AcceptVisitorAstNode(visitor);

    EXPECT_EQ(result, static_cast<Syntax::AstNode*>(visitor.replacement));
    EXPECT_EQ(visitor.visits, 1);
}

// An un-overridden per-node visit falls through to the default walk and returns null (the C#
// `default(T)`), NOT the node.
TEST(DepthFirstAstVisitorAstNodeTest, UnhandledNodeReturnsNull)
{
    ReplacementVisitor visitor;
    auto* primitive = new Syntax::PrimitiveExpression(42);

    EXPECT_EQ(primitive->AcceptVisitorAstNode(visitor), nullptr);
    EXPECT_EQ(visitor.visits, 0);
}

// The default `VisitChildren` walks every descendant and returns null.
TEST(DepthFirstAstVisitorAstNodeTest, VisitChildrenWalksEveryDescendant)
{
    CountingVisitor visitor;
    auto* tree = new Syntax::SyntaxTree();
    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Members().Add(new Syntax::MethodDeclaration());
    typeDecl->Members().Add(new Syntax::MethodDeclaration());
    typeDecl->Members().Add(new Syntax::MethodDeclaration());
    tree->Members().Add(typeDecl);

    Syntax::AstNode* result = tree->AcceptVisitorAstNode(visitor);

    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(visitor.visits, 3);
}

// A nested override is reached through the default container walk: each method of the nested
// type is visited in document order.
TEST(DepthFirstAstVisitorAstNodeTest, NestedOverrideReachedThroughWalk)
{
    struct MethodCountingVisitor : Syntax::DepthFirstAstVisitorAstNode {
        int methods = 0;
        Syntax::AstNode* VisitMethodDeclaration(Syntax::MethodDeclaration*) override {
            methods++;
            return nullptr;
        }
    };

    MethodCountingVisitor visitor;
    auto* tree = new Syntax::SyntaxTree();
    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Members().Add(new Syntax::MethodDeclaration());
    auto* nested = new Syntax::TypeDeclaration();
    nested->Members().Add(new Syntax::MethodDeclaration());
    typeDecl->Members().Add(nested);
    tree->Members().Add(typeDecl);

    tree->AcceptVisitorAstNode(visitor);

    EXPECT_EQ(visitor.methods, 2);
}

// A no-override visitor returns null for a node that has children too.
TEST(DepthFirstAstVisitorAstNodeTest, NoOverrideReturnsNullForContainer)
{
    struct NoOpVisitor : Syntax::DepthFirstAstVisitorAstNode {};
    NoOpVisitor visitor;
    auto* tree = new Syntax::SyntaxTree();
    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Members().Add(new Syntax::MethodDeclaration());
    tree->Members().Add(typeDecl);

    EXPECT_EQ(tree->AcceptVisitorAstNode(visitor), nullptr);
}

// The pattern-placeholder arm routes through `AcceptVisitorAstNode` too.
TEST(DepthFirstAstVisitorAstNodeTest, PatternPlaceholderDispatchesToVisitPatternPlaceholder)
{
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<Syntax::Expression> placeholder(PM::PatternExtensions::ToExpression(any));

    PlaceholderVisitor visitor;
    Syntax::AstNode* result = placeholder->AcceptVisitorAstNode(visitor);

    EXPECT_EQ(result, placeholder.get());
    EXPECT_EQ(visitor.visits, 1);
    EXPECT_EQ(visitor.placeholder, placeholder.get());
    EXPECT_EQ(visitor.pattern, any.get());
}

// ---- ContextTrackingVisitor ----------------------------------------------------------

// The current type/method are set while walking a method of the annotated type.
TEST(ContextTrackingVisitorTest, TracksTypeAndMethodDuringMethodWalk)
{
    TransformFixture fixture;
    auto outer = MakeTypeDef(fixture.compilation, "Outer");
    auto method = std::make_shared<TestSupport::LookupMethod>("M1", fixture.compilation);

    auto* typeDecl = MakeTypeDecl("Outer", outer);
    typeDecl->Members().Add(MakeMethod("M1", method.get()));

    TrackingVisitor visitor;
    typeDecl->AcceptVisitorAstNode(visitor);

    ASSERT_EQ(visitor.hits.size(), 1u);
    EXPECT_EQ(visitor.hits[0].first, outer.get());
    EXPECT_EQ(visitor.hits[0].second, method.get());
    EXPECT_EQ(visitor.names[0], "M1");
    // The slots are restored to the pre-walk state when the top-level visit returns.
    EXPECT_EQ(visitor.CurrentType(), nullptr);
    EXPECT_EQ(visitor.CurrentMethod(), nullptr);
}

// A nested type's methods see the nested type; the outer type is restored afterwards.
TEST(ContextTrackingVisitorTest, NestedTypeReplacesAndRestoresContext)
{
    TransformFixture fixture;
    auto outer = MakeTypeDef(fixture.compilation, "Outer");
    auto inner = MakeTypeDef(fixture.compilation, "Inner");
    auto outerMethod = std::make_shared<TestSupport::LookupMethod>("OuterM", fixture.compilation);
    auto innerMethod = std::make_shared<TestSupport::LookupMethod>("InnerM", fixture.compilation);

    auto* typeDecl = MakeTypeDecl("Outer", outer);
    typeDecl->Members().Add(MakeMethod("OuterM", outerMethod.get()));
    auto* nested = MakeTypeDecl("Inner", inner);
    nested->Members().Add(MakeMethod("InnerM", innerMethod.get()));
    typeDecl->Members().Add(nested);

    TrackingVisitor visitor;
    typeDecl->AcceptVisitorAstNode(visitor);

    ASSERT_EQ(visitor.hits.size(), 2u);
    EXPECT_EQ(visitor.hits[0].first, outer.get());
    EXPECT_EQ(visitor.hits[0].second, outerMethod.get());
    EXPECT_EQ(visitor.hits[1].first, inner.get());
    EXPECT_EQ(visitor.hits[1].second, innerMethod.get());
    EXPECT_EQ(visitor.CurrentType(), nullptr);
    EXPECT_EQ(visitor.CurrentMethod(), nullptr);
}

// The constructor and accessor arms track the method the same way.
TEST(ContextTrackingVisitorTest, TracksConstructorAndAccessorMethods)
{
    TransformFixture fixture;
    auto typeDef = MakeTypeDef(fixture.compilation, "C");
    auto ctor = std::make_shared<TestSupport::LookupMethod>(".ctor", fixture.compilation);
    auto accessorMethod = std::make_shared<TestSupport::LookupMethod>("get_P", fixture.compilation);

    auto* typeDecl = MakeTypeDecl("C", typeDef);
    auto* constructorDecl = new Syntax::ConstructorDeclaration();
    constructorDecl->AddAnnotation(
        std::make_shared<Sem::MemberResolveResult>(nullptr, ctor.get(), std::make_shared<StubType>("T")));
    typeDecl->Members().Add(constructorDecl);

    // `Accessor` is not a `TypeDeclaration.Members` element (it attaches to a property or
    // event), so the accessor arm is driven directly.
    auto* accessor = new Syntax::Accessor(Syntax::AccessorKind::Getter);
    accessor->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, accessorMethod.get(), std::make_shared<StubType>("T")));

    // A visitor recording the slots at the constructor/accessor visits. The base's
    // `Visit<Declaration>` arms set the slots before calling `VisitChildren`, so the visitor
    // records in the overridden `VisitChildren` for the declaration nodes.
    class CtorAccessorVisitor : public Transforms::ContextTrackingVisitor {
    public:
        std::vector<const TS::IMethod*> methods;

    protected:
        Syntax::AstNode* VisitChildren(Syntax::AstNode* node) override {
            if (dynamic_cast<Syntax::ConstructorDeclaration*>(node) != nullptr
                || dynamic_cast<Syntax::Accessor*>(node) != nullptr) {
                methods.push_back(currentMethod);
            }
            return Syntax::DepthFirstAstVisitorAstNode::VisitChildren(node);
        }
    };

    CtorAccessorVisitor visitor;
    typeDecl->AcceptVisitorAstNode(visitor);
    accessor->AcceptVisitorAstNode(visitor);

    ASSERT_EQ(visitor.methods.size(), 2u);
    EXPECT_EQ(visitor.methods[0], ctor.get());
    EXPECT_EQ(visitor.methods[1], accessorMethod.get());
}

// `Initialize` seeds the slots from the context's decompilation position.
TEST(ContextTrackingVisitorTest, InitializeSeedsSlotsFromContext)
{
    TransformFixture fixture;
    auto typeDef = MakeTypeDef(fixture.compilation, "Seeded");
    auto method = std::make_shared<TestSupport::LookupMethod>("SeededM", fixture.compilation);

    TS::SimpleTypeResolveContext simple(fixture.compilation);
    std::unique_ptr<TS::ITypeResolveContext> withType = simple.WithCurrentTypeDefinition(typeDef.get());
    std::unique_ptr<TS::ITypeResolveContext> withMember = withType->WithCurrentMember(method.get());
    Transforms::TransformContext context(fixture.compilation, fixture.run, *withMember, fixture.astBuilder);

    TrackingVisitor visitor;
    visitor.Seed(context);

    EXPECT_EQ(visitor.CurrentType(), typeDef.get());
    EXPECT_EQ(visitor.CurrentMethod(), method.get());
}

// `Uninitialize` clears both slots.
TEST(ContextTrackingVisitorTest, UninitializeClearsSlots)
{
    TransformFixture fixture;
    auto typeDef = MakeTypeDef(fixture.compilation, "Seeded");
    auto method = std::make_shared<TestSupport::LookupMethod>("SeededM", fixture.compilation);

    TS::SimpleTypeResolveContext simple(fixture.compilation);
    std::unique_ptr<TS::ITypeResolveContext> withType = simple.WithCurrentTypeDefinition(typeDef.get());
    std::unique_ptr<TS::ITypeResolveContext> withMember = withType->WithCurrentMember(method.get());
    Transforms::TransformContext context(fixture.compilation, fixture.run, *withMember, fixture.astBuilder);

    TrackingVisitor visitor;
    visitor.Seed(context);
    visitor.Clear();

    EXPECT_EQ(visitor.CurrentType(), nullptr);
    EXPECT_EQ(visitor.CurrentMethod(), nullptr);
}
