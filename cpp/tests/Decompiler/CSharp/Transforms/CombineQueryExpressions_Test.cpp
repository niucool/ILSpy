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

// Tests for the `CombineQueryExpressions` transform: the `Cast` type-argument move into the
// from clause, the query-continuation introduction and its three guards (non-transparent
// identifier, no select clause, select is not an anonymous type), and the
// transparent-identifier removal (the from/select pair removal, the inner-clause hoist, the
// `let` clauses for anonymous-type members, the transparent-reference replacement with its
// type-argument move and annotation propagation), plus the `QueryExpressions`-off no-op.
//
// The query trees are built directly (the transform is resolver-free); the annotation
// propagation case attaches an `ILVariableResolveResult` to the hoisted initializer, matching
// what the (unported) `DeclareVariables` analysis would have attached.

#include "Decompiler/CSharp/Transforms/CombineQueryExpressions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/QueryContinuationClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryFromClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryLetClause.hpp"
#include "Decompiler/CSharp/Syntax/QuerySelectClause.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the FixNameCollisions suite shape): a real compilation over
// MinimalCorlib plus the resolve/ast-builder state a context composes.
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
        auto root = std::make_shared<
            ::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A minimal named `IType` used only as the variable's type (the transform never reads it).
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

// ---- tree builders (the test convention: raw `new`, owned by the tree, intentionally not
// freed) ---------------------------------------------------------------------------------

Syntax::IdentifierExpression* Ref(const std::string& name) {
    return new Syntax::IdentifierExpression(name);
}

Syntax::QueryFromClause* AddFrom(Syntax::QueryExpression* query, const std::string& identifier,
                                 Syntax::Expression* expression) {
    auto* from = new Syntax::QueryFromClause();
    from->Identifier(identifier);
    from->Expression(expression);
    query->Clauses().Add(from);
    return from;
}

Syntax::QuerySelectClause* AddSelect(Syntax::QueryExpression* query,
                                     Syntax::Expression* expression) {
    auto* select = new Syntax::QuerySelectClause();
    select->Expression(expression);
    query->Clauses().Add(select);
    return select;
}

Syntax::AnonymousTypeCreateExpression* Anon(const std::vector<Syntax::Expression*>& elements) {
    auto* anon = new Syntax::AnonymousTypeCreateExpression();
    for (Syntax::Expression* element : elements)
        anon->Initializers().Add(element);
    return anon;
}

Syntax::MemberReferenceExpression* Member(Syntax::Expression* target,
                                          const std::string& memberName) {
    return new Syntax::MemberReferenceExpression(target, memberName);
}

// The transparent-identifier outer query: `from <>h__TransparentIdentifier0 in (inner) select
// <>h__TransparentIdentifier0.<member>`.
Syntax::QueryExpression* OuterWithTransparentIdentifier(Syntax::QueryExpression* inner,
                                                        const std::string& member) {
    auto* outer = new Syntax::QueryExpression();
    AddFrom(outer, "<>h__TransparentIdentifier0", inner);
    AddSelect(outer, Member(Ref("<>h__TransparentIdentifier0"), member));
    return outer;
}

} // namespace

// `from x in z.Cast<int>()` moves the cast's type argument into the from clause and lifts the
// cast's target out as the source expression.
TEST(CombineQueryExpressionsTest, MovesCastTypeIntoFromClause) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* query = new Syntax::QueryExpression();
    auto* z = Ref("z");
    auto* intType = new Syntax::SimpleType("int");
    auto* cast = Member(z, "Cast");
    cast->TypeArguments().Add(intType);
    auto* from = AddFrom(query, "x", new Syntax::InvocationExpression(cast));
    AddSelect(query, Ref("x"));

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    EXPECT_EQ(from->Type(), intType);
    EXPECT_EQ(from->Expression(), z);
    EXPECT_EQ(query->Clauses().At(0), from);
}

// A plain `from x in z` source is left alone.
TEST(CombineQueryExpressionsTest, KeepsPlainFromExpression) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* query = new Syntax::QueryExpression();
    auto* z = Ref("z");
    auto* from = AddFrom(query, "x", z);
    AddSelect(query, Ref("x"));

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    EXPECT_EQ(from->Expression(), z);
    EXPECT_EQ(from->Type(), nullptr);
    EXPECT_EQ(query->Clauses().Count(), 2);
}

