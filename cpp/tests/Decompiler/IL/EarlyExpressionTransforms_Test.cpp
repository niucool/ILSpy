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
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
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

ITypePtr MakeDecimalType() {
    return std::make_shared<KnownType>(KnownTypeCode::Decimal);
}

// Build a vector of owning instructions from individual rvalues (a braced-
// init-list of unique_ptrs would copy them, which is deleted; the variadic
// helper moves each argument into the vector, the D82 learning).
template <typename... Ts>
std::vector<std::unique_ptr<ILInstruction>> MakeArgs(Ts... args) {
    std::vector<std::unique_ptr<ILInstruction>> v;
    v.reserve(sizeof...(Ts));
    (v.push_back(std::move(args)), ...);
    return v;
}

// Build a `newobj Decimal(...)` Call with the given arguments and parameter
// types (the latter carried on Call::ParameterIType, mirroring the IL reader).
// DeclaringType is KnownType(Decimal); IsNewObj is set; ReturnType is O (newobj
// leaves the constructed object on the stack).
std::unique_ptr<Call> MakeNewObjDecimal(
    std::vector<std::unique_ptr<ILInstruction>> args,
    std::vector<ITypePtr> paramTypes) {
    auto call = std::make_unique<Call>("System.Decimal::.ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = MakeDecimalType();
    call->ParameterIType = std::move(paramTypes);
    for (auto& a : args)
        call->AddArg(std::move(a));
    return call;
}

// A one-block function assigning a `newobj Decimal(...)` to a local, so the
// fold replaces the Call with an LdcDecimal and the seed renders the literal.
std::unique_ptr<ILFunction> MakeFnWithNewObjDecimal(
    std::unique_ptr<Call> newobjCall) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto d = std::make_shared<ILVariable>(VariableKind::Local, MakeDecimalType(), -1);
    d->Name = "d";
    fn->Variables.push_back(d);
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(d, std::move(newobjCall)));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    return fn;
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

// TransformDecimalCtorToConstant: a `newobj Decimal(int)` with a constant
// argument folds into the corresponding LdcDecimal constant. The 1-arg case
// dispatches on the first parameter's KnownTypeCode (Int32 here) to interpret
// the constant's bit pattern as a signed int32.
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalInt32Ctor) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(42)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_FALSE(fn->Body->Blocks[0]->Instructions.empty());
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st->Value, nullptr);
    ASSERT_EQ(st->Value->Op, OpCode::LdcDecimal) << "newobj Decimal(42) -> ldc.decimal(42)";
    auto* ldc = static_cast<LdcDecimal*>(st->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "42");
    EXPECT_FALSE(ldc->Value.isNegative);
}

// The 1-arg Int32 case with a negative constant: FromInt32 carries the sign.
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalInt32Negative) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(-5)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    auto* ldc = static_cast<LdcDecimal*>(st->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "-5");
    EXPECT_TRUE(ldc->Value.isNegative);
}

// The UInt32 overload interprets the same bit pattern as an unsigned magnitude:
// `newobj Decimal((uint)0xFFFFFFFF)` => 4294967295m (not -1m). Faithful to the
// C# `new decimal(unchecked((uint)val))`. The .NET Framework 4 legacy-csc corpus
// has no UInt32-overload Decimal ctors, so this is a faithfulness-only test.
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalUInt32Ctor) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(static_cast<std::int32_t>(0xFFFFFFFF))),
        {std::make_shared<KnownType>(KnownTypeCode::UInt32)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* ldc = static_cast<LdcDecimal*>(
        static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get())->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "4294967295");
    EXPECT_FALSE(ldc->Value.isNegative);
}

// The Int64 overload takes an LdcI8 (a 64-bit constant).
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalInt64Ctor) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI8>(9223372036854775807LL)),
        {std::make_shared<KnownType>(KnownTypeCode::Int64)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* ldc = static_cast<LdcDecimal*>(
        static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get())->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "9223372036854775807");
}

// The UInt64 overload interprets the constant as an unsigned 64-bit magnitude.
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalUInt64Ctor) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI8>(static_cast<std::int64_t>(0xFFFFFFFFFFFFFFFFull))),
        {std::make_shared<KnownType>(KnownTypeCode::UInt64)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* ldc = static_cast<LdcDecimal*>(
        static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get())->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "18446744073709551615");
    EXPECT_FALSE(ldc->Value.isNegative);
}

// The 5-arg `newobj Decimal(lo, mid, hi, isNegative, scale)` ctor folds into
// the LdcDecimal carrying those exact bits, with the decimal point inserted
// `scale` digits from the right.
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalFiveArgCtor) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(15), std::make_unique<LdcI4>(0),
         std::make_unique<LdcI4>(0), std::make_unique<LdcI4>(0),
         std::make_unique<LdcI4>(1)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Boolean),
         std::make_shared<KnownType>(KnownTypeCode::Byte)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* ldc = static_cast<LdcDecimal*>(
        static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get())->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "1.5");
}

// A 5-arg ctor with a negative isNegative flag carries the sign.
TEST(EarlyExpressionTransforms, FoldsNewObjDecimalFiveArgNegative) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(25), std::make_unique<LdcI4>(0),
         std::make_unique<LdcI4>(0), std::make_unique<LdcI4>(1),
         std::make_unique<LdcI4>(2)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Boolean),
         std::make_shared<KnownType>(KnownTypeCode::Byte)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* ldc = static_cast<LdcDecimal*>(
        static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get())->Value.get());
    EXPECT_EQ(ldc->Value.ToString(), "-0.25");
}

