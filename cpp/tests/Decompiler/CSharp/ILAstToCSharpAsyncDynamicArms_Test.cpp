// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The async/dynamic/yield render arms of the ILAstToCSharp seed: the
// Await, YieldReturn, and dynamic-call nodes the state-machine and
// callsite transforms produce render as their C# forms.

#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Await.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/DynamicInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace CSharp = ::ILSpy::Decompiler::CSharp;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<::ILSpy::Decompiler::IL::ILVariable>;

namespace {

// The single-block body fixture: the block's instructions and the final are
// the caller's.
struct BodyFixture {
    std::unique_ptr<::ILSpy::Decompiler::IL::ILFunction> fn;
    ::ILSpy::Decompiler::IL::Block* block = nullptr;

    explicit BodyFixture(std::unique_ptr<::ILSpy::Decompiler::IL::ILInstruction> final) {
        fn = std::make_unique<::ILSpy::Decompiler::IL::ILFunction>();
        auto body = std::make_unique<::ILSpy::Decompiler::IL::BlockContainer>();
        fn->Body = std::move(body);
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        auto block = std::make_unique<::ILSpy::Decompiler::IL::Block>();
        block->Kind = ::ILSpy::Decompiler::IL::BlockKind::ControlFlow;
        this->block = block.get();
        fn->Body->AddBlock(std::move(block));
        if (final != nullptr)
            this->block->SetFinal(std::move(final));
    }

    ILVariablePtr MakeLocal(const char* name) {
        auto t = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
        return fn->RegisterVariable(::ILSpy::Decompiler::IL::VariableKind::Local, t,
                                    std::string(name));
    }

    std::string Render() {
        return CSharp::CSharpDecompiler::DecompileFunctionToString(
            *fn, "void", "M", "");
    }
};

} // namespace

// `yield return <value>;` renders the iterator's yielded statement.
TEST(ILAstToCSharpAsyncDynamicArmsTest, YieldReturnRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::YieldReturn>(
        std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(1)));
    std::string text = f.Render();
    EXPECT_NE(text.find("yield return 1;"), std::string::npos) << text;
}

// An await as the block's final expression statement renders `await
// <expr>;`.
TEST(ILAstToCSharpAsyncDynamicArmsTest, AwaitStatementRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto taskCall = std::make_unique<::ILSpy::Decompiler::IL::Call>(
        "System.Threading.Tasks.Task::Delay");
    taskCall->AddArg(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(1));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::Await>(std::move(taskCall)));
    std::string text = f.Render();
    EXPECT_NE(text.find("await System.Threading.Tasks.Task.Delay(1);"),
              std::string::npos)
        << text;
}

// An await as a value (stored into a local) renders the await expression.
TEST(ILAstToCSharpAsyncDynamicArmsTest, AwaitValueRenders) {
    BodyFixture f{nullptr};
    ILVariablePtr finalVar = f.MakeLocal("V_1");
    f.block->SetFinal(
        std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(finalVar));
    auto taskCall = std::make_unique<::ILSpy::Decompiler::IL::Call>(
        "System.Threading.Tasks.Task::Delay");
    taskCall->AddArg(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(1));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::StLoc>(
        f.MakeLocal("V_0"),
        std::make_unique<::ILSpy::Decompiler::IL::Await>(
            std::move(taskCall))));
    std::string text = f.Render();
    EXPECT_NE(text.find("await System.Threading.Tasks.Task.Delay(1)"),
              std::string::npos)
        << text;
    EXPECT_EQ(text.find("(default)"), std::string::npos) << text;
}

// The dynamic get-member renders `target.Name`.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicGetMemberRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        auto t = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(
            f.MakeLocal("V_0"));
    };
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicGetMemberInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, "Foo", nullptr,
        ::ILSpy::Decompiler::IL::CSharpArgumentInfo{}, std::move(makeLdLoc())));
    std::string text = f.Render();
    EXPECT_NE(text.find("num.Foo;"), std::string::npos) << text;
    EXPECT_EQ(text.find("(default)"), std::string::npos) << text;
}

// The dynamic invoke-member renders `target.Name(args)`.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicInvokeMemberRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    std::vector<std::unique_ptr<::ILSpy::Decompiler::IL::ILInstruction>> args;
    args.push_back(makeLdLoc());
    args.push_back(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(5));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicInvokeMemberInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, "Bar",
        std::vector<TS::ITypePtr>{}, nullptr,
        std::vector<::ILSpy::Decompiler::IL::CSharpArgumentInfo>(2), std::move(args)));
    std::string text = f.Render();
    EXPECT_NE(text.find("num.Bar(5);"), std::string::npos) << text;
}

