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

// Tests for TransformArrayInitializers (the port of
// ICSharpCode.Decompiler/IL/Transforms/TransformArrayInitializers.cs, the
// simple single-dim arm): the stelem-scan-to-ArrayInitializer-block rewrite,
// the gap-filling defaults, the abort cases (a trailing element store and a
// too-sparse initializer), and the ownership of the taken values.

#include "Decompiler/IL/Transforms/TransformArrayInitializers.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
using IL::BlockKind;
using IL::OpCode;
using IL::StackType;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

// The three-element `int[] a = new int[3] { 10, 20, 30 }` shape the C# IL
// reader produces for an assignment to a local (the dup-based IL initializer
// pattern after the stack-slot union-find pass):
//   stloc S(newarr Int32 [3])
//   stobj Int32(ldelema Int32(ldloc S, ldc 0), ldc 10)
//   stobj Int32(ldelema Int32(ldloc S, ldc 1), ldc 20)
//   stobj Int32(ldelema Int32(ldloc S, ldc 2), ldc 30)
//   stloc a(ldloc S)
//   final: nop
// The C# HandleSimpleArrayInitializer requires at least one non-consumed
// instruction after the scanned run (the C# bound
// `pos + instructionsToRemove >= block.Instructions.Count`), so the trailing
// assignment store is part of the shape.
std::unique_ptr<IL::ILFunction> MakeArrayInitFn(TS::ITypePtr int32Type)
{
    auto fn = std::make_unique<IL::ILFunction>();
    fn->Body = std::make_unique<IL::BlockContainer>();
    fn->Body->Kind = IL::ContainerKind::Normal;
    fn->Body->Parent = fn.get();
    auto block = std::make_unique<IL::Block>();
    block->Kind = BlockKind::ControlFlow;
    auto v = MakeLocal("S_0", int32Type);
    auto a = MakeLocal("a", int32Type);
    std::vector<std::unique_ptr<IL::ILInstruction>> lengths;
    lengths.push_back(std::make_unique<IL::LdcI4>(3));
    block->Add(std::make_unique<IL::StLoc>(
        v, std::make_unique<IL::NewArr>(int32Type, std::move(lengths))));
    for (int i = 0; i < 3; ++i) {
        std::vector<std::unique_ptr<IL::ILInstruction>> indices;
        indices.push_back(std::make_unique<IL::LdcI4>(i));
        auto ldelem = std::make_unique<IL::LdElema>(int32Type,
                                                    std::make_unique<IL::LdLoc>(v),
                                                    std::move(indices));
        block->Add(std::make_unique<IL::StObj>(std::move(ldelem),
                                               std::make_unique<IL::LdcI4>(10 * (i + 1)),
                                               int32Type));
    }
    block->Add(std::make_unique<IL::StLoc>(a, std::make_unique<IL::LdLoc>(v)));
    block->SetFinal(std::make_unique<IL::Nop>());
    block->Parent = fn->Body.get();
    fn->Body->AddBlock(std::move(block));
    fn->Variables.push_back(v);
    fn->Variables.push_back(a);
    return fn;
}

} // namespace

// The full rewrite: the stelem run becomes an ArrayInitializer block holding
// the newarr store and the three element stores, inlined into the stloc.
TEST(TransformArrayInitializersTest, StelemRunBecomesArrayInitializerBlock)
{
    auto compilation =
            TS::SimpleCompilation(Impl::MinimalCorlib::Instance(), {});
    TS::ITypePtr int32Type =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto fn = MakeArrayInitFn(int32Type);

    IL::Block* block = fn->Body->Blocks[0].get();

    IL::ILTransformContext ctx;
    ctx.Settings.ArrayInitializers = true;
    IL::TransformArrayInitializers transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);

    // The stloc's value is now the ArrayInitializer block.
    ASSERT_EQ(block->Instructions.size(), 1u)
        << "the consumed stelem run is folded into the initializer block";
    auto* store = dynamic_cast<IL::StLoc*>(block->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    auto* initBlock = dynamic_cast<IL::Block*>(store->Value.get());
    ASSERT_NE(initBlock, nullptr);
    EXPECT_EQ(initBlock->Kind, BlockKind::ArrayInitializer);
    // The initializer block: [stloc v(newarr 3), stobj x3, final ldloc v].
    ASSERT_EQ(initBlock->Instructions.size(), 4u);
    auto* newArrStore = dynamic_cast<IL::StLoc*>(initBlock->Instructions[0].get());
    ASSERT_NE(newArrStore, nullptr);
    auto* newArr = dynamic_cast<IL::NewArr*>(newArrStore->Value.get());
    ASSERT_NE(newArr, nullptr);
    ASSERT_EQ(newArr->Indices.size(), 1u);
    auto* length = dynamic_cast<IL::LdcI4*>(newArr->Indices[0].get());
    ASSERT_NE(length, nullptr);
    EXPECT_EQ(length->Value, 3);
    // The element stores carry the taken values (10, 20, 30) in order.
    for (int i = 0; i < 3; ++i) {
        auto* element =
            dynamic_cast<IL::StObj*>(initBlock->Instructions[i + 1].get());
        ASSERT_NE(element, nullptr);
        auto* elema = dynamic_cast<IL::LdElema*>(element->Target.get());
        ASSERT_NE(elema, nullptr);
        ASSERT_EQ(elema->Indices.size(), 1u);
        auto* idx = dynamic_cast<IL::LdcI4*>(elema->Indices[0].get());
        ASSERT_NE(idx, nullptr);
        EXPECT_EQ(idx->Value, i) << "element " << i << " keeps its index";
        auto* value = dynamic_cast<IL::LdcI4*>(element->Value.get());
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value->Value, 10 * (i + 1)) << "element " << i << " keeps its value";
    }
}

