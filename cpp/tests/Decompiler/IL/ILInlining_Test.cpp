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

// ILInlining tests: single-use variable inlining (the third transform in
// GetILTransforms). A StLoc whose variable is stored once and loaded/addressed
// once inlines its value into the load site; dead stores of pure expressions
// vanish. Hand-built trees pin the shapes; the mscorlib sweep pins the global
// contract (invariant + variable-count drop).

#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeVar(VariableKind kind, std::string name, std::int32_t index) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = kind;
    v->Index = index;
    return v;
}

std::unique_ptr<ILFunction> WrapBlocks(std::vector<std::unique_ptr<Block>> blocks) {
    auto container = std::make_unique<BlockContainer>();
    for (auto& b : blocks) container->AddBlock(std::move(b));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    return fn;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

// A reference-type declaring type (System.Object) and a value-type one
// (System.Int32), for exercising the ChainedConstructorCallILOffset
// IsReferenceType gate by Kind.
ITypePtr RefType() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr ValueTypeDecl() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// Build a void constructor `call` (the C# `: base(...)`/`: this(...)` shape) --
// an instance, non-newobj Call named "System.Object::.ctor" added as a block
// statement, with its IL range set so ChainedConstructorCallILOffset can report
// it. `offset` is the call's IL byte offset.
std::unique_ptr<Call> MakeCtorCall(ITypePtr declaringType, std::int32_t offset) {
    auto call = std::make_unique<Call>("System.Object::.ctor");
    call->DeclaringType = std::move(declaringType);
    call->IsInstanceCall = true;
    call->IsNewObj = false;
    call->SetILRange(offset, offset + 5);
    return call;
}

// An ILFunction wrapping a single empty block with a void Leave final;
// IsConstructor/IsStatic preset for the constructor-initializer tests. Tests add
// statements via `fn->Body->Blocks[0]->Add(...)` (Block::Add wires Parent and
// keeps the final's ChildIndex in step), matching the established
// WrapBlocks-based test pattern (a vector<unique_ptr> cannot be brace-initialised).
std::unique_ptr<ILFunction> MakeEmptyCtorFn(bool isCtor, bool isStatic) {
    auto container = std::make_unique<BlockContainer>();
    container->AddBlock(std::make_unique<Block>());
    auto fn = std::make_unique<ILFunction>();
    fn->IsConstructor = isCtor;
    fn->IsStatic = isStatic;
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    return fn;
}

} // namespace

TEST(ILInlining, InlinesSingleUseLocalIntoLeave) {
    auto local = MakeVar(VariableKind::Local, "V_0", 0);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(42)));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(local)));
    fn->Variables.push_back(local);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    EXPECT_TRUE(only->Instructions.empty()) << "stloc must be removed";
    auto* leave = dynamic_cast<Leave*>(only->FinalInstruction.get());
    ASSERT_NE(leave, nullptr);
    ASSERT_NE(leave->Value, nullptr);
    EXPECT_EQ(leave->Value->Op, OpCode::LdcI4) << "ldloc inlined to ldc.i4";
}

TEST(ILInlining, InlinesStackSlotIntoCall) {
    auto slot = MakeVar(VariableKind::StackSlot, "S_0", -1);
    auto arg = MakeVar(VariableKind::Parameter, "arg_1", 1);
    auto call = std::make_unique<Call>("System.Object::Equals");
    call->AddArg(std::make_unique<LdLoc>(slot));
    call->AddArg(std::make_unique<LdcI4>(1));
    call->ReturnType = StackType::I4;
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(slot, std::make_unique<LdLoc>(arg)));
    block->Add(std::move(call));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(slot);
    fn->Variables.push_back(arg);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    ASSERT_EQ(only->Instructions.size(), 1u) << "stloc removed, call remains";
    auto* c = dynamic_cast<Call*>(only->Instructions[0].get());
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->Arguments.size(), 2u);
    EXPECT_EQ(c->Arguments[0]->Op, OpCode::LdLoc);
    auto* ld = dynamic_cast<LdLoc*>(c->Arguments[0].get());
    ASSERT_NE(ld, nullptr);
    EXPECT_EQ(ld->Variable->Name, "arg_1") << "S_0 inlined to its source";
}

