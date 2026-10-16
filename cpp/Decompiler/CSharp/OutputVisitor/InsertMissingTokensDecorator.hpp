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

// Port of `InsertMissingTokensDecorator` in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/InsertMissingTokensDecorator.cs -- the
// `DecoratingTokenWriter` (D316) subclass that records source spans back onto the AST nodes as
// the `CSharpOutputVisitor` (not yet ported) drives the token stream through it. The output
// visitor calls `StartNode`/`EndNode` around every node and `WriteIdentifier`/`WriteKeyword`/
// `WriteToken`/`WritePrimitiveValue`/`WritePrimitiveType` for the tokens each node prints; this
// decorator intercepts those calls to set each node's `StartLocation`/`EndLocation` to the span
// of the tokens it actually printed (a node that prints no token collapses to a zero-width span
// at the end of the previous token) and to re-attach the printed children to their parent by
// slot kind (the output visitor prints children in document order; the decorator re-establishes
// the parent/child relationships the slot system expects).
//
// This is the next in-order Phase-5 piece of the output stage per the D320 decision-log entry,
// which named this decorator (the second of the two) as the next-in-order piece now that
// `InsertRequiredSpacesDecorator` (D320) is in place. Every dependency this decorator consults
// is already ported: the `DecoratingTokenWriter`/`TokenWriter`/`ILocatable` bases (D316), the
// `AstNode` span/mutation API `StorePrintStart`/`StorePrintEnd`/`Slot`/`Remove`/`AddChildUnsafe`/
// `LeadingTrivia`/`TrailingTrivia` (D220/D223/D224), the `Trivia` base `SetStartLocation`/
// `SetEndLocation` (D224), the `Comment` node (D288), the `ErrorExpression` `Location` setter
// (D298), the `EmptyStatement` `Location` setter and trivia lists (D259), the
// `ThisReferenceExpression`/`BaseReferenceExpression`/`NullReferenceExpression`/
// `PrimitiveType` `StorePrintStart` (D226/D236), the `Identifier` `SetStartLocation` (D227), the
// `PrimitiveExpression` `SetLocation` (D228), the `TextLocation` value type (D218), and the
// `CSharpSlotInfo` slot/`Kind` (D220).
//
// C#-to-C++ porting decisions:
//  * the C# `Stack<List<AstNode>> nodes` / `List<AstNode> currentList` port to a
//    `std::stack<std::vector<AstNode*>> nodes_` / `std::vector<AstNode*> currentList_`. The C#
//    pushes a REFERENCE to `currentList` then reassigns `currentList` to a fresh list; the
//    port copies `currentList_` onto the stack (`nodes_.push(currentList_)`) then clears it
//    (`currentList_.clear()`). The stack entries are only ever READ after the push (the C#
//    `nodes.Peek().LastOrDefault()` is read-only, never `nodes.Peek().Add(...)`), so the
//    value-semantics copy is observationally identical to the C# reference share -- the
//    contents at every point are the same. `EndNode` restores `currentList_` from the stack
//    via `std::move(nodes_.top()); nodes_.pop();` (move to avoid a second copy).
//  * the C# `HashSet<AstNode> nodesAwaitingStartLocation` ports to
//    `std::unordered_set<AstNode*> nodesAwaitingStartLocation_` (pointer identity, the faithful
//    `HashSet` reference-equality). `Count` -> `empty()`, `Add` -> `insert`, `Remove` (which
//    returns whether the node was present) -> `erase` (which returns the count erased, 0 or 1),
//    `Clear` -> `clear()`. The iteration order does not matter (every awaiting node receives the
//    SAME location), so `unordered_set` is faithful.
//  * the C# `node is not Trivia` / `node is Comment comment` / `node is ErrorExpression error` /
//    `node is EmptyStatement emptyStatement` / `... as ThisReferenceExpression` / etc. type tests
//    port to `dynamic_cast` (the established is-a convention). The `node is not Trivia` test is
//    a plain is-a check (NOT a sibling/slot navigation), so `dynamic_cast<Trivia*>(node) ==
//    nullptr` is the faithful port -- this does NOT conflict with the D224 deferral, which kept
//    the trivia SIBLING/SLOT branches in `AstNode`'s navigation methods dropped (trivia is
//    reached through the `LeadingTrivia`/`TrailingTrivia` lists, not the sibling space); a plain
//    is-a test on a node pointer is a different concern.
//  * the C# `nodes.Peek().LastOrDefault()` (the last node in the top/parent list, or null) ports
//    to a private `TopLastOrDefault()` helper that returns `nullptr` for an empty stack or an
//    empty top vector (the C# `Peek()` throws on an empty stack, but the output visitor always
//    `StartNode`s the root before writing tokens, so the stack is never empty in practice; the
//    defensive `nullptr` return is a harmless robustness improvement -- `dynamic_cast` on null
//    returns null, so a write before any `StartNode` is a graceful no-op rather than UB).
//  * the C# `emptyStatement.LeadingTrivia.Concat(emptyStatement.TrailingTrivia).FirstOrDefault(t
//    => !t.StartLocation.IsEmpty)` ports to two sequential range-for loops (leading then
//    trailing), each returning the first trivia whose `StartLocation().IsEmpty` is false -- the
//    faithful `Concat`+`FirstOrDefault` semantics (leading is searched before trailing, the first
//    non-empty-location trivia wins, or null if none).
//  * the C# `child.Slot is not { } slot` guard (skip a parentless print-only artifact with no
//    slot) ports to `const CSharpSlotInfo* slot = child->Slot(); if (slot == nullptr) continue;`
//    (the C# `Slot` is null when the child is unparented; `child->Slot()` returns null likewise).
//    The `slot.Kind!` (the canonical `Slots` kind, null-forgiving) ports to `slot->Kind()`; a
//    parented child's per-node slot always points at a non-null canonical kind, so the
//    null-forgiving assertion holds.
//  * the `WritePrimitiveValue` override does NOT repeat the `LiteralFormat::None` default (the
//    D316 default-arg-on-pure-virtual crux: a default argument on a pure-virtual is resolved at
//    the static call-site type, so the base's default applies only through a `TokenWriter&`
//    reference, not the concrete override type; the override takes `format` by value and forwards
//    it explicitly to `DecoratingTokenWriter::WritePrimitiveValue(value, format)`).
// NO C++ name-shadowing crux (the method/member names do not collide with any class in the
// `Syntax` or `OutputVisitor` namespaces). NO new `Slots` constant, NO new enum, NO new
// `AstNode` helper (this is the output-stage decorator). Namespace mapping
// `ICSharpCode.Decompiler.CSharp.OutputVisitor` -> `ILSpy::Decompiler::CSharp::OutputVisitor`
// (the convention). Header-only (no CMake source listing -- the TokenWriter/TextWriterTokenWriter/
// InsertRequiredSpacesDecorator header-only precedent), so the CSharp `CMakeLists.txt` is
// unchanged; only the test `.cpp` is added to `tests/CMakeLists.txt`.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTMISSINGTOKENSDECORATOR_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTMISSINGTOKENSDECORATOR_HPP

