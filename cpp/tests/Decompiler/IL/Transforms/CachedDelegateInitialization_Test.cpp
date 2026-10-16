// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, rights to use, copy, modify, merge,
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

// Tests for CachedDelegateInitialization (the next in-order Phase 4 transform
// after the D76 DelegateConstruction.MatchDelegateConstruction foundation).
// This iteration ports the CachedDelegateInitializationWithLocal shape: a
// local-cached delegate lazy init (`if (v == null) v = new Delegate(...)`;
// `<use v>`) collapsed to the unconditional init, adapted to this port's
// if-as-final block model. The field-cached / Roslyn / VB shapes (needing
// IField metadata) are deferred. The .NET Framework 4 legacy csc corpus uses
// the field-cached shape, not the local one, so this subset fires 0 times on
// mscorlib (confirmed by a full-corpus probe); the hand-built tests verify the
// rewrite and the sweep verifies the ILAst invariant holds, matching the
// DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNullable
// precedent.

#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
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
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
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
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// A delegate declaring type: a SimpleType with Kind == Delegate.
std::shared_ptr<IType> MakeDelegateType(std::string ns, std::string name) {
    return std::make_shared<SimpleType>(TopLevelTypeName(std::move(ns), std::move(name)),
                                        TypeKind::Delegate);
}

// Build a `newobj DelegateType(target, ldftn method)` delegate construction.
std::unique_ptr<Call> MakeNewObjDelegate(std::shared_ptr<IType> delegateType,
                                         std::unique_ptr<ILInstruction> target,
                                         std::string ldftnMethod) {
    auto call = std::make_unique<Call>(delegateType
        ? delegateType->ReflectionName() + "..ctor" : std::string("::.ctor"));
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = std::move(delegateType);
    call->AddArg(std::move(target));
    call->AddArg(std::make_unique<LdFtn>(std::move(ldftnMethod)));
    return call;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// The canonical WithLocal shape:
//   P (host): [stloc v(ldnull)], final = if (comp(ldloc v == ldnull)) { Block { stloc v(Delegate) } }
//   Q (next):  [call Use(ldloc v)], final = leave(body)
// `delegateValue` overrides the cache-fill value (default a real delegate
// construction); `otherValue` overrides the null-init value (default ldnull);
// `falseInst` overrides the if's FalseInst (default null); `condKind` is
// Equality by default; `usageInQ` controls whether Q loads v; `extraStore`
// appends an extra stloc v(...) to P (to break the StoreCount == 2 guard);
// `trueInstOverride` replaces the Block TrueInst (to break the Block check).
struct WithLocalSetup {
    std::unique_ptr<ILFunction> fn;
    ILVariablePtr v;
    Block* P = nullptr;
    Block* Q = nullptr;
    StLoc* delegateStore = nullptr;  // the stloc v(Delegate) inside the TrueInst Block
};

WithLocalSetup BuildWithLocal(std::unique_ptr<ILInstruction> delegateValue = nullptr,
                              std::unique_ptr<ILInstruction> otherValue = nullptr,
                              std::unique_ptr<ILInstruction> falseInst = nullptr,
                              ComparisonKind condKind = ComparisonKind::Equality,
                              bool usageInQ = true,
                              std::unique_ptr<ILInstruction> extraStore = nullptr,
                              std::unique_ptr<ILInstruction> trueInstOverride = nullptr) {
    WithLocalSetup s;
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    s.v = MakeLocal("v");
    fn->Variables.push_back(s.v);

    if (!delegateValue)
        delegateValue = MakeNewObjDelegate(MakeDelegateType("System", "Action"),
                                           std::make_unique<LdNull>(), "System.Foo::Bar");
    if (!otherValue) otherValue = std::make_unique<LdNull>();

    // The cache-fill store inside the if's TrueInst Block.
    auto trueBlock = std::make_unique<Block>();
    auto delegateStore = std::make_unique<StLoc>(s.v, std::move(delegateValue));
    s.delegateStore = delegateStore.get();
    trueBlock->Add(std::move(delegateStore));

    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(s.v),
                                       std::make_unique<LdNull>(), condKind, false);
    std::unique_ptr<ILInstruction> trueInst = trueInstOverride
        ? std::move(trueInstOverride) : std::move(trueBlock);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueInst),
                                               std::move(falseInst));

    auto P = std::make_unique<Block>();
    P->Add(std::make_unique<StLoc>(s.v, std::move(otherValue)));  // the null-init
    if (extraStore) P->Add(std::move(extraStore));
    P->SetFinal(std::move(iff));
    s.P = P.get();

    auto Q = std::make_unique<Block>();
    if (usageInQ) {
        auto call = std::make_unique<Call>("System.Foo::Use");
        call->AddArg(std::make_unique<LdLoc>(s.v));
        Q->Add(std::move(call));
    }
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    s.Q = Q.get();

    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    s.fn = std::move(fn);
    return s;
}