TEST(ILInlining, RemovesDeadPureStackSlotStore) {
    auto slot = MakeVar(VariableKind::StackSlot, "S_0", -1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(slot, std::make_unique<LdcI4>(99)));
    block->Add(std::make_unique<LdcI4>(0));  // a stray constant, never loaded
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(slot);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    // The dead stloc (pure ldc.i4) is removed; the stray constant stays
    // (it's not a StLoc so ILInlining doesn't touch it).
    EXPECT_EQ(only->Instructions.size(), 1u);
}

TEST(ILInlining, DoesNotInlineMultiUseVariable) {
    auto local = MakeVar(VariableKind::Local, "V_0", 0);
    auto other = MakeVar(VariableKind::Local, "V_1", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(local, std::make_unique<LdcI4>(1)));
    block->Add(std::make_unique<StLoc>(other, std::make_unique<LdLoc>(local)));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(local)));
    fn->Variables.push_back(local);
    fn->Variables.push_back(other);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    // V_0 has loadCount 2 (into V_1 store + leave) -> not single-use, not inlined.
    // V_1 is a dead store (never loaded) -> removed as dead.
    const auto& only = fn->Body->Blocks[0];
    EXPECT_EQ(only->Instructions.size(), 1u) << "V_0 survives (multi-use), V_1 dead store removed";
    auto* st = dynamic_cast<StLoc*>(only->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->Variable->Name, "V_0");
}

// FindLoadInNext is the search for the single load of a variable inside an
// instruction subtree, into which an expression can be inlined. It is exposed
// (in ILInlining.hpp) so other per-statement transforms (NullCoalescingTransform's
// value-types throw-expression fold and the hoisted-constructor-argument null
// guard) can locate the use they redirect -- the prerequisite the D111
// NullCoalescingTransform throw-expression port flagged as deferred. These unit
// tests pin the faithful contract: Found for both an LdLoc(v) and an LdLoca(v)
// match (the C# returns Found for both; the CALLER gates whether an ldloca can
// actually be inlined).

// An LdLoc(v) nested as a call argument is found: FindLoadInNext returns Found
// and reports the load.
TEST(ILInlining, FindLoadInNextFindsLdLocOfVariable) {
    auto v = MakeVar(VariableKind::Local, "v", 0);
    auto call = std::make_unique<Call>("System.Object::Equals");
    call->AddArg(std::make_unique<LdLoc>(v));
    call->AddArg(std::make_unique<LdcI4>(1));
    call->ReturnType = StackType::I4;
    auto* callPtr = call.get();
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    auto moved = std::make_unique<LdcI4>(99);  // a pure expression being moved
    FindResult r = FindLoadInNext(callPtr, v.get(), moved.get());
    EXPECT_EQ(r.type, FindResultType::Found);
    ASSERT_NE(r.loadInst, nullptr);
    EXPECT_EQ(r.loadInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(r.loadInst)->Variable.get(), v.get());
}

// An LdLoca(v) nested as a call argument is found: FindLoadInNext returns Found
// and reports the ldloca. This is the faithful change -- the prior port returned
// Stop for an LdLoca(v) match, but the C# returns Found for both LdLoc(v) and
// LdLoca(v); the caller decides whether the ldloca can be inlined.
TEST(ILInlining, FindLoadInNextFindsLdLocaOfVariable) {
    auto v = MakeVar(VariableKind::Local, "v", 0);
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->AddArg(std::make_unique<LdLoca>(v));
    call->ReturnType = StackType::I4;
    auto* callPtr = call.get();
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::make_unique<Block>());
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    auto moved = std::make_unique<LdcI4>(99);
    FindResult r = FindLoadInNext(callPtr, v.get(), moved.get());
    EXPECT_EQ(r.type, FindResultType::Found)
        << "an LdLoca(v) must be Found, faithful to the C# (the caller gates the "
           "ldloca inline)";
    ASSERT_NE(r.loadInst, nullptr);
    EXPECT_EQ(r.loadInst->Op, OpCode::LdLoca);
    EXPECT_EQ(static_cast<LdLoca*>(r.loadInst)->Variable.get(), v.get());
}

