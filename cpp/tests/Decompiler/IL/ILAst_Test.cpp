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

// ILAst model foundation tests. Builds a small ILFunction tree by hand
// (stloc(v, ldc.i4(1)) + leave) and checks the strict-tree invariant
// (parent/child/index/connectedness/flags), the bottom-up Flags computation,
// the StackType mapping, and the WriteTo dump. This is the Phase 3 foundation;
// the IL reader that builds this tree from real method bodies comes next.

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

static ILVariablePtr MakeInt32Local(std::string name) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = VariableKind::Local;
    v->Type = std::make_shared<KnownType>(KnownTypeCode::Int32);
    v->Index = 0;
    return v;
}

TEST(ILAst, TreeInvariantAndFlags) {
    auto v = MakeInt32Local("x");

    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    auto* container = static_cast<BlockContainer*>(nullptr);

    auto containerOwner = std::make_unique<BlockContainer>();
    container = containerOwner.get();
    container->AddBlock(std::move(block));

    auto fn = std::make_unique<ILFunction>();
    fn->Variables.push_back(v);
    fn->Body = std::move(containerOwner);
    if (fn->Body) { fn->Body->Parent = fn.get(); fn->Body->ChildIndex = 0; }

    // The block's final is a leave of the function's body container.
    auto& b = *container->Blocks[0];
    b.SetFinal(std::make_unique<Leave>(container));

    // Invariant must hold across the whole tree.
    fn->CheckInvariant(ILPhase::Normal);

    // Connectedness: root and all descendants are connected.
    EXPECT_TRUE(fn->IsRoot());
    EXPECT_TRUE(fn->IsConnected());
    EXPECT_TRUE(fn->Body->IsConnected());
    EXPECT_TRUE(container->Blocks[0]->IsConnected());
    EXPECT_TRUE(b.Instructions[0]->IsConnected());
    EXPECT_TRUE(b.FinalInstruction->IsConnected());

    // Parent/ChildIndex wiring.
    EXPECT_EQ(fn->Body->Parent, fn.get());
    EXPECT_EQ(fn->Body->ChildIndex, 0);
    EXPECT_EQ(container->Blocks[0]->Parent, container);
    EXPECT_EQ(b.Instructions[0]->Parent, &b);
    EXPECT_EQ(b.Instructions[0]->ChildIndex, 0);
    EXPECT_EQ(b.FinalInstruction->Parent, &b);
    EXPECT_EQ(b.FinalInstruction->ChildIndex, 1);  // after the one instruction

    // Flags: StLoc(MayWriteLocals) over LdcI4(None) => MayWriteLocals.
    EXPECT_TRUE(HasFlag(b.Instructions[0]->Flags(), InstructionFlags::MayWriteLocals));
    // Leave(MayBranch|EndPointUnreachable).
    EXPECT_TRUE(HasFlag(b.FinalInstruction->Flags(), InstructionFlags::MayBranch));
    EXPECT_TRUE(HasFlag(b.FinalInstruction->Flags(), InstructionFlags::EndPointUnreachable));
    // Block flags union children: includes MayWriteLocals and MayBranch.
    EXPECT_TRUE(HasFlag(b.Flags(), InstructionFlags::MayWriteLocals));
    EXPECT_TRUE(HasFlag(b.Flags(), InstructionFlags::MayBranch));
}

TEST(ILAst, StackTypeMapping) {
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::Int32)), StackType::I4);
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::Int64)), StackType::I8);
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::Single)), StackType::F4);
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::Double)), StackType::F8);
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::IntPtr)), StackType::I);
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::Object)), StackType::O);
    EXPECT_EQ(StackTypeOf(std::make_shared<KnownType>(KnownTypeCode::Void)), StackType::Void);
    auto v = MakeInt32Local("y");
    auto ld = std::make_unique<LdLoc>(v);
    EXPECT_EQ(ld->ResultType(), StackType::I4);
}

TEST(ILAst, WriteToDump) {
    auto v = MakeInt32Local("x");
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1)));
    block->SetFinal(std::make_unique<Nop>());  // placeholder final just for the dump
    std::string out;
    block->WriteTo(out);
    EXPECT_NE(out.find("stloc(x, ldc.i4(1)"), std::string::npos) << out;
    EXPECT_NE(out.find("nop"), std::string::npos) << out;
}

TEST(ILAst, TakeChildOrphansTheChild) {
    // TakeChild removes a child from its slot, returning ownership and clearing
    // the child's Parent/ChildIndex so it can be re-planted elsewhere. The tree
    // invariant (a child has exactly one parent) is maintained by this ownership
    // transfer; SetChild additionally asserts a fresh child has no parent.
    auto v = MakeInt32Local("x");
    auto st = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1));
    ASSERT_EQ(st->ChildCount(), 1);
    auto* inner = st->Value.get();
    ASSERT_EQ(inner->Parent, st.get());
    auto taken = st->TakeChild(0);
    EXPECT_EQ(st->ChildCount(), 0);            // slot is now empty
    EXPECT_EQ(st->GetChild(0), nullptr);
    EXPECT_EQ(taken.get(), inner);
    EXPECT_EQ(taken->Parent, nullptr);         // orphaned
    EXPECT_EQ(taken->ChildIndex, -1);
    // The rest of the tree is still valid.
    st->CheckInvariant(ILPhase::Normal);
    // Re-planting the taken child into a fresh parent must succeed and re-wire.
    auto st2 = std::make_unique<StLoc>(v, std::move(taken));
    EXPECT_EQ(st2->Value->Parent, st2.get());
    EXPECT_EQ(st2->Value->ChildIndex, 0);
    st2->CheckInvariant(ILPhase::Normal);
}

TEST(ILAst, SetChildEnforcesTreeInvariant) {
    // SetChild rejects a child that already has a parent (debug-only assert).
    // A well-formed tree never produces this; it is a safety net against misuse.
#ifdef NDEBUG
    SUCCEED() << "assertions disabled in release; tree-invariant assert not exercised";
#else
    auto v = MakeInt32Local("x");
    auto st = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(1));
    auto inner = st->TakeChild(0);            // now orphaned
    auto st2 = std::make_unique<StLoc>(v, std::make_unique<LdcI4>(2));
    // Re-parent st2's child by handing the still-owned st2 value to SetChild on st:
    // first extract it the wrong way (release without orphaning) to simulate a
    // misuse, then expect SetChild to assert.
    std::unique_ptr<ILInstruction> stolen(st2->Value.release());  // stolen but Parent still set
    ASSERT_NE(stolen->Parent, nullptr);
    EXPECT_DEATH(st->SetChild(0, std::move(stolen)), ".*")
        << "SetChild should reject an already-parented child";
#endif
}

TEST(ILAst, CallHasSideEffectAndThrows) {
    Call c("System.Console::WriteLine");
    c.AddArg(std::make_unique<LdcI4>(42));
    EXPECT_TRUE(HasFlag(c.DirectFlags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(c.DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_EQ(c.ChildCount(), 1);
    EXPECT_EQ(c.GetChild(0)->Op, OpCode::LdcI4);
    c.CheckInvariant(ILPhase::Normal);
    std::string out;
    c.WriteTo(out);
    EXPECT_NE(out.find("call System.Console::WriteLine(ldc.i4(42))"), std::string::npos) << out;
}
