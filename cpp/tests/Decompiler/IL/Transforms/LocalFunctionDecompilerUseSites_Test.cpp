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

// Tests for LocalFunctionDecompiler's use-site walk (the C# FindUseSites +
// TransformUseSites): a call or ldftn whose method name parses as a local
// function name is a use-site; a delegate construction use-site (the
// `newobj Delegate(target, ldftn localFunction)` shape) has its capture
// target replaced with ldnull, and a non-local-function ldftn is left
// untouched.

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

// The use-site walk over a mixed body: one plain call use-site and one
// delegate-construction use-site. The transform's observable rewrite is the
// delegate construction's target becoming ldnull (the C#
// TransformToLocalFunctionReference target arm); the plain call keeps its
// arguments (the invocation reshape needs the parameter metadata surface).
TEST(LocalFunctionDecompilerUseSitesTest, RewritesDelegateConstructionTargetToLdNull)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr captured = fn->RegisterVariable(
        IL::VariableKind::Local, intType, std::string("captured"));

    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    // call Test.C::<M>g__LF|0_0(ldloc captured) -- a plain local-function call.
    auto plainCall = std::make_unique<IL::Call>("Test.C::<M>g__LF|0_0");
    plainCall->AddArg(std::make_unique<IL::LdLoc>(captured));
    blockPtr->Add(std::move(plainCall));

    // newobj Action(ldloc captured, ldftn Test.C::<M>g__LF2|0_1) -- a
    // delegate construction use-site (the declaring type must resolve to
    // Kind == Delegate for MatchDelegateConstruction).
    auto newobj = std::make_unique<IL::Call>("Test.Action::.ctor");
    newobj->IsNewObj = true;
    newobj->DeclaringType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string("System"), std::string("Action")),
        TS::TypeKind::Delegate);
    newobj->AddArg(std::make_unique<IL::LdLoc>(captured));
    newobj->AddArg(
        std::make_unique<IL::LdFtn>(std::string("Test.C::<M>g__LF2|0_1")));
    IL::Call* newobjPtr = newobj.get();
    blockPtr->Add(std::move(newobj));

    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(captured));

    IL::ILTransformContext ctx;
    ctx.Settings.LocalFunctions = true;
    IL::LocalFunctionDecompiler transform;
    transform.Run(*fn, ctx);

    // The delegate construction's capture target became ldnull.
    auto* newTarget = dynamic_cast<IL::LdNull*>(newobjPtr->Arguments[0].get());
    EXPECT_NE(newTarget, nullptr)
        << "the delegate construction target is replaced with ldnull";
    EXPECT_EQ(newobjPtr->Arguments[1]->Op, IL::OpCode::LdFtn)
        << "the ldftn argument is kept";
    // The plain call use-site keeps its arguments (the invocation reshape
    // needs the parameter metadata surface).
    ASSERT_EQ(blockPtr->Instructions.size(), 2u);
    auto* kept = dynamic_cast<IL::Call*>(blockPtr->Instructions[0].get());
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->Arguments.size(), 1u);
    EXPECT_EQ(kept->MethodName, "Test.C::<M>g__LF|0_0");
}

