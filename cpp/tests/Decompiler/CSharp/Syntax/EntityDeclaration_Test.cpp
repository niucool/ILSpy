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

// Tests for the `EntityDeclaration` abstract base (cpp/.../Syntax/EntityDeclaration.hpp, the port
// of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/EntityDeclaration.cs) -- the common base of
// the `TypeMember` hierarchy, with the abstract `SymbolKind` property, the virtual
// `Attributes`/`Name`/`NameToken`/`ReturnType`, the `Modifiers` scalar, and the
// `MatchAttributesAndModifiers` helper -- plus the `DestructorDeclaration` concrete node
// (cpp/.../Syntax/DestructorDeclaration.hpp, the first concrete `TypeMember`) and the
// `GetChildren<T>` kind-based collection read on `AstNode` (the D271-deferred piece now landed).
// The next in-order Phase-5 piece per the D271 plan. The suite shares a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
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

// A recording depth-first visitor: overrides the `VisitDestructorDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier` of its `Attributes` collection, the
// `Identifier` of its `NameToken`, and the `BlockStatement`/`ReturnStatement` of its `Body`),
// recording a tag and recursing via `VisitChildren` (the inherited depth-first default). The
// trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitDestructorDeclaration(DestructorDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-dtor>"); return; }
        trace.push_back("dtor");
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
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
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

// A holder keeping a `DestructorDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes). A `DestructorDeclaration` with a
// `NameToken` `Identifier`, a `Body` `BlockStatement` whose `Statements` hold a `ReturnStatement`
// (and, for the attribute variant, an `Attributes` `AttributeSection` whose `Attributes` hold an
// `Attribute` whose `Type` is a `SimpleType` `Foo`).
struct DtorHolder {
    std::unique_ptr<DestructorDeclaration> dd;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<BlockStatement> body;
    std::unique_ptr<ReturnStatement> ret;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    DestructorDeclaration* get() const { return dd.get(); }
    DestructorDeclaration* operator->() const { return dd.get(); }
};

// Build a `DestructorDeclaration` with a `NameToken` `Foo` and a `Body` `BlockStatement` holding a
// `ReturnStatement` (no attributes). The holder keeps every node alive.
DtorHolder make_DestructorDeclaration() {
    DtorHolder h;
    h.dd = std::make_unique<DestructorDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.dd->NameToken(h.nameToken.get());
    h.body = std::make_unique<BlockStatement>();
    h.ret = std::make_unique<ReturnStatement>();
    h.body->Statements().Add(h.ret.get());
    h.dd->Body(h.body.get());
    return h;
}

// Build a `DestructorDeclaration` with a `NameToken` `Foo`, an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`, and a `Body` `BlockStatement`
// (no statements).
DtorHolder make_DestructorDeclarationWithAttribute() {
    DtorHolder h;
    h.dd = std::make_unique<DestructorDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.dd->NameToken(h.nameToken.get());
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.dd->Attributes().Add(h.attrSec.get());
    h.body = std::make_unique<BlockStatement>();
    h.dd->Body(h.body.get());
    return h;
}

} // namespace

// ==========================================================================
// EntityDeclaration (the abstract TypeMember base)
// ==========================================================================

// `EntityDeclaration` is an abstract base (the C# `abstract partial class`): it cannot be
// instantiated, only derived from. The port makes it abstract via the pure-virtual `SymbolKind`
// (and the inherited `AstNode` pure-virtuals `DoMatch`/`AcceptVisitor`/`Clone`).
TEST(CSharp_EntityDeclaration, IsAbstract) {
    EXPECT_TRUE(std::is_abstract_v<EntityDeclaration>);
}

// `EntityDeclaration` derives from `AstNode` (a type member is a structural container, not a
// `Statement`/`Expression`/`AstType`); the `DestructorDeclaration` subclass is an
// `EntityDeclaration` and an `AstNode` but NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_EntityDeclaration, IsAstNodeNotStatementNotExpressionNotAstType) {
    DestructorDeclaration dd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&dd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&dd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&dd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&dd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&dd), nullptr);
}

// ---- The abstract SymbolKind property ------------------------------------

