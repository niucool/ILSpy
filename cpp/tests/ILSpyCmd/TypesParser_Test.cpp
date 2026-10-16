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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// The TypesParser port (the -l/--list option's entity-type selection):
// ParseSelection (the C# TypesParser.ParseSelection matrix -- the
// kind-character soup vs the spelled-out key trim) and
// SplitEntityTypeValues (the C# OnExecuteAsync
// `EntityTypes.SelectMany(v => v.Split(',', ';'))` -- the split whose
// empty entries gate the character-soup path). Every expectation was
// probed against the real ilspycmd 11.0 tool (`-l <value>` over the
// mscorlib fixture): the value matrix below reproduces the tool's
// observable behavior pair for pair.

#include "ILSpyCmd/TypesParser.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::ILSpyCmd::ParseSelection;
using ILSpy::ILSpyCmd::SplitEntityTypeValues;

// Renders the selected kinds as a char string in the fixed order
// class/interface/struct/delegate/enum (readable failure diffs; 'V' marks
// the Void kind nothing in these tests can select).
std::string KindChars(const std::set<TypeKind>& kinds) {
    std::string r;
    if (kinds.count(TypeKind::Class)) r += 'c';
    if (kinds.count(TypeKind::Interface)) r += 'i';
    if (kinds.count(TypeKind::Struct)) r += 's';
    if (kinds.count(TypeKind::Delegate)) r += 'd';
    if (kinds.count(TypeKind::Enum)) r += 'e';
    if (kinds.count(TypeKind::Void)) r += 'V';
    return r;
}

std::set<TypeKind> Parse(const std::vector<std::string>& values) {
    return ParseSelection(values);
}

std::string Parsed(const std::vector<std::string>& values) {
    return KindChars(ParseSelection(values));
}

}  // namespace

// ---- ParseSelection: the single-value kind-character soup ----

// The C# `values.Length == 1 && !possibleValues.Keys.Any(v => values[0].StartsWith(v, OrdinalIgnoreCase))`
// arm: a single value that does not BEGIN with a key is a soup of
// kind characters, matched case-SENSITIVELY (so "C" selects nothing).
TEST(TypesParserTest, SingleValueIsKindCharacterSoup) {
    EXPECT_EQ(Parsed({"c"}), "c");
    EXPECT_EQ(Parsed({"i"}), "i");
    EXPECT_EQ(Parsed({"s"}), "s");
    EXPECT_EQ(Parsed({"d"}), "d");
    EXPECT_EQ(Parsed({"e"}), "e");
    EXPECT_EQ(Parsed({"cis"}), "cis");
    EXPECT_EQ(Parsed({"cisde"}), "cisde");
    EXPECT_EQ(Parsed({"de"}), "de");
    // A single letter pair that is not a key prefix is still character
    // soup: "en" selects Enum ('n' is ignored).
    EXPECT_EQ(Parsed({"en"}), "e");
    EXPECT_EQ(Parsed({"intf"}), "i");
    // Unknown characters are ignored.
    EXPECT_EQ(Parsed({"foo"}), "");
    EXPECT_EQ(Parsed({"x"}), "");
    EXPECT_EQ(Parsed({""}), "");
    // The character match is case-sensitive: uppercase selects nothing.
    EXPECT_EQ(Parsed({"C"}), "");
    EXPECT_EQ(Parsed({"CL"}), "");
}

// ---- ParseSelection: the spelled-out keys ----

// The else arm: a single value that BEGINS with a key (or several values)
// is trimmed from the end until it is a key; the dictionary is
// case-insensitive ("Class" and "CLASS" select Class), an unknown value
// trims past its start and is silently ignored.
TEST(TypesParserTest, SpelledOutKeysAreCaseInsensitivePrefixTrims) {
    EXPECT_EQ(Parsed({"class"}), "c");
    EXPECT_EQ(Parsed({"struct"}), "s");
    EXPECT_EQ(Parsed({"interface"}), "i");
    EXPECT_EQ(Parsed({"enum"}), "e");
    EXPECT_EQ(Parsed({"delegate"}), "d");
    // Case-insensitive keys ("Class" reaches the else arm through the
    // StartsWith gate and matches the dictionary).
    EXPECT_EQ(Parsed({"Class"}), "c");
    EXPECT_EQ(Parsed({"ENUM"}), "e");
    // A trailing suffix is trimmed away character by character.
    EXPECT_EQ(Parsed({"classes"}), "c");
    EXPECT_EQ(Parsed({"classX"}), "c");
    EXPECT_EQ(Parsed({"classe"}), "c");
    EXPECT_EQ(Parsed({"structs"}), "s");
    EXPECT_EQ(Parsed({"interfaces"}), "i");
    EXPECT_EQ(Parsed({"enums"}), "e");
    EXPECT_EQ(Parsed({"delegates"}), "d");
}

