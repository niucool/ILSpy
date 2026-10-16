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

// Port of the `UnsafeStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/UnsafeStatement.cs (the generated
// `UnsafeStatement.g.cs`). The next in-order Phase-5 piece per the D259 plan ("the remaining
// concrete statements: ... CheckedStatement, UncheckedStatement, UnsafeStatement ..."). A
// `unsafe_statement ::= 'unsafe' block` (C# grammar 24.2): the structural twin of
// `CheckedStatement`/`UncheckedStatement` -- a sealed `Statement` carrying a single REQUIRED
// `BlockStatement` `Body` child (the block under the `unsafe` context) plus the `UnsafeKeyword`
// const string. It is the `ExpressionStatement` D255 single-required-slot shape with a
// `BlockStatement` child instead of an `Expression`, plus the const keyword.
//
// The structural twin of `CheckedStatement`/`UncheckedStatement` (the same
// single-required-`BlockStatement`-`Body`-slot shape, the same `Slots::Body` kind, the same
// `MatchRequired` `DoMatch`, the same const-index `SetChildNode` slot storage, the same
// per-concrete-node `Clone`); the only divergences are the class name, the `UnsafeKeyword` const
// string, and the `Visit` method name. The hand-written partial declares only the
// `UnsafeKeyword` const string and the `Body` slot property. The generator emits the `BodySlot`
// slot static pointing at the shared `Slots::Body` kind (already cycle-broken into
// `BlockStatement.hpp` by `CheckedStatement` -- no new `Slots` constant), the const-index
// `SetChildNode(ref field, value, 0)` setter, the `GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` overrides over the one single slot, and the `DoMatch` `return other is
// UnsafeStatement o && this.Body.DoMatch(o.Body, match)` (a NON-nullable recursive child, the
// direct dispatch routed through `AstNode::MatchRequired`).
//
// NO C++ name-shadowing crux (the `CheckedStatement` precedent): the `Body()` accessor shadows
// nothing (no class named `Body` lives in the `Syntax` namespace), the `BlockStatement` child type
// is in scope via the include, so no elaborated-type-specifier is needed anywhere. The
// `UnsafeKeyword` const string is a `static constexpr const char*` (the static-field-not-in-
// `MembersToMatch` precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_UNSAFESTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_UNSAFESTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class UnsafeStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The `CheckedStatement` single-required-`BlockStatement`-slot shape
// with the `UnsafeKeyword` const.
class UnsafeStatement final : public Statement {
public:
    ~UnsafeStatement() override = default;

    // The `UnsafeKeyword` const string (the C#
    // `public const string UnsafeKeyword = "unsafe"`). A static field (not instance state),
    // excluded from `MembersToMatch`/`DoMatch` and read by the unported output visitor (the
    // `CheckedExpression.CheckedKeyword` D234 precedent).
    static constexpr const char* UnsafeKeyword = "unsafe";

    UnsafeStatement() = default;

    explicit UnsafeStatement(BlockStatement* body)
        : UnsafeStatement() {
        Body(body);
    }

    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value, 0);
    }

    // The per-node slot static pointing at the shared `Slots::Body` kind (already cycle-broken
    // into `BlockStatement.hpp` by `CheckedStatement` -- no new `Slots` constant).
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, false};

    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUnsafeStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitUnsafeStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitUnsafeStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return body_;
            default: throw std::out_of_range("UnsafeStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(body_, static_cast<BlockStatement*>(value), 0); break;
            default: throw std::out_of_range("UnsafeStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &BodySlot;
            default: throw std::out_of_range("UnsafeStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // `return other is UnsafeStatement o && this.Body.DoMatch(o.Body, match)` -- a
    // NON-nullable recursive child, the direct dispatch routed through `AstNode::MatchRequired`
    // (the `CheckedStatement` precedent).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<UnsafeStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(body_, o->body_, match);
    }

public:
    // The per-concrete-node `Clone` (the `CheckedStatement` precedent): a fresh node, the
    // annotation channel copied, the `Body` child deep-cloned through the setter, no location
    // copy (it does not derive `EndLocation`). The covariant return is `UnsafeStatement*`
    // (through `Statement*`, the `Statement::Clone` pure-virtual).
    UnsafeStatement* Clone() const override {
        auto* node = new UnsafeStatement();
        node->CloneAnnotationsFrom(*this);
        if (body_ != nullptr)
            node->Body(static_cast<BlockStatement*>(body_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    BlockStatement* body_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_UNSAFESTATEMENT_HPP
