// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Version` (cpp/Decompiler/TypeSystem/Version.hpp, the port of the BCL
// `System.Version` value type). `Version` is the type `IModule.AssemblyVersion`
// returns by value, a leaf dependency of `IModule` (the next TypeSystem interface
// toward `ICompilation` / `TypeSystemAstBuilder` / `CSharpAmbience`). The tests pin
// the four component fields (Major/Minor/Build/Revision), the `-1`-sentinel
// convention for unspecified Build/Revision (matching `System.Version`), the
// per-arity constructors, the field-wise equality, and the `ToString` shape that
// mirrors `System.Version.ToString` ("Major.Minor" with ".Build"/".Revision"
// appended when each is specified).
//
// The `VersionStringCtor` / `VersionToStringFieldCount` suites below cover the
// two members `Metadata::AssemblyNameReference` consumes (the `Version.cpp`
// pair): the `System.Version(String)` ctor -- two to four `.`-separated
// components, each parsed with the .NET number-parser `NumberStyles.Integer`
// shape (leading/trailing whitespace and an optional sign), with the exact
// exception messages -- and the `ToString(int fieldCount)` render with its
// bounds checks. Every expectation was dumped from the real .NET 10 runtime
// (the C:\temp-probe\AnrProbe matrix).

#include "Decompiler/TypeSystem/Version.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// The default-constructed `Version` matches `new System.Version()`: Major=0,
// Minor=0, Build=-1, Revision=-1. `Build`/`Revision` are `-1` (unspecified), the
// `System.Version` sentinel, so a default-constructed `AssemblyVersion` formats
// as "0.0".
// ---------------------------------------------------------------------------
TEST(VersionTest, DefaultCtorIsZeroMajorMinorAndUnspecifiedBuildRevision)
{
    TS::Version v;
    EXPECT_EQ(v.Major, 0);
    EXPECT_EQ(v.Minor, 0);
    EXPECT_EQ(v.Build, -1);
    EXPECT_EQ(v.Revision, -1);
}

// ---------------------------------------------------------------------------
// `Version(major, minor)` mirrors `new System.Version(int, int)`: Build and
// Revision stay unspecified (-1).
// ---------------------------------------------------------------------------
TEST(VersionTest, TwoArgCtorSetsMajorMinorAndLeavesBuildRevisionUnspecified)
{
    TS::Version v(4, 8);
    EXPECT_EQ(v.Major, 4);
    EXPECT_EQ(v.Minor, 8);
    EXPECT_EQ(v.Build, -1);
    EXPECT_EQ(v.Revision, -1);
}

// ---------------------------------------------------------------------------
// `Version(major, minor, build)` mirrors `new System.Version(int, int, int)`:
// Revision stays unspecified (-1).
// ---------------------------------------------------------------------------
TEST(VersionTest, ThreeArgCtorSetsMajorMinorBuildAndLeavesRevisionUnspecified)
{
    TS::Version v(4, 8, 0);
    EXPECT_EQ(v.Major, 4);
    EXPECT_EQ(v.Minor, 8);
    EXPECT_EQ(v.Build, 0);
    EXPECT_EQ(v.Revision, -1);
}

// ---------------------------------------------------------------------------
// `Version(major, minor, build, revision)` mirrors the four-argument
// `System.Version` ctor: all four components are set.
// ---------------------------------------------------------------------------
TEST(VersionTest, FourArgCtorSetsAllComponents)
{
    TS::Version v(2, 0, 1, 9);
    EXPECT_EQ(v.Major, 2);
    EXPECT_EQ(v.Minor, 0);
    EXPECT_EQ(v.Build, 1);
    EXPECT_EQ(v.Revision, 9);
}

