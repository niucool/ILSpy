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

// Tests for the IndexRangeTransform port (the C#
// ICSharpCode.Decompiler/IL/Transforms/IndexRangeTransform.cs): the
// HandleLdElema `array[^i]` case and the Run driver's TransformIndexing
// (`array[GetOffset(...)]` -> SyntheticRangeIndexAccessor). The fixture type
// system registers System.Index / System.Range stub types with the member
// lists the C# IndexMethods scan consults (GetConstructors / GetMethods /
// GetProperties), plus a container type with the Length property and the
// int-parameter indexer the CSharpWillGenerateIndexer gate scans.

#include "Decompiler/IL/Transforms/IndexRangeTransform.hpp"
#include "Decompiler/TypeSystem/Implementation/SyntheticRangeIndexAccessor.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"

#include <cstdio>
#include <string>
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl_ = ::ILSpy::Decompiler::TypeSystem::Implementation;
using TS::TestSupport::LookupTypeDefinition;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

// A LookupTypeDefinition with hand-wired GetConstructors / GetMethods /
// GetProperties lists (the IType member-enumeration surface the C#
// IndexMethods scan and the CSharpWillGenerateIndexer property scan walk; the
// LookupStubs base only implements the GetProperties filter form).
class MemberListTypeDefinition : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetCtors(std::vector<const TS::IMethod*> ctors) { ctors_ = std::move(ctors); }
    void SetMethodList(std::vector<const TS::IMethod*> methods) {
        methods_ = std::move(methods);
    }
    void SetPropertyList(std::vector<const TS::IProperty*> properties) {
        properties_ = std::move(properties);
    }

    std::vector<const TS::IMethod*> GetConstructors(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions) const override {
        std::vector<const TS::IMethod*> result;
        for (const TS::IMethod* m : ctors_)
            if (!filter || filter(m)) result.push_back(m);
        return result;
    }
    std::vector<const TS::IMethod*> GetMethods(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions) const override {
        std::vector<const TS::IMethod*> result;
        for (const TS::IMethod* m : methods_)
            if (!filter || filter(m)) result.push_back(m);
        return result;
    }
    std::vector<const TS::IProperty*> GetProperties(
        std::function<bool(const TS::IProperty*)> filter,
        TS::GetMemberOptions) const override {
        std::vector<const TS::IProperty*> result;
        for (const TS::IProperty* p : properties_)
            if (!filter || filter(p)) result.push_back(p);
        return result;
    }

private:
    std::vector<const TS::IMethod*> ctors_;
    std::vector<const TS::IMethod*> methods_;
    std::vector<const TS::IProperty*> properties_;
};

// The C# `int[] arr = ...; arr[^1]` fixture type system: the System.Index
// stub (the 2-arg ctor, the op_Implicit conversion and the GetOffset method
// the C# IndexMethods scan and MatchGetOffset consult) over a LookupStubs
// compilation, with the container's Length/get_Item members for the Run
// driver's CSharpWillGenerateIndexer scan.
struct IndexRangeFixture {
    TS::TestSupport::LookupCompilation compilation;
    std::shared_ptr<MemberListTypeDefinition> int32Def;
    std::shared_ptr<MemberListTypeDefinition> boolDef;
    std::shared_ptr<MemberListTypeDefinition> indexDef;
    std::shared_ptr<MemberListTypeDefinition> rangeDef;
    std::shared_ptr<MemberListTypeDefinition> containerDef;
    std::shared_ptr<Impl_::FakeMethod> indexCtor;
    std::shared_ptr<Impl_::FakeMethod> indexImplicitConv;
    std::shared_ptr<Impl_::FakeMethod> getOffsetMethod;
    std::shared_ptr<Impl_::FakeMethod> rangeCtor;
    std::shared_ptr<Impl_::FakeProperty> lengthProperty;
    std::shared_ptr<Impl_::FakeMethod> lengthGetter;
    std::shared_ptr<Impl_::FakeProperty> itemProperty;
    std::shared_ptr<Impl_::FakeMethod> getItemMethod;
    ILVariablePtr arr;
    ILVariablePtr len;
    ILVariablePtr off;
    ILVariablePtr result;

