// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The foreach element-name tests (the flat render's array-pattern foreach):
// the element variable's name comes from the AssignVariableNames
// type-based inference -- the known-type dict (byte -> b, string -> text)
// and the lowercased short type name (ImageDebugDirectory ->
// imageDebugDirectory) -- never from the rendered type text itself (the
// keyword `byte` is not a legal identifier).

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

namespace {

namespace CSharp = ::ILSpy::Decompiler::CSharp;
namespace Metadata = ::ILSpy::Decompiler::Metadata;

const char* kForEachNameFixture =
    "/home/jim/ilspy-test-fixtures/foreach_name/ForEachName.dll";

bool RenderForEachShapes(std::string& text) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kForEachNameFixture, ec))
        return false;
    Metadata::MetadataFile file(kForEachNameFixture);
    if (!file.IsValid())
        return false;
    for (const auto& t : file.TypeDefs()) {
        if (t.Name != "ForEachShapes")
            continue;
        return CSharp::CSharpDecompiler::DecompileTypeToString(file, t.Token,
                                                                text);
    }
    return false;
}

} // namespace

TEST(ForeachVariableNameTest, ElementNameFromTypeInference)
{
    std::string text;
    if (!RenderForEachShapes(text))
        GTEST_SKIP() << "the foreach-name fixture is not provisioned";
    // The known-type dict: byte -> b, string -> text.
    EXPECT_NE(text.find("foreach (byte b in "), std::string::npos)
        << "the byte element takes the dict name, not the type keyword: "
        << text;
    EXPECT_NE(text.find("foreach (string text in "), std::string::npos)
        << text;
    // A class-typed element takes the lowercased short type name.
    EXPECT_NE(text.find("foreach (ImageDebugDirectory imageDebugDirectory in "),
              std::string::npos)
        << "the class-typed element lowercases the type name: " << text;
    // The keyword form is never emitted as an identifier.
    EXPECT_EQ(text.find("foreach (byte byte in "), std::string::npos)
        << "the type keyword is not a legal element name: " << text;
}

// The store proposal (the C# GetNameFromInstruction): a local whose store
// is a get_/Get*-method call takes the method-name remainder, not the
// type-based fallback (`string[] names = GetNames()`, not `array`).
TEST(ForeachVariableNameTest, StoreProposalNamesFromGetMethod)
{
    std::string text;
    if (!RenderForEachShapes(text))
        GTEST_SKIP() << "the foreach-name fixture is not provisioned";
    EXPECT_NE(text.find("string[] names = "), std::string::npos)
        << "the Get*-method store names the local from the method name: "
        << text;
}
