// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

// Tests for NamedArgumentTransform (the named-argument promotion per-statement
// child of the StatementTransform) and the ILInlining named-argument search it
// composes. The hand-built trees pin the can-introduce / can-extend guards and
// the exact promoted CallWithNamedArgs block shape; a mscorlib sweep is not used
// because the transform is not wired into GetILTransforms() until the
// CallWithNamedArgs block render lands in CallBuilder.

#include "Decompiler/IL/Transforms/NamedArgumentTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {
namespace {

using TypeSystem::ITypePtr;
using TypeSystem::KnownType;
using TypeSystem::KnownTypeCode;
using TypeSystem::SymbolKind;

namespace Impl = TypeSystem::Implementation;
namespace TS = TypeSystem;

struct Fixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<Impl::FakeMethod> targetMethod;

    Fixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {
        targetMethod = MakeMethod("Target", SymbolKind::Method, {"a", "b", "c"}, false);
    }

    ITypePtr Known(KnownTypeCode code) {
        return std::make_shared<KnownType>(code);
    }

    std::shared_ptr<Impl::FakeMethod> MakeMethod(
        const char* name, SymbolKind kind, std::vector<std::string> parameterNames,
        bool isStatic) {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, kind);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        method->SetDeclaringType(Known(KnownTypeCode::Object));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (const std::string& pn : parameterNames) {
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                Known(KnownTypeCode::Int32), pn));
        }
        method->SetParameters(parameters);
        method->SetReturnType(Known(KnownTypeCode::Void));
        return method;
    }
};

ILVariablePtr MakeVar(VariableKind kind, std::string name, std::int32_t index,
                      ITypePtr type) {
    auto v = std::make_shared<ILVariable>(kind, std::move(type), index);
    v->Name = std::move(name);
    return v;
}

// An impure expression (a void call) that cannot be re-ordered past another
// impure expression.
std::unique_ptr<Call> MakeVoidCall(const char* name) {
    auto call = std::make_unique<Call>(name);
    call->IsInstanceCall = false;
    call->IsNewObj = false;
    call->ReturnType = StackType::Void;
    return call;
}

// A function with one block. The caller populates `instructions` and the final.
std::unique_ptr<ILFunction> MakeFn() {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    return fn;
}

Block* OnlyBlock(ILFunction* fn) { return fn->Body->Blocks[0].get(); }

// The canonical shape: `stloc v(impure()); call Target(impure(), ldloc v)`.
// `v` cannot be re-ordered past the first argument, so it is promoted to a named
// argument.
struct NamedArgScenario {
    Fixture fixture;
    std::unique_ptr<ILFunction> fn;
    ILVariablePtr v;

    NamedArgScenario(bool instanceCall = false) {
        v = MakeVar(VariableKind::Local, "V_0", 0, fixture.Known(KnownTypeCode::Int32));
        fn = MakeFn();
        Block* block = OnlyBlock(fn.get());
        block->Add(std::make_unique<StLoc>(v, MakeVoidCall("Moved")));

        auto target = std::make_unique<Call>("Target");
        target->Method = fixture.targetMethod;
        target->IsInstanceCall = instanceCall;
        target->IsNewObj = false;
        target->ReturnType = StackType::Void;
        if (instanceCall) {
            target->AddArg(std::make_unique<LdLoc>(
                MakeVar(VariableKind::Local, "thisPtr", 1,
                        fixture.Known(KnownTypeCode::Object))));
        }
        target->AddArg(MakeVoidCall("ArgWithSideEffect"));
        target->AddArg(std::make_unique<LdLoc>(v));
        block->SetFinal(std::move(target));

        fn->Variables.push_back(v);
        ComputeVariableUsage(*fn);
    }
};

TEST(NamedArgumentTransform, IntroducesNamedArgumentForStaticCall) {
    NamedArgScenario s;
    ILTransformContext ctx;
    Block* block = OnlyBlock(s.fn.get());
    StatementTransformContext stctx(ctx, block);
    NamedArgumentTransform().Run(*block, 0, stctx);

    // The original stloc was removed; the block's final is now the named-argument block.
    EXPECT_TRUE(block->Instructions.empty());
    auto* namedArgs = dynamic_cast<Block*>(block->FinalInstruction.get());
    ASSERT_NE(namedArgs, nullptr);
    EXPECT_EQ(namedArgs->Kind, BlockKind::CallWithNamedArgs);
    // One promoted argument stloc, holding the moved expression.
    ASSERT_EQ(namedArgs->Instructions.size(), 1u);
    auto* promoted = dynamic_cast<StLoc*>(namedArgs->Instructions[0].get());
    ASSERT_NE(promoted, nullptr);
    EXPECT_NE(promoted->Variable.get(), s.v.get());
    EXPECT_NE(promoted->Value.get(), nullptr);
    EXPECT_EQ(promoted->Value->Op, OpCode::Call);
    EXPECT_EQ(static_cast<Call*>(promoted->Value.get())->MethodName, "Moved");
    // The call's second argument now loads the named-argument variable.
    auto* call = dynamic_cast<Call*>(namedArgs->FinalInstruction.get());
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->Arguments.size(), 2u);
    EXPECT_EQ(call->Arguments[0]->Op, OpCode::Call);
    ASSERT_EQ(call->Arguments[1]->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(call->Arguments[1].get())->Variable.get(),
              promoted->Variable.get());
    s.fn->CheckInvariant(ILPhase::Normal);
}

