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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for UserDefinedLogicTransform (the C# user-defined short-circuiting `&&`
// / `||` operator fold, the next in-order per-statement child of the
// StatementTransform after the D138 UserDefinedLogicOperator node foundation).
// This iteration ports the LegacyPattern (the legacy-csc shape) and the shared
// MatchCondition / MatchBitwiseCall helpers, adapted to the if-as-final block
// model. The hand-built tests verify the rewrite; the mscorlib sweep pins the
// global contract (the invariant holds -- the transform fires 0 times on the
// .NET Framework 4 legacy-csc mscorlib corpus, which carries no op_True /
// op_False operator definitions, matching the DetectCatchWhenConditionBlocks
// / LdLocaDupInitObj / SwitchOnNullable precedent of a faithfulness-only
// transform on this corpus).

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/UserDefinedLogicTransform.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/NullCoalescingTransform.hpp"
#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::BlockContainer;
using ILSpy::Decompiler::IL::Branch;
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::Comp;
using ILSpy::Decompiler::IL::ComparisonKind;
using ILSpy::Decompiler::IL::ILFunction;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::ILPhase;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::IfInstruction;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::Leave;
using ILSpy::Decompiler::IL::OpCode;
using ILSpy::Decompiler::IL::StatementTransform;
using ILSpy::Decompiler::IL::StLoc;
using ILSpy::Decompiler::IL::UserDefinedLogicTransform;
using ILSpy::Decompiler::IL::VariableKind;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeStackSlot(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, std::move(type), -1);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

// A two-block function: block0 (empty Instructions, a Leave final) + block1 (a
// Leave final). The caller populates block0 and uses block1 as the fall-through
// (the use block).
std::unique_ptr<ILFunction> MakeTwoBlockFn(std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto b0 = std::make_unique<Block>();
    b0->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    auto b1 = std::make_unique<Block>();
    b1->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

// A `call op_False(ldloc v)` (the condition call): a static operator Call with
// IsOperator, !IsInstanceCall, !IsLifted, 1 argument.
std::unique_ptr<Call> MakeOpFalseCall(ILVariablePtr v) {
    auto call = std::make_unique<Call>("Ns.Type::op_False");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->IsOperator = true;
    call->IsInstanceCall = false;
    call->ReturnType = ILSpy::Decompiler::IL::StackType::I4;
    call->AddArg(std::make_unique<LdLoc>(v));
    return call;
}

// A `call op_True(ldloc v)` (the condition call for the || shape).
std::unique_ptr<Call> MakeOpTrueCall(ILVariablePtr v) {
    auto call = std::make_unique<Call>("Ns.Type::op_True");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->IsOperator = true;
    call->IsInstanceCall = false;
    call->ReturnType = ILSpy::Decompiler::IL::StackType::I4;
    call->AddArg(std::make_unique<LdLoc>(v));
    return call;
}

// A `call op_BitwiseAnd(ldloc v, rhs)` (the bitwise call for the && shape): a
// static operator Call with IsOperator, !IsInstanceCall, !IsLifted, 2 args.
std::unique_ptr<Call> MakeOpBitwiseAndCall(ILVariablePtr v,
                                            std::unique_ptr<ILInstruction> rhs) {
    auto call = std::make_unique<Call>("Ns.Type::op_BitwiseAnd");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->IsOperator = true;
    call->IsInstanceCall = false;
    call->ReturnType = ILSpy::Decompiler::IL::StackType::O;
    call->AddArg(std::make_unique<LdLoc>(v));
    call->AddArg(std::move(rhs));
    return call;
}

// A `call op_BitwiseOr(ldloc v, rhs)` (the bitwise call for the || shape).
std::unique_ptr<Call> MakeOpBitwiseOrCall(ILVariablePtr v,
                                           std::unique_ptr<ILInstruction> rhs) {
    auto call = std::make_unique<Call>("Ns.Type::op_BitwiseOr");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->IsOperator = true;
    call->IsInstanceCall = false;
    call->ReturnType = ILSpy::Decompiler::IL::StackType::O;
    call->AddArg(std::make_unique<LdLoc>(v));
    call->AddArg(std::move(rhs));
    return call;
}

void RunUserDefinedLogicTransform(ILFunction& fn) {
    StatementTransform st;
    st.AddChild(std::make_unique<ILSpy::Decompiler::IL::ILInlining>());
    st.AddChild(std::make_unique<ILSpy::Decompiler::IL::ExpressionTransforms>());
    st.AddChild(std::make_unique<UserDefinedLogicTransform>());
    ILTransformContext ctx;
    st.Run(fn, ctx);
}

void RunPrePipeline(ILFunction& fn, ILTransformContext& ctx) {
    ILSpy::Decompiler::IL::ControlFlowSimplification().Run(fn, ctx);
    ILSpy::Decompiler::IL::StObjToStLoc().Run(fn, ctx);
    ILSpy::Decompiler::IL::ILInlining().Run(fn, ctx);
    ILSpy::Decompiler::IL::InlineReturnTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::RemoveInfeasiblePathTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::DetectPinnedRegions().Run(fn, ctx);
    ILSpy::Decompiler::IL::DetectCatchWhenConditionBlocks().Run(fn, ctx);
    ILSpy::Decompiler::IL::LdLocaDupInitObjTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::EarlyExpressionTransforms().Run(fn, ctx);
    ILSpy::Decompiler::IL::RemoveDeadVariableInit().Run(fn, ctx);
    ILSpy::Decompiler::IL::ControlFlowSimplification().Run(fn, ctx);
    ILSpy::Decompiler::IL::SwitchDetection().Run(fn, ctx);
    ILSpy::Decompiler::IL::SwitchOnNullableTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::LoopDetection().Run(fn, ctx);
    ILSpy::Decompiler::IL::PatternMatchingTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::ConditionDetection().Run(fn, ctx);
    ILSpy::Decompiler::IL::LockTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::UsingTransform().Run(fn, ctx);
    ILSpy::Decompiler::IL::CachedDelegateInitialization().Run(fn, ctx);
    ILSpy::Decompiler::IL::CachedReadOnlySpanInitialization().Run(fn, ctx);
    {
        StatementTransform st;
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::ILInlining>());
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::ExpressionTransforms>());
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::TransformAssignment>());
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::NullCoalescingTransform>());
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::NullableLiftingStatementTransform>());
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::NullPropagationStatementTransform>());
        st.AddChild(std::make_unique<UserDefinedLogicTransform>());
        st.Run(fn, ctx);
    }
}

