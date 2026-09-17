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

#include "Decompiler/CSharp/Transforms/CombineQueryExpressions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryContinuationClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryFromClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryLetClause.hpp"
#include "Decompiler/CSharp/Syntax/QuerySelectClause.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AnonymousTypeCreateExpression;
using Syntax::AstNode;
using Syntax::AstType;
using Syntax::Expression;
using Syntax::IdentifierExpression;
using Syntax::InvocationExpression;
using Syntax::MemberReferenceExpression;
using Syntax::NamedExpression;
using Syntax::QueryClause;
using Syntax::QueryContinuationClause;
using Syntax::QueryExpression;
using Syntax::QueryFromClause;
using Syntax::QueryLetClause;
using Syntax::QuerySelectClause;

namespace {

namespace PM = Syntax::PatternMatching;

// The C# `CSharpDecompiler.IsTransparentIdentifier(string)` predicate. The Roslyn compiler
// names the carriers of its query range variables `<>h__TransparentIdentifier0` (older
// compilers `<>TranspIdent0`); the VB compiler names its carriers `$VB$It`, `$VB$It1`, ... .
// Shared with the not-yet-ported `IntroduceQueryExpressions` (move it to a shared home when
// that transform lands).
bool IsTransparentIdentifier(const std::string& identifier) {
    if (identifier.rfind("<>", 0) == 0) {
        return identifier.find("TransparentIdentifier") != std::string::npos
               || identifier.find("TranspIdent") != std::string::npos;
    }
    return identifier.rfind("$VB$It", 0) == 0;
}

// Owns the nodes of a pattern tree built for one sub-transform invocation. The C# patterns
// are `static readonly` (process lifetime); the port rebuilds an equivalent tree per call
// and keeps every node in this holder, so the non-owning child pointers the pattern nodes
// carry stay valid for the whole match. A pattern wrapped in a `PatternPlaceholderNode` is
// owned by the placeholder's shared_ptr, so it is NOT put in this holder. (A local copy of
// the helper PatternStatementTransform.cpp carries in its own anonymous namespace.)
class PatternTree {
public:
    template <class T, class... Args>
    T* Make(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = node.get();
        nodes_.push_back(std::move(node));
        return result;
    }

    // The generated `implicit operator <TNode>(Pattern)`: wrap a pattern in a placeholder so
    // it can occupy an AST slot while still matching (PatternPlaceholder.hpp).
    template <class TNode>
    TNode* Wrap(std::shared_ptr<PM::Pattern> pattern) {
        return Make<Syntax::PatternPlaceholderNode<TNode>>(std::move(pattern));
    }

private:
    std::vector<std::unique_ptr<PM::INode>> nodes_;
};

// The C# `static readonly InvocationExpression castPattern`: the shared shape
// `<inExpr>.Cast<<targetType>>()`.
InvocationExpression* BuildCastPattern(PatternTree& tree) {
    auto* memberReference = tree.Make<MemberReferenceExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::AnyNode>("inExpr")),
        std::string("Cast"));
    memberReference->TypeArguments().Add(
        tree.Wrap<AstType>(std::make_shared<PM::AnyNode>("targetType")));
    auto* invocation = tree.Make<InvocationExpression>();
    invocation->Target(memberReference);
    return invocation;
}

// The C# `static readonly QuerySelectClause selectTransparentIdentifierPattern`:
// `select new { <expr>, ... }` where each initializer is an identifier, a member reference
// or a named expression. Built into `tree` each call.
QuerySelectClause* BuildTransparentIdentifierSelectPattern(PatternTree& tree) {
    auto* selectPattern = tree.Make<QuerySelectClause>();
    auto* anonymousType = tree.Make<AnonymousTypeCreateExpression>();

    auto* choice = tree.Make<PM::Choice>();
    choice->Add(tree.Make<PM::NamedNode>(
        "expr", tree.Make<IdentifierExpression>(std::string(PM::Pattern::AnyString))));
    choice->Add(tree.Make<PM::NamedNode>(
        "expr",
        tree.Make<MemberReferenceExpression>(
            tree.Wrap<Expression>(std::make_shared<PM::AnyNode>()),
            std::string(PM::Pattern::AnyString))));
    choice->Add(tree.Make<PM::NamedNode>(
        "expr",
        tree.Make<NamedExpression>(
            std::string(PM::Pattern::AnyString),
            tree.Wrap<Expression>(std::make_shared<PM::AnyNode>()))));

    auto repeat = std::make_shared<PM::Repeat>(choice);
    repeat->MinCount = 1;
    anonymousType->Initializers().Add(tree.Wrap<Expression>(std::move(repeat)));
    selectPattern->Expression(anonymousType);
    return selectPattern;
}

