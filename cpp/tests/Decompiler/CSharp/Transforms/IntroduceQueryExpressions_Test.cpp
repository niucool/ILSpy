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

// Tests for `IntroduceQueryExpressions`: the LINQ method-chain to query-expression rewrite
// (`Select`/`Where`/`GroupBy`/`SelectMany`/`OrderBy`/`ThenBy`/`Join`/`GroupJoin`), the
// degenerate-select insertion, the nested-degenerate-query combination (including the
// `ILVariableResolveResult` range-variable rebinding), the discard-assignment wrap, and the
// guard no-ops. The query trees are built directly (the transform is resolver-free).

#include "Decompiler/CSharp/Transforms/IntroduceQueryExpressions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/QueryFromClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryGroupClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryJoinClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrderClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrdering.hpp"
#include "Decompiler/CSharp/Syntax/QuerySelectClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryWhereClause.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
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
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace CSharp = ::ILSpy::Decompiler::CSharp;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the CombineQueryExpressions suite shape).
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
        // Disable the discard-assignment wrap by default so the query-building tests observe
        // the bare query; the discard case is exercised explicitly.
        settings.SetDiscards(false);
    }

    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A minimal named `IType` (only used as the variable's type; the transform never reads it).
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

// ---- tree builders (raw `new`; the tree owns the nodes and leaks on purpose) ------------

Syntax::IdentifierExpression* Ref(const std::string& name) {
    return new Syntax::IdentifierExpression(name);
}

Syntax::MemberReferenceExpression* Member(Syntax::Expression* target,
                                          const std::string& memberName) {
    return new Syntax::MemberReferenceExpression(target, memberName);
}

Syntax::LambdaExpression* Lambda1(const std::string& parameter, Syntax::Expression* body) {
    auto* lambda = new Syntax::LambdaExpression();
    auto* p = new Syntax::ParameterDeclaration();
    p->Name(parameter);
    lambda->Parameters().Add(p);
    lambda->Body(body);
    return lambda;
}

Syntax::LambdaExpression* Lambda2(const std::string& p1, const std::string& p2,
                                  Syntax::Expression* body) {
    auto* lambda = new Syntax::LambdaExpression();
    auto* parameter1 = new Syntax::ParameterDeclaration();
    parameter1->Name(p1);
    lambda->Parameters().Add(parameter1);
    auto* parameter2 = new Syntax::ParameterDeclaration();
    parameter2->Name(p2);
    lambda->Parameters().Add(parameter2);
    lambda->Body(body);
    return lambda;
}

// The receiver chain `src.Foo()`: an invocation, so a following query call counts as a
// "complex query" without introducing a nested query clause.
Syntax::InvocationExpression* FooReceiver() {
    return new Syntax::InvocationExpression(Member(Ref("src"), "Foo"));
}

Syntax::InvocationExpression* Invoke(const std::string& method, Syntax::Expression* target) {
    return new Syntax::InvocationExpression(Member(target, method));
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

// Runs the transform over a block holding one expression statement wrapping `expression`,
// and returns the expression statement afterward.
Syntax::ExpressionStatement* RunOn(TransformFixture& fixture, Syntax::Expression* expression) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    auto* statement = new Syntax::ExpressionStatement(expression);
    block->Statements().Add(statement);
    Transforms::IntroduceQueryExpressions transform;
    transform.Run(*block, context);
    return statement;
}

// The annotation lookup helper over the `ILVariableResolveResult` holder.
CSharp::ILVariableResolveResult* ResolveResult(Syntax::AstNode* node) {
    return node->Annotation<CSharp::ILVariableResolveResult>();
}

} // namespace

// With `QueryExpressions` off, a degenerate query is left untouched.
TEST(IntroduceQueryExpressionsTest, QueryExpressionsDisabledIsNoOp) {
    TransformFixture fixture;
    fixture.settings.SetQueryExpressions(false);
    auto* query = new Syntax::QueryExpression();
    AddFrom(query, "x", Ref("src"));

    RunOn(fixture, query);

    EXPECT_EQ(query->Clauses().Count(), 1);
    EXPECT_EQ(dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(0)), nullptr);
}

// A query that does not end in `select`/`group` gets a degenerate `select` of its from range
// variable, with the from clause's annotations copied.
TEST(IntroduceQueryExpressionsTest, AddsDegenerateSelectClause) {
    TransformFixture fixture;
    auto* query = new Syntax::QueryExpression();
    AddFrom(query, "x", Ref("src"));

    RunOn(fixture, query);

    ASSERT_EQ(query->Clauses().Count(), 2);
    auto* select = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(1));
    ASSERT_NE(select, nullptr);
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(select->Expression());
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->Identifier(), "x");
}

