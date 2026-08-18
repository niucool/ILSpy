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

// Tests for the `IsKeyword` free helper (CSharpKeywordCheck.hpp) -- the factored-out
// `CSharpOutputVisitor.IsKeyword` static helper that decides whether an identifier must be
// rendered with a leading `@` (the verbatim-identifier prefix) because its name is a reserved
// C# keyword in the identifier's context. The helper is the D317 plan's resolution of the
// tangle where `TextWriterTokenWriter.WriteIdentifier` references the not-yet-ported
// `CSharpOutputVisitor.IsKeyword`; factoring it out lets the concrete `TextWriterTokenWriter`
// land before the 97k-line `CSharpOutputVisitor` pretty-printer. The context-dependent cases
// build small AST trees (a `QueryExpression` with a clause, an async/sync `LambdaExpression` /
// `AnonymousMethodExpression` / `MethodDeclaration` with a parameter) so the helper's
// `context.Ancestors` walk exercises the real ported `dynamic_cast` is-a tests.

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

#include "Decompiler/CSharp/OutputVisitor/CSharpKeywordCheck.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/QueryWhereClause.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AnonymousMethodExpression;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::LambdaExpression;
using ILSpy::Decompiler::CSharp::Syntax::MethodDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::ParameterDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::QueryExpression;
using ILSpy::Decompiler::CSharp::Syntax::QueryWhereClause;

namespace {

// A fresh `ParameterDeclaration` in a `unique_ptr` -- the test owns the node (the AST
// non-owning-raw-pointer model: adding it to a parent's collection re-parents it without
// transferring ownership, so the test must keep it alive for the duration of the `IsKeyword`
// call that walks its ancestor chain).
std::unique_ptr<ParameterDeclaration> makeParam() {
	return std::make_unique<ParameterDeclaration>();
}

// A fresh `QueryWhereClause` in a `unique_ptr` (a `QueryClause` that can be added to a
// `QueryExpression.Clauses` to give it a `QueryExpression` ancestor).
std::unique_ptr<QueryWhereClause> makeWhere() {
	return std::make_unique<QueryWhereClause>();
}

}  // namespace

// The unconditional keywords are reserved in every context (and with a null context).
TEST(CSharp_KeywordCheck, UnconditionalKeywordsAreKeywordsWithNullContext) {
	EXPECT_TRUE(IsKeyword("if"));
	EXPECT_TRUE(IsKeyword("class"));
	EXPECT_TRUE(IsKeyword("while"));
	EXPECT_TRUE(IsKeyword("namespace"));
	EXPECT_TRUE(IsKeyword("stackalloc"));
	EXPECT_TRUE(IsKeyword("foreach"));
	EXPECT_TRUE(IsKeyword("volatile"));
	EXPECT_TRUE(IsKeyword("true"));
	EXPECT_TRUE(IsKeyword("typeof"));
	EXPECT_TRUE(IsKeyword("unchecked"));
}

// Plain identifiers (and identifiers that only differ in case from a keyword) are not keywords.
TEST(CSharp_KeywordCheck, NonKeywordsAreNotKeywords) {
	EXPECT_FALSE(IsKeyword("foo"));
	EXPECT_FALSE(IsKeyword("Math"));
	EXPECT_FALSE(IsKeyword("myVar"));
	EXPECT_FALSE(IsKeyword("System"));
	EXPECT_FALSE(IsKeyword("int32"));
	EXPECT_FALSE(IsKeyword("asyncMethod"));
}

// The length gate: only 2..maxKeywordLength-char identifiers can be keywords.
TEST(CSharp_KeywordCheck, EmptyAndSingleCharNotKeywords) {
	EXPECT_FALSE(IsKeyword(""));
	EXPECT_FALSE(IsKeyword("a"));
	EXPECT_FALSE(IsKeyword("i"));
}

// `maxKeywordLength` is 10 (the longest keyword is "descending"); an 11-char lowercase
// non-keyword is rejected by the length gate before any set lookup.
TEST(CSharp_KeywordCheck, TooLongNotKeyword) {
	EXPECT_FALSE(IsKeyword("abcdefghijk"));
}

// The `identifier[0] < 'a'` gate rejects uppercase / digit / symbol starts before any set
// lookup, so "If"/"Class" (which would match case-insensitively) are not keywords -- the
// keyword tables are all lowercase, and the gate is an ASCII-lowercase fast-path.
TEST(CSharp_KeywordCheck, UppercaseStartNotKeyword) {
	EXPECT_FALSE(IsKeyword("If"));
	EXPECT_FALSE(IsKeyword("Class"));
	EXPECT_FALSE(IsKeyword("ABSTRACT"));
	EXPECT_FALSE(IsKeyword("1abc"));
	EXPECT_FALSE(IsKeyword("@foo"));
}

// With a null context every query keyword is treated as unconditional (the C# `context == null
// || ...` short-circuit).
TEST(CSharp_KeywordCheck, QueryKeywordNullContextIsKeyword) {
	EXPECT_TRUE(IsKeyword("from"));
	EXPECT_TRUE(IsKeyword("select"));
	EXPECT_TRUE(IsKeyword("where"));
	EXPECT_TRUE(IsKeyword("by"));
	EXPECT_TRUE(IsKeyword("ascending"));
	EXPECT_TRUE(IsKeyword("descending"));
	EXPECT_TRUE(IsKeyword("group"));
	EXPECT_TRUE(IsKeyword("into"));
}

