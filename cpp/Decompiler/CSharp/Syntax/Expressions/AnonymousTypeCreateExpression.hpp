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

// Port of the `AnonymousTypeCreateExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.cs (the
// generated `AnonymousTypeCreateExpression.g.cs` + the hand-written partial). The next
// in-order Phase-5 piece: a remaining Expression node whose dependencies are all ported.
// `anonymous_object_creation_expression ::= 'new' '{' expression* '}'` (C# grammar 12.8.17.4):
// an `AnonymousTypeCreateExpression` is a sealed `Expression` whose sole child slot is the
// `Initializers` collection of `Expression` (the comma-separated member-initializer list
// inside the braces).
//
// It is the `ArrayInitializerExpression` D250 / `TupleExpression` D296 collection-only shape
// applied to the anonymous-object-creation production: the `Initializers` collection (an
// `AstNodeCollection<Expression>`) is the node's only slot and its last slot, so
// `supportsIncremental` is true (an element's flattened `ChildIndex` is exactly its local
// position) and `GetChildCount` is the collection's current length (an empty node reports
// `GetChildCount` 0). Like `TupleExpression` it carries a plain `[DecompilerAstNode]` (no arg),
// so `hasPatternPlaceholder` defaults to false and the C# `sealed` ports to C++ `final`.
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Initializers.DoMatch(o.Initializers, match)` (the generator emits the
// collection-typed recursive term directly -- NOT `MatchOptional`, which it emits only for a
// nullable NON-collection child). Per PORT_PLAN.md section 5.2 / decision D1 the concrete node
// is hand-translated from the generated output rather than regenerated. The generated
// `AcceptVisitor` calls `visitor.VisitAnonymousTypeCreateExpression(this)` (the class name
// does not end in "AstType", so the generator's visit-method-name default yields
// `VisitAnonymousTypeCreateExpression`). The generated slot static is `InitializersSlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, collection).
//
// NO C++ name-shadowing crux: the `Initializers()` accessor is a member function, but no class
// named `Initializers` lives in the `Syntax` namespace, and no member is named `Expression`
// (the `Initializers` accessor does not collide with the `Expression` base type -- it is
// `Initializers()`, not `Expression()`), so no elaborated-type-specifier (`class Expression`)
// is needed anywhere, and the plain `Expression` resolves to the base class in every type
// position (the `TupleExpression`/`ArrayInitializerExpression`/`BlockStatement`
// differently-named-property precedent).
//
// NO new `Slots` constant: the `[Slot("Expression")]` argument names the slot kind "Expression",
// already ported by `UnaryOperatorExpression` (D231) as a single-`Expression` operand position
// and already reused as a collection kind by `ArrayInitializerExpression` (D250),
// `BlockStatement` (D256), and `TupleExpression` (D296). The kind carries identity + the
// element type only; the per-position `IsCollection`/`IsOptional` flags live on the per-node
// slot.
//
// `AnonymousTypeCreateExpression.cs` declares NO hand-written ctors (only the `Initializers`
// slot property and the `NewKeyword` const), so the port carries only the generated ctors. The
// generated collection ctors (`AnonymousTypeCreateExpression(IEnumerable<Expression>)` and the
// `params Expression[]` form) use `AddRange`, which lands with the collection convenience
// mutators (the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor,
// and an initializer list is built via `Initializers().Add(...)` until `AddRange` lands. The
// `NewKeyword` const string ("new") is part of the node's public API (the output visitor emits
// it), ported as a `static constexpr const char*` (a static field, not instance state, so it is
// NOT in `MembersToMatch`/`DoMatch` -- the generator's scan adds only settable instance
// `IPropertySymbol`s, excluding `const` string fields entirely -- the
// `CheckedExpression.CheckedKeyword` D234 / `TupleExpression`-no-const precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ANONYMOUSTYPECREATEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ANONYMOUSTYPECREATEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class AnonymousTypeCreateExpression : Expression` -- the
// anonymous-object-creation node. `final` (the C# is `sealed`; `[DecompilerAstNode]` with no
// arg means `hasPatternPlaceholder` defaults to false, so no `PatternPlaceholder` derives from
// it). The collection-only shape: the only child slot is the `Initializers` collection.
class AnonymousTypeCreateExpression final : public Expression {
public:
    // The `new` keyword the output visitor emits before the initializer list. A public-API
    // const string (the `ArrayCreateExpression.NewKeyword`/`ObjectCreateExpression.NewKeyword`
    // family), ported as a `static constexpr const char*` -- a static field, not instance state,
    // so it is NOT in `MembersToMatch`/`DoMatch` (the generator's scan adds only settable
    // instance `IPropertySymbol`s, excluding `const` string fields).
    static constexpr const char* NewKeyword = "new";

