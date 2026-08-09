// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for UsingTransform: folds the C# `using` statement's IDisposable
// try/finally pattern into a UsingInstruction (`using (resource) { body }`).
// Three finally shapes are matched (all observed on the .NET Framework 4
// mscorlib corpus, see UsingTransform.hpp): Shape A (reference type, two-block
// null-check), Shape B (struct, one-block ldloca, no null check), and Shape C
// (reference type, two-block isinst-temp). The resource stloc sits in the
// preceding block (the dominant mscorlib case -- CFS does not merge the EH
// wrapper); the same-block case (stloc + TryFinally consecutive) is also
// handled. The VB / async / NullableOfT / ref-struct-own-Dispose shapes are
// deferred.

#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
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
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
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
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeParam(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Parameter, std::move(type), 1);
    v->Name = std::move(name);
    return v;
}

// `call System.IDisposable::Dispose(arg)` -- the 1-argument Dispose. When
// `useLdloca` the argument is the address (a struct disposed by address, the
// one-block Shape B); otherwise a plain load (Shapes A/C).
std::unique_ptr<Call> MakeDisposeCall(ILVariablePtr arg, bool useLdloca) {
    auto call = std::make_unique<Call>("System.IDisposable::Dispose");
    if (useLdloca)
        call->AddArg(std::make_unique<LdLoca>(arg));
    else
        call->AddArg(std::make_unique<LdLoc>(arg));
    return call;
}

// Shape A finally: two blocks.
//   fin[0]: empty Instructions, final = if (comp(eq, ldloc obj, ldnull)) leave(fin)
//     (TrueInst = the skip-leave, FalseInst = nullptr, fall-through)
//   fin[1]: [call Dispose(ldloc obj)], final = leave(fin)
void AddRefTypeFinally(BlockContainer* fin, ILVariablePtr obj) {
    auto b0 = std::make_unique<Block>();
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(obj),
                                       std::make_unique<LdNull>(),
                                       ComparisonKind::Equality, false);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond),
                                                 std::make_unique<Leave>(fin)));
    fin->AddBlock(std::move(b0));
    auto b1 = std::make_unique<Block>();
    b1->Add(MakeDisposeCall(obj, /*useLdloca=*/false));
    b1->SetFinal(std::make_unique<Leave>(fin));
    fin->AddBlock(std::move(b1));
}

// Shape B finally: one block (a struct -- no null check).
//   fin[0]: [call Dispose(ldloca obj)], final = leave(fin)
void AddStructFinally(BlockContainer* fin, ILVariablePtr obj) {
    auto b0 = std::make_unique<Block>();
    b0->Add(MakeDisposeCall(obj, /*useLdloca=*/true));
    b0->SetFinal(std::make_unique<Leave>(fin));
    fin->AddBlock(std::move(b0));
}

// Shape C finally: two blocks with the isinst-temp.
//   fin[0]: [stloc temp(isinst IDisposable(ldloc obj))],
//           final = if (comp(eq, ldloc temp, ldnull)) leave(fin)
//   fin[1]: [call Dispose(ldloc temp)], final = leave(fin)
void AddIsInstTempFinally(BlockContainer* fin, ILVariablePtr obj, ILVariablePtr temp) {
    auto b0 = std::make_unique<Block>();
    auto isinst = std::make_unique<IsInst>(
        std::make_shared<KnownType>(KnownTypeCode::IDisposable),
        std::make_unique<LdLoc>(obj));
    b0->Add(std::make_unique<StLoc>(temp, std::move(isinst)));
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(temp),
                                       std::make_unique<LdNull>(),
                                       ComparisonKind::Equality, false);
    b0->SetFinal(std::make_unique<IfInstruction>(std::move(cond),
                                                 std::make_unique<Leave>(fin)));
    fin->AddBlock(std::move(b0));
    auto b1 = std::make_unique<Block>();
    b1->Add(MakeDisposeCall(temp, /*useLdloca=*/false));
    b1->SetFinal(std::make_unique<Leave>(fin));
    fin->AddBlock(std::move(b1));
}

