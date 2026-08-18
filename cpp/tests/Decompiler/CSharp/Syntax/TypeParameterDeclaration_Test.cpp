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

// Tests for the `TypeParameterDeclaration` concrete node (cpp/.../Syntax/TypeParameterDeclaration.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/TypeParameterDeclaration.cs) -- the
// `type_parameter` node: an `Attributes` `AttributeSection` collection + a `Variance`
// `VarianceModifier` enum scalar + a non-nullable `string Name` (over a REQUIRED `NameToken`
// `Identifier`). The next in-order Phase-5 piece per the D281 plan (the dependency of
// `MethodDeclaration.TypeParameters`). The suite shares a `RecordingVisitor` and a `DoMatchAgainst`
// helper (the D234 pattern). Also tests the new `VarianceModifier` `TypeSystem` enum (the
// `SymbolKind` D271 / `ReferenceKind` D278 precedent).

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;

namespace {

// A recording depth-first visitor: overrides the `VisitTypeParameterDeclaration` under test (plus
// the `AttributeSection`/`Attribute`/`SimpleType`/`Identifier` of its `Attributes` collection and
// `NameToken`, and the `WhileStatement` used for the cross-type DoMatch rejection), recording a tag
// and recursing via `VisitChildren` (the inherited depth-first default). The trace is the visited
// nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitTypeParameterDeclaration(TypeParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-tpp>"); return; }
        trace.push_back("tpp:" + node->Name());
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

// A holder keeping a `TypeParameterDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes).
struct TppHolder {
    std::unique_ptr<TypeParameterDeclaration> tpp;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    TypeParameterDeclaration* get() const { return tpp.get(); }
    TypeParameterDeclaration* operator->() const { return tpp.get(); }
};

// Build a `TypeParameterDeclaration` with a `Name` `T` (no attributes). The holder keeps every
// node alive.
TppHolder make_TypeParameterDeclaration() {
    TppHolder h;
    h.tpp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("T")));
    h.tpp->NameToken(h.nameToken.get());
    return h;
}

// Build a `TypeParameterDeclaration` with a `Name` `T` and an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
TppHolder make_TypeParameterDeclarationWithAttribute() {
    TppHolder h;
    h.tpp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.tpp->Attributes().Add(h.attrSec.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("T")));
    h.tpp->NameToken(h.nameToken.get());
    return h;
}

} // namespace

// ==========================================================================
// VarianceModifier (the TypeSystem enum, the second ported TypeSystem enum
// consumed by a CSharp.Syntax scalar -- the SymbolKind D271 / ReferenceKind
// D278 precedent)
// ==========================================================================

// `Invariant` is the zero value (the C# default for an uninitialized `Variance` property).
TEST(CSharp_VarianceModifier, InvariantIsZero) {
    EXPECT_EQ(static_cast<std::uint8_t>(VarianceModifier::Invariant), 0);
}

// The last member `Contravariant` is value 2 (the 3 members Invariant .. Contravariant mirror the
// C# declaration order).
TEST(CSharp_VarianceModifier, ContravariantIsLast) {
    EXPECT_EQ(static_cast<std::uint8_t>(VarianceModifier::Contravariant), 2);
}

// The C# underlying type is `byte`; the port keeps `std::uint8_t`.
TEST(CSharp_VarianceModifier, IsByteSized) {
    EXPECT_EQ(sizeof(VarianceModifier), 1u);
    static_assert(std::is_enum_v<VarianceModifier>);
    static_assert(std::is_same_v<std::underlying_type_t<VarianceModifier>, std::uint8_t>);
}

// The members are distinct and in C# declaration order (Invariant=0, Covariant=1, Contravariant=2).
TEST(CSharp_VarianceModifier, MembersAreDistinctAndOrdered) {
    EXPECT_NE(static_cast<std::uint8_t>(VarianceModifier::Covariant),
        static_cast<std::uint8_t>(VarianceModifier::Contravariant));
    EXPECT_EQ(static_cast<std::uint8_t>(VarianceModifier::Invariant), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(VarianceModifier::Covariant), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(VarianceModifier::Contravariant), 2);
}

