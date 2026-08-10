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

// Minimal IL reader tests. Reads straight-line method bodies from mscorlib into
// an ILFunction tree and checks that every decoded tree passes the ILAst
// invariant, ends in a Leave/Throw final, and that the dump contains the
// expected structure. This is the seed of the Phase 3 ILReader (stack
// simulation -> ILAst); branches/switch/exception handlers still bail out.

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
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

TEST(ILReader, DecodesStraightLineMethodBodiesFromMscorlib) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int decoded = 0;
    int invariantOk = 0;
    int withLeaveFinal = 0;
    int withThrowFinal = 0;
    int withCall = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadStraightLineIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++decoded;
        // Every decoded tree must pass the invariant (CheckInvariant already ran
        // inside the reader; re-run to be explicit in the test).
        fn->CheckInvariant(ILPhase::Normal);
        ++invariantOk;
        // The body is a BlockContainer with one Block; the block's final is a
        // Leave (ret) or a Throw.
        ASSERT_NE(fn->Body, nullptr);
        ASSERT_EQ(fn->Body->Blocks.size(), 1u);
        auto& block = *fn->Body->Blocks[0];
        ASSERT_NE(block.FinalInstruction, nullptr);
        auto op = block.FinalInstruction->Op;
        if (op == OpCode::Leave) ++withLeaveFinal;
        else if (op == OpCode::Throw) ++withThrowFinal;
        else ADD_FAILURE() << "unexpected final: " << block.FinalInstruction->ToString();
        // Sanity: the dump is non-empty and starts with ILFunction.
        std::string dump = fn->ToString();
        EXPECT_NE(dump.find("ILFunction"), std::string::npos);
        // Count calls in the body (statement-level calls or value calls).
        for (auto& ins : block.Instructions) {
            if (ins->Op == OpCode::Call) ++withCall;
        }
        if (decoded > 4000) break;
    }
    // mscorlib has many tiny straight-line methods (.ctors, wrappers, constants).
    EXPECT_GT(decoded, 100) << "too few straight-line methods decoded";
    EXPECT_EQ(invariantOk, decoded) << "an invariant failed";
    EXPECT_GT(withLeaveFinal, 0) << "no ret-final body decoded";
    // Most straight-line bodies end in ret; a few in throw. Don't require throw.
    (void)withThrowFinal;
    (void)withCall;
}

TEST(ILReader, HandlesVoidReturnAndValueReturn) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // Find System.Object::.ctor: a void instance method whose body is just `ret`.
    // Its ILFunction should be ILFunction { BlockContainer { Block { leave } } }
    // with a void Leave (no value child).
    std::uint32_t objectTok = 0;
    std::uint32_t ctorTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Object") { objectTok = t.Token; break; }
    }
    ASSERT_NE(objectTok, 0u);
    for (const auto& m : f.GetMethods(objectTok)) {
        if (m.Name == ".ctor") { ctorTok = m.Token; break; }
    }
    ASSERT_NE(ctorTok, 0u);
    // .ctor has a body (RVA != 0).
    std::uint32_t ctorRva = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.Token == ctorTok) { ctorRva = m.RVA; break; }
    }
    ASSERT_NE(ctorRva, 0u);
    auto fn = ReadStraightLineIL(f, ctorTok, ctorRva);
    ASSERT_NE(fn, nullptr) << "System.Object..ctor should decode as straight-line";
    auto& block = *fn->Body->Blocks[0];
    ASSERT_EQ(block.FinalInstruction->Op, OpCode::Leave);
    auto& leave = static_cast<Leave&>(*block.FinalInstruction);
    EXPECT_EQ(leave.Value, nullptr) << "void return must have no value child";
    // The dump shows "leave" with no operand.
    std::string dump = fn->ToString();
    EXPECT_NE(dump.find("leave"), std::string::npos);
}