// `from x in (from y in src select y)` with a non-transparent identifier becomes a query
// continuation.
TEST(CombineQueryExpressionsTest, IntroducesQueryContinuationForNonTransparentIdentifier) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    AddFrom(inner, "y", Ref("source"));
    AddSelect(inner, Ref("y"));
    auto* query = new Syntax::QueryExpression();
    auto* from = AddFrom(query, "x", inner);
    AddSelect(query, Ref("x"));

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    auto* continuation = dynamic_cast<Syntax::QueryContinuationClause*>(query->Clauses().At(0));
    ASSERT_NE(continuation, nullptr);
    EXPECT_EQ(continuation->PrecedingQuery(), inner);
    EXPECT_EQ(continuation->Identifier(), "x");
    EXPECT_EQ(query->Clauses().Count(), 2);
    EXPECT_EQ(from->Parent(), nullptr);
}

// A transparent identifier whose inner query does not end in a select clause still becomes a
// continuation (the transparent-identifier pattern is not matched).
TEST(CombineQueryExpressionsTest, IntroducesQueryContinuationWhenInnerHasNoSelect) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    AddFrom(inner, "y", Ref("source"));
    AddFrom(inner, "z", Ref("other"));
    auto* query = OuterWithTransparentIdentifier(inner, "y");

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    ASSERT_NE(dynamic_cast<Syntax::QueryContinuationClause*>(query->Clauses().At(0)), nullptr);
    EXPECT_EQ(query->Clauses().Count(), 2);
}

// A transparent identifier whose inner select is not an anonymous type still becomes a
// continuation.
TEST(CombineQueryExpressionsTest, IntroducesQueryContinuationWhenSelectIsNotAnonymousType) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    AddFrom(inner, "y", Ref("source"));
    AddSelect(inner, Ref("y"));
    auto* query = OuterWithTransparentIdentifier(inner, "y");

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    ASSERT_NE(dynamic_cast<Syntax::QueryContinuationClause*>(query->Clauses().At(0)), nullptr);
    EXPECT_EQ(query->Clauses().Count(), 2);
}

// The positive transparent-identifier case: the from/select pair is removed, the inner from
// clause is hoisted, the anonymous-type member becomes a `let` clause, and the transparent
// reference is replaced by the underlying member.
TEST(CombineQueryExpressionsTest, RemovesTransparentIdentifierAndHoistsInnerClauses) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    auto* innerFrom = AddFrom(inner, "a", Ref("source"));
    AddSelect(inner, Anon({Ref("a"), new Syntax::NamedExpression("b", Member(Ref("a"), "Foo"))}));
    auto* query = OuterWithTransparentIdentifier(inner, "a");

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    ASSERT_EQ(query->Clauses().Count(), 3);
    EXPECT_EQ(query->Clauses().At(0), innerFrom);
    EXPECT_EQ(innerFrom->Parent(), query);
    auto* let = dynamic_cast<Syntax::QueryLetClause*>(query->Clauses().At(1));
    ASSERT_NE(let, nullptr);
    EXPECT_EQ(let->Identifier(), "b");
    auto* member = dynamic_cast<Syntax::MemberReferenceExpression*>(let->Expression());
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->MemberName(), "Foo");
    auto* select = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2));
    ASSERT_NE(select, nullptr);
    auto* replaced = dynamic_cast<Syntax::IdentifierExpression*>(select->Expression());
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->Identifier(), "a");
}

// A bare member-reference anonymous-type initializer (`new { a.Prop }`) becomes a `let` clause
// named after the member.
TEST(CombineQueryExpressionsTest, AddsQueryLetClauseForMemberReferenceInitializer) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    auto* innerFrom = AddFrom(inner, "a", Ref("source"));
    AddSelect(inner, Anon({Member(Ref("a"), "Prop")}));
    auto* query = OuterWithTransparentIdentifier(inner, "Prop");

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    ASSERT_EQ(query->Clauses().Count(), 3);
    EXPECT_EQ(query->Clauses().At(0), innerFrom);
    auto* let = dynamic_cast<Syntax::QueryLetClause*>(query->Clauses().At(1));
    ASSERT_NE(let, nullptr);
    EXPECT_EQ(let->Identifier(), "Prop");
    auto* select = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2));
    ASSERT_NE(select, nullptr);
    auto* replaced = dynamic_cast<Syntax::IdentifierExpression*>(select->Expression());
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->Identifier(), "Prop");
}

