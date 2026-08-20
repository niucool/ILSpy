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

// Port of the `AttributeSection` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/AttributeSection.cs (the generated
// `AttributeSection.g.cs` + the hand-written partial, which declares the `AttributeTarget`
// string convenience accessor, the `AttributeTargetToken` token slot, the `Attributes`
// collection, and the `(Attribute)` hand-written ctor). The next in-order Phase-5 piece per
// the D240 plan ("AttributeSection (an optional Identifier AttributeTargetToken + an Attribute
// Attributes collection ... needed by ComposedType's Attributes collection and by every
// Statement/TypeMember/GeneralScope/ParameterDeclaration node)"):
// `attribute_section ::= '[' ( identifier ':' )? attribute* ']'` (C# grammar 23.3) -- a
// bracketed group of attributes, optionally carrying a target (`[return: ...]`/
// `[assembly: ...]`/...). The `AttributeTargetToken` is an OPTIONAL nullable `Identifier` token
// slot (absent for the common `[Foo]` case, present only for `[target: ...]`); the `Attributes`
// is an `AstNodeCollection<Attribute>` (one or more `Attribute` nodes).
//
// It is the second concrete `AstNode` that lives in the `GeneralScope` sub-namespace of the C#
// source (the C# `ICSharpCode.Decompiler.CSharp.Syntax.AttributeSection`); the port keeps it in
// the flat `Syntax` directory (the established convention -- the Attribute D240 precedent). It
// derives DIRECTLY from `AstNode` (not `AstType` and not `Expression`): an attribute section is a
// structural container, not a type reference or an expression.
//
// It is the first ported node to combine an OPTIONAL (nullable) single `Identifier` child slot
// with a collection slot -- the `SimpleType` shape (a nullable `Identifier` token + a collection),
// but with an `Attribute` collection instead of `AstType` type arguments. Its generated `DoMatch`
// has two terms in `MembersToMatch` order: a `MatchString` on the `AttributeTarget` string (the
// hand-written `string` convenience accessor -- a non-`[Slot]` instance `string` property, so the
// generator adds it as a `String` term; the backing `AttributeTargetToken` is `[ExcludeFromMatch]`
// so the token is not matched twice) and a collection recursive `Attributes` term
// (`this.Attributes.DoMatch`). NO `MatchOptional` (the token is `[ExcludeFromMatch]`, not a
// matched child), NO `MatchRequired` (no required child), NO plain-equality scalar.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitAttributeSection(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitAttributeSection`). The generated slot
// statics are `AttributeTargetTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at
// `Slots.Identifier`, optional -- the target may be absent) and `AttributesSlot` (a
// `CSharpSlotInfoT<Attribute>` pointing at `Slots.Attribute`, collection). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the `AttributeTargetToken` (if present) and every `Attributes` element through
// `Add` (which re-parents and re-indexes).
//
// NO C++ name-shadowing crux (unlike `SimpleType`/`UnaryOperatorExpression`): the `AttributeTarget`
// and `AttributeTargetToken` accessors are not named `Identifier`/`Expression`/`AstType`, so no
// class in the `Syntax` namespace is shadowed, and no elaborated-type-specifier is needed. The
// `Identifier::Create` factory call in the `AttributeTarget` setter is unqualified (the
// `Identifier` class is not shadowed by a member named `Identifier`).
//
// The `AttributeTarget` hand-written string property is implemented directly over the
// `attributeTargetToken_` backing field (the `SimpleType` `Identifier()` precedent -- the token IS
// the only `Identifier`-kind slot, so reading/writing it directly is semantically identical to the
// C# `GetChild(Slots.Identifier)`/`SetChild(Slots.Identifier, ...)` kind-scan). The getter returns
// `string.Empty` (an empty `std::string`) when the token is absent (the C# `?? string.Empty`); the
// setter creates the token via `Identifier::Create` (NOT `CreateIfNotEmpty` -- the C# uses
// `Identifier.Create`, so an empty target still creates a token with an empty `Name`, not a null
// token, faithful to the C#).
//
// The collection ctors (`AttributeSection(Identifier?, IEnumerable<Attribute>)` and the
// `params Attribute[]` form) are DEFERRED: they use `AddRange`, which lands with the collection
// convenience mutators (the D222 deferral). The empty + the hand-written `(Attribute)` ctors
// cover the construction API; further attributes are added via `Attributes().Add(...)` until
// `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ATTRIBUTESECTION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ATTRIBUTESECTION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class AttributeSection : AstNode`. `final` (the C# `sealed`):
// no further derivation. A structural container (a bracketed group of attributes on a
// target), not an `AstType` and not an `Expression`. The second ported `GeneralScope`-sub-
// namespace node, and the first ported node to combine an optional (nullable) single
// `Identifier` child with a collection.
class AttributeSection final : public AstNode {
public:
    ~AttributeSection() override = default;

