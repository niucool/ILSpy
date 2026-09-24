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

// Tests for SwitchOnStringTransform (the port of
// ICSharpCode.Decompiler/IL/Transforms/SwitchOnStringTransform.cs): the
// modern Roslyn switch(ComputeStringHash(s)) arm over a hand-built block
// graph, plus the cascading-if arm and its rejection gates.

#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/Util/LongSet.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace Util = ::ILSpy::Decompiler::Util;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeLocal(std::string name, TS::ITypePtr type = nullptr)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

// A stub IMethod for the compiler-generated hash/equality helpers (the
// NamedMethodStub shape from the TransformExpressionTrees fixture).
class NamedMethodStub : public TS::IMethod {
public:
    NamedMethodStub(std::string ns, std::string typeName, std::string methodName)
        : ns_(std::move(ns)), typeName_(std::move(typeName)),
          name_(std::move(methodName)) {
        declaringType_ = std::make_shared<TS::SimpleType>(
            TS::TopLevelTypeName(ns_, typeName_));
        voidType_ = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void);
    }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return ns_ + "." + typeName_ + "." + name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return ns_; }
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("NamedMethodStub::Compilation");
    }
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
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
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
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

    std::string ns_;
    std::string typeName_;
    std::string name_;
    TS::ITypePtr declaringType_;
    TS::ITypePtr voidType_;
};


// ---- The Roslyn switch(ComputeStringHash(s)) arm --------------------------

using MakeSwitchFn = std::function<std::unique_ptr<IL::SwitchInstruction>()>;

// Build the Roslyn shape in a fresh ILFunction:
//   head: [stloc hash(call ComputeStringHash(ldloc s)), switch (ldloc hash)]
//   caseA/caseB: [if op_Equality(ldloc s, value) br bodyX; br exit]
//   bodyA/bodyB/exit: leave container
// Returns {fn, head}.
struct RoslynFixture {
    std::unique_ptr<IL::ILFunction> fn;
    IL::Block* head = nullptr;
    IL::Block* caseA = nullptr;
    IL::Block* caseB = nullptr;
    ILVariablePtr s;
};

RoslynFixture BuildRoslynSwitch(int caseCount)
{
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    RoslynFixture fx;
    fx.fn = std::make_unique<IL::ILFunction>();
    auto container = std::make_unique<IL::BlockContainer>();
    fx.s = MakeLocal("s", stringType);
    auto hash = MakeLocal("hash");
    fx.fn->Variables.push_back(fx.s);
    fx.fn->Variables.push_back(hash);

    auto head = std::make_unique<IL::Block>();
    head->Kind = IL::BlockKind::ControlFlow;
    IL::Block* headPtr = head.get();

    // stloc hash(call ComputeStringHash(ldloc s)) -- Roslyn emits the hash
    // helper inside <PrivateImplementationDetails> (a CompilerGenerated
    // type); the port's stand-in gate checks that declaring type.
    auto hashCall = std::make_unique<IL::Call>(
        "<PrivateImplementationDetails>::ComputeStringHash");
    hashCall->DeclaringType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string(), std::string("<PrivateImplementationDetails>")));
    hashCall->Arguments.push_back(std::make_unique<IL::LdLoc>(fx.s));
    head->Add(std::make_unique<IL::StLoc>(hash, std::move(hashCall)));

    // body + exit blocks
    std::vector<IL::Block*> bodies;
    auto exit = std::make_unique<IL::Block>();
    exit->Kind = IL::BlockKind::ControlFlow;
    exit->Add(std::make_unique<IL::Leave>(container.get()));
    IL::Block* exitPtr = exit.get();
    container->Blocks.push_back(std::move(exit));
    for (int i = 0; i < caseCount; i++) {
        auto body = std::make_unique<IL::Block>();
        body->Kind = IL::BlockKind::ControlFlow;
        body->Add(std::make_unique<IL::Leave>(container.get()));
        bodies.push_back(body.get());
        container->Blocks.push_back(std::move(body));
    }

    // case heads: if (op_Equality(ldloc s, "vN")) br bodyN; br exit
    auto makeCaseHead = [&](const std::string& value, IL::Block* body) {
        auto b = std::make_unique<IL::Block>();
        b->Kind = IL::BlockKind::ControlFlow;
        auto eqCall = std::make_unique<IL::Call>("System.String::op_Equality");
        eqCall->DeclaringType =
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
        eqCall->Arguments.push_back(std::make_unique<IL::LdLoc>(fx.s));
        eqCall->Arguments.push_back(std::make_unique<IL::LdStr>(value));
        auto ifInst = std::make_unique<IL::IfInstruction>(
            std::move(eqCall), std::make_unique<IL::Branch>(body));
        b->Add(std::move(ifInst));
        b->Add(std::make_unique<IL::Branch>(exitPtr));
        container->Blocks.push_back(std::move(b));
        return container->Blocks.back().get();
    };
    std::vector<IL::Block*> caseHeads;
    for (int i = 0; i < caseCount; i++) {
        caseHeads.push_back(makeCaseHead("v" + std::to_string(i), bodies[i]));
    }

    // switch (ldloc hash) with per-case labels + default -> exit
    auto sw = std::make_unique<IL::SwitchInstruction>(
        std::make_unique<IL::LdLoc>(hash));
    for (int i = 0; i < caseCount; i++) {
        long long base = 1000 + i;
        auto section = std::make_unique<IL::SwitchSection>(
            Util::LongSet(Util::LongInterval(base, base + 1)));
        section->SetBody(std::make_unique<IL::Branch>(caseHeads[i]));
        sw->Sections.push_back(std::move(section));
    }
    // The default section: the compiled shape carries the inverted
    // complement of the used labels (the widest label set, which the C#
    // GetDefaultSection picks by label count).
    auto defaultSection = std::make_unique<IL::SwitchSection>(
        Util::LongSet(Util::LongInterval(0, 1000)).Invert());
    defaultSection->SetBody(std::make_unique<IL::Branch>(exitPtr));
    sw->Sections.push_back(std::move(defaultSection));
    head->Add(std::move(sw));

    container->Blocks.push_back(std::move(head));
    fx.head = headPtr;
    fx.fn->Body = std::move(container);
    IL::RecomputeIncomingEdgeCounts(*fx.fn);
    // The head is the container entry: the C# method-body entry block carries
    // an implicit incoming edge (the container's entry position), which the
    // port models by setting the count after the branch-based recompute.
    fx.head->IncomingEdgeCount = 1;
    IL::ComputeVariableUsage(*fx.fn);
    fx.caseA = caseHeads[0];
    fx.caseB = caseHeads[1];
    return fx;
}

