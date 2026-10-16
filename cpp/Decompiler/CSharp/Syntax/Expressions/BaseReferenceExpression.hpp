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
// OTHERWISE, ARISING FROM, OUT OF OR IN IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `BaseReferenceExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.cs (the generated
// `BaseReferenceExpression.g.cs` + the hand-written partial). A concrete C# AST node of the
// generated hierarchy, plugging into the `IAstVisitor`/`AcceptVisitor` dispatch.
//
// `base_access ::= 'base'` (C# grammar 12.8.15): a leaf expression with no children and no
// slots. The hand-written part overrides `EndLocation` (the keyword spans `"base"` from the
// print-time `StartLocation`); the generator emits the `AcceptVisitor` override calling
// `visitor.VisitBaseReferenceExpression(this)` and the `DoMatch` (a type-only match). `Clone`
// is inherited in C#; this port overrides it (no `MemberwiseClone`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_BASEREFERENCEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_BASEREFERENCEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class BaseReferenceExpression : Expression`.
class BaseReferenceExpression final : public Expression {
public:
    ~BaseReferenceExpression() override = default;
    BaseReferenceExpression() = default;

    explicit BaseReferenceExpression(TextLocation location) {
        StorePrintStart(location);
    }

    // The C# `public override TextLocation EndLocation` -- the keyword `"base"` (4 chars)
    // spans from `StartLocation`.
    TextLocation EndLocation() const override {
        return TextLocation(StartLocation().Line, StartLocation().Column + 4);
    }

    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitBaseReferenceExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitBaseReferenceExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitBaseReferenceExpression(this);
    }

protected:
    // The generated `return other is BaseReferenceExpression` -- a type-only match.
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        return dynamic_cast<BaseReferenceExpression*>(other) != nullptr;
    }

public:
    BaseReferenceExpression* Clone() const override {
        auto* node = new BaseReferenceExpression();
        node->StorePrintStart(StartLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_BASEREFERENCEEXPRESSION_HPP
