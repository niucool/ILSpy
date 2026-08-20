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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `ForStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/ForStatement.cs (the generated
// `ForStatement.g.cs` + the hand-written partial, which declares only the `ForKeyword` const and
// the four slot properties, no ctors, no helpers). The next in-order Phase-5 piece per the D262
// plan ("ForStatement with two Statement collections and a nullable Condition").
// `for_statement ::= 'for' '(' statement* ';' expression? ';' statement* ')' statement` (C#
// grammar 13.9.4) -- a `for (init; test; iter) body` loop is an `Initializers` collection (the
// comma-separated init statements before the first `;`), an optional `Condition` `Expression`
// (the test, absent for `for (;;)`), an `Iterators` collection (the comma-separated step
// statements after the second `;`), and a required `EmbeddedStatement` (the loop body).
//
// It is the first ported node with a collection -> single -> collection -> single slot layout.
// The slot layout in source declaration order is: `Initializers` (an `AstNodeCollection<Statement>`
// collection at slot 0), `Condition` (a single NULLABLE `Expression?` at slot 1), `Iterators`
// (an `AstNodeCollection<Statement>` collection at slot 2), and `EmbeddedStatement` (a single
// REQUIRED `Statement` at slot 3). The generator's `supportsIncremental` flag is
// `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 2`, so it is
// FALSE for BOTH collections: every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the
// parent's indices for a lazy rebuild (`EnsureChildIndices`), and `IndexOf` falls back to a
// linear identity search. This is the `ComposedType` (D242) two-collection shape with a nullable
// single BETWEEN the collections and a required single AFTER both (the first ported node with a
// single slot between two collections that is NULLABLE, and the first with a single after two
// collections that is REQUIRED).
//
// The `Condition` single slot FOLLOWS a collection (`Initializers`), so the generator's
// `constIndex = !slots.Take(1).Any(IsCollection)` is `false`, and the `Condition` setter uses the
// index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the
// flattened index is dynamic -- the `Initializers` count can change). The `EmbeddedStatement`
// single slot FOLLOWS TWO collections (`Initializers` + `Iterators`), so `constIndex` is likewise
// `false`, and the `EmbeddedStatement` setter uses the index-less `SetChildNode(ref field,
// value)`. The `SetChild` override still passes the known flattened `index` to the const-index
// `SetChildNode(ref field, value, index)` for each single slot.
//
// Its generated `DoMatch` has FOUR terms in `MembersToMatch` (source declaration) order: a
// collection recursive `Initializers` term (`this.Initializers.DoMatch` -- the generator emits the
// collection-typed recursive term directly, NOT `MatchOptional`, which it emits only for a
// nullable NON-collection child), a nullable recursive `Condition` term (dispatched through
// `MatchOptional` -- the `BinaryOperatorExpression` D229 nullable-child path), a collection
// recursive `Iterators` term (likewise direct), and a non-nullable recursive `EmbeddedStatement`
// term (dispatched through `MatchRequired` -- the D231 [class.access.derived] workaround). It is
// the first ported node to combine TWO collection-`DoMatch` terms with a `MatchOptional` and a
// `MatchRequired` interleaved across one `DoMatch`, and the first whose dispatch walk has a
// collection -> single -> collection -> single shape.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitForStatement(this)`. The generated slot statics are `InitializersSlot`
// (a `CSharpSlotInfoT<Statement>` pointing at `Slots.ForInitializer`, collection), `ConditionSlot`
// (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Condition`, optional -- the `Condition`
// is nullable), `IteratorsSlot` (a `CSharpSlotInfoT<Statement>` pointing at `Slots.Iterator`,
// collection), and `EmbeddedStatementSlot` (a `CSharpSlotInfoT<Statement>` pointing at
// `Slots.EmbeddedStatement`, required -- the `EmbeddedStatement` is non-nullable). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `Initializers`/`Iterators` elements
// deep-cloned through `Add` (which re-parents and re-indexes; `Statement::Clone()` returns
// `Statement*`, which the typed `Add`s accept directly), the `Condition` deep-cloned through the
// setter when present, and the `EmbeddedStatement` deep-cloned through the setter when present.
//
// NO C++ name-shadowing crux (the `ComposedType`/`ArrayCreateExpression` differently-named-
// property precedent): the `Initializers`/`Condition`/`Iterators`/`EmbeddedStatement` accessors
// are member functions, but no class named any of them lives in the `Syntax` namespace, and no
// member is named `Expression`/`Statement`/`AstType`. So no elaborated-type-specifier is needed
// anywhere, and the plain `Statement`/`Expression` resolve to the classes in every type position.
//
// Two NEW `Slots` constants for the collection kinds: `Slots::ForInitializer` and `Slots::Iterator`
// (both `CSharpSlotInfoT<Statement>`, defined after `Slots::Statement`, so the element type is
// qualified). `Slots::Condition` (by `ConditionalExpression` D232) and `Slots::EmbeddedStatement`
// (by `WhileStatement` D258) are reused with no new constant for the two single slots; the
// per-node `ConditionSlot` carries `IsOptional=true` (the `Condition` is nullable) while the
// shared `Slots::Condition` kind is constructed non-optional (the per-position optionality lives
// on the per-node slot -- the `IfElseStatement.FalseStatement` D258 precedent applied across two
// nodes sharing one kind with different optionality).
//
// `ForStatement.cs` declares NO hand-written ctors (only the `ForKeyword` const and the four
// slot properties), so the port carries only the generated ctors. The generated collection ctors
// use `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so
// they are DEFERRED; the `RequiredConstructorPrefixLength` is 0 (the first slot is a collection,
// not required, so the required prefix stops before it), so the empty ctor is the only portable
// ctor. An initializer list is built via `Initializers().Add(...)`, a condition via
// `Condition(...)`, iterators via `Iterators().Add(...)`, and a body via `EmbeddedStatement(...)`
// until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FORSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FORSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ForStatement : Statement`. `final` (the C# `sealed`): no
// further derivation. The first ported node with a collection -> single -> collection -> single
// slot layout (`Initializers` + nullable `Condition` + `Iterators` + required
// `EmbeddedStatement`).
class ForStatement final : public Statement {
public:
    ~ForStatement() override = default;

    // The generated empty ctor (the C# `public ForStatement()`). The `Initializers` collection is
    // a member initialized with `baseIndex = 0` (its slot index 0) and `supportsIncremental =
    // false` (it is NOT the node's sole collection, so it does not own the contiguous
    // `[slotIndex, ..)` range with nothing after it); the `Iterators` collection is a member
    // initialized with `baseIndex = 2` (its slot index 2 -- the generator passes the slot index,
    // not the dynamic flattened index) and `supportsIncremental = false`. With both collections
    // non-incremental, every `Add`/`Insert`/`Remove`/single-slot-set invalidates the parent's
    // indices for a lazy rebuild (`EnsureChildIndices`). `Condition`/`EmbeddedStatement` default
    // to null (no condition, no body).
    ForStatement() : initializers_(this, &InitializersSlot, 0, false),
                     iterators_(this, &IteratorsSlot, 2, false) {}

    // ---- The const keyword token (the output-visitor token literal) ----------------
    // The C# `public const string ForKeyword = "for"`. Part of the node's public API (the output
    // visitor reads it); port as a `static constexpr const char*` (the `WhileStatement.
    // WhileKeyword` precedent). The generator excludes const string fields from `MembersToMatch`
    // (it iterates only instance `IPropertySymbol`s), so it never appears in the generated
    // `DoMatch`.
    static constexpr const char* ForKeyword = "for";

    // ---- The `Initializers` collection slot -------------------------------------------
    // The generated `[Slot("ForInitializer")] public partial AstNodeCollection<Statement>
    // Initializers` -- the collection of init statements (the comma-separated list before the
    // first `;`, e.g. `a = 2, b = 1` in `for (a = 2, b = 1; a > b; a--)`; a
    // `CSharpSlotInfoT<Statement>` slot at slot index 0). The C# lazily allocates the wrapper; the
    // D222 port makes the collection an always-present stack member, so the accessor returns the
    // member directly (the empty-until-first-Add element-list profile is preserved).
    // `supportsIncremental` is `false` (two collections), so `Add` invalidates the parent's
    // indices.
    AstNodeCollectionT<Statement>& Initializers() { return initializers_; }
    const AstNodeCollectionT<Statement>& Initializers() const { return initializers_; }

    // ---- The `Condition` slot (a single NULLABLE `Expression` child) ----------------
    // The generated `[Slot("Condition")] public partial Expression? Condition` -- a single
    // nullable `Expression` slot at slot index 1 (the loop test, absent for `for (;;)`). A
    // COLLECTION precedes it (`Initializers` at slot 0), so the generator's `constIndex =
    // !slots.Take(1).Any(IsCollection)` is `false`, and the setter uses the index-less
    // `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the flattened
    // index is dynamic -- the `Initializers` count can change). This is the
    // `ObjectCreateExpression.Initializer` (D251) pattern applied to a nullable single child after
    // a collection. No name shadowing (no class named `Condition` lives in the `Syntax` namespace
    // -- there is `AstType`/`Expression`/`Statement`, not `Condition`), so the operand type is
    // the plain `Expression` (no elaborated specifier).
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value);
    }

    // ---- The `Iterators` collection slot -------------------------------------------
    // The generated `[Slot("Iterator")] public partial AstNodeCollection<Statement> Iterators` --
    // the collection of step statements (the comma-separated list after the second `;`, e.g.
    // `a--` in `for (;; ; a--)`; a `CSharpSlotInfoT<Statement>` slot at slot index 2).
    // `supportsIncremental` is `false` (two collections), so `Add` invalidates the parent's
    // indices. `baseIndex = 2` (the slot index; the dynamic flattened index `Initializers.Count
    // + 1` is rebuilt lazily by `EnsureChildIndices`, since the fast path is off).
    AstNodeCollectionT<Statement>& Iterators() { return iterators_; }
    const AstNodeCollectionT<Statement>& Iterators() const { return iterators_; }

    // ---- The `EmbeddedStatement` slot (a single REQUIRED `Statement` child) ----------
    // The generated `[Slot("EmbeddedStatement")] public partial Statement EmbeddedStatement` --
    // a single non-nullable `Statement` slot at slot index 3 (the loop body). TWO COLLECTIONS
    // precede it (`Initializers` at slot 0, `Iterators` at slot 2) plus a single (`Condition` at
    // slot 1), so the generator's `constIndex = !slots.Take(3).Any(IsCollection)` is `false`, and
    // the setter uses the index-less `SetChildNode(ref field, value)` (which invalidates on a
    // set/clear, since the flattened index is dynamic -- either collection's count can change).
    // This is the `ArrayCreateExpression.Initializer` (D252) pattern applied to a single child
    // after TWO collections. No name shadowing (the `EmbeddedStatement()` accessor does not
    // collide with the `Statement` base type -- a member named `EmbeddedStatement` is not the
    // name `Statement`), so the operand type is the plain `Statement` (no elaborated specifier).
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `InitializersSlot` (a `CSharpSlotInfoT<Statement>` pointing at `Slots.ForInitializer`,
    // collection); the `ConditionSlot` (a `CSharpSlotInfoT<Expression>` pointing at
    // `Slots.Condition`, optional -- the `Condition` is nullable, so `IsOptional` is `true`);
    // the `IteratorsSlot` (a `CSharpSlotInfoT<Statement>` pointing at `Slots.Iterator`,
    // collection); the `EmbeddedStatementSlot` (a `CSharpSlotInfoT<Statement>` pointing at
    // `Slots.EmbeddedStatement`, required -- the `EmbeddedStatement` is non-nullable, so
    // `IsCollection || IsNullable` is `false`). No name shadowing (`Statement`/`Expression`
    // resolve to the classes -- no member is named any of them).
    static inline const CSharpSlotInfoT<Statement> InitializersSlot{"Initializers", true, &Slots::ForInitializer, true};
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, true};
    static inline const CSharpSlotInfoT<Statement> IteratorsSlot{"Iterators", true, &Slots::Iterator, true};
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitForStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitForStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitForStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitForStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Four slots in declaration order: an `Initializers` collection at slot 0 (the contiguous
    // range `[0, initCount)`), a `Condition` single slot at slot 1 (index `initCount`), an
    // `Iterators` collection at slot 2 (the range `[initCount + 1, initCount + 1 + iterCount)`),
    // and an `EmbeddedStatement` single slot at slot 3 (index `initCount + 1 + iterCount`).
    // `GetChildCount` is `2 + initCount + iterCount` (the two single slots plus both
    // collections' current lengths); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, a single step, a collection step, then a single step). `GetCollectionByKind` returns
    // each collection for its kind. This is the first ported node with a collection -> single ->
    // collection -> single dispatch walk (a single BETWEEN two collections and a single AFTER two
    // collections).

    int GetChildCount() const override { return 2 + initializers_.Count() + iterators_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = initializers_.Count();
            if (i < n)
                return initializers_.At(i);
            i -= n;
        }
        if (i == 0)
            return condition_;
        i--;
        {
            int n = iterators_.Count();
            if (i < n)
                return iterators_.At(i);
            i -= n;
        }
        if (i == 0)
            return embeddedStatement_;
        throw std::out_of_range("ForStatement::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = initializers_.Count();
            if (i < n) {
                initializers_.SetAt(i, static_cast<Statement*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(condition_, static_cast<Expression*>(value), index);
            return;
        }
        i--;
        {
            int n = iterators_.Count();
            if (i < n) {
                iterators_.SetAt(i, static_cast<Statement*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(embeddedStatement_, static_cast<Statement*>(value), index);
            return;
        }
        throw std::out_of_range("ForStatement::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = initializers_.Count();
            if (i < n)
                return &InitializersSlot;
            i -= n;
        }
        if (i == 0)
            return &ConditionSlot;
        i--;
        {
            int n = iterators_.Count();
            if (i < n)
                return &IteratorsSlot;
            i -= n;
        }
        if (i == 0)
            return &EmbeddedStatementSlot;
        throw std::out_of_range("ForStatement::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::ForInitializer)
            return &initializers_;
        if (kind == &Slots::Iterator)
            return &iterators_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ForStatement o && this.Initializers.DoMatch(o.Initializers, match) &&
    // MatchOptional(this.Condition, o.Condition, match) && this.Iterators.DoMatch(o.Iterators,
    // match) && this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. The four terms are
    // in `MembersToMatch` order, which is the source declaration order (`Initializers`,
    // `Condition`, `Iterators`, `EmbeddedStatement`). The `Initializers`/`Iterators` terms are
    // collection recursive matches (the generator emits the collection-typed recursive term
    // directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child);
    // the `Condition` term is a nullable non-collection recursive child, so the generator emits
    // `MatchOptional` (both absent, or both present and the pattern's `DoMatch` decides); the
    // `EmbeddedStatement` term is a non-nullable recursive child, so the generator emits a DIRECT
    // `this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)` -- ported through
    // `MatchRequired` (the D231 [class.access.derived] workaround). A type-only mismatch (not a
    // `ForStatement`) rejects early. The `EmbeddedStatement` `MatchRequired` is reached only
    // after the `Initializers` + `Condition` + `Iterators` terms pass, so a half-constructed
    // pattern (a null `EmbeddedStatement`) rejects without crashing (the `MatchRequired`
    // null-pattern guard), and a null candidate `EmbeddedStatement` flows through
    // `DoMatch(nullptr)` which returns false.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ForStatement*>(other);
        if (o == nullptr)
            return false;
        return initializers_.DoMatch(o->initializers_, match)
            && MatchOptional(condition_, o->condition_, match)
            && iterators_.DoMatch(o->iterators_, match)
            && MatchRequired(embeddedStatement_, o->embeddedStatement_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), every
    // `Initializers`/`Iterators` element deep-cloned through `Add` (which re-parents and
    // re-indexes; `Statement::Clone()` returns `Statement*`, which the typed `Add`s accept
    // directly), the `Condition` deep-cloned through the setter when present, and the
    // `EmbeddedStatement` deep-cloned through the setter when present. The deep-copy order
    // follows the slot declaration order (`Initializers`, `Condition`, `Iterators`,
    // `EmbeddedStatement`) matching the generated `CloneChildrenInto`; with both collections
    // non-incremental every mutation invalidates, so the order does not affect the final rebuilt
    // state. No own location fields (`StartLocation`/`EndLocation` are the print-time base fields
    // set by the unported output visitor -- `ForStatement` does not derive `EndLocation`), so
    // they are not copied (the `WhileStatement`/`ComposedType` precedent for nodes without derived
    // locations). The covariant return is `ForStatement*` (through `Statement*`, the
    // `Statement::Clone` pure-virtual).
    ForStatement* Clone() const override {
        auto* node = new ForStatement();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < initializers_.Count(); i++)
            node->initializers_.Add(initializers_.At(i)->Clone());
        if (condition_ != nullptr)
            node->Condition(condition_->Clone());
        for (int i = 0; i < iterators_.Count(); i++)
            node->iterators_.Add(iterators_.At(i)->Clone());
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(embeddedStatement_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `condition_` is null until the test is set (an OPTIONAL slot --
    // `CheckInvariant` passes with it null, the `for (;;)` shape); `embeddedStatement_` is null
    // until the body is set (a REQUIRED slot -- `CheckInvariant` asserts it is filled);
    // `initializers_`/`iterators_` are the always-present collection members (empty until the
    // first `Add`, non-incremental). No name shadowing (no member is named `Expression`/
    // `Statement`), so the field types are the plain classes. The declaration order mirrors the
    // source slot order so the ctor member-initializer list matches the declaration order
    // (`initializers_`, `condition_`, `iterators_`, `embeddedStatement_`); the two collection
    // members are initialized first in the ctor init list (they take ctor args), the two single
    // members default-initialize (the default member initializers `= nullptr`).
    AstNodeCollectionT<Statement> initializers_;
    Expression* condition_ = nullptr;
    AstNodeCollectionT<Statement> iterators_;
    Statement* embeddedStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FORSTATEMENT_HPP