int CountUserDefinedLogic(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::UserDefinedLogicOperator) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Build the post-ConditionDetection legacy `&&` shape on block0 of `fn`:
//   block0.Instructions = [stloc s(lhsInst)]
//   block0.FinalInstruction =
//     if (logic.not(call op_False(ldloc s)))
//       Block { stloc s(call op_BitwiseAnd(ldloc s, rhsInst)); br block1 }
//   block1 (the use block) carries a Leave final + a load of s.
// `s` must already be on fn->Variables. Returns block0.
Block* BuildLegacyAndShape(ILFunction& fn, ILVariablePtr s,
                            std::unique_ptr<ILInstruction> lhsInst,
                            std::unique_ptr<ILInstruction> rhsInst) {
    auto& b0 = fn.Body->Blocks[0];
    auto& b1 = fn.Body->Blocks[1];
    // The value store: stloc s(lhsInst) -- a non-terminal in block0.
    b0->Add(std::make_unique<StLoc>(s, std::move(lhsInst)));
    // The true-arm Block: stloc s(call op_BitwiseAnd(ldloc s, rhsInst)); br block1.
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(s, MakeOpBitwiseAndCall(s, std::move(rhsInst))));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    // The condition: logic.not(call op_False(ldloc s)) =
    // comp(Equality, call op_False(ldloc s), ldc.i4 0).
    auto cond = std::make_unique<Comp>(MakeOpFalseCall(s),
                                        std::make_unique<LdcI4>(0),
                                        ComparisonKind::Equality);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock));
    b0->SetFinal(std::move(iff));
    // The use block (block1) gets a load of s so s is not a dead variable.
    b1->Add(std::make_unique<LdLoc>(s));
    return b0.get();
}

} // namespace

// ---- MatchCondition ----