// The capture/declaration-scope machinery (the C# DetermineCaptureAndDeclarationScopes):
// a static local function invoked with `ldloca captured` marks the captured
// display-struct local as DisplayClassLocal, records its CaptureScope, and
// assigns the decoded definition's DeclarationScope (the closest container
// of the captured variable's first address-taking use). The definition's
// closure parameter is a ByReferenceType to the display struct, and the
// current-type anchor drives IsPotentialClosure.
TEST(LocalFunctionDecompilerUseSitesTest, DeterminesCaptureAndDeclarationScopes)
{
    TS::TestSupport::LookupCompilation compilation;
    auto decompiledDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        std::string("C"), std::string("Test"),
        TS::FullTypeName(TS::TopLevelTypeName(std::string("Test"), std::string("C"))),
        TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr);
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> displayDef =
        std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        std::string("<>c__DisplayClass0"), std::string("Test"),
        TS::FullTypeName(TS::TopLevelTypeName(
            std::string("Test"), std::string("<>c__DisplayClass0"))),
        TS::TypeKind::Struct, TS::Accessibility::Private, compilation, nullptr);
    displayDef->SetDeclaringTypeDefinition(decompiledDef.get());
    auto compilerGeneratedType =
        std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
            std::string("System.Runtime.CompilerServices"),
            std::string("CompilerGeneratedAttribute")));
    auto compilerGeneratedAttr =
        std::make_shared<TS::TestSupport::LookupAttribute>(
            compilerGeneratedType, std::vector<TS::CustomAttributeTypedArgument>{});
    displayDef->SetAttributes(
        std::vector<const TS::IAttribute*>{compilerGeneratedAttr.get()});

    auto fn = std::make_unique<IL::ILFunction>();
    ILVariablePtr captured = fn->RegisterVariable(
        IL::VariableKind::Local, displayDef, std::string("captured"));

    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    // stloc captured(newobj <>c__DisplayClass0..ctor()) -- the display-struct
    // initializer, then the use-site `call <M>g__LF|0_0(ldloca captured)`.
    auto ctor = std::make_unique<IL::Call>(
        std::string("Test.<>c__DisplayClass0::.ctor"));
    ctor->IsNewObj = true;
    auto init = std::make_unique<IL::StLoc>(captured, std::move(ctor));
    blockPtr->Add(std::move(init));
    auto plainCall = std::make_unique<IL::Call>(
        std::string("Test.C::<M>g__LF|0_0"));
    plainCall->AddArg(std::make_unique<IL::LdLoca>(captured));
    blockPtr->Add(std::move(plainCall));
    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(captured));

    IL::ILTransformContext ctx;
    ctx.Settings.LocalFunctions = true;
    ctx.CurrentTypeDefinition = decompiledDef.get();
    // The decoded definition: static, one ByReferenceType closure parameter
    // over the display struct (the C# IsClosureParameter shape).
    ctx.LocalFunctionBodyResolver =
        [displayDef, &captured](const std::string& methodName)
        -> std::unique_ptr<IL::ILFunction> {
        if (methodName != "Test.C::<M>g__LF|0_0") return nullptr;
        auto def = std::make_unique<IL::ILFunction>();
        def->Name = methodName;
        def->Kind = IL::ILFunctionKind::LocalFunction;
        def->IsStatic = true;
        auto refType =
            std::make_shared<TS::ByReferenceType>(displayDef);
        def->Parameters.push_back(
            std::make_shared<TS::Implementation::DefaultParameter>(
            refType, std::string("captured")));
        auto defBody = std::make_unique<IL::BlockContainer>();
        def->Body = std::move(defBody);
        auto defBlock = std::make_unique<IL::Block>();
        defBlock->Kind = IL::BlockKind::ControlFlow;
        def->Body->AddBlock(std::move(defBlock));
        return def;
    };

    IL::LocalFunctionDecompiler transform;
    transform.Run(*fn, ctx);

    ASSERT_EQ(fn->LocalFunctions.size(), 1u);
    IL::ILFunction* definition = fn->LocalFunctions[0].get();
    EXPECT_EQ(definition->DeclarationScope, fn->Body.get())
        << "the declaration scope is the container of the captured "
           "variable's first address-taking use";
    EXPECT_EQ(captured->CaptureScope, fn->Body.get())
        << "the captured variable's capture scope is the same container";
    EXPECT_EQ(captured->Kind, IL::VariableKind::DisplayClassLocal)
        << "the captured local is retargeted to DisplayClassLocal";
}

