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

// Tests for the DelegateConstruction.MatchDelegateConstruction helper (the
// tested-but-not-yet-wired foundation CachedDelegateInitialization -- the next
// in-order transform -- and the later DelegateConstruction transform consume),
// the Call::IsNewObj flag that distinguishes a newobj from a call/callvirt, the
// TypeKind derivation in ResolveMethodDeclaringType that lets a delegate
// constructor's declaring type resolve to Kind == Delegate, and the
// AnonymousMethods setting. The helper is not yet wired into the pipeline; the
// mscorlib sweep exercises the IsNewObj flag + DeclaringType kind derivation +
// the helper on real newobj/ldftn sites.

#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// A delegate declaring type: a SimpleType with Kind == Delegate (the shape the
// type-kind derivation produces for an in-module delegate constructor).
std::shared_ptr<IType> MakeDelegateType(std::string ns, std::string name) {
    return std::make_shared<SimpleType>(TopLevelTypeName(std::move(ns), std::move(name)),
                                        TypeKind::Delegate);
}

// Build a `newobj DelegateType(target, ldftn method)` delegate construction.
std::unique_ptr<Call> MakeNewObjDelegate(std::shared_ptr<IType> delegateType,
                                         std::unique_ptr<ILInstruction> target,
                                         std::string ldftnMethod) {
    auto call = std::make_unique<Call>(delegateType
        ? delegateType->ReflectionName() + "..ctor" : std::string("::.ctor"));
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = std::move(delegateType);
    call->AddArg(std::move(target));
    call->AddArg(std::make_unique<LdFtn>(std::move(ldftnMethod)));
    return call;
}

