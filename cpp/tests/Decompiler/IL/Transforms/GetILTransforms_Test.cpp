// Copyright (c) 2026 Jim Hester
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
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

// Tests for the GetILTransforms() list factory (the C#
// `public static List<IILTransform> GetILTransforms()` -- CSharpDecompiler.cs
// line 89, ported as the IL-namespace list the Phase-7 facade adopts): the
// list is fresh-instance (a second call yields distinct objects), the
// pipeline head matches the C# order (ControlFlowSimplification first), and
// the late-pipeline ordering pins (SwitchOnString before SwitchOnNullable;
// the StatementTransform entry with its interleaved children present).

#include "Decompiler/IL/Transforms/GetILTransforms.hpp"

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;

TEST(GetILTransformsTest, ListIsFreshInstancePerCall)
{
    auto first = IL::GetILTransforms();
    auto second = IL::GetILTransforms();
    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first.size(), second.size());
    EXPECT_NE(first[0].get(), second[0].get())
        << "each call returns fresh transform instances";
}

TEST(GetILTransformsTest, PipelineHeadIsControlFlowSimplification)
{
    auto transforms = IL::GetILTransforms();
    ASSERT_FALSE(transforms.empty());
    auto* head = dynamic_cast<IL::ControlFlowSimplification*>(
        transforms[0].get());
    EXPECT_NE(head, nullptr)
        << "the C# list's first entry is ControlFlowSimplification";
}

TEST(GetILTransformsTest, SwitchOnStringPrecedesSwitchOnNullable)
{
    auto transforms = IL::GetILTransforms();
    std::size_t stringPos = transforms.size();
    std::size_t nullablePos = transforms.size();
    for (std::size_t i = 0; i < transforms.size(); i++) {
        if (dynamic_cast<IL::SwitchOnStringTransform*>(
                transforms[i].get()) != nullptr) {
            stringPos = i;
        }
        if (dynamic_cast<IL::SwitchOnNullableTransform*>(
                transforms[i].get()) != nullptr) {
            nullablePos = i;
        }
    }
    ASSERT_LT(stringPos, transforms.size())
        << "the SwitchOnString entry is present";
    ASSERT_LT(nullablePos, transforms.size())
        << "the SwitchOnNullable entry is present";
    EXPECT_LT(stringPos, nullablePos)
        << "the SwitchOnString entry precedes SwitchOnNullable";
}

TEST(GetILTransformsTest, StatementTransformCarriesTheInterleavedChildren)
{
    auto transforms = IL::GetILTransforms();
    IL::StatementTransform* statement = nullptr;
    for (auto& t : transforms) {
        statement = dynamic_cast<IL::StatementTransform*>(t.get());
        if (statement != nullptr) break;
    }
    ASSERT_NE(statement, nullptr)
        << "the StatementTransform entry is present";
    // The interleaved per-statement children: the first is ILInlining (the
    // C# comment: inlining runs first because it does not trigger re-runs).
    EXPECT_FALSE(statement->ChildCount() == 0)
        << "the StatementTransform carries its interleaved children";
}

} // namespace
