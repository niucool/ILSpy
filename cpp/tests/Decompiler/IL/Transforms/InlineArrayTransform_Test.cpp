// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
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

// Tests for InlineArrayTransform (the port of
// ICSharpCode.Decompiler/IL/Transforms/InlineArrayTransform.cs): the
// InlineArrayElementRef fold into ldelema.inlinearray, the
// InlineArrayFirstElementRef fold (a synthesized ldc.i4 0 index), and the
// abort cases (an out-of-bounds index is rejected so the helper call stays).

#include "Decompiler/IL/Transforms/InlineArrayTransform.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdElemaInlineArray.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"  // KnownType lives here
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The InlineArrayTestAttribute: the IAttribute stub returning the fixed
// `[InlineArray(N)]` length argument (the TestAttribute shape in
// MemberResolveResult_Test.cpp).
class InlineArrayTestAttribute : public TS::IAttribute {
public:
    explicit InlineArrayTestAttribute(int length) : length_(length) {}
    const TS::IType& AttributeType() const override { return attrType_; }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
        return {TS::CustomAttributeTypedArgument(
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32),
            std::any(length_))};
    }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
        return {};
    }
private:
    int length_;
    TS::KnownType attrType_{TS::KnownTypeCode::Object};
};

// An [InlineArray(N)] struct type definition: a LookupTypeDefinition whose
// GetAttribute surfaces the InlineArray attribute (the base's returns null
// unconditionally; the overrides keep the rest of the stub behavior).
class InlineArrayTypeDefinition : public TS::TestSupport::LookupTypeDefinition {
public:
    InlineArrayTypeDefinition(int length, const TS::ICompilation& compilation)
        : TS::TestSupport::LookupTypeDefinition(
              "Buffer8", std::string(),
              TS::FullTypeName("Buffer8"), TS::TypeKind::Struct,
              TS::Accessibility::Public, compilation, nullptr),
          attr_(length) {}

    using LookupTypeDefinition::GetAttribute;
    const TS::IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        if (attribute == TS::KnownAttribute::InlineArray) return &attr_;
        return nullptr;
    }

private:
    mutable InlineArrayTestAttribute attr_;
};

} // namespace

using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

namespace {

// The fixture: the minimal-corlib compilation (the
// TransformCollectionAndObjectInitializers_Test precedent) + the
// [InlineArray(8)] buffer type + the `InlineArrayElementRef` /
// `InlineArrayFirstElementRef` helper stubs on
// `<PrivateImplementationDetails>`.
// The `<PrivateImplementationDetails>.InlineArrayElementRef(ReadOnly)(T&, int)`
// / `InlineArrayFirstElementRef(ReadOnly)(T&)` helper stub.
class HelperMethodStub : public TS::IMethod {
public:
    HelperMethodStub(std::string name, TS::ITypePtr bufferType,
                     std::shared_ptr<TS::ByReferenceType> bufferRef,
                     TS::ITypePtr indexType)
        : name_(std::move(name)), bufferType_(std::move(bufferType)),
          bufferRef_(std::move(bufferRef)), indexType_(std::move(indexType)) {
        declaringType_ = std::make_shared<TS::SimpleType>(
            TS::TopLevelTypeName(std::string(), "<PrivateImplementationDetails>"));
    }

    // --- ISymbol / INamedElement ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return std::string(); }
    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("HelperMethodStub::Compilation");
    }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override {
        std::vector<const TS::IParameter*> result;
        for (const auto& p : params_) result.push_back(p.get());
        return result;
    }
    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *bufferType_; }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMethod* Specialize(
        const TS::TypeParameterSubstitution* substitution) const override {
        (void)substitution;
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }
    // --- IMethod ---
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Internal;
    }
    bool IsStatic() const override { return true; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override { return declaringType_; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override {
        return {bufferType_, std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32)};
    }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }

    // Late-bound parameters (the fixture builds them after construction).
    void InitParams() {
        DefaultParameterFactory factory;
        params_.push_back(factory.Make(bufferRef_, std::string("buffer")));
        if (indexType_ != nullptr) {
            params_.push_back(factory.Make(indexType_, std::string("index")));
        }
    }

    struct DefaultParameterFactory {
        std::shared_ptr<TS::Implementation::DefaultParameter> Make(
            TS::ITypePtr type, std::string name) {
            return std::make_shared<TS::Implementation::DefaultParameter>(
                std::move(type), std::move(name));
        }
    };

    std::string name_;
    TS::ITypePtr declaringType_;
    TS::ITypePtr bufferType_;
    std::shared_ptr<TS::ByReferenceType> bufferRef_;
    TS::ITypePtr indexType_;
    std::vector<std::shared_ptr<const TS::IParameter>> params_;
};

struct InlineArrayFixture {
    TS::SimpleCompilation compilation{TS::Implementation::MinimalCorlib::Instance(), {}};
    std::shared_ptr<InlineArrayTypeDefinition> bufferType;
    TS::ITypePtr int32Type;

    InlineArrayFixture()
        : bufferType(std::make_shared<InlineArrayTypeDefinition>(8, compilation)) {
        // The compilation-driven Int32 (the minimal KnownType(Int32) has a
        // null GetDefinition, so IsKnownType fails on it).
        int32Type = TS::ITypePtr(
            std::shared_ptr<TS::IType>(),
            const_cast<TS::IType*>(&compilation.FindType(TS::KnownTypeCode::Int32)));
    }

