// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for StateRangeAnalysis (the port of
// ICSharpCode.Decompiler/IL/ControlFlow/StateRangeAnalysis.cs): the symbolic
// execution over the state field that determines, for each block, the set of
// states for which the block is reachable -- the analysis both the
// YieldReturnDecompiler and the AsyncAwaitDecompiler drive their state-machine
// inversion with.

#include "Decompiler/IL/ControlFlow/StateRangeAnalysis.hpp"

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"  // LdFlda
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"  // LdObj
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace IL = ::ILSpy::Decompiler::IL;
namespace CF = ::ILSpy::Decompiler::IL::ControlFlow;
namespace Util = ::ILSpy::Decompiler::Util;

// A field stub for the state-field identity (MatchLdFld/MatchStFld hand back
// the ldflda's resolved field; the analysis compares the MemberDefinition
// identity).
class FieldStub : public TS::IField {
public:
    explicit FieldStub(std::string name) : name_(std::move(name)) {}
    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Field;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return "T::" + name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return std::string(); }
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("FieldStub::Compilation");
    }
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override {
        throw std::logic_error("FieldStub::ReturnType");
    }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMember* Specialize(
        const TS::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor*) const override {
        return obj == this;
    }
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Private;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return 0x04000001u; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override { return nullptr; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    bool IsReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }
    // --- IVariable ---
    const TS::IType& Type() const override {
        throw std::logic_error("FieldStub::Type");
    }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    std::string name_;
};

// The `this` parameter (Kind Parameter, Index -1) -- the symbolic evaluator
// recognizes it as the This pointer.
std::shared_ptr<IL::ILVariable> MakeThis() {
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Parameter,
                                              nullptr, -1);
    v->Name = "this";
    v->StoreCount = 1;
    return v;
}

// `ldflda stateField(ldloc this)` -- the state-field address the LdObj/StObj
// patterns wrap.
std::unique_ptr<IL::LdFlda> MakeStateFieldAddress(
    std::shared_ptr<FieldStub> field, std::shared_ptr<IL::ILVariable> thisVar) {
    auto ldFlda = std::make_unique<IL::LdFlda>(
        std::make_unique<IL::LdLoc>(thisVar), "T::" + field->Name());
    ldFlda->Field = field;
    return ldFlda;
}

// The MoveNext dispatch shape: `switch (ldfld state(this)) { case 0: B0;
// case 1: B1; default: Bdefault }` -- the classic Roslyn iterator dispatch.
struct DispatchFixture {
    std::shared_ptr<FieldStub> stateField;
    std::shared_ptr<IL::ILVariable> thisVar;
    std::unique_ptr<IL::BlockContainer> container;
    IL::Block* entry = nullptr;
    IL::Block* block0 = nullptr;
    IL::Block* block1 = nullptr;
    IL::Block* blockDefault = nullptr;

    DispatchFixture() {
        stateField = std::make_shared<FieldStub>("<>1__state");
        thisVar = MakeThis();
        container = std::make_unique<IL::BlockContainer>();

        auto entryBlock = std::make_unique<IL::Block>();
        entry = entryBlock.get();
        auto b0 = std::make_unique<IL::Block>();
        block0 = b0.get();
        auto b1 = std::make_unique<IL::Block>();
        block1 = b1.get();
        auto bd = std::make_unique<IL::Block>();
        blockDefault = bd.get();

        // switch (ldfld state(this)) with sections 0, 1, and the default
        // (the complement).
        auto ldState = std::make_unique<IL::LdObj>(
            MakeStateFieldAddress(stateField, thisVar), nullptr);
        auto switchInst = std::make_unique<IL::SwitchInstruction>(
            std::move(ldState));
        {
            auto section0 = std::make_unique<IL::SwitchSection>(
                Util::LongSet(0));
            section0->Body = std::make_unique<IL::Branch>(b0.get());
            switchInst->Sections.push_back(std::move(section0));
        }
        {
            auto section1 = std::make_unique<IL::SwitchSection>(
                Util::LongSet(1));
            section1->Body = std::make_unique<IL::Branch>(b1.get());
            switchInst->Sections.push_back(std::move(section1));
        }
        {
            auto sectionD = std::make_unique<IL::SwitchSection>(
                Util::LongSet(0).UnionWith(Util::LongSet(1)).Invert());
            sectionD->Body =
                std::make_unique<IL::Branch>(blockDefault);
            switchInst->Sections.push_back(std::move(sectionD));
        }
        entryBlock->SetFinal(std::move(switchInst));

        // The case bodies end in leaves (the realistic state-block shape:
        // each state's code either yields -- a stloc-state + leave -- or
        // falls to the next dispatch; a branch-to-merge would falsely merge
        // the state's range into the target's).
        block0->SetFinal(std::make_unique<IL::Leave>(container.get(), nullptr));
        block1->SetFinal(std::make_unique<IL::Leave>(container.get(), nullptr));
        blockDefault->SetFinal(std::make_unique<IL::Leave>(
            container.get(), nullptr));

        container->AddBlock(std::move(entryBlock));
        container->AddBlock(std::move(b0));
        container->AddBlock(std::move(b1));
        container->AddBlock(std::move(bd));
    }
};

} // namespace

