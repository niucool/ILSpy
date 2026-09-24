// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for NamedArgumentTransform (the port of
// ICSharpCode.Decompiler/IL/Transforms/NamedArgumentTransform.cs): the
// CanIntroduceNamedArgument gates, the IntroduceNamedArgument promotion (the
// CallWithNamedArgs block with the StLoc/LdLoc argument slots), and the
// InlineOneIfPossible integration (the blocked inline resolves through the
// named-argument introduction).

#include "Decompiler/IL/Transforms/NamedArgumentTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
using IL::ILVariablePtr;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
using IL::BlockKind;
using IL::InliningOptions;
using IL::OpCode;
using IL::StackType;
using ILSpy::Decompiler::TypeSystem::KnownType;

ILVariablePtr MakeLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

// The canonical named-argument shape (the C# InlineTesting pattern):
//   stloc v2(callvirt G(ldloc a))      -- the moved expression is IMPURE, so the
//                                         plain inline into arg0 is blocked by
//                                         the arg0 call's side effects
//   call M(callvirt G(ldloc a), ldloc v2)  -- the v2 load sits in the SECOND
//                                         argument slot; the promotion stores
//                                         that load's operand early and the
//                                         inliner moves the impure value there
// M is STATIC (an instance call's slot 0 is the this-pointer, which
// CanIntroduceNamedArgument refuses to promote).
struct NamedArgFixture {
    TS::SimpleCompilation compilation{
        TS::Implementation::MinimalCorlib::Instance(), {}};
    TS::ITypePtr int32Type =
        std::make_shared<KnownType>(TS::KnownTypeCode::Int32);
    std::shared_ptr<Impl::FakeMethod> method =
        std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
    ILVariablePtr a = MakeLocal("a", int32Type);
    ILVariablePtr v2 = MakeLocal("v2", int32Type);
    ILVariablePtr v2Alternates = MakeLocal("v2", int32Type);

    NamedArgFixture()
    {
        method->SetName("M");
        method->SetDeclaringType(int32Type);
        method->SetParameters(
            {std::make_shared<const Impl::DefaultParameter>(int32Type, "first"),
             std::make_shared<const Impl::DefaultParameter>(int32Type, "second")});
    }

    // The canonical named-argument shape (the C# InlineTesting pattern): the
    // stloc's value is the PURE `ldc.i4 42`; the call's first argument is an
    // impure call (`Impure()`) whose subtree stops the FindLoadInNext walk
    // before it reaches the v2 load in the second slot, so the promotion is
    // required. M is STATIC (an instance call's slot 0 is the this-pointer,
    // which CanIntroduceNamedArgument refuses to promote).
    std::unique_ptr<IL::ILFunction> MakeFunction()
    {
        auto fn = std::make_unique<IL::ILFunction>();
        fn->Body = std::make_unique<IL::BlockContainer>();
        fn->Body->Kind = IL::ContainerKind::Normal;
        fn->Body->Parent = fn.get();
        auto block = std::make_unique<IL::Block>();
        block->Kind = BlockKind::ControlFlow;
        auto compute = std::make_unique<IL::Call>("Compute");
        compute->IsInstanceCall = false;
        compute->ReturnIType = int32Type;
        block->Add(std::make_unique<IL::StLoc>(v2, std::move(compute)));
        auto call = std::make_unique<IL::Call>(method);
        call->IsInstanceCall = false;
        call->ReturnIType = int32Type;
        auto impure = std::make_unique<IL::Call>("Impure");
        impure->IsInstanceCall = false;
        impure->ReturnIType = int32Type;
        call->AddArg(std::move(impure));
        call->AddArg(std::make_unique<IL::LdLoc>(v2));
        block->Add(std::move(call));
        block->SetFinal(std::make_unique<IL::Nop>());
        fn->Body->AddBlock(std::move(block));
        fn->Variables.push_back(a);
        fn->Variables.push_back(v2);
        return fn;
    }
};