// A null expression aborts the search with Stop.
TEST(ILInlining, FindLoadInNextReturnsStopForNullExpression) {
    auto v = MakeVar(VariableKind::Local, "v", 0);
    auto moved = std::make_unique<LdcI4>(99);
    FindResult r = FindLoadInNext(nullptr, v.get(), moved.get());
    EXPECT_EQ(r.type, FindResultType::Stop);
    EXPECT_EQ(r.loadInst, nullptr);
}

// Regression guard: a variable used once via ldloca (StoreCount 1, LoadCount 0,
// AddressCount 1) is NOT inlined by InlineOneIfPossible. The ldloca-into-
// addressof path is deferred (needs an AddressOf node +
// IsGeneratedTemporaryForAddressOf), so the found LdLoca must be skipped and the
// stloc must survive. This pins that the LdLoca->Found change plus the
// InlineOneIfPossible LdLoc-gate preserve the prior "don't inline ldloca"
// behavior.
TEST(ILInlining, DoesNotInlineLdLocaOnlyVariable) {
    auto v = MakeVar(VariableKind::Local, "v", 0);
    auto a = MakeVar(VariableKind::Parameter, "a", 1);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(a)));
    // The single use of v is an ldloca inside a call in the block's final Leave.
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->AddArg(std::make_unique<LdLoca>(v));
    call->ReturnType = StackType::I4;
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::move(call)));
    fn->Variables.push_back(v);
    fn->Variables.push_back(a);
    fn->CheckInvariant(ILPhase::Normal);

    ILInlining().Run(*fn, Ctx());
    fn->CheckInvariant(ILPhase::Normal);

    const auto& only = fn->Body->Blocks[0];
    EXPECT_EQ(only->Instructions.size(), 1u)
        << "the stloc v must survive (ldloca use is not inlined)";
    auto* st = dynamic_cast<StLoc*>(only->Instructions[0].get());
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->Variable.get(), v.get());
    // The ldloca v survives inside the Leave's call value.
    auto* leave = dynamic_cast<Leave*>(only->FinalInstruction.get());
    ASSERT_NE(leave, nullptr);
    ASSERT_NE(leave->Value, nullptr);
    ASSERT_EQ(leave->Value->Op, OpCode::Call);
    auto* c = static_cast<Call*>(leave->Value.get());
    ASSERT_EQ(c->Arguments.size(), 1u);
    EXPECT_EQ(c->Arguments[0]->Op, OpCode::LdLoca)
        << "the ldloca v must survive (not inlined)";
    EXPECT_EQ(static_cast<LdLoca*>(c->Arguments[0].get())->Variable.get(), v.get());
}

TEST(ILInlining, InliningOnMscorlibReducesVariableCount) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int totalVarsBefore = 0;
    int totalVarsAfter = 0;
    int transformed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        totalVarsBefore += static_cast<int>(fn->Variables.size());
        ILInlining().Run(*fn, Ctx());
        fn->CheckInvariant(ILPhase::Normal);
        totalVarsAfter += static_cast<int>(fn->Variables.size());
        ++transformed;
        if (transformed >= 8000) break;
    }
    EXPECT_GT(transformed, 5000);
    EXPECT_LT(totalVarsAfter, totalVarsBefore)
        << "inlining must reduce the total variable count";
}

// ILFunction::ChainedConstructorCallILOffset -- the lazy, cached offset of the
// first `: base(...)`/`: this(...)` call, the gate IsInConstructorInitializer
// compares hoisted null-guards against. A tested-but-not-yet-wired foundation
// (the D112 FindLoadInNext / D114 ILFunction.Method precedent) for the next
// in-order NullCoalescingTransform hoisted-constructor-argument null-guard fold.

TEST(ILInlining, ChainedConstructorCallILOffsetFindsReferenceTypeCtorCall) {
    auto fn = MakeEmptyCtorFn(/*isCtor=*/true, /*isStatic=*/false);
    auto call = MakeCtorCall(RefType(), 10);
    auto* callPtr = call.get();
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), 10)
        << "the chained .ctor Call's StartILOffset is the offset";
    EXPECT_EQ(callPtr->Parent->Op, OpCode::Block) << "call is a block statement";
}

