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

// Port of the `TryCatchStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/TryCatchStatement.cs (the generated
// `TryCatchStatement.g.cs` + the hand-written partial, which declares only the two const strings
// and the three slot properties, no ctors, no helpers). Part of the try/catch family -- the next
// in-order Phase-5 piece per the D268 plan ("TryCatchStatement, LocalFunctionDeclarationStatement,
// VariableDeclarationStatement ...").
// `try_statement ::= 'try' block catch_clause* ( 'finally' block )?` (C# grammar 13.11): a
// `Statement` with a REQUIRED `TryBlock` (the `try` body), a `CatchClauses` collection (zero or
// more `CatchClause` children), and an optional `FinallyBlock` (the `finally` body, absent for a
// `try`/`catch` without `finally`).
//
// The hand-written partial declares only the `TryKeyword`/`FinallyKeyword` const strings and the
// three slot properties. It is a sealed `Statement` (the `[DecompilerAstNode]` default -- no
// `hasPatternPlaceholder` argument, so the generated `PatternPlaceholder` is NOT emitted and the
// node is `final`).
//
// The three slots in source declaration order: a REQUIRED (non-nullable) `BlockStatement`
// `TryBlock` `[Slot("TryBlock")]` single slot at flattened index 0 (the `UnaryOperatorExpression`
// D231 required-single-slot shape on a `BlockStatement` child -- the NEW `Slots::TryBlock` kind,
// cycle-broken into `BlockStatement.hpp`); a `CatchClauses AstNodeCollection<CatchClause>`
// collection `[Slot("CatchClause")]` at slot 1 (the NEW cycle-broken `Slots::CatchClause` kind
// in `CatchClause.hpp`); and a NULLABLE `BlockStatement?` `FinallyBlock` `[Slot("FinallyBlock")]`
// single slot at flattened index `1 + Count` (the NEW `Slots::FinallyBlock` kind, cycle-broken
// into `BlockStatement.hpp`; the `ReturnStatement` D255 nullable-single-slot shape on a
// `BlockStatement` child, but the single slot FOLLOWS a collection).
//
// This is the `ObjectCreateExpression` D251 shape (a single REQUIRED child + a NON-INCREMENTAL
// collection + a NULLABLE trailing single) applied to the `Statement` hierarchy with a
// `BlockStatement`/`CatchClause`/`BlockStatement` child set. The generator's `supportsIncremental`
// flag is `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 1` but
// the `CatchClauses` collection is at `slotIndex == 1`, NOT the last slot (`slots.Count - 1 == 2`
// -- the `FinallyBlock` single slot follows), so `supportsIncremental` is FALSE: every
// `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's indices for a lazy rebuild
// (`EnsureChildIndices`), and `IndexOf` falls back to a linear identity search. The `TryBlock`
// single slot PRECEDES the collection, so its setter uses the const-index
// `SetChildNode(ref field, value, 0)`. The `FinallyBlock` single slot FOLLOWS a collection, so the
// generator's `constIndex = !slots.Take(slotIndex).Any(IsCollection)` is `false`, and the
// `FinallyBlock` property setter uses the index-less `SetChildNode(ref field, value)` (which
// invalidates on a set/clear, since the flattened index is dynamic -- the `CatchClauses` count can
// change); the `SetChild` override still passes the known flattened `index` to the const-index
// `SetChildNode(ref field, value, index)`. `GetChildCount` is `2 + Count` (the two single slots
// plus the collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
// subtracting each one's width from a running index (the generator's
// `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single step, a
// collection step, then a single step).
//
// Its generated `DoMatch` has THREE terms in `MembersToMatch` (source declaration) order: a
// non-nullable recursive `TryBlock` term (dispatched through `MatchRequired` -- the D231
// `[class.access.derived]` workaround), a collection recursive `CatchClauses` term
// (`this.CatchClauses.DoMatch` -- the generator emits the collection-typed recursive term directly,
// NOT `MatchOptional`, which it emits only for a nullable NON-collection child), and a nullable
// recursive `FinallyBlock` term (dispatched through `MatchOptional` -- the
// `BinaryOperatorExpression` D229 nullable-child path, the nullable single child that FOLLOWS a
// collection). This is the `ObjectCreateExpression` D251 three-term `MatchRequired` +
// collection-`DoMatch` + `MatchOptional` shape applied to a `Statement` base.
//
// NO C++ name-shadowing crux (the `ObjectCreateExpression` D251 / `Attribute` D240
// differently-named-property precedent): the `TryBlock()`/`CatchClauses()`/`FinallyBlock()`
// accessors are member functions, but no class named `TryBlock`/`CatchClauses`/`FinallyBlock`
// lives in the `Syntax` namespace (there is `BlockStatement`, not `TryBlock`/`FinallyBlock`), and
// no member is named `BlockStatement`/`CatchClause`. So no elaborated-type-specifier is needed
// anywhere, and the plain `BlockStatement`/`CatchClause` resolve to the classes in every type
// position (the `MemberReferenceExpression`/`InvocationExpression` differently-named-property
// precedent). The plural `CatchClauses()` accessor does NOT collide with the `CatchClause` class
// (a member named `CatchClauses` is not the name `CatchClause` -- the plural).
//
// `TryCatchStatement.cs` declares NO hand-written ctors (only the two const strings and the three
// slot properties), so the port carries only the generated ctors. The generated collection ctors
// (the `(BlockStatement, IEnumerable<CatchClause>)` form, the `params CatchClause[]` overload, and
// the `(BlockStatement, IEnumerable<CatchClause>, BlockStatement?)` all-params ctor) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so they
// are DEFERRED; the empty + the `(BlockStatement)` required-prefix ctors cover the construction API
// (`TryBlock` is the only required ctor param -- `CatchClauses` is an optional collection and
// `FinallyBlock` is an optional single). A catch-clause list is built via `CatchClauses().Add(...)`
// and the `finally` block is set via `FinallyBlock(...)` until `AddRange` lands. The
// `(BlockStatement)` ctor is `explicit` (a single-argument ctor is a converting ctor by default),
// matching the generator's public ctor but avoiding an implicit `BlockStatement` ->
// `TryCatchStatement` conversion.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_TRYCATCHSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_TRYCATCHSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class TryCatchStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The `ObjectCreateExpression` D251 shape (a single REQUIRED child + a
// NON-INCREMENTAL collection + a NULLABLE trailing single) applied to the `Statement` hierarchy.
class TryCatchStatement final : public Statement {
public:
    ~TryCatchStatement() override = default;

