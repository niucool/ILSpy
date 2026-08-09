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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for SwitchOnNullableTransform: the legacy csc and Roslyn switch-on-
// Nullable<T> shapes fold into a single lifted SwitchInstruction with an
// explicit `case null:` arm (SwitchSection.HasNullLabel), the seed renders
// `case null:`, and the ILAst invariant holds across the corpus. Adapted to
// this port's if-as-final block model (the fall-through is the next block in
// the container; the switchBlock's switch is its FinalInstruction).

#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
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
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::Util::LongSet;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

// Nullable<T> as a generic instantiation: ParameterizedType(KnownType(NullableOfT), {T}).
ITypePtr MakeNullableOf(KnownTypeCode underlying) {
    std::vector<ITypePtr> args;
    args.push_back(std::make_shared<KnownType>(underlying));
    return std::make_shared<ParameterizedType>(
        std::make_shared<KnownType>(KnownTypeCode::NullableOfT), std::move(args));
}

// A `call methodName(arg)` on the given declaring type.
std::unique_ptr<Call> MakeCall(ITypePtr declaringType, std::string methodFullName,
                               std::unique_ptr<ILInstruction> arg) {
    auto call = std::make_unique<Call>(std::move(methodFullName));
    call->DeclaringType = std::move(declaringType);
    call->AddArg(std::move(arg));
    return call;
}

// logic.not(X) = comp(Equality, X, LdcI4(0)), the port's representation of !X.
std::unique_ptr<Comp> MakeLogicNot(std::unique_ptr<ILInstruction> x) {
    return std::make_unique<Comp>(std::move(x), std::make_unique<LdcI4>(0),
                                  ComparisonKind::Equality, false);
}

Block* SectionTarget(const SwitchSection& s) {
    if (!s.Body || s.Body->Op != OpCode::Branch) return nullptr;
    return static_cast<Branch*>(s.Body.get())->TargetBlock;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// A switch(V) with `cases` (label -> target block) plus a default -> defBlock,
// returned as the SwitchInstruction (caller sets it as a block's final).
std::unique_ptr<SwitchInstruction> MakeSwitchOn(ILVariablePtr V,
        const std::vector<std::pair<long long, Block*>>& cases, Block* defBlock) {
    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(V));
    for (auto& c : cases) {
        auto s = std::make_unique<SwitchSection>(LongSet(c.first));
        s->SetBody(std::make_unique<Branch>(c.second));
        sw->AddSection(std::move(s));
    }
    auto def = std::make_unique<SwitchSection>();
    def->SetBody(std::make_unique<Branch>(defBlock));
    sw->AddSection(std::move(def));
    return sw;
}

// The shared case/null/def blocks for the switch-on-nullable fixtures. Each
// ends in a leave of the function body. `body` must already have the blocks
// added (the caller wires their finals).
struct NullableSwitchBlocks {
    Block* root;
    Block* switchBlock;
    Block* nullCase;
    Block* caseA;
    Block* def;
};

NullableSwitchBlocks AddNullableBlocks(ILFunction& fn) {
    NullableSwitchBlocks r;
    for (int i = 0; i < 5; ++i) fn.Body->AddBlock(std::make_unique<Block>());
    r.root = fn.Body->Blocks[0].get();
    r.switchBlock = fn.Body->Blocks[1].get();
    r.nullCase = fn.Body->Blocks[2].get();
    r.caseA = fn.Body->Blocks[3].get();
    r.def = fn.Body->Blocks[4].get();
    r.nullCase->SetFinal(std::make_unique<Leave>(fn.Body.get()));
    r.caseA->SetFinal(std::make_unique<Leave>(fn.Body.get()));
    r.def->SetFinal(std::make_unique<Leave>(fn.Body.get()));
    return r;
}

} // namespace

