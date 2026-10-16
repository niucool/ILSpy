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

// Port of the `FixedStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/FixedStatement.cs (the generated
// `FixedStatement.g.cs` + the hand-written partial, which declares only the `FixedKeyword`
// const and the three slot properties, no ctors, no helpers). The next in-order Phase-5 piece
// per the D266 plan ("FixedStatement (now unblocked -- it needs the `VariableInitializer` just
// ported plus a NEW `Slots::Variable` kind for its `Variables AstNodeCollection<VariableInitializer>`
// collection, plus the already-ported `Slots::Type`/`Slots::EmbeddedStatement` kinds)"):
// `fixed_statement ::= 'fixed' '(' type variable_initializer* ')' statement` (C# grammar
// 24.7) -- a `fixed (T ptr = expr, ...) body` pin statement is a `Type` reference (the pinned
// pointer's element type), a `Variables` collection (the comma-separated `name = initializer`
// declarators, each a `VariableInitializer`), and an `EmbeddedStatement` (the body whose scope
// keeps the pins alive).
//
// The slot layout in source declaration order is: `Type` (a single REQUIRED `AstType` at slot 0),
// `Variables` (an `AstNodeCollection<VariableInitializer>` collection at slot 1), and
// `EmbeddedStatement` (a single REQUIRED `Statement` at slot 2). The generator's
// `supportsIncremental` flag is `collectionCount == 1 && slotIndex == slots.Count - 1`; here
// `collectionCount == 1` but the `Variables` collection is at `slotIndex == 1`, NOT the last slot
// (`slots.Count - 1 == 2` -- the `EmbeddedStatement` single slot follows), so
// `supportsIncremental` is FALSE: every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the
// parent's indices for a lazy rebuild (`EnsureChildIndices`), and `IndexOf` falls back to a linear
// identity search. The `EmbeddedStatement` single slot FOLLOWS a collection, so the generator's
// `constIndex = !slots.Take(slotIndex).Any(IsCollection)` is `false`, and the `EmbeddedStatement`
// property setter uses the index-less `SetChildNode(ref field, value)` (which invalidates on a
// set/clear, since the flattened index is dynamic -- the `Variables` count can change); the
// `SetChild` override still passes the known flattened `index` to the const-index
// `SetChildNode(ref field, value, index)`. The `Type` single slot PRECEDES the collection, so its
// setter uses the const-index `SetChildNode(ref field, value, 0)`. This is the
// `ObjectCreateExpression` (D251) shape (a single slot + a collection + a trailing single slot)
// with the trailing single REQUIRED (not nullable) and a `Statement` (not
// `ArrayInitializerExpression`) child, applied to the `Statement` hierarchy.
//
// Its generated `DoMatch` has THREE terms in `MembersToMatch` (source declaration) order: a
// non-nullable recursive `Type` term (dispatched through `MatchRequired` -- the D231
// [class.access.derived] workaround), a collection recursive `Variables` term
// (`this.Variables.DoMatch` -- the generator emits the collection-typed recursive term directly,
// NOT `MatchOptional`, which it emits only for a nullable NON-collection child), and a
// non-nullable recursive `EmbeddedStatement` term (dispatched through `MatchRequired`). Both
// recursive single terms are REQUIRED (non-nullable), so neither uses `MatchOptional`.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitFixedStatement(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitFixedStatement`). The generated slot statics
// are `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required), `VariablesSlot`
// (a `CSharpSlotInfoT<VariableInitializer>` pointing at `Slots.Variable`, collection), and
// `EmbeddedStatementSlot` (a `CSharpSlotInfoT<Statement>` pointing at `Slots.EmbeddedStatement`,
// required). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
// deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`, the
// covariant override), every `Variables` element deep-cloned through `Add` (which re-parents and
// re-indexes; `VariableInitializer::Clone()` returns `VariableInitializer*`, which
// `Add(VariableInitializer*)` accepts directly), and the `EmbeddedStatement` deep-cloned through
// the setter (which re-parents; `Statement::Clone()` returns `Statement*`, which
// `EmbeddedStatement(Statement*)` accepts directly).
//
// NO C++ name-shadowing crux (the `ObjectCreateExpression` D251 differently-named-property
// precedent): the `Type()`/`Variables()`/`EmbeddedStatement()` accessors are member functions, but
// no class named `Type`/`Variables`/`EmbeddedStatement` lives in the `Syntax` namespace (there is
// `AstType`, not `Type`), and no member is named `AstType`/`VariableInitializer`/`Statement`. So
// no elaborated-type-specifier is needed anywhere, and the plain `AstType`/`VariableInitializer`/
// `Statement` resolve to the classes in every type position (the `MemberReferenceExpression`/
// `InvocationExpression` differently-named-property precedent).
//
// NO new `Slots` constant for `Type`/`EmbeddedStatement`: both are already ported (`Slots::Type` by
// `Attribute` D240, `Slots::EmbeddedStatement` by `WhileStatement` D258). The `Slots::Variable`
// kind is NEW and cycle-broken into `VariableInitializer.hpp` (the D241/D242/D251 precedent:
// `VariableInitializer.hpp` includes `Slots.hpp` for its per-node slot statics, so the kind cannot
// live in `Slots.hpp` -- a circular include -- and is defined after the `VariableInitializer` class
// where both `CSharpSlotInfoT` and the concrete type are complete).
//
// `FixedStatement.cs` declares NO hand-written ctors (only the `FixedKeyword` const and the three
// slot properties), so the port carries only the generated ctors. The generated collection ctors
// (the `(AstType, IEnumerable<VariableInitializer>)`, the `params VariableInitializer[]` form, and
// the `(AstType, IEnumerable<VariableInitializer>, Statement)` all-params ctor) use `AddRange`,
// which lands with the collection convenience mutators (the D222 deferral), so they are DEFERRED;
// the empty + the `(AstType)` required-prefix ctors cover the construction API (the `Type` is the
// only required ctor param before the `Variables` collection -- `Variables` is an optional
// collection and `EmbeddedStatement` is a required single that follows a collection, so it goes
// into the deferred all-params ctor). A variables list is built via `Variables().Add(...)` and the
// body is set via `EmbeddedStatement(...)` until `AddRange` lands. The `(AstType)` ctor is
// `explicit` (a single-argument ctor is a converting ctor by default), matching the generator's
// public ctor but avoiding an implicit `AstType` -> `FixedStatement` conversion.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FIXEDSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FIXEDSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class FixedStatement : Statement`. `final` (the C# `sealed`): no
// further derivation. A single `Type` slot + a `Variables` collection + a trailing single
// `EmbeddedStatement` slot (the `ObjectCreateExpression` D251 shape with a `Statement` base and a
// REQUIRED trailing single).
class FixedStatement final : public Statement {
public:
    ~FixedStatement() override = default;

