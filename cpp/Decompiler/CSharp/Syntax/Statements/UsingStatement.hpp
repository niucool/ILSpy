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

// Port of the `UsingStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/UsingStatement.cs (the generated
// `UsingStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the D261
// plan ("the remaining collection/multi-slot statements: UsingStatement, the next simplest with
// two bool scalars and an AstNode ResourceAcquisition slot needing the new
// Slots::ResourceAcquisition kind ...").
//
// `using_statement ::= 'await'? 'using' '(' ( local_variable_declaration | expression ) ')'
// statement` (C# grammar 13.14.1): a `Statement` with two single, REQUIRED (non-nullable) `[Slot]`
// children -- a `ResourceAcquisition` `AstNode` (the resource, typed the abstract `AstNode` base
// because the production takes EITHER a `local_variable_declaration` (a `VariableDeclarationStatement`,
// not yet ported) OR an `expression` (an `Expression`); both derive from `AstNode`, so the slot
// is typed `AstNode` to accept either) and an `EmbeddedStatement` `Statement` (the body) -- plus
// two bool scalars (`IsAsync`, the leading `await`; `IsEnhanced`, the C# 8 using-declarations
// enhancement flag) and NO scalar enum.
//
// The generator emits two typed slot statics (`ResourceAcquisitionSlot` pointing at the new
// `Slots::ResourceAcquisition` kind -- a `CSharpSlotInfoT<AstNode>` -- and `EmbeddedStatementSlot`
// pointing at the shared `Slots::EmbeddedStatement` kind, already ported by `WhileStatement`), the
// const-index `SetChildNode(ref field, value, index)` setters (no collection precedes either
// slot, so each flattened index is the constant slot position 0/1), the `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the two single slots, and the
// `DoMatch` `return other is UsingStatement o && this.IsAsync == o.IsAsync &&
// this.IsEnhanced == o.IsEnhanced && this.ResourceAcquisition.DoMatch(o.ResourceAcquisition,
// match) && this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. The two bools are
// fall-through plain-equality terms; both children are NON-NULLABLE recursive, so the generator
// emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (the `MatchRequired`
// same-class static in the port). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): copies the two scalars,
// deep-clones the two children through the setters (which re-parent), and copies the annotation
// channel.
//
// NO C++ name-shadowing crux: the `ResourceAcquisition()` accessor does not collide with any
// class in the `Syntax` namespace (no class named `ResourceAcquisition`), and the `AstNode`
// element type is the abstract base (no class named `ResourceAcquisition` shadows it); the
// `EmbeddedStatement()` accessor does not collide with the `Statement` base type (a member named
// `EmbeddedStatement` is not the name `Statement`). So no elaborated-type-specifier is needed
// anywhere (the `WhileStatement` D258 no-shadowing precedent), and the `ResourceAcquisition`
// setter takes a plain `AstNode*` (no cast), since the slot is typed the abstract base.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_USINGSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_USINGSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class UsingStatement : Statement`. `final` (the C# `sealed`): no
// further derivation. The `WhileStatement` D258 two-required-single-slot shape plus two bool
// scalars, with the first slot typed the abstract `AstNode` base (the `ResourceAcquisition`
// production takes either a declaration or an expression).
class UsingStatement final : public Statement {
public:
    ~UsingStatement() override = default;

    // The generated empty ctor (the C# `public UsingStatement()`). Both slots default to null.
    // Null slots violate the required-slot invariant, so a default-constructed node is only valid
    // until the slots are set (or until `DoMatch`/`CheckInvariant` observe the missing children)
    // -- the `WhileStatement` D258 required-slot behavior. The two bools default to `false`
    // (neither `await` nor the enhancement flag by default).
    UsingStatement() = default;

    // The generated all-params ctor (the C# `public UsingStatement(AstNode resourceAcquisition,
    // Statement embeddedStatement)`). `RequiredConstructorPrefixLength` is 2 (both required) and
    // there is no collection, so the single full ctor IS the required-prefix ctor (no shorter
    // prefix ctor and no `params` overload). Delegated to the empty ctor then the setters so the
    // slot machinery re-parents and re-indexes the children. The bool scalars are not ctor params
    // (the generator adds only settable ENUM-typed scalars to `CtorParams`; a bool is not an
    // enum), so they are left at their `false` default and set via the property setters.
    UsingStatement(AstNode* resourceAcquisition, Statement* embeddedStatement)
        : UsingStatement() {
        ResourceAcquisition(resourceAcquisition);
        EmbeddedStatement(embeddedStatement);
    }

