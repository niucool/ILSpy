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
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
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
