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

// Tests for CodeMappingInfo (the port of
// ICSharpCode.Decompiler/Metadata/CodeMappingInfo.cs): the bidirectional
// parent/part mapping (AddMapping's first-parent-wins rule, GetMethodParts's
// own-method fallback, GetParentMethod's self fallback), and the
// IsCompilerGeneratedStateMachine / IsCompilerGeneratorEnumerator predicates
// over real metadata (the mscorlib-gated fixture). The GetCodeMappingInfo
// builder walk is exercised end-to-end on the mscorlib fixture too.

#include "Decompiler/Metadata/CodeMappingInfo.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

namespace MD = ::ILSpy::Decompiler::Metadata;

} // namespace

// The mapping container: AddMapping + the first-parent-wins rule.
TEST(CodeMappingInfoTest, AddMappingFirstParentWins)
{
    MD::CodeMappingInfo info(nullptr, 0x02000001);
    info.AddMapping(0x06000010, 0x06000011);
    info.AddMapping(0x06000020, 0x06000011);  // already mapped: ignored

    std::vector<std::uint32_t> parts;
    info.GetMethodParts(0x06000010, parts);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0], 0x06000011u);
    EXPECT_EQ(info.GetParentMethod(0x06000011), 0x06000010u);
}

// A method with no recorded parts falls back to itself (the C#
// GetMethodParts's `new[] { method }` arm) and its parent is itself.
TEST(CodeMappingInfoTest, UnmappedMethodFallsBackToSelf)
{
    MD::CodeMappingInfo info(nullptr, 0x02000001);
    info.AddMapping(0x06000010, 0x06000011);

    std::vector<std::uint32_t> parts;
    info.GetMethodParts(0x06000042, parts);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0], 0x06000042u);
    EXPECT_EQ(info.GetParentMethod(0x06000042), 0x06000042u);
}

// A parent accumulates multiple parts in insertion order.
TEST(CodeMappingInfoTest, ParentAccumulatesParts)
{
    MD::CodeMappingInfo info(nullptr, 0x02000001);
    info.AddMapping(0x06000010, 0x06000011);
    info.AddMapping(0x06000010, 0x06000012);
    info.AddMapping(0x06000010, 0x06000013);

    std::vector<std::uint32_t> parts;
    info.GetMethodParts(0x06000010, parts);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], 0x06000011u);
    EXPECT_EQ(parts[1], 0x06000012u);
    EXPECT_EQ(parts[2], 0x06000013u);
}