TEST(ILInlining, ChainedConstructorCallILOffsetRejectsNewObj) {
    auto fn = MakeEmptyCtorFn(true, false);
    auto call = std::make_unique<Call>("System.Object::.ctor");
    call->DeclaringType = RefType();
    call->IsNewObj = true;  // the C# `!(call is NewObj)` gate
    call->SetILRange(10, 15);
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), -1)
        << "a newobj is a constructor *call* but not a chained base/this call";
}

TEST(ILInlining, ChainedConstructorCallILOffsetRejectsValueTypeDeclaringType) {
    auto fn = MakeEmptyCtorFn(true, false);
    fn->Body->Blocks[0]->Add(MakeCtorCall(ValueTypeDecl(), 10));  // Int32 is a struct
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), -1)
        << "value-type declaring types are not chained via : base/: this";
}

TEST(ILInlining, ChainedConstructorCallILOffsetRejectsNonCtorName) {
    auto fn = MakeEmptyCtorFn(true, false);
    auto call = std::make_unique<Call>("System.Object::Equals");
    call->DeclaringType = RefType();
    call->IsInstanceCall = true;
    call->IsNewObj = false;
    call->SetILRange(10, 15);
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), -1)
        << "a non-.ctor method is not a chained constructor call";
}

TEST(ILInlining, ChainedConstructorCallILOffsetRejectsValuePositionCall) {
    // A .ctor-named call sitting in a value slot (a Leave's value, not a block
    // statement) fails the `Parent is Block` gate, matching the C#.
    auto call = std::make_unique<Call>("System.Object::.ctor");
    call->DeclaringType = RefType();
    call->IsNewObj = false;
    call->ReturnType = StackType::I4;  // pretend value-returning so it can be a Leave value
    call->SetILRange(10, 15);
    auto* callPtr = call.get();
    auto fn = MakeEmptyCtorFn(true, false);
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_NE(callPtr->Parent, nullptr);
    EXPECT_NE(callPtr->Parent->Op, OpCode::Block) << "the call is the Leave's value, not a statement";
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), -1);
}

TEST(ILInlining, ChainedConstructorCallILOffsetRejectsNonInstanceConstructor) {
    auto fn = MakeEmptyCtorFn(/*isCtor=*/false, /*isStatic=*/false);
    fn->Body->Blocks[0]->Add(MakeCtorCall(RefType(), 10));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), -1)
        << "a non-constructor has no chained constructor call";

    auto fnStatic = MakeEmptyCtorFn(/*isCtor=*/true, /*isStatic=*/true);
    fnStatic->Body->Blocks[0]->Add(MakeCtorCall(RefType(), 10));
    fnStatic->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fnStatic->ChainedConstructorCallILOffset(), -1)
        << "a static .cctor has no chained instance constructor call";
}

TEST(ILInlining, ChainedConstructorCallILOffsetEmptyBody) {
    auto fn = MakeEmptyCtorFn(true, false);  // ctor with no chained call
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), -1);
}

TEST(ILInlining, ChainedConstructorCallILOffsetIsCached) {
    auto fn = MakeEmptyCtorFn(true, false);
    fn->Body->Blocks[0]->Add(MakeCtorCall(RefType(), 7));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), 7);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), 7) << "a second call returns the cached value";
}

// ILFunction::RegisterVariable -- the helper the hoisted-constructor-argument
// null-guard fold uses to allocate the temp that redirects the parameter's
// first use.

TEST(ILInlining, RegisterVariableGeneratesHelperName) {
    auto fn = MakeEmptyCtorFn(true, false);
    auto v0 = fn->RegisterVariable(VariableKind::StackSlot, nullptr);
    EXPECT_EQ(v0->Name, "I_0");
    EXPECT_TRUE(v0->HasGeneratedName);
    EXPECT_EQ(v0->Kind, VariableKind::StackSlot);
    ASSERT_EQ(fn->Variables.size(), 1u);
    EXPECT_EQ(fn->Variables[0].get(), v0.get());

    auto v1 = fn->RegisterVariable(VariableKind::StackSlot, nullptr);
    EXPECT_EQ(v1->Name, "I_1");
    EXPECT_TRUE(v1->HasGeneratedName);
    ASSERT_EQ(fn->Variables.size(), 2u);
}