    ~AnonymousTypeCreateExpression() override = default;

    // The generated empty ctor (the C# `public AnonymousTypeCreateExpression()`). The
    // `Initializers` collection is a member (the D222 always-present-stack-member design),
    // initialized here with `baseIndex = 0` (it is the node's only slot, so the first
    // element's flattened `ChildIndex` is 0) and `supportsIncremental = true` (it is the node's
    // only collection and last slot, so an element's flattened `ChildIndex` is exactly its
    // local position). The collection starts empty (no initializers); the node has no required
    // single slots, so a default-constructed node is a valid empty anonymous-object-creation
    // (`new { }`, `CheckInvariant` passes).
    AnonymousTypeCreateExpression() : initializers_(this, &InitializersSlot, 0, true) {}

    // ---- The `Initializers` collection slot -------------------------------------
    // The generated `[Slot("Expression")] public partial AstNodeCollection<Expression>
    // Initializers` -- the collection of member-initializer expressions (a
    // `CSharpSlotInfoT<Expression>` slot at flattened index 0, the node's only collection and
    // last slot). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly (the
    // empty-until-first-Add element-list profile is preserved -- `list_` is empty until the
    // first `Add`).
    AstNodeCollectionT<Expression>& Initializers() { return initializers_; }
    const AstNodeCollectionT<Expression>& Initializers() const { return initializers_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag
    // is true (the slot is a collection); the `IsOptional` flag is true (the generator sets it
    // true for every collection slot). The kind is `Slots::Expression` (the shared "Expression"
    // slot name, ported by `UnaryOperatorExpression` as a single-`Expression` operand
    // position, already reused as a collection kind by `ArrayInitializerExpression`,
    // `BlockStatement`, and `TupleExpression`). No name shadowing (no member is named
    // `Expression`), so the element type is the plain `Expression`.
    static inline const CSharpSlotInfoT<Expression> InitializersSlot{"Initializers", true, &Slots::Expression, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitAnonymousTypeCreateExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitAnonymousTypeCreateExpression(this);
    }

    // ---- Slot storage (the generated overrides) ----------------------------------------
    // A single `Initializers` collection occupying the contiguous range [0, Count).
    // `GetChildCount` is the collection's current length (an empty node reports 0 -- no
    // single-slot count term); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single
    // collection slot (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape with one collection step and no single step).
    // `GetCollectionByKind` returns the `Initializers` collection for the `Expression` kind.

    int GetChildCount() const override { return initializers_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = initializers_.Count();
        if (i < n)
            return initializers_.At(i);
        throw std::out_of_range("AnonymousTypeCreateExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = initializers_.Count();
        if (i < n) {
            initializers_.SetAt(i, static_cast<Expression*>(value));
            return;
        }
        throw std::out_of_range("AnonymousTypeCreateExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = initializers_.Count();
        if (i < n)
            return &InitializersSlot;
        throw std::out_of_range("AnonymousTypeCreateExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Expression)
            return &initializers_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is AnonymousTypeCreateExpression o && this.Initializers.DoMatch(o.Initializers, match)`.
    // The single term is the collection recursive match (the generator emits the
    // collection-typed recursive term directly, NOT `MatchOptional`, which it emits only for a
    // nullable NON-collection child). A type-only mismatch (not an
    // `AnonymousTypeCreateExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<AnonymousTypeCreateExpression*>(other);
        if (o == nullptr)
            return false;
        return initializers_.DoMatch(o->initializers_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Initializers` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Expression::Clone()` returns `Expression*`, the covariant override, which
    // `Add(Expression*)` accepts directly). No own location fields (`StartLocation`/
    // `EndLocation` are the print-time base fields set by the unported output visitor), so
    // they are not copied (the `TupleExpression`/`ArrayInitializerExpression`/
    // `ConditionalExpression`/`SimpleType` precedent for nodes without derived locations). The
    // covariant return is `AnonymousTypeCreateExpression*` (through `Expression*`, the
    // `Expression::Clone` pure-virtual).
    AnonymousTypeCreateExpression* Clone() const override {
        auto* node = new AnonymousTypeCreateExpression();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < initializers_.Count(); i++)
            node->initializers_.Add(initializers_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `initializers_` is the always-present collection member (empty until
    // the first `Add`). No name shadowing (no member is named `Expression`), so the field type
    // is the plain class.
    AstNodeCollectionT<Expression> initializers_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ANONYMOUSTYPECREATEEXPRESSION_HPP
