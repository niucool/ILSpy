// Tests for the reaching-definitions engine (the C#
// ReachingDefinitionsVisitor/DataFlowVisitor pair) and the SplitVariables
// transform it drives: the per-load reaching stores, and the live-range
// split of a local with two disjoint def-use chains.

#include "Decompiler/FlowAnalysis/ReachingDefinitionsVisitor.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/SplitVariables.hpp"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace ILSpy::Decompiler::IL;

// A probe visitor: records, per load marker, the reaching stores' marker
// values (the markers ride the stores' LdcI4 payloads).
class LoadProbeVisitor final : public ReachingDefinitionsVisitor {
public:
    LoadProbeVisitor(ILFunction& scope)
        : ReachingDefinitionsVisitor(
              scope, [](const ILVariable* v) { return v->Name == "v"; }) {}

    // (loadMarker, storeMarker) pairs in walk order.
    std::vector<std::pair<int, int>> observations;
    std::map<const LdLoc*, int> loadMarkers;

    void OnLdLoc(LdLoc* inst) override {
        if (inst->Variable == nullptr || inst->Variable->Name != "v") return;
        auto markerIt = loadMarkers.find(inst);
        if (markerIt == loadMarkers.end()) return;
        for (ILInstruction* store : GetStoresFor(state, inst->Variable.get())) {
            auto* st = static_cast<StLoc*>(store);
            auto* ldc = static_cast<LdcI4*>(st->Value.get());
            observations.emplace_back(markerIt->second, ldc->Value);
        }
    }
};

// A tree walk collecting the StLoc/LdLoc nodes (the fixture probe helpers
// re-find the moved nodes after the container takes ownership).
struct NodeFinder {
    std::vector<StLoc*> stores;
    std::vector<LdLoc*> loads;
    void Walk(ILInstruction* inst) {
        if (inst == nullptr) return;
        if (auto* st = dynamic_cast<StLoc*>(inst)) stores.push_back(st);
        if (auto* ld = dynamic_cast<LdLoc*>(inst)) loads.push_back(ld);
        for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i));
    }
};

// `stloc v(1); use(ldloc v); stloc v(2); use(ldloc v)` in one block: the
// engine sees only store 1 at the first use and only store 2 at the second
// (the second store kills the first from its position on).
TEST(ReachingDefinitions, LinearStoresKillPreviousDefinitions) {
    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto entry = std::make_unique<Block>();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "v";
    fn->Variables.push_back(v);

    entry->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    entry->Add(std::make_unique<LdLoc>(v));
    entry->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    entry->Add(std::make_unique<LdLoc>(v));
    // The function body exits through a Leave (the container's exit shape;
    // a target-less Branch would read as an unsupported dump shape).
    entry->SetFinal(std::make_unique<Leave>(container.get(), nullptr));
    container->AddBlock(std::move(entry));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    NodeFinder finder;
    finder.Walk(fn->Body.get());
    ASSERT_EQ(finder.stores.size(), 2u);
    ASSERT_EQ(finder.loads.size(), 2u);
    // Tag the stores' payloads and the loads (the chain markers).
    finder.stores[0]->Value = std::make_unique<LdcI4>(1);
    finder.stores[1]->Value = std::make_unique<LdcI4>(2);

    LoadProbeVisitor probe(*fn);
    probe.loadMarkers[finder.loads[0]] = 1;
    probe.loadMarkers[finder.loads[1]] = 2;
    ASSERT_TRUE(probe.Analyze());
    ASSERT_EQ(probe.observations.size(), 2u);
    EXPECT_EQ(probe.observations[0].first, 1);
    EXPECT_EQ(probe.observations[0].second, 1);
    EXPECT_EQ(probe.observations[1].first, 2);
    EXPECT_EQ(probe.observations[1].second, 2);
}

