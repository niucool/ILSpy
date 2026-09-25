// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Tests for the AsyncAwaitDecompiler (the port of
// ICSharpCode.Decompiler/IL/ControlFlow/AsyncAwaitDecompiler.cs). The
// fixtures run over a locally compiled async library (the net48 corpus is
// the reference-assembly set with stripped bodies, so real Roslyn state
// machines come from a csc-built fixture -- see the yield_fixture precedent).

#include "Decompiler/IL/ControlFlow/AsyncAwaitDecompiler.hpp"

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/TypeSystem/DecompilerTypeSystem.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace MD = ::ILSpy::Decompiler::Metadata;
namespace IL = ::ILSpy::Decompiler::IL;

// The locally-compiled async fixture (Roslyn net8.0 state machines over the
// async void / Task / Task<int> shapes; built with the box's csc).
constexpr const char* kAsyncFixture =
    "/home/jim/ilspy-test-fixtures/async_fixture/AsyncFixture.dll";

struct AsyncFixtureData {
    std::unique_ptr<MD::MetadataFile> file;
    std::unique_ptr<MD::UniversalAssemblyResolver> resolver;
    std::unique_ptr<TS::DecompilerTypeSystem> ts;

    bool Load() {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(kAsyncFixture, ec))
            return false;
        file = std::make_unique<MD::MetadataFile>(kAsyncFixture);
        if (!file->IsValid())
            return false;
        resolver = std::make_unique<MD::UniversalAssemblyResolver>(
            std::string(kAsyncFixture), false,
            MD::DetectTargetFrameworkId(*file).value_or(std::string()));
        ts = std::make_unique<TS::DecompilerTypeSystem>(*file, *resolver);
        return true;
    }

    // The resolved method (the real IMethod with the real return type), the
    // driver-equivalent of the C# `function.Method`.
    std::shared_ptr<TS::IMethod> ResolveMethod(std::uint32_t methodToken) {
        const auto* module = dynamic_cast<const TS::MetadataModule*>(
            &ts->MainModule());
        if (module == nullptr) return nullptr;
        return TS::AliasMethod(module->GetDefinitionMethod(methodToken));
    }
};

IL::ILTransformContext MakeContext(AsyncFixtureData& fixture) {
    IL::ILTransformContext ctx;
    ctx.Settings.AsyncAwait = true;
    ctx.Metadata = fixture.file.get();
    ctx.TypeSystem = fixture.ts.get();
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t token,
                   std::uint32_t rva) -> std::unique_ptr<IL::ILFunction> {
        if (rva == 0) return nullptr;
        return IL::ReadIL(*fixture.file, token, rva);
    };
    return ctx;
}

// The decoded async method body + the wired method handle.
struct DecodedAsyncMethod {
    std::unique_ptr<IL::ILFunction> function;
    std::shared_ptr<TS::IMethod> method;
};

DecodedAsyncMethod DecodeMethod(AsyncFixtureData& fixture,
                                const char* methodName) {
    DecodedAsyncMethod result;
    for (const auto& t : fixture.file->TypeDefs()) {
        std::string tn = t.Name;
        if (tn != "AsyncShapes") continue;
        for (auto& m : fixture.file->GetMethods(t.Token)) {
            if (m.Name != methodName || m.RVA == 0) continue;
            result.function = IL::ReadIL(*fixture.file, m.Token, m.RVA);
            result.method = fixture.ResolveMethod(m.Token);
            if (result.function != nullptr && result.method != nullptr) {
                result.function->Method = result.method.get();
            }
            return result;
        }
    }
    return result;
}

} // namespace

// The task-creation pattern over the real `async Task` method: the builder
// field initialization, the -1 state assignment, the Start call, and the
// get_Task return all match; the pattern classifies the method as
// AsyncMethodType.Task.
TEST(AsyncAwaitDecompilerTest, MatchesTaskCreationOverRealBody) {
    AsyncFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the async fixture is not provisioned";
    DecodedAsyncMethod method = DecodeMethod(fixture, "AwaitTask");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    IL::ILTransformContext ctx = MakeContext(fixture);
    IL::AsyncAwaitDecompiler decompiler;
    EXPECT_TRUE(
        decompiler.MatchTaskCreationPattern(*method.function, ctx));
    EXPECT_EQ(decompiler.MethodType(), IL::AsyncMethodType::Task);
    EXPECT_NE(decompiler.StateField(), nullptr);
    EXPECT_NE(decompiler.BuilderField(), nullptr);
    EXPECT_NE(decompiler.BuilderField(), decompiler.StateField());
    EXPECT_EQ(decompiler.InitialState(), -1);
}