    IndexRangeFixture()
    {
        int32Def = std::make_shared<MemberListTypeDefinition>(
            "Int32", "System", TS::FullTypeName("System.Int32"),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation,
            nullptr, TS::KnownTypeCode::Int32);
        boolDef = std::make_shared<MemberListTypeDefinition>(
            "Boolean", "System", TS::FullTypeName("System.Boolean"),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation,
            nullptr, TS::KnownTypeCode::Boolean);
        indexDef = std::make_shared<MemberListTypeDefinition>(
            "Index", "System", TS::FullTypeName("System.Index"),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation,
            nullptr, TS::KnownTypeCode::Index);
        rangeDef = std::make_shared<MemberListTypeDefinition>(
            "Range", "System", TS::FullTypeName("System.Range"),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation,
            nullptr, TS::KnownTypeCode::Range);
        containerDef = std::make_shared<MemberListTypeDefinition>(
            "Container", "Test", TS::FullTypeName("Test.Container"),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation,
            nullptr);

        indexCtor = std::make_shared<Impl_::FakeMethod>(
            compilation, TS::SymbolKind::Constructor);
        indexCtor->SetName(".ctor");
        indexCtor->SetDeclaringType(indexDef);
        indexCtor->SetParameters({std::make_shared<const Impl_::DefaultParameter>(
                                      int32Def, "value"),
                                  std::make_shared<const Impl_::DefaultParameter>(
                                      boolDef, "fromEnd")});
        // The operator kind drives the C# `m.IsOperator` filter (the
        // FakeMethod ctor's SymbolKind::Operator arm).
        indexImplicitConv = std::make_shared<Impl_::FakeMethod>(
            compilation, TS::SymbolKind::Operator);
        indexImplicitConv->SetName("op_Implicit");
        indexImplicitConv->SetDeclaringType(indexDef);
        indexImplicitConv->SetIsStatic(true);
        indexImplicitConv->SetParameters(
            {std::make_shared<const Impl_::DefaultParameter>(int32Def, "value")});
        getOffsetMethod = std::make_shared<Impl_::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        getOffsetMethod->SetName("GetOffset");
        getOffsetMethod->SetDeclaringType(indexDef);
        getOffsetMethod->SetIsStatic(true);
        getOffsetMethod->SetParameters(
            {std::make_shared<const Impl_::DefaultParameter>(int32Def, "offset"),
             std::make_shared<const Impl_::DefaultParameter>(int32Def, "length")});
        indexDef->SetCtors({indexCtor.get()});
        indexDef->SetMethodList({indexImplicitConv.get(), getOffsetMethod.get()});

        rangeCtor = std::make_shared<Impl_::FakeMethod>(
            compilation, TS::SymbolKind::Constructor);
        rangeCtor->SetName(".ctor");
        rangeCtor->SetDeclaringType(rangeDef);
        rangeCtor->SetParameters(
            {std::make_shared<const Impl_::DefaultParameter>(indexDef, "start"),
             std::make_shared<const Impl_::DefaultParameter>(indexDef, "end")});
        rangeDef->SetCtors({rangeCtor.get()});

        // The container's Length property and int-parameter indexer.
        lengthProperty = std::make_shared<Impl_::FakeProperty>(compilation);
        lengthProperty->SetName("Length");
        lengthProperty->SetDeclaringType(containerDef);
        lengthProperty->SetReturnType(int32Def);
        lengthGetter = std::make_shared<Impl_::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        lengthGetter->SetName("get_Length");
        lengthGetter->SetDeclaringType(containerDef);
        lengthGetter->SetReturnType(int32Def);
        lengthGetter->SetAccessorOwner(
            static_cast<const TS::IProperty*>(lengthProperty.get()));
        lengthGetter->SetAccessorKind(
            TS::MethodSemanticsAttributes::Getter);
        itemProperty = std::make_shared<Impl_::FakeProperty>(compilation);
        itemProperty->SetName("Item");
        itemProperty->SetDeclaringType(containerDef);
        itemProperty->SetIsIndexer(true);
        itemProperty->SetParameters(
            {std::make_shared<const Impl_::DefaultParameter>(int32Def, "index")});
        itemProperty->SetReturnType(int32Def);
        getItemMethod = std::make_shared<Impl_::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        getItemMethod->SetName("get_Item");
        getItemMethod->SetDeclaringType(containerDef);
        getItemMethod->SetReturnType(int32Def);
        getItemMethod->SetAccessorOwner(
            static_cast<const TS::IProperty*>(itemProperty.get()));
        getItemMethod->SetAccessorKind(TS::MethodSemanticsAttributes::Getter);
        getItemMethod->SetParameters(
            {std::make_shared<const Impl_::DefaultParameter>(int32Def, "index")});
        containerDef->SetPropertyList({lengthProperty.get(), itemProperty.get()});
        // The C# `context.TypeSystem` lookup (the C# IndexMethods scan runs
        // `compilation.FindType(KnownTypeCode.Index/Range)`).
        compilation.RegisterKnownType(TS::KnownTypeCode::Index, indexDef.get());
        compilation.RegisterKnownType(TS::KnownTypeCode::Range, rangeDef.get());

        // The variables: the container is the reader's stack-slot shape; the
        // len/off variables carry the Int32 stub type (StackType I4).
        arr = std::make_shared<IL::ILVariable>(
            IL::VariableKind::StackSlot,
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
        arr->Name = "arr";
        len = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, int32Def);
        len->Name = "len";
        off = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, int32Def);
        off->Name = "off";
        result = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, int32Def);
        result->Name = "result";
        // The usage counts the C# reader/tree maintains (the port keeps them
        // static): the length is loaded once (inside GetOffset), the offset is
        // loaded once (inside get_Item), each store is a single definition.
        len->StoreCount = 1;
        len->LoadCount = 1;
        off->StoreCount = 1;
        off->LoadCount = 1;
    }
};

} // namespace