// `VarianceModifier` declares NO `Any` member (unlike the `BinaryOperatorType`/
// `AssignmentOperatorType`/`AccessorKind` enums), so the generator's `hasAny` path does NOT fire for
// `TypeParameterDeclaration.Variance` -- the `DoMatch` term is the plain `==`, NOT a wildcard.
TEST(CSharp_VarianceModifier, HasNoAnyMember) {
    // There is no `VarianceModifier::Any` -- this test documents that fact (the absence is what
    // makes the `Variance` DoMatch term a plain equality).
    SUCCEED();
}

// ==========================================================================
// TypeParameterDeclaration (the type-parameter node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `TypeParameterDeclaration` is `final` (the C# is `sealed`; the bare `[DecompilerAstNode]` has
// `hasPatternPlaceholder` default false, so NO `PatternPlaceholder` subclass is emitted -- the
// `NullReferenceExpression` D226 sealed-leaf precedent applied to a node with slots).
TEST(CSharp_TypeParameterDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<TypeParameterDeclaration>);
    EXPECT_FALSE(std::is_abstract_v<TypeParameterDeclaration>);
}

// `TypeParameterDeclaration` derives directly from `AstNode` (a type parameter is a structural node
// owned by a declaration's `TypeParameters` collection, NOT a member declaration); it is an
// `AstNode` but NOT a `Statement`/`Expression`/`AstType`/`EntityDeclaration`.
TEST(CSharp_TypeParameterDeclaration, IsAstNodeNotStatementNotExpressionNotAstTypeNotEntityDeclaration) {
    TypeParameterDeclaration tpp(std::string("T"));
    EXPECT_NE(dynamic_cast<AstNode*>(&tpp), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&tpp), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&tpp), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&tpp), nullptr);
    EXPECT_EQ(dynamic_cast<EntityDeclaration*>(&tpp), nullptr);
}

// ---- construction -------------------------------------------------------

// The hand-written `(string)` convenience ctor sets `Name` (creating a `NameToken`).
TEST(CSharp_TypeParameterDeclaration, StringCtorSetsName) {
    TypeParameterDeclaration tpp(std::string("T"));
    EXPECT_NE(tpp.NameToken(), nullptr);
    EXPECT_EQ(tpp.Name(), "T");
    EXPECT_EQ(tpp.NameToken()->Parent(), &tpp);
}

// The empty ctor leaves `NameToken` null and `Variance` at the default `Invariant`.
TEST(CSharp_TypeParameterDeclaration, EmptyCtor) {
    TypeParameterDeclaration tpp;
    EXPECT_EQ(tpp.NameToken(), nullptr);
    EXPECT_EQ(tpp.Variance(), VarianceModifier::Invariant);
    EXPECT_EQ(tpp.Attributes().Count(), 0);
}

// ---- The const keyword tokens --------------------------------------------

// The two const-string variance keyword tokens are part of the public API (the output visitor
// reads them) and port as `static constexpr const char*`.
TEST(CSharp_TypeParameterDeclaration, ConstKeywordTokens) {
    EXPECT_STREQ(TypeParameterDeclaration::OutVarianceKeyword, "out");
    EXPECT_STREQ(TypeParameterDeclaration::InVarianceKeyword, "in");
}

// ---- The Variance scalar property (NOT a [Slot], not a child slot) --------

// `Variance` defaults to `VarianceModifier::Invariant` (the C# default) and round-trips. NO name
// shadowing (the accessor name differs from the enum name, so no elaborated specifier is needed --
// the `ParameterDeclaration.ParameterModifier` D278 precedent).
TEST(CSharp_TypeParameterDeclaration, VarianceScalar) {
    TypeParameterDeclaration tpp(std::string("T"));
    EXPECT_EQ(tpp.Variance(), VarianceModifier::Invariant);
    tpp.Variance(VarianceModifier::Covariant);
    EXPECT_EQ(tpp.Variance(), VarianceModifier::Covariant);
    tpp.Variance(VarianceModifier::Contravariant);
    EXPECT_EQ(tpp.Variance(), VarianceModifier::Contravariant);
    tpp.Variance(VarianceModifier::Invariant);
    EXPECT_EQ(tpp.Variance(), VarianceModifier::Invariant);
}

