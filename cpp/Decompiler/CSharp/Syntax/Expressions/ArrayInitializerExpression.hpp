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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `ArrayInitializerExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.cs (the generated
// `ArrayInitializerExpression.g.cs` + the hand-written partial). The next in-order Phase-5
// piece: the dependency of `ObjectCreateExpression.Initializer` and `ArrayCreateExpression.
// Initializer` (both nullable `[Slot("Initializer")] ArrayInitializerExpression?` single
// slots), so it lands before them. `array_initializer ::= '{' expression* '}'` (C# grammar
// 17.7): an `ArrayInitializerExpression` is an `Expression` whose sole child slot is the
// `Elements` collection of `Expression` (the comma-separated element list inside the braces).
//
// It is the SIMPLEST collection-slot node ported so far: the only node with a COLLECTION slot
// and NO single child slot. The `Elements` collection (an `AstNodeCollection<Expression>`) is
// the node's only slot and its last slot, so `supportsIncremental` is true (an element's
// flattened `ChildIndex` is exactly its local position) and `GetChildCount` is the collection's
// current length (an empty node reports `GetChildCount` 0 -- the first ported node where an
// empty node has no single-slot count term). It is also the FIRST ported concrete node that is
// NOT sealed: the C# declares `public partial class ArrayInitializerExpression` (no `sealed`)
// because `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a sealed nested
// `PatternPlaceholder : ArrayInitializerExpression` that derives from it. The port therefore
// does NOT use `final` (faithful to the C# not being sealed), and the pattern placeholder is
// DEFERRED (the D226 deferral: `hasPatternPlaceholder` emits an implicit
// `operator ArrayInitializerExpression(Pattern)` plus a sealed `PatternPlaceholder` nested
// class implementing `INode`/`IPatternPlaceholder`; it lands with the concrete pattern nodes
// and `VisitPatternPlaceholder` on `IAstVisitor`). `Clone` is a per-concrete-node override
// regardless (no `MemberwiseClone` in C++).
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Elements.DoMatch(o.Elements, match)` (the generator emits the collection-typed
// recursive term directly -- NOT `MatchOptional`, which it emits only for a nullable
// NON-collection child). Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is
// hand-translated from the generated output rather than regenerated. The generated
// `AcceptVisitor` calls `visitor.VisitArrayInitializerExpression(this)` (the class name does
// not end in "AstType", so the generator's visit-method-name default yields
// `VisitArrayInitializerExpression`). The generated slot static is `ElementsSlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, collection).
//
// NO C++ name-shadowing crux: the `Elements()` accessor is a member function, but no class
// named `Elements` lives in the `Syntax` namespace, and no member is named `Expression`
// (the `Elements` accessor does not collide with the `Expression` base type -- it is
// `Elements()`, not `Expression()`), so no elaborated-type-specifier (`class Expression`) is
// needed anywhere, and the plain `Expression` resolves to the base class in every type
// position (the `MemberReferenceExpression`/`InvocationExpression` differently-named-property
// precedent).
//
// NO new `Slots` constant: the `[Slot("Expression")]` argument names the slot kind
// "Expression", already ported by `UnaryOperatorExpression` (D231) as a single-`Expression`
// operand position. The kind carries identity + the element type only; the per-position
// `IsCollection`/`IsOptional` flags live on the per-node slot, so the shared
// `Slots::Expression` constant (constructed non-collection) is reused as the kind for this
// collection slot -- the first ported node to reuse `Slots::Expression` for a COLLECTION
// position (the kind is keyed by the `[Slot]` name, not by single-vs-collection).
//
// `ArrayInitializerExpression.cs` declares NO hand-written ctors (only the `Elements` slot
// property), so the port carries only the generated ctors. The generated collection ctors
// (`ArrayInitializerExpression(IEnumerable<Expression>)` and the `params Expression[]` form)
// use `AddRange`, which lands with the collection convenience mutators (the D222 deferral),
// so they are DEFERRED; the empty ctor is the only portable ctor, and an element list is built
// via `Elements().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ARRAYINITIALIZEREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ARRAYINITIALIZEREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public partial class ArrayInitializerExpression : Expression` (NOT `sealed` -- the
// generated `PatternPlaceholder` derives from it; the pattern placeholder is deferred, but the
// class stays non-`final` to match the C# and to not block the placeholder landing). The first
// ported concrete node that is not sealed, and the first with a collection slot and no single
// child slot.
class ArrayInitializerExpression : public Expression {
public:
    ~ArrayInitializerExpression() override = default;

    // The generated empty ctor (the C# `public ArrayInitializerExpression()`). The `Elements`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (it is the node's only slot, so the first element's flattened `ChildIndex`
    // is 0) and `supportsIncremental = true` (it is the node's only collection and its last slot,
    // so an element's flattened `ChildIndex` is exactly its local position). The collection
    // starts empty (no elements); the node has no required single slots, so a default-constructed
    // node is a valid empty initializer (`CheckInvariant` passes).
    ArrayInitializerExpression() : elements_(this, &ElementsSlot, 0, true) {}

