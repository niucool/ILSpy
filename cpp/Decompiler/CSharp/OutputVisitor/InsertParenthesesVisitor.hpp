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

// Port of `InsertParenthesesVisitor` in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/InsertParenthesesVisitor.cs -- the AST transform
// that inserts the parentheses required so the AST can be printed back to unambiguous C#.
// Operator precedence is not represented in the syntax tree (a `BinaryOperatorExpression(2,
// Mul, BinaryOperatorExpression(1, Add, 1))` would otherwise print as "2 * 1 + 1"); this visitor
// walks the tree and wraps sub-expressions in `ParenthesizedExpression` nodes wherever the C#
// language spec (or, optionally, readability) requires it. The C# decompiler pipeline runs it
// over the root `SyntaxTree` immediately before `GenericGrammarAmbiguityVisitor.ResolveAmbiguities`
// (CSharpDecompiler.cs), so the parentheses it inserts are present by the time the output visitor
// renders the tree.
//
// The class derives from `DepthFirstAstVisitor` (the void variant, the default `IAstVisitor`
// implementation whose per-node `Visit` methods recurse into children via `VisitChildren`). It
// overrides the `Visit` methods for the operator-bearing and operand-bearing expression nodes,
// parenthesizing the relevant child slots BEFORE recursing into them (the `base.VisitXxx(node)`
// call then walks the now-parenthesized children). A bool `InsertParenthesesForReadability`
// policy widens the parenthesization beyond the language-required minimum (the C#
// `CSharpDecompiler` sets it to `true`).
//
// C#-to-C++ porting decisions:
//  * `public class InsertParenthesesVisitor : DepthFirstAstVisitor` -> a C++ class deriving
//    publicly from `Syntax::DepthFirstAstVisitor` (the ported void variant). The per-node
//    `Visit` overrides call `DepthFirstAstVisitor::VisitXxx(node)` (the C# `base.VisitXxx(node)`)
//    to recurse into children -- the depth-first walk the parenthesized children then descend.
//  * `public bool InsertParenthesesForReadability { get; set; }` -> a public `bool` member
//    defaulting to `false` (the C# auto-property defaults to `false`); the pipeline sets it
//    `true`, the tests set it directly.
//  * `enum PrecedenceLevel` (private nested in C#) -> a private nested `enum class` with the
//    same integer ordering (the C# 4.0 spec operator-precedence table; higher value = higher
//    precedence = binds tighter). The C# enum arithmetic (`precedence + 1`, `a < b`) is
//    restored by the two hidden-friend operators below -- a C# `enum` supports arithmetic and
//    comparison on its underlying `int`, a C++ `enum class` does not, so the operators
//    (defined inline as `friend`s so they are found via ADL on the nested type) mirror the
//    C# arithmetic the `ParenthesizeIfRequired`/`VisitBinaryOperatorExpression` logic relies on.
//  * `Expression?` / `AstType?` slots -> raw `Expression*` / `AstType*` (nullable); the C# `?.`
//    and `is not null` tests port to explicit `!= nullptr` guards (the `Slot()?.Kind` test in
//    `HandleLambdaOrQuery` ports to `Slot() != nullptr && Slot()->Kind() == &Slots::Left`).
//  * `expr is X` / `expr is X { Prop: val }` pattern-tests -> `dynamic_cast<X*>(expr)`
//    (the established C#-pattern-to-C++-dynamic_cast convention).
//  * `expr.ReplaceWith(e => new ParenthesizedExpression { Expression = e })` -> the
//    `AstNode::ReplaceWith(std::function<AstNode*(AstNode*)>)` overload with a lambda that
//    `new`s a `ParenthesizedExpression` and sets its `Expression` slot (the Syntax-layer
//    non-owning `new` model -- `AstNode::~AstNode` does not delete children, mirroring the C#
//    GC-owned `new`).
//  * `BinaryOperatorType?` -> `std::optional<BinaryOperatorType>`; the C# `nullable == enum`
//    comparison (false when the nullable is empty) ports directly (`std::optional<T> == T`
//    returns `false` when the optional is empty).

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTPARENTHESESVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTPARENTHESESVISITOR_HPP

#include <optional>

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"      // base (brings the node headers)
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"   // Expression*
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"  // BinaryOperatorType
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"     // ConditionalExpression*
#include "Decompiler/CSharp/Syntax/AstType.hpp"                  // AstType*

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

class InsertParenthesesVisitor : public Syntax::DepthFirstAstVisitor {
public:
    // The C# `public bool InsertParenthesesForReadability { get; set; }`. When true the visitor
    // inserts parentheses beyond the language-required minimum for readability. Defaults to
    // false (the C# auto-property default); the decompiler pipeline sets it true.
    bool InsertParenthesesForReadability = false;

