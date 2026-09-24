// Copyright (c) 2026 Jim Hester
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
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

// Tests for the CSharpDecompiler facade (the C# `public class
// CSharpDecompiler` port shell): the pipeline entry aliases the
// IL-namespace list, and DecompileFunctionToString runs the pipeline over
// a synthetic function and renders the method text.

#include "Decompiler/CSharp/CSharpDecompiler.hpp"

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Metadata = ::ILSpy::Decompiler::Metadata;
using ::ILSpy::Decompiler::Metadata::MethodSignature;
namespace CSharp = ::ILSpy::Decompiler::CSharp;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

TEST(CSharpDecompilerTest, GetILTransformsAliasesTheILNamespaceFactory)
{
    auto transforms = CSharp::CSharpDecompiler::GetILTransforms();
    ASSERT_FALSE(transforms.empty());
    // The same list shape the IL-namespace factory produces (the head is
    // ControlFlowSimplification, per the C# GetILTransforms list).
    EXPECT_NE(dynamic_cast<IL::ControlFlowSimplification*>(
                  transforms[0].get()),
              nullptr);
}

// The per-body decompile half: a synthetic function whose body is
// `stloc v(ldc.i4 42)` runs through the pipeline and renders with the
// constant folded.
TEST(CSharpDecompilerTest, DecompileFunctionToStringRendersThePipelineOutput)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr v = fn->RegisterVariable(IL::VariableKind::Local,
                                           intType, std::string("V_0"));
    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    // The Body's Parent wiring (the C# ILFunction ctor's child-slot
    // assignment; the port's hand-built trees wire it explicitly -- the
    // CachedDelegateInitialization fixture precedent).
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));
    {
        auto value = std::make_unique<IL::LdcI4>(42);
        auto store = std::make_unique<IL::StLoc>(v, std::move(value));
        blockPtr->Add(std::move(store));
    }
    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(v));

    std::string text = CSharp::CSharpDecompiler::DecompileFunctionToString(
        *fn, "int", "M", "");
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("42"), std::string::npos)
        << "the rendered method text carries the constant";
}

// The signature-decl builder (the C# Decompile path's parameter-declaration
// half, extracted from the CLI's inline block): named parameters carry
// their name; unnamed parameters fall back to arg_<base + index> (base 1
// for an instance method -- `this` is the implicit arg_0; base 0 static).
TEST(CSharpDecompilerTest, MethodDeclStringFallsBackToArgNames)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto stringType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    MethodSignature sig;
    sig.ReturnType = intType;
    sig.ParameterTypes = {intType, stringType};
    sig.IsInstance = true;
    // The first parameter carries a metadata name; the second does not.
    EXPECT_EQ(CSharp::CSharpDecompiler::MethodDeclString(
                  sig, std::vector<std::string>{"x"}),
              "int x, string arg_2");
}

// The static shape: the fallback base is 0 (no implicit this).
TEST(CSharpDecompilerTest, MethodDeclStringStaticBaseIsZero)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    MethodSignature sig;
    sig.ReturnType = intType;
    sig.ParameterTypes = {intType, intType};
    sig.IsInstance = false;
    EXPECT_EQ(CSharp::CSharpDecompiler::MethodDeclString(
                  sig, std::vector<std::string>{}),
              "int arg_0, int arg_1");
}

// The full per-method entry over the in-repo corpus: the connid_res.dll
// fixture (a real csc-compiled library) decompiles at least one method
// body with its method name in the rendered text.
TEST(CSharpDecompilerTest, DecompileMethodRendersACorpusMethod)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    bool rendered = false;
    for (const auto& t : module.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        for (const auto& m : module.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            std::string text;
            if (CSharp::CSharpDecompiler::DecompileMethodToString(
                    module, m.Token, m.RVA, m.Name, text)) {
                EXPECT_NE(text.find(m.Name), std::string::npos)
                    << "the rendered method carries its name";
                rendered = true;
                break;
            }
        }
        if (rendered) break;
    }
    EXPECT_TRUE(rendered) << "the corpus yields at least one decodable body";
}

// The type-level entry: the connid_res corpus type renders with the type
// header and at least one member body.
TEST(CSharpDecompilerTest, DecompileTypeRendersTheTypeAndMembers)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    for (const auto& t : module.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        std::string text;
        if (CSharp::CSharpDecompiler::DecompileTypeToString(module, t.Token,
                                                            text)) {
            EXPECT_NE(text.find(t.Name), std::string::npos)
                << "the rendered type carries its name";
            SUCCEED();
            return;
        }
    }
    FAIL() << "no type in the corpus decompiled";
}

} // namespace
