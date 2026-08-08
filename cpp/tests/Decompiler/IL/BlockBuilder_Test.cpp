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

// BlockBuilder tests: methods with exception-handling regions must decode to a
// NESTED ILAst (TryCatch/TryFinally/TryFault containers over the region's
// blocks), not a flat block list. Ports BlockBuilder.cs behavior: try/handler
// blocks move into region containers, the TryInstruction sits in a wrapper
// block in the enclosing container, IL `leave` decodes as a plain Branch out
// of the region, and endfinally/endfilter Leaves get the innermost finally/
// filter container as their target.

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::ExceptionHandlerKind;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// The StartILOffset range a BlockContainer covers, from its blocks.
std::pair<std::uint32_t, std::uint32_t> ContainerRange(const BlockContainer& c) {
    std::uint32_t lo = UINT32_MAX, hi = 0;
    for (const auto& b : c.Blocks) {
        if (b && b->StartILOffset != UINT32_MAX) {
            lo = std::min(lo, b->StartILOffset);
            hi = std::max(hi, b->StartILOffset + 1);
        }
    }
    return {lo, hi};
}

// Find the first decodable method that has an EH clause of the given kind and
// whose decode produced a nested try instruction. Returns the function plus,
// via out-params, the located TryCatch/TryFinally node.
std::unique_ptr<ILFunction> FindNested(MetadataFile& f, ExceptionHandlerKind kind,
                                       TryCatch** outCatch, TryFinally** outFinally,
                                       std::string* outMethodName) {
    *outCatch = nullptr;
    *outFinally = nullptr;
    for (const auto& t : f.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto body = f.GetMethodBody(m.RVA);
            if (!body.IsValid() || body.Handlers().empty()) continue;
            bool hasKind = false;
            for (const auto& eh : body.Handlers())
                if (eh.Kind == kind) { hasKind = true; break; }
            if (!hasKind) continue;
            auto fn = ReadIL(f, m.Token, m.RVA);
            if (!fn) continue;
            TryCatch* tc = nullptr;
            TryFinally* tf = nullptr;
            Walk(fn->Body.get(), [&](ILInstruction* i) {
                if (!tc && i->Op == OpCode::TryCatch) tc = static_cast<TryCatch*>(i);
                if (!tf && i->Op == OpCode::TryFinally) tf = static_cast<TryFinally*>(i);
            });
            bool nested = (kind == ExceptionHandlerKind::Catch && tc) ||
                          (kind == ExceptionHandlerKind::Finally && tf);
            if (!nested) continue;  // decoded but not nested (or bailed elsewhere)
            if (outMethodName) *outMethodName = t.Namespace + "." + t.Name + "::" + m.Name;
            *outCatch = tc;
            *outFinally = tf;
            return fn;
        }
    }
    return nullptr;
}

} // namespace

TEST(BlockBuilder, TryFinallyNestsRegionBlocksIntoContainers) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    TryCatch* tc = nullptr;
    TryFinally* tf = nullptr;
    std::string name;
    auto fn = FindNested(f, ExceptionHandlerKind::Finally, &tc, &tf, &name);
    ASSERT_NE(fn, nullptr) << "no try/finally method decoded to a nested ILAst";
    ASSERT_NE(tf, nullptr) << name;
    fn->CheckInvariant(ILPhase::Normal);

    // The try and finally children are region containers holding the region's blocks.
    auto* tryC = dynamic_cast<BlockContainer*>(tf->TryBlock.get());
    ASSERT_NE(tryC, nullptr) << "TryFinally.TryBlock must be a BlockContainer in " << name;
    EXPECT_FALSE(tryC->Blocks.empty()) << name;
    auto* finC = dynamic_cast<BlockContainer*>(tf->FinallyBlock.get());
    ASSERT_NE(finC, nullptr) << "TryFinally.FinallyBlock must be a BlockContainer in " << name;
    EXPECT_FALSE(finC->Blocks.empty()) << name;

    // endfinally decodes as Leave targeting the innermost (finally) container.
    bool sawEndfinallyLeave = false;
    Walk(finC, [&](ILInstruction* i) {
        if (auto* leave = dynamic_cast<Leave*>(i)) {
            if (leave->TargetContainer == finC) sawEndfinallyLeave = true;
        }
    });
    EXPECT_TRUE(sawEndfinallyLeave)
        << "endfinally must become Leave(finally-container) in " << name;

    // IL `leave` inside the try region decodes as a Branch whose target is
    // outside the try's offset range (the code after the region).
    auto range = ContainerRange(*tryC);
    bool sawLeaveBranch = false;
    Walk(tryC, [&](ILInstruction* i) {
        if (auto* br = dynamic_cast<Branch*>(i)) {
            if (br->TargetBlock &&
                (br->TargetBlock->StartILOffset < range.first ||
                 br->TargetBlock->StartILOffset >= range.second)) {
                sawLeaveBranch = true;
            }
        }
    });
    EXPECT_TRUE(sawLeaveBranch)
        << "try-region IL leave must decode as a Branch out of the region in " << name;
}