int CountIfs(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::IfInstruction) ++n; });
    return n;
}

// Count stloc v(ldnull) remaining in the tree (the null-init store; 0 after a
// successful fold).
int CountNullInitStores(ILFunction& fn, ILVariable* v) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        auto* st = dynamic_cast<StLoc*>(i);
        if (st && st->Variable.get() == v && st->Value && st->Value->Op == OpCode::LdNull) ++n;
    });
    return n;
}

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
    PatternMatchingTransform().Run(fn, ctx);
    ConditionDetection().Run(fn, ctx);
    LockTransform().Run(fn, ctx);
    UsingTransform().Run(fn, ctx);
}

} // namespace

// The canonical WithLocal shape folds: the if and the null-init store are
// gone, the cache-fill store (stloc v(Delegate)) is in the host block, and the
// host block's final is a Branch to the next block (the explicit fall-through
// the if's null FalseInst represented).
TEST(CachedDelegateInitialization, WithLocalFolds) {
    auto setup = BuildWithLocal();
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    // The if is gone.
    EXPECT_EQ(CountIfs(*fn), 0);
    // The null-init store is gone.
    EXPECT_EQ(CountNullInitStores(*fn, setup.v.get()), 0);
    // The host block now holds the cache-fill store and a Branch to the next block.
    ASSERT_EQ(setup.P->Instructions.size(), 1u);
    EXPECT_EQ(setup.P->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(setup.P->Instructions[0].get());
    EXPECT_EQ(st->Variable.get(), setup.v.get());
    ASSERT_EQ(st->Value->Op, OpCode::Call);
    EXPECT_TRUE(static_cast<Call*>(st->Value.get())->IsNewObj);
    ASSERT_EQ(setup.P->FinalInstruction->Op, OpCode::Branch);
    auto* br = static_cast<Branch*>(setup.P->FinalInstruction.get());
    EXPECT_EQ(br->TargetBlock, setup.Q);
    // The next block (the usage) is unchanged.
    ASSERT_EQ(setup.Q->Instructions.size(), 1u);
    EXPECT_EQ(setup.Q->Instructions[0]->Op, OpCode::Call);
}

// A cache-fill value that is not a delegate construction does not fold.
TEST(CachedDelegateInitialization, RejectsNonDelegateValue) {
    auto nonDelegate = std::make_unique<Call>("System.Foo::Bar");
    nonDelegate->ReturnType = StackType::O;
    auto setup = BuildWithLocal(std::move(nonDelegate));
    auto& fn = setup.fn;
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
    EXPECT_EQ(CountNullInitStores(*fn, setup.v.get()), 1);
}

// A variable with an extra store (StoreCount != 2) does not fold -- the cache
// must be exactly the null init plus the delegate fill.
TEST(CachedDelegateInitialization, RejectsExtraStore) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v,
        MakeNewObjDelegate(MakeDelegateType("System", "Action"), std::make_unique<LdNull>(),
                           "System.Foo::Bar")));
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
                                       ComparisonKind::Equality, false);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock));
    auto P = std::make_unique<Block>();
    P->Add(std::make_unique<StLoc>(v, std::make_unique<LdNull>()));  // null init
    P->Add(std::make_unique<StLoc>(v, std::make_unique<LdNull>()));  // extra store -> StoreCount 3
    P->SetFinal(std::move(iff));
    auto Q = std::make_unique<Block>();
    auto call = std::make_unique<Call>("System.Foo::Use");
    call->AddArg(std::make_unique<LdLoc>(v));
    Q->Add(std::move(call));
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A variable with an extra load (LoadCount != 2 -- two usages in the next
// block) does not fold.
TEST(CachedDelegateInitialization, RejectsExtraLoad) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v,
        MakeNewObjDelegate(MakeDelegateType("System", "Action"), std::make_unique<LdNull>(),
                           "System.Foo::Bar")));
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
                                       ComparisonKind::Equality, false);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock));
    auto P = std::make_unique<Block>();
    P->Add(std::make_unique<StLoc>(v, std::make_unique<LdNull>()));
    P->SetFinal(std::move(iff));
    auto Q = std::make_unique<Block>();
    auto call1 = std::make_unique<Call>("System.Foo::Use");
    call1->AddArg(std::make_unique<LdLoc>(v));
    auto call2 = std::make_unique<Call>("System.Foo::Use2");
    call2->AddArg(std::make_unique<LdLoc>(v));  // second usage -> LoadCount 3
    Q->Add(std::move(call1));
    Q->Add(std::move(call2));
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A variable whose single usage is NOT in the next block does not fold.
TEST(CachedDelegateInitialization, RejectsUsageNotInNextBlock) {
    auto setup = BuildWithLocal(/*delegateValue=*/nullptr, /*otherValue=*/nullptr,
                                /*falseInst=*/nullptr, ComparisonKind::Equality,
                                /*usageInQ=*/false);  // no usage in Q
    auto& fn = setup.fn;
    // Put the usage in a separate third block instead (so LoadCount is still 2,
    // but the usage is not in the immediate next block).
    auto R = std::make_unique<Block>();
    auto call = std::make_unique<Call>("System.Foo::Use");
    call->AddArg(std::make_unique<LdLoc>(setup.v));
    R->Add(std::move(call));
    R->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(R));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A null-init store whose value is not ldnull does not fold.