TEST(ILInlining, RegisterVariableUsesGivenName) {
    auto fn = MakeEmptyCtorFn(true, false);
    auto v = fn->RegisterVariable(VariableKind::StackSlot, RefType(), "temp");
    EXPECT_EQ(v->Name, "temp");
    EXPECT_FALSE(v->HasGeneratedName);
    EXPECT_NE(v->Type, nullptr);
    // A blank-name call after a named one still continues the counter from 0.
    auto v2 = fn->RegisterVariable(VariableKind::StackSlot, nullptr);
    EXPECT_EQ(v2->Name, "I_0");
}

// TopLevelStatement / IsInConstructorInitializer -- the gate the
// hoisted-constructor-argument null-guard fold consults.

TEST(ILInlining, TopLevelStatementReturnsBlockDirectChild) {
    auto fn = MakeEmptyCtorFn(true, false);
    auto call = std::make_unique<Call>("System.Object::Equals");
    call->AddArg(std::make_unique<LdNull>());
    auto* callPtr = call.get();
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(TopLevelStatement(callPtr), callPtr) << "a block statement is its own top level";
}

TEST(ILInlining, TopLevelStatementFindsEnclosingStatement) {
    // An LdNull nested as a Call argument: its top-level statement is the Call.
    auto fn = MakeEmptyCtorFn(true, false);
    auto ldnull = std::make_unique<LdNull>();
    auto* ldnullPtr = ldnull.get();
    auto call = std::make_unique<Call>("System.Object::Equals");
    call->AddArg(std::move(ldnull));
    auto* callPtr = call.get();
    fn->Body->Blocks[0]->Add(std::move(call));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(TopLevelStatement(ldnullPtr), callPtr);
}

TEST(ILInlining, IsInConstructorInitializerTrueForGuardBeforeChainedCall) {
    // A guard (stand-in: a Call) ending at offset 5, chained call at offset 10.
    auto fn = MakeEmptyCtorFn(true, false);
    auto guard = std::make_unique<Call>("System.Object::Equals");
    guard->SetILRange(2, 5);
    auto* guardPtr = guard.get();
    fn->Body->Blocks[0]->Add(std::move(guard));
    fn->Body->Blocks[0]->Add(MakeCtorCall(RefType(), 10));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(fn->ChainedConstructorCallILOffset(), 10);
    EXPECT_TRUE(IsInConstructorInitializer(fn.get(), guardPtr))
        << "a guard ending before the chained call is in the initializer";
}

TEST(ILInlining, IsInConstructorInitializerFalseForGuardAfterChainedCall) {
    auto fn = MakeEmptyCtorFn(true, false);
    fn->Body->Blocks[0]->Add(MakeCtorCall(RefType(), 10));
    auto guard = std::make_unique<Call>("System.Object::Equals");
    guard->SetILRange(15, 18);  // ends after the chained call starts
    auto* guardPtr = guard.get();
    fn->Body->Blocks[0]->Add(std::move(guard));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_FALSE(IsInConstructorInitializer(fn.get(), guardPtr))
        << "a guard ending after the chained call is not in the initializer";
}

TEST(ILInlining, IsInConstructorInitializerFalseForNullFunction) {
    auto guard = std::make_unique<Call>("System.Object::Equals");
    guard->SetILRange(2, 5);
    EXPECT_FALSE(IsInConstructorInitializer(nullptr, guard.get()));
}

TEST(ILInlining, IsInConstructorInitializerFalseForNonCtorFunction) {
    // A non-constructor has no chained call (offset -1); any non-empty guard
    // range (EndILOffset > -1) short-circuits to false.
    auto fn = MakeEmptyCtorFn(/*isCtor=*/false, /*isStatic=*/false);
    auto guard = std::make_unique<Call>("System.Object::Equals");
    guard->SetILRange(2, 5);
    auto* guardPtr = guard.get();
    fn->Body->Blocks[0]->Add(std::move(guard));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_FALSE(IsInConstructorInitializer(fn.get(), guardPtr));
}

TEST(ILInlining, IsInConstructorInitializerTrueForEmptyRangeGuard) {
    // An empty-range guard (EndILOffset 0) against a real chained call at 10:
    // 0 <= 10 passes the first check, the top-level statement is the guard
    // itself, and 0 <= 10 -> true. This pins the faithful C# behaviour (an
    // empty range is Start==End==0; EndILOffset 0 <= ctorCallStart).
    auto fn = MakeEmptyCtorFn(true, false);
    auto guard = std::make_unique<Call>("System.Object::Equals");
    // default range {0,0} is empty
    auto* guardPtr = guard.get();
    fn->Body->Blocks[0]->Add(std::move(guard));
    fn->Body->Blocks[0]->Add(MakeCtorCall(RefType(), 10));
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_TRUE(IsInConstructorInitializer(fn.get(), guardPtr))
        << "an empty-range guard is treated as starting at offset 0, before the call";
}

