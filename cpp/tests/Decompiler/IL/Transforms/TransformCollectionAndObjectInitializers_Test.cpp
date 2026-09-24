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

// Tests for the TransformCollectionAndObjectInitializers port (the C#
// ICSharpCode.Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.cs):
// the collection-initializer fold (`new List<int> { 10, 20 }`), the
// object-initializer fold (`new Data { a = 1 }`), and the abort when the
// initializer variable has an incompatible usage directly after the scan
// (the C# IsMethodCallOnVariable gate).

#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
using TS::TestSupport::LookupTypeDefinition;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

// The three-element `List<int> list = new List<int> { 10, 20 }` shape the C#
// IL reader produces for an assignment to a local (the dup-based IL
// initializer pattern after the stack-slot union-find pass):
//   stloc S(newobj List::_ctor)
//   callvirt Add(ldloc S, ldc 10)
//   callvirt Add(ldloc S, ldc 20)
//   stloc list(ldloc S)
//   final: nop
// The `withCollection` flag switches the element stores between the Add calls
// and the object-initializer `stobj(ldflda(ldloc S, "Data::a"), value)`
// stores; `abortWithCall` appends a `callvirt ToString(ldloc S)` instead of
// the consumer store so the C# IsMethodCallOnVariable gate aborts.
struct CollectionFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<LookupTypeDefinition> listDef;
    std::shared_ptr<LookupTypeDefinition> dataDef;
    std::shared_ptr<Impl::FakeMethod> ctorMethod;
    std::shared_ptr<Impl::FakeMethod> addMethod;
    std::shared_ptr<Impl::FakeMethod> toStringMethod;
    ILVariablePtr s;
    ILVariablePtr list;

    CollectionFixture()
    {
        // The list type carries a direct base whose definition is stamped with
        // the IEnumerable known-type code, so the C# IsMethodApplicable
        // IEnumerable base-type check passes for the Add methods.
        auto ienumerableDef = std::make_shared<LookupTypeDefinition>(
            "IEnumerable", "System.Collections",
            TS::FullTypeName("System.Collections.IEnumerable"),
            TS::TypeKind::Interface, TS::Accessibility::Public, compilation,
            nullptr, TS::KnownTypeCode::IEnumerable);
        listDef = std::make_shared<LookupTypeDefinition>(
            "List", "System.Collections.Generic",
            TS::FullTypeName("System.Collections.Generic.List"),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation,
            nullptr);
        listDef->AddDirectBaseType(ienumerableDef);
        // The object-initializer data type (a plain class, no base types).
        dataDef = std::make_shared<LookupTypeDefinition>(
            "Data", "Test", TS::FullTypeName("Test.Data"), TS::TypeKind::Class,
            TS::Accessibility::Public, compilation, nullptr);
        ctorMethod = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Constructor);
        ctorMethod->SetDeclaringType(listDef);
        addMethod = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        addMethod->SetName("Add");
        addMethod->SetDeclaringType(listDef);
        addMethod->SetIsStatic(false);
        toStringMethod = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        toStringMethod->SetName("ToString");
        toStringMethod->SetDeclaringType(listDef);
        toStringMethod->SetIsStatic(false);
        // The initializer target is the reader's stack slot S_0 (the C#
        // FlushExpressionStack introduces it for the dup'd newobj); the
        // consumer store assigns it to the `list` local.
        // The stack slot's type is the reference-typed System.Object (the
        // reader's FlushExpressionStack derives the slot type from the pushed
        // value); the IsMethodCallOnVariable gate matches an ldloc of a
        // reference-type variable.
        s = std::make_shared<IL::ILVariable>(
            IL::VariableKind::StackSlot,
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
        s->Name = "S_0";
        list = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                listDef);
        list->Name = "list";
        list->StoreCount = 1;  // IsSingleDefinition for the index-variable scan
    }

    std::unique_ptr<IL::ILFunction> MakeFunction(bool withObjectInitializer,
                                                 bool abortWithCall)
    {
        auto fn = std::make_unique<IL::ILFunction>();
        fn->Body = std::make_unique<IL::BlockContainer>();
        fn->Body->Kind = IL::ContainerKind::Normal;
        fn->Body->Parent = fn.get();
        auto block = std::make_unique<IL::Block>();
        block->Kind = IL::BlockKind::ControlFlow;
        // stloc S(newobj List::_ctor())
        auto ctorCall = std::make_unique<IL::Call>(ctorMethod);
        ctorCall->IsNewObj = true;
        ctorCall->ILStackWasEmpty = true;
        ctorCall->ReturnIType = listDef;
        block->Add(std::make_unique<IL::StLoc>(s, std::move(ctorCall)));
        if (withObjectInitializer) {
            // stobj Data(ldflda Data(ldloc S, "Test.Data::a"), ldc 1)
            std::vector<std::unique_ptr<IL::ILInstruction>> ldfldaTarget;
            ldfldaTarget.push_back(std::make_unique<IL::LdLoc>(s));
            auto ldflda = std::make_unique<IL::LdFlda>(
                std::make_unique<IL::LdLoc>(s), "Test.Data::a");
            block->Add(std::make_unique<IL::StObj>(
                std::move(ldflda), std::make_unique<IL::LdcI4>(1), listDef));
        } else {
            // callvirt Add(ldloc S, ldc 10) / (ldc 20)
            for (int value : {10, 20}) {
                auto call = std::make_unique<IL::Call>(addMethod);
                call->IsInstanceCall = true;
                call->AddArg(std::make_unique<IL::LdLoc>(s));
                call->AddArg(std::make_unique<IL::LdcI4>(value));
                block->Add(std::move(call));
            }
        }
        if (abortWithCall) {
            // callvirt ToString(ldloc S): an incompatible usage of the
            // initializer variable right after the initializer.
            auto toString = std::make_unique<IL::Call>(toStringMethod);
            toString->IsInstanceCall = true;
            toString->AddArg(std::make_unique<IL::LdLoc>(s));
            block->Add(std::move(toString));
        } else {
            // stloc list(ldloc S): the assignment that consumed the array
            // value (the C# HandleSimpleArrayInitializer-style trailing
            // consumer; the collection shape needs it too, since the C#
            // IsPartOfInitializer scan stops on it and the
            // IsMethodCallOnVariable check runs against it).
            block->Add(
                std::make_unique<IL::StLoc>(list, std::make_unique<IL::LdLoc>(s)));
        }
        block->SetFinal(std::make_unique<IL::Nop>());
        block->Parent = fn->Body.get();
        fn->Body->AddBlock(std::move(block));
        fn->Variables.push_back(s);
        fn->Variables.push_back(list);
        return fn;
    }
};

} // namespace

