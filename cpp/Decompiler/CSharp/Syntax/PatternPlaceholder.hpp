// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the generated pattern-placeholder machinery
// (DecompilerSyntaxTreeGenerator.WritePatternPlaceholder). For every AST base marked
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` the generator emits an `implicit
// operator <Base>(Pattern pattern)` plus a nested `sealed class PatternPlaceholder :
// <Base>, INode, IPatternPlaceholder` that wraps the `Pattern` and delegates matching
// and visitor dispatch to it. The C# bases are `AstNode`, `AstType`, `Expression`,
// `Statement`, `ArrayInitializerExpression`, `AttributeSection`, `BlockStatement`,
// `SwitchStatement`, `TryCatchStatement`, `ParameterDeclaration` and
// `VariableInitializer`.
//
// C++ has no user-defined implicit conversion from `Pattern` and no per-base nested
// class requirement, so a single class template plays every generated placeholder's
// role: `PatternPlaceholderNode<TNode>` derives from the AST base `TNode` and behaves
// exactly like the emitted nested class. The C# `implicit operator` call sites port to
// the `PatternExtensions::ToType` / `ToExpression` / `ToStatement` factories (and the
// `WithName` shims) declared in PatternNodes.hpp and defined in PatternPlaceholder.cpp.
//
// Ownership: the C# placeholder stores the `Pattern` by reference and the GC keeps it
// alive as long as the placeholder. The port owns the top-level pattern through a
// `std::shared_ptr<Pattern>` (the pattern nodes themselves keep non-owning child
// references, so a dynamically built pattern tree's inner nodes stay the caller's
// responsibility -- the PatternNodes.hpp ownership note).
//
// The placeholder's `DoMatch`/`DoMatchCollection` delegate straight to the wrapped
// pattern, `AcceptVisitor`/`AcceptVisitorBool` route to `VisitPatternPlaceholder` on
// the visitor (so `DepthFirstAstVisitor`'s default `VisitChildren` walk sees the
// placeholder as a childless node), and `Clone` returns a fresh placeholder sharing
// the same pattern (the C# MemberwiseClone of the placeholder).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PATTERNPLACEHOLDER_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PATTERNPLACEHOLDER_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/IPatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# generated `sealed class PatternPlaceholder : <Base>, INode, IPatternPlaceholder`
// for the base `TNode`. `TNode` must be a non-final AST base that does not delete its
// default constructor (every `hasPatternPlaceholder: true` base qualifies).
template <class TNode>
class PatternPlaceholderNode : public TNode, public PatternMatching::IPatternPlaceholder {
public:
    // The C# `public PatternPlaceholder(Pattern child)`. The pattern is non-null in every
    // generated call site (the operator guards a null pattern and never constructs a
    // placeholder for it); the port keeps a defensive throw.
    explicit PatternPlaceholderNode(std::shared_ptr<PatternMatching::Pattern> child)
        : child_(std::move(child)) {
        if (child_ == nullptr)
            throw std::invalid_argument("child");
    }

    // The wrapped pattern (the C# `readonly Pattern child` field).
    PatternMatching::Pattern& Child() const { return *child_; }

    // The C# MemberwiseClone of the placeholder: a fresh placeholder sharing the same
    // pattern. Covariant `TNode*` return (a valid override of `AstNode::Clone` and of
    // the `hasPatternPlaceholder` base's own typed redeclaration).
    TNode* Clone() const override { return new PatternPlaceholderNode<TNode>(child_); }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitPatternPlaceholder(this, *child_);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` realized as the
    // port's `bool` instantiation (the `AcceptVisitorBool` convention).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitPatternPlaceholder(this, *child_);
    }

    // The C# explicit `bool INode.DoMatchCollection(...)` -- delegate straight to the
    // wrapped pattern so a non-deterministic pattern (Repeat/OptionalNode) placed in a
    // collection slot keeps its backtracking behaviour.
    bool DoMatchCollection(const std::vector<PatternMatching::INode*>& other, int pos,
                           PatternMatching::Match match,
                           PatternMatching::BacktrackingInfo& backtrackingInfo) override {
        return child_->DoMatchCollection(other, pos, match, backtrackingInfo);
    }

protected:
    // The C# `protected internal override bool DoMatch(AstNode? other, Match match)`.
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        return child_->DoMatch(other, match);
    }

private:
    std::shared_ptr<PatternMatching::Pattern> child_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PATTERNPLACEHOLDER_HPP
