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

// Port of the `NullReferenceExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.cs (the generated
// `NullReferenceExpression.g.cs` + the hand-written partial). The first concrete C# AST node
// of the generated hierarchy (PORT_PLAN.md section 5.2 / decision D1: port the generated
// *output* by hand), plugging into the `IAstVisitor`/`AcceptVisitor` dispatch (the next
// in-order Phase-5 piece per the D225 plan).
//
// `null_reference_expression ::= 'null'` (C# grammar 6.4.5.1): a leaf expression with no
// children and no slots. The hand-written part overrides `EndLocation` (the keyword spans
// `"null"` from the print-time `StartLocation`); the generator emits the `AcceptVisitor`
// override calling `visitor.VisitNullReferenceExpression(this)` and the `DoMatch` (a
// type-only match -- `return other is NullReferenceExpression`, since the node has no
// `[Slot]` children and no match members). `Clone` is inherited from `AstNode` in C#
// (`MemberwiseClone` + `CloneChildrenInto`), but this port has no `MemberwiseClone`, so the
// concrete node overrides it: copies the print-time `StartLocation` (the only state) and
// copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_NULLREFERENCEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_NULLREFERENCEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class NullReferenceExpression : Expression`. `final`
// (the C# `sealed`): no further derivation.
class NullReferenceExpression final : public Expression {
public:
    ~NullReferenceExpression() override = default;
    NullReferenceExpression() = default;

    // The C# `NullReferenceExpression(TextLocation location)` -- records the print-time
    // start (the output visitor brackets the node with `StartNode`/`EndNode`).
    explicit NullReferenceExpression(TextLocation location) {
        StorePrintStart(location);
    }

    // The C# `public override TextLocation EndLocation` -- the keyword `"null"` (4 chars)
    // spans from `StartLocation`.
    TextLocation EndLocation() const override {
        return TextLocation(StartLocation().Line, StartLocation().Column + 4);
    }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch
    // entry: routes back to the matching `Visit` on the visitor.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitNullReferenceExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitNullReferenceExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitNullReferenceExpression(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`
    // for a no-member node: `return other is NullReferenceExpression`. A type-only match --
    // any other `NullReferenceExpression` matches, any other node does not.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        return dynamic_cast<NullReferenceExpression*>(other) != nullptr;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node with the same print-time
    // `StartLocation`, plus the annotation channel copied (`CloneAnnotationsFrom` +
    // `ReparentTrivia`, the D223 concrete-clone pattern). No children to deep-copy.
    NullReferenceExpression* Clone() const override {
        auto* node = new NullReferenceExpression();
        node->StorePrintStart(StartLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_NULLREFERENCEEXPRESSION_HPP