#include <memory>
#include <stack>
#include <unordered_set>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"  // DecoratingTokenWriter, TokenWriter, ILocatable, PrimitiveValue, LiteralFormat, TextLocation, AstNode (complete), Comment (transitive), PrimitiveExpression (transitive)
#include "Decompiler/CSharp/Syntax/Comment.hpp"             // Comment, SetStartLocation/SetEndLocation
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"  // BaseReferenceExpression, StorePrintStart
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"          // ErrorExpression, Location setter
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"  // NullReferenceExpression, StorePrintStart
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"  // ThisReferenceExpression, StorePrintStart
#include "Decompiler/CSharp/Syntax/Identifier.hpp"        // Identifier, SetStartLocation
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"     // PrimitiveType, StorePrintStart
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"  // EmptyStatement, Location setter, LeadingTrivia/TrailingTrivia
#include "Decompiler/CSharp/Syntax/Trivia.hpp"             // Trivia (the `node is Trivia` is-a test)

namespace ILSpy::Decompiler::CSharp {

// The `Syntax` node types the decorator casts to (the `dynamic_cast` is-a tests and the
// `WriteIdentifier`/`WritePrimitiveType` parameter types) are brought into the `OutputVisitor`
// scope so the method bodies read unqualified. `AstNode`/`Identifier`/`CommentType`/
// `PreProcessorDirectiveType`/`LiteralFormat`/`PrimitiveValue`/`TextLocation` are already brought
// in by `TokenWriter.hpp`; the rest are added here (the headers are included explicitly above so
// the types are complete for the casts).
using Syntax::CSharpSlotInfo;
using Syntax::Comment;
using Syntax::Expression;
using Syntax::ErrorExpression;
using Syntax::EmptyStatement;
using Syntax::BaseReferenceExpression;
using Syntax::NullReferenceExpression;
using Syntax::PrimitiveExpression;
using Syntax::PrimitiveType;
using Syntax::ThisReferenceExpression;
using Syntax::Trivia;

namespace OutputVisitor {

// The C# `class InsertMissingTokensDecorator : DecoratingTokenWriter` -- records source spans
// back onto the AST nodes as the output visitor drives the token stream through it. Tracks a
// stack of per-node child lists (the children printed inside each `StartNode`/`EndNode` pair), a
// set of nodes whose first token has not been written yet (the start location is assigned lazily
// on the next write, not at `StartNode` -- which precedes any leading newline/indentation), and
// the position immediately after the most recently written token (a node's end location is the
// end of its last token, not the `EndNode` position which follows the trailing newline).
class InsertMissingTokensDecorator : public DecoratingTokenWriter {
public:
	// The C# ctor -- the wrapped writer (null-checked by the `DecoratingTokenWriter` base) plus
	// the `ILocatable` the decorator reads the current source location from (the concrete
	// `TextWriterTokenWriter` plays both roles in the real pipeline). `currentList_` starts as an
	// empty vector (the root list).
	explicit InsertMissingTokensDecorator(TokenWriter* writer, ILocatable* locationProvider)
		: DecoratingTokenWriter(writer), locationProvider_(locationProvider) {}

