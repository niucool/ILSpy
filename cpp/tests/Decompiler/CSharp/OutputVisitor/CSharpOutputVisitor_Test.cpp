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

// Tests for the `CSharpOutputVisitor` infrastructure skeleton
// (OutputVisitor/CSharpOutputVisitor.hpp) -- the first in-order slice of the C# pretty-printer.
// The class implements `IAstVisitor` (130 `Visit` methods), but only the infrastructure helpers
// (the two ctors, `StartNode`/`EndNode`, the `Comma` family, the token/brace writers, the
// write-construct helpers) are ported; the 130 `Visit` methods are throwing stubs (verified here
// to throw `std::logic_error`). The infrastructure is exercised directly through a recording
// inner `TokenWriter` (the visitor's second ctor wraps it in an `InsertRequiredSpacesDecorator`;
// the decorator only inserts a space where two tokens would merge, so the recorded call sequence
// for the punctuation/keyword/newline calls under test is exactly what the visitor wrote). This is
// the next in-order Phase-5 piece of the output stage per the D322 decision-log entry.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/CSharpOutputVisitor.hpp"
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UncheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TupleExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/WithInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/ParenthesizedVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorInitializer.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/TupleTypeElement.hpp"
#include "Decompiler/CSharp/Syntax/TupleAstType.hpp"
#include "Decompiler/CSharp/Syntax/InvocationAstType.hpp"
#include "Decompiler/CSharp/Syntax/FunctionPointerAstType.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/FixedFieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/FixedVariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/EnumMemberDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ExtensionDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/CustomEventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/IndexerDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/UsingAliasDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/UsingDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ExternAliasDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/CheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UncheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UnsafeStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SwitchExpression.hpp"
#include "Decompiler/CSharp/Syntax/SwitchExpressionSection.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::AstType;
using ILSpy::Decompiler::CSharp::Syntax::ArraySpecifier;
using ILSpy::Decompiler::CSharp::Syntax::BlockStatement;
using ILSpy::Decompiler::CSharp::Syntax::BreakStatement;
using ILSpy::Decompiler::CSharp::Syntax::BaseReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::Attribute;
using ILSpy::Decompiler::CSharp::Syntax::AttributeSection;
using ILSpy::Decompiler::CSharp::Syntax::Comment;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::ContinueStatement;
using ILSpy::Decompiler::CSharp::Syntax::GotoCaseStatement;
using ILSpy::Decompiler::CSharp::Syntax::GotoDefaultStatement;
using ILSpy::Decompiler::CSharp::Syntax::GotoStatement;
using ILSpy::Decompiler::CSharp::Syntax::IfElseStatement;
using ILSpy::Decompiler::CSharp::Syntax::LabelStatement;
using ILSpy::Decompiler::CSharp::Syntax::EmptyStatement;
using ILSpy::Decompiler::CSharp::Syntax::ExpressionStatement;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirective;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::ReturnStatement;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::ThisReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::YieldBreakStatement;
using ILSpy::Decompiler::CSharp::Syntax::YieldReturnStatement;
using ILSpy::Decompiler::CSharp::Syntax::WhileStatement;
using ILSpy::Decompiler::CSharp::Syntax::DoWhileStatement;
using ILSpy::Decompiler::CSharp::Syntax::ForStatement;
using ILSpy::Decompiler::CSharp::Syntax::ForeachStatement;
using ILSpy::Decompiler::CSharp::Syntax::LockStatement;
using ILSpy::Decompiler::CSharp::Syntax::UsingStatement;
using ILSpy::Decompiler::CSharp::Syntax::FixedStatement;
using ILSpy::Decompiler::CSharp::Syntax::VariableDeclarationStatement;
using ILSpy::Decompiler::CSharp::Syntax::SwitchStatement;
using ILSpy::Decompiler::CSharp::Syntax::SwitchSection;
using ILSpy::Decompiler::CSharp::Syntax::CaseLabel;
using ILSpy::Decompiler::CSharp::Syntax::SwitchExpression;
using ILSpy::Decompiler::CSharp::Syntax::SwitchExpressionSection;
using ILSpy::Decompiler::CSharp::Syntax::CatchClause;
using ILSpy::Decompiler::CSharp::Syntax::TryCatchStatement;
using ILSpy::Decompiler::CSharp::Syntax::LocalFunctionDeclarationStatement;
using ILSpy::Decompiler::CSharp::Syntax::ConstructorInitializer;
using ILSpy::Decompiler::CSharp::Syntax::ConstructorInitializerType;
using ILSpy::Decompiler::CSharp::Syntax::ConstructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::DestructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::FieldDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::FixedFieldDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::FixedVariableInitializer;
using ILSpy::Decompiler::CSharp::Syntax::EnumMemberDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::ExtensionDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::Accessor;
using ILSpy::Decompiler::CSharp::Syntax::AccessorKind;
using ILSpy::Decompiler::CSharp::Syntax::PropertyDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::CustomEventDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EventDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::IndexerDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::MethodDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::OperatorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::OperatorType;
using ILSpy::Decompiler::CSharp::Syntax::TypeDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentExpression;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::CheckedExpression;
using ILSpy::Decompiler::CSharp::Syntax::CheckedStatement;
using ILSpy::Decompiler::CSharp::Syntax::ConditionalExpression;
using ILSpy::Decompiler::CSharp::Syntax::DirectionExpression;
using ILSpy::Decompiler::CSharp::Syntax::FieldDirection;
using ILSpy::Decompiler::CSharp::Syntax::ParenthesizedExpression;
using ILSpy::Decompiler::CSharp::Syntax::ThrowExpression;
using ILSpy::Decompiler::CSharp::Syntax::AsExpression;
using ILSpy::Decompiler::CSharp::Syntax::IsExpression;
using ILSpy::Decompiler::CSharp::Syntax::CastExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeOfExpression;
using ILSpy::Decompiler::CSharp::Syntax::DefaultValueExpression;
using ILSpy::Decompiler::CSharp::Syntax::SizeOfExpression;
using ILSpy::Decompiler::CSharp::Syntax::IdentifierExpression;
using ILSpy::Decompiler::CSharp::Syntax::IndexerExpression;
using ILSpy::Decompiler::CSharp::Syntax::MemberReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PointerReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::InvocationExpression;
using ILSpy::Decompiler::CSharp::Syntax::TupleExpression;
using ILSpy::Decompiler::CSharp::Syntax::NamedExpression;
using ILSpy::Decompiler::CSharp::Syntax::NamedArgumentExpression;
using ILSpy::Decompiler::CSharp::Syntax::ThrowStatement;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::UncheckedStatement;
using ILSpy::Decompiler::CSharp::Syntax::UnsafeStatement;
using ILSpy::Decompiler::CSharp::Syntax::UncheckedExpression;
using ILSpy::Decompiler::CSharp::Syntax::ObjectCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::ArrayCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::ArrayInitializerExpression;
using ILSpy::Decompiler::CSharp::Syntax::OutVarDeclarationExpression;
using ILSpy::Decompiler::CSharp::Syntax::WithInitializerExpression;
using ILSpy::Decompiler::CSharp::Syntax::UndocumentedExpression;
using ILSpy::Decompiler::CSharp::Syntax::UndocumentedExpressionType;
using ILSpy::Decompiler::CSharp::Syntax::StackAllocExpression;
using ILSpy::Decompiler::CSharp::Syntax::VariableInitializer;
using ILSpy::Decompiler::CSharp::Syntax::ErrorExpression;
using ILSpy::Decompiler::CSharp::Syntax::SingleVariableDesignation;
using ILSpy::Decompiler::CSharp::Syntax::ParenthesizedVariableDesignation;
using ILSpy::Decompiler::CSharp::Syntax::TupleTypeElement;
using ILSpy::Decompiler::CSharp::Syntax::TupleAstType;
using ILSpy::Decompiler::CSharp::Syntax::InvocationAstType;
using ILSpy::Decompiler::CSharp::Syntax::FunctionPointerAstType;
using ILSpy::Decompiler::CSharp::Syntax::AnonymousTypeCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::LambdaExpression;
using ILSpy::Decompiler::CSharp::Syntax::AnonymousMethodExpression;
using ILSpy::Decompiler::CSharp::Syntax::DeclarationExpression;
using ILSpy::Decompiler::CSharp::Syntax::ParameterDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::TypeParameterDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::Constraint;
using ILSpy::Decompiler::CSharp::Syntax::UsingAliasDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::UsingDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::ExternAliasDeclaration;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;

namespace {

// A recording `TokenWriter` that appends a short string per call into a vector -- the inner sink
// the visitor's `InsertRequiredSpacesDecorator` wraps. The recorded sequence lets the tests assert
// exactly what the visitor drove through the writer (the decorator inserts a space only where two
// tokens would merge, so for the punctuation/keyword/newline calls under test the sequence is
// exactly what the visitor wrote).
class RecordingTokenWriter : public TokenWriter {
public:
	std::vector<std::string> calls;

	void StartNode(AstNode* /*node*/) override { calls.push_back("start"); }
	void EndNode(AstNode* /*node*/) override { calls.push_back("end"); }
	void WriteIdentifier(Identifier* identifier) override {
		calls.push_back("id:" + std::string(identifier->Name()));
	}
	void WriteKeyword(std::string_view keyword) override {
		calls.push_back("kw:" + std::string(keyword));
	}
	void WriteToken(std::string_view token) override {
		calls.push_back("tok:" + std::string(token));
	}
	void WritePrimitiveType(std::string_view type) override {
		calls.push_back("primtype:" + std::string(type));
	}
	void WriteInterpolatedText(std::string_view text) override {
		calls.push_back("interp:" + std::string(text));
	}
	void WritePrimitiveValue(const PrimitiveValue& /*value*/, LiteralFormat /*format*/) override {
		calls.push_back("primval");
	}
	void Space() override { calls.push_back("space"); }
	void Indent() override { calls.push_back("indent"); }
	void Unindent() override { calls.push_back("unindent"); }
	void NewLine() override { calls.push_back("newline"); }
	void WriteComment(CommentType /*commentType*/, std::string_view content) override {
		calls.push_back("comment:" + std::string(content));
	}
	void WritePreProcessorDirective(PreProcessorDirectiveType /*type*/, std::optional<std::string_view> argument) override {
		calls.push_back("pp:" + (argument.has_value() ? std::string(*argument) : std::string("<null>")));
	}
};

// A fresh inner recorder + visitor pair per test (the visitor wraps the recorder in an
// `InsertRequiredSpacesDecorator` it owns; the recorder is kept alive here as the inner sink).
struct V {
	RecordingTokenWriter inner;
	std::unique_ptr<CSharpOutputVisitor> visitor;
	V() : visitor(std::make_unique<CSharpOutputVisitor>(static_cast<TokenWriter*>(&inner), CSharpFormattingOptions{})) {}
};

// A `unique_ptr<Identifier>` owning a token created via the public `Identifier::Create` factory
// (the 2-arg ctor is private, D241/D257).
std::unique_ptr<Identifier> MakeId(std::string name) {
	return std::unique_ptr<Identifier>(Identifier::Create(std::move(name)));
}

}  // namespace

// ---- ctors ----------------------------------------------------------------

// The ostream ctor rejects a null stream (the C# `ArgumentNullException` -> `std::invalid_argument`).
TEST(CSharp_OutputVisitor, OstreamCtorRejectsNullStream) {
	EXPECT_THROW(CSharpOutputVisitor v(static_cast<std::ostream*>(nullptr), CSharpFormattingOptions{}), std::invalid_argument);
}

// The `TokenWriter` ctor rejects a null writer.
TEST(CSharp_OutputVisitor, TokenWriterCtorRejectsNullWriter) {
	EXPECT_THROW(CSharpOutputVisitor v(static_cast<TokenWriter*>(nullptr), CSharpFormattingOptions{}), std::invalid_argument);
}

// Both ctors construct a usable visitor (the `writer_` is set; driving a keyword records it).
TEST(CSharp_OutputVisitor, TokenWriterCtorConstructs) {
	RecordingTokenWriter inner;
	CSharpOutputVisitor v(&inner, CSharpFormattingOptions{});
	v.WriteKeyword("void");
	ASSERT_EQ(inner.calls.size(), 1u);
	EXPECT_EQ(inner.calls[0], "kw:void");
}

// ---- StartNode/EndNode ----------------------------------------------------

// `StartNode`/`EndNode` on a trivia-free node push/pop the nesting stack and drive the writer
// (no trivia loop, so no `AcceptVisitor` call).
TEST(CSharp_OutputVisitor, StartNodeEndNodeTriviaFree) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->StartNode(node.get());
	h.visitor->EndNode(node.get());
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "end");
}

// ---- Comma ----------------------------------------------------------------

// `Comma` writes a comma (the default policy has no bracket-comma spaces, so just the token).
TEST(CSharp_OutputVisitor, CommaWritesComma) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->Comma(node.get());
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "tok:,");
}

// ---- Write tokens ---------------------------------------------------------

TEST(CSharp_OutputVisitor, WriteKeyword) {
	V h;
	h.visitor->WriteKeyword("void");
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "kw:void");
}

TEST(CSharp_OutputVisitor, WriteIdentifier) {
	V h;
	auto id = MakeId("Foo");
	h.visitor->WriteIdentifier(id.get());
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "id:Foo");
}

TEST(CSharp_OutputVisitor, WriteToken) {
	V h;
	h.visitor->WriteToken(";");
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "tok:;");
}

TEST(CSharp_OutputVisitor, LParRPar) {
	V h;
	h.visitor->LPar();
	h.visitor->RPar();
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "tok:(");
	EXPECT_EQ(h.inner.calls[1], "tok:)");
}

// `Semicolon` on a node with no slot kind (the default case) writes a semicolon and a newline.
TEST(CSharp_OutputVisitor, SemicolonDefault) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->StartNode(node.get());
	h.visitor->Semicolon();
	h.visitor->EndNode(node.get());
	// start, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:;");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// ---- Space / NewLine ------------------------------------------------------

TEST(CSharp_OutputVisitor, Space) {
	V h;
	h.visitor->Space();          // addSpace=true, isAfterSpace=false -> writes a space
	h.visitor->Space(false);    // addSpace=false -> no-op
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "space");
}

TEST(CSharp_OutputVisitor, NewLine) {
	V h;
	h.visitor->NewLine();
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "newline");
}

// ---- OpenBrace / CloseBrace -----------------------------------------------

// `OpenBrace(EndOfLine)` + `CloseBrace(EndOfLine)`: at the start of a line, just `{` (no leading
// space), then the new-line+indent, then unindent + `}`.
TEST(CSharp_OutputVisitor, OpenCloseBraceEndOfLine) {
	V h;
	h.visitor->OpenBrace(BraceStyle::EndOfLine);
	h.visitor->CloseBrace(BraceStyle::EndOfLine);
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "tok:{");
	EXPECT_EQ(h.inner.calls[1], "indent");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "unindent");
	EXPECT_EQ(h.inner.calls[4], "tok:}");
}

// `OpenBrace(NextLineShifted)` returns early (no trailing `Indent`+`NewLine`); the `{` follows a
// `NewLine`+`Indent` and the `NextLineShifted` case then emits its own `NewLine`.
TEST(CSharp_OutputVisitor, OpenBraceNextLineShifted) {
	V h;
	h.visitor->OpenBrace(BraceStyle::NextLineShifted);
	// NewLine, Indent, tok:{, NewLine (the early return skips the trailing Indent+NewLine)
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "newline");
	EXPECT_EQ(h.inner.calls[1], "indent");
	EXPECT_EQ(h.inner.calls[2], "tok:{");
	EXPECT_EQ(h.inner.calls[3], "newline");
}

// ---- WriteBlock ------------------------------------------------------------

// `WriteBlock` on an empty block: `StartNode`, `OpenBrace`, (no statements), `CloseBrace`,
// `EndNode`.
TEST(CSharp_OutputVisitor, WriteBlockEmpty) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	h.visitor->WriteBlock(block.get(), BraceStyle::EndOfLine);
	// start, tok:{, indent, newline, unindent, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[2], "indent");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "unindent");
	EXPECT_EQ(h.inner.calls[5], "tok:}");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// ---- Write constructs ------------------------------------------------------

// `WriteTypeArguments` / `WriteTypeParameters` on an empty list write nothing (the non-empty case
// recurses via `AcceptVisitor`, which the throwing stubs reject, so only the empty case is
// exercised here).
TEST(CSharp_OutputVisitor, WriteTypeArgumentsEmpty) {
	V h;
	h.visitor->WriteTypeArguments({});
	EXPECT_TRUE(h.inner.calls.empty());
}

TEST(CSharp_OutputVisitor, WriteTypeParametersEmpty) {
	V h;
	h.visitor->WriteTypeParameters({});
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WriteModifiers` iterates `CSharpModifiers::AllModifiers` in output order and writes each set
// bit's keyword + a trailing space.
TEST(CSharp_OutputVisitor, WriteModifiers) {
	V h;
	h.visitor->WriteModifiers(Modifiers::Static | Modifiers::Public);
	// AllModifiers order: Public, Private, Protected, Internal, New, Unsafe, Static, ...
	// Public then Static are set -> kw:public, space, kw:static, space
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "kw:public");
	EXPECT_EQ(h.inner.calls[1], "space");
	EXPECT_EQ(h.inner.calls[2], "kw:static");
	EXPECT_EQ(h.inner.calls[3], "space");
}

// `WriteModifiers(None)` writes nothing (no bits set; `Any` is not set either).
TEST(CSharp_OutputVisitor, WriteModifiersNone) {
	V h;
	h.visitor->WriteModifiers(Modifiers::None);
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WriteQualifiedIdentifier` writes a `.` between identifiers (and the identifiers themselves).
TEST(CSharp_OutputVisitor, WriteQualifiedIdentifier) {
	V h;
	auto a = MakeId("System");
	auto b = MakeId("Math");
	h.visitor->WriteQualifiedIdentifier({a.get(), b.get()});
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "id:System");
	EXPECT_EQ(h.inner.calls[1], "tok:.");
	EXPECT_EQ(h.inner.calls[2], "id:Math");
}

// `WriteEmbeddedStatement(nullptr)` just writes a newline.
TEST(CSharp_OutputVisitor, WriteEmbeddedStatementNull) {
	V h;
	h.visitor->WriteEmbeddedStatement(nullptr);
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "newline");
}

// `WriteEmbeddedStatement(block)` writes the block then a trailing newline.
TEST(CSharp_OutputVisitor, WriteEmbeddedStatementBlock) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	h.visitor->WriteEmbeddedStatement(block.get());
	// start, tok:{, indent, newline, unindent, tok:}, end, newline
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "newline");
}

// `WriteMethodBody(nullptr)` writes a semicolon (the empty-body case). `Semicolon` reads the
// current node's slot kind from the nesting stack (the C# `containerStack.Peek()`), so a node is
// `StartNode`'d first, as a real `VisitMethodDeclaration` would have it.
TEST(CSharp_OutputVisitor, WriteMethodBodyNull) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->StartNode(node.get());
	h.visitor->WriteMethodBody(nullptr, BraceStyle::EndOfLine);
	h.visitor->EndNode(node.get());
	// start, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:;");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `WriteMethodBody(block)` writes the block then a trailing newline.
TEST(CSharp_OutputVisitor, WriteMethodBodyBlock) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	h.visitor->WriteMethodBody(block.get(), BraceStyle::EndOfLine);
	// start, tok:{, indent, newline, unindent, tok:}, end, newline
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "newline");
}

// `WriteAttributes` on an empty list writes nothing (the non-empty case recurses via
// `AcceptVisitor`, which the throwing stubs reject).
TEST(CSharp_OutputVisitor, WriteAttributesEmpty) {
	V h;
	h.visitor->WriteAttributes({});
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WritePrivateImplementationType(nullptr)` writes nothing (the non-null case recurses via
// `AcceptVisitor`, which the throwing stubs reject).
TEST(CSharp_OutputVisitor, WritePrivateImplementationTypeNull) {
	V h;
	h.visitor->WritePrivateImplementationType(nullptr);
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WriteCommaSeparatedListInParenthesis` on an empty list writes just the parens.
TEST(CSharp_OutputVisitor, WriteCommaSeparatedListInParenthesisEmpty) {
	V h;
	h.visitor->WriteCommaSeparatedListInParenthesis<AstNode>({}, false);
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "tok:(");
	EXPECT_EQ(h.inner.calls[1], "tok:)");
}

// ---- The 130 Visit stubs throw ---------------------------------------------

// A representative still-unported `Visit` method throws `std::logic_error` (the throwing-stub
// design); `VisitTypeDeclaration` remains a stub (the `VariableDeclarationStatement`, the
// try/catch family, `ConstructorInitializer`, `ConstructorDeclaration`, `DestructorDeclaration`,
// `EnumMemberDeclaration`, `ExtensionDeclaration`, `EventDeclaration`, `CustomEventDeclaration`,
// `FieldDeclaration`, `FixedFieldDeclaration`, `IndexerDeclaration`, `MethodDeclaration`,
// `OperatorDeclaration` and `PropertyDeclaration` `Visit` methods above are implemented -- the
// full EntityDeclaration family is done; the GeneralScope/TypeDeclaration members below are
// still stubs).
TEST(CSharp_OutputVisitor, VisitStubThrows) {
	V h;
	auto decl = std::make_unique<TypeDeclaration>();
	EXPECT_THROW(h.visitor->VisitTypeDeclaration(decl.get()), std::logic_error);
}

// ---- The implemented leaf Visit methods ------------------------------------

// `VisitNullReferenceExpression` writes the `null` literal: `StartNode`, `WritePrimitiveValue`
// (the monostate/null alternative), `EndNode` (the `isAfterSpace_ = false` is internal state).
TEST(CSharp_OutputVisitor, VisitNullReferenceExpression) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->VisitNullReferenceExpression(node.get());
	// start, primval, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "primval");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitThisReferenceExpression` writes the `this` keyword.
