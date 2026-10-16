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
// PURPOSE, NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for FixRemainingIncrements (the user-defined op_Increment/op_Decrement
// calls that TransformAssignment's inc/dec folds did NOT fold -- the cases where
// the variable-being-incremented was optimized out, e.g.
//   stloc V(call op_Increment(expr))  ->  stloc V(expr); compound.assign op_Increment(V)
// The hand-built tests verify the rewrite (the primary branch -- the call is a
// StLoc's Value and the StLoc is a non-terminal in a Block) and the negatives;
// the mscorlib sweep pins the global contract -- the transform fires 0 times on
// the .NET Framework 4 legacy-csc mscorlib corpus (no non-Decimal op_Increment/
// op_Decrement + the Roslyn optimized-out-variable codegen is absent), so the
// sweep asserts the ILAst invariant holds across the corpus (faithfulness-only,
// matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNull-
// able precedent).

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/FixRemainingIncrements.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
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
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::CompoundEvalMode;
using ILSpy::Decompiler::IL::CompoundTargetKind;
using ILSpy::Decompiler::IL::FixRemainingIncrements;
using ILSpy::Decompiler::IL::ILFunction;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::ILPhase;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::LdLoca;
using ILSpy::Decompiler::IL::Leave;
using ILSpy::Decompiler::IL::OpCode;
using ILSpy::Decompiler::IL::StackType;
using ILSpy::Decompiler::IL::StLoc;
using ILSpy::Decompiler::IL::UserDefinedCompoundAssign;
using ILSpy::Decompiler::IL::VariableKind;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ITypePtr KT(KnownTypeCode code) { return std::make_shared<KnownType>(code); }

// A one-block function (block0 with a Leave final).
std::unique_ptr<ILFunction> MakeFn() {
    auto container = std::make_unique<BlockContainer>();
    container->AddBlock(std::make_unique<Block>());
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

// A static operator Call named "Ns.Type::op_Increment" (or op_Decrement) on a
// non-Decimal user type, 1 arg.
std::unique_ptr<Call> MakeOpIncCall(std::string methodName, ITypePtr declaringType,
                                     StackType returnType, std::unique_ptr<ILInstruction> arg) {
    auto call = std::make_unique<Call>(std::move(methodName));
    call->IsOperator = true;
    call->IsInstanceCall = false;  // static
    call->DeclaringType = std::move(declaringType);
    call->ReturnType = returnType;
    call->AddArg(std::move(arg));
    return call;
}

void RunFix(ILFunction& fn) {
    ILTransformContext ctx;
    FixRemainingIncrements().Run(fn, ctx);
}

// Count UserDefinedCompoundAssign nodes in the function.
int CountCompoundAssigns(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::UserDefinedCompoundAssign) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

} // namespace

// ---- Positive: the primary branch folds ----

TEST(FixRemainingIncrements, FoldsStLocValueSlotOpIncrement) {
    // stloc V(call op_Increment(ldloc other))  -- the arg is ldloc other, NOT
    // ldloc V (the variable-being-incremented was optimized out). Transform-
    // Assignment's MatchLdLoc gate rejects this (the arg is not a load of V);
    // FixRemainingIncrements turns it into stloc V(ldloc other); ++V.
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("Ns.Type::op_Increment", KT(KnownTypeCode::Object),
                             StackType::O, std::make_unique<LdLoc>(other));
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountCompoundAssigns(*fn), 0);
    ASSERT_EQ(block->Instructions.size(), 1u);

    RunFix(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The stloc V(call op_Increment(..)) became stloc V(ldloc other) ++ the
    // compound assign was inserted after it.
    ASSERT_EQ(block->Instructions.size(), 2u);
    auto* st = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->Variable.get(), v.get());
    // The call was replaced with its argument (ldloc other).
    ASSERT_EQ(st->Value->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(st->Value.get())->Variable.get(), other.get());
    // The inserted instruction is the compound assign.
    auto* uca = dynamic_cast<UserDefinedCompoundAssign*>(block->Instructions[1].get());
    ASSERT_NE(uca, nullptr);
    EXPECT_EQ(uca->EvalMode, CompoundEvalMode::EvaluatesToNewValue);
    EXPECT_EQ(uca->TargetKind, CompoundTargetKind::Address);
    EXPECT_EQ(uca->MethodName, "Ns.Type::op_Increment");
    auto* lda = dynamic_cast<LdLoca*>(uca->Target.get());
    ASSERT_NE(lda, nullptr);
    EXPECT_EQ(lda->Variable.get(), v.get());
    auto* one = dynamic_cast<LdcI4*>(uca->Value.get());
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(one->Value, 1);
    EXPECT_EQ(CountCompoundAssigns(*fn), 1);
}