// The C# `Dictionary<string, object?>.TryGetValue` companion: the owning handle of the
// first annotation of type `T` on `node`, or null. (`Annotation<T>()` returns a raw pointer
// and loses the shared_ptr the map and the re-attachment need.)
template <class T>
std::shared_ptr<Syntax::AnnotationBase> FindSharedAnnotation(const AstNode& node) {
    for (const auto& annotation : node.SharedAnnotations()) {
        if (dynamic_cast<T*>(annotation.get()) != nullptr)
            return annotation;
    }
    return nullptr;
}

} // namespace

void CombineQueryExpressions::Run(AstNode& rootNode, TransformContext& context) {
    // The C# `if (!context.Settings.QueryExpressions) return;`.
    if (!context.Settings().QueryExpressions())
        return;
    context_ = &context;
    try {
        FromOrLetIdentifiers fromOrLetIdentifiers;
        CombineQueries(&rootNode, fromOrLetIdentifiers);
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    // The C# `finally { this.context = null; }`.
    context_ = nullptr;
}

void CombineQueryExpressions::CombineQueries(AstNode* node,
                                             FromOrLetIdentifiers& fromOrLetIdentifiers) {
    // The C# `for (AstNode? child = node.FirstChild; child != null; child = next)` with
    // `next = child.NextSibling` captured BEFORE the recursion: a child's own subtree is
    // rewritten, but the sibling chain at this level is not, so the stored sibling stays
    // valid.
    AstNode* next;
    for (AstNode* child = node->FirstChild(); child != nullptr; child = next) {
        next = child->NextSibling();
        CombineQueries(child, fromOrLetIdentifiers);
    }

    auto* query = dynamic_cast<QueryExpression*>(node);
    if (query == nullptr)
        return;

    // The C# `QueryFromClause fromClause = (QueryFromClause)query.Clauses.First()`: the
    // grammar guarantees the first clause is a from clause, so a miss is an invalid-cast
    // state (the C# `InvalidCastException`).
    if (query->Clauses().Count() == 0)
        throw Util::InvalidCastException{};
    auto* fromClause = dynamic_cast<QueryFromClause*>(query->Clauses().At(0));
    if (fromClause == nullptr)
        throw Util::InvalidCastException{};

    auto* innerQuery = dynamic_cast<QueryExpression*>(fromClause->Expression());
    if (innerQuery != nullptr) {
        if (TryRemoveTransparentIdentifier(query, fromClause, innerQuery,
                                           fromOrLetIdentifiers)) {
            RemoveTransparentIdentifierReferences(query, fromOrLetIdentifiers);
        } else {
            // The C# `new QueryContinuationClause { PrecedingQuery = innerQuery.Detach(),
            // Identifier = fromClause.Identifier }` with the annotations copied across.
            context_->Step("Introduce query continuation", fromClause);
            auto* continuation = new QueryContinuationClause();
            continuation->PrecedingQuery(Syntax::Detach(innerQuery));
            continuation->Identifier(fromClause->Identifier());
            ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(continuation, *fromClause);
            fromClause->ReplaceWith(continuation);
            context_->EndStep(continuation);
        }
    } else {
        PatternTree tree;
        auto* castPattern = BuildCastPattern(tree);
        PM::Match match = PM::PatternExtensions::Match(*castPattern, fromClause->Expression());
        if (match.Success()) {
            // The C# `fromClause.Type = m.Get<AstType>("targetType").Single().Detach();
            // fromClause.Expression = m.Get<Expression>("inExpr").Single().Detach();`. The
            // captured nodes are the actual children of the matched invocation; detaching
            // lifts them out of the (now unused) invocation.
            std::vector<AstType*> types = match.Get<AstType>("targetType");
            std::vector<Expression*> expressions = match.Get<Expression>("inExpr");
            if (types.empty() || expressions.empty())
                return;
            context_->Step("Move Cast type into from clause", fromClause);
            fromClause->Type(Syntax::Detach(types[0]));
            fromClause->Expression(Syntax::Detach(expressions[0]));
        }
    }
}

bool CombineQueryExpressions::TryRemoveTransparentIdentifier(
    QueryExpression* query, QueryFromClause* fromClause, QueryExpression* innerQuery,
    FromOrLetIdentifiers& letClauses) {
    if (!IsTransparentIdentifier(fromClause->Identifier()))
        return false;
    if (innerQuery->Clauses().Count() == 0)
        return false;
    auto* selectClause =
        dynamic_cast<QuerySelectClause*>(innerQuery->Clauses().At(innerQuery->Clauses().Count() - 1));
    if (selectClause == nullptr)
        return false;

    PatternTree tree;
    auto* selectPattern = BuildTransparentIdentifierSelectPattern(tree);
    PM::Match match = PM::PatternExtensions::Match(*selectPattern, selectClause);
    if (!match.Success())
        return false;

    // from * in (from x in ... select new { members of anonymous type }) ...
    // =>
    // from x in ... { let x = ... } ...
    context_->Step("Remove transparent query identifier", fromClause);
    fromClause->Remove();
    selectClause->Remove();
    // Move clauses from innerQuery to query. The C# `foreach (var clause in innerQuery.Clauses)
    // query.Clauses.InsertAfter(insertionPos, insertionPos = clause.Detach())`; the port
    // detaches the front element each round (the C# collection enumerator tolerates the
    // removal, a vector index loop is the equivalent).
    QueryClause* insertionPos = nullptr;
    while (innerQuery->Clauses().Count() > 0) {
        QueryClause* clause = innerQuery->Clauses().At(0);
        clause->Remove();
        query->Clauses().InsertAfter(insertionPos, clause);
        insertionPos = clause;
    }
    context_->EndStep(query->Clauses().Count() > 0 ? query->Clauses().At(0) : nullptr);

    // The C# local function (its `insertionPos` is captured but never advanced, so several
    // let clauses all insert after the same clause -- faithful).
    auto AddQueryLetClause = [&](const std::string& name, Expression* expression) {
        auto* letClause = new QueryLetClause();
        letClause->Identifier(name);
        letClause->Expression(Syntax::Detach(expression));
        auto annotation = std::make_shared<LetIdentifierAnnotation>();
        letClause->AddAnnotation(annotation);
        letClauses[name] = annotation;
        query->Clauses().InsertAfter(insertionPos, letClause);
    };

    for (Expression* expression : match.Get<Expression>("expr")) {
        if (auto* identifier = dynamic_cast<IdentifierExpression*>(expression)) {
            // `name` is equivalent to `name = name`.
            letClauses[identifier->Identifier()] =
                FindSharedAnnotation<ILVariableResolveResult>(*identifier);
        } else if (auto* member = dynamic_cast<MemberReferenceExpression*>(expression)) {
            // `expr.name` is equivalent to `name = expr.name`.
            AddQueryLetClause(member->MemberName(), member);
        } else if (auto* namedExpression = dynamic_cast<NamedExpression*>(expression)) {
            auto* identifierExpression =
                dynamic_cast<IdentifierExpression*>(namedExpression->Expression());
            if (identifierExpression != nullptr
                && namedExpression->Name() == identifierExpression->Identifier()) {
                letClauses[namedExpression->Name()] =
                    FindSharedAnnotation<ILVariableResolveResult>(*identifierExpression);
                continue;
            }
            AddQueryLetClause(namedExpression->Name(), namedExpression->Expression());
        }
    }
    return true;
}

void CombineQueryExpressions::RemoveTransparentIdentifierReferences(
    AstNode* node, const FromOrLetIdentifiers& fromOrLetIdentifiers) {
    for (AstNode* child : node->Children()) {
        RemoveTransparentIdentifierReferences(child, fromOrLetIdentifiers);
    }
    auto* memberReference = dynamic_cast<MemberReferenceExpression*>(node);
    if (memberReference == nullptr)
        return;
    auto* identifier = dynamic_cast<IdentifierExpression*>(memberReference->Target());
    if (identifier == nullptr || !IsTransparentIdentifier(identifier->Identifier()))
        return;

    // `<transparent>.name` becomes `name` (the anonymous-type property reference is
    // dropped). Type arguments move to the replacement identifier.
    auto* newIdentifier = new IdentifierExpression(memberReference->MemberName());
    memberReference->TypeArguments().MoveTo(newIdentifier->TypeArguments());
    ::ILSpy::Decompiler::CSharp::CopyAnnotationsFrom(newIdentifier, *memberReference);
    newIdentifier->RemoveAnnotations<Sem::MemberResolveResult>(); // the anonymous-type property
    auto it = fromOrLetIdentifiers.find(memberReference->MemberName());
    if (it != fromOrLetIdentifiers.end() && it->second != nullptr)
        newIdentifier->AddAnnotation(it->second);
    context_->Step("Replace transparent query identifier reference", memberReference);
    memberReference->ReplaceWith(newIdentifier);
    context_->EndStep(newIdentifier);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
