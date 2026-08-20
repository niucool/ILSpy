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

// Port of the `SwitchSection` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/SwitchStatement.cs (the generated
// `SwitchSection.g.cs` + the hand-written partial, which declares only the two collection slot
// properties, no ctors, no helpers, no const strings). Part of the switch family -- the next
// in-order Phase-5 piece per the D267 plan. `switch_section ::= switch_label+ statement*`
// (C# grammar 13.8.3): a switch section is one or more `case`/`default` labels (the `CaseLabels`
// collection, each a `CaseLabel`) followed by zero or more statements (the `Statements`
// collection, each a `Statement`).
//
// The hand-written partial declares only the two collection slot properties. The
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` (the explicit `hasPatternPlaceholder: true`
// argument) means the node is NOT `sealed` (the generated `PatternPlaceholder` derives from it --
// the `ArrayInitializerExpression` D250 / `VariableInitializer` D266 non-sealed precedent; the
// pattern placeholder is deferred, but the class stays non-`final` to match the C# and to not
// block the placeholder landing). It derives DIRECTLY from the `AstNode` root (not
// `Expression`/`Statement`/`AstType`), an element of the `SwitchStatement`'s `SwitchSections`
// collection.
//
// The two slots in source declaration order: a `CaseLabels` `AstNodeCollection<CaseLabel>`
// collection at slot index 0 (the `case`/`default` labels) and a `Statements`
// `AstNodeCollection<Statement>` collection at slot index 1 (the section's statement list -- the
// `[Slot("EmbeddedStatement")]` argument names the slot KIND `EmbeddedStatement`, reused from the
// `WhileStatement` D258 single-`Statement` position, while the PROPERTY is `Statements`; the
// kind-collapsing design is by `[Slot]` name regardless of single-vs-collection, so a collection
// reuses the `Slots::EmbeddedStatement` kind a single position already registered -- the
// `BlockStatement` D256 / `ArrayInitializerExpression` D250 precedent). It is the `ComposedType`
// D242 two-collection shape but with NO single slot between (a collection -> collection layout):
// the generator's `supportsIncremental` flag is `collectionCount == 1 && slotIndex == slots.Count
// - 1`; with two collections that is `false` for BOTH, so neither maintains its elements' flattened
// `ChildIndex` incrementally -- every `Add`/`Insert`/`Remove` invalidates the parent's indices for a
// lazy rebuild (`EnsureChildIndices`), and `IndexOf` falls back to a linear identity search. The
// dynamic flattened layout is `CaseLabels [0, caseCount)` then `Statements [caseCount, caseCount +
// stmtCount)`.
//
// NO C++ name-shadowing crux (the `ComposedType` D242 differently-named-property precedent): the
// `CaseLabels()`/`Statements()` accessors are member functions, but no class named
// `CaseLabels`/`Statements` lives in the `Syntax` namespace, and the property names (plural) do
// not collide with the `CaseLabel`/`Statement` element-type classes (a member named `CaseLabels`
// is not the name `CaseLabel`; a member named `Statements` is not the name `Statement`). So no
// elaborated-type-specifier is needed anywhere, and the plain `CaseLabel`/`Statement` resolve to
// the classes in every type position.
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is SwitchSection o && this.CaseLabels.DoMatch(o.CaseLabels,
// match) && this.Statements.DoMatch(o.Statements, match)`. Both terms are collection recursive
// matches (the generator emits the collection-typed recursive term directly, NOT `MatchOptional`,
// which it emits only for a nullable non-collection child); there is no scalar term. A type-only
// mismatch (not a `SwitchSection`) rejects early.
//
// NO new `Slots` constant for the `Statements` collection: `Slots::EmbeddedStatement` is already
// ported (by `WhileStatement` D258). The `Slots::CaseLabel` kind is NEW and cycle-broken into
// `CaseLabel.hpp` (the D241/D242/D251/D267 precedent: `CaseLabel.hpp` includes `Slots.hpp` for its
// per-node `ExpressionSlot`, so the kind cannot live in `Slots.hpp` -- a circular include -- and is
// defined in `CaseLabel.hpp` after the `CaseLabel` class where both `CSharpSlotInfoT` and
// `CaseLabel` are complete).
//
// The generated ctors are DEFERRED except the empty ctor: every parametrized ctor takes a
// collection as its first param (the `CaseLabels` collection precedes the `Statements`
// collection, so the required-prefix ctor is `(IEnumerable<CaseLabel>, ...)` and includes a
// collection param), and the generator's ctor body calls `this.CaseLabels.AddRange(...)` for it
// -- `AddRange` is the D222-deferred collection convenience mutator, so the whole
// parametrized-ctor surface (the required-prefix `(caseLabels)` and `(caseLabels, statements)`
// ctors and any `params` overload) is deferred until `AddRange` lands. `SwitchSection.cs`
// declares NO hand-written ctors, so the empty ctor is the only portable ctor; a section is built
// via the empty ctor + `CaseLabels().Add(...)` / `Statements().Add(...)`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SWITCHSECTION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SWITCHSECTION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public partial class SwitchSection : AstNode` (NOT `sealed` -- the
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a sealed `PatternPlaceholder` deriving
// from it; the pattern placeholder is deferred, but the class stays non-`final` to match the C#
// and to not block the placeholder landing -- the `ArrayInitializerExpression` D250 /
// `VariableInitializer` D266 precedent). Derives directly from the `AstNode` root. The
// two-collection shape (a `CaseLabels` collection + a `Statements` collection, no single slot
// between -- the `ComposedType` D242 two-collection shape with no single slot).
class SwitchSection : public AstNode {
public:
    ~SwitchSection() override = default;

    // The generated empty ctor (the C# `public SwitchSection()`). The `CaseLabels` collection is
    // a member initialized with `baseIndex = 0` (its slot index 0) and `supportsIncremental =
    // false` (it is NOT the node's sole collection, so it does not own the contiguous
    // `[slotIndex, ..)` range with nothing after it); the `Statements` collection is a member
    // initialized with `baseIndex = 1` (its slot index 1 -- the generator passes the slot index,
    // not the dynamic flattened index) and `supportsIncremental = false`. With both collections
    // non-incremental, every `Add`/`Insert`/`Remove` invalidates the parent's indices for a lazy
    // rebuild (`EnsureChildIndices`).
    SwitchSection() : caseLabels_(this, &CaseLabelsSlot, 0, false),
                      statements_(this, &StatementsSlot, 1, false) {}

    // ---- The `CaseLabels` collection slot -------------------------------------------
    // The generated `[Slot("CaseLabel")] public partial AstNodeCollection<CaseLabel> CaseLabels`
    // -- the collection of `case`/`default` labels (one or more per section) at slot index 0. The
    // C# lazily allocates the wrapper; the D222 port makes the collection an always-present stack
    // member, so the accessor returns the member directly (the empty-until-first-Add
    // element-list profile is preserved). `supportsIncremental` is `false` (two collections), so
    // `Add` invalidates the parent's indices.
    AstNodeCollectionT<CaseLabel>& CaseLabels() { return caseLabels_; }
    const AstNodeCollectionT<CaseLabel>& CaseLabels() const { return caseLabels_; }

    // ---- The `Statements` collection slot -------------------------------------------
    // The generated `[Slot("EmbeddedStatement")] public partial AstNodeCollection<Statement>
    // Statements` -- the collection of statements in the section at slot index 1. The
    // `[Slot("EmbeddedStatement")]` argument names the slot KIND `EmbeddedStatement` (reused from
    // the `WhileStatement` D258 single-`Statement` position; the kind-collapsing design is by
    // `[Slot]` name regardless of single-vs-collection); the per-node `StatementsSlot` carries
    // the `IsCollection` flag while the shared `Slots::EmbeddedStatement` kind is constructed
    // non-collection. `supportsIncremental` is `false` (two collections). `baseIndex = 1` (the
    // slot index; the dynamic flattened index `caseLabels_.Count()` is rebuilt lazily by
    // `EnsureChildIndices`, since the fast path is off).
    AstNodeCollectionT<Statement>& Statements() { return statements_; }
    const AstNodeCollectionT<Statement>& Statements() const { return statements_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `CaseLabelsSlot` (a `CSharpSlotInfoT<CaseLabel>` pointing at `Slots.CaseLabel`,
    // collection); the `StatementsSlot` (a `CSharpSlotInfoT<Statement>` pointing at
    // `Slots.EmbeddedStatement`, collection). No name shadowing (no member is named
    // `CaseLabel`/`Statement`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<CaseLabel> CaseLabelsSlot{"CaseLabels", true, &Slots::CaseLabel, true};
    static inline const CSharpSlotInfoT<Statement> StatementsSlot{"Statements", true, &Slots::EmbeddedStatement, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSwitchSection` (`SwitchSection` does not end in "AstType", so the
    // generator's visit-method-name default yields `VisitSwitchSection`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSwitchSection(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitSwitchSection`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitSwitchSection(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Two collections in declaration order: a `CaseLabels` collection at slot 0 (the contiguous
    // range `[0, caseCount)`) and a `Statements` collection at slot 1 (the range `[caseCount,
    // caseCount + stmtCount)`). `GetChildCount` is `caseCount + stmtCount` (both collections'
    // current lengths); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each
    // one's width from a running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a collection step then a collection step).
    // `GetCollectionByKind` returns each collection for its kind. This is the `ComposedType` D242
    // dispatch shape with NO single slot between (collection -> collection).

    int GetChildCount() const override { return caseLabels_.Count() + statements_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = caseLabels_.Count();
            if (i < n)
                return caseLabels_.At(i);
            i -= n;
        }
        {
            int n = statements_.Count();
            if (i < n)
                return statements_.At(i);
        }
        throw std::out_of_range("SwitchSection::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = caseLabels_.Count();
            if (i < n) {
                caseLabels_.SetAt(i, static_cast<CaseLabel*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = statements_.Count();
            if (i < n) {
                statements_.SetAt(i, static_cast<Statement*>(value));
                return;
            }
        }
        throw std::out_of_range("SwitchSection::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = caseLabels_.Count();
            if (i < n)
                return &CaseLabelsSlot;
            i -= n;
        }
        {
            int n = statements_.Count();
            if (i < n)
                return &StatementsSlot;
        }
        throw std::out_of_range("SwitchSection::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::CaseLabel)
            return &caseLabels_;
        if (kind == &Slots::EmbeddedStatement)
            return &statements_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SwitchSection o && this.CaseLabels.DoMatch(o.CaseLabels, match) &&
    // this.Statements.DoMatch(o.Statements, match)`. The two terms are in `MembersToMatch` order,
    // which is the source declaration order (`CaseLabels`, `Statements`). Both terms are
    // collection recursive matches (the generator emits the collection-typed recursive term
    // directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child);
    // there is no scalar term. A type-only mismatch (not a `SwitchSection`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<SwitchSection*>(other);
        if (o == nullptr)
            return false;
        return caseLabels_.DoMatch(o->caseLabels_, match)
            && statements_.DoMatch(o->statements_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `CaseLabels`/`Statements` element deep-cloned through `Add` (which re-parents and
    // re-indexes; `CaseLabel::Clone()` returns `CaseLabel*` and `Statement::Clone()` returns
    // `Statement*`, which the typed `Add`s accept directly). The deep-copy order follows the slot
    // declaration order (`CaseLabels`, `Statements`) matching the generated `CloneChildrenInto`;
    // with both collections non-incremental every mutation invalidates, so the order does not
    // affect the final rebuilt state. No own location fields (`SwitchSection` does not derive
    // `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `ComposedType` D242 / `ArrayInitializerExpression` D250 no-location-copy precedent). NO
    // elaborated specifiers (no member is named `CaseLabel`/`Statement`); the typed `Add`s accept
    // the covariant `Clone()` returns directly.
    SwitchSection* Clone() const override {
        auto* node = new SwitchSection();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < caseLabels_.Count(); i++)
            node->caseLabels_.Add(caseLabels_.At(i)->Clone());
        for (int i = 0; i < statements_.Count(); i++)
            node->statements_.Add(statements_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `caseLabels_`/`statements_` are the always-present collection members
    // (empty until the first `Add`, non-incremental). No name shadowing (no member is named
    // `CaseLabel`/`Statement`), so the field types are the plain classes.
    AstNodeCollectionT<CaseLabel> caseLabels_;
    AstNodeCollectionT<Statement> statements_;
};

// The `SwitchSection` kind -- the collection slot kind for every
// `[Slot("SwitchSection")] AstNodeCollection<SwitchSection>` (`SwitchStatement.SwitchSections`). A
// `CSharpSlotInfoT<SwitchSection>` (the element type is the concrete `SwitchSection` node).
//
// Defined HERE (in SwitchSection.hpp, after the `SwitchSection` class) rather than in Slots.hpp
// because `CSharpSlotInfoT<SwitchSection>` needs `SwitchSection` complete (the
// `dynamic_cast<const SwitchSection*>` is-a test in the ctor), and `SwitchSection` is a concrete
// node with per-node slot statics (its `CaseLabelsSlot`/`StatementsSlot` reference
// `&Slots::CaseLabel`/`&Slots::EmbeddedStatement`, so this header includes Slots.hpp). Placing the
// kind in Slots.hpp would form a circular include: Slots.hpp would have to include
// SwitchSection.hpp (for the complete `SwitchSection`), but SwitchSection.hpp includes Slots.hpp
// (for `Slots::CaseLabel`/`Slots::EmbeddedStatement`), and with Slots.hpp's guard set those
// definitions would not be visible where SwitchSection's class body needs them. After the class
// both `CSharpSlotInfoT` (visible via the Slots.hpp include) and `SwitchSection` are complete, so
// the kind defines cleanly. The `inline` variable still has external linkage and one address
// across translation units (the C++17 `inline` guarantee), preserving the pointer-identity
// comparison `node.Slot.Kind == &Slots::SwitchSection` the slot system relies on. This is the
// `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Initializer`/`Slots::Variable`/
// `Slots::CaseLabel` cycle-breaking precedent (D241/D242/D251/D267) applied to a collection kind.
// The shared constant is constructed non-collection/non-optional
// (`{"SwitchSection", false, nullptr, false}`); the per-node `SwitchSectionsSlot` on the owning
// `SwitchStatement` carries the `IsCollection` flag (the collection `[Slot]` makes the per-node
// slot a collection). The kind name `SwitchSection` collides with the `SwitchSection` CLASS in the
// parent `Syntax` namespace (the `Expression`/`Identifier`/`Statement`/`CaseLabel` collision
// pattern): the template argument in this definition resolves to the class (the constant being
// declared is not yet in scope at the point its type is parsed), and a LATER `Slots` entry wanting
// the `SwitchSection` class as its element type must qualify it
// (`::ILSpy::Decompiler::CSharp::Syntax::SwitchSection`) to avoid resolving to this constant.
namespace Slots {
inline const CSharpSlotInfoT<SwitchSection> SwitchSection{"SwitchSection", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SWITCHSECTION_HPP