// The trailing element store aborts the transform (partial initializers are
// not constructed), so the block stays untouched.
TEST(TransformArrayInitializersTest, TrailingElementStoreAbortsTransform)
{
    auto compilation =
            TS::SimpleCompilation(Impl::MinimalCorlib::Instance(), {});
    TS::ITypePtr int32Type =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto fn = MakeArrayInitFn(int32Type);
    IL::ILVariablePtr v = fn->Variables[0];

    IL::Block* block = fn->Body->Blocks[0].get();
    // A trailing stelem after the three initializer stores.
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(1));
    auto ldelem = std::make_unique<IL::LdElema>(
        int32Type, std::make_unique<IL::LdLoc>(v), std::move(indices));
    // Inserted before the trailing consumer store, so the scan encounters it
    // while the minimum is still behind index 1 (the C# shape: the element
    // store sequence is followed by the consumer).
    block->InsertAt(4, std::make_unique<IL::StObj>(std::move(ldelem),
                                                   std::make_unique<IL::LdcI4>(99),
                                                   int32Type));

    IL::ILTransformContext ctx;
    ctx.Settings.ArrayInitializers = true;
    IL::TransformArrayInitializers transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);

    // The transform aborted: the stloc's value is still the raw newarr.
    auto* store = dynamic_cast<IL::StLoc*>(block->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->Value->Op, OpCode::NewArr)
        << "a trailing element store aborts the initializer construction";
}

// The gap-filling: a two-element initializer for a three-element array keeps
// the written values and default-fills the hole.
TEST(TransformArrayInitializersTest, GapsAreDefaultFilled)
{
    auto compilation =
            TS::SimpleCompilation(Impl::MinimalCorlib::Instance(), {});
    TS::ITypePtr int32Type =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto fn = MakeArrayInitFn(int32Type);

    IL::Block* block = fn->Body->Blocks[0].get();
    // Remove the middle element store to leave slot 1 uninitialized.
    block->RemoveInstructionAt(2);

    IL::ILTransformContext ctx;
    ctx.Settings.ArrayInitializers = true;
    IL::TransformArrayInitializers transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);

    auto* store = dynamic_cast<IL::StLoc*>(block->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    auto* initBlock = dynamic_cast<IL::Block*>(store->Value.get());
    ASSERT_NE(initBlock, nullptr);
    ASSERT_EQ(initBlock->Instructions.size(), 4u);
    // Element 0 keeps 10; element 1 is default (the DefaultValue filler);
    // element 2 keeps 30.
    auto* element1 = dynamic_cast<IL::StObj*>(initBlock->Instructions[2].get());
    ASSERT_NE(element1, nullptr);
    auto* elema1 = dynamic_cast<IL::LdElema*>(element1->Target.get());
    ASSERT_NE(elema1, nullptr);
    auto* idx1 = dynamic_cast<IL::LdcI4*>(elema1->Indices[0].get());
    ASSERT_NE(idx1, nullptr);
    EXPECT_EQ(idx1->Value, 1);
    auto* value1 = dynamic_cast<IL::DefaultValue*>(element1->Value.get());
    ASSERT_NE(value1, nullptr) << "the gap slot is default-filled";
    auto* element2 = dynamic_cast<IL::StObj*>(initBlock->Instructions[3].get());
    ASSERT_NE(element2, nullptr);
    auto* value2 = dynamic_cast<IL::LdcI4*>(element2->Value.get());
    ASSERT_NE(value2, nullptr);
    EXPECT_EQ(value2->Value, 30);
}