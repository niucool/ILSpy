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

// The AST-transform layer foundation suite: the `TransformContext` read surface
// over a MinimalCorlib compilation, and the first concrete `IAstTransform`
// (`EscapeInvalidIdentifiers`) end-to-end.

#include "Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharp = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The fixture the ExpressionBuilder/MinimalCorlib suites use: a compilation over
// MinimalCorlib, a root using scope, a run, and the resolve/ast-builder state a
// TransformContext composes.
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// TransformContext

TEST(TransformContextTest, ExposesCompilationRunAndPosition)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    EXPECT_EQ(&context.TypeSystem(), &fixture.compilation);
    EXPECT_EQ(&context.DecompileRun(), &fixture.run);
    EXPECT_EQ(&context.Settings(), &fixture.settings);
    EXPECT_EQ(&context.TypeSystemAstBuilder(), &fixture.astBuilder);
    // The position accessors delegate to the resolve context; no current type/member.
    EXPECT_EQ(context.CurrentModule(), &fixture.compilation.MainModule());
    EXPECT_EQ(context.CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(context.CurrentMember(), nullptr);
}

TEST(TransformContextTest, RequiredNamespacesSupersetTracksTheRun)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    EXPECT_TRUE(context.RequiredNamespacesSuperset().empty());

    fixture.run.SetNamespaces(std::unordered_set<std::string>{"System", "System.IO"});
    const std::unordered_set<std::string> superset = context.RequiredNamespacesSuperset();
    EXPECT_EQ(superset.size(), 2u);
    EXPECT_TRUE(superset.count("System"));
    EXPECT_TRUE(superset.count("System.IO"));
}

// ---------------------------------------------------------------------------
// EscapeInvalidIdentifiers

TEST(EscapeInvalidIdentifiersTest, IsValidClassifiesTheCSharpIdentifierSet)
{
    EXPECT_TRUE(Transforms::EscapeInvalidIdentifiers::IsValid(u'a'));
    EXPECT_TRUE(Transforms::EscapeInvalidIdentifiers::IsValid(u'Z'));
    EXPECT_TRUE(Transforms::EscapeInvalidIdentifiers::IsValid(u'0'));
    EXPECT_TRUE(Transforms::EscapeInvalidIdentifiers::IsValid(u'_'));
    // A non-ASCII Unicode letter is a letter (the char.IsLetterOrDigit probe table).
    EXPECT_TRUE(Transforms::EscapeInvalidIdentifiers::IsValid(u'\u00E9'));
    EXPECT_FALSE(Transforms::EscapeInvalidIdentifiers::IsValid(u'<'));
    EXPECT_FALSE(Transforms::EscapeInvalidIdentifiers::IsValid(u' '));
    EXPECT_FALSE(Transforms::EscapeInvalidIdentifiers::IsValid(u'\u00A9'));
}

TEST(EscapeInvalidIdentifiersTest, ReplaceInvalidEscapesAndGuardsTheLeadingUnit)
{
    // A fully valid name is unchanged.
    EXPECT_EQ(Transforms::EscapeInvalidIdentifiers::ReplaceInvalid("Valid_Name1"), "Valid_Name1");
    // An invalid unit becomes its uppercase-hex escape.
    EXPECT_EQ(Transforms::EscapeInvalidIdentifiers::ReplaceInvalid("a<b"), "a_003Cb");
    // A valid-but-not-first-character unit (a digit) gets the leading underscore.
    EXPECT_EQ(Transforms::EscapeInvalidIdentifiers::ReplaceInvalid("1abc"), "_1abc");
    // A leading invalid unit already starts with '_', so no extra underscore.
    EXPECT_EQ(Transforms::EscapeInvalidIdentifiers::ReplaceInvalid("<a"), "_003Ca");
    // A non-ASCII symbol is escaped over its UTF-16 unit (U+00A9). The bytes are
    // spelled explicitly so the test supplies valid UTF-8 regardless of the
    // compiler's narrow execution character set.
    EXPECT_EQ(Transforms::EscapeInvalidIdentifiers::ReplaceInvalid("a\xC2\xA9" "b"), "a_00A9b");
    // The empty string stays empty (the C# `name.Length >= 1` guard).
    EXPECT_EQ(Transforms::EscapeInvalidIdentifiers::ReplaceInvalid(""), "");
}

TEST(EscapeInvalidIdentifiersTest, RunRewritesIdentifiersInPlace)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    Syntax::SyntaxTree tree;
    auto* invalid = new Syntax::IdentifierExpression("a<b>c");
    tree.Members().Add(invalid);
    auto* valid = new Syntax::IdentifierExpression("Keep_Me");
    tree.Members().Add(valid);

    Transforms::EscapeInvalidIdentifiers transform;
    transform.Run(tree, context);

    EXPECT_EQ(invalid->Identifier(), "a_003Cb_003Ec");
    EXPECT_EQ(valid->Identifier(), "Keep_Me");
}