    // The generated empty ctor (the C# `public FixedStatement()`). The `Variables` collection is a
    // member (the D222 always-present-stack-member design), initialized here with `baseIndex = 1`
    // (the `Type` single slot at slot 0 precedes it) and `supportsIncremental = false` (the
    // collection is the node's only collection but NOT its last slot -- the `EmbeddedStatement`
    // single slot follows at slot 2 -- so an element's flattened `ChildIndex` is NOT a simple
    // `baseIndex + local position`; every `Add`/`Insert`/`Remove`/single-slot-set invalidates the
    // parent's indices for a lazy rebuild). `Type` and `EmbeddedStatement` default to null via
    // their default member initializers (no type, no body).
    FixedStatement() : variables_(this, &VariablesSlot, 1, false) {}

    // The generated required-prefix ctor (the C# `public FixedStatement(AstType type)`) -- the only
    // required ctor param before the `Variables` collection (`Variables` is an optional collection
    // and `EmbeddedStatement` is a required single that follows a collection, so it goes into the
    // deferred all-params ctor). Sets `Type` in declaration order. Delegates to the empty ctor so
    // the collection member is initialized. `explicit` (a single-argument ctor is a converting ctor
    // by default).
    explicit FixedStatement(AstType* type) : FixedStatement() {
        Type(type);
    }