// A query already ending in `select` is left alone.
TEST(IntroduceQueryExpressionsTest, KeepsNonDegenerateQuery) {
    TransformFixture fixture;
    auto* query = new Syntax::QueryExpression();
    AddFrom(query, "x", Ref("src"));
    AddSelect(query, Ref("x"));

    RunOn(fixture, query);

    EXPECT_EQ(query->Clauses().Count(), 2);
}

// `expr.Select(x => x)` (on a complex-query receiver) becomes `from x in expr select x`.
TEST(IntroduceQueryExpressionsTest, BuildsSelectQuery) {
    TransformFixture fixture;
    auto* receiver = FooReceiver();
    auto* select = Invoke("Select", receiver);
    select->Arguments().Add(Lambda1("x", Ref("x")));

    auto* statement = RunOn(fixture, select);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 2);
    auto* from = dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0));
    ASSERT_NE(from, nullptr);
    EXPECT_EQ(from->Identifier(), "x");
    EXPECT_EQ(from->Expression(), receiver);
    auto* selectClause = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(1));
    ASSERT_NE(selectClause, nullptr);
    // The body matches the range variable, so it is wrapped in parentheses.
    auto* parenthesized =
        dynamic_cast<Syntax::ParenthesizedExpression*>(selectClause->Expression());
    ASSERT_NE(parenthesized, nullptr);
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(parenthesized->Expression());
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->Identifier(), "x");
}

// `expr.Where(x => pred).Select(y => y)` flattens to `from x in expr where pred select y`
// (the nested degenerate inner query is combined).
TEST(IntroduceQueryExpressionsTest, BuildsWhereAndCombinesNestedDegenerateQuery) {
    TransformFixture fixture;
    auto* receiver = FooReceiver();
    auto* where = Invoke("Where", receiver);
    auto* predicate = Member(Ref("x"), "Pred");
    where->Arguments().Add(Lambda1("x", predicate));
    auto* select = Invoke("Select", where);
    select->Arguments().Add(Lambda1("y", Ref("y")));

    auto* statement = RunOn(fixture, select);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 3);
    auto* from = dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0));
    ASSERT_NE(from, nullptr);
    EXPECT_EQ(from->Identifier(), "x");
    EXPECT_EQ(from->Expression(), receiver);
    auto* whereClause = dynamic_cast<Syntax::QueryWhereClause*>(query->Clauses().At(1));
    ASSERT_NE(whereClause, nullptr);
    EXPECT_EQ(whereClause->Condition(), predicate);
    auto* selectClause = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2));
    ASSERT_NE(selectClause, nullptr);
    auto* parenthesized =
        dynamic_cast<Syntax::ParenthesizedExpression*>(selectClause->Expression());
    ASSERT_NE(parenthesized, nullptr);
    auto* selectIdent = dynamic_cast<Syntax::IdentifierExpression*>(parenthesized->Expression());
    ASSERT_NE(selectIdent, nullptr);
    EXPECT_EQ(selectIdent->Identifier(), "y");
}

// `src.GroupBy(x => key, x => elem)` becomes `from x in src group elem by key`.
TEST(IntroduceQueryExpressionsTest, BuildsTwoArgumentGroupQuery) {
    TransformFixture fixture;
    auto* key = Member(Ref("x"), "Key");
    auto* element = Member(Ref("x"), "Element");
    auto* groupBy = Invoke("GroupBy", Ref("src"));
    groupBy->Arguments().Add(Lambda1("x", key));
    groupBy->Arguments().Add(Lambda1("x", element));

    auto* statement = RunOn(fixture, groupBy);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 2);
    EXPECT_NE(dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0)), nullptr);
    auto* groupClause = dynamic_cast<Syntax::QueryGroupClause*>(query->Clauses().At(1));
    ASSERT_NE(groupClause, nullptr);
    EXPECT_EQ(groupClause->Projection(), static_cast<Syntax::Expression*>(element));
    EXPECT_EQ(groupClause->Key(), static_cast<Syntax::Expression*>(key));
    EXPECT_NE(groupClause->Annotation<CSharp::QueryGroupClauseAnnotation>(), nullptr);
}

