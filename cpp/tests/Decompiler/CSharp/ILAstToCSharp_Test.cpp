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

// Tests for the ILAst-to-C#-text seed (Decompiler/CSharp/ILAstToCSharp).
// Hand-built ILAst trees are translated and the emitted text is compared
// exactly (the seed's output format is fixed), plus an end-to-end run against
// mscorlib: ReadIL decodes a real getter and the translator must emit the
// field-return statement.

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeVar(VariableKind kind, std::string name, std::int32_t index,
                      ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = kind;
    v->Index = index;
    v->Type = std::move(type);
    return v;
}

// Build a one-method ILFunction from a list of prepared blocks and wire up the
// function-level links the way the IL reader leaves them.
std::unique_ptr<ILFunction> MakeFunction(std::vector<std::unique_ptr<Block>> blocks) {
    auto container = std::make_unique<BlockContainer>();
    for (auto& b : blocks) container->AddBlock(std::move(b));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

std::unique_ptr<Leave> ReturnFinal(BlockContainer* container,
                                   std::unique_ptr<ILInstruction> value = nullptr) {
    return std::make_unique<Leave>(container, std::move(value));
}

} // namespace

TEST(ILAstToCSharp, StraightLineStoreAndEmptyReturn) {
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                       std::make_unique<LdcI4>(42)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(ILAstToCSharp(*fn, "void", "M", ""),
              "void M()\n{\n    var V_0 = 42;\n    return;\n}\n");
}

TEST(ILAstToCSharp, ParametersAreAssignedNotDeclared) {
    auto param = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(param, std::make_unique<LdcI4>(5)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "int arg_1");
    EXPECT_NE(text.find("\n    arg_1 = 5;\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("var arg_1"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SecondStoreIsAssignmentNotRedeclaration) {
    auto local = MakeVar(VariableKind::Local, "V_0", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(1)));
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(2)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    var V_0 = 1;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    V_0 = 2;\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, ArithmeticAndComparisonExpressions) {
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto v0 = MakeVar(VariableKind::Local, "V_0", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v0, std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(arg1), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add)));

    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get(),
        std::make_unique<Comp>(std::make_unique<LdLoc>(v0), std::make_unique<LdcI4>(0),
                               ComparisonKind::GreaterThan)));
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(ILAstToCSharp(*fn, "bool", "Check", "int arg_1"),
              "bool Check(int arg_1)\n{\n    var V_0 = (arg_1 + 1);\n    return (V_0 > 0);\n}\n");
}

TEST(ILAstToCSharp, ConditionalBranchEmitsIfGotoAndLabel) {
    // b0: if (1 == 1) goto b2;  b1: stloc (body);  b2: return.
    // b2 is NOT the next block (b1 is), so the goto is non-redundant.
    auto b0 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                    std::make_unique<LdcI4>(0)));
    auto b1 = std::make_unique<Block>();
    b1->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_1", 1),
                                    std::make_unique<LdcI4>(1)));
    auto b2 = std::make_unique<Block>();

    auto fn = MakeFunction({});
    Block* b2Ptr = b2.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));

    auto br = std::make_unique<Branch>(static_cast<std::uint32_t>(0x20));
    br->TargetBlock = b2Ptr;
    br->HasOffset = false;
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(1),
                               ComparisonKind::Equality),
        std::move(br)));
    fn->Body->Blocks[1]->SetFinal(std::make_unique<Branch>(b2Ptr));  // b1 -> b2
    fn->Body->Blocks[2]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    if (1 == 1) goto IL_0020;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("\nIL_0020:\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, VoidCallStatementAndStringEscapes) {
    auto call = std::make_unique<Call>("System.Console::WriteLine");
    call->AddArg(std::make_unique<LdStr>("a\nb\"\\"));
    call->ReturnType = StackType::Void;

    auto block = std::make_unique<Block>();
    block->Add(std::move(call));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find(R"(    System.Console.WriteLine("a\nb\"\\");)"),
              std::string::npos) << text;
}

