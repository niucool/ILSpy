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

// Tests for the ILStructure analysis (cpp/Decompiler/Disassembler/
// ILStructure.{hpp,cpp}): the exception-structure tree (try/filter/handler
// nesting, the shared-try dedup, the overlap rejection), the very simple
// loop detection (backward branches, the entry-point rules, the continue
// special case, switch targets), SortChildren, and GetInnermost -- over
// synthetic IL streams and EH tables plus a real-mscorlib invariant sweep.

#include "Decompiler/Disassembler/ILStructure.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

using ILSpy::Decompiler::Disassembler::ILStructure;
using ILSpy::Decompiler::Disassembler::ILStructureType;
namespace MD = ILSpy::Decompiler::Metadata;
namespace Util = ILSpy::Decompiler::Util;

namespace {

#if defined(_WIN32)
const char* MscorlibPath() { return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll"; }
#else
const char* MscorlibPath() { return "/usr/lib/mono/4.5/mscorlib.dll"; }
#endif

MD::ExceptionHandlerClause MakeClause(MD::ExceptionHandlerKind kind,
    std::uint32_t tryOffset, std::uint32_t tryLength,
    std::uint32_t handlerOffset, std::uint32_t handlerLength,
    std::uint32_t classTokenOrFilterOffset) {
    return MD::ExceptionHandlerClause{kind, tryOffset, tryLength,
        handlerOffset, handlerLength, classTokenOrFilterOffset};
}

// A synthetic IL byte vector of `size` nops (the tree logic reads only the
// opcodes the branch walk decodes, and a stream of nops decodes as `size`
// single-byte instructions). The vector must outlive the MakeRoot call (the
// root ctor reads the span during construction only).
std::vector<std::uint8_t> Nops(std::size_t size) {
    return std::vector<std::uint8_t>(size, 0x00);
}

Util::Span<const std::uint8_t> IlOf(const std::vector<std::uint8_t>& il) {
    return Util::Span<const std::uint8_t>(il.data(), il.size());
}

Util::Span<const MD::ExceptionHandlerClause> ClausesOf(
    const std::vector<MD::ExceptionHandlerClause>& handlers) {
    return Util::Span<const MD::ExceptionHandlerClause>(handlers.data(), handlers.size());
}

ILStructure MakeRoot(MD::MetadataFile& f, Util::Span<const std::uint8_t> il,
    Util::Span<const MD::ExceptionHandlerClause> handlers) {
    return ILStructure(f, 0x06000001u, MD::MetadataGenericContext{}, il, handlers);
}

std::vector<MD::ExceptionHandlerClause> Vec(
    std::initializer_list<MD::ExceptionHandlerClause> clauses) {
    return std::vector<MD::ExceptionHandlerClause>(clauses);
}

}  // namespace

// ---------------------------------------------------------------------------
// The exception-structure tree (the root ctor's first half).
// ---------------------------------------------------------------------------

TEST(ILStructureTest, TryCatchBuildsTryAndHandlerStructures) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto handlers = Vec({MakeClause(MD::ExceptionHandlerKind::Catch, 0, 10, 10, 5, 0x02000001u)});
    auto il = Nops(15);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    EXPECT_EQ(root.Type, ILStructureType::Root);
    EXPECT_EQ(root.StartOffset, 0);
    EXPECT_EQ(root.EndOffset, 15);
    ASSERT_EQ(root.Children.size(), 2u);

    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Try);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[0]->EndOffset, 10);
    EXPECT_EQ(root.Children[0]->ExceptionHandler.Kind, MD::ExceptionHandlerKind::Catch);

    EXPECT_EQ(root.Children[1]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[1]->StartOffset, 10);
    EXPECT_EQ(root.Children[1]->EndOffset, 15);
    EXPECT_EQ(root.Children[1]->ExceptionHandler.ClassTokenOrFilterOffset, 0x02000001u);
}

