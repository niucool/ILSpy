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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the IAssemblyReference / AssemblyNameReference port (cpp/Decompiler/
// Metadata/AssemblyNameReference.{hpp,cpp}), pinned against the real .NET classes:
// every Parse/FullName expectation and exception message was dumped from the
// installed ilspycmd 11.0 tool's ICSharpCode.Decompiler.dll driven over the
// identical inputs (the C:\temp-probe\AnrProbe matrix).

#include "Decompiler/Metadata/AssemblyNameReference.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::Metadata::AssemblyNameReference;
using ILSpy::Decompiler::Metadata::IAssemblyReference;
using ILSpy::Decompiler::TypeSystem::Version;

namespace {

std::vector<std::uint8_t> Bytes(std::initializer_list<std::uint8_t> list)
{
    return std::vector<std::uint8_t>(list);
}

TEST(AssemblyNameReferenceParse, ParsesTheDefaultBamlReferences)
{
    // The seven BamlDecompilerTypeSystem default references round-trip exactly.
    const char* refs[] = {
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
        "System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
        "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
        "PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
        "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
        "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
        "System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    };
    for (const char* ref : refs) {
        auto r = AssemblyNameReference::Parse(ref);
        EXPECT_EQ(r.FullName(), ref);
        EXPECT_EQ(r.ToString(), ref);
        ASSERT_TRUE(r.Version().has_value());
        EXPECT_EQ(*r.Version(), Version(4, 0, 0, 0));
        ASSERT_TRUE(r.Culture().has_value());
        EXPECT_EQ(*r.Culture(), "");
        ASSERT_TRUE(r.PublicKeyToken().has_value());
        EXPECT_EQ(r.PublicKeyToken()->size(), 8u);
        EXPECT_FALSE(r.IsWindowsRuntime());
        EXPECT_FALSE(r.IsRetargetable());
    }
}

TEST(AssemblyNameReferenceParse, SingleComponentHasNullProperties)
{
    auto r = AssemblyNameReference::Parse("System");
    EXPECT_EQ(r.Name(), "System");
    EXPECT_FALSE(r.Version().has_value());
    EXPECT_FALSE(r.Culture().has_value());
    EXPECT_FALSE(r.PublicKeyToken().has_value());
    // A null version renders the UniversalAssemblyResolver.ZeroVersion 0.0.0.0.
    EXPECT_EQ(r.FullName(),
        "System, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
}

TEST(AssemblyNameReferenceParse, PartialVersionMakesFullNameThrow)
{
    // A version with fewer than four components parses fine, but the FullName
    // render (ToString(fieldCount: 4)) throws the Version ArgumentException.
    auto r = AssemblyNameReference::Parse("x, Version=1.2");
    ASSERT_TRUE(r.Version().has_value());
    EXPECT_EQ(*r.Version(), Version(1, 2));
    try {
        r.FullName();
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 2. (Parameter 'fieldCount')");
    }
    auto r3 = AssemblyNameReference::Parse("x, Version=1.2.3");
    try {
        r3.FullName();
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 3. (Parameter 'fieldCount')");
    }
    // A signed one-component version is the same shape.
    auto rPlus = AssemblyNameReference::Parse("x, Version=+1.0");
    ASSERT_TRUE(rPlus.Version().has_value());
    EXPECT_EQ(*rPlus.Version(), Version(1, 0));
}

TEST(AssemblyNameReferenceParse, IgnoresUnknownKeysAndNeverSetsTheFlags)
{
    // The Parse switch has no case for retargetable/windowsruntime, so the
    // flags stay false even when the input spells them; unknown keys are
    // silently ignored.
    auto r = AssemblyNameReference::Parse("x, Retargetable=Yes, Version=1.0.0.0");
    EXPECT_FALSE(r.IsRetargetable());
    EXPECT_EQ(r.FullName(), "x, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");

    auto r2 = AssemblyNameReference::Parse("x, WindowsRuntime=true, Version=1.0.0.0");
    EXPECT_FALSE(r2.IsWindowsRuntime());

    // Empty keys (a lone '=' token) are ignored.
    auto r3 = AssemblyNameReference::Parse("x, =");
    EXPECT_EQ(r3.Name(), "x");
    EXPECT_FALSE(r3.Version().has_value());
    auto r4 = AssemblyNameReference::Parse("x, =1");
    EXPECT_FALSE(r4.Version().has_value());
}

TEST(AssemblyNameReferenceParse, KeyMatchIsCaseInsensitiveOnTheExactText)
{
    // version / VERSION / publickeytoken all fold onto their case.
    EXPECT_EQ(*AssemblyNameReference::Parse("x, version=1.0.0.0").Version(), Version(1, 0, 0, 0));
    EXPECT_EQ(*AssemblyNameReference::Parse("x, VERSION=1.0.0.0").Version(), Version(1, 0, 0, 0));
    ASSERT_TRUE(AssemblyNameReference::Parse("x, publickeytoken=B77A5C561934E089")
                   .PublicKeyToken()
                   ->size() == 8u);
    // A space inside the key ("Version = ...") keeps it from matching -- the
    // key is compared after folding, with no inner trim.
    EXPECT_FALSE(AssemblyNameReference::Parse("x, Version = 1.0.0.0").Version().has_value());
    // The value is NOT trimmed either, but the version parser tolerates the
    // whitespace itself.
    EXPECT_EQ(*AssemblyNameReference::Parse("x, Version= 1.0.0.0").Version(), Version(1, 0, 0, 0));
    // Whitespace around the whole key=value token is trimmed before the split.
    EXPECT_EQ(AssemblyNameReference::Parse("x , Version=1.0.0.0").Name(), "x");
    // A later duplicate key wins.
    EXPECT_EQ(*AssemblyNameReference::Parse("x, Version=1.0.0.0, Version=2.0.0.0").Version(),
        Version(2, 0, 0, 0));
}

TEST(AssemblyNameReferenceParse, CultureMatrix)
{
    // "neutral" (and an empty value) collapse to the empty culture, which the
    // FullName renders back as "neutral"; any other value passes through raw.
    auto neutral = AssemblyNameReference::Parse("x, Culture=neutral");
    ASSERT_TRUE(neutral.Culture().has_value());
    EXPECT_EQ(*neutral.Culture(), "");
    EXPECT_EQ(neutral.FullName(), "x, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");

    auto empty = AssemblyNameReference::Parse("x, Culture=");
    ASSERT_TRUE(empty.Culture().has_value());
    EXPECT_EQ(*empty.Culture(), "");

    auto de = AssemblyNameReference::Parse("x, Culture=de-DE");
    ASSERT_TRUE(de.Culture().has_value());
    EXPECT_EQ(*de.Culture(), "de-DE");
    EXPECT_EQ(de.FullName(), "x, Version=0.0.0.0, Culture=de-DE, PublicKeyToken=null");

    // A space INSIDE the value is kept (only the whole token gets trimmed).
    auto inner = AssemblyNameReference::Parse("x, Culture= neutral");
    ASSERT_TRUE(inner.Culture().has_value());
    EXPECT_EQ(*inner.Culture(), " neutral");
    EXPECT_EQ(inner.FullName(), "x, Version=0.0.0.0, Culture= neutral, PublicKeyToken=null");

    // A trailing space is part of the token trim, so "neutral " still matches.
    auto trailing = AssemblyNameReference::Parse("x, Culture=neutral ");
    ASSERT_TRUE(trailing.Culture().has_value());
    EXPECT_EQ(*trailing.Culture(), "");
}

TEST(AssemblyNameReferenceParse, PublicKeyTokenMatrix)
{
    // "null" keeps the token null.
    EXPECT_FALSE(AssemblyNameReference::Parse("x, PublicKeyToken=null").PublicKeyToken().has_value());

    auto pkt = AssemblyNameReference::Parse("x, PublicKeyToken=b77a5c561934e089");
    ASSERT_TRUE(pkt.PublicKeyToken().has_value());
    EXPECT_EQ(*pkt.PublicKeyToken(),
        Bytes({0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34, 0xe0, 0x89}));
    EXPECT_EQ(pkt.FullName(), "x, Version=0.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");

    // An odd length drops the trailing character (new byte[len / 2]).
    auto odd = AssemblyNameReference::Parse("x, PublicKeyToken=b77");
    ASSERT_TRUE(odd.PublicKeyToken().has_value());
    EXPECT_EQ(*odd.PublicKeyToken(), Bytes({0xb7}));
    EXPECT_EQ(odd.FullName(), "x, Version=0.0.0.0, Culture=neutral, PublicKeyToken=b7");

    // A one-hex-byte token.
    auto one = AssemblyNameReference::Parse("x, PublicKeyToken=b7");
    ASSERT_TRUE(one.PublicKeyToken().has_value());
    EXPECT_EQ(*one.PublicKeyToken(), Bytes({0xb7}));

    // An empty value is an EMPTY (found) array -- rendered "null" by FullName
    // because of the length check.
    auto empty = AssemblyNameReference::Parse("x, PublicKeyToken=");
    ASSERT_TRUE(empty.PublicKeyToken().has_value());
    EXPECT_EQ(empty.PublicKeyToken()->size(), 0u);
    EXPECT_EQ(empty.FullName(), "x, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");

    // Whitespace inside the two-character slice is tolerated by the hex parse.
    auto space = AssemblyNameReference::Parse("x, PublicKeyToken=0 1");
    ASSERT_TRUE(space.PublicKeyToken().has_value());
    EXPECT_EQ(*space.PublicKeyToken(), Bytes({0x00}));
    auto lead = AssemblyNameReference::Parse("x, PublicKeyToken= 1");
    ASSERT_TRUE(lead.PublicKeyToken().has_value());
    EXPECT_EQ(*lead.PublicKeyToken(), Bytes({0x01}));

    // A hex parse failure quotes the RAW two-character slice.
    try {
        AssemblyNameReference::Parse("x, PublicKeyToken=zz");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string 'zz' was not in a correct format.");
    }
    try {
        AssemblyNameReference::Parse("x, PublicKeyToken=nullX");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string 'nu' was not in a correct format.");
    }
    // Signs are NOT allowed by NumberStyles.HexNumber.
    try {
        AssemblyNameReference::Parse("x, PublicKeyToken=-0");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string '-0' was not in a correct format.");
    }
    try {
        AssemblyNameReference::Parse("x, PublicKeyToken=+01");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string '+0' was not in a correct format.");
    }
}

TEST(AssemblyNameReferenceParse, MalformedNameArms)
{
    // An empty input is the "Name can not be empty" arm.
    try {
        AssemblyNameReference::Parse("");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Name can not be empty");
    }
    // A non-first token without exactly one '=' is "Malformed name" -- including
    // the empty tokens a trailing/double comma produces.
    for (const char* input : { "a,b", "a,b=1,c", "x,", "x,,", "x, Version=1.0.0.0," }) {
        try {
            AssemblyNameReference::Parse(input);
            FAIL() << "expected throw for " << input;
        } catch (const std::invalid_argument& ex) {
            EXPECT_STREQ(ex.what(), "Malformed name");
        }
    }
}

TEST(AssemblyNameReferenceParse, VersionComponentFailures)
{
    // The version value delegates to the System.Version ctor, so its exact
    // exceptions surface through Parse.
    for (const char* input : { "x, Version=", "x, Version=1.2.3.4.5", "x, Version=abc" }) {
        try {
            AssemblyNameReference::Parse(input);
            FAIL() << "expected throw for " << input;
        } catch (const std::invalid_argument& ex) {
            EXPECT_STREQ(ex.what(),
                "Version string portion was too short or too long. (Parameter 'input')");
        }
    }
    try {
        AssemblyNameReference::Parse("x, Version=-1.0");
        FAIL() << "expected throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "input ('-1') must be a non-negative value. (Parameter 'input')\n"
            "Actual value was -1.");
    }
    try {
        AssemblyNameReference::Parse("x, Version=1..2");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string '' was not in a correct format.");
    }
    try {
        AssemblyNameReference::Parse("x, Version=2147483648.0");
        FAIL() << "expected throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Value was either too large or too small for an Int32.");
    }
}

TEST(AssemblyNameReferenceParse, EmptyNameIsAllowed)
{
    // Only the empty FULL NAME is rejected; a first token that trims to empty
    // parses with Name "" (and the FullName renders the leading ", ").
    auto r = AssemblyNameReference::Parse(", Version=1.0.0.0");
    EXPECT_EQ(r.Name(), "");
    EXPECT_EQ(r.FullName(), ", Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");

    auto blank = AssemblyNameReference::Parse(" ");
    EXPECT_EQ(blank.Name(), "");
    EXPECT_EQ(blank.FullName(), ", Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
}

TEST(AssemblyNameReference, DispatchesThroughTheInterface)
{
    // The BamlContext consumption: the parsed value read through the
    // IAssemblyReference base.
    auto r = AssemblyNameReference::Parse(
        "PresentationFramework, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=31bf3856ad364e35");
    const IAssemblyReference& ref = r;
    EXPECT_EQ(ref.Name(), "PresentationFramework");
    EXPECT_EQ(ref.FullName(),
        "PresentationFramework, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=31bf3856ad364e35");
    ASSERT_TRUE(ref.Version().has_value());
    EXPECT_EQ(ref.Version()->Major, 4);
    EXPECT_TRUE(ref.IsRetargetable() == false);
}

TEST(AssemblyNameReference, FullNameCachesItsRender)
{
    // The lazy fullName cache: the second read is the cached string.
    auto r = AssemblyNameReference::Parse("x, Version=1.0.0.0");
    std::string first = r.FullName();
    EXPECT_EQ(r.FullName(), first);
    // ToString is the FullName form.
    EXPECT_EQ(r.ToString(), first);
}

} // namespace