// The collection-initializer fold: the Add-call run becomes a
// CollectionInitializer block holding the newobj store and the Add calls,
// with the receiver rewritten from the stack slot to the InitializerTarget.
TEST(TransformCollectionAndObjectInitializersTest, AddRunBecomesCollectionInitializerBlock)
{
    CollectionFixture fixture;
    auto fn = fixture.MakeFunction(false, false);
    IL::Block* block = fn->Body->Blocks[0].get();

    IL::ILTransformContext ctx;
    ctx.Settings.ObjectOrCollectionInitializers = true;
    IL::TransformCollectionAndObjectInitializers transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);

    // The scan consumed the two Add calls; after the C# InlineIfPossible the
    // initializer block is inlined into the consumer store
    // (`stloc list(initializer block)`), so the outer block is a single
    // statement.
    ASSERT_EQ(block->Instructions.size(), 1u)
        << "the consumed Add run is folded into the initializer block";
    auto* store = dynamic_cast<IL::StLoc*>(block->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->Variable.get(), fixture.list.get());
    auto* initBlock = dynamic_cast<IL::Block*>(store->Value.get());
    ASSERT_NE(initBlock, nullptr);
    EXPECT_EQ(initBlock->Kind, IL::BlockKind::CollectionInitializer);
    // The initializer block: [stloc final(newobj), Add(ldloc final, 10),
    // Add(ldloc final, 20)] with the ldloc final final instruction.
    ASSERT_EQ(initBlock->Instructions.size(), 3u);
    auto* newObjStore =
        dynamic_cast<IL::StLoc*>(initBlock->Instructions[0].get());
    ASSERT_NE(newObjStore, nullptr);
    EXPECT_EQ(newObjStore->Variable->Kind, IL::VariableKind::InitializerTarget);
    auto* newCall = dynamic_cast<IL::Call*>(newObjStore->Value.get());
    ASSERT_NE(newCall, nullptr);
    EXPECT_TRUE(newCall->IsNewObj);
    for (int i = 1; i <= 2; ++i) {
        auto* add = dynamic_cast<IL::Call*>(initBlock->Instructions[i].get());
        ASSERT_NE(add, nullptr) << "Add call " << i;
        ASSERT_EQ(add->Arguments.size(), 2u);
        auto* receiver = dynamic_cast<IL::LdLoc*>(add->Arguments[0].get());
        ASSERT_NE(receiver, nullptr);
        EXPECT_EQ(receiver->Variable.get(), newObjStore->Variable.get())
            << "the receiver is rewritten to the initializer target";
        auto* value = dynamic_cast<IL::LdcI4*>(add->Arguments[1].get());
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value->Value, 10 * i) << "Add " << i << " keeps its value";
    }
    auto* finalLoad = dynamic_cast<IL::LdLoc*>(initBlock->FinalInstruction.get());
    ASSERT_NE(finalLoad, nullptr);
    EXPECT_EQ(finalLoad->Variable.get(), newObjStore->Variable.get());
}

