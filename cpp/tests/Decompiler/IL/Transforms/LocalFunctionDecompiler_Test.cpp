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

// Tests for LocalFunctionDecompiler's ParseLocalFunctionName (the C#
// `^<(.*)>g__([^\|]*)\|{0,1}\d+(_\d+)?$` regex split): the accept shapes
// (plain, ordinal, `_<n>` suffix, greedy caller-name), the reject shapes
// (no `>g__`, empty function name, missing ordinal, trailing junk), and the
// settings gate.

#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

TEST(LocalFunctionDecompilerTest, ParsesPlainLocalFunctionName)
{
    std::string caller, fn;
    EXPECT_TRUE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "<M>g__Helper|0_0", caller, fn));
    EXPECT_EQ(caller, "M");
    EXPECT_EQ(fn, "Helper");
}

TEST(LocalFunctionDecompilerTest, ParsesOrdinalWithoutSuffix)
{
    std::string caller, fn;
    EXPECT_TRUE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "<M>g__Local|1", caller, fn));
    EXPECT_EQ(caller, "M");
    EXPECT_EQ(fn, "Local");
}

TEST(LocalFunctionDecompilerTest, GreedyCallerNameTakesTheLastGMarker)
{
    // The regex `(.*)` is greedy: a caller name containing `>g__` (nested
    // display-class names) takes the LAST marker.
    std::string caller, fn;
    EXPECT_TRUE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "<A>g__Outer>g__Inner|0_1", caller, fn));
    EXPECT_EQ(caller, "A>g__Outer");
    EXPECT_EQ(fn, "Inner");
}

TEST(LocalFunctionDecompilerTest, RejectsNonLocalFunctionNames)
{
    std::string caller, fn;
    EXPECT_FALSE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "RegularMethodName", caller, fn));
    EXPECT_FALSE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "<M>g__|0", caller, fn)) << "an empty function name is rejected";
    EXPECT_FALSE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "<M>g__Helper", caller, fn)) << "a missing ordinal is rejected";
    EXPECT_FALSE(IL::LocalFunctionDecompiler::ParseLocalFunctionName(
        "<M>g__Helper|0x", caller, fn)) << "trailing junk is rejected";
}

TEST(LocalFunctionDecompilerTest, SettingsGateShapeIsExercised)
{
    IL::ILFunction fn;
    IL::ILTransformContext ctx;
    ctx.Settings.LocalFunctions = true;
    IL::LocalFunctionDecompiler transform;
    transform.Run(fn, ctx);
    SUCCEED() << "the gate shape is exercised without a crash";
}
