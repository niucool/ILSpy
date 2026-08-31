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

// Tests for the `AstNode` formatted-output rendering -- `ToString` (the port of
// ICSharpCode.Decompiler/CSharp/Syntax/AstNode.cs lines 1006-1018) plus the plain-text
// fast-path overrides the hand-written partials declare (`ComposedType.ToString`
// ComposedType.cs line 79, `ArraySpecifier.ToString` ComposedType.cs line 150,
// `PrimitiveType.ToString` PrimitiveType.cs line 83) -- and the `AstType.Create`
// dotted-name chain builder (AstType.cs line 131).
//
// These are the two direct prerequisites of `CSharpAmbience` (the next in-order
// output-stage file): `CSharpAmbience.ConvertType`/`ConvertVariable` render their
// converted AST nodes through `ToString()`, and `WriteQualifiedName` builds the namespace
// name chain through `AstType.Create`.
//
// The base `ToString(formattingOptions)` accepts a `CSharpOutputVisitor` over a string
// sink (a null policy takes the `FormattingOptionsFactory.CreateMono()` defaults); the
// three overrides are FAST PATHS that build the plain text directly and ignore the policy
// parameter entirely (faithfully -- the C# never consults it). The returned nodes follow
// the D223 non-owning leak model (`new`-ed raw pointers; the tests never free them).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"
#include "Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
namespace OV = ILSpy::Decompiler::CSharp::OutputVisitor;

namespace {

// A fresh heap-allocated `PrimitiveType("int")` -- the receiver the composed-type tests
// wrap (heap-allocated so the wrapped node's `Parent()` back-pointer stays valid for the
// test's lifetime; the leak model never frees it). The `PrimitiveType` base (not a
// `SimpleType("int")`) is what renders the bare keyword: a keyword-named `SimpleType`
// renders VERBATIM-ESCAPED (`@int`) through the output visitor's identifier writer.
PrimitiveType* MakeIntType() {
    return new PrimitiveType(std::string("int"));
}

// The composed node wrapping `MakeIntType()` in one pointer rank (the `Make*` builder
// composition the rendering tests render).
ComposedType* MakeIntPointer() {
    return dynamic_cast<ComposedType*>(MakeIntType()->MakePointerType());
}

} // namespace

// ---- The base output-visitor path ---------------------------------------------

// A literal renders its value: the base `ToString()` accepts a `CSharpOutputVisitor`
// over the string sink and returns the rendered text.
TEST(CSharp_AstNodeToString, PrimitiveExpressionRendersLiteralValue) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(42)));
    EXPECT_EQ(e.ToString(), "42");
}

// A type reference renders its name (a further base-path shape: the identifier walk of
// `VisitSimpleType`).
TEST(CSharp_AstNodeToString, SimpleTypeRendersName) {
    SimpleType t(std::string("System"));
    EXPECT_EQ(t.ToString(), "System");
}

// A keyword-named identifier renders VERBATIM-ESCAPED: the output visitor's identifier
// writer prefixes `@` when the identifier name is a keyword in its context (the C#
// `TextWriterTokenWriter.WriteIdentifier`'s `IsVerbatim || IsKeyword(name)` -- the faithful
// C# behavior: `new SimpleType("int").ToString()` is `"@int"`). The `PrimitiveType("int")`
// is the shape that renders the bare keyword (its own fast path returns it verbatim).
TEST(CSharp_AstNodeToString, KeywordNamedSimpleTypeRendersVerbatimEscaped) {
    SimpleType t(std::string("int"));
    EXPECT_EQ(t.ToString(), "@int");
}

// The 1-arg `ToString(CSharpFormattingOptions*)` overload exists alongside the no-arg
// form and renders through the given policy.
TEST(CSharp_AstNodeToString, ToStringWithExplicitPolicyRendersThroughTheVisitor) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(42)));
    OV::CSharpFormattingOptions policy = OV::FormattingOptionsFactory::CreateEmpty();
    EXPECT_EQ(e.ToString(&policy), "42");
}