TEST(NamedArgumentTransform, IntroducesThisArgumentForInstanceCall) {
    NamedArgScenario s(/*instanceCall=*/true);
    ILTransformContext ctx;
    Block* block = OnlyBlock(s.fn.get());
    StatementTransformContext stctx(ctx, block);
    NamedArgumentTransform().Run(*block, 0, stctx);

    ASSERT_TRUE(block->Instructions.empty());
    auto* namedArgs = dynamic_cast<Block*>(block->FinalInstruction.get());
    ASSERT_NE(namedArgs, nullptr);
    EXPECT_EQ(namedArgs->Kind, BlockKind::CallWithNamedArgs);
    // The this pointer stloc first, then the promoted argument stloc.
    ASSERT_EQ(namedArgs->Instructions.size(), 2u);
    auto* thisStloc = dynamic_cast<StLoc*>(namedArgs->Instructions[0].get());
    ASSERT_NE(thisStloc, nullptr);
    EXPECT_EQ(thisStloc->Variable->Name, "this_arg");
    ASSERT_NE(thisStloc->Value.get(), nullptr);
    EXPECT_EQ(thisStloc->Value->Op, OpCode::LdLoc);
    auto* promoted = dynamic_cast<StLoc*>(namedArgs->Instructions[1].get());
    ASSERT_NE(promoted, nullptr);
    EXPECT_EQ(promoted->Value->Op, OpCode::Call);
    auto* call = dynamic_cast<Call*>(namedArgs->FinalInstruction.get());
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->Arguments.size(), 3u);
    ASSERT_EQ(call->Arguments[0]->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(call->Arguments[0].get())->Variable.get(),
              thisStloc->Variable.get());
    EXPECT_EQ(static_cast<LdLoc*>(call->Arguments[2].get())->Variable.get(),
              promoted->Variable.get());
    s.fn->CheckInvariant(ILPhase::Normal);
}

TEST(NamedArgumentTransform, SettingOffKeepsOriginal) {
    NamedArgScenario s;
    ILTransformContext ctx;
    ctx.Settings.NamedArguments = false;
    Block* block = OnlyBlock(s.fn.get());
    StatementTransformContext stctx(ctx, block);
    NamedArgumentTransform().Run(*block, 0, stctx);

    EXPECT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(block->FinalInstruction->Op, OpCode::Call);
}

TEST(NamedArgumentTransform, ExtendsExistingNamedArgumentBlock) {
    // A block already wrapped: `stloc v(impure()); block { stloc w(0); call Target(impure(), ldloc v) }`
    NamedArgScenario s;
    Block* block = OnlyBlock(s.fn.get());
    auto* call = dynamic_cast<Call*>(block->FinalInstruction.get());
    ASSERT_NE(call, nullptr);
    auto wrapper = std::make_unique<Block>();
    wrapper->Kind = BlockKind::CallWithNamedArgs;
    auto w = MakeVar(VariableKind::Local, "W", 2, s.fixture.Known(KnownTypeCode::Int32));
    wrapper->Add(std::make_unique<StLoc>(w, std::make_unique<LdcI4>(0)));
    auto callOwned = block->TakeChild(1);
    wrapper->SetFinal(std::move(callOwned));
    block->SetChild(1, std::move(wrapper));
    ComputeVariableUsage(*s.fn);

    ILTransformContext ctx;
    StatementTransformContext stctx(ctx, block);
    NamedArgumentTransform().Run(*block, 0, stctx);

    auto* namedArgs = dynamic_cast<Block*>(block->FinalInstruction.get());
    ASSERT_NE(namedArgs, nullptr);
    EXPECT_EQ(namedArgs->Kind, BlockKind::CallWithNamedArgs);
    // The pre-existing stloc w plus the newly promoted argument stloc.
    ASSERT_EQ(namedArgs->Instructions.size(), 2u);
    EXPECT_EQ(namedArgs->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(namedArgs->Instructions[1]->Op, OpCode::StLoc);
    s.fn->CheckInvariant(ILPhase::Normal);
}

TEST(NamedArgumentTransform, OperatorMethodRejected) {
    Fixture fixture;
    auto op = fixture.MakeMethod("op_Addition", SymbolKind::Operator, {"a", "b"}, true);
    auto v = MakeVar(VariableKind::Local, "V_0", 0, fixture.Known(KnownTypeCode::Int32));
    auto call = std::make_unique<Call>("op_Addition");
    call->Method = op;
    call->AddArg(MakeVoidCall("Arg0"));
    call->AddArg(std::make_unique<LdLoc>(v));
    auto expr = MakeVoidCall("Moved");
    FindResult r = NamedArgumentTransform::CanIntroduceNamedArgument(
        call.get(), call->Arguments[0].get(), v.get(), expr.get());
    EXPECT_EQ(r.type, FindResultType::Stop);
}

TEST(NamedArgumentTransform, ConstructorWithDelegateDeclaringTypeRejected) {
    Fixture fixture;
    auto ctor = fixture.MakeMethod(".ctor", SymbolKind::Constructor, {"a", "b"}, true);
    ctor->SetDeclaringType(std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("System", "MyDelegate"), TS::TypeKind::Delegate));
    auto v = MakeVar(VariableKind::Local, "V_0", 0, fixture.Known(KnownTypeCode::Int32));
    auto call = std::make_unique<Call>(".ctor");
    call->Method = ctor;
    call->AddArg(MakeVoidCall("Arg0"));
    call->AddArg(std::make_unique<LdLoc>(v));
    auto expr = MakeVoidCall("Moved");
    FindResult r = NamedArgumentTransform::CanIntroduceNamedArgument(
        call.get(), call->Arguments[0].get(), v.get(), expr.get());
    EXPECT_EQ(r.type, FindResultType::Stop);
}