// ---------------------------------------------------------------------------
// Equality is field-wise across all four components; any single differing
// component makes two versions unequal. This is the comparison `MetadataModule`
// would use to detect a configured `AssemblyVersion`, and the shape
// `TypeSystemAstBuilder` / `CSharpAmbience` rely on for value semantics.
// ---------------------------------------------------------------------------
TEST(VersionTest, EqualityComparesAllFourComponents)
{
    EXPECT_TRUE(TS::Version(1, 2, 3, 4) == TS::Version(1, 2, 3, 4));
    EXPECT_FALSE(TS::Version(1, 2, 3, 4) != TS::Version(1, 2, 3, 4));
    EXPECT_FALSE(TS::Version(1, 2, 3, 4) == TS::Version(1, 2, 3, 9));
    EXPECT_FALSE(TS::Version(1, 2, 3, 4) == TS::Version(1, 2, 9, 4));
    EXPECT_FALSE(TS::Version(1, 2, 3, 4) == TS::Version(1, 9, 3, 4));
    EXPECT_FALSE(TS::Version(1, 2, 3, 4) == TS::Version(9, 2, 3, 4));
    // Unspecified Build/Revision (-1) is a real value compared by equality.
    EXPECT_TRUE(TS::Version(1, 2) == TS::Version(1, 2, -1, -1));
    EXPECT_FALSE(TS::Version(1, 2) == TS::Version(1, 2, 0, -1));
}

// ---------------------------------------------------------------------------
// `ToString` mirrors `System.Version.ToString`: when `Build` is unspecified
// (-1) the format is "Major.Minor" -- the two-argument and default-constructed
// cases. `MetadataModule` formats `assembly.Version.ToString()` into an
// `[AssemblyVersion("...")]` attribute, so the shape is load-bearing.
// ---------------------------------------------------------------------------
TEST(VersionTest, ToStringFormatsMajorMinorOnlyWhenBuildUnspecified)
{
    EXPECT_EQ(TS::Version(1, 2).ToString(), "1.2");
    EXPECT_EQ(TS::Version().ToString(), "0.0");
    EXPECT_EQ(TS::Version(4, 8).ToString(), "4.8");
}

// ---------------------------------------------------------------------------
// When `Build` is specified but `Revision` is unspecified, `ToString` appends
// only ".Build": "Major.Minor.Build".
// ---------------------------------------------------------------------------
TEST(VersionTest, ToStringIncludesBuildWhenSpecifiedButRevisionUnspecified)
{
    EXPECT_EQ(TS::Version(1, 2, 3).ToString(), "1.2.3");
    EXPECT_EQ(TS::Version(4, 8, 0).ToString(), "4.8.0");
}

// ---------------------------------------------------------------------------
// When both `Build` and `Revision` are specified, `ToString` appends both:
// "Major.Minor.Build.Revision".
// ---------------------------------------------------------------------------
TEST(VersionTest, ToStringIncludesBuildAndRevisionWhenBothSpecified)
{
    EXPECT_EQ(TS::Version(1, 2, 3, 4).ToString(), "1.2.3.4");
    EXPECT_EQ(TS::Version(2, 0, 1, 9).ToString(), "2.0.1.9");
}

// ---------------------------------------------------------------------------
// `Version(String)` parses "major.minor[.build[.revision]]" -- two to four
// `.`-separated components.
// ---------------------------------------------------------------------------
TEST(VersionStringCtor, ParsesTwoToFourComponents)
{
    EXPECT_EQ(TS::Version("4.0.0.0"), TS::Version(4, 0, 0, 0));
    EXPECT_EQ(TS::Version("1.2"), TS::Version(1, 2));
    EXPECT_EQ(TS::Version("1.2.3"), TS::Version(1, 2, 3));
    EXPECT_EQ(TS::Version("1.2.3.4"), TS::Version(1, 2, 3, 4));
    // Unspecified components are -1.
    EXPECT_EQ(TS::Version("1.2").Build, -1);
    EXPECT_EQ(TS::Version("1.2").Revision, -1);
    EXPECT_EQ(TS::Version("1.2.3").Revision, -1);
}

// ---------------------------------------------------------------------------
// Each component is parsed with the .NET number-parser `NumberStyles.Integer`
// shape: leading/trailing whitespace and an optional sign per component (a
// negative zero parses as 0 and passes the non-negative check).
// ---------------------------------------------------------------------------
TEST(VersionStringCtor, ToleratesWhitespaceAndSignsPerComponent)
{
    EXPECT_EQ(TS::Version(" 1.0"), TS::Version(1, 0));
    EXPECT_EQ(TS::Version("1.0 "), TS::Version(1, 0));
    EXPECT_EQ(TS::Version("1 .0"), TS::Version(1, 0));
    EXPECT_EQ(TS::Version("1. +0"), TS::Version(1, 0));
    EXPECT_EQ(TS::Version("+1.0.0.0"), TS::Version(1, 0, 0, 0));
    EXPECT_EQ(TS::Version("-0.1"), TS::Version(0, 1));
    // Leading zeros fold away.
    EXPECT_EQ(TS::Version("01.02.03.04"), TS::Version(1, 2, 3, 4));
}

