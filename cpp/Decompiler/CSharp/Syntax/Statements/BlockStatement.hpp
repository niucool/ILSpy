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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Port of the `BlockStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/BlockStatement.cs (the generated
// `BlockStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D255 plan ("the collection-bearing statements: BlockStatement the statement collection").
// `block ::= '{' statement* '}'` (C# grammar 13.3.1): a `BlockStatement` is a `Statement` whose
// sole child slot is the `Statements` collection of `Statement` (the statement list inside the
// braces).
//
// It is the collection-only shape (the `ArrayInitializerExpression` D250 precedent: the only
// node with a COLLECTION slot and NO single child slot) applied to the Statement hierarchy.
// The `Statements` collection (an `AstNodeCollection<Statement>`) is the node's only slot and
// its last slot, so `supportsIncremental` is true (an element's flattened `ChildIndex` is
// exactly its local position) and `GetChildCount` is the collection's current length (an empty
// block reports `GetChildCount` 0 -- the collection-only shape, like `ArrayInitializerExpression`).
// It is a NON-sealed concrete node: the C# declares `public partial class BlockStatement`
// (no `sealed`) because `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a sealed nested
// `PatternPlaceholder : BlockStatement` that derives from it (the `ArrayInitializerExpression`
// D250 precedent). The port therefore does NOT use `final` (faithful to the C# not being
// sealed), and the pattern placeholder is DEFERRED (the D226 deferral: `hasPatternPlaceholder`
// emits an `implicit operator BlockStatement(Pattern)` plus a sealed `PatternPlaceholder` nested
// class implementing `INode`/`IPatternPlaceholder`; it lands with the concrete pattern nodes and
// `VisitPatternPlaceholder` on `IAstVisitor`). `Clone` is a per-concrete-node override
// regardless (no `MemberwiseClone` in C++).
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Statements.DoMatch(o.Statements, match)` (the generator emits the collection-typed
// recursive term directly -- NOT `MatchOptional`, which it emits only for a nullable
// NON-collection child). Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is
// hand-translated from the generated output rather than regenerated. The generated
// `AcceptVisitor` calls `visitor.VisitBlockStatement(this)` (the class name does not end in
// "AstType", so the generator's visit-method-name default yields `VisitBlockStatement`). The
// generated slot static is `StatementsSlot` (a `CSharpSlotInfoT<Statement>` pointing at
// `Slots.Statement`, collection).
//
// NO C++ name-shadowing crux: the `Statements()` accessor is a member function, but no class
// named `Statements` lives in the `Syntax` namespace, and no member is named `Statement`
// (the `Statements` accessor does not collide with the `Statement` base type -- it is
// `Statements()`, not `Statement()`), so no elaborated-type-specifier (`class Statement`) is
// needed anywhere, and the plain `Statement` resolves to the base class in every type
// position (the `ArrayInitializerExpression` D250 differently-named-property precedent applied
// to the Statement hierarchy).
//
// NEW `Slots` constant: the `[Slot("Statement")]` argument names the slot kind "Statement",
// not yet ported. `Slots.Statement` is a `CSharpSlotInfoT<Statement>` (the element type is the
// `Statement` abstract base). The `Statement.hpp` header does NOT include `Slots.hpp` (the
// `Statement` abstract base has no per-node slot statics), so the kind lives HERE-in-`Slots.hpp`
// (no include cycle) with a `Statements/Statement.hpp` include -- the `Slots.ArraySpecifier`
// D242 precedent (an element-type node header that does not include `Slots.hpp` lives in
// `Slots.hpp`). The kind name `Statement` collides with the `Statement` CLASS in the parent
// `Syntax` namespace (the `Expression`/`Identifier` D231 collision pattern); the template
// argument in this definition resolves to the class (the constant being declared is not yet
// in scope at the point its type is parsed), and a LATER `Slots` entry wanting the `Statement`
// class as its element type must qualify it (`::ILSpy::Decompiler::CSharp::Syntax::Statement`).
//
// `BlockStatement.cs` declares NO hand-written generated ctors (the generated ctors use
// `AddRange`, the D222 collection-convenience-mutator deferral, so they are DEFERRED); the empty
// ctor is the only portable ctor, and a statement list is built via `Statements().Add(...)`
// until `AddRange` lands. The hand-written `Add(Statement)`/`Add(Expression)` convenience
// overloads (the C# forwards to `AddChild(statement, Slots.Statement)` / wraps the expression
// in a `new ExpressionStatement(expression)`) are DEFERRED: they are thin wrappers consumed by
// the unported output-visitor/resolver stage (the D235 hand-written-helper deferral), and the
// `Statements().Add(...)` collection API (already ported, D222) is the equivalent for building a
// block; the `Add(Expression)` overload additionally requires the `ExpressionStatement` ctor
// (already ported, D255) but is still a behavior-convenience, not the generated slot/DoMatch/
// Clone surface this slice ports. The `IEnumerable<Statement>` enumeration (the C# explicit
// interface implementation forwarding to `Statements.GetEnumerator()`) is DEFERRED (the C#
// lazy-sequence surface; the ported `Statements()` collection exposes `begin()`/`end()` range-
// for and `Count`/`At`/`operator[]` instead -- the D222 enumeration-surface deferral).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_BLOCKSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_BLOCKSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public partial class BlockStatement : Statement, IEnumerable<Statement>` (NOT
// `sealed` -- the generated `PatternPlaceholder` derives from it; the pattern placeholder is
// deferred, but the class stays non-`final` to match the C# and to not block the placeholder
// landing). The second ported concrete node that is not sealed (after `ArrayInitializerExpression`
// D250), and the first with a collection slot and no single child slot in the Statement hierarchy.
class BlockStatement : public Statement {
public:
    ~BlockStatement() override = default;

    // The generated empty ctor (the C# `public BlockStatement()`). The `Statements` collection is
    // a member (the D222 always-present-stack-member design), initialized here with `baseIndex =
    // 0` (it is the node's only slot, so the first element's flattened `ChildIndex` is 0) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so an
    // element's flattened `ChildIndex` is exactly its local position). The collection starts
    // empty (no statements); the node has no required single slots, so a default-constructed
    // node is a valid empty block (`CheckInvariant` passes -- the collection-only shape, like
    // `ArrayInitializerExpression`).
    BlockStatement() : statements_(this, &StatementsSlot, 0, true) {}

    // ---- The `Statements` collection slot ---------------------------------------
    // The generated `[Slot("Statement")] public partial AstNodeCollection<Statement>
    // Statements` -- the collection of statements (a `CSharpSlotInfoT<Statement>` slot at
    // flattened index 0, the node's only collection and last slot). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly (the empty-until-first-Add element-list profile is
    // preserved -- `list_` is empty until the first `Add`).
    AstNodeCollectionT<Statement>& Statements() { return statements_; }
    const AstNodeCollectionT<Statement>& Statements() const { return statements_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag is
    // true (the slot is a collection); the `IsOptional` flag is true (the generator sets it true
    // for every collection slot). The kind is `Slots::Statement` (the new shared "Statement" slot
    // name, added to `Slots.hpp` with this node). No name shadowing (no member is named
    // `Statement`), so the element type is the plain `Statement` (the base class, in scope via
    // the `Statement.hpp` include).
    static inline const CSharpSlotInfoT<Statement> StatementsSlot{"Statements", true, &Slots::Statement, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitBlockStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitBlockStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `Statements` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty block reports 0 -- no single-slot count term,
    // the collection-only shape); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single
    // collection slot (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWith-
    // Collections` shape with one collection step and no single step). `GetCollectionByKind`
    // returns the `Statements` collection for the `Statement` kind.

    int GetChildCount() const override { return statements_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = statements_.Count();
        if (i < n)
            return statements_.At(i);
        throw std::out_of_range("BlockStatement::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = statements_.Count();
        if (i < n) {
            statements_.SetAt(i, static_cast<Statement*>(value));
            return;
        }
        throw std::out_of_range("BlockStatement::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = statements_.Count();
        if (i < n)
            return &StatementsSlot;
        throw std::out_of_range("BlockStatement::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Statement)
            return &statements_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is BlockStatement o && this.Statements.DoMatch(o.Statements, match)`.
    // The single term is the collection recursive match (the generator emits the
    // collection-typed recursive term directly, NOT `MatchOptional`, which it emits only for a
    // nullable NON-collection child). A type-only mismatch (not a `BlockStatement`) rejects
    // early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<BlockStatement*>(other);
        if (o == nullptr)
            return false;
        return statements_.DoMatch(o->statements_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Statements` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Statement::Clone()` returns `Statement*`, the covariant override, which `Add(Statement*)`
    // accepts directly). No own location fields (`StartLocation`/`EndLocation` are the
    // print-time base fields set by the unported output visitor), so they are not copied (the
    // collection-only `ArrayInitializerExpression` D250 no-location-copy precedent). The
    // covariant return is `BlockStatement*` (through `Statement*`, the `Statement::Clone`
    // pure-virtual).
    BlockStatement* Clone() const override {
        auto* node = new BlockStatement();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < statements_.Count(); i++)
            node->statements_.Add(statements_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `statements_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no member is named `Statement`), so the field type is the
    // plain class (the base `Statement`, in scope via the include).
    AstNodeCollectionT<Statement> statements_;
};

// The `Body` kind -- a single `BlockStatement` child (the body block of a keyword statement
// that wraps a block: `CheckedStatement.Body`, `UncheckedStatement.Body`, `UnsafeStatement.Body`,
// and the later `FixedStatement`/`LockStatement`/`UsingStatement`/`UnsafeStatement`-style nodes).
// A `CSharpSlotInfoT<BlockStatement>` (the element type is the concrete `BlockStatement` node).
//
// Defined HERE (in BlockStatement.hpp, after the `BlockStatement` class) rather than in
// Slots.hpp because `CSharpSlotInfoT<BlockStatement>` needs `BlockStatement` complete (the
// `dynamic_cast<const BlockStatement*>` is-a test in the ctor), and `BlockStatement` is a
// concrete node that INCLUDES `Slots.hpp` (its `StatementsSlot` references `&Slots::Statement`).
// Placing the kind in `Slots.hpp` would form a circular include (the `Slots::Attribute`/
// `Slots::AttributeSection` cycle-breaking precedent): `Slots.hpp` would have to include
// `BlockStatement.hpp` (for the complete `BlockStatement`), but `BlockStatement.hpp` includes
// `Slots.hpp` (for `Slots::Statement`), and with `Slots.hpp`'s guard set those definitions
// would not be visible where `BlockStatement.hpp`'s class body needs them. After the
// `BlockStatement` class both `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and
// `BlockStatement` are complete, so the kind defines cleanly. The `inline` variable still has
// external linkage and one address across translation units (the C++17 `inline` guarantee),
// preserving the pointer-identity comparison `node.Slot.Kind == &Slots::Body` the slot system
// relies on.
namespace Slots {
inline const CSharpSlotInfoT<BlockStatement> Body{"Body", false, nullptr, false};
} // namespace Slots

// The `TryBlock` kind -- a single REQUIRED `BlockStatement` child (the `try` body of a
// `TryCatchStatement.TryBlock`). Unique to `TryCatchStatement` among the ported nodes (the
// `catch`/`finally` bodies are the separate `FinallyBlock` kind). A `CSharpSlotInfoT<BlockStatement>`
// (the element type is the concrete `BlockStatement` node). Defined HERE (in BlockStatement.hpp,
// after the `BlockStatement` class and the `Slots::Body` kind) for the same cycle-breaking reason
// as `Slots::Body` (BlockStatement.hpp includes Slots.hpp for its `StatementsSlot`, so a
// `CSharpSlotInfoT<BlockStatement>` kind cannot live in Slots.hpp -- a circular include -- and is
// defined here where both `CSharpSlotInfoT` and `BlockStatement` are complete). The `inline`
// variable has external linkage and one address across translation units (the C++17 `inline`
// guarantee), preserving the pointer-identity comparison `node.Slot.Kind == &Slots::TryBlock` the
// slot system relies on. The kind name `TryBlock` collides with no class in the `Syntax`
// namespace (there is `BlockStatement`, not `TryBlock`), so no elaborated-type-specifier is needed.
// The shared constant is constructed non-collection/non-optional (`{"TryBlock", false, nullptr,
// false}`); the per-node `TryBlockSlot` on `TryCatchStatement` carries the `IsOptional=false`
// flag (the `TryBlock` is a required slot).
namespace Slots {
inline const CSharpSlotInfoT<BlockStatement> TryBlock{"TryBlock", false, nullptr, false};
} // namespace Slots

// The `FinallyBlock` kind -- a single NULLABLE `BlockStatement` child (the `finally` body of a
// `TryCatchStatement.FinallyBlock`, absent for a `try`/`catch` without `finally`). Unique to
// `TryCatchStatement` among the ported nodes. A `CSharpSlotInfoT<BlockStatement>` (the element
// type is the concrete `BlockStatement` node). Defined HERE (in BlockStatement.hpp, after the
// `Slots::TryBlock` kind) for the same cycle-breaking reason as `Slots::Body`/`Slots::TryBlock`
// (BlockStatement.hpp includes Slots.hpp, so the kind cannot live in Slots.hpp). The `inline`
// variable has external linkage and one address across translation units (the C++17 `inline`
// guarantee), preserving the pointer-identity comparison `node.Slot.Kind == &Slots::FinallyBlock`.
// The kind name `FinallyBlock` collides with no class in the `Syntax` namespace (there is
// `BlockStatement`, not `FinallyBlock`), so no elaborated-type-specifier is needed. The shared
// constant is constructed non-collection/non-optional (`{"FinallyBlock", false, nullptr,
// false}`); the per-node `FinallyBlockSlot` on `TryCatchStatement` carries the `IsOptional=true`
// flag (the `FinallyBlock` is a nullable slot -- the shared kind is constructed non-optional,
// the per-node slot carries the optionality, the `IfElseStatement.FalseStatement` D258 precedent).
namespace Slots {
inline const CSharpSlotInfoT<BlockStatement> FinallyBlock{"FinallyBlock", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_BLOCKSTATEMENT_HPP
