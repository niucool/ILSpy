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

// Tests for the `PrimitiveExpression` concrete node (cpp/.../Expressions/PrimitiveExpression.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.cs) -- the
// literal-carrying leaf expression. Exercises the `object Value` -> `std::variant` port (the
// `PrimitiveValue` alternatives, the `object.Equals` semantics reproduced by
// `std::variant::operator==`), the `AnyValue` pattern wildcard, the `LiteralFormat` scalar,
// the `SetLocation` span setter, the `AcceptVisitor` dispatch, and the per-concrete-node
// `Clone`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitPrimitiveExpression` (the role the
// generated per-node `Visit` override plays), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The leaf has no children, so the
// trace is the visited nodes.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("prim");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("null");
        VisitChildren(node);
    }
};

} // namespace

// ---- Construction -----------------------------------------------------

// The single-arg ctor stores the value (a boxed `int` 42) and defaults `Format` to None.
TEST(CSharp_PrimitiveExpression, CtorStoresValueAndDefaultsFormat) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(42)));
    ASSERT_TRUE(std::holds_alternative<std::int32_t>(e.Value()));
    EXPECT_EQ(std::get<std::int32_t>(e.Value()), 42);
    EXPECT_EQ(e.Format(), LiteralFormat::None);
}

// The two-arg ctor stores the value and the format.
TEST(CSharp_PrimitiveExpression, CtorWithFormatStoresBoth) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(255)), LiteralFormat::HexadecimalNumber);
    ASSERT_TRUE(std::holds_alternative<std::int32_t>(e.Value()));
    EXPECT_EQ(std::get<std::int32_t>(e.Value()), 255);
    EXPECT_EQ(e.Format(), LiteralFormat::HexadecimalNumber);
}

// ---- The value alternatives (the `object Value` -> `std::variant` port) -----

// A `bool` value is held as the `bool` alternative.
TEST(CSharp_PrimitiveExpression, HoldsBool) {
    PrimitiveExpression e(PrimitiveValue(true));
    ASSERT_TRUE(std::holds_alternative<bool>(e.Value()));
    EXPECT_EQ(std::get<bool>(e.Value()), true);
}

// A `char` value is held as the `char16_t` alternative (C# `char` is a UTF-16 code unit).
TEST(CSharp_PrimitiveExpression, HoldsChar) {
    PrimitiveExpression e(PrimitiveValue(char16_t{u'A'}));
    ASSERT_TRUE(std::holds_alternative<char16_t>(e.Value()));
    EXPECT_EQ(std::get<char16_t>(e.Value()), u'A');
}

// A `string` value is held as the `std::string` alternative.
TEST(CSharp_PrimitiveExpression, HoldsString) {
    PrimitiveExpression e(PrimitiveValue(std::string("hello")));
    ASSERT_TRUE(std::holds_alternative<std::string>(e.Value()));
    EXPECT_EQ(std::get<std::string>(e.Value()), "hello");
}

// A `double` value is held as the `double` alternative.
TEST(CSharp_PrimitiveExpression, HoldsDouble) {
    PrimitiveExpression e(PrimitiveValue(3.14));
    ASSERT_TRUE(std::holds_alternative<double>(e.Value()));
    EXPECT_DOUBLE_EQ(std::get<double>(e.Value()), 3.14);
}

// A `float` value is held as the `float` alternative (distinct from `double`).
TEST(CSharp_PrimitiveExpression, HoldsFloat) {
    PrimitiveExpression e(PrimitiveValue(1.5f));
    ASSERT_TRUE(std::holds_alternative<float>(e.Value()));
    EXPECT_FLOAT_EQ(std::get<float>(e.Value()), 1.5f);
}

// A `decimal` value is held as the `DecimalValue` alternative (the faithful System.Decimal
// model reused from the IL layer).
TEST(CSharp_PrimitiveExpression, HoldsDecimal) {
    PrimitiveExpression e(PrimitiveValue(DecimalValue::FromInt32(42)));
    ASSERT_TRUE(std::holds_alternative<DecimalValue>(e.Value()));
    EXPECT_EQ(std::get<DecimalValue>(e.Value()), DecimalValue::FromInt32(42));
}

// The C# integer types are distinct alternatives: `int` (int32), `uint` (uint32), `long`
// (int64), `ulong` (uint64). Each is held by its own alternative.
TEST(CSharp_PrimitiveExpression, HoldsIntegerAlternatives) {
    PrimitiveExpression i32(PrimitiveValue(std::int32_t(7)));
    PrimitiveExpression u32(PrimitiveValue(std::uint32_t(7)));
    PrimitiveExpression i64(PrimitiveValue(std::int64_t(7)));
    PrimitiveExpression u64(PrimitiveValue(std::uint64_t(7)));
    ASSERT_TRUE(std::holds_alternative<std::int32_t>(i32.Value()));
    ASSERT_TRUE(std::holds_alternative<std::uint32_t>(u32.Value()));
    ASSERT_TRUE(std::holds_alternative<std::int64_t>(i64.Value()));
    ASSERT_TRUE(std::holds_alternative<std::uint64_t>(u64.Value()));
}