// The `async Task<int>` shape classifies as TaskOfT (the generic builder).
TEST(AsyncAwaitDecompilerTest, MatchesTaskOfTCreationOverRealBody) {
    AsyncFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the async fixture is not provisioned";
    DecodedAsyncMethod method = DecodeMethod(fixture, "AwaitTaskOfT");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    IL::ILTransformContext ctx = MakeContext(fixture);
    IL::AsyncAwaitDecompiler decompiler;
    EXPECT_TRUE(
        decompiler.MatchTaskCreationPattern(*method.function, ctx));
    EXPECT_EQ(decompiler.MethodType(), IL::AsyncMethodType::TaskOfT);
}

// The `async void` shape classifies as Void (the AsyncVoidMethodBuilder).
TEST(AsyncAwaitDecompilerTest, MatchesVoidCreationOverRealBody) {
    AsyncFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the async fixture is not provisioned";
    DecodedAsyncMethod method = DecodeMethod(fixture, "AwaitPlain");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    IL::ILTransformContext ctx = MakeContext(fixture);
    IL::AsyncAwaitDecompiler decompiler;
    EXPECT_TRUE(
        decompiler.MatchTaskCreationPattern(*method.function, ctx));
    EXPECT_EQ(decompiler.MethodType(), IL::AsyncMethodType::Void);
}

// The MoveNext analyses + the body inlining: Run over the real async Task
// method swaps the body for the state machine's try block and marks the
// function async (the await points are still their raw instructions --
// the state machine analysis is the next slice).
TEST(AsyncAwaitDecompilerTest, InlinesMoveNextBodyAndMarksAsync) {
    AsyncFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the async fixture is not provisioned";
    DecodedAsyncMethod method = DecodeMethod(fixture, "AwaitTask");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    IL::ILTransformContext ctx = MakeContext(fixture);
    IL::AsyncAwaitDecompiler decompiler;
    decompiler.Run(*method.function, ctx);

    EXPECT_TRUE(method.function->IsAsync())
        << "the function is marked async";
    ASSERT_NE(method.function->AsyncReturnType, nullptr);
    // `async Task` -- the underlying return type is void.
    EXPECT_TRUE(TS::IsKnownType(*method.function->AsyncReturnType,
                                TS::KnownTypeCode::Void));
    // The body is the state machine's try block: the awaited task's
    // GetAwaiter call is inlined into it.
    std::size_t getAwaiterCalls = 0;
    std::vector<IL::ILInstruction*> stack{method.function->Body.get()};
    while (!stack.empty()) {
        IL::ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* call = dynamic_cast<IL::Call*>(node)) {
            if (call->MethodName.find("GetAwaiter") != std::string::npos)
                getAwaiterCalls++;
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (IL::ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    EXPECT_GE(getAwaiterCalls, 1u)
        << "the inlined MoveNext body carries the await's GetAwaiter call";
}

// The Task<int> shape's underlying return type is the T.
TEST(AsyncAwaitDecompilerTest, TaskOfTUnderlyingReturnTypeIsTheElement) {
    AsyncFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the async fixture is not provisioned";
    DecodedAsyncMethod method = DecodeMethod(fixture, "AwaitTaskOfT");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    IL::ILTransformContext ctx = MakeContext(fixture);
    IL::AsyncAwaitDecompiler decompiler;
    decompiler.Run(*method.function, ctx);

    EXPECT_TRUE(method.function->IsAsync());
    ASSERT_NE(method.function->AsyncReturnType, nullptr);
    EXPECT_TRUE(TS::IsKnownType(*method.function->AsyncReturnType,
                                TS::KnownTypeCode::Int32));
}

// A non-async method (the fixture's constructor) does not match.
TEST(AsyncAwaitDecompilerTest, RejectsNonAsyncMethod) {
    AsyncFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the async fixture is not provisioned";
    DecodedAsyncMethod method = DecodeMethod(fixture, ".ctor");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    IL::ILTransformContext ctx = MakeContext(fixture);
    IL::AsyncAwaitDecompiler decompiler;
    EXPECT_FALSE(
        decompiler.MatchTaskCreationPattern(*method.function, ctx));
}