TEST(UserDefinedLogicTransform, MatchConditionMatchesOpFalse) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpFalseCall(s);
    ILVariablePtr v;
    std::string name;
    ASSERT_TRUE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
    EXPECT_EQ(v.get(), s.get());
    EXPECT_EQ(name, "op_False");
}

TEST(UserDefinedLogicTransform, MatchConditionMatchesOpTrue) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpTrueCall(s);
    ILVariablePtr v;
    std::string name;
    ASSERT_TRUE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
    EXPECT_EQ(v.get(), s.get());
    EXPECT_EQ(name, "op_True");
}

TEST(UserDefinedLogicTransform, MatchConditionRejectsNonOperator) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpFalseCall(s);
    call->IsOperator = false;  // not an operator
    ILVariablePtr v;
    std::string name;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
}

TEST(UserDefinedLogicTransform, MatchConditionRejectsLifted) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpFalseCall(s);
    call->IsLifted = true;  // a lifted operator
    ILVariablePtr v;
    std::string name;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
}

TEST(UserDefinedLogicTransform, MatchConditionRejectsWrongArgCount) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpFalseCall(s);
    call->AddArg(std::make_unique<LdLoc>(s));  // 2 args, not 1
    ILVariablePtr v;
    std::string name;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
}

TEST(UserDefinedLogicTransform, MatchConditionRejectsWrongName) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpFalseCall(s);
    call->MethodName = "Ns.Type::op_BitwiseAnd";  // not op_True/op_False
    ILVariablePtr v;
    std::string name;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
}

TEST(UserDefinedLogicTransform, MatchConditionRejectsNonLdLocArg) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = std::make_unique<Call>("Ns.Type::op_False");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->IsOperator = true;
    call->ReturnType = ILSpy::Decompiler::IL::StackType::I4;
    call->AddArg(std::make_unique<LdcI4>(5));  // arg is a constant, not a load
    ILVariablePtr v;
    std::string name;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchCondition(call.get(), v, name));
}

TEST(UserDefinedLogicTransform, MatchConditionRejectsNonCall) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    ILVariablePtr v;
    std::string name;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchCondition(
        std::make_unique<LdLoc>(s).get(), v, name));
}

// ---- MatchBitwiseCall ----

TEST(UserDefinedLogicTransform, MatchBitwiseCallMatchesOpBitwiseAndWithOpFalse) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs));
    EXPECT_TRUE(UserDefinedLogicTransform::MatchBitwiseCall(call.get(), s.get(), "op_False"));
}

TEST(UserDefinedLogicTransform, MatchBitwiseCallMatchesOpBitwiseOrWithOpTrue) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpBitwiseOrCall(s, std::make_unique<LdLoc>(rhs));
    EXPECT_TRUE(UserDefinedLogicTransform::MatchBitwiseCall(call.get(), s.get(), "op_True"));
}

TEST(UserDefinedLogicTransform, MatchBitwiseCallRejectsOpBitwiseAndWithOpTrue) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs));
    // op_False pairs with op_BitwiseAnd, op_True pairs with op_BitwiseOr -- a
    // mismatch (op_True + op_BitwiseAnd) does not match.
    EXPECT_FALSE(UserDefinedLogicTransform::MatchBitwiseCall(call.get(), s.get(), "op_True"));
}

TEST(UserDefinedLogicTransform, MatchBitwiseCallRejectsNonLdLocFirstArg) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = std::make_unique<Call>("Ns.Type::op_BitwiseAnd");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->IsOperator = true;
    call->ReturnType = ILSpy::Decompiler::IL::StackType::O;
    call->AddArg(std::make_unique<LdcI4>(5));  // first arg is not a load of s
    call->AddArg(std::make_unique<LdLoc>(rhs));
    EXPECT_FALSE(UserDefinedLogicTransform::MatchBitwiseCall(call.get(), s.get(), "op_False"));
}

TEST(UserDefinedLogicTransform, MatchBitwiseCallRejectsWrongArgCount) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpBitwiseAndCall(s, std::make_unique<LdcI4>(5));
    call->AddArg(std::make_unique<LdcI4>(6));  // 3 args, not 2
    EXPECT_FALSE(UserDefinedLogicTransform::MatchBitwiseCall(call.get(), s.get(), "op_False"));
}