TEST(ILReader, UnsupportedControlFlowBailsOut) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    // System.Object::Equals(object) : bool has branches (the ReferenceEquals
    // short-circuit), so it must bail out (return nullptr) rather than produce
    // a wrong tree.
    std::uint32_t objectTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Object") { objectTok = t.Token; break; }
    }
    ASSERT_NE(objectTok, 0u);
    bool sawBranchingEquals = false;
    for (const auto& m : f.GetMethods(objectTok)) {
        if (m.Name != "Equals") continue;
        // The instance Equals(object) : bool has a body with branches.
        auto fn = ReadStraightLineIL(f, m.Token, m.RVA);
        if (!fn) { sawBranchingEquals = true; break; }
        // If it did decode, it must still pass the invariant (don't fail), but we
        // expect at least one Equals overload to bail.
    }
    EXPECT_TRUE(sawBranchingEquals) << "expected a branching Equals to bail out";
}

TEST(ILReader, InvalidInputsAreGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    // RVA 0 (abstract) and out-of-range token -> nullptr, not throw.
    EXPECT_EQ(ReadStraightLineIL(f, 0x06000001u, 0), nullptr);
    EXPECT_EQ(ReadStraightLineIL(f, 0x06FFFFFFu, 0x12345678u), nullptr);
}

// ILFunction::IsConstructor / IsStatic (the pre-resolved subset of the C#
// ILFunction.Method handle) -- the metadata foundation the
// NullCoalescingTransform hoisted-constructor-argument null-guard fold will
// consult. These tests verify the MetadataFile helper and the IL reader
// populate the flags faithfully on real mscorlib methods; no transform
// consumes them yet (a tested-but-not-yet-wired foundation).

TEST(ILFunctionMethod, GetMethodDefKindInfoReportsConstructorAndStatic) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::uint32_t objectTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Object") { objectTok = t.Token; break; }
    }
    ASSERT_NE(objectTok, 0u);

    std::uint32_t instanceCtorTok = 0;       // System.Object..ctor
    std::uint32_t staticRefEqualsTok = 0;    // static ReferenceEquals(object, object)
    std::uint32_t instanceToStringTok = 0;   // instance ToString()
    for (const auto& m : f.GetMethods(objectTok)) {
        if (m.Name == ".ctor" && instanceCtorTok == 0) instanceCtorTok = m.Token;
        // ReferenceEquals is a single static overload (unambiguous, unlike Equals
        // which has both an instance and a static overload).
        if (m.Name == "ReferenceEquals" && staticRefEqualsTok == 0) staticRefEqualsTok = m.Token;
        if (m.Name == "ToString" && instanceToStringTok == 0) instanceToStringTok = m.Token;
    }
    ASSERT_NE(instanceCtorTok, 0u);
    ASSERT_NE(staticRefEqualsTok, 0u);
    ASSERT_NE(instanceToStringTok, 0u);

    // System.Object..ctor: an instance constructor -> IsConstructor && !IsStatic.
    auto kind = f.GetMethodDefKindInfo(instanceCtorTok);
    EXPECT_TRUE(kind.IsConstructor) << ".ctor must be a constructor";
    EXPECT_FALSE(kind.IsStatic) << "instance .ctor must not be static";

    // System.Object.ReferenceEquals(object, object) is static -> IsStatic &&
    // !IsConstructor.
    auto refEqKind = f.GetMethodDefKindInfo(staticRefEqualsTok);
    EXPECT_TRUE(refEqKind.IsStatic) << "a static method must report IsStatic";
    EXPECT_FALSE(refEqKind.IsConstructor);

    // System.Object.ToString() is an instance non-constructor -> neither flag.
    auto toStringKind = f.GetMethodDefKindInfo(instanceToStringTok);
    EXPECT_FALSE(toStringKind.IsConstructor);
    EXPECT_FALSE(toStringKind.IsStatic);

    // An out-of-range / wrong-table token yields the default (false/false),
    // never throws.
    auto badKind = f.GetMethodDefKindInfo(0x06FFFFFFu);
    EXPECT_FALSE(badKind.IsConstructor);
    EXPECT_FALSE(badKind.IsStatic);
    auto nonMethodKind = f.GetMethodDefKindInfo(0x02000001u);  // TypeDef token
    EXPECT_FALSE(nonMethodKind.IsConstructor);
    EXPECT_FALSE(nonMethodKind.IsStatic);
}