// A null value (the C# `object` null) is held as `std::monostate` (the first alternative).
TEST(CSharp_PrimitiveExpression, HoldsNull) {
    PrimitiveExpression e(PrimitiveValue(std::monostate{}));
    ASSERT_TRUE(std::holds_alternative<std::monostate>(e.Value()));
}

// ---- Value / Format setters -------------------------------------------

// `Value` re-assigns the held variant.
TEST(CSharp_PrimitiveExpression, ValueSetterReassigns) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(1)));
    e.Value(PrimitiveValue(std::string("two")));
    ASSERT_TRUE(std::holds_alternative<std::string>(e.Value()));
    EXPECT_EQ(std::get<std::string>(e.Value()), "two");
}

// `Format` gets/sets the lexical format.
TEST(CSharp_PrimitiveExpression, FormatGetterSetter) {
    PrimitiveExpression e(PrimitiveValue(true));
    EXPECT_EQ(e.Format(), LiteralFormat::None);
    e.Format(LiteralFormat::DecimalNumber);
    EXPECT_EQ(e.Format(), LiteralFormat::DecimalNumber);
}

// ---- AnyValue (the pattern wildcard) ----------------------------------

// `AnyValue()` is a distinct alternative (`AnyValueTag`), not a real value.
TEST(CSharp_PrimitiveExpression, AnyValueIsDistinctAlternative) {
    const PrimitiveValue any = PrimitiveExpression::AnyValue();
    EXPECT_TRUE(std::holds_alternative<AnyValueTag>(any));
    // It is not a monostate (null) and not any value alternative.
    EXPECT_FALSE(std::holds_alternative<std::monostate>(any));
}

// ---- SetLocation ------------------------------------------------------

// `SetLocation` records both the start and the end span (reusing the inherited fields, so
// `StartLocation`/`EndLocation` return them without an override).
TEST(CSharp_PrimitiveExpression, SetLocationRecordsStartAndEnd) {
    PrimitiveExpression e(PrimitiveValue(true));
    e.SetLocation(TextLocation(3, 5), TextLocation(3, 9));
    EXPECT_EQ(e.StartLocation(), TextLocation(3, 5));
    EXPECT_EQ(e.EndLocation(), TextLocation(3, 9));
}