TEST(ILAstToCSharp, InstanceCallRendersAsReceiverDotMethod) {
    // An instance call `call Type::Method(receiver, arg)` renders as
    // `receiver.Method(arg)`, not `Type.Method(receiver, arg)`.
    auto recv = MakeVar(VariableKind::Parameter, "this", 0);
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto call = std::make_unique<Call>("System.Object::ToString");
    call->IsInstanceCall = true;
    call->ReturnType = StackType::O;
    call->AddArg(std::make_unique<LdLoc>(recv));
    call->AddArg(std::make_unique<LdLoc>(arg1));

    auto block = std::make_unique<Block>();
    block->Add(std::move(call));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "int arg_1");
    EXPECT_NE(text.find("    this.ToString(arg_1);\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("System.Object.ToString("), std::string::npos) << "not a static-style call";
}

TEST(ILAstToCSharp, ConstructorCallEmitsNewExpression) {
    auto call = std::make_unique<Call>("System.Text.StringBuilder::.ctor");
    call->ReturnType = StackType::Void;
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    call->AddArg(std::make_unique<LdLoc>(arg1));

    auto block = std::make_unique<Block>();
    block->Add(std::move(call));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "int arg_1");
    EXPECT_NE(text.find("    new System.Text.StringBuilder(arg_1);\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, BaseConstructorCallEmitsBase) {
    // A `.ctor` call statement whose first arg is `this` is a base ctor call:
    // render as `base(args)`, dropping the implicit `this`.
    auto thisVar = MakeVar(VariableKind::Parameter, "this", 0);
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto call = std::make_unique<Call>("System.Object::.ctor");
    call->ReturnType = StackType::Void;
    call->AddArg(std::make_unique<LdLoc>(thisVar));
    call->AddArg(std::make_unique<LdLoc>(arg1));

    auto block = std::make_unique<Block>();
    block->Add(std::move(call));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", ".ctor", "int arg_1");
    EXPECT_NE(text.find("    base(arg_1);\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("new System.Object"), std::string::npos) << "base ctor call is not new";
}

TEST(ILAstToCSharp, FieldStoreAndLoadThroughLdFlda) {
    auto thisVar = MakeVar(VariableKind::Parameter, "this", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StObj>(
        std::make_unique<LdFlda>(std::make_unique<LdLoc>(thisVar), "MyApp.C::count"),
        std::make_unique<LdcI4>(7), nullptr));

    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get(),
        std::make_unique<LdObj>(
            std::make_unique<LdFlda>(std::make_unique<LdLoc>(thisVar), "MyApp.C::count"),
            std::make_unique<KnownType>(KnownTypeCode::Int32))));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "int", "get_Count", "");
    EXPECT_NE(text.find("    count = 7;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    return count;\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, ByRefVariableDerefIsImplicit) {
    // ldobj/stobj on a byref variable (a managed ref) renders as the variable
    // name -- the indirection is implicit in C#. `*(this)` in a value-type
    // method and `*(byrefParam)` both collapse to the bare name.
    auto thisVar = MakeVar(VariableKind::Parameter, "this", 0);
    auto refParam = MakeVar(VariableKind::Parameter, "array", 1,
        std::make_shared<ByReferenceType>(
            std::make_shared<KnownType>(KnownTypeCode::Int32)));
    auto block = std::make_unique<Block>();
    // array = *(array)  (stobj on the byref)
    block->Add(std::make_unique<StObj>(std::make_unique<LdLoc>(refParam),
        std::make_unique<LdcI4>(0), nullptr));
    // return *(this)  (ldobj on this)
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get(),
        std::make_unique<LdObj>(std::make_unique<LdLoc>(thisVar),
            std::make_shared<KnownType>(KnownTypeCode::Int32))));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "int", "M", "ref int array");
    EXPECT_NE(text.find("    array = 0;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    return this;\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("*("), std::string::npos) << "no explicit deref of a byref";
}

TEST(ILAstToCSharp, ThrowEmitsThrowStatement) {
    auto block = std::make_unique<Block>();
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Throw>(std::make_unique<LdNull>()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    throw null;\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchInlinesLeaveFinalBodies) {
    // A switch whose bodies self-terminate (Leave/return) with no shared exit
    // convergence inlines them: each case renders its body's return directly,
    // no thunk gotos, no IL_ labels.
    auto v0 = MakeVar(VariableKind::Local, "V_0", 0);
    auto b0 = std::make_unique<Block>();
    auto b1 = std::make_unique<Block>();
    auto b2 = std::make_unique<Block>();

    auto fn = MakeFunction({});
    Block* b1Ptr = b1.get();
    Block* b2Ptr = b2.get();
    auto b3 = std::make_unique<Block>();  // post-switch continuation
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(b3));

    auto brCase = std::make_unique<Branch>(static_cast<std::uint32_t>(0x30));
    brCase->TargetBlock = b1Ptr;
    brCase->HasOffset = false;
    auto brDefault = std::make_unique<Branch>(static_cast<std::uint32_t>(0x40));
    brDefault->TargetBlock = b2Ptr;
    brDefault->HasOffset = false;

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v0));
    auto caseSection = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    caseSection->SetBody(std::move(brCase));
    sw->AddSection(std::move(caseSection));
    auto defaultSection = std::make_unique<SwitchSection>();
    defaultSection->SetBody(std::move(brDefault));
    sw->AddSection(std::move(defaultSection));
    fn->Body->Blocks[0]->SetFinal(std::move(sw));

    fn->Body->Blocks[1]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->Body->Blocks[2]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->Body->Blocks[3]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    switch (V_0)\n"), std::string::npos) << text;
    EXPECT_NE(text.find("        case 0:\n"), std::string::npos) << text;
    EXPECT_NE(text.find("        default:\n"), std::string::npos) << text;
    // The bodies inline: no thunk gotos, no IL_ labels.
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    // Each case's return renders inline.
    EXPECT_NE(text.find("return;"), std::string::npos) << text;
}

TEST(ILAstToCSharp, CastsAndTypeOperators) {
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);

    // isinst: (arg_1 as System.String)
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
        std::make_unique<IsInst>(std::make_unique<KnownType>(KnownTypeCode::String),
                                 std::make_unique<LdLoc>(arg1))));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    // castclass: return (System.String)(V_0)
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get(),
        std::make_unique<CastClass>(std::make_unique<KnownType>(KnownTypeCode::String),
                                    std::make_unique<LdLoc>(
                                        MakeVar(VariableKind::Local, "V_0", 0)))));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "System.String", "M", "object arg_1");
    EXPECT_NE(text.find("    var V_0 = arg_1 as string;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    return (string)(V_0);\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, ArrayAndLengthExpressions) {
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto block = std::make_unique<Block>();
    // V_0 = arg_1.Length
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
        std::make_unique<LdLen>(StackType::I4, std::make_unique<LdLoc>(arg1))));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    // return arg_1[V_0] (ldobj over ldelema, the shape ldelem.* produces)
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<LdLoc>(MakeVar(VariableKind::Local, "V_0", 0)));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get(),
        std::make_unique<LdObj>(
            std::make_unique<LdElema>(std::make_unique<KnownType>(KnownTypeCode::Int32),
                                      std::make_unique<LdLoc>(arg1), std::move(indices)),
            std::make_unique<KnownType>(KnownTypeCode::Int32))));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "int", "M", "int[] arg_1");
    EXPECT_NE(text.find("    var V_0 = arg_1.Length;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    return arg_1[V_0];\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, ConvI4OverLdLenIsImplicit) {
    // conv.i4(ldlen(arr)) is the IL for `arr.Length`; the cast to signed i4 is
    // implicit in C#, so the seed renders just `arr.Length` (no `(int)(...)`).
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
        std::make_unique<Conv>(
            std::make_unique<LdLen>(StackType::I, std::make_unique<LdLoc>(arg1)), PrimitiveType::I4, false, Sign::None)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "int[] arg_1");
    EXPECT_NE(text.find("    var V_0 = arg_1.Length;\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("(int)"), std::string::npos) << "no redundant cast around ldlen";
}

TEST(ILAstToCSharp, CompoundAssignmentForSelfBinaryStore) {
    // `V = V + expr` -> `V += expr`; `V = V + 1` -> `V++`; `V = V - 1` -> `V--`.
    auto V = MakeVar(VariableKind::Local, "V_0", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(V, std::make_unique<LdcI4>(0)));  // declare V_0
    block->Add(std::make_unique<StLoc>(V,
        std::make_unique<BinaryNumericInstruction>(
            std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(2),
            BinaryNumericOperator::Add)));
    block->Add(std::make_unique<StLoc>(V,
        std::make_unique<BinaryNumericInstruction>(
            std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(1),
            BinaryNumericOperator::Add)));
    block->Add(std::make_unique<StLoc>(V,
        std::make_unique<BinaryNumericInstruction>(
            std::make_unique<LdLoc>(V), std::make_unique<LdcI4>(1),
            BinaryNumericOperator::Sub)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    V_0 += 2;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    V_0++;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    V_0--;\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, PlainStoreWhenLeftIsNotTheTarget) {
    // `V = other + expr` (left is a different variable) stays a plain store.
    auto V = MakeVar(VariableKind::Local, "V_0", 0);
    auto other = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(V,
        std::make_unique<BinaryNumericInstruction>(
            std::make_unique<LdLoc>(other), std::make_unique<LdcI4>(1),
            BinaryNumericOperator::Add)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "int arg_1");
    EXPECT_NE(text.find("    var V_0 = (arg_1 + 1);\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("V_0 += "), std::string::npos) << "not a compound assignment";
}

TEST(ILAstToCSharp, TypedDeclarationUsesCSharpKeyword) {
    // A local whose type is a known primitive declares with the C# keyword
    // (`int V_0 = ...`), not `var`. A null-typed local (e.g. a stack slot)
    // falls back to `var`.
    auto intV = MakeVar(VariableKind::Local, "V_0", 0,
        std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto untyped = MakeVar(VariableKind::Local, "V_1", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(intV, std::make_unique<LdcI4>(0)));
    block->Add(std::make_unique<StLoc>(untyped, std::make_unique<LdcI4>(0)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    int V_0 = 0;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    var V_1 = 0;\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SubtractionFromZeroIsUnaryNegation) {
    // `0 - x` is the IL for unary negation `-x`.
    auto x = MakeVar(VariableKind::Parameter, "x", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
        std::make_unique<BinaryNumericInstruction>(
            std::make_unique<LdcI4>(0), std::make_unique<LdLoc>(x),
            BinaryNumericOperator::Sub)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "int x");
    EXPECT_NE(text.find("    var V_0 = -x;\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("(0 - x)"), std::string::npos) << "not a binary subtraction";
}

TEST(ILAstToCSharp, BooleanEqualityToZeroIsLogicalNot) {
    // `comp(eq, ldloc boolVar, ldc.i4 0)` is `!boolVar`; `comp(ne, .., 0)` is
    // just `boolVar`. Only when the variable's type is Boolean.
    auto flag = MakeVar(VariableKind::Parameter, "flag", 0,
        std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto block = std::make_unique<Block>();
    // if (flag == 0) return;
    block->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(flag), std::make_unique<LdcI4>(0),
                               ComparisonKind::Equality),
        std::make_unique<Leave>(nullptr)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "bool flag");
    EXPECT_NE(text.find("if (!flag)"), std::string::npos) << text;
    EXPECT_EQ(text.find("flag == 0"), std::string::npos) << "bool == 0 is !bool";
}

TEST(ILAstToCSharp, ConstantTrueCatchFilterIsOmitted) {
    // A plain catch carries the constant filter ldc.i4(1) (BlockBuilder.cs);
    // the C#-text seed prints it as a plain `catch (T name)`, no `when`.
    auto tryC = std::make_unique<BlockContainer>();
    {
        auto tb = std::make_unique<Block>();
        auto fn0 = tryC.get();
        tb->SetFinal(std::make_unique<Leave>(nullptr));
        tryC->AddBlock(std::move(tb));
        (void)fn0;
    }
    auto tc = std::make_unique<TryCatch>(std::move(tryC));
    auto hxVar = MakeVar(VariableKind::ExceptionStackSlot, "E_5", -1,
                         std::make_unique<KnownType>(KnownTypeCode::Object));
    auto bodyC = std::make_unique<BlockContainer>();
    {
        auto hb = std::make_unique<Block>();
        bodyC->AddBlock(std::move(hb));
    }
    tc->AddHandler(std::make_unique<TryCatchHandler>(
        std::make_unique<LdcI4>(1), std::move(bodyC), hxVar));

    auto block = std::make_unique<Block>();
    block->Add(std::move(tc));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("catch (System.Object E_5)"), std::string::npos) << text;
    EXPECT_EQ(text.find("when ("), std::string::npos) << text;
}

TEST(ILAstToCSharp, RethrowEmitsBareThrow) {
    auto block = std::make_unique<Block>();
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Rethrow>());
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    throw;\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, EmptyBodyEmitsEmptyMethod) {
    auto fn = std::make_unique<ILFunction>();
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text, "void M()\n{\n}\n");
}

TEST(ILAstToCSharp, DecodesRealGetterFromMscorlib) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::uint32_t stringTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "String") { stringTok = t.Token; break; }
    }
    ASSERT_NE(stringTok, 0u);

    bool found = false;
    for (const auto& m : f.GetMethods(stringTok)) {
        if (m.Name != "get_FirstChar" || m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        ASSERT_NE(fn, nullptr) << "ReadIL failed on String::get_FirstChar";
        fn->CheckInvariant(ILPhase::Normal);
        std::string text = ILAstToCSharp(*fn, "char", "get_FirstChar", "");
        EXPECT_NE(text.find("return m_firstChar;"), std::string::npos) << text;
        found = true;
        break;
    }
    ASSERT_TRUE(found) << "System.String::get_FirstChar not found in fixture";
}

TEST(ILAstToCSharp, TranslatesEveryDecodableMscorlibMethodWithoutCrashing) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int translated = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto fn = ReadIL(f, m.Token, m.RVA);
            if (!fn) continue;
            std::string text = ILAstToCSharp(*fn, "void", m.Name, "");
            EXPECT_FALSE(text.empty());
            EXPECT_NE(text.find(m.Name), std::string::npos) << text;
            ++translated;
        }
        if (translated > 3000) break;
    }
    EXPECT_GT(translated, 1000) << "too few methods translated";
}

TEST(ILAstToCSharp, LoopContainerRendersAsWhileTrueWithContinueAndBreak) {
    // A Loop-kind container renders as `while (true) { ... }`; a back-edge
    // branch to the header (the first block) is `continue`, and a leave of
    // the loop container is `break`. A leave of the function body is `return`.
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    // Pre-header block: holds the loop container, then returns.
    auto preHeader = std::make_unique<Block>();
    // The loop container.
    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::Loop;
    auto header = std::make_unique<Block>();
    Block* headerPtr = header.get();
    loopC->AddBlock(std::move(header));
    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();
    loopC->AddBlock(std::move(body));
    auto incr = std::make_unique<Block>();
    Block* incrPtr = incr.get();
    loopC->AddBlock(std::move(incr));
    // header: leave(loop) (break out). body: br header (mid-loop continue).
    // incr: br header (last-block back-edge -- implicit, dropped).
    headerPtr->SetFinal(std::make_unique<Leave>(loopC.get()));
    bodyPtr->SetFinal(std::make_unique<Branch>(headerPtr));
    incrPtr->SetFinal(std::make_unique<Branch>(headerPtr));
    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("while (true)"), std::string::npos) << text;
    EXPECT_NE(text.find("break;"), std::string::npos) << text;
    EXPECT_NE(text.find("continue;"), std::string::npos) << text;
    EXPECT_NE(text.find("return;"), std::string::npos) << text;
    // No IL_ labels and no gotos (the loop header is not labeled).
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
}

TEST(ILAstToCSharp, ReturnLdcI4InBoolFunctionIsFalseTrue) {
    // `leave(ldc.i4(0))` / `leave(ldc.i4(1))` in a Boolean-returning function
    // renders as `return false;` / `return true;` (the IL idiom for bool
    // return values). A non-bool function keeps `return 0;` / `return 1;`.
    auto block = std::make_unique<Block>();
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdcI4>(0)));
    fn->CheckInvariant(ILPhase::Normal);

    // A bool-returning function: return false.
    std::string text = ILAstToCSharp(*fn, "bool", "M", "");
    EXPECT_NE(text.find("return false;"), std::string::npos) << text;
    EXPECT_EQ(text.find("return 0;"), std::string::npos) << text;

    // An int-returning function: return 0 (not false).
    std::string textInt = ILAstToCSharp(*fn, "int", "M", "");
    EXPECT_NE(textInt.find("return 0;"), std::string::npos) << textInt;
    EXPECT_EQ(textInt.find("return false;"), std::string::npos) << textInt;
}


