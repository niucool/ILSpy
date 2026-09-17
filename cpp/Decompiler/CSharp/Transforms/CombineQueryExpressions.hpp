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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/CombineQueryExpressions.cs -- the
// transform that flattens nested LINQ query expressions back into a single query and
// removes the compiler's transparent-identifier carriers:
//   * a nested query in a from clause either becomes a query continuation
//     (`from x in (from y in src ...) ...` -> `from y in src ... into x ...`) or -- when
//     the from identifier is a transparent identifier and the inner query ends in an
//     anonymous-type select -- the from/select pair is removed, the inner clauses are
//     hoisted, the anonymous-type members become `let` clauses, and every
//     transparent-identifier reference is replaced by the underlying member.
//   * `from x in expr.Cast<T>()` moves the cast's type argument into the from clause's
//     type slot (`from T x in expr`).
//
// The port is resolver-free (the only symbol read is the `ILVariableResolveResult`
// annotation the `DeclareVariables` analysis would attach, and a missing annotation is
// tolerated). `CSharpDecompiler.IsTransparentIdentifier` has no ported home yet, so it is a
// file-local predicate in the .cpp (its other C# consumer is the IL-stage
// `AssignVariableNames` transform, not `IntroduceQueryExpressions`).

#pragma once

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace ILSpy::Decompiler::CSharp::Syntax {
class AstNode;
class QueryExpression;
class QueryFromClause;
} // namespace ILSpy::Decompiler::CSharp::Syntax

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class LetIdentifierAnnotation` (declared in CombineQueryExpressions.cs):
// the marker `IntroduceQueryExpressions` and this transform attach to a `let` clause whose
// identifier came from a hoisted transparent-identifier member.
class LetIdentifierAnnotation final : public Syntax::AnnotationBase {};

// The C# `public class CombineQueryExpressions : IAstTransform`. `final` (the port marks
// concrete transforms final unless the C# derives from them).
class CombineQueryExpressions final : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: the
    // `QueryExpressions`-gated entry that walks the tree with a fresh (empty)
    // from/let-identifier dictionary.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

private:
    // The C# `Dictionary<string, object?>`: the transparent-identifier carrier names mapped
    // to either the `ILVariableResolveResult` annotation or the freshly created
    // `LetIdentifierAnnotation` (`AnnotationBase` is the port's common annotation root).
    using FromOrLetIdentifiers =
        std::unordered_map<std::string, std::shared_ptr<Syntax::AnnotationBase>>;

    // The C# `void CombineQueries(AstNode node, Dictionary<string, object?> fromOrLetIdentifiers)`:
    // recurses into the children first, then rewrites a `QueryExpression` node.
    void CombineQueries(Syntax::AstNode* node, FromOrLetIdentifiers& fromOrLetIdentifiers);

    // The C# `bool TryRemoveTransparentIdentifier(QueryExpression query, QueryFromClause
    // fromClause, QueryExpression innerQuery, Dictionary<string, object?> letClauses)`.
    bool TryRemoveTransparentIdentifier(Syntax::QueryExpression* query,
                                        Syntax::QueryFromClause* fromClause,
                                        Syntax::QueryExpression* innerQuery,
                                        FromOrLetIdentifiers& letClauses);

    // The C# `void RemoveTransparentIdentifierReferences(AstNode node,
    // Dictionary<string, object?> fromOrLetIdentifiers)`.
    void RemoveTransparentIdentifierReferences(
        Syntax::AstNode* node, const FromOrLetIdentifiers& fromOrLetIdentifiers);

    // The C# `[AllowNull] TransformContext context` run state (null outside a run).
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