// Drive the pipeline the way the port's GetILTransforms does: the ILInlining
// statement child (which computes the usage counts and runs the per-statement
// inlining) followed by NamedArgumentTransform.
void RunTransforms(IL::ILFunction& fn, TS::ICompilation& compilation)
{
    IL::StatementTransform st;
    st.AddChild(std::make_unique<IL::ILInlining>());
    st.AddChild(std::make_unique<IL::NamedArgumentTransform>());
    IL::ILTransformContext ctx;
    ctx.TypeSystem = &compilation;
    ctx.Settings.NamedArguments = true;
    st.Run(fn, ctx);
}

// The blocked inline (arg0's impure call stops the walk) resolves through the
// named-argument introduction: the promoted CallWithNamedArgs block stores the
// arg1 value (the moved expression) into a NamedArgument variable and the
// call's slot loads it.
TEST(NamedArgumentTransformTest, BlockedInlineIntroducesNamedArgument)
{
    NamedArgFixture f;
    auto fn = f.MakeFunction();
    RunTransforms(*fn, f.compilation);

    IL::Block* block = fn->Body->Blocks[0].get();
    // The promotion wraps the call in a nested CallWithNamedArgs block (the
    // C# `call.ReplaceWith(namedArgBlock)` shape).
    ASSERT_EQ(block->Instructions.size(), 1u);
    const auto* nested =
        dynamic_cast<const IL::Block*>(block->Instructions[0].get());
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(nested->Kind, BlockKind::CallWithNamedArgs);
    ASSERT_GE(nested->Instructions.size(), 1u);
    // The promoted store: stloc vNamed(<the moved Compute call>) -- the FIRST
    // instruction of the promoted block is a StLoc into a NamedArgument
    // variable.
    const auto* promotedStore =
        dynamic_cast<const IL::StLoc*>(nested->Instructions[0].get());
    ASSERT_NE(promotedStore, nullptr);
    EXPECT_EQ(promotedStore->Variable->Kind, IL::VariableKind::NamedArgument);
    ASSERT_EQ(promotedStore->Value->Op, OpCode::Call)
        << "the moved impure expression is inlined into the promoted slot";
    // The call's second argument is now a load of the promoted variable.
    const auto* finalCall =
        dynamic_cast<const IL::Call*>(nested->FinalInstruction.get());
    ASSERT_NE(finalCall, nullptr);
    ASSERT_EQ(finalCall->Arguments.size(), 2u);
    EXPECT_EQ(finalCall->Arguments[1]->Op, OpCode::LdLoc);
    const auto* arg1 = static_cast<const IL::LdLoc*>(finalCall->Arguments[1].get());
    EXPECT_EQ(arg1->Variable->Kind, IL::VariableKind::NamedArgument);
    EXPECT_EQ(arg1->Variable.get(), promotedStore->Variable.get());
}

// The CanIntroduceNamedArgument gates: an instance call's slot-0 promotion is
// refused (the this-pointer slot), so the promotion does not fire and the
// stloc stays.
TEST(NamedArgumentTransformTest, InstanceCallThisPointerSlotIsRefused)
{
    NamedArgFixture f;
    auto fn = f.MakeFunction();
    IL::Block* block = fn->Body->Blocks[0].get();
    // Rewrite the call to an instance call with the v2 load in slot 0 (the
    // this-pointer slot of an instance call).
    auto* theCall = dynamic_cast<IL::Call*>(block->Instructions[1].get());
    ASSERT_NE(theCall, nullptr);
    theCall->IsInstanceCall = true;
    theCall->Arguments[0] = std::make_unique<IL::LdLoc>(f.v2);
    theCall->Arguments[1] = std::make_unique<IL::LdLoc>(f.a);

    IL::ILTransformContext ctx;
    ctx.TypeSystem = &f.compilation;
    ctx.Settings.NamedArguments = true;
    IL::NamedArgumentTransform transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);
    EXPECT_EQ(block->Instructions[0]->Op, OpCode::StLoc)
        << "the instance-call slot-0 promotion must be refused";
}

} // namespace
