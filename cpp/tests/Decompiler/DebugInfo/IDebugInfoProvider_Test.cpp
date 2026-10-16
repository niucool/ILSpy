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

// Tests for the debug-info provider contract (cpp/Decompiler/DebugInfo/):
// the SequencePoint data holder (the 0xfeefee hidden-marker rules) and the
// Variable/PdbExtraTypeInfo structs. The provider-driven rendering lives in
// MethodBodyDisassembler_Test.cpp (the ShowSequencePoints lines and the
// .locals debug names); these tests pin the struct semantics the C# source
// defines inline.

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"

#include <gtest/gtest.h>

namespace DI = ILSpy::Decompiler::DebugInfo;

TEST(SequencePointTest, IsHiddenMarksTheFeefeeMarker) {
    DI::SequencePoint sp;
    sp.StartLine = 0xfeefee;
    sp.EndLine = 0xfeefee;
    EXPECT_TRUE(sp.IsHidden());
    // Hidden requires both lines to be the marker on the SAME line: a
    // 0xfeefee-to-0xfeef0 range is not hidden.
    DI::SequencePoint range;
    range.StartLine = 0xfeefee;
    range.EndLine = 0xfeef0;
    EXPECT_FALSE(range.IsHidden());
    DI::SequencePoint normal;
    normal.StartLine = 10;
    normal.EndLine = 10;
    EXPECT_FALSE(normal.IsHidden());
}

TEST(SequencePointTest, SetHiddenMarksBothLines) {
    DI::SequencePoint sp;
    sp.StartLine = 10;
    sp.EndLine = 20;
    EXPECT_FALSE(sp.IsHidden());
    sp.SetHidden();
    EXPECT_TRUE(sp.IsHidden());
    EXPECT_EQ(sp.StartLine, 0xfeefee);
    EXPECT_EQ(sp.EndLine, 0xfeefee);
}

TEST(VariableTest, CarriesIndexAndName) {
    DI::Variable v(2, "values");
    EXPECT_EQ(v.Index, 2);
    EXPECT_EQ(v.Name, "values");
    DI::Variable def;
    EXPECT_EQ(def.Index, 0);
    EXPECT_TRUE(def.Name.empty());
}

TEST(PdbExtraTypeInfoTest, DefaultsEmpty) {
    DI::PdbExtraTypeInfo info;
    EXPECT_FALSE(info.TupleElementNames.has_value());
    EXPECT_FALSE(info.DynamicFlags.has_value());
    info.TupleElementNames = std::vector<std::string>{ "Item1", "Item2" };
    info.DynamicFlags = std::vector<bool>{ true, false, true };
    EXPECT_EQ(info.TupleElementNames->size(), 2u);
    EXPECT_EQ(info.DynamicFlags->size(), 3u);
    EXPECT_TRUE((*info.DynamicFlags)[2]);
    // The C# fields are nullable arrays: an engaged optional holding an
    // empty vector is a found result (a decode that produced nothing),
    // distinct from the disengaged not-found state.
    DI::PdbExtraTypeInfo found;
    found.TupleElementNames = std::vector<std::string>{};
    found.DynamicFlags = std::vector<bool>{};
    EXPECT_TRUE(found.TupleElementNames.has_value());
    EXPECT_TRUE(found.TupleElementNames->empty());
    EXPECT_TRUE(found.DynamicFlags.has_value());
    EXPECT_TRUE(found.DynamicFlags->empty());
}