// ---- The Attributes collection --------------------------------------------

// The `Attributes` collection is empty by default. An empty node reports `GetChildCount` 1 (the
// `NameToken` single slot counts even when absent -- the slot is required, but it still
// contributes 1 to the flattened count). Adding an `AttributeSection` re-parents it and grows the
// collection (non-incremental -- the `NameToken` trails the collection, so a mutation invalidates
// the parent's indices for a lazy `EnsureChildIndices` rebuild).
TEST(CSharp_TypeParameterDeclaration, AttributesCollection) {
    TypeParameterDeclaration tpp(std::string("T"));
    EXPECT_EQ(tpp.Attributes().Count(), 0);
    EXPECT_EQ(tpp.GetChildCount(), 1);
    auto attrType = std::make_unique<SimpleType>(std::string("Foo"));
    auto attr = std::make_unique<Attribute>(attrType.get());
    auto attrSec = std::make_unique<AttributeSection>(attr.get());
    tpp.Attributes().Add(attrSec.get());
    EXPECT_EQ(tpp.Attributes().Count(), 1);
    EXPECT_EQ(tpp.GetChildCount(), 2);
    EXPECT_EQ(attrSec->Parent(), &tpp);
}

// ---- The NameToken slot + Name string accessor ----------------------------

// `NameToken` defaults to null on the empty-ctor node, but the `(string)` ctor fills it. `Name()`
// derefs the token (a null token is a half-constructed node that would NRE in C#); the non-nullable
// `string` semantics mean `Name()` returns `std::string`, NOT `std::optional` (the
// `LabelStatement.Label` D259 / `MemberType.MemberName` D238 non-nullable precedent).
TEST(CSharp_TypeParameterDeclaration, NameAndNameToken) {
    TypeParameterDeclaration tpp;
    EXPECT_EQ(tpp.NameToken(), nullptr);
    tpp.Name(std::string("T"));
    EXPECT_NE(tpp.NameToken(), nullptr);
    EXPECT_EQ(tpp.Name(), "T");
    EXPECT_EQ(tpp.NameToken()->Parent(), &tpp);
}

// Setting a name via the `NameToken` slot directly (passing a created `Identifier`) re-parents the
// token. Clearing with null detaches the old token.
TEST(CSharp_TypeParameterDeclaration, NameTokenSetter) {
    TypeParameterDeclaration tpp(std::string("T"));
    auto oldToken = tpp.NameToken();
    auto newToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("U")));
    tpp.NameToken(newToken.get());
    EXPECT_EQ(tpp.NameToken(), newToken.get());
    EXPECT_EQ(tpp.Name(), "U");
    EXPECT_EQ(newToken->Parent(), &tpp);
    EXPECT_EQ(oldToken->Parent(), nullptr);
    tpp.NameToken(nullptr);
    EXPECT_EQ(tpp.NameToken(), nullptr);
    EXPECT_EQ(newToken->Parent(), nullptr);
}

// An empty name is a REAL empty match, NOT a wildcard: `Identifier::Create` (not `CreateIfNotEmpty`)
// creates a token with an empty `Name` for an empty string (the non-nullable behaviour -- two empty
// names match, an empty name does not match a non-empty name -- the `LabelStatement.Label` D259 /
// `IdentifierExpression.Identifier` D246 precedent).
TEST(CSharp_TypeParameterDeclaration, EmptyNameIsNotEmptyToken) {
    TypeParameterDeclaration tpp;
    tpp.Name(std::string(""));
    EXPECT_NE(tpp.NameToken(), nullptr);
    EXPECT_EQ(tpp.Name(), "");
}

// ---- Slot storage (the collection -> single dispatch) ---------------------

// `GetChild`/`GetChildSlotInfo` walk the collection then the single; the valid index for an
// empty-collection node is 0=`NameToken`; out-of-range throws.
TEST(CSharp_TypeParameterDeclaration, SlotStorageGetChildAndGetChildSlotInfo) {
    TppHolder h = make_TypeParameterDeclaration();
    EXPECT_EQ(h->GetChildCount(), 1);
    EXPECT_EQ(h->GetChild(0), h.nameToken.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &TypeParameterDeclaration::NameTokenSlot);
    EXPECT_THROW(h->GetChild(1), std::out_of_range);
    EXPECT_THROW(h->GetChildSlotInfo(1), std::out_of_range);
}