// The abstract `SymbolKind` is overridden by `DestructorDeclaration` to return
// `SymbolKind::Destructor` (the kind of member a finalizer is).
TEST(CSharp_EntityDeclaration, SymbolKindOverrideReturnsDestructor) {
    DestructorDeclaration dd;
    EXPECT_EQ(dd.SymbolKind(), SymbolKind::Destructor);
}

// ---- The Modifiers scalar (a settable [Flags] enum, NOT a [Slot]) --------

// `Modifiers` defaults to `None` (the enum's zero value -- a declaration with no modifiers). The
// setter round-trips a combined mask (the `[Flags]` bitwise operators).
TEST(CSharp_EntityDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    DestructorDeclaration dd;
    EXPECT_EQ(dd.Modifiers(), Modifiers::None);
    dd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(dd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

// `HasModifier(mod)` is a bitmask test (all of `mod`'s bits set), NOT the pattern-match `==`.
TEST(CSharp_EntityDeclaration, HasModifierIsBitmaskTest) {
    DestructorDeclaration dd;
    dd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(dd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(dd.HasModifier(Modifiers::Public));
    EXPECT_TRUE(dd.HasModifier(Modifiers::Static | Modifiers::Public));
    EXPECT_FALSE(dd.HasModifier(Modifiers::Virtual));
    EXPECT_FALSE(dd.HasModifier(Modifiers::Static | Modifiers::Virtual));
}

// ---- The virtual Name/NameToken (over the Identifier slot) ----------------

// `NameToken` (the concrete override) returns the backing field. The `Name` virtual (inherited
// base kinds-walk) returns the `NameToken`'s `Name`, empty when the token is absent.
TEST(CSharp_EntityDeclaration, NameAndNameTokenOverIdentifierSlot) {
    DestructorDeclaration dd;
    // No NameToken yet: the inherited base `Name()` kind-walks for the `Identifier` kind and
    // returns empty (the token is absent).
    EXPECT_EQ(dd.Name(), "");
    EXPECT_EQ(dd.NameToken(), nullptr);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    dd.NameToken(tok.get());
    EXPECT_EQ(dd.NameToken(), tok.get());
    EXPECT_EQ(dd.Name(), "Foo");
    EXPECT_EQ(tok->Parent(), &dd);
}

// The `Name(string)` setter (the inherited base virtual) creates an `Identifier` token via
// `Identifier::Create` and sets the `Identifier`-kind slot.
TEST(CSharp_EntityDeclaration, NameStringSetterCreatesToken) {
    DestructorDeclaration dd;
    dd.Name(std::string("Bar"));
    ASSERT_NE(dd.NameToken(), nullptr);
    EXPECT_EQ(dd.NameToken()->Name(), "Bar");
    EXPECT_EQ(dd.Name(), "Bar");
}

// ---- The virtual ReturnType (over the Type slot; nullable) --------------

// `ReturnType` (the inherited base virtual) kind-walks for the `Type` kind. A `DestructorDeclaration`
// declares no `Type` slot, so it returns null (a finalizer has no return type).
TEST(CSharp_EntityDeclaration, ReturnTypeIsNullForDestructor) {
    DestructorDeclaration dd;
    EXPECT_EQ(dd.ReturnType(), nullptr);
}

// ---- GetChildren<T> (the D271-deferred kind-based collection read) --------

// `GetChildren<T>` returns the real collection for a kind the node declares a collection of (the
// `Attributes` `AttributeSection` collection of a `DestructorDeclaration`).
TEST(CSharp_EntityDeclaration, GetChildrenReturnsRealCollectionForDeclaredKind) {
    DestructorDeclaration dd;
    auto& attrs = dd.GetChildren<AttributeSection>(&Slots::AttributeSection);
    EXPECT_EQ(attrs.Count(), 0);
    EXPECT_EQ(&attrs, &dd.Attributes());
}

// `GetChildren<T>` returns a detached EMPTY collection for a kind the node declares NO collection
// of (a `DestructorDeclaration` has no `Statement` collection). The detached empty is empty and
// distinct from the real `Attributes` collection (a stable per-`<T>` singleton).
TEST(CSharp_EntityDeclaration, GetChildrenReturnsDetachedEmptyForAbsentKind) {
    DestructorDeclaration dd;
    auto& stmts = dd.GetChildren<Statement>(&Slots::Statement);
    EXPECT_EQ(stmts.Count(), 0);
    auto& attrs = dd.GetChildren<AttributeSection>(&Slots::AttributeSection);
    // The two collections span distinct element types (`Statement` vs `AttributeSection`), so
    // `AstNodeCollectionT<Statement>*` and `AstNodeCollectionT<AttributeSection>*` are unrelated
    // pointer types -- cast to the common `AstNodeCollection*` base for the `EXPECT_NE` (the
    // D251/D252 cross-element-type crux).
    EXPECT_NE(static_cast<AstNodeCollection*>(&stmts),
              static_cast<AstNodeCollection*>(&attrs));
    // The detached empty for the same kind is the same singleton.
    auto& stmts2 = dd.GetChildren<Statement>(&Slots::Statement);
    EXPECT_EQ(&stmts, &stmts2);
}

// ==========================================================================
// DestructorDeclaration (the first concrete TypeMember)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `DestructorDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_DestructorDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<DestructorDeclaration>);
}

// ---- The TildeToken const string -----------------------------------------

TEST(CSharp_DestructorDeclaration, TildeTokenConstString) {
    EXPECT_STREQ(DestructorDeclaration::TildeToken, "~");
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `NameToken`, no `Body`, empty `Attributes`. `GetChildCount` is `0 + 2 = 2`
// (the empty `Attributes` collection plus the two single slots `NameToken` + `Body`).
TEST(CSharp_DestructorDeclaration, EmptyCtorHasNoSlots) {
    DestructorDeclaration dd;
    EXPECT_EQ(dd.NameToken(), nullptr);
    EXPECT_EQ(dd.Body(), nullptr);
    EXPECT_EQ(dd.Attributes().Count(), 0);
    EXPECT_EQ(dd.GetChildCount(), 2);
    EXPECT_EQ(dd.SymbolKind(), SymbolKind::Destructor);
    EXPECT_EQ(dd.Modifiers(), Modifiers::None);
}

// ---- The NameToken slot (a single REQUIRED Identifier) ------------------

// The `NameToken` setter re-parents the token. The slot follows the `Attributes` collection, so
// the index-less `SetChildNode` invalidates the parent's indices; the reindex is triggered by
// `Slot()` (the `EnsureChildIndices` call). After the reindex the `NameToken`'s flattened
// `ChildIndex` is `attrCount` (0 with no attributes -- the `NameToken` is the first non-empty
// slot).
TEST(CSharp_DestructorDeclaration, NameTokenSetterReparentsAndReindexes) {
    DestructorDeclaration dd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    dd.NameToken(tok.get());
    EXPECT_EQ(dd.NameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &dd);
    (void)tok->Slot();  // trigger the lazy reindex
    EXPECT_EQ(tok->ChildIndex, 0);  // attrCount (0) -- NameToken at flattened index 0
}

// The `NameToken` setter detaches the old token.
TEST(CSharp_DestructorDeclaration, NameTokenSetterDetachesOld) {
    DestructorDeclaration dd;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    dd.NameToken(a.get());
    EXPECT_EQ(a->Parent(), &dd);
    dd.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(dd.NameToken(), b.get());
}

// ---- The Body slot (a single NULLABLE BlockStatement) --------------------

// The `Body` setter re-parents the block.
TEST(CSharp_DestructorDeclaration, BodySetterReparents) {
    DestructorDeclaration dd;
    auto body = std::make_unique<BlockStatement>();
    dd.Body(body.get());
    EXPECT_EQ(dd.Body(), body.get());
    EXPECT_EQ(body->Parent(), &dd);
}

// The `Body` setter detaches the old block and clears with null.
TEST(CSharp_DestructorDeclaration, BodySetterDetachesAndClears) {
    DestructorDeclaration dd;
    auto a = std::make_unique<BlockStatement>();
    auto b = std::make_unique<BlockStatement>();
    dd.Body(a.get());
    dd.Body(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(dd.Body(), b.get());
    dd.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(dd.Body(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

// `Attributes().Add(...)` appends and parents; the collection is NON-incremental (the `NameToken`
// and `Body` singles trail it), so `Add` invalidates the parent's indices for a lazy reindex.
TEST(CSharp_DestructorDeclaration, AttributesCollectionAddReparents) {
    DestructorDeclaration dd;
    auto attrSec = std::make_unique<AttributeSection>();  // an empty AttributeSection
    dd.Attributes().Add(attrSec.get());
    EXPECT_EQ(dd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &dd);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, then `NameToken` at
// `attrCount`, then `Body` at `attrCount + 1`.
TEST(CSharp_DestructorDeclaration, GetChildWalksSlots) {
    DestructorDeclaration dd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    dd.NameToken(tok.get());
    auto body = std::make_unique<BlockStatement>();
    dd.Body(body.get());
    EXPECT_EQ(dd.GetChildCount(), 2);  // 0 attrs + NameToken + Body
    EXPECT_EQ(dd.GetChild(0), tok.get());
    EXPECT_EQ(dd.GetChild(1), body.get());
    EXPECT_THROW(dd.GetChild(2), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot for each flattened index.
TEST(CSharp_DestructorDeclaration, GetChildSlotInfoWalksSlots) {
    DestructorDeclaration dd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    dd.NameToken(tok.get());
    auto body = std::make_unique<BlockStatement>();
    dd.Body(body.get());
    EXPECT_EQ(dd.GetChildSlotInfo(0), &dd.NameTokenSlot);   // NameToken at attrCount (0)
    EXPECT_EQ(dd.GetChildSlotInfo(1), &dd.BodySlot);        // Body at attrCount + 1
    EXPECT_THROW(dd.GetChildSlotInfo(2), std::out_of_range);
}

// `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind, null
// for other kinds (the single-slot kinds `Identifier`/`Body` and unrelated kinds all fall back).
TEST(CSharp_DestructorDeclaration, GetCollectionByKindReturnsAttributes) {
    DestructorDeclaration dd;
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::AttributeSection), &dd.Attributes());
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::Body), nullptr);
}

// `SetChild` replaces the `NameToken` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_DestructorDeclaration, SetChildReplacesNameToken) {
    DestructorDeclaration dd;
    auto tok1 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    dd.NameToken(tok1.get());
    auto tok2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    dd.SetChild(0, tok2.get());  // NameToken at flattened index 0 (0 attrs)
    EXPECT_EQ(dd.NameToken(), tok2.get());
    EXPECT_EQ(tok1->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

// The slot statics point at the shared `Slots` kinds.
TEST(CSharp_DestructorDeclaration, SlotStaticsPointAtSharedKinds) {
    DestructorDeclaration dd;
    EXPECT_EQ(dd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(dd.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(dd.BodySlot.Kind(), &Slots::Body);
}

// The slot statics are distinct (the three kinds are distinct -- cast to the common
// `CSharpSlotInfo*` base for the cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_DestructorDeclaration, SlotStaticsAreDistinct) {
    DestructorDeclaration dd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&dd.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&dd.BodySlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&dd.BodySlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

// `AcceptVisitor` dispatches to `VisitDestructorDeclaration` (the concrete `Visit`).
TEST(CSharp_DestructorDeclaration, AcceptVisitorDispatches) {
    DestructorDeclaration dd;
    RecordingVisitor v;
    dd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"dtor"}));
}

// `AcceptVisitor` is virtual: a call through `AstNode*` dispatches to the concrete override.
TEST(CSharp_DestructorDeclaration, AcceptVisitorVirtualThroughBase) {
    DestructorDeclaration dd;
    AstNode* node = &dd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"dtor"}));
}

// `AcceptVisitor` is virtual: a call through `EntityDeclaration*` dispatches to the concrete
// override (the `EntityDeclaration` base declares the abstract `AcceptVisitor`; the concrete
// `DestructorDeclaration` overrides it).
TEST(CSharp_DestructorDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    DestructorDeclaration dd;
    EntityDeclaration* node = &dd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"dtor"}));
}