    // The C# `public bool IsAsync { get; set; }` -- whether the statement carries a leading
    // `await` (the `await using` form). A plain bool field: not a child slot (no `[Slot]`), not a
    // ctor param (the generator adds only settable ENUM-typed scalars), so it is set via the
    // property setter. It IS in `MembersToMatch` (the generator adds every non-`[Slot]` instance
    // property), and a bool (not an enum, no `Any`, not a string) emits the fall-through
    // plain-equality `DoMatch` term (the `ComposedType.HasRefSpecifier` precedent). No name
    // shadowing.
    bool IsAsync() const { return isAsync_; }
    void IsAsync(bool value) { isAsync_ = value; }

    // The C# `public bool IsEnhanced { get; set; }` -- the C# 8 using-declarations enhancement
    // flag (set when the statement was lowered from an enhanced `using` declaration, used by the
    // output visitor to choose the enhanced form). Same shape as `IsAsync`: a plain bool field set
    // via the property setter, in `MembersToMatch` with a plain-equality `DoMatch` term.
    bool IsEnhanced() const { return isEnhanced_; }
    void IsEnhanced(bool value) { isEnhanced_ = value; }

    // The C# `[Slot("ResourceAcquisition")] AstNode ResourceAcquisition` -- a single, REQUIRED
    // (non-nullable) `AstNode` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes this slot). The slot is
    // typed the abstract `AstNode` base because the production takes EITHER a
    // `VariableDeclarationStatement` (the local-variable-declaration form) OR an `Expression`
    // (the expression form); both derive from `AstNode`, so the slot accepts either. No name
    // shadowing (the `ResourceAcquisition()` accessor does not collide with any class), so the
    // element type is the plain `AstNode`; the setter takes a plain `AstNode*` (no cast -- the
    // `SetChild` override passes the incoming `AstNode*` straight through).
    AstNode* ResourceAcquisition() const { return resourceAcquisition_; }
    void ResourceAcquisition(AstNode* value) {
        SetChildNode(resourceAcquisition_, value, 0);
    }