    // The C# private `enum PrecedenceLevel` -- the C# 4.0 operator-precedence table. A higher
    // integer value means higher precedence (binds tighter). The ordering and the implicit
    // integer values (sequential from 0) are load-bearing: `ParenthesizeIfRequired` compares
    // `GetPrecedence(child) < minimumPrecedence` and `VisitBinaryOperatorExpression` adds 1 to
    // the precedence for the tighter-operand side of a left-associative operator, so the enum's
    // numeric spacing must match the C# exactly.
    enum class PrecedenceLevel {
        Assignment,
        Conditional,
        NullCoalescing,
        ConditionalOr,
        ConditionalAnd,
        BitwiseOr,
        ExclusiveOr,
        BitwiseAnd,
        Equality,
        RelationalAndTypeTesting,
        Shift,
        Additive,
        Multiplicative,
        Switch,
        Range,
        Unary,
        QueryOrLambda,
        NullableRewrap,
        Primary,
    };

    // Restore the C# enum arithmetic/comparison a C++ `enum class` lacks (hidden friends:
    // found via ADL on the nested `PrecedenceLevel` type from the class's own methods).
    friend bool operator<(PrecedenceLevel a, PrecedenceLevel b) noexcept {
        return static_cast<int>(a) < static_cast<int>(b);
    }
    friend PrecedenceLevel operator+(PrecedenceLevel a, int n) noexcept {
        return static_cast<PrecedenceLevel>(static_cast<int>(a) + n);
    }

    // ---- Static helpers (the C# `static` methods) -----------------------

    // The C# `static PrecedenceLevel GetPrecedence(Expression expr)` -- the row in the C# 4.0
    // operator-precedence table for the given expression's outermost operator.
    static PrecedenceLevel GetPrecedence(Syntax::Expression* expr);

    // The C# `static void ParenthesizeIfRequired(Expression? expr, PrecedenceLevel)`.
    static void ParenthesizeIfRequired(Syntax::Expression* expr, PrecedenceLevel minimumPrecedence);

    // The C# `static void Parenthesize(Expression expr)` -- wrap `expr` in a
    // `ParenthesizedExpression` in place (via `ReplaceWith`).
    static void Parenthesize(Syntax::Expression* expr);

    // The C# `static bool TypeCanBeMisinterpretedAsExpression(AstType type)` -- whether a cast's
    // type could be misread as an expression (a `SimpleType`, or a non-double-colon `MemberType`).
    static bool TypeCanBeMisinterpretedAsExpression(Syntax::AstType* type);

    // The C# `static bool IsBitwise(BinaryOperatorType op)`.
    static bool IsBitwise(Syntax::BinaryOperatorType op);

    // ---- Instance helpers (the C# private instance methods) -------------

    // The C# `BinaryOperatorType? GetBinaryOperatorType(Expression? expr)`.
    std::optional<Syntax::BinaryOperatorType> GetBinaryOperatorType(Syntax::Expression* expr);

    // The C# `private bool IsConditionalRefExpression(ConditionalExpression)`.
    bool IsConditionalRefExpression(Syntax::ConditionalExpression* conditionalExpression);

    // The C# `private void HandleAssignmentRHS(Expression right)`.
    void HandleAssignmentRHS(Syntax::Expression* right);

    // The C# `void HandleLambdaOrQuery(Expression expr)`.
    void HandleLambdaOrQuery(Syntax::Expression* expr);

    // ---- Visit overrides (the C# `public override void VisitXxx`) -------

    // Primary expressions (parenthesize the target to `Primary`).
    void VisitMemberReferenceExpression(Syntax::MemberReferenceExpression*) override;
    void VisitPointerReferenceExpression(Syntax::PointerReferenceExpression*) override;
    void VisitInvocationExpression(Syntax::InvocationExpression*) override;
    void VisitIndexerExpression(Syntax::IndexerExpression*) override;

    // Unary expressions.
    void VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression*) override;
    void VisitCastExpression(Syntax::CastExpression*) override;

    // Binary operators.
    void VisitBinaryOperatorExpression(Syntax::BinaryOperatorExpression*) override;

    // `is` / `as`.
    void VisitIsExpression(Syntax::IsExpression*) override;
    void VisitAsExpression(Syntax::AsExpression*) override;

    // Interpolation (parenthesize `global::` inside the expression).
    void VisitInterpolation(Syntax::Interpolation*) override;

    // Conditional `?:`.
    void VisitConditionalExpression(Syntax::ConditionalExpression*) override;

    // Assignment (right-associative; extra parens inside array initializers).
    void VisitAssignmentExpression(Syntax::AssignmentExpression*) override;
    void VisitVariableInitializer(Syntax::VariableInitializer*) override;

    // Lambdas / queries (greedy ends; parenthesize when used as a left operand or under is/as).
    void VisitQueryExpression(Syntax::QueryExpression*) override;
    void VisitLambdaExpression(Syntax::LambdaExpression*) override;

    // Named argument / switch expression.
    void VisitNamedExpression(Syntax::NamedExpression*) override;
    void VisitSwitchExpression(Syntax::SwitchExpression*) override;
};

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor

#endif // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTPARENTHESESVISITOR_HPP
