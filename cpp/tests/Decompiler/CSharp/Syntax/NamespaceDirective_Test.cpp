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

// Tests for the namespace-level directive family (cpp/.../Syntax/ExternAliasDeclaration.hpp,
// UsingDeclaration.hpp, UsingAliasDeclaration.hpp -- the ports of
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/ExternAliasDeclaration.cs,
// UsingDeclaration.cs, UsingAliasDeclaration.cs) -- the next in-order Phase-5 piece per the
// D288 plan (the remaining GeneralScope nodes). Three sealed `AstNode`s deriving directly from
// the `AstNode` root (the `VariableInitializer` D266 / `CatchClause` D269 direct-`AstNode`
// precedent): `ExternAliasDeclaration` (the `LabelStatement` D259 one-REQUIRED-string-name-
// `[Slot("Identifier")]` shape, reusing `Slots::Identifier`), `UsingDeclaration` (the
// `TypeReferenceExpression` D245 one-required-`AstType`-slot shape, adding the new
// `Slots::Import` kind), and `UsingAliasDeclaration` (the `VariableInitializer` D266
// two-single-slot shape with the second slot an `AstType`, adding the new `Slots::Alias` kind
// and reusing `Slots::Import`). The `AcceptVisitor` dispatch, the generated `DoMatch`, and the
// per-concrete-node `Clone`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/ExternAliasDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/UsingAliasDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/UsingDeclaration.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete node types (the directive node, the backing `Identifier` token of the
// string-name leaf, and the `SimpleType` import), recursing via the inherited `VisitChildren`
// (the document-order walk).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitExternAliasDeclaration(ExternAliasDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-extern>"); return; }
        trace.push_back("extern:" + node->Name());
        VisitChildren(node);
    }
    void VisitUsingDeclaration(UsingDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-using>"); return; }
        trace.push_back("using");
        VisitChildren(node);
    }
    void VisitUsingAliasDeclaration(UsingAliasDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-using-alias>"); return; }
        trace.push_back("using-alias:" + node->Alias());
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-simple>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or(""));
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// =========================================================================
// ExternAliasDeclaration
// =========================================================================

// ---- Construction ------------------------------------------------------

// The default `ExternAliasDeclaration()` has a null `NameToken` (the token is a REQUIRED slot,
// so a default-constructed node is half-constructed -- UB to deref `Name()` or pass to
// `DoMatch`).
TEST(CSharp_ExternAliasDeclaration, DefaultCtorHasNullNameToken) {
    ExternAliasDeclaration e;
    EXPECT_EQ(e.NameToken(), nullptr);
}

// The generated `(string)` ctor sets the `Name` (creating the backing `NameToken`).
TEST(CSharp_ExternAliasDeclaration, StringCtorSetsName) {
    ExternAliasDeclaration e{std::string("System")};
    EXPECT_EQ(e.Name(), "System");
    EXPECT_NE(e.NameToken(), nullptr);
    EXPECT_EQ(e.NameToken()->Name(), "System");
}

// ---- Name/NameToken accessors ------------------------------------------

// `NameToken` gets/sets the backing `Identifier` token (re-parents and re-indexes).
TEST(CSharp_ExternAliasDeclaration, NameTokenGetterSetter) {
    ExternAliasDeclaration e;
    EXPECT_EQ(e.NameToken(), nullptr);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("X")));
    e.NameToken(tok.get());
    EXPECT_EQ(e.NameToken(), tok.get());
    EXPECT_EQ(e.NameToken()->Parent(), &e);
    EXPECT_EQ(e.NameToken()->ChildIndex, 0);
    e.NameToken(nullptr);
    EXPECT_EQ(e.NameToken(), nullptr);
}

// `Name` gets/sets the convenience string over the `NameToken` (creating a token even for an
// empty string -- the non-nullable behaviour).
TEST(CSharp_ExternAliasDeclaration, NameGetterSetter) {
    ExternAliasDeclaration e;
    e.Name(std::string("Collections"));
    EXPECT_EQ(e.Name(), "Collections");
    EXPECT_NE(e.NameToken(), nullptr);
    // An empty name yields a token with an empty `Name`, NOT a null token (the non-nullable
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 precedent).
    e.Name(std::string(""));
    EXPECT_NE(e.NameToken(), nullptr);
    EXPECT_EQ(e.Name(), "");
    EXPECT_EQ(e.NameToken()->Name(), "");
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitExternAliasDeclaration`.
TEST(CSharp_ExternAliasDeclaration, AcceptVisitorDispatches) {
    ExternAliasDeclaration e{std::string("System")};
    RecordingVisitor v;
    e.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"extern:System", "id:System"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override.
TEST(CSharp_ExternAliasDeclaration, AcceptVisitorIsVirtualThroughAstNode) {
    auto e = std::make_unique<ExternAliasDeclaration>(std::string("X"));
    AstNode* asAst = e.get();
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"extern:X", "id:X"}));
}

// The depth-first walk recurses into the backing `NameToken` (a real `[Slot]` child at
// flattened index 0 -- the D259 recurring 'a string-name `[Slot]` backing token is a visited
// child' gotcha).
TEST(CSharp_ExternAliasDeclaration, DepthFirstWalkRecordsNodeAndToken) {
    auto e = std::make_unique<ExternAliasDeclaration>(std::string("Y"));
    RecordingVisitor v;
    e->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"extern:Y", "id:Y"}));
}

// ---- is-a (AstNode, not Expression/Statement/AstType) ----------------

// `ExternAliasDeclaration` is an `AstNode` but NOT an `Expression`, `Statement`, or `AstType`
// (it derives directly from `AstNode`). It is `final` (the C# `sealed`).
TEST(CSharp_ExternAliasDeclaration, IsAstNodeButNotExpressionStatementOrAstType) {
    auto e = std::make_unique<ExternAliasDeclaration>();
    EXPECT_NE(dynamic_cast<AstNode*>(e.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(e.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(e.get()), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(e.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<ExternAliasDeclaration>);
}

// ---- Slot storage -----------------------------------------------------

// One single REQUIRED `Identifier` slot at flattened index 0; `GetChildCount` is 1 (the slot
// counts even when the token is absent).
TEST(CSharp_ExternAliasDeclaration, SlotStorageContract) {
    ExternAliasDeclaration e;
    EXPECT_EQ(e.GetChildCount(), 1);
    EXPECT_EQ(e.GetChild(0), nullptr);
    EXPECT_EQ(e.GetChildSlotInfo(0), &ExternAliasDeclaration::NameTokenSlot);
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Z")));
    e.SetChild(0, tok.get());
    EXPECT_EQ(e.GetChild(0), tok.get());
    EXPECT_THROW(e.SetChild(1, tok.get()), std::out_of_range);
}

// The per-node `NameTokenSlot` points at the shared `Slots::Identifier` kind (already ported by
// `SimpleType` D237 -- no new `Slots` constant).
TEST(CSharp_ExternAliasDeclaration, SlotKindPointsAtSharedIdentifier) {
    EXPECT_EQ(ExternAliasDeclaration::NameTokenSlot.Kind(), &Slots::Identifier);
}

// The slot's `IsInstanceOfType` is-a test accepts an `Identifier` (the element type).
TEST(CSharp_ExternAliasDeclaration, SlotIsInstanceOfTypeAcceptsIdentifier) {
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("W")));
    EXPECT_TRUE(ExternAliasDeclaration::NameTokenSlot.IsInstanceOfType(tok.get()));
    // A non-Identifier AstNode is rejected.
    NullReferenceExpression nre;
    EXPECT_FALSE(ExternAliasDeclaration::NameTokenSlot.IsInstanceOfType(&nre));
}

// ---- DoMatch (the generated MatchString match) ------------------------

// Two `ExternAliasDeclaration`s with the same `Name` match.
TEST(CSharp_ExternAliasDeclaration, DoMatchMatchesSameName) {
    ExternAliasDeclaration a{std::string("System")};
    ExternAliasDeclaration b{std::string("System")};
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Two `ExternAliasDeclaration`s with different `Name`s do not match (the `MatchString` term
// rejects).
TEST(CSharp_ExternAliasDeclaration, DoMatchRejectsDifferentName) {
    ExternAliasDeclaration a{std::string("System")};
    ExternAliasDeclaration b{std::string("Collections")};
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// The `$any$` wildcard (`Pattern::AnyString`) in the pattern's `Name` matches any candidate
// name (the `MatchString` wildcard path).
TEST(CSharp_ExternAliasDeclaration, DoMatchAnyStringWildcardMatchesAnyName) {
    ExternAliasDeclaration pattern{std::string(Pattern::AnyString)};
    ExternAliasDeclaration candA{std::string("anything")};
    ExternAliasDeclaration candB{std::string("")};
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candA));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candB));
}

// Two empty-name `ExternAliasDeclaration`s match (two empty names match).
TEST(CSharp_ExternAliasDeclaration, DoMatchEmptyNameMatchesEmptyName) {
    ExternAliasDeclaration a{std::string("")};
    ExternAliasDeclaration b{std::string("")};
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// An empty name does not match a non-empty name.
TEST(CSharp_ExternAliasDeclaration, DoMatchEmptyNameDoesNotMatchNonEmpty) {
    ExternAliasDeclaration a{std::string("")};
    ExternAliasDeclaration b{std::string("X")};
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// An `ExternAliasDeclaration` does not match a different concrete type (the
// `other is ExternAliasDeclaration` gate).
TEST(CSharp_ExternAliasDeclaration, DoMatchRejectsDifferentType) {
    ExternAliasDeclaration e{std::string("X")};
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&e, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &e));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_ExternAliasDeclaration, DoMatchRejectsNullCandidate) {
    ExternAliasDeclaration e{std::string("X")};
    EXPECT_FALSE(DoMatchAgainst(&e, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` copies the `NameToken` (deep-copy, re-parented) and detaches.
TEST(CSharp_ExternAliasDeclaration, CloneCopiesNameTokenAndDetaches) {
    auto original = std::make_unique<ExternAliasDeclaration>(std::string("System"));
    std::unique_ptr<ExternAliasDeclaration> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Name(), "System");
    EXPECT_NE(copy->NameToken(), original->NameToken());
    EXPECT_EQ(copy->NameToken()->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*`.
TEST(CSharp_ExternAliasDeclaration, CloneIsVirtualThroughAstNode) {
    auto original = std::make_unique<ExternAliasDeclaration>(std::string("X"));
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<ExternAliasDeclaration*>(copy.get()), nullptr);
    EXPECT_EQ(static_cast<ExternAliasDeclaration*>(copy.get())->Name(), "X");
}

// `Clone` is a deep copy (mutating the copy does not affect the original).
TEST(CSharp_ExternAliasDeclaration, CloneIsDeepCopy) {
    auto original = std::make_unique<ExternAliasDeclaration>(std::string("orig"));
    std::unique_ptr<ExternAliasDeclaration> copy(original->Clone());
    copy->Name(std::string("copy"));
    EXPECT_EQ(original->Name(), "orig");
    EXPECT_EQ(copy->Name(), "copy");
}

// ---- CheckInvariant ---------------------------------------------------

// A default-constructed (empty) node violates the required-`NameToken` invariant (the assert
// fires in debug).
TEST(CSharp_ExternAliasDeclaration, CheckInvariantRejectsEmptyNode) {
    ExternAliasDeclaration e;
    EXPECT_DEBUG_DEATH(e.CheckInvariant(), "");
}

// A filled node passes `CheckInvariant`.
TEST(CSharp_ExternAliasDeclaration, CheckInvariantPassesOnFilled) {
    auto e = std::make_unique<ExternAliasDeclaration>(std::string("System"));
    e->CheckInvariant();
}

// =========================================================================
// UsingDeclaration
// =========================================================================

// ---- Construction ------------------------------------------------------

// The default `UsingDeclaration()` has a null `Import` (the slot is REQUIRED, so a
// default-constructed node is half-constructed).
TEST(CSharp_UsingDeclaration, DefaultCtorHasNullImport) {
    UsingDeclaration u;
    EXPECT_EQ(u.Import(), nullptr);
}

// The generated `(AstType)` ctor sets the `Import`.
TEST(CSharp_UsingDeclaration, AstTypeCtorSetsImport) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingDeclaration u(st.get());
    EXPECT_EQ(u.Import(), st.get());
    EXPECT_EQ(u.Import()->Parent(), &u);
}

// ---- UsingKeyword const -----------------------------------------------

// The `UsingKeyword` const string is `"using"` (shared with `UsingAliasDeclaration`).
TEST(CSharp_UsingDeclaration, UsingKeywordConst) {
    EXPECT_STREQ(UsingDeclaration::UsingKeyword, "using");
    EXPECT_EQ(UsingDeclaration::UsingKeyword, UsingAliasDeclaration::UsingKeyword);
}

// ---- Import accessor --------------------------------------------------

// `Import` gets/sets the `AstType` child (re-parents and re-indexes).
TEST(CSharp_UsingDeclaration, ImportGetterSetter) {
    UsingDeclaration u;
    EXPECT_EQ(u.Import(), nullptr);
    auto st = std::make_unique<SimpleType>(std::string("System"));
    u.Import(st.get());
    EXPECT_EQ(u.Import(), st.get());
    EXPECT_EQ(u.Import()->Parent(), &u);
    EXPECT_EQ(u.Import()->ChildIndex, 0);
    u.Import(nullptr);
    EXPECT_EQ(u.Import(), nullptr);
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitUsingDeclaration`.
TEST(CSharp_UsingDeclaration, AcceptVisitorDispatches) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingDeclaration u(st.get());
    RecordingVisitor v;
    u.AcceptVisitor(v);
    // The walk recurses into the `Import` `SimpleType`, which recurses into its backing
    // `IdentifierToken` (the D259 recurring 'a string-name `[Slot]` backing token is a visited
    // child' gotcha applied to the `SimpleType` import).
    EXPECT_EQ(v.trace, (std::vector<std::string>{"using", "simple:System", "id:System"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override.
TEST(CSharp_UsingDeclaration, AcceptVisitorIsVirtualThroughAstNode) {
    auto st = std::make_unique<SimpleType>(std::string("IO"));
    auto u = std::make_unique<UsingDeclaration>(st.get());
    AstNode* asAst = u.get();
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"using", "simple:IO", "id:IO"}));
}

// The depth-first walk recurses into the `Import` (a `SimpleType`, which recurses into its
// backing `IdentifierToken`).
TEST(CSharp_UsingDeclaration, DepthFirstWalkRecordsNodeAndImport) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto u = std::make_unique<UsingDeclaration>(st.get());
    RecordingVisitor v;
    u->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"using", "simple:System", "id:System"}));
}