// `src.GroupBy(x => key)` becomes `from x in src group x by key` (the projection is a
// synthesized identifier named after the range variable).
TEST(IntroduceQueryExpressionsTest, BuildsOneArgumentGroupQuery) {
    TransformFixture fixture;
    auto* key = Member(Ref("x"), "Key");
    auto* groupBy = Invoke("GroupBy", Ref("src"));
    groupBy->Arguments().Add(Lambda1("x", key));

    auto* statement = RunOn(fixture, groupBy);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 2);
    auto* groupClause = dynamic_cast<Syntax::QueryGroupClause*>(query->Clauses().At(1));
    ASSERT_NE(groupClause, nullptr);
    auto* projection = dynamic_cast<Syntax::IdentifierExpression*>(groupClause->Projection());
    ASSERT_NE(projection, nullptr);
    EXPECT_EQ(projection->Identifier(), "x");
    EXPECT_EQ(groupClause->Key(), static_cast<Syntax::Expression*>(key));
}

// `src.SelectMany(x => coll, (x, y) => x)` becomes two from clauses plus a select; the
// select body matching the range variable is parenthesized.
TEST(IntroduceQueryExpressionsTest, BuildsSelectManyQuery) {
    TransformFixture fixture;
    auto* coll = Member(Ref("x"), "Coll");
    auto* selectMany = Invoke("SelectMany", Ref("src"));
    selectMany->Arguments().Add(Lambda1("x", coll));
    selectMany->Arguments().Add(Lambda2("x", "y", Ref("x")));

    auto* statement = RunOn(fixture, selectMany);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 3);
    auto* from1 = dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0));
    ASSERT_NE(from1, nullptr);
    EXPECT_EQ(from1->Identifier(), "x");
    auto* from2 = dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(1));
    ASSERT_NE(from2, nullptr);
    EXPECT_EQ(from2->Identifier(), "y");
    EXPECT_EQ(from2->Expression(), static_cast<Syntax::Expression*>(coll));
    auto* selectClause = dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2));
    ASSERT_NE(selectClause, nullptr);
    auto* parenthesized =
        dynamic_cast<Syntax::ParenthesizedExpression*>(selectClause->Expression());
    ASSERT_NE(parenthesized, nullptr);
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(parenthesized->Expression());
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->Identifier(), "x");
}

// `expr.OrderBy(x => key)` becomes `from x in expr orderby key select x`.
TEST(IntroduceQueryExpressionsTest, BuildsOrderByQuery) {
    TransformFixture fixture;
    auto* receiver = FooReceiver();
    auto* key = Member(Ref("x"), "Key");
    auto* orderBy = Invoke("OrderBy", receiver);
    orderBy->Arguments().Add(Lambda1("x", key));

    auto* statement = RunOn(fixture, orderBy);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 3);
    ASSERT_NE(dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0)), nullptr);
    auto* orderClause = dynamic_cast<Syntax::QueryOrderClause*>(query->Clauses().At(1));
    ASSERT_NE(orderClause, nullptr);
    ASSERT_EQ(orderClause->Orderings().Count(), 1);
    EXPECT_EQ(orderClause->Orderings().At(0)->Expression(), static_cast<Syntax::Expression*>(key));
    EXPECT_EQ(orderClause->Orderings().At(0)->Direction(), Syntax::QueryOrderingDirection::None);
    ASSERT_NE(dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2)), nullptr);
}

// `expr.OrderBy(x => k1).ThenByDescending(x => k2)` builds an order clause carrying both
// orderings (the nested degenerate inner order query is combined, so two order clauses remain).
TEST(IntroduceQueryExpressionsTest, BuildsThenByChain) {
    TransformFixture fixture;
    auto* receiver = FooReceiver();
    auto* key1 = Member(Ref("x"), "K1");
    auto* key2 = Member(Ref("x"), "K2");
    auto* orderBy = Invoke("OrderBy", receiver);
    orderBy->Arguments().Add(Lambda1("x", key1));
    auto* thenBy = Invoke("ThenByDescending", orderBy);
    thenBy->Arguments().Add(Lambda1("x", key2));

    auto* statement = RunOn(fixture, thenBy);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    // The whole `OrderBy().ThenByDescending()` chain is consumed by one query whose from
    // source is the innermost order call's receiver, so a single order clause carries both
    // orderings.
    ASSERT_EQ(query->Clauses().Count(), 3);
    ASSERT_NE(dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0)), nullptr);
    auto* orderClause = dynamic_cast<Syntax::QueryOrderClause*>(query->Clauses().At(1));
    ASSERT_NE(orderClause, nullptr);
    ASSERT_EQ(orderClause->Orderings().Count(), 2);
    EXPECT_EQ(orderClause->Orderings().At(0)->Expression(), static_cast<Syntax::Expression*>(key1));
    EXPECT_EQ(orderClause->Orderings().At(0)->Direction(), Syntax::QueryOrderingDirection::None);
    EXPECT_EQ(orderClause->Orderings().At(1)->Expression(), static_cast<Syntax::Expression*>(key2));
    EXPECT_EQ(orderClause->Orderings().At(1)->Direction(),
              Syntax::QueryOrderingDirection::Descending);
    ASSERT_NE(dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2)), nullptr);
}

