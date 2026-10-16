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

// Port of `GenericGrammarAmbiguityVisitor` in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/GenericGrammarAmbiguityVisitor.cs -- the AST
// transform that resolves the "F(G<A,B>(7));" grammar ambiguity. When the C# output contains a
// `LessThan` binary expression whose right operand, walked forward in document order, is
// eventually closed by a matching `Greater/Greater/GreaterGreater/UnsignedGreaterGreater` whose
// own right operand is a parenthesized expression, the printed `a < b > (...)` would re-parse as
// a generic method call `a<b>(...)`. To keep the output unambiguous the visitor wraps the
// offending `LessThan` binary in a `ParenthesizedExpression`.
//
// The class derives from `DepthFirstAstVisitor<bool>` (the `bool`-returning generic visitor base,
// ported as `DepthFirstAstVisitorBool`). It overrides `VisitChildren` (to stop on an unhandled
// node -- an unhandled node is not syntactically valid inside a type-argument list) and four
// per-node `Visit` methods that together walk a candidate type-argument span tracking the
// `<`/`>`/`>>`/`>>>` nesting. The static `CausesAmbiguityWithGenerics` drives one walk per
// `LessThan` binary; the static `ResolveAmbiguities` walks the whole tree and wraps each
// ambiguous binary. The C# decompiler pipeline runs `ResolveAmbiguities` over the root
// `SyntaxTree` immediately after `InsertParenthesesVisitor` (so the `(...)` the detection
// depends on is already a `ParenthesizedExpression`).
//
// C#-to-C++ porting decisions:
//  * `class GenericGrammarAmbiguityVisitor : DepthFirstAstVisitor<bool>` -> a C++ class
//    deriving publicly from `Syntax::DepthFirstAstVisitorBool`. The per-node `Visit` overrides
//    and `node.AcceptVisitor(v)` dispatch via `node->AcceptVisitorBool(*this)` (C++ has no
//    virtual template methods, so the C# generic `AcceptVisitor<T>` is the per-instantiation
//    `AcceptVisitorBool`, the D370 convention).
//  * `rootNode.Descendants.OfType<BinaryOperatorExpression>()` -> iterate the
//    `AstNode::Descendants()` snapshot (a `std::vector<AstNode*>` pre-order walk, excluding the
//    root) and `dynamic_cast` each entry to `BinaryOperatorExpression*`. The snapshot is taken
//    once before any replacement; `ReplaceWith` re-parents the wrapped binary intact under the
//    new `ParenthesizedExpression`, so every snapshot pointer stays valid and in-tree -- the
//    same pre-order the C# lazy `Descendants` yields (the C# iterator records the next sibling
//    before yielding, so a mid-walk replace does not lose the place).
//  * `node.GetNextNode()` -> `node->GetNextNode()` (the document-order next node).
//  * `node.ReplaceWith(n => new ParenthesizedExpression(n))` -> the
//    `AstNode::ReplaceWith(std::function<AstNode*(AstNode*)>)` overload (the C++ port defers the
//    typed `Expression::ReplaceWith`, so the lambda takes `AstNode*` and the `ParenthesizedExpression`
//    ctor's `Expression*` parameter takes a `static_cast<Expression*>(n)`).
//  * `Debug.Assert(...)` -> `assert(...)` (a debug-only check, compiled out with `NDEBUG`).
//  * `expr.Right is ParenthesizedExpression` -> `dynamic_cast<ParenthesizedExpression*>(...) != nullptr`.
//  * `case BinaryOperatorType.ShiftRight when genericNestingLevel >= 2:` -> a guarded switch
//    case: the C# `when` guard fails when the condition is false, so the case is skipped and the
//    `default` runs. The port inlines the guard (`if (level >= N) level -= N; else return true;`)
//    so a failed guard also stops the walk (no ambiguity), matching the `default` fall-through.
//  * `Left!` / `Right!` (null-forgiving, NOT `?.`) -> unguarded dereferences (the D354 convention:
//    the C# `?.` presence gates a null guard; `!` does not). The operand slots are non-null in a
//    well-formed binary the ambiguity walk reaches.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_GENERICGRAMMARAMBIGUITYVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_GENERICGRAMMARAMBIGUITYVISITOR_HPP

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorBool.hpp"  // base (brings the node headers)

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

// The C# `class GenericGrammarAmbiguityVisitor : DepthFirstAstVisitor<bool>` -- the
// "F(G<A,B>(7));" grammar-ambiguity resolver. A single instance drives one candidate-span walk
// (its `genericNestingLevel_`/`ambiguityFound_` state); the two static entry points create an
// instance per `LessThan` binary they probe.
class GenericGrammarAmbiguityVisitor : public Syntax::DepthFirstAstVisitorBool {
public:
    // The C# `public static void ResolveAmbiguities(AstNode rootNode)` -- walk every
    // `BinaryOperatorExpression` descendant of `rootNode` and wrap each one that
    // `CausesAmbiguityWithGenerics` flags in a `ParenthesizedExpression`.
    static void ResolveAmbiguities(Syntax::AstNode* rootNode);

    // The C# `public static bool CausesAmbiguityWithGenerics(BinaryOperatorExpression)` -- true
    // when `binaryOperatorExpression` is a `LessThan` whose right operand, walked forward in
    // document order, is closed by a matching `>`/`>>`/`>>>` whose own right operand is a
    // `ParenthesizedExpression` (the `G<...>(...)` shape that re-parses as a generic call).
    static bool CausesAmbiguityWithGenerics(Syntax::BinaryOperatorExpression* binaryOperatorExpression);

    // ---- Visit overrides (the C# `public override bool VisitXxx`) -------
    bool VisitBinaryOperatorExpression(Syntax::BinaryOperatorExpression* binaryOperatorExpression) override;
    bool VisitIdentifierExpression(Syntax::IdentifierExpression* identifierExpression) override;
    bool VisitTypeReferenceExpression(Syntax::TypeReferenceExpression* typeReferenceExpression) override;
    bool VisitMemberReferenceExpression(Syntax::MemberReferenceExpression* memberReferenceExpression) override;

protected:
    // The C# `protected override bool VisitChildren(AstNode node)` -- an unhandled node is
    // probably not syntactically valid in a type-argument list, so stop visiting (no ambiguity).
    bool VisitChildren(Syntax::AstNode* node) override;

private:
    // The C# instance fields `int genericNestingLevel` / `bool ambiguityFound` (private, default
    // 0 / false). `CausesAmbiguityWithGenerics` sets `genericNestingLevel = 1` (one unmatched `<`)
    // before driving the walk, and reads `ambiguityFound` when a `Visit` override stops the walk.
    int genericNestingLevel_ = 0;
    bool ambiguityFound_ = false;
};

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor

#endif // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_GENERICGRAMMARAMBIGUITYVISITOR_HPP