// ILFunction::RecombineVariables -- replace all occurrences of variable2 with
// variable1 (the C# ILFunction.RecombineVariables), the finalizeMatch the
// TransformAssignment.IsMatchingCompoundLoad LdLoc/StLoc branch calls so a
// `stloc V(binary.op(ldloc V, rhs))` whose load and store are split fragments of
// one original variable collapses to a single variable for the `V op= rhs`
// compound assign. A tested-but-not-yet-wired foundation (the D112
// FindLoadInNext / D114 ILFunction.Method / D115 ILFunction.cpp precedent) for
// the next in-order TransformAssignment.HandleCompoundAssign fold; this port
// has no per-variable instruction lists, so a tree walk replaces the C# list
// iteration and a manual count increment replaces the C# property-setter's
// list maintenance. Tested here alongside the other ILFunction API methods
// (RegisterVariable / ChainedConstructorCallILOffset).

TEST(ILInlining, RecombineVariablesIsNoOpForSameVariable) {
    auto v = MakeVar(VariableKind::Local, "V_0", 0);
    auto fn = WrapBlocks({});
    fn->Variables.push_back(v);
    fn->RecombineVariables(v, v);
    EXPECT_EQ(fn->Variables.size(), 1u) << "the same variable is not removed";
    EXPECT_EQ(fn->Variables.front().get(), v.get());
}

TEST(ILInlining, RecombineVariablesIsNoOpForNullVariable) {
    auto v = MakeVar(VariableKind::Local, "V_0", 0);
    auto fn = WrapBlocks({});
    fn->Variables.push_back(v);
    fn->RecombineVariables(nullptr, v);
    fn->RecombineVariables(v, nullptr);
    fn->RecombineVariables(nullptr, nullptr);
    EXPECT_EQ(fn->Variables.size(), 1u) << "a null operand is a no-op";
}

TEST(ILInlining, RecombineVariablesReassignsLoadStoreAndAddress) {
    auto v1 = MakeVar(VariableKind::Local, "V_0", 0);
    auto v2 = MakeVar(VariableKind::Local, "V_1", 0);  // split fragment (same Index)
    auto block = std::make_unique<Block>();
    auto st = std::make_unique<StLoc>(v2, std::make_unique<LdcI4>(5));
    auto* stPtr = st.get();
    block->Add(std::move(st));
    auto call = std::make_unique<Call>("Foo::Bar");
    auto lda = std::make_unique<LdLoca>(v2);
    auto* ldaPtr = lda.get();
    call->AddArg(std::move(lda));
    block->Add(std::move(call));
    auto ld = std::make_unique<LdLoc>(v2);
    auto* ldPtr = ld.get();
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::move(ld)));
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    fn->CheckInvariant(ILPhase::Normal);
    ComputeVariableUsage(*fn);  // v2: Store 1 / Load 1 / Address 1; v1: 0
    ASSERT_EQ(v2->StoreCount, 1);
    ASSERT_EQ(v2->LoadCount, 1);
    ASSERT_EQ(v2->AddressCount, 1);

    fn->RecombineVariables(v1, v2);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(stPtr->Variable.get(), v1.get()) << "the store reassigned to v1";
    EXPECT_EQ(ldaPtr->Variable.get(), v1.get()) << "the address reassigned to v1";
    EXPECT_EQ(ldPtr->Variable.get(), v1.get()) << "the load reassigned to v1";
    EXPECT_EQ(v1->StoreCount, 1) << "v1 gained v2's store";
    EXPECT_EQ(v1->LoadCount, 1) << "v1 gained v2's load";
    EXPECT_EQ(v1->AddressCount, 1) << "v1 gained v2's address";
    EXPECT_EQ(v2->StoreCount, 0) << "v2's counts are drained";
    EXPECT_EQ(v2->LoadCount, 0);
    EXPECT_EQ(v2->AddressCount, 0);
    bool v1Listed = false, v2Listed = false;
    for (auto& var : fn->Variables) {
        if (var.get() == v1.get()) v1Listed = true;
        if (var.get() == v2.get()) v2Listed = true;
    }
    EXPECT_TRUE(v1Listed) << "v1 stays on the function";
    EXPECT_FALSE(v2Listed) << "v2 dropped from the function";
}