// The seed's recursive emitters (EmitStatement / EmitBlock / EmitContainer /
// EmitBraced / Expr) have a recursion-depth guard so a malformed ILAst -- a
// cycle or a pathologically deep tree -- produces a single `/* max rendering
// depth */` marker instead of a runaway (a repeated token such as `lock (...)`
// ad infinitum) that would corrupt the output or OOM. A pathologically deep
// but otherwise valid strict tree (deeply nested if-then blocks) triggers the
// guard; the output is bounded and contains the marker.
TEST(ILAstToCSharp, DepthGuardBoundsPathologicallyDeepTree) {
    // Build a 150-deep nested if-then: each block's final is an IfInstruction
    // whose TrueInst is a fresh Block, nested 150x. Each nesting level
    // increments the seed's depth_ counter ~3x (EmitStatement + EmitBraced +
    // EmitBlock), so 150 levels exceeds the 300 limit and the guard fires.
    auto fn = MakeFunction({});
    auto* container = fn->Body.get();
    auto rootBlock = std::make_unique<Block>();
    Block* cur = rootBlock.get();
    fn->Body->AddBlock(std::move(rootBlock));
    for (int i = 0; i < 150; ++i) {
        auto inner = std::make_unique<Block>();
        Block* innerPtr = inner.get();
        cur->SetFinal(std::make_unique<IfInstruction>(
            std::make_unique<LdcI4>(0), std::move(inner), nullptr));
        cur = innerPtr;  // descend into the fresh inner block (owned by the if)
    }
    cur->SetFinal(ReturnFinal(container));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    // The guard must fire: the output contains the marker, not a runaway.
    EXPECT_NE(text.find("max rendering depth"), std::string::npos)
        << "the depth guard must fire on a 150-deep tree";
    // The output is bounded: well under a runaway (a runaway would be
    // megabytes; a 150-deep tree with the guard is a few KB at most).
    EXPECT_LT(text.size(), 100000u)
        << "the output must be bounded, not a runaway";
    // The guard does NOT crash (the method still renders, bounded by the guard).
    EXPECT_NE(text.find("void M()"), std::string::npos);
}