// `a = a` (a named expression whose name equals its identifier) records the identifier's
// resolve result and adds NO `let` clause.
TEST(CombineQueryExpressionsTest, NamedExpressionWithSameIdentifierAddsNoLetClause) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    AddFrom(inner, "a", Ref("source"));
    AddSelect(inner, Anon({new Syntax::NamedExpression("a", Ref("a"))}));
    auto* query = OuterWithTransparentIdentifier(inner, "a");

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    // Only the hoisted from clause and the select remain -- no let clause.
    ASSERT_EQ(query->Clauses().Count(), 2);
    auto* select = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(1));
    ASSERT_NE(select, nullptr);
    auto* replaced = dynamic_cast<Syntax::IdentifierExpression*>(select->Expression());
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->Identifier(), "a");
}

// The replacement identifier receives the `ILVariableResolveResult` the hoisted initializer
// carried.
TEST(CombineQueryExpressionsTest, CopiesResolveResultAnnotationToReplacementIdentifier) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    AddFrom(inner, "a", Ref("source"));
    auto* initializer = Ref("a");
    AddSelect(inner, Anon({initializer}));
    auto variable = std::make_shared<::ILSpy::Decompiler::IL::ILVariable>(
        ::ILSpy::Decompiler::IL::VariableKind::Local,
        std::make_shared<StubType>("T"));
    initializer->AddAnnotation(std::make_shared<::ILSpy::Decompiler::CSharp::ILVariableResolveResult>(
        variable));
    auto* query = OuterWithTransparentIdentifier(inner, "a");

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    ASSERT_EQ(query->Clauses().Count(), 2);
    auto* select = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(1));
    ASSERT_NE(select, nullptr);
    auto* replaced = dynamic_cast<Syntax::IdentifierExpression*>(select->Expression());
    ASSERT_NE(replaced, nullptr);
    EXPECT_NE(replaced->Annotation<::ILSpy::Decompiler::CSharp::ILVariableResolveResult>(),
              nullptr);
}

// The replacement identifier takes the transparent reference's type arguments.
TEST(CombineQueryExpressionsTest, MovesTypeArgumentsToReplacementIdentifier) {
    TransformFixture fixture;
    auto context = fixture.MakeContext();
    auto* inner = new Syntax::QueryExpression();
    AddFrom(inner, "a", Ref("source"));
    AddSelect(inner, Anon({Ref("a")}));
    auto* query = new Syntax::QueryExpression();
    AddFrom(query, "<>h__TransparentIdentifier0", inner);
    auto* intType = new Syntax::SimpleType("int");
    auto* transparentRef = Member(Ref("<>h__TransparentIdentifier0"), "a");
    transparentRef->TypeArguments().Add(intType);
    AddSelect(query, transparentRef);

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    ASSERT_EQ(query->Clauses().Count(), 2);
    auto* select = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(1));
    ASSERT_NE(select, nullptr);
    auto* replaced = dynamic_cast<Syntax::IdentifierExpression*>(select->Expression());
    ASSERT_NE(replaced, nullptr);
    ASSERT_EQ(replaced->TypeArguments().Count(), 1);
    EXPECT_EQ(replaced->TypeArguments().At(0), intType);
}

// With `QueryExpressions` off the transform is a no-op.
TEST(CombineQueryExpressionsTest, QueryExpressionsDisabledIsNoOp) {
    TransformFixture fixture;
    fixture.settings.SetQueryExpressions(false);
    auto context = fixture.MakeContext();
    auto* query = new Syntax::QueryExpression();
    auto* z = Ref("z");
    auto* cast = Member(z, "Cast");
    cast->TypeArguments().Add(new Syntax::SimpleType("int"));
    auto* from = AddFrom(query, "x", new Syntax::InvocationExpression(cast));

    Transforms::CombineQueryExpressions transform;
    transform.Run(*query, context);

    EXPECT_EQ(from->Type(), nullptr);
    EXPECT_EQ(query->Clauses().Count(), 1);
    EXPECT_NE(dynamic_cast<Syntax::InvocationExpression*>(from->Expression()), nullptr);
}