TEST(ILStructureTest, FilterClauseBuildsFilterBetweenTryAndHandler) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // The filter block spans [FilterOffset, HandlerOffset).
    auto handlers = Vec({MakeClause(MD::ExceptionHandlerKind::Filter, 0, 8, 12, 3, 10)});
    auto il = Nops(15);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    ASSERT_EQ(root.Children.size(), 3u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Try);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[0]->EndOffset, 8);
    EXPECT_EQ(root.Children[1]->Type, ILStructureType::Filter);
    EXPECT_EQ(root.Children[1]->StartOffset, 10);
    EXPECT_EQ(root.Children[1]->EndOffset, 12);
    EXPECT_EQ(root.Children[2]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[2]->StartOffset, 12);
    EXPECT_EQ(root.Children[2]->EndOffset, 15);
}

TEST(ILStructureTest, SharedTryRangeIsDeduplicated) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Two catch clauses over the SAME try range: only one Try structure.
    auto handlers = Vec({
        MakeClause(MD::ExceptionHandlerKind::Catch, 0, 10, 10, 5, 0x02000001u),
        MakeClause(MD::ExceptionHandlerKind::Catch, 0, 10, 15, 5, 0x02000002u),
    });
    auto il = Nops(20);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    ASSERT_EQ(root.Children.size(), 3u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Try);
    EXPECT_EQ(root.Children[1]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[1]->StartOffset, 10);
    EXPECT_EQ(root.Children[2]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[2]->StartOffset, 15);
}

TEST(ILStructureTest, SmallerTryNestsInsideBiggerTry) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto handlers = Vec({
        MakeClause(MD::ExceptionHandlerKind::Catch, 0, 20, 25, 5, 0x02000001u),
        MakeClause(MD::ExceptionHandlerKind::Catch, 5, 5, 10, 5, 0x02000002u),
    });
    auto il = Nops(35);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    // The big try contains the small try and its handler; the big handler is
    // a sibling of the big try.
    ASSERT_EQ(root.Children.size(), 2u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Try);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[0]->EndOffset, 20);
    ASSERT_EQ(root.Children[0]->Children.size(), 2u);
    EXPECT_EQ(root.Children[0]->Children[0]->Type, ILStructureType::Try);
    EXPECT_EQ(root.Children[0]->Children[0]->StartOffset, 5);
    EXPECT_EQ(root.Children[0]->Children[0]->EndOffset, 10);
    EXPECT_EQ(root.Children[0]->Children[1]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[0]->Children[1]->StartOffset, 10);
    EXPECT_EQ(root.Children[0]->Children[1]->EndOffset, 15);
    EXPECT_EQ(root.Children[1]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[1]->StartOffset, 25);
}

TEST(ILStructureTest, OverlappingTryIsRejected) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // [0,10) and [5,15) overlap without containment: the second try cannot
    // nest, so it is not added.
    auto handlers = Vec({
        MakeClause(MD::ExceptionHandlerKind::Catch, 0, 10, 10, 4, 0x02000001u),
        MakeClause(MD::ExceptionHandlerKind::Catch, 5, 10, 15, 5, 0x02000002u),
    });
    auto il = Nops(20);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    ASSERT_EQ(root.Children.size(), 3u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Try);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[0]->EndOffset, 10);
    // The overlapping [5,15) try is absent; both handlers survive.
    EXPECT_EQ(root.Children[1]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[1]->StartOffset, 10);
    EXPECT_EQ(root.Children[2]->Type, ILStructureType::Handler);
    EXPECT_EQ(root.Children[2]->StartOffset, 15);
}