TEST(BlockBuilder, TryCatchNestsWithHandlerVariableAndBody) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    TryCatch* tc = nullptr;
    TryFinally* tf = nullptr;
    std::string name;
    auto fn = FindNested(f, ExceptionHandlerKind::Catch, &tc, &tf, &name);
    ASSERT_NE(fn, nullptr) << "no try/catch method decoded to a nested ILAst";
    ASSERT_NE(tc, nullptr) << name;
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_GE(tc->Handlers.size(), 1u) << name;
    auto& h = tc->Handlers[0];
    ASSERT_NE(h, nullptr);
    // The catch handler carries the exception stack slot the runtime pushes.
    ASSERT_NE(h->Variable, nullptr) << name;
    EXPECT_EQ(h->Variable->Kind, VariableKind::ExceptionStackSlot) << name;
    EXPECT_EQ(h->Variable->Name.rfind("E_", 0), 0u) << name;
    // A plain catch gets the constant filter ldc.i4 1 (ports BlockBuilder.cs).
    ASSERT_NE(h->Filter, nullptr) << name;
    auto* one = dynamic_cast<LdcI4*>(h->Filter.get());
    ASSERT_NE(one, nullptr) << "plain catch filter must be ldc.i4(1) in " << name;
    EXPECT_EQ(one->Value, 1) << name;
    // The handler body is a region container with blocks.
    auto* body = dynamic_cast<BlockContainer*>(h->Body.get());
    ASSERT_NE(body, nullptr) << "TryCatchHandler.Body must be a BlockContainer in " << name;
    EXPECT_FALSE(body->Blocks.empty()) << name;
    EXPECT_NE(dynamic_cast<BlockContainer*>(tc->TryBlock.get()), nullptr) << name;
}

TEST(BlockBuilder, LongFormConditionalBranchesDecodeAndResolve) {
    // The long (4-byte-delta) conditional branches beq..blt(.un) sit at opcode
    // values that a size heuristic once misread as short forms; the mscorlib
    // methods below contain such far conditional jumps and must decode with
    // every branch resolved to a Block.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::uint32_t runtimeTypeTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "RuntimeType") { runtimeTypeTok = t.Token; break; }
    }
    ASSERT_NE(runtimeTypeTok, 0u);
    bool found = false;
    for (const auto& m : f.GetMethods(runtimeTypeTok)) {
        if (m.Name != "FilterApplyMethodBase" || m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        ASSERT_NE(fn, nullptr) << "far-conditional-branch method must decode";
        fn->CheckInvariant(ILPhase::Normal);
        int unresolved = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (auto* br = dynamic_cast<Branch*>(i))
                if (br->HasOffset && !br->TargetBlock) ++unresolved;
        });
        EXPECT_EQ(unresolved, 0) << "long conditional branch targets misread";
        found = true;
        break;
    }
    ASSERT_TRUE(found) << "System.RuntimeType::FilterApplyMethodBase not in fixture";
}

TEST(BlockBuilder, EveryDecodedMethodHasResolvedBranchesAndValidInvariant) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int decoded = 0;
    int nestedEh = 0;
    int unresolvedBranches = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto body = f.GetMethodBody(m.RVA);
            auto fn = ReadIL(f, m.Token, m.RVA);
            if (!fn) continue;
            ++decoded;
            fn->CheckInvariant(ILPhase::Normal);
            bool hasTry = false;
            Walk(fn->Body.get(), [&](ILInstruction* i) {
                if (i->Op == OpCode::TryCatch || i->Op == OpCode::TryFinally ||
                    i->Op == OpCode::TryFault) {
                    hasTry = true;
                }
                if (auto* br = dynamic_cast<Branch*>(i)) {
                    if (br->HasOffset && !br->TargetBlock) ++unresolvedBranches;
                }
            });
            if (hasTry && body.IsValid() && !body.Handlers().empty()) ++nestedEh;
        }
    }
    EXPECT_GT(decoded, 10000) << "decode coverage regressed";
    EXPECT_GT(nestedEh, 50) << "too few EH methods nested";
    EXPECT_EQ(unresolvedBranches, 0) << "branches must resolve to blocks";
}
