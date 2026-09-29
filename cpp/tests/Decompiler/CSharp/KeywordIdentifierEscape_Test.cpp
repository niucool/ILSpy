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

// The keyword-identifier escape tests: the metadata names that are C#
// keywords render with the `@` verbatim prefix (the C#
// TextWriterTokenWriter.WriteIdentifier rule) -- in the field
// declarations, the method signatures, and the body uses. A raw keyword
// in an identifier position is invalid C#, not a cosmetic divergence.

#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

namespace CSharp = ::ILSpy::Decompiler::CSharp;
namespace Metadata = ::ILSpy::Decompiler::Metadata;

const char* kKeywordNamesFixture =
    "/home/jim/ilspy-test-fixtures/keyword_names/KeywordNames.dll";

bool RenderKeywordNames(std::string& text) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kKeywordNamesFixture, ec))
        return false;
    Metadata::MetadataFile file(kKeywordNamesFixture);
    if (!file.IsValid())
        return false;
    for (const auto& t : file.TypeDefs()) {
        if (t.Name != "KeywordNames")
            continue;
        return CSharp::CSharpDecompiler::DecompileTypeToString(file, t.Token,
                                                                text);
    }
    return false;
}

} // namespace

TEST(KeywordIdentifierEscapeTest, EscapesFieldDeclarationNames)
{
    std::string text;
    if (!RenderKeywordNames(text))
        GTEST_SKIP() << "the keyword-names fixture is not provisioned";
    // The field declarations carry the `@` prefix.
    EXPECT_NE(text.find("string @namespace;"), std::string::npos) << text;
    EXPECT_NE(text.find("int @class;"), std::string::npos) << text;
    EXPECT_NE(text.find("int @interface;"), std::string::npos) << text;
    // The raw keyword forms never appear in identifier positions.
    EXPECT_EQ(text.find("string namespace;"), std::string::npos) << text;
    EXPECT_EQ(text.find("int class;"), std::string::npos) << text;
    EXPECT_EQ(text.find("int interface;"), std::string::npos) << text;
}

TEST(KeywordIdentifierEscapeTest, EscapesSignatureParameterNames)
{
    std::string text;
    if (!RenderKeywordNames(text))
        GTEST_SKIP() << "the keyword-names fixture is not provisioned";
    EXPECT_NE(text.find("KeywordNames(string @namespace, int @class)"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("SumWith(int @event)"), std::string::npos) << text;
    EXPECT_EQ(text.find("int event)"), std::string::npos) << text;
}

TEST(KeywordIdentifierEscapeTest, EscapesBodyUses)
{
    std::string text;
    if (!RenderKeywordNames(text))
        GTEST_SKIP() << "the keyword-names fixture is not provisioned";
    // The parameter uses in the bodies escape.
    EXPECT_NE(text.find("@namespace = @namespace"), std::string::npos)
        << text;
    EXPECT_NE(text.find("@class = @class"), std::string::npos) << text;
    EXPECT_NE(text.find("@event + @class"), std::string::npos) << text;
    // The implicit this never escapes (it is a keyword token, not an
    // identifier).
    EXPECT_EQ(text.find("@this"), std::string::npos) << text;
}

TEST(KeywordIdentifierEscapeTest, ThisReceiverRuleForShadowedAndBaseMembers)
{
    std::string text;
    if (!RenderKeywordNames(text))
        GTEST_SKIP() << "the keyword-names fixture is not provisioned";
    // The own-type field store behind a same-named parameter keeps the
    // explicit receiver (the C# RequiresQualifier shadow rule) -- the
    // bare form would be a self-assignment.
    EXPECT_NE(text.find("this.@namespace = @namespace"), std::string::npos)
        << text;
    EXPECT_NE(text.find("this.@class = @class"), std::string::npos) << text;
    // A base-declared virtual method invoked with the `call` opcode (the
    // `base.M()` source form) renders the base reference.
    EXPECT_NE(text.find("base.Suffix()"), std::string::npos) << text;
    // The this-targeted callvirt elides the receiver.
    EXPECT_NE(text.find("GetNamespace() + Suffix()"), std::string::npos)
        << text;
    EXPECT_EQ(text.find("this.GetNamespace()"), std::string::npos) << text;
}