	// The owning ctor (used by the `TokenWriter::CreateWriterThatSetsLocationsInAST` factory
	// that composes a stack and returns a single owning handle to the top) -- takes ownership of
	// the wrapped writer via the `DecoratingTokenWriter` owning-mode base ctor. The
	// `locationProvider` is read-only after construction (the decorator never mutates it), so a
	// non-owning pointer is faithful; the caller passes the same writer's `ILocatable` subobject
	// (the factory grabs it before moving the writer into the decorator, so the pointer stays
	// valid for the decorator's lifetime -- the owned writer lives inside the decorator).
	explicit InsertMissingTokensDecorator(std::unique_ptr<TokenWriter> writer, ILocatable* locationProvider)
		: DecoratingTokenWriter(std::move(writer)), locationProvider_(locationProvider) {}

	// The C# `public override void StartNode(AstNode node)` -- a non-trivia node is pushed onto
	// the stack (its children will be collected in a fresh list) and marked as awaiting its start
	// location; a `Comment` trivia records its start location immediately; an `ErrorExpression`
	// records its `Location` immediately (regardless of trivia-ness, the C# `if` is outside the
	// if/else). The start location of a non-trivia node is NOT set here -- it is assigned lazily
	// on the next write (the position of the first printed token, not the `StartNode` position).
	void StartNode(AstNode* node) override {
		if (dynamic_cast<Trivia*>(node) == nullptr) {
			currentList_.push_back(node);
			nodes_.push(currentList_);
			currentList_.clear();
			nodesAwaitingStartLocation_.insert(node);
		} else if (auto* comment = dynamic_cast<Comment*>(node)) {
			comment->SetStartLocation(locationProvider_->Location());
		}
		if (auto* error = dynamic_cast<ErrorExpression*>(node)) {
			error->Location(locationProvider_->Location());
		}
		DecoratingTokenWriter::StartNode(node);
	}

