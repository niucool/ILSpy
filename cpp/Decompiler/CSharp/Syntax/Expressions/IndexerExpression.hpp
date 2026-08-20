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
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `IndexerExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/IndexerExpression.cs (the generated
// `IndexerExpression.g.cs` + the hand-written partial -- the hand-written partial declares
// only the two slot properties, no ctors, no helpers). The next in-order Phase-5 piece per the
// D248 plan ("IndexerExpression -- a Target Expression slot + an Arguments
// AstNodeCollection<Expression> collection -- structurally identical to InvocationExpression
// but for the indexer expression '[' expression* ']' production, reusing the now-ported
// Slots::TargetExpression/Slots::Argument kinds"): `element_access ::= expression '['
// expression* ']'` (C# grammar 12.8.12.1) -- an element access is a `Target` `Expression` (the
// expression being indexed) followed by zero or more `Arguments` (the index `Expression` list
// inside the brackets).
//
// It is structurally IDENTICAL to `InvocationExpression` (D248): a single required `Expression`
// child slot (`Target`) at flattened index 0 and an `Arguments` `AstNodeCollection<Expression>`
// collection at flattened index 1, with NO string-name `[Slot]` (no backing `Identifier` token, so
// no `MatchString` term) and NO scalar. The only divergence from `InvocationExpression` is the
// grammatical role of the `Arguments` collection (an index list rather than a call argument list)
// and the visit-method name -- the two are disjoint concrete types so the pattern matcher's
// `other is IndexerExpression` gate distinguishes them. Its generated `DoMatch` has two terms in
// source declaration order: a non-nullable recursive `Target` term (dispatched through
// `MatchRequired` -- the D231 [class.access.derived] workaround, since a derived node may not call
// the protected `DoMatch` through a base `Expression*`) and a collection recursive `Arguments`
// term (`this.Arguments.DoMatch`).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitIndexerExpression(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitIndexerExpression`). The generated slot
// statics are `TargetSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.TargetExpression`,
// required -- the `Target` `Expression` is non-nullable) and `ArgumentsSlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`, collection). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the `Target` through the setter (which re-parents) and every
// `Arguments` element through `Add` (which re-parents and re-indexes), and copies the annotation
// channel.
//
// NO C++ name-shadowing crux (the `InvocationExpression` D248 differently-named-property
// precedent): the `Target()` accessor is a member function, but no member is named `Expression`
// (the `Target`/`Arguments` accessors do not collide with the `Expression` base type -- the
// `Target` accessor is `Target()`, not `Expression()`), and no class named `Target`/`Arguments`
// lives in the `Syntax` namespace. So no elaborated-type-specifier (`class Expression`) is needed
// anywhere, and the plain `Expression` resolves to the base class in every type position (the
// `InvocationExpression` precedent -- the `Target`-of-type-`Expression` case, distinct from
// `CastExpression` whose `Expression()` accessor shadows the base type).
//
// NO new `Slots` constant: both slot kinds are already ported (`Slots::TargetExpression` by
// `MemberReferenceExpression` D247, `Slots::Argument` by `Attribute` D240), so `Slots.hpp` is
// unchanged -- `IndexerExpression` is the third collection-slot `Expression` node to reuse two
// existing `Slots` kinds without adding any. `IndexerExpression.cs` declares NO hand-written
// ctors (only the two slot properties), so the port carries only the generated ctors (the empty
// ctor + the `(Expression)` required-prefix ctor). The collection ctors that take arguments
// (`IndexerExpression(Expression, IEnumerable<Expression>)` and the `params Expression[]` form)
// are DEFERRED: they use `AddRange`, which lands with the collection convenience mutators (the
// D222 deferral); an argument list is built via `Arguments().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_INDEXEREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_INDEXEREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class IndexerExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The third `Expression` with both a single `Expression` child
// slot and a collection slot, and the first element access (a target plus an index list).
class IndexerExpression final : public Expression {
public:
    ~IndexerExpression() override = default;

