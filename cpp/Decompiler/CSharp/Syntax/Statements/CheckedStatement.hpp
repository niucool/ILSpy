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

// Port of the `CheckedStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/CheckedStatement.cs (the generated
// `CheckedStatement.g.cs`). The next in-order Phase-5 piece per the D259 plan ("the remaining
// concrete statements: ... CheckedStatement, UncheckedStatement, UnsafeStatement ..."). A
// `checked_statement ::= 'checked' block` (C# grammar 13.12): a sealed `Statement` carrying a
// single REQUIRED `BlockStatement` `Body` child (the block under the `checked` context) plus
// the `CheckedKeyword` const string. It is the `ExpressionStatement` D255 single-required-slot
// shape (a single REQUIRED child at flattened index 0, no scalar) with a `BlockStatement` child
// instead of an `Expression`, plus the const keyword (the `CheckedExpression` D234 const-string
// precedent applied to a statement).
//
// The hand-written partial declares only the `CheckedKeyword` const string and the `Body` slot
// property, no ctors, no helpers. The generator emits the `BodySlot` slot static pointing at the
// shared `Slots::Body` kind (a NEW kind, cycle-broken into `BlockStatement.hpp` after the
// `BlockStatement` class -- the `Slots::Attribute`/`Slots::AttributeSection` precedent, since
// `BlockStatement.hpp` includes `Slots.hpp`), the const-index `SetChildNode(ref field, value, 0)`
// setter, the `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one
// single slot, and the `DoMatch` `return other is CheckedStatement o &&
// this.Body.DoMatch(o.Body, match)` -- a NON-nullable recursive child, so the generator emits the
// direct dispatch (NOT `MatchOptional`); routed through `AstNode::MatchRequired` (the D231
// same-class static helper, since C++ `[class.access.derived]` forbids a derived node from
// calling the protected `DoMatch` through a base `AstNode*`). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the child through the setter and copies the annotation channel.
//
// NO C++ name-shadowing crux: the `Body()` accessor is a member function, but no class named
// `Body` lives in the `Syntax` namespace (the `Attribute` D240 no-class-named-`Type` lesson), and
// the `BlockStatement` child type is in scope via the `BlockStatement.hpp` include (the
// `BlockStatement` name does not collide with any member here), so no elaborated-type-specifier
// is needed anywhere, and the plain `BlockStatement` resolves to the concrete class in every
// type position. The `CheckedKeyword` const string is a `static constexpr const char*` (the
// `CheckedExpression.CheckedKeyword` D234 precedent: a static field, not instance state, so the
// generator's `MembersToMatch` -- which iterates only instance `IPropertySymbol`s -- excludes it
// from the `DoMatch`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_CHECKEDSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_CHECKEDSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class CheckedStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The `ExpressionStatement` D255 single-required-slot shape with a
// `BlockStatement` child plus the `CheckedKeyword` const.
class CheckedStatement final : public Statement {
public:
    ~CheckedStatement() override = default;

    // The `CheckedKeyword` const string (the C# `public const string CheckedKeyword = "checked"`).
    // A static field (not instance state), excluded from `MembersToMatch`/`DoMatch` and read by the
    // unported output visitor (the `CheckedExpression.CheckedKeyword` D234 precedent).
    static constexpr const char* CheckedKeyword = "checked";

    // The generated empty ctor (the C# `public CheckedStatement()`). `Body` defaults to null (no
    // body block). A null body violates the required-slot invariant, so a default-constructed
    // node is only valid until `Body` is set (or until `DoMatch`/`CheckInvariant` observe the
    // missing child) -- the `UnaryOperatorExpression` D231 required-slot behavior.
    CheckedStatement() = default;

    // The generated all-params ctor (the C# `public CheckedStatement(BlockStatement body)`); the
    // `Body` slot is REQUIRED so the required-prefix length is 1, which IS the full count (1), so
    // this single-arg form is both the required-prefix ctor and the all-params ctor (no shorter
    // prefix ctor and no params overload since there is no collection). `explicit` because a
    // single-argument ctor is a converting ctor by default (the `ExpressionStatement` D255 / the
    // `ReturnStatement` D248 precedent). No name shadowing (no member is named `BlockStatement`),
    // so the plain `BlockStatement` (the concrete child type, in scope via the include) resolves.
    explicit CheckedStatement(BlockStatement* body)
        : CheckedStatement() {
        Body(body);
    }

    // The C# `[Slot("Body")] BlockStatement Body` -- a single, REQUIRED (non-nullable)
    // `BlockStatement` child at flattened index 0. The const-index `SetChildNode(ref field,
    // value, 0)` setter (no collection precedes this slot). The C# getter returns the backing
    // field null-forgiving (`field!`) because the slot is required; the port returns the raw
    // pointer (a required slot is non-null only by invariant, not by type). No name shadowing
    // (no member/class is named `Body`), so the plain `BlockStatement` resolves to the concrete
    // child type.
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable); the
    // kind carries identity only. `Slots::Body` is the new shared "Body" slot kind
    // (cycle-broken into `BlockStatement.hpp`). No name shadowing, so the element type is the
    // plain `BlockStatement` (the concrete child type, in scope via the include).
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitCheckedStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitCheckedStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitCheckedStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitCheckedStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Body`); no collection, so `GetChildCount` is the
    // constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return body_;
            default: throw std::out_of_range("CheckedStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(body_, static_cast<BlockStatement*>(value), 0); break;
            default: throw std::out_of_range("CheckedStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &BodySlot;
            default: throw std::out_of_range("CheckedStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is CheckedStatement o && this.Body.DoMatch(o.Body, match)`. `Body` is a
    // NON-NULLABLE recursive child, so the generator emits the direct `this.Body.DoMatch(o.Body,
    // match)` term (NOT `MatchOptional`, which it emits only for a nullable recursive child). The
    // C# direct dispatch assumes the required child is present; the port routes it through
    // `AstNode::MatchRequired` (the same-class static helper) because C++
    // `[class.access.derived]` forbids a derived node from calling the protected `DoMatch`
    // through a base `AstNode*`. `MatchRequired` guards a missing operand defensively (a null
    // pattern child does not match; the C# would null-deref), and a null candidate child flows
    // through the operand's `DoMatch(nullptr)` which returns false. For well-formed nodes (the
    // body always set) the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<CheckedStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Body` child deep-cloned through the setter (which re-parents and re-indexes via
    // `SetChildNode`). No own location fields (it does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `BreakStatement` D254
    // no-location-copy precedent). `BlockStatement::Clone()` returns `BlockStatement*` (the
    // covariant override), which the `Body(BlockStatement*)` setter accepts directly. The
    // covariant return is `CheckedStatement*` (through `Statement*`, the `Statement::Clone`
    // pure-virtual).
    CheckedStatement* Clone() const override {
        auto* node = new CheckedStatement();
        node->CloneAnnotationsFrom(*this);
        if (body_ != nullptr)
            node->Body(static_cast<BlockStatement*>(body_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. No name shadowing (no member/class is named `BlockStatement`), so the
    // field type is the plain concrete child class (in scope via the include). A required slot
    // is non-null only by invariant, so the pointer is null until the body is set.
    BlockStatement* body_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_CHECKEDSTATEMENT_HPP