// The object-initializer fold: the stobj(ldflda) store becomes an
// ObjectInitializer block (the C# Setter case flips the block kind).
TEST(TransformCollectionAndObjectInitializersTest, StObjRunBecomesObjectInitializerBlock)
{
    CollectionFixture fixture;
    auto fn = fixture.MakeFunction(true, false);
    IL::Block* block = fn->Body->Blocks[0].get();

    IL::ILTransformContext ctx;
    ctx.Settings.ObjectOrCollectionInitializers = true;
    IL::TransformCollectionAndObjectInitializers transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u)
        << "the consumed stobj is folded into the initializer block";
    auto* store = dynamic_cast<IL::StLoc*>(block->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->Variable.get(), fixture.list.get());
    auto* initBlock = dynamic_cast<IL::Block*>(store->Value.get());
    ASSERT_NE(initBlock, nullptr);
    EXPECT_EQ(initBlock->Kind, IL::BlockKind::ObjectInitializer);
    ASSERT_EQ(initBlock->Instructions.size(), 2u);
    auto* newObjStore =
        dynamic_cast<IL::StLoc*>(initBlock->Instructions[0].get());
    ASSERT_NE(newObjStore, nullptr);
    EXPECT_EQ(newObjStore->Variable->Kind, IL::VariableKind::InitializerTarget);
    auto* stObj = dynamic_cast<IL::StObj*>(initBlock->Instructions[1].get());
    ASSERT_NE(stObj, nullptr);
    auto* ldflda = dynamic_cast<IL::LdFlda*>(stObj->Target.get());
    ASSERT_NE(ldflda, nullptr);
    EXPECT_EQ(ldflda->FieldName, "Test.Data::a");
    auto* receiver = dynamic_cast<IL::LdLoc*>(ldflda->Target.get());
    ASSERT_NE(receiver, nullptr);
    EXPECT_EQ(receiver->Variable.get(), newObjStore->Variable.get())
        << "the field target is rewritten to the initializer target";
    auto* value = dynamic_cast<IL::LdcI4*>(stObj->Value.get());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->Value, 1);
}

// The abort: a `callvirt ToString(ldloc S)` directly after the initializer
// stloc is an incompatible usage of the initializer variable (the C#
// IsMethodCallOnVariable gate), so the block stays untouched.
TEST(TransformCollectionAndObjectInitializersTest, IncompatibleUsageAbortsTransform)
{
    CollectionFixture fixture;
    auto fn = fixture.MakeFunction(false, true);
    IL::Block* block = fn->Body->Blocks[0].get();

    IL::ILTransformContext ctx;
    ctx.Settings.ObjectOrCollectionInitializers = true;
    IL::TransformCollectionAndObjectInitializers transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);
    // The transform aborted: the stloc's value is still the raw newobj and
    // the ToString call is untouched. The block keeps all four statements
    // (the stloc, the two Adds, and the ToString call).
    ASSERT_EQ(block->Instructions.size(), 4u);
    auto* store = dynamic_cast<IL::StLoc*>(block->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    auto* newCall = dynamic_cast<IL::Call*>(store->Value.get());
    ASSERT_NE(newCall, nullptr);
    EXPECT_TRUE(newCall->IsNewObj);
    auto* toString = dynamic_cast<IL::Call*>(block->Instructions[3].get());
    ASSERT_NE(toString, nullptr);
    EXPECT_EQ(toString->Method.get(), fixture.toStringMethod.get());
}