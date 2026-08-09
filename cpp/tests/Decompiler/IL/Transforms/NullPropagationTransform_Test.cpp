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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for NullPropagationTransform (the C# `?.` null-conditional operator
// lowering): the IsProtectedIfInst and MatchNullableRewrap static helpers, the
// Run entry (ReferenceType mode + ldnull output case), the access chain
// analysis (IsValidAccessChain approximated for the Call/LdFld/LdLen/LdElema/
// NullableUnwrap cases), and the IntroduceUnwrap rewrap. The NullableByValue /
// NullableByReference / UnconstrainedType modes, RunStatements, and the
// default(Nullable<T>) / NullCoalescing output cases are deferred. The
// ReferenceType `?.` is a Roslyn-era (C# 6.0) codegen pattern that fires 0
// times on the .NET Framework 4 legacy-csc mscorlib corpus, so the sweep
// asserts the ILAst invariant holds (not a fold count), matching the
// DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNullable precedent.

#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

// A single-block function whose body block has a Leave final; the test adds
// non-terminal statements via the returned block pointer.
std::unique_ptr<ILFunction> MakeFnWithBlock(std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<Block>();
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(block));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

void RunExpressionTransforms(ILFunction& fn) {
    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    st.Run(fn, ctx);
}

} // namespace

// --- IsProtectedIfInst tests ---

TEST(NullPropagationTransform, IsProtectedIfInstRejectsNull) {
    EXPECT_FALSE(NullPropagationTransform::IsProtectedIfInst(nullptr));
}

TEST(NullPropagationTransform, IsProtectedIfInstRejectsPlainIf) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::make_unique<Branch>(nullptr));
    EXPECT_FALSE(NullPropagationTransform::IsProtectedIfInst(iff.get()));
}

TEST(NullPropagationTransform, IsProtectedIfInstRejectsLogicOrNotInConditionSlot) {
    // logic.or: if (a) ldc.i4 1 else b -- but NOT in a condition slot (it's the
    // block's final, parent is the block, not an IfInstruction at ChildIndex 0).
    auto a = MakeLocal("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(a), std::make_unique<LdcI4>(0),
        ComparisonKind::Inequality);
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::make_unique<LdcI4>(1),
        std::make_unique<LdNull>());
    EXPECT_FALSE(NullPropagationTransform::IsProtectedIfInst(iff.get()));
}

TEST(NullPropagationTransform, IsProtectedIfInstAcceptsLogicOrInConditionSlot) {
    // logic.or in a condition slot: an outer if whose Condition is the inner
    // logic.or IfInstruction.
    auto a = MakeLocal("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto innerCond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(a), std::make_unique<LdcI4>(0),
        ComparisonKind::Inequality);
    auto innerIf = std::make_unique<IfInstruction>(
        std::move(innerCond), std::make_unique<LdcI4>(1),
        std::make_unique<LdNull>());
    // Wrap the inner if as the Condition of an outer if (ChildIndex 0).
    auto outerIf = std::make_unique<IfInstruction>(
        std::move(innerIf), std::make_unique<Branch>(nullptr));
    EXPECT_TRUE(NullPropagationTransform::IsProtectedIfInst(
        static_cast<IfInstruction*>(outerIf->Condition.get())));
}

TEST(NullPropagationTransform, IsProtectedIfInstAcceptsLogicAndInConditionSlot) {
    // logic.and in a condition slot: if (a) b else ldc.i4 0, wrapped as the
    // Condition of an outer if.
    auto a = MakeLocal("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto innerCond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(a), std::make_unique<LdcI4>(0),
        ComparisonKind::Inequality);
    auto innerIf = std::make_unique<IfInstruction>(
        std::move(innerCond), std::make_unique<LdNull>(),
        std::make_unique<LdcI4>(0));
    auto outerIf = std::make_unique<IfInstruction>(
        std::move(innerIf), std::make_unique<Branch>(nullptr));
    EXPECT_TRUE(NullPropagationTransform::IsProtectedIfInst(
        static_cast<IfInstruction*>(outerIf->Condition.get())));
}

// --- MatchNullableRewrap tests ---

