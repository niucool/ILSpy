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

// The DynamicCallSiteTransform tests over the locally-compiled dynamic
// fixture: the callsite-cache null check, the binder-init block scan, and
// the delegate-invoke replacement produce the dynamic ILAst nodes.

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/DynamicInstructions.hpp"
#include "Decompiler/IL/Transforms/DynamicCallSiteTransform.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
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
#include <vector>

using namespace ILSpy::Decompiler::IL;
namespace MD = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;

// The locally-compiled dynamic fixture (the Roslyn callsite-cache shapes for
// the get-member / invoke-member / invoke binders; built with the box's csc
// over the shared-framework implementation assemblies -- the reference-pack
// Microsoft.CSharp lacks the compiler-required Binder members).
constexpr const char* kDynamicFixture =
    "/home/jim/ilspy-test-fixtures/dynamic_fixture/DynamicFixture.dll";

struct DynamicFixtureData {
    std::unique_ptr<MD::MetadataFile> file;
    std::unique_ptr<MD::UniversalAssemblyResolver> resolver;
    std::unique_ptr<TS::DecompilerTypeSystem> ts;

    bool Load() {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(kDynamicFixture, ec))
            return false;
        file = std::make_unique<MD::MetadataFile>(kDynamicFixture);
        if (!file->IsValid())
            return false;
        resolver = std::make_unique<MD::UniversalAssemblyResolver>(
            std::string(kDynamicFixture), false,
            MD::DetectTargetFrameworkId(*file).value_or(std::string()));
        ts = std::make_unique<TS::DecompilerTypeSystem>(*file, *resolver);
        return true;
    }

    std::shared_ptr<TS::IMethod> ResolveMethod(std::uint32_t methodToken) {
        const auto* module =
            dynamic_cast<const TS::MetadataModule*>(&ts->MainModule());
        if (module == nullptr) return nullptr;
        return TS::AliasMethod(module->GetDefinitionMethod(methodToken));
    }
};

struct DecodedDynamicMethod {
    std::unique_ptr<ILFunction> function;
    std::shared_ptr<TS::IMethod> method;
};

namespace {

DecodedDynamicMethod DecodeMethod(DynamicFixtureData& fixture,
                                  const char* methodName) {
    DecodedDynamicMethod result;
    for (const auto& t : fixture.file->TypeDefs()) {
        if (std::string(t.Name) != "DynamicShapes") continue;
        for (auto& m : fixture.file->GetMethods(t.Token)) {
            if (m.Name != methodName || m.RVA == 0) continue;
            result.function = ReadIL(*fixture.file, m.Token, m.RVA);
            result.method = fixture.ResolveMethod(m.Token);
            if (result.function != nullptr && result.method != nullptr) {
                result.function->Method = result.method.get();
            }
            return result;
        }
    }
    return result;
}

ILTransformContext MakeContext(DynamicFixtureData& fixture) {
    ILTransformContext ctx;
    ctx.Settings.Dynamic = true;
    ctx.Metadata = fixture.file.get();
    ctx.TypeSystem = fixture.ts.get();
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t token,
                   std::uint32_t rva) -> std::unique_ptr<ILFunction> {
        if (rva == 0) return nullptr;
        return ReadIL(*fixture.file, token, rva);
    };
    return ctx;
}

// The pipeline prefix stages the DynamicCallSiteTransform slot sees: the C#
// GetILTransforms order through the second CFS (the transform's slot sits
// right after it, before SwitchDetection / LoopDetection / the block
// transforms -- the later stages would already restructure the callsite
// control flow).
void RunPipelinePrefix(ILFunction& function, ILTransformContext& ctx) {
    RunILTransformsThroughSecondCFS(function, ctx);
}

} // namespace