	// The C# `public override void EndNode(AstNode node)` -- a non-trivia node: if it printed no
	// token of its own (still awaiting its start location), it collapses to a zero-width span at
	// the end of the previous token (and a comment-only `EmptyStatement` points its `Location` at
	// the comment it carries); then its end location is the end of the previous token; then the
	// children printed inside it are re-attached to it by slot kind (a parentless print-only
	// artifact with no slot is left out); then the parent list is restored from the stack. A
	// `Comment` trivia records its end location.
	void EndNode(AstNode* node) override {
		if (dynamic_cast<Trivia*>(node) == nullptr) {
			if (nodesAwaitingStartLocation_.erase(node) != 0) {
				node->StorePrintStart(lastTokenEnd_);
				if (auto* emptyStatement = dynamic_cast<EmptyStatement*>(node)) {
					// A comment-only empty statement (the carrier for a decompiler warning) prints
					// no semicolon, so point its location at the comment it carries -- already
					// printed by now, so the statement lines up with the text the reader sees --
					// rather than leaving it at the empty `lastTokenEnd`.
					Trivia* trivia = nullptr;
					for (Trivia* t : emptyStatement->LeadingTrivia()) {
						if (!t->StartLocation().IsEmpty()) {
							trivia = t;
							break;
						}
					}
					if (trivia == nullptr) {
						for (Trivia* t : emptyStatement->TrailingTrivia()) {
							if (!t->StartLocation().IsEmpty()) {
								trivia = t;
								break;
							}
						}
					}
					emptyStatement->Location(trivia != nullptr ? trivia->StartLocation() : lastTokenEnd_);
				}
			}
			node->StorePrintEnd(lastTokenEnd_);
			for (AstNode* child : currentList_) {
				const CSharpSlotInfo* slot = child->Slot();
				// A parentless child is a print-only artifact with no slot in the tree; it cannot
				// be re-attached by kind, so leave it out rather than route a missing kind into the
				// throwing child setter. `Slot` is derived from the child's index in its parent,
				// so it is read before `Remove()` detaches the child.
				if (slot == nullptr)
					continue;
				const CSharpSlotInfo* kind = slot->Kind();
				child->Remove();
				node->AddChildUnsafe(child, kind);
			}
			currentList_ = std::move(nodes_.top());
			nodes_.pop();
		} else if (auto* comment = dynamic_cast<Comment*>(node)) {
			comment->SetEndLocation(locationProvider_->Location());
		}
		DecoratingTokenWriter::EndNode(node);
	}

	// The C# `public override void WriteToken(string token)` -- assign the pending start
	// locations (the nodes awaiting their first token get the current location); an
	// `EmptyStatement` or `ErrorExpression` as the top-most node gets its `Location` set at the
	// current position (the token it is printing); then forward and record the post-token end.
	void WriteToken(std::string_view token) override {
		AssignPendingStartLocations();
		AstNode* last = TopLastOrDefault();
		if (auto* emptyStatement = dynamic_cast<EmptyStatement*>(last)) {
			emptyStatement->Location(locationProvider_->Location());
		} else if (auto* errorExpression = dynamic_cast<ErrorExpression*>(last)) {
			errorExpression->Location(locationProvider_->Location());
		}
		DecoratingTokenWriter::WriteToken(token);
		lastTokenEnd_ = locationProvider_->Location();
	}

	// The C# `public override void WriteKeyword(string keyword)` -- assign the pending start
	// locations; the `this`/`base` keywords set the enclosing `ThisReferenceExpression`/
	// `BaseReferenceExpression` start location (the node is the top-most in its parent's list);
	// then forward and record the post-token end.
	void WriteKeyword(std::string_view keyword) override {
		AssignPendingStartLocations();
		TextLocation start = locationProvider_->Location();
		if (keyword == "this") {
			if (auto* node = dynamic_cast<ThisReferenceExpression*>(TopLastOrDefault()))
				node->StorePrintStart(start);
		} else if (keyword == "base") {
			if (auto* node = dynamic_cast<BaseReferenceExpression*>(TopLastOrDefault()))
				node->StorePrintStart(start);
		}
		DecoratingTokenWriter::WriteKeyword(keyword);
		lastTokenEnd_ = locationProvider_->Location();
	}

	// The C# `public override void WriteIdentifier(Identifier identifier)` -- assign the pending
	// start locations; record the identifier's start location; add the identifier to the current
	// child list (so it is re-attached to its parent at the enclosing `EndNode`); then forward
	// and record the post-token end.
	void WriteIdentifier(Identifier* identifier) override {
		AssignPendingStartLocations();
		identifier->SetStartLocation(locationProvider_->Location());
		currentList_.push_back(identifier);
		DecoratingTokenWriter::WriteIdentifier(identifier);
		lastTokenEnd_ = locationProvider_->Location();
	}