    // The generated empty ctor (the C# `public TryCatchStatement()`). The `CatchClauses`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `TryBlock` single slot at slot 0 precedes it) and
    // `supportsIncremental = false` (the collection is the node's only collection but NOT its
    // last slot -- the `FinallyBlock` single slot follows at slot 2 -- so an element's flattened
    // `ChildIndex` is NOT a simple `baseIndex + local position`; every `Add`/`Insert`/`Remove`/
    // single-slot-set invalidates the parent's indices for a lazy rebuild). `TryBlock` and
    // `FinallyBlock` default to null via their default member initializers (no try body, no
    // finally).
    TryCatchStatement() : catchClauses_(this, &CatchClausesSlot, 1, false) {}

    // The generated required-prefix ctor (the C# `public TryCatchStatement(BlockStatement
    // tryBlock)`) -- the only required ctor param is `TryBlock` (`CatchClauses` is an optional
    // collection and `FinallyBlock` is an optional single). Sets `TryBlock` in declaration order.
    // Delegates to the empty ctor so the collection member is initialized. `explicit` (a
    // single-argument ctor is a converting ctor by default).
    explicit TryCatchStatement(BlockStatement* tryBlock) : TryCatchStatement() {
        TryBlock(tryBlock);
    }

    // ---- The const keyword tokens (the output-visitor token literals) ----------------
    // The C# `public const string TryKeyword = "try"` / `FinallyKeyword = "finally"`. Part of the
    // node's public API (the output visitor reads them); port as `static constexpr const char*`
    // (the `IfElseStatement.IfKeyword` D258 / `SwitchStatement.SwitchKeyword` D268 precedent). The
    // generator excludes const string fields from `MembersToMatch` (it iterates only instance
    // `IPropertySymbol`s), so they never appear in the generated `DoMatch`.
    static constexpr const char* TryKeyword = "try";
    static constexpr const char* FinallyKeyword = "finally";

    // ---- The `TryBlock` slot (the required try body block) -----------------------------
    // The C# `[Slot("TryBlock")] public partial BlockStatement TryBlock` -- a single, REQUIRED
    // (non-nullable) `BlockStatement` child at flattened index 0 (the `try` body). The generator
    // emits the const-index `SetChildNode(ref field, value, 0)` setter (no collection precedes
    // it), so the index is assigned directly and the parent's indices stay valid by
    // construction. No name shadowing (the `TryBlock()` accessor does not collide with the
    // `BlockStatement` class -- no class named `TryBlock` lives in the `Syntax` namespace), so
    // the operand type is the plain `BlockStatement` (no elaborated specifier).
    BlockStatement* TryBlock() const { return tryBlock_; }
    void TryBlock(BlockStatement* value) {
        SetChildNode(tryBlock_, value, 0);
    }

