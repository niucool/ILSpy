// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
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

// EarlyExpressionTransforms tests. The transform folds three early expression-
// level rewrites the rest of the pipeline depends on:
//   - StObjToStLoc: stobj(ldloca V, value) -> stloc V, value
//   - LdObjToLdLoc: ldobj(ldloca V, type) -> ldloc V
//   - FixComparisonKindLdNull: normalize comparison kinds against ldnull and
//     unwrap box T(arg) ==/!= ldnull to arg ==/!= ldnull (T a type parameter).

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameter;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name, KnownTypeCode code) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local,
        std::make_shared<KnownType>(code), -1);
    v->Name = std::move(name);
    return v;
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

// --- StObjToStLoc (the EarlyExpressionTransforms copy) ---

TEST(EarlyExpressionTransforms, ConvertsStoreToLocalAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0", KnownTypeCode::Int32);
    fn->Variables.push_back(V);
    fn->Body->AddBlock(std::make_unique<Block>());
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    fn->Body->Blocks[0]->Add(std::make_unique<StObj>(
        std::make_unique<LdLoca>(V), std::make_unique<LdcI4>(1), intType));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_FALSE(fn->Body->Blocks[0]->Instructions.empty());
    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr) << "stobj(ldloca V, ..) becomes stloc V, ..";
    EXPECT_EQ(st->Variable.get(), V.get());
    ASSERT_NE(st->Value, nullptr);
    EXPECT_EQ(st->Value->Op, OpCode::LdcI4);
}

TEST(EarlyExpressionTransforms, LeavesStoreToFieldAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StObj>(
        std::make_unique<LdFlda>(std::make_unique<LdLoc>(nullptr), "NS.T::field"),
        std::make_unique<LdcI4>(1), nullptr));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_FALSE(fn->Body->Blocks[0]->Instructions.empty());
    EXPECT_EQ(fn->Body->Blocks[0]->Instructions[0]->Op, OpCode::StObj)
        << "a store to a field address is not a local store";
}

// --- LdObjToLdLoc ---

// A typed load through a local's address (`ldobj(ldloca V, T)`, rendered
// `*(T*)&V`) becomes a direct local load (`ldloc V`) -- the shape ILInlining
// and the rest of the pipeline expect.
TEST(EarlyExpressionTransforms, ConvertsLoadFromLocalAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0", KnownTypeCode::Int32);
    fn->Variables.push_back(V);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    fn->Body->AddBlock(std::make_unique<Block>());
    // The LdObj is the StLoc's value: `stloc S(ldobj(Int32, ldloca V_0))`.
    auto S = MakeLocal("S_1", KnownTypeCode::Int32);
    fn->Variables.push_back(S);
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S,
        std::make_unique<LdObj>(std::make_unique<LdLoca>(V), intType)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    ASSERT_FALSE(fn->Body->Blocks[0]->Instructions.empty());
    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    ASSERT_NE(st->Value, nullptr);
    EXPECT_EQ(st->Value->Op, OpCode::LdLoc) << "ldobj(ldloca V) becomes ldloc V";
    auto* ldloc = static_cast<LdLoc*>(st->Value.get());
    EXPECT_EQ(ldloc->Variable.get(), V.get());
}

// A load through a field address (not a local's address) is left as ldobj.
TEST(EarlyExpressionTransforms, LeavesLoadFromFieldAddress) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto S = MakeLocal("S_1", KnownTypeCode::Int32);
    fn->Variables.push_back(S);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S,
        std::make_unique<LdObj>(
            std::make_unique<LdFlda>(std::make_unique<LdLoc>(nullptr), "NS.T::field"),
            intType)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    ASSERT_NE(st->Value, nullptr);
    EXPECT_EQ(st->Value->Op, OpCode::LdObj)
        << "a load from a field address is not a local load";
}

// A load whose memory type is not compatible with the local's type (Int32 local
// vs a String-typed ldobj) is left as ldobj.
TEST(EarlyExpressionTransforms, LeavesLoadWithIncompatibleType) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto V = MakeLocal("V_0", KnownTypeCode::Int32);
    fn->Variables.push_back(V);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto S = MakeLocal("S_1", KnownTypeCode::String);
    fn->Variables.push_back(S);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(S,
        std::make_unique<LdObj>(std::make_unique<LdLoca>(V), stringType)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    ASSERT_NE(st->Value, nullptr);
    EXPECT_EQ(st->Value->Op, OpCode::LdObj)
        << "a load of an incompatible type is not a local load";
}

// --- FixComparisonKindLdNull ---

// comp(x > ldnull) => comp(x != ldnull): a reference is never "greater than" null.
TEST(EarlyExpressionTransforms, FixesGreaterThanAgainstLdNullRight) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto O = MakeLocal("o", KnownTypeCode::String);
    fn->Variables.push_back(O);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(O,
        std::make_unique<Comp>(std::make_unique<LdLoc>(O),
            std::make_unique<LdNull>(), ComparisonKind::GreaterThan)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Inequality);
}

// comp(x <= ldnull) => comp(x == ldnull): a reference is null at most.
TEST(EarlyExpressionTransforms, FixesLessThanOrEqualAgainstLdNullRight) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto O = MakeLocal("o", KnownTypeCode::String);
    fn->Variables.push_back(O);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(O,
        std::make_unique<Comp>(std::make_unique<LdLoc>(O),
            std::make_unique<LdNull>(), ComparisonKind::LessThanOrEqual)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Equality);
}

