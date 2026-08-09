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
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
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

TEST(ReadIL, UsesMetadataParameterNames) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int namedParams = 0;
    int checked = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++checked;
        for (auto& v : fn->Variables) {
            if (v && v->Kind == VariableKind::Parameter) {
                // A real metadata name is not the "arg_N" fallback and not "this".
                if (v->Name != "this" && v->Name.rfind("arg_", 0) != 0 && !v->Name.empty())
                    ++namedParams;
            }
        }
        if (checked > 3000) break;
    }
    EXPECT_GT(namedParams, 0) << "no parameter picked up a metadata name";
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

TEST(ReadIL, ExceptionHandlerMethodsAllDecode) {
    // EH methods decode to nested TryCatch/TryFinally/TryFault trees. mscorlib:
    // every handler-bearing method must now decode (the historical bail-outs --
    // filters, rethrow, TypedReference signatures -- are all implemented).
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int ehMethods = 0;
    int ehDecoded = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid() || body.Handlers().empty()) continue;
        ++ehMethods;
        if (ReadIL(f, m.Token, m.RVA)) ++ehDecoded;
    }
    EXPECT_GT(ehMethods, 1000) << "fixture has fewer EH methods than expected";
    EXPECT_EQ(ehDecoded, ehMethods) << "EH methods must all decode";
}

TEST(ReadIL, RefAnyTypeDecodes) {
    // refanytype (FE 1D) pops a TypedReference, pushes its type handle.
    // System.TypedReference methods use it.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    bool found = false;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        bool hasRefany = false;
        for (std::size_t i = 0; i + 1 < body.IL().size(); ++i) {
            if (body.IL()[i] == 0xFE && body.IL()[i + 1] == 0x1D) { hasRefany = true; break; }
        }
        if (!hasRefany) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        ASSERT_NE(fn, nullptr) << "refanytype method must decode: " << m.Name;
        fn->CheckInvariant(ILPhase::Normal);
        EXPECT_NE(fn->ToString().find("refanytype"), std::string::npos) << m.Name;
        found = true;
        break;
    }
    EXPECT_TRUE(found) << "no refanytype method found in fixture";
}

TEST(ReadIL, StfldUsesTargetThenValueStackOrder) {
    // stfld pops value (top), then target: the composed tree must be
    // stobj(ldflda(field, target), value) with target below value on the IL
    // stack. Trivial property setters (set_X with a short body containing
    // stfld) pin the order down: the store target is `this`, the value is the
    // parameter. (Legitimate this.field = this stores exist -- Delegate
    // ctors -- so the check uses setters, where field = value is canonical.)
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int checkedStores = 0;
    int swappedStores = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0 || m.Name.rfind("set_", 0) != 0) continue;
            auto body = f.GetMethodBody(m.RVA);
            if (!body.IsValid() || body.CodeSize() > 30 || !body.Handlers().empty()) continue;
            bool hasStfld = false;
            for (auto byte : body.IL()) if (byte == 0x7D) { hasStfld = true; break; }
            if (!hasStfld) continue;
            auto fn = ReadIL(f, m.Token, m.RVA);
            if (!fn) continue;
            std::function<void(ILInstruction*)> walk = [&](ILInstruction* i) {
                if (!i) return;
                if (i->Op == OpCode::StObj) {
                    auto* st = static_cast<StObj*>(i);
                    if (auto* flda = dynamic_cast<LdFlda*>(st->Target.get())) {
                        auto* tgt = dynamic_cast<LdLoc*>(flda->Target.get());
                        auto* val = dynamic_cast<LdLoc*>(st->Value.get());
                        if (tgt && val && tgt->Variable && val->Variable) {
                            ++checkedStores;
                            if (tgt->Variable->Name != "this" &&
                                val->Variable->Name == "this") {
                                ++swappedStores;
                                ADD_FAILURE() << "stfld target/value swapped in "
                                              << m.Name << ": " << fn->ToString();
                            }
                        }
                    }
                }
                for (int k = 0; k < i->ChildCount(); ++k) walk(i->GetChild(k));
            };
            walk(fn->Body.get());
        }
    }
    EXPECT_GT(checkedStores, 100) << "too few setter stores found to trust the check";
    EXPECT_EQ(swappedStores, 0) << "stfld must pop value, then target";
}