TEST(UserDefinedLogicTransform, MatchBitwiseCallRejectsLifted) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs));
    call->IsLifted = true;
    EXPECT_FALSE(UserDefinedLogicTransform::MatchBitwiseCall(call.get(), s.get(), "op_False"));
}

// ---- LegacyPattern ----

TEST(UserDefinedLogicTransform, LegacyPatternFoldsBitwiseAnd) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs, rhs});
    BuildLegacyAndShape(*fn, s, std::make_unique<LdLoc>(lhs),
                        std::make_unique<LdLoc>(rhs));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountUserDefinedLogic(*fn), 0);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountUserDefinedLogic(*fn), 1)
        << "the fold must produce exactly one UserDefinedLogicOperator";
    auto& b0 = fn->Body->Blocks[0];
    // The stloc s(...) is the only non-terminal; its Value is now the user logic op.
    ASSERT_EQ(b0->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(b0->Instructions[0].get());
    ASSERT_EQ(st->Variable.get(), s.get());
    ASSERT_EQ(st->Value->Op, OpCode::UserDefinedLogicOperator)
        << "the stloc's Value must be the UserDefinedLogicOperator";
    auto* ulo = static_cast<ILSpy::Decompiler::IL::UserDefinedLogicOperator*>(st->Value.get());
    // The method name is the bitwise call's resolved name.
    EXPECT_EQ(ulo->MethodName, "Ns.Type::op_BitwiseAnd");
    // Left is the lhs (ldloc lhs); Right is the rhs (ldloc rhs).
    ASSERT_EQ(ulo->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(ulo->Left.get())->Variable.get(), lhs.get());
    ASSERT_EQ(ulo->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(ulo->Right.get())->Variable.get(), rhs.get());
    // The if-final was replaced with a Branch to the next block (the fall-through).
    ASSERT_EQ(b0->FinalInstruction->Op, OpCode::Branch)
        << "the if-final must be replaced with a Branch to the next block";
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsNonStackSlotVariable) {
    // A Local s (not a StackSlot) -- the legacy pattern requires s.Kind == StackSlot.
    auto s = MakeLocal("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs, rhs});
    BuildLegacyAndShape(*fn, s, std::make_unique<LdLoc>(lhs),
                        std::make_unique<LdLoc>(rhs));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountUserDefinedLogic(*fn), 0)
        << "a non-StackSlot s must not fold";
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsNonIfFinal) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs});
    // block0: stloc s(ldloc lhs); Leave final (not an if).
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    fn->Body->Blocks[1]->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0);
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsNonLogicNotCondition) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs, rhs});
    // Build the shape but with a bare op_False condition (not logic.not-wrapped).
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(s, MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs))));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    // Bare op_False condition (not logic.not).
    auto iff = std::make_unique<IfInstruction>(MakeOpFalseCall(s), std::move(trueBlock));
    b0->SetFinal(std::move(iff));
    b1->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0)
        << "a non-logic.not condition must not fold";
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsConditionOnDifferentVariable) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto other = MakeStackSlot("other", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, other, lhs, rhs});
    // The condition's op_False loads `other`, not s -- MatchCondition reports
    // other, and the s2 == s check fails.
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(s, MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs))));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    auto cond = std::make_unique<Comp>(MakeOpFalseCall(other),
                                        std::make_unique<LdcI4>(0),
                                        ComparisonKind::Equality);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock)));
    b1->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0);
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsIfWithElseArm) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs, rhs});
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(s, MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs))));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    auto cond = std::make_unique<Comp>(MakeOpFalseCall(s),
                                        std::make_unique<LdcI4>(0),
                                        ComparisonKind::Equality);
    // An else arm (a Branch to b1) -- the legacy pattern has no else.
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                                 std::make_unique<Branch>(b1.get()));
    b0->SetFinal(std::move(iff));
    b1->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0)
        << "an if with an else arm must not fold (the legacy pattern has no else)";
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsRhsUsingS) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs});
    // The rhs is `ldloc s` -- it references s, so short-circuiting would change
    // semantics (the user logic operator only evaluates the rhs when the lhs is
    // "false", but the rhs reads s which the stloc overwrote). IsUsedWithin rejects.
    BuildLegacyAndShape(*fn, s, std::make_unique<LdLoc>(lhs),
                        std::make_unique<LdLoc>(s));  // rhs = ldloc s
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0)
        << "an rhs that references s must not fold (short-circuiting would change semantics)";
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsTrueArmNotStLocToS) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto other = MakeStackSlot("other", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, other, lhs, rhs});
    // The true arm stores to `other`, not s -- the UnwrapLegacyTrueInst stloc's
    // variable must be s.
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(other,
        MakeOpBitwiseAndCall(s, std::make_unique<LdLoc>(rhs))));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    auto cond = std::make_unique<Comp>(MakeOpFalseCall(s),
                                        std::make_unique<LdcI4>(0),
                                        ComparisonKind::Equality);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock)));
    b1->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0)
        << "a true arm storing to a different variable must not fold";
}

