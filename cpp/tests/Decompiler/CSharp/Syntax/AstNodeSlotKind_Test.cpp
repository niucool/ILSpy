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

// Tests for the kind-based slot accessors added to the `AstNode` base
// (`GetChild<T>(CSharpSlotInfoT<T>*)` / `SetChild<T>(CSharpSlotInfoT<T>*, T*)`, the port of
// the C# `public T? GetChild<T>(CSharpSlotInfo<T>)` / `protected void SetChild<T>(CSharpSlotInfo<T>, T?)`)
// -- the single-slot read/write by canonical `Slots` kind that the hand-written
// `Name`/`NameToken`/`ReturnType` virtuals on `EntityDeclaration` (and the output/resolver
// stage) build on. Exercises the kind walk against the real ported concrete nodes
// (`SimpleType`, `UnaryOperatorExpression`, `BinaryOperatorExpression`), the null return for
// a kind the node declares no slot of, the `dynamic_cast` downcast (faithful to the C# `as T`),
// the collection-slot first-element return, and the `SetChildByKindUntyped` throw on a
// missing kind. Also tests the `SymbolKind` enum (cpp/.../TypeSystem/SymbolKind.hpp, the port
// of ICSharpCode.Decompiler/TypeSystem/ISymbol.cs) -- the Phase-2 `TypeSystem` dependency the
// `EntityDeclaration.SymbolKind` abstract property (the next in-order TypeMember piece) needs.
//
// The C# `GetChild<T>`/`SetChild<T>` kind-based overloads are named `GetChildByKind<T>`/
// `SetChildByKind<T>` in the C++ port because the C# `GetChild`/`SetChild` are also
// overloaded with the non-generic `internal AstNode? GetChild(int)` / `SetChild(int, ...)`
// slot-storage virtuals; a C++ member template sharing that name with the non-template
// `GetChild(int)`/`SetChild(int, ...)` makes MSVC's permissive-mode parser treat the `<T>` as
// a less-than rather than a template-argument list. The template-only `GetChildByKind<T>`/
// `SetChildByKind<T>` (paralleling the existing `GetCollectionByKind`) avoid the collision.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

// ---- The kind-based slot accessors on `AstNode` ------------------------------

TEST(CSharp_SlotKindAccessors, GetChildReturnsSingleSlotByKind) {
	std::unique_ptr<Identifier> token(Identifier::Create("Foo"));
	ASSERT_NE(token, nullptr);
	SimpleType st(token.get());
	// The `Identifier`-kind slot is at flattened index 0; `GetChild` walks the slot storage
	// and returns the child at the first slot whose `Kind()` is `Slots::Identifier`. The
	// `template` disambiguator parses `<Identifier>` as the template argument (the name
	// `GetChild` is shared with the non-template `GetChild(int)` slot-storage virtual).
	Identifier* got = st.GetChildByKind<Identifier>(&Slots::Identifier);
	ASSERT_NE(got, nullptr);
	EXPECT_EQ(got, token.get());
	EXPECT_EQ(got->Name(), "Foo");
}

TEST(CSharp_SlotKindAccessors, GetChildReturnsNullForAbsentKind) {
	std::unique_ptr<Identifier> token(Identifier::Create("Foo"));
	SimpleType st(token.get());
	// `SimpleType` declares no `Expression`-kind slot (its slots are `Identifier` +
	// `TypeArgument`); the kind walk finds no match and returns null (the C# default `T?`).
	EXPECT_EQ(st.GetChildByKind<Expression>(&Slots::Expression), nullptr);
	// `UnaryOperatorExpression` declares no `Identifier`-kind slot either.
	UnaryOperatorExpression uo;
	EXPECT_EQ(uo.GetChildByKind<Identifier>(&Slots::Identifier), nullptr);
}