    // ---- The const keyword token (the output-visitor token literal) ----------------
    // The C# `public const string FixedKeyword = "fixed"`. Part of the node's public API (the
    // output visitor reads it); port as a `static constexpr const char*` (the `CheckedExpression`.
    // `CheckedKeyword` precedent). The generator excludes const string fields from
    // `MembersToMatch` (it iterates only instance `IPropertySymbol`s), so it never appears in
    // the generated `DoMatch`.
    static constexpr const char* FixedKeyword = "fixed";

    // ---- The `Type` slot (a single REQUIRED `AstType` child) -------------------------
    // The generated `[Slot("Type")] public partial AstType Type` -- a single non-nullable
    // `AstType` slot at flattened index 0. The const-index `SetChildNode(ref field, value, 0)`
    // setter (no collection precedes it) re-parents and re-indexes in place. No name shadowing
    // (the `Type()` accessor does not collide with the `AstType` base type -- no class named
    // `Type` lives in the `Syntax` namespace, and no member is named `AstType`), so the operand
    // type is the plain `AstType` (no elaborated specifier).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `Variables` collection slot -------------------------------------------
    // The generated `[Slot("Variable")] public partial AstNodeCollection<VariableInitializer>
    // Variables` -- the collection of `name = initializer` declarators (a
    // `CSharpSlotInfoT<VariableInitializer>` slot at slot index 1). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-Add element-list profile is preserved).
    // `supportsIncremental` is `false` (the collection is not the node's last slot -- the
    // `EmbeddedStatement` single slot follows), so `Add` invalidates the parent's indices.
    AstNodeCollectionT<VariableInitializer>& Variables() { return variables_; }
    const AstNodeCollectionT<VariableInitializer>& Variables() const { return variables_; }

    // ---- The `EmbeddedStatement` slot (a single REQUIRED `Statement` child) ----------
    // The generated `[Slot("EmbeddedStatement")] public partial Statement EmbeddedStatement` --
    // a single non-nullable `Statement` slot at slot index 2 (the body whose scope pins the
    // pointers). A COLLECTION precedes it (`Variables` at slot 1), so the generator's
    // `constIndex = !slots.Take(2).Any(IsCollection)` is `false`, and the setter uses the
    // index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the
    // flattened index is dynamic -- the `Variables` count can change). This is the
    // `ObjectCreateExpression.Initializer` (D251) pattern applied to a REQUIRED single child after
    // a collection. No name shadowing (no class named `EmbeddedStatement` lives in the `Syntax`
    // namespace, and no member is named `Statement`), so the operand type is the plain `Statement`
    // (no elaborated specifier).
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
    // `Type` `AstType` is non-nullable); the `VariablesSlot` (a `CSharpSlotInfoT<VariableInitializer>`
    // pointing at `Slots.Variable`, collection); the `EmbeddedStatementSlot` (a
    // `CSharpSlotInfoT<Statement>` pointing at `Slots.EmbeddedStatement`, required -- the
    // `EmbeddedStatement` `Statement` is non-nullable). No name shadowing
    // (`AstType`/`VariableInitializer`/`Statement` resolve to the classes -- no member is named any
    // of them).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<VariableInitializer> VariablesSlot{"Variables", true, &Slots::Variable, true};
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitFixedStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitFixedStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitFixedStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitFixedStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Three slots in declaration order: a `Type` single slot at index 0, a `Variables` collection
    // occupying the contiguous range `[1, 1 + Count)`, and an `EmbeddedStatement` single slot at
    // index `1 + Count` (the trailing single slot after the collection). `GetChildCount` is
    // `2 + Count` (the two single slots plus the collection's current length);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
    // running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a single step, a collection step, then a single
    // step). `GetCollectionByKind` returns the `Variables` collection for the `Variable` kind (the
    // node's only collection). This is the `ObjectCreateExpression` (D251) dispatch shape (a single
    // -> collection -> single walk).

