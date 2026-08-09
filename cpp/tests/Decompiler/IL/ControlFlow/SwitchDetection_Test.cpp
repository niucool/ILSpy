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
// SwitchDetection tests: the SimplifySwitchInstruction subset (de-dup,
// AdjustLabels, SortSwitchSections) and the full Run/ProcessBlock path that
// reconstructs a C# switch compiled to a sequence of if-statements (non-
// contiguous case labels) as a single SwitchInstruction via UseCSharpSwitch.
// The deferred pieces (MatchRoslynSwitchOnString, AddNullCase,
// InlineSwitchExpressionDefaultCaseThrowHelper) are skipped -- the former two
// need SwitchOnStringTransform / NullableLiftingTransform helpers, the latter
// needs IMethod/IType resolution.

#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ILReader.hpp"
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
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Util/LongSet.hpp"
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
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

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

// ---------------------------------------------------------------------------
// Run / ProcessBlock / UseCSharpSwitch -- the full SwitchDetection transform.
// ---------------------------------------------------------------------------

namespace {

ILVariablePtr MakeTypedLocal(std::string name, KnownTypeCode code) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local,
                                         std::make_shared<KnownType>(code), 0);
    v->Name = std::move(name);
    return v;
}

// Build an if-chain that tests one int variable: `if (V == 0) br caseA; if
// (V == 1) br caseB; [default] leave`. The blocks are laid out so the root's
// fall-through is the second if-test (an inner block), whose fall-through is
// the default/exit. caseA/caseB are the case targets. Returns the function,
// the root block, the inner (second if-test) block, and the case blocks.
struct IfChainSwitch {
    std::unique_ptr<ILFunction> fn;
    Block* root;    // if (V == 0) br caseA
    Block* inner;   // if (V == 1) br caseB  (absorbed by the switch)
    Block* def;     // leave (the default/exit)
    Block* caseA;
    Block* caseB;
    ILVariablePtr V;
};

IfChainSwitch BuildIfChainSwitch() {
    IfChainSwitch fx;
    fx.fn = std::make_unique<ILFunction>();
    fx.fn->Body = std::make_unique<BlockContainer>();
    fx.fn->Body->Parent = fx.fn.get();
    fx.fn->Body->ChildIndex = 0;
    fx.V = MakeTypedLocal("V", KnownTypeCode::Int32);
    fx.fn->Variables.push_back(fx.V);
    for (int i = 0; i < 5; ++i) fx.fn->Body->AddBlock(std::make_unique<Block>());
    fx.root = fx.fn->Body->Blocks[0].get();
    fx.inner = fx.fn->Body->Blocks[1].get();
    fx.def = fx.fn->Body->Blocks[2].get();
    fx.caseA = fx.fn->Body->Blocks[3].get();
    fx.caseB = fx.fn->Body->Blocks[4].get();
    // root: if (V == 0) br caseA; fall-through to inner
    fx.root->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(fx.V), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(fx.caseA), nullptr));
    // inner: if (V == 1) br caseB; fall-through to def
    fx.inner->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(fx.V), std::make_unique<LdcI4>(1),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(fx.caseB), nullptr));
    fx.def->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    fx.caseA->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    fx.caseB->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fx.fn);
    return fx;
}

} // namespace