TEST(CSharp_SlotKindAccessors, GetChildDowncastsStoredChildToElementType) {
	std::unique_ptr<NullReferenceExpression> operand = std::make_unique<NullReferenceExpression>();
	UnaryOperatorExpression uo(operand.get(), UnaryOperatorType::Any);
	// `GetChildByKind<Expression>` does a `dynamic_cast<Expression*>` on the stored child
	// (an `AstNode*` returned by `GetChild(i)`); the operand is a `NullReferenceExpression`
	// (an `Expression`), so the downcast succeeds and returns it (faithful to the C#
	// `GetChild(i) as T`). The `Expression` element type is deduced from the slot
	// (`Slots::Expression` is a `CSharpSlotInfoT<Expression>`), matching the C# generic
	// `GetChild<T>(CSharpSlotInfo<T>)` where `T` is the slot's element type, so a call
	// passing an `Expression` slot can only ask for `Expression` (not a more-derived type).
	Expression* got = uo.GetChildByKind<Expression>(&Slots::Expression);
	ASSERT_NE(got, nullptr);
	EXPECT_EQ(got, operand.get());
	// The returned `Expression*` is the operand (a `NullReferenceExpression`); a further
	// downcast recovers the concrete type, confirming `GetChildByKind` returned the stored
	// child rather than a stale or null view.
	EXPECT_EQ(dynamic_cast<NullReferenceExpression*>(got), operand.get());
}

TEST(CSharp_SlotKindAccessors, GetChildReturnsNullForEmptySlot) {
	// An `UnaryOperatorExpression` with no operand: the `Expression`-kind slot exists but
	// `GetChild(0)` is null, so `dynamic_cast<...>(nullptr)` returns null.
	UnaryOperatorExpression uo;
	EXPECT_EQ(uo.GetChildByKind<Expression>(&Slots::Expression), nullptr);
}

TEST(CSharp_SlotKindAccessors, GetChildDistinguishesLeftFromRightByKind) {
	std::unique_ptr<NullReferenceExpression> left = std::make_unique<NullReferenceExpression>();
	std::unique_ptr<NullReferenceExpression> right = std::make_unique<NullReferenceExpression>();
	BinaryOperatorExpression bo(left.get(), BinaryOperatorType::Any, right.get());
	// `BinaryOperatorExpression` has two `Expression`-typed single slots of distinct kinds
	// (`Left` and `Right`); `GetChild` returns the one whose `Kind()` matches.
	EXPECT_EQ(bo.GetChildByKind<Expression>(&Slots::Left), left.get());
	EXPECT_EQ(bo.GetChildByKind<Expression>(&Slots::Right), right.get());
	// `GetChild` on the `Expression`-kind (which neither slot uses) finds no match.
	EXPECT_EQ(bo.GetChildByKind<Expression>(&Slots::Expression), nullptr);
}

TEST(CSharp_SlotKindAccessors, GetChildReturnsFirstCollectionElementByKind) {
	std::unique_ptr<Identifier> token(Identifier::Create("List"));
	SimpleType st(token.get());
	std::unique_ptr<PrimitiveType> arg = std::make_unique<PrimitiveType>();
	arg->Keyword("int");
	st.TypeArguments().Add(arg.get());
	// A collection slot's per-position `GetChildSlotInfo` returns the collection's slot, whose
	// `Kind()` is the collection kind (`TypeArgument`); `GetChild` returns the FIRST element.
	EXPECT_EQ(st.GetChildByKind<AstType>(&Slots::TypeArgument), arg.get());
}

TEST(CSharp_SlotKindAccessors, GetChildReturnsNullForEmptyCollectionByKind) {
	std::unique_ptr<Identifier> token(Identifier::Create("List"));
	SimpleType st(token.get());
	// An empty `TypeArguments` collection has no children at the `TypeArgument`-kind slot, so
	// the kind walk finds no match and returns null.
	EXPECT_EQ(st.GetChildByKind<AstType>(&Slots::TypeArgument), nullptr);
}