    int GetChildCount() const override { return 2 + variables_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return type_;
        i--;
        {
            int n = variables_.Count();
            if (i < n)
                return variables_.At(i);
            i -= n;
        }
        if (i == 0)
            return embeddedStatement_;
        throw std::out_of_range("FixedStatement::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(type_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        {
            int n = variables_.Count();
            if (i < n) {
                variables_.SetAt(i, static_cast<VariableInitializer*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(embeddedStatement_, static_cast<Statement*>(value), index);
            return;
        }
        throw std::out_of_range("FixedStatement::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TypeSlot;
        i--;
        {
            int n = variables_.Count();
            if (i < n)
                return &VariablesSlot;
            i -= n;
        }
        if (i == 0)
            return &EmbeddedStatementSlot;
        throw std::out_of_range("FixedStatement::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Variable)
            return &variables_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is FixedStatement o && this.Type.DoMatch(o.Type, match) &&
    // this.Variables.DoMatch(o.Variables, match) && this.EmbeddedStatement.DoMatch(o.EmbeddedStatement,
    // match)`. The three terms are in `MembersToMatch` order, which is the source declaration order
    // (`Type`, `Variables`, `EmbeddedStatement`). The `Type` term is a non-nullable recursive
    // child, so the generator emits a DIRECT `this.Type.DoMatch(o.Type, match)` -- ported through
    // `MatchRequired` (the D231 [class.access.derived] workaround, since a derived node may not call
    // the protected `DoMatch` through a base `AstType*`); the `Variables` term is the collection
    // recursive match (the generator emits the collection-typed recursive term directly, NOT
    // `MatchOptional`, which it emits only for a nullable non-collection child); the
    // `EmbeddedStatement` term is a non-nullable recursive child, so the generator emits a DIRECT
    // `this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)` -- ported through `MatchRequired`.
    // A type-only mismatch (not a `FixedStatement`) rejects early. The `Type` `MatchRequired` is
    // the first term, so a half-constructed pattern (a null `Type`) rejects without crashing (the
    // `MatchRequired` null-pattern guard).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<FixedStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && variables_.DoMatch(o->variables_, match)
            && MatchRequired(embeddedStatement_, o->embeddedStatement_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
    // deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`,
    // the covariant override), every `Variables` element deep-cloned through `Add` (which
    // re-parents and re-indexes; `VariableInitializer::Clone()` returns `VariableInitializer*`,
    // which `Add(VariableInitializer*)` accepts directly), and the `EmbeddedStatement` deep-cloned
    // through the setter (which re-parents; `Statement::Clone()` returns `Statement*`, which
    // `EmbeddedStatement(Statement*)` accepts directly). The deep-copy order follows the slot
    // declaration order (`Type`, `Variables`, `EmbeddedStatement`) matching the generated
    // `CloneChildrenInto`; with the `Variables` collection non-incremental every mutation
    // invalidates, so the order does not affect the final rebuilt state. No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `FixedStatement` does not derive `EndLocation`), so they are not copied (the
    // `ObjectCreateExpression`/`WhileStatement` no-location-copy precedent). The covariant return
    // is `FixedStatement*` (through `Statement*`, the `Statement::Clone` pure-virtual).
    FixedStatement* Clone() const override {
        auto* node = new FixedStatement();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        for (int i = 0; i < variables_.Count(); i++)
            node->variables_.Add(variables_.At(i)->Clone());
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(embeddedStatement_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is null until the type is set (a required slot --
    // `CheckInvariant` asserts it is filled); `embeddedStatement_` is null until the body is set
    // (a required slot -- `CheckInvariant` asserts it is filled); `variables_` is the
    // always-present collection member (empty until the first `Add`, non-incremental). No name
    // shadowing (no member is named `AstType`/`VariableInitializer`/`Statement`), so the field
    // types are the plain classes.
    AstType* type_ = nullptr;
    AstNodeCollectionT<VariableInitializer> variables_;
    Statement* embeddedStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FIXEDSTATEMENT_HPP