// A freshly-constructed node (no `SetLocation`) has `Empty` start/end.
TEST(CSharp_PrimitiveExpression, DefaultLocationIsEmpty) {
    PrimitiveExpression e(PrimitiveValue(true));
    EXPECT_EQ(e.StartLocation(), TextLocation::Empty);
    EXPECT_EQ(e.EndLocation(), TextLocation::Empty);
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitPrimitiveExpression` (the visitor-pattern round-trip).
TEST(CSharp_PrimitiveExpression, AcceptVisitorDispatchesToVisitPrimitiveExpression) {
    PrimitiveExpression e(PrimitiveValue(true));
    RecordingVisitor v;
    e.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_PrimitiveExpression, AcceptVisitorIsVirtualThroughAstNode) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(0)));
    AstNode* asAst = &e;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim"}));
}

// `AcceptVisitor` is virtual through `Expression*` too (the covariant base).
TEST(CSharp_PrimitiveExpression, AcceptVisitorIsVirtualThroughExpression) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(0)));
    Expression* asExpr = &e;
    RecordingVisitor v;
    asExpr->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim"}));
}

// `VisitChildren` walks the (zero) children -- a leaf records just itself.
TEST(CSharp_PrimitiveExpression, DepthFirstWalkRecordsJustTheLeaf) {
    PrimitiveExpression e(PrimitiveValue(true));
    RecordingVisitor v;
    e.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim"}));
}

// `PrimitiveExpression` is both an `AstNode` and an `Expression` (it derives via `Expression`).
TEST(CSharp_PrimitiveExpression, IsAstNodeAndExpression) {
    PrimitiveExpression e(PrimitiveValue(true));
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
}

// ---- DoMatch (the hand-written type + value match) -------------------

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Two `PrimitiveExpression`s with the same value (same type + equal) match.
TEST(CSharp_PrimitiveExpression, DoMatchMatchesSameValue) {
    PrimitiveExpression a(PrimitiveValue(std::int32_t(42)));
    PrimitiveExpression b(PrimitiveValue(std::int32_t(42)));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Two `PrimitiveExpression`s with different values (same type, different) do not match.
TEST(CSharp_PrimitiveExpression, DoMatchRejectsDifferentValue) {
    PrimitiveExpression a(PrimitiveValue(std::int32_t(42)));
    PrimitiveExpression b(PrimitiveValue(std::int32_t(7)));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// `object.Equals` is type-strict: a boxed `int` 0 and a boxed `long` 0 are NOT equal (different
// types), even though they are numerically equal. The `std::variant::operator==` reproduces
// this (the index -- the type -- is compared first).
TEST(CSharp_PrimitiveExpression, DoMatchIsTypeStrictIntVsLong) {
    PrimitiveExpression i32(PrimitiveValue(std::int32_t(0)));
    PrimitiveExpression i64(PrimitiveValue(std::int64_t(0)));
    EXPECT_FALSE(DoMatchAgainst(&i32, &i64));
    EXPECT_FALSE(DoMatchAgainst(&i64, &i32));
}

// A boxed `int` 0 and a boxed `double` 0.0 are NOT equal (different types).
TEST(CSharp_PrimitiveExpression, DoMatchIsTypeStrictIntVsDouble) {
    PrimitiveExpression i32(PrimitiveValue(std::int32_t(0)));
    PrimitiveExpression f64(PrimitiveValue(0.0));
    EXPECT_FALSE(DoMatchAgainst(&i32, &f64));
    EXPECT_FALSE(DoMatchAgainst(&f64, &i32));
}

// A `float` and a `double` with the same numeric value are NOT equal (different types).
TEST(CSharp_PrimitiveExpression, DoMatchIsTypeStrictFloatVsDouble) {
    PrimitiveExpression f32(PrimitiveValue(1.0f));
    PrimitiveExpression f64(PrimitiveValue(1.0));
    EXPECT_FALSE(DoMatchAgainst(&f32, &f64));
}

// Two strings match when equal, reject when different.
TEST(CSharp_PrimitiveExpression, DoMatchString) {
    PrimitiveExpression a(PrimitiveValue(std::string("x")));
    PrimitiveExpression b(PrimitiveValue(std::string("x")));
    PrimitiveExpression c(PrimitiveValue(std::string("y")));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&a, &c));
}

// Two bools match when equal.
TEST(CSharp_PrimitiveExpression, DoMatchBool) {
    PrimitiveExpression t(PrimitiveValue(true));
    PrimitiveExpression f(PrimitiveValue(false));
    PrimitiveExpression t2(PrimitiveValue(true));
    EXPECT_TRUE(DoMatchAgainst(&t, &t2));
    EXPECT_FALSE(DoMatchAgainst(&t, &f));
}

// Two decimals match when equal (the `DecimalValue::operator==`).
TEST(CSharp_PrimitiveExpression, DoMatchDecimal) {
    PrimitiveExpression a(PrimitiveValue(DecimalValue::FromInt32(5)));
    PrimitiveExpression b(PrimitiveValue(DecimalValue::FromInt32(5)));
    PrimitiveExpression c(PrimitiveValue(DecimalValue::FromInt32(6)));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&a, &c));
}

// `object.Equals(null, null)` is true: two null `PrimitiveExpression`s match.
TEST(CSharp_PrimitiveExpression, DoMatchNullVsNullMatches) {
    PrimitiveExpression a(PrimitiveValue(std::monostate{}));
    PrimitiveExpression b(PrimitiveValue(std::monostate{}));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A null and a non-null do not match (`object.Equals(null, x)` is false).
TEST(CSharp_PrimitiveExpression, DoMatchNullVsNonNullRejects) {
    PrimitiveExpression n(PrimitiveValue(std::monostate{}));
    PrimitiveExpression i(PrimitiveValue(std::int32_t(0)));
    EXPECT_FALSE(DoMatchAgainst(&n, &i));
    EXPECT_FALSE(DoMatchAgainst(&i, &n));
}

// A `PrimitiveExpression` does not match a different concrete type (the `other is
// PrimitiveExpression` gate).
TEST(CSharp_PrimitiveExpression, DoMatchRejectsDifferentType) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(0)));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&e, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &e));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_PrimitiveExpression, DoMatchRejectsNullCandidate) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(0)));
    EXPECT_FALSE(DoMatchAgainst(&e, nullptr));
}

// The `AnyValue` wildcard: a pattern whose `Value` is `AnyValue()` matches any candidate
// (the `this.Value == AnyValue` short-circuit), regardless of the candidate's value/type.
TEST(CSharp_PrimitiveExpression, DoMatchAnyValueWildcardMatchesAny) {
    PrimitiveExpression pattern(PrimitiveExpression::AnyValue());
    PrimitiveExpression i(PrimitiveValue(std::int32_t(42)));
    PrimitiveExpression s(PrimitiveValue(std::string("x")));
    PrimitiveExpression n(PrimitiveValue(std::monostate{}));
    PrimitiveExpression d(PrimitiveValue(3.14));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &i));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &s));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &n));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &d));
}

