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

// SwitchDetection tests for the SimplifySwitchInstruction subset: de-dup
// sections branching to the same block, move an Add/Sub offset from the switch
// value into the labels (AdjustLabels), and sort the sections (SortSwitchSections).
// The full SwitchDetection.Run (UseCSharpSwitch/LoopContext/AddNullCase) is
// deferred pending its HighLevelLoopTransform / NullableLiftingTransform
// dependencies; these tests cover the self-contained piece CFS calls as its
// 1st pass, which runs on the SwitchInstructions the IL reader emits.

#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Util/LongSet.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Util::LongSet;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// The block a section's body branches to, or nullptr if the body is not a
// resolved Branch.
Block* SectionTarget(const SwitchSection& s) {
    if (!s.Body || s.Body->Op != OpCode::Branch) return nullptr;
    return static_cast<Branch*>(s.Body.get())->TargetBlock;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// Build a switch(V) on `V` whose sections branch to fresh case blocks. `cases`
// is (label, target StartILOffset) per case section, in insertion order; a
// default section (empty labels) branches to a def block at `defOffset`.
// Returns the function, the root block, the switch (non-owning), and the case
// blocks (in the same order as `cases`), plus the def block.
struct BuiltSwitch {
    std::unique_ptr<ILFunction> fn;
    Block* root;
    SwitchInstruction* sw;
    Block* def;
    std::vector<Block*> cases;
};

BuiltSwitch BuildSwitchOnLdLoc(ILVariablePtr V,
                               const std::vector<std::pair<long long, std::uint32_t>>& cases,
                               std::uint32_t defOffset) {
    BuiltSwitch b;
    b.fn = std::make_unique<ILFunction>();
    b.fn->Body = std::make_unique<BlockContainer>();
    b.fn->Body->Parent = b.fn.get();
    b.fn->Body->ChildIndex = 0;
    b.fn->Variables.push_back(V);
    const std::size_t nBlocks = 2 + cases.size();  // root + def + cases
    for (std::size_t i = 0; i < nBlocks; ++i) b.fn->Body->AddBlock(std::make_unique<Block>());
    b.root = b.fn->Body->Blocks[0].get();
    b.def = b.fn->Body->Blocks[1].get();
    b.def->StartILOffset = defOffset;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        Block* cb = b.fn->Body->Blocks[2 + i].get();
        cb->StartILOffset = cases[i].second;
        b.cases.push_back(cb);
    }
    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(V));
    for (std::size_t i = 0; i < cases.size(); ++i) {
        auto s = std::make_unique<SwitchSection>();
        s->Labels = LongSet(cases[i].first);
        s->SetBody(std::make_unique<Branch>(b.cases[i]));
        sw->AddSection(std::move(s));
    }
    auto defSection = std::make_unique<SwitchSection>();
    defSection->SetBody(std::make_unique<Branch>(b.def));  // default (empty labels)
    sw->AddSection(std::move(defSection));
    b.sw = sw.get();
    b.root->SetFinal(std::move(sw));
    b.def->SetFinal(std::make_unique<Leave>(b.fn->Body.get()));
    for (Block* cb : b.cases) cb->SetFinal(std::make_unique<Leave>(b.fn->Body.get()));
    RecomputeIncomingEdgeCounts(*b.fn);
    return b;
}

} // namespace