	// The C# `public override void WritePrimitiveValue(object?, LiteralFormat = None)` -- assign
	// the pending start locations; the enclosing `Expression` (the top-most node) gets its span
	// set: a `PrimitiveExpression` gets `SetLocation(start, end)`, a `NullReferenceExpression`
	// gets `StorePrintStart(start)`. The default `LiteralFormat::None` argument is NOT repeated
	// (the D316 default-arg-on-pure-virtual crux).
	void WritePrimitiveValue(const PrimitiveValue& value, LiteralFormat format) override {
		AssignPendingStartLocations();
		Expression* node = dynamic_cast<Expression*>(TopLastOrDefault());
		TextLocation startLocation = locationProvider_->Location();
		DecoratingTokenWriter::WritePrimitiveValue(value, format);
		if (auto* prim = dynamic_cast<PrimitiveExpression*>(node)) {
			prim->SetLocation(startLocation, locationProvider_->Location());
		}
		if (auto* nullref = dynamic_cast<NullReferenceExpression*>(node)) {
			nullref->StorePrintStart(startLocation);
		}
		lastTokenEnd_ = locationProvider_->Location();
	}

	// The C# `public override void WritePrimitiveType(string type)` -- assign the pending start
	// locations; the enclosing `PrimitiveType` (the top-most node) gets its start location set;
	// then forward and record the post-token end.
	void WritePrimitiveType(std::string_view type) override {
		AssignPendingStartLocations();
		if (auto* node = dynamic_cast<PrimitiveType*>(TopLastOrDefault()))
			node->StorePrintStart(locationProvider_->Location());
		DecoratingTokenWriter::WritePrimitiveType(type);
		lastTokenEnd_ = locationProvider_->Location();
	}

private:
	// The C# `void AssignPendingStartLocations()` -- every node awaiting its first token gets
	// its start location set to the current source location (the position of the next token to be
	// written), then the set is cleared. A no-op when nothing is pending.
	void AssignPendingStartLocations() {
		if (nodesAwaitingStartLocation_.empty())
			return;
		TextLocation location = locationProvider_->Location();
		for (AstNode* node : nodesAwaitingStartLocation_)
			node->StorePrintStart(location);
		nodesAwaitingStartLocation_.clear();
	}

	// The C# `nodes.Peek().LastOrDefault()` -- the last node in the top (parent) child list, or
	// null when the stack or the top list is empty. Defensive against an empty stack (the C#
	// `Peek()` would throw; the output visitor always starts the root before writing tokens, so
	// this is a graceful no-op for an unexpected empty stack rather than UB).
	AstNode* TopLastOrDefault() {
		if (nodes_.empty())
			return nullptr;
		const std::vector<AstNode*>& top = nodes_.top();
		return top.empty() ? nullptr : top.back();
	}

	// The C# `readonly Stack<List<AstNode>> nodes` -- the stack of per-node child lists; the top
	// is the immediate parent's list (the children printed inside the currently-open node).
	std::stack<std::vector<AstNode*>> nodes_;
	// The C# `List<AstNode> currentList` -- the children printed inside the currently-open node,
	// re-attached to it at its `EndNode`. Cleared (made a fresh empty list) at each non-trivia
	// `StartNode` after the parent list is copied onto the stack.
	std::vector<AstNode*> currentList_;
	// The C# `readonly ILocatable locationProvider` -- the current source location source (the
	// concrete `TextWriterTokenWriter` in the real pipeline).
	ILocatable* locationProvider_;
	// The C# `readonly HashSet<AstNode> nodesAwaitingStartLocation` -- nodes started but whose
	// first token has not been written yet; their start location is assigned lazily on the next
	// write.
	std::unordered_set<AstNode*> nodesAwaitingStartLocation_;
	// The C# `TextLocation lastTokenEnd` -- the position immediately after the most recently
	// written token; a node's end location is the end of its last token.
	TextLocation lastTokenEnd_;
};

}  // namespace OutputVisitor
}  // namespace ILSpy::Decompiler::CSharp

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTMISSINGTOKENSDECORATOR_HPP