TEST(UserDefinedLogicTransform, LegacyPatternRejectsNonBitwiseCallTrueArm) {
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs});
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    auto trueBlock = std::make_unique<Block>();
    // The true arm stores a plain LdLoc (not a bitwise call).
    trueBlock->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    auto cond = std::make_unique<Comp>(MakeOpFalseCall(s),
                                        std::make_unique<LdcI4>(0),
                                        ComparisonKind::Equality);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock)));
    b1->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 0)
        << "a true arm that is not a bitwise call must not fold";
}

TEST(UserDefinedLogicTransform, LegacyPatternFoldsBitwiseOr) {
    // The || shape: condition = logic.not(call op_True(ldloc s)), true arm =
    // stloc s(call op_BitwiseOr(ldloc s, rhsInst)).
    auto s = MakeStackSlot("s", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto lhs = MakeLocal("lhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto rhs = MakeLocal("rhs", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeTwoBlockFn({s, lhs, rhs});
    auto& b0 = fn->Body->Blocks[0];
    auto& b1 = fn->Body->Blocks[1];
    b0->Add(std::make_unique<StLoc>(s, std::make_unique<LdLoc>(lhs)));
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(s, MakeOpBitwiseOrCall(s, std::make_unique<LdLoc>(rhs))));
    trueBlock->SetFinal(std::make_unique<Branch>(b1.get()));
    auto cond = std::make_unique<Comp>(MakeOpTrueCall(s),
                                        std::make_unique<LdcI4>(0),
                                        ComparisonKind::Equality);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock)));
    b1->Add(std::make_unique<LdLoc>(s));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountUserDefinedLogic(*fn), 0);

    RunUserDefinedLogicTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUserDefinedLogic(*fn), 1);
    auto& b0b = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(b0b->Instructions[0].get());
    auto* ulo = static_cast<ILSpy::Decompiler::IL::UserDefinedLogicOperator*>(st->Value.get());
    EXPECT_EQ(ulo->MethodName, "Ns.Type::op_BitwiseOr");
    ASSERT_EQ(b0b->FinalInstruction->Op, OpCode::Branch);
}

// ---- mscorlib sweep (UserDefinedLogicTransform in the full per-statement pipeline) ----

// The .NET Framework 4 legacy-csc mscorlib corpus carries no op_True / op_False
// operator definitions (a user-defined short-circuiting `&&` / `||` is a rare
// C# feature; mscorlib has no operator-overloading types that define them), so
// the LegacyPattern fires 0 times on it -- a faithfulness-only transform on
// this corpus, matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj /
// SwitchOnNullable precedent. The sweep verifies the ILAst invariant holds
// across the corpus with UserDefinedLogicTransform in the full per-statement
// pipeline (the transform does not crash or corrupt the tree) and the total
// UserDefinedLogicOperator count is 0 (the transform does not misfire).
TEST(UserDefinedLogicTransform, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int totalFolds = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ILSpy::Decompiler::IL::ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        totalFolds += CountUserDefinedLogic(*fn);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The legacy-csc mscorlib corpus carries no op_True / op_False operator
    // definitions, so the LegacyPattern fires 0 times on it (faithfulness-only,
    // matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj /
    // SwitchOnNullable precedent); the per-method CheckInvariant above verifies
    // the fold does not crash or corrupt the tree on the corpus.
    EXPECT_EQ(totalFolds, 0);
}
