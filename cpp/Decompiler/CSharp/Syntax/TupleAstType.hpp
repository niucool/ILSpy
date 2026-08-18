// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `TupleAstType` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TupleAstType.cs (the generated `TupleAstType.g.cs` -- the
// hand-written partial declares only the `Elements` slot property). The next in-order Phase-5
// piece per the D288 plan ("the remaining GeneralScope ... FunctionPointerAstType,
// InvocationAstType, TupleAstType, SyntaxTree"). `tuple_type ::= '(' tuple_type_element (','
// tuple_type_element)+ ')'` (C# grammar 8.3.1): a tuple type is an `AstType` whose sole child
// slot is the `Elements` collection of `TupleTypeElement` (the comma-separated element list
// inside the parentheses, two or more).
//
// It is the SIMPLEST collection-slot `AstType` ported so far: the only node with a COLLECTION
// slot and NO single child slot, applied to the `AstType` hierarchy -- the
// `ArrayInitializerExpression` D250 / `BlockStatement` D256 collection-only shape with a
// `TupleTypeElement` element. The `Elements` collection (an `AstNodeCollection<TupleTypeElement>`)
// is the node's only slot and its last slot, so `supportsIncremental` is true (an element's
// flattened `ChildIndex` is exactly its local position) and `GetChildCount` is the collection's
// current length (an empty node reports `GetChildCount` 0 -- the `ArrayInitializerExpression` D250
// precedent). The C# declares `public sealed partial class TupleAstType : AstType` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder` is false, so `final`).
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Elements.DoMatch(o.Elements, match)` (the generator emits the collection-typed recursive
// term directly -- NOT `MatchOptional`, which it emits only for a nullable NON-collection child).
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitTupleType(this)`
// (`TupleAstType` ends in "AstType", so the generator's visit-method-name rewriting
// (`s.Replace("AstType", "Type")`) yields `VisitTupleType` -- the `FunctionPointerAstType`/
// `InvocationAstType`/`TupleAstType` rewriting the D236 note flagged). The generated slot static
// is `ElementsSlot` (a `CSharpSlotInfoT<TupleTypeElement>` pointing at `Slots.Element`,
// collection).
//
// NO C++ name-shadowing crux: the `Elements()` accessor is a member function, but no class named
// `Elements` lives in the `Syntax` namespace, and no member is named `TupleTypeElement` (the
// `Elements` accessor does not collide with the `TupleTypeElement` class -- the
// `ArrayInitializerExpression` D250 / `MemberReferenceExpression` D247 differently-named-property
// precedent), so no elaborated-type-specifier is needed anywhere, and the plain `TupleTypeElement`
// resolves to the concrete class in every type position.
//
// The new `Slots::Element` kind (a `CSharpSlotInfoT<TupleTypeElement>` collection kind) is
// cycle-broken into `TupleTypeElement.hpp` (this header's dependency) after the
// `TupleTypeElement` class -- `TupleTypeElement.hpp` includes `Slots.hpp` for its per-node slot
// statics, so the kind cannot live in `Slots.hpp` (the `Slots::Attribute` D241 /
// `Slots::Variable` D267 cycle-breaking precedent). `Slots.hpp` itself is unchanged.
//
// `TupleAstType.cs` declares NO hand-written ctors (only the `Elements` slot property), so the
// port carries only the generated ctors. The generated collection ctors
// (`TupleAstType(IEnumerable<TupleTypeElement>)` and the `params TupleTypeElement[]` form) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so they
// are DEFERRED; the empty ctor is the only portable ctor, and an element list is built via
// `Elements().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_TUPLEASTTYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_TUPLEASTTYPE_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/TupleTypeElement.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class TupleAstType : AstType`. `final` (the C# `sealed`): no
// further derivation. The first collection-slot `AstType` with no single child slot, the
// `ArrayInitializerExpression` D250 collection-only shape applied to the `AstType` hierarchy
// with a `TupleTypeElement` element.
class TupleAstType final : public AstType {
public:
    ~TupleAstType() override = default;