// A non-Decimal declaring type does not fold (the newobj stays a Call).
TEST(EarlyExpressionTransforms, RejectsNonDecimalNewObj) {
    auto call = std::make_unique<Call>("System.Object::.ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    call->ParameterIType = {std::make_shared<KnownType>(KnownTypeCode::Int32)};
    call->AddArg(std::make_unique<LdcI4>(1));
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(st->Value, nullptr);
    EXPECT_EQ(st->Value->Op, OpCode::Call) << "non-Decimal newobj stays a Call";
}

// A 1-arg Decimal ctor whose argument is not a constant (e.g. a variable
// load) does not fold -- MatchLdcI fails.
TEST(EarlyExpressionTransforms, RejectsNonConstantArg) {
    auto v = MakeLocal("v", KnownTypeCode::Int32);
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdLoc>(v)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::Call) << "non-constant arg stays a Call";
}

// A 2-arg Decimal ctor (an overload this fold does not handle) does not fold.
TEST(EarlyExpressionTransforms, RejectsTwoArgCtor) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::Call) << "2-arg ctor stays a Call";
}

// A 5-arg ctor whose scale > 28 (an invalid decimal scale) does not fold --
// the C# `unchecked((byte)scale) <= 28` guard.
TEST(EarlyExpressionTransforms, RejectsFiveArgWithBadScale) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
         std::make_unique<LdcI4>(0), std::make_unique<LdcI4>(0),
         std::make_unique<LdcI4>(29)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Boolean),
         std::make_shared<KnownType>(KnownTypeCode::Byte)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::Call) << "5-arg ctor with scale 29 stays a Call";
}

// A 5-arg ctor whose 3rd arg is not an LdcI4 does not fold.
TEST(EarlyExpressionTransforms, RejectsFiveArgWithNonConstantArg) {
    auto v = MakeLocal("v", KnownTypeCode::Int32);
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
         std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
         std::make_unique<LdcI4>(1)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Int32),
         std::make_shared<KnownType>(KnownTypeCode::Boolean),
         std::make_shared<KnownType>(KnownTypeCode::Byte)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);
    EarlyExpressionTransforms().Run(*fn, Ctx());
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::Call) << "5-arg ctor with a non-constant arg stays a Call";
}

// The fold's LdcDecimal renders as the C# decimal literal form with the
// trailing `m` suffix in the seed's `--csharp` output.
TEST(EarlyExpressionTransforms, SeedRendersFoldedDecimalLiteral) {
    auto call = MakeNewObjDecimal(MakeArgs(std::make_unique<LdcI4>(1)),
        {std::make_shared<KnownType>(KnownTypeCode::Int32)});
    auto fn = MakeFnWithNewObjDecimal(std::move(call));
    EarlyExpressionTransforms().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "decimal d");
    EXPECT_NE(text.find("d = 1m"), std::string::npos) << text;
}

// On the real mscorlib corpus, running the full pre-pipeline through
// EarlyExpressionTransforms preserves the ILAst invariant and the
// TransformDecimalCtorToConstant fold fires (the .NET Framework 4 legacy-csc
// corpus constructs Decimal constants via `newobj Decimal(int)` and the 5-arg
// ctor, so the fold makes real-corpus progress). The per-method LdcDecimal
// count is monotone non-decreasing (each fold creates one; nothing in this
// transform removes one) and the newobj-Decimal count is monotone
// non-increasing (each fold removes one).
TEST(EarlyExpressionTransforms, MscorlibDecimalCtorSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int processed = 0;
    int totalDecimalCtorFolds = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        ControlFlowSimplification().Run(*fn, ctx);
        StObjToStLoc().Run(*fn, ctx);
        ILInlining().Run(*fn, ctx);
        InlineReturnTransform().Run(*fn, ctx);
        RemoveInfeasiblePathTransform().Run(*fn, ctx);
        DetectPinnedRegions().Run(*fn, ctx);
        DetectCatchWhenConditionBlocks().Run(*fn, ctx);
        LdLocaDupInitObjTransform().Run(*fn, ctx);
        int ldcDecimalBefore = 0, newobjDecimalBefore = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::LdcDecimal) ++ldcDecimalBefore;
            if (i->Op == OpCode::Call) {
                auto* c = static_cast<Call*>(i);
                if (c->IsNewObj && c->DeclaringType) {
                    auto* kt = dynamic_cast<const KnownType*>(c->DeclaringType.get());
                    if (kt && kt->Code() == KnownTypeCode::Decimal) ++newobjDecimalBefore;
                }
            }
        });
        EarlyExpressionTransforms().Run(*fn, ctx);
        int ldcDecimalAfter = 0, newobjDecimalAfter = 0;
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::LdcDecimal) ++ldcDecimalAfter;
            if (i->Op == OpCode::Call) {
                auto* c = static_cast<Call*>(i);
                if (c->IsNewObj && c->DeclaringType) {
                    auto* kt = dynamic_cast<const KnownType*>(c->DeclaringType.get());
                    if (kt && kt->Code() == KnownTypeCode::Decimal) ++newobjDecimalAfter;
                }
            }
        });
        fn->CheckInvariant(ILPhase::Normal);
        EXPECT_GE(ldcDecimalAfter, ldcDecimalBefore);
        EXPECT_LE(newobjDecimalAfter, newobjDecimalBefore);
        totalDecimalCtorFolds += (ldcDecimalAfter - ldcDecimalBefore);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The fold fires on the legacy-csc corpus: mscorlib constructs Decimal
    // constants via `newobj Decimal(int)` (3 sites) and the 5-arg ctor (6
    // sites), so a non-zero total confirms the fold makes real-corpus progress.
    EXPECT_GT(totalDecimalCtorFolds, 0)
        << "TransformDecimalCtorToConstant must fold some Decimal ctors on mscorlib";
}