    // The C# `[Slot("EmbeddedStatement")] Statement EmbeddedStatement` -- a single, REQUIRED
    // (non-nullable) `Statement` child at flattened index 1. The generator emits the const-index
    // `SetChildNode(ref field, value, 1)` setter. No name shadowing (the `EmbeddedStatement()`
    // accessor does not collide with the `Statement` base type -- a member named
    // `EmbeddedStatement` is not the name `Statement`), so the plain `Statement` is used
    // throughout.
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. The
    // `IsOptional` flag is false for each (both required); the kind carries identity only.
    // `ResourceAcquisition` is the new kind added with this node (a `CSharpSlotInfoT<AstNode>`,
    // the first ported slot kind whose element type is the abstract `AstNode` base);
    // `EmbeddedStatement` is already ported (by `WhileStatement`), so no new `Slots` constant
    // beyond `ResourceAcquisition`. No name shadowing (no member is named `AstNode`/
    // `Statement`), so the element types are the plain base types.
    static inline const CSharpSlotInfoT<AstNode> ResourceAcquisitionSlot{"ResourceAcquisition", false, &Slots::ResourceAcquisition, false};
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};

    // The C# `public const string UsingKeyword = "using"` (the keyword token the output visitor
    // emits). Ports as a `static constexpr const char*` (a static field, not instance state), so
    // the generator's `MembersToMatch` (which iterates only instance `IPropertySymbol`s) excludes
    // it from the `DoMatch` (the `BreakStatement` D254 / `WhileStatement` D258 precedent applied
    // to the using statement).
    static constexpr const char* UsingKeyword = "using";

    // The C# `public const string AwaitKeyword = UnaryOperatorExpression.AwaitKeyword` -- the
    // `await` keyword token (the `await using` form), aliased to the `AwaitKeyword` const on
    // `UnaryOperatorExpression` (which carries the canonical `await` literal for the `Await`
    // unary operator). Ports as a `static constexpr const char*` initialized from
    // `UnaryOperatorExpression::AwaitKeyword` (faithful to the C# alias; the value is the same
    // compile-time constant `"await"`, so the alias preserves the single-source-of-truth). A
    // static field (not instance state), so excluded from `MembersToMatch`/`DoMatch`.
    static constexpr const char* AwaitKeyword = UnaryOperatorExpression::AwaitKeyword;

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitUsingStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUsingStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`ResourceAcquisition`/`EmbeddedStatement`); no
    // collection, so `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/
    // `GetChildSlotInfo` are a flat index switch (the generator's `WriteReturnDispatchSwitch`
    // shape, with two cases). The `ResourceAcquisition` slot is typed `AstNode`, so `SetChild`
    // passes the incoming `AstNode*` straight through (no `static_cast`); the `EmbeddedStatement`
    // slot is typed `Statement`, so `SetChild` downcasts to `Statement*` (the slot element type).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return resourceAcquisition_;
            case 1: return embeddedStatement_;
            default: throw std::out_of_range("UsingStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(resourceAcquisition_, value, 0); break;
            case 1: SetChildNode(embeddedStatement_, static_cast<Statement*>(value), 1); break;
            default: throw std::out_of_range("UsingStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ResourceAcquisitionSlot;
            case 1: return &EmbeddedStatementSlot;
            default: throw std::out_of_range("UsingStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is UsingStatement o && this.IsAsync == o.IsAsync &&
    // this.IsEnhanced == o.IsEnhanced && this.ResourceAcquisition.DoMatch(o.ResourceAcquisition,
    // match) && this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. The two bools are
    // fall-through plain-equality terms; both children are NON-NULLABLE recursive, so the
    // generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (NOT
    // `MatchOptional`, which the generator emits only for a nullable recursive child); there is
    // no scalar enum, so there is no `Any`-wildcard term. A type-only mismatch (not a
    // `UsingStatement`) rejects early. The terms are in `MembersToMatch` (source declaration)
    // order: the two bool scalars precede the two `[Slot]` children in the C# source, so they
    // appear before the recursive terms.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `AstNode*`/`Statement*`. `MatchRequired` guards a missing
    // pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)`
    // which returns false. For well-formed nodes (both children set) the behavior is identical
    // to the C#. The `ResourceAcquisition` term dispatches through the child's own `DoMatch`
    // (the child is either a `VariableDeclarationStatement` or an `Expression`, each with its own
    // `DoMatch` override), reached polymorphically through the `AstNode*`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<UsingStatement*>(other);
        if (o == nullptr)
            return false;
        return isAsync_ == o->isAsync_
            && isEnhanced_ == o->isEnhanced_
            && MatchRequired(resourceAcquisition_, o->resourceAcquisition_, match)
            && MatchRequired(embeddedStatement_, o->embeddedStatement_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): copies the two bool scalars and the annotation channel
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and
    // deep-clones the two children through the setters (which re-parent and re-index via
    // `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not stored on this node
    // (no own location fields), so only the scalars + children + annotation channel are copied.
    // Each child is skipped if absent (`Clone` tolerates a missing child even though the slots are
    // required -- the invariant is enforced by `CheckInvariant`, not by `Clone`). The
    // `ResourceAcquisition` child clones through `AstNode::Clone()` (the virtual dispatch returns
    // `AstNode*`, the slot's element type), which the `ResourceAcquisition(AstNode*)` setter
    // accepts directly (no cast -- the slot is typed the abstract base); the `EmbeddedStatement`
    // child clones through `Statement::Clone()` (covariant, returns `Statement*`), which the
    // `EmbeddedStatement(Statement*)` setter accepts directly.
    UsingStatement* Clone() const override {
        auto* node = new UsingStatement();
        node->isAsync_ = isAsync_;
        node->isEnhanced_ = isEnhanced_;
        node->CloneAnnotationsFrom(*this);
        if (resourceAcquisition_ != nullptr)
            node->ResourceAcquisition(resourceAcquisition_->Clone());
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(static_cast<Statement*>(embeddedStatement_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. The two bool scalars default to `false`; each required-slot pointer is
    // null until the child is set (non-null only by invariant). No name shadowing (no member is
    // named `AstNode`/`Statement`), so the field types are the plain base types.
    bool isAsync_ = false;
    bool isEnhanced_ = false;
    AstNode* resourceAcquisition_ = nullptr;
    Statement* embeddedStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_USINGSTATEMENT_HPP