// A null policy takes the Mono defaults (the C# `formattingOptions ?? CreateMono()`),
// producing the same rendering as the no-arg form for a simple literal.
TEST(CSharp_AstNodeToString, ToStringWithNullPolicyTakesTheMonoDefaults) {
    PrimitiveExpression e(PrimitiveValue(std::int32_t(42)));
    EXPECT_EQ(e.ToString(nullptr), e.ToString());
    EXPECT_EQ(e.ToString(nullptr), "42");
}

// ---- The `ComposedType` plain-text fast path ------------------------------------

// A pointer type renders `int*` (the base type's own `ToString()` plus the `*` run).
TEST(CSharp_AstNodeToString, ComposedTypeRendersPointerRun) {
    ComposedType* composed = MakeIntPointer();
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(composed->ToString(), "int*");
}

// A second `MakePointerType` EXTENDS the pointer run in place (`PointerRank` 2), so the
// fast path renders `int**` -- one node, two `*`s.
TEST(CSharp_AstNodeToString, ComposedTypeExtendsPointerRunInPlace) {
    ComposedType* composed = MakeIntPointer();
    ASSERT_NE(composed, nullptr);
    AstType* extended = composed->MakePointerType();
    EXPECT_EQ(extended, static_cast<AstType*>(composed));  // in-place: the same node
    EXPECT_EQ(composed->ToString(), "int**");
}

// The nullable specifier renders as the trailing `?`.
TEST(CSharp_AstNodeToString, ComposedTypeRendersNullableSpecifier) {
    AstType* nullable = MakeIntType()->MakeNullableType();
    EXPECT_EQ(nullable->ToString(), "int?");
}

// The array rank specifiers render as `[` + (Dimensions - 1) commas + `]` each: rank 1
// yields `[]`, rank 3 yields `[,,]`.
TEST(CSharp_AstNodeToString, ComposedTypeRendersArrayRank) {
    EXPECT_EQ(MakeIntType()->MakeArrayType()->ToString(), "int[]");
    EXPECT_EQ(MakeIntType()->MakeArrayType(3)->ToString(), "int[,,]");
}

// The leading `ref `/`readonly ` prefixes render before the base type, and both may be
// combined (`ref readonly int`).
TEST(CSharp_AstNodeToString, ComposedTypeRendersRefAndReadonlyPrefixes) {
    EXPECT_EQ(MakeIntType()->MakeRefType()->ToString(), "ref int");
    ComposedType* readonlyComposed =
        dynamic_cast<ComposedType*>(MakeIntType()->MakeRefType());
    ASSERT_NE(readonlyComposed, nullptr);
    readonlyComposed->HasRefSpecifier(false);
    readonlyComposed->HasReadOnlySpecifier(true);
    EXPECT_EQ(readonlyComposed->ToString(), "readonly int");
    readonlyComposed->HasRefSpecifier(true);
    EXPECT_EQ(readonlyComposed->ToString(), "ref readonly int");
}

// A pointer to an array is a FRESH wrapper (the `MakePointerType` array fallback: a
// pointer to an array is never one node with both a `*` and rank specifiers), rendering
// `int[]*`.
TEST(CSharp_AstNodeToString, ComposedTypeRendersArrayThenPointer) {
    AstType* arrayPointer = MakeIntType()->MakeArrayType()->MakePointerType();
    EXPECT_EQ(arrayPointer->ToString(), "int[]*");
}

// The fast path IGNORES the formatting options parameter entirely: an explicit policy
// renders the same text as the no-arg call.
TEST(CSharp_AstNodeToString, FastPathIgnoresTheFormattingOptions) {
    ComposedType* composed = MakeIntPointer();
    ASSERT_NE(composed, nullptr);
    OV::CSharpFormattingOptions policy = OV::FormattingOptionsFactory::CreateEmpty();
    EXPECT_EQ(composed->ToString(&policy), composed->ToString());
    EXPECT_EQ(composed->ToString(&policy), "int*");
}

