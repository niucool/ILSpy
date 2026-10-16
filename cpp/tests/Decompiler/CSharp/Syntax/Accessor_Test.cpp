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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the `Accessor` concrete node (cpp/.../Syntax/Accessor.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/Accessor.cs) -- the simplest remaining concrete
// `EntityDeclaration` (an `Attributes` `AttributeSection` collection + a nullable `Body`
// `BlockStatement` single slot, the `DestructorDeclaration` D272 shape minus `NameToken`, plus an
// `AccessorKind` scalar and the `Name`/`NameToken` no-op overrides). The next in-order Phase-5
// piece per the D273 plan, and the dependency that unblocks `PropertyDeclaration`/
// `IndexerDeclaration`/`EventDeclaration`. The suite shares a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitAccessor` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier` of its `Attributes` collection and the
// `BlockStatement`/`ReturnStatement` of its `Body`, and the `DestructorDeclaration`/`WhileStatement`
// used for the cross-type DoMatch rejections), recording a tag and recursing via `VisitChildren`
// (the inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitAccessor(Accessor* node) override {
        if (node == nullptr) { trace.push_back("<null-accessor>"); return; }
        trace.push_back("accessor");
        VisitChildren(node);
    }
    void VisitAttributeSection(AttributeSection* node) override {
        if (node == nullptr) { trace.push_back("<null-attrsec>"); return; }
        trace.push_back("attrsec:" + node->AttributeTarget());
        VisitChildren(node);
    }
    void VisitAttribute(Attribute* node) override {
        if (node == nullptr) { trace.push_back("<null-attr>"); return; }
        trace.push_back("attr");
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-simple>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or(""));
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-block>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitReturnStatement(ReturnStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-return>"); return; }
        trace.push_back("return");
        VisitChildren(node);
    }
    void VisitDestructorDeclaration(DestructorDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-dtor>"); return; }
        trace.push_back("dtor");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping an `Accessor` and all its children alive in the test scope (the port's
// non-owning raw-pointer child slots -- the D223 design: the parent does not take ownership; the
// holder's `unique_ptr`s own the nodes). An `Accessor` with a `Body` `BlockStatement` holding a
// `ReturnStatement` (and, for the attribute variant, an `Attributes` `AttributeSection` whose
// `Attributes` hold an `Attribute` whose `Type` is a `SimpleType` `Foo`).
struct AccessorHolder {
    std::unique_ptr<Accessor> acc;
    std::unique_ptr<BlockStatement> body;
    std::unique_ptr<ReturnStatement> ret;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    Accessor* get() const { return acc.get(); }
    Accessor* operator->() const { return acc.get(); }
};

// Build an `Accessor` (a `Getter`) with a `Body` `BlockStatement` holding a `ReturnStatement`
// (no attributes). The holder keeps every node alive.
AccessorHolder make_AccessorGetter() {
    AccessorHolder h;
    h.acc = std::make_unique<Accessor>(AccessorKind::Getter);
    h.body = std::make_unique<BlockStatement>();
    h.ret = std::make_unique<ReturnStatement>();
    h.body->Statements().Add(h.ret.get());
    h.acc->Body(h.body.get());
    return h;
}

// Build an `Accessor` (a `Getter`) with a `Body`, and an `Attributes` `AttributeSection` holding
// an `Attribute` whose `Type` is a `SimpleType` `Foo`.
AccessorHolder make_AccessorGetterWithAttribute() {
    AccessorHolder h;
    h.acc = std::make_unique<Accessor>(AccessorKind::Getter);
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.acc->Attributes().Add(h.attrSec.get());
    h.body = std::make_unique<BlockStatement>();
    h.ret = std::make_unique<ReturnStatement>();
    h.body->Statements().Add(h.ret.get());
    h.acc->Body(h.body.get());
    return h;
}

} // namespace

// ==========================================================================
// Accessor (the simplest remaining concrete TypeMember)
// ==========================================================================

// ---- is-a + final ---------------------------------------------------------

// `Accessor` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_Accessor, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<Accessor>);
}

// `Accessor` derives from `EntityDeclaration` (the `TypeMember` base), which derives from
// `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_Accessor, IsEntityDeclarationAndAstNode) {
    Accessor acc;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&acc), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&acc), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&acc), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&acc), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&acc), nullptr);
}