// A non-local-function ldftn (not part of a delegate construction) is not a
// use-site and is left untouched.
TEST(LocalFunctionDecompilerUseSitesTest, IgnoresNonLocalFunctionLdFtn)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr captured = fn->RegisterVariable(
        IL::VariableKind::Local, intType, std::string("captured"));

    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    auto ldftn = std::make_unique<IL::LdFtn>(std::string("Test.C::PlainMethod"));
    IL::LdFtn* ldftnPtr = ldftn.get();
    blockPtr->Add(std::move(ldftn));
    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(captured));

    IL::ILTransformContext ctx;
    ctx.Settings.LocalFunctions = true;
    IL::LocalFunctionDecompiler transform;
    transform.Run(*fn, ctx);

    ASSERT_EQ(blockPtr->Instructions.size(), 1u);
    EXPECT_EQ(blockPtr->Instructions[0].get(), ldftnPtr)
        << "the non-local-function ldftn node is untouched";
}

// The deep-decode entry (the C# ReadLocalFunctionDefinition): a first
// sighting resolves the local function's body through the context's
// resolver hook, embeds it into the top-level function's LocalFunctions
// (the C# flat embedding -- every decoded definition lands on
// context.Function regardless of nesting), and recurses the use-site walk
// into the new body so nested use-sites are found.
TEST(LocalFunctionDecompilerUseSitesTest, ResolvesAndEmbedsLocalFunctionDefinition)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr captured = fn->RegisterVariable(
        IL::VariableKind::Local, intType, std::string("captured"));

    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    // call Test.C::<M>g__LF|0_0(ldloc captured) -- a plain local-function call
    // whose body the resolver supplies.
    auto plainCall = std::make_unique<IL::Call>("Test.C::<M>g__LF|0_0");
    plainCall->AddArg(std::make_unique<IL::LdLoc>(captured));
    blockPtr->Add(std::move(plainCall));
    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(captured));

    IL::ILTransformContext ctx;
    ctx.Settings.LocalFunctions = true;
    // The resolver: Test.C::<M>g__LF|0_0 decodes to a body that itself calls
    // Test.C::<M>g__LF2|0_1 -- the nested use-site is only reachable through
    // the decoded body (the C# FindUseSites(info.Definition, ...) recursion).
    ctx.LocalFunctionBodyResolver =
        [&captured](const std::string& methodName)
        -> std::unique_ptr<IL::ILFunction> {
        if (methodName != "Test.C::<M>g__LF|0_0" &&
            methodName != "Test.C::<M>g__LF2|0_1") {
            return nullptr;
        }
        auto def = std::make_unique<IL::ILFunction>();
        def->Name = methodName;
        def->Kind = IL::ILFunctionKind::LocalFunction;
        auto defBody = std::make_unique<IL::BlockContainer>();
        def->Body = std::move(defBody);
        auto defBlock = std::make_unique<IL::Block>();
        defBlock->Kind = IL::BlockKind::ControlFlow;
        IL::Block* defBlockPtr = defBlock.get();
        def->Body->AddBlock(std::move(defBlock));
        if (methodName == "Test.C::<M>g__LF|0_0") {
            // The nested use-site inside the decoded body.
            auto nested = std::make_unique<IL::Call>(
                std::string("Test.C::<M>g__LF2|0_1"));
            nested->AddArg(std::make_unique<IL::LdLoc>(captured));
            defBlockPtr->Add(std::move(nested));
        }
        defBlockPtr->SetFinal(
            std::make_unique<IL::LdLoc>(captured));
        (void)defBlockPtr;
        return def;
    };

    IL::LocalFunctionDecompiler transform;
    transform.Run(*fn, ctx);

    // Both definitions are embedded (the C# flat embedding into
    // context.Function.LocalFunctions), in discovery order.
    ASSERT_EQ(fn->LocalFunctions.size(), 2u);
    EXPECT_EQ(fn->LocalFunctions[0]->Name, "Test.C::<M>g__LF|0_0");
    EXPECT_EQ(fn->LocalFunctions[0]->Kind, IL::ILFunctionKind::LocalFunction);
    EXPECT_EQ(fn->LocalFunctions[1]->Name, "Test.C::<M>g__LF2|0_1");
    // The plain call's use-site was rewritten (its ldnull target shape is
    // not exercised here; the call stays a call).
    ASSERT_EQ(blockPtr->Instructions.size(), 1u);
}

} // namespace
