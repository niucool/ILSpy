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
// THE SOFTWARE IS PROVIDED "AS IS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for PatternMatchingTransform: the C# 7.0 `is` patterns (a type test
// plus a variable capture) rewrite the isinst + null-test block tail into a
// single MatchInstruction condition. Two shapes (PatternMatchValueTypes with
// isinst+unbox.any, PatternMatchRefTypes with stloc V(isinst)+null-check), the
// null-direction swap, the double-store form, negatives, the setting gate, and
// an 8000-method mscorlib sweep preserving the ILAst invariant. Adapted to this
// port's if-as-final block model (the if is the block's FinalInstruction; the
// false arm is the next block in the container).

#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/OpCode.hpp"
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
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TypeParameter;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeStackSlot(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, std::move(type), -1);
    v->Name = std::move(name);
    return v;
}

// comp(arg OP ldnull) for the null-check direction.
std::unique_ptr<Comp> MakeNullComp(ComparisonKind kind, std::unique_ptr<ILInstruction> arg) {
    return std::make_unique<Comp>(std::move(arg), std::make_unique<LdNull>(), kind, false);
}

// logic.not(X) = comp(Equality, X, LdcI4(0)).
std::unique_ptr<Comp> MakeLogicNot(std::unique_ptr<ILInstruction> x) {
    return std::make_unique<Comp>(std::move(x), std::make_unique<LdcI4>(0),
                                  ComparisonKind::Equality, false);
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// A shared fixture: a function body with `count` blocks, each ending in a leave
// of the body (the caller overrides the relevant block finals).
struct Blocks {
    ILFunction* fn;
    std::vector<Block*> b;
};

Blocks AddBlocks(ILFunction& fn, int count) {
    Blocks r{&fn, {}};
    for (int i = 0; i < count; ++i) {
        fn.Body->AddBlock(std::make_unique<Block>());
        r.b.push_back(fn.Body->Blocks[static_cast<std::size_t>(i)].get());
    }
    // Default every block to a leave of the body; the caller replaces the
    // ones that need an if/switch final.
    for (Block* blk : r.b) blk->SetFinal(std::make_unique<Leave>(fn.Body.get()));
    return r;
}

// Run the full pre-pipeline through PatternMatchingTransform (the GetILTransforms()
// position -- after LoopDetection, before ConditionDetection), so the sweep and
// the integration tests exercise the realistic pipeline shape.
void RunPrePipeline(ILFunction& fn, ILTransformContext& ctx) {
    ControlFlowSimplification().Run(fn, ctx);
    StObjToStLoc().Run(fn, ctx);
    ILInlining().Run(fn, ctx);
    InlineReturnTransform().Run(fn, ctx);
    RemoveInfeasiblePathTransform().Run(fn, ctx);
    DetectPinnedRegions().Run(fn, ctx);
    DetectCatchWhenConditionBlocks().Run(fn, ctx);
    LdLocaDupInitObjTransform().Run(fn, ctx);
    EarlyExpressionTransforms().Run(fn, ctx);
    RemoveDeadVariableInit().Run(fn, ctx);
    ControlFlowSimplification().Run(fn, ctx);
    SwitchDetection().Run(fn, ctx);
    SwitchOnNullableTransform().Run(fn, ctx);
    LoopDetection().Run(fn, ctx);
}

} // namespace

// ---- PatternMatchValueTypes ----