// The HandleLdElema `array[^1]` case: ldelema T(ldloc arr,
// binary.sub.i4(ldlen.i4(ldloc arr), ldc 1)) is rewritten to
// withsystemindex.ldelema T(ldloc arr, newobj System.Index(ldc 1, fromEnd)).
TEST(IndexRangeTransformTest, HandleLdElemaFromEndIndex)
{
    IndexRangeFixture fixture;
    // The ldelema: the index is the sub(len, 1) expression.
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::BinaryNumericInstruction>(
        std::make_unique<IL::LdLen>(IL::StackType::I4,
                                    std::make_unique<IL::LdLoc>(fixture.arr)),
        std::make_unique<IL::LdcI4>(1), IL::BinaryNumericOperator::Sub,
        false, TS::Sign::None));
    auto ldelema = std::make_unique<IL::LdElema>(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32),
        std::make_unique<IL::LdLoc>(fixture.arr), std::move(indices));

    IL::ILTransformContext ctx;
    ctx.TypeSystem = &fixture.compilation;
    ctx.Settings.Ranges = true;
    bool handled = IL::IndexRangeTransform::HandleLdElema(*ldelema, ctx);

    ASSERT_TRUE(handled);
    EXPECT_TRUE(ldelema->WithSystemIndex);
    // The index is now the System.Index construction over the original right
    // operand (the C# MakeIndex(FromEnd) -> newobj System.Index(ldc 1, true)).
    ASSERT_EQ(ldelema->Indices.size(), 1u);
    auto* newIndex = dynamic_cast<IL::Call*>(ldelema->Indices[0].get());
    ASSERT_NE(newIndex, nullptr);
    EXPECT_TRUE(newIndex->IsNewObj);
    ASSERT_EQ(newIndex->Arguments.size(), 2u);
    auto* value = dynamic_cast<IL::LdcI4*>(newIndex->Arguments[0].get());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->Value, 1);
    auto* fromEnd = dynamic_cast<IL::LdcI4*>(newIndex->Arguments[1].get());
    ASSERT_NE(fromEnd, nullptr);
    EXPECT_EQ(fromEnd->Value, 1);
    EXPECT_EQ(newIndex->Method.get(), fixture.indexCtor.get());
}

