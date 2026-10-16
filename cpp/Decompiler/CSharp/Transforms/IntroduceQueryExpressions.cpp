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

#include "Decompiler/CSharp/Transforms/IntroduceQueryExpressions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryFromClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryGroupClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryJoinClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrderClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrdering.hpp"
#include "Decompiler/CSharp/Syntax/QuerySelectClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryWhereClause.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::AssignmentExpression;
using Syntax::Expression;
using Syntax::Identifier;
using Syntax::IdentifierExpression;
using Syntax::InvocationExpression;
using Syntax::LambdaExpression;
using Syntax::MemberReferenceExpression;
using Syntax::ParameterDeclaration;
using Syntax::ParenthesizedExpression;
using Syntax::QueryClause;
using Syntax::QueryExpression;
using Syntax::QueryFromClause;
using Syntax::QueryGroupClause;
using Syntax::QueryJoinClause;
using Syntax::QueryOrderClause;
using Syntax::QueryOrdering;
using Syntax::QueryOrderingDirection;
using Syntax::QuerySelectClause;
using Syntax::QueryWhereClause;
using Syntax::UnaryOperatorExpression;
using Syntax::UnaryOperatorType;

namespace {

// The C# `parameter.Name!` reads (the parameter grammar guarantees a name; the port's
// `ParameterDeclaration::Name()` is an optional over the backing token, so a missing token
// yields the empty string).
std::string ParameterName(ParameterDeclaration* parameter) {
    auto name = parameter->Name();
    return name.has_value() ? *name : std::string();
}

// The C# `rootNode.Descendants.OfType<QueryExpression>()` walk is lazy, so a query detached
// by an earlier iteration is never visited. The port snapshots `Descendants()` before the
// loop, so it skips a query that is no longer attached to the walk root -- the equivalent of
// the lazy iterator not reaching a detached node.
bool IsAttachedTo(AstNode* node, AstNode* root) {
    for (AstNode* n = node; n != nullptr; n = n->Parent()) {
        if (n == root)
            return true;
    }
    return false;
}

} // namespace