// ---- is-a (AstNode, not Expression/Statement/AstType) ----------------

// `UsingDeclaration` is an `AstNode` but NOT an `Expression`, `Statement`, or `AstType`. It is
// `final`.
TEST(CSharp_UsingDeclaration, IsAstNodeButNotExpressionStatementOrAstType) {
    auto u = std::make_unique<UsingDeclaration>();
    EXPECT_NE(dynamic_cast<AstNode*>(u.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(u.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(u.get()), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(u.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<UsingDeclaration>);
}

// ---- Slot storage -----------------------------------------------------

// One single REQUIRED `AstType` slot at flattened index 0.
TEST(CSharp_UsingDeclaration, SlotStorageContract) {
    UsingDeclaration u;
    EXPECT_EQ(u.GetChildCount(), 1);
    EXPECT_EQ(u.GetChild(0), nullptr);
    EXPECT_EQ(u.GetChildSlotInfo(0), &UsingDeclaration::ImportSlot);
    EXPECT_THROW(u.GetChild(1), std::out_of_range);
    auto st = std::make_unique<SimpleType>(std::string("Z"));
    u.SetChild(0, st.get());
    EXPECT_EQ(u.GetChild(0), st.get());
    EXPECT_THROW(u.SetChild(1, st.get()), std::out_of_range);
}

// The per-node `ImportSlot` points at the new `Slots::Import` kind (shared with
// `UsingAliasDeclaration.Import`).
TEST(CSharp_UsingDeclaration, SlotKindPointsAtSharedImport) {
    EXPECT_EQ(UsingDeclaration::ImportSlot.Kind(), &Slots::Import);
    EXPECT_EQ(UsingAliasDeclaration::ImportSlot.Kind(), &Slots::Import);
}

// The slot's `IsInstanceOfType` is-a test accepts an `AstType` (e.g. a `SimpleType`).
TEST(CSharp_UsingDeclaration, SlotIsInstanceOfTypeAcceptsAstType) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    EXPECT_TRUE(UsingDeclaration::ImportSlot.IsInstanceOfType(st.get()));
    // A non-`AstType` `AstNode` is rejected.
    NullReferenceExpression nre;
    EXPECT_FALSE(UsingDeclaration::ImportSlot.IsInstanceOfType(&nre));
}

// ---- DoMatch (the generated MatchRequired match) ---------------------

// Two `UsingDeclaration`s with the same `Import` shape match.
TEST(CSharp_UsingDeclaration, DoMatchMatchesSameImport) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("System"));
    UsingDeclaration a(stA.get());
    UsingDeclaration b(stB.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Two `UsingDeclaration`s with different `Import` types do not match (the `MatchRequired` term
// rejects via the `SimpleType` `DoMatch`).
TEST(CSharp_UsingDeclaration, DoMatchRejectsDifferentImport) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("Collections"));
    UsingDeclaration a(stA.get());
    UsingDeclaration b(stB.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `UsingDeclaration` does not match a different concrete type (the
// `other is UsingDeclaration` gate).
TEST(CSharp_UsingDeclaration, DoMatchRejectsDifferentType) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingDeclaration u(st.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&u, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &u));
}

// A null candidate is rejected.
TEST(CSharp_UsingDeclaration, DoMatchRejectsNullCandidate) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingDeclaration u(st.get());
    EXPECT_FALSE(DoMatchAgainst(&u, nullptr));
}

// A missing pattern-side `Import` (a default-constructed pattern) is rejected defensively
// (the C# would null-deref; the port's `MatchRequired` guards it).
TEST(CSharp_UsingDeclaration, DoMatchRejectsNullPatternImport) {
    UsingDeclaration pattern;  // no Import
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingDeclaration cand(st.get());
    EXPECT_FALSE(DoMatchAgainst(&pattern, &cand));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` copies the `Import` (deep-copy, re-parented) and detaches.
TEST(CSharp_UsingDeclaration, CloneCopiesImportAndDetaches) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto original = std::make_unique<UsingDeclaration>(st.get());
    std::unique_ptr<UsingDeclaration> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Import(), nullptr);
    EXPECT_NE(copy->Import(), original->Import());
    EXPECT_EQ(copy->Import()->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*`.
TEST(CSharp_UsingDeclaration, CloneIsVirtualThroughAstNode) {
    auto st = std::make_unique<SimpleType>(std::string("X"));
    auto original = std::make_unique<UsingDeclaration>(st.get());
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<UsingDeclaration*>(copy.get()), nullptr);
    ASSERT_NE(static_cast<UsingDeclaration*>(copy.get())->Import(), nullptr);
}

// `Clone` is a deep copy (mutating the copy's `Import` does not affect the original).
TEST(CSharp_UsingDeclaration, CloneIsDeepCopy) {
    auto st = std::make_unique<SimpleType>(std::string("orig"));
    auto original = std::make_unique<UsingDeclaration>(st.get());
    std::unique_ptr<UsingDeclaration> copy(original->Clone());
    copy->Import(std::unique_ptr<SimpleType>(static_cast<SimpleType*>(
        std::unique_ptr<SimpleType>(std::make_unique<SimpleType>(std::string("copy"))).release()))->Clone());
    // The original's Import is unchanged.
    EXPECT_EQ(static_cast<SimpleType*>(original->Import())->Identifier().value_or(""), "orig");
}

// ---- CheckInvariant ---------------------------------------------------

// A default-constructed (empty) node violates the required-`Import` invariant.
TEST(CSharp_UsingDeclaration, CheckInvariantRejectsEmptyNode) {
    UsingDeclaration u;
    EXPECT_DEBUG_DEATH(u.CheckInvariant(), "");
}

// A filled node passes `CheckInvariant`.
TEST(CSharp_UsingDeclaration, CheckInvariantPassesOnFilled) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto u = std::make_unique<UsingDeclaration>(st.get());
    u->CheckInvariant();
}

// =========================================================================
// UsingAliasDeclaration
// =========================================================================

// ---- Construction ------------------------------------------------------

// The default `UsingAliasDeclaration()` has null `AliasToken` and `Import`.
TEST(CSharp_UsingAliasDeclaration, DefaultCtorHasNullSlots) {
    UsingAliasDeclaration u;
    EXPECT_EQ(u.AliasToken(), nullptr);
    EXPECT_EQ(u.Import(), nullptr);
}

// The generated `(string, AstType)` ctor sets both `Alias` and `Import`.
TEST(CSharp_UsingAliasDeclaration, StringAstTypeCtorSetsBoth) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration u{std::string("A"), st.get()};
    EXPECT_EQ(u.Alias(), "A");
    EXPECT_NE(u.AliasToken(), nullptr);
    EXPECT_EQ(u.AliasToken()->Name(), "A");
    EXPECT_EQ(u.Import(), st.get());
    EXPECT_EQ(u.Import()->Parent(), &u);
}

// The hand-written `(string, string)` ctor sets both `Alias` and `Import` (via
// `Identifier.Create` + `new SimpleType`).
TEST(CSharp_UsingAliasDeclaration, StringStringCtorSetsBoth) {
    UsingAliasDeclaration u{std::string("A"), std::string("System")};
    EXPECT_EQ(u.Alias(), "A");
    EXPECT_NE(u.AliasToken(), nullptr);
    EXPECT_EQ(u.AliasToken()->Name(), "A");
    ASSERT_NE(u.Import(), nullptr);
    EXPECT_EQ(u.Import()->Parent(), &u);
    EXPECT_EQ(static_cast<SimpleType*>(u.Import())->Identifier().value_or(""), "System");
}

// The `(string, AstType)` and `(string, string)` ctors overload without ambiguity: passing a
// `SimpleType*` (upcasting to `AstType*`) selects the `(string, AstType)` ctor; passing a
// `std::string` selects the `(string, string)` ctor.
TEST(CSharp_UsingAliasDeclaration, TwoCtorOverloadDisambiguation) {
    auto st = std::make_unique<SimpleType>(std::string("IO"));
    UsingAliasDeclaration viaAstType{std::string("A"), st.get()};
    EXPECT_EQ(viaAstType.Alias(), "A");
    EXPECT_EQ(static_cast<SimpleType*>(viaAstType.Import())->Identifier().value_or(""), "IO");

    UsingAliasDeclaration viaString{std::string("A"), std::string("IO")};
    EXPECT_EQ(viaString.Alias(), "A");
    EXPECT_EQ(static_cast<SimpleType*>(viaString.Import())->Identifier().value_or(""), "IO");
}

// ---- Alias/AliasToken/Import accessors --------------------------------

// `AliasToken` gets/sets the backing `Identifier` token.
TEST(CSharp_UsingAliasDeclaration, AliasTokenGetterSetter) {
    UsingAliasDeclaration u;
    EXPECT_EQ(u.AliasToken(), nullptr);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    u.AliasToken(tok.get());
    EXPECT_EQ(u.AliasToken(), tok.get());
    EXPECT_EQ(u.AliasToken()->Parent(), &u);
    EXPECT_EQ(u.AliasToken()->ChildIndex, 0);
    u.AliasToken(nullptr);
    EXPECT_EQ(u.AliasToken(), nullptr);
}

// `Alias` gets/sets the convenience string over the `AliasToken` (creating a token even for an
// empty string -- the non-nullable behaviour).
TEST(CSharp_UsingAliasDeclaration, AliasGetterSetter) {
    UsingAliasDeclaration u;
    u.Alias(std::string("MyAlias"));
    EXPECT_EQ(u.Alias(), "MyAlias");
    EXPECT_NE(u.AliasToken(), nullptr);
    // An empty alias yields a token with an empty `Name`, NOT a null token.
    u.Alias(std::string(""));
    EXPECT_NE(u.AliasToken(), nullptr);
    EXPECT_EQ(u.Alias(), "");
    EXPECT_EQ(u.AliasToken()->Name(), "");
}

// `Import` gets/sets the `AstType` child at flattened index 1.
TEST(CSharp_UsingAliasDeclaration, ImportGetterSetter) {
    UsingAliasDeclaration u;
    EXPECT_EQ(u.Import(), nullptr);
    auto st = std::make_unique<SimpleType>(std::string("System"));
    u.Import(st.get());
    EXPECT_EQ(u.Import(), st.get());
    EXPECT_EQ(u.Import()->Parent(), &u);
    EXPECT_EQ(u.Import()->ChildIndex, 1);
    u.Import(nullptr);
    EXPECT_EQ(u.Import(), nullptr);
}

// ---- UsingKeyword const -----------------------------------------------

// The `UsingKeyword` const string is `"using"` (shared with `UsingDeclaration`).
TEST(CSharp_UsingAliasDeclaration, UsingKeywordConst) {
    EXPECT_STREQ(UsingAliasDeclaration::UsingKeyword, "using");
    EXPECT_EQ(UsingAliasDeclaration::UsingKeyword, UsingDeclaration::UsingKeyword);
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitUsingAliasDeclaration`.
TEST(CSharp_UsingAliasDeclaration, AcceptVisitorDispatches) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration u{std::string("A"), st.get()};
    RecordingVisitor v;
    u.AcceptVisitor(v);
    // The walk recurses into the `AliasToken` (index 0), then the `Import` `SimpleType` (index 1,
    // which recurses into its backing `IdentifierToken`).
    EXPECT_EQ(v.trace, (std::vector<std::string>{"using-alias:A", "id:A", "simple:System", "id:System"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override.
TEST(CSharp_UsingAliasDeclaration, AcceptVisitorIsVirtualThroughAstNode) {
    auto st = std::make_unique<SimpleType>(std::string("IO"));
    auto u = std::make_unique<UsingAliasDeclaration>(std::string("A"), st.get());
    AstNode* asAst = u.get();
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"using-alias:A", "id:A", "simple:IO", "id:IO"}));
}

// The depth-first walk recurses into the `AliasToken` then the `Import`.
TEST(CSharp_UsingAliasDeclaration, DepthFirstWalkRecordsNodeTokenAndImport) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto u = std::make_unique<UsingAliasDeclaration>(std::string("A"), st.get());
    RecordingVisitor v;
    u->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"using-alias:A", "id:A", "simple:System", "id:System"}));
}

// ---- is-a (AstNode, not Expression/Statement/AstType) ----------------

// `UsingAliasDeclaration` is an `AstNode` but NOT an `Expression`, `Statement`, or `AstType`.
// It is `final`.
TEST(CSharp_UsingAliasDeclaration, IsAstNodeButNotExpressionStatementOrAstType) {
    auto u = std::make_unique<UsingAliasDeclaration>();
    EXPECT_NE(dynamic_cast<AstNode*>(u.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(u.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(u.get()), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(u.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<UsingAliasDeclaration>);
}

// ---- Slot storage -----------------------------------------------------

// Two single slots at flattened indices 0 (`AliasToken`) and 1 (`Import`).
TEST(CSharp_UsingAliasDeclaration, SlotStorageContract) {
    UsingAliasDeclaration u;
    EXPECT_EQ(u.GetChildCount(), 2);
    EXPECT_EQ(u.GetChild(0), nullptr);
    EXPECT_EQ(u.GetChild(1), nullptr);
    EXPECT_EQ(u.GetChildSlotInfo(0), &UsingAliasDeclaration::AliasTokenSlot);
    EXPECT_EQ(u.GetChildSlotInfo(1), &UsingAliasDeclaration::ImportSlot);
    EXPECT_THROW(u.GetChild(2), std::out_of_range);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    u.SetChild(0, tok.get());
    EXPECT_EQ(u.GetChild(0), tok.get());
    auto st = std::make_unique<SimpleType>(std::string("Z"));
    u.SetChild(1, st.get());
    EXPECT_EQ(u.GetChild(1), st.get());
    EXPECT_THROW(u.SetChild(2, st.get()), std::out_of_range);
}

// The per-node slot statics point at the shared `Slots` kinds (`AliasTokenSlot` at the new
// `Slots::Alias`, `ImportSlot` at the new `Slots::Import` shared with `UsingDeclaration`).
TEST(CSharp_UsingAliasDeclaration, SlotKindsPointAtSharedSlots) {
    EXPECT_EQ(UsingAliasDeclaration::AliasTokenSlot.Kind(), &Slots::Alias);
    EXPECT_EQ(UsingAliasDeclaration::ImportSlot.Kind(), &Slots::Import);
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Alias),
              static_cast<const CSharpSlotInfo*>(&Slots::Import));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Alias),
              static_cast<const CSharpSlotInfo*>(&Slots::Identifier));
}

// The `AliasTokenSlot`'s `IsInstanceOfType` is-a test accepts an `Identifier`; the `ImportSlot`'s
// accepts an `AstType`.
TEST(CSharp_UsingAliasDeclaration, SlotIsInstanceOfTypeCrossCheck) {
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    EXPECT_TRUE(UsingAliasDeclaration::AliasTokenSlot.IsInstanceOfType(tok.get()));
    EXPECT_FALSE(UsingAliasDeclaration::AliasTokenSlot.IsInstanceOfType(
        static_cast<AstNode*>(std::make_unique<SimpleType>(std::string("X")).get())));
    auto st = std::make_unique<SimpleType>(std::string("System"));
    EXPECT_TRUE(UsingAliasDeclaration::ImportSlot.IsInstanceOfType(st.get()));
}

// ---- DoMatch (the generated MatchString + MatchRequired match) --------

// Two `UsingAliasDeclaration`s with the same `Alias` and `Import` shape match.
TEST(CSharp_UsingAliasDeclaration, DoMatchMatchesSameAliasAndImport) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration a{std::string("A"), stA.get()};
    UsingAliasDeclaration b{std::string("A"), stB.get()};
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Two `UsingAliasDeclaration`s with different `Alias` do not match (the `MatchString` term
// rejects).
TEST(CSharp_UsingAliasDeclaration, DoMatchRejectsDifferentAlias) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration a{std::string("A"), stA.get()};
    UsingAliasDeclaration b{std::string("B"), stB.get()};
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `UsingAliasDeclaration`s with the same `Alias` but different `Import` do not match (the
// `MatchRequired` term rejects via the `SimpleType` `DoMatch`).
TEST(CSharp_UsingAliasDeclaration, DoMatchRejectsDifferentImport) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("Collections"));
    UsingAliasDeclaration a{std::string("A"), stA.get()};
    UsingAliasDeclaration b{std::string("A"), stB.get()};
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// The `$any$` wildcard (`Pattern::AnyString`) in the pattern's `Alias` matches any candidate
// alias (the `MatchString` wildcard path) -- but the `Import` must still match.
TEST(CSharp_UsingAliasDeclaration, DoMatchAnyStringWildcardOnAlias) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration pattern{std::string(Pattern::AnyString), stA.get()};
    UsingAliasDeclaration cand{std::string("anything"), stB.get()};
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand));
    // The wildcard is on Alias only; a different Import still rejects.
    auto stC = std::make_unique<SimpleType>(std::string("Collections"));
    UsingAliasDeclaration cand2{std::string("anything"), stC.get()};
    EXPECT_FALSE(DoMatchAgainst(&pattern, &cand2));
}

// Two empty-alias `UsingAliasDeclaration`s with the same `Import` match.
TEST(CSharp_UsingAliasDeclaration, DoMatchEmptyAliasMatchesEmptyAlias) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration a{std::string(""), stA.get()};
    UsingAliasDeclaration b{std::string(""), stB.get()};
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `UsingAliasDeclaration` does not match a different concrete type (the
// `other is UsingAliasDeclaration` gate -- a cross-structural-twin `UsingDeclaration` rejection).
TEST(CSharp_UsingAliasDeclaration, DoMatchRejectsDifferentType) {
    auto stA = std::make_unique<SimpleType>(std::string("System"));
    auto stB = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration uaa{std::string("A"), stA.get()};
    UsingDeclaration ud(stB.get());
    EXPECT_FALSE(DoMatchAgainst(&uaa, &ud));
    EXPECT_FALSE(DoMatchAgainst(&ud, &uaa));
}

// A null candidate is rejected.
TEST(CSharp_UsingAliasDeclaration, DoMatchRejectsNullCandidate) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    UsingAliasDeclaration u{std::string("A"), st.get()};
    EXPECT_FALSE(DoMatchAgainst(&u, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` copies the `AliasToken` and `Import` (deep-copy, re-parented) and detaches.
TEST(CSharp_UsingAliasDeclaration, CloneCopiesBothAndDetaches) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto original = std::make_unique<UsingAliasDeclaration>(std::string("A"), st.get());
    std::unique_ptr<UsingAliasDeclaration> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Alias(), "A");
    EXPECT_NE(copy->AliasToken(), original->AliasToken());
    EXPECT_EQ(copy->AliasToken()->Parent(), copy.get());
    ASSERT_NE(copy->Import(), nullptr);
    EXPECT_NE(copy->Import(), original->Import());
    EXPECT_EQ(copy->Import()->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*`.
TEST(CSharp_UsingAliasDeclaration, CloneIsVirtualThroughAstNode) {
    auto st = std::make_unique<SimpleType>(std::string("X"));
    auto original = std::make_unique<UsingAliasDeclaration>(std::string("A"), st.get());
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<UsingAliasDeclaration*>(copy.get()), nullptr);
    EXPECT_EQ(static_cast<UsingAliasDeclaration*>(copy.get())->Alias(), "A");
}

// `Clone` is a deep copy (mutating the copy does not affect the original).
TEST(CSharp_UsingAliasDeclaration, CloneIsDeepCopy) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto original = std::make_unique<UsingAliasDeclaration>(std::string("A"), st.get());
    std::unique_ptr<UsingAliasDeclaration> copy(original->Clone());
    copy->Alias(std::string("B"));
    EXPECT_EQ(original->Alias(), "A");
    EXPECT_EQ(copy->Alias(), "B");
}

// ---- CheckInvariant ---------------------------------------------------

// A default-constructed (empty) node violates the required-`AliasToken` invariant (the
// `AliasToken` is required).
TEST(CSharp_UsingAliasDeclaration, CheckInvariantRejectsEmptyNode) {
    UsingAliasDeclaration u;
    EXPECT_DEBUG_DEATH(u.CheckInvariant(), "");
}

// A node with the `AliasToken` set but the `Import` missing is rejected (the `Import` is
// required).
TEST(CSharp_UsingAliasDeclaration, CheckInvariantRejectsMissingImport) {
    UsingAliasDeclaration u;
    u.Alias(std::string("A"));
    EXPECT_DEBUG_DEATH(u.CheckInvariant(), "");
}

// A filled node passes `CheckInvariant`.
TEST(CSharp_UsingAliasDeclaration, CheckInvariantPassesOnFilled) {
    auto st = std::make_unique<SimpleType>(std::string("System"));
    auto u = std::make_unique<UsingAliasDeclaration>(std::string("A"), st.get());
    u->CheckInvariant();
}