// The no-arg form delegates to the VIRTUAL 1-arg form, so the call through an
// `AstNode*` base pointer reaches the `ComposedType` fast-path override (the base's
// output-visitor path would render the same text but through the visitor).
TEST(CSharp_AstNodeToString, VirtualDispatchThroughTheAstNodeBase) {
    AstNode* node = MakeIntPointer();
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->ToString(), "int*");
}

// ---- The `ArraySpecifier` / `PrimitiveType` fast paths ---------------------------

// A rank specifier renders `[` + (Dimensions - 1) commas + `]` directly: rank 1 is `[]`,
// rank 3 is `[,,]`.
TEST(CSharp_AstNodeToString, ArraySpecifierRendersRank) {
    ArraySpecifier rank1(1);
    EXPECT_EQ(rank1.ToString(), "[]");
    ArraySpecifier rank3(3);
    EXPECT_EQ(rank3.ToString(), "[,,]");
}

// A built-in-type reference renders just its keyword.
TEST(CSharp_AstNodeToString, PrimitiveTypeRendersKeyword) {
    PrimitiveType pt(std::string("int"));
    EXPECT_EQ(pt.ToString(), "int");
}

// ---- `AstType::Create` ------------------------------------------------------------

// A single-part dotted name is the `SimpleType` head alone.
TEST(CSharp_AstTypeCreate, SinglePartYieldsSimpleType) {
    AstType* type = AstType::Create("System");
    auto* simple = dynamic_cast<SimpleType*>(type);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("System"));
    EXPECT_EQ(type->ToString(), "System");
}

// A multi-part dotted name builds the OUTSIDE-IN `MemberType` chain: the LAST part is
// the outermost `MemberType`, each `Target` pointing inward, ending at the `SimpleType`
// head.
TEST(CSharp_AstTypeCreate, MultiPartBuildsTheMemberTypeChain) {
    AstType* type = AstType::Create("System.Collections.Generic");
    auto* outer = dynamic_cast<MemberType*>(type);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->MemberName(), "Generic");
    auto* middle = dynamic_cast<MemberType*>(outer->Target());
    ASSERT_NE(middle, nullptr);
    EXPECT_EQ(middle->MemberName(), "Collections");
    auto* head = dynamic_cast<SimpleType*>(middle->Target());
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(head->Identifier(), std::optional<std::string>("System"));
}

// The chain renders back to the dotted name through the base output-visitor path.
TEST(CSharp_AstTypeCreate, MultiPartRendersTheDottedName) {
    EXPECT_EQ(AstType::Create("System.Collections.Generic")->ToString(),
              "System.Collections.Generic");
}

// The C# `string.Split('.')` semantics on an empty input: one empty part, so the result
// is the bare `SimpleType` head with a null identifier token (the
// `Identifier.CreateIfNotEmpty` no-token shape).
TEST(CSharp_AstTypeCreate, EmptyNameYieldsSingleEmptyPart) {
    AstType* type = AstType::Create("");
    auto* simple = dynamic_cast<SimpleType*>(type);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::nullopt);
}

// The C# `string.Split('.')` semantics on a trailing dot: a trailing EMPTY part wraps
// the head in a `MemberType` carrying the empty member name (a real token with an empty
// `Name`, not a null token -- the non-nullable `MemberName` always creates one).
TEST(CSharp_AstTypeCreate, TrailingDotYieldsTrailingEmptyPart) {
    AstType* type = AstType::Create("A.");
    auto* outer = dynamic_cast<MemberType*>(type);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->MemberName(), "");
    auto* head = dynamic_cast<SimpleType*>(outer->Target());
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(head->Identifier(), std::optional<std::string>("A"));
}
