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

// Exception-handler ILAst node tests. Builds TryCatch/TryCatchHandler/
// TryFinally/TryFault trees by hand and checks the tree invariant, DirectFlags
// (ControlFlow for the try nodes, ControlFlow|MayWriteLocals for handlers),
// result Void, and the WriteTo dump. The full EH reader integration (the
// BlockBuilder that nests blocks into try/handler containers from the EH
// region table) is a separate slice; these nodes are what it will emit.

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

static ILVariablePtr CatchVar() {
    auto v = std::make_shared<ILVariable>();
    v->Name = "ex";
    v->Kind = VariableKind::ExceptionLocal;
    v->Type = std::make_shared<KnownType>(KnownTypeCode::Exception);
    return v;
}

TEST(EHNodes, TryCatchTreeAndFlags) {
    // try { throw ldnull } catch (System.Exception ex) { leave }
    auto tryBlock = std::make_unique<Block>();
    tryBlock->SetFinal(std::make_unique<Throw>(std::make_unique<LdNull>()));

    auto handlerBody = std::make_unique<Block>();
    handlerBody->SetFinal(std::make_unique<Leave>(nullptr));

    auto tc = std::make_unique<TryCatch>(std::move(tryBlock));
    tc->AddHandler(std::make_unique<TryCatchHandler>(nullptr, std::move(handlerBody), CatchVar()));

    EXPECT_EQ(tc->DirectFlags(), InstructionFlags::ControlFlow);
    EXPECT_EQ(tc->ResultType(), StackType::Void);
    EXPECT_EQ(tc->ChildCount(), 2);  // TryBlock + 1 handler
    EXPECT_EQ(tc->GetChild(0)->Op, OpCode::Block);
    EXPECT_EQ(tc->GetChild(1)->Op, OpCode::TryCatchHandler);
    auto* h = static_cast<TryCatchHandler*>(tc->GetChild(1));
    EXPECT_TRUE(HasFlag(h->DirectFlags(), InstructionFlags::ControlFlow));
    EXPECT_TRUE(HasFlag(h->DirectFlags(), InstructionFlags::MayWriteLocals));
    EXPECT_EQ(h->Variable->Name, "ex");
    EXPECT_EQ(h->ChildCount(), 1);  // Body only (no Filter)

    // Wire into a function and check the invariant.
    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto* containerPtr = container.get();
    auto outer = std::make_unique<Block>();
    outer->SetFinal(std::move(tc));
    container->AddBlock(std::move(outer));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->CheckInvariant(ILPhase::Normal);

    std::string dump = fn->ToString();
    EXPECT_NE(dump.find("try {"), std::string::npos) << dump;
    EXPECT_NE(dump.find("} catch System.Exception ex {"), std::string::npos) << dump;
    EXPECT_NE(dump.find("throw ldnull"), std::string::npos) << dump;
}

TEST(EHNodes, TryFinallyTree) {
    auto tryBlock = std::make_unique<Block>();
    tryBlock->SetFinal(std::make_unique<Leave>(nullptr));
    auto finallyBlock = std::make_unique<Block>();
    finallyBlock->SetFinal(std::make_unique<Leave>(nullptr));
    auto tf = std::make_unique<TryFinally>(std::move(tryBlock), std::move(finallyBlock));
    EXPECT_EQ(tf->DirectFlags(), InstructionFlags::ControlFlow);
    EXPECT_EQ(tf->ResultType(), StackType::Void);
    EXPECT_EQ(tf->ChildCount(), 2);
    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto outer = std::make_unique<Block>();
    outer->SetFinal(std::move(tf));
    container->AddBlock(std::move(outer));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_NE(fn->ToString().find("} finally {"), std::string::npos);
}

TEST(EHNodes, TryFaultTree) {
    auto tryBlock = std::make_unique<Block>();
    tryBlock->SetFinal(std::make_unique<Leave>(nullptr));
    auto faultBlock = std::make_unique<Block>();
    faultBlock->SetFinal(std::make_unique<Leave>(nullptr));
    auto tflt = std::make_unique<TryFault>(std::move(tryBlock), std::move(faultBlock));
    EXPECT_EQ(tflt->Op, OpCode::TryFault);
    EXPECT_EQ(tflt->ChildCount(), 2);
    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto outer = std::make_unique<Block>();
    outer->SetFinal(std::move(tflt));
    container->AddBlock(std::move(outer));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_NE(fn->ToString().find("} fault {"), std::string::npos);
}

TEST(EHNodes, CatchHandlerWithFilter) {
    // A filter catch: Filter (Block returning I4) + Body.
    auto filterBlock = std::make_unique<Block>();
    filterBlock->SetFinal(std::make_unique<Leave>(nullptr));  // placeholder
    auto bodyBlock = std::make_unique<Block>();
    bodyBlock->SetFinal(std::make_unique<Leave>(nullptr));
    auto h = std::make_unique<TryCatchHandler>(std::move(filterBlock), std::move(bodyBlock), CatchVar());
    EXPECT_EQ(h->ChildCount(), 2);  // Filter + Body
    EXPECT_EQ(h->GetChild(0)->Op, OpCode::Block);
    EXPECT_EQ(h->GetChild(1)->Op, OpCode::Block);
    h->CheckInvariant(ILPhase::Normal);
}