TEST(ILStructureTest, ChildrenAreSortedByStartOffset) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Clauses whose structures insert out of order: [10,15) first, [0,5) and
    // [5,10) after. SortChildren orders them by StartOffset.
    auto handlers = Vec({
        MakeClause(MD::ExceptionHandlerKind::Catch, 10, 5, 20, 5, 0x02000001u),
        MakeClause(MD::ExceptionHandlerKind::Catch, 0, 5, 5, 5, 0x02000002u),
    });
    auto il = Nops(25);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    ASSERT_EQ(root.Children.size(), 4u);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[1]->StartOffset, 5);
    EXPECT_EQ(root.Children[2]->StartOffset, 10);
    EXPECT_EQ(root.Children[3]->StartOffset, 20);
}

// ---------------------------------------------------------------------------
// The very simple loop detection (the root ctor's second half).
// ---------------------------------------------------------------------------

namespace {

// ldc.i4.1; ldc.i4.2; add; br.s -> 1; ret -- one backward branch forming the
// loop [1,5) with the head as the fall-through entry point.
const std::uint8_t kSimpleLoop[] = {
    0x17,             // [0,1) ldc.i4.1
    0x18,             // [1,2) ldc.i4.2
    0x58,             // [2,3) add
    0x2B, 0xFC,       // [3,5) br.s -> 5 - 4 = 1
    0x2A,             // [5,6) ret
};

// br.s -> 7; ldc.i4.1; ldc.i4.2; ldc.i4.3; ret; ldc.i4.4; ldc.i4.5; br.s -> 6
// -- the loop head at 6 follows a ret (unconditional), so the entry point
// comes from the outside-branch scan: the br.s at 0 jumps to 7.
const std::uint8_t kOutsideEntryLoop[] = {
    0x2B, 0x05,       // [0,2) br.s -> 2 + 5 = 7
    0x17,             // [2,3) ldc.i4.1
    0x18,             // [3,4) ldc.i4.2
    0x19,             // [4,5) ldc.i4.3
    0x2A,             // [5,6) ret (unconditional; the loop head follows it)
    0x1A,             // [6,7) ldc.i4.4 (loop head)
    0x1B,             // [7,8) ldc.i4.5
    0x2B, 0xFC,       // [8,10) br.s -> 10 - 4 = 6 (the back edge)
};

// br.s -> 6; br.s -> 8; <loop [4,12)> ... br.s -> 4 -- two outside branches
// into two DISTINCT in-loop offsets: multiple entry points, no loop.
const std::uint8_t kMultipleEntryLoop[] = {
    0x2B, 0x04,       // [0,2) br.s -> 2 + 4 = 6
    0x2B, 0x04,       // [2,4) br.s -> 4 + 4 = 8
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,  // [4,10) the loop body
    0x2B, 0xF8,       // [10,12) br.s -> 12 - 8 = 4 (the back edge)
};

// ldc.i4.1; ldc.i4.2; br.s -> 0; ldc.i4.3; br.s -> 0 -- two back edges to the
// same head: the continue special case keeps only the bigger [0,7) loop.
const std::uint8_t kContinueLoop[] = {
    0x17,             // [0,1) ldc.i4.1 (loop head)
    0x18,             // [1,2) ldc.i4.2
    0x2B, 0xFC,       // [2,4) br.s -> 4 - 4 = 0 (back edge 1)
    0x19,             // [4,5) ldc.i4.3
    0x2B, 0xF9,       // [5,7) br.s -> 7 - 7 = 0 (back edge 2)
};

// ldc.i4.1; switch (2: -> 0, -> 14); ret -- a switch whose first target is a
// backward branch: the loop [0,14) with no recorded entry point.
const std::uint8_t kSwitchLoop[] = {
    0x17,             // [0,1) ldc.i4.1
    0x45, 0x02, 0x00, 0x00, 0x00,  // [1,6) switch opcode + count=2
    0xF2, 0xFF, 0xFF, 0xFF,         // delta -14 -> 14 - 14 = 0
    0x00, 0x00, 0x00, 0x00,         // delta 0 -> 14
    0x2A,             // [14,15) ret
};

Util::Span<const std::uint8_t> SpanOf(const std::uint8_t* p, std::size_t n) {
    return Util::Span<const std::uint8_t>(p, n);
}

}  // namespace

