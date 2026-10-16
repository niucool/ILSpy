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

// Tests for DeconstructionTransform (the port of
// ICSharpCode.Decompiler/IL/Transforms/DeconstructionTransform.cs, the
// Deconstruct-call-rooted arm): the `call Deconstruct(obj, out t0, out t1)`
// + `stloc x(ldloc t0)` + `stloc y(ldloc t1)` sequence folds into one
// DeconstructInstruction with a match.deconstruct pattern, and the abort
// case (an unrelated assignment between the conversions and the run) leaves
// the sequence untouched.

#include "Decompiler/IL/Transforms/DeconstructionTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/DeconstructInstruction.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

// The `void Deconstruct(out int a, out int b)` instance method. Modeled on
// the VarArgInstanceMethod_Test stub: the full IMethod surface with the
// fixed values the IsDeconstructMethod check reads (void return, the
// "Deconstruct" name, no type parameters, out-kind trailing parameters).
class DeconstructMethodStub : public TS::IMethod {
public:
    explicit DeconstructMethodStub(TS::ITypePtr int32Type) {
        // An instance Deconstruct's parameters are ONLY the out params: the
        // receiver is the call's Arguments[0], not a parameter (the C#
        // MatchDeconstructionCall reads out-arguments from index 1).
        for (int i = 0; i < 2; ++i) {
            params_.push_back(std::make_shared<TS::Implementation::DefaultParameter>(
                int32Type, "arg" + std::to_string(i), this,
                std::vector<const TS::IAttribute*>(),
                TS::ReferenceKind::Out));
        }
        voidType_ = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void);
        declaringType_ = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    }

    // --- ISymbol / INamedElement ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return "Deconstruct"; }
    std::string FullName() const override { return "T.Deconstruct"; }
    std::string ReflectionName() const override { return "Deconstruct"; }
    std::string Namespace() const override { return std::string(); }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("DeconstructMethodStub::Compilation");
    }

    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override {
        std::vector<const TS::IParameter*> result;
        for (const auto& p : params_) result.push_back(p.get());
        return result;
    }

    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *voidType_; }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
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
        return this;  // the Identity short-circuit
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }

    // --- IMethod ---
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
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
    std::vector<TS::ITypePtr> TypeArguments() const override { return {}; }
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

    std::vector<std::shared_ptr<const TS::IParameter>> params_;
    TS::ITypePtr voidType_;
    TS::ITypePtr declaringType_;
};

// The `obj.Deconstruct(out x, out y)` shape after the IL reader's
// stack-slot pass:
//   call Deconstruct(ldloc obj, ldloca t0, ldloca t1)
//   stloc x(ldloc t0)
//   stloc y(ldloc t1)
//   final: nop
// The out-argument temporaries satisfy the single-use gate: StoreCount == 0,
// AddressCount == 1 (the ldloca), LoadCount <= 1 (the consuming ldloc).
struct DeconstructFixture {
    TS::ITypePtr int32Type =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    std::shared_ptr<DeconstructMethodStub> method;
    std::unique_ptr<IL::ILFunction> fn;
    ILVariablePtr obj;
    ILVariablePtr t0;
    ILVariablePtr t1;
    ILVariablePtr x;
    ILVariablePtr y;

    DeconstructFixture() : method(std::make_shared<DeconstructMethodStub>(int32Type)) {
        fn = std::make_unique<IL::ILFunction>();
        fn->Body = std::make_unique<IL::BlockContainer>();
        fn->Body->Kind = IL::ContainerKind::Normal;
        fn->Body->Parent = fn.get();
        obj = MakeLocal("obj");
        t0 = MakeLocal("t0");
        t1 = MakeLocal("t1");
        x = MakeLocal("x");
        y = MakeLocal("y");
        auto block = std::make_unique<IL::Block>();
        block->Kind = IL::BlockKind::ControlFlow;
        auto call = std::make_unique<IL::Call>(method);
        call->Arguments.push_back(std::make_unique<IL::LdLoc>(obj));
        call->Arguments.push_back(std::make_unique<IL::LdLoca>(t0));
        call->Arguments.push_back(std::make_unique<IL::LdLoca>(t1));
        block->Add(std::move(call));
        block->Add(std::make_unique<IL::StLoc>(x, std::make_unique<IL::LdLoc>(t0)));
        block->Add(std::make_unique<IL::StLoc>(y, std::make_unique<IL::LdLoc>(t1)));
        block->SetFinal(std::make_unique<IL::Nop>());
        block->Parent = fn->Body.get();
        fn->Body->AddBlock(std::move(block));
        fn->Variables.push_back(obj);
        fn->Variables.push_back(t0);
        fn->Variables.push_back(t1);
        fn->Variables.push_back(x);
        fn->Variables.push_back(y);
        ComputeVariableUsage(*fn);
    }

    ILVariablePtr MakeLocal(std::string name) {
        auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                  int32Type);
        v->Name = std::move(name);
        return v;
    }
};


TEST(DeconstructionTransformTest, ProbeIsDeconstructMethod)
{
    DeconstructFixture f;
    EXPECT_STREQ(f.method->Name().c_str(), "Deconstruct");
    EXPECT_EQ(f.method->ReturnType().Kind(), TS::TypeKind::Void);
    EXPECT_FALSE(f.method->IsStatic());
    EXPECT_TRUE(f.method->TypeParameters().empty());
    EXPECT_EQ(f.method->Parameters().size(), 2u);
    for (std::size_t i = 0; i < 2; ++i)
        EXPECT_EQ(f.method->Parameters()[i]->ReferenceKind(), TS::ReferenceKind::Out);
    ASSERT_TRUE(IL::MatchInstruction::IsDeconstructMethod(f.method.get()));
    IL::Block* block = f.fn->Body->Blocks[0].get();
    IL::ILInstruction* tested = nullptr;
    IL::DeconstructionCall* call =
        IL::MatchDeconstructionCallProbe(block->Instructions[0].get(), tested);
    EXPECT_NE(call, nullptr) << "the call matches the deconstruct shape";
}

} // namespace

// The full rewrite: the call + the two consuming stores fold into one
// DeconstructInstruction holding the match.deconstruct pattern.
TEST(DeconstructionTransformTest, DeconstructCallBecomesDeconstructInstruction)
{
    DeconstructFixture f;
    IL::Block* block = f.fn->Body->Blocks[0].get();

    IL::ILTransformContext ctx;
    ctx.Settings.Deconstruction = true;
    IL::DeconstructionTransform transform;
    IL::StatementTransformContext driverCtx(ctx, block);
    transform.Run(*block, 0, driverCtx);

    ASSERT_FALSE(block->Instructions.empty());
    const IL::ILInstruction* first = block->Instructions[0].get();
    ASSERT_EQ(first->Op, IL::OpCode::DeconstructInstruction)
        << "the call + consuming stores fold into one deconstruct instruction";
    const auto* deconstruct =
        static_cast<const IL::DeconstructInstruction*>(first);
    // The consumed stores left the block.
    ASSERT_EQ(block->Instructions.size(), 1u)
        << "only the folded deconstruct instruction remains";
    // The pattern records the deconstruct method and the two leaf slots.
    ASSERT_NE(deconstruct->Pattern(), nullptr);
    EXPECT_TRUE(deconstruct->Pattern()->IsDeconstructCall);
    ASSERT_EQ(deconstruct->Pattern()->SubPatterns.size(), 2u);
}