// `src.Join(inner, x => k1, y => k2, (x, y) => x)` becomes a join clause plus a select.
TEST(IntroduceQueryExpressionsTest, BuildsJoinQuery) {
    TransformFixture fixture;
    auto* inner = Ref("inner");
    auto* key1 = Member(Ref("x"), "K1");
    auto* key2 = Member(Ref("y"), "K2");
    auto* join = Invoke("Join", Ref("src"));
    join->Arguments().Add(inner);
    join->Arguments().Add(Lambda1("x", key1));
    join->Arguments().Add(Lambda1("y", key2));
    join->Arguments().Add(Lambda2("x", "y", Ref("x")));

    auto* statement = RunOn(fixture, join);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 3);
    ASSERT_NE(dynamic_cast<Syntax::QueryFromClause*>(query->Clauses().At(0)), nullptr);
    auto* joinClause = dynamic_cast<Syntax::QueryJoinClause*>(query->Clauses().At(1));
    ASSERT_NE(joinClause, nullptr);
    EXPECT_EQ(joinClause->JoinIdentifier(), "y");
    EXPECT_EQ(joinClause->InExpression(), static_cast<Syntax::Expression*>(inner));
    EXPECT_EQ(joinClause->OnExpression(), static_cast<Syntax::Expression*>(key1));
    EXPECT_EQ(joinClause->EqualsExpression(), static_cast<Syntax::Expression*>(key2));
    EXPECT_FALSE(joinClause->IntoIdentifier().has_value());
    EXPECT_FALSE(joinClause->IsGroupJoin());
    EXPECT_NE(joinClause->Annotation<CSharp::QueryJoinClauseAnnotation>(), nullptr);
    ASSERT_NE(dynamic_cast<Syntax::QuerySelectClause*>(query->Clauses().At(2)), nullptr);
}

// `src.GroupJoin(inner, x => k1, y => k2, (x, g) => x)` accepts the mismatched inner name
// (the `into` identifier) and records it.
TEST(IntroduceQueryExpressionsTest, BuildsGroupJoinQuery) {
    TransformFixture fixture;
    auto* inner = Ref("inner");
    auto* key1 = Member(Ref("x"), "K1");
    auto* key2 = Member(Ref("y"), "K2");
    auto* join = Invoke("GroupJoin", Ref("src"));
    join->Arguments().Add(inner);
    join->Arguments().Add(Lambda1("x", key1));
    join->Arguments().Add(Lambda1("y", key2));
    join->Arguments().Add(Lambda2("x", "g", Ref("x")));

    auto* statement = RunOn(fixture, join);

    auto* query = dynamic_cast<Syntax::QueryExpression*>(statement->Expression());
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->Clauses().Count(), 3);
    auto* joinClause = dynamic_cast<Syntax::QueryJoinClause*>(query->Clauses().At(1));
    ASSERT_NE(joinClause, nullptr);
    EXPECT_EQ(joinClause->JoinIdentifier(), "y");
    ASSERT_TRUE(joinClause->IntoIdentifier().has_value());
    EXPECT_EQ(*joinClause->IntoIdentifier(), "g");
    EXPECT_TRUE(joinClause->IsGroupJoin());
}

// A `Select` with the wrong argument count is left as an invocation.
TEST(IntroduceQueryExpressionsTest, SelectWithWrongArgumentCountIsIgnored) {
    TransformFixture fixture;
    auto* select = Invoke("Select", FooReceiver());

    auto* statement = RunOn(fixture, select);

    EXPECT_EQ(statement->Expression(), static_cast<Syntax::Expression*>(select));
}

// A bare `src.Where(...)` is not a complex query (its grandparent is not a query clause) and
// is left alone.
TEST(IntroduceQueryExpressionsTest, BareWhereIsIgnored) {
    TransformFixture fixture;
    auto* where = Invoke("Where", Ref("src"));
    where->Arguments().Add(Lambda1("x", Member(Ref("x"), "Pred")));

    auto* statement = RunOn(fixture, where);

    EXPECT_EQ(statement->Expression(), static_cast<Syntax::Expression*>(where));
}

