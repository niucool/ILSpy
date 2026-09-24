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
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
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
#include <optional>
#include <vector>
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


// ---- The C#1 string.IsInterned cascading-if arm ---------------------------

TEST(SwitchOnStringTransformTest, CSharp1IsInternedChainConvertsToSwitch)
{
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto temp = std::make_shared<IL::ILVariable>(IL::VariableKind::StackSlot,
                                                 stringType);
    temp->Name = "temp";
    auto switchValueVar = MakeLocal("s", stringType);
    auto switchValueVarCopy = MakeLocal("s2", stringType);
    auto fn = std::make_unique<IL::ILFunction>();
    auto container = std::make_unique<IL::BlockContainer>();
    fn->Variables.push_back(temp);
    fn->Variables.push_back(switchValueVar);
    fn->Variables.push_back(switchValueVarCopy);

    auto exit = std::make_unique<IL::Block>();
    exit->Kind = IL::BlockKind::ControlFlow;
    exit->Add(std::make_unique<IL::Leave>(container.get()));
    IL::Block* exitPtr = exit.get();
    container->Blocks.push_back(std::move(exit));

    auto bodyA = std::make_unique<IL::Block>();
    bodyA->Kind = IL::BlockKind::ControlFlow;
    bodyA->Add(std::make_unique<IL::Leave>(container.get()));
    IL::Block* bodyAPtr = bodyA.get();
    container->Blocks.push_back(std::move(bodyA));

    auto bodyB = std::make_unique<IL::Block>();
    bodyB->Kind = IL::BlockKind::ControlFlow;
    bodyB->Add(std::make_unique<IL::Leave>(container.get()));
    IL::Block* bodyBPtr = bodyB.get();
    container->Blocks.push_back(std::move(bodyB));

    // The IsInterned block: stloc copy(IsInterned(ldloc s));
    // if (comp(copy == ldstr "a")) br caseA; br caseHeader2.
    auto isInternedBlock = std::make_unique<IL::Block>();
    isInternedBlock->Kind = IL::BlockKind::ControlFlow;
    auto internedCall = std::make_unique<IL::Call>(
        "System.String::IsInterned");
    internedCall->Arguments.push_back(std::make_unique<IL::LdLoc>(switchValueVar));
    isInternedBlock->Add(
        std::make_unique<IL::StLoc>(switchValueVarCopy, std::move(internedCall)));
    auto eqA = std::make_unique<IL::Comp>(
        std::make_unique<IL::LdLoc>(switchValueVarCopy),
        std::make_unique<IL::LdStr>("a"), IL::ComparisonKind::Equality, false);
    auto ifA = std::make_unique<IL::IfInstruction>(
        std::move(eqA), std::make_unique<IL::Branch>(bodyAPtr));
    isInternedBlock->Add(std::move(ifA));
    isInternedBlock->Add(std::make_unique<IL::Branch>(nullptr));  // set below
    IL::Block* isInternedPtr = isInternedBlock.get();
    container->Blocks.push_back(std::move(isInternedBlock));

    // caseHeader2: if (comp(copy == ldstr "b")) br caseB; br exit.
    auto caseHeader2 = std::make_unique<IL::Block>();
    caseHeader2->Kind = IL::BlockKind::ControlFlow;
    auto eqB = std::make_unique<IL::Comp>(
        std::make_unique<IL::LdLoc>(switchValueVarCopy),
        std::make_unique<IL::LdStr>("b"), IL::ComparisonKind::Equality, false);
    auto ifB = std::make_unique<IL::IfInstruction>(
        std::move(eqB), std::make_unique<IL::Branch>(bodyBPtr));
    caseHeader2->Add(std::move(ifB));
    caseHeader2->Add(std::make_unique<IL::Branch>(exitPtr));
    IL::Block* caseHeader2Ptr = caseHeader2.get();
    container->Blocks.push_back(std::move(caseHeader2));

    // The switch block: stloc s(ldloc temp);
    // if (comp(ldloc temp == ldnull)) br exit; br isInterned.
    auto head = std::make_unique<IL::Block>();
    head->Kind = IL::BlockKind::ControlFlow;
    head->Add(std::make_unique<IL::StLoc>(
        switchValueVar, std::make_unique<IL::LdLoc>(temp)));
    auto nullComp = std::make_unique<IL::Comp>(
        std::make_unique<IL::LdLoc>(temp), std::make_unique<IL::LdNull>(),
        IL::ComparisonKind::Equality, false);
    head->Add(std::make_unique<IL::IfInstruction>(
        std::move(nullComp), std::make_unique<IL::Branch>(exitPtr)));
    head->Add(std::make_unique<IL::Branch>(isInternedPtr));
    IL::Block* headPtr = head.get();
    container->Blocks.push_back(std::move(head));

    // Wire the IsInterned block's trailing branch to caseHeader2.
    static_cast<IL::Branch*>(isInternedPtr->Instructions[2].get())
        ->TargetBlock = caseHeader2Ptr;
    // The default body of the eventual switch: caseHeader2 is the chain's
    // end target, matching the C# currentCaseBlock.

    fn->Body = std::move(container);
    IL::RecomputeIncomingEdgeCounts(*fn);
    // The head is the container entry: the implicit incoming edge is set
    // AFTER the branch-based recompute (which zeroes it).
    headPtr->IncomingEdgeCount = 1;
    IL::ComputeVariableUsage(*fn);

    IL::ILTransformContext ctx;
    ctx.Settings.SwitchStatementOnString = true;
    IL::SwitchOnStringTransform transform;
    transform.Run(*fn, ctx);

    // The head's if folds into a SwitchInstruction over a StringToInt of the
    // interned copy variable; the stloc and the br are gone.
    ASSERT_EQ(headPtr->Instructions.size(), 1u);
    auto* sw = dynamic_cast<IL::SwitchInstruction*>(headPtr->Instructions[0].get());
    ASSERT_NE(sw, nullptr) << "the IsInterned chain folds into a switch";
    auto* stringToInt = dynamic_cast<IL::StringToInt*>(sw->Value.get());
    ASSERT_NE(stringToInt, nullptr);
    EXPECT_EQ(stringToInt->Map.size(), 2u);
}