TEST(NullPropagationTransform, MatchNullableRewrapMatchesRewrap) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto rewrap = std::make_unique<NullableRewrap>(std::move(call));
    ILInstruction* arg = nullptr;
    EXPECT_TRUE(NullPropagationTransform::MatchNullableRewrap(rewrap.get(), arg));
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->Op, OpCode::Call);
}

TEST(NullPropagationTransform, MatchNullableRewrapRejectsNonRewrap) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto ld = std::make_unique<LdLoc>(v);
    ILInstruction* arg = reinterpret_cast<ILInstruction*>(0xDEAD);  // should be reset to null
    EXPECT_FALSE(NullPropagationTransform::MatchNullableRewrap(ld.get(), arg));
    EXPECT_EQ(arg, nullptr);
}

// --- Run tests (ReferenceType mode) ---

TEST(NullPropagationTransform, RunFoldsInequalityNullCheckToRewrap) {
    // `comp(ldloc v != null) ? call ToString(ldloc v) : ldnull`
    // => `nullable.rewrap(call ToString(nullable.unwrap(ldloc v)))`
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Op, OpCode::NullableRewrap);
    auto* rewrap = static_cast<NullableRewrap*>(result.get());
    ASSERT_NE(rewrap->Argument, nullptr);
    EXPECT_EQ(rewrap->Argument->Op, OpCode::Call);
    auto* rcall = static_cast<Call*>(rewrap->Argument.get());
    ASSERT_FALSE(rcall->Arguments.empty());
    EXPECT_EQ(rcall->Arguments[0]->Op, OpCode::NullableUnwrap);
    auto* unwrap = static_cast<NullableUnwrap*>(rcall->Arguments[0].get());
    ASSERT_NE(unwrap->Argument, nullptr);
    EXPECT_EQ(unwrap->Argument->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(unwrap->Argument.get())->Variable.get(), v.get());
}

TEST(NullPropagationTransform, RunFoldsEqualityNullCheckToRewrap) {
    // `comp(ldloc v == null) ? ldnull : call ToString(ldloc v)`
    // => `nullable.rewrap(call ToString(nullable.unwrap(ldloc v)))`
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Equality);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::make_unique<LdNull>(), std::move(call));
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Op, OpCode::NullableRewrap);
    auto* rewrap = static_cast<NullableRewrap*>(result.get());
    ASSERT_NE(rewrap->Argument, nullptr);
    EXPECT_EQ(rewrap->Argument->Op, OpCode::Call);
    auto* rcall = static_cast<Call*>(rewrap->Argument.get());
    ASSERT_FALSE(rcall->Arguments.empty());
    EXPECT_EQ(rcall->Arguments[0]->Op, OpCode::NullableUnwrap);
}

TEST(NullPropagationTransform, RunFoldsFieldAccessChain) {
    // `comp(ldloc v != null) ? ldobj(ldflda(ldloc v, field), type) : ldnull`
    // => `nullable.rewrap(ldobj(ldflda(nullable.unwrap(ldloc v), field), type))`
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto ldFlda = std::make_unique<LdFlda>(
        std::make_unique<LdLoc>(v), "System.Object::someField");
    auto ldObj = std::make_unique<LdObj>(
        std::move(ldFlda), std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(ldObj), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Op, OpCode::NullableRewrap);
    auto* rewrap = static_cast<NullableRewrap*>(result.get());
    ASSERT_NE(rewrap->Argument, nullptr);
    EXPECT_EQ(rewrap->Argument->Op, OpCode::LdObj);
    auto* rLdObj = static_cast<LdObj*>(rewrap->Argument.get());
    ASSERT_NE(rLdObj->Target, nullptr);
    EXPECT_EQ(rLdObj->Target->Op, OpCode::LdFlda);
    auto* rLdFlda = static_cast<LdFlda*>(rLdObj->Target.get());
    ASSERT_NE(rLdFlda->Target, nullptr);
    EXPECT_EQ(rLdFlda->Target->Op, OpCode::NullableUnwrap);
}

