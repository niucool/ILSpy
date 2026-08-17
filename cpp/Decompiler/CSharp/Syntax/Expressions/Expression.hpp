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

// Port of the `Expression` abstract base in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/Expression.cs -- the first slice of the
// generated C# AST node hierarchy (PORT_PLAN.md section 5.2 / decision D1: port the generated
// *output* by hand), the concrete nodes that override `AcceptVisitor` to call
// `visitor.Visit<NodeName>(this)` and add the matching `Visit` pure-virtuals to `IAstVisitor`
// (the next in-order Phase-5 piece per the D225 plan).
//
// `Expression` is the common base of every C# expression node. It is an abstract, otherwise
// empty `partial class : AstNode` carrying the `[DecompilerAstNode(hasPatternPlaceholder:
// true)]` attribute; the hand-written part adds only a typed `Clone` and a typed `ReplaceWith`
// (the C# `new` keyword hides the `AstNode` overloads and downcasts the result so callers get
// an `Expression` back instead of an `AstNode`). The generator additionally emits a pattern
// placeholder (the `implicit operator Expression(Pattern)` + a sealed `PatternPlaceholder`
// nested class wrapping a `Pattern`) because `hasPatternPlaceholder` is true; that lands when
// the concrete pattern nodes (`AnyNode`/`NamedNode`/...) are ported (the D219 deferral), so it
// is deferred here -- the abstract base and the concrete leaf nodes do not depend on it.
//
// The typed `Clone` ports as a covariant pure-virtual override: `AstNode::Clone()` returns
// `AstNode*` (its base body throws, since C++ has no `MemberwiseClone`); `Expression`
// re-declares it `virtual Expression* Clone() const override = 0` so a call through an
// `Expression*` returns an `Expression*` (the typed return the C# `new Expression Clone()`
// gives), and a concrete expression MUST override it (the redeclaration makes it pure, so
// the base throwing body is unreachable through the expression hierarchy). The typed
// `ReplaceWith(Func<Expression,Expression>)` is deferred: the C# `new` hides the `AstNode`
// overloads, but a C++ member of the same name would hide the inherited `ReplaceWith(AstNode*)`
// and `ReplaceWith(std::function<AstNode*(AstNode*)>)` (no `using`-declaration would re-introduce
// them), a churn that is not needed for the visitor-dispatch slice; the `AstNode` overloads
// work for expressions (an `Expression` IS an `AstNode`), and the typed convenience lands with
// the engine's expression-level `ReplaceWith` call sites.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_EXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_EXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract partial class Expression : AstNode`. Abstract: a concrete
// expression overrides at least `DoMatch`, `AcceptVisitor`, and `Clone`. Derives from
// `AstNode` (the annotation channel + the pattern-match interface + the slot-storage
// contract); `AstNode` is polymorphic, so `Expression` is too (the `dynamic_cast`-based
// is-a tests the slot system and the annotation channel use stay valid).
class Expression : public AstNode {
public:
    ~Expression() override = default;
    Expression() = default;
    Expression(const Expression&) = delete;
    Expression& operator=(const Expression&) = delete;

    // The C# `public new Expression Clone()` -- a typed clone returning an `Expression`
    // instead of an `AstNode`. The C# `new` hides the base virtual and downcasts
    // `base.Clone()`; this port re-declares the inherited `AstNode::Clone()` (which returns
    // `AstNode*` and has a throwing base body) as a covariant pure-virtual returning
    // `Expression*`, so a call through an `Expression*` is typed and a concrete expression
    // must override it (the base body becomes unreachable through the expression hierarchy,
    // matching the C# where every concrete `Expression` overrides the virtual `Clone`).
    // Covariant: `Expression*` derives from `AstNode*`, so this is a valid override of
    // `AstNode::Clone()`.
    Expression* Clone() const override = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_EXPRESSION_HPP