// `if (isinst T(x) == null) br falseBlock; br unboxBlock` where unboxBlock is
// `stloc V(unbox.any T(x)); ...`. Folds to `if (match.type[T].notnull(V = x))
// br unboxBlock; br falseBlock`. The stloc V(unbox.any) in unboxBlock is folded
// into the MatchInstruction (dropped from unboxBlock).
TEST(PatternMatchingTransform, ValueTypePatternFoldsIsInstUnboxAny) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    auto V2 = MakeLocal("V2", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    fn->Variables.push_back(V2);
    auto blocks = AddBlocks(*fn, 3);  // root, unboxBlock, falseBlock
    Block* root = blocks.b[0];
    Block* unboxBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    // root: if (comp(isinst T(ldloc x) == ldnull)) br falseBlock
    //       (fall-through to unboxBlock)
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality,
                     std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))),
        std::make_unique<Branch>(falseBlock), nullptr));
    // unboxBlock: stloc V(unbox.any T(ldloc x)); stloc V2(ldloc V); leave
    unboxBlock->Add(std::make_unique<StLoc>(V,
        std::make_unique<UnboxAny>(T, std::make_unique<LdLoc>(x))));
    unboxBlock->Add(std::make_unique<StLoc>(V2, std::make_unique<LdLoc>(V)));
    // falseBlock: leave (already set)

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ASSERT_EQ(unboxBlock->IncomingEdgeCount, 1);

    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    ASSERT_TRUE(root->FinalInstruction);
    ASSERT_EQ(root->FinalInstruction->Op, OpCode::IfInstruction);
    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    ASSERT_EQ(iff->Condition->Op, OpCode::MatchInstruction);
    auto* m = static_cast<MatchInstruction*>(iff->Condition.get());
    EXPECT_TRUE(m->CheckType);
    EXPECT_TRUE(m->CheckNotNull);
    EXPECT_EQ(m->Variable.get(), V.get());
    // TestedOperand is the original ldloc x.
    ASSERT_EQ(m->TestedOperand->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(m->TestedOperand.get())->Variable.get(), x.get());
    // The matched arm branches to unboxBlock; the null arm to falseBlock.
    ASSERT_EQ(iff->TrueInst->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(iff->TrueInst.get())->TargetBlock, unboxBlock);
    ASSERT_EQ(iff->FalseInst->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(iff->FalseInst.get())->TargetBlock, falseBlock);
    // The stloc V(unbox.any) was folded into the pattern (removed from unboxBlock);
    // the stloc V2(ldloc V) stays.
    ASSERT_EQ(unboxBlock->Instructions.size(), 1u);
    EXPECT_EQ(unboxBlock->Instructions[0]->Op, OpCode::StLoc);
    // The capture variable is now a PatternLocal.
    EXPECT_EQ(V->Kind, VariableKind::PatternLocal);
    fn->CheckInvariant(ILPhase::Normal);
}

// The boxed-generic variant: the isinst tests `box Tparam(ldloc x)` (the C#
// `x is T y` codegen for a type parameter T). The MatchInstruction's
// TestedOperand is the inner ldloc x, not the box.
TEST(PatternMatchingTransform, ValueTypePatternUnwrapsBoxedTypeParameter) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto TParam = std::make_shared<TypeParameter>(0,
        TypeParameter::OwnerKind::Method, std::string("T"));
    auto x = MakeLocal("x", TParam);
    auto V = MakeLocal("V", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* unboxBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality,
                     std::make_unique<IsInst>(T,
                         std::make_unique<Box>(TParam, std::make_unique<LdLoc>(x)))),
        std::make_unique<Branch>(falseBlock), nullptr));
    unboxBlock->Add(std::make_unique<StLoc>(V,
        std::make_unique<UnboxAny>(T,
            std::make_unique<Box>(TParam, std::make_unique<LdLoc>(x)))));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    ASSERT_EQ(iff->Condition->Op, OpCode::MatchInstruction);
    auto* m = static_cast<MatchInstruction*>(iff->Condition.get());
    // The MatchInstruction's TestedOperand is the isinst's argument (the box of
    // the type parameter); the C# keeps the box in the pattern and only extracts
    // testedVariable from inside it. The box's argument is the ldloc x.
    ASSERT_EQ(m->TestedOperand->Op, OpCode::Box);
    auto* bx = static_cast<Box*>(m->TestedOperand.get());
    ASSERT_EQ(bx->Argument->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(bx->Argument.get())->Variable.get(), x.get());
    EXPECT_EQ(V->Kind, VariableKind::PatternLocal);
    fn->CheckInvariant(ILPhase::Normal);
}