    // The generated empty ctor (the C# `public AttributeSection()`). The `Attributes` collection
    // is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `AttributeTargetToken` single slot at index 0 precedes it) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so an
    // element's flattened `ChildIndex` is exactly `1 + its local position`). The
    // `AttributeTargetToken` defaults to null (no target) via its default member initializer.
    AttributeSection() : attributes_(this, &AttributesSlot, 1, true) {}

    // The hand-written `public AttributeSection(Attribute attr)` (the C# source) -- adds a
    // single attribute to the `Attributes` collection. Delegates to the empty ctor so the
    // collection member is initialized.
    explicit AttributeSection(Attribute* attr) : AttributeSection() {
        attributes_.Add(attr);
    }

    // ---- The `AttributeTargetToken` slot (the backing `Identifier` token of the target) ----
    // The generated `[Slot("Identifier")] public partial Identifier? AttributeTargetToken` -- a
    // single OPTIONAL (nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. Absent for the common `[Foo]` (no target); present only for
    // `[target: ...]`. No name shadowing (the `AttributeTargetToken` accessor does not collide
    // with the `Identifier` class -- no member is named `Identifier`).
    Identifier* AttributeTargetToken() const { return attributeTargetToken_; }
    void AttributeTargetToken(Identifier* value) {
        SetChildNode(attributeTargetToken_, value, 0);
    }

    // ---- The `AttributeTarget` string convenience accessor (over the token) -------------
    // The hand-written `public string AttributeTarget` -- a convenience string over the
    // `AttributeTargetToken` slot. The C# `get => GetChild(Slots.Identifier)?.Name ??
    // string.Empty; set => SetChild(Slots.Identifier, Identifier.Create(value))`. Implemented
    // directly over the `attributeTargetToken_` field (the token is the only `Identifier`-kind
    // slot, so reading/writing it directly is the faithful kind-scan equivalent). The getter
    // returns an empty `std::string` when the token is absent (the C# `?? string.Empty`); the
    // setter creates the token via `Identifier::Create` (NOT `CreateIfNotEmpty` -- the C# uses
    // `Identifier.Create`, so an empty target creates a token with an empty `Name`, not a null
    // token). Not a `[Slot]` and not `[ExcludeFromMatch]`, so it IS in `MembersToMatch` (a
    // `String` term matched via `MatchString`).
    std::string AttributeTarget() const {
        return attributeTargetToken_ != nullptr ? attributeTargetToken_->Name() : std::string();
    }
    void AttributeTarget(std::string_view value) {
        AttributeTargetToken(Identifier::Create(std::string(value)));
    }

