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
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
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
    auto b0 = std::make_unique<Block>();
    b0->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
                                    std::make_unique<LdcI4>(0)));
    auto b1 = std::make_unique<Block>();

    auto fn = MakeFunction({});
    Block* b1Ptr = b1.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));

    auto br = std::make_unique<Branch>(static_cast<std::uint32_t>(0x20));
    br->TargetBlock = b1Ptr;
    br->HasOffset = false;
    fn->Body->Blocks[0]->SetFinal(std::make_unique<IfInstruction>(
        std::make_unique<Comp>(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(1),
                               ComparisonKind::Equality),
        std::move(br)));
    fn->Body->Blocks[1]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    if ((1 == 1)) goto IL_0020;\n"), std::string::npos) << text;
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
    EXPECT_NE(text.find("    this.count = 7;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    return this.count;\n"), std::string::npos) << text;
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

TEST(ILAstToCSharp, SwitchEmitsCaseLabelsWithGotos) {
    auto v0 = MakeVar(VariableKind::Local, "V_0", 0);
    auto b0 = std::make_unique<Block>();
    auto b1 = std::make_unique<Block>();
    auto b2 = std::make_unique<Block>();

    auto fn = MakeFunction({});
    Block* b1Ptr = b1.get();
    Block* b2Ptr = b2.get();
    fn->Body->AddBlock(std::move(b0));
    fn->Body->AddBlock(std::move(b1));
    fn->Body->AddBlock(std::move(b2));

    auto brCase = std::make_unique<Branch>(static_cast<std::uint32_t>(0x30));
    brCase->TargetBlock = b1Ptr;
    brCase->HasOffset = false;
    auto brDefault = std::make_unique<Branch>(static_cast<std::uint32_t>(0x40));
    brDefault->TargetBlock = b2Ptr;
    brDefault->HasOffset = false;

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(v0));
    auto caseSection = std::make_unique<SwitchSection>(std::set<std::int64_t>{0});
    caseSection->SetBody(std::move(brCase));
    sw->AddSection(std::move(caseSection));
    auto defaultSection = std::make_unique<SwitchSection>();
    defaultSection->SetBody(std::move(brDefault));
    sw->AddSection(std::move(defaultSection));
    fn->Body->Blocks[0]->SetFinal(std::move(sw));

    fn->Body->Blocks[1]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->Body->Blocks[2]->SetFinal(ReturnFinal(fn->Body.get()));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("    switch (V_0)\n"), std::string::npos) << text;
    EXPECT_NE(text.find("        case 0:\n"), std::string::npos) << text;
    EXPECT_NE(text.find("        goto IL_0030;\n"), std::string::npos) << text;
    EXPECT_NE(text.find("        default:\n"), std::string::npos) << text;
    EXPECT_NE(text.find("        goto IL_0040;\n"), std::string::npos) << text;
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
    EXPECT_NE(text.find("    var V_0 = (arg_1 as System.String);\n"), std::string::npos) << text;
    EXPECT_NE(text.find("    return (System.String)(V_0);\n"), std::string::npos) << text;
}

TEST(ILAstToCSharp, ArrayAndLengthExpressions) {
    auto arg1 = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto block = std::make_unique<Block>();
    // V_0 = arg_1.Length
    block->Add(std::make_unique<StLoc>(MakeVar(VariableKind::Local, "V_0", 0),
        std::make_unique<LdLen>(std::make_unique<LdLoc>(arg1))));
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
            std::make_unique<LdLen>(std::make_unique<LdLoc>(arg1)), StackType::I4)));
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
        EXPECT_NE(text.find("return this.m_firstChar;"), std::string::npos) << text;
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