// The temp-store variant: `stloc temp(ldloc x); if (isinst T(x) == null) br
// falseBlock; br unboxBlock` where unboxBlock is `stloc V(unbox.any T(ldloc temp))`
// -- the unbox uses the copy `temp`, not the original `x`. The tempStore
// (`stloc temp(ldloc x)`) is dropped, and the pattern captures `x` (the original
// tested value). Guards the MatchLdLocVar fix (the extract form would clobber
// testedVariable when the back instruction is a copy).
TEST(PatternMatchingTransform, ValueTypePatternFoldsTempStoreForm) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto temp = MakeLocal("temp", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(temp);
    fn->Variables.push_back(V);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* unboxBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    // root: stloc temp(ldloc x); if (comp(isinst T(ldloc x) == ldnull)) br falseBlock
    root->Add(std::make_unique<StLoc>(temp, std::make_unique<LdLoc>(x)));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality,
                     std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))),
        std::make_unique<Branch>(falseBlock), nullptr));
    // unboxBlock: stloc V(unbox.any T(ldloc temp)); leave
    unboxBlock->Add(std::make_unique<StLoc>(V,
        std::make_unique<UnboxAny>(T, std::make_unique<LdLoc>(temp))));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ASSERT_EQ(temp->LoadCount, 1);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    ASSERT_EQ(iff->Condition->Op, OpCode::MatchInstruction);
    auto* m = static_cast<MatchInstruction*>(iff->Condition.get());
    // The pattern captures the original tested value `x` (not the temp copy).
    ASSERT_EQ(m->TestedOperand->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(m->TestedOperand.get())->Variable.get(), x.get());
    // The temp store and the unbox store were both absorbed; root and unboxBlock
    // have no non-terminal instructions left.
    EXPECT_TRUE(root->Instructions.empty());
    EXPECT_TRUE(unboxBlock->Instructions.empty());
    EXPECT_EQ(V->Kind, VariableKind::PatternLocal);
    fn->CheckInvariant(ILPhase::Normal);
}

// A non-isinst condition (a plain null check on a variable) does not match the
// value-type pattern; the block is left untouched.
TEST(PatternMatchingTransform, ValueTypeRejectsNonIsInstCondition) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* unboxBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    // if (ldloc x == null) -- not an isinst test.
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality, std::make_unique<LdLoc>(x)),
        std::make_unique<Branch>(falseBlock), nullptr));
    unboxBlock->Add(std::make_unique<StLoc>(V,
        std::make_unique<UnboxAny>(T, std::make_unique<LdLoc>(x))));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    // Untouched: the final is still the original if (not a MatchInstruction).
    ASSERT_EQ(root->FinalInstruction->Op, OpCode::IfInstruction);
    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    EXPECT_NE(iff->Condition->Op, OpCode::MatchInstruction);
    EXPECT_EQ(V->Kind, VariableKind::Local);
    fn->CheckInvariant(ILPhase::Normal);
}

// ---- PatternMatchRefTypes ----