TEST(NullPropagationTransform, RunRejectsNonCompCondition) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(call), std::make_unique<LdNull>(), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsNonLdLocLeft) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdNull>(), std::make_unique<LdLoc>(v),
        ComparisonKind::Inequality);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsNonLdNullRight) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(5),
        ComparisonKind::Inequality);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsLiftedComp) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality, ComparisonLiftingKind::CSharp,
        StackType::O);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsRelationalKind) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::LessThan);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsNonAccessChainTrueInst) {
    // The trueInst is a bare `ldloc v` (chainLength 0) -- not a valid access chain.
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::make_unique<LdLoc>(v), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsNonLdNullFalseInst) {
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdcI4>(5));
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

TEST(NullPropagationTransform, RunRejectsStaticCall) {
    // A static call (IsInstanceCall = false) is not a valid `?.` access chain.
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto call = std::make_unique<Call>("System.Object::StaticMethod");
    call->IsInstanceCall = false;
    call->AddArg(std::make_unique<LdLoc>(v));
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto result = NullPropagationTransform::Run(
        iff->Condition.get(), iff->TrueInst.get(), iff->FalseInst.get());
    EXPECT_EQ(result, nullptr);
}

// --- Wired integration through ExpressionTransforms ---

TEST(NullPropagationTransform, WiredFoldsThroughExpressionTransforms) {
    // `stloc result(if (comp(ldloc v != null)) call ToString(ldloc v) else ldnull)`
    // Run ExpressionTransforms (which calls RunIfNullableLift -> LiftNullableCore ->
    // NullPropagationTransform::Run). The if should be replaced by a NullableRewrap.
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeFnWithBlock({v, result});
    auto& block = fn->Body->Blocks[0];
    auto cond = std::make_unique<Comp>(
        std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
        ComparisonKind::Inequality);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->AddArg(std::make_unique<LdLoc>(v));
    call->ReturnType = StackType::O;
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(call), std::make_unique<LdNull>());
    auto stloc = std::make_unique<StLoc>(result, std::move(iff));
    block->Instructions.push_back(std::move(stloc));
    RunExpressionTransforms(*fn);
    ASSERT_FALSE(block->Instructions.empty());
    auto& st = block->Instructions[0];
    ASSERT_EQ(st->Op, OpCode::StLoc);
    auto* stL = static_cast<StLoc*>(st.get());
    ASSERT_NE(stL->Value, nullptr);
    EXPECT_EQ(stL->Value->Op, OpCode::NullableRewrap);
    auto* rewrap = static_cast<NullableRewrap*>(stL->Value.get());
    ASSERT_NE(rewrap->Argument, nullptr);
    EXPECT_EQ(rewrap->Argument->Op, OpCode::Call);
    auto* rcall = static_cast<Call*>(rewrap->Argument.get());
    ASSERT_FALSE(rcall->Arguments.empty());
    EXPECT_EQ(rcall->Arguments[0]->Op, OpCode::NullableUnwrap);
}

// --- mscorlib sweep ---

TEST(NullPropagationTransform, MscorlibSweepPreservesInvariant) {
    // The ReferenceType `?.` is a Roslyn-era (C# 6.0) codegen pattern that fires
    // 0 times on the .NET Framework 4 legacy-csc mscorlib corpus. The sweep
    // verifies the ILAst invariant holds across the corpus (not a fold count),
    // matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj precedent.
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    ILTransformContext ctx;
    ctx.Settings.NullPropagation = true;
    ctx.Settings.LiftNullables = true;

    int methodsProcessed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++methodsProcessed;
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
        SwitchDetection().Run(*fn, ctx);
        SwitchOnNullableTransform().Run(*fn, ctx);
        LoopDetection().Run(*fn, ctx);
        ConditionDetection().Run(*fn, ctx);
        PatternMatchingTransform().Run(*fn, ctx);
        LockTransform().Run(*fn, ctx);
        UsingTransform().Run(*fn, ctx);
        CachedDelegateInitialization().Run(*fn, ctx);
        CachedReadOnlySpanInitialization().Run(*fn, ctx);
        {
            StatementTransform st;
            st.AddChild(std::make_unique<ILInlining>());
            st.AddChild(std::make_unique<ExpressionTransforms>());
            st.Run(*fn, ctx);
        }
        fn->CheckInvariant(ILPhase::Normal);
    }
    EXPECT_GT(methodsProcessed, 1000);
}
