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

// ILAst instruction-batch tests. Exercises the Simple/Unary/Binary bases and the
// concrete instructions (LdStr, LdNull, CastClass, IsInst, Box, UnboxAny, Throw,
// LdLen, Conv, Comp, BinaryNumericInstruction): tree invariant, DirectFlags,
// ResultType, and WriteTo. Grows the model toward what the IL reader needs.

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

static ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
static ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
static ILVariablePtr Local(std::string name, ITypePtr t) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = VariableKind::Local;
    v->Type = std::move(t);
    return v;
}

TEST(ILAstInstructions, SimpleInstructions) {
    LdStr s("hello");
    EXPECT_EQ(s.Op, OpCode::LdStr);
    EXPECT_EQ(s.ResultType(), StackType::O);
    EXPECT_EQ(s.ChildCount(), 0);
    EXPECT_EQ(s.ToString(), "ldstr \"hello\"");

    LdNull n;
    EXPECT_EQ(n.Op, OpCode::LdNull);
    EXPECT_EQ(n.ResultType(), StackType::O);
    EXPECT_EQ(n.ToString(), "ldnull");
}

TEST(ILAstInstructions, UnaryInstructionsFlagsAndResult) {
    auto obj = Object();
    auto ldc = std::make_unique<LdcI4>(0);  // from LdcI4.hpp
    // CastClass: MayThrow; result O.
    CastClass cc(obj, std::make_unique<LdcI4>(0));
    EXPECT_TRUE(HasFlag(cc.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_EQ(cc.ResultType(), StackType::O);
    EXPECT_EQ(cc.ChildCount(), 1);
    cc.CheckInvariant(ILPhase::Normal);

    IsInst ii(obj, std::make_unique<LdcI4>(0));
    EXPECT_FALSE(HasFlag(ii.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_EQ(ii.ResultType(), StackType::O);

    Box b(obj, std::make_unique<LdcI4>(0));
    EXPECT_EQ(b.ResultType(), StackType::O);

    UnboxAny u(Int32(), std::make_unique<LdcI4>(0));
    EXPECT_TRUE(HasFlag(u.DirectFlags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(u.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_EQ(u.ResultType(), StackType::I4);

    Throw th(std::make_unique<LdNull>());
    EXPECT_TRUE(HasFlag(th.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_TRUE(HasFlag(th.DirectFlags(), InstructionFlags::EndPointUnreachable));
    EXPECT_EQ(th.ResultType(), StackType::Void);

    // LdLen carries a StackType resultType (I for the raw `ldlen` opcode, I4
    // for the synthetic `ldlen.i4`, I8 for `ldlen.i8`); construct each variant
    // and verify the ResultType + the dump suffix (faithful to the C#
    // WriteToCore `ldlen.<resultType>(array)`).
    LdLen len(StackType::I4, std::make_unique<LdNull>());
    EXPECT_TRUE(HasFlag(len.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_EQ(len.ResultType(), StackType::I4);
    EXPECT_NE(len.ToString().find("ldlen.I4("), std::string::npos) << len.ToString();

    LdLen lenI(StackType::I, std::make_unique<LdNull>());
    EXPECT_EQ(lenI.ResultType(), StackType::I);
    EXPECT_NE(lenI.ToString().find("ldlen.I("), std::string::npos) << lenI.ToString();

    LdLen lenI8(StackType::I8, std::make_unique<LdNull>());
    EXPECT_EQ(lenI8.ResultType(), StackType::I8);
    EXPECT_NE(lenI8.ToString().find("ldlen.I8("), std::string::npos) << lenI8.ToString();

    Conv conv(std::make_unique<LdcI4>(1), PrimitiveType::I8, false, Sign::None);
    EXPECT_EQ(conv.ResultType(), StackType::I8);
}

TEST(ILAstInstructions, BinaryInstructions) {
    Comp c(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2), ComparisonKind::Equality);
    EXPECT_EQ(c.ResultType(), StackType::I4);
    EXPECT_EQ(c.ChildCount(), 2);
    c.CheckInvariant(ILPhase::Normal);
    EXPECT_NE(c.ToString().find("comp(eq"), std::string::npos);

    BinaryNumericInstruction add(std::make_unique<LdcI4>(1), std::make_unique<LdcI4>(2),
                                  BinaryNumericOperator::Add, StackType::I4);
    EXPECT_EQ(add.ResultType(), StackType::I4);
    EXPECT_EQ(add.ChildCount(), 2);
    add.CheckInvariant(ILPhase::Normal);
    EXPECT_NE(add.ToString().find("binary.add"), std::string::npos);
    // Flags: a binary numeric op has no direct flags (its operands may).
    EXPECT_FALSE(HasFlag(add.DirectFlags(), InstructionFlags::SideEffect));
}

TEST(ILAstInstructions, NestedTreeInvariant) {
    // stloc(x, unbox.any(int32, castclass(object, ldc.i4(0)))) inside a block.
    auto v = Local("x", Int32());
    auto inner = std::make_unique<UnboxAny>(Int32(),
        std::make_unique<CastClass>(Object(), std::make_unique<LdcI4>(0)));
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v, std::move(inner)));
    block->SetFinal(std::make_unique<Throw>(std::make_unique<LdNull>()));

    auto container = std::make_unique<BlockContainer>();
    auto* containerPtr = container.get();
    container->AddBlock(std::move(block));

    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    // The block's final throw leaves the function body container.
    auto& b = *containerPtr->Blocks[0];
    static_cast<Throw&>(*b.FinalInstruction);  // sanity: it's a Throw

    fn->CheckInvariant(ILPhase::Normal);

    // Flags propagate: the block carries MayThrow (from CastClass/UnboxAny/Throw)
    // and SideEffect (from UnboxAny).
    EXPECT_TRUE(HasFlag(b.Flags(), InstructionFlags::MayThrow));
    EXPECT_TRUE(HasFlag(b.Flags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(fn->Flags(), InstructionFlags::MayThrow));

    // Dump contains the nested structure.
    std::string out = fn->ToString();
    EXPECT_NE(out.find("stloc(x, unbox.any(System.Int32, castclass(System.Object, ldc.i4(0)))"), std::string::npos)
        << out;
    EXPECT_NE(out.find("throw ldnull"), std::string::npos) << out;
}


// HasCycle: the cycle-detection guard for the --ilast dump path. WriteTo is a
// recursive, unbounded walker; a Parent-pointer cycle (a transform bug) would
// make it emit a repeated token ad infinitum and OOM. HasCycle detects the
// cycle generically (via ChildCount/GetChild + a visited-set) so the CLI can
// emit a marker instead of the runaway.
TEST(ILAstInstructions, HasCycleIsFalseForStrictTree) {
    // A valid strict tree: stloc(unbox.any(castclass(ldc.i4(0)))) -- no cycle.
    auto st = std::make_unique<StLoc>(
        std::make_shared<ILVariable>(),
        std::make_unique<UnboxAny>(
            std::make_shared<KnownType>(KnownTypeCode::Int32),
            std::make_unique<CastClass>(
                std::make_shared<KnownType>(KnownTypeCode::Object),
                std::make_unique<LdcI4>(0))));
    EXPECT_FALSE(st->HasCycle());
}

TEST(ILAstInstructions, HasCycleDetectsParentCycle) {
    // Build a 2-node cycle by hand: an IfInstruction (A) whose TrueInst is a
    // Block (B), and B's FinalInstruction is A again (A -> B -> A). The strict-
    // tree invariant forbids this (A would be its own descendant), but a
    // transform bug could produce it; HasCycle must catch it. The cycle is a
    // double-free under normal unique_ptr ownership (A owns B via TrueInst, B
    // owns A via FinalInstruction), so we LEAK the whole cycle: allocate A and
    // B with new, wire the cross-references, run HasCycle, then release every
    // owning slot so no destructor runs. The intentional leak is acceptable in
    // a test (the process exits immediately after).
    auto* aPtr = new IfInstruction(std::make_unique<LdcI4>(1), nullptr, nullptr);
    auto* bPtr = new Block();
    // A.TrueInst = B (A owns B).
    aPtr->TrueInst = std::unique_ptr<ILInstruction>(bPtr);
    bPtr->Parent = aPtr;
    bPtr->ChildIndex = 1;
    // B.FinalInstruction = A (B owns A -- the cycle). A's existing Condition
    // (a leaf LdcI4(1)) is destroyed when we... no: A is held by B's Final now,
    // and A still owns its Condition. The cycle is A->B->A.
    bPtr->SetFinal(std::unique_ptr<ILInstruction>(aPtr));
    aPtr->Parent = bPtr;  // A's parent is now B (the cycle's back edge)
    aPtr->ChildIndex = static_cast<int>(bPtr->Instructions.size());  // the final slot

    EXPECT_TRUE(aPtr->HasCycle())
        << "A->B->A must be detected as a cycle";

    // Leak the cycle: release every owning slot so no destructor runs (the
    // cross-ownership would double-free otherwise). The nodes live until the
    // process exits.
    aPtr->Condition.release();
    aPtr->TrueInst.release();
    bPtr->FinalInstruction.release();
}