// Legacy csc shape: stloc tmp(ldloca V); stloc sw(call GetValueOrDefault(ldloc
// tmp)); if (!get_HasValue(ldloc tmp)) br nullCase; [fall-through] switchBlock
// { switch (ldloc sw) { case 0: br caseA; default: br def } }. Folds into a
// lifted switch(ldloc V) with the switchBlock's sections plus `case null:`.
TEST(SwitchOnNullableTransform, FoldsLegacyShapeIntoLiftedSwitch) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    auto tmp = MakeLocal("tmp");
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(tmp);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    // root: stloc tmp(ldloca V); stloc sw(call GetValueOrDefault(ldloc tmp));
    //       if (!get_HasValue(ldloc tmp)) br nullCase
    b.root->Add(std::make_unique<StLoc>(tmp, std::make_unique<LdLoca>(V)));
    b.root->Add(std::make_unique<StLoc>(sw,
        MakeCall(nullable, "System.Nullable`1::GetValueOrDefault", std::make_unique<LdLoc>(tmp))));
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue",
                              std::make_unique<LdLoc>(tmp))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    // switchBlock: switch (ldloc sw) { case 0: br caseA; default: br def }
    b.switchBlock->SetFinal(MakeSwitchOn(sw, {{0, b.caseA}}, b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ASSERT_EQ(b.switchBlock->IncomingEdgeCount, 1);
    ASSERT_EQ(tmp->LoadCount, 2);
    ASSERT_EQ(sw->LoadCount, 1);

    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);

    // The root now ends in a lifted switch; the two stlocs are gone.
    ASSERT_TRUE(b.root->FinalInstruction);
    ASSERT_EQ(b.root->FinalInstruction->Op, OpCode::SwitchInstruction);
    EXPECT_TRUE(b.root->Instructions.empty());
    auto* newSw = static_cast<SwitchInstruction*>(b.root->FinalInstruction.get());
    EXPECT_TRUE(newSw->IsLifted);
    ASSERT_TRUE(newSw->Type);
    // The switch value is ldloc V (the source nullable).
    ASSERT_EQ(newSw->Value->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newSw->Value.get())->Variable.get(), V.get());
    // 3 sections from switchBlock's switch (case 0 + default) + the null case.
    ASSERT_EQ(newSw->Sections.size(), 3u);
    bool foundCase0 = false, foundDef = false, foundNull = false;
    for (const auto& s : newSw->Sections) {
        if (s->HasNullLabel) {
            foundNull = true;
            EXPECT_TRUE(s->Labels.IsEmpty());
            EXPECT_EQ(SectionTarget(*s), b.nullCase);
        } else if (s->Labels.Contains(0) && s->Labels.Count() == 1u) {
            foundCase0 = true;
            EXPECT_EQ(SectionTarget(*s), b.caseA);
        } else if (s->Labels.IsEmpty() && !s->HasNullLabel) {
            foundDef = true;
            EXPECT_EQ(SectionTarget(*s), b.def);
        }
    }
    EXPECT_TRUE(foundCase0);
    EXPECT_TRUE(foundDef);
    EXPECT_TRUE(foundNull);
    // The switchBlock is now unreachable (the root no longer falls through);
    // it stays in the tree (per D58) but with no incoming edges.
    EXPECT_EQ(b.switchBlock->IncomingEdgeCount, 0);
    fn->CheckInvariant(ILPhase::Normal);
}

// The legacy shape does not match when get_HasValue is not on a Nullable<T>
// (the declaring type is Int32): the block is left untouched.
TEST(SwitchOnNullableTransform, LegacyRejectsNonNullableDeclaringType) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nonNullable = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto V = MakeLocal("V");
    auto tmp = MakeLocal("tmp");
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(tmp);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    b.root->Add(std::make_unique<StLoc>(tmp, std::make_unique<LdLoca>(V)));
    b.root->Add(std::make_unique<StLoc>(sw,
        MakeCall(nonNullable, "System.Int32::GetValueOrDefault", std::make_unique<LdLoc>(tmp))));
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nonNullable, "System.Int32::get_HasValue",
                              std::make_unique<LdLoc>(tmp))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    b.switchBlock->SetFinal(MakeSwitchOn(sw, {{0, b.caseA}}, b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);

    // No fold: the root still ends in the if.
    ASSERT_TRUE(b.root->FinalInstruction);
    EXPECT_EQ(b.root->FinalInstruction->Op, OpCode::IfInstruction);
    EXPECT_EQ(b.root->Instructions.size(), 2u);
    fn->CheckInvariant(ILPhase::Normal);
}

// The legacy shape does not match when the fall-through block is not a single-
// predecessor switch block: left untouched.
TEST(SwitchOnNullableTransform, LegacyRejectsNonSwitchFallThrough) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    auto tmp = MakeLocal("tmp");
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(tmp);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    b.root->Add(std::make_unique<StLoc>(tmp, std::make_unique<LdLoca>(V)));
    b.root->Add(std::make_unique<StLoc>(sw,
        MakeCall(nullable, "System.Nullable`1::GetValueOrDefault", std::make_unique<LdLoc>(tmp))));
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue",
                              std::make_unique<LdLoc>(tmp))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    // switchBlock ends in a Branch (not a switch): the fall-through is not a
    // switch block, so the legacy matcher bails.
    b.switchBlock->SetFinal(std::make_unique<Branch>(b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);
    ASSERT_TRUE(b.root->FinalInstruction);
    EXPECT_EQ(b.root->FinalInstruction->Op, OpCode::IfInstruction);
    fn->CheckInvariant(ILPhase::Normal);
}

