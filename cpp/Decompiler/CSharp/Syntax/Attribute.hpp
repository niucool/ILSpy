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

// Port of the `Attribute` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/Attribute.cs (the generated
// `Attribute.g.cs` + the hand-written partial, which declares only the properties, no ctors).
// The next in-order Phase-5 piece per the D239 plan ("Attribute (a sealed AstNode with a
// required AstType Type slot + an Expression Arguments collection + a HasArgumentList bool
// scalar -- the MemberType shape, needed by AttributeSection)"):
// `attribute ::= type ( '(' expression* ')' )?` (C# grammar 23.3) -- an attribute is a `Type`
// reference (the attribute class) plus an optional argument list. The `Type` is a REQUIRED
// `AstType` child slot; the `Arguments` is an `AstNodeCollection<Expression>` (zero or more
// argument expressions); `HasArgumentList` is a plain bool distinguishing `[Foo]` (no argument
// list, `false`) from `[Foo()]` (an empty argument list, `true`).
//
// It is the first concrete `AstNode` that lives in the `GeneralScope` sub-namespace of the C#
// source (the C# `ICSharpCode.Decompiler.CSharp.Syntax.Attribute`); the port keeps it in the
// flat `Syntax` directory (the established convention -- no ported file uses the C# subfolder
// layout). It derives DIRECTLY from `AstNode` (not `AstType` and not `Expression`): an
// attribute is a structural node attached to an `AttributeSection`, not a type reference or an
// expression.
//
// It is the second ported node to combine a REQUIRED `AstType` child (`Type`) with a collection
// slot (`Arguments`) -- the `MemberType` shape (Target AstType + TypeArguments collection),
// but SIMPLER: `Attribute` has NO string-name `[Slot]` (no backing `Identifier` token), so
// there is no `MemberName`/`MemberNameToken` pair and no `MatchString` term. Its generated
// `DoMatch` has three terms in source declaration order: a non-nullable recursive `Type` term
// (dispatched through `MatchRequired` -- the D231 [class.access.derived] workaround), a
// collection recursive `Arguments` term (`this.Arguments.DoMatch`), and a plain-equality
// `HasArgumentList` bool term (the fall-through, after the recursive/hasAny/String branches -- a
// bool is not an enum, no `Any`). It is the second ported node with a plain (non-`[Slot]`,
// non-enum) bool scalar (`MemberType.IsDoubleColon` was the first).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitAttribute(this)` (the class name does not end in "AstType", so the generator's
// visit-method-name default yields `VisitAttribute`). The generated slot statics are `TypeSlot`
// (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the `Type` `AstType` is
// non-nullable) and `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at
// `Slots.Argument`, collection). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): copies the
// `HasArgumentList` scalar, deep-clones the `Type` through the setter (which re-parents), and
// deep-clones every `Arguments` element through `Add` (which re-parents and re-indexes).
//
// NO C++ name-shadowing crux (unlike `SimpleType`/`UnaryOperatorExpression`): the `Type()`
// accessor is a member function, but no class named `Type` lives in the `Syntax` namespace
// (there is `AstType`, not `Type`), and no member is named `Expression` (the `Arguments`
// accessor does not collide with the `Expression` base). So no elaborated-type-specifier (`class
// Type` / `class Expression`) is needed anywhere, and the `Slots.Type` / `Slots.Argument`
// references in the slot statics are fully qualified (the `Slots` constants live in the
// `Slots` namespace, not the `Syntax` namespace, so they do not collide with the `Type()`
// accessor).
//
// The collection ctors that take arguments (`Attribute(AstType, IEnumerable<Expression>)` and
// the `params Expression[]` form) are DEFERRED: they use `AddRange`, which lands with the
// collection convenience mutators (the D222 deferral). The empty + the `(AstType)` required
// prefix ctors cover the construction API; an argument list is built via `Arguments().Add(...))"
// until `AddRange` lands. `Attribute.cs` declares NO hand-written ctors (unlike `SimpleType`),
// so the port carries only the generated ctors.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ATTRIBUTE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ATTRIBUTE_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class Attribute : AstNode`. `final` (the C# `sealed`): no
// further derivation. A structural node (an attribute on an `AttributeSection`), not an
// `AstType` and not an `Expression`. The second ported node to combine a required `AstType`
// child with a collection, and the first ported `GeneralScope`-sub-namespace node.
class Attribute final : public AstNode {
public:
    ~Attribute() override = default;