// The depth-first walk recurses into the `NameToken` (a real `Identifier` child) and the `Body`
// (a `BlockStatement` with a `ReturnStatement`) in document order.
TEST(CSharp_DestructorDeclaration, DepthFirstWalk) {
    auto dd = make_DestructorDeclaration();
    RecordingVisitor v;
    dd->AcceptVisitor(v);
    // dtor -> id:Foo (the NameToken) -> block (the Body) -> return (the Body's statement)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "dtor", "id:Foo", "block", "return",
    }));
}

// A `DestructorDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` in document order (the `Attributes` collection is
// the first slot, before `NameToken` and `Body`).
TEST(CSharp_DestructorDeclaration, DepthFirstWalkWithAttribute) {
    auto dd = make_DestructorDeclarationWithAttribute();
    RecordingVisitor v;
    dd->AcceptVisitor(v);
    // dtor -> attrsec (Attributes[0]) -> attr -> simple:Foo (the Attribute's Type) -> id:Foo
    //      -> id:Foo (the NameToken) -> block (the Body)
    ASSERT_EQ(v.trace.size(), 7u);
    EXPECT_EQ(v.trace[0], "dtor");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:Foo");
    EXPECT_EQ(v.trace[6], "block");
}

// ---- DoMatch (the generated pattern match) --------------------------------