// The dynamic invoke renders `target(args)`.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicInvokeRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    std::vector<std::unique_ptr<::ILSpy::Decompiler::IL::ILInstruction>> args;
    args.push_back(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(7));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicInvokeInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, nullptr,
        std::vector<::ILSpy::Decompiler::IL::CSharpArgumentInfo>(1), std::move(args)));
    std::string text = f.Render();
    EXPECT_NE(text.find("7();"), std::string::npos) << text;
}

// The dynamic get-index renders `target[args]`.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicGetIndexRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    std::vector<std::unique_ptr<::ILSpy::Decompiler::IL::ILInstruction>> args;
    args.push_back(makeLdLoc());
    args.push_back(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(2));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicGetIndexInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, nullptr,
        std::vector<::ILSpy::Decompiler::IL::CSharpArgumentInfo>(2), std::move(args)));
    std::string text = f.Render();
    EXPECT_NE(text.find("num[2];"), std::string::npos) << text;
}

// The dynamic invoke-constructor renders `new T(args)`.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicInvokeConstructorRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    std::vector<std::unique_ptr<::ILSpy::Decompiler::IL::ILInstruction>> args;
    args.push_back(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(9));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicInvokeConstructorInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None,
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String), nullptr,
        std::vector<::ILSpy::Decompiler::IL::CSharpArgumentInfo>(1), std::move(args)));
    std::string text = f.Render();
    EXPECT_NE(text.find("new string(9);"), std::string::npos) << text;
}

// The dynamic binary operator renders the C# operator symbol.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicBinaryOperatorRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicBinaryOperatorInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, ::ILSpy::Decompiler::IL::ExpressionType::Add, nullptr,
        ::ILSpy::Decompiler::IL::CSharpArgumentInfo{}, makeLdLoc(), ::ILSpy::Decompiler::IL::CSharpArgumentInfo{},
        std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(3)));
    std::string text = f.Render();
    EXPECT_NE(text.find("num + 3;"), std::string::npos) << text;
}

// The dynamic unary operator renders the prefix operator.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicUnaryOperatorRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicUnaryOperatorInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, ::ILSpy::Decompiler::IL::ExpressionType::Negate, nullptr,
        ::ILSpy::Decompiler::IL::CSharpArgumentInfo{}, makeLdLoc()));
    std::string text = f.Render();
    EXPECT_NE(text.find("-num;"), std::string::npos) << text;
}

// The dynamic set-member renders the assignment.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicSetMemberRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicSetMemberInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, "Foo", nullptr,
        ::ILSpy::Decompiler::IL::CSharpArgumentInfo{}, makeLdLoc(), ::ILSpy::Decompiler::IL::CSharpArgumentInfo{},
        std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(4)));
    std::string text = f.Render();
    EXPECT_NE(text.find("num.Foo = 4;"), std::string::npos) << text;
}

// The dynamic set-index renders the element assignment.
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicSetIndexRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    std::vector<std::unique_ptr<::ILSpy::Decompiler::IL::ILInstruction>> args;
    args.push_back(makeLdLoc());
    args.push_back(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(1));
    args.push_back(std::make_unique<::ILSpy::Decompiler::IL::LdcI4>(6));
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicSetIndexInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::None, nullptr,
        std::vector<::ILSpy::Decompiler::IL::CSharpArgumentInfo>(3), std::move(args)));
    std::string text = f.Render();
    EXPECT_NE(text.find("num[1, 6] = 6;"), std::string::npos) << text;
}

// The dynamic convert renders the cast (explicit) or the operand alone
// (implicit).
TEST(ILAstToCSharpAsyncDynamicArmsTest, DynamicConvertRenders) {
    BodyFixture f{std::make_unique<::ILSpy::Decompiler::IL::Nop>()};
    auto makeLdLoc = [&f]() {
        return std::make_unique<::ILSpy::Decompiler::IL::LdLoc>(f.MakeLocal("V_0"));
    };
    f.block->Add(std::make_unique<::ILSpy::Decompiler::IL::DynamicConvertInstruction>(
        ::ILSpy::Decompiler::IL::CSharpBinderFlags::ConvertExplicit,
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32), nullptr,
        makeLdLoc()));
    std::string text = f.Render();
    EXPECT_NE(text.find("(int)num;"), std::string::npos) << text;
}