// A try entry: one body call (optionally using the resource) then a leave of
// the try container. `useObj` adds a `call Use(ldloc obj)` so the resource is
// loaded inside the try body (a realistic using).
Block* AddTryEntry(BlockContainer* tryContainer, ILVariablePtr obj, bool useObj) {
    auto entry = std::make_unique<Block>();
    if (useObj) {
        auto call = std::make_unique<Call>("System.Foo::Use");
        call->AddArg(std::make_unique<LdLoc>(obj));
        entry->Add(std::move(call));
    } else {
        entry->Add(std::make_unique<Call>("System.Foo::Bar"));
    }
    entry->SetFinal(std::make_unique<Leave>(tryContainer));
    auto* p = entry.get();
    tryContainer->AddBlock(std::move(entry));
    return p;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

int CountUsings(ILFunction& fn) {
    int n = 0;
    Walk(fn.Body.get(), [&](ILInstruction* i) {
        if (i->Op == OpCode::UsingInstruction) ++n;
    });
    return n;
}

// The shape of the finally to build.
enum class FinallyShape { RefType, Struct, IsInstTemp };

// Build a using-pattern ILFunction. If `precedingBlock`, the resource stloc
// sits in a separate block before the TryFinally's block (the dominant
// mscorlib case); otherwise the stloc + TryFinally share one block (the C#
// literal same-block shape). If `quirkBranch` (preceding-block only), the
// preceding block's `br` targets the try entry inside the TryFinally (the
// BlockBuilder quirk, D73) instead of the TryFinally wrapper block.
struct UsingSetup {
    std::unique_ptr<ILFunction> fn;
    ILVariablePtr resource;
    ILVariablePtr temp;  // non-null only for the IsInstTemp shape
};
UsingSetup BuildUsing(FinallyShape shape, bool precedingBlock, bool quirkBranch,
                      std::unique_ptr<ILInstruction> resourceExpr,
                      bool useObjInTry = true) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto resource = MakeLocal("obj", std::make_shared<KnownType>(KnownTypeCode::Object));
    ILVariablePtr temp;
    if (shape == FinallyShape::IsInstTemp) {
        temp = MakeLocal("temp", std::make_shared<KnownType>(KnownTypeCode::Object));
        fn->Variables.push_back(temp);
    }
    fn->Variables.push_back(resource);

    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    Block* tryEntry = AddTryEntry(tryC, resource, useObjInTry);
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    if (shape == FinallyShape::RefType)
        AddRefTypeFinally(finC, resource);
    else if (shape == FinallyShape::Struct)
        AddStructFinally(finC, resource);
    else
        AddIsInstTempFinally(finC, resource, temp);

    if (precedingBlock) {
        // P: [stloc obj(resourceExpr)], final = br Q (or br tryEntry for the quirk).
        auto P = std::make_unique<Block>();
        P->Add(std::make_unique<StLoc>(resource, std::move(resourceExpr)));
        auto Q = std::make_unique<Block>();
        Block* QPtr = Q.get();
        Block* brTarget = quirkBranch ? tryEntry : QPtr;
        P->SetFinal(std::make_unique<Branch>(brTarget));
        Q->Add(std::move(tf));
        Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        fn->Body->AddBlock(std::move(P));
        fn->Body->AddBlock(std::move(Q));
    } else {
        // root: [stloc obj(resourceExpr), TryFinally], final = leave(body).
        auto root = std::make_unique<Block>();
        root->Add(std::make_unique<StLoc>(resource, std::move(resourceExpr)));
        root->Add(std::move(tf));
        root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        fn->Body->AddBlock(std::move(root));
    }
    return {std::move(fn), resource, temp};
}

// Run the full pre-pipeline through UsingTransform (the GetILTransforms()
// position -- after ConditionDetection and LockTransform in the BlockILTransform
// post-order set), so the sweep and integration tests exercise the realistic
// pipeline shape.
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
}

} // namespace

// ---- Shape A: reference type, two-block, preceding-block (dominant) ----

TEST(UsingTransform, ShapeA_PrecedingBlockFolds) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(CountUsings(*fn), 1);
    // The TryFinally's block was dropped; the body now has just the preceding
    // block (holding the UsingInstruction with the absorbed continuation final).
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* pb = fn->Body->Blocks[0].get();
    ASSERT_EQ(pb->Instructions.size(), 1u);
    ASSERT_EQ(pb->Instructions[0]->Op, OpCode::UsingInstruction);
    auto* u = static_cast<UsingInstruction*>(pb->Instructions[0].get());
    ASSERT_EQ(u->ResourceExpression->Op, OpCode::LdLoc);
    ASSERT_EQ(u->Body->Op, OpCode::BlockContainer);
    // The resource became a UsingLocal.
    EXPECT_EQ(setup.resource->Kind, VariableKind::UsingLocal);
    // The preceding block absorbed the TryFinally's block final (the body leave).
    ASSERT_EQ(pb->FinalInstruction->Op, OpCode::Leave);
}

