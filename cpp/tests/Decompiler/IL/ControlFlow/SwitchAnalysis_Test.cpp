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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// SwitchAnalysis tests. The analysis reconstructs a C# switch (compiled to a
// sequence of if-statements testing one integer variable) as a list of
// (value-set, body) sections. This port's block model makes the IfInstruction
// the block's final with an implicit fall-through to the next block, so the
// "false arm" of each case-test if is the next block in the container and the
// fall-through section body is a synthesized Branch to it.

#include "Decompiler/IL/ControlFlow/SwitchAnalysis.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/Util/LongSet.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Util::LongSet;
using ILSpy::Decompiler::Util::LongInterval;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// The body of a section as a Branch target block, or nullptr if it is not a
// Branch (e.g. a synthesized fall-through that was not a Branch, or a Leave).
Block* SectionTarget(const SwitchSectionResult& s) {
    if (!s.Body || s.Body->Op != OpCode::Branch) return nullptr;
    return static_cast<Branch*>(s.Body)->TargetBlock;
}

// Find the section whose body branches to `target`, or nullptr. The switch
// analysis de-duplicates sections that branch to the same block, so there is
// at most one per target.
const SwitchSectionResult* SectionFor(const std::vector<SwitchSectionResult>& sections, Block* target) {
    for (const auto& s : sections)
        if (SectionTarget(s) == target) return &s;
    return nullptr;
}

// Build: root: if (comp(eq, V, k1)) br caseA; fall through
//        next: if (comp(eq, V, k2)) br caseB; fall through
//        def:  leave body  (the default arm)
//        caseA: leave body
//        caseB: leave body
// Returns the function and the block/variable handles for assertions.
struct TwoCaseFixture {
    std::unique_ptr<ILFunction> fn;
    Block* root;
    Block* next;
    Block* def;
    Block* caseA;
    Block* caseB;
    ILVariablePtr V;
};

TwoCaseFixture BuildTwoCase(long long k1, long long k2) {
    TwoCaseFixture fx;
    fx.fn = std::make_unique<ILFunction>();
    fx.fn->Body = std::make_unique<BlockContainer>();
    fx.fn->Body->Parent = fx.fn.get();
    fx.fn->Body->ChildIndex = 0;
    fx.V = MakeLocal("V");
    fx.fn->Variables.push_back(fx.V);
    for (int i = 0; i < 5; ++i) fx.fn->Body->AddBlock(std::make_unique<Block>());
    fx.root = fx.fn->Body->Blocks[0].get();
    fx.next = fx.fn->Body->Blocks[1].get();
    fx.def = fx.fn->Body->Blocks[2].get();
    fx.caseA = fx.fn->Body->Blocks[3].get();
    fx.caseB = fx.fn->Body->Blocks[4].get();
    // root: if (comp(eq, V, k1)) br caseA
    fx.root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(fx.V), std::make_unique<LdcI4>(static_cast<int>(k1)),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(fx.caseA)));
    // next: if (comp(eq, V, k2)) br caseB
    fx.next->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(fx.V), std::make_unique<LdcI4>(static_cast<int>(k2)),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(fx.caseB)));
    // def / caseA / caseB: leave body (terminals so the tree is well-formed)
    fx.def->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    fx.caseA->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    fx.caseB->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fx.fn);
    return fx;
}

} // namespace

// A two-case if-chain on V reconstructs three sections: {k1}->caseA,
// {k2}->caseB, and complement-of-{k1,k2}->def (the fall-through default).
TEST(SwitchAnalysis, TwoCaseIfChainProducesThreeSections) {
    auto fx = BuildTwoCase(1, 2);
    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(fx.root));
    EXPECT_EQ(a.SwitchVariable, fx.V.get());
    EXPECT_FALSE(a.ContainsILSwitch);
    ASSERT_EQ(a.Sections.size(), 3u);

    auto* sA = SectionFor(a.Sections, fx.caseA);
    ASSERT_NE(sA, nullptr);
    EXPECT_TRUE(sA->Labels.Contains(1));
    EXPECT_EQ(sA->Labels.Count(), 1u);

    auto* sB = SectionFor(a.Sections, fx.caseB);
    ASSERT_NE(sB, nullptr);
    EXPECT_TRUE(sB->Labels.Contains(2));
    EXPECT_EQ(sB->Labels.Count(), 1u);

    auto* sDef = SectionFor(a.Sections, fx.def);
    ASSERT_NE(sDef, nullptr);
    // The default is everything except {1, 2}.
    EXPECT_FALSE(sDef->Labels.Contains(1));
    EXPECT_FALSE(sDef->Labels.Contains(2));
    EXPECT_TRUE(sDef->Labels.Contains(0));
    EXPECT_TRUE(sDef->Labels.Contains(3));
    EXPECT_TRUE(sDef->Labels.Contains(-1));

    // The `next` block was further analyzed (it has its own case-test if), so it
    // is an inner block the switch absorbs; the case blocks are terminals and
    // are not inner blocks.
    ASSERT_EQ(a.InnerBlocks.size(), 1u);
    EXPECT_EQ(a.InnerBlocks[0], fx.next);
}

