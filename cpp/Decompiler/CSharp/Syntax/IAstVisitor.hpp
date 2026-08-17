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

// Port of the `IAstVisitor` interface in ICSharpCode.Decompiler/CSharp/Syntax (the
// generated `IAstVisitor.g.cs`, emitted by DecompilerSyntaxTreeGenerator.cs). This is the
// visitor the concrete AST nodes' `AcceptVisitor` dispatch to and the output visitor
// (`CSharpOutputVisitor`) implements -- the dispatch half of the visitor pattern, paired
// with `AstNode::AcceptVisitor` (the other half, declared abstract on `AstNode`).
//
// The C# generator emits three visitor interfaces -- `IAstVisitor` (returns void, no data),
// `IAstVisitor<out S>` (returns S), and `IAstVisitor<in T, out S>` (returns S, takes T
// data) -- each carrying one `Visit<NodeName>(ConcreteNode)` method per concrete node that
// needs a visitor. Only the void `IAstVisitor` is consumed by the engine (the
// `CSharpOutputVisitor : IAstVisitor` pretty-printer; the generic variants are declared on
// `AstNode` and the generated `DepthFirstAstVisitor<T>`/`<T,S>` bases but unused elsewhere
// -- verified by grep), and C++ has no virtual template methods (the C# `abstract T
// AcceptVisitor<T>(IAstVisitor<T>)` generic-method dispatch cannot be ported faithfully), so
// this port carries the void `IAstVisitor` only; the generic variants stay deferred.
//
// This header is the foundation the concrete nodes plug into. It is currently an abstract
// base with no per-node `Visit` methods: the concrete node hierarchy has not been ported
// yet (it lands next, per PORT_PLAN.md section 5.2 / decision D1: port the generated
// *output* by hand). As each concrete node lands it adds a pure-virtual
// `virtual void Visit<NodeName>(class <NodeName>*) = 0;` here and overrides
// `AstNode::AcceptVisitor` to call `visitor.Visit<NodeName>(this)`; the node's hand-written
// file forward-declares the concrete type so this header need not include it. The
// `DepthFirstAstVisitor` base (DepthFirstAstVisitor.hpp) supplies the default
// `VisitChildren` walk every per-node `Visit` override delegates to.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP

namespace ILSpy::Decompiler::CSharp::Syntax {

// Forward declarations of the concrete AST nodes whose `Visit` methods are declared below.
// A pointer parameter needs only a forward declaration, so this header does not include the
// concrete node headers (the node's own header includes this one so its `AcceptVisitor`
// override can call `visitor.Visit<NodeName>(this)`); more are added as the hierarchy lands.
class Identifier;
class NullReferenceExpression;
class ThisReferenceExpression;
class BaseReferenceExpression;
class PrimitiveExpression;
class BinaryOperatorExpression;
class AssignmentExpression;
class UnaryOperatorExpression;
class ConditionalExpression;
class ParenthesizedExpression;
class CheckedExpression;
class UncheckedExpression;
class DirectionExpression;
class ThrowExpression;
// `PrimitiveType` is the first concrete `AstType` (a leaf, no `[Slot]` children); the
// rest of the `AstType` hierarchy (`SimpleType`/`MemberType`/`ComposedType`/...) lands next.
class PrimitiveType;

// The C# `public interface IAstVisitor` -- the void-returning AST visitor interface. The
// concrete node's `AcceptVisitor(IAstVisitor&)` calls the matching `Visit<NodeName>(this)`
// on it; `CSharpOutputVisitor` (the pretty-printer) implements it. An abstract base (a
// protected constructor + a virtual destructor): it cannot be instantiated directly, only
// derived from, matching the C# interface. Per-node `Visit` pure-virtuals are added as the
// concrete node hierarchy lands.
class IAstVisitor {
protected:
    IAstVisitor() = default;

public:
    virtual ~IAstVisitor() = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` pure-virtual methods. Each concrete node's
    // `AcceptVisitor` override calls the matching `Visit<NodeName>(this)`. The concrete leaf
    // nodes land here (the rest of the generated hierarchy follows): the `Identifier` token
    // (the first non-`Expression` concrete node), then the leaf expressions (the three
    // reference expressions and the literal-carrying `PrimitiveExpression`).
    virtual void VisitIdentifier(Identifier*) = 0;
    virtual void VisitNullReferenceExpression(NullReferenceExpression*) = 0;
    virtual void VisitThisReferenceExpression(ThisReferenceExpression*) = 0;
    virtual void VisitBaseReferenceExpression(BaseReferenceExpression*) = 0;
    virtual void VisitPrimitiveExpression(PrimitiveExpression*) = 0;
    virtual void VisitBinaryOperatorExpression(BinaryOperatorExpression*) = 0;
    virtual void VisitAssignmentExpression(AssignmentExpression*) = 0;
    virtual void VisitUnaryOperatorExpression(UnaryOperatorExpression*) = 0;
    virtual void VisitConditionalExpression(ConditionalExpression*) = 0;
    virtual void VisitParenthesizedExpression(ParenthesizedExpression*) = 0;
    virtual void VisitCheckedExpression(CheckedExpression*) = 0;
    virtual void VisitUncheckedExpression(UncheckedExpression*) = 0;
    virtual void VisitDirectionExpression(DirectionExpression*) = 0;
    virtual void VisitThrowExpression(ThrowExpression*) = 0;
    virtual void VisitPrimitiveType(PrimitiveType*) = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP
