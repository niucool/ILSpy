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

// Tests for `InsertMissingTokensDecorator` (OutputVisitor/InsertMissingTokensDecorator.hpp) --
// the `DecoratingTokenWriter` subclass that records source spans back onto the AST nodes as the
// output visitor drives the token stream through it. The tests drive the decorator over a
// `TrackingWriter` that is BOTH the inner `TokenWriter` AND the `ILocatable` (the role the
// concrete `TextWriterTokenWriter` plays in the real pipeline): it advances its own line/column
// as it writes, so the decorator's before/after `Location()` reads observe a moving position --
// the faithful simulation of the real pipeline where the writer tracks location itself. The
// tests assert that each node's `StartLocation`/`EndLocation` (and the `Comment`/`ErrorExpression`/
// `EmptyStatement`/`PrimitiveType`/identifier special cases) are set exactly where the C#
// `InsertMissingTokensDecorator` sets them, and that the printed children are re-attached to
// their parent by slot kind. This is the next in-order Phase-5 piece of the output stage per the
// D320 decision-log entry.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/InsertMissingTokensDecorator.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::BaseReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::BreakStatement;
using ILSpy::Decompiler::CSharp::Syntax::Comment;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::EmptyStatement;
using ILSpy::Decompiler::CSharp::Syntax::ErrorExpression;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::ThisReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::TextLocation;
using ILSpy::Decompiler::CSharp::Syntax::Trivia;

namespace {

// A `TokenWriter` that is ALSO the `ILocatable` the decorator reads the current source location
// from -- the role the concrete `TextWriterTokenWriter` plays in the real pipeline. It advances
// its own column by the length of each written token/keyword/identifier/primitive-type (and by a
// fixed 1 for a primitive value, whose rendered length is irrelevant to the span-recording
// logic), so the decorator's before/after `Location()` reads observe a moving position. It also
// records a short string per call so the tests can assert on the forwarded call sequence.
class TrackingWriter : public TokenWriter, public ILocatable {
public:
	std::vector<std::string> calls;
	int line_ = 1;
	int col_ = 1;

	TextLocation Location() const override { return TextLocation{line_, col_}; }
	int Length() const override { return col_; }

	void StartNode(AstNode* /*node*/) override { calls.push_back("start"); }
	void EndNode(AstNode* /*node*/) override { calls.push_back("end"); }
	void WriteIdentifier(Identifier* identifier) override {
		calls.push_back("id:" + std::string(identifier->Name()));
		Advance(identifier->Name().size());
	}
	void WriteKeyword(std::string_view keyword) override {
		calls.push_back("kw:" + std::string(keyword));
		Advance(keyword.size());
	}
	void WriteToken(std::string_view token) override {
		calls.push_back("tok:" + std::string(token));
		Advance(token.size());
	}
	void WritePrimitiveType(std::string_view type) override {
		calls.push_back("primtype:" + std::string(type));
		Advance(type.size());
	}
	void WriteInterpolatedText(std::string_view text) override { Advance(text.size()); }
	void WritePrimitiveValue(const PrimitiveValue& /*value*/, LiteralFormat /*format*/) override {
		calls.push_back("primval");
		Advance(1);
	}
	void Space() override { calls.push_back("space"); Advance(1); }
	void Indent() override { calls.push_back("indent"); }
	void Unindent() override { calls.push_back("unindent"); }
	void NewLine() override { calls.push_back("newline"); line_++; col_ = 1; }
	void WriteComment(CommentType /*commentType*/, std::string_view content) override {
		calls.push_back("comment:" + std::string(content));
		Advance(content.size());
	}
	void WritePreProcessorDirective(PreProcessorDirectiveType /*type*/, std::optional<std::string_view> /*argument*/) override {
		calls.push_back("pp");
	}

private:
	void Advance(std::size_t n) { col_ += static_cast<int>(n); }
};

// A fresh inner tracker + decorator pair per test. The tracker plays both the inner-writer and
// the `ILocatable` roles (the real pipeline's `TextWriterTokenWriter` does both).
struct D {
	TrackingWriter inner;
	InsertMissingTokensDecorator dec;
	D() : dec(&inner, &inner) {}
};

// A `unique_ptr<Identifier>` owning a token created via the public `Identifier::Create` factory
// (the 2-arg ctor is private, D241/D257).
std::unique_ptr<Identifier> MakeId(std::string name) {
	return std::unique_ptr<Identifier>(Identifier::Create(std::move(name)));
}

}  // namespace

// The decorator is a concrete `TokenWriter` and a `DecoratingTokenWriter` subclass.
TEST(CSharp_InsertMissingTokensDecorator, IsDecoratingTokenWriter) {
	D h;
	TokenWriter& tw = h.dec;
	(void)tw;
	EXPECT_TRUE((std::is_base_of_v<DecoratingTokenWriter, InsertMissingTokensDecorator>));
	EXPECT_FALSE(std::is_abstract_v<InsertMissingTokensDecorator>);
}

// The ctor null-check on the wrapped writer is inherited from `DecoratingTokenWriter` (the C#
// `ArgumentNullException` -> `std::invalid_argument`).
TEST(CSharp_InsertMissingTokensDecorator, CtorRejectsNullWriter) {
	TrackingWriter inner;
	EXPECT_THROW({ InsertMissingTokensDecorator dec(nullptr, &inner); }, std::invalid_argument);
}

// A node that prints a token gets `StartLocation` at the token's start (assigned lazily on the
// first write, not at `StartNode`) and `EndLocation` at the token's end (the position after the
// last token, not the `EndNode` position).
TEST(CSharp_InsertMissingTokensDecorator, StartNodeEndNodeRecordsSpan) {
	D h;
	BreakStatement br;
	EXPECT_TRUE(br.StartLocation().IsEmpty());  // nothing set yet
	h.dec.StartNode(&br);                       // br awaits its first token; no span yet
	EXPECT_TRUE(br.StartLocation().IsEmpty());
	h.dec.WriteToken("break");  // br.StorePrintStart((1,1)); col -> 6; lastTokenEnd=(1,6)
	h.dec.WriteToken(";");       // col -> 7; lastTokenEnd=(1,7)
	h.dec.EndNode(&br);          // br.StorePrintEnd((1,7))
	EXPECT_EQ(br.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(br.EndLocation(), (TextLocation{1, 7}));
}

// A node that prints NO token of its own collapses to a zero-width span at the end of the
// previous token (its start and end are both the post-end of the prior token).
TEST(CSharp_InsertMissingTokensDecorator, TokenlessNodeGetsZeroWidthSpan) {
	D h;
	BreakStatement first;
	BreakStatement empty;  // prints no token
	h.dec.StartNode(&first);
	h.dec.WriteToken(";");  // lastTokenEnd=(1,2)
	h.dec.EndNode(&first);
	h.dec.StartNode(&empty);  // empty awaits its first token
	h.dec.EndNode(&empty);    // empty printed nothing: StorePrintStart(lastTokenEnd=(1,2)); StorePrintEnd((1,2))
	EXPECT_EQ(empty.StartLocation(), (TextLocation{1, 2}));
	EXPECT_EQ(empty.EndLocation(), (TextLocation{1, 2}));
}

// The `this` keyword sets the enclosing `ThisReferenceExpression` start location (the node is the
// top-most in its parent's list).
TEST(CSharp_InsertMissingTokensDecorator, WriteKeywordThisSetsThisRefStart) {
	D h;
	ThisReferenceExpression tre;
	h.dec.StartNode(&tre);
	h.dec.WriteKeyword("this");  // tre.StorePrintStart((1,1)); col -> 5; lastTokenEnd=(1,5)
	h.dec.EndNode(&tre);         // tre.StorePrintEnd((1,5))
	EXPECT_EQ(tre.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(tre.EndLocation(), (TextLocation{1, 5}));  // derived: column + 4
}

// The `base` keyword sets the enclosing `BaseReferenceExpression` start location.
TEST(CSharp_InsertMissingTokensDecorator, WriteKeywordBaseSetsBaseRefStart) {
	D h;
	BaseReferenceExpression bre;
	h.dec.StartNode(&bre);
	h.dec.WriteKeyword("base");  // bre.StorePrintStart((1,1)); col -> 5; lastTokenEnd=(1,5)
	h.dec.EndNode(&bre);
	EXPECT_EQ(bre.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(bre.EndLocation(), (TextLocation{1, 5}));  // derived: column + 4
}

// A keyword that is not `this`/`base` does not set a `ThisReferenceExpression`/`BaseReferenceExpression`
// start (the node keeps the start the lazy `AssignPendingStartLocations` gave it).
TEST(CSharp_InsertMissingTokensDecorator, WriteKeywordOtherDoesNotSpecialCase) {
	D h;
	ThisReferenceExpression tre;
	h.dec.StartNode(&tre);
	h.dec.WriteKeyword("return");  // no `this`/`base` special case; tre gets the lazy start (1,1)
	h.dec.EndNode(&tre);
	EXPECT_EQ(tre.StartLocation(), (TextLocation{1, 1}));
}

// `WriteIdentifier` records the identifier's start location and the identifier becomes the
// top-most node for any following write.
TEST(CSharp_InsertMissingTokensDecorator, WriteIdentifierSetsStartLocation) {
	D h;
	auto id = MakeId("x");
	id->SetStartLocation(TextLocation{9, 9});  // pre-set, should be overwritten
	h.dec.StartNode(id.get());
	h.dec.WriteIdentifier(id.get());  // id.SetStartLocation((1,1)); col -> 2; lastTokenEnd=(1,2)
	h.dec.EndNode(id.get());
	EXPECT_EQ(id->StartLocation(), (TextLocation{1, 1}));
}

// A `PrimitiveExpression` as the enclosing node gets `SetLocation(start, end)` spanning the
// written primitive value.
TEST(CSharp_InsertMissingTokensDecorator, WritePrimitiveValuePrimitiveExprSetsLocation) {
	D h;
	PrimitiveExpression pe(PrimitiveValue(std::int32_t(42)));
	h.dec.StartNode(&pe);
	h.dec.WritePrimitiveValue(PrimitiveValue(std::int32_t(42)), LiteralFormat::DecimalNumber);
	// pe.SetLocation((1,1),(1,2)); lastTokenEnd=(1,2)
	h.dec.EndNode(&pe);
	EXPECT_EQ(pe.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(pe.EndLocation(), (TextLocation{1, 2}));
}

// A `NullReferenceExpression` as the enclosing node gets `StorePrintStart(start)` (the `null`
// literal); its `EndLocation` is derived (column + 4) regardless of the stored end.
TEST(CSharp_InsertMissingTokensDecorator, WritePrimitiveValueNullRefSetsStart) {
	D h;
	NullReferenceExpression nre;
	h.dec.StartNode(&nre);
	h.dec.WritePrimitiveValue(PrimitiveValue(std::monostate{}), LiteralFormat::None);
	// nre.StorePrintStart((1,1)); lastTokenEnd=(1,2)
	h.dec.EndNode(&nre);
	EXPECT_EQ(nre.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(nre.EndLocation(), (TextLocation{1, 5}));  // derived: column + 4
}

// A `PrimitiveType` as the enclosing node gets `StorePrintStart` at the primitive-type keyword.
TEST(CSharp_InsertMissingTokensDecorator, WritePrimitiveTypeSetsPrimitiveTypeStart) {
	D h;
	PrimitiveType pt("int");
	h.dec.StartNode(&pt);
	h.dec.WritePrimitiveType("int");  // pt.StorePrintStart((1,1)); col -> 4; lastTokenEnd=(1,4)
	h.dec.EndNode(&pt);
	EXPECT_EQ(pt.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(pt.EndLocation(), (TextLocation{1, 4}));  // derived: column + 3
}

// A `Comment` (trivia) records its start location at `StartNode` (immediately, not lazily).
TEST(CSharp_InsertMissingTokensDecorator, CommentStartNodeSetsStartLocation) {
	D h;
	Comment c("warn", CommentType::MultiLine);
	EXPECT_TRUE(c.StartLocation().IsEmpty());
	h.dec.StartNode(&c);  // c is a Comment (trivia): c.SetStartLocation((1,1))
	EXPECT_EQ(c.StartLocation(), (TextLocation{1, 1}));
	h.dec.WriteComment(CommentType::MultiLine, "warn");  // col -> 5; lastTokenEnd=(1,5)
	h.dec.EndNode(&c);                                   // c.SetEndLocation((1,5))
	EXPECT_EQ(c.EndLocation(), (TextLocation{1, 5}));
}

// An `ErrorExpression` records its `Location` at `StartNode` (immediately, regardless of
// trivia-ness -- the C# `if` is outside the if/else).
TEST(CSharp_InsertMissingTokensDecorator, ErrorExpressionStartSetsLocation) {
	D h;
	ErrorExpression err;
	EXPECT_TRUE(err.Location().IsEmpty());
	h.dec.StartNode(&err);  // err.Location((1,1))
	EXPECT_EQ(err.Location(), (TextLocation{1, 1}));
	h.dec.WriteToken("err");  // err is the top-most: err.Location((1,1)) again (same value)
	h.dec.EndNode(&err);
	EXPECT_EQ(err.Location(), (TextLocation{1, 1}));
}

// An `EmptyStatement` whose semicolon token IS written gets its `Location` set at the token start
// (the `WriteToken` `EmptyStatement` branch).
TEST(CSharp_InsertMissingTokensDecorator, EmptyStatementWriteTokenSetsLocation) {
	D h;
	EmptyStatement es;
	h.dec.StartNode(&es);
	h.dec.WriteToken(";");  // es.StorePrintStart((1,1)) [lazy]; es.Location((1,1)); col -> 2; lastTokenEnd=(1,2)
	h.dec.EndNode(&es);     // es.StorePrintEnd((1,2))
	EXPECT_EQ(es.Location(), (TextLocation{1, 1}));
	EXPECT_EQ(es.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(es.EndLocation(), (TextLocation{1, 2}));  // derived: column + 1
}

// A comment-only `EmptyStatement` (no semicolon printed) points its `Location` at the comment it
// carries (already printed by now) rather than leaving it at the empty `lastTokenEnd`.
TEST(CSharp_InsertMissingTokensDecorator, EmptyStatementNoTokenUsesTriviaLocation) {
	D h;
	EmptyStatement es;
	// Heap-allocated: `AddLeadingTrivia` takes ownership (wraps in `unique_ptr`, deletes on
	// parent destruction -- the D224 `NodeTrivia` ownership model), so the Comment must outlive the
	// test body via `es` rather than be a stack object.
	Comment* c = new Comment("warn", CommentType::MultiLine);
	// Print the comment first so its `StartLocation` is set and `lastTokenEnd` advances.
	h.dec.StartNode(c);                            // c.SetStartLocation((1,1))
	h.dec.WriteComment(CommentType::MultiLine, "warn");  // col -> 5; lastTokenEnd=(1,5)
	h.dec.EndNode(c);                              // c.SetEndLocation((1,5))
	ASSERT_FALSE(c->StartLocation().IsEmpty());
	// Attach the printed comment to the empty statement's leading trivia (es now owns c).
	es.AddLeadingTrivia(c);
	// The empty statement prints no token of its own.
	h.dec.StartNode(&es);  // es awaits its first token; lastTokenEnd=(1,5)
	h.dec.EndNode(&es);    // es printed nothing: StorePrintStart((1,5)); then the EmptyStatement
	//                          branch finds the comment (StartLocation=(1,1)) and sets es.Location=(1,1)
	EXPECT_EQ(es.Location(), (TextLocation{1, 1}));
}

// A comment-only `EmptyStatement` whose trivia all have empty locations falls back to
// `lastTokenEnd` (the `FirstOrDefault` finds nothing).
TEST(CSharp_InsertMissingTokensDecorator, EmptyStatementNoTokenNoTriviaFallsBackToLastTokenEnd) {
	D h;
	EmptyStatement es;
	// Establish a non-trivial `lastTokenEnd` via a prior token.
	BreakStatement first;
	h.dec.StartNode(&first);
	h.dec.WriteToken(";");  // lastTokenEnd=(1,2)
	h.dec.EndNode(&first);
	h.dec.StartNode(&es);  // es awaits; no trivia attached
	h.dec.EndNode(&es);    // no trivia found -> es.Location = lastTokenEnd=(1,2)
	EXPECT_EQ(es.Location(), (TextLocation{1, 2}));
}

// The children printed inside a node (an identifier written via `WriteIdentifier`) are re-attached
// to the node by slot kind at its `EndNode`. Here a `SimpleType`'s name token, written via
// `WriteIdentifier`, is removed and re-added to the same `Identifier`-kind slot (a no-op
// re-parenting that leaves the token attached).
TEST(CSharp_InsertMissingTokensDecorator, WriteIdentifierReparentsChildByKind) {
	D h;
	SimpleType st("T");
	Identifier* tok = st.IdentifierToken();
	ASSERT_NE(tok, nullptr);
	ASSERT_NE(tok->Parent(), nullptr);  // the token is parented to st
	h.dec.StartNode(&st);
	h.dec.WriteIdentifier(tok);  // tok added to st's child list; tok.SetStartLocation((1,1))
	h.dec.EndNode(&st);           // tok re-attached to st by Slots::Identifier
	EXPECT_EQ(st.IdentifierToken(), tok);          // still attached (re-parented to the same slot)
	EXPECT_EQ(st.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(st.EndLocation(), (TextLocation{1, 2}));
	EXPECT_EQ(tok->StartLocation(), (TextLocation{1, 1}));
}

// A parentless child (a print-only artifact with no slot in the tree) is SKIPPED in the
// re-parenting rather than routed into the throwing child setter. A free-standing `Identifier`
// written inside a node has no slot, so it is not re-attached.
TEST(CSharp_InsertMissingTokensDecorator, ParentlessChildIsSkipped) {
	D h;
	auto freeTok = MakeId("x");  // unparented
	ASSERT_EQ(freeTok->Parent(), nullptr);
	SimpleType st;  // empty, no name token
	ASSERT_EQ(st.IdentifierToken(), nullptr);
	h.dec.StartNode(&st);
	h.dec.WriteIdentifier(freeTok.get());  // freeTok added to st's child list (but unparented)
	h.dec.EndNode(&st);                     // freeTok has no slot -> skipped, NOT re-attached
	EXPECT_EQ(st.IdentifierToken(), nullptr);  // the free token was not attached to st
	EXPECT_EQ(freeTok->Parent(), nullptr);     // and remains unparented (Remove not called)
}

// Nested `StartNode`/`EndNode` pairs maintain the stack discipline: each node's span is
// independent and determined by the tokens written inside it (not its nested children's tokens).
TEST(CSharp_InsertMissingTokensDecorator, NestedNodesMaintainStack) {
	D h;
	BreakStatement outer;
	BreakStatement innerBr;
	h.dec.StartNode(&outer);       // outer awaits; span starts at next write
	h.dec.WriteToken("break");      // outer.StorePrintStart((1,1)); col -> 6; lastTokenEnd=(1,6)
	h.dec.StartNode(&innerBr);      // innerBr awaits (added to outer's child list)
	h.dec.WriteToken("break");      // innerBr.StorePrintStart((1,6)); col -> 11; lastTokenEnd=(1,11)
	h.dec.WriteToken(";");          // col -> 12; lastTokenEnd=(1,12)
	h.dec.EndNode(&innerBr);        // innerBr.StorePrintEnd((1,12))
	h.dec.WriteToken(";");           // col -> 13; lastTokenEnd=(1,13)
	h.dec.EndNode(&outer);          // outer.StorePrintEnd((1,13))
	EXPECT_EQ(outer.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(outer.EndLocation(), (TextLocation{1, 13}));
	EXPECT_EQ(innerBr.StartLocation(), (TextLocation{1, 6}));
	EXPECT_EQ(innerBr.EndLocation(), (TextLocation{1, 12}));
}

// A node started before any token of an ENCLOSING node's token: the enclosing node's start is
// assigned lazily at the first token written after it started (not at its own `StartNode`), and
// multiple awaiting nodes share the same start location.
TEST(CSharp_InsertMissingTokensDecorator, AssignPendingStartLocationsSetsAllAwaiting) {
	D h;
	BreakStatement a;
	BreakStatement b;
	h.dec.StartNode(&a);  // a awaits
	h.dec.StartNode(&b);   // b awaits (a still awaiting -- a printed no token before b started)
	h.dec.WriteToken(";");  // BOTH a and b get StorePrintStart((1,1)); col -> 2; lastTokenEnd=(1,2)
	h.dec.EndNode(&b);      // b.StorePrintEnd((1,2))
	h.dec.EndNode(&a);      // a printed nothing of its own after b ended? a is NOT awaiting (removed
	//                          in the WriteToken), so a.StorePrintEnd((1,2)) -- a zero-width span at (1,2)
	EXPECT_EQ(a.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(a.EndLocation(), (TextLocation{1, 2}));
	EXPECT_EQ(b.StartLocation(), (TextLocation{1, 1}));
	EXPECT_EQ(b.EndLocation(), (TextLocation{1, 2}));
}

// A trivia node (`Comment`) does NOT push onto the stack or get a lazy start: its start is set
// immediately at `StartNode`, and a following OVERRIDDEN write (`WriteToken`, not the inherited
// `WriteComment`) still treats the enclosing non-trivia node as the top-most (the comment did not
// become it), so the enclosing node gets its start at that next write -- at the position AFTER the
// comment, proving the comment did not displace it as the top-most node.
TEST(CSharp_InsertMissingTokensDecorator, TriviaDoesNotPushStack) {
	D h;
	BreakStatement br;
	Comment c("note", CommentType::SingleLine);
	h.dec.StartNode(&br);      // br pushed, awaits
	h.dec.StartNode(&c);       // c is trivia: c.SetStartLocation((1,1)); NOT pushed
	EXPECT_EQ(c.StartLocation(), (TextLocation{1, 1}));
	h.dec.WriteComment(CommentType::SingleLine, "note");  // col -> 5; lastTokenEnd=(1,5)
	// `WriteComment` is NOT overridden by the decorator (it inherits the pass-through), so it does
	// NOT call `AssignPendingStartLocations`: br's start is still unassigned.
	EXPECT_TRUE(br.StartLocation().IsEmpty());
	h.dec.EndNode(&c);  // c.SetEndLocation((1,5))
	EXPECT_EQ(c.EndLocation(), (TextLocation{1, 5}));
	h.dec.WriteToken(";");  // AssignPendingStartLocations: br.StorePrintStart((1,5)) -- the position
	//                          AFTER the comment; col -> 6; lastTokenEnd=(1,6)
	EXPECT_EQ(br.StartLocation(), (TextLocation{1, 5}));  // the comment did not displace br
	h.dec.EndNode(&br);  // br.StorePrintEnd((1,6))
	EXPECT_EQ(br.EndLocation(), (TextLocation{1, 6}));
}