TEST(ReadIL, BranchesCarryingStackValuesDecodeViaStackSlots) {
    // A branch taken with values still pending on the evaluation stack
    // (diamonds that merge a value, loop-carried temporaries) must not fail
    // the whole method: the pending values flush into S_ stack-slot variables
    // and the target block starts with them loaded. Distinct predecessors'
    // slots for the same stack position merge into one variable.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int withStackSlots = 0;
    int unresolved = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        fn->CheckInvariant(ILPhase::Normal);
        bool hasSlot = false, hasBranch = false;
        std::function<void(ILInstruction*)> walk = [&](ILInstruction* i) {
            if (!i) return;
            if (i->Op == OpCode::StLoc) {
                auto* st = static_cast<StLoc*>(i);
                if (st->Variable && st->Variable->Kind == VariableKind::StackSlot &&
                    st->Variable->Name.rfind("S_", 0) == 0) {
                    hasSlot = true;
                }
            }
            if (i->Op == OpCode::Branch) {
                hasBranch = true;
                auto* br = static_cast<Branch*>(i);
                if (br->HasOffset && !br->TargetBlock) ++unresolved;
            }
            for (int k = 0; k < i->ChildCount(); ++k) walk(i->GetChild(k));
        };
        walk(fn->Body.get());
        if (hasSlot && hasBranch) ++withStackSlots;
        if (withStackSlots > 50) break;
    }
    EXPECT_GT(withStackSlots, 10)
        << "too few methods decode with merge stack slots (stack-merge missing?)";
    EXPECT_EQ(unresolved, 0);
}

TEST(ReadIL, RethrowDecodesToTerminalNode) {
    // `catch { throw; }` compiles to the rethrow opcode (FE 1A). Methods with
    // rethrow must decode, with a Rethrow node ending its block.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int found = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        bool hasRethrow = false;
        for (std::size_t i = 0; i + 1 < body.IL().size(); ++i) {
            if (body.IL()[i] == 0xFE && body.IL()[i + 1] == 0x1A) { hasRethrow = true; break; }
        }
        if (!hasRethrow) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        fn->CheckInvariant(ILPhase::Normal);
        bool sawRethrow = false;
        std::function<void(ILInstruction*)> walk = [&](ILInstruction* i) {
            if (!i) return;
            if (i->Op == OpCode::Rethrow) sawRethrow = true;
            for (int k = 0; k < i->ChildCount(); ++k) walk(i->GetChild(k));
        };
        walk(fn->Body.get());
        EXPECT_TRUE(sawRethrow) << m.Name << " decoded but lost its rethrow";
        if (sawRethrow) ++found;
        if (found >= 5) break;
    }
    EXPECT_GE(found, 3) << "too few rethrow methods decode";
}

TEST(ReadIL, StackMergeRaisesDecodeCoverage) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int decoded = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        if (ReadIL(f, m.Token, m.RVA)) ++decoded;
    }
    // Coverage milestones on the framework fixture: 16684 (flat EH), 17829
    // (stack merge + MemberRef/MethodSpec signatures), 25214 (newobj pushes
    // the constructed object). The gate keeps them from regressing.
    EXPECT_GE(decoded, 25000) << "decode coverage regressed";
}

TEST(ReadIL, NewobjPushesTheConstructedObject) {
    // newobj leaves the constructed object on the stack (the constructor's
    // declared void return is irrelevant). The `throw new X(...)` idiom and
    // `x = new X(...)` must decode with the ctor call nested as a value.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int throwNewDecoded = 0;
    int assignedNewDecoded = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        bool hasNewobj = false;
        for (auto byte : body.IL()) if (byte == 0x73) { hasNewobj = true; break; }
        if (!hasNewobj) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        fn->CheckInvariant(ILPhase::Normal);
        std::string dump = fn->ToString();
        // ctor call nested inside a throw -> the value flowed onto the stack
        if (dump.find("throw call ") != std::string::npos &&
            dump.find("::.ctor") != std::string::npos) ++throwNewDecoded;
        // ctor call as a store value
        if (dump.find(", call ") != std::string::npos &&
            dump.find("::.ctor") != std::string::npos) ++assignedNewDecoded;
        if (throwNewDecoded > 40) break;
    }
    EXPECT_GT(throwNewDecoded, 20) << "throw new X(...) methods must decode end-to-end";
    EXPECT_GT(assignedNewDecoded, 20) << "x = new X(...) methods must decode end-to-end";
}

TEST(ReadIL, InvalidInputsAreGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    EXPECT_EQ(ReadIL(f, 0x06000001u, 0), nullptr);
    EXPECT_EQ(ReadIL(f, 0x06FFFFFFu, 0x12345678u), nullptr);
}
