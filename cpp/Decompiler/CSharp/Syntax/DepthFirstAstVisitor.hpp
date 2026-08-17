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

// Port of the `DepthFirstAstVisitor` abstract class in
// ICSharpCode.Decompiler/CSharp/Syntax/DepthFirstAstVisitor.cs -- the default
// implementation of `IAstVisitor` whose every per-node `Visit<NodeName>` method delegates
// to `VisitChildren` (a depth-first walk of the node's children calling `AcceptVisitor`
// on each). Concrete visitors derive from it and override only the `Visit` methods they
// care about; the rest fall through to the depth-first walk.
//
// Paired with `IAstVisitor` (IAstVisitor.hpp) and `AstNode::AcceptVisitor` (the dispatch
// entry, declared abstract on `AstNode`). The C# generator emits three `DepthFirstAstVisitor`
// variants (void, `<T>`, `<T,S>`); only the void one is consumed by the engine and ported
// here (see IAstVisitor.hpp for the generic-variant deferral).
//
// This header ports the load-bearing `VisitChildren` default. The per-node `Visit` overrides
// are added as the concrete node hierarchy lands (each one is a one-liner calling
// `VisitChildren(node)`), matching the C# generator's emitted `public virtual void
// Visit<NodeName>(<Node> node) { VisitChildren(node); }`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITOR_HPP

#include "AstNode.hpp"
#include "IAstVisitor.hpp"
#include "Expressions/BaseReferenceExpression.hpp"
#include "Expressions/NullReferenceExpression.hpp"
#include "Expressions/ThisReferenceExpression.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract class DepthFirstAstVisitor : IAstVisitor` -- the depth-first
// visitor base. Abstract (a protected constructor): cannot be instantiated directly, only
// derived from, matching the C# `abstract class`. Per-node `Visit` overrides (added as
// the concrete node hierarchy lands) default to `VisitChildren`.
class DepthFirstAstVisitor : public IAstVisitor {
protected:
    DepthFirstAstVisitor() = default;

    // The C# `protected virtual void VisitChildren(AstNode node)`: visits every child of
    // `node` in document order by calling `child.AcceptVisitor(this)`. `Children()`
    // enumerates in document order and its enumerator captures each child's successor
    // before yielding it, so a `Visit` override that removes or replaces the current child
    // mid-walk does not lose the place -- the hand-over-hand guarantee the transforms rely
    // on. Each child's `AcceptVisitor(this)` dispatches back to this visitor's matching
    // `Visit<ChildNode>`, which (by default) recurses via `VisitChildren` -- the
    // depth-first traversal.
    virtual void VisitChildren(AstNode* node) {
        if (node == nullptr)
            return;
        for (AstNode* child : node->Children()) {
            child->AcceptVisitor(*this);
        }
    }

public:
    ~DepthFirstAstVisitor() override = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` overrides. Each defaults to
    // `VisitChildren(node)` (the depth-first walk), matching the C# generator's emitted
    // `public virtual void Visit<NodeName>(<Node> node) { VisitChildren(node); }`; a derived
    // visitor overrides only the nodes it cares about. The first three concrete leaf
    // expressions land here (the rest of the generated hierarchy follows). The concrete
    // node headers are included above so the implicit `ConcreteNode* -> AstNode*` upcast in
    // `VisitChildren(node)` has the complete derived type.
    virtual void VisitNullReferenceExpression(NullReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitThisReferenceExpression(ThisReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitBaseReferenceExpression(BaseReferenceExpression* node) {
        VisitChildren(node);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITOR_HPP