// A `DestructorDeclaration` matches itself (same `Modifiers`, both `Body` present).
TEST(CSharp_DestructorDeclaration, DoMatchSameNode) {
    auto a = make_DestructorDeclaration();
    auto b = make_DestructorDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask; `Static` matches only `Static`, not `Static|Public`).
TEST(CSharp_DestructorDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_DestructorDeclaration();
    auto b = make_DestructorDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_DestructorDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_DestructorDeclaration();
    auto b = make_DestructorDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` mismatch rejects (the `MatchOptional` `Body` term -- both present match; one present +
// one absent reject). Here both have a `Body` (the holder builds one), so they match.
TEST(CSharp_DestructorDeclaration, DoMatchBodyBothPresentMatches) {
    auto a = make_DestructorDeclaration();
    auto b = make_DestructorDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` asymmetry rejects: a pattern with a `Body` vs a candidate without one.
TEST(CSharp_DestructorDeclaration, DoMatchBodyAsymmetryRejects) {
    auto a = make_DestructorDeclaration();  // has Body
    DestructorDeclaration b;  // no Body (empty ctor)
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    b.NameToken(tok.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

// A non-`DestructorDeclaration` candidate rejects (the type-check gate).
TEST(CSharp_DestructorDeclaration, DoMatchRejectsNonDestructor) {
    auto a = make_DestructorDeclaration();
    auto b = std::make_unique<WhileStatement>();  // a Statement, not a DestructorDeclaration
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_DestructorDeclaration, DoMatchRejectsNull) {
    auto a = make_DestructorDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `NameToken`/`Body`/`Attributes`, copies the `Modifiers` scalar, and does
// not detach the source.
TEST(CSharp_DestructorDeclaration, CloneDeepCopies) {
    auto a = make_DestructorDeclaration();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<DestructorDeclaration>(
        static_cast<DestructorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Destructor);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    EXPECT_EQ(clone->NameToken()->Name(), "Foo");
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), a->Body());
    // The source is not detached.
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
    EXPECT_EQ(a->Body()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `DestructorDeclaration*`.
TEST(CSharp_DestructorDeclaration, CloneVirtualAndCovariant) {
    auto a = make_DestructorDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<DestructorDeclaration*>(clone.get()), nullptr);
    // Covariant: `DestructorDeclaration::Clone` returns `DestructorDeclaration*` through the
    // `AstNode::Clone` virtual.
    auto cov = std::unique_ptr<DestructorDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_DestructorDeclaration, CloneCopiesAttributes) {
    auto a = make_DestructorDeclarationWithAttribute();
    auto clone = std::unique_ptr<DestructorDeclaration>(
        static_cast<DestructorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `NameToken` required slot is filled, the `Body`
// is nullable so its absence is invariant-valid).
TEST(CSharp_DestructorDeclaration, CheckInvariantPassesOnFilled) {
    auto dd = make_DestructorDeclaration();  // NameToken filled, Body present
    dd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NameToken` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_DestructorDeclaration, CheckInvariantRejectsEmpty) {
    DestructorDeclaration dd;  // no NameToken
    EXPECT_DEATH(dd.CheckInvariant(), "");
}
#endif