TEST(CSharp_OutputVisitor, VisitThisReferenceExpression) {
	V h;
	auto node = std::make_unique<ThisReferenceExpression>();
	h.visitor->VisitThisReferenceExpression(node.get());
	// start, kw:this, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:this");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitBaseReferenceExpression` writes the `base` keyword.
TEST(CSharp_OutputVisitor, VisitBaseReferenceExpression) {
	V h;
	auto node = std::make_unique<BaseReferenceExpression>();
	h.visitor->VisitBaseReferenceExpression(node.get());
	// start, kw:base, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:base");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitPrimitiveExpression` writes the literal value via `WritePrimitiveValue`.
TEST(CSharp_OutputVisitor, VisitPrimitiveExpression) {
	V h;
	auto node = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	h.visitor->VisitPrimitiveExpression(node.get());
	// start, primval, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "primval");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitBreakStatement` writes `break` then a semicolon (the `Semicolon` helper writes the token
// and a trailing newline for a parentless node's default slot kind).
TEST(CSharp_OutputVisitor, VisitBreakStatement) {
	V h;
	auto node = std::make_unique<BreakStatement>();
	h.visitor->VisitBreakStatement(node.get());
	// start, kw:break, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:break");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitContinueStatement` writes `continue` then a semicolon.
TEST(CSharp_OutputVisitor, VisitContinueStatement) {
	V h;
	auto node = std::make_unique<ContinueStatement>();
	h.visitor->VisitContinueStatement(node.get());
	// start, kw:continue, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:continue");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitYieldBreakStatement` writes `yield break;` -- the `InsertRequiredSpacesDecorator` inserts
// the inter-keyword space the C# formatter relies on it to insert (two consecutive keywords
// would otherwise merge into one lexeme).
TEST(CSharp_OutputVisitor, VisitYieldBreakStatement) {
	V h;
	auto node = std::make_unique<YieldBreakStatement>();
	h.visitor->VisitYieldBreakStatement(node.get());
	// start, kw:yield, space, kw:break, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:yield");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:break");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitEmptyStatement` (no trivia) writes a bare semicolon; a statement carrying a comment
// would render as just that comment (the trivia path needs `VisitComment` to exercise end-to-end).
TEST(CSharp_OutputVisitor, VisitEmptyStatement) {
	V h;
	auto node = std::make_unique<EmptyStatement>();
	h.visitor->VisitEmptyStatement(node.get());
	// start, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:;");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// An end-to-end snippet: a `break;` statement driven through the visitor and its
// `InsertRequiredSpacesDecorator` into the recording sink yields the `break ;` token stream (the
// decorator inserts no space here -- a keyword followed by a punctuation token does not merge).
TEST(CSharp_OutputVisitor, VisitBreakStatementEndToEnd) {
	V h;
	auto node = std::make_unique<BreakStatement>();
	h.visitor->VisitBreakStatement(node.get());
	// The recorded sequence is the visitor's full output for the node.
	ASSERT_GE(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[1], "kw:break");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
}

// ---- The AstType Visit family --------------------------------------------

// `VisitIdentifier` writes the identifier token directly (NO `StartNode`/`EndNode` -- the C#
// deliberately omits them so the `ITokenWriter` treats the identifier as a flat token, not a
// nested child node).
TEST(CSharp_OutputVisitor, VisitIdentifier) {
	V h;
	auto id = MakeId("Foo");
	h.visitor->VisitIdentifier(id.get());
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "id:Foo");
}

// `VisitPrimitiveType` writes the primitive-type keyword (e.g. `int`) via `WritePrimitiveType`.
TEST(CSharp_OutputVisitor, VisitPrimitiveType) {
	V h;
	auto node = std::make_unique<PrimitiveType>(std::string("int"));
	h.visitor->VisitPrimitiveType(node.get());
	// start, primtype:int, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "primtype:int");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitArraySpecifier` of rank 1 renders as `[]`.
TEST(CSharp_OutputVisitor, VisitArraySpecifierRank1) {
	V h;
	auto node = std::make_unique<ArraySpecifier>(1);
	h.visitor->VisitArraySpecifier(node.get());
	// start, tok:[, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:[");
	EXPECT_EQ(h.inner.calls[2], "tok:]");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitArraySpecifier` of rank 2 renders as `[,]` (one comma for the second dimension).
TEST(CSharp_OutputVisitor, VisitArraySpecifierRank2) {
	V h;
	auto node = std::make_unique<ArraySpecifier>(2);
	h.visitor->VisitArraySpecifier(node.get());
	// start, tok:[, tok:,, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:[");
	EXPECT_EQ(h.inner.calls[2], "tok:,");
	EXPECT_EQ(h.inner.calls[3], "tok:]");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitSimpleType` (named, no type arguments) writes the backing `IdentifierToken`.
TEST(CSharp_OutputVisitor, VisitSimpleTypeNamed) {
	V h;
	auto node = std::make_unique<SimpleType>(std::string("Foo"));
	h.visitor->VisitSimpleType(node.get());
	// start, id:Foo, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitSimpleType` with no backing token (the nameless `typeof(List<>)` case) writes nothing
// but the `StartNode`/`EndNode` pair (no identifier, no type arguments).
TEST(CSharp_OutputVisitor, VisitSimpleTypeNameless) {
	V h;
	auto node = std::make_unique<SimpleType>();
	h.visitor->VisitSimpleType(node.get());
	// start, end
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "end");
}

// `VisitMemberType` writes `Target.MemberName` (a dot separator by default): the target `SimpleType`
// recurses through `VisitSimpleType`, then the dot, then the member-name identifier.
TEST(CSharp_OutputVisitor, VisitMemberType) {
	V h;
	auto target = std::make_unique<SimpleType>(std::string("System"));
	auto node = std::make_unique<MemberType>(target.get(), std::string("Math"));
	h.visitor->VisitMemberType(node.get());
	// start(MemberType), start(SimpleType), id:System, end(SimpleType), tok:., id:Math, end(MemberType)
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:System");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:.");
	EXPECT_EQ(h.inner.calls[5], "id:Math");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitComposedType` with a bare `BaseType` (no specifiers) recurses into the base type only.
TEST(CSharp_OutputVisitor, VisitComposedTypeBare) {
	V h;
	auto baseType = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<ComposedType>();
	node->BaseType(baseType.get());
	h.visitor->VisitComposedType(node.get());
	// start(ComposedType), start(PrimitiveType), primtype:int, end(PrimitiveType), end(ComposedType)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitComposedType` with a nullable specifier, a pointer rank, and an array specifier renders
// `int?**[]` after the base type (the `?`, two `*`, then the array specifier recurses).
TEST(CSharp_OutputVisitor, VisitComposedTypeNullablePointerArray) {
	V h;
	auto baseType = std::make_unique<PrimitiveType>(std::string("int"));
	auto arrSpec = std::make_unique<ArraySpecifier>(1);
	auto node = std::make_unique<ComposedType>();
	node->BaseType(baseType.get());
	node->HasNullableSpecifier(true);
	node->PointerRank(2);
	node->ArraySpecifiers().Add(arrSpec.get());
	h.visitor->VisitComposedType(node.get());
	// start, start(primtype), primtype:int, end(primtype), tok:?, tok:*, tok:*, start(arrspec),
	// tok:[, tok:], end(arrspec), end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[4], "tok:?");
	EXPECT_EQ(h.inner.calls[5], "tok:*");
	EXPECT_EQ(h.inner.calls[6], "tok:*");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "tok:[");
	EXPECT_EQ(h.inner.calls[9], "tok:]");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitTypeReferenceExpression` writes just its `Type` child (a bare type-name expression
// such as `int` used as a value renders the type alone).
TEST(CSharp_OutputVisitor, VisitTypeReferenceExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TypeReferenceExpression>(type.get());
	h.visitor->VisitTypeReferenceExpression(node.get());
	// start(TypeReferenceExpression), start(PrimitiveType), primtype:int, end(PrimitiveType),
	// end(TypeReferenceExpression)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// ---- The type-keyword expression Visit methods ----------------------------

// `VisitCastExpression` over `(int)a` -- writes `( ` + Type + ` )` + the cast operand; the default
// policy has no spaces within the cast parens nor after the typecast, so it renders `(int)a`.
TEST(CSharp_OutputVisitor, VisitCastExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<CastExpression>(type.get(), expr.get());
	h.visitor->VisitCastExpression(node.get());
	// start, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:),
	// start(PrimitiveExpression), primval, end(PrimitiveExpression), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primtype:int");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "primval");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitAsExpression` over `42 as int` -- writes the operand, an explicit space, the `as` keyword,
// an explicit space, then the type (the C# writes both spaces explicitly, so the decorator inserts
// none before the type).
TEST(CSharp_OutputVisitor, VisitAsExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<AsExpression>(expr.get(), type.get());
	h.visitor->VisitAsExpression(node.get());
	// start, start(PrimitiveExpression), primval, end(PrimitiveExpression), space, kw:as, space,
	// start(PrimitiveType), primtype:int, end(PrimitiveType), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:as");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primtype:int");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitIsExpression` over `42 is int` -- writes the operand, a space, the `is` keyword, then the
// type; the C# writes NO explicit space after `is`, so the decorator inserts the separating space
// before the primitive type (a keyword-then-primitive-type merge guard).
TEST(CSharp_OutputVisitor, VisitIsExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<IsExpression>(expr.get(), type.get());
	h.visitor->VisitIsExpression(node.get());
	// start, start(PrimitiveExpression), primval, end(PrimitiveExpression), space, kw:is,
	// start(PrimitiveType), space (decorator-inserted before the primitive type), primtype:int,
	// end(PrimitiveType), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:is");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "primtype:int");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitTypeOfExpression` over `typeof(int)` -- writes the `typeof` keyword, parens, and the type;
// the default policy has no spaces within the parens, so it renders `typeof(int)`.
TEST(CSharp_OutputVisitor, VisitTypeOfExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TypeOfExpression>(type.get());
	h.visitor->VisitTypeOfExpression(node.get());
	// start, kw:typeof, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:typeof");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitDefaultValueExpression` over `default(int)` -- structurally identical to `typeof(int)`
// but with the `default` keyword (the C# uses the same `SpacesWithinTypeOfParentheses` policy).
TEST(CSharp_OutputVisitor, VisitDefaultValueExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<DefaultValueExpression>(type.get());
	h.visitor->VisitDefaultValueExpression(node.get());
	// start, kw:default, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:default");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitSizeOfExpression` over `sizeof(int)` -- structurally identical to `typeof(int)` but with
// the `sizeof` keyword and the `SpacesWithinSizeOfParentheses` policy.
TEST(CSharp_OutputVisitor, VisitSizeOfExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<SizeOfExpression>(type.get());
	h.visitor->VisitSizeOfExpression(node.get());
	// start, kw:sizeof, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:sizeof");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// ---- The Trivia Visit leaves ---------------------------------------------

// `VisitComment` drives the writer directly (NOT the visitor's `StartNode`/`EndNode`): a comment
// is trivia emitted as a flat token group (start, the comment text, end).
TEST(CSharp_OutputVisitor, VisitComment) {
	V h;
	auto node = std::make_unique<Comment>(std::string("hello"));
	h.visitor->VisitComment(node.get());
	// start, comment:hello, end (no visitor StartNode/EndNode, so no trivia walk)
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "comment:hello");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitPreProcessorDirective` drives the writer directly with the directive type and its
// optional argument (a `#if DEBUG` renders as start, pp:DEBUG, end).
TEST(CSharp_OutputVisitor, VisitPreProcessorDirective) {
	V h;
	auto node = std::make_unique<PreProcessorDirective>();
	node->Type(PreProcessorDirectiveType::If);
	node->Argument(std::string("DEBUG"));
	h.visitor->VisitPreProcessorDirective(node.get());
	// start, pp:DEBUG, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "pp:DEBUG");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitPreProcessorDirective` with no argument renders the null argument as `<null>`.
TEST(CSharp_OutputVisitor, VisitPreProcessorDirectiveNoArgument) {
	V h;
	auto node = std::make_unique<PreProcessorDirective>();
	node->Type(PreProcessorDirectiveType::Region);
	h.visitor->VisitPreProcessorDirective(node.get());
	// start, pp:<null>, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "pp:<null>");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// ---- The operator-bearing + simple Expression Visit methods (D326) --------

// `VisitBinaryOperatorExpression` over `a + b` (default policy: no space around additive
// operators) renders as start, the left `primval`, the `+` token, the right `primval`, end.
// The default `CSharpFormattingOptions` has `SpaceAroundAdditiveOperator=false`, so no spaces.
TEST(CSharp_OutputVisitor, VisitBinaryOperatorExpressionAdd) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<BinaryOperatorExpression>(left.get(), BinaryOperatorType::Add, right.get());
	h.visitor->VisitBinaryOperatorExpression(node.get());
	// start, start, primval, end, tok:+, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:+");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitBinaryOperatorExpression` over `a ?? b` -- the `NullCoalescing` operator always has
// `spacePolicy=true`, so spaces are inserted around the `??` token.
TEST(CSharp_OutputVisitor, VisitBinaryOperatorExpressionNullCoalescing) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<BinaryOperatorExpression>(left.get(), BinaryOperatorType::NullCoalescing, right.get());
	h.visitor->VisitBinaryOperatorExpression(node.get());
	// start, start, primval, end, space, tok:??, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:??");
	EXPECT_EQ(h.inner.calls[6], "space");
}

// `VisitBinaryOperatorExpression` over the `is` pattern operator uses `WriteKeyword` (the token
// equals `BinaryOperatorExpression::IsKeyword`), not `WriteToken`; `IsPattern` always has
// `spacePolicy=true`.
TEST(CSharp_OutputVisitor, VisitBinaryOperatorExpressionIsPattern) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<BinaryOperatorExpression>(left.get(), BinaryOperatorType::IsPattern, right.get());
	h.visitor->VisitBinaryOperatorExpression(node.get());
	// start, start, primval, end, space, kw:is, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[5], "kw:is");
}

// `VisitAssignmentExpression` over `a = b` (default policy: no space around assignment).
TEST(CSharp_OutputVisitor, VisitAssignmentExpression) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<AssignmentExpression>(left.get(), right.get());
	h.visitor->VisitAssignmentExpression(node.get());
	// start, start, primval, end, tok:=, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[4], "tok:=");
}

// `VisitUnaryOperatorExpression` over `-a` (prefix minus, no space policy).
TEST(CSharp_OutputVisitor, VisitUnaryOperatorExpressionMinus) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<UnaryOperatorExpression>(expr.get(), UnaryOperatorType::Minus);
	h.visitor->VisitUnaryOperatorExpression(node.get());
	// start, tok:-, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:-");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primval");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "end");
}

// `VisitUnaryOperatorExpression` over `await a` -- the `Await` operator uses `WriteKeyword` +
// an explicit `Space()`.
TEST(CSharp_OutputVisitor, VisitUnaryOperatorExpressionAwait) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<UnaryOperatorExpression>(expr.get(), UnaryOperatorType::Await);
	h.visitor->VisitUnaryOperatorExpression(node.get());
	// start, kw:await, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:await");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitUnaryOperatorExpression` over `a++` (postfix increment -- the token is written AFTER
// the expression).
TEST(CSharp_OutputVisitor, VisitUnaryOperatorExpressionPostIncrement) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<UnaryOperatorExpression>(expr.get(), UnaryOperatorType::PostIncrement);
	h.visitor->VisitUnaryOperatorExpression(node.get());
	// start, start, primval, end, tok:++, end
	ASSERT_EQ(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:++");
	EXPECT_EQ(h.inner.calls[5], "end");
}

// `VisitConditionalExpression` over `a ? b : c` (default policy: no conditional spaces).
TEST(CSharp_OutputVisitor, VisitConditionalExpression) {
	V h;
	auto cond = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto trueExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto falseExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(3)));
	auto node = std::make_unique<ConditionalExpression>(cond.get(), trueExpr.get(), falseExpr.get());
	h.visitor->VisitConditionalExpression(node.get());
	// start, start, primval, end, tok:?, start, primval, end, tok::, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[4], "tok:?");
	EXPECT_EQ(h.inner.calls[8], "tok::");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// `VisitParenthesizedExpression` over `(a)` (default policy: no spaces within parentheses).
TEST(CSharp_OutputVisitor, VisitParenthesizedExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<ParenthesizedExpression>(inner.get());
	h.visitor->VisitParenthesizedExpression(node.get());
	// start, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primval");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitCheckedExpression` over `checked(a)` -- writes the `checked` keyword then parens.
TEST(CSharp_OutputVisitor, VisitCheckedExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<CheckedExpression>(inner.get());
	h.visitor->VisitCheckedExpression(node.get());
	// start, kw:checked, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:checked");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitUncheckedExpression` over `unchecked(a)` -- writes the `unchecked` keyword then parens.
TEST(CSharp_OutputVisitor, VisitUncheckedExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<UncheckedExpression>(inner.get());
	h.visitor->VisitUncheckedExpression(node.get());
	// start, kw:unchecked, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:unchecked");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitDirectionExpression` over `out a` -- writes the `out` keyword then a space then the expr.
TEST(CSharp_OutputVisitor, VisitDirectionExpressionOut) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<DirectionExpression>(FieldDirection::Out, inner.get());
	h.visitor->VisitDirectionExpression(node.get());
	// start, kw:out, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:out");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitDirectionExpression` over `ref a` -- writes the `ref` keyword then a space then the expr.
TEST(CSharp_OutputVisitor, VisitDirectionExpressionRef) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<DirectionExpression>(FieldDirection::Ref, inner.get());
	h.visitor->VisitDirectionExpression(node.get());
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[1], "kw:ref");
}

// `VisitThrowExpression` over `throw a` -- writes the `throw` keyword then a space then the expr.
TEST(CSharp_OutputVisitor, VisitThrowExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<ThrowExpression>(inner.get());
	h.visitor->VisitThrowExpression(node.get());
	// start, kw:throw, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:throw");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// ---- The implemented statement Visit methods (the simple statements) ----------

// `VisitReturnStatement` with no expression writes `return;`.
TEST(CSharp_OutputVisitor, VisitReturnStatementNoExpression) {
	V h;
	auto node = std::make_unique<ReturnStatement>();
	h.visitor->VisitReturnStatement(node.get());
	// start, kw:return, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:return");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitReturnStatement` with an expression writes `return <expr>;`.
TEST(CSharp_OutputVisitor, VisitReturnStatementWithExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<ReturnStatement>();
	node->Expression(expr.get());
	h.visitor->VisitReturnStatement(node.get());
	// start, kw:return, space, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:return");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:;");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitThrowStatement` with no expression writes `throw;`.
TEST(CSharp_OutputVisitor, VisitThrowStatementNoExpression) {
	V h;
	auto node = std::make_unique<ThrowStatement>();
	h.visitor->VisitThrowStatement(node.get());
	// start, kw:throw, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[1], "kw:throw");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
}

// `VisitThrowStatement` with an expression writes `throw <expr>;`.
TEST(CSharp_OutputVisitor, VisitThrowStatementWithExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<ThrowStatement>();
	node->Expression(expr.get());
	h.visitor->VisitThrowStatement(node.get());
	// start, kw:throw, space, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[4], "primval");
}

// `VisitExpressionStatement` writes the expression then a semicolon.
TEST(CSharp_OutputVisitor, VisitExpressionStatement) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(7)));
	auto node = std::make_unique<ExpressionStatement>(expr.get());
	h.visitor->VisitExpressionStatement(node.get());
	// start, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitGotoStatement` writes `goto <label>;` -- the `InsertRequiredSpacesDecorator`
// inserts the space between the `goto` keyword and the label identifier.
TEST(CSharp_OutputVisitor, VisitGotoStatement) {
	V h;
	auto node = std::make_unique<GotoStatement>(std::string("myLabel"));
	h.visitor->VisitGotoStatement(node.get());
	// start, kw:goto, space, id:myLabel, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:goto");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "id:myLabel");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitGotoCaseStatement` writes `goto case <expr>;` -- the decorator inserts the space
// between the two keywords, and the explicit `Space()` precedes the label expression.
TEST(CSharp_OutputVisitor, VisitGotoCaseStatement) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<GotoCaseStatement>(expr.get());
	h.visitor->VisitGotoCaseStatement(node.get());
	// start, kw:goto, space, kw:case, space, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:goto");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:case");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitGotoDefaultStatement` writes `goto default;` -- the decorator inserts the space.
TEST(CSharp_OutputVisitor, VisitGotoDefaultStatement) {
	V h;
	auto node = std::make_unique<GotoDefaultStatement>();
	h.visitor->VisitGotoDefaultStatement(node.get());
	// start, kw:goto, space, kw:default, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:goto");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:default");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitBlockStatement` on an empty block writes `{` indent newline unindent `}` then a newline.
TEST(CSharp_OutputVisitor, VisitBlockStatementEmpty) {
	V h;
	auto node = std::make_unique<BlockStatement>();
	h.visitor->VisitBlockStatement(node.get());
	// start, tok:{, indent, newline, unindent, tok:}, end, newline
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[2], "indent");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "unindent");
	EXPECT_EQ(h.inner.calls[5], "tok:}");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "newline");
}

// `VisitLabelStatement` on a parentless label (no following sibling) emits a trailing
// semicolon so the label is syntactically valid as the last statement.
TEST(CSharp_OutputVisitor, VisitLabelStatementAloneEmitsSemicolon) {
	V h;
	auto node = std::make_unique<LabelStatement>(std::string("myLabel"));
	h.visitor->VisitLabelStatement(node.get());
	// start, id:myLabel, tok::, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:myLabel");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "tok:;");
	EXPECT_EQ(h.inner.calls[4], "newline");
	EXPECT_EQ(h.inner.calls[5], "end");
}

// `VisitLabelStatement` on a label that has a following sibling statement (same slot kind)
// emits no trailing semicolon -- the following statement already makes the label valid.
TEST(CSharp_OutputVisitor, VisitLabelStatementWithFollowingStatementNoSemicolon) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	auto label = std::make_unique<LabelStatement>(std::string("myLabel"));
	auto brk = std::make_unique<BreakStatement>();
	block->Statements().Add(label.get());
	block->Statements().Add(brk.get());
	h.visitor->VisitLabelStatement(label.get());
	// start, id:myLabel, tok::, newline, end (no semicolon)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:myLabel");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// ---- The control-flow statement Visit methods -----------------------------

// `VisitWhileStatement` writes `while (cond) body`: the condition is a required `Expression`
// (a `PrimitiveExpression` records `start`/`primval`/`end`), the body a required embedded
// `Statement`. With the default policy (no space before/within the parens), a non-block `break`
// body renders as newline + indent + the break's own sequence + unindent; the `Semicolon` emits a
// `tok:;` then a `newline`.
TEST(CSharp_OutputVisitor, VisitWhileStatementNonBlockBody) {
	V h;
	auto node = std::make_unique<WhileStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BreakStatement>();
	node->Condition(cond.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitWhileStatement(node.get());
	// start, kw:while, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:while");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitWhileStatement` with a `BlockStatement` body renders the block inline via
// `WriteEmbeddedStatement`'s block path (`WriteBlock` + newline). The `OpenBrace` for the default
// `EndOfLine` brace style inserts a `space` before `tok:{` because the line is not empty (the
// close paren was just written), so the block records `start`/`space`/`tok:{`/.../`tok:}`/`end`.
TEST(CSharp_OutputVisitor, VisitWhileStatementBlockBody) {
	V h;
	auto node = std::make_unique<WhileStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BlockStatement>();
	node->Condition(cond.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitWhileStatement(node.get());
	// start, kw:while, tok:(, start, primval, end, tok:), start, space, tok:{, indent, newline,
	// unindent, tok:}, end, newline, end
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:while");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "tok:{");
	EXPECT_EQ(h.inner.calls[10], "indent");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "unindent");
	EXPECT_EQ(h.inner.calls[13], "tok:}");
	EXPECT_EQ(h.inner.calls[14], "end");
	EXPECT_EQ(h.inner.calls[15], "newline");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitDoWhileStatement` writes `do body while (cond);`: the body precedes the `while` clause
// (the slot order is reversed relative to `WhileStatement`), the block body's `OpenBrace`
// inserts a `space` after `do`, and a trailing `Semicolon` (tok:; + newline) closes the statement.
TEST(CSharp_OutputVisitor, VisitDoWhileStatement) {
	V h;
	auto node = std::make_unique<DoWhileStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BlockStatement>();
	node->EmbeddedStatement(body.get());
	node->Condition(cond.get());
	h.visitor->VisitDoWhileStatement(node.get());
	// start, kw:do, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, kw:while,
	// tok:(, start, primval, end, tok:), tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 20u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:do");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "kw:while");
	EXPECT_EQ(h.inner.calls[12], "tok:(");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "primval");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "tok:)");
	EXPECT_EQ(h.inner.calls[17], "tok:;");
	EXPECT_EQ(h.inner.calls[18], "newline");
	EXPECT_EQ(h.inner.calls[19], "end");
}

// `VisitIfElseStatement` with no `FalseStatement` writes `if (cond) body` (the single-branch
// `WriteEmbeddedStatement(TrueStatement)` path). The condition `PrimitiveExpression` records its
// own `start`/`primval`/`end`; the non-block `break` body renders as newline + indent + the
// break's sequence + unindent.
TEST(CSharp_OutputVisitor, VisitIfElseStatementSingleBranch) {
	V h;
	auto node = std::make_unique<IfElseStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BreakStatement>();
	node->Condition(cond.get());
	node->TrueStatement(body.get());
	h.visitor->VisitIfElseStatement(node.get());
	// start, kw:if, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:if");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitIfElseStatement` with a non-`IfElseStatement` `FalseStatement` writes `if (cond) body
// else body` -- the `else` keyword follows the true branch (on its own line per the default
// `ElseNewLinePlacement`), then the false branch is written as another embedded statement. No
// `space` is inserted before `kw:else` because the true branch's `Semicolon` `newline` and the
// following `unindent` both leave `lastWritten` as `Whitespace`.
TEST(CSharp_OutputVisitor, VisitIfElseStatementElseBranch) {
	V h;
	auto node = std::make_unique<IfElseStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto trueBody = std::make_unique<BreakStatement>();
	auto falseBody = std::make_unique<ContinueStatement>();
	node->Condition(cond.get());
	node->TrueStatement(trueBody.get());
	node->FalseStatement(falseBody.get());
	h.visitor->VisitIfElseStatement(node.get());
	// start, kw:if, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, kw:else, newline, indent, start, kw:continue, tok:;, newline, end,
	// unindent, end
	ASSERT_EQ(h.inner.calls.size(), 25u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:if");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "kw:else");
	EXPECT_EQ(h.inner.calls[16], "newline");
	EXPECT_EQ(h.inner.calls[17], "indent");
	EXPECT_EQ(h.inner.calls[18], "start");
	EXPECT_EQ(h.inner.calls[19], "kw:continue");
	EXPECT_EQ(h.inner.calls[20], "tok:;");
	EXPECT_EQ(h.inner.calls[21], "newline");
	EXPECT_EQ(h.inner.calls[22], "end");
	EXPECT_EQ(h.inner.calls[23], "unindent");
	EXPECT_EQ(h.inner.calls[24], "end");
}

// `VisitIfElseStatement` with a nested `IfElseStatement` as the `FalseStatement` writes
// `else if` on one line -- the false branch is recursed directly (no `WriteEmbeddedStatement`
// newline/indent), and the `InsertRequiredSpacesDecorator` inserts a `space` between `else` and
// the nested `if` because the two `WriteKeyword` calls would merge (the `StartNode` between them
// does not reset `lastWritten`).
TEST(CSharp_OutputVisitor, VisitIfElseStatementElseIf) {
	V h;
	auto node = std::make_unique<IfElseStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto trueBody = std::make_unique<BreakStatement>();
	auto nested = std::make_unique<IfElseStatement>();
	auto nestedCond = std::make_unique<PrimitiveExpression>(int32_t(2));
	auto nestedBody = std::make_unique<ContinueStatement>();
	nested->Condition(nestedCond.get());
	nested->TrueStatement(nestedBody.get());
	node->Condition(cond.get());
	node->TrueStatement(trueBody.get());
	node->FalseStatement(nested.get());
	h.visitor->VisitIfElseStatement(node.get());
	// start, kw:if, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, kw:else, start (nested), space (decorator), kw:if, tok:(, start,
	// primval, end, tok:), newline, indent, start, kw:continue, tok:;, newline, end, unindent,
	// end (nested), end (outer)
	ASSERT_EQ(h.inner.calls.size(), 34u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:if");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "kw:else");
	EXPECT_EQ(h.inner.calls[16], "start");
	EXPECT_EQ(h.inner.calls[17], "space");
	EXPECT_EQ(h.inner.calls[18], "kw:if");
	EXPECT_EQ(h.inner.calls[19], "tok:(");
	EXPECT_EQ(h.inner.calls[20], "start");
	EXPECT_EQ(h.inner.calls[21], "primval");
	EXPECT_EQ(h.inner.calls[22], "end");
	EXPECT_EQ(h.inner.calls[23], "tok:)");
	EXPECT_EQ(h.inner.calls[24], "newline");
	EXPECT_EQ(h.inner.calls[25], "indent");
	EXPECT_EQ(h.inner.calls[26], "start");
	EXPECT_EQ(h.inner.calls[27], "kw:continue");
	EXPECT_EQ(h.inner.calls[28], "tok:;");
	EXPECT_EQ(h.inner.calls[29], "newline");
	EXPECT_EQ(h.inner.calls[30], "end");
	EXPECT_EQ(h.inner.calls[31], "unindent");
	EXPECT_EQ(h.inner.calls[32], "end");
	EXPECT_EQ(h.inner.calls[33], "end");
}

// `VisitYieldReturnStatement` writes `yield return expr;`: two consecutive `WriteKeyword` calls
// (`yield` then `return`) would merge into one lexeme without the `InsertRequiredSpacesDecorator`,
// which inserts the inter-keyword `space`; then a `Space` + the expression (its own
// `start`/`primval`/`end`) + `Semicolon` (tok:; + newline).
TEST(CSharp_OutputVisitor, VisitYieldReturnStatement) {
	V h;
	auto node = std::make_unique<YieldReturnStatement>();
	auto expr = std::make_unique<PrimitiveExpression>(int32_t(1));
	node->Expression(expr.get());
	h.visitor->VisitYieldReturnStatement(node.get());
	// start, kw:yield, space (decorator-inserted), kw:return, space (visitor-written), start,
	// primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:yield");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:return");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitCheckedStatement` writes `checked` then recurses into the `Body` block via
// `AcceptVisitor` (dispatching to `VisitBlockStatement` -> `WriteBlock` + `NewLine`). The default
// `EndOfLine` brace style inserts a `space` before `tok:{` because the line is not empty (the
// `checked` keyword was just written), so the block records `start`/`space`/`tok:{`/`indent`/
// `newline`/`unindent`/`tok:}`/`end`/`newline`.
TEST(CSharp_OutputVisitor, VisitCheckedStatement) {
	V h;
	auto node = std::make_unique<CheckedStatement>();
	auto body = std::make_unique<BlockStatement>();
	node->Body(body.get());
	h.visitor->VisitCheckedStatement(node.get());
	// start, kw:checked, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:checked");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitUncheckedStatement` is the structural twin of `VisitCheckedStatement` with the
// `unchecked` keyword; the recorded sequence is identical except for the keyword.
TEST(CSharp_OutputVisitor, VisitUncheckedStatement) {
	V h;
	auto node = std::make_unique<UncheckedStatement>();
	auto body = std::make_unique<BlockStatement>();
	node->Body(body.get());
	h.visitor->VisitUncheckedStatement(node.get());
	// start, kw:unchecked, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:unchecked");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitUnsafeStatement` is the structural twin of `VisitCheckedStatement` with the `unsafe`
// keyword; the recorded sequence is identical except for the keyword.
TEST(CSharp_OutputVisitor, VisitUnsafeStatement) {
	V h;
	auto node = std::make_unique<UnsafeStatement>();
	auto body = std::make_unique<BlockStatement>();
	node->Body(body.get());
	h.visitor->VisitUnsafeStatement(node.get());
	// start, kw:unsafe, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:unsafe");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// ---- The simple member/argument expression Visit methods -------------------

// `VisitIdentifierExpression` (named, no type arguments) writes the backing `IdentifierToken`.
// Structurally `VisitSimpleType` applied to the `Expression` hierarchy (the D325 `ToVector`
// snapshot of an empty `TypeArguments` collection writes nothing).
TEST(CSharp_OutputVisitor, VisitIdentifierExpressionNamed) {
	V h;
	auto node = std::make_unique<IdentifierExpression>(std::string("Foo"));
	h.visitor->VisitIdentifierExpression(node.get());
	// start, id:Foo, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitIdentifierExpression` with a `TypeArguments` collection renders `List<T>` -- the
// `WriteTypeArguments` writes the chevrons and recurses through `VisitSimpleType`.
TEST(CSharp_OutputVisitor, VisitIdentifierExpressionWithTypeArguments) {
	V h;
	auto typeArg = std::make_unique<SimpleType>(std::string("T"));
	auto node = std::make_unique<IdentifierExpression>(std::string("List"));
	node->TypeArguments().Add(typeArg.get());
	h.visitor->VisitIdentifierExpression(node.get());
	// start, id:List, tok:<, start(SimpleType), id:T, end(SimpleType), tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:List");
	EXPECT_EQ(h.inner.calls[2], "tok:<");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:T");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:>");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitIndexerExpression` over `a[0]` -- the `Target` recurses through `VisitIdentifierExpression`,
// then the bracketed argument list (the default policy has no spaces before/within the brackets).
TEST(CSharp_OutputVisitor, VisitIndexerExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto node = std::make_unique<IndexerExpression>(target.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitIndexerExpression(node.get());
	// start, start(IdentifierExpression), id:a, end, tok:[, start(PrimitiveExpression), primval,
	// end, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:[");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:]");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitIndexerExpression` with a null `Target` and an empty argument list renders `[]`.
TEST(CSharp_OutputVisitor, VisitIndexerExpressionNoTargetEmpty) {
	V h;
	auto node = std::make_unique<IndexerExpression>();
	h.visitor->VisitIndexerExpression(node.get());
	// start, tok:[, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:[");
	EXPECT_EQ(h.inner.calls[2], "tok:]");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitNamedArgumentExpression` over `name: 0` -- the name identifier, a colon, an explicit
// `Space()`, then the argument expression.
TEST(CSharp_OutputVisitor, VisitNamedArgumentExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto node = std::make_unique<NamedArgumentExpression>(std::string("name"), expr.get());
	h.visitor->VisitNamedArgumentExpression(node.get());
	// start, id:name, tok::, space, start(PrimitiveExpression), primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:name");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "primval");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitNamedExpression` over `name = 0` -- the name identifier, an explicit `Space()`, the
// assign token, another explicit `Space()`, then the initializer expression.
TEST(CSharp_OutputVisitor, VisitNamedExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto node = std::make_unique<NamedExpression>(std::string("name"), expr.get());
	h.visitor->VisitNamedExpression(node.get());
	// start, id:name, space, tok:=, space, start(PrimitiveExpression), primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:name");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:=");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitMemberReferenceExpression` over `a.B` -- the `Target` recurses through
// `VisitIdentifierExpression`, then the dot and the member-name identifier. A standalone member
// reference (the `Target` is not an `InvocationExpression`) has chain length 0, so no chain
// newline/indent is inserted.
TEST(CSharp_OutputVisitor, VisitMemberReferenceExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto node = std::make_unique<MemberReferenceExpression>(target.get(), std::string("B"));
	h.visitor->VisitMemberReferenceExpression(node.get());
	// start, start(IdentifierExpression), id:a, end, tok:., id:B, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:.");
	EXPECT_EQ(h.inner.calls[5], "id:B");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitMemberReferenceExpression` with a `TypeArguments` collection renders `a.B<T>` -- the
// `WriteTypeArguments` writes the chevrons and recurses through `VisitSimpleType`.
TEST(CSharp_OutputVisitor, VisitMemberReferenceExpressionWithTypeArguments) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto typeArg = std::make_unique<SimpleType>(std::string("T"));
	auto node = std::make_unique<MemberReferenceExpression>(target.get(), std::string("B"));
	node->TypeArguments().Add(typeArg.get());
	h.visitor->VisitMemberReferenceExpression(node.get());
	// start, start(id:a), id:a, end, tok:., id:B, tok:<, start(SimpleType), id:T, end, tok:>, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:.");
	EXPECT_EQ(h.inner.calls[5], "id:B");
	EXPECT_EQ(h.inner.calls[6], "tok:<");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:T");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:>");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitPointerReferenceExpression` over `a->B` -- the structural twin of the member reference,
// using the `ArrowToken` (`->`) instead of the dot. No chain newline logic (the C# source has
// none for pointer member access).
TEST(CSharp_OutputVisitor, VisitPointerReferenceExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto node = std::make_unique<PointerReferenceExpression>(target.get(), std::string("B"));
	h.visitor->VisitPointerReferenceExpression(node.get());
	// start, start(IdentifierExpression), id:a, end, tok:->, id:B, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:->");
	EXPECT_EQ(h.inner.calls[5], "id:B");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitInvocationExpression` over `Foo()` -- the `Target` recurses through
// `VisitIdentifierExpression`, then the empty argument list in parentheses (the default policy
// has no space before/within the call parentheses).
TEST(CSharp_OutputVisitor, VisitInvocationExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("Foo"));
	auto node = std::make_unique<InvocationExpression>(target.get());
	h.visitor->VisitInvocationExpression(node.get());
	// start, start(IdentifierExpression), id:Foo, end, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitInvocationExpression` over `Foo(1)` -- a single argument recurses through
// `VisitPrimitiveExpression` between the parentheses (no comma for a single-element list).
TEST(CSharp_OutputVisitor, VisitInvocationExpressionOneArg) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("Foo"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<InvocationExpression>(target.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitInvocationExpression(node.get());
	// start, start(id:Foo), id:Foo, end, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitTupleExpression` over `(1, 2)` -- the elements in parentheses, comma-separated (the
// default policy has no bracket-comma spaces).
TEST(CSharp_OutputVisitor, VisitTupleExpression) {
	V h;
	auto e0 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<TupleExpression>();
	node->Elements().Add(e0.get());
	node->Elements().Add(e1.get());
	h.visitor->VisitTupleExpression(node.get());
	// start, tok:(, start, primval, end, tok:,, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primval");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:,");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "primval");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "tok:)");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitObjectCreateExpression` over `new Foo()` -- no arguments and no initializer, so the
// `useParenthesis = Arguments.Any() || Initializer is null` gate emits the empty call parens
// (the default policy has no space before/within the call parentheses).
TEST(CSharp_OutputVisitor, VisitObjectCreateExpressionNoArgsNoInitializer) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("Foo"));
	auto node = std::make_unique<ObjectCreateExpression>(type.get());
	h.visitor->VisitObjectCreateExpression(node.get());
	// start, kw:new, start(SimpleType), space (decorator: new->identifier), id:Foo, end, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "id:Foo");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:(");
	EXPECT_EQ(h.inner.calls[7], "tok:)");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitObjectCreateExpression` over `new Foo(1)` -- one argument, so the call parens wrap the
// comma-separated argument list (the default policy has no inner spaces).
TEST(CSharp_OutputVisitor, VisitObjectCreateExpressionOneArg) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("Foo"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<ObjectCreateExpression>(type.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitObjectCreateExpression(node.get());
	// start, kw:new, start(SimpleType), space (decorator: new->identifier), id:Foo, end, tok:(, start, primval, end, tok:), end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "id:Foo");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:(");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:)");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitArrayCreateExpression` over `new int[5]` -- a sized array (the `Type` recurses through
// `VisitPrimitiveType`, the size expression in brackets, no initializer).
TEST(CSharp_OutputVisitor, VisitArrayCreateExpressionSized) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto size = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(5)));
	auto node = std::make_unique<ArrayCreateExpression>(type.get());
	node->Arguments().Add(size.get());
	h.visitor->VisitArrayCreateExpression(node.get());
	// start, kw:new, start(PrimitiveType), space (decorator: new->primtype), primtype:int, end, tok:[, start, primval, end, tok:], end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:[");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:]");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitArrayCreateExpression` over `new int[] { 1, 2 }` -- an empty size bracket followed by an
// `ArrayInitializerExpression` (the empty `Arguments` skips the bracket write; the initializer
// recurses through `VisitArrayInitializerExpression`).
TEST(CSharp_OutputVisitor, VisitArrayCreateExpressionWithInitializer) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto e0 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto init = std::make_unique<ArrayInitializerExpression>();
	init->Elements().Add(e0.get());
	init->Elements().Add(e1.get());
	auto node = std::make_unique<ArrayCreateExpression>(type.get());
	node->Initializer(init.get());
	h.visitor->VisitArrayCreateExpression(node.get());
	// start, kw:new, start(PrimitiveType), space (decorator: new->primtype), primtype:int, end, then the initializer
	// { 1, 2 }: start, space (OpenBrace, line not empty), tok:{, space, start, primval, end, tok:,, space,
	// start, primval, end, space, tok:}, end, then end.
	ASSERT_EQ(h.inner.calls.size(), 22u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "tok:{");
	EXPECT_EQ(h.inner.calls[9], "space");
	EXPECT_EQ(h.inner.calls[10], "start");
	EXPECT_EQ(h.inner.calls[11], "primval");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "tok:,");
	EXPECT_EQ(h.inner.calls[14], "space");
	EXPECT_EQ(h.inner.calls[15], "start");
	EXPECT_EQ(h.inner.calls[16], "primval");
	EXPECT_EQ(h.inner.calls[17], "end");
	EXPECT_EQ(h.inner.calls[18], "space");
	EXPECT_EQ(h.inner.calls[19], "tok:}");
	EXPECT_EQ(h.inner.calls[20], "end");
	EXPECT_EQ(h.inner.calls[21], "end");
}

// `VisitArrayCreateExpression` over `new int[5][]` -- a sized first bracket followed by an
// additional rank-2 `ArraySpecifier` (the trailing `[,]` without size info).
TEST(CSharp_OutputVisitor, VisitArrayCreateExpressionAdditionalSpecifier) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto size = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(5)));
	auto spec = std::make_unique<ArraySpecifier>(2);
	auto node = std::make_unique<ArrayCreateExpression>(type.get());
	node->Arguments().Add(size.get());
	node->AdditionalArraySpecifiers().Add(spec.get());
	h.visitor->VisitArrayCreateExpression(node.get());
	// start, kw:new, start(PrimitiveType), space (decorator: new->primtype), primtype:int, end, tok:[, start, primval, end, tok:],
	// then VisitArraySpecifier rank 2: start, tok:[, tok:,, tok:], end, then end.
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:[");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:]");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "tok:[");
	EXPECT_EQ(h.inner.calls[13], "tok:,");
	EXPECT_EQ(h.inner.calls[14], "tok:]");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitArrayInitializerExpression` over a standalone `{ 1, 2 }` -- the `PrintInitializerElements`
// path (the `bracesAreOptional` gate is false for a parentless initializer), the default policy
// does not wrap two simple primitives, so the braces hug with inner spaces.
TEST(CSharp_OutputVisitor, VisitArrayInitializerExpressionTwoElements) {
	V h;
	auto e0 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<ArrayInitializerExpression>();
	node->Elements().Add(e0.get());
	node->Elements().Add(e1.get());
	h.visitor->VisitArrayInitializerExpression(node.get());
	// start, tok:{, space, start, primval, end, tok:,, space, start, primval, end, space, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 14u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:,");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "primval");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "space");
	EXPECT_EQ(h.inner.calls[12], "tok:}");
	EXPECT_EQ(h.inner.calls[13], "end");
}

// `VisitArrayInitializerExpression` over the inner `{ 1 }` of `new List { { 1 } }` -- the
// `bracesAreOptional` gate is true (a single non-assignment element whose enclosing initializer is
// the `Initializer` slot of an `ObjectCreateExpression`), so the nested braces are omitted and only
// the single element renders. The full parent chain (`inner` is an `Elements` child of `outer`,
// which is the `Initializer` of `objCreate`) is built so `IsObjectOrCollectionInitializer` sees
// the outer initializer's slot kind.
TEST(CSharp_OutputVisitor, VisitArrayInitializerExpressionBracesOptional) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("List"));
	auto prim1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto inner = std::make_unique<ArrayInitializerExpression>();
	inner->Elements().Add(prim1.get());
	auto outer = std::make_unique<ArrayInitializerExpression>();
	outer->Elements().Add(inner.get());
	auto objCreate = std::make_unique<ObjectCreateExpression>(type.get());
	objCreate->Initializer(outer.get());
	h.visitor->VisitArrayInitializerExpression(inner.get());
	// start(inner), start(PrimitiveExpression), primval, end, end(inner) -- the nested braces are
	// optional so only the single element renders.
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitVariableInitializer` over `x` (a name with no initializer) -- the `NameToken` identifier is
// written as a flat token (no `StartNode`/`EndNode`), and the nullable `Initializer` is absent so
// the `=` clause is skipped.
TEST(CSharp_OutputVisitor, VisitVariableInitializer) {
	V h;
	auto node = std::make_unique<VariableInitializer>(std::string("x"));
	h.visitor->VisitVariableInitializer(node.get());
	// start, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:x");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitVariableInitializer` over `x = 1` -- the default `SpaceAroundAssignment=false` omits the
// spaces around the `=` token, and the `Initializer` recurses through `VisitPrimitiveExpression`.
TEST(CSharp_OutputVisitor, VisitVariableInitializerWithInitializer) {
	V h;
	auto init = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<VariableInitializer>(std::string("x"), init.get());
	h.visitor->VisitVariableInitializer(node.get());
	// start, id:x, tok:=, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:x");
	EXPECT_EQ(h.inner.calls[2], "tok:=");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitOutVarDeclarationExpression` over `out int x` -- the `out` keyword, an explicit space, the
// `Type` recursing through `VisitSimpleType`, another explicit space, and the `Variable` recursing
// through `VisitVariableInitializer` (the name only, no initializer).
TEST(CSharp_OutputVisitor, VisitOutVarDeclarationExpression) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto variable = std::make_unique<VariableInitializer>(std::string("x"));
	auto node = std::make_unique<OutVarDeclarationExpression>(type.get(), variable.get());
	h.visitor->VisitOutVarDeclarationExpression(node.get());
	// start, kw:out, space, start(SimpleType), id:int, end, space, start(VariableInitializer),
	// id:x, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:out");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:x");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitWithInitializerExpression` over `null with { }` -- the `Expression` recurses through
// `VisitNullReferenceExpression`, the `with` keyword follows, and the empty `Initializer`
// `ArrayInitializerExpression` renders its braces (the `bracesAreOptional` gate is false since the
// enclosing slot is the `WithInitializerExpression`, not an object/collection initializer). The
// `OpenBrace` inserts a space before `{` (the line is not empty), and `PrintInitializerElements`
// inserts another space before the closing `}` (the `if (!wrap) Space()` before the empty list).
TEST(CSharp_OutputVisitor, VisitWithInitializerExpression) {
	V h;
	auto expr = std::make_unique<NullReferenceExpression>();
	auto initializer = std::make_unique<ArrayInitializerExpression>();
	auto node = std::make_unique<WithInitializerExpression>(expr.get(), initializer.get());
	h.visitor->VisitWithInitializerExpression(node.get());
	// start, start(NullRef), primval, end, kw:with, start(ArrayInit), space, tok:{, space, tok:},
	// end, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "kw:with");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "tok:}");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitUndocumentedExpression` over `__arglist()` (the `ArgList` case with empty `Arguments`) --
// the `__arglist` keyword, no space (the default `SpaceBeforeMethodCallParentheses=false`), and the
// empty argument list in parentheses.
TEST(CSharp_OutputVisitor, VisitUndocumentedExpressionArgListEmpty) {
	V h;
	auto node = std::make_unique<UndocumentedExpression>(UndocumentedExpressionType::ArgList);
	h.visitor->VisitUndocumentedExpression(node.get());
	// start, kw:__arglist, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:__arglist");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:)");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitUndocumentedExpression` over `__arglist` (the `ArgListAccess` case) -- the `ArgListAccess`
// branch writes only the keyword and skips the argument-list parentheses (the C# `!= ArgListAccess`
// gate).
TEST(CSharp_OutputVisitor, VisitUndocumentedExpressionArgListAccess) {
	V h;
	auto node = std::make_unique<UndocumentedExpression>();
	h.visitor->VisitUndocumentedExpression(node.get());
	// start, kw:__arglist, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:__arglist");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitStackAllocExpression` over `stackalloc int[1]` (a `Type`, a `CountExpression`, no
// `Initializer`) -- the `stackalloc` keyword, the `Type` recursing through `VisitSimpleType` (the
// `InsertRequiredSpacesDecorator` inserts a space before `id:int` since `int` is a keyword), and the
// single-element `CountExpression` in brackets (the default `SpacesWithinBrackets=false` omits the
// inner spaces).
TEST(CSharp_OutputVisitor, VisitStackAllocExpression) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto count = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<StackAllocExpression>();
	node->Type(type.get());
	node->CountExpression(count.get());
	h.visitor->VisitStackAllocExpression(node.get());
	// start, kw:stackalloc, start(SimpleType), space, id:int, end, tok:[, start, primval, end, tok:], end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:stackalloc");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:[");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:]");
	EXPECT_EQ(h.inner.calls[11], "end");
}


// `VisitErrorExpression` over a default-constructed node -- the C# `VisitErrorNode` is a
// `StartNode`/`EndNode` pair with no body (a leaf placeholder with no [Slot] children); the
// `InsertMissingTokensDecorator` records the span onto `ErrorExpression::Location` from
// `StartNode`'s `ILocatable` position.
TEST(CSharp_OutputVisitor, VisitErrorExpression) {
	V h;
	auto node = std::make_unique<ErrorExpression>();
	h.visitor->VisitErrorExpression(node.get());
	// start, end
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "end");
}

// `VisitSingleVariableDesignation` over `x` -- the `StartNode`, the `IdentifierToken` rendered as
// a flat identifier (no nested StartNode/EndNode, the `VisitIdentifier` ITokenWriter contract),
// and the `EndNode`.
TEST(CSharp_OutputVisitor, VisitSingleVariableDesignation) {
	V h;
	auto node = std::make_unique<SingleVariableDesignation>(std::string("x"));
	h.visitor->VisitSingleVariableDesignation(node.get());
	// start, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:x");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitParenthesizedVariableDesignation` over `()` -- the `LPar`, an empty comma list (no inner
// tokens), and the `RPar`.
TEST(CSharp_OutputVisitor, VisitParenthesizedVariableDesignationEmpty) {
	V h;
	auto node = std::make_unique<ParenthesizedVariableDesignation>();
	h.visitor->VisitParenthesizedVariableDesignation(node.get());
	// start, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "tok:)");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitParenthesizedVariableDesignation` over `(x, y)` -- the two `SingleVariableDesignation`
// children recurse through `VisitSingleVariableDesignation` (the comma-separated list inserts a
// `tok:,` between them; the default policy has no bracket-comma spaces).
TEST(CSharp_OutputVisitor, VisitParenthesizedVariableDesignationTwo) {
	V h;
	auto a = std::make_unique<SingleVariableDesignation>(std::string("x"));
	auto b = std::make_unique<SingleVariableDesignation>(std::string("y"));
	auto node = std::make_unique<ParenthesizedVariableDesignation>();
	node->VariableDesignations().Add(a.get());
	node->VariableDesignations().Add(b.get());
	h.visitor->VisitParenthesizedVariableDesignation(node.get());
	// start, tok:(, start, id:x, end, tok:,, start, id:y, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "id:x");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:,");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "id:y");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "tok:)");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitTupleTypeElement` with a name `int Name` -- the `Type` recurses through
// `VisitPrimitiveType`, then an explicit `Space`, then the `NameToken` recurses through
// `VisitIdentifier` (which is a flat token, no `StartNode`/`EndNode` per the D325 contract).
TEST(CSharp_OutputVisitor, VisitTupleTypeElementNamed) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TupleTypeElement>(type.get(), std::string("Name"));
	h.visitor->VisitTupleTypeElement(node.get());
	// start, start(PrimitiveType), primtype:int, end(PrimitiveType), space, id:Name, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "id:Name");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitTupleTypeElement` without a name `int` -- the nullable `NameToken` is absent, so the
// `Space` + `NameToken` recursion is skipped (the bare `type` element form); the node's own
// `StartNode`/`EndNode` still bracket the `Type` recursion.
TEST(CSharp_OutputVisitor, VisitTupleTypeElementNameless) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TupleTypeElement>();
	node->Type(type.get());
	h.visitor->VisitTupleTypeElement(node.get());
	// start(TupleTypeElement), start(PrimitiveType), primtype:int, end(PrimitiveType), end(TupleTypeElement)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitTupleType` over `(int, string)` -- the elements in parentheses, comma-separated (the
// default policy has no bracket-comma spaces); each `TupleTypeElement` recurses through
// `VisitTupleTypeElement` (its own `start`/`end` plus the `Type`'s `start`/`primtype`/`end`), so
// the first element `int` contributes 5 entries and the second `string` contributes 5.
TEST(CSharp_OutputVisitor, VisitTupleType) {
	V h;
	auto intType = std::make_unique<PrimitiveType>(std::string("int"));
	auto e0 = std::make_unique<TupleTypeElement>();
	e0->Type(intType.get());
	auto strType = std::make_unique<SimpleType>(std::string("string"));
	auto e1 = std::make_unique<TupleTypeElement>();
	e1->Type(strType.get());
	auto node = std::make_unique<TupleAstType>();
	node->Elements().Add(e0.get());
	node->Elements().Add(e1.get());
	h.visitor->VisitTupleType(node.get());
	// start, tok:(, start, start, primtype:int, end, end, tok:,, start, start, id:string, end, end, tok:), end
	ASSERT_GE(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "tok:,");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "id:string");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "tok:)");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `VisitInvocationType` over `Foo()` -- the `BaseType` recurses through `VisitSimpleType`, then
// the empty `Arguments` list renders just the parens (the C# uses `WriteToken(Tokens.LPar/RPar)`
// rather than the `LPar()`/`RPar()` helpers).
TEST(CSharp_OutputVisitor, VisitInvocationTypeNoArgs) {
	V h;
	auto baseType = std::make_unique<SimpleType>(std::string("Foo"));
	auto node = std::make_unique<InvocationAstType>();
	node->BaseType(baseType.get());
	h.visitor->VisitInvocationType(node.get());
	// start, start(SimpleType), id:Foo, end(SimpleType), tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitInvocationType` over `Foo(null)` -- a single `Arguments` element recurses through
// `VisitNullReferenceExpression` (a `start/primval/end` sub-trace) between the parens.
TEST(CSharp_OutputVisitor, VisitInvocationTypeOneArg) {
	V h;
	auto baseType = std::make_unique<SimpleType>(std::string("Foo"));
	auto arg = std::make_unique<NullReferenceExpression>();
	auto node = std::make_unique<InvocationAstType>();
	node->BaseType(baseType.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitInvocationType(node.get());
	// start, start(SimpleType), id:Foo, end, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitFunctionPointerType` over `delegate*<int>` -- no unmanaged calling convention, no
// calling-convention list, and an empty `Parameters` (so the angle-bracket list is just the
// `ReturnType` `int`); the `delegate` keyword and the `*` pointer token have no space between
// them (the decorator does not insert one for a keyword followed by `*`).
TEST(CSharp_OutputVisitor, VisitFunctionPointerTypeBare) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<FunctionPointerAstType>();
	node->ReturnType(retType.get());
	h.visitor->VisitFunctionPointerType(node.get());
	// start, kw:delegate, tok:*, tok:<, start, primtype:int, end, tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:delegate");
	EXPECT_EQ(h.inner.calls[2], "tok:*");
	EXPECT_EQ(h.inner.calls[3], "tok:<");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "primtype:int");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "tok:>");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitFunctionPointerType` over `delegate* unmanaged[Cdecl]<int>` -- the
// `HasUnmanagedCallingConvention` gate inserts a `Space` + the `unmanaged` keyword, the
// `CallingConventions` list renders the `[Cdecl]` bracket group (the convention is a `SimpleType`
// recursing through `VisitSimpleType`), and the empty `Parameters` leaves the `ReturnType` `int`
// as the sole angle-bracket element.
TEST(CSharp_OutputVisitor, VisitFunctionPointerTypeUnmanagedConvention) {
	V h;
	auto conv = std::make_unique<SimpleType>(std::string("Cdecl"));
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<FunctionPointerAstType>();
	node->HasUnmanagedCallingConvention(true);
	node->CallingConventions().Add(conv.get());
	node->ReturnType(retType.get());
	h.visitor->VisitFunctionPointerType(node.get());
	// start, kw:delegate, tok:*, space, kw:unmanaged, tok:[, start, id:Cdecl, end, tok:], tok:<,
	// start, primtype:int, end, tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:delegate");
	EXPECT_EQ(h.inner.calls[2], "tok:*");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "kw:unmanaged");
	EXPECT_EQ(h.inner.calls[5], "tok:[");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "id:Cdecl");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "tok:]");
	EXPECT_EQ(h.inner.calls[10], "tok:<");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "primtype:int");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "tok:>");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// ---- VisitDeclarationExpression / VisitAnonymousTypeCreateExpression /
//      VisitLambdaExpression / VisitAnonymousMethodExpression ----------------

// `VisitDeclarationExpression` over `int x` (the `T x` declaration form) -- the `Type` recurses
// through `VisitPrimitiveType` (start/primtype:int/end), an explicit `Space`, then the
// `Designation` recurses through `VisitSingleVariableDesignation` (start/id:x/end).
TEST(CSharp_OutputVisitor, VisitDeclarationExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto designation = std::make_unique<SingleVariableDesignation>(std::string("x"));
	auto node = std::make_unique<DeclarationExpression>(type.get(), designation.get());
	h.visitor->VisitDeclarationExpression(node.get());
	// start, start(PrimitiveType), primtype:int, end, space, start(designation), id:x, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitDeclarationExpression` over `var (a, b)` (a deconstruction) -- the `Type` recurses through
// `VisitSimpleType` (start/id:var/end), an explicit `Space`, then the `ParenthesizedVariableDesignation`
// recurses through `VisitParenthesizedVariableDesignation` rendering `(a, b)`.
TEST(CSharp_OutputVisitor, VisitDeclarationExpressionVarDeconstruction) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("var"));
	auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
	auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
	auto designation = std::make_unique<ParenthesizedVariableDesignation>();
	designation->VariableDesignations().Add(a.get());
	designation->VariableDesignations().Add(b.get());
	auto node = std::make_unique<DeclarationExpression>(type.get(), designation.get());
	h.visitor->VisitDeclarationExpression(node.get());
	// start, start(SimpleType), id:var, end, space, start(ParenDesig), tok:(, start, id:a, end,
	// tok:,, start, id:b, end, tok:), end, end
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:var");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "tok:(");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:a");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:,");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "id:b");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "tok:)");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitAnonymousTypeCreateExpression` over `new {}` (empty initializers) -- `new` then the
// initializer braces directly over the (empty) `Initializers` collection (no type, no nested
// `ArrayInitializerExpression` node). `PrintInitializerElements` emits the OpenBrace space (the
// line is not empty after `kw:new`), one inner space, and the close brace.
TEST(CSharp_OutputVisitor, VisitAnonymousTypeCreateExpressionEmpty) {
	V h;
	auto node = std::make_unique<AnonymousTypeCreateExpression>();
	h.visitor->VisitAnonymousTypeCreateExpression(node.get());
	// start, kw:new, space, tok:{, space, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:{");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:}");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitAnonymousTypeCreateExpression` over `new { 1, 2 }` (two simple primitive initializers) --
// `PrintInitializerElements` does not wrap (two simple primitives), so the braces hug the list
// with inner spaces and a bare comma between the elements.
TEST(CSharp_OutputVisitor, VisitAnonymousTypeCreateExpressionTwoElements) {
	V h;
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e2 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<AnonymousTypeCreateExpression>();
	node->Initializers().Add(e1.get());
	node->Initializers().Add(e2.get());
	h.visitor->VisitAnonymousTypeCreateExpression(node.get());
	// start, kw:new, space, tok:{, space, start, primval, end, tok:,, space, start, primval, end,
	// space, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:{");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:,");
	EXPECT_EQ(h.inner.calls[9], "space");
	EXPECT_EQ(h.inner.calls[10], "start");
	EXPECT_EQ(h.inner.calls[11], "primval");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "space");
	EXPECT_EQ(h.inner.calls[14], "tok:}");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitLambdaExpression` over `() => null` (empty parameters, expression body) -- the empty
// parameter list is parenthesized (`LambdaNeedsParenthesis` returns true for zero parameters),
// then the `=>` arrow and the expression body (a `NullReferenceExpression`).
TEST(CSharp_OutputVisitor, VisitLambdaExpressionExpressionBody) {
	V h;
	auto body = std::make_unique<NullReferenceExpression>();
	auto node = std::make_unique<LambdaExpression>();
	node->Body(body.get());
	h.visitor->VisitLambdaExpression(node.get());
	// start, tok:(, tok:), space, tok:=>, space, start(NullRef), primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "tok:)");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:=>");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "primval");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitLambdaExpression` over `async () => null` (async, empty parameters, expression body) --
// the `async` modifier keyword plus an explicit `Space` precede the parenthesized parameter list.
TEST(CSharp_OutputVisitor, VisitLambdaExpressionAsync) {
	V h;
	auto body = std::make_unique<NullReferenceExpression>();
	auto node = std::make_unique<LambdaExpression>();
	node->IsAsync(true);
	node->Body(body.get());
	h.visitor->VisitLambdaExpression(node.get());
	// start, kw:async, space, tok:(, tok:), space, tok:=>, space, start, primval, end, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:async");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:(");
	EXPECT_EQ(h.inner.calls[4], "tok:)");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "tok:=>");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "primval");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}
// `VisitLambdaExpression` over `() => { }` (empty parameters, block body) -- the body is a
// `BlockStatement`, so it recurses through `WriteBlock` (no `Space` before the block, the
// `OpenBrace` inserts its own space since the line is not empty after `=>`).
TEST(CSharp_OutputVisitor, VisitLambdaExpressionBlockBody) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<LambdaExpression>();
	node->Body(body.get());
	h.visitor->VisitLambdaExpression(node.get());
	// start, tok:(, tok:), space, tok:=>, start(block), space, tok:{, indent, newline,
	// unindent, tok:}, end, end (the `WriteBlock` body's `OpenBrace` defaults `newLine=true`, so it
	// emits `indent`+`newline` after `{`, and `CloseBrace` defaults `unindent=true`, so it emits
	// `unindent` before `}`).
	ASSERT_EQ(h.inner.calls.size(), 14u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "tok:)");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:=>");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "unindent");
	EXPECT_EQ(h.inner.calls[11], "tok:}");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "end");
}

// `VisitAnonymousMethodExpression` over `delegate {}` (empty parameters, empty block body) --
// the `delegate` keyword, the parameter list omitted entirely (empty `Parameters`), then the
// `WriteBlock` body.
TEST(CSharp_OutputVisitor, VisitAnonymousMethodExpression) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<AnonymousMethodExpression>();
	node->Body(body.get());
	h.visitor->VisitAnonymousMethodExpression(node.get());
	// start, kw:delegate, start(block), space, tok:{, indent, newline, unindent, tok:}, end, end
	// (the `WriteBlock` body's `OpenBrace` defaults `newLine=true` -> `indent`+`newline`, and
	// `CloseBrace` defaults `unindent=true` -> `unindent`).
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:delegate");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitAnonymousMethodExpression` over `async delegate {}` (async, empty parameters, empty block
// body) -- the `async` modifier keyword plus an explicit `Space` precede the `delegate` keyword.
TEST(CSharp_OutputVisitor, VisitAnonymousMethodExpressionAsync) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<AnonymousMethodExpression>();
	node->IsAsync(true);
	node->Body(body.get());
	h.visitor->VisitAnonymousMethodExpression(node.get());
	// start, kw:async, space, kw:delegate, start(block), space, tok:{, indent, newline, unindent,
	// tok:}, end, end (the `WriteBlock` body's `OpenBrace`/`CloseBrace` default `newLine=true`/
	// `unindent=true`).
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:async");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:delegate");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "tok:{");
	EXPECT_EQ(h.inner.calls[7], "indent");
	EXPECT_EQ(h.inner.calls[8], "newline");
	EXPECT_EQ(h.inner.calls[9], "unindent");
	EXPECT_EQ(h.inner.calls[10], "tok:}");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// `VisitParameterDeclaration` over `int x` (a plain value parameter, no modifiers) -- the `Type`
// recurses through `VisitSimpleType` (start/id:int/end), a `Space` separates the type from the
// name (the `Type is not null && Name` non-empty gate), and the `NameToken` identifier follows.
// `WriteAttributes` over the empty `Attributes` collection records nothing.
TEST(CSharp_OutputVisitor, VisitParameterDeclarationPlain) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, start(SimpleType), id:int, end, space, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "id:x");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitParameterDeclaration` over `out int x` (the `Out` `ParameterModifier` branch) -- the
// `out` keyword + an explicit `Space` precede the type/name (the `ReferenceKind.Out` switch
// case).
TEST(CSharp_OutputVisitor, VisitParameterDeclarationOut) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->ParameterModifier(ReferenceKind::Out);
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, kw:out, space, start, id:int, end, space, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:out");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "id:x");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitParameterDeclaration` over `ref readonly int x` (the `RefReadOnly` branch) -- the `ref`
// keyword, a decorator-inserted space (two consecutive `WriteKeyword` calls), the `readonly`
// keyword, an explicit `Space`, then the type/name. The `InsertRequiredSpacesDecorator` inserts
// the space between `ref` and `readonly` (its `WriteKeyword` override: `lastWritten ==
// KeywordOrIdentifier` -> `Space()`).
TEST(CSharp_OutputVisitor, VisitParameterDeclarationRefReadOnly) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->ParameterModifier(ReferenceKind::RefReadOnly);
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, kw:ref, space(decorator), kw:readonly, space(explicit), start, id:int, end, space,
	// id:x, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:ref");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:readonly");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:int");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "id:x");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitParameterDeclaration` over `int x = 5` (a `DefaultExpression`) -- the type/name render,
// then the default (`= expr`) with the default `SpaceAroundAssignment=false` policy so no spaces
// around `=`, and the `DefaultExpression` recurses through `VisitPrimitiveExpression`.
TEST(CSharp_OutputVisitor, VisitParameterDeclarationWithDefault) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto defaultExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(5)));
	auto node = std::make_unique<ParameterDeclaration>();
	node->Type(type.get());
	node->Name("x");
	node->DefaultExpression(defaultExpr.get());
	h.visitor->VisitParameterDeclaration(node.get());
	// start, start(SimpleType), id:int, end, space, id:x, tok:=, start(Primitive), primval, end,
	// end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "id:x");
	EXPECT_EQ(h.inner.calls[6], "tok:=");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitParameterDeclaration` over a nameless `int` (a `Type` with no `Name`) -- the type
// recurses, but the `Type is not null && Name` non-empty gate is false (the name is empty) so
// no `Space` and no `WriteIdentifier`; the parameter renders just the type.
TEST(CSharp_OutputVisitor, VisitParameterDeclarationNameless) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->Type(type.get());
	h.visitor->VisitParameterDeclaration(node.get());
	// start, start(SimpleType), id:int, end, end (no name -> no space, no identifier)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitParameterDeclaration` over `this int x` (an extension-method `this` parameter) -- the
// `this` keyword + an explicit `Space` precede the type/name (the `HasThisModifier` branch).
TEST(CSharp_OutputVisitor, VisitParameterDeclarationThisModifier) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->HasThisModifier(true);
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, kw:this, space, start(SimpleType), id:int, end, space, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:this");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "id:x");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// ---- The control-flow statement Visit methods (Lock/Using/For/Foreach/Fixed) -------------

// `VisitLockStatement` writes `lock (expr) body`: the `Expression` is a required slot recursed
// inside the parens, the body a required embedded `Statement`. With the default policy (no space
// before/within the lock parens) and a non-block `break` body, the sequence is the keyword, the
// parenthesized expression, then the embedded `break` (newline + indent + the break's own
// sequence + unindent).
TEST(CSharp_OutputVisitor, VisitLockStatement) {
	V h;
	auto node = std::make_unique<LockStatement>();
	auto expr = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->Expression(expr.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitLockStatement(node.get());
	// start, kw:lock, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:lock");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitUsingStatement` writes `using (resource) body` (the non-enhanced, non-async form). The
// `ResourceAcquisition` is a required `AstNode`-typed slot recursed inside the parens; the body a
// required embedded `Statement`. With the default policy and a non-block `break` body, the
// sequence mirrors `VisitLockStatement` (the `using` keyword, the parenthesized resource, the
// embedded `break`).
TEST(CSharp_OutputVisitor, VisitUsingStatement) {
	V h;
	auto node = std::make_unique<UsingStatement>();
	auto resource = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->ResourceAcquisition(resource.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitUsingStatement(node.get());
	// start, kw:using, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:using");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitUsingStatement` with `IsAsync` writes the leading `await` keyword before `using`. The
// `InsertRequiredSpacesDecorator` inserts a space between the two consecutive `WriteKeyword`
// calls (`await` then `using`) that the `Visit` method never writes explicitly, so the sequence
// carries a decorator-inserted `space` between `kw:await` and `kw:using`.
TEST(CSharp_OutputVisitor, VisitUsingStatementAsync) {
	V h;
	auto node = std::make_unique<UsingStatement>();
	auto resource = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->IsAsync(true);
	node->ResourceAcquisition(resource.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitUsingStatement(node.get());
	// start, kw:await, space(decorator), kw:using, tok:(, start, primval, end, tok:), newline,
	// indent, start, kw:break, tok:;, newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 18u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:await");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:using");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "indent");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "kw:break");
	EXPECT_EQ(h.inner.calls[13], "tok:;");
	EXPECT_EQ(h.inner.calls[14], "newline");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "unindent");
	EXPECT_EQ(h.inner.calls[17], "end");
}

// `VisitForStatement` writes `for (init; cond; iter) body`. With empty `Initializers`, a null
// `Condition`, and empty `Iterators` (the `for (;;)` form), the two `WriteToken(Semicolon)`
// calls emit two bare `tok:;` tokens (the default policy adds no surrounding spaces), then the
// closing paren and the embedded `break` body.
TEST(CSharp_OutputVisitor, VisitForStatementEmpty) {
	V h;
	auto node = std::make_unique<ForStatement>();
	auto body = std::make_unique<BreakStatement>();
	node->EmbeddedStatement(body.get());
	h.visitor->VisitForStatement(node.get());
	// start, kw:for, tok:(, tok:;, tok:;, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_GE(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:for");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:;");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "indent");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "kw:break");
	EXPECT_EQ(h.inner.calls[10], "tok:;");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "unindent");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `VisitForeachStatement` writes `foreach (T x in e) body`: the `VariableType` recurses, an
// explicit `Space`, the `VariableDesignation` recurses, an explicit `Space`, the `in` keyword,
// an explicit `Space`, the `InExpression` recurses, then the closing paren and the embedded
// body. A `PrimitiveType` type and a `SingleVariableDesignation` designation each contribute their
// own `start`/.../`end` sub-trace.
TEST(CSharp_OutputVisitor, VisitForeachStatement) {
	V h;
	auto node = std::make_unique<ForeachStatement>();
	auto varType = std::make_unique<PrimitiveType>(std::string("int"));
	auto designation = std::make_unique<SingleVariableDesignation>(std::string("x"));
	auto inExpr = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->VariableType(varType.get());
	node->VariableDesignation(designation.get());
	node->InExpression(inExpr.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitForeachStatement(node.get());
	// start, kw:foreach, tok:(, start, primtype:int, end, space, start, id:x, end, space, kw:in,
	// space, start, primval, end, tok:), newline, indent, start, kw:break, tok:;, newline, end,
	// unindent, end
	ASSERT_EQ(h.inner.calls.size(), 26u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:foreach");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:x");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "space");
	EXPECT_EQ(h.inner.calls[11], "kw:in");
	EXPECT_EQ(h.inner.calls[12], "space");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "primval");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "tok:)");
	EXPECT_EQ(h.inner.calls[17], "newline");
	EXPECT_EQ(h.inner.calls[18], "indent");
	EXPECT_EQ(h.inner.calls[19], "start");
	EXPECT_EQ(h.inner.calls[20], "kw:break");
	EXPECT_EQ(h.inner.calls[21], "tok:;");
	EXPECT_EQ(h.inner.calls[22], "newline");
	EXPECT_EQ(h.inner.calls[23], "end");
	EXPECT_EQ(h.inner.calls[24], "unindent");
	EXPECT_EQ(h.inner.calls[25], "end");
}

// `VisitFixedStatement` writes `fixed (T v) body`: the `Type` recurses, an explicit `Space`, the
// comma-separated `Variables` list (a single `VariableInitializer` rendering `start`/`id:x`/
// `end`), then the closing paren and the embedded `break` body. It reuses the `using` paren
// policy fields (`SpaceBeforeUsingParentheses`/`SpacesWithinUsingParentheses`), both default
// `false`, so no surrounding spaces appear.
TEST(CSharp_OutputVisitor, VisitFixedStatement) {
	V h;
	auto node = std::make_unique<FixedStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto var = std::make_unique<VariableInitializer>(std::string("x"));
	auto body = std::make_unique<BreakStatement>();
	node->Type(type.get());
	node->Variables().Add(var.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitFixedStatement(node.get());
	// start, kw:fixed, tok:(, start, primtype:int, end, space, start, id:x, end, tok:), newline,
	// indent, start, kw:break, tok:;, newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 20u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:fixed");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:x");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:)");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "indent");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "kw:break");
	EXPECT_EQ(h.inner.calls[15], "tok:;");
	EXPECT_EQ(h.inner.calls[16], "newline");
	EXPECT_EQ(h.inner.calls[17], "end");
	EXPECT_EQ(h.inner.calls[18], "unindent");
	EXPECT_EQ(h.inner.calls[19], "end");
}

// `VisitCaseLabel` for `case 0:` -- the `case` keyword, an explicit `Space`, the case
// `Expression` recursing through `VisitPrimitiveExpression` (its own `start`/`primval`/`end`),
// then the `Colon` token. The `InsertRequiredSpacesDecorator` inserts no spurious spaces: the
// explicit `Space()` after `kw:case` resets `lastWritten` to `Whitespace`, so the primitive value
// needs no leading decorator space.
TEST(CSharp_OutputVisitor, VisitCaseLabel) {
	V h;
	auto node = std::make_unique<CaseLabel>();
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(int32_t(0)));
	node->Expression(expr.get());
	h.visitor->VisitCaseLabel(node.get());
	// start, kw:case, space, start, primval, end, tok::, end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:case");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok::");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitCaseLabel` for `default:` -- a null `Expression` selects the `DefaultKeyword` branch
// (no `case` keyword, no `Space`), then the `Colon` token. The decorator inserts no space between
// the `default` keyword and the `:` token (`:` is not one of the `+`/`-`/`&`/`?` merge-guards).
TEST(CSharp_OutputVisitor, VisitCaseLabelDefault) {
	V h;
	auto node = std::make_unique<CaseLabel>();
	h.visitor->VisitCaseLabel(node.get());
	// start, kw:default, tok::, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:default");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitSwitchSection` -- one `default:` label + one `break;` statement (not a `BlockStatement`).
// The labels loop emits the label with no leading `NewLine` (the `first` gate), the `isBlock` test
// is `false` (the single statement is a `BreakStatement`, not a `BlockStatement`), so a `NewLine`
// separates the label from the statement, then the `break` recurses through
// `VisitBreakStatement` (a `Semicolon` emitting `tok:;` + `newline`). The default
// `IndentCaseBody=false` means the `Indent`/`Unindent` around the statements are skipped.
TEST(CSharp_OutputVisitor, VisitSwitchSection) {
	V h;
	auto section = std::make_unique<SwitchSection>();
	auto label = std::make_unique<CaseLabel>();
	auto stmt = std::make_unique<BreakStatement>();
	section->CaseLabels().Add(label.get());
	section->Statements().Add(stmt.get());
	h.visitor->VisitSwitchSection(section.get());
	// start, start, kw:default, tok::, end, newline, start, kw:break, tok:;, newline, end, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "kw:default");
	EXPECT_EQ(h.inner.calls[3], "tok::");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "kw:break");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitSwitchStatement` -- a `switch (null) { default: break; }`. The `switch` keyword, the parens
// (default policy adds no surrounding spaces), the governing `Expression` recursing through
// `VisitNullReferenceExpression` (its own `start`/`primval`/`end`), then `OpenBrace` (EndOfLine
// style, default `newLine=true`) inserts a `space` before `{` (the line is not empty after `)`),
// then `{`, `indent`, `newline`; the default `IndentSwitchBody=false` `Unindent`s the body; the
// single section recurses through `VisitSwitchSection`; the `IndentSwitchBody=false` `Indent`
// restores the indent; `CloseBrace` `Unindent`s then emits `}`; a trailing `NewLine`.
TEST(CSharp_OutputVisitor, VisitSwitchStatement) {
	V h;
	auto node = std::make_unique<SwitchStatement>();
	auto governingExpr = std::make_unique<NullReferenceExpression>();
	auto section = std::make_unique<SwitchSection>();
	auto label = std::make_unique<CaseLabel>();
	auto stmt = std::make_unique<BreakStatement>();
	node->Expression(governingExpr.get());
	section->CaseLabels().Add(label.get());
	section->Statements().Add(stmt.get());
	node->SwitchSections().Add(section.get());
	h.visitor->VisitSwitchStatement(node.get());
	// start, kw:switch, tok:(, start, primval, end, tok:), space, tok:{, indent, newline, unindent,
	// start, start, kw:default, tok::, end, newline, start, kw:break, tok:;, newline, end, end,
	// indent, unindent, tok:}, newline, end
	ASSERT_EQ(h.inner.calls.size(), 29u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:switch");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "tok:{");
	EXPECT_EQ(h.inner.calls[9], "indent");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "unindent");
	EXPECT_EQ(h.inner.calls[12], "start");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "kw:default");
	EXPECT_EQ(h.inner.calls[15], "tok::");
	EXPECT_EQ(h.inner.calls[16], "end");
	EXPECT_EQ(h.inner.calls[17], "newline");
	EXPECT_EQ(h.inner.calls[18], "start");
	EXPECT_EQ(h.inner.calls[19], "kw:break");
	EXPECT_EQ(h.inner.calls[20], "tok:;");
	EXPECT_EQ(h.inner.calls[21], "newline");
	EXPECT_EQ(h.inner.calls[22], "end");
	EXPECT_EQ(h.inner.calls[23], "end");
	EXPECT_EQ(h.inner.calls[24], "indent");
	EXPECT_EQ(h.inner.calls[25], "unindent");
	EXPECT_EQ(h.inner.calls[26], "tok:}");
	EXPECT_EQ(h.inner.calls[27], "newline");
	EXPECT_EQ(h.inner.calls[28], "end");
}

// `VisitSwitchExpressionSection` -- a switch-expression arm `pattern => body`. The `Pattern`
// recurses through `VisitPrimitiveExpression` (its own `start`/`primval`/`end`), then an explicit
// `Space`, the `=>` arrow token, another explicit `Space`, and the `Body` recursing through
// `VisitPrimitiveExpression` (likewise `start`/`primval`/`end`). Both explicit `Space` calls reset
// the decorator's `lastWritten` so no decorator-inserted space precedes the arrow.
TEST(CSharp_OutputVisitor, VisitSwitchExpressionSection) {
	V h;
	auto section = std::make_unique<SwitchExpressionSection>();
	auto pattern = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto body = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	section->Pattern(pattern.get());
	section->Body(body.get());
	h.visitor->VisitSwitchExpressionSection(section.get());
	// start, start, primval, end, space, tok:=>, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:=>");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitSwitchExpression` -- `expr switch { arm => body, }`. The governing `Expression` recurses
// through `VisitPrimitiveExpression`, then an explicit `Space`, the `switch` keyword, `OpenBrace`
// (EndOfLine array-initializer style, `newLine=true`) inserts a `space` before `{` (the line is not
// empty after `switch`), then `{`, `indent`, `newline`; the single section recurses through
// `VisitSwitchExpressionSection`; `Comma` writes a trailing `tok:,` (C# writes a comma after every
// arm including the last) with no surrounding spaces (the default `SpaceBeforeBracketComma` /
// `SpaceAfterBracketComma` are both false); `NewLine`; `CloseBrace` (EndOfLine) `unindent`s then
// emits `}`.
TEST(CSharp_OutputVisitor, VisitSwitchExpression) {
	V h;
	auto node = std::make_unique<SwitchExpression>();
	auto governingExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto section = std::make_unique<SwitchExpressionSection>();
	auto pattern = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto body = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	node->Expression(governingExpr.get());
	section->Pattern(pattern.get());
	section->Body(body.get());
	node->SwitchSections().Add(section.get());
	h.visitor->VisitSwitchExpression(node.get());
	// start, start, primval, end, space, kw:switch, space, tok:{, indent, newline,
	// start, start, primval, end, space, tok:=>, space, start, primval, end, end,
	// tok:,, newline, unindent, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 26u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:switch");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "start");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "primval");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "space");
	EXPECT_EQ(h.inner.calls[15], "tok:=>");
	EXPECT_EQ(h.inner.calls[16], "space");
	EXPECT_EQ(h.inner.calls[17], "start");
	EXPECT_EQ(h.inner.calls[18], "primval");
	EXPECT_EQ(h.inner.calls[19], "end");
	EXPECT_EQ(h.inner.calls[20], "end");
	EXPECT_EQ(h.inner.calls[21], "tok:,");
	EXPECT_EQ(h.inner.calls[22], "newline");
	EXPECT_EQ(h.inner.calls[23], "unindent");
	EXPECT_EQ(h.inner.calls[24], "tok:}");
	EXPECT_EQ(h.inner.calls[25], "end");
}

// `VisitSwitchExpression` with no sections -- the `foreach` loop body never runs, so no `Comma` /
// `NewLine` per arm; `OpenBrace` and `CloseBrace` still bracket the (empty) arm list.
TEST(CSharp_OutputVisitor, VisitSwitchExpressionEmpty) {
	V h;
	auto node = std::make_unique<SwitchExpression>();
	auto governingExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	node->Expression(governingExpr.get());
	h.visitor->VisitSwitchExpression(node.get());
	// start, start, primval, end, space, kw:switch, space, tok:{, indent, newline, unindent, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:switch");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "unindent");
	EXPECT_EQ(h.inner.calls[11], "tok:}");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// ---- The try/catch family ------------------------------------------------

// `VisitCatchClause` on a bare `catch {}` (no `Type`, no `VariableName`, no `Condition`, an
// empty `Body`): `StartNode`, `WriteKeyword(catch)`, then `WriteBlock(Body)` (the `OpenBrace`
// inserts a `Space` since the line is not empty after the keyword) directly (NOT via
// `VisitBlockStatement`, so no trailing `NewLine`), then `EndNode`.
TEST(CSharp_OutputVisitor, VisitCatchClauseBare) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto clause = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, body.get());
	h.visitor->VisitCatchClause(clause.get());
	// start, kw:catch, start, space, tok:{, indent, newline, unindent, tok:}, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:catch");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitCatchClause` with a `when` filter and no caught type: `catch when (true) {}`. The
// `Condition` is a `PrimitiveExpression(true)` whose `VisitPrimitiveExpression` contributes its
// own `start`/`primval`/`end`. The `Space(SpaceBeforeIfParentheses=false)` and
// `Space(SpacesWithinIfParentheses=false)` calls are no-ops (default policy).
TEST(CSharp_OutputVisitor, VisitCatchClauseWithFilter) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(PrimitiveValue(true));
	auto clause = std::make_unique<CatchClause>(nullptr, std::string(), cond.get(), body.get());
	h.visitor->VisitCatchClause(clause.get());
	// start, kw:catch, space, kw:when, tok:(, start, primval, end, tok:),
	// start, space, tok:{, indent, newline, unindent, tok:}, end, end
	ASSERT_EQ(h.inner.calls.size(), 18u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:catch");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:when");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "space");
	EXPECT_EQ(h.inner.calls[11], "tok:{");
	EXPECT_EQ(h.inner.calls[12], "indent");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "tok:}");
	EXPECT_EQ(h.inner.calls[16], "end");
	EXPECT_EQ(h.inner.calls[17], "end");
}

// `VisitTryCatchStatement` with `try {} catch {}` (an empty `TryBlock`, one bare `CatchClause`,
// no `FinallyBlock`). The default `CatchNewLinePlacement` is `DoNotCare` (not `SameLine`), so a
// `NewLine` precedes the catch clause; the catch clause recurses through `VisitCatchClause`; a
// trailing `NewLine` follows before `EndNode`. The catch clause is added to `CatchClauses` so its
// `Parent` is the `TryCatchStatement` (the `StartNode` nesting-order assert).
TEST(CSharp_OutputVisitor, VisitTryCatchStatement) {
	V h;
	auto tryBlock = std::make_unique<BlockStatement>();
	auto tryStmt = std::make_unique<TryCatchStatement>(tryBlock.get());
	auto catchBody = std::make_unique<BlockStatement>();
	auto clause = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, catchBody.get());
	tryStmt->CatchClauses().Add(clause.get());
	h.visitor->VisitTryCatchStatement(tryStmt.get());
	// start, kw:try, start, space, tok:{, indent, newline, unindent, tok:}, end,
	// newline, start, kw:catch, start, space, tok:{, indent, newline, unindent, tok:}, end, end,
	// newline, end
	ASSERT_EQ(h.inner.calls.size(), 24u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:try");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "kw:catch");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "space");
	EXPECT_EQ(h.inner.calls[15], "tok:{");
	EXPECT_EQ(h.inner.calls[16], "indent");
	EXPECT_EQ(h.inner.calls[17], "newline");
	EXPECT_EQ(h.inner.calls[18], "unindent");
	EXPECT_EQ(h.inner.calls[19], "tok:}");
	EXPECT_EQ(h.inner.calls[20], "end");
	EXPECT_EQ(h.inner.calls[21], "end");
	EXPECT_EQ(h.inner.calls[22], "newline");
	EXPECT_EQ(h.inner.calls[23], "end");
}

// ---- VisitVariableDeclarationStatement -----------------------------------

// `VisitVariableDeclarationStatement` writes `Modifiers Type v1, v2, ...;`: the `WriteModifiers`
// (empty for `None`), the `Type` recursing (a `PrimitiveType` contributes `start`/`primtype`/
// `end`), an explicit `Space`, the comma-separated `Variables` list (a `VariableInitializer`
// contributes `start`/`id:x`/`end`), then `Semicolon` (`tok:;` + `newline`). The default
// `SpaceAroundAssignment`/`SpaceBeforeBracketComma`/`SpaceAfterBracketComma` policies are false,
// so no extra spaces appear; the `InsertRequiredSpacesDecorator` inserts none either (the
// explicit `Space()` resets `lastWritten`, so the identifier after it needs no leading space).
TEST(CSharp_OutputVisitor, VisitVariableDeclarationStatement) {
	V h;
	auto node = std::make_unique<VariableDeclarationStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto var = std::make_unique<VariableInitializer>(std::string("x"));
	node->Type(type.get());
	node->Variables().Add(var.get());
	h.visitor->VisitVariableDeclarationStatement(node.get());
	// start, start, primtype:int, end, space, start, id:x, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `int x = 0;` -- the `VariableInitializer` carries a `PrimitiveExpression` initializer: the
// `=` token is written with `SpaceAroundAssignment=false` (no surrounding spaces), and the
// `PrimitiveExpression` recurses through `VisitPrimitiveExpression` (`start`/`primval`/`end`).
TEST(CSharp_OutputVisitor, VisitVariableDeclarationStatementWithInitializer) {
	V h;
	auto node = std::make_unique<VariableDeclarationStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto init = std::make_unique<PrimitiveExpression>(int32_t(0));
	auto var = std::make_unique<VariableInitializer>(std::string("x"), init.get());
	node->Type(type.get());
	node->Variables().Add(var.get());
	h.visitor->VisitVariableDeclarationStatement(node.get());
	// start, start, primtype:int, end, space, start, id:x, tok:=, start, primval, end, end,
	// tok:;, newline, end
	ASSERT_GE(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "tok:=");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "primval");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "tok:;");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `int x, y;` -- two `VariableInitializer` elements in the `Variables` collection: the
// `WriteCommaSeparatedList` template emits a `Comma` (just `tok:,` with the default
// `SpaceBeforeBracketComma`/`SpaceAfterBracketComma` both false) between the two elements.
TEST(CSharp_OutputVisitor, VisitVariableDeclarationStatementTwoVariables) {
	V h;
	auto node = std::make_unique<VariableDeclarationStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto var1 = std::make_unique<VariableInitializer>(std::string("x"));
	auto var2 = std::make_unique<VariableInitializer>(std::string("y"));
	node->Type(type.get());
	node->Variables().Add(var1.get());
	node->Variables().Add(var2.get());
	h.visitor->VisitVariableDeclarationStatement(node.get());
	// start, start, primtype:int, end, space, start, id:x, end, tok:, start, id:y, end,
	// tok:;, newline, end
	ASSERT_GE(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:,");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "id:y");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "tok:;");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// ---- Accessor (the EntityDeclaration family start) -----------------------------------------
// The `Accessor` is the `get`/`set`/`init`/`add`/`remove` accessor of a property/indexer/event.
// `VisitAccessor` dispatches on the slot the accessor occupies in its PARENT (the `Slot()?.Kind`
// pointer compare against `Slots::Getter`/`Setter`/`AddAccessor`/`RemoveAccessor`), writing the
// keyword (`get`/`set`/`init`/`add`/`remove`) then the body via `WriteMethodBody`. A null body
// yields a `;` (the `get;`/`set;` auto-property form); the `Semicolon` helper's auto-property
// `skipNewLine` (a `get`/`set` with no body/attributes and `AutoPropertyFormatting==SingleLine`)
// replaces the trailing `NewLine` with a `Space` so the `get;` and `set;` stay on one line.

// `get;` -- a getter accessor (parented in a `PropertyDeclaration`'s `Getter` slot) with no body:
// the auto-property `skipNewLine` replaces the post-semicolon `NewLine` with a `Space`.
TEST(CSharp_OutputVisitor, VisitAccessorGet) {
	V h;
	auto prop = std::make_unique<PropertyDeclaration>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto name = MakeId("X");
	prop->ReturnType(type.get());
	prop->NameToken(name.get());
	auto getter = std::make_unique<Accessor>(AccessorKind::Getter);
	prop->Getter(getter.get());
	h.visitor->VisitAccessor(getter.get());
	// start, kw:get, tok:;, space, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:get");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `set;` -- a setter accessor (parented in a `PropertyDeclaration`'s `Setter` slot, `Kind=Setter`)
// with no body: the auto-property `skipNewLine` replaces the post-semicolon `NewLine` with a
// `Space` (the `set` keyword is emitted, NOT `init`, since `Kind != Init`).
TEST(CSharp_OutputVisitor, VisitAccessorSet) {
	V h;
	auto prop = std::make_unique<PropertyDeclaration>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto name = MakeId("X");
	prop->ReturnType(type.get());
	prop->NameToken(name.get());
	auto setter = std::make_unique<Accessor>(AccessorKind::Setter);
	prop->Setter(setter.get());
	h.visitor->VisitAccessor(setter.get());
	// start, kw:set, tok:;, space, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:set");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `init;` -- an init-only setter accessor (parented in a `PropertyDeclaration`'s `Setter` slot,
// `Kind=Init`): the `init` keyword is emitted (the `Kind == Init` branch), and the auto-property
// `skipNewLine` replaces the post-semicolon `NewLine` with a `Space`.
TEST(CSharp_OutputVisitor, VisitAccessorInit) {
	V h;
	auto prop = std::make_unique<PropertyDeclaration>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto name = MakeId("X");
	prop->ReturnType(type.get());
	prop->NameToken(name.get());
	auto init = std::make_unique<Accessor>(AccessorKind::Init);
	prop->Setter(init.get());
	h.visitor->VisitAccessor(init.get());
	// start, kw:init, tok:;, space, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:init");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `get { }` -- a getter accessor with an empty `BlockStatement` body: `WriteMethodBody` calls
// `WriteBlock` (the block's `StartNode` is recorded BEFORE `OpenBrace`; the `OpenBrace(EndOfLine)`
// inserts a `Space` before `{` since the line is not empty after `get`, then `Indent`+`NewLine`;
// `CloseBrace` does `Unindent`+`}`) then a trailing `NewLine` -- distinct from the auto-property
// `get;` form (the D329 `VisitCheckedStatement` 12-entry sequence precedent).
TEST(CSharp_OutputVisitor, VisitAccessorGetWithBody) {
	V h;
	auto prop = std::make_unique<PropertyDeclaration>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto name = MakeId("X");
	prop->ReturnType(type.get());
	prop->NameToken(name.get());
	auto body = std::make_unique<BlockStatement>();
	auto getter = std::make_unique<Accessor>(AccessorKind::Getter);
	getter->Body(body.get());
	prop->Getter(getter.get());
	h.visitor->VisitAccessor(getter.get());
	// start, kw:get, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:get");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `add;` -- an `add` accessor (parented in a `CustomEventDeclaration`'s `AddAccessor` slot) with
// no body: the `Semicolon` `skipNewLine` applies ONLY to `Slots::Getter`/`Slots::Setter` (NOT
// `AddAccessor`), so a `NewLine` (NOT a `Space`) follows the semicolon.
TEST(CSharp_OutputVisitor, VisitAccessorAdd) {
	V h;
	auto evt = std::make_unique<CustomEventDeclaration>();
	auto type = std::make_unique<PrimitiveType>(std::string("EventHandler"));
	auto name = MakeId("E");
	evt->ReturnType(type.get());
	evt->NameToken(name.get());
	auto add = std::make_unique<Accessor>(AccessorKind::Adder);
	evt->AddAccessor(add.get());
	h.visitor->VisitAccessor(add.get());
	// start, kw:add, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:add");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitConstructorInitializer` over `: base()` -- the `Base` initializer with no arguments.
// `StartNode`, `WriteToken(:)`, `Space()`, `WriteKeyword(base)`, then
// `WriteCommaSeparatedListInParenthesis([], SpaceWithinMethodCallParentheses=false)` writes
// `(` `)` (empty list: no inner spaces, no comma), then `EndNode`. The `Space(SpaceBeforeMethod-
// CallParentheses=false)` before the parens is a no-op (default policy).
TEST(CSharp_OutputVisitor, VisitConstructorInitializerBaseNoArgs) {
	V h;
	auto node = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::Base);
	h.visitor->VisitConstructorInitializer(node.get());
	// start, tok::, space, kw:base, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok::");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:base");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitConstructorInitializer` over `: this(1)` -- the `This` initializer with one argument.
// The argument recurses through `VisitPrimitiveExpression` between the parentheses (no comma
// for a single-element list; the default policy's `SpaceWithinMethodCallParentheses=false` so
// no inner spaces).
TEST(CSharp_OutputVisitor, VisitConstructorInitializerThisOneArg) {
	V h;
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::This);
	node->Arguments().Add(arg.get());
	h.visitor->VisitConstructorInitializer(node.get());
	// start, tok::, space, kw:this, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok::");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:this");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitConstructorDeclaration` over `Foo() {}` -- a parentless ctor (no Attributes, no
// Modifiers, NameToken `Foo`, no Parameters, no Initializer, an empty Body). WriteAttributes is
// a no-op (empty), WriteModifiers(0) is a no-op, the `Parent as TypeDeclaration` is null so the
// `else` writes NameToken, Space(false) before the parens is a no-op, the empty Parameters list
// writes `(` `)`, no Initializer, then WriteMethodBody(Body) writes the block braces + a newline.
TEST(CSharp_OutputVisitor, VisitConstructorDeclarationBare) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<ConstructorDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	node->Body(body.get());
	h.visitor->VisitConstructorDeclaration(node.get());
	// The port WriteIdentifier is a direct writer call (id:Foo only, NOT the C# recursive
	// start/VisitIdentifier/end -- the D334/D338 convention); OpenBrace inserts a space before
	// { since the line carries "Foo()".
	// start, id:Foo, tok:(, tok:), start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_EQ(h.inner.calls.size(), 14u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:)");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "tok:{");
	EXPECT_EQ(h.inner.calls[7], "indent");
	EXPECT_EQ(h.inner.calls[8], "newline");
	EXPECT_EQ(h.inner.calls[9], "unindent");
	EXPECT_EQ(h.inner.calls[10], "tok:}");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
}

// `VisitConstructorDeclaration` over `Foo(int x)` -- one parameter recurses through
// VisitParameterDeclaration between the parens (no comma for a single element). The parameter
// has a null Type and null NameToken, so VisitParameterDeclaration drives only its own
// start/end (no inner tokens); the parens still bracket it, and the body braces follow.
TEST(CSharp_OutputVisitor, VisitConstructorDeclarationOneParam) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto param = std::make_unique<ParameterDeclaration>();
	auto node = std::make_unique<ConstructorDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	node->Parameters().Add(param.get());
	node->Body(body.get());
	h.visitor->VisitConstructorDeclaration(node.get());
	ASSERT_GE(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	// the parameter recursion (VisitParameterDeclaration) sits at index 3+; assert the parens
	// bracket it and the body braces follow the close paren.
	auto rpar = std::find(h.inner.calls.begin(), h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "the parameter list must close with tok:)";
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end()) << "the body block must open with tok:{";
}

// `VisitConstructorDeclaration` over `Foo() : base(1)` -- an Initializer (the D344
// VisitConstructorInitializer) recurses after the parens, indented: NewLine + writer.Indent +
// VisitConstructorInitializer + writer.Unindent, then the body. The initializer `: base(1)`
// drives tok:: / space / kw:base / tok:( / <primval> / tok:).
TEST(CSharp_OutputVisitor, VisitConstructorDeclarationWithInitializer) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto init = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::Base);
	init->Arguments().Add(arg.get());
	auto node = std::make_unique<ConstructorDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	node->Initializer(init.get());
	node->Body(body.get());
	h.visitor->VisitConstructorDeclaration(node.get());
	// start, id:Foo, tok:(, tok:), newline, indent,
	// <VisitConstructorInitializer>: start, tok::, space, kw:base, tok:(, start, primval, end, tok:), end,
	// unindent, <body: start, space, tok:{, indent, newline, unindent, tok:}, end>, newline, end
	ASSERT_EQ(h.inner.calls.size(), 27u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:)");
	EXPECT_EQ(h.inner.calls[4], "newline");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "tok::");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "kw:base");
	EXPECT_EQ(h.inner.calls[10], "tok:(");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "primval");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "tok:)");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "unindent");
	EXPECT_EQ(h.inner.calls[17], "start");
	EXPECT_EQ(h.inner.calls[18], "space");
	EXPECT_EQ(h.inner.calls[19], "tok:{");
	EXPECT_EQ(h.inner.calls[20], "indent");
	EXPECT_EQ(h.inner.calls[21], "newline");
	EXPECT_EQ(h.inner.calls[22], "unindent");
	EXPECT_EQ(h.inner.calls[23], "tok:}");
	EXPECT_EQ(h.inner.calls[24], "end");
	EXPECT_EQ(h.inner.calls[25], "newline");
	EXPECT_EQ(h.inner.calls[26], "end");
}

// `VisitDestructorDeclaration` over `~Foo() {}` -- a parentless destructor (no Attributes,
// no Modifiers, NameToken `Foo`, an empty Body). WriteAttributes is a no-op (empty),
// WriteModifiers(None) is a no-op, the `if (Modifiers != None) Space()` is skipped, then
// WriteToken(~) + the `Parent as TypeDeclaration` is null so the `else` writes NameToken,
// Space(false) before the parens is a no-op, LPar + RPar, then WriteMethodBody(Body) writes
// the block braces + a trailing newline. The port WriteIdentifier is the direct writer call
// (id:Foo only, the D345 convention).
TEST(CSharp_OutputVisitor, VisitDestructorDeclarationBare) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<DestructorDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	node->Body(body.get());
	h.visitor->VisitDestructorDeclaration(node.get());
	// start, tok:~, id:Foo, tok:(, tok:), start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_GE(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:~");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "tok:(");
	EXPECT_EQ(h.inner.calls[4], "tok:)");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "unindent");
	EXPECT_EQ(h.inner.calls[11], "tok:}");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `VisitDestructorDeclaration` with a modifier set (e.g. `static ~Foo() {}` -- not valid C#,
// but the node allows it). WriteModifiers writes the keyword + a trailing Space, then the
// `if (Modifiers != None) Space()` is a second Space -- a no-op since the port Space() is
// idempotent (isAfterSpace_ is true after WriteModifiers trailing space), so only ONE space
// precedes the `~`. Then tok:~ / id:Foo / tok:( / tok:) / the body.
TEST(CSharp_OutputVisitor, VisitDestructorDeclarationWithModifier) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<DestructorDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	node->Body(body.get());
	node->Modifiers(Modifiers::Static);
	h.visitor->VisitDestructorDeclaration(node.get());
	ASSERT_GE(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:static");
	// WriteModifiers writes a trailing space after the keyword; the explicit Space() is a
	// no-op (isAfterSpace_), so the next token is the tilde.
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:~");
	EXPECT_EQ(h.inner.calls[4], "id:Foo");
	EXPECT_EQ(h.inner.calls[5], "tok:(");
}

// `VisitEnumMemberDeclaration` over `Foo` (no initializer): StartNode + WriteAttributes (no-op,
// empty) + WriteModifiers (no-op, None) + WriteIdentifier(NameToken) + EndNode. The port
// WriteIdentifier is the direct writer call (id:Foo only, the D345 convention).
TEST(CSharp_OutputVisitor, VisitEnumMemberDeclarationBare) {
	V h;
	auto node = std::make_unique<EnumMemberDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	h.visitor->VisitEnumMemberDeclaration(node.get());
	// start, id:Foo, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitEnumMemberDeclaration` over `Foo = 1` -- an Initializer (a PrimitiveExpression).
// Space(SpaceAroundAssignment=false) is a no-op (default policy), WriteToken(Assign "="),
// Space(false) is a no-op, then the Initializer recurses through VisitPrimitiveExpression
// (start/primval/end).
TEST(CSharp_OutputVisitor, VisitEnumMemberDeclarationWithInitializer) {
	V h;
	auto init = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<EnumMemberDeclaration>();
	node->NameToken(Identifier::Create("Foo"));
	node->Initializer(init.get());
	h.visitor->VisitEnumMemberDeclaration(node.get());
	// start, id:Foo, tok:=, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "tok:=");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitExtensionDeclaration` over a bare `extension () { }` (no Attributes, no Modifiers, no
// TypeParameters, empty ReceiverParameters, no Constraints, no Members). WriteAttributes and
// WriteModifiers are no-ops (None), WriteKeyword("extension"), WriteTypeParameters (empty) is a
// no-op, Space(false) is a no-op, the empty ReceiverParameters writes `(` `)`, the Constraints
// loop is a no-op, OpenBrace(ClassBraceStyle=EndOfLine) inserts a space (not at start of line after
// `extension ()`) + `{` + indent + newline, the Members loop is a no-op, CloseBrace writes unindent
// + `}`, then NewLine, EndNode.
TEST(CSharp_OutputVisitor, VisitExtensionDeclarationBare) {
	V h;
	auto node = std::make_unique<ExtensionDeclaration>();
	h.visitor->VisitExtensionDeclaration(node.get());
	// start, kw:extension, tok:(, tok:), space, tok:{, indent, newline, unindent, tok:}, newline, end
	ASSERT_GE(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:extension");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:)");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:{");
	EXPECT_EQ(h.inner.calls[6], "indent");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "unindent");
	EXPECT_EQ(h.inner.calls[9], "tok:}");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitEventDeclaration` over `event SomeHandler Foo;` -- a field-like event: StartNode +
// WriteAttributes (no-op) + WriteModifiers (no-op) + WriteKeyword(event) + ReturnType->AcceptVisitor
// (a PrimitiveType "SomeHandler" recurses through VisitPrimitiveType: start/primtype/end) + Space() +
// WriteCommaSeparatedList(Variables) (one VariableInitializer "Foo" recurses: start/id:Foo/end) +
// Semicolon + EndNode. The WriteCommaSeparatedList template iterates the ToVector snapshot with a
// Comma between elements (none for a single element).

// `VisitEventDeclaration` over `event SomeHandler Foo;` -- a field-like event: StartNode +
// WriteAttributes (no-op) + WriteModifiers (no-op) + WriteKeyword(event) + ReturnType->AcceptVisitor
// (a PrimitiveType "SomeHandler" recurses: start/space/primtype:SomeHandler/end -- the space is the
// InsertRequiredSpacesDecorator auto-space between "event" and "SomeHandler") + Space() (before the
// Variables) + WriteCommaSeparatedList(Variables) (one VariableInitializer "Foo": start/id:Foo/end) +
// Semicolon + EndNode. The WriteCommaSeparatedList template iterates the ToVector snapshot with a
// Comma between elements (none for a single element).
TEST(CSharp_OutputVisitor, VisitEventDeclarationOneVar) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("SomeHandler"));
	auto var = std::make_unique<VariableInitializer>(std::string("Foo"));
	auto node = std::make_unique<EventDeclaration>();
	node->ReturnType(retType.get());
	node->Variables().Add(var.get());
	h.visitor->VisitEventDeclaration(node.get());
	// start, kw:event, start, space, primtype:SomeHandler, end, space, start, id:Foo, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:event");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:SomeHandler");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:Foo");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:;");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// `VisitEventDeclaration` over `event SomeHandler Foo, Bar;` -- two variables, so the
// WriteCommaSeparatedList writes a Comma between them.
TEST(CSharp_OutputVisitor, VisitEventDeclarationTwoVars) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("SomeHandler"));
	auto v0 = std::make_unique<VariableInitializer>(std::string("Foo"));
	auto v1 = std::make_unique<VariableInitializer>(std::string("Bar"));
	auto node = std::make_unique<EventDeclaration>();
	node->ReturnType(retType.get());
	node->Variables().Add(v0.get());
	node->Variables().Add(v1.get());
	h.visitor->VisitEventDeclaration(node.get());
	// start, kw:event, start, space, primtype:SomeHandler, end, space,
	// start, id:Foo, end, tok:, start, id:Bar, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:event");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:SomeHandler");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:Foo");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:,");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "id:Bar");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "tok:;");
	EXPECT_EQ(h.inner.calls[15], "newline");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitCustomEventDeclaration` over `event SomeHandler Foo { }` -- a custom event with no
// add/remove accessors (an empty body). StartNode + WriteAttributes (no-op) + WriteModifiers
// (no-op) + WriteKeyword(event) + ReturnType->AcceptVisitor (PrimitiveType "SomeHandler":
// start/space/primtype:SomeHandler/end) + Space() + WritePrivateImplementationType(nullptr)
// (no-op) + WriteIdentifier(NameToken "Foo") + OpenBrace(EventBraceStyle=EndOfLine: space/tok:{/
// indent/newline) + the add/remove loop (no accessors, no-op) + CloseBrace (unindent/tok:}) +
// NewLine + EndNode.
TEST(CSharp_OutputVisitor, VisitCustomEventDeclarationBare) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("SomeHandler"));
	auto node = std::make_unique<CustomEventDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	h.visitor->VisitCustomEventDeclaration(node.get());
	// start, kw:event, start, space, primtype:SomeHandler, end, space, id:Foo,
	// space, tok:{, indent, newline, unindent, tok:}, newline, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:event");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:SomeHandler");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "id:Foo");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "tok:{");
	EXPECT_EQ(h.inner.calls[10], "indent");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "unindent");
	EXPECT_EQ(h.inner.calls[13], "tok:}");
	EXPECT_EQ(h.inner.calls[14], "newline");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitCustomEventDeclaration` over `event SomeHandler Foo { add; remove; }` -- with add and
// remove accessors. The FirstChild/NextSibling loop finds the AddAccessor and RemoveAccessor
// slot children and recurses through VisitAccessor (each accessor with an empty Body renders
// start/kw:add|remove/start/space/tok:{/indent/newline/unindent/tok:}/end).
TEST(CSharp_OutputVisitor, VisitCustomEventDeclarationWithAccessors) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("SomeHandler"));
	auto addBody = std::make_unique<BlockStatement>();
	auto removeBody = std::make_unique<BlockStatement>();
	auto add = std::make_unique<Accessor>(AccessorKind::Adder);
	auto remove = std::make_unique<Accessor>(AccessorKind::Remover);
	add->Body(addBody.get());
	remove->Body(removeBody.get());
	auto node = std::make_unique<CustomEventDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	node->AddAccessor(add.get());
	node->RemoveAccessor(remove.get());
	h.visitor->VisitCustomEventDeclaration(node.get());
	// The add/remove accessors sit between the OpenBrace and CloseBrace. Find the opening
	// brace, then assert the add keyword appears before the remove keyword (source order).
	auto lbrace = std::find(h.inner.calls.begin(), h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end()) << "the event body must open with tok:{";
	auto addKw = std::find(lbrace, h.inner.calls.end(), "kw:add");
	ASSERT_NE(addKw, h.inner.calls.end()) << "the add accessor must render kw:add";
	auto removeKw = std::find(addKw, h.inner.calls.end(), "kw:remove");
	ASSERT_NE(removeKw, h.inner.calls.end()) << "the remove accessor must render kw:remove after add";
}

// `VisitFieldDeclaration` over `int x;` -- a single field. Structurally identical to
// VisitEventDeclaration (D349) minus the `event` keyword: StartNode + WriteAttributes (no-op) +
// WriteModifiers (no-op) + ReturnType->AcceptVisitor (PrimitiveType "int": start/primtype:int/end)
// + Space() + WriteCommaSeparatedList(Variables) (one VariableInitializer "x": start/id:x/end) +
// Semicolon + EndNode. The ReturnType is the first token (no preceding keyword), so no
// auto-space before it.
TEST(CSharp_OutputVisitor, VisitFieldDeclarationOneVar) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto var = std::make_unique<VariableInitializer>(std::string("x"));
	auto node = std::make_unique<FieldDeclaration>();
	node->ReturnType(retType.get());
	node->Variables().Add(var.get());
	h.visitor->VisitFieldDeclaration(node.get());
	// start, start, primtype:int, end, space, start, id:x, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitFieldDeclaration` over `int x, y;` -- two fields, so the WriteCommaSeparatedList
// writes a Comma between them.
TEST(CSharp_OutputVisitor, VisitFieldDeclarationTwoVars) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto v0 = std::make_unique<VariableInitializer>(std::string("x"));
	auto v1 = std::make_unique<VariableInitializer>(std::string("y"));
	auto node = std::make_unique<FieldDeclaration>();
	node->ReturnType(retType.get());
	node->Variables().Add(v0.get());
	node->Variables().Add(v1.get());
	h.visitor->VisitFieldDeclaration(node.get());
	// start, start, primtype:int, end, space, start, id:x, end, tok:, start, id:y, end, tok:;, newline, end
	ASSERT_GE(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:,");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "id:y");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitFixedVariableInitializer` over `ptr` (no count expression): StartNode +
// WriteIdentifier(NameToken) + EndNode (structurally like VisitVariableInitializer
// [D334] but on the FixedVariableInitializer node).
TEST(CSharp_OutputVisitor, VisitFixedVariableInitializerNoCount) {
	V h;
	auto node = std::make_unique<FixedVariableInitializer>();
	node->NameToken(Identifier::Create("ptr"));
	h.visitor->VisitFixedVariableInitializer(node.get());
	// start, id:ptr, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:ptr");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitFixedVariableInitializer` over `ptr[10]` -- with a CountExpression (a
// PrimitiveExpression(10)). WriteToken(LBracket) + Space(false) + CountExpression->AcceptVisitor
// + Space(false) + WriteToken(RBracket).
TEST(CSharp_OutputVisitor, VisitFixedVariableInitializerWithCount) {
	V h;
	auto count = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(10)));
	auto node = std::make_unique<FixedVariableInitializer>();
	node->NameToken(Identifier::Create("ptr"));
	node->CountExpression(count.get());
	h.visitor->VisitFixedVariableInitializer(node.get());
	// start, id:ptr, tok:[, start, primval, end, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:ptr");
	EXPECT_EQ(h.inner.calls[2], "tok:[");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:]");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitFixedFieldDeclaration` over `fixed int* ptr;` -- a fixed field: StartNode +
// WriteAttributes (no-op) + WriteModifiers (no-op) + WriteKeyword(fixed) + Space() +
// ReturnType->AcceptVisitor (PrimitiveType "int*") + Space() + WriteCommaSeparatedList(Variables)
// (one FixedVariableInitializer "ptr": start/id:ptr/end) + Semicolon + EndNode.
TEST(CSharp_OutputVisitor, VisitFixedFieldDeclarationBare) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int*"));
	auto var = std::make_unique<FixedVariableInitializer>();
	var->NameToken(Identifier::Create("ptr"));
	auto node = std::make_unique<FixedFieldDeclaration>();
	node->ReturnType(retType.get());
	node->Variables().Add(var.get());
	h.visitor->VisitFixedFieldDeclaration(node.get());
	// start, kw:fixed, space, start, space, primtype:int*, end, space, start, id:ptr, end, tok:;, newline, end
	ASSERT_GE(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:fixed");
	EXPECT_EQ(h.inner.calls[2], "space");
	// the ReturnType recurses here (PrimitiveType: start/space/primtype:int*/end)
	auto primtype = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:int*");
	ASSERT_NE(primtype, h.inner.calls.end()) << "the ReturnType must render primtype:int*";
	// the fixed variable initializer renders id:ptr
	auto idPtr = std::find(h.inner.calls.begin(), h.inner.calls.end(), "id:ptr");
	ASSERT_NE(idPtr, h.inner.calls.end()) << "the variable must render id:ptr";
	EXPECT_LT(primtype, idPtr) << "the type must precede the variable";
}

// `VisitIndexerDeclaration` over `int this[string x] { get; set; }` -- an auto-property indexer with
// a getter and setter (no bodies, no attributes, so `isSingleLine` is true under the default
// `AutoPropertyFormatting == SingleLine` policy). The `string` parameter type is chosen distinct
// from the `int` return type so the find-based ordering assertions can pin each unambiguously.
// Renders: StartNode + WriteAttributes (no-op) + WriteModifiers (no-op) + ReturnType->AcceptVisitor
// (PrimitiveType "int") + Space() + WritePrivateImplementationType(nullptr) (no-op) +
// WriteKeyword("this") + Space(SpaceBeforeMethodDeclarationParentheses=false, no-op) +
// WriteCommaSeparatedListInBrackets (the one ParameterDeclaration "string x") + OpenBrace (EndOfLine,
// newLine=false) + isSingleLine Space() + the FirstChild/NextSibling walk recursing through
// VisitAccessor for the Getter (the auto-property Semicolon emits tok:; + Space, not a NewLine) and
// the Setter + CloseBrace (EndOfLine, unindent=false) + NewLine + EndNode.
TEST(CSharp_OutputVisitor, VisitIndexerDeclarationAutoPropertyGetSet) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto paramType = std::make_unique<PrimitiveType>(std::string("string"));
	auto param = std::make_unique<ParameterDeclaration>();
	param->Type(paramType.get());
	param->NameToken(Identifier::Create("x"));
	auto getter = std::make_unique<Accessor>(AccessorKind::Getter);
	auto setter = std::make_unique<Accessor>(AccessorKind::Setter);
	auto node = std::make_unique<IndexerDeclaration>();
	node->ReturnType(retType.get());
	node->Parameters().Add(param.get());
	node->Getter(getter.get());
	node->Setter(setter.get());
	h.visitor->VisitIndexerDeclaration(node.get());

	// Ordering of the key tokens (robust to the InsertRequiredSpacesDecorator's inter-token
	// spaces): ReturnType < this < [ < param type < param name < ] < { < get < set < } < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the ReturnType must render primtype:int";
	auto thisKw = std::find(retTypeTok, h.inner.calls.end(), "kw:this");
	ASSERT_NE(thisKw, h.inner.calls.end()) << "kw:this must render after the ReturnType";
	auto lbracket = std::find(thisKw, h.inner.calls.end(), "tok:[");
	ASSERT_NE(lbracket, h.inner.calls.end()) << "tok:[ must render after kw:this";
	auto paramTypeTok = std::find(lbracket, h.inner.calls.end(), "primtype:string");
	ASSERT_NE(paramTypeTok, h.inner.calls.end()) << "the parameter type must render after tok:[";
	auto paramName = std::find(paramTypeTok, h.inner.calls.end(), "id:x");
	ASSERT_NE(paramName, h.inner.calls.end()) << "the parameter name must render after its type";
	auto rbracket = std::find(paramName, h.inner.calls.end(), "tok:]");
	ASSERT_NE(rbracket, h.inner.calls.end()) << "tok:] must render after the parameter name";
	auto lbrace = std::find(rbracket, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end()) << "the indexer body must open with tok:{ after tok:]";
	auto getKw = std::find(lbrace, h.inner.calls.end(), "kw:get");
	ASSERT_NE(getKw, h.inner.calls.end()) << "the get accessor must render kw:get after tok:{";
	auto setKw = std::find(getKw, h.inner.calls.end(), "kw:set");
	ASSERT_NE(setKw, h.inner.calls.end()) << "the set accessor must render kw:set after kw:get";
	auto rbrace = std::find(setKw, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end()) << "the body must close with tok:} after kw:set";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitIndexerDeclaration` over `int this[string x] => 1` -- an expression-bodied indexer (the
// ExpressionBody is non-null, so the else branch runs). The `string` parameter type is chosen
// distinct from the `int` return type and the body is a `PrimitiveExpression(1)` (rendering
// `primval`) so the find-based ordering assertions can pin each unambiguously. Renders: StartNode +
// WriteAttributes (no-op) + WriteModifiers (no-op) + ReturnType->AcceptVisitor (PrimitiveType "int")
// + Space() + WritePrivateImplementationType(nullptr) (no-op) + WriteKeyword("this") +
// Space(SpaceBeforeMethodDeclarationParentheses=false, no-op) + WriteCommaSeparatedListInBrackets
// (the one ParameterDeclaration "string x") + the else branch: Space() + WriteToken(Tokens::Arrow
// "=>") + Space() + ExpressionBody->AcceptVisitor (PrimitiveExpression(1): primval) + Semicolon
// (tok:;/newline) + EndNode.
TEST(CSharp_OutputVisitor, VisitIndexerDeclarationExpressionBody) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto paramType = std::make_unique<PrimitiveType>(std::string("string"));
	auto param = std::make_unique<ParameterDeclaration>();
	param->Type(paramType.get());
	param->NameToken(Identifier::Create("x"));
	auto body = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<IndexerDeclaration>();
	node->ReturnType(retType.get());
	node->Parameters().Add(param.get());
	node->ExpressionBody(body.get());
	h.visitor->VisitIndexerDeclaration(node.get());

	// Ordering of the key tokens (robust to the InsertRequiredSpacesDecorator's inter-token
	// spaces): ReturnType < this < [ < param type < param name < ] < => < body < ; < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the ReturnType must render primtype:int";
	auto thisKw = std::find(retTypeTok, h.inner.calls.end(), "kw:this");
	ASSERT_NE(thisKw, h.inner.calls.end()) << "kw:this must render after the ReturnType";
	auto lbracket = std::find(thisKw, h.inner.calls.end(), "tok:[");
	ASSERT_NE(lbracket, h.inner.calls.end()) << "tok:[ must render after kw:this";
	auto paramTypeTok = std::find(lbracket, h.inner.calls.end(), "primtype:string");
	ASSERT_NE(paramTypeTok, h.inner.calls.end()) << "the parameter type must render after tok:[";
	auto paramName = std::find(paramTypeTok, h.inner.calls.end(), "id:x");
	ASSERT_NE(paramName, h.inner.calls.end()) << "the parameter name must render after its type";
	auto rbracket = std::find(paramName, h.inner.calls.end(), "tok:]");
	ASSERT_NE(rbracket, h.inner.calls.end()) << "tok:] must render after the parameter name";
	auto arrow = std::find(rbracket, h.inner.calls.end(), "tok:=>");
	ASSERT_NE(arrow, h.inner.calls.end()) << "the expression body must render tok:=> after tok:]";
	auto bodyVal = std::find(arrow, h.inner.calls.end(), "primval");
	ASSERT_NE(bodyVal, h.inner.calls.end()) << "the expression body must render primval after tok:=>";
	auto semi = std::find(bodyVal, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the expression-bodied indexer must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitMethodDeclaration` over `void Foo() {}` -- the bare method: a `void` ReturnType, a `Foo`
// NameToken, an empty parameter list, and an empty Body block. Renders: StartNode + WriteAttributes
// (no-op) + WriteModifiers (no-op) + ReturnType->AcceptVisitor (PrimitiveType "void") + Space() +
// WritePrivateImplementationType(nullptr) (no-op) + WriteIdentifier("Foo") + WriteTypeParameters
// (empty, no-op) + Space(SpaceBeforeMethodDeclarationParentheses=false, no-op) +
// WriteCommaSeparatedListInParenthesis (empty: tok:( / tok:)) + the empty Constraints loop +
// WriteMethodBody(Body) (the block braces + a trailing newline) + EndNode. The ReturnType is
// `void` and the param-less `Foo()` shape, so the find-based ordering pins each unambiguously.
TEST(CSharp_OutputVisitor, VisitMethodDeclarationBare) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("void"));
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<MethodDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	node->Body(body.get());
	h.visitor->VisitMethodDeclaration(node.get());

	// Ordering of the key tokens: ReturnType < NameToken < ( < ) < { < } < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:void");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the ReturnType must render primtype:void";
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end()) << "the NameToken must render after the ReturnType";
	auto lpar = std::find(nameTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end()) << "the parameter list must open with tok:( after the name";
	auto rpar = std::find(lpar, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "the parameter list must close with tok:)";
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end()) << "the body must open with tok:{ after the close paren";
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end()) << "the body must close with tok:} after tok:{";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitMethodDeclaration` over `void Foo(int x) {}` -- one parameter recurses through
// VisitParameterDeclaration between the parens. The parameter type (`int`) and name (`x`) are
// distinct from the `void` ReturnType so the find-based ordering pins each unambiguously.
TEST(CSharp_OutputVisitor, VisitMethodDeclarationOneParam) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("void"));
	auto paramType = std::make_unique<PrimitiveType>(std::string("int"));
	auto param = std::make_unique<ParameterDeclaration>();
	param->Type(paramType.get());
	param->NameToken(Identifier::Create("x"));
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<MethodDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	node->Parameters().Add(param.get());
	node->Body(body.get());
	h.visitor->VisitMethodDeclaration(node.get());

	// Ordering: ReturnType < NameToken < ( < param type < param name < ) < { < } < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:void");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end());
	auto lpar = std::find(nameTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto paramTypeTok = std::find(lpar, h.inner.calls.end(), "primtype:int");
	ASSERT_NE(paramTypeTok, h.inner.calls.end()) << "the parameter type must render after tok:(";
	auto paramName = std::find(paramTypeTok, h.inner.calls.end(), "id:x");
	ASSERT_NE(paramName, h.inner.calls.end()) << "the parameter name must render after its type";
	auto rpar = std::find(paramName, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "tok:) must render after the parameter name";
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end());
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end());
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitMethodDeclaration` over `void Foo();` -- a bodyless method (an abstract/interface/extern
// method declaration). WriteMethodBody(nullptr) writes a Semicolon (tok:;/newline), so the
// signature closes with `;` instead of a block.
TEST(CSharp_OutputVisitor, VisitMethodDeclarationNoBody) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("void"));
	auto node = std::make_unique<MethodDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	h.visitor->VisitMethodDeclaration(node.get());

	// Ordering: ReturnType < NameToken < ( < ) < ; < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:void");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end());
	auto lpar = std::find(nameTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto rpar = std::find(lpar, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end());
	auto semi = std::find(rpar, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "a bodyless method must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitOperatorDeclaration` over `long operator +(string a, int b) {}` -- a binary operator
// (OperatorType::Addition): the non-conversion shape writes the ReturnType first, then
// `operator +` (the operator keyword + GetToken's `+`), then the parameter list, then the body.
// The `long` ReturnType and `string`/`int` param types are all distinct so the find-based ordering
// pins each unambiguously, and crucially verifies the ReturnType renders BEFORE `operator` (the
// non-conversion shape) -- the discriminator vs the conversion operators below.
TEST(CSharp_OutputVisitor, VisitOperatorDeclarationAddition) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("long"));
	auto paramTypeA = std::make_unique<PrimitiveType>(std::string("string"));
	auto paramA = std::make_unique<ParameterDeclaration>();
	paramA->Type(paramTypeA.get());
	paramA->NameToken(Identifier::Create("a"));
	auto paramTypeB = std::make_unique<PrimitiveType>(std::string("int"));
	auto paramB = std::make_unique<ParameterDeclaration>();
	paramB->Type(paramTypeB.get());
	paramB->NameToken(Identifier::Create("b"));
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<OperatorDeclaration>();
	node->ReturnType(retType.get());
	node->OperatorType(OperatorType::Addition);
	node->Parameters().Add(paramA.get());
	node->Parameters().Add(paramB.get());
	node->Body(body.get());
	h.visitor->VisitOperatorDeclaration(node.get());

	// Ordering: ReturnType < kw:operator < tok:+ < ( < param a type < param a name < ,
	// < param b type < param b name < ) < { < } < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:long");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the ReturnType must render primtype:long";
	auto opKw = std::find(retTypeTok, h.inner.calls.end(), "kw:operator");
	ASSERT_NE(opKw, h.inner.calls.end()) << "kw:operator must render AFTER the ReturnType (non-conversion shape)";
	auto plusTok = std::find(opKw, h.inner.calls.end(), "tok:+");
	ASSERT_NE(plusTok, h.inner.calls.end()) << "the operator token tok:+ must render after kw:operator";
	auto lpar = std::find(plusTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto paramAType = std::find(lpar, h.inner.calls.end(), "primtype:string");
	ASSERT_NE(paramAType, h.inner.calls.end());
	auto paramAName = std::find(paramAType, h.inner.calls.end(), "id:a");
	ASSERT_NE(paramAName, h.inner.calls.end());
	auto comma = std::find(paramAName, h.inner.calls.end(), "tok:,");
	ASSERT_NE(comma, h.inner.calls.end()) << "the comma must render between the two parameters";
	auto paramBType = std::find(comma, h.inner.calls.end(), "primtype:int");
	ASSERT_NE(paramBType, h.inner.calls.end());
	auto paramBName = std::find(paramBType, h.inner.calls.end(), "id:b");
	ASSERT_NE(paramBName, h.inner.calls.end());
	auto rpar = std::find(paramBName, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end());
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end());
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end());
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitOperatorDeclaration` over `implicit operator int(string value) {}` -- a conversion
// operator (OperatorType::Implicit): the conversion shape writes the `implicit` keyword (NOT the
// ReturnType) before `operator`, then the ReturnType AFTER `operator` (the conversion target type).
// IsChecked(Implicit) is false, so no `checked` keyword. The `int` ReturnType and `string` param
// type are distinct so the find-based ordering pins the ReturnType-after-operator shape.
TEST(CSharp_OutputVisitor, VisitOperatorDeclarationImplicit) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto paramType = std::make_unique<PrimitiveType>(std::string("string"));
	auto param = std::make_unique<ParameterDeclaration>();
	param->Type(paramType.get());
	param->NameToken(Identifier::Create("value"));
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<OperatorDeclaration>();
	node->ReturnType(retType.get());
	node->OperatorType(OperatorType::Implicit);
	node->Parameters().Add(param.get());
	node->Body(body.get());
	h.visitor->VisitOperatorDeclaration(node.get());

	// Ordering: kw:implicit < kw:operator < ReturnType < ( < param type < param name < ) < { < } < end.
	// The `kw:implicit` precedes `kw:operator`, and the ReturnType renders AFTER `kw:operator`
	// (the conversion-operator shape, the discriminator vs the binary/unary shape above).
	auto implicitKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:implicit");
	ASSERT_NE(implicitKw, h.inner.calls.end()) << "kw:implicit must render for an implicit conversion";
	auto opKw = std::find(implicitKw, h.inner.calls.end(), "kw:operator");
	ASSERT_NE(opKw, h.inner.calls.end()) << "kw:operator must render after kw:implicit";
	auto retTypeTok = std::find(opKw, h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the ReturnType must render AFTER kw:operator (conversion shape)";
	auto lpar = std::find(retTypeTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto paramTypeTok = std::find(lpar, h.inner.calls.end(), "primtype:string");
	ASSERT_NE(paramTypeTok, h.inner.calls.end());
	auto paramName = std::find(paramTypeTok, h.inner.calls.end(), "id:value");
	ASSERT_NE(paramName, h.inner.calls.end());
	auto rpar = std::find(paramName, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end());
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end());
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end());
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitOperatorDeclaration` over `checked explicit operator int(string value) {}` -- a checked
// conversion operator (OperatorType::CheckedExplicit): the `explicit` keyword before `operator`,
// then the `checked` keyword (IsChecked(CheckedExplicit) is true), then the ReturnType (the
// conversion target), then the parameter list, then the body. Pins the three-keyword sequence
// `kw:explicit < kw:operator < kw:checked` and the ReturnType-after-`checked` shape.
TEST(CSharp_OutputVisitor, VisitOperatorDeclarationCheckedExplicit) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto paramType = std::make_unique<PrimitiveType>(std::string("string"));
	auto param = std::make_unique<ParameterDeclaration>();
	param->Type(paramType.get());
	param->NameToken(Identifier::Create("value"));
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<OperatorDeclaration>();
	node->ReturnType(retType.get());
	node->OperatorType(OperatorType::CheckedExplicit);
	node->Parameters().Add(param.get());
	node->Body(body.get());
	h.visitor->VisitOperatorDeclaration(node.get());

	// Ordering: kw:explicit < kw:operator < kw:checked < ReturnType < ( < param type < param name
	// < ) < { < } < end.
	auto explicitKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:explicit");
	ASSERT_NE(explicitKw, h.inner.calls.end()) << "kw:explicit must render for an explicit conversion";
	auto opKw = std::find(explicitKw, h.inner.calls.end(), "kw:operator");
	ASSERT_NE(opKw, h.inner.calls.end()) << "kw:operator must render after kw:explicit";
	auto checkedKw = std::find(opKw, h.inner.calls.end(), "kw:checked");
	ASSERT_NE(checkedKw, h.inner.calls.end()) << "kw:checked must render after kw:operator (CheckedExplicit)";
	auto retTypeTok = std::find(checkedKw, h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the ReturnType must render AFTER kw:checked";
	auto lpar = std::find(retTypeTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto paramTypeTok = std::find(lpar, h.inner.calls.end(), "primtype:string");
	ASSERT_NE(paramTypeTok, h.inner.calls.end());
	auto paramName = std::find(paramTypeTok, h.inner.calls.end(), "id:value");
	ASSERT_NE(paramName, h.inner.calls.end());
	auto rpar = std::find(paramName, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end());
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end());
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end());
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitPropertyDeclaration` over `int Foo { get; set; }` -- an auto-property with a getter and
// setter (no bodies, no attributes), so `isSingleLine` is true under the default
// `AutoPropertyFormatting == SingleLine` policy: the braces are EndOfLine with no inner newline, an
// isSingleLine Space() separates `{` from the accessors, and the get/set accessors render in their
// tree order (the FirstChild/NextSibling walk) as `kw:get`/`kw:set` (the auto-property Semicolon +
// space each). No Initializer, so a NewLine closes the property. Mirrors the D353
// VisitIndexerDeclarationAutoPropertyGetSet shape (the auto-property Semicolon skipNewLine path).
TEST(CSharp_OutputVisitor, VisitPropertyDeclarationAutoPropertyGetSet) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto getter = std::make_unique<Accessor>(AccessorKind::Getter);
	auto setter = std::make_unique<Accessor>(AccessorKind::Setter);
	auto node = std::make_unique<PropertyDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	node->Getter(getter.get());
	node->Setter(setter.get());
	h.visitor->VisitPropertyDeclaration(node.get());

	// Ordering: ReturnType < NameToken < { < kw:get < kw:set < } < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end()) << "the NameToken must render after the ReturnType";
	auto lbrace = std::find(nameTok, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end()) << "the property body must open with tok:{ after the name";
	auto getKw = std::find(lbrace, h.inner.calls.end(), "kw:get");
	ASSERT_NE(getKw, h.inner.calls.end()) << "kw:get must render after tok:{";
	auto setKw = std::find(getKw, h.inner.calls.end(), "kw:set");
	ASSERT_NE(setKw, h.inner.calls.end()) << "kw:set must render after kw:get";
	auto rbrace = std::find(setKw, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end()) << "the body must close with tok:} after kw:set";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitPropertyDeclaration` over `int Foo => 1` -- an expression-bodied property (the ExpressionBody
// is non-null, so the else branch runs): Space() + WriteToken(Tokens::Arrow `=>`) + Space() +
// ExpressionBody->AcceptVisitor (PrimitiveExpression(1): primval) + Semicolon (tok:;/newline) +
// EndNode. Mirrors the D353 VisitIndexerDeclarationExpressionBody shape.
TEST(CSharp_OutputVisitor, VisitPropertyDeclarationExpressionBody) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto body = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<PropertyDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	node->ExpressionBody(body.get());
	h.visitor->VisitPropertyDeclaration(node.get());

	// Ordering: ReturnType < NameToken < => < primval < ; < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end());
	auto arrow = std::find(nameTok, h.inner.calls.end(), "tok:=>");
	ASSERT_NE(arrow, h.inner.calls.end()) << "the expression body must render tok:=> after the name";
	auto bodyVal = std::find(arrow, h.inner.calls.end(), "primval");
	ASSERT_NE(bodyVal, h.inner.calls.end()) << "the expression body must render primval after tok:=>";
	auto semi = std::find(bodyVal, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the expression-bodied property must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitPropertyDeclaration` over `int Foo { get; set; } = 1` -- an auto-property WITH an
// Initializer: the same `isSingleLine` get/set body as the auto-property test, then (no Initializer
// is false here) Space(SpaceAroundAssignment=false, no-op) + WriteToken(Tokens::Assign `=`) +
// Space(false, no-op) + Initializer->AcceptVisitor (PrimitiveExpression(1): primval) + Semicolon
// (tok:;/newline) + EndNode. Pins the `tok:=` initializer-assignment token between the close brace
// and the initializer value.
TEST(CSharp_OutputVisitor, VisitPropertyDeclarationWithInitializer) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto getter = std::make_unique<Accessor>(AccessorKind::Getter);
	auto setter = std::make_unique<Accessor>(AccessorKind::Setter);
	auto init = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<PropertyDeclaration>();
	node->ReturnType(retType.get());
	node->NameToken(Identifier::Create("Foo"));
	node->Getter(getter.get());
	node->Setter(setter.get());
	node->Initializer(init.get());
	h.visitor->VisitPropertyDeclaration(node.get());

	// Ordering: ReturnType < NameToken < { < kw:get < kw:set < } < tok:= < primval < ; < end.
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:int");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end());
	auto lbrace = std::find(nameTok, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end());
	auto getKw = std::find(lbrace, h.inner.calls.end(), "kw:get");
	ASSERT_NE(getKw, h.inner.calls.end());
	auto setKw = std::find(getKw, h.inner.calls.end(), "kw:set");
	ASSERT_NE(setKw, h.inner.calls.end());
	auto rbrace = std::find(setKw, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end()) << "the body must close with tok:} before the initializer";
	auto assign = std::find(rbrace, h.inner.calls.end(), "tok:=");
	ASSERT_NE(assign, h.inner.calls.end()) << "the initializer assignment tok:= must render after tok:}";
	auto initVal = std::find(assign, h.inner.calls.end(), "primval");
	ASSERT_NE(initVal, h.inner.calls.end()) << "the initializer value must render after tok:=";
	auto semi = std::find(initVal, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the initialized property must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// ---- VisitAttribute / VisitAttributeSection (D357, the next GeneralScope family) ----------

// `VisitAttribute` over `[Foo]` (no arguments, HasArgumentList false) -- the bare attribute: just
// the type reference. The Attribute's Type slot is a SimpleType "Foo" (VisitSimpleType renders
// start/id:Foo/end). Renders: StartNode(Attribute) + Type->AcceptVisitor (the SimpleType's own
// start/id:Foo/end) + EndNode(Attribute) -- no argument list, so the Space + InParenthesis branch
// is skipped (Arguments.Count == 0 AND HasArgumentList == false).
TEST(CSharp_OutputVisitor, VisitAttributeBare) {
	V h;
	auto type = std::make_unique<SimpleType>(Identifier::Create("Foo"));
	auto node = std::make_unique<Attribute>();
	node->Type(type.get());
	h.visitor->VisitAttribute(node.get());

	// The type identifier renders between the two StartNode calls and the two EndNode calls.
	auto firstStart = std::find(h.inner.calls.begin(), h.inner.calls.end(), "start");
	ASSERT_NE(firstStart, h.inner.calls.end());
	auto idTok = std::find(firstStart, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(idTok, h.inner.calls.end()) << "the attribute type must render id:Foo";
	auto lastEnd = std::find(idTok, h.inner.calls.end(), "end");
	ASSERT_NE(lastEnd, h.inner.calls.end()) << "the attribute must end with end";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitAttribute` over `[Foo()]` (HasArgumentList true, no arguments) -- an empty argument list
// is still an argument list, so the Space(SpaceBeforeMethodCallParentheses) +
// WriteCommaSeparatedListInParenthesis branch runs. The default SpaceBeforeMethodCallParentheses is
// false, so the Space is a no-op; the empty list renders just tok:( / tok:). Renders: start /
// [SimpleType start/id:Foo/end] / tok:( / tok:) / end.
TEST(CSharp_OutputVisitor, VisitAttributeWithEmptyArgumentList) {
	V h;
	auto type = std::make_unique<SimpleType>(Identifier::Create("Foo"));
	auto node = std::make_unique<Attribute>();
	node->Type(type.get());
	node->HasArgumentList(true);
	h.visitor->VisitAttribute(node.get());

	// The type renders before the open paren, which renders before the close paren, before the end.
	auto idTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "id:Foo");
	ASSERT_NE(idTok, h.inner.calls.end());
	auto lpar = std::find(idTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end()) << "tok:( must render after the attribute type";
	auto rpar = std::find(lpar, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "tok:) must render after tok:(";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitAttribute` over `[Foo(1)]` (one PrimitiveExpression argument) -- the argument recurses
// through VisitPrimitiveExpression (start/primval/end) inside the parens. Renders: start /
// [SimpleType start/id:Foo/end] / tok:( / [PrimitiveExpression start/primval/end] / tok:) / end.
TEST(CSharp_OutputVisitor, VisitAttributeWithOneArgument) {
	V h;
	auto type = std::make_unique<SimpleType>(Identifier::Create("Foo"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<Attribute>();
	node->Type(type.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitAttribute(node.get());

	// The type < open paren < the argument value < close paren < end.
	auto idTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "id:Foo");
	ASSERT_NE(idTok, h.inner.calls.end());
	auto lpar = std::find(idTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end()) << "tok:( must render after the attribute type";
	auto primval = std::find(lpar, h.inner.calls.end(), "primval");
	ASSERT_NE(primval, h.inner.calls.end()) << "the argument value must render inside the parens";
	auto rpar = std::find(primval, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "tok:) must render after the argument value";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitAttributeSection` over `[Foo]` (no target, no parent -> the default switch branch emits a
// NewLine). Renders: StartNode(section) + WriteToken(LBracket) + [no target] +
// WriteCommaSeparatedList (the one Attribute, recursing through VisitAttributeBare's type) +
// WriteToken(RBracket) + NewLine (the default branch) + EndNode.
TEST(CSharp_OutputVisitor, VisitAttributeSectionBare) {
	V h;
	auto type = std::make_unique<SimpleType>(Identifier::Create("Foo"));
	auto attr = std::make_unique<Attribute>();
	attr->Type(type.get());
	auto node = std::make_unique<AttributeSection>();
	node->Attributes().Add(attr.get());
	h.visitor->VisitAttributeSection(node.get());

	// tok:[ < the attribute type < tok:] < newline < end.
	auto lbracket = std::find(h.inner.calls.begin(), h.inner.calls.end(), "tok:[");
	ASSERT_NE(lbracket, h.inner.calls.end()) << "the section must open with tok:[";
	auto idTok = std::find(lbracket, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(idTok, h.inner.calls.end()) << "the attribute type must render after tok:[";
	auto rbracket = std::find(idTok, h.inner.calls.end(), "tok:]");
	ASSERT_NE(rbracket, h.inner.calls.end()) << "tok:] must render after the attribute type";
	auto nl = std::find(rbracket, h.inner.calls.end(), "newline");
	ASSERT_NE(nl, h.inner.calls.end()) << "the default branch must emit a newline after tok:]";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitAttributeSection` over `[return: Foo]` (AttributeTarget "return") -- the target branch:
// WriteKeyword("return") + WriteToken(Colon) + Space() between the `[` and the attribute list.
// Renders: start / tok:[ / kw:return / tok:: / space / [Attribute start/id:Foo/end] / tok:] /
// newline / end.
TEST(CSharp_OutputVisitor, VisitAttributeSectionWithTarget) {
	V h;
	auto type = std::make_unique<SimpleType>(Identifier::Create("Foo"));
	auto attr = std::make_unique<Attribute>();
	attr->Type(type.get());
	auto node = std::make_unique<AttributeSection>();
	node->AttributeTarget("return");
	node->Attributes().Add(attr.get());
	h.visitor->VisitAttributeSection(node.get());

	// tok:[ < kw:return < tok:: < space < id:Foo < tok:] < newline < end.
	auto lbracket = std::find(h.inner.calls.begin(), h.inner.calls.end(), "tok:[");
	ASSERT_NE(lbracket, h.inner.calls.end());
	auto targetKw = std::find(lbracket, h.inner.calls.end(), "kw:return");
	ASSERT_NE(targetKw, h.inner.calls.end()) << "the target keyword must render after tok:[";
	auto colon = std::find(targetKw, h.inner.calls.end(), "tok::");
	ASSERT_NE(colon, h.inner.calls.end()) << "tok:: must render after the target keyword";
	auto sp = std::find(colon, h.inner.calls.end(), "space");
	ASSERT_NE(sp, h.inner.calls.end()) << "a space must render after the colon";
	auto idTok = std::find(sp, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(idTok, h.inner.calls.end()) << "the attribute type must render after the target space";
	auto rbracket = std::find(idTok, h.inner.calls.end(), "tok:]");
	ASSERT_NE(rbracket, h.inner.calls.end()) << "tok:] must render after the attribute type";
	auto nl = std::find(rbracket, h.inner.calls.end(), "newline");
	ASSERT_NE(nl, h.inner.calls.end()) << "the default branch must emit a newline after tok:]";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitAttributeSection` whose Parent is a ParameterDeclaration (the ParameterDeclaration switch
// branch) and is the last (only) attribute section -- the `pd.Attributes.Last() == section` else
// branch emits a Space() (not the SpaceBetweenParameterAttributeSections, and not the default
// NewLine). Adding the section to the parameter's Attributes collection sets the section's Parent.
TEST(CSharp_OutputVisitor, VisitAttributeSectionOnParameter) {
	V h;
	auto type = std::make_unique<SimpleType>(Identifier::Create("Foo"));
	auto attr = std::make_unique<Attribute>();
	attr->Type(type.get());
	auto section = std::make_unique<AttributeSection>();
	section->Attributes().Add(attr.get());
	auto param = std::make_unique<ParameterDeclaration>();
	param->Attributes().Add(section.get());
	h.visitor->VisitAttributeSection(section.get());

	// tok:[ < id:Foo < tok:] < space < end (a space, not a newline -- the ParameterDeclaration
	// is-last branch).
	auto rbracket = std::find(h.inner.calls.begin(), h.inner.calls.end(), "tok:]");
	ASSERT_NE(rbracket, h.inner.calls.end()) << "tok:] must render";
	auto sp = std::find(rbracket, h.inner.calls.end(), "space");
	ASSERT_NE(sp, h.inner.calls.end()) << "the is-last parameter branch must emit a space after tok:]";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// ---- VisitTypeParameterDeclaration / VisitConstraint (D358, the generic-parameter family) --

// `VisitTypeParameterDeclaration` over `T` (VarianceModifier::Invariant, no attributes) -- the
// Invariant switch case breaks without writing a variance keyword, so only the name token
// renders. Renders: StartNode(tpd) + [no attributes] + [Invariant: break] + WriteIdentifier(T) +
// EndNode(tpd) -- start / id:T / end.
TEST(CSharp_OutputVisitor, VisitTypeParameterDeclarationBare) {
	V h;
	auto node = std::make_unique<TypeParameterDeclaration>(std::string("T"));
	node->Variance(VarianceModifier::Invariant);
	h.visitor->VisitTypeParameterDeclaration(node.get());

	// start < id:T < end (the Invariant case writes no variance keyword).
	auto startCall = std::find(h.inner.calls.begin(), h.inner.calls.end(), "start");
	ASSERT_NE(startCall, h.inner.calls.end());
	auto idTok = std::find(startCall, h.inner.calls.end(), "id:T");
	ASSERT_NE(idTok, h.inner.calls.end()) << "the type parameter name must render id:T";
	auto endCall = std::find(idTok, h.inner.calls.end(), "end");
	ASSERT_NE(endCall, h.inner.calls.end()) << "the type parameter must end with end";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitTypeParameterDeclaration` over `out T` (VarianceModifier::Covariant) -- the Covariant
// switch case writes the `out` variance keyword before the name. The InsertRequiredSpacesDecorator
// inserts a space between the `out` keyword and the `T` identifier (they would merge into `outT`).
// Renders: start / kw:out / space / id:T / end.
TEST(CSharp_OutputVisitor, VisitTypeParameterDeclarationCovariant) {
	V h;
	auto node = std::make_unique<TypeParameterDeclaration>(std::string("T"));
	node->Variance(VarianceModifier::Covariant);
	h.visitor->VisitTypeParameterDeclaration(node.get());

	// start < kw:out < id:T < end (the Covariant case writes the `out` keyword before the name).
	auto outKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:out");
	ASSERT_NE(outKw, h.inner.calls.end()) << "the Covariant case must write kw:out";
	auto idTok = std::find(outKw, h.inner.calls.end(), "id:T");
	ASSERT_NE(idTok, h.inner.calls.end()) << "the name must render after kw:out";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitTypeParameterDeclaration` over `in T` (VarianceModifier::Contravariant) -- the
// Contravariant switch case writes the `in` variance keyword before the name. The decorator
// inserts a space between `in` and `T` (they would merge into `inT`). Renders: start / kw:in /
// space / id:T / end.
TEST(CSharp_OutputVisitor, VisitTypeParameterDeclarationContravariant) {
	V h;
	auto node = std::make_unique<TypeParameterDeclaration>(std::string("T"));
	node->Variance(VarianceModifier::Contravariant);
	h.visitor->VisitTypeParameterDeclaration(node.get());

	// start < kw:in < id:T < end (the Contravariant case writes the `in` keyword before the name).
	auto inKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:in");
	ASSERT_NE(inKw, h.inner.calls.end()) << "the Contravariant case must write kw:in";
	auto idTok = std::find(inKw, h.inner.calls.end(), "id:T");
	ASSERT_NE(idTok, h.inner.calls.end()) << "the name must render after kw:in";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitConstraint` over `where T : Base` (one base type) -- the leading Space, the `where`
// keyword, the constrained TypeParameter (a SimpleType `T`), the Space/Colon/Space, and the one
// base type (a SimpleType `Base`). The decorator inserts a space between `where` and `T` (they
// would merge into `whereT`). Renders: start / space / kw:where / [SimpleType start/space/id:T/end]
// / space / tok:: / space / [SimpleType start/id:Base/end] / end.
TEST(CSharp_OutputVisitor, VisitConstraintBare) {
	V h;
	auto typeParam = std::make_unique<SimpleType>(Identifier::Create("T"));
	auto base = std::make_unique<SimpleType>(Identifier::Create("Base"));
	auto node = std::make_unique<Constraint>(typeParam.get());
	node->BaseTypes().Add(base.get());
	h.visitor->VisitConstraint(node.get());

	// space < kw:where < id:T < tok:: < id:Base < end.
	auto whereKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:where");
	ASSERT_NE(whereKw, h.inner.calls.end()) << "the constraint must open with kw:where";
	auto idT = std::find(whereKw, h.inner.calls.end(), "id:T");
	ASSERT_NE(idT, h.inner.calls.end()) << "the constrained type parameter must render after kw:where";
	auto colon = std::find(idT, h.inner.calls.end(), "tok::");
	ASSERT_NE(colon, h.inner.calls.end()) << "tok:: must render after the type parameter";
	auto idBase = std::find(colon, h.inner.calls.end(), "id:Base");
	ASSERT_NE(idBase, h.inner.calls.end()) << "the base type must render after tok::";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitConstraint` over `where T : Base1, Base2` (two base types) -- the WriteCommaSeparatedList
// recurses through both base types with a Comma (tok:,) between them. Renders: start / space /
// kw:where / [SimpleType T] / space / tok:: / space / [SimpleType Base1] / tok:, / [SimpleType
// Base2] / end.
TEST(CSharp_OutputVisitor, VisitConstraintMultipleBaseTypes) {
	V h;
	auto typeParam = std::make_unique<SimpleType>(Identifier::Create("T"));
	auto base1 = std::make_unique<SimpleType>(Identifier::Create("Base1"));
	auto base2 = std::make_unique<SimpleType>(Identifier::Create("Base2"));
	auto node = std::make_unique<Constraint>(typeParam.get());
	node->BaseTypes().Add(base1.get());
	node->BaseTypes().Add(base2.get());
	h.visitor->VisitConstraint(node.get());

	// kw:where < id:T < tok:: < id:Base1 < tok:, < id:Base2 < end (the comma separates the bases).
	auto colon = std::find(h.inner.calls.begin(), h.inner.calls.end(), "tok::");
	ASSERT_NE(colon, h.inner.calls.end());
	auto idBase1 = std::find(colon, h.inner.calls.end(), "id:Base1");
	ASSERT_NE(idBase1, h.inner.calls.end()) << "the first base type must render after tok::";
	auto comma = std::find(idBase1, h.inner.calls.end(), "tok:,");
	ASSERT_NE(comma, h.inner.calls.end()) << "a comma must separate the two base types";
	auto idBase2 = std::find(comma, h.inner.calls.end(), "id:Base2");
	ASSERT_NE(idBase2, h.inner.calls.end()) << "the second base type must render after the comma";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// ---- VisitLocalFunctionDeclarationStatement (D359, the local-function wrapper) ------------

// `VisitLocalFunctionDeclarationStatement` over `void Foo() {}` -- the wrapper delegates the
// entire render to its single REQUIRED `MethodDeclaration` `Declaration` child, recursing through
// `VisitMethodDeclaration` (D354). The outer `LocalFunctionDeclarationStatement` contributes just
// a `start`/`end` pair around the wrapped method's full token sequence. Renders: start(lfds) /
// [VisitMethodDeclaration `void Foo() {}`: start/primtype:void/space/id:Foo/tok:(/tok:)/tok:{/
// indent/newline/unindent/tok:}/newline/end] / end(lfds). The find-based ordering pins the outer
// `start` first, the wrapped method's key tokens in order between it and the trailing `end`, and
// the outer `end` last -- confirming the delegation wraps the method render without reordering it.
TEST(CSharp_OutputVisitor, VisitLocalFunctionDeclarationStatementBare) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("void"));
	auto body = std::make_unique<BlockStatement>();
	auto method = std::make_unique<MethodDeclaration>();
	method->ReturnType(retType.get());
	method->NameToken(Identifier::Create("Foo"));
	method->Body(body.get());
	auto node = std::make_unique<LocalFunctionDeclarationStatement>(method.get());
	h.visitor->VisitLocalFunctionDeclarationStatement(node.get());

	// The outer wrapper opens with `start` and closes with `end`; the wrapped method's key tokens
	// (ReturnType < NameToken < ( < ) < { < }) render in order between them.
	EXPECT_EQ(h.inner.calls.front(), "start") << "the outer wrapper must open with start";
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:void");
	ASSERT_NE(retTypeTok, h.inner.calls.end()) << "the wrapped method's ReturnType must render";
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end()) << "the wrapped method's NameToken must render after the ReturnType";
	auto lpar = std::find(nameTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end()) << "the parameter list must open with tok:( after the name";
	auto rpar = std::find(lpar, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "the parameter list must close with tok:)";
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end()) << "the body must open with tok:{ after the close paren";
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end()) << "the body must close with tok:} after tok:{";
	EXPECT_EQ(h.inner.calls.back(), "end") << "the outer wrapper must close with end";
}

// `VisitLocalFunctionDeclarationStatement` over `void Foo(int x) {}` -- the wrapped method has
// one parameter, which recurses through `VisitParameterDeclaration` between the parens. The
// `int`/`x` parameter is distinct from the `void` ReturnType so the find-based ordering pins each
// unambiguously, confirming the delegation carries the parameter render through unchanged.
TEST(CSharp_OutputVisitor, VisitLocalFunctionDeclarationStatementWithParam) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("void"));
	auto paramType = std::make_unique<PrimitiveType>(std::string("int"));
	auto param = std::make_unique<ParameterDeclaration>();
	param->Type(paramType.get());
	param->NameToken(Identifier::Create("x"));
	auto body = std::make_unique<BlockStatement>();
	auto method = std::make_unique<MethodDeclaration>();
	method->ReturnType(retType.get());
	method->NameToken(Identifier::Create("Foo"));
	method->Parameters().Add(param.get());
	method->Body(body.get());
	auto node = std::make_unique<LocalFunctionDeclarationStatement>(method.get());
	h.visitor->VisitLocalFunctionDeclarationStatement(node.get());

	// Outer start < ReturnType < NameToken < ( < param type < param name < ) < { < } < outer end.
	EXPECT_EQ(h.inner.calls.front(), "start");
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:void");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end());
	auto lpar = std::find(nameTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto paramTypeTok = std::find(lpar, h.inner.calls.end(), "primtype:int");
	ASSERT_NE(paramTypeTok, h.inner.calls.end()) << "the parameter type must render after tok:(";
	auto paramName = std::find(paramTypeTok, h.inner.calls.end(), "id:x");
	ASSERT_NE(paramName, h.inner.calls.end()) << "the parameter name must render after its type";
	auto rpar = std::find(paramName, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end()) << "tok:) must render after the parameter name";
	auto lbrace = std::find(rpar, h.inner.calls.end(), "tok:{");
	ASSERT_NE(lbrace, h.inner.calls.end());
	auto rbrace = std::find(lbrace, h.inner.calls.end(), "tok:}");
	ASSERT_NE(rbrace, h.inner.calls.end());
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitLocalFunctionDeclarationStatement` over `void Foo();` -- the wrapped method is bodyless
// (an abstract/interface/extern local function), so `WriteMethodBody(nullptr)` writes a
// `Semicolon` (tok:;) instead of a block. The delegation must carry the `;` through unchanged.
TEST(CSharp_OutputVisitor, VisitLocalFunctionDeclarationStatementNoBody) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("void"));
	auto method = std::make_unique<MethodDeclaration>();
	method->ReturnType(retType.get());
	method->NameToken(Identifier::Create("Foo"));
	auto node = std::make_unique<LocalFunctionDeclarationStatement>(method.get());
	h.visitor->VisitLocalFunctionDeclarationStatement(node.get());

	// Outer start < ReturnType < NameToken < ( < ) < ; < outer end (the bodyless method closes
	// with tok:; via WriteMethodBody(nullptr)).
	EXPECT_EQ(h.inner.calls.front(), "start");
	auto retTypeTok = std::find(h.inner.calls.begin(), h.inner.calls.end(), "primtype:void");
	ASSERT_NE(retTypeTok, h.inner.calls.end());
	auto nameTok = std::find(retTypeTok, h.inner.calls.end(), "id:Foo");
	ASSERT_NE(nameTok, h.inner.calls.end());
	auto lpar = std::find(nameTok, h.inner.calls.end(), "tok:(");
	ASSERT_NE(lpar, h.inner.calls.end());
	auto rpar = std::find(lpar, h.inner.calls.end(), "tok:)");
	ASSERT_NE(rpar, h.inner.calls.end());
	auto semi = std::find(rpar, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "a bodyless wrapped method must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// ---- VisitUsingDeclaration / VisitUsingAliasDeclaration / VisitExternAliasDeclaration (D360,
// the namespace-level directive family: using_directive / using_alias_directive /
// extern_alias_directive, C# grammar 14.4-14.6) --------------------------------------

// `VisitUsingDeclaration` over `using System;` -- the `using_directive ::= 'using' type ';'`
// (C# grammar 14.6.3): the `UsingKeyword` then the required `Import` AstType (a SimpleType renders
// just its identifier) then a `Semicolon` (tok:; + NewLine). The find-based ordering pins the
// `using` keyword before the imported namespace identifier before the terminating semicolon.
TEST(CSharp_OutputVisitor, VisitUsingDeclarationBare) {
	V h;
	auto import = std::make_unique<SimpleType>(std::string("System"));
	auto node = std::make_unique<UsingDeclaration>(import.get());
	h.visitor->VisitUsingDeclaration(node.get());

	EXPECT_EQ(h.inner.calls.front(), "start");
	auto usingKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:using");
	ASSERT_NE(usingKw, h.inner.calls.end()) << "the using directive must open with kw:using";
	auto ns = std::find(usingKw, h.inner.calls.end(), "id:System");
	ASSERT_NE(ns, h.inner.calls.end()) << "the imported namespace must render after kw:using";
	auto semi = std::find(ns, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the using directive must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitUsingDeclaration` over `using System.Collections;` -- the `Import` is a `MemberType`
// (`System.Collections`), so the render recurses through `VisitSimpleType` (the `System` target)
// then `VisitMemberType` (the `.Collections` member). The find-based ordering pins the dotted
// namespace: kw:using < id:System < tok:. < id:Collections < tok:;.
TEST(CSharp_OutputVisitor, VisitUsingDeclarationDotted) {
	V h;
	auto target = std::make_unique<SimpleType>(std::string("System"));
	auto import = std::make_unique<MemberType>(target.get(), std::string("Collections"));
	auto node = std::make_unique<UsingDeclaration>(import.get());
	h.visitor->VisitUsingDeclaration(node.get());

	EXPECT_EQ(h.inner.calls.front(), "start");
	auto usingKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:using");
	ASSERT_NE(usingKw, h.inner.calls.end());
	auto ns = std::find(usingKw, h.inner.calls.end(), "id:System");
	ASSERT_NE(ns, h.inner.calls.end()) << "the MemberType target must render after kw:using";
	auto dot = std::find(ns, h.inner.calls.end(), "tok:.");
	ASSERT_NE(dot, h.inner.calls.end()) << "the dot separator must render between the two names";
	auto member = std::find(dot, h.inner.calls.end(), "id:Collections");
	ASSERT_NE(member, h.inner.calls.end()) << "the member name must render after the dot";
	auto semi = std::find(member, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the using directive must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitUsingAliasDeclaration` over `using A = System;` -- the
// `using_alias_directive ::= 'using' identifier '=' type ';'` (C# grammar 14.6.2): the `UsingKeyword`,
// the `AliasToken` identifier, the `Tokens::Assign` (flanked by the `SpaceAroundEqualityOperator`
// spaces, no-ops under the default options), the required `Import` AstType, then a `Semicolon`.
// The find-based ordering pins kw:using < id:A < tok:= < id:System < tok:;.
TEST(CSharp_OutputVisitor, VisitUsingAliasDeclarationBare) {
	V h;
	auto import = std::make_unique<SimpleType>(std::string("System"));
	auto node = std::make_unique<UsingAliasDeclaration>(std::string("A"), import.get());
	h.visitor->VisitUsingAliasDeclaration(node.get());

	EXPECT_EQ(h.inner.calls.front(), "start");
	auto usingKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:using");
	ASSERT_NE(usingKw, h.inner.calls.end()) << "the alias directive must open with kw:using";
	auto alias = std::find(usingKw, h.inner.calls.end(), "id:A");
	ASSERT_NE(alias, h.inner.calls.end()) << "the alias identifier must render after kw:using";
	auto assign = std::find(alias, h.inner.calls.end(), "tok:=");
	ASSERT_NE(assign, h.inner.calls.end()) << "the assignment token must render after the alias";
	auto ns = std::find(assign, h.inner.calls.end(), "id:System");
	ASSERT_NE(ns, h.inner.calls.end()) << "the imported type must render after tok:=";
	auto semi = std::find(ns, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the alias directive must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitUsingAliasDeclaration` over `using B = System.Collections;` -- the `Import` is a
// `MemberType` (`System.Collections`), so the alias pins a dotted namespace after the
// assignment: kw:using < id:B < tok:= < id:System < tok:. < id:Collections < tok:;.
TEST(CSharp_OutputVisitor, VisitUsingAliasDeclarationDotted) {
	V h;
	auto target = std::make_unique<SimpleType>(std::string("System"));
	auto import = std::make_unique<MemberType>(target.get(), std::string("Collections"));
	auto node = std::make_unique<UsingAliasDeclaration>(std::string("B"), import.get());
	h.visitor->VisitUsingAliasDeclaration(node.get());

	EXPECT_EQ(h.inner.calls.front(), "start");
	auto usingKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:using");
	ASSERT_NE(usingKw, h.inner.calls.end());
	auto alias = std::find(usingKw, h.inner.calls.end(), "id:B");
	ASSERT_NE(alias, h.inner.calls.end());
	auto assign = std::find(alias, h.inner.calls.end(), "tok:=");
	ASSERT_NE(assign, h.inner.calls.end());
	auto ns = std::find(assign, h.inner.calls.end(), "id:System");
	ASSERT_NE(ns, h.inner.calls.end()) << "the MemberType target must render after tok:=";
	auto dot = std::find(ns, h.inner.calls.end(), "tok:.");
	ASSERT_NE(dot, h.inner.calls.end()) << "the dot separator must render between the two names";
	auto member = std::find(dot, h.inner.calls.end(), "id:Collections");
	ASSERT_NE(member, h.inner.calls.end()) << "the member name must render after the dot";
	auto semi = std::find(member, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the alias directive must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}

// `VisitExternAliasDeclaration` over `extern alias G;` -- the
// `extern_alias_directive ::= 'extern' 'alias' identifier ';'` (C# grammar 14.4): the two
// namespace-scope keyword consts (`Tokens::ExternKeyword`/`Tokens::AliasKeyword`), each followed
// by an explicit `Space()`, then the `NameToken` identifier, then a `Semicolon`. The find-based
// ordering pins the keyword sequence kw:extern < kw:alias before the name identifier before the
// terminating semicolon.
TEST(CSharp_OutputVisitor, VisitExternAliasDeclarationBare) {
	V h;
	auto node = std::make_unique<ExternAliasDeclaration>(std::string("G"));
	h.visitor->VisitExternAliasDeclaration(node.get());

	EXPECT_EQ(h.inner.calls.front(), "start");
	auto externKw = std::find(h.inner.calls.begin(), h.inner.calls.end(), "kw:extern");
	ASSERT_NE(externKw, h.inner.calls.end()) << "the extern alias must open with kw:extern";
	auto aliasKw = std::find(externKw, h.inner.calls.end(), "kw:alias");
	ASSERT_NE(aliasKw, h.inner.calls.end()) << "kw:alias must render after kw:extern";
	auto name = std::find(aliasKw, h.inner.calls.end(), "id:G");
	ASSERT_NE(name, h.inner.calls.end()) << "the alias name must render after kw:alias";
	auto semi = std::find(name, h.inner.calls.end(), "tok:;");
	ASSERT_NE(semi, h.inner.calls.end()) << "the extern alias must close with tok:;";
	EXPECT_EQ(h.inner.calls.back(), "end");
}
