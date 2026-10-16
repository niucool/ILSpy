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

// Tests for ILFunction.RunTransforms (the C# `public void RunTransforms(
// IEnumerable<IILTransform> transforms, ILTransformContext context)` --
// ILFunction.cs line 402): the transforms run in list order with an
// invariant check after each, and the per-transform step groups land in the
// debug log hook.

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;

// A counting transform: appends its ordinal to the log list.
class CountingTransform : public IL::IILTransform {
public:
    CountingTransform(std::string name, std::vector<std::string>& log)
        : name_(std::move(name)), log_(log) {}
    void Run(IL::ILFunction& function, IL::ILTransformContext& context) override {
        (void)function;
        (void)context;
        log_.push_back(name_);
    }
private:
    std::string name_;
    std::vector<std::string>& log_;
};

// The transforms run in list order; the context's step hook observes each
// transform's group start (the C# StepStartGroup(transform type name)
// folding onto the port's single Step hook).
TEST(RunTransformsTest, TransformsRunInListOrder)
{
    auto fn = std::make_unique<IL::ILFunction>();
    std::vector<std::string> log;
    std::vector<std::unique_ptr<IL::IILTransform>> transforms;
    transforms.push_back(std::make_unique<CountingTransform>("first", log));
    transforms.push_back(std::make_unique<CountingTransform>("second", log));

    IL::ILTransformContext ctx;
    std::vector<std::string> steps;
    ctx.Step = [&](const char* what) { steps.push_back(what); };

    fn->RunTransforms(transforms, ctx);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "first");
    EXPECT_EQ(log[1], "second");
    EXPECT_FALSE(steps.empty())
        << "each transform's step group reaches the log hook";
    EXPECT_EQ(steps[0], "CountingTransform")
        << "the step group carries the transform's type name";
}

} // namespace