// ---- The AccessorKind enum ------------------------------------------------

// The `AccessorKind` enum values are in C# declaration order, with `Any` as the zero value.
TEST(CSharp_Accessor, AccessorKindValues) {
    EXPECT_EQ(static_cast<int>(AccessorKind::Any), 0);
    EXPECT_EQ(static_cast<int>(AccessorKind::Getter), 1);
    EXPECT_EQ(static_cast<int>(AccessorKind::Setter), 2);
    EXPECT_EQ(static_cast<int>(AccessorKind::Init), 3);
    EXPECT_EQ(static_cast<int>(AccessorKind::Adder), 4);
    EXPECT_EQ(static_cast<int>(AccessorKind::Remover), 5);
}

// ---- Construction ---------------------------------------------------------

// The empty ctor: no `Body`, empty `Attributes`. `GetChildCount` is `0 + 1 = 1` (the empty
// `Attributes` collection plus the one `Body` single slot -- the `Body` single slot contributes 1
// to the flattened count even when absent). The `Kind` defaults to `Any` (the enum's zero value);
// `SymbolKind` is `Method`.
TEST(CSharp_Accessor, EmptyCtor) {
    Accessor acc;
    EXPECT_EQ(acc.SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(acc.Kind(), AccessorKind::Any);
    EXPECT_EQ(acc.Body(), nullptr);
    EXPECT_EQ(acc.Attributes().Count(), 0);
    EXPECT_EQ(acc.GetChildCount(), 1);  // 0 attrs + 1 Body (the single slot counts even when null)
}

// The `(AccessorKind)` required-prefix ctor sets `Kind`; the `Body`/`Attributes` stay empty.
TEST(CSharp_Accessor, KindCtorSetsKind) {
    Accessor acc(AccessorKind::Getter);
    EXPECT_EQ(acc.Kind(), AccessorKind::Getter);
    EXPECT_EQ(acc.Body(), nullptr);
    EXPECT_EQ(acc.Attributes().Count(), 0);
}

// The `(AccessorKind)` ctor is `explicit` (a single-argument ctor is a converting ctor by default);
// an implicit conversion from `AccessorKind` to `Accessor` does NOT compile. The outer parens protect
// the template-arg comma from the preprocessor (the gtest workaround for two-arg type traits).
TEST(CSharp_Accessor, KindCtorIsExplicit) {
    EXPECT_TRUE((std::is_constructible_v<Accessor, AccessorKind>));
    EXPECT_FALSE((std::is_convertible_v<AccessorKind, Accessor>));
}

// ---- The SymbolKind override ----------------------------------------------

TEST(CSharp_Accessor, SymbolKindOverrideReturnsMethod) {
    Accessor acc;
    EXPECT_EQ(acc.SymbolKind(), SymbolKind::Method);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) --------------

TEST(CSharp_Accessor, ModifiersDefaultsToNoneAndRoundTrips) {
    Accessor acc;
    EXPECT_EQ(acc.Modifiers(), Modifiers::None);
    acc.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(acc.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_Accessor, HasModifierIsBitmaskTest) {
    Accessor acc;
    acc.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(acc.HasModifier(Modifiers::Static));
    EXPECT_TRUE(acc.HasModifier(Modifiers::Public));
    EXPECT_FALSE(acc.HasModifier(Modifiers::Virtual));
}

// ---- The Kind scalar ------------------------------------------------------

// The `Kind` scalar round-trips through the getter/setter.
TEST(CSharp_Accessor, KindScalarRoundTrips) {
    Accessor acc;
    EXPECT_EQ(acc.Kind(), AccessorKind::Any);
    acc.Kind(AccessorKind::Setter);
    EXPECT_EQ(acc.Kind(), AccessorKind::Setter);
    acc.Kind(AccessorKind::Init);
    EXPECT_EQ(acc.Kind(), AccessorKind::Init);
}

// ---- The Name/NameToken no-op overrides (the accessor carries no name) -----

// `Name` returns the empty string; `NameToken` returns null. The setters are NO-OPS (NOT throws
// -- faithful to the C# `set { }`, unlike `FieldDeclaration` whose setters throw).
TEST(CSharp_Accessor, NameAndNameTokenAreNoOps) {
    Accessor acc;
    EXPECT_EQ(acc.Name(), "");
    EXPECT_EQ(acc.NameToken(), nullptr);
    // The setters are no-ops: they do not throw and do not change the values.
    acc.Name(std::string("Foo"));
    EXPECT_EQ(acc.Name(), "");  // still empty -- the setter did nothing
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    acc.NameToken(tok.get());
    EXPECT_EQ(acc.NameToken(), nullptr);  // still null -- the setter did nothing
}

// ---- The ReturnType (inherited base kind-walk; an Accessor has no Type slot) -

// The inherited base `ReturnType()` kind-walks for the `Type` kind and returns null (an `Accessor`
// declares no `Type` slot).
TEST(CSharp_Accessor, ReturnTypeIsNullNoTypeSlot) {
    Accessor acc;
    EXPECT_EQ(acc.ReturnType(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

// `Attributes().Add(...)` appends and parents; the collection is NON-incremental (the `Body` single
// slot trails it), so `Add` invalidates the parent's indices for a lazy reindex.
TEST(CSharp_Accessor, AttributesCollectionAddReparents) {
    Accessor acc;
    auto attrSec = std::make_unique<AttributeSection>();
    acc.Attributes().Add(attrSec.get());
    EXPECT_EQ(acc.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &acc);
}

// ---- The Body slot (a nullable single BlockStatement, index-less) ----------

// The `Body` setter re-parents the body. The slot follows the `Attributes` collection, so the
// index-less `SetChildNode` invalidates the parent's indices; the reindex is triggered by
// `Slot()`. After the reindex the `Body`'s flattened `ChildIndex` is `attrCount` (0 with no
// attributes -- the `Body` is the first non-empty slot).
TEST(CSharp_Accessor, BodySetterReparentsAndReindexes) {
    Accessor acc;
    auto body = std::make_unique<BlockStatement>();
    acc.Body(body.get());
    EXPECT_EQ(acc.Body(), body.get());
    EXPECT_EQ(body->Parent(), &acc);
    (void)body->Slot();  // trigger the lazy reindex
    EXPECT_EQ(body->ChildIndex, 0);  // attrCount (0) -- Body at flattened index 0
}

// The `Body` setter detaches the old body and clears with null.
TEST(CSharp_Accessor, BodySetterDetachesAndClears) {
    Accessor acc;
    auto a = std::make_unique<BlockStatement>();
    auto b = std::make_unique<BlockStatement>();
    acc.Body(a.get());
    EXPECT_EQ(a->Parent(), &acc);
    acc.Body(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(acc.Body(), b.get());
    acc.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(acc.Body(), nullptr);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, then `Body` at
// `attrCount`. `GetChildCount` is `attrCount + 1` (the single `Body` slot contributes 1 to the
// flattened count regardless of whether the `Body` is filled -- the formula is
// `attributes_.Count() + 1`, not `+ (body_ != nullptr ? 1 : 0)`).
TEST(CSharp_Accessor, GetChildWalksSlots) {
    auto acc = make_AccessorGetter();
    EXPECT_EQ(acc->GetChildCount(), 1);  // 0 attrs + 1 Body (the single slot)
    EXPECT_EQ(acc->GetChild(0), acc->Body());
    EXPECT_THROW(acc->GetChild(1), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot for each flattened index.
TEST(CSharp_Accessor, GetChildSlotInfoWalksSlots) {
    auto acc = make_AccessorGetter();
    EXPECT_EQ(acc->GetChildSlotInfo(0), &acc->BodySlot);  // Body at attrCount (0)
    EXPECT_THROW(acc->GetChildSlotInfo(1), std::out_of_range);
}

// `GetCollectionByKind` returns the `Attributes` collection for its kind, null for other kinds.
TEST(CSharp_Accessor, GetCollectionByKindReturnsAttributes) {
    Accessor acc;
    EXPECT_EQ(acc.GetCollectionByKind(&Slots::AttributeSection), &acc.Attributes());
    EXPECT_EQ(acc.GetCollectionByKind(&Slots::Body), nullptr);  // Body is a single slot, not a collection
    EXPECT_EQ(acc.GetCollectionByKind(&Slots::Type), nullptr);
}

// `SetChild` replaces the `Body` in place (the slot must already exist at the flattened index).
TEST(CSharp_Accessor, SetChildReplacesBody) {
    auto acc = make_AccessorGetter();
    auto body2 = std::make_unique<BlockStatement>();
    auto* oldBody = acc->Body();
    acc->SetChild(0, body2.get());  // Body at flattened index 0 (0 attrs)
    EXPECT_EQ(acc->Body(), body2.get());
    EXPECT_EQ(oldBody->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) --------

// The slot statics point at the shared `Slots` kinds.
TEST(CSharp_Accessor, SlotStaticsPointAtSharedKinds) {
    Accessor acc;
    EXPECT_EQ(acc.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(acc.BodySlot.Kind(), &Slots::Body);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_Accessor, SlotStaticsAreDistinct) {
    Accessor acc;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&acc.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&acc.BodySlot));
}

// The `Body` collection reuses the SAME `Slots::Body` kind as `DestructorDeclaration` (the D260
// cycle-broken kind), confirming the kind is shared across the `TypeMember` hierarchy by `[Slot]`
// name.
TEST(CSharp_Accessor, BodySlotSharesBodyKindWithDestructorDeclaration) {
    Accessor acc;
    EXPECT_EQ(acc.BodySlot.Kind(), &Slots::Body);
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

// `AcceptVisitor` dispatches to `VisitAccessor` (the concrete `Visit`).
TEST(CSharp_Accessor, AcceptVisitorDispatches) {
    Accessor acc;
    RecordingVisitor v;
    acc.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"accessor"}));
}

// `AcceptVisitor` is virtual: a call through `AstNode*` dispatches to the concrete override.
TEST(CSharp_Accessor, AcceptVisitorVirtualThroughBase) {
    Accessor acc;
    AstNode* node = &acc;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"accessor"}));
}

// `AcceptVisitor` is virtual: a call through `EntityDeclaration*` dispatches to the concrete
// override.
TEST(CSharp_Accessor, AcceptVisitorVirtualThroughEntityDeclaration) {
    Accessor acc;
    EntityDeclaration* node = &acc;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"accessor"}));
}

// The depth-first walk recurses into the `Body` (a `BlockStatement` holding a `ReturnStatement`) in
// document order (Attributes -> Body, with no attributes here).
TEST(CSharp_Accessor, DepthFirstWalk) {
    auto acc = make_AccessorGetter();
    RecordingVisitor v;
    acc->AcceptVisitor(v);
    // accessor -> block (Body) -> return (BlockStatement.Statements[0])
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "accessor", "block", "return",
    }));
}