// Two sections branching to the same block merge into one: their label sets
// union, and the duplicate section is removed. The primary keeps the first
// section's body (a Branch to the shared target).
TEST(SwitchDetection, DeduplicatesSectionsBranchingToSameBlock) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get();
    Block* shared = fn->Body->Blocks[2].get();
    Block* other = fn->Body->Blocks[3].get();

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(V));
    auto addCase = [&](long long label, Block* target) {
        auto s = std::make_unique<SwitchSection>();
        s->Labels = LongSet(label);
        s->SetBody(std::make_unique<Branch>(target));
        sw->AddSection(std::move(s));
    };
    addCase(1, shared);
    addCase(2, shared);  // same target -> merges into the {1} section
    addCase(3, other);
    auto defSection = std::make_unique<SwitchSection>();
    defSection->SetBody(std::make_unique<Branch>(def));
    sw->AddSection(std::move(defSection));
    auto* swp = sw.get();
    root->SetFinal(std::move(sw));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    shared->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    other->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    ASSERT_EQ(swp->Sections.size(), 4u);
    ILTransformContext ctx;
    SwitchDetection::SimplifySwitchInstruction(root, ctx);
    ASSERT_EQ(swp->Sections.size(), 3u);  // the {2} section merged into {1}

    bool foundShared = false;
    for (const auto& s : swp->Sections) {
        if (SectionTarget(*s) == shared) {
            foundShared = true;
            EXPECT_TRUE(s->Labels.Contains(1));
            EXPECT_TRUE(s->Labels.Contains(2));
            EXPECT_EQ(s->Labels.Count(), 2u);
        }
    }
    EXPECT_TRUE(foundShared);
    fn->CheckInvariant(ILPhase::Normal);
}

// switch(V + 5): the +5 offset moves into the labels (Add offset = -5), so the
// switch value becomes the bare V and each case label shifts by -5. case 0 ->
// V == -5, case 1 -> V == -4.
TEST(SwitchDetection, AdjustLabelsMovesAddOffsetIntoLabels) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 4; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* def = fn->Body->Blocks[1].get(); def->StartILOffset = 30;
    Block* caseA = fn->Body->Blocks[2].get(); caseA->StartILOffset = 10;
    Block* caseB = fn->Body->Blocks[3].get(); caseB->StartILOffset = 20;

    auto sw = std::make_unique<SwitchInstruction>(
        std::make_unique<BinaryNumericInstruction>(std::make_unique<LdLoc>(V),
            std::make_unique<LdcI4>(5), BinaryNumericOperator::Add, StackType::I4));
    auto s1 = std::make_unique<SwitchSection>(); s1->Labels = LongSet(0LL);
    s1->SetBody(std::make_unique<Branch>(caseA));
    sw->AddSection(std::move(s1));
    auto s2 = std::make_unique<SwitchSection>(); s2->Labels = LongSet(1LL);
    s2->SetBody(std::make_unique<Branch>(caseB));
    sw->AddSection(std::move(s2));
    auto defSection = std::make_unique<SwitchSection>();
    defSection->SetBody(std::make_unique<Branch>(def));
    sw->AddSection(std::move(defSection));
    auto* swp = sw.get();
    root->SetFinal(std::move(sw));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseB->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    ILTransformContext ctx;
    SwitchDetection::SimplifySwitchInstruction(root, ctx);
    // The switch value is now the bare V (not a BinaryNumericInstruction).
    ASSERT_EQ(swp->Value->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(swp->Value.get())->Variable.get(), V.get());
    // Labels shifted by -5: {0}->{-5}, {1}->{-4}.
    bool foundA = false, foundB = false;
    for (const auto& s : swp->Sections) {
        Block* t = SectionTarget(*s);
        if (t == caseA) { EXPECT_TRUE(s->Labels.Contains(-5)); EXPECT_EQ(s->Labels.Count(), 1u); foundA = true; }
        if (t == caseB) { EXPECT_TRUE(s->Labels.Contains(-4)); EXPECT_EQ(s->Labels.Count(), 1u); foundB = true; }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
    fn->CheckInvariant(ILPhase::Normal);
}

