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

// Port of the `YieldBreakStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.cs (the generated
// `YieldBreakStatement.g.cs` + the hand-written partial). The third concrete C# AST statement
// node -- a leaf statement with no `[Slot]` children and no members, carrying the two
// `YieldKeyword`/`BreakKeyword` const strings (the `yield`/`break` keyword tokens the output
// visitor emits); plugging into the `IAstVisitor`/`AcceptVisitor` dispatch (the next
// in-order Phase-5 piece per the D253 plan).
//
// `yield_statement ::= 'yield' 'break' ';'` (C# grammar 13.15): a leaf statement with no
// children and no slots. The hand-written part declares only the two const strings; the
// generator emits the `AcceptVisitor` override calling `visitor.VisitYieldBreakStatement(this)`
// and the `DoMatch` (a type-only match -- `return other is YieldBreakStatement`, since the
// node has no `[Slot]` children and no match members; the const strings are static fields,
// not instance state, so the generator's `MembersToMatch` -- which iterates only instance
// `IPropertySymbol`s -- excludes them). `Clone` is inherited from `AstNode` in C#
// (`MemberwiseClone` + `CloneChildrenInto`), but this port has no `MemberwiseClone`, so the
// concrete node overrides it: copies the annotation channel (`CloneAnnotationsFrom` +
// `ReparentTrivia`). It has no own location fields (it does not derive `EndLocation`), so
// `StartLocation`/`EndLocation` (the print-time base fields set by the unported output
// visitor) are not copied -- the ArraySpecifier D239 no-location-copy precedent.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_YIELDBREAKSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_YIELDBREAKSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class YieldBreakStatement : Statement`. `final`
// (the C# `sealed`): no further derivation.
class YieldBreakStatement final : public Statement {
public:
    ~YieldBreakStatement() override = default;
    YieldBreakStatement() = default;

    // The C# `public const string YieldKeyword = "yield"` and `BreakKeyword = "break"` (the
    // `yield`/`break` keyword tokens the output visitor emits) -- port as `static constexpr
    // const char*` (static fields, not instance state), so the generator's `MembersToMatch`
    // (which iterates only instance `IPropertySymbol`s) excludes them from the `DoMatch` (the
    // `CheckedExpression.CheckedKeyword` D234 / `BreakStatement.BreakKeyword` precedent applied
    // to a two-keyword statement).
    static constexpr const char* YieldKeyword = "yield";
    static constexpr const char* BreakKeyword = "break";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch
    // entry: routes back to the matching `Visit` on the visitor.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitYieldBreakStatement(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`
    // for a no-member node: `return other is YieldBreakStatement`. A type-only match --
    // any other `YieldBreakStatement` matches, any other node does not.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        return dynamic_cast<YieldBreakStatement*>(other) != nullptr;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node with the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern). No
    // children to deep-copy, no own location fields to copy.
    YieldBreakStatement* Clone() const override {
        auto* node = new YieldBreakStatement();
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_YIELDBREAKSTATEMENT_HPP