// With an attribute, the flattened layout is `Attributes [0,1)`, `NameToken` at 1; `GetChildSlotInfo`
// returns the `AttributesSlot` for the collection range.
TEST(CSharp_TypeParameterDeclaration, SlotStorageWithAttribute) {
    TppHolder h = make_TypeParameterDeclarationWithAttribute();
    EXPECT_EQ(h->GetChildCount(), 2);
    EXPECT_EQ(h->GetChild(0), h.attrSec.get());
    EXPECT_EQ(h->GetChild(1), h.nameToken.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &TypeParameterDeclaration::AttributesSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &TypeParameterDeclaration::NameTokenSlot);
}

// `SetChild` replaces the `NameToken` single slot in place (the index computed by walking the
// slots -- 0 for an empty collection, 1 with an attribute).
TEST(CSharp_TypeParameterDeclaration, SetChildReplacesNameToken) {
    TppHolder h = make_TypeParameterDeclaration();
    auto newToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("U")));
    auto* oldToken = h.nameToken.get();
    h->SetChild(0, newToken.get());
    EXPECT_EQ(h->NameToken(), newToken.get());
    EXPECT_EQ(newToken->Parent(), h.get());
    EXPECT_EQ(oldToken->Parent(), nullptr);
}

// `SetChild` on a collection index replaces the collection element in place.
TEST(CSharp_TypeParameterDeclaration, SetChildReplacesCollectionElement) {
    TppHolder h = make_TypeParameterDeclarationWithAttribute();
    auto newAttrType = std::make_unique<SimpleType>(std::string("Bar"));
    auto newAttr = std::make_unique<Attribute>(newAttrType.get());
    auto newAttrSec = std::make_unique<AttributeSection>(newAttr.get());
    auto* oldAttrSec = h.attrSec.get();
    h->SetChild(0, newAttrSec.get());
    EXPECT_EQ(h->Attributes().At(0), newAttrSec.get());
    EXPECT_EQ(newAttrSec->Parent(), h.get());
    EXPECT_EQ(oldAttrSec->Parent(), nullptr);
}

// `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind and null
// for other kinds.
TEST(CSharp_TypeParameterDeclaration, GetCollectionByKind) {
    TppHolder h = make_TypeParameterDeclarationWithAttribute();
    EXPECT_EQ(h->GetCollectionByKind(&Slots::AttributeSection), &h->Attributes());
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Type), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

// The slot statics point at the already-ported shared `Slots` kinds; the cross-element-type
// distinctness check casts both operands to the common `const CSharpSlotInfo*` base (the D251/D252
// EXPECT_NE cross-element-type crux).
TEST(CSharp_TypeParameterDeclaration, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(TypeParameterDeclaration::AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(TypeParameterDeclaration::NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_TRUE(TypeParameterDeclaration::AttributesSlot.IsCollection());
    EXPECT_FALSE(TypeParameterDeclaration::NameTokenSlot.IsCollection());
    // The `NameToken` slot is REQUIRED (non-nullable name -- IsOptional false).
    EXPECT_FALSE(TypeParameterDeclaration::NameTokenSlot.IsOptional());

    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&TypeParameterDeclaration::AttributesSlot),
        static_cast<const CSharpSlotInfo*>(&TypeParameterDeclaration::NameTokenSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

// `AcceptVisitor` dispatches to `VisitTypeParameterDeclaration` (the recording visitor records the
// tag). A bare node (no `NameToken`) has `Children()` empty (the single slot is null), so
// `VisitChildren` yields just the node tag -- but `Name()` derefs the null token (UB), so the test
// uses a filled node's `NameToken` for the tag and exercises an empty-node walk via a directly-set
// null `NameToken` only where `Name()` is not called. Here a nameless node is built via the empty
// ctor and the tag is recorded by the visitor (which calls `Name()` -- so give it a name).
TEST(CSharp_TypeParameterDeclaration, AcceptVisitorDispatch) {
    TypeParameterDeclaration tpp(std::string("T"));
    RecordingVisitor v;
    tpp.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "tpp:T");
    EXPECT_EQ(v.trace[1], "id:T");
}

// The dispatch is virtual through `AstNode*` (a `TypeParameterDeclaration*` upcast to `AstNode*`
// still routes to `VisitTypeParameterDeclaration`).
TEST(CSharp_TypeParameterDeclaration, AcceptVisitorIsVirtual) {
    TypeParameterDeclaration tpp(std::string("T"));
    AstNode* node = &tpp;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "tpp:T");
    EXPECT_EQ(v.trace[1], "id:T");
}

// ---- The depth-first walk ------------------------------------------------

// The depth-first walk over a filled type parameter (no attributes) visits the `NameToken`
// `Identifier` `T` (the backing token is a visited child -- the `MemberType.MemberName` D238
// recurring gotcha).
TEST(CSharp_TypeParameterDeclaration, DepthFirstWalkNoAttributes) {
    TppHolder h = make_TypeParameterDeclaration();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "tpp:T");
    EXPECT_EQ(v.trace[1], "id:T");
}