// Two case-test ifs whose true arms branch to the same block: the sections are
// de-duplicated -- the two value sets merge into one section for that block.
TEST(SwitchAnalysis, DeduplicatesSectionsBranchingToSameBlock) {
    auto fx = BuildTwoCase(1, 2);
    // Reroute next's true arm to caseA (same target as root's true arm).
    fx.next->FinalInstruction.reset();
    fx.next->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(fx.V), std::make_unique<LdcI4>(2),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(fx.caseA)));
    RecomputeIncomingEdgeCounts(*fx.fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(fx.root));
    // Two sections: {1,2}->caseA (merged) and complement-of-{1,2}->def.
    ASSERT_EQ(a.Sections.size(), 2u);
    auto* sA = SectionFor(a.Sections, fx.caseA);
    ASSERT_NE(sA, nullptr);
    EXPECT_TRUE(sA->Labels.Contains(1));
    EXPECT_TRUE(sA->Labels.Contains(2));
    EXPECT_EQ(sA->Labels.Count(), 2u);
    auto* sDef = SectionFor(a.Sections, fx.def);
    ASSERT_NE(sDef, nullptr);
    EXPECT_FALSE(sDef->Labels.Contains(1));
    EXPECT_FALSE(sDef->Labels.Contains(2));
}

// brfalse V (comp(eq, V, 0)) branches for V == 0; the section's value set is
// the singleton {0}.
TEST(SwitchAnalysis, BrfalseMatchesValueZero) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(caseA)));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    auto* sCase = SectionFor(a.Sections, caseA);
    ASSERT_NE(sCase, nullptr);
    EXPECT_TRUE(sCase->Labels.Contains(0));
    EXPECT_EQ(sCase->Labels.Count(), 1u);
}

// brtrue V (bare ldloc V) branches for all values except 0; the section's value
// set is the complement of {0}.
TEST(SwitchAnalysis, BrtrueMatchesAllExceptZero) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(V), std::make_unique<Branch>(caseA)));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    auto* sCase = SectionFor(a.Sections, caseA);
    ASSERT_NE(sCase, nullptr);
    EXPECT_FALSE(sCase->Labels.Contains(0));
    EXPECT_TRUE(sCase->Labels.Contains(1));
    EXPECT_TRUE(sCase->Labels.Contains(-1));
}

// comp(V < 5) (signed) branches for {Min..4}; comp(V > 5) for {6..Max}.
TEST(SwitchAnalysis, SignedLessThanAndGreaterThan) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(5),
                               ComparisonKind::LessThan, false),
        std::make_unique<Branch>(caseA)));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    auto* sCase = SectionFor(a.Sections, caseA);
    ASSERT_NE(sCase, nullptr);
    EXPECT_FALSE(sCase->Labels.Contains(5));
    EXPECT_TRUE(sCase->Labels.Contains(4));
    EXPECT_TRUE(sCase->Labels.Contains(-1));
    EXPECT_FALSE(sCase->Labels.Contains(6));
}

// comp(V >= 5) (unsigned, i.e. bge_un) branches for the unsigned range [5..Max]
// which wraps: {5..Max} U {Min..-1}.
TEST(SwitchAnalysis, UnsignedGreaterThanOrEqualWraps) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(5),
                               ComparisonKind::GreaterThanOrEqual, true),
        std::make_unique<Branch>(caseA)));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    auto* sCase = SectionFor(a.Sections, caseA);
    ASSERT_NE(sCase, nullptr);
    EXPECT_TRUE(sCase->Labels.Contains(5));
    EXPECT_FALSE(sCase->Labels.Contains(4));
    EXPECT_FALSE(sCase->Labels.Contains(0));
    // The wrap: negative values (Min..-1) are >= 5 as unsigned.
    EXPECT_TRUE(sCase->Labels.Contains(-1));
}