// A diamond: v is stored before the branch and again on the false arm; the
// join load sees both reaching stores.
TEST(ReachingDefinitions, BranchStoresJoinAtMergePoint) {
    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto entry = std::make_unique<Block>();
    auto join = std::make_unique<Block>();
    Block* joinPtr = join.get();
    BlockContainer* containerPtr = container.get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "v";
    fn->Variables.push_back(v);

    auto trueBlock = std::make_unique<Block>();
    trueBlock->SetFinal(std::make_unique<Branch>(joinPtr));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    falseBlock->SetFinal(std::make_unique<Branch>(joinPtr));
    // The condition is a load of a second variable (non-constant, so both
    // arms are reachable -- a constant-true condition would fold the false
    // arm to an unreachable path, which the engine correctly treats as
    // never-executed).
    auto condVar = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 1);
    condVar->Name = "cond";
    fn->Variables.push_back(condVar);
    auto ifInst = std::make_unique<IfInstruction>(std::make_unique<LdLoc>(condVar),
                                                  std::move(trueBlock),
                                                  std::move(falseBlock));
    entry->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    entry->Add(std::move(ifInst));
    entry->SetFinal(std::make_unique<Branch>(joinPtr));
    join->Add(std::make_unique<LdLoc>(v));
    join->SetFinal(std::make_unique<Leave>(containerPtr, nullptr));

    container->AddBlock(std::move(entry));
    container->AddBlock(std::move(join));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    NodeFinder finder;
    finder.Walk(fn->Body.get());
    // Two LdLoc nodes: the condition's cond load and the join's v load.
    ASSERT_EQ(finder.loads.size(), 2u);
    ASSERT_EQ(finder.loads[0]->Variable->Name, "cond");
    ASSERT_EQ(finder.loads[1]->Variable->Name, "v");
    LoadProbeVisitor probe(*fn);
    probe.loadMarkers[finder.loads[1]] = 9;
    ASSERT_TRUE(probe.Analyze());
    ASSERT_EQ(probe.observations.size(), 2u);
    // Both stores reach the join (the markers 1 and 2).
    EXPECT_EQ(probe.observations[0].second, 1);
    EXPECT_EQ(probe.observations[1].second, 2);
    EXPECT_EQ(probe.observations[0].first, 9);
    EXPECT_EQ(probe.observations[1].first, 9);
}


// The SplitVariables transform: a Local with two disjoint def-use chains
// (each chain's stores and loads grouped by the reaching-definitions
// analysis) splits into two variables.
TEST(SplitVariables, SplitsDisjointDefUseChains) {
    // Block A: stloc v(1); use A1; br B.
    // Block B: stloc v(2); use B1; leave.
    // The RD analysis groups {A's store, A's use} and {B's store, B's use};
    // each group becomes its own variable named "v".
    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    BlockContainer* containerPtr = container.get();
    auto blockA = std::make_unique<Block>();
    Block* joinPtrUnused = nullptr;
    (void)joinPtrUnused;
    auto blockB = std::make_unique<Block>();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "v";
    fn->Variables.push_back(v);

    blockA->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    blockA->Add(std::make_unique<LdLoc>(v));
    blockA->SetFinal(std::make_unique<Branch>(blockB.get()));
    blockB->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    blockB->Add(std::make_unique<LdLoc>(v));
    blockB->SetFinal(std::make_unique<Leave>(containerPtr, nullptr));
    container->AddBlock(std::move(blockA));
    container->AddBlock(std::move(blockB));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    ILTransformContext ctx;
    SplitVariables().Run(*fn, ctx);

    // The two chains split: the function now carries two variables named v
    // (the original plus the split copy), and each use points at its own
    // chain's variable.
    NodeFinder finder;
    finder.Walk(fn->Body.get());
    ASSERT_EQ(finder.loads.size(), 2u);
    const ILVariable* first = finder.loads[0]->Variable.get();
    const ILVariable* second = finder.loads[1]->Variable.get();
    EXPECT_NE(first, second) << "the two chains use distinct variables";
    EXPECT_EQ(first->Name, "v");
    EXPECT_EQ(second->Name, "v");
    // Each split variable is single-assignment: one store, one load.
    EXPECT_EQ(first->StoreCount, 1);
    EXPECT_EQ(first->LoadCount, 1);
    EXPECT_EQ(second->StoreCount, 1);
    EXPECT_EQ(second->LoadCount, 1);
}

} // namespace