// ---- The legacy Dictionary<string,int> arm (matchers) ----------------------

// Build the Dictionary<string, int> type (a ParameterizedType over the
// generic definition, the C# IsStringToIntDictionary shape).
std::shared_ptr<TS::ParameterizedType> MakeStringIntDictionary()
{
    auto genericDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Collections.Generic"),
        std::string("Dictionary`1")));
    std::vector<TS::ITypePtr> args;
    args.push_back(std::make_shared<TS::KnownType>(TS::KnownTypeCode::String));
    args.push_back(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32));
    return std::make_shared<TS::ParameterizedType>(std::move(genericDef),
                                                   std::move(args));
}

TEST(SwitchOnStringTransformTest, ExtractStringValuesFromInitBlockAcceptsAdds)
{
    auto dictType = MakeStringIntDictionary();
    auto dictVar = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, dictType);
    dictVar->Name = "dict";
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    auto ctor = std::make_unique<IL::Call>("System.Collections.Generic.Dictionary`1::.ctor");
    ctor->IsNewObj = true;
    ctor->AddArg(std::make_unique<IL::LdcI4>(3));
    ctor->DeclaringType = dictType;
    block->Add(std::make_unique<IL::StLoc>(dictVar, std::move(ctor)));
    auto makeAdd = [&](const char* value, int index) {
        auto add = std::make_unique<IL::Call>(
            "System.Collections.Generic.Dictionary`1::Add");
        add->DeclaringType = dictType;
        add->AddArg(std::make_unique<IL::LdLoc>(dictVar));
        add->AddArg(std::make_unique<IL::LdStr>(value));
        add->AddArg(std::make_unique<IL::LdcI4>(index));
        return add;
    };
    block->Add(makeAdd("alpha", 0));
    block->Add(makeAdd("beta", 1));
    // The final store: volatile.stobj dictionaryType(ldsflda $$method0x600000c-1,
    // ldloc dict)
    auto ldsflda = std::make_unique<IL::LdsFlda>(
        "System.Runtime.CompilerServices.CompilerGenerated::$$method0x600000c-1");
    ldsflda->IsCompilerGeneratedField = true;
    auto stobj = std::make_unique<IL::StObj>(
        std::move(ldsflda), std::make_unique<IL::LdLoc>(dictVar), dictType);
    block->Add(std::move(stobj));
    // The next block after the init block (the TryGetValue head).
    auto nextBlock = std::make_unique<IL::Block>();
    nextBlock->Kind = IL::BlockKind::ControlFlow;
    IL::Block* nextPtr = nextBlock.get();
    block->Add(std::make_unique<IL::Branch>(nextPtr));
    (void)nextBlock.release();

    std::vector<std::pair<std::optional<std::string>, int>> values;
    IL::Block* after = nullptr;
    std::string error;
    bool ok = IL::SwitchOnStringProbes::ExtractStringValuesFromInitBlock(
        block.get(), values, after,
        [](const TS::IType& t) { return t.Name() == "Dictionary"; },
        dictType.get(), false, error);
    EXPECT_TRUE(ok) << error;
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0].first.value(), "alpha");
    EXPECT_EQ(values[0].second, 0);
    EXPECT_EQ(values[1].first.value(), "beta");
    EXPECT_EQ(values[1].second, 1);
}

TEST(SwitchOnStringTransformTest, ExtractStringValuesRejectsBadFinalStore)
{
    auto dictType = MakeStringIntDictionary();
    auto dictVar = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, dictType);
    dictVar->Name = "dict";
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    auto ctor = std::make_unique<IL::Call>(
        "System.Collections.Generic.Dictionary`1::.ctor");
    ctor->IsNewObj = true;
    ctor->AddArg(std::make_unique<IL::LdcI4>(1));
    ctor->DeclaringType = dictType;
    block->Add(std::make_unique<IL::StLoc>(dictVar, std::move(ctor)));
    auto add = std::make_unique<IL::Call>(
        "System.Collections.Generic.Dictionary`1::Add");
    add->DeclaringType = dictType;
    add->AddArg(std::make_unique<IL::LdLoc>(dictVar));
    add->AddArg(std::make_unique<IL::LdStr>("alpha"));
    add->AddArg(std::make_unique<IL::LdcI4>(0));
    block->Add(std::move(add));
    // The final store is WRONG: a plain stloc instead of the volatile stobj.
    block->Add(std::make_unique<IL::StLoc>(
        dictVar, std::make_unique<IL::LdLoc>(dictVar)));
    block->Add(std::make_unique<IL::Branch>(nullptr));

    std::vector<std::pair<std::optional<std::string>, int>> values;
    IL::Block* after = nullptr;
    std::string error;
    EXPECT_FALSE(IL::SwitchOnStringProbes::ExtractStringValuesFromInitBlock(
        block.get(), values, after,
        [](const TS::IType& t) { return t.Name() == "Dictionary"; },
        dictType.get(), false, error));
}

} // namespace