// An `Accessor` with no `Body` records just the accessor node (the walk has no children to recurse
// into -- the `Attributes` collection is empty and the `Body` is null).
TEST(CSharp_Accessor, DepthFirstWalkNoBody) {
    Accessor acc;
    RecordingVisitor v;
    acc.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"accessor"}));
}

// An `Accessor` with an attribute recurses into the `AttributeSection` -> `Attribute` -> its
// `SimpleType` `Type` -> its `Identifier` BEFORE the `Body` (the `Attributes` collection is the
// first slot). The `Body` `BlockStatement` holds a `ReturnStatement`, so the walk continues into it.
TEST(CSharp_Accessor, DepthFirstWalkWithAttribute) {
    auto acc = make_AccessorGetterWithAttribute();
    RecordingVisitor v;
    acc->AcceptVisitor(v);
    // accessor -> attrsec (Attributes[0]) -> attr -> simple:Foo (the Attribute's Type) -> id:Foo
    //         -> block (Body) -> return (BlockStatement.Statements[0])
    ASSERT_EQ(v.trace.size(), 7u);
    EXPECT_EQ(v.trace[0], "accessor");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "block");
    EXPECT_EQ(v.trace[6], "return");
}

// ---- DoMatch (the generated pattern match) --------------------------------

// An `Accessor` matches itself (same `Kind`, same `Body`).
TEST(CSharp_Accessor, DoMatchSameNode) {
    auto a = make_AccessorGetter();
    auto b = make_AccessorGetter();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Kind` mismatch rejects (the `Any`-wildcard is NOT a bitmask; `Getter` matches only `Getter`,
// not `Setter`).
TEST(CSharp_Accessor, DoMatchKindMismatchRejects) {
    auto a = make_AccessorGetter();  // Getter
    auto b = make_AccessorGetter();
    b->Kind(AccessorKind::Setter);  // different kind
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `AccessorKind::Any` matches any candidate (the wildcard).
TEST(CSharp_Accessor, DoMatchKindAnyWildcard) {
    auto a = make_AccessorGetter();
    a->Kind(AccessorKind::Any);
    auto b = make_AccessorGetter();
    b->Kind(AccessorKind::Setter);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask; `Static` matches only `Static`, not `Static|Public`).
TEST(CSharp_Accessor, DoMatchModifiersMismatchRejects) {
    auto a = make_AccessorGetter();
    auto b = make_AccessorGetter();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_Accessor, DoMatchModifiersAnyWildcard) {
    auto a = make_AccessorGetter();
    auto b = make_AccessorGetter();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` mismatch rejects (the `MatchOptional` `Body` term -- both present and the
// `BlockStatement.DoMatch` rejects on a different child count).
TEST(CSharp_Accessor, DoMatchBodyMismatchRejects) {
    auto a = make_AccessorGetter();  // Body with 1 statement
    AccessorHolder b;
    b.acc = std::make_unique<Accessor>(AccessorKind::Getter);
    b.body = std::make_unique<BlockStatement>();  // empty body -- 0 statements
    b.acc->Body(b.body.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` asymmetry rejects: a pattern with a `Body` vs a candidate without one.
TEST(CSharp_Accessor, DoMatchBodyAsymmetryRejects) {
    auto a = make_AccessorGetter();  // has Body
    Accessor b(AccessorKind::Getter);  // no Body (empty ctor + Kind)
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

// A `Body` both-absent matches (the `MatchOptional(null, null)` is true -- both accessors with no
// body).
TEST(CSharp_Accessor, DoMatchBodyBothAbsentMatches) {
    Accessor a(AccessorKind::Getter);
    Accessor b(AccessorKind::Getter);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A non-`Accessor` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not an `Accessor`).
TEST(CSharp_Accessor, DoMatchRejectsNonAccessorEntityDeclaration) {
    auto a = make_AccessorGetter();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_Accessor, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_AccessorGetter();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_Accessor, DoMatchRejectsNull) {
    auto a = make_AccessorGetter();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `Body`/`Attributes`, copies the `Kind`/`Modifiers` scalars, and does not
// detach the source.
TEST(CSharp_Accessor, CloneDeepCopies) {
    auto a = make_AccessorGetter();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<Accessor>(
        static_cast<Accessor*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(clone->Kind(), AccessorKind::Getter);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), a->Body());
    // The source is not detached.
    EXPECT_EQ(a->Body()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `Accessor*`.
TEST(CSharp_Accessor, CloneVirtualAndCovariant) {
    auto a = make_AccessorGetter();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<Accessor*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<Accessor>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_Accessor, CloneCopiesAttributes) {
    auto a = make_AccessorGetterWithAttribute();
    auto clone = std::unique_ptr<Accessor>(
        static_cast<Accessor*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->Body()->Parent(), clone.get());
}

// `Clone` skips the `Body` when it is absent (the nullable slot).
TEST(CSharp_Accessor, CloneSkipsAbsentBody) {
    Accessor a(AccessorKind::Setter);
    auto clone = std::unique_ptr<Accessor>(
        static_cast<Accessor*>(a.Clone()));
    EXPECT_EQ(clone->Kind(), AccessorKind::Setter);
    EXPECT_EQ(clone->Body(), nullptr);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `Body` is nullable, so even an accessor with no
// body is invariant-valid -- there are no required single slots).
TEST(CSharp_Accessor, CheckInvariantPassesOnEmpty) {
    Accessor acc;  // no Body, no Attributes
    acc.CheckInvariant();  // should not assert -- no required slots
    SUCCEED();
}

// `CheckInvariant` passes on a node with a `Body` too.
TEST(CSharp_Accessor, CheckInvariantPassesOnFilled) {
    auto acc = make_AccessorGetter();
    acc->CheckInvariant();  // should not assert
    SUCCEED();
}