// Build an `ldvirtdelegate DelegateType Method(target)` (the node the
// ExpressionTransforms.TransformDelegateCtorLdVirtFtnToLdVirtDelegate fold
// produces from a virtual delegate construction).
std::unique_ptr<LdVirtDelegate> MakeLdVirtDelegate(std::shared_ptr<IType> delegateType,
                                                   std::unique_ptr<ILInstruction> target,
                                                   std::string method) {
    return std::make_unique<LdVirtDelegate>(std::move(target), std::move(delegateType),
                                            std::move(method));
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// A resolved method for the transform's token gates (the
// DeconstructionMethodStub pattern): carries a MethodDef token and a
// lambda-shaped name.
class DelegateTargetMethodStub : public ILSpy::Decompiler::TypeSystem::IMethod {
public:
    explicit DelegateTargetMethodStub(std::uint32_t token,
                                      std::string name)
        : token_(token), name_(std::move(name)) {}
    // --- ISymbol / INamedElement ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return "T::" + name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return std::string(); }
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation()
        const override {
        throw std::logic_error("DelegateTargetMethodStub::Compilation");
    }
    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>
    Parameters() const override {
        return {};
    }
    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition()
        const override {
        return this;
    }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override {
        throw std::logic_error("DelegateTargetMethodStub::ReturnType");
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*
    Substitution() const override {
        return &ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution::
            Identity();
    }
    const ILSpy::Decompiler::TypeSystem::IMethod* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*)
        const override {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor*)
        const override {
        return obj == this;
    }
    // --- IMethod (the IEntity group) ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>
    GetAttributes() const override {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return token_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition*
    DeclaringTypeDefinition() const override {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule()
        const override {
        return nullptr;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>
    GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*>
    TypeParameters() const override {
        return {};
    }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> TypeArguments()
        const override {
        return {};
    }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IMember* AccessorOwner()
        const override {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind()
        const override {
        return ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::None;
    }
    const ILSpy::Decompiler::TypeSystem::IMethod* ReducedFrom()
        const override {
        return nullptr;
    }

private:
    std::uint32_t token_;
    std::string name_;
};

// Build a hand-wired delegate-construction Run fixture: `stloc v(newobj
// ActionType(ldloc thisVar, ldftn <resolved>))` inside a block whose final
// is a branch.
struct RunFixture {
    std::unique_ptr<ILFunction> fn;
    std::shared_ptr<ILVariable> targetVar;
    std::shared_ptr<ILVariable> delegateVar;
    std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod> method;
    BlockContainer* body = nullptr;
    Block* entry = nullptr;
    StLoc* store = nullptr;

    RunFixture()
    {
        fn = std::make_unique<ILFunction>();
        fn->Kind = ILFunctionKind::TopLevelFunction;
        auto container = std::make_unique<BlockContainer>();
        body = container.get();
        auto entryBlock = std::make_unique<Block>();
        entry = entryBlock.get();
        container->AddBlock(std::move(entryBlock));
        fn->Body = std::move(container);
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;

        targetVar = std::make_shared<ILVariable>(VariableKind::Parameter,
                                                 nullptr, -1);
        targetVar->Name = "this";
        // The parameter arrives with a value (the C# IsSingleDefinition for
        // a parameter: StoreCount == 1, AddressCount == 0).
        targetVar->StoreCount = 1;
        delegateVar = MakeLocal("v");
        fn->Variables.push_back(delegateVar);

        method = std::make_shared<DelegateTargetMethodStub>(
            0x06000042u, "<Do>b__0_0");
        auto ldftn = std::make_unique<LdFtn>(method);
        auto call = std::make_unique<Call>("System.Action..ctor");
        call->IsNewObj = true;
        call->ReturnType = StackType::O;
        call->DeclaringType =
            MakeDelegateType("System", "Action");
        call->AddArg(std::make_unique<LdLoc>(targetVar));
        call->AddArg(std::move(ldftn));
        auto st = std::make_unique<StLoc>(delegateVar, std::move(call));
        store = st.get();
        entry->Add(std::move(st));
        entry->SetFinal(std::make_unique<Branch>());
    }
};

// The Run test's resolver: a hand-built nested function body (a leave with
// a constant -- the minimal decodable body).
std::unique_ptr<ILFunction> MakeNestedBody(std::uint32_t token,
                                           std::uint32_t rva,
                                           bool* resolverHit) {
    if (resolverHit != nullptr) *resolverHit = true;
    auto fn = std::make_unique<ILFunction>();
    fn->Kind = ILFunctionKind::TopLevelFunction;
    auto container = std::make_unique<BlockContainer>();
    auto entryBlock = std::make_unique<Block>();
    auto leave = std::make_unique<Leave>(container.get(),
                                         std::make_unique<LdcI4>(7));
    entryBlock->SetFinal(std::move(leave));
    container->AddBlock(std::move(entryBlock));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    (void)token;
    (void)rva;
    return fn;
}

} // namespace

// A `newobj DelegateType(target, ldftn m)` with a Delegate declaring type
// matches; the target and ldftn method name are captured.
TEST(DelegateConstruction, MatchNewObjDelegateWithLdFtn) {
    auto v = MakeLocal("v");
    auto call = MakeNewObjDelegate(MakeDelegateType("System", "Action"),
                                  std::make_unique<LdLoc>(v),
                                  "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_TRUE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
    ASSERT_NE(m.target, nullptr);
    EXPECT_EQ(m.target->Op, OpCode::LdLoc);
    ASSERT_NE(m.delegateType, nullptr);
    EXPECT_EQ(m.delegateType->Kind(), TypeKind::Delegate);
    EXPECT_EQ(m.targetMethod, "System.Foo::Bar");
}

// ldvirtftn as the second argument also matches.
TEST(DelegateConstruction, MatchNewObjDelegateWithLdVirtFtn) {
    auto v = MakeLocal("v");
    auto delegateType = MakeDelegateType("System", "Action");
    auto call = std::make_unique<Call>("System.Action..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = delegateType;
    call->AddArg(std::make_unique<LdLoc>(v));
    call->AddArg(std::make_unique<LdVirtFtn>("System.Foo::Bar"));
    DelegateConstructionMatch m;
    EXPECT_TRUE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
    EXPECT_EQ(m.targetMethod, "System.Foo::Bar");
}

// An Unknown declaring type (the C# fallback for an unresolvable cross-assembly
// TypeRef) also matches -- the C# accepts Kind == Unknown.
TEST(DelegateConstruction, MatchNewObjDelegateAcceptsUnknownDeclaringType) {
    auto v = MakeLocal("v");
    auto unknownType = std::make_shared<SimpleType>(TopLevelTypeName("System", "Action"),
                                                    TypeKind::Unknown);
    auto call = MakeNewObjDelegate(unknownType, std::make_unique<LdLoc>(v), "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_TRUE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
}

// An `ldvirtdelegate DelegateType Method(target)` (the C# `case LdVirtDelegate`,
// the shape ExpressionTransforms.TransformDelegateCtorLdVirtFtnToLdVirtDelegate
// produces from a virtual delegate construction) matches: the target (the
// Argument), the method name, and the delegate type are captured.
TEST(DelegateConstruction, MatchLdVirtDelegate) {
    auto v = MakeLocal("v");
    auto ldv = MakeLdVirtDelegate(MakeDelegateType("System", "Action"),
                                  std::make_unique<LdLoc>(v), "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_TRUE(DelegateConstruction::MatchDelegateConstruction(ldv.get(), m, true));
    ASSERT_NE(m.target, nullptr);
    EXPECT_EQ(m.target->Op, OpCode::LdLoc);
    ASSERT_NE(m.delegateType, nullptr);
    EXPECT_EQ(m.delegateType->Kind(), TypeKind::Delegate);
    EXPECT_EQ(m.targetMethod, "System.Foo::Bar");
}

// An LdVirtDelegate whose Type is Unknown (the C# fallback) also matches.
TEST(DelegateConstruction, MatchLdVirtDelegateAcceptsUnknownType) {
    auto v = MakeLocal("v");
    auto unknownType = std::make_shared<SimpleType>(TopLevelTypeName("System", "Action"),
                                                     TypeKind::Unknown);
    auto ldv = MakeLdVirtDelegate(unknownType, std::make_unique<LdLoc>(v), "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_TRUE(DelegateConstruction::MatchDelegateConstruction(ldv.get(), m, true));
    ASSERT_NE(m.delegateType, nullptr);
    EXPECT_EQ(m.delegateType->Kind(), TypeKind::Unknown);
}

// An LdVirtDelegate whose Type is a non-delegate (Class) does not match -- the
// final gate is Kind == Delegate || Unknown.
TEST(DelegateConstruction, RejectsLdVirtDelegateNonDelegateType) {
    auto v = MakeLocal("v");
    auto classType = std::make_shared<SimpleType>(TopLevelTypeName("System", "String"),
                                                   TypeKind::Class);
    auto ldv = MakeLdVirtDelegate(classType, std::make_unique<LdLoc>(v), "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(ldv.get(), m, true));
}

// An LdVirtDelegate whose Type could not be resolved (null) does not match -- a
// null Type is treated like the C# null DeclaringTypeDefinition.
TEST(DelegateConstruction, RejectsLdVirtDelegateNullType) {
    auto v = MakeLocal("v");
    auto ldv = MakeLdVirtDelegate(nullptr, std::make_unique<LdLoc>(v), "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(ldv.get(), m, true));
}

// A non-newobj call (call/callvirt) does not match, even with a delegate
// declaring type and an ldftn argument.
TEST(DelegateConstruction, RejectsNonNewObjCall) {
    auto v = MakeLocal("v");
    auto call = MakeNewObjDelegate(MakeDelegateType("System", "Action"),
                                   std::make_unique<LdLoc>(v), "System.Foo::Bar");
    call->IsNewObj = false;  // a plain call, not a newobj
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
}

// A newobj with the wrong argument count (1 or 3) does not match.
TEST(DelegateConstruction, RejectsWrongArgCount) {
    auto v = MakeLocal("v");
    // 1 argument (just the target, no ldftn).
    auto call1 = std::make_unique<Call>("System.Action..ctor");
    call1->IsNewObj = true;
    call1->ReturnType = StackType::O;
    call1->DeclaringType = MakeDelegateType("System", "Action");
    call1->AddArg(std::make_unique<LdLoc>(v));
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(call1.get(), m, true));

    // 3 arguments.
    auto call3 = std::make_unique<Call>("System.Action..ctor");
    call3->IsNewObj = true;
    call3->ReturnType = StackType::O;
    call3->DeclaringType = MakeDelegateType("System", "Action");
    call3->AddArg(std::make_unique<LdLoc>(v));
    call3->AddArg(std::make_unique<LdFtn>("System.Foo::Bar"));
    call3->AddArg(std::make_unique<LdLoc>(v));
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(call3.get(), m, true));
}

// A newobj whose second argument is not an ldftn/ldvirtftn does not match.
TEST(DelegateConstruction, RejectsNonLdFtnSecondArg) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Action..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = MakeDelegateType("System", "Action");
    call->AddArg(std::make_unique<LdLoc>(v));
    call->AddArg(std::make_unique<LdLoc>(v));  // not an ldftn
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
}

// A newobj whose declaring type could not be resolved (null) does not match --
// a null DeclaringType is treated like the C# null DeclaringTypeDefinition.
TEST(DelegateConstruction, RejectsNullDeclaringType) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Action..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = nullptr;
    call->AddArg(std::make_unique<LdLoc>(v));
    call->AddArg(std::make_unique<LdFtn>("System.Foo::Bar"));
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
}

// A newobj whose declaring type is a non-delegate (Class) does not match.
TEST(DelegateConstruction, RejectsNonDelegateDeclaringType) {
    auto v = MakeLocal("v");
    auto classType = std::make_shared<SimpleType>(TopLevelTypeName("System", "String"),
                                                   TypeKind::Class);
    auto call = MakeNewObjDelegate(classType, std::make_unique<LdLoc>(v), "System.Foo::Bar");
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(call.get(), m, true));
}

// A non-Call instruction does not match.
TEST(DelegateConstruction, RejectsNonCall) {
    auto v = MakeLocal("v");
    auto ld = std::make_unique<LdLoc>(v);
    DelegateConstructionMatch m;
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(ld.get(), m, true));
    EXPECT_FALSE(DelegateConstruction::MatchDelegateConstruction(nullptr, m, true));
}

// The AnonymousMethods setting defaults to true (matching DecompilerSettings,
// which gates CachedDelegateInitialization and the delegate-construction family).
TEST(DelegateConstruction, AnonymousMethodsSettingDefaultsTrue) {
    ILTransformSettings settings;
    EXPECT_TRUE(settings.AnonymousMethods);
}

// On the real mscorlib corpus the IL reader must mark newobj calls with
// IsNewObj, the type-kind derivation must resolve a delegate constructor's
// declaring type to Kind == Delegate, and MatchDelegateConstruction must
// recognise the `newobj DelegateType(.., ldftn ..)` sites. The ILAst invariant
// holds across the corpus.
TEST(DelegateConstruction, MscorlibDelegateConstructionSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int newObjCount = 0;             // calls the reader marked IsNewObj
    int delegateNewObjShape = 0;     // IsNewObj + 2 args + arg[1] is ldftn/ldvirtftn
    int delegateKindCount = 0;      // of those, DeclaringType->Kind() == Delegate
    int matchCount = 0;              // MatchDelegateConstruction returned true
    int unknownKindCount = 0;        // of the shape, DeclaringType->Kind() == Unknown
    int genericDelegateCount = 0;    // of the shape, DeclaringType is a ParameterizedType
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (!inst || inst->Op != OpCode::Call) return;
            auto* call = static_cast<Call*>(inst);
            if (call->IsNewObj) ++newObjCount;
            // The delegate-construction shape: newobj with 2 args, arg[1] ldftn/ldvirtftn.
            if (!call->IsNewObj || call->Arguments.size() != 2) return;
            auto* opArg = call->Arguments[1].get();
            if (!opArg || (opArg->Op != OpCode::LdFtn && opArg->Op != OpCode::LdVirtFtn)) return;
            ++delegateNewObjShape;
            if (call->DeclaringType) {
                if (dynamic_cast<ParameterizedType*>(call->DeclaringType.get())) ++genericDelegateCount;
                TypeKind k = call->DeclaringType->Kind();
                if (k == TypeKind::Delegate) ++delegateKindCount;
                else if (k == TypeKind::Unknown) ++unknownKindCount;
            }
            DelegateConstructionMatch dm;
            if (DelegateConstruction::MatchDelegateConstruction(call, dm, true)) {
                ASSERT_NE(dm.target, nullptr);
                ASSERT_NE(dm.delegateType, nullptr);
                ASSERT_FALSE(dm.targetMethod.empty());
                ++matchCount;
            }
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    // The reader decodes thousands of methods and marks newobj calls.
    EXPECT_GT(processed, 5000);
    EXPECT_GT(newObjCount, 0);
    // The .NET Framework 4 (legacy csc) mscorlib corpus contains real
    // `newobj Delegate(.., ldftn ..)` delegate-construction sites (both non-generic
    // in-module constructors via the MethodDef parent, and generic
    // instantiations like System.Func<int> via the TypeSpec path), so the
    // type-kind derivation must resolve their declaring types to Kind == Delegate
    // and the helper must recognise them.
    EXPECT_GT(delegateNewObjShape, 0);
    EXPECT_GT(genericDelegateCount, 0);  // the TypeSpec generic-delegate path fires
    EXPECT_GT(delegateKindCount, 0);
    EXPECT_GT(matchCount, 0);
    // Every matched call has a Delegate (in-module, including generic via TypeSpec)
    // or Unknown (cross-assembly TypeRef) declaring type, so the match count
    // equals the shape count whose kind is Delegate or Unknown -- the helper is
    // consistent with the shape and the kind derivation.
    EXPECT_EQ(matchCount, delegateKindCount + unknownKindCount);
}




// The Run embed: `stloc v(newobj Action(ldloc this, ldftn <Do>b__0_0))`
// with the AnonymousMethods gate on and a wired body resolver folds to the
// embedded ILFunction (the C# TransformDelegateConstruction: the resolver
// read, the Kind/DelegateType assignment, the ReplaceWith, the variable
// rename, and the nested pipeline + this-swap).
TEST(DelegateConstruction, RunEmbedsTheDecodedBody) {
    RunFixture fx;
    ILTransformContext ctx;
    bool resolverHit = false;
    ctx.Settings.AnonymousMethods = true;
    ctx.DelegateBodyResolver = [&](std::uint32_t token, std::uint32_t rva) {
        return MakeNestedBody(token, rva, &resolverHit);
    };
    DelegateConstruction().Run(*fx.fn, ctx);

    EXPECT_TRUE(resolverHit) << "the resolver read the delegate target";
    // The stloc's value is now the embedded ILFunction (Kind Delegate, the
    // delegate type carried over).
    ASSERT_EQ(fx.store->Value->Op, OpCode::ILFunction);
    auto* nested = static_cast<ILFunction*>(fx.store->Value.get());
    EXPECT_EQ(nested->Kind, ILFunctionKind::Delegate);
    ASSERT_NE(nested->DelegateType, nullptr);
    EXPECT_EQ(nested->DelegateType->Kind(), TypeKind::Delegate);
    fx.fn->CheckInvariant(ILPhase::Normal);
}

// The AnonymousMethods gate off leaves the construction untouched.
TEST(DelegateConstruction, RunHonorsTheAnonymousMethodsGate) {
    RunFixture fx;
    ILTransformContext ctx;
    ctx.Settings.AnonymousMethods = false;
    bool resolverHit = false;
    ctx.DelegateBodyResolver = [&](std::uint32_t, std::uint32_t) {
        return MakeNestedBody(0, 0, &resolverHit);
    };
    DelegateConstruction().Run(*fx.fn, ctx);
    EXPECT_FALSE(resolverHit);
    EXPECT_EQ(fx.store->Value->Op, OpCode::Call);
}

// A non-anonymous target name (no '<' prefix and no '$') does not match --
// the C# IsAnonymousMethod name arms.
TEST(DelegateConstruction, RunRejectsPlainTargetNames) {
    RunFixture fx;
    fx.method = std::make_shared<DelegateTargetMethodStub>(
        0x06000042u, "PlainMethod");
    // Rebuild the store's value with the plain-name ldftn.
    auto call = std::make_unique<Call>("System.Action..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = MakeDelegateType("System", "Action");
    call->AddArg(std::make_unique<LdLoc>(fx.targetVar));
    auto ldftn = std::make_unique<LdFtn>(fx.method);
    call->AddArg(std::move(ldftn));
    fx.store->Value = std::move(call);
    fx.store->Value->Parent = fx.store;
    fx.store->Value->ChildIndex = 0;

    ILTransformContext ctx;
    bool resolverHit = false;
    ctx.DelegateBodyResolver = [&](std::uint32_t, std::uint32_t) {
        return MakeNestedBody(0, 0, &resolverHit);
    };
    DelegateConstruction().Run(*fx.fn, ctx);
    EXPECT_FALSE(resolverHit) << "a plain method is not an anonymous method";
    EXPECT_EQ(fx.store->Value->Op, OpCode::Call);
}