TEST(ILAstToCSharp, PreHeaderEntryBranchToWhileConditionIsDropped) {
    // A while loop's pre-header entry branch targets the loop condition block.
    // That block becomes the `while (cond)` head and carries no IL label, so
    // emitting `goto IL_XXXX;` for the entry branch produces a DANGLING goto
    // (a reference to a label that is never emitted). The pre-header falls
    // through into the loop, so the branch must be dropped instead.
    //
    //   b0:           br header            (entry; must be dropped)
    //   preHeader:    { whileC } ; leave   (the While container, emitted next)
    //   whileC:       header: if (cond) br body else leave(whileC)   (while head)
    //                 body:   ... ; br header                         (back edge)
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    auto whileC = std::make_unique<BlockContainer>();
    whileC->Kind = ContainerKind::While;
    auto header = std::make_unique<Block>();
    header->StartILOffset = 0x20;
    Block* headerPtr = header.get();
    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();

    auto preHeader = std::make_unique<Block>();
    Block* preHeaderPtr = preHeader.get();

    // header: while condition `if (cond) br body else leave(whileC)`.
    headerPtr->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::make_unique<Branch>(bodyPtr), std::make_unique<Leave>(whileC.get())));
    // body: a statement, then back-edge to the header (dropped at render).
    bodyPtr->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                         std::make_unique<LdcI4>(1)));
    bodyPtr->SetFinal(std::make_unique<Branch>(headerPtr));

    whileC->AddBlock(std::move(header));
    whileC->AddBlock(std::move(body));
    preHeader->Add(std::move(whileC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));

    // b0: the entry branch into the loop condition (the dangling goto if kept).
    auto b0 = std::make_unique<Block>();
    auto entryBr = std::make_unique<Branch>(headerPtr);
    entryBr->TargetOffset = 0x20;  // the label text if (wrongly) emitted
    b0->SetFinal(std::move(entryBr));

    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(preHeader));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("while ("), std::string::npos) << text;
    EXPECT_EQ(text.find("goto IL_0020"), std::string::npos)
        << "the entry branch to the while condition must be dropped, not emitted "
           "as a dangling goto:\n" << text;
}

TEST(ILAstToCSharp, CSharpTypeNameRendersGeneric) {
    // A parameterized (generic) type must render as C# `List<string>`, not the
    // ECMA reflection name `List`1<System.String>` nor a mangled last-segment
    // substring like `String>`.
    using namespace ILSpy::Decompiler::TypeSystem;
    auto listOfString = std::make_shared<ParameterizedType>(
        std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic.List`1")),
        std::vector<ITypePtr>{ std::make_shared<KnownType>(KnownTypeCode::String) });
    EXPECT_EQ(CSharpTypeName(listOfString), "List<string>");
    // Nested: Dictionary<int, List<string>>.
    auto dictOfIntListString = std::make_shared<ParameterizedType>(
        std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic.Dictionary`2")),
        std::vector<ITypePtr>{ std::make_shared<KnownType>(KnownTypeCode::Int32), listOfString });
    EXPECT_EQ(CSharpTypeName(dictOfIntListString), "Dictionary<int, List<string>>");
}

TEST(ILAstToCSharp, GenericNewObjRendersCSharpTypeArguments) {
    // A newobj on a generic type must render `new List<string>(...)` via the
    // resolved DeclaringType, not the metadata name `List`1<System.String>`.
    using namespace ILSpy::Decompiler::TypeSystem;
    auto listOfString = std::make_shared<ParameterizedType>(
        std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic.List`1")),
        std::vector<ITypePtr>{ std::make_shared<KnownType>(KnownTypeCode::String) });
    auto call = std::make_unique<Call>("System.Collections.Generic.List`1<System.String>::.ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = listOfString;

    auto v = MakeVar(VariableKind::Local, "list", 0, listOfString);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v, std::move(call)));
    auto fn = MakeFunction({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("new List<string>()"), std::string::npos) << text;
    EXPECT_EQ(text.find("List`1"), std::string::npos) << "metadata arity suffix leaked:\n" << text;
}

TEST(ILAstToCSharp, NopElseArmIsNotEmitted) {
    // An if whose else arm is a Nop (or an empty block) contributes nothing and
    // must not render an empty `else { }` block.
    auto fn = MakeFunction({});
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                       std::make_unique<LdcI4>(1)));
    auto trueArm = std::make_unique<Block>();
    trueArm->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_1", 1),
                                         std::make_unique<LdcI4>(2)));
    block->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::move(trueArm), std::make_unique<Nop>()));
    fn->Body->AddBlock(std::move(block));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("if (1 != 0)"), std::string::npos) << text;
    EXPECT_EQ(text.find("else"), std::string::npos)
        << "a Nop else arm must not produce an `else` block:\n" << text;
}

TEST(ILAstToCSharp, RedundantConditionalGotoToNextBlockIsDropped) {
    // b0: if (1 != 0) br b1   (no else)   -- b1 is the NEXT block, so the
    //   branch is taken iff the condition holds and falls through otherwise;
    //   both paths reach b1, so the whole if is a redundant no-op.
    // The branch is the TRUE ARM of the block-final if (not the block final
    // itself), which the fall-through drop used to miss, emitting a dangling
    // `if (1 != 0) goto IL_XXXX;` plus a redundant label.
    auto fn = MakeFunction({});
    auto b0 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                    std::make_unique<LdcI4>(1)));
    auto b1 = std::make_unique<Block>();
    b1->StartILOffset = 0x20;
    Block* b1Ptr = b1.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));

    auto br = std::make_unique<Branch>(b1Ptr);
    br->TargetOffset = 0x20;
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(0),
                               ComparisonKind::Inequality),
        std::move(br)));
    fn->Body->Blocks[1]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos)
        << "a conditional jump to the immediately-following block is a no-op:\n" << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos)
        << "no loop/block label should be emitted for a dropped jump:\n" << text;
}

TEST(ILAstToCSharp, GenericMemberRefMetadataNameRendersCSharpForm) {
    // A reference to a member of a generic type (e.g. `EmptyArray<System.Byte>.Zero`
    // or `ArraySortHelper<System.Object>::.ctor`) must not leak the ECMA metadata
    // name's ``N<...>` arity suffix. The arity marker is a CLR encoding detail;
    // C# writes the type as `Name<args>`.
    auto fn = MakeFunction({});
    auto block = std::make_unique<Block>();
    // return System.EmptyArray`1<System.Byte>::Zero  (static field read as an
    // expression; the seed renders ldsflda as the raw name for now).
    block->Add(std::make_unique<StLoc>(
        MakeVar(VariableKind::Local, "V_0", 0),
        std::make_unique<LdTypeToken>(
            "System.Collections.Generic.List`1<System.String>")));
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(ReturnFinal(fn->Body.get(),
        std::make_unique<LdLoc>(MakeVar(VariableKind::Local, "V_0", 0))));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "object", "M", "");
    EXPECT_EQ(text.find("`1"), std::string::npos)
        << "the arity suffix ``N` leaked into the output:\n" << text;
    EXPECT_NE(text.find("List<System.String>"), std::string::npos) << text;
}

