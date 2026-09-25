// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the CSharpDecompiler.DecompileBodyForAnalysis analysis prefix:
// the fixed C# 1.0 settings (AnalysisTransformSettings), the
// GetILTransforms()-truncated-at-the-last-BlockILTransform list plus
// CombineExitsTransform (RunILTransformsForAnalysis), and the read-and-run
// wrapper (DecompileBodyForAnalysis). The synthetic body exercises the
// CombineExits fold (present in the analysis list, absent from the full
// pipeline); the real-fixture tests exercise the read path and the analysis
// sweep.

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using ILSpy::Decompiler::IL::AnalysisTransformSettings;
using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::BlockContainer;
using ILSpy::Decompiler::IL::DecompileBodyForAnalysis;
using ILSpy::Decompiler::IL::ILFunction;
using ILSpy::Decompiler::IL::IfInstruction;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::Leave;
using ILSpy::Decompiler::IL::RunGetILTransforms;
using ILSpy::Decompiler::IL::RunILTransformsForAnalysis;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::unique_ptr<ILFunction> MakeFn(Block*& block) {
    auto container = std::make_unique<BlockContainer>();
    auto b = std::make_unique<Block>();
    block = b.get();
    container->AddBlock(std::move(b));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

// The fold result: a Leave whose Value is an IfInstruction, or null.
IfInstruction* CombinedIf(Block* block) {
    auto* leave = dynamic_cast<Leave*>(block->FinalInstruction.get());
    if (!leave) return nullptr;
    return dynamic_cast<IfInstruction*>(leave->Value.get());
}

} // namespace

// ---- The fixed C# 1.0 settings ----

TEST(DecompileBodyForAnalysis, AnalysisSettingsDisableLanguageGatedFeatures) {
    const auto s = AnalysisTransformSettings();
    // C# 2: nullables; C# 3: anonymous methods; C# 6: null propagation,
    // string interpolation; C# 7: throw expressions, pattern matching;
    // C# 8/9: recursive patterns / combinators / relational patterns;
    // C# 9/11: native integers, unsigned right shift, checked operators.
    EXPECT_FALSE(s.LiftNullables);
    EXPECT_FALSE(s.AnonymousMethods);
    EXPECT_FALSE(s.NullPropagation);
    EXPECT_FALSE(s.StringInterpolation);
    EXPECT_FALSE(s.ThrowExpressions);
    EXPECT_FALSE(s.PatternMatching);
    EXPECT_FALSE(s.RecursivePatternMatching);
    EXPECT_FALSE(s.PatternCombinators);
    EXPECT_FALSE(s.RelationalPatterns);
    EXPECT_FALSE(s.NativeIntegers);
    EXPECT_FALSE(s.UnsignedRightShift);
    EXPECT_FALSE(s.CheckedOperators);
    // C# 1 features stay on.
    EXPECT_TRUE(s.LockStatement);
    EXPECT_TRUE(s.UsingStatement);
    EXPECT_TRUE(s.ArrayInitializers);
    EXPECT_TRUE(s.SparseIntegerSwitch);
}

// ---- The analysis prefix includes CombineExitsTransform ----

TEST(DecompileBodyForAnalysis, AnalysisPrefixAppliesCombineExits) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    block->Add(std::make_unique<IfInstruction>(
        std::make_unique<LdcI4>(1), std::make_unique<Leave>(body, std::make_unique<LdcI4>(10))));
    block->SetFinal(std::make_unique<Leave>(body, std::make_unique<LdcI4>(20)));

    ILTransformContext ctx;
    ctx.Settings = AnalysisTransformSettings();
    RunILTransformsForAnalysis(*fn, ctx);

    auto* combined = CombinedIf(block);
    ASSERT_NE(combined, nullptr);
    ASSERT_NE(dynamic_cast<LdcI4*>(combined->TrueInst.get()), nullptr);
    ASSERT_NE(dynamic_cast<LdcI4*>(combined->FalseInst.get()), nullptr);
    EXPECT_EQ(static_cast<LdcI4*>(combined->TrueInst.get())->Value, 10);
    EXPECT_EQ(static_cast<LdcI4*>(combined->FalseInst.get())->Value, 20);
}

TEST(DecompileBodyForAnalysis, FullPipelineDoesNotAppendCombineExits) {
    Block* block = nullptr;
    auto fn = MakeFn(block);
    auto* body = static_cast<BlockContainer*>(fn->Body.get());
    block->Add(std::make_unique<IfInstruction>(
        std::make_unique<LdcI4>(1), std::make_unique<Leave>(body, std::make_unique<LdcI4>(10))));
    block->SetFinal(std::make_unique<Leave>(body, std::make_unique<LdcI4>(20)));

    ILTransformContext ctx;
    RunGetILTransforms(*fn, ctx);

    // The if is still a straight-line instruction; the final leave is
    // unconditional. CombineExits is only in the analysis list.
    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(CombinedIf(block), nullptr);
}

// ---- The read-and-run wrapper ----

TEST(DecompileBodyForAnalysis, ReadsRealMethodBodyAndRunsPrefix) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // System.String::Copy is a small real method with a body.
    bool found = false;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = DecompileBodyForAnalysis(f, m.Token, m.RVA);
        if (!fn) continue;
        ASSERT_NE(fn->Body, nullptr);
        fn->CheckInvariant(ILSpy::Decompiler::IL::ILPhase::Normal);
        ASSERT_NE(fn->Body->EntryPoint(), nullptr);
        found = true;
        break;
    }
    ASSERT_TRUE(found) << "no decodable method body found";
}

TEST(DecompileBodyForAnalysis, NullForMethodWithoutBody) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    // rva 0 is the C# `!methodDef.HasBody()` case.
    EXPECT_EQ(DecompileBodyForAnalysis(f, 0x06000001, 0), nullptr);
}

TEST(DecompileBodyForAnalysis, AnalysisSweepOverRealBodies) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int decoded = 0;
    int checked = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = DecompileBodyForAnalysis(f, m.Token, m.RVA);
        if (!fn) continue;
        ++decoded;
        // Every successfully decoded body must satisfy the ILAst invariant
        // after the analysis prefix.
        fn->CheckInvariant(ILSpy::Decompiler::IL::ILPhase::Normal);
        ++checked;
        if (decoded >= 200) break;
    }
    EXPECT_GT(checked, 0);
}