TEST(CachedDelegateInitialization, RejectsOtherStoreNotLdNull) {
    auto setup = BuildWithLocal(/*delegateValue=*/nullptr,
                                /*otherValue=*/std::make_unique<Call>("System.Foo::Bar"),
                                /*falseInst=*/nullptr, ComparisonKind::Equality, true);
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// An if with a non-null FalseInst (an else arm) does not fold (the C#
// requires FalseInst.MatchNop()).
TEST(CachedDelegateInitialization, RejectsFalseInstNotNull) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("v");
    fn->Variables.push_back(v);
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v,
        MakeNewObjDelegate(MakeDelegateType("System", "Action"), std::make_unique<LdNull>(),
                           "System.Foo::Bar")));
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(v), std::make_unique<LdNull>(),
                                       ComparisonKind::Equality, false);
    // An else arm (a Block) -- FalseInst not null.
    auto elseBlock = std::make_unique<Block>();
    elseBlock->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                              std::move(elseBlock));
    auto P = std::make_unique<Block>();
    P->Add(std::make_unique<StLoc>(v, std::make_unique<LdNull>()));
    P->SetFinal(std::move(iff));
    auto Q = std::make_unique<Block>();
    auto call = std::make_unique<Call>("System.Foo::Use");
    call->AddArg(std::make_unique<LdLoc>(v));
    Q->Add(std::move(call));
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A condition that is comp(ldloc v != ldnull) (Inequality, not Equality) does
// not fold (MatchCompEqualsNull requires Equality).
TEST(CachedDelegateInitialization, RejectsConditionNotEquality) {
    auto setup = BuildWithLocal(/*delegateValue=*/nullptr, /*otherValue=*/nullptr,
                                /*falseInst=*/nullptr, ComparisonKind::Inequality, true);
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// A TrueInst that is not a Block (a Branch) does not fold.
TEST(CachedDelegateInitialization, RejectsTrueInstNotBlock) {
    auto setup = BuildWithLocal(/*delegateValue=*/nullptr, /*otherValue=*/nullptr,
                                /*falseInst=*/nullptr, ComparisonKind::Equality, true,
                                /*extraStore=*/nullptr,
                                /*trueInstOverride=*/std::make_unique<Branch>(nullptr));
    auto& fn = setup.fn;
    // A Branch(null) as TrueInst is structurally odd but CheckInvariant-valid
    // (the transform checks TrueInst is a Block before touching it).
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
}

// With the AnonymousMethods setting off, the transform is a no-op.
TEST(CachedDelegateInitialization, AnonymousMethodsOffIsNoOp) {
    auto setup = BuildWithLocal();
    auto& fn = setup.fn;
    ILTransformContext ctx;
    ctx.Settings.AnonymousMethods = false;
    CachedDelegateInitialization().Run(*fn, ctx);
    EXPECT_EQ(CountIfs(*fn), 1);
    EXPECT_EQ(CountNullInitStores(*fn, setup.v.get()), 1);
}

// On the real mscorlib corpus, running the full pre-pipeline through
// UsingTransform + CachedDelegateInitialization (the GetILTransforms() position)
// preserves the ILAst invariant. The .NET Framework 4 legacy csc corpus uses
// the field-cached delegate shape (not the local one this iteration ports), so
// the WithLocal fold fires 0 times; the sweep confirms the transform is a safe
// no-op across the corpus (no crash, invariant holds).
TEST(CachedDelegateInitialization, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        CachedDelegateInitialization().Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
}