// ---- ParseSelection: several values never take the character path ----

// The `values.Length == 1` gate: with two or more entries every value goes
// through the key trim -- and the bare kind letters are NOT keys, so
// {"c", "i"} selects nothing while {"class", "struct"} selects both.
TEST(TypesParserTest, MultipleValuesNeverTakeTheCharacterPath) {
    EXPECT_EQ(Parsed({"c", "i"}), "");
    EXPECT_EQ(Parsed({"class", "i"}), "c");
    EXPECT_EQ(Parsed({"class", "struct"}), "cs");
    EXPECT_EQ(Parsed({"c", "class"}), "c");
    EXPECT_EQ(Parsed({"", ""}), "");
    EXPECT_EQ(Parsed(std::vector<std::string>{}), "");
}

// ---- SplitEntityTypeValues: the ','/';' option-value split ----

// The C# `v.Split(',', ';')`: both delimiters split at once, consecutive
// and trailing delimiters yield EMPTY entries (StringSplitOptions.None),
// and the entries flatten across occurrences in order.
TEST(TypesParserTest, SplitEntityTypeValuesSplitsOnCommaAndSemicolon) {
    EXPECT_EQ(SplitEntityTypeValues({"c,is"}), (std::vector<std::string>{"c", "is"}));
    EXPECT_EQ(SplitEntityTypeValues({"c;i"}), (std::vector<std::string>{"c", "i"}));
    EXPECT_EQ(SplitEntityTypeValues({"c,;i"}), (std::vector<std::string>{"c", "", "i"}));
    EXPECT_EQ(SplitEntityTypeValues({"c,,i"}), (std::vector<std::string>{"c", "", "i"}));
    EXPECT_EQ(SplitEntityTypeValues({"c,"}), (std::vector<std::string>{"c", ""}));
    EXPECT_EQ(SplitEntityTypeValues({",c"}), (std::vector<std::string>{"", "c"}));
    EXPECT_EQ(SplitEntityTypeValues({"cis"}), (std::vector<std::string>{"cis"}));
    EXPECT_EQ(SplitEntityTypeValues({""}), (std::vector<std::string>{""}));
    EXPECT_EQ(SplitEntityTypeValues({"a,b", "c"}),
        (std::vector<std::string>{"a", "b", "c"}));
}

// ---- The CLI composition: split first, then parse ----

// The C# OnExecuteAsync composition `TypesParser.ParseSelection(values)`
// over the split values -- the split is load-bearing: "class,i" is TWO
// entries (the key arm, "i" ignored), "cis" is ONE (the character soup),
// and the trailing delimiter's empty entry pushes "c," off the
// single-value path.
TEST(TypesParserTest, SplitThenParseIsTheCliComposition) {
    EXPECT_EQ(KindChars(ParseSelection(SplitEntityTypeValues({"class,i"}))), "c");
    EXPECT_EQ(KindChars(ParseSelection(SplitEntityTypeValues({"cis"}))), "cis");
    EXPECT_EQ(KindChars(ParseSelection(SplitEntityTypeValues({"c;i"}))), "");
    EXPECT_EQ(KindChars(ParseSelection(SplitEntityTypeValues({"c,"}))), "");
    EXPECT_EQ(KindChars(ParseSelection(SplitEntityTypeValues({"class", "struct"}))), "cs");
    EXPECT_EQ(KindChars(ParseSelection(SplitEntityTypeValues({"classes"}))), "c");
}