// `stloc V(isinst T(x)); if (V == null) br falseBlock; br trueBlock` with T a
// reference type. Folds to `if (match.type[T].notnull(V = x)) br trueBlock;
// br falseBlock`. The stloc V(isinst) is removed; V becomes a PatternLocal.
TEST(PatternMatchingTransform, RefTypePatternFoldsIsInstNullCheck) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::String);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    auto V3 = MakeLocal("V3", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    fn->Variables.push_back(V3);
    auto blocks = AddBlocks(*fn, 3);  // root, trueBlock, falseBlock
    Block* root = blocks.b[0];
    Block* trueBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    // root: stloc V(isinst T(ldloc x)); if (V == null) br falseBlock
    //       (fall-through to trueBlock)
    root->Add(std::make_unique<StLoc>(V,
        std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality, std::make_unique<LdLoc>(V)),
        std::make_unique<Branch>(falseBlock), nullptr));
    // trueBlock: stloc V3(ldloc V); leave (so V is used in the matched path)
    trueBlock->Add(std::make_unique<StLoc>(V3, std::make_unique<LdLoc>(V)));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    ASSERT_EQ(root->FinalInstruction->Op, OpCode::IfInstruction);
    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    ASSERT_EQ(iff->Condition->Op, OpCode::MatchInstruction);
    auto* m = static_cast<MatchInstruction*>(iff->Condition.get());
    EXPECT_TRUE(m->CheckType);
    EXPECT_TRUE(m->CheckNotNull);
    EXPECT_EQ(m->Variable.get(), V.get());
    ASSERT_EQ(m->TestedOperand->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(m->TestedOperand.get())->Variable.get(), x.get());
    // Matched arm -> trueBlock, null arm -> falseBlock.
    EXPECT_EQ(static_cast<Branch*>(iff->TrueInst.get())->TargetBlock, trueBlock);
    EXPECT_EQ(static_cast<Branch*>(iff->FalseInst.get())->TargetBlock, falseBlock);
    // The stloc V(isinst) was absorbed; root has no non-terminal instructions.
    EXPECT_TRUE(root->Instructions.empty());
    EXPECT_EQ(V->Kind, VariableKind::PatternLocal);
    fn->CheckInvariant(ILPhase::Normal);
}

// The `!= null` direction: `if (V != null) br trueBlock; br falseBlock` keeps
// the null path as the positional fall-through (no explicit FalseInst).
TEST(PatternMatchingTransform, RefTypePatternNotEqualsNullKeepsFallThrough) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::String);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    auto V3 = MakeLocal("V3", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    fn->Variables.push_back(V3);
    // Order: root, falseBlock (fall-through, null), trueBlock (branch, matched).
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* falseBlock = blocks.b[1];
    Block* trueBlock = blocks.b[2];

    root->Add(std::make_unique<StLoc>(V,
        std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Inequality, std::make_unique<LdLoc>(V)),
        std::make_unique<Branch>(trueBlock), nullptr));
    trueBlock->Add(std::make_unique<StLoc>(V3, std::make_unique<LdLoc>(V)));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    ASSERT_EQ(iff->Condition->Op, OpCode::MatchInstruction);
    // Matched arm -> trueBlock (explicit), null arm is the positional
    // fall-through (falseBlock == next block), so FalseInst stays null.
    EXPECT_EQ(static_cast<Branch*>(iff->TrueInst.get())->TargetBlock, trueBlock);
    EXPECT_EQ(iff->FalseInst, nullptr);
    EXPECT_EQ(V->Kind, VariableKind::PatternLocal);
    fn->CheckInvariant(ILPhase::Normal);
}

// The double-store form: `stloc s(isinst T(x)); stloc v(ldloc s);
// if (logic.not(comp(s != null))) br falseBlock; br trueBlock` folds with the
// isinst captured into v.
TEST(PatternMatchingTransform, RefTypePatternFoldsDoubleStoreForm) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::String);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto s = MakeStackSlot("s", T);
    auto v = MakeLocal("v", T);
    auto v3 = MakeLocal("v3", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(s);
    fn->Variables.push_back(v);
    fn->Variables.push_back(v3);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* trueBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    // root: stloc s(isinst T(ldloc x)); stloc v(ldloc s);
    //       if (logic.not(comp(ldloc s != null))) br falseBlock
    root->Add(std::make_unique<StLoc>(s,
        std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))));
    root->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(s)));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeNullComp(ComparisonKind::Inequality, std::make_unique<LdLoc>(s))),
        std::make_unique<Branch>(falseBlock), nullptr));
    trueBlock->Add(std::make_unique<StLoc>(v3, std::make_unique<LdLoc>(v)));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    // s is loaded twice (in the stloc v and in the null check) and stored once.
    ASSERT_EQ(s->LoadCount, 2);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    ASSERT_EQ(iff->Condition->Op, OpCode::MatchInstruction);
    auto* m = static_cast<MatchInstruction*>(iff->Condition.get());
    EXPECT_EQ(m->Variable.get(), v.get());
    ASSERT_EQ(m->TestedOperand->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(m->TestedOperand.get())->Variable.get(), x.get());
    EXPECT_TRUE(root->Instructions.empty());
    EXPECT_EQ(v->Kind, VariableKind::PatternLocal);
    fn->CheckInvariant(ILPhase::Normal);
}