    // The generated empty ctor (the C# `public IndexerExpression()`). The `Arguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `Target` single slot at index 0 precedes it) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so an
    // element's flattened `ChildIndex` is exactly `1 + its local position`). The `Target`
    // defaults to null (no target) via its default member initializer; it is a required slot, so
    // a default-constructed node is only valid until the target is set (or until
    // `DoMatch`/`CheckInvariant` observe the missing slot).
    IndexerExpression() : arguments_(this, &ArgumentsSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public IndexerExpression(Expression
    // target)`): the required prefix runs through the last non-optional ctor param (`Target` is
    // required; `Arguments` is an optional collection). Sets `Target` in declaration order (the
    // generator's ctor body emits the assignments in `CtorParams` order, which is the source
    // declaration order). Delegates to the empty ctor so the collection member is initialized.
    // `explicit` (a single-argument ctor is a converting ctor by default), matching the
    // generator's public ctor but avoiding an implicit `Expression -> IndexerExpression`
    // conversion.
    explicit IndexerExpression(Expression* target) : IndexerExpression() {
        Target(target);
    }

    // ---- The `Target` slot (a single REQUIRED `Expression` child) ----------------------
    // The generated `[Slot("TargetExpression")] public partial Expression Target` -- a single
    // non-nullable `Expression` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. No name shadowing (the `Target()` accessor does not collide with the
    // `Expression` base type -- no member is named `Expression`), so the operand type is the
    // plain `Expression` (no elaborated specifier, unlike `CastExpression` whose `Expression()`
    // accessor shadows the base type).
    Expression* Target() const { return target_; }
    void Target(Expression* value) {
        SetChildNode(target_, value, 0);
    }

    // ---- The `Arguments` collection slot -------------------------------------------
    // The generated `public partial AstNodeCollection<Expression> Arguments` -- the collection of
    // index expressions (a `CSharpSlotInfoT<Expression>` slot at flattened index 1, the node's
    // only collection and last slot). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly (the
    // empty-until-first-Add element-list profile is preserved -- `list_` is empty until the first
    // `Add`).
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) --------------
    // The `TargetSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.TargetExpression`,
    // required -- the `Target` `Expression` is non-nullable); the `ArgumentsSlot` (a
    // `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`, collection). No name shadowing
    // (`Expression` resolves to the base class -- no member is named `Expression`).
    static inline const CSharpSlotInfoT<Expression> TargetSlot{"Target", false, &Slots::TargetExpression, false};
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Argument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitIndexerExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitIndexerExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitIndexerExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitIndexerExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A `Target` single slot at index 0 and an `Arguments` collection occupying the contiguous
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
            return target_;
        i--;
        int n = arguments_.Count();
        if (i < n)
            return arguments_.At(i);
        throw std::out_of_range("IndexerExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(target_, static_cast<Expression*>(value), index);
            return;
        }
        i--;
        int n = arguments_.Count();
        if (i < n) {
            arguments_.SetAt(i, static_cast<Expression*>(value));
            return;
        }
        throw std::out_of_range("IndexerExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TargetSlot;
        i--;
        int n = arguments_.Count();
        if (i < n)
            return &ArgumentsSlot;
        throw std::out_of_range("IndexerExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Argument)
            return &arguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is IndexerExpression o && this.Target.DoMatch(o.Target, match) &&
    // this.Arguments.DoMatch(o.Arguments, match)`. The terms are in `MembersToMatch` order, which
    // is the source declaration order (`Target`, `Arguments`). The `Target` term is a
    // non-nullable recursive child, so the generator emits a DIRECT `this.Target.DoMatch(o.Target,
    // match)` -- ported through `MatchRequired` (the D231 [class.access.derived] workaround, since
    // a derived node may not call the protected `DoMatch` through a base `Expression*`); the
    // `Arguments` term is the collection recursive match (the generator emits the
    // collection-typed recursive term directly, NOT `MatchOptional`, which the generator emits
    // only for a nullable non-collection child). A type-only mismatch (not an
    // `IndexerExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<IndexerExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(target_, o->target_, match)
            && arguments_.DoMatch(o->arguments_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Target`
    // deep-cloned through the setter (which re-parents; `Expression::Clone()` returns
    // `Expression*`, the covariant override, which `Target(Expression*)` accepts directly), and
    // every `Arguments` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Expression::Clone()` returns `Expression*`, which `Add(Expression*)` accepts directly).
    // No own location fields (`StartLocation`/`EndLocation` are the print-time base fields set by
    // the unported output visitor), so they are not copied (the ConditionalExpression/SimpleType/
    // MemberType/Attribute/InvocationExpression precedent for nodes without derived locations).
    IndexerExpression* Clone() const override {
        auto* node = new IndexerExpression();
        node->CloneAnnotationsFrom(*this);
        if (target_ != nullptr)
            node->Target(target_->Clone());
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `target_` is null until the target is set (a required slot --
    // `CheckInvariant` asserts it is filled); `arguments_` is the always-present collection
    // member (empty until the first `Add`). No name shadowing (no member is named `Expression`),
    // so the field types are the plain classes.
    Expression* target_ = nullptr;
    AstNodeCollectionT<Expression> arguments_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_INDEXEREXPRESSION_HPP
