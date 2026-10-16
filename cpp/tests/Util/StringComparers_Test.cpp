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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the .NET invariant-culture string collation (cpp/Decompiler/Util/
// StringComparers.{hpp,cpp}): the stringComparer<string>.Default /
// string.CompareTo semantics the C# SortByNameProcessor's OrderBy sorts with --
// linguistic (punctuation and digits sort before letters, letters compare
// case-insensitively first, then lowercase before uppercase), not ordinal.
// Every pinned pair was verified against .NET's StringComparer.InvariantCulture
// and the en-US current culture (they agree on all printable-ASCII cases).

#include "Decompiler/Util/StringComparers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

TEST(CompareInvariantCultureTest, PunctuationSortsBeforeLetters)
{
    // The SortMembers gold fixture's core case: '_'-prefixed field names sort
    // before letter-prefixed ones under the C# OrderBy (ordinal says the
    // opposite: '_' = 0x5F > 'T' = 0x54).
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "_testEnumArray", "TestBoxed"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("_", "T"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "get_X", "getx"), 0);
}

TEST(CompareInvariantCultureTest, DigitsSortAfterPunctuationBeforeLetters)
{
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("_", "2"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("2", "a"), 0);
    EXPECT_GT(ILSpy::Decompiler::Util::CompareInvariantCulture("x2", "x_2"), 0);
}

TEST(CompareInvariantCultureTest, GenericArityMarkerSortsBeforeDigits)
{
    // A generic method's sort key renders "Empty`1()": the backtick is
    // punctuation, so it sorts before both digits and letters.
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "Foo`1", "Foo1"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "Foo`1", "FooBar"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "Empty`1()", "EmptyX"), 0);
}

TEST(CompareInvariantCultureTest, LettersCompareCaseInsensitivelyThenLowercaseFirst)
{
    // Primary weights fold case, so "Zebra" sorts AFTER "apple" (a < z);
    // the case tiebreak puts lowercase first ("a" < "A", "abc" < "ABC").
    EXPECT_GT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "Zebra", "apple"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("a", "A"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("abc", "ABC"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("abc", "aBc"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("aBc", "AbC"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "get_Chars", "GetHashCode"), 0);
}

TEST(CompareInvariantCultureTest, PunctuationInternalOrderMatchesTheDotnetTable)
{
    // The probed .NET invariant single-character order:
    // _ - , ; : ! ? . ' " ( ) [ ] { } @ * / \ & # % ` ^ + < = > | ~ $ 0-9 a-z.
    const char* ordered[] = { "_", "-", ",", ";", ":", "!", "?", ".", "'",
        "\"", "(", ")", "[", "]", "{", "}", "@", "*", "/", "\\", "&", "#",
        "%", "`", "^", "+", "<", "=", ">", "|", "~", "$" };
    for (std::size_t i = 1; i < sizeof(ordered) / sizeof(ordered[0]); i++) {
        EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                      ordered[i - 1], ordered[i]), 0)
            << "expected " << ordered[i - 1] << " < " << ordered[i];
    }
}

TEST(CompareInvariantCultureTest, PrefixShorterSortsFirst)
{
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "System.IList", "System.IList`1"), 0);
    EXPECT_GT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "Empty`1()", "Empty"), 0);
}

TEST(CompareInvariantCultureTest, EqualAndReflexivePairs)
{
    EXPECT_EQ(ILSpy::Decompiler::Util::CompareInvariantCulture("abc", "abc"), 0);
    EXPECT_EQ(ILSpy::Decompiler::Util::CompareInvariantCulture("", ""), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("", "a"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture("a", "ab"), 0);
}

TEST(CompareInvariantCultureTest, ParameterListPunctuationIsComparedPrimary)
{
    // Method sort keys render the parameter list "(types)": the '(' before a
    // letter, and nested type names use '/' -- all compared at primary level.
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "x(y)", "xz"), 0);
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "op_Equality(object,object)", "op_Equality(string,string)"), 0);
    EXPECT_GT(ILSpy::Decompiler::Util::CompareInvariantCulture("()", "(_"), 0);
}

TEST(CompareInvariantCultureTest, DigitRunsCompareCharacterwiseNotNumerically)
{
    // "Foo`10" < "Foo`2" under .NET (no numeric collation).
    EXPECT_LT(ILSpy::Decompiler::Util::CompareInvariantCulture(
                  "Foo`10", "Foo`2"), 0);
}

TEST(CompareInvariantCultureTest, SortsTheGoldFixtureFieldSetLikeDotnet)
{
    // The SortMembers.expected.il gold order (the C# test harness generated
    // it with SortByNameProcessor): '_'-fields first, then the letter-fields.
    std::vector<std::string> names = { "TestInt32", "_testInt32Array",
        "TestBoxedArray", "_testEnumArray", "TestType", "_testStringArray",
        "TestBoxed", "TestString", "TestBoxedType", "_testTypeArray",
        "TestEnumType", "TestBoxed2", "TestBoxedString" };
    std::stable_sort(names.begin(), names.end(),
        [](const std::string& a, const std::string& b) {
            return ILSpy::Decompiler::Util::CompareInvariantCulture(a, b) < 0;
        });
    const std::vector<std::string> expected = { "_testEnumArray",
        "_testInt32Array", "_testStringArray", "_testTypeArray", "TestBoxed",
        "TestBoxed2", "TestBoxedArray", "TestBoxedString", "TestBoxedType",
        "TestEnumType", "TestInt32", "TestString", "TestType" };
    ASSERT_EQ(names.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(names[i], expected[i]) << "position " << i;
    }
}

}  // namespace