// Roslyn shape (with the stloc sw + switch): if (!get_HasValue(ldloca V)) br
// nullCase; [fall-through] switchBlock { stloc sw(call GetValueOrDefault(ldloca
// V)); switch (ldloc sw) { case 0: br caseA; default: br def } }. Folds into a
// lifted switch(ldloc V) with the sections plus `case null:`.
TEST(SwitchOnNullableTransform, FoldsRoslynShapeWithStore) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    // root: if (!get_HasValue(ldloca V)) br nullCase
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue",
                              std::make_unique<LdLoca>(V))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    // switchBlock: stloc sw(call GetValueOrDefault(ldloca V)); switch (ldloc sw)
    b.switchBlock->Add(std::make_unique<StLoc>(sw,
        MakeCall(nullable, "System.Nullable`1::GetValueOrDefault", std::make_unique<LdLoca>(V))));
    b.switchBlock->SetFinal(MakeSwitchOn(sw, {{0, b.caseA}}, b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ASSERT_EQ(b.switchBlock->IncomingEdgeCount, 1);

    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);

    ASSERT_TRUE(b.root->FinalInstruction);
    ASSERT_EQ(b.root->FinalInstruction->Op, OpCode::SwitchInstruction);
    auto* newSw = static_cast<SwitchInstruction*>(b.root->FinalInstruction.get());
    EXPECT_TRUE(newSw->IsLifted);
    ASSERT_EQ(newSw->Value->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newSw->Value.get())->Variable.get(), V.get());
    ASSERT_EQ(newSw->Sections.size(), 3u);
    bool foundNull = false;
    for (const auto& s : newSw->Sections) {
        if (s->HasNullLabel) { foundNull = true; EXPECT_EQ(SectionTarget(*s), b.nullCase); }
    }
    EXPECT_TRUE(foundNull);
    fn->CheckInvariant(ILPhase::Normal);
}

// Roslyn inlined shape: if (!get_HasValue(ldloca V)) br nullCase; [fall-through]
// switchBlock { switch (call GetValueOrDefault(ldloca V)) { case 0: br caseA;
// default: br def } } -- the GetValueOrDefault is inlined into the switch value.
TEST(SwitchOnNullableTransform, FoldsRoslynInlinedShape) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    fn->Variables.push_back(V);
    auto b = AddNullableBlocks(*fn);

    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue",
                              std::make_unique<LdLoca>(V))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    // switchBlock: switch (call GetValueOrDefault(ldloca V)) (inlined)
    auto gvo = MakeCall(nullable, "System.Nullable`1::GetValueOrDefault",
                        std::make_unique<LdLoca>(V));
    auto sw = std::make_unique<SwitchInstruction>(std::move(gvo));
    auto s = std::make_unique<SwitchSection>(LongSet(0));
    s->SetBody(std::make_unique<Branch>(b.caseA));
    sw->AddSection(std::move(s));
    auto def = std::make_unique<SwitchSection>();
    def->SetBody(std::make_unique<Branch>(b.def));
    sw->AddSection(std::move(def));
    b.switchBlock->SetFinal(std::move(sw));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);

    ASSERT_TRUE(b.root->FinalInstruction);
    ASSERT_EQ(b.root->FinalInstruction->Op, OpCode::SwitchInstruction);
    auto* newSw = static_cast<SwitchInstruction*>(b.root->FinalInstruction.get());
    EXPECT_TRUE(newSw->IsLifted);
    // The switch value is ldloc V (target was ldloca V).
    ASSERT_EQ(newSw->Value->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newSw->Value.get())->Variable.get(), V.get());
    bool foundNull = false;
    for (const auto& s : newSw->Sections) {
        if (s->HasNullLabel) foundNull = true;
    }
    EXPECT_TRUE(foundNull);
    fn->CheckInvariant(ILPhase::Normal);
}