// The Run driver's indexing case: stloc len(get_Length(ldloc arr)) +
// stloc off(GetOffset(1, ldloc len)) + stloc result(get_Item(ldloc arr,
// ldloc off)) is folded into a SyntheticRangeIndexAccessor call over the
// container with a System.Index argument, and the len/off stores disappear.
TEST(IndexRangeTransformTest, RunFoldsGetOffsetIntoSyntheticIndexer)
{
    IndexRangeFixture fixture;
    auto fn = std::make_unique<IL::ILFunction>();
    fn->Body = std::make_unique<IL::BlockContainer>();
    fn->Body->Kind = IL::ContainerKind::Normal;
    fn->Body->Parent = fn.get();
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    {
        auto getLength = std::make_unique<IL::Call>(fixture.lengthGetter);
        getLength->IsInstanceCall = true;
        getLength->AddArg(std::make_unique<IL::LdLoc>(fixture.arr));
        block->Add(std::make_unique<IL::StLoc>(fixture.len, std::move(getLength)));
    }
    {
        auto getOffset = std::make_unique<IL::Call>(fixture.getOffsetMethod);
        getOffset->IsInstanceCall = false;
        getOffset->AddArg(std::make_unique<IL::LdcI4>(1));
        getOffset->AddArg(std::make_unique<IL::LdLoc>(fixture.len));
        block->Add(std::make_unique<IL::StLoc>(fixture.off, std::move(getOffset)));
    }
    {
        auto getItem = std::make_unique<IL::Call>(fixture.getItemMethod);
        getItem->IsInstanceCall = true;
        getItem->AddArg(std::make_unique<IL::LdLoc>(fixture.arr));
        getItem->AddArg(std::make_unique<IL::LdLoc>(fixture.off));
        block->Add(std::make_unique<IL::StLoc>(fixture.result, std::move(getItem)));
    }
    block->SetFinal(std::make_unique<IL::Nop>());
    block->Parent = fn->Body.get();
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    IL::ILTransformContext ctx;
    ctx.TypeSystem = &fixture.compilation;
    ctx.Settings.Ranges = true;
    IL::IndexRangeTransform transform;
    IL::StatementTransformContext driverCtx(ctx, blockPtr);
    transform.Run(*blockPtr, 0, driverCtx);
    // The consumed len/off stores are gone; the statement at pos 0 is the
    // result store whose value now carries the synthetic accessor call.
    ASSERT_EQ(blockPtr->Instructions.size(), 1u);
    auto* store = dynamic_cast<IL::StLoc*>(blockPtr->Instructions[0].get());
    ASSERT_NE(store, nullptr);
    auto* newCall = dynamic_cast<IL::Call*>(store->Value.get());
    ASSERT_NE(newCall, nullptr);
    // The synthetic accessor wraps the original get_Item with the Index type;
    // the first argument is the container, the second the System.Index.
    ASSERT_EQ(newCall->Arguments.size(), 2u);
    auto* container = dynamic_cast<IL::LdLoc*>(newCall->Arguments[0].get());
    ASSERT_NE(container, nullptr);
    EXPECT_EQ(container->Variable.get(), fixture.arr.get());
    // The RefSystemIndex makeIndex path: the index becomes an ldobj of the
    // System.Index type over the original index load (the C#
    // `MakeIndex(IndexKind.RefSystemIndex)` -> `new LdObj(indexLoad,
    // specialMethods.IndexType)`).
    auto* indexArg = dynamic_cast<IL::LdObj*>(newCall->Arguments[1].get());
    ASSERT_NE(indexArg, nullptr);
    ASSERT_NE(indexArg->Type, nullptr);
    EXPECT_EQ(indexArg->Type->ReflectionName(), "System.Index");
    auto* value = dynamic_cast<IL::LdcI4*>(indexArg->Target.get());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->Value, 1);
    // The consumed len/off stores are gone: the block keeps only the result
    // store.
}

// ---- ExtendSlicing (the second-pass extension) ----------------------------
// A partially-transformed slicing pattern: the block's statement at pos is a
// bare expression instruction (not a stloc), so the primary pattern bails and
// ExtendSlicing merges the NewObj Range(GetOffset(len), GetOffset(len))
// inside the SyntheticRangeIndexAccessor call into a direct range
// construction.