    // The generated empty ctor (the C# `public Attribute()`). The `Arguments` collection is a
    // member (the D222 always-present-stack-member design), initialized here with `baseIndex =
    // 1` (the `Type` single slot at index 0 precedes it) and `supportsIncremental = true` (it is
    // the node's only collection and its last slot, so an element's flattened `ChildIndex` is
    // exactly `1 + its local position`). The `Type` defaults to null (no type) via its default
    // member initializer; `HasArgumentList` defaults to false.
    Attribute() : arguments_(this, &ArgumentsSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public Attribute(AstType type)`): the required
    // prefix runs through the last non-optional ctor param (`Type` is required; `Arguments` is an
    // optional collection). Sets `Type` in declaration order (the generator's ctor body emits the
    // assignments in `CtorParams` order, which is the source declaration order). Delegates to the
    // empty ctor so the collection member is initialized.
    explicit Attribute(AstType* type) : Attribute() {
        Type(type);
    }

    // ---- The `HasArgumentList` bool scalar (a plain property, not a `[Slot]`) ------------
    // The C# `public bool HasArgumentList { get; set; }` -- whether the attribute carries an
    // argument list at all (`[Foo]` is `false`, `[Foo()]` is `true`). A plain bool field: not a
    // child slot (no `[Slot]`), not a ctor param (the generator adds only settable ENUM-typed
    // scalars to `CtorParams`, and a bool is not an enum), so it is set via the property setter
    // (object initializer). It IS in `MembersToMatch` (the generator adds every non-`[Slot]`
    // instance property), and a bool (not an enum, no `Any`) emits the fall-through plain-equality
    // `DoMatch` term. No name shadowing (no class named `HasArgumentList`).
    bool HasArgumentList() const { return hasArgumentList_; }
    void HasArgumentList(bool value) { hasArgumentList_ = value; }

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

    // ---- The `Arguments` collection slot -------------------------------------------
    // The generated `public partial AstNodeCollection<Expression> Arguments` -- the collection
    // of argument expressions (a `CSharpSlotInfoT<Expression>` slot at flattened index 1, the
    // node's only collection and last slot). The C# lazily allocates the wrapper; the D222 port
    // makes the collection an always-present stack member, so the accessor returns the member
    // directly (the empty-until-first-Add element-list profile is preserved).
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
    // `Type` `AstType` is non-nullable); the `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>`
    // pointing at `Slots.Argument`, collection). No name shadowing (`AstType`/`Expression`
    // resolve to the classes -- no member is named `AstType` or `Expression`).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Argument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitAttribute`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitAttribute(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A `Type` single slot at index 0 and an `Arguments` collection occupying the contiguous
    // range [1, 1 + Count). `GetChildCount` is `1 + Count` (the single slot plus the
    // collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single case
    // then a collection step). `GetCollectionByKind` returns the `Arguments` collection for the
    // `Argument` kind (the node's only collection).

    int GetChildCount() const override { return 1 + arguments_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return type_;
        i--;
        int n = arguments_.Count();
        if (i < n)
            return arguments_.At(i);
        throw std::out_of_range("Attribute::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(type_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        int n = arguments_.Count();
        if (i < n) {
            arguments_.SetAt(i, static_cast<Expression*>(value));
            return;
        }
        throw std::out_of_range("Attribute::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TypeSlot;
        i--;
        int n = arguments_.Count();
        if (i < n)
            return &ArgumentsSlot;
        throw std::out_of_range("Attribute::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Argument)
            return &arguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is Attribute o && this.Type.DoMatch(o.Type, match) &&
    // this.Arguments.DoMatch(o.Arguments, match) && this.HasArgumentList == o.HasArgumentList`.
    // The terms are in `MembersToMatch` order, which is the source declaration order (`Type`,
    // `Arguments`, `HasArgumentList`). The `Type` term is a non-nullable recursive child, so the
    // generator emits a DIRECT `this.Type.DoMatch(o.Type, match)` -- ported through
    // `MatchRequired` (the D231 [class.access.derived] workaround, since a derived node may not
    // call the protected `DoMatch` through a base `AstType*`); the `Arguments` term is the
    // collection recursive match (the generator emits the collection-typed recursive term
    // directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child); the
    // `HasArgumentList` term is the fall-through plain-equality (a bool scalar, not an enum, no
    // `Any`). A type-only mismatch (not an `Attribute`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<Attribute*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && arguments_.DoMatch(o->arguments_, match)
            && hasArgumentList_ == o->hasArgumentList_;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the `HasArgumentList` scalar copied, the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern), the `Type` deep-cloned through the setter (which re-parents;
    // `AstType::Clone()` returns `AstType*`, the covariant override), and every `Arguments`
    // element deep-cloned through `Add` (which re-parents and re-indexes; `Expression::Clone()`
    // returns `Expression*`, which `Add(Expression*)` accepts directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor), so they are not copied (the ConditionalExpression/SimpleType/MemberType/ArraySpecifier
    // precedent for nodes without derived locations).
    Attribute* Clone() const override {
        auto* node = new Attribute();
        node->CloneAnnotationsFrom(*this);
        node->hasArgumentList_ = hasArgumentList_;
        if (type_ != nullptr)
            node->Type(type_->Clone());
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `hasArgumentList_` is the plain bool scalar (default false); `type_`
    // is null until the type is set (a required slot -- `CheckInvariant` asserts it is filled);
    // `arguments_` is the always-present collection member (empty until the first `Add`). No name
    // shadowing (no member is named `AstType` or `Expression`), so the field types are the plain
    // classes.
    bool hasArgumentList_ = false;
    AstType* type_ = nullptr;
    AstNodeCollectionT<Expression> arguments_;
};

// The `Attribute` kind -- the collection-element kind for every
// `[Slot("Attribute")] AstNodeCollection<Attribute>` (AttributeSection.Attributes, and the
// Attributes collections on the Statement/TypeMember/GeneralScope/ParameterDeclaration nodes
// that land later). A `CSharpSlotInfoT<Attribute>` (the element type is the concrete `Attribute`
// node).
//
// Defined HERE (in Attribute.hpp, after the `Attribute` class) rather than in Slots.hpp because
// `CSharpSlotInfoT<Attribute>` needs `Attribute` complete (the `dynamic_cast<const Attribute*>`
// is-a test in the ctor), and `Attribute` is the first concrete node that is BOTH a `Slots`
// element type AND a node with per-node slot statics (its `TypeSlot`/`ArgumentsSlot` reference
// `&Slots::Type`/`&Slots::Argument`, so Attribute.hpp includes Slots.hpp). Placing the kind in
// Slots.hpp would form a circular include: Slots.hpp would have to include Attribute.hpp (for the
// complete `Attribute`), but Attribute.hpp includes Slots.hpp (for `Slots::Type`/`Slots::Argument`),
// and with Slots.hpp's guard set the `Slots::Type`/`Slots::Argument` definitions would not be
// visible where Attribute.hpp's class body needs them. After the `Attribute` class both
// `CSharpSlotInfoT` (visible via the Slots.hpp include) and `Attribute` are complete, so the
// kind defines cleanly. The `inline` variable still has external linkage and one address across
// translation units (the C++17 `inline` guarantee), preserving the pointer-identity comparison
// `node.Slot.Kind == &Slots::Attribute` the slot system relies on. This is the resolution for
// every future concrete-node element kind whose node header includes Slots.hpp.
namespace Slots {
inline const CSharpSlotInfoT<Attribute> Attribute{"Attribute", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ATTRIBUTE_HPP