// An if-chain `if (V == 0) br caseA; if (V == 1) br caseB; [default] leave` is
// reconstructed as a single SwitchInstruction with three sections ({0}->caseA,
// {1}->caseB, {complement-of-0,1}->def). The absorbed inner if-test block is
// removed; the switch value is the bare variable (Int32 -> no Conv widen).
TEST(SwitchDetection, RunReconstructsIfChainAsSwitch) {
    auto fx = BuildIfChainSwitch();
    ASSERT_EQ(fx.fn->Body->Blocks.size(), 5u);
    ILTransformContext ctx;
    SwitchDetection().Run(*fx.fn, ctx);

    ASSERT_TRUE(fx.root->FinalInstruction);
    ASSERT_EQ(fx.root->FinalInstruction->Op, OpCode::SwitchInstruction);
    auto* sw = static_cast<SwitchInstruction*>(fx.root->FinalInstruction.get());
    // The switch value is the bare variable (Int32 is I4, so no Conv widen).
    ASSERT_EQ(sw->Value->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(sw->Value.get())->Variable.get(), fx.V.get());
    // Three sections: {0}->caseA, {1}->caseB, {complement}->def.
    ASSERT_EQ(sw->Sections.size(), 3u);
    bool foundA = false, foundB = false, foundDef = false;
    for (const auto& s : sw->Sections) {
        Block* t = SectionTarget(*s);
        if (t == fx.caseA) {
            foundA = true;
            EXPECT_TRUE(s->Labels.Contains(0));
            EXPECT_EQ(s->Labels.Count(), 1u);
        } else if (t == fx.caseB) {
            foundB = true;
            EXPECT_TRUE(s->Labels.Contains(1));
            EXPECT_EQ(s->Labels.Count(), 1u);
        } else if (t == fx.def) {
            foundDef = true;
            // The default is the complement of {0,1}: huge, and not 0 or 1.
            EXPECT_FALSE(s->Labels.Contains(0));
            EXPECT_FALSE(s->Labels.Contains(1));
            EXPECT_GT(s->Labels.Count(), 100u);
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
    EXPECT_TRUE(foundDef);
    // The inner if-test block was absorbed and removed; the container now has
    // 4 blocks (root, def, caseA, caseB).
    EXPECT_EQ(fx.fn->Body->Blocks.size(), 4u);
    fx.fn->CheckInvariant(ILPhase::Normal);
}

// When SparseIntegerSwitch is off, Run is a no-op: the if-chain stays as ifs.
TEST(SwitchDetection, RunIsNoOpWhenSparseIntegerSwitchOff) {
    auto fx = BuildIfChainSwitch();
    ILTransformContext ctx;
    ctx.Settings.SparseIntegerSwitch = false;
    SwitchDetection().Run(*fx.fn, ctx);
    // The root's final is still the if (not a switch); the inner block remains.
    ASSERT_TRUE(fx.root->FinalInstruction);
    EXPECT_EQ(fx.root->FinalInstruction->Op, OpCode::IfInstruction);
    EXPECT_EQ(fx.fn->Body->Blocks.size(), 5u);
    fx.fn->CheckInvariant(ILPhase::Normal);
}

// A block that does not form a switch (a plain if/branch with no integer
// variable to switch on, or a single non-recursive block) is left alone -- Run
// falls through to the 2nd-pass SimplifySwitchInstruction (a no-op on a non-
// switch final).
TEST(SwitchDetection, RunLeavesNonSwitchBlockAlone) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeTypedLocal("V", KnownTypeCode::Int32);
    fn->Variables.push_back(V);
    for (int i = 0; i < 2; ++i) fn->Body->AddBlock(std::make_unique<Block>());
    Block* root = fn->Body->Blocks[0].get();
    Block* next = fn->Body->Blocks[1].get();
    root->SetFinal(std::make_unique<Branch>(next));
    next->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    RecomputeIncomingEdgeCounts(*fn);
    ILTransformContext ctx;
    SwitchDetection().Run(*fn, ctx);
    EXPECT_EQ(root->FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(fn->Body->Blocks.size(), 2u);
    fn->CheckInvariant(ILPhase::Normal);
}

// An existing IL SwitchInstruction (the reader emits one from a `switch`
// opcode) is handled by Run without crashing: with the default settings
// (RemoveDeadCode off), a switch with a default section is not rebuilt (the
// analysis bails on the empty default), so Run runs the 2nd-pass
// SimplifySwitchInstruction on it -- the switch stays a switch.
TEST(SwitchDetection, RunHandlesExistingILSwitch) {
    auto V = MakeTypedLocal("V", KnownTypeCode::Int32);
    auto b = BuildSwitchOnLdLoc(V, {{0, 10}, {1, 20}}, 30);
    ILTransformContext ctx;
    SwitchDetection().Run(*b.fn, ctx);
    // The root still ends in a SwitchInstruction.
    ASSERT_TRUE(b.root->FinalInstruction);
    EXPECT_EQ(b.root->FinalInstruction->Op, OpCode::SwitchInstruction);
    b.fn->CheckInvariant(ILPhase::Normal);
}

// On the real mscorlib corpus, running the full pre-pipeline through
// SwitchDetection.Run (at the GetILTransforms() position: after the second CFS,
// before LoopDetection) must not crash and must preserve the ILAst invariant
// across thousands of methods.
TEST(SwitchDetection, MscorlibRunSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int switchesBefore = 0;
    int switchesAfter = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Pre-pipeline through the second CFS, then SwitchDetection (the
        // GetILTransforms() position -- before LoopDetection).
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        EarlyExpressionTransforms().Run(*fn, ctx);
        RemoveDeadVariableInit().Run(*fn, ctx);
        ControlFlowSimplification().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::SwitchInstruction) ++switchesBefore;
        });
        SwitchDetection().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::SwitchInstruction) ++switchesAfter;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The reader emits real SwitchInstructions from `switch` opcodes; Run must
    // not drop them (the 2nd-pass SimplifySwitchInstruction keeps them, and the
    // if-chain reconstruction only adds switches).
    EXPECT_GE(switchesAfter, switchesBefore);
}