// The get-member shape: `return ((dynamic)d).Foo;` becomes a
// DynamicGetMemberInstruction over the target, and the callsite-init block
// and the cache null check disappear.
TEST(DynamicCallSiteTransformTest, ConvertsGetMemberOverRealBody) {
    DynamicFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the dynamic fixture is not provisioned";
    DecodedDynamicMethod method = DecodeMethod(fixture, "GetMember");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    ILTransformContext ctx = MakeContext(fixture);
    RunPipelinePrefix(*method.function, ctx);

    DynamicCallSiteTransform transform;
    transform.Run(*method.function, ctx);

    std::vector<DynamicGetMemberInstruction*> getMembers;
    std::size_t callsiteNews = 0;
    std::vector<ILInstruction*> stack{method.function->Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* gm = dynamic_cast<DynamicGetMemberInstruction*>(node))
            getMembers.push_back(gm);
        if (auto* call = dynamic_cast<Call*>(node)) {
            if (call->MethodName.find("CallSite") != std::string::npos)
                callsiteNews++;
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    ASSERT_EQ(getMembers.size(), 1u) << "exactly one dynamic get-member";
    EXPECT_EQ(getMembers[0]->Name, "Foo");
    ASSERT_NE(getMembers[0]->Target, nullptr);
    EXPECT_EQ(getMembers[0]->Target->Op, OpCode::LdLoc)
        << "the target is the (cached) receiver";
    EXPECT_EQ(callsiteNews, 0u)
        << "the CallSite.Create machinery is consumed";
}

// The invoke-member shape: `return ((dynamic)d).Bar(i);` becomes a
// DynamicInvokeMemberInstruction with the member name and the argument.
TEST(DynamicCallSiteTransformTest, ConvertsInvokeMemberOverRealBody) {
    DynamicFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the dynamic fixture is not provisioned";
    DecodedDynamicMethod method = DecodeMethod(fixture, "InvokeMember");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    ILTransformContext ctx = MakeContext(fixture);
    RunPipelinePrefix(*method.function, ctx);

    DynamicCallSiteTransform transform;
    transform.Run(*method.function, ctx);

    std::vector<DynamicInvokeMemberInstruction*> invokes;
    std::vector<ILInstruction*> stack{method.function->Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* im = dynamic_cast<DynamicInvokeMemberInstruction*>(node))
            invokes.push_back(im);
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    ASSERT_EQ(invokes.size(), 1u) << "exactly one dynamic invoke-member";
    EXPECT_EQ(invokes[0]->Name, "Bar");
    EXPECT_EQ(invokes[0]->ChildCount(), 2) << "the receiver + the argument";
}

// The invoke shape: `return ((dynamic)d)();` becomes a
// DynamicInvokeInstruction over the receiver.
TEST(DynamicCallSiteTransformTest, ConvertsInvokeOverRealBody) {
    DynamicFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the dynamic fixture is not provisioned";
    DecodedDynamicMethod method = DecodeMethod(fixture, "Invoke");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    ILTransformContext ctx = MakeContext(fixture);
    RunPipelinePrefix(*method.function, ctx);

    DynamicCallSiteTransform transform;
    transform.Run(*method.function, ctx);

    std::vector<DynamicInvokeInstruction*> invokes;
    std::vector<ILInstruction*> stack{method.function->Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* inv = dynamic_cast<DynamicInvokeInstruction*>(node))
            invokes.push_back(inv);
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    ASSERT_EQ(invokes.size(), 1u) << "exactly one dynamic invoke";
    EXPECT_EQ(invokes[0]->ChildCount(), 1) << "the receiver alone";
}

// The setting gate: with Dynamic off, the transform is a no-op (the
// callsite machinery stays).
TEST(DynamicCallSiteTransformTest, SettingGateBlocksTheTransform) {
    DynamicFixtureData fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the dynamic fixture is not provisioned";
    DecodedDynamicMethod method = DecodeMethod(fixture, "GetMember");
    ASSERT_NE(method.function, nullptr);
    ASSERT_NE(method.method, nullptr);

    ILTransformContext ctx = MakeContext(fixture);
    ctx.Settings.Dynamic = false;
    RunPipelinePrefix(*method.function, ctx);

    DynamicCallSiteTransform transform;
    transform.Run(*method.function, ctx);

    std::size_t dynamicNodes = 0;
    std::vector<ILInstruction*> stack{method.function->Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (dynamic_cast<DynamicInstruction*>(node))
            dynamicNodes++;
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    EXPECT_EQ(dynamicNodes, 0u)
        << "the transform does not run with the setting off";
}