// switch(V - 3): the -3 offset moves into the labels (Sub offset = +3), so case
// 5 -> V == 8.
TEST(SwitchDetection, AdjustLabelsMovesSubOffsetIntoLabels) {
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

    auto sw = std::make_unique<SwitchInstruction>(
        std::make_unique<BinaryNumericInstruction>(std::make_unique<LdLoc>(V),
            std::make_unique<LdcI4>(3), BinaryNumericOperator::Sub, StackType::I4));
    auto s1 = std::make_unique<SwitchSection>(); s1->Labels = LongSet(5LL);
    s1->SetBody(std::make_unique<Branch>(caseA));
    sw->AddSection(std::move(s1));
    auto defSection = std::make_unique<SwitchSection>();
    defSection->SetBody(std::make_unique<Branch>(def));
    sw->AddSection(std::move(defSection));
    auto* swp = sw.get();
    root->SetFinal(std::move(sw));
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    ILTransformContext ctx;
    SwitchDetection::SimplifySwitchInstruction(root, ctx);
    ASSERT_EQ(swp->Value->Op, OpCode::LdLoc);
    bool foundA = false;
    for (const auto& s : swp->Sections) {
        if (SectionTarget(*s) == caseA) {
            EXPECT_TRUE(s->Labels.Contains(8));  // 5 + 3
            EXPECT_FALSE(s->Labels.Contains(5));
            foundA = true;
        }
    }
    EXPECT_TRUE(foundA);
    fn->CheckInvariant(ILPhase::Normal);
}

// AdjustLabels is a no-op for a switch on the bare variable (no Add/Sub bop),
// for a Mul (not an offset op), and for an Add with overflow checking.
TEST(SwitchDetection, AdjustLabelsNoOpForNonOffsetValues) {
    auto build = [](BinaryNumericOperator op, bool overflow) {
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
        std::unique_ptr<ILInstruction> value;
        if (op == BinaryNumericOperator::None) {
            value = std::make_unique<LdLoc>(V);
        } else {
            auto bop = std::make_unique<BinaryNumericInstruction>(std::make_unique<LdLoc>(V),
                std::make_unique<LdcI4>(5), op, StackType::I4);
            bop->CheckForOverflow = overflow;
            value = std::move(bop);
        }
        auto sw = std::make_unique<SwitchInstruction>(std::move(value));
        auto s1 = std::make_unique<SwitchSection>(); s1->Labels = LongSet(7LL);
        s1->SetBody(std::make_unique<Branch>(caseA));
        sw->AddSection(std::move(s1));
        auto defSection = std::make_unique<SwitchSection>();
        defSection->SetBody(std::make_unique<Branch>(def));
        sw->AddSection(std::move(defSection));
        auto* swp = sw.get();
        root->SetFinal(std::move(sw));
        def->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        caseA->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        RecomputeIncomingEdgeCounts(*fn);
        return std::make_tuple(std::move(fn), root, swp, caseA);
    };

    ILTransformContext ctx;
    auto runAndCheck = [&](BinaryNumericOperator op, bool overflow, bool expectBop) {
        auto [fn, root, swp, caseA] = build(op, overflow);
        SwitchDetection::SimplifySwitchInstruction(root, ctx);
        if (expectBop)
            EXPECT_EQ(swp->Value->Op, OpCode::BinaryNumericInstruction);
        else
            EXPECT_EQ(swp->Value->Op, OpCode::LdLoc);
        for (const auto& s : swp->Sections)
            if (SectionTarget(*s) == caseA) EXPECT_TRUE(s->Labels.Contains(7));
        fn->CheckInvariant(ILPhase::Normal);
    };
    runAndCheck(BinaryNumericOperator::None, false, false);  // bare V
    runAndCheck(BinaryNumericOperator::Mul, false, true);    // Mul: not an Add/Sub offset
    runAndCheck(BinaryNumericOperator::Add, true, true);     // Add.ovf: skipped (!CheckForOverflow)
}

// SortSwitchSections (default setting off): sections are reordered by their
// branch-target IL offset, preserving the original case order across rebuilds.
TEST(SwitchDetection, SortsSectionsByBranchTargetOffset) {
    auto V = MakeLocal("V");
    // Cases in insertion order (label, offset): {2,30}, {0,10}, {1,20}. After
    // the offset sort they order 10,20,30 -> labels {0},{1},{2}; default(40) last.
    auto b = BuildSwitchOnLdLoc(V, {{2, 30}, {0, 10}, {1, 20}}, 40);
    ILTransformContext ctx;  // SortSwitchSections = false (default)
    SwitchDetection::SimplifySwitchInstruction(b.root, ctx);
    ASSERT_EQ(b.sw->Sections.size(), 4u);
    EXPECT_EQ(SectionTarget(*b.sw->Sections[0]), b.cases[1]);  // offset 10, label 0
    EXPECT_TRUE(b.sw->Sections[0]->Labels.Contains(0));
    EXPECT_EQ(SectionTarget(*b.sw->Sections[1]), b.cases[2]);  // offset 20, label 1
    EXPECT_TRUE(b.sw->Sections[1]->Labels.Contains(1));
    EXPECT_EQ(SectionTarget(*b.sw->Sections[2]), b.cases[0]);  // offset 30, label 2
    EXPECT_TRUE(b.sw->Sections[2]->Labels.Contains(2));
    EXPECT_EQ(SectionTarget(*b.sw->Sections[3]), b.def);       // default, offset 40
    EXPECT_TRUE(b.sw->Sections[3]->Labels.IsEmpty());
    b.fn->CheckInvariant(ILPhase::Normal);
}