// A null-conditional join source is rejected.
TEST(IntroduceQueryExpressionsTest, JoinWithNullConditionalSourceIsIgnored) {
    TransformFixture fixture;
    auto* nullableInner = new Syntax::UnaryOperatorExpression(
        Ref("inner"), Syntax::UnaryOperatorType::NullConditional);
    auto* join = Invoke("Join", Ref("src"));
    join->Arguments().Add(nullableInner);
    join->Arguments().Add(Lambda1("x", Member(Ref("x"), "K1")));
    join->Arguments().Add(Lambda1("y", Member(Ref("y"), "K2")));
    join->Arguments().Add(Lambda2("x", "y", Ref("x")));

    auto* statement = RunOn(fixture, join);

    EXPECT_EQ(statement->Expression(), static_cast<Syntax::Expression*>(join));
}

// A non-query method call is left alone.
TEST(IntroduceQueryExpressionsTest, NonQueryMethodIsIgnored) {
    TransformFixture fixture;
    auto* call = Invoke("Foo", Ref("src"));

    auto* statement = RunOn(fixture, call);

    EXPECT_EQ(statement->Expression(), static_cast<Syntax::Expression*>(call));
}

// With `Discards` on, a query replacing an expression statement is wrapped in `_ = query`.
TEST(IntroduceQueryExpressionsTest, WrapsInDiscardAssignmentWhenDiscardsEnabled) {
    TransformFixture fixture;
    fixture.settings.SetDiscards(true);
    auto* selectMany = Invoke("SelectMany", Ref("src"));
    selectMany->Arguments().Add(Lambda1("x", Member(Ref("x"), "Coll")));
    selectMany->Arguments().Add(Lambda2("x", "y", Ref("y")));

    auto* statement = RunOn(fixture, selectMany);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(statement->Expression());
    ASSERT_NE(assignment, nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->Identifier(), "_");
    EXPECT_NE(dynamic_cast<Syntax::QueryExpression*>(assignment->Right()), nullptr);
}

// Combining a nested degenerate query rebinds the inner range variable references to the
// outer variable (the `ILVariableResolveResult` annotations drive the rewrite).
TEST(IntroduceQueryExpressionsTest, CombineRebindsRangeVariableReferences) {
    TransformFixture fixture;
    auto innerVariable = std::make_shared<::ILSpy::Decompiler::IL::ILVariable>(
        ::ILSpy::Decompiler::IL::VariableKind::Local, std::make_shared<StubType>("T"));
    innerVariable->Name = "x";
    auto outerVariable = std::make_shared<::ILSpy::Decompiler::IL::ILVariable>(
        ::ILSpy::Decompiler::IL::VariableKind::Local, std::make_shared<StubType>("T"));
    outerVariable->Name = "y";

    auto* innerQuery = new Syntax::QueryExpression();
    auto* innerFrom = AddFrom(innerQuery, "x", Ref("src"));
    innerFrom->AddAnnotation(std::make_shared<CSharp::ILVariableResolveResult>(innerVariable));
    auto* targetReference = Ref("x");
    targetReference->AddAnnotation(
        std::make_shared<CSharp::ILVariableResolveResult>(innerVariable));
    auto* condition = Member(targetReference, "Pred");
    auto* whereClause = new Syntax::QueryWhereClause();
    whereClause->Condition(condition);
    innerQuery->Clauses().Add(whereClause);

    auto* outerQuery = new Syntax::QueryExpression();
    auto* outerFrom = AddFrom(outerQuery, "y", innerQuery);
    outerFrom->AddAnnotation(std::make_shared<CSharp::ILVariableResolveResult>(outerVariable));
    AddSelect(outerQuery, Ref("y"));

    RunOn(fixture, outerQuery);

    // The from y clause was replaced by the hoisted from x, the where clause followed, and the
    // select stays last.
    ASSERT_EQ(outerQuery->Clauses().Count(), 3);
    EXPECT_EQ(outerQuery->Clauses().At(0), innerFrom);
    EXPECT_EQ(outerQuery->Clauses().At(1), whereClause);

    // The reference `x.Pred` is rebound to the outer variable `y`.
    auto* where = dynamic_cast<Syntax::QueryWhereClause*>(outerQuery->Clauses().At(1));
    ASSERT_NE(where, nullptr);
    auto* member = dynamic_cast<Syntax::MemberReferenceExpression*>(where->Condition());
    ASSERT_NE(member, nullptr);
    auto* target = dynamic_cast<Syntax::IdentifierExpression*>(member->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Identifier(), "y");
    auto* rebind = ResolveResult(member->Target());
    ASSERT_NE(rebind, nullptr);
    EXPECT_EQ(rebind->Variable(), outerVariable.get());
}