void IntroduceQueryExpressions::Run(AstNode& rootNode, TransformContext& context) {
    // The C# `if (!context.Settings.QueryExpressions) return;`.
    if (!context.Settings().QueryExpressions())
        return;
    context_ = &context;
    try {
        DecompileQueries(&rootNode);
        // The C# is `foreach (QueryExpression query in rootNode.Descendants.OfType<...>())`.
        for (AstNode* node : rootNode.Descendants()) {
            auto* query = dynamic_cast<QueryExpression*>(node);
            if (query == nullptr || !IsAttachedTo(query, &rootNode))
                continue;
            if (query->Clauses().Count() == 0)
                throw Util::InvalidCastException{};
            auto* fromClause = dynamic_cast<QueryFromClause*>(query->Clauses().At(0));
            if (fromClause == nullptr)
                throw Util::InvalidCastException{};
            if (IsDegenerateQuery(query)) {
                // The C# `query.Clauses.Add(new QuerySelectClause { Expression = new
                // IdentifierExpression(fromClause.Identifier).CopyAnnotationsFrom(fromClause) })`.
                context_->Step("Add degenerate query select clause", query);
                auto* select = new QuerySelectClause();
                select->Expression(
                    ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(
                        new IdentifierExpression(fromClause->Identifier()), *fromClause));
                query->Clauses().Add(select);
            }
            // See if the data source of this query is a degenerate query, and combine the
            // queries if possible.
            auto* innerQuery = dynamic_cast<QueryExpression*>(fromClause->Expression());
            while (innerQuery != nullptr && IsDegenerateQuery(innerQuery)) {
                if (innerQuery->Clauses().Count() == 0)
                    throw Util::InvalidCastException{};
                auto* innerFromClause =
                    dynamic_cast<QueryFromClause*>(innerQuery->Clauses().At(0));
                if (innerFromClause == nullptr)
                    throw Util::InvalidCastException{};
                auto* innerRR = innerFromClause->Annotation<ILVariableResolveResult>();
                IL::ILVariablePtr innerVariable =
                    innerRR != nullptr ? innerRR->VariableHandle() : nullptr;
                auto* rangeRR = fromClause->Annotation<ILVariableResolveResult>();
                IL::ILVariablePtr rangeVariable =
                    rangeRR != nullptr ? rangeRR->VariableHandle() : nullptr;
                context_->Step("Combine nested query clauses", fromClause);
                // Replace the fromClause with all clauses from the inner query.
                fromClause->Remove();
                QueryClause* insertionPos = nullptr;
                while (innerQuery->Clauses().Count() > 0) {
                    QueryClause* clause = innerQuery->Clauses().At(0);
                    CombineRangeVariables(clause, innerVariable, rangeVariable);
                    clause->Remove();
                    query->Clauses().InsertAfter(insertionPos, clause);
                    insertionPos = clause;
                }
                context_->EndStep(innerFromClause);
                fromClause = innerFromClause;
                innerQuery = dynamic_cast<QueryExpression*>(fromClause->Expression());
            }
        }
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

void IntroduceQueryExpressions::CombineRangeVariables(QueryClause* clause,
                                                      const IL::ILVariablePtr& oldVariable,
                                                      const IL::ILVariablePtr& newVariable) {
    if (!oldVariable || !newVariable)
        return;
    // The C# `foreach (var identifier in clause.DescendantNodes().OfType<Identifier>())`. The
    // snapshot is stable across the single-identifier replacements below (an `Identifier` has
    // no children).
    for (AstNode* node : clause->DescendantNodes()) {
        auto* identifier = dynamic_cast<Identifier*>(node);
        if (identifier == nullptr)
            continue;
        AstNode* parent = identifier->Parent();
        if (parent == nullptr)
            continue;
        auto* resolveResult = parent->Annotation<ILVariableResolveResult>();
        IL::ILVariable* variable =
            resolveResult != nullptr ? resolveResult->Variable() : nullptr;
        if (variable == oldVariable.get()) {
            context_->Step("Combine query range variables", identifier);
            parent->RemoveAnnotations<ILVariableResolveResult>();
            parent->AddAnnotation(std::make_shared<ILVariableResolveResult>(newVariable));
            auto* newIdentifier = Identifier::Create(newVariable->Name);
            identifier->ReplaceWith(newIdentifier);
            context_->EndStep(newIdentifier);
        }
    }
}

bool IntroduceQueryExpressions::IsDegenerateQuery(QueryExpression* query) {
    if (query == nullptr)
        return false;
    QueryClause* lastClause = query->Clauses().LastOrNull();
    return !(dynamic_cast<QuerySelectClause*>(lastClause) != nullptr
             || dynamic_cast<QueryGroupClause*>(lastClause) != nullptr);
}

void IntroduceQueryExpressions::DecompileQueries(AstNode* node) {
    Expression* query = DecompileQuery(dynamic_cast<InvocationExpression*>(node));
    if (query != nullptr) {
        if (dynamic_cast<Syntax::ExpressionStatement*>(node->Parent()) != nullptr
            && CanUseDiscardAssignment()) {
            query = new AssignmentExpression(new IdentifierExpression("_"), query);
        }
        node->ReplaceWith(query);
        context_->EndStep(query);
    }

    // The C# `for (AstNode? child = (query ?? node).FirstChild; child != null; child = next)`
    // with the next sibling captured before the recursion.
    AstNode* next;
    AstNode* container = query != nullptr ? static_cast<AstNode*>(query) : node;
    for (AstNode* child = container->FirstChild(); child != nullptr; child = next) {
        next = child->NextSibling();
        DecompileQueries(child);
    }
}

bool IntroduceQueryExpressions::CanUseDiscardAssignment() {
    // The C# TODO (checking whether a variable named '_' is in scope) is not implemented; the
    // setting alone gates the rewrite.
    return context_->Settings().Discards();
}

Syntax::QueryExpression* IntroduceQueryExpressions::DecompileQuery(
    InvocationExpression* invocation) {
    if (invocation == nullptr)
        return nullptr;
    auto* mre = dynamic_cast<MemberReferenceExpression*>(invocation->Target());
    if (mre == nullptr || IsNullConditional(mre->Target()))
        return nullptr;
    const std::string memberName = mre->MemberName();
    if (memberName == "Select") {
        if (invocation->Arguments().Count() != 1)
            return nullptr;
        if (!IsComplexQuery(mre))
            return nullptr;
        Expression* expr = invocation->Arguments().At(0);
        ParameterDeclaration* parameter = nullptr;
        Expression* body = nullptr;
        if (MatchSimpleLambda(expr, parameter, body)) {
            context_->Step("Build select query", invocation);
            auto* query = new QueryExpression();
            query->Clauses().Add(MakeFromClause(parameter, Syntax::Detach(mre->Target())));
            auto* select = new QuerySelectClause();
            select->Expression(WrapExpressionInParenthesesIfNecessary(
                Syntax::Detach(body), ParameterName(parameter)));
            ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(select, *expr);
            query->Clauses().Add(select);
            return query;
        }
        return nullptr;
    }
    if (memberName == "GroupBy") {
        if (invocation->Arguments().Count() == 2) {
            Expression* keyLambda = invocation->Arguments().At(0);
            Expression* projectionLambda = invocation->Arguments().At(1);
            ParameterDeclaration* parameter1 = nullptr;
            Expression* keySelector = nullptr;
            ParameterDeclaration* parameter2 = nullptr;
            Expression* elementSelector = nullptr;
            if (MatchSimpleLambda(keyLambda, parameter1, keySelector)
                && MatchSimpleLambda(projectionLambda, parameter2, elementSelector)
                && ParameterName(parameter1) == ParameterName(parameter2)) {
                context_->Step("Build group query", invocation);
                auto* query = new QueryExpression();
                query->Clauses().Add(
                    MakeFromClause(parameter1, Syntax::Detach(mre->Target())));
                auto* queryGroupClause = new QueryGroupClause();
                queryGroupClause->Projection(Syntax::Detach(elementSelector));
                queryGroupClause->Key(Syntax::Detach(keySelector));
                queryGroupClause->AddAnnotation(std::make_shared<QueryGroupClauseAnnotation>(
                    GetILFunction(*keyLambda), GetILFunction(*projectionLambda)));
                query->Clauses().Add(queryGroupClause);
                return query;
            }
        } else if (invocation->Arguments().Count() == 1) {
            Expression* lambda = invocation->Arguments().At(0);
            ParameterDeclaration* parameter = nullptr;
            Expression* keySelector = nullptr;
            if (MatchSimpleLambda(lambda, parameter, keySelector)) {
                context_->Step("Build group query", invocation);
                auto* query = new QueryExpression();
                query->Clauses().Add(
                    MakeFromClause(parameter, Syntax::Detach(mre->Target())));
                auto* queryGroupClause = new QueryGroupClause();
                queryGroupClause->Projection(::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(
                    new IdentifierExpression(ParameterName(parameter)), *parameter));
                queryGroupClause->Key(Syntax::Detach(keySelector));
                query->Clauses().Add(queryGroupClause);
                return query;
            }
        }
        return nullptr;
    }
    if (memberName == "SelectMany") {
        if (invocation->Arguments().Count() != 2)
            return nullptr;
        Expression* fromExpressionLambda = invocation->Arguments().At(0);
        ParameterDeclaration* parameter = nullptr;
        Expression* collectionSelector = nullptr;
        if (!MatchSimpleLambda(fromExpressionLambda, parameter, collectionSelector))
            return nullptr;
        if (IsNullConditional(collectionSelector))
            return nullptr;
        auto* lambda = dynamic_cast<LambdaExpression*>(invocation->Arguments().At(1));
        if (lambda != nullptr && lambda->Parameters().Count() == 2
            && dynamic_cast<Expression*>(lambda->Body()) != nullptr) {
            ParameterDeclaration* p1 = lambda->Parameters().At(0);
            ParameterDeclaration* p2 = lambda->Parameters().At(1);
            if (ParameterName(p1) == ParameterName(parameter)) {
                context_->Step("Build select-many query", invocation);
                auto* query = new QueryExpression();
                query->Clauses().Add(MakeFromClause(p1, Syntax::Detach(mre->Target())));
                query->Clauses().Add(::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(
                    MakeFromClause(p2, Syntax::Detach(collectionSelector)),
                    *fromExpressionLambda));
                auto* select = new QuerySelectClause();
                select->Expression(WrapExpressionInParenthesesIfNecessary(
                    static_cast<Expression*>(Syntax::Detach(lambda->Body())),
                    ParameterName(parameter)));
                query->Clauses().Add(select);
                return query;
            }
        }
        return nullptr;
    }
    if (memberName == "Where") {
        if (invocation->Arguments().Count() != 1)
            return nullptr;
        if (!IsComplexQuery(mre))
            return nullptr;
        Expression* expr = invocation->Arguments().At(0);
        ParameterDeclaration* parameter = nullptr;
        Expression* body = nullptr;
        if (MatchSimpleLambda(expr, parameter, body)) {
            context_->Step("Build where query", invocation);
            auto* query = new QueryExpression();
            query->Clauses().Add(MakeFromClause(parameter, Syntax::Detach(mre->Target())));
            auto* where = new QueryWhereClause();
            where->Condition(Syntax::Detach(body));
            ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(where, *expr);
            query->Clauses().Add(where);
            return query;
        }
        return nullptr;
    }
    if (memberName == "OrderBy" || memberName == "OrderByDescending"
        || memberName == "ThenBy" || memberName == "ThenByDescending") {
        if (invocation->Arguments().Count() != 1)
            return nullptr;
        if (!IsComplexQuery(mre))
            return nullptr;
        Expression* lambda = invocation->Arguments().At(0);
        ParameterDeclaration* parameter = nullptr;
        Expression* orderExpression = nullptr;
        if (MatchSimpleLambda(lambda, parameter, orderExpression)) {
            if (ValidateThenByChain(invocation, ParameterName(parameter))) {
                context_->Step("Build order query", invocation);
                auto* orderClause = new QueryOrderClause();
                while (mre->MemberName() == "ThenBy" || mre->MemberName() == "ThenByDescending") {
                    // Insert the new ordering at the beginning.
                    auto* ordering = new QueryOrdering();
                    ordering->Expression(Syntax::Detach(orderExpression));
                    ordering->Direction(mre->MemberName() == "ThenBy"
                                            ? QueryOrderingDirection::None
                                            : QueryOrderingDirection::Descending);
                    ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(ordering, *lambda);
                    orderClause->Orderings().InsertAfter(nullptr, ordering);

                    auto* tmp = static_cast<InvocationExpression*>(mre->Target());
                    mre = static_cast<MemberReferenceExpression*>(tmp->Target());
                    lambda = tmp->Arguments().At(0);
                    // ValidateThenByChain already confirmed every clause in the chain is a
                    // simple lambda, so this match always succeeds.
                    bool matched = MatchSimpleLambda(lambda, parameter, orderExpression);
                    (void)matched;
                    assert(matched);
                }
                // Insert the new ordering at the beginning.
                auto* ordering = new QueryOrdering();
                ordering->Expression(Syntax::Detach(orderExpression));
                ordering->Direction(mre->MemberName() == "OrderBy"
                                        ? QueryOrderingDirection::None
                                        : QueryOrderingDirection::Descending);
                ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(ordering, *lambda);
                orderClause->Orderings().InsertAfter(nullptr, ordering);

                auto* query = new QueryExpression();
                query->Clauses().Add(MakeFromClause(parameter, Syntax::Detach(mre->Target())));
                query->Clauses().Add(orderClause);
                return query;
            }
        }
        return nullptr;
    }
    if (memberName == "Join" || memberName == "GroupJoin") {
        if (invocation->Arguments().Count() != 4)
            return nullptr;
        Expression* source1 = mre->Target();
        Expression* source2 = invocation->Arguments().At(0);
        if (IsNullConditional(source2))
            return nullptr;
        Expression* outerLambda = invocation->Arguments().At(1);
        ParameterDeclaration* element1 = nullptr;
        Expression* key1 = nullptr;
        if (!MatchSimpleLambda(outerLambda, element1, key1))
            return nullptr;
        Expression* innerLambda = invocation->Arguments().At(2);
        ParameterDeclaration* element2 = nullptr;
        Expression* key2 = nullptr;
        if (!MatchSimpleLambda(innerLambda, element2, key2))
            return nullptr;
        auto* lambda = dynamic_cast<LambdaExpression*>(invocation->Arguments().At(3));
        if (lambda != nullptr && lambda->Parameters().Count() == 2
            && dynamic_cast<Expression*>(lambda->Body()) != nullptr) {
            ParameterDeclaration* p1 = lambda->Parameters().At(0);
            ParameterDeclaration* p2 = lambda->Parameters().At(1);
            if (ValidateParameter(p1) && ValidateParameter(p2)
                && ParameterName(p1) == ParameterName(element1)
                && (ParameterName(p2) == ParameterName(element2) || memberName == "GroupJoin")) {
                context_->Step(memberName == "GroupJoin" ? "Build group join query"
                                                         : "Build join query",
                               invocation);
                auto* query = new QueryExpression();
                query->Clauses().Add(MakeFromClause(element1, Syntax::Detach(source1)));
                auto* joinClause = new QueryJoinClause();
                joinClause->JoinIdentifier(ParameterName(element2));
                ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(
                    joinClause->JoinIdentifierToken(), *element2);
                joinClause->InExpression(Syntax::Detach(source2));
                joinClause->OnExpression(Syntax::Detach(key1));
                joinClause->EqualsExpression(Syntax::Detach(key2));
                if (memberName == "GroupJoin") {
                    joinClause->IntoIdentifier(ParameterName(p2));
                    ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(
                        joinClause->IntoIdentifierToken(), *p2);
                }
                joinClause->AddAnnotation(std::make_shared<QueryJoinClauseAnnotation>(
                    GetILFunction(*outerLambda), GetILFunction(*innerLambda)));
                query->Clauses().Add(joinClause);
                auto* select = new QuerySelectClause();
                select->Expression(static_cast<Expression*>(Syntax::Detach(lambda->Body())));
                ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(select, *lambda);
                query->Clauses().Add(select);
                return query;
            }
        }
        return nullptr;
    }
    return nullptr;
}

bool IntroduceQueryExpressions::IsComplexQuery(MemberReferenceExpression* mre) {
    AstNode* parent = mre->Parent();
    AstNode* grandParent = parent != nullptr ? parent->Parent() : nullptr;
    return (dynamic_cast<InvocationExpression*>(mre->Target()) != nullptr
            && dynamic_cast<InvocationExpression*>(parent) != nullptr)
           || dynamic_cast<QueryClause*>(grandParent) != nullptr;
}

Syntax::QueryFromClause* IntroduceQueryExpressions::MakeFromClause(
    ParameterDeclaration* parameter, Expression* body) {
    auto* fromClause = new QueryFromClause();
    fromClause->Identifier(ParameterName(parameter));
    fromClause->Expression(body);
    ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(fromClause, *parameter);
    return fromClause;
}

bool IntroduceQueryExpressions::IsNullConditional(Expression* target) {
    auto* uoe = dynamic_cast<UnaryOperatorExpression*>(target);
    return uoe != nullptr && uoe->Operator() == UnaryOperatorType::NullConditional;
}

Syntax::Expression* IntroduceQueryExpressions::WrapExpressionInParenthesesIfNecessary(
    Expression* expression, const std::string& parameterName) {
    auto* ident = dynamic_cast<IdentifierExpression*>(expression);
    if (ident != nullptr && parameterName == ident->Identifier())
        return new ParenthesizedExpression(expression);
    return expression;
}

bool IntroduceQueryExpressions::ValidateThenByChain(InvocationExpression* invocation,
                                                    const std::string& expectedParameterName) {
    if (invocation == nullptr || invocation->Arguments().Count() != 1)
        return false;
    auto* mre = dynamic_cast<MemberReferenceExpression*>(invocation->Target());
    if (mre == nullptr)
        return false;
    ParameterDeclaration* parameter = nullptr;
    Expression* body = nullptr;
    if (!MatchSimpleLambda(invocation->Arguments().At(0), parameter, body))
        return false;
    if (ParameterName(parameter) != expectedParameterName)
        return false;

    if (mre->MemberName() == "OrderBy" || mre->MemberName() == "OrderByDescending")
        return !IsNullConditional(mre->Target());
    if (mre->MemberName() == "ThenBy" || mre->MemberName() == "ThenByDescending")
        return ValidateThenByChain(dynamic_cast<InvocationExpression*>(mre->Target()),
                                   expectedParameterName);
    return false;
}

bool IntroduceQueryExpressions::MatchSimpleLambda(Expression* expr,
                                                  ParameterDeclaration*& parameter,
                                                  Expression*& body) {
    auto* lambda = dynamic_cast<LambdaExpression*>(expr);
    if (lambda != nullptr && lambda->Parameters().Count() == 1
        && dynamic_cast<Expression*>(lambda->Body()) != nullptr) {
        ParameterDeclaration* p = lambda->Parameters().At(0);
        if (ValidateParameter(p)) {
            parameter = p;
            body = static_cast<Expression*>(lambda->Body());
            return true;
        }
    }
    parameter = nullptr;
    body = nullptr;
    return false;
}

bool IntroduceQueryExpressions::ValidateParameter(ParameterDeclaration* p) {
    return p->ParameterModifier() == ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None
           && p->Attributes().Count() == 0;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