    // ---- The `CatchClauses` collection slot -------------------------------------------
    // The generated `[Slot("CatchClause")] public partial AstNodeCollection<CatchClause>
    // CatchClauses` -- the collection of `CatchClause` children (a `CSharpSlotInfoT<CatchClause>`
    // slot at slot index 1). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly
    // (the empty-until-first-Add element-list profile is preserved). `supportsIncremental` is
    // `false` (the collection is not the node's last slot -- the `FinallyBlock` single slot
    // follows), so `Add` invalidates the parent's indices.
    AstNodeCollectionT<CatchClause>& CatchClauses() { return catchClauses_; }
    const AstNodeCollectionT<CatchClause>& CatchClauses() const { return catchClauses_; }

    // ---- The `FinallyBlock` slot (the optional finally body block) --------------------
    // The C# `[Slot("FinallyBlock")] public partial BlockStatement? FinallyBlock` -- a single,
    // NULLABLE `BlockStatement` child at slot index 2 (the `finally` body, absent for a
    // `try`/`catch` without `finally`). A COLLECTION precedes it (`CatchClauses` at slot 1), so
    // the generator's `constIndex = !slots.Take(2).Any(IsCollection)` is `false`, and the setter
    // uses the index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear,
    // since the flattened index is dynamic -- the `CatchClauses` count can change). This is the
    // `ObjectCreateExpression.Initializer` D251 pattern (a nullable single child after a
    // collection). No name shadowing (no class named `FinallyBlock` lives in the `Syntax`
    // namespace), so the operand type is the plain `BlockStatement` (no elaborated specifier).
    BlockStatement* FinallyBlock() const { return finallyBlock_; }
    void FinallyBlock(BlockStatement* value) {
        SetChildNode(finallyBlock_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TryBlockSlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.TryBlock`,
    // required -- the `TryBlock` `BlockStatement` is non-nullable); the `CatchClausesSlot` (a
    // `CSharpSlotInfoT<CatchClause>` pointing at `Slots.CatchClause`, collection); the
    // `FinallyBlockSlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.FinallyBlock`,
    // optional -- the `FinallyBlock` is nullable). `Slots::TryBlock`/`Slots::FinallyBlock` are
    // NEW and cycle-broken into `BlockStatement.hpp` (after the `Slots::Body` kind); `Slots::CatchClause`
    // is NEW and cycle-broken into `CatchClause.hpp`. No name shadowing (no member is named
    // `BlockStatement`/`CatchClause`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<BlockStatement> TryBlockSlot{"TryBlock", false, &Slots::TryBlock, false};
    static inline const CSharpSlotInfoT<CatchClause> CatchClausesSlot{"CatchClauses", true, &Slots::CatchClause, true};
    static inline const CSharpSlotInfoT<BlockStatement> FinallyBlockSlot{"FinallyBlock", false, &Slots::FinallyBlock, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitTryCatchStatement` (`TryCatchStatement` does not end in "AstType", so
    // the generator's visit-method-name default yields `VisitTryCatchStatement`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitTryCatchStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitTryCatchStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitTryCatchStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Three slots in declaration order: a `TryBlock` single slot at index 0, a `CatchClauses`
    // collection occupying the contiguous range `[1, 1 + Count)`, and a `FinallyBlock` single
    // slot at index `1 + Count` (the trailing single slot after the collection). `GetChildCount`
    // is `2 + Count` (the two single slots plus the collection's current length);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
    // running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a single step, a collection step, then a single
    // step). `GetCollectionByKind` returns the `CatchClauses` collection for the `CatchClause`
    // kind (the node's only collection). This is the `ObjectCreateExpression` D251 single ->
    // collection -> single dispatch-walk shape.

    int GetChildCount() const override { return 2 + catchClauses_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return tryBlock_;
        i--;
        {
            int n = catchClauses_.Count();
            if (i < n)
                return catchClauses_.At(i);
            i -= n;
        }
        if (i == 0)
            return finallyBlock_;
        throw std::out_of_range("TryCatchStatement::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(tryBlock_, static_cast<BlockStatement*>(value), index);
            return;
        }
        i--;
        {
            int n = catchClauses_.Count();
            if (i < n) {
                catchClauses_.SetAt(i, static_cast<CatchClause*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(finallyBlock_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("TryCatchStatement::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TryBlockSlot;
        i--;
        {
            int n = catchClauses_.Count();
            if (i < n)
                return &CatchClausesSlot;
            i -= n;
        }
        if (i == 0)
            return &FinallyBlockSlot;
        throw std::out_of_range("TryCatchStatement::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::CatchClause)
            return &catchClauses_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is TryCatchStatement o && this.TryBlock.DoMatch(o.TryBlock, match) &&
    // this.CatchClauses.DoMatch(o.CatchClauses, match) && MatchOptional(this.FinallyBlock,
    // o.FinallyBlock, match)`. The three terms are in `MembersToMatch` order, which is the source
    // declaration order (`TryBlock`, `CatchClauses`, `FinallyBlock`). The `TryBlock` term is a
    // non-nullable recursive child, so the generator emits a DIRECT `this.TryBlock.DoMatch` --
    // ported through `MatchRequired` (the D231 `[class.access.derived]` workaround, since a
    // derived node may not call the protected `DoMatch` through a base `BlockStatement*`); the
    // `CatchClauses` term is the collection recursive match (the generator emits the
    // collection-typed recursive term directly, NOT `MatchOptional`, which it emits only for a
    // nullable non-collection child); the `FinallyBlock` term is a nullable non-collection
    // recursive child, so the generator emits `MatchOptional(this.FinallyBlock, o.FinallyBlock,
    // match)` (the `BinaryOperatorExpression` D229 nullable-child path, the nullable single child
    // that FOLLOWS a collection). A type-only mismatch (not a `TryCatchStatement`) rejects early.
    // The `TryBlock` `MatchRequired` is the first term, so a half-constructed pattern (a null
    // `TryBlock`) rejects without crashing (the `MatchRequired` null-pattern guard). The
    // `FinallyBlock` `MatchOptional` returns true when BOTH are absent (the common `try`/`catch`
    // shape with no `finally`), and delegates to the pattern's `DoMatch` when the pattern carries
    // a `FinallyBlock`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<TryCatchStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(tryBlock_, o->tryBlock_, match)
            && catchClauses_.DoMatch(o->catchClauses_, match)
            && MatchOptional(finallyBlock_, o->finallyBlock_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `TryBlock`
    // deep-cloned through the setter (which re-parents; `BlockStatement::Clone()` returns
    // `BlockStatement*`, the covariant override), every `CatchClauses` element deep-cloned through
    // `Add` (which re-parents and re-indexes; `CatchClause::Clone()` returns `CatchClause*`, which
    // `Add(CatchClause*)` accepts directly), and the `FinallyBlock` deep-cloned through the
    // setter when present (which re-parents; `BlockStatement::Clone()` returns `BlockStatement*`).
    // The deep-copy order follows the slot declaration order (`TryBlock`, `CatchClauses`,
    // `FinallyBlock`) matching the generated `CloneChildrenInto`; with the `CatchClauses`
    // collection non-incremental every mutation invalidates, so the order does not affect the
    // final rebuilt state. No own location fields (`StartLocation`/`EndLocation` are the
    // print-time base fields set by the unported output visitor -- `TryCatchStatement` does not
    // derive `EndLocation`), so they are not copied (the `ObjectCreateExpression` D251 /
    // `IfElseStatement` D258 no-location-copy precedent). The covariant return is
    // `TryCatchStatement*` (through `Statement*`, the `Statement::Clone` pure-virtual). No
    // elaborated specifiers (no member is named `BlockStatement`/`CatchClause`); the child
    // `Clone()` calls return the typed pointers the setters/`Add` accept directly.
    TryCatchStatement* Clone() const override {
        auto* node = new TryCatchStatement();
        node->CloneAnnotationsFrom(*this);
        if (tryBlock_ != nullptr)
            node->TryBlock(tryBlock_->Clone());
        for (int i = 0; i < catchClauses_.Count(); i++)
            node->catchClauses_.Add(catchClauses_.At(i)->Clone());
        if (finallyBlock_ != nullptr)
            node->FinallyBlock(finallyBlock_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `tryBlock_` is null until the try body is set (a required slot --
    // `CheckInvariant` asserts it is filled); `finallyBlock_` is null until the finally body is
    // set (an OPTIONAL slot -- `CheckInvariant` passes with it null); `catchClauses_` is the
    // always-present collection member (empty until the first `Add`, non-incremental). No name
    // shadowing (no member is named `BlockStatement`/`CatchClause`), so the field types are the
    // plain classes.
    BlockStatement* tryBlock_ = nullptr;
    AstNodeCollectionT<CatchClause> catchClauses_;
    BlockStatement* finallyBlock_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_TRYCATCHSTATEMENT_HPP