TEST(NamedArgumentTransform, EmptyParameterNameRejected) {
    Fixture fixture;
    auto method = fixture.MakeMethod("M", SymbolKind::Method, {"a", ""}, true);
    auto v = MakeVar(VariableKind::Local, "V_0", 0, fixture.Known(KnownTypeCode::Int32));
    auto call = std::make_unique<Call>("M");
    call->Method = method;
    call->AddArg(MakeVoidCall("Arg0"));
    call->AddArg(std::make_unique<LdLoc>(v));
    auto expr = MakeVoidCall("Moved");
    FindResult r = NamedArgumentTransform::CanIntroduceNamedArgument(
        call.get(), call->Arguments[0].get(), v.get(), expr.get());
    EXPECT_EQ(r.type, FindResultType::Stop);
}

TEST(NamedArgumentTransform, InstanceThisArgumentNotPromoted) {
    Fixture fixture;
    auto v = MakeVar(VariableKind::Local, "V_0", 0, fixture.Known(KnownTypeCode::Int32));
    auto call = std::make_unique<Call>("Target");
    call->Method = fixture.targetMethod;
    call->IsInstanceCall = true;
    auto thisPtr = MakeVar(VariableKind::Local, "thisPtr", 1,
                           fixture.Known(KnownTypeCode::Object));
    call->AddArg(std::make_unique<LdLoc>(thisPtr));
    call->AddArg(std::make_unique<LdLoc>(v));
    auto expr = MakeVoidCall("Moved");
    FindResult r = NamedArgumentTransform::CanIntroduceNamedArgument(
        call.get(), call->Arguments[0].get(), v.get(), expr.get());
    EXPECT_EQ(r.type, FindResultType::Stop);
}

TEST(NamedArgumentTransform, NoLaterLoadStops) {
    Fixture fixture;
    auto w = MakeVar(VariableKind::Local, "W", 0, fixture.Known(KnownTypeCode::Int32));
    auto other = MakeVar(VariableKind::Local, "Z", 1, fixture.Known(KnownTypeCode::Int32));
    auto call = std::make_unique<Call>("Target");
    call->Method = fixture.targetMethod;
    call->AddArg(MakeVoidCall("Arg0"));
    call->AddArg(std::make_unique<LdLoc>(other));
    auto expr = MakeVoidCall("Moved");
    FindResult r = NamedArgumentTransform::CanIntroduceNamedArgument(
        call.get(), call->Arguments[0].get(), w.get(), expr.get());
    EXPECT_EQ(r.type, FindResultType::Stop);
}

TEST(NamedArgumentTransform, FindLoadInNextReportsNamedArgument) {
    NamedArgScenario s;
    Block* block = OnlyBlock(s.fn.get());
    auto* call = dynamic_cast<Call*>(block->FinalInstruction.get());
    ASSERT_NE(call, nullptr);
    auto* moved = dynamic_cast<StLoc*>(block->Instructions[0].get());
    ASSERT_NE(moved, nullptr);
    auto value = moved->TakeChild(0);
    FindResult r = FindLoadInNext(call, s.v.get(), value.get(),
                                  InliningOptions::IntroduceNamedArguments);
    EXPECT_EQ(r.type, FindResultType::NamedArgument);
    ASSERT_NE(r.loadInst, nullptr);
    EXPECT_EQ(r.loadInst->Op, OpCode::LdLoc);
    EXPECT_EQ(r.callArgument, call->Arguments[1].get());
    moved->SetChild(0, std::move(value));
}

} // namespace
} // namespace ILSpy::Decompiler::IL