// The Roslyn shape does not match when the HasValue target is impure (a call
// with side effects): folding it into the switch value would drop the side
// effect, so the block is left untouched.
TEST(SwitchOnNullableTransform, RoslynRejectsImpureTarget) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    // target = a call (impure): get_HasValue(call Foo()) -- the call is not pure.
    // Build two independent copies so each call owns its argument.
    auto makeImpure = [&] {
        return MakeCall(nullable, "System.Nullable`1::Foo",
                        std::make_unique<LdLoca>(V));
    };
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue", makeImpure())),
        std::make_unique<Branch>(b.nullCase), nullptr));
    b.switchBlock->Add(std::make_unique<StLoc>(sw,
        MakeCall(nullable, "System.Nullable`1::GetValueOrDefault", makeImpure())));
    b.switchBlock->SetFinal(MakeSwitchOn(sw, {{0, b.caseA}}, b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);
    ASSERT_TRUE(b.root->FinalInstruction);
    EXPECT_EQ(b.root->FinalInstruction->Op, OpCode::IfInstruction);
    fn->CheckInvariant(ILPhase::Normal);
}

// The Roslyn shape does not match when the GetValueOrDefault argument differs
// from the HasValue argument (target2 != target): the nullable being switched
// is not the one tested for null, so the fold is unsafe.
TEST(SwitchOnNullableTransform, RoslynRejectsMismatchedTarget) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    auto W = MakeLocal("W", nullable);  // a different nullable
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(W);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    // get_HasValue(ldloca V) but GetValueOrDefault(ldloca W): targets differ.
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue",
                              std::make_unique<LdLoca>(V))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    b.switchBlock->Add(std::make_unique<StLoc>(sw,
        MakeCall(nullable, "System.Nullable`1::GetValueOrDefault", std::make_unique<LdLoca>(W))));
    b.switchBlock->SetFinal(MakeSwitchOn(sw, {{0, b.caseA}}, b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    SwitchOnNullableTransform().Run(*fn, ctx);
    ASSERT_TRUE(b.root->FinalInstruction);
    EXPECT_EQ(b.root->FinalInstruction->Op, OpCode::IfInstruction);
    fn->CheckInvariant(ILPhase::Normal);
}

// When LiftNullables is off, Run is a no-op: the legacy shape stays as ifs.
TEST(SwitchOnNullableTransform, NoOpWhenLiftNullablesOff) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    auto V = MakeLocal("V", nullable);
    auto tmp = MakeLocal("tmp");
    auto sw = MakeLocal("sw");
    fn->Variables.push_back(V);
    fn->Variables.push_back(tmp);
    fn->Variables.push_back(sw);
    auto b = AddNullableBlocks(*fn);

    b.root->Add(std::make_unique<StLoc>(tmp, std::make_unique<LdLoca>(V)));
    b.root->Add(std::make_unique<StLoc>(sw,
        MakeCall(nullable, "System.Nullable`1::GetValueOrDefault", std::make_unique<LdLoc>(tmp))));
    b.root->SetFinal(std::make_unique<IfInstruction>(
        MakeLogicNot(MakeCall(nullable, "System.Nullable`1::get_HasValue",
                              std::make_unique<LdLoc>(tmp))),
        std::make_unique<Branch>(b.nullCase), nullptr));
    b.switchBlock->SetFinal(MakeSwitchOn(sw, {{0, b.caseA}}, b.def));

    RecomputeIncomingEdgeCounts(*fn);
    ComputeVariableUsage(*fn);
    ILTransformContext ctx;
    ctx.Settings.LiftNullables = false;
    SwitchOnNullableTransform().Run(*fn, ctx);
    ASSERT_TRUE(b.root->FinalInstruction);
    EXPECT_EQ(b.root->FinalInstruction->Op, OpCode::IfInstruction);
    fn->CheckInvariant(ILPhase::Normal);
}

// On the real mscorlib corpus, running the full pre-pipeline through
// SwitchOnNullableTransform (at the GetILTransforms() position: after
// SwitchDetection, before LoopDetection) must not crash and must preserve the
// ILAst invariant across thousands of methods. Whether the legacy csc corpus
// has any switch-on-nullable is corpus-dependent; the sweep asserts the
// invariant, not a specific fold count.
TEST(SwitchOnNullableTransform, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int liftedSwitches = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Pre-pipeline through SwitchDetection, then SwitchOnNullable (the
        // GetILTransforms() position -- before LoopDetection).
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
        Walk(fn->Body.get(), [&](ILInstruction* i) {
            if (i->Op == OpCode::SwitchInstruction &&
                static_cast<SwitchInstruction*>(i)->IsLifted)
                ++liftedSwitches;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    (void)liftedSwitches;  // corpus-dependent; reported not asserted
}