// SortSwitchSections on (setting true): sections are ordered by label value
// instead of by branch-target offset.
TEST(SwitchDetection, SortsSectionsByLabelValueWhenSettingOn) {
    auto V = MakeLocal("V");
    auto b = BuildSwitchOnLdLoc(V, {{2, 30}, {0, 10}, {1, 20}}, 40);
    ILTransformContext ctx;
    ctx.Settings.SortSwitchSections = true;
    SwitchDetection::SimplifySwitchInstruction(b.root, ctx);
    ASSERT_EQ(b.sw->Sections.size(), 4u);
    // By label value: default(0), caseA(0), caseB(1), caseC(2). The default and
    // the label-0 case both have labelFirst 0; stable sort keeps the default
    // (inserted last but labelFirst 0 ties with label 0) -- the case {0} and the
    // default both sort at 0, stable order preserves their relative insertion
    // order (case {0} was inserted before the default), so case {0} comes first.
    EXPECT_EQ(SectionTarget(*b.sw->Sections[0]), b.cases[1]);  // label 0
    EXPECT_EQ(SectionTarget(*b.sw->Sections[1]), b.def);       // default (labelFirst 0)
    EXPECT_EQ(SectionTarget(*b.sw->Sections[2]), b.cases[2]);   // label 1
    EXPECT_EQ(SectionTarget(*b.sw->Sections[3]), b.cases[0]);   // label 2
    b.fn->CheckInvariant(ILPhase::Normal);
}

// A block that does not end in a SwitchInstruction is left untouched.
TEST(SwitchDetection, NoOpForBlockWithoutSwitch) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V");
    fn->Variables.push_back(V);
    for (int i = 0; i < 2; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* next = fn->Body->Blocks[1].get();
    root->SetFinal(std::make_unique<Branch>(next));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);

    ILTransformContext ctx;
    SwitchDetection::SimplifySwitchInstruction(root, ctx);  // must not crash
    EXPECT_EQ(root->FinalInstruction->Op, OpCode::Branch);
    fn->CheckInvariant(ILPhase::Normal);
}

// On the real mscorlib corpus the CFS first-pass SimplifySwitchInstruction (now
// wired into ControlFlowSimplification) must not violate the invariant across
// thousands of methods, and the SwitchInstructions the reader emits from
// `switch` opcodes are exercised (sections get de-duped/adjusted/sorted).
TEST(SwitchDetection, MscorlibCfsSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int switchMethods = 0;
    int totalSectionsBefore = 0;
    int totalSectionsAfter = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        int swBefore = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::SwitchInstruction) {
                ++swBefore;
                totalSectionsBefore += static_cast<SwitchInstruction*>(i)->Sections.size();
            }
        });
        if (swBefore > 0) ++switchMethods;
        ControlFlowSimplification().Run(*fn, ctx);  // calls SimplifySwitchInstruction
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::SwitchInstruction)
                totalSectionsAfter += static_cast<SwitchInstruction*>(i)->Sections.size();
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // mscorlib has real `switch` opcodes (e.g. System.Convert, System.Char),
    // so the transform is exercised; de-dup can only reduce the section count.
    EXPECT_GT(switchMethods, 0);
    EXPECT_LE(totalSectionsAfter, totalSectionsBefore);
}