TEST(ILAstToCSharp, NestedInlineAssignmentRendersChained) {
    // An inline assignment used as the VALUE of another store -- the IL idiom
    // for C# `a = b = value` -- previously fell through Expr to the `(default)`
    // comment. Render it as `a = b = value`.
    auto fn = MakeFunction({});
    auto block = std::make_unique<Block>();
    auto aVar = MakeVar(VariableKind::Local, "dup_0", 0);
    auto bVar = MakeVar(VariableKind::Local, "V_1", 1);
    // stloc dup_0, stloc V_1, ldc.i4 7   ->  dup_0 = V_1 = 7
    block->Add(std::make_unique<StLoc>(aVar,
        std::make_unique<StLoc>(bVar, std::make_unique<LdcI4>(7))));
    block->SetFinal(ReturnFinal(fn->Body.get()));
    fn->Body->AddBlock(std::move(block));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("dup_0 = V_1 = 7;"), std::string::npos) << text;
    EXPECT_EQ(text.find("(default)"), std::string::npos)
        << "inline-assignment value must not be the default fallback:\n" << text;
}

TEST(ILAstToCSharp, GenericArrayNewRendersMethodTypeParamName) {
    // In a generic method, `newarr T` carries a TypeSpec operand `!!0[...]`
    // whose MVAR scopes to the CALLING method's generic params. The IL reader
    // resolves TypeSpec tokens with that context so the declaration-ordered
    // method name (T) renders instead of the positional placeholder.
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::uint32_t arrayTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Array") { arrayTok = t.Token; break; }
    }
    ASSERT_NE(arrayTok, 0u);

    bool found = false;
    for (const auto& m : f.GetMethods(arrayTok)) {
        if (m.Name != "Resize" || m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        ASSERT_NE(fn, nullptr) << "ReadIL failed on Array::Resize";
        fn->CheckInvariant(ILPhase::Normal);
        std::string text = ILAstToCSharp(*fn, "void", "Resize", "");
        EXPECT_NE(text.find("new T[newSize]"), std::string::npos)
            << "expected named method type param in newarr emission:\n" << text;
        EXPECT_EQ(text.find("!!0"), std::string::npos)
            << "positional MVAR placeholder leaked into emission:\n" << text;
        found = true;
        break;
    }
    ASSERT_TRUE(found) << "System.Array::Resize<T> not found in fixture";
}

TEST(ILAstToCSharp, StaticCallOnCallerContextGenericRendersNamedArg) {
    // A static call on a generic type whose declaring-type token is a TypeSpec
    // (e.g. EqualityComparer<!0>.get_Default inside ValueTuple<T1>) resolves
    // through ResolveTokenToString. That TypeSpec's VAR binds in the caller's
    // scope, so the text should read EqualityComparer<T1>.Default.GetHashCode.
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::uint32_t vtTok = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "ValueTuple`1") { vtTok = t.Token; break; }
    }
    ASSERT_NE(vtTok, 0u) << "System.ValueTuple`1 not found in fixture";

    bool found = false;
    for (const auto& m : f.GetMethods(vtTok)) {
        if (m.Name != "GetHashCode" || m.RVA == 0) continue;
        auto sig = f.GetMethodSignature(m.Token);
        if (!sig || !sig->ParameterTypes.empty()) continue;  // the parameterless one
        auto fn = ReadIL(f, m.Token, m.RVA);
        ASSERT_NE(fn, nullptr) << "ReadIL failed on ValueTuple`1::GetHashCode";
        fn->CheckInvariant(ILPhase::Normal);
        std::string text = ILAstToCSharp(*fn, "int", "GetHashCode", "");
        EXPECT_NE(text.find("EqualityComparer<T1>.Default.GetHashCode"), std::string::npos)
            << "expected caller-scope VAR name in static-call text:\n" << text;
        EXPECT_EQ(text.find("!0"), std::string::npos)
            << "positional VAR placeholder leaked into emission:\n" << text;
        found = true;
        break;
    }
    ASSERT_TRUE(found) << "System.ValueTuple`1::GetHashCode() not found in fixture";
}

TEST(ILAstToCSharp, ForLoopContainerRendersForWithIncrementClause) {
    // A For-kind container (HighLevelLoopTransform's MatchForLoop result:
    // entry+condition if, body blocks, and the increment block at the END)
    // renders as `for (; cond; incr) { body }`. The increment clause comes
    // from the last block's StLoc(s) (`num++`), the increment block's
    // back-edge branch is implicit, and an in-body `br entry` renders
    // `continue;`.
    auto num = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    num->Name = "num";
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto preHeader = std::make_unique<Block>();
    preHeader->Add(std::make_unique<StLoc>(num, std::make_unique<LdcI4>(0)));  // num = 0;

    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::For;
    BlockContainer* loopPtr = loopC.get();
    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();
    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();
    auto incr = std::make_unique<Block>();
    Block* incrPtr = incr.get();

    // entry: if (num < 5) br body else leave loop
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(5),
                               ComparisonKind::LessThan),
        std::make_unique<Branch>(bodyPtr), std::make_unique<Leave>(loopPtr)));
    loopC->AddBlock(std::move(entry));
    // body: if (num == 3) br incr (an in-body continue), num = num * 2;
    // br incr (the natural fall into the increment block).
    body->Add(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(3),
                               ComparisonKind::Equality),
        std::make_unique<Branch>(incrPtr)));
    body->Add(std::make_unique<StLoc>(
        num, std::make_unique<BinaryNumericInstruction>(
                 std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(2),
                 BinaryNumericOperator::Mul, StackType::I4)));
    bodyPtr->SetFinal(std::make_unique<Branch>(incrPtr));
    loopC->AddBlock(std::move(body));
    // incr: num++; br entry (implicit back-edge)
    incr->Add(std::make_unique<StLoc>(
        num, std::make_unique<BinaryNumericInstruction>(
                 std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(1),
                 BinaryNumericOperator::Add, StackType::I4)));
    incrPtr->SetFinal(std::make_unique<Branch>(entryPtr));
    loopC->AddBlock(std::move(incr));

    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->Variables.push_back(num);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("for (; num < 5; num++)"), std::string::npos) << text;
    EXPECT_NE(text.find("num *= 2;"), std::string::npos) << text;
    EXPECT_NE(text.find("continue;"), std::string::npos) << text;
    // The increment block's back-edge renders nothing extra: no `goto IL_`.
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
}
TEST(ILAstToCSharp, ForSplitIncrementEmitsNoTrailingContinue) {
    // MatchForLoop's no-dedicated-block split leaves the body's final as a
    // Branch to the NEW increment block. At the end of the loop body that
    // edge is the iteration itself, not a `continue` -- the emission must
    // drop the trailing `br increment`, not print a dangling `continue;`
    // right before the loop close brace.
    auto num = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    num->Name = "num";
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto preHeader = std::make_unique<Block>();
    preHeader->Add(std::make_unique<StLoc>(num, std::make_unique<LdcI4>(0)));  // num = 0;

    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::For;
    BlockContainer* loopPtr = loopC.get();
    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();
    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();
    auto incr = std::make_unique<Block>();
    Block* incrPtr = incr.get();

    // entry: if (num < 5) br body else leave loop
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(5),
                               ComparisonKind::LessThan),
        std::make_unique<Branch>(bodyPtr), std::make_unique<Leave>(loopPtr)));
    loopC->AddBlock(std::move(entry));
    // body: num = num * 2; br incr  (the split's iteration step -- drops silently)
    body->Add(std::make_unique<StLoc>(
        num, std::make_unique<BinaryNumericInstruction>(
                 std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(2),
                 BinaryNumericOperator::Mul, StackType::I4)));
    bodyPtr->SetFinal(std::make_unique<Branch>(incrPtr));
    loopC->AddBlock(std::move(body));
    // incr: num++; br entry (implicit back-edge)
    incr->Add(std::make_unique<StLoc>(
        num, std::make_unique<BinaryNumericInstruction>(
                 std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(1),
                 BinaryNumericOperator::Add, StackType::I4)));
    incrPtr->SetFinal(std::make_unique<Branch>(entryPtr));
    loopC->AddBlock(std::move(incr));

    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->Variables.push_back(num);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("for (; num < 5; num++)"), std::string::npos) << text;
    EXPECT_NE(text.find("num *= 2"), std::string::npos) << text;
    // The body's trailing br-incr is the iteration, not a continue.
    EXPECT_EQ(text.find("continue;"), std::string::npos) << text;
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
}