// ---- Shape A: the BlockBuilder quirk -- the preceding block's br targets the
// try entry inside the TryFinally (not the wrapper). The corpus uses this. ----
TEST(UsingTransform, ShapeA_QuirkBranchFolds) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/true,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUsings(*fn), 1);
}

// ---- Shape A: same-block (stloc + TryFinally consecutive) ----
TEST(UsingTransform, ShapeA_SameBlockFolds) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/false, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(CountUsings(*fn), 1);
    auto* root = fn->Body->Blocks[0].get();
    ASSERT_EQ(root->Instructions.size(), 1u);
    ASSERT_EQ(root->Instructions[0]->Op, OpCode::UsingInstruction);
}

// ---- Shape B: struct, one-block ldloca, no null check ----
TEST(UsingTransform, ShapeB_StructFolds) {
    auto setup = BuildUsing(FinallyShape::Struct, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountUsings(*fn), 1);
    EXPECT_EQ(setup.resource->Kind, VariableKind::UsingLocal);
}

// ---- Shape C: reference type, two-block isinst-temp ----
TEST(UsingTransform, ShapeC_IsInstTempFolds) {
    auto setup = BuildUsing(FinallyShape::IsInstTemp, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_EQ(CountUsings(*fn), 1);
    auto* u = static_cast<UsingInstruction*>(fn->Body->Blocks[0]->Instructions[0].get());
    // The UsingInstruction wraps the original resource (the isinst-temp is the
    // compiler's null-check implementation and is discarded with the finally).
    EXPECT_EQ(u->Variable.get(), setup.resource.get());
    ASSERT_EQ(u->ResourceExpression->Op, OpCode::LdLoc);
}

// ---- using (null): the resource is ldnull ----
TEST(UsingTransform, UsingNullFolds) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdNull>());
    auto& fn = setup.fn;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountUsings(*fn), 1);
    auto* u = static_cast<UsingInstruction*>(fn->Body->Blocks[0]->Instructions[0].get());
    EXPECT_EQ(u->ResourceExpression->Op, OpCode::LdNull);
}

// ---- using (null) with the Dispose arg as ldnull (the usingNull special case) ----
TEST(UsingTransform, UsingNullWithLdNullDisposeArgFolds) {
    // Build Shape A but rewrite the Dispose arg to ldnull (the compiler-emitted
    // `call Dispose(ldnull)` for using (null)).
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdNull>());
    auto& fn = setup.fn;
    auto* tf = static_cast<TryFinally*>(fn->Body->Blocks[1]->Instructions[0].get());
    auto* fin = static_cast<BlockContainer*>(tf->FinallyBlock.get());
    auto* disp = static_cast<Call*>(fin->Blocks[1]->Instructions[0].get());
    disp->Arguments[0] = std::make_unique<LdNull>();
    disp->Arguments[0]->Parent = disp;
    disp->Arguments[0]->ChildIndex = 0;
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountUsings(*fn), 1);
}

// ---- Seed rendering ----

TEST(UsingTransform, SeedRendersUsingStatement) {
    auto obj = MakeParam("obj", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(obj);

    auto tryContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto entry = std::make_unique<Block>();
    entry->Add(std::make_unique<Call>("System.Foo::Bar"));
    entry->SetFinal(std::make_unique<Leave>(tryC));
    tryContainer->AddBlock(std::move(entry));

    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<UsingInstruction>(obj, std::make_unique<LdLoc>(obj), std::move(tryContainer)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "object obj");
    EXPECT_NE(text.find("using (obj)"), std::string::npos);
    EXPECT_NE(text.find("System.Foo.Bar"), std::string::npos);
}

// ---- Negatives ----

// A Dispose call whose method is not System.IDisposable::Dispose must not fold.
TEST(UsingTransform, RejectsNonIDisposableDisposeName) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    auto* tf = static_cast<TryFinally*>(fn->Body->Blocks[1]->Instructions[0].get());
    auto* fin = static_cast<BlockContainer*>(tf->FinallyBlock.get());
    auto* disp = static_cast<Call*>(fin->Blocks[1]->Instructions[0].get());
    disp->MethodName = "MyNamespace.MyType::Dispose";
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// A Dispose call with the wrong argument count must not fold.
TEST(UsingTransform, RejectsMultiArgDispose) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    auto* tf = static_cast<TryFinally*>(fn->Body->Blocks[1]->Instructions[0].get());
    auto* fin = static_cast<BlockContainer*>(tf->FinallyBlock.get());
    auto* disp = static_cast<Call*>(fin->Blocks[1]->Instructions[0].get());
    disp->AddArg(std::make_unique<LdNull>());  // now 2 args
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// A resource that is not a Local (here a Parameter) must not fold.
TEST(UsingTransform, RejectsResourceNotLocal) {
    // Use a Parameter as the resource by storing a param into... no -- the
    // resource stloc's Variable must be a Local. Build with a Parameter-kind
    // resource variable directly.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto resource = MakeParam("obj", std::make_shared<KnownType>(KnownTypeCode::Object));  // Parameter, not Local
    fn->Variables.push_back(resource);

    auto tryContainer = std::make_unique<BlockContainer>();
    auto finallyContainer = std::make_unique<BlockContainer>();
    auto* tryC = tryContainer.get();
    auto* finC = finallyContainer.get();
    AddTryEntry(tryC, resource, /*useObj=*/false);
    auto tf = std::make_unique<TryFinally>(std::move(tryContainer), std::move(finallyContainer));
    AddRefTypeFinally(finC, resource);

    auto P = std::make_unique<Block>();
    auto Q = std::make_unique<Block>();
    Block* QPtr = Q.get();
    P->Add(std::make_unique<StLoc>(resource, std::make_unique<LdNull>()));
    P->SetFinal(std::make_unique<Branch>(QPtr));
    Q->Add(std::move(tf));
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));

    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// A resource stored twice (not single-definition) must not fold.