    // The generated empty ctor (the C# `public TupleAstType()`). The `Elements` collection is a
    // member (the D222 always-present-stack-member design), initialized here with `baseIndex = 0`
    // (it is the node's only slot, so the first element's flattened `ChildIndex` is 0) and
    // `supportsIncremental = true` (it is the node's only collection and last slot, so an
    // element's flattened `ChildIndex` is exactly its local position). The collection starts empty
    // (no elements); the node has no required single slots, so a default-constructed node is a
    // valid empty tuple type (`CheckInvariant` passes -- the `ArrayInitializerExpression` D250
    // collection-only precedent).
    TupleAstType() : elements_(this, &ElementsSlot, 0, true) {}

    // ---- The `Elements` collection slot ----------------------------------------------
    // The generated `[Slot("Element")] public partial AstNodeCollection<TupleTypeElement>
    // Elements` -- the collection of tuple elements (a `CSharpSlotInfoT<TupleTypeElement>` slot at
    // flattened index 0, the node's only collection and last slot). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-Add element-list profile is preserved --
    // `list_` is empty until the first `Add`).
    AstNodeCollectionT<TupleTypeElement>& Elements() { return elements_; }
    const AstNodeCollectionT<TupleTypeElement>& Elements() const { return elements_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag is
    // true (the slot is a collection); the `IsOptional` flag is true (the generator sets it true
    // for every collection slot). The kind is `Slots::Element` (cycle-broken into
    // `TupleTypeElement.hpp`, the `Slots::Attribute`/`Slots::Variable` precedent). No name
    // shadowing (no member is named `TupleTypeElement`), so the element type is the plain
    // `TupleTypeElement`.
    static inline const CSharpSlotInfoT<TupleTypeElement> ElementsSlot{"Elements", true, &Slots::Element, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitTupleType` (`TupleAstType` ends in "AstType", so the generator's
    // visit-method-name rewriting yields `VisitTupleType`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitTupleType(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `Elements` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- no single-slot count term,
    // the `ArrayInitializerExpression` D250 precedent); `GetChild`/`SetChild`/`GetChildSlotInfo`
    // walk the single collection slot (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape with one collection step and no single step).
    // `GetCollectionByKind` returns the `Elements` collection for the `Element` kind.

    int GetChildCount() const override { return elements_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = elements_.Count();
        if (i < n)
            return elements_.At(i);
        throw std::out_of_range("TupleAstType::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = elements_.Count();
        if (i < n) {
            elements_.SetAt(i, static_cast<TupleTypeElement*>(value));
            return;
        }
        throw std::out_of_range("TupleAstType::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = elements_.Count();
        if (i < n)
            return &ElementsSlot;
        throw std::out_of_range("TupleAstType::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Element)
            return &elements_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is TupleAstType o && this.Elements.DoMatch(o.Elements, match)`. The single
    // term is the collection recursive match (the generator emits the collection-typed recursive
    // term directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection
    // child). A type-only mismatch (not a `TupleAstType`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<TupleAstType*>(other);
        if (o == nullptr)
            return false;
        return elements_.DoMatch(o->elements_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Elements` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `TupleTypeElement::Clone()` returns `TupleTypeElement*`, the covariant override through
    // `AstNode*`, which `Add(TupleTypeElement*)` accepts directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor), so they are not copied (the `ArrayInitializerExpression` D250 /
    // `SimpleType` D237 no-location-copy precedent). The covariant return is `TupleAstType*`
    // (through `AstType*`, the `AstType::Clone` pure-virtual -- the typed return the C# `new
    // AstType Clone()` gives, applied to the concrete `TupleAstType`).
    TupleAstType* Clone() const override {
        auto* node = new TupleAstType();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < elements_.Count(); i++)
            node->elements_.Add(elements_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `elements_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no member is named `TupleTypeElement`), so the field type
    // is the plain class.
    AstNodeCollectionT<TupleTypeElement> elements_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_TUPLEASTTYPE_HPP