TEST(SwitchOnStringTransformTest, RoslynSwitchOnStringConvertsToSwitch)
{
    RoslynFixture fx = BuildRoslynSwitch(2);
    IL::ILTransformContext ctx;
    ctx.Settings.SwitchStatementOnString = true;
    IL::SwitchOnStringTransform transform;
    transform.Run(*fx.fn, ctx);

    // The head's first instruction is now the new switch; the old hash-store
    // and the old switch are gone.
    ASSERT_NE(fx.head, nullptr);
    ASSERT_EQ(fx.head->Instructions.size(), 1u);
    auto* sw = dynamic_cast<IL::SwitchInstruction*>(fx.head->Instructions[0].get());
    ASSERT_NE(sw, nullptr) << "the hash-store folds into a SwitchInstruction";
    auto* stringToInt = dynamic_cast<IL::StringToInt*>(sw->Value.get());
    ASSERT_NE(stringToInt, nullptr);
    EXPECT_EQ(stringToInt->Map.size(), 2u);
    EXPECT_EQ(stringToInt->ExpectedType.get(), nullptr);
    // The value dispatches on the raw string local (the hash indirection is
    // removed).
    auto* ldloc = dynamic_cast<IL::LdLoc*>(stringToInt->Argument.get());
    ASSERT_NE(ldloc, nullptr);
    EXPECT_EQ(ldloc->Variable.get(), fx.s.get());
}

TEST(SwitchOnStringTransformTest, MismatchedCaseVariableIsRejected)
{
    // A switch whose case heads compare a DIFFERENT variable than the hash
    // input is not a switch-on-string shape; the transform leaves the block
    // untouched (the C# MatchStringEqualityComparison variable gate).
    RoslynFixture fx = BuildRoslynSwitch(2);
    auto other = MakeLocal("other", fx.s->Type);
    fx.fn->Variables.push_back(other);
    // Rebuild the case heads to compare `other` instead of the hash input.
    for (IL::Block* caseHead : {fx.caseA, fx.caseB}) {
        auto* ifInst = dynamic_cast<IL::IfInstruction*>(
            caseHead->Instructions[0].get());
        ASSERT_NE(ifInst, nullptr);
        auto* eqCall = dynamic_cast<IL::Call*>(ifInst->Condition.get());
        ASSERT_NE(eqCall, nullptr);
        auto* ldloc = static_cast<IL::LdLoc*>(eqCall->Arguments[0].get());
        ASSERT_NE(ldloc, nullptr);
        ldloc->Variable = other;
    }
    IL::ComputeVariableUsage(*fx.fn);

    IL::ILTransformContext ctx;
    ctx.Settings.SwitchStatementOnString = true;
    IL::SwitchOnStringTransform transform;
    transform.Run(*fx.fn, ctx);

    ASSERT_EQ(fx.head->Instructions.size(), 2u)
        << "the mismatched-variable shape stays untouched";
    EXPECT_EQ(fx.head->Instructions[0]->Op, IL::OpCode::StLoc);
}

} // namespace