    // Build the helper method for the named arm (the InitParams call needs
    // the ByReferenceType parameter over the buffer type).
    void InitParamsFor(const std::string& name, bool withIndex) {
        auto bufferRef = std::make_shared<TS::ByReferenceType>(
            TS::ITypePtr(bufferType));
        helperMethod = std::make_shared<HelperMethodStub>(
            name, TS::ITypePtr(bufferType), std::move(bufferRef),
            withIndex ? int32Type : nullptr);
        helperMethod->InitParams();
    }

    // The helper-call node: `call Method(addressof(ldloc buf), [index])`.
    std::unique_ptr<IL::Call> MakeHelperCall() {
        auto ldloc = std::make_unique<IL::LdLoc>(buf);
        auto addressOf = std::make_unique<IL::AddressOf>(std::move(ldloc),
                                                         bufferType);
        auto call = std::make_unique<IL::Call>(helperMethod);
        call->Arguments.push_back(std::move(addressOf));
        if (withIndex >= 0) call->Arguments.push_back(std::make_unique<IL::LdcI4>(withIndex));
        return call;
    }

    // The index argument for the last InitParamsFor call (-1 = no index;
    // mirrored into the call shape).
    int withIndex = -1;
    ILVariablePtr buf;

    std::shared_ptr<HelperMethodStub> helperMethod;
};

} // namespace

// The ElementRef fold: `call InlineArrayElementRef(addr, index)` becomes
// `ldelema.inlinearray[Buffer8](addr, index)`.
TEST(InlineArrayTransformTest, ElementRefCallBecomesLdElemaInlineArray)
{
    InlineArrayFixture f;
    auto buf = MakeLocal("buf", f.bufferType);
    f.buf = buf;
    f.withIndex = 3;
    f.InitParamsFor("InlineArrayElementRef", true);
    auto call = f.MakeHelperCall();
    IL::ILFunction fn;
    fn.Variables.push_back(buf);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Call* callPtr = call.get();
    block->Add(std::move(call));
    block->Parent = &fn;
    IL::Block* blockPtr = block.get();
    fn.Body = std::make_unique<IL::BlockContainer>();
    fn.Body->Parent = &fn;
    fn.Body->AddBlock(std::move(block));

    IL::ILTransformContext ctx;
    ctx.Settings.InlineArrays = true;
    IL::StatementTransformContext driverCtx(ctx, blockPtr);
    EXPECT_TRUE(IL::InlineArrayTransform::RunOnExpression(callPtr, driverCtx));

    // The folded node replaced the call in the block.
    ASSERT_EQ(blockPtr->Instructions.size(), 1u);
    ASSERT_EQ(blockPtr->Instructions[0]->Op, IL::OpCode::LdElemaInlineArray);
    const auto* folded = static_cast<IL::LdElemaInlineArray*>(
        blockPtr->Instructions[0].get());
    EXPECT_EQ(folded->Indices.size(), 1u);
    EXPECT_EQ(folded->Type->ReflectionName(), "Buffer8");
    EXPECT_FALSE(folded->IsReadOnly);
}

// The FirstElementRef fold: the index is the synthesized `ldc.i4 0`.
TEST(InlineArrayTransformTest, FirstElementRefFoldsWithIndexZero)
{
    InlineArrayFixture f;
    auto buf = MakeLocal("buf", f.bufferType);
    f.buf = buf;
    f.InitParamsFor("InlineArrayFirstElementRef", false);
    auto call = f.MakeHelperCall();
    IL::ILFunction fn;
    fn.Variables.push_back(buf);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Call* callPtr = call.get();
    block->Add(std::move(call));
    block->Parent = &fn;
    IL::Block* blockPtr = block.get();
    fn.Body = std::make_unique<IL::BlockContainer>();
    fn.Body->Parent = &fn;
    fn.Body->AddBlock(std::move(block));

    IL::ILTransformContext ctx;
    ctx.Settings.InlineArrays = true;
    IL::StatementTransformContext driverCtx(ctx, blockPtr);
    EXPECT_TRUE(IL::InlineArrayTransform::RunOnExpression(callPtr, driverCtx));

    ASSERT_EQ(blockPtr->Instructions[0]->Op, IL::OpCode::LdElemaInlineArray);
    const auto* folded = static_cast<IL::LdElemaInlineArray*>(
        blockPtr->Instructions[0].get());
    const auto* index = dynamic_cast<const IL::LdcI4*>(folded->Indices[0].get());
    ASSERT_NE(index, nullptr) << "the synthesized index is ldc.i4 0";
    EXPECT_EQ(index->Value, 0);
}

// An out-of-bounds index aborts the fold (the C# bound
// `indexValue < 0 || indexValue >= inlineArrayType.GetInlineArrayLength()`).
TEST(InlineArrayTransformTest, OutOfBoundsIndexAbortsTheFold)
{
    InlineArrayFixture f;
    auto buf = MakeLocal("buf", f.bufferType);
    f.buf = buf;
    f.withIndex = 8;  // == the length
    f.InitParamsFor("InlineArrayElementRef", true);
    auto call = f.MakeHelperCall();
    IL::ILFunction fn;
    fn.Variables.push_back(buf);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Call* callPtr = call.get();
    block->Add(std::move(call));
    block->Parent = &fn;
    IL::Block* blockPtr = block.get();
    fn.Body = std::make_unique<IL::BlockContainer>();
    fn.Body->Parent = &fn;
    fn.Body->AddBlock(std::move(block));

    IL::ILTransformContext ctx;
    ctx.Settings.InlineArrays = true;
    IL::StatementTransformContext driverCtx(ctx, blockPtr);
    EXPECT_FALSE(IL::InlineArrayTransform::RunOnExpression(callPtr, driverCtx));
    EXPECT_EQ(blockPtr->Instructions[0]->Op, IL::OpCode::Call)
        << "the helper call stays";
}
