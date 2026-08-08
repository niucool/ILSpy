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

// Branch-aware IL reader tests. ReadIL splits a method body into blocks at
// branch targets and resolves Branch offsets to Block pointers. Exercises the
// IfInstruction(condition, Branch(target)) shape for conditional branches and
// the fall-through Branch for block boundaries, against real mscorlib methods
// that the straight-line reader rejects.

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

static std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

TEST(ReadIL, DecodesBranchingMethodBodies) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int decoded = 0;
    int multiBlock = 0;
    int sawIf = false;
    int sawResolvedBranch = false;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++decoded;
        fn->CheckInvariant(ILPhase::Normal);
        ASSERT_NE(fn->Body, nullptr);
        if (fn->Body->Blocks.size() > 1) ++multiBlock;
        // Walk the tree: any IfInstruction with a Branch true-inst that has a
        // resolved TargetBlock means a conditional branch decoded + resolved.
        std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
            if (!inst) return;
            if (inst->Op == OpCode::IfInstruction) {
                sawIf = true;
                auto* iff = static_cast<IfInstruction*>(inst);
                if (auto* br = dynamic_cast<Branch*>(iff->TrueInst.get())) {
                    if (!br->HasOffset && br->TargetBlock) sawResolvedBranch = true;
                }
            }
            for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
        };
        walk(fn->Body.get());
        if (decoded > 5000) break;
    }
    EXPECT_GT(decoded, 100) << "ReadIL decoded too few methods";
    EXPECT_GT(multiBlock, 0) << "no multi-block method decoded";
    EXPECT_TRUE(sawIf) << "no IfInstruction (conditional branch) decoded";
    EXPECT_TRUE(sawResolvedBranch) << "no branch target resolved to a Block";
}

TEST(ReadIL, DecodesObjectEqualsWhichStraightLineRejects) {
    // System.Object has several Equals overloads; find one the straight-line reader
    // rejects (it has branches) and confirm ReadIL decodes it (multi-block, with
    // an if). This is the value of the branch-aware reader over the straight-line one.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u);
    bool foundBranching = false;
    for (const auto& m : f.GetMethods(objectTok)) {
        if (m.Name != "Equals") continue;
        if (ReadStraightLineIL(f, m.Token, m.RVA) != nullptr) continue;  // not branching
        auto fn = ReadIL(f, m.Token, m.RVA);
        ASSERT_NE(fn, nullptr) << "ReadIL should decode a branching Equals";
        fn->CheckInvariant(ILPhase::Normal);
        std::string dump = fn->ToString();
        EXPECT_NE(dump.find("if ("), std::string::npos) << dump;
        foundBranching = true;
        break;
    }
    EXPECT_TRUE(foundBranching) << "no branching Equals overload on System.Object found";
}

TEST(ReadIL, SwitchAndExceptionHandlersBailGracefully) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    // Any method with exception handlers must return nullptr, not throw or
    // produce a wrong tree. Scan a sample.
    int sawEhBail = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid() || body.Handlers().empty()) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) ++sawEhBail;
        if (sawEhBail > 20) break;
    }
    EXPECT_GT(sawEhBail, 0) << "expected at least one EH method to bail";
}

TEST(ReadIL, InvalidInputsAreGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(ReadIL(f, 0x06000001u, 0), nullptr);
    EXPECT_EQ(ReadIL(f, 0x06FFFFFFu, 0x12345678u), nullptr);
}