// The depth-first walk over a type parameter with an attribute visits the `AttributeSection` (which
// recurses into its `Attribute` -> `SimpleType` `Foo` -> `Identifier` `Foo`) first, then the
// `NameToken` `Identifier` `T`.
TEST(CSharp_TypeParameterDeclaration, DepthFirstWalkWithAttribute) {
    TppHolder h = make_TypeParameterDeclarationWithAttribute();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 6u);
    EXPECT_EQ(v.trace[0], "tpp:T");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:T");
}

// ---- DoMatch ---------------------------------------------------------------

// A type parameter matches an identical type parameter (all terms pass).
TEST(CSharp_TypeParameterDeclaration, DoMatchSame) {
    TppHolder a = make_TypeParameterDeclaration();
    TppHolder b = make_TypeParameterDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Variance` mismatch rejects (the plain-equality term, NOT a wildcard -- `VarianceModifier` has
// no `Any` member, so an `Invariant` pattern matches only an `Invariant` candidate).
TEST(CSharp_TypeParameterDeclaration, DoMatchVarianceMismatch) {
    TppHolder a = make_TypeParameterDeclaration();
    TppHolder b = make_TypeParameterDeclaration();
    b->Variance(VarianceModifier::Covariant);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `MatchString` term).
TEST(CSharp_TypeParameterDeclaration, DoMatchNameMismatch) {
    TppHolder a = make_TypeParameterDeclaration();
    TppHolder b = make_TypeParameterDeclaration();
    b->Name(std::string("U"));
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `Attributes` mismatch rejects (the collection `DoMatch` term).
TEST(CSharp_TypeParameterDeclaration, DoMatchAttributesMismatch) {
    TppHolder a = make_TypeParameterDeclaration();
    TppHolder b = make_TypeParameterDeclarationWithAttribute();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Two empty names match (the `MatchString("", "")` path -- a real empty match, the non-nullable
// behaviour).
TEST(CSharp_TypeParameterDeclaration, DoMatchBothEmptyNames) {
    TypeParameterDeclaration a;
    a.Name(std::string(""));
    TypeParameterDeclaration b;
    b.Name(std::string(""));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// An empty name does not match a non-empty name.
TEST(CSharp_TypeParameterDeclaration, DoMatchEmptyNameDoesNotMatchNonEmpty) {
    TypeParameterDeclaration a;
    a.Name(std::string(""));
    TppHolder b = make_TypeParameterDeclaration();  // Name "T"
    EXPECT_FALSE(DoMatchAgainst(&a, b.get()));
}

// The `$any$` wildcard in the pattern's `Name` matches any candidate name (the `AnyString`
// wildcard, the `GotoStatement` D257 / `ParameterDeclaration` D278 precedent). The `Variance` and
// `Attributes` terms must also match (an `Invariant` pattern matches an `Invariant` candidate, and
// both have no attributes), so the pattern is a full-structured holder with the `Name` overridden
// to the wildcard.
TEST(CSharp_TypeParameterDeclaration, DoMatchAnyStringWildcard) {
    TppHolder pattern = make_TypeParameterDeclaration();
    pattern->Name(std::string(Pattern::AnyString));
    TppHolder b = make_TypeParameterDeclaration();
    EXPECT_TRUE(DoMatchAgainst(pattern.get(), b.get()));
}

// A non-`TypeParameterDeclaration` candidate rejects early (the type-check gate). A
// `WhileStatement` is an unrelated `Statement` node.
TEST(CSharp_TypeParameterDeclaration, DoMatchRejectsNonTypeParameterCandidate) {
    TppHolder a = make_TypeParameterDeclaration();
    WhileStatement ws;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &ws));
}

// A null candidate rejects (the type-check gate, `dynamic_cast` yields null).
TEST(CSharp_TypeParameterDeclaration, DoMatchRejectsNull) {
    TppHolder a = make_TypeParameterDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ----------------------------------------------------------------

// `Clone` deep-copies the `NameToken` and `Attributes`, copies the `Variance` scalar, and does not
// detach the source's children.
TEST(CSharp_TypeParameterDeclaration, CloneDeepCopies) {
    TppHolder h = make_TypeParameterDeclarationWithAttribute();
    h->Variance(VarianceModifier::Covariant);
    std::unique_ptr<TypeParameterDeclaration> clone(h->Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), h.get());
    // Scalar copied.
    EXPECT_EQ(clone->Variance(), VarianceModifier::Covariant);
    // `NameToken` deep-copied (distinct pointer, same name).
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), h.nameToken.get());
    EXPECT_EQ(clone->Name(), "T");
    // `Attributes` deep-copied (distinct pointer).
    EXPECT_EQ(clone->Attributes().Count(), 1);
    EXPECT_NE(clone->Attributes().At(0), h.attrSec.get());
    // The source's children are NOT detached (still parented to the source).
    EXPECT_EQ(h.nameToken->Parent(), h.get());
    EXPECT_EQ(h.attrSec->Parent(), h.get());
    // The clone's children are parented to the clone.
    EXPECT_EQ(clone->NameToken()->Parent(), clone.get());
    EXPECT_EQ(clone->Attributes().At(0)->Parent(), clone.get());
}

// `Clone` is virtual through `AstNode*` and returns a `TypeParameterDeclaration*` (covariant).
TEST(CSharp_TypeParameterDeclaration, CloneIsVirtualAndCovariant) {
    TppHolder h = make_TypeParameterDeclaration();
    AstNode* node = h.get();
    AstNode* clone = node->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<TypeParameterDeclaration*>(clone), nullptr);
    delete clone;
}

// `Clone` of a minimal node (a name only) copies the name and yields an empty `Attributes`
// collection and the default `Variance`.
TEST(CSharp_TypeParameterDeclaration, CloneMinimal) {
    TppHolder h = make_TypeParameterDeclaration();
    std::unique_ptr<TypeParameterDeclaration> clone(h->Clone());
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_EQ(clone->Name(), "T");
    EXPECT_EQ(clone->Attributes().Count(), 0);
    EXPECT_EQ(clone->Variance(), VarianceModifier::Invariant);
}

// `Clone` of an empty node (no `NameToken`) yields a null `NameToken` and the default `Variance`.
TEST(CSharp_TypeParameterDeclaration, CloneEmpty) {
    TypeParameterDeclaration tpp;
    std::unique_ptr<TypeParameterDeclaration> clone(tpp.Clone());
    EXPECT_EQ(clone->NameToken(), nullptr);
    EXPECT_EQ(clone->Attributes().Count(), 0);
    EXPECT_EQ(clone->Variance(), VarianceModifier::Invariant);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `NameToken` required slot is filled).
TEST(CSharp_TypeParameterDeclaration, CheckInvariantPassesOnFilled) {
    TppHolder h = make_TypeParameterDeclaration();
    h->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NameToken` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug, the
// `LabelStatement` D259 / `FieldDeclaration` D273 required-token precedent).
#ifndef NDEBUG
TEST(CSharp_TypeParameterDeclaration, CheckInvariantRejectsEmpty) {
    TypeParameterDeclaration tpp;  // no NameToken
    EXPECT_DEATH(tpp.CheckInvariant(), "");
}
#endif