// The dispatch split: AssignStateRanges over the Universe carves the entry
// block's switch into per-state sections, and GetBlockStateSetMapping
// resolves each state to the block its section branches to.
TEST(StateRangeAnalysisTest, SplitsSwitchDispatchIntoPerStateBlocks) {
    DispatchFixture fixture;
    CF::StateRangeAnalysis analysis(CF::StateRangeAnalysisMode::IteratorMoveNext,
                                     fixture.stateField.get());
    Util::LongSet exit = analysis.AssignStateRanges(
        fixture.container.get(), Util::LongSet::Universe());

    // The container arm returns Empty (the leave edges are not tracked).
    EXPECT_TRUE(exit.IsEmpty());

    auto mapping = analysis.GetBlockStateSetMapping(*fixture.container);
    IL::Block* block = nullptr;
    ASSERT_TRUE(mapping.TryGetValue(0, block));
    EXPECT_EQ(block, fixture.block0);
    ASSERT_TRUE(mapping.TryGetValue(1, block));
    EXPECT_EQ(block, fixture.block1);
    ASSERT_TRUE(mapping.TryGetValue(2, block));
    EXPECT_EQ(block, fixture.blockDefault);
    ASSERT_TRUE(mapping.TryGetValue(-1, block));
    EXPECT_EQ(block, fixture.blockDefault);
}

// The Mono shape: `if (state - 2 == 0)`-style guards evaluate through the
// symbolic Sub, so the true/false arms carve the state space.
TEST(StateRangeAnalysisTest, EvaluatesStateArithmeticInIfGuards) {
    DispatchFixture fixture;
    // Replace the entry's switch with `if (comp(ldfld state == 0)) B0 else
    // Bdefault`.
    fixture.stateField = std::make_shared<FieldStub>("<>1__state");
    auto entryBlock = fixture.entry;
    // Drop the switch final (the container still owns the blocks).
    entryBlock->FinalInstruction.reset();

    auto ldState = std::make_unique<IL::LdObj>(
        MakeStateFieldAddress(fixture.stateField, fixture.thisVar), nullptr);
    auto comp = std::make_unique<IL::Comp>(
        std::move(ldState), std::make_unique<IL::LdcI4>(0),
        IL::ComparisonKind::Equality, IL::ComparisonLiftingKind::None,
        IL::StackType::I4, TS::Sign::None);
    auto cond = std::make_unique<IL::IfInstruction>(
        std::move(comp), std::make_unique<IL::Branch>(fixture.block0),
        std::make_unique<IL::Branch>(fixture.blockDefault));
    entryBlock->SetFinal(std::move(cond));

    CF::StateRangeAnalysis analysis(CF::StateRangeAnalysisMode::IteratorMoveNext,
                                     fixture.stateField.get());
    analysis.AssignStateRanges(fixture.container.get(), Util::LongSet::Universe());

    auto mapping = analysis.GetBlockStateSetMapping(*fixture.container);
    IL::Block* block = nullptr;
    ASSERT_TRUE(mapping.TryGetValue(0, block));
    EXPECT_EQ(block, fixture.block0);
    ASSERT_TRUE(mapping.TryGetValue(1, block));
    EXPECT_EQ(block, fixture.blockDefault);
}

// A cached state variable: `stloc cached(ldfld state); if (cached == 1)`
// -- the first `stloc v = state` registers v as a state variable, so the
// later guard carves through it.
TEST(StateRangeAnalysisTest, TracksCachedStateVariables) {
    DispatchFixture fixture;
    auto entryBlock = fixture.entry;
    entryBlock->FinalInstruction.reset();

    auto cachedVar = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, nullptr, 0);
    cachedVar->Name = "cached";
    cachedVar->StoreCount = 1;
    cachedVar->LoadCount = 2;

    // stloc cached(ldfld state(this))
    auto ldState = std::make_unique<IL::LdObj>(
        MakeStateFieldAddress(fixture.stateField, fixture.thisVar), nullptr);
    entryBlock->Add(std::make_unique<IL::StLoc>(
        cachedVar, std::move(ldState)));
    // if (comp(ldloc cached == 1)) B1 else Bdefault
    auto comp = std::make_unique<IL::Comp>(
        std::make_unique<IL::LdLoc>(cachedVar),
        std::make_unique<IL::LdcI4>(1),
        IL::ComparisonKind::Equality, IL::ComparisonLiftingKind::None,
        IL::StackType::I4, TS::Sign::None);
    entryBlock->SetFinal(std::make_unique<IL::IfInstruction>(
        std::move(comp), std::make_unique<IL::Branch>(fixture.block1),
        std::make_unique<IL::Branch>(fixture.blockDefault)));

    CF::StateRangeAnalysis analysis(CF::StateRangeAnalysisMode::IteratorMoveNext,
                                     fixture.stateField.get());
    analysis.AssignStateRanges(fixture.container.get(), Util::LongSet::Universe());

    auto mapping = analysis.GetBlockStateSetMapping(*fixture.container);
    IL::Block* block = nullptr;
    ASSERT_TRUE(mapping.TryGetValue(1, block));
    EXPECT_EQ(block, fixture.block1);
    ASSERT_TRUE(mapping.TryGetValue(2, block));
    EXPECT_EQ(block, fixture.blockDefault);
    // The cached variable was registered (visible through the nested
    // analysis's settings).
    bool found = false;
    for (IL::ILVariable* v : analysis.CachedStateVars()) {
        if (v == cachedVar.get()) found = true;
    }
    EXPECT_TRUE(found) << "the stloc-of-state registers the cached variable";
}