TEST(ILStructureTest, BackwardBranchFormsLoopWithFallThroughEntry) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    ILStructure root = MakeRoot(f, SpanOf(kSimpleLoop, sizeof(kSimpleLoop)),
        Util::Span<const MD::ExceptionHandlerClause>());

    ASSERT_EQ(root.Children.size(), 1u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Loop);
    EXPECT_EQ(root.Children[0]->StartOffset, 1);
    EXPECT_EQ(root.Children[0]->EndOffset, 5);
    // The instruction before the head (ldc.i4.1) is not an unconditional
    // branch, so the head itself is the entry point.
    EXPECT_EQ(root.Children[0]->LoopEntryPointOffset, 1);
}

TEST(ILStructureTest, LoopEntryPointFallsBackToOutsideBranch) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    ILStructure root = MakeRoot(f, SpanOf(kOutsideEntryLoop, sizeof(kOutsideEntryLoop)),
        Util::Span<const MD::ExceptionHandlerClause>());

    ASSERT_EQ(root.Children.size(), 1u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Loop);
    EXPECT_EQ(root.Children[0]->StartOffset, 6);
    EXPECT_EQ(root.Children[0]->EndOffset, 10);
    // The head follows a ret, so the entry point is the outside branch's
    // target (7), not the head.
    EXPECT_EQ(root.Children[0]->LoopEntryPointOffset, 7);
}

TEST(ILStructureTest, MultipleEntryPointsRejectTheLoop) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    ILStructure root = MakeRoot(f, SpanOf(kMultipleEntryLoop, sizeof(kMultipleEntryLoop)),
        Util::Span<const MD::ExceptionHandlerClause>());

    // Two outside branches jump to distinct in-loop offsets (6 and 8): the
    // loop [4,12) has multiple entry points and is not added.
    EXPECT_EQ(root.Children.size(), 0u);
}

TEST(ILStructureTest, ContinueShapedLoopIsNotNested) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    ILStructure root = MakeRoot(f, SpanOf(kContinueLoop, sizeof(kContinueLoop)),
        Util::Span<const MD::ExceptionHandlerClause>());

    // The bigger [0,7) loop survives; the [0,4) loop with the same start is
    // the continue special case and is not added.
    ASSERT_EQ(root.Children.size(), 1u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Loop);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[0]->EndOffset, 7);
    EXPECT_EQ(root.Children[0]->LoopEntryPointOffset, -1);
}

TEST(ILStructureTest, SwitchBackwardTargetFormsLoop) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    ILStructure root = MakeRoot(f, SpanOf(kSwitchLoop, sizeof(kSwitchLoop)),
        Util::Span<const MD::ExceptionHandlerClause>());

    ASSERT_EQ(root.Children.size(), 1u);
    EXPECT_EQ(root.Children[0]->Type, ILStructureType::Loop);
    EXPECT_EQ(root.Children[0]->StartOffset, 0);
    EXPECT_EQ(root.Children[0]->EndOffset, 14);
    // loopStart == 0, so the entry point is never recorded.
    EXPECT_EQ(root.Children[0]->LoopEntryPointOffset, -1);
}

// ---------------------------------------------------------------------------
// GetInnermost.
// ---------------------------------------------------------------------------