TEST(CSharp_SlotKindAccessors, SetChildWritesSingleSlotByKind) {
	std::unique_ptr<NullReferenceExpression> oldOp = std::make_unique<NullReferenceExpression>();
	UnaryOperatorExpression uo(oldOp.get(), UnaryOperatorType::Any);
	ASSERT_EQ(uo.GetChildByKind<Expression>(&Slots::Expression), oldOp.get());
	// `SetChild` writes the `Expression`-kind slot by kind (delegating to
	// `SetChildByKindUntyped`), replacing the operand; the old child is detached.
	std::unique_ptr<NullReferenceExpression> newOp = std::make_unique<NullReferenceExpression>();
	uo.SetChildByKind<Expression>(&Slots::Expression, newOp.get());
	EXPECT_EQ(uo.GetChildByKind<Expression>(&Slots::Expression), newOp.get());
	EXPECT_EQ(newOp->Parent(), &uo);
	EXPECT_EQ(oldOp->Parent(), nullptr);
}

TEST(CSharp_SlotKindAccessors, SetChildClearsSingleSlotByKind) {
	std::unique_ptr<NullReferenceExpression> op = std::make_unique<NullReferenceExpression>();
	UnaryOperatorExpression uo(op.get(), UnaryOperatorType::Any);
	ASSERT_NE(uo.GetChildByKind<Expression>(&Slots::Expression), nullptr);
	// `SetChild` with null clears the slot (the C# `T? newChild` null clears it).
	uo.SetChildByKind<Expression>(&Slots::Expression, nullptr);
	EXPECT_EQ(uo.GetChildByKind<Expression>(&Slots::Expression), nullptr);
	EXPECT_EQ(op->Parent(), nullptr);
}

TEST(CSharp_SlotKindAccessors, SetChildThrowsForAbsentKind) {
	std::unique_ptr<Identifier> token(Identifier::Create("Foo"));
	SimpleType st(token.get());
	std::unique_ptr<NullReferenceExpression> stray = std::make_unique<NullReferenceExpression>();
	// `SimpleType` declares no `Expression`-kind slot; `SetChildByKindUntyped` (via
	// `SetChildByKind<Expression>`) throws `std::logic_error` ("no slot of this kind") rather
	// than silently no-op'ing. The stray is an `Expression` so the `SetChildByKind<Expression>`
	// argument converts (the throw is on the missing kind, not the type).
	EXPECT_THROW(st.SetChildByKind<Expression>(&Slots::Expression, stray.get()), std::logic_error);
	// The stray node was not adopted (it has no parent).
	EXPECT_EQ(stray->Parent(), nullptr);
}

// ---- The `SymbolKind` enum --------------------------------------------------

TEST(CSharp_SymbolKind, NoneIsZero) {
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::None), 0);
}

TEST(CSharp_SymbolKind, ReturnTypeIsLast) {
	// The 18 members (None .. ReturnType) mirror the C# declaration order; the last member
	// `ReturnType` is value 17 (one-indexed past `None` at 0).
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::ReturnType), 17);
}

TEST(CSharp_SymbolKind, IsByteSized) {
	// The C# underlying type is `byte`; the port keeps `std::uint8_t`.
	EXPECT_EQ(sizeof(SymbolKind), 1u);
	static_assert(std::is_enum_v<SymbolKind>);
	static_assert(std::is_same_v<std::underlying_type_t<SymbolKind>, std::uint8_t>);
}

TEST(CSharp_SymbolKind, MembersAreDistinctAndOrdered) {
	EXPECT_NE(static_cast<std::uint8_t>(SymbolKind::Method),
		static_cast<std::uint8_t>(SymbolKind::Field));
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::Field), 3);
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::Method), 7);
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::Destructor), 10);
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::Parameter), 14);
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::TypeParameter), 15);
	EXPECT_EQ(static_cast<std::uint8_t>(SymbolKind::Constraint), 16);
}
