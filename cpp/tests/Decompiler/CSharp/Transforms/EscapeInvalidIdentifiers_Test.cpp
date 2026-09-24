// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for EscapeInvalidIdentifiers (the port of
// ICSharpCode.Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.cs): an
// identifier holding characters outside [A-Za-z0-9_] becomes the `_XXXX`
// hex-escape form (per invalid character), a leading non-letter gains an
// underscore, and a clean identifier passes through unchanged.

#include "Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;

} // namespace

// A leading digit is underscore-prefixed (`9abc` -> `_9abc`: every character
// is valid but the first one is not a letter).
TEST(EscapeInvalidIdentifiersTest, LeadingDigitIsPrefixed)
{
    auto ident = Syntax::Identifier::Create("9abc");
    Transforms::TransformContext context;
    Transforms::EscapeInvalidIdentifiers transform;
    transform.Run(*ident, context);
    EXPECT_EQ(ident->Name(), "_9abc");
}

// An invalid character becomes its `_XXXX` hex escape (`a-b` -> `a_002Db`).
TEST(EscapeInvalidIdentifiersTest, InvalidCharacterIsHexEscaped)
{
    auto ident = Syntax::Identifier::Create("a-b");
    Transforms::TransformContext context;
    Transforms::EscapeInvalidIdentifiers transform;
    transform.Run(*ident, context);
    EXPECT_EQ(ident->Name(), "a_002Db");
}

// A name that is already valid is left untouched (no Step, no change).
TEST(EscapeInvalidIdentifiersTest, ValidIdentifierIsUntouched)
{
    auto ident = Syntax::Identifier::Create("valid_name1");
    Transforms::TransformContext context;
    int steps = 0;
    context.Step = [&steps](const std::string&, const void*) { steps++; };
    Transforms::EscapeInvalidIdentifiers transform;
    transform.Run(*ident, context);
    EXPECT_EQ(ident->Name(), "valid_name1");
    EXPECT_EQ(steps, 0);
}

// The transform descends into the tree: an identifier inside an expression is
// escaped too.
TEST(EscapeInvalidIdentifiersTest, DescendsIntoIdentifiersInsideExpressions)
{
    auto expr = std::make_unique<Syntax::IdentifierExpression>("a.b");
    Transforms::TransformContext context;
    Transforms::EscapeInvalidIdentifiers transform;
    transform.Run(*expr, context);
    // The root IdentifierExpression carries the "a.b" identifier; the escape
    // applies to it (the '.' becomes _002E).
    EXPECT_EQ(expr->IdentifierToken()->Name(), "a_002Eb");
}