TEST(ILStructureTest, GetInnermostFindsTheDeepestStructure) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto handlers = Vec({
        MakeClause(MD::ExceptionHandlerKind::Catch, 0, 20, 25, 5, 0x02000001u),
        MakeClause(MD::ExceptionHandlerKind::Catch, 5, 5, 10, 5, 0x02000002u),
    });
    auto il = Nops(35);
    ILStructure root = MakeRoot(f, IlOf(il), ClausesOf(handlers));

    const ILStructure* s7 = root.GetInnermost(7);
    ASSERT_NE(s7, nullptr);
    EXPECT_EQ(s7->Type, ILStructureType::Try);
    EXPECT_EQ(s7->StartOffset, 5);
    EXPECT_EQ(s7->EndOffset, 10);

    const ILStructure* s12 = root.GetInnermost(12);
    ASSERT_NE(s12, nullptr);
    EXPECT_EQ(s12->Type, ILStructureType::Handler);
    EXPECT_EQ(s12->StartOffset, 10);

    const ILStructure* s27 = root.GetInnermost(27);
    ASSERT_NE(s27, nullptr);
    EXPECT_EQ(s27->Type, ILStructureType::Handler);
    EXPECT_EQ(s27->StartOffset, 25);

    // Inside the outer try but outside its children: the outer try itself.
    const ILStructure* s2 = root.GetInnermost(2);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->Type, ILStructureType::Try);
    EXPECT_EQ(s2->StartOffset, 0);
    EXPECT_EQ(s2->EndOffset, 20);

    // Between the outer try and the big handler: the root.
    const ILStructure* s22 = root.GetInnermost(22);
    ASSERT_NE(s22, nullptr);
    EXPECT_EQ(s22, &root);
}

// ---------------------------------------------------------------------------
// The real-mscorlib invariant sweep.
// ---------------------------------------------------------------------------

namespace {

struct SweepCounts {
    int structures = 0;
    int loops = 0;
};

SweepCounts CheckStructureInvariants(const ILStructure& s) {
    SweepCounts counts;
    counts.structures = 1;
    counts.loops = (s.Type == ILStructureType::Loop) ? 1 : 0;
    for (std::size_t i = 0; i < s.Children.size(); i++) {
        const ILStructure& child = *s.Children[i];
        if (i > 0)
            EXPECT_LE(s.Children[i - 1]->StartOffset, child.StartOffset)
                << "children must be sorted by StartOffset";
        EXPECT_LE(s.StartOffset, child.StartOffset);
        EXPECT_LE(child.EndOffset, s.EndOffset);
        EXPECT_LT(child.StartOffset, child.EndOffset);
        SweepCounts sub = CheckStructureInvariants(child);
        counts.structures += sub.structures;
        counts.loops += sub.loops;
    }
    return counts;
}

}  // namespace

TEST(ILStructureTest, MscorlibBodiesBuildWellFormedTrees) {
    MD::MetadataFile f(MscorlibPath());
    if (!f.IsValid()) {
        GTEST_SKIP() << "mscorlib not present";
    }
    int bodies = 0;
    SweepCounts totals;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            std::uint32_t rva = m.RVA;
            if (rva == 0) continue;
            auto body = f.GetMethodBody(rva);
            if (!body.IsValid()) continue;
            auto il = body.IL();
            if (il.size() == 0) continue;

            ILStructure root(f, m.Token, MD::MetadataGenericContext::ForMethod(m.Token, f),
                il, body.Handlers());
            EXPECT_EQ(root.Type, ILStructureType::Root);
            EXPECT_EQ(root.StartOffset, 0);
            EXPECT_EQ(root.EndOffset, static_cast<int>(il.size()));
            // GetInnermost returns a containing structure for an in-range offset.
            const ILStructure* innermost = root.GetInnermost(static_cast<int>(il.size()) - 1);
            ASSERT_NE(innermost, nullptr);
            EXPECT_LE(innermost->StartOffset, static_cast<int>(il.size()) - 1);
            EXPECT_LT(static_cast<int>(il.size()) - 1, innermost->EndOffset);

            SweepCounts counts = CheckStructureInvariants(root);
            totals.structures += counts.structures;
            totals.loops += counts.loops;
            bodies++;
            if (bodies >= 1500) break;
        }
        if (bodies >= 1500) break;
    }
    EXPECT_GT(bodies, 0);
    EXPECT_GT(totals.structures, 0);
    // Real methods carry backward branches: the loop detection fires.
    EXPECT_GT(totals.loops, 0);
}