TEST(ILAstToCSharp, WhileLoopHeaderPreambleStatementsRenderInsideBody) {
    // A While container's header block may carry statements between the entry
    // (the guard) and the condition if: the do-while-like `u = f(); if (c) }`
    // shape the C# compiler emits. The emission must not drop them silently.
    auto num = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    num->Name = "num";
    auto other = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    other->Name = "other";
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto preHeader = std::make_unique<Block>();
    preHeader->Add(std::make_unique<StLoc>(num, std::make_unique<LdcI4>(0)));  // num = 0;

    auto loopC = std::make_unique<BlockContainer>();
    loopC->Kind = ContainerKind::While;
    BlockContainer* loopPtr = loopC.get();
    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();
    auto body = std::make_unique<Block>();
    Block* bodyPtr = body.get();

    // header: stloc other(42); then the condition.
    entry->Add(std::make_unique<StLoc>(other, std::make_unique<LdcI4>(42)));
    entry->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(5),
                               ComparisonKind::LessThan),
        std::make_unique<Branch>(bodyPtr), std::make_unique<Leave>(loopPtr)));
    loopC->AddBlock(std::move(entry));
    // body: stloc num(num+1); br entry.
    body->Add(std::make_unique<StLoc>(
        num, std::make_unique<BinaryNumericInstruction>(
                 std::make_unique<LdLoc>(num), std::make_unique<LdcI4>(1),
                 BinaryNumericOperator::Add, StackType::I4)));
    bodyPtr->SetFinal(std::make_unique<Branch>(entryPtr));
    loopC->AddBlock(std::move(body));

    preHeader->Add(std::move(loopC));
    preHeader->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(preHeader));
    fn->Variables.push_back(num);
    fn->Variables.push_back(other);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("while (num < 5)"), std::string::npos) << text;
    // The header preamble (other = 42) must render inside the loop body --
    // the current emission drops it silently.
    EXPECT_NE(text.find("other = 42"), std::string::npos)
        << "header preamble statement dropped from the While body rendering:\n" << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
}

TEST(ILAstToCSharp, GotoFallingOutOfUsingConstructToFollowingBlockIsDropped) {
    // The using-exit shape `using (r) { ...; br after }; after:` emits a
    // redundant `goto after;` right before the using brace closes -- the
    // branch falls out of the construct to the textually-following block.
    // IsFallThroughGoto must see through the construct boundary (it currently
    // only checks same-container siblings): the goto drops silently.
    auto r = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    r->Name = "streamReader";

    auto fn = MakeFunction({});

    // Entry block: holds the UsingInstruction; falls through to the
    // following block (the construct-exit label).
    auto entry = std::make_unique<Block>();
    Block* entryPtr = entry.get();

    // After block: the block the using's interior goto falls out to.
    auto after = std::make_unique<Block>();
    Block* afterPtr = after.get();
    after->SetFinal(std::make_unique<Leave>(fn->Body.get()));

    // The using body container: [loop-like block whose final is br afterPtr].
    auto usingBody = std::make_unique<BlockContainer>();
    usingBody->Kind = ContainerKind::Loop;   // exercise loop shapes minimally
    auto work = std::make_unique<Block>();
    Block* workPtr = work.get();
    work->Add(std::make_unique<StLoc>(r, std::make_unique<LdcI4>(42)));
    // Inside the using, the block's final branches to the block after the
    // using (the construct-exit fall-through).
    work->SetFinal(std::make_unique<Branch>(afterPtr));
    usingBody->AddBlock(std::move(work));

    entry->Add(std::make_unique<UsingInstruction>(
        r, std::make_unique<LdNull>(), std::move(usingBody)));
    // The entry falls to the after block.
    entryPtr->SetFinal(std::make_unique<Branch>(afterPtr));
    fn->Body->AddBlock(std::move(entry));
    fn->Body->AddBlock(std::move(after));
    fn->Variables.push_back(r);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    // The goto inside the using falls out of the construct to `after` -- no
    // goto should print (it is the construct-exit fall-through).
    EXPECT_EQ(text.find("goto"), std::string::npos)
        << "redundant exit-from-construct goto emitted:\n" << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    // The using statement still renders (the seed elides the using-local and
    // renders `using (resource)` for whatever expression shape the call had).
    EXPECT_NE(text.find("using (null)"), std::string::npos) << text;
    // The label-free block content renders.
    EXPECT_NE(text.find("streamReader = 42"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchBodyThunksInlineIntoCaseSections) {
    // The lowered-switch layout the seed receives: a switch instruction whose
    // every section body is a Branch thunk to a body block laid out after the
    // switch-host block in the same outer container, with the bodies ending in
    // a Branch to the shared exit block that follows them all. The C# form
    // places the bodies under their case labels with `break` in place of the
    // exit branch. The seed previously rendered the thunk gotos and left the
    // bodies as labeled blocks after the switch.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto v1 = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 1);
    v1->Name = "x";
    auto v2 = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 2);
    v2->Name = "y";
    auto fn = MakeFunction({});

    auto host = std::make_unique<Block>();
    Block* hostPtr = host.get();
    auto b1 = std::make_unique<Block>();
    Block* b1Ptr = b1.get();
    auto b2 = std::make_unique<Block>();
    Block* b2Ptr = b2.get();
    auto exitB = std::make_unique<Block>();
    Block* exitPtr = exitB.get();

    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(exitB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto sec0 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    auto brCase = std::make_unique<Branch>(b1Ptr);
    brCase->HasOffset = false;
    sec0->SetBody(std::move(brCase));
    sw->AddSection(std::move(sec0));
    auto secDef = std::make_unique<SwitchSection>();
    auto brDef = std::make_unique<Branch>(b2Ptr);
    brDef->HasOffset = false;
    secDef->SetBody(std::move(brDef));
    sw->AddSection(std::move(secDef));
    hostPtr->SetFinal(std::move(sw));

    // case 0 body: x = 1; br exit
    b1Ptr->Add(std::make_unique<StLoc>(v1, std::make_unique<LdcI4>(1)));
    b1Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    // default body: y = 2; br exit
    b2Ptr->Add(std::make_unique<StLoc>(v2, std::make_unique<LdcI4>(2)));
    b2Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    // exit: return
    exitPtr->SetFinal(ReturnFinal(fn->Body.get()));

    fn->Variables.push_back(v);
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    // The bodies inline under their cases: no thunk gotos, no IL labels.
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 0:"), std::string::npos) << text;
    EXPECT_NE(text.find("default:"), std::string::npos) << text;
    // The body statements appear INSIDE the switch braces.
    auto casePos = text.find("case 0:");
    auto exitPos = text.find("return;");
    EXPECT_NE(casePos, std::string::npos);
    EXPECT_NE(text.find("x = 1;"), std::string::npos) << text;
    EXPECT_NE(text.find("y = 2;"), std::string::npos) << text;
    EXPECT_LT(text.find("x = 1;"), exitPos) << text;
    // The exit-branch gotos render as `break;`.
    EXPECT_NE(text.find("break;"), std::string::npos) << text;
    // The exit block's label does not print (no gotos remain referencing it).
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchBodyWithConditionalExitInlineAsIfBreak) {
    // A body may end with a conditional exit `if (cond) br exit` (a block-
    // final if with no else): it renders as `if (cond) break;` inline. The
    // fall-through (the cond-false path) continues into the *next outer
    // block*; for a mid-switch body that must be the next section's body
    // (section order == outer order wherever fall-through ordering matters),
    // otherwise the fold bails to the dbgthunk rendering.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto v1 = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 1);
    v1->Name = "x";
    auto v2 = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 2);
    v2->Name = "y";
    auto fn = MakeFunction({});

    auto host = std::make_unique<Block>();
    Block* hostPtr = host.get();
    auto b1 = std::make_unique<Block>();
    Block* b1Ptr = b1.get();
    auto b2 = std::make_unique<Block>();
    Block* b2Ptr = b2.get();
    auto exitB = std::make_unique<Block>();
    Block* exitPtr = exitB.get();

    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(exitB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto sec0 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    auto brCase = std::make_unique<Branch>(b1Ptr);
    brCase->HasOffset = false;
    sec0->SetBody(std::move(brCase));
    sw->AddSection(std::move(sec0));
    auto secDef = std::make_unique<SwitchSection>();
    auto brDef = std::make_unique<Branch>(b2Ptr);
    brDef->HasOffset = false;
    secDef->SetBody(std::move(brDef));
    sw->AddSection(std::move(secDef));
    hostPtr->SetFinal(std::move(sw));

    // case 0 body: x = 1; if (v) br exit   -- the false path falls through
    // into b2 (the next OUTER block, which is also the next SECTION body).
    b1Ptr->Add(std::make_unique<StLoc>(v1, std::make_unique<LdcI4>(1)));
    b1Ptr->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<Branch>(exitPtr)));
    // default body: y = 2; br exit
    b2Ptr->Add(std::make_unique<StLoc>(v2, std::make_unique<LdcI4>(2)));
    b2Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    // exit: return
    exitPtr->SetFinal(ReturnFinal(fn->Body.get()));

    fn->Variables.push_back(v);
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 0:"), std::string::npos) << text;
    EXPECT_NE(text.find("if (num) break;"), std::string::npos) << text;
    EXPECT_NE(text.find("default:"), std::string::npos) << text;
    EXPECT_NE(text.find("y = 2;"), std::string::npos) << text;
    EXPECT_NE(text.find("break;"), std::string::npos) << text;
    // The case-0 conditional break comes BEFORE the default block (the
    // section-order == outer-order constraint is what makes this safe).
    EXPECT_LT(text.find("if (num) break;"), text.find("default:")) << text;
}