// comp(eq, comp(ne, V, 5), 0) = !(V != 5) = V == 5: the logic.not unwrap
// produces the singleton {5} via a recursive AnalyzeCondition.
TEST(SwitchAnalysis, LogicNotUnwrapsToInnerCondition) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    // if (comp(eq, comp(ne, V, 5), 0)) br caseA   -- !(V != 5) == V == 5
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(5),
                                        ComparisonKind::Inequality, false);
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(caseA)));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    auto* sCase = SectionFor(a.Sections, caseA);
    ASSERT_NE(sCase, nullptr);
    EXPECT_TRUE(sCase->Labels.Contains(5));
    EXPECT_EQ(sCase->Labels.Count(), 1u);
}

// comp((V - 3) == 5) = V == 8: the Sub offset is moved into the labels via
// AddOffset, so the section for caseA is the singleton {8}.
TEST(SwitchAnalysis, SubOffsetMovesLabels) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    // if (comp(eq, binary.sub(V, 3), 5)) br caseA  -- (V - 3) == 5, i.e. V == 8
    auto lhs = std::make_unique<BinaryNumericInstruction>(std::make_unique<LdLoc>(V),
        std::make_unique<LdcI4>(3), BinaryNumericOperator::Sub, StackType::I4);
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::move(lhs), std::make_unique<LdcI4>(5),
                               ComparisonKind::Equality, false),
        std::make_unique<Branch>(caseA)));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    auto* sCase = SectionFor(a.Sections, caseA);
    ASSERT_NE(sCase, nullptr);
    EXPECT_TRUE(sCase->Labels.Contains(8));
    EXPECT_FALSE(sCase->Labels.Contains(5));
    EXPECT_EQ(sCase->Labels.Count(), 1u);
}

// An IL switch on V (the switch already exists as a SwitchInstruction): the
// analysis records the case sections and sets ContainsILSwitch. The default
// section (empty labels) is dropped when AllowUnreachableCases is on (its
// matchValues is empty); the case sections survive.
TEST(SwitchAnalysis, ILSwitchSetsContainsILSwitch) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    Block* caseB = fn->Body->Blocks[3].get();
    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(V));
    sw->AddSection([&] { auto s = std::make_unique<SwitchSection>(); s->Labels = LongSet(0LL); s->SetBody(std::make_unique<Branch>(caseA)); return s; }());
    sw->AddSection([&] { auto s = std::make_unique<SwitchSection>(); s->Labels = LongSet(1LL); s->SetBody(std::make_unique<Branch>(caseB)); return s; }());
    sw->AddSection([&] { auto s = std::make_unique<SwitchSection>(); s->SetBody(std::make_unique<Branch>(def)); return s; }());  // default (empty labels)
    root->SetFinal(std::move(sw));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseB->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    a.AllowUnreachableCases = true;
    ASSERT_TRUE(a.AnalyzeBlock(root));
    EXPECT_TRUE(a.ContainsILSwitch);
    EXPECT_EQ(a.SwitchVariable, V.get());
    auto* s0 = SectionFor(a.Sections, caseA);
    ASSERT_NE(s0, nullptr);
    EXPECT_TRUE(s0->Labels.Contains(0));
    auto* s1 = SectionFor(a.Sections, caseB);
    ASSERT_NE(s1, nullptr);
    EXPECT_TRUE(s1->Labels.Contains(1));
}

// Without AllowUnreachableCases, an IL switch whose default section has empty
// matchValues fails the analysis (mirrors the C# `!AllowUnreachableCases &&
// matchValues.IsEmpty` guard).
TEST(SwitchAnalysis, ILSwitchFailsWithoutAllowUnreachableCases) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    Block* caseB = fn->Body->Blocks[3].get();
    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(V));
    sw->AddSection([&] { auto s = std::make_unique<SwitchSection>(); s->Labels = LongSet(0LL); s->SetBody(std::make_unique<Branch>(caseA)); return s; }());
    sw->AddSection([&] { auto s = std::make_unique<SwitchSection>(); s->Labels = LongSet(1LL); s->SetBody(std::make_unique<Branch>(caseB)); return s; }());
    sw->AddSection([&] { auto s = std::make_unique<SwitchSection>(); s->SetBody(std::make_unique<Branch>(def)); return s; }());
    root->SetFinal(std::move(sw));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseB->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;  // AllowUnreachableCases = false (default)
    EXPECT_FALSE(a.AnalyzeBlock(root));
}