// A query keyword with a non-null context whose ancestors do NOT include a `QueryExpression`
// is NOT a keyword. A standalone `QueryWhereClause` (no parent) has an empty ancestor chain.
TEST(CSharp_KeywordCheck, QueryKeywordNoQueryAncestorNotKeyword) {
	auto where = makeWhere();
	ASSERT_EQ(where->Ancestors().size(), 0u);
	EXPECT_FALSE(IsKeyword("select", where.get()));
	EXPECT_FALSE(IsKeyword("from", where.get()));
	EXPECT_FALSE(IsKeyword("ascending", where.get()));
}

// A query keyword whose ancestor chain includes a `QueryExpression` IS a keyword. Build a
// `QueryExpression`, add a `QueryWhereClause` to its `Clauses`; the clause's ancestor chain is
// `[QueryExpression]`.
TEST(CSharp_KeywordCheck, QueryKeywordInsideQueryExpressionIsKeyword) {
	auto query = std::make_unique<QueryExpression>();
	auto where = makeWhere();
	query->Clauses().Add(where.get());
	ASSERT_EQ(where->Ancestors().size(), 1u);
	EXPECT_TRUE(IsKeyword("select", where.get()));
	EXPECT_TRUE(IsKeyword("from", where.get()));
	EXPECT_TRUE(IsKeyword("ascending", where.get()));
}

// An unconditional keyword is a keyword in every context (the unconditional set is checked
// before the context-dependent query/await branches), so "class" inside a query is still a
// keyword.
TEST(CSharp_KeywordCheck, UnconditionalKeywordInsideQueryStillKeyword) {
	auto query = std::make_unique<QueryExpression>();
	auto where = makeWhere();
	query->Clauses().Add(where.get());
	EXPECT_TRUE(IsKeyword("class", where.get()));
	EXPECT_TRUE(IsKeyword("while", where.get()));
}

// With a null context "await" is treated as unconditional.
TEST(CSharp_KeywordCheck, AwaitNullContextIsKeyword) {
	EXPECT_TRUE(IsKeyword("await"));
}

// A standalone `ParameterDeclaration` (no parent) has no lambda / anonymous-method / member
// ancestor, so "await" is not a keyword.
TEST(CSharp_KeywordCheck, AwaitNoAsyncAncestorNotKeyword) {
	auto param = makeParam();
	ASSERT_EQ(param->Ancestors().size(), 0u);
	EXPECT_FALSE(IsKeyword("await", param.get()));
}

// "await" inside an async lambda IS a keyword. The parameter's first ancestor is the lambda,
// and the lambda's `IsAsync` decides.
TEST(CSharp_KeywordCheck, AwaitInsideAsyncLambdaIsKeyword) {
	auto lambda = std::make_unique<LambdaExpression>();
	lambda->IsAsync(true);
	auto param = makeParam();
	lambda->Parameters().Add(param.get());
	ASSERT_EQ(param->Ancestors().size(), 1u);
	EXPECT_TRUE(IsKeyword("await", param.get()));
}

// "await" inside a non-async lambda is NOT a keyword.
TEST(CSharp_KeywordCheck, AwaitInsideSyncLambdaNotKeyword) {
	auto lambda = std::make_unique<LambdaExpression>();
	lambda->IsAsync(false);
	auto param = makeParam();
	lambda->Parameters().Add(param.get());
	EXPECT_FALSE(IsKeyword("await", param.get()));
}

// "await" inside an async anonymous method IS a keyword.
TEST(CSharp_KeywordCheck, AwaitInsideAsyncAnonymousMethodIsKeyword) {
	auto anon = std::make_unique<AnonymousMethodExpression>();
	anon->IsAsync(true);
	auto param = makeParam();
	anon->Parameters().Add(param.get());
	EXPECT_TRUE(IsKeyword("await", param.get()));
}

// "await" inside a non-async anonymous method is NOT a keyword.
TEST(CSharp_KeywordCheck, AwaitInsideSyncAnonymousMethodNotKeyword) {
	auto anon = std::make_unique<AnonymousMethodExpression>();
	anon->IsAsync(false);
	auto param = makeParam();
	anon->Parameters().Add(param.get());
	EXPECT_FALSE(IsKeyword("await", param.get()));
}

// "await" inside a member declaration carrying the `Async` modifier IS a keyword. The
// `MethodDeclaration` (an `EntityDeclaration`) ancestor's `Modifiers & Async` decides.
TEST(CSharp_KeywordCheck, AwaitInsideAsyncMethodDeclarationIsKeyword) {
	auto method = std::make_unique<MethodDeclaration>();
	method->Modifiers(Modifiers::Async);
	auto param = makeParam();
	method->Parameters().Add(param.get());
	EXPECT_TRUE(IsKeyword("await", param.get()));
}

// "await" inside a member declaration WITHOUT the `Async` modifier is NOT a keyword.
TEST(CSharp_KeywordCheck, AwaitInsideSyncMethodDeclarationNotKeyword) {
	auto method = std::make_unique<MethodDeclaration>();
	method->Modifiers(Modifiers::None);
	auto param = makeParam();
	method->Parameters().Add(param.get());
	EXPECT_FALSE(IsKeyword("await", param.get()));
}

// The default `context = nullptr` argument: a bare `IsKeyword("await")` call is equivalent to
// `IsKeyword("await", nullptr)` (the unconditional treatment).
TEST(CSharp_KeywordCheck, DefaultContextIsNullptr) {
	EXPECT_TRUE(IsKeyword(std::string_view("await")));
	EXPECT_TRUE(IsKeyword(std::string_view("class")));
	EXPECT_FALSE(IsKeyword(std::string_view("foo")));
}