TEST(ILInlining, RecombineVariablesSumsCountsWhenTargetHasExistingUses) {
    // v1 already has a load (the stloc value); v2 has a store and a load.
    // After recombine, v1 absorbs v2's store and load on top of its own.
    auto v1 = MakeVar(VariableKind::Local, "V_0", 0);
    auto v2 = MakeVar(VariableKind::Local, "V_1", 0);
    auto block = std::make_unique<Block>();
    // stloc v2(ldloc v1): v2's store, v1's load
    auto st = std::make_unique<StLoc>(v2, std::make_unique<LdLoc>(v1));
    auto* stPtr = st.get();
    auto* stValueLd = static_cast<StLoc*>(stPtr)->Value.get();
    block->Add(std::move(st));
    auto ld = std::make_unique<LdLoc>(v2);  // v2's load
    auto* ldPtr = ld.get();
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(std::make_unique<Leave>(fn->Body.get(), std::move(ld)));
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    fn->CheckInvariant(ILPhase::Normal);
    ComputeVariableUsage(*fn);  // v1 Load 1; v2 Store 1 / Load 1
    ASSERT_EQ(v1->LoadCount, 1);
    ASSERT_EQ(v2->StoreCount, 1);
    ASSERT_EQ(v2->LoadCount, 1);

    fn->RecombineVariables(v1, v2);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(stPtr->Variable.get(), v1.get()) << "v2's store reassigned to v1";
    EXPECT_EQ(ldPtr->Variable.get(), v1.get()) << "v2's load reassigned to v1";
    EXPECT_EQ(stValueLd->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(stValueLd)->Variable.get(), v1.get())
        << "v1's own load is unchanged";
    EXPECT_EQ(v1->StoreCount, 1) << "v1 gained v2's store (was 0)";
    EXPECT_EQ(v1->LoadCount, 2) << "v1's own load + v2's reassigned load";
    EXPECT_EQ(v2->StoreCount, 0);
    EXPECT_EQ(v2->LoadCount, 0);
    EXPECT_EQ(fn->Variables.size(), 1u) << "only v1 remains";
}

TEST(ILInlining, RecombineVariablesPreservesOtherVariablesCounts) {
    // A third variable v3 with its own uses is untouched by the recombine (the
    // manual count increment only touches v1 and v2, unlike a full
    // ComputeVariableUsage recompute that would re-walk every variable).
    auto v1 = MakeVar(VariableKind::Local, "V_0", 0);
    auto v2 = MakeVar(VariableKind::Local, "V_1", 0);
    auto v3 = MakeVar(VariableKind::Local, "V_2", 2);
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(v2, std::make_unique<LdcI4>(5)));
    block->Add(std::make_unique<StLoc>(v3, std::make_unique<LdcI4>(7)));
    auto fn = WrapBlocks({});
    fn->Body->AddBlock(std::move(block));
    fn->Body->Blocks[0]->SetFinal(
        std::make_unique<Leave>(fn->Body.get(), std::make_unique<LdLoc>(v3)));
    fn->Variables.push_back(v1);
    fn->Variables.push_back(v2);
    fn->Variables.push_back(v3);
    fn->CheckInvariant(ILPhase::Normal);
    ComputeVariableUsage(*fn);  // v2 Store 1; v3 Store 1 / Load 1; v1 0
    ASSERT_EQ(v3->StoreCount, 1);
    ASSERT_EQ(v3->LoadCount, 1);

    fn->RecombineVariables(v1, v2);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(v3->StoreCount, 1) << "v3's counts are unchanged";
    EXPECT_EQ(v3->LoadCount, 1);
    EXPECT_EQ(v1->StoreCount, 1) << "v1 absorbed v2's store";
    EXPECT_EQ(fn->Variables.size(), 2u) << "v2 removed; v1 and v3 remain";
}