// A value-type T (Struct) is not a reference type, so the ref-type pattern
// rejects it; the block is left untouched.
TEST(PatternMatchingTransform, RefTypeRejectsNonReferenceType) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::Int32);  // Struct
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];

    root->Add(std::make_unique<StLoc>(V,
        std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality, std::make_unique<LdLoc>(V)),
        std::make_unique<Branch>(blocks.b[2]), nullptr));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    EXPECT_NE(iff->Condition->Op, OpCode::MatchInstruction);
    EXPECT_EQ(V->Kind, VariableKind::Local);
    fn->CheckInvariant(ILPhase::Normal);
}

// A use of V outside the matched region (in the null-path block) is not
// dominated by the matched block, so the ref-type pattern rejects it.
TEST(PatternMatchingTransform, RefTypeRejectsUseNotDominatedByMatchedBlock) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::String);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    auto bad = MakeLocal("bad", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    fn->Variables.push_back(bad);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* trueBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    root->Add(std::make_unique<StLoc>(V,
        std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality, std::make_unique<LdLoc>(V)),
        std::make_unique<Branch>(falseBlock), nullptr));
    // V is used in the NULL path (falseBlock), which is not dominated by
    // trueBlock -- the dominance guard must reject the pattern.
    falseBlock->Add(std::make_unique<StLoc>(bad, std::make_unique<LdLoc>(V)));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    EXPECT_NE(iff->Condition->Op, OpCode::MatchInstruction);
    EXPECT_EQ(V->Kind, VariableKind::Local);
    fn->CheckInvariant(ILPhase::Normal);
}

// With the PatternMatching setting off, the transform is a no-op.
TEST(PatternMatchingTransform, SettingOffIsNoOp) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<KnownType>(KnownTypeCode::String);
    auto x = MakeLocal("x", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto V = MakeLocal("V", T);
    fn->Variables.push_back(x);
    fn->Variables.push_back(V);
    auto blocks = AddBlocks(*fn, 3);
    Block* root = blocks.b[0];
    Block* trueBlock = blocks.b[1];
    Block* falseBlock = blocks.b[2];

    root->Add(std::make_unique<StLoc>(V,
        std::make_unique<IsInst>(T, std::make_unique<LdLoc>(x))));
    root->SetFinal(std::make_unique<IfInstruction>(
        MakeNullComp(ComparisonKind::Equality, std::make_unique<LdLoc>(V)),
        std::make_unique<Branch>(falseBlock), nullptr));
    trueBlock->Add(std::make_unique<StLoc>(MakeLocal("v3", T), std::make_unique<LdLoc>(V)));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    ctx.Settings.PatternMatching = false;
    PatternMatchingTransform().Run(*fn, ctx);

    auto* iff = static_cast<IfInstruction*>(root->FinalInstruction.get());
    EXPECT_NE(iff->Condition->Op, OpCode::MatchInstruction);
    fn->CheckInvariant(ILPhase::Normal);
}

// On the real mscorlib corpus, running the full pre-pipeline through
// PatternMatchingTransform (the GetILTransforms() position -- after LoopDetection,
// before ConditionDetection) preserves the ILAst invariant across the corpus.
// Whether the legacy-csc .NET Framework 4 corpus contains any C# 7 `is`
// patterns is corpus-dependent (the feature is Roslyn-era), so the sweep
// asserts the invariant holds, not a specific match count.
TEST(PatternMatchingTransform, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int matched = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        PatternMatchingTransform().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::MatchInstruction) ++matched;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    (void)matched;  // corpus-dependent; reported not asserted
}
