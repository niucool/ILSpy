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

#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include <gtest/gtest.h>

#include <string>

using ILSpy::Decompiler::CSharp::Syntax::TextLocation;

// ---- Construction & constants -------------------------------------------

TEST(CSharp_TextLocation, DefaultConstructorIsEmpty) {
    TextLocation loc;
    EXPECT_EQ(loc.Line, 0);
    EXPECT_EQ(loc.Column, 0);
    EXPECT_TRUE(loc.IsEmpty());
}

TEST(CSharp_TextLocation, EmptyIsZeroZero) {
    EXPECT_EQ(TextLocation::Empty.Line, 0);
    EXPECT_EQ(TextLocation::Empty.Column, 0);
    EXPECT_TRUE(TextLocation::Empty.IsEmpty());
}

TEST(CSharp_TextLocation, MinLineAndMinColumnAreOne) {
    EXPECT_EQ(TextLocation::MinLine, 1);
    EXPECT_EQ(TextLocation::MinColumn, 1);
}

TEST(CSharp_TextLocation, ConstructorRecordsLineAndColumn) {
    TextLocation loc(3, 7);
    EXPECT_EQ(loc.Line, 3);
    EXPECT_EQ(loc.Column, 7);
    EXPECT_FALSE(loc.IsEmpty());
}

TEST(CSharp_TextLocation, OneBasedLocationIsNotEmpty) {
    TextLocation loc(TextLocation::MinLine, TextLocation::MinColumn);
    EXPECT_FALSE(loc.IsEmpty());
}

// ---- ToString ------------------------------------------------------------

TEST(CSharp_TextLocation, ToStringFormatsLineThenColumn) {
    // The C# format string is "(Line {1}, Col {0})", so Line before Col.
    TextLocation loc(3, 7);
    EXPECT_EQ(loc.ToString(), "(Line 3, Col 7)");
}

TEST(CSharp_TextLocation, ToStringEmpty) {
    EXPECT_EQ(TextLocation::Empty.ToString(), "(Line 0, Col 0)");
}

// ---- Equality ------------------------------------------------------------

TEST(CSharp_TextLocation, EqualLocationsAreEqual) {
    EXPECT_TRUE(TextLocation(2, 5) == TextLocation(2, 5));
    EXPECT_FALSE(TextLocation(2, 5) != TextLocation(2, 5));
}

TEST(CSharp_TextLocation, DifferentColumnNotEqual) {
    EXPECT_FALSE(TextLocation(2, 5) == TextLocation(2, 6));
    EXPECT_TRUE(TextLocation(2, 5) != TextLocation(2, 6));
}

TEST(CSharp_TextLocation, DifferentLineNotEqual) {
    EXPECT_FALSE(TextLocation(2, 5) == TextLocation(3, 5));
    EXPECT_TRUE(TextLocation(2, 5) != TextLocation(3, 5));
}

// ---- Ordering (line-major, then column) ---------------------------------

TEST(CSharp_TextLocation, LessThanLineFirst) {
    EXPECT_TRUE(TextLocation(2, 9) < TextLocation(3, 1));
}

TEST(CSharp_TextLocation, LessThanSameLineColumnBreaksTie) {
    EXPECT_TRUE(TextLocation(3, 4) < TextLocation(3, 5));
    EXPECT_FALSE(TextLocation(3, 5) < TextLocation(3, 4));
    EXPECT_FALSE(TextLocation(3, 5) < TextLocation(3, 5));
}

TEST(CSharp_TextLocation, GreaterThan) {
    EXPECT_TRUE(TextLocation(3, 1) > TextLocation(2, 9));
    EXPECT_TRUE(TextLocation(3, 5) > TextLocation(3, 4));
    EXPECT_FALSE(TextLocation(3, 4) > TextLocation(3, 5));
    EXPECT_FALSE(TextLocation(3, 5) > TextLocation(3, 5));
}

TEST(CSharp_TextLocation, LessThanOrEqual) {
    EXPECT_TRUE(TextLocation(2, 9) <= TextLocation(3, 1));
    EXPECT_TRUE(TextLocation(3, 5) <= TextLocation(3, 5));
    EXPECT_FALSE(TextLocation(3, 1) <= TextLocation(2, 9));
}

TEST(CSharp_TextLocation, GreaterThanOrEqual) {
    EXPECT_TRUE(TextLocation(3, 1) >= TextLocation(2, 9));
    EXPECT_TRUE(TextLocation(3, 5) >= TextLocation(3, 5));
    EXPECT_FALSE(TextLocation(2, 9) >= TextLocation(3, 1));
}

// ---- CompareTo -----------------------------------------------------------

TEST(CSharp_TextLocation, CompareToEqual) {
    EXPECT_EQ(TextLocation(3, 7).CompareTo(TextLocation(3, 7)), 0);
}

TEST(CSharp_TextLocation, CompareToLess) {
    EXPECT_EQ(TextLocation(3, 1).CompareTo(TextLocation(5, 9)), -1);
}

TEST(CSharp_TextLocation, CompareToGreater) {
    EXPECT_EQ(TextLocation(9, 9).CompareTo(TextLocation(5, 1)), 1);
}

// ---- GetHashCode --------------------------------------------------------

TEST(CSharp_TextLocation, GetHashCodeIsStableAndDistinct) {
    TextLocation a(3, 7);
    EXPECT_EQ(a.GetHashCode(), a.GetHashCode());
    // Different locations should (as a quality matter) usually hash differently;
    // this just guards the obvious degenerate case for these small values.
    EXPECT_NE(TextLocation(7, 3).GetHashCode(), TextLocation(3, 7).GetHashCode());
}