TEST(IndexRangeTransformTest, ExtendSlicingMergesThePartialPattern)
{
    IndexRangeFixture fixture;
    // The slicing wrapper asserts a 2-parameter underlying method (the C#
    // `get_Item(this, index)` shape); the fixture's getItemMethod carries the
    // index parameter only, so the test builds a container-parameter twin.
    auto sliceItem = std::make_shared<Impl_::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    sliceItem->SetName("get_Item");
    sliceItem->SetDeclaringType(fixture.containerDef);
    sliceItem->SetReturnType(fixture.int32Def);
    sliceItem->SetParameters(
        {std::make_shared<const Impl_::DefaultParameter>(fixture.containerDef, "this"),
         std::make_shared<const Impl_::DefaultParameter>(fixture.int32Def, "index")});
    auto synthetic =
        std::make_shared<Impl_::SyntheticRangeIndexAccessor>(
            sliceItem, fixture.rangeDef, true);
    auto len2 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                 fixture.int32Def);
    len2->Name = "len";
    auto fn = std::make_unique<IL::ILFunction>();
    fn->Body = std::make_unique<IL::BlockContainer>();
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Parent = fn->Body.get();

    // stloc len(call get_Length(ldloc arr))
    {
        auto getLength = std::make_unique<IL::Call>(fixture.lengthGetter);
        getLength->IsInstanceCall = true;
        getLength->AddArg(std::make_unique<IL::LdLoc>(fixture.arr));
        block->Add(std::make_unique<IL::StLoc>(len2, std::move(getLength)));
    }
    // The Range ctor arguments: op_Implicit(GetOffset(n, ldloc len)) x2.
    auto makeArg = [&](int index) {
        auto getOffset = std::make_unique<IL::Call>(fixture.getOffsetMethod);
        getOffset->AddArg(std::make_unique<IL::LdcI4>(index));
        getOffset->AddArg(std::make_unique<IL::LdLoc>(len2));
        auto implicitConv =
            std::make_unique<IL::Call>(fixture.indexImplicitConv);
        implicitConv->AddArg(std::move(getOffset));
        return implicitConv;
    };
    auto rangeCtor = std::make_unique<IL::Call>(fixture.rangeCtor, true);
    rangeCtor->AddArg(makeArg(1));
    rangeCtor->AddArg(makeArg(2));
    // The synthetic slicing call (a bare expression instruction at pos 1).
    auto sliceCall = std::make_unique<IL::Call>(synthetic);
    sliceCall->AddArg(std::make_unique<IL::LdLoc>(fixture.arr));
    sliceCall->AddArg(std::move(rangeCtor));
    block->Add(std::move(sliceCall));
    block->SetFinal(std::make_unique<IL::Nop>());
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));
    // The usage counts the transform consults (len: stored once, loaded by
    // the two GetOffset calls).
    len2->StoreCount = 1;
    len2->LoadCount = 2;
    IL::ComputeVariableUsage(*fn);

    IL::ILTransformContext ctx;
    ctx.TypeSystem = &fixture.compilation;
    ctx.Settings.Ranges = true;
    IL::IndexRangeTransform transform;
    IL::StatementTransformContext driverCtx(ctx, blockPtr);
    transform.Run(*blockPtr, 1, driverCtx);

    // The ExtendSlicing pass replaced the NewObj Range(...) inside the
    // synthetic call: the block's shape is intact and the Range ctor's
    // arguments no longer carry the GetOffset/len indirection (the merged
    // range construction reads the offset loads directly).
    ASSERT_EQ(blockPtr->Instructions.size(), 2u);
    auto* slice = dynamic_cast<IL::Call*>(blockPtr->Instructions[1].get());
    ASSERT_NE(slice, nullptr);
    ASSERT_EQ(slice->Arguments.size(), 2u);
    auto* newRange = dynamic_cast<IL::Call*>(slice->Arguments[1].get());
    ASSERT_NE(newRange, nullptr) << "the Range ctor survives the merge";
    EXPECT_EQ(newRange->Arguments.size(), 2u);
}