TEST(ILAstToCSharp, SwitchBodyWithConditionalExitAndNopElseInlines) {
    // Same as SwitchBodyWithConditionalExitInlineAsIfBreak but the trailing if
    // carries a redundant Nop else arm (`if (cond) br exit else nop`). The Nop
    // else is semantically fall-through (no-op), so the body is equivalent to
    // a no-else conditional exit and must inline as `if (cond) break;` -- not
    // bail to the goto thunk form. The emitter already treats a Nop arm as
    // empty; the analysis must too.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto v1 = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 1);
    v1->Name = "x";
    auto v2 = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 2);
    v2->Name = "y";
    auto fn = MakeFunction({});

    auto host = std::make_unique<Block>();
    Block* hostPtr = host.get();
    auto b1 = std::make_unique<Block>();
    Block* b1Ptr = b1.get();
    auto b2 = std::make_unique<Block>();
    Block* b2Ptr = b2.get();
    auto exitB = std::make_unique<Block>();
    Block* exitPtr = exitB.get();

    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(exitB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto sec0 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    auto brCase = std::make_unique<Branch>(b1Ptr);
    brCase->HasOffset = false;
    sec0->SetBody(std::move(brCase));
    sw->AddSection(std::move(sec0));
    auto secDef = std::make_unique<SwitchSection>();
    auto brDef = std::make_unique<Branch>(b2Ptr);
    brDef->HasOffset = false;
    secDef->SetBody(std::move(brDef));
    sw->AddSection(std::move(secDef));
    hostPtr->SetFinal(std::move(sw));

    // case 0 body: x = 1; if (v) br exit else nop -- the redundant Nop else
    // is fall-through; the false path falls into b2 (next outer == next section).
    b1Ptr->Add(std::make_unique<StLoc>(v1, std::make_unique<LdcI4>(1)));
    b1Ptr->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::make_unique<Branch>(exitPtr),
        std::make_unique<Nop>()));
    // default body: y = 2; br exit
    b2Ptr->Add(std::make_unique<StLoc>(v2, std::make_unique<LdcI4>(2)));
    b2Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    exitPtr->SetFinal(ReturnFinal(fn->Body.get()));

    fn->Variables.push_back(v);
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 0:"), std::string::npos) << text;
    EXPECT_NE(text.find("if (num) break;"), std::string::npos) << text;
    EXPECT_NE(text.find("default:"), std::string::npos) << text;
    EXPECT_NE(text.find("y = 2;"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchBodyWithThrowExitIsInlined) {
    // A case body that terminates in a `throw` (guarding a public API entry
    // into the switch) has a Throw final, contributing no exit branch. The
    // emission inlines it with the throw in place; the other sections must
    // carry the exit convergence.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = MakeFunction({});

    auto host = std::make_unique<Block>();
    Block* hostPtr = host.get();
    auto b1 = std::make_unique<Block>();
    Block* b1Ptr = b1.get();
    auto b2 = std::make_unique<Block>();
    Block* b2Ptr = b2.get();
    auto exitB = std::make_unique<Block>();
    Block* exitPtr = exitB.get();

    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(exitB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto sec0 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(5)));
    auto brCase = std::make_unique<Branch>(b1Ptr);
    brCase->HasOffset = false;
    sec0->SetBody(std::move(brCase));
    sw->AddSection(std::move(sec0));
    auto secDef = std::make_unique<SwitchSection>();
    auto brDef = std::make_unique<Branch>(b2Ptr);
    brDef->HasOffset = false;
    secDef->SetBody(std::move(brDef));
    sw->AddSection(std::move(secDef));
    hostPtr->SetFinal(std::move(sw));

    // case 5 body: throw new System.ArgumentException("x");
    b1Ptr->SetFinal(std::make_unique<Throw>(
        std::make_unique<Call>()));
    // default body: stloc; br exit
    b2Ptr->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    b2Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    exitPtr->SetFinal(ReturnFinal(fn->Body.get()));

    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 5:"), std::string::npos) << text;
    EXPECT_NE(text.find("throw"), std::string::npos) << text;
    EXPECT_NE(text.find("default:"), std::string::npos) << text;
    EXPECT_NE(text.find("break;"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchBodyWithConditionalReturnInlineAsIfReturn) {
    // A case body whose trailing is `if (cond) { return; }` (no else): the true
    // arm is a Block whose final is a Leave. The body contributes no exit branch
    // (the true path returns); the false path falls positionally. The emission
    // inlines it as `if (cond) { return; }` and the section fall-through handles
    // the false path.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = MakeFunction({});
    auto host = std::make_unique<Block>(); Block* hostPtr = host.get();
    auto b1 = std::make_unique<Block>(); Block* b1Ptr = b1.get();
    auto b2 = std::make_unique<Block>(); Block* b2Ptr = b2.get();
    auto exitB = std::make_unique<Block>(); Block* exitPtr = exitB.get();
    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(exitB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto sec0 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    auto brCase = std::make_unique<Branch>(b1Ptr); brCase->HasOffset = false;
    sec0->SetBody(std::move(brCase));
    sw->AddSection(std::move(sec0));
    auto secDef = std::make_unique<SwitchSection>();
    auto brDef = std::make_unique<Branch>(b2Ptr); brDef->HasOffset = false;
    secDef->SetBody(std::move(brDef));
    sw->AddSection(std::move(secDef));
    hostPtr->SetFinal(std::move(sw));

    // case 0 body: if (num) { return; }  -- the true arm is a Block with a Leave final.
    auto retBlock = std::make_unique<Block>();
    retBlock->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    b1Ptr->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::move(retBlock)));
    // default body: stloc; br exit
    b2Ptr->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    b2Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    exitPtr->SetFinal(ReturnFinal(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 0:"), std::string::npos) << text;
    EXPECT_NE(text.find("if (num)"), std::string::npos) << text;
    EXPECT_NE(text.find("return;"), std::string::npos) << text;
    EXPECT_NE(text.find("default:"), std::string::npos) << text;
    EXPECT_NE(text.find("break;"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchBodyWithFallingThroughTrueArmInlines) {
    // A case body whose trailing is `if (cond) { work; }` (no else): the true
    // arm is a Block with real instructions but NO final instruction, so it
    // falls through. The body contributes no exit branch -- both the true
    // path (after the work) and the false path fall positionally. The emission
    // inlines the if in place; the section fall-through handles the rest.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = MakeFunction({});
    auto host = std::make_unique<Block>(); Block* hostPtr = host.get();
    auto b2 = std::make_unique<Block>(); Block* b2Ptr = b2.get();
    auto b1 = std::make_unique<Block>(); Block* b1Ptr = b1.get();
    auto exitB = std::make_unique<Block>(); Block* exitPtr = exitB.get();
    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(exitB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto sec1 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(1)));
    sec1->SetBody([&]{ auto b=std::make_unique<Branch>(b2Ptr); b->HasOffset=false; return b; }());
    sw->AddSection(std::move(sec1));
    auto sec0 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(0)));
    sec0->SetBody([&]{ auto b=std::make_unique<Branch>(b1Ptr); b->HasOffset=false; return b; }());
    sw->AddSection(std::move(sec0));
    hostPtr->SetFinal(std::move(sw));

    // case 1 body (first section): stloc; br exit -- sets the convergence exit.
    b2Ptr->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2)));
    b2Ptr->SetFinal(std::make_unique<Branch>(exitPtr));
    // case 0 body (last section): if (num) { num = 1; } -- true arm is a Block
    // with an StLoc and no final, so it falls through; no exit branch.
    auto workBlock = std::make_unique<Block>();
    workBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    b1Ptr->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::move(workBlock)));
    exitPtr->SetFinal(ReturnFinal(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 0:"), std::string::npos) << text;
    EXPECT_NE(text.find("case 1:"), std::string::npos) << text;
    EXPECT_NE(text.find("if (num)"), std::string::npos) << text;
    EXPECT_NE(text.find("num = 1"), std::string::npos) << text;
    EXPECT_NE(text.find("num = 2"), std::string::npos) << text;
}

TEST(ILAstToCSharp, SwitchAllBodiesExitOrFallThroughNoConvergenceExit) {
    // The no-convergence switch: every body self-terminates (throw/return) or
    // falls positionally into the next section's body -- no body branches to a
    // shared exit. The Win32Error lowering shape: `case A: throw; case B:
    // doStuff(); case C: if (err) throw; ...; return;`. The fold accepts it
    // (exit = null); no `break;` lines render.
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = "num";
    auto fn = MakeFunction({});
    auto host = std::make_unique<Block>(); Block* hostPtr = host.get();
    auto b1 = std::make_unique<Block>(); Block* b1Ptr = b1.get();
    auto b2 = std::make_unique<Block>(); Block* b2Ptr = b2.get();
    auto b3 = std::make_unique<Block>(); Block* b3Ptr = b3.get();
    auto postB = std::make_unique<Block>(); Block* postPtr = postB.get();
    fn->Body->AddBlock(std::move(host));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));
    fn->Body->AddBlock(std::move(b3));
    fn->Body->AddBlock(std::move(postB));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v));
    auto s1 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(1)));
    s1->SetBody([&]{ auto b=std::make_unique<Branch>(b1Ptr); b->HasOffset=false; return b; }());
    sw->AddSection(std::move(s1));
    auto s2 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(2)));
    s2->SetBody([&]{ auto b=std::make_unique<Branch>(b2Ptr); b->HasOffset=false; return b; }());
    sw->AddSection(std::move(s2));
    auto s3 = std::make_unique<SwitchSection>(ILSpy::Decompiler::Util::LongSet(static_cast<long long>(3)));
    s3->SetBody([&]{ auto b=std::make_unique<Branch>(b3Ptr); b->HasOffset=false; return b; }());
    sw->AddSection(std::move(s3));
    hostPtr->SetFinal(std::move(sw));

    // case 1: throw (self-terminating)
    b1Ptr->SetFinal(std::make_unique<Throw>(std::make_unique<Call>()));
    // case 2: null-final (falls positionally into case 3's body)
    b2Ptr->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(42)));
    // case 3: if (num) throw; ...; return (self-terminating)
    auto throwBlk = std::make_unique<Block>();
    throwBlk->SetFinal(std::make_unique<Throw>(std::make_unique<Call>()));
    b3Ptr->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(v), std::move(throwBlk)));
    // post-switch block (continues after the switch)
    postPtr->SetFinal(ReturnFinal(fn->Body.get()));

    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_EQ(text.find("goto"), std::string::npos) << text;
    EXPECT_EQ(text.find("IL_"), std::string::npos) << text;
    EXPECT_NE(text.find("case 1:"), std::string::npos) << text;
    EXPECT_NE(text.find("case 2:"), std::string::npos) << text;
    EXPECT_NE(text.find("case 3:"), std::string::npos) << text;
    EXPECT_NE(text.find("throw"), std::string::npos) << text;
    EXPECT_NE(text.find("num = 42"), std::string::npos) << text;
    // No break: no body branches to a shared exit.
    EXPECT_EQ(text.find("break;"), std::string::npos) << text;
}