// comp(ldnull < x) => comp(ldnull != x): the left-side analog.
TEST(EarlyExpressionTransforms, FixesLessThanAgainstLdNullLeft) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto O = MakeLocal("o", KnownTypeCode::String);
    fn->Variables.push_back(O);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(O,
        std::make_unique<Comp>(std::make_unique<LdNull>(),
            std::make_unique<LdLoc>(O), ComparisonKind::LessThan)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Inequality);
}

// comp(ldnull >= x) => comp(ldnull == x): the left-side analog.
TEST(EarlyExpressionTransforms, FixesGreaterThanOrEqualAgainstLdNullLeft) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto O = MakeLocal("o", KnownTypeCode::String);
    fn->Variables.push_back(O);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(O,
        std::make_unique<Comp>(std::make_unique<LdNull>(),
            std::make_unique<LdLoc>(O), ComparisonKind::GreaterThanOrEqual)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Equality);
}

// A comparison against ldnull that is already eq/ne is left as-is.
TEST(EarlyExpressionTransforms, LeavesEqualityAgainstLdNull) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto O = MakeLocal("o", KnownTypeCode::String);
    fn->Variables.push_back(O);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(O,
        std::make_unique<Comp>(std::make_unique<LdLoc>(O),
            std::make_unique<LdNull>(), ComparisonKind::Equality)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Equality)
        << "eq against ldnull is already canonical";
}

// box T(arg) == ldnull  ->  arg == ldnull (T a type parameter): a boxed
// generic-default null check is really a check on the underlying value. The Box
// is unwrapped and its Argument becomes the comparison's left.
TEST(EarlyExpressionTransforms, UnwrapsBoxTypeParameterEqualityAgainstLdNull) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<TypeParameter>(0, TypeParameter::OwnerKind::Class, "T");
    auto arg = MakeLocal("a", KnownTypeCode::String);
    fn->Variables.push_back(arg);
    auto result = MakeLocal("r", KnownTypeCode::Boolean);
    fn->Variables.push_back(result);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result,
        std::make_unique<Comp>(
            std::make_unique<Box>(T, std::make_unique<LdLoc>(arg)),
            std::make_unique<LdNull>(), ComparisonKind::Equality)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Equality);
    ASSERT_NE(comp->Left, nullptr);
    EXPECT_EQ(comp->Left->Op, OpCode::LdLoc) << "the Box is unwrapped to its Argument";
    auto* ldloc = static_cast<LdLoc*>(comp->Left.get());
    EXPECT_EQ(ldloc->Variable.get(), arg.get());
    int boxCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::Box) ++boxCount; });
    EXPECT_EQ(boxCount, 0) << "the Box node is gone";
}

// box T(arg) != ldnull  ->  arg != ldnull (the Inequality variant).
TEST(EarlyExpressionTransforms, UnwrapsBoxTypeParameterInequalityAgainstLdNull) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto T = std::make_shared<TypeParameter>(0, TypeParameter::OwnerKind::Class, "T");
    auto arg = MakeLocal("a", KnownTypeCode::String);
    fn->Variables.push_back(arg);
    auto result = MakeLocal("r", KnownTypeCode::Boolean);
    fn->Variables.push_back(result);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result,
        std::make_unique<Comp>(
            std::make_unique<Box>(T, std::make_unique<LdLoc>(arg)),
            std::make_unique<LdNull>(), ComparisonKind::Inequality)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->Kind, ComparisonKind::Inequality);
    ASSERT_NE(comp->Left, nullptr);
    EXPECT_EQ(comp->Left->Op, OpCode::LdLoc);
    int boxCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::Box) ++boxCount; });
    EXPECT_EQ(boxCount, 0);
}

// box Int32(arg) == ldnull (T a value type, not a type parameter) is NOT
// unwrapped -- the Box's type is a concrete value type, not a type parameter.
TEST(EarlyExpressionTransforms, LeavesBoxNonTypeParameterAgainstLdNull) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto arg = MakeLocal("a", KnownTypeCode::Int32);
    fn->Variables.push_back(arg);
    auto result = MakeLocal("r", KnownTypeCode::Boolean);
    fn->Variables.push_back(result);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result,
        std::make_unique<Comp>(
            std::make_unique<Box>(intType, std::make_unique<LdLoc>(arg)),
            std::make_unique<LdNull>(), ComparisonKind::Equality)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = dynamic_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    auto* comp = dynamic_cast<Comp*>(st->Value.get());
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(comp->Left, nullptr);
    EXPECT_EQ(comp->Left->Op, OpCode::Box)
        << "a Box of a non-type-parameter is not unwrapped";
    int boxCount = 0;
    Walk(fn->Body.get(), [&](ILInstruction* i) { if (i->Op == OpCode::Box) ++boxCount; });
    EXPECT_EQ(boxCount, 1);
}

// On the real mscorlib corpus the transform must not violate the invariant, and
// the load/store conversions should fire (the sweep confirms both that the
// invariant holds across the corpus and that some ldobj/stobj become ldloc/stloc).
TEST(EarlyExpressionTransforms, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int ldobjBefore = 0, ldobjAfter = 0;
    int stobjBefore = 0, stobjAfter = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // The CLI pre-pipeline up to this transform (DetectExitPoints, which
        // would sit between DetectCatchWhenConditionBlocks and LdLocaDupInitObj
        // in the C# order, is deferred).
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::LdObj) ++ldobjBefore;
            if (i->Op == OpCode::StObj) ++stobjBefore;
        });
        EarlyExpressionTransforms().Run(*fn, ctx);
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::LdObj) ++ldobjAfter;
            if (i->Op == OpCode::StObj) ++stobjAfter;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_LE(ldobjAfter, ldobjBefore) << "some ldobj(ldloca) should become ldloc";
    EXPECT_LE(stobjAfter, stobjBefore) << "some stobj(ldloca) should become stloc";
}