// The wildcard is asymmetric: a real value does not match an `AnyValue` candidate (the
// candidate's `Value` is `AnyValue`, but the pattern's is not, and `object.Equals` of a real
// value against the `AnyValue` object is false).
TEST(CSharp_PrimitiveExpression, DoMatchAnyValueWildcardIsAsymmetric) {
    PrimitiveExpression pattern(PrimitiveValue(std::int32_t(42)));
    PrimitiveExpression anyCandidate(PrimitiveExpression::AnyValue());
    EXPECT_FALSE(DoMatchAgainst(&pattern, &anyCandidate));
}

// `Format` is not compared by `DoMatch` (it is a lexical hint, not structural): two nodes
// with the same value but different `Format` still match.
TEST(CSharp_PrimitiveExpression, DoMatchExcludesFormat) {
    PrimitiveExpression a(PrimitiveValue(std::int32_t(255)), LiteralFormat::None);
    PrimitiveExpression b(PrimitiveValue(std::int32_t(255)), LiteralFormat::HexadecimalNumber);
    ASSERT_NE(a.Format(), b.Format());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` copies the value, the format, and the locations, and detaches.
TEST(CSharp_PrimitiveExpression, CloneCopiesValueFormatAndLocations) {
    PrimitiveExpression original(PrimitiveValue(std::int32_t(42)), LiteralFormat::HexadecimalNumber);
    original.SetLocation(TextLocation(2, 3), TextLocation(2, 5));
    std::unique_ptr<PrimitiveExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);  // a fresh node
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    ASSERT_TRUE(std::holds_alternative<std::int32_t>(copy->Value()));
    EXPECT_EQ(std::get<std::int32_t>(copy->Value()), 42);
    EXPECT_EQ(copy->Format(), LiteralFormat::HexadecimalNumber);
    EXPECT_EQ(copy->StartLocation(), TextLocation(2, 3));
    EXPECT_EQ(copy->EndLocation(), TextLocation(2, 5));
}

// `Clone` copies a string value (deep copy of the string by value).
TEST(CSharp_PrimitiveExpression, CloneCopiesStringValue) {
    PrimitiveExpression original(PrimitiveValue(std::string("hello")));
    std::unique_ptr<PrimitiveExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(copy->Value()));
    EXPECT_EQ(std::get<std::string>(copy->Value()), "hello");
}

// `Clone` copies a decimal value.
TEST(CSharp_PrimitiveExpression, CloneCopiesDecimalValue) {
    PrimitiveExpression original(PrimitiveValue(DecimalValue::FromInt32(99)));
    std::unique_ptr<PrimitiveExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_TRUE(std::holds_alternative<DecimalValue>(copy->Value()));
    EXPECT_EQ(std::get<DecimalValue>(copy->Value()), DecimalValue::FromInt32(99));
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`,
// dispatched to the concrete override (and downcastable to `PrimitiveExpression`).
TEST(CSharp_PrimitiveExpression, CloneIsVirtualThroughAstNode) {
    PrimitiveExpression original(PrimitiveValue(true));
    original.SetLocation(TextLocation(1, 1), TextLocation(1, 5));
    AstNode* node = &original;
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PrimitiveExpression*>(copy.get()), nullptr);
    EXPECT_EQ(copy->StartLocation(), TextLocation(1, 1));
    EXPECT_EQ(copy->EndLocation(), TextLocation(1, 5));
}

// `Clone` is covariant through `Expression*`: a call through an `Expression*` returns an
// `Expression*`.
TEST(CSharp_PrimitiveExpression, CloneIsCovariantThroughExpression) {
    PrimitiveExpression original(PrimitiveValue(true));
    Expression* node = &original;
    std::unique_ptr<Expression> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PrimitiveExpression*>(copy.get()), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A leaf `PrimitiveExpression` with no children passes the inherited `CheckInvariant` (the
// no-child case the base handles). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_PrimitiveExpression, CheckInvariantPassesOnLeaf) {
    PrimitiveExpression e(PrimitiveValue(true));
    e.CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) ------------------

// A leaf `PrimitiveExpression` reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_PrimitiveExpression, LeafHasNoChildren) {
    PrimitiveExpression e(PrimitiveValue(true));
    EXPECT_EQ(e.GetChildCount(), 0);
    EXPECT_FALSE(e.HasChildren());
    EXPECT_EQ(e.FirstChild(), nullptr);
    EXPECT_EQ(e.LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : e.Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);
}
