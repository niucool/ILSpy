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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/IntroduceQueryExpressions.cs -- the
// transform that turns the compiler's LINQ method-chain calls (`Select`, `Where`, `GroupBy`,
// `SelectMany`, `OrderBy`/`ThenBy`, `Join`/`GroupJoin`) back into C# query expressions
// (`from ... where ... orderby ... select ...`), and then combines nested degenerate queries
// and adds the missing degenerate `select` (see the C# 4.0 spec, 7.16.2 query translation).
//
// Ported state:
//   * `Run` seeds the run context and, after `DecompileQueries` has built the query nodes,
//     walks every `QueryExpression` to add a degenerate `select` and to combine a degenerate
//     inner query into its consumer (hoisting the inner clauses and rebinding the inner and
//     outer range variables through the `ILVariableResolveResult` annotation).
//   * `DecompileQuery` matches the invocation shapes and builds the corresponding clause list.
//   * the `ApplyAnnotationVisitor` nested class in the C# source is dead code (declared but
//     never instantiated); it is deliberately not ported.
//
// The transform is resolver-free: the only symbol it reads is the `ILVariableResolveResult`
// annotation a `DeclareVariables` run attaches to a query range variable (plus the
// `ILFunction` annotations the query-group/query-join clauses carry).

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include "Decompiler/IL/ILVariable.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {
class AstNode;
class Expression;
class InvocationExpression;
class LambdaExpression;
class MemberReferenceExpression;
class ParameterDeclaration;
class QueryClause;
class QueryExpression;
class QueryFromClause;
} // namespace ILSpy::Decompiler::CSharp::Syntax

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class IntroduceQueryExpressions : IAstTransform`. `final` (the port marks
// concrete transforms final unless the C# derives from them).
class IntroduceQueryExpressions final : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

private:
    // The C# `void CombineRangeVariables(QueryClause clause, ILVariable? oldVariable,
    // ILVariable? newVariable)`: rebinds every descendant identifier whose
    // `ILVariableResolveResult` is `oldVariable` to `newVariable`.
    void CombineRangeVariables(Syntax::QueryClause* clause,
                               const IL::ILVariablePtr& oldVariable,
                               const IL::ILVariablePtr& newVariable);

    // The C# `bool IsDegenerateQuery(QueryExpression? query)`: true when the query does not
    // end in a `select` or `group` clause.
    bool IsDegenerateQuery(Syntax::QueryExpression* query);

    // The C# `void DecompileQueries(AstNode node)`: replaces a `Select`/... invocation with
    // its query form (optionally wrapped in a discard assignment) and recurses into the
    // resulting node's children.
    void DecompileQueries(Syntax::AstNode* node);

    // The C# `bool CanUseDiscardAssignment()`.
    bool CanUseDiscardAssignment();

    // The C# `QueryExpression? DecompileQuery(InvocationExpression? invocation)`: the
    // per-method-name matcher that builds a query, or null when the invocation is not one of
    // the query-translatable shapes.
    Syntax::QueryExpression* DecompileQuery(Syntax::InvocationExpression* invocation);

    // The C# `static bool IsComplexQuery(MemberReferenceExpression mre)`.
    static bool IsComplexQuery(Syntax::MemberReferenceExpression* mre);

    // The C# `QueryFromClause MakeFromClause(ParameterDeclaration parameter, Expression body)`.
    Syntax::QueryFromClause* MakeFromClause(Syntax::ParameterDeclaration* parameter,
                                            Syntax::Expression* body);

    // The C# `bool IsNullConditional(Expression target)`.
    bool IsNullConditional(Syntax::Expression* target);

    // The C# `Expression WrapExpressionInParenthesesIfNecessary(Expression expression,
    // string parameterName)`.
    Syntax::Expression* WrapExpressionInParenthesesIfNecessary(Syntax::Expression* expression,
                                                               const std::string& parameterName);

    // The C# `bool ValidateThenByChain(InvocationExpression? invocation, string
    // expectedParameterName)`.
    bool ValidateThenByChain(Syntax::InvocationExpression* invocation,
                             const std::string& expectedParameterName);

    // The C# `bool MatchSimpleLambda(Expression expr, out ParameterDeclaration? parameter,
    // out Expression? body)`.
    bool MatchSimpleLambda(Syntax::Expression* expr, Syntax::ParameterDeclaration*& parameter,
                           Syntax::Expression*& body);

    // The C# `static bool ValidateParameter(ParameterDeclaration p)`.
    static bool ValidateParameter(Syntax::ParameterDeclaration* p);

    // The C# `[AllowNull] TransformContext context` run state (null outside a run).
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