    // ---- The `Elements` collection slot ------------------------------------------
    // The generated `[Slot("Expression")] public partial AstNodeCollection<Expression>
    // Elements` -- the collection of element expressions (a `CSharpSlotInfoT<Expression>` slot at
    // flattened index 0, the node's only collection and last slot). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-Add element-list profile is preserved --
    // `list_` is empty until the first `Add`).
    AstNodeCollectionT<Expression>& Elements() { return elements_; }
    const AstNodeCollectionT<Expression>& Elements() const { return elements_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag is
    // true (the slot is a collection); the `IsOptional` flag is true (the generator sets it true
    // for every collection slot). The kind is `Slots::Expression` (the shared "Expression" slot
    // name, ported by `UnaryOperatorExpression` as a single-`Expression` operand position --
    // reused here as the kind for the collection, the first collection reuse of `Slots::
    // Expression`). No name shadowing (no member is named `Expression`), so the element type is
    // the plain `Expression`.
    static inline const CSharpSlotInfoT<Expression> ElementsSlot{"Elements", true, &Slots::Expression, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitArrayInitializerExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitArrayInitializerExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `Elements` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- no single-slot count term,
    // the first ported node where an empty node has `GetChildCount` 0); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the single collection slot (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape with one
    // collection step and no single step). `GetCollectionByKind` returns the `Elements`
    // collection for the `Expression` kind.

    int GetChildCount() const override { return elements_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = elements_.Count();
        if (i < n)
            return elements_.At(i);
        throw std::out_of_range("ArrayInitializerExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = elements_.Count();
        if (i < n) {
            elements_.SetAt(i, static_cast<Expression*>(value));
            return;
        }
        throw std::out_of_range("ArrayInitializerExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = elements_.Count();
        if (i < n)
            return &ElementsSlot;
        throw std::out_of_range("ArrayInitializerExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Expression)
            return &elements_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ArrayInitializerExpression o && this.Elements.DoMatch(o.Elements, match)`.
    // The single term is the collection recursive match (the generator emits the
    // collection-typed recursive term directly, NOT `MatchOptional`, which it emits only for a
    // nullable NON-collection child). A type-only mismatch (not an `ArrayInitializerExpression`)
    // rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ArrayInitializerExpression*>(other);
        if (o == nullptr)
            return false;
        return elements_.DoMatch(o->elements_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Elements` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Expression::Clone()` returns `Expression*`, the covariant override, which
    // `Add(Expression*)` accepts directly). No own location fields (`StartLocation`/
    // `EndLocation` are the print-time base fields set by the unported output visitor), so they
    // are not copied (the ConditionalExpression/SimpleType/MemberType/Attribute/
    // InvocationExpression precedent for nodes without derived locations). The covariant
    // return is `ArrayInitializerExpression*` (through `Expression*`, the `Expression::Clone`
    // pure-virtual).
    ArrayInitializerExpression* Clone() const override {
        auto* node = new ArrayInitializerExpression();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < elements_.Count(); i++)
            node->elements_.Add(elements_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `elements_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no member is named `Expression`), so the field type is the
    // plain class.
    AstNodeCollectionT<Expression> elements_;
};

// The `Initializer` kind -- the nullable single-child slot kind for every
// `[Slot("Initializer")] ArrayInitializerExpression?` (`ObjectCreateExpression.Initializer`,
// `ArrayCreateExpression.Initializer`). A `CSharpSlotInfoT<ArrayInitializerExpression>` (the
// element type is the concrete `ArrayInitializerExpression` node).
//
// Defined HERE (in ArrayInitializerExpression.hpp, after the `ArrayInitializerExpression` class)
// rather than in Slots.hpp because `CSharpSlotInfoT<ArrayInitializerExpression>` needs
// `ArrayInitializerExpression` complete (the `dynamic_cast<const ArrayInitializerExpression*>`
// is-a test in the ctor), and `ArrayInitializerExpression` is a concrete node with per-node slot
// statics (its `ElementsSlot` references `&Slots::Expression`, so this header includes Slots.hpp).
// Placing the kind in Slots.hpp would form a circular include: Slots.hpp would have to include
// ArrayInitializerExpression.hpp (for the complete `ArrayInitializerExpression`), but
// ArrayInitializerExpression.hpp includes Slots.hpp (for `Slots::Expression`), and with Slots.hpp's
// guard set the `Slots::Expression` definition would not be visible where ArrayInitializerExpression's
// class body needs it. After the class both `CSharpSlotInfoT` (visible via the Slots.hpp include)
// and `ArrayInitializerExpression` are complete, so the kind defines cleanly. The `inline`
// variable still has external linkage and one address across translation units (the C++17
// `inline` guarantee), preserving the pointer-identity comparison `node.Slot.Kind ==
// &Slots::Initializer` the slot system relies on. This is the `Slots::Attribute`/
// `Slots::AttributeSection` cycle-breaking precedent (D241/D242) applied to a NULLABLE
// single-child kind (the first such): the shared constant is constructed non-collection/non-optional
// (`{"Initializer", false, nullptr, false}`), and the per-node `InitializerSlot` on the owning
// node carries the `IsOptional` flag (the nullable `[Slot]` makes the per-node slot optional).
namespace Slots {
inline const CSharpSlotInfoT<ArrayInitializerExpression> Initializer{"Initializer", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ARRAYINITIALIZEREXPRESSION_HPP