TEST(ILFunctionMethod, ReadILPopulatesConstructorStaticFlags) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::uint32_t objectTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Object") { objectTok = t.Token; break; }
    }
    ASSERT_NE(objectTok, 0u);

    std::uint32_t instanceCtorTok = 0;
    std::uint32_t instanceCtorRva = 0;
    for (const auto& m : f.GetMethods(objectTok)) {
        if (m.Name == ".ctor" && instanceCtorTok == 0) {
            instanceCtorTok = m.Token;
            instanceCtorRva = m.RVA;
        }
    }
    ASSERT_NE(instanceCtorTok, 0u);
    ASSERT_NE(instanceCtorRva, 0u);

    // System.Object..ctor is `ret` only, so both readers decode it. The static
    // case is covered by the corpus sweep below (many static methods have
    // branches and bail the straight-line reader; the sweep's cross-check +
    // `staticMethod > 0` assertion pin the reader flags a static method).
    auto fnStraight = ReadStraightLineIL(f, instanceCtorTok, instanceCtorRva);
    ASSERT_NE(fnStraight, nullptr);
    EXPECT_TRUE(fnStraight->IsConstructor) << "reader must flag an instance .ctor";
    EXPECT_FALSE(fnStraight->IsStatic);

    auto fnBranch = ReadIL(f, instanceCtorTok, instanceCtorRva);
    ASSERT_NE(fnBranch, nullptr);
    EXPECT_TRUE(fnBranch->IsConstructor);
    EXPECT_FALSE(fnBranch->IsStatic);
}

TEST(ILFunctionMethod, MscorlibConstructorStaticFlagSweep) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // Cross-check the reader-populated flags against the helper across the
    // corpus (the reader must call the helper), and confirm the four flag
    // combinations all appear (instance ctor, static ctor, instance method,
    // static method) on the decoded subset.
    int decoded = 0;
    int instanceCtor = 0;       // IsConstructor && !IsStatic  (the gate the fold needs)
    int staticCtor = 0;          // IsConstructor && IsStatic    (.cctor)
    int instanceMethod = 0;      // !IsConstructor && !IsStatic
    int staticMethod = 0;        // !IsConstructor && IsStatic
    int crossCheckOk = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadStraightLineIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++decoded;
        auto kind = f.GetMethodDefKindInfo(m.Token);
        if (fn->IsConstructor == kind.IsConstructor && fn->IsStatic == kind.IsStatic) {
            ++crossCheckOk;
        } else {
            ADD_FAILURE() << "reader/helper flag mismatch for method token " << m.Token;
        }
        if (fn->IsConstructor && !fn->IsStatic) ++instanceCtor;
        else if (fn->IsConstructor && fn->IsStatic) ++staticCtor;
        else if (!fn->IsConstructor && !fn->IsStatic) ++instanceMethod;
        else ++staticMethod;
        if (decoded > 4000) break;
    }
    EXPECT_GT(decoded, 100) << "too few straight-line methods decoded";
    EXPECT_EQ(crossCheckOk, decoded) << "reader must populate flags from the helper";
    EXPECT_GT(instanceCtor, 0) << "corpus must have instance constructors";
    EXPECT_GT(staticMethod, 0) << "corpus must have static methods";
    EXPECT_GT(instanceMethod, 0) << "corpus must have instance non-constructors";
    (void)staticCtor;  // .cctor may or may not appear in the straight-line sample
}