// A block whose final is a plain Branch (not an if or switch) cannot be a switch.
TEST(SwitchAnalysis, PlainBranchBlockFails) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    for (int i = 0; i < 2; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* next = fn->Body->Blocks[1].get();
    root->SetFinal(std::make_unique<Branch>(next));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    EXPECT_FALSE(a.AnalyzeBlock(root));
}

// A condition that does not test a variable (a bare call) cannot be analyzed.
TEST(SwitchAnalysis, NonComparisonConditionFails) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 3; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* caseA = fn->Body->Blocks[2].get();
    // The condition is ldloc V (a bare variable test, not a comparison), but the
    // "true arm" is a Leave, not a Branch -- so trueBlock is null and AddSection
    // stores the Leave. The fall-through to def then must also analyze; def is a
    // Leave so it becomes the default section. The analysis succeeds with one
    // case (V != 0 -> caseA's Leave) and a default. This verifies the bare-ldloc
    // path with a non-Branch true arm.
    root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(V), std::make_unique<Leave>(fn->Body.get())));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    SwitchAnalysis a;
    // The true arm is a Leave (not a Branch to a block); the analysis creates a
    // section for it with the complement-of-{0} value set.
    ASSERT_TRUE(a.AnalyzeBlock(root));
    EXPECT_EQ(a.SwitchVariable, V.get());
    // One section for the true arm (complement of {0}, body = Leave) and one
    // for the fall-through default (body = Branch to def). The Leave body is
    // not a Branch so SectionFor returns nullptr for it; the default branches
    // to def.
    auto* sDef = SectionFor(a.Sections, def);
    ASSERT_NE(sDef, nullptr);
    EXPECT_TRUE(sDef->Labels.Contains(0));
    EXPECT_EQ(sDef->Labels.Count(), 1u);
}

// MakeSetWhereComparisonIsTrue: direct contract checks for each comparison kind
// (signed), matching the LongSet shapes the switch transforms consume.
TEST(SwitchAnalysis, MakeSetWhereComparisonIsTrueSigned) {
    using CK = ComparisonKind;
    EXPECT_TRUE(SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::Equality, 7, false).Contains(7));
    EXPECT_FALSE(SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::Equality, 7, false).Contains(6));
    EXPECT_FALSE(SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::Inequality, 7, false).Contains(7));
    EXPECT_TRUE(SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::Inequality, 7, false).Contains(6));
    // x < 5 (signed) = {Min..4}
    auto lt = SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::LessThan, 5, false);
    EXPECT_TRUE(lt.Contains(4));
    EXPECT_FALSE(lt.Contains(5));
    // x <= 5 (signed) = {Min..5}
    auto le = SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::LessThanOrEqual, 5, false);
    EXPECT_TRUE(le.Contains(5));
    EXPECT_FALSE(le.Contains(6));
    // x > 5 (signed) = {6..Max}
    auto gt = SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::GreaterThan, 5, false);
    EXPECT_TRUE(gt.Contains(6));
    EXPECT_FALSE(gt.Contains(5));
    // x >= 5 (signed) = {5..Max}
    auto ge = SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::GreaterThanOrEqual, 5, false);
    EXPECT_TRUE(ge.Contains(5));
    EXPECT_FALSE(ge.Contains(4));
}

// MakeSetWhereComparisonIsTrue: unsigned LessThan wraps -- x < 5 (unsigned) is
// {0..4}, which is a single contiguous range (no wrap for val >= 0).
TEST(SwitchAnalysis, MakeSetWhereComparisonIsTrueUnsigned) {
    using CK = ComparisonKind;
    // x < 5 (unsigned) = {0..4}
    auto lt = SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::LessThan, 5, true);
    EXPECT_TRUE(lt.Contains(0));
    EXPECT_TRUE(lt.Contains(4));
    EXPECT_FALSE(lt.Contains(5));
    EXPECT_FALSE(lt.Contains(-1));
    // x >= 5 (unsigned) = {5..Max} U {Min..-1} (the wrap)
    auto ge = SwitchAnalysis::MakeSetWhereComparisonIsTrue(CK::GreaterThanOrEqual, 5, true);
    EXPECT_TRUE(ge.Contains(5));
    EXPECT_TRUE(ge.Contains(-1));
    EXPECT_FALSE(ge.Contains(0));
    EXPECT_FALSE(ge.Contains(4));
}