TEST(FixRemainingIncrements, FoldsOpDecrement) {
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("Ns.Type::op_Decrement", KT(KnownTypeCode::Object),
                             StackType::O, std::make_unique<LdLoc>(other));
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    RunFix(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 1);
    auto* uca = dynamic_cast<UserDefinedCompoundAssign*>(block->Instructions[1].get());
    ASSERT_NE(uca, nullptr);
    EXPECT_EQ(uca->MethodName, "Ns.Type::op_Decrement");
}

TEST(FixRemainingIncrements, FoldsCheckedIncrementWhenSettingOn) {
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("Ns.Type::op_CheckedIncrement", KT(KnownTypeCode::Object),
                             StackType::O, std::make_unique<LdLoc>(other));
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    RunFix(*fn);  // CheckedOperators defaults true
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 1);
    auto* uca = dynamic_cast<UserDefinedCompoundAssign*>(block->Instructions[1].get());
    ASSERT_NE(uca, nullptr);
    EXPECT_EQ(uca->MethodName, "Ns.Type::op_CheckedIncrement");
}

// ---- Negatives: each gate ----

TEST(FixRemainingIncrements, RejectsDecimalOpIncrement) {
    // System.Decimal op_Increment -- skipped (handled in ReplaceMethodCallsWith-
    // Operators, a resolver path this port does not model).
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Decimal), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Decimal), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("System.Decimal::op_Increment", KT(KnownTypeCode::Decimal),
                             StackType::O, std::make_unique<LdLoc>(other));
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    RunFix(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 0)
        << "Decimal op_Increment must be skipped";
    // The call stays as a call (the StLoc's Value is still the Call).
    ASSERT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(block->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(static_cast<StLoc*>(block->Instructions[0].get())->Value->Op, OpCode::Call);
}

TEST(FixRemainingIncrements, RejectsNonOperatorCall) {
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("Ns.Type::op_Increment", KT(KnownTypeCode::Object),
                             StackType::O, std::make_unique<LdLoc>(other));
    call->IsOperator = false;  // not an operator
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    RunFix(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 0);
}

TEST(FixRemainingIncrements, RejectsWrongArgCount) {
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("Ns.Type::op_Increment", KT(KnownTypeCode::Object),
                             StackType::O, std::make_unique<LdLoc>(other));
    call->AddArg(std::make_unique<LdLoc>(v));  // 2 args, not 1
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    RunFix(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 0);
}

TEST(FixRemainingIncrements, RejectsCheckedIncrementWhenSettingOff) {
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    auto call = MakeOpIncCall("Ns.Type::op_CheckedIncrement", KT(KnownTypeCode::Object),
                             StackType::O, std::make_unique<LdLoc>(other));
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    ctx.Settings.CheckedOperators = false;
    FixRemainingIncrements().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 0)
        << "op_CheckedIncrement must not be recognised when CheckedOperators is off";
}

TEST(FixRemainingIncrements, RejectsNonStLocValueSlot) {
    // The call is NOT a StLoc's Value -- it's an argument to another call. The
    // else branch (Extract) is deferred, so the call stays as a call.
    auto fn = MakeFn();
    auto* block = fn->Body->Blocks[0].get();
    auto v = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 0);
    v->Name = "V";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, KT(KnownTypeCode::Object), 1);
    other->Name = "other";
    fn->Variables.push_back(v);
    fn->Variables.push_back(other);
    // call Outer(call op_Increment(ldloc other)) -- the op_Increment call is an
    // argument to Outer, not a StLoc's Value.
    auto inner = MakeOpIncCall("Ns.Type::op_Increment", KT(KnownTypeCode::Object),
                               StackType::O, std::make_unique<LdLoc>(other));
    auto outer = std::make_unique<Call>("Ns.Type::Outer");
    outer->IsInstanceCall = false;
    outer->ReturnType = StackType::Void;
    outer->AddArg(std::move(inner));
    block->Add(std::move(outer));
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    RunFix(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountCompoundAssigns(*fn), 0)
        << "the non-StLoc-ValueSlot case (else branch / Extract) is deferred";
}

// ---- mscorlib sweep: the transform fires 0 times on the .NET Framework 4 ----
// legacy-csc corpus (no non-Decimal op_Increment/op_Decrement + the Roslyn
// optimized-out-variable codegen is absent); the sweep asserts the ILAst
// invariant holds across the corpus.

TEST(FixRemainingIncrements, MscorlibSweepPreservesInvariant) {
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
        FixRemainingIncrements().Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        totalFolds += CountCompoundAssigns(*fn);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The .NET Framework 4 legacy-csc mscorlib corpus has no non-Decimal
    // op_Increment/op_Decrement operator definitions and the Roslyn
    // optimized-out-variable codegen is absent, so the fold fires 0 times;
    // the per-method CheckInvariant above verifies the transform does not crash
    // or corrupt the tree on the corpus.
    EXPECT_EQ(totalFolds, 0);
}