// ---------------------------------------------------------------------------
// Fewer than two or more than four components is the "too short or too long"
// ArgumentException -- including a single non-numeric token, which splits to
// one component before any digit is examined.
// ---------------------------------------------------------------------------
TEST(VersionStringCtor, RejectsWrongComponentCounts)
{
    try {
        TS::Version("1");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Version string portion was too short or too long. (Parameter 'input')");
    }
    try {
        TS::Version("1.2.3.4.5");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Version string portion was too short or too long. (Parameter 'input')");
    }
    try {
        TS::Version("abc");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Version string portion was too short or too long. (Parameter 'input')");
    }
    try {
        TS::Version(std::string{});
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Version string portion was too short or too long. (Parameter 'input')");
    }
}

// ---------------------------------------------------------------------------
// A non-numeric component quotes the RAW component in the FormatException
// message; a component above int32 is the OverflowException; a negative
// component is the ThrowIfNegative ArgumentOutOfRangeException naming the
// component ("input" for major/minor, "build", "revision") and appending the
// actual value on a second line.
// ---------------------------------------------------------------------------
TEST(VersionStringCtor, RejectsBadComponents)
{
    try {
        TS::Version("1..2");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string '' was not in a correct format.");
    }
    try {
        TS::Version("1.x.3");
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "The input string 'x' was not in a correct format.");
    }
    try {
        TS::Version("2147483648.0");
        FAIL() << "expected throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Value was either too large or too small for an Int32.");
    }
    try {
        TS::Version("-1.0");
        FAIL() << "expected throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "input ('-1') must be a non-negative value. (Parameter 'input')\n"
            "Actual value was -1.");
    }
    try {
        TS::Version("1.0.-2");
        FAIL() << "expected throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "build ('-2') must be a non-negative value. (Parameter 'build')\n"
            "Actual value was -2.");
    }
    try {
        TS::Version("1.0.0.-3");
        FAIL() << "expected throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "revision ('-3') must be a non-negative value. (Parameter 'revision')\n"
            "Actual value was -3.");
    }
}

// ---------------------------------------------------------------------------
// `ToString(fieldCount)` renders exactly that many components joined with '.'.
// ---------------------------------------------------------------------------
TEST(VersionToStringFieldCount, RendersTheRequestedComponentCount)
{
    TS::Version v(0, 0, 0, 0);
    EXPECT_EQ(v.ToString(0), "");
    EXPECT_EQ(v.ToString(1), "0");
    EXPECT_EQ(v.ToString(2), "0.0");
    EXPECT_EQ(v.ToString(3), "0.0.0");
    EXPECT_EQ(v.ToString(4), "0.0.0.0");
}

// ---------------------------------------------------------------------------
// `ToString(fieldCount)` bounds: outside [0, 4] throws "between 0 and 4"; a
// count beyond the specified components throws with the specified bound (2
// when Build is unspecified, 3 when Revision is).
// ---------------------------------------------------------------------------
TEST(VersionToStringFieldCount, BoundsChecks)
{
    TS::Version v(0, 0, 0, 0);
    try {
        v.ToString(-1);
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 4. (Parameter 'fieldCount')");
    }
    try {
        v.ToString(5);
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 4. (Parameter 'fieldCount')");
    }
    try {
        TS::Version(1, 2).ToString(3);
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 2. (Parameter 'fieldCount')");
    }
    try {
        TS::Version(1, 2).ToString(4);
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 2. (Parameter 'fieldCount')");
    }
    try {
        TS::Version(1, 2, 3).ToString(4);
        FAIL() << "expected throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Argument must be between 0 and 3. (Parameter 'fieldCount')");
    }
}

// ---------------------------------------------------------------------------
// The parameterless `ToString()` keeps its DefaultFormatFieldCount form.
// ---------------------------------------------------------------------------
TEST(VersionToStringFieldCount, DefaultToStringUnchanged)
{
    EXPECT_EQ(TS::Version(1, 2).ToString(), "1.2");
    EXPECT_EQ(TS::Version(1, 2, 3).ToString(), "1.2.3");
    EXPECT_EQ(TS::Version(1, 2, 3, 4).ToString(), "1.2.3.4");
}