    // ---- The `Attributes` collection slot -------------------------------------------
    // The generated `public partial AstNodeCollection<Attribute> Attributes` -- the collection
    // of attributes (a `CSharpSlotInfoT<Attribute>` slot at flattened index 1, the node's only
    // collection and last slot). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly.
    AstNodeCollectionT<Attribute>& Attributes() { return attributes_; }
    const AstNodeCollectionT<Attribute>& Attributes() const { return attributes_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `AttributeTargetTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at
    // `Slots.Identifier`, optional -- the target may be absent); the `AttributesSlot` (a
    // `CSharpSlotInfoT<Attribute>` pointing at `Slots.Attribute`, collection). No name
    // shadowing (`Identifier`/`Attribute` resolve to the classes -- no member is named
    // `Identifier` or `Attribute`).
    static inline const CSharpSlotInfoT<Identifier> AttributeTargetTokenSlot{"AttributeTargetToken", false, &Slots::Identifier, true};
    static inline const CSharpSlotInfoT<Attribute> AttributesSlot{"Attributes", true, &Slots::Attribute, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitAttributeSection`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitAttributeSection(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitAttributeSection`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitAttributeSection(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // An `AttributeTargetToken` single slot at index 0 and an `Attributes` collection occupying
    // the contiguous range [1, 1 + Count). `GetChildCount` is `1 + Count` (the single slot plus
    // the collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single case
    // then a collection step). `GetCollectionByKind` returns the `Attributes` collection for the
    // `Attribute` kind (the node's only collection).

    int GetChildCount() const override { return 1 + attributes_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return attributeTargetToken_;
        i--;
        int n = attributes_.Count();
        if (i < n)
            return attributes_.At(i);
        throw std::out_of_range("AttributeSection::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(attributeTargetToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        int n = attributes_.Count();
        if (i < n) {
            attributes_.SetAt(i, static_cast<Attribute*>(value));
            return;
        }
        throw std::out_of_range("AttributeSection::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &AttributeTargetTokenSlot;
        i--;
        int n = attributes_.Count();
        if (i < n)
            return &AttributesSlot;
        throw std::out_of_range("AttributeSection::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Attribute)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is AttributeSection o && MatchString(this.AttributeTarget, o.AttributeTarget)
    // && this.Attributes.DoMatch(o.Attributes, match)`. The terms are in `MembersToMatch` order,
    // which is the source declaration order (`AttributeTarget`, `Attributes`). The
    // `AttributeTarget` term is a `String` `MatchString` (the hand-written `string` property; the
    // `$any$` wildcard in the pattern's `AttributeTarget` matches any candidate target); the
    // `Attributes` term is a RECURSIVE collection match -- the generator emits
    // `this.Attributes.DoMatch(o.Attributes, match)` (the `AstNodeCollection`-typed recursive
    // term, NOT `MatchOptional`, which the generator emits only for a nullable non-collection
    // child -- the `AttributeTargetToken` is `[ExcludeFromMatch]` so it never appears in
    // `MembersToMatch`). A type-only mismatch (not an `AttributeSection`) rejects early.
    // `AttributeTarget()` returns `std::string` (non-null, the C# `string`); `MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per side (the temporary `std::string`
    // lives until the end of the full `return` expression, keeping the views valid for the
    // `MatchString` call).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<AttributeSection*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(AttributeTarget()),
                   std::optional<std::string_view>(o->AttributeTarget()))
            && attributes_.DoMatch(o->attributes_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `AttributeTargetToken` deep-cloned through the setter (which re-parents; the cloned token
    // carries its own `Name`), and every `Attributes` element deep-cloned through `Add` (which
    // re-parents and re-indexes; `Attribute::Clone()` returns `Attribute*`, which `Add(Attribute*)`
    // accepts directly). No own location fields (`StartLocation`/`EndLocation` are the print-time
    // base fields set by the unported output visitor), so they are not copied (the
    // ConditionalExpression/SimpleType/MemberType/ArraySpecifier/Attribute precedent for nodes
    // without derived locations).
    AttributeSection* Clone() const override {
        auto* node = new AttributeSection();
        node->CloneAnnotationsFrom(*this);
        if (attributeTargetToken_ != nullptr)
            node->AttributeTargetToken(attributeTargetToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributeTargetToken_` is null until the target is set (an optional
    // slot -- `CheckInvariant` does NOT assert it is filled); `attributes_` is the always-present
    // collection member (empty until the first `Add`). No name shadowing (no member is named
    // `Identifier` or `Attribute`), so the field types are the plain classes.
    Identifier* attributeTargetToken_ = nullptr;
    AstNodeCollectionT<Attribute> attributes_;
};

// The `AttributeSection` kind -- the collection-element kind for every
// `[Slot("AttributeSection")] AstNodeCollection<AttributeSection>` (`ComposedType.Attributes`,
// and the `Attributes` collections on the Statement/TypeMember/GeneralScope/
// ParameterDeclaration nodes that land later). A `CSharpSlotInfoT<AttributeSection>` (the
// element type is the concrete `AttributeSection` node).
//
// Defined HERE (in AttributeSection.hpp, after the `AttributeSection` class) rather than in
// Slots.hpp because `CSharpSlotInfoT<AttributeSection>` needs `AttributeSection` complete (the
// `dynamic_cast<const AttributeSection*>` is-a test in the ctor), and `AttributeSection` is a
// concrete node that INCLUDES `Slots.hpp` (its `AttributeTargetTokenSlot` references
// `&Slots::Identifier` and its `AttributesSlot` references `&Slots::Attribute`). Placing the kind
// in `Slots.hpp` would form a circular include (the `Slots::Attribute` cycle-breaking precedent
// in Attribute.hpp): `Slots.hpp` would have to include `AttributeSection.hpp` (for the complete
// `AttributeSection`), but `AttributeSection.hpp` includes `Slots.hpp` (for `Slots::Identifier`/
// `Slots::Attribute`), and with `Slots.hpp`'s guard set those definitions would not be visible
// where `AttributeSection.hpp`'s class body needs them. After the `AttributeSection` class both
// `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and `AttributeSection` are complete, so
// the kind defines cleanly. The `inline` variable still has external linkage and one address
// across translation units (the C++17 `inline` guarantee), preserving the pointer-identity
// comparison `node.Slot.Kind == &Slots::AttributeSection` the slot system relies on.
namespace Slots {
inline const CSharpSlotInfoT<AttributeSection> AttributeSection{"AttributeSection", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ATTRIBUTESECTION_HPP