TEST(UsingTransform, RejectsMultiStoreResource) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    // Add a second store to the resource in the preceding block (before the
    // existing stloc), so StoreCount becomes 2.
    auto* P = fn->Body->Blocks[0].get();
    auto st = std::make_unique<StLoc>(setup.resource, std::make_unique<LdNull>());
    st->Parent = P;
    P->Instructions.insert(P->Instructions.begin(), std::move(st));
    P->RenumberChildren();
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// A resource loaded outside the TryFinally must not fold (the resource escapes
// the using).
TEST(UsingTransform, RejectsResourceLoadedOutsideTryFinally) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    // Append a block after the TryFinally's block that loads the resource.
    auto after = std::make_unique<Block>();
    auto call = std::make_unique<Call>("System.Foo::Use");
    call->AddArg(std::make_unique<LdLoc>(setup.resource));
    after->Add(std::move(call));
    after->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    // Re-point the TryFinally's block final to branch to the after block, then
    // the after block leaves the body.
    auto* Q = fn->Body->Blocks[1].get();
    auto* afterPtr = after.get();
    Q->FinalInstruction = std::make_unique<Branch>(afterPtr);
    Q->FinalInstruction->Parent = Q;
    Q->FinalInstruction->ChildIndex = static_cast<int>(Q->Instructions.size());
    fn->Body->AddBlock(std::move(after));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// A preceding block whose final is not a branch into the TryFinally must not
// fold (the stloc is not the resource feeding the try).
TEST(UsingTransform, RejectsNonBranchPrecedingBlock) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    // Replace the preceding block's `br Q` with a leave of the function body
    // (so it does not flow into the TryFinally).
    auto* P = fn->Body->Blocks[0].get();
    P->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);
    ILTransformContext ctx;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// ---- Setting gate ----

TEST(UsingTransform, UsingStatementOffIsNoOp) {
    auto setup = BuildUsing(FinallyShape::RefType, /*precedingBlock=*/true, /*quirk=*/false,
                           std::make_unique<LdLoc>(MakeParam("expr", std::make_shared<KnownType>(KnownTypeCode::Object))));
    auto& fn = setup.fn;
    ILTransformContext ctx;
    ctx.Settings.UsingStatement = false;
    UsingTransform().Run(*fn, ctx);
    EXPECT_EQ(CountUsings(*fn), 0);
}

// ---- mscorlib sweep ----

// On the real mscorlib corpus, running the full pre-pipeline through
// UsingTransform (the GetILTransforms() position) preserves the ILAst invariant
// AND fires on the dominant reference-type / struct / isinst-temp shapes, so
// the sweep asserts both the invariant and a non-trivial fold count.
TEST(UsingTransform, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int folded = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        UsingTransform().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::UsingInstruction) ++folded;
        });
        fn->CheckInvariant(ILPhase::Normal);
    }
    EXPECT_GT(processed, 5000);
    // The dominant reference-type two-block shape and the struct one-block
    // shape fire on the .NET Framework 4 corpus (the IEnumerator dispose from
    // foreach, the FileStream/StreamReader usings, etc.).
    EXPECT_GT(folded, 100);
}
