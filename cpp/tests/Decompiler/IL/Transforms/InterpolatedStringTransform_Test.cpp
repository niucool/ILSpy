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
// PURPOSE, NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for InterpolatedStringTransform (the C# 10/.NET 6 `$"..."` via
// DefaultInterpolatedStringHandler fold, the last per-statement child of the
// GetILTransforms() StatementTransform). The hand-built tests verify the
// rewrite (the stloc + AppendLiteral/AppendFormatted + ToStringAndClear
// sequence folds to a Block(InterpolatedString) with the ToStringAndClear as
// its FinalInstruction, v promoted to InitializerTarget) and the negatives
// (each gate); the mscorlib sweep pins the global contract -- the transform
// fires 0 times on the .NET Framework 4 legacy-csc mscorlib corpus
// (DefaultInterpolatedStringHandler is a .NET 6+ type, absent from it), so the
// sweep asserts the ILAst invariant holds across the corpus (faithfulness-only,
// matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNull-
// able / NullCoalescingTransform precedent).

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/BlockKind.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/InterpolatedStringTransform.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::BlockContainer;
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::ILFunction;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::ILPhase;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::InterpolatedStringTransform;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::LdLoca;
using ILSpy::Decompiler::IL::LdStr;
using ILSpy::Decompiler::IL::Leave;
using ILSpy::Decompiler::IL::OpCode;
using ILSpy::Decompiler::IL::StackType;
using ILSpy::Decompiler::IL::StatementTransform;
using ILSpy::Decompiler::IL::StLoc;
using ILSpy::Decompiler::IL::VariableKind;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ITypePtr HandlerType() {
    return std::make_shared<KnownType>(KnownTypeCode::DefaultInterpolatedStringHandler);
}

ILVariablePtr MakeHandlerLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, HandlerType(), 0);
    v->Name = std::move(name);
    return v;
}

// A `newobj DefaultInterpolatedStringHandler..ctor(ldc.i4, ldc.i4)` -- a Call
// with IsNewObj, 2 LdcI4 args, DeclaringType the handler.
std::unique_ptr<Call> MakeHandlerCtor(std::int32_t literalLength, std::int32_t formattedCount) {
    auto call = std::make_unique<Call>("System::DefaultInterpolatedStringHandler::.ctor");
    call->DeclaringType = HandlerType();
    call->IsNewObj = true;
    call->IsInstanceCall = false;  // newobj: not an instance call
    call->ReturnType = StackType::Void;
    call->AddArg(std::make_unique<LdcI4>(literalLength));
    call->AddArg(std::make_unique<LdcI4>(formattedCount));
    return call;
}

// A `call AppendLiteral(ldloca v, ldstr literal)` -- an instance Call on the
// handler, 2 args.
std::unique_ptr<Call> MakeAppendLiteral(ILVariablePtr v, std::string literal) {
    auto call = std::make_unique<Call>("System::DefaultInterpolatedStringHandler::AppendLiteral");
    call->DeclaringType = HandlerType();
    call->IsInstanceCall = true;
    call->ReturnType = StackType::Void;
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdStr>(std::move(literal)));
    return call;
}

// A `call AppendFormatted(ldloca v, expr)` -- an instance Call on the handler,
// 2 args.
std::unique_ptr<Call> MakeAppendFormatted(ILVariablePtr v,
                                          std::unique_ptr<ILInstruction> expr) {
    auto call = std::make_unique<Call>("System::DefaultInterpolatedStringHandler::AppendFormatted");
    call->DeclaringType = HandlerType();
    call->IsInstanceCall = true;
    call->ReturnType = StackType::O;
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::move(expr));
    return call;
}

// A `call ToStringAndClear(ldloca v)` -- an instance Call on the handler, 1
// arg, returns the interpolated string.
std::unique_ptr<Call> MakeToStringAndClear(ILVariablePtr v) {
    auto call = std::make_unique<Call>("System::DefaultInterpolatedStringHandler::ToStringAndClear");
    call->DeclaringType = HandlerType();
    call->IsInstanceCall = true;
    call->ReturnType = StackType::O;
    call->AddArg(std::make_unique<LdLoca>(v));
    return call;
}

// A one-block function: block0 (the caller populates it) + block1 (a Leave
// final, the fall-through). The caller adds the stloc + Append + ToStringAndClear
// non-terminals to block0 and a Leave final.
std::unique_ptr<ILFunction> MakeOneBlockFn(std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto b0 = std::make_unique<Block>();
    b0->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(b0));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

Block* BuildInterpolatedStringShape(ILFunction& fn, ILVariablePtr v,
                                     std::vector<std::unique_ptr<ILInstruction>> appends,
                                     std::unique_ptr<ILInstruction> toStringAndClear) {
    auto& b0 = fn.Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, MakeHandlerCtor(6, 1)));
    for (auto& a : appends) b0->Add(std::move(a));
    b0->Add(std::move(toStringAndClear));
    return b0.get();
}

void RunInterpolatedStringTransform(ILFunction& fn, bool stringInterpolation = true) {
    StatementTransform st;
    st.AddChild(std::make_unique<ILSpy::Decompiler::IL::ILInlining>());
    st.AddChild(std::make_unique<ILSpy::Decompiler::IL::ExpressionTransforms>());
    st.AddChild(std::make_unique<InterpolatedStringTransform>());
    ILTransformContext ctx;
    ctx.Settings.StringInterpolation = stringInterpolation;
    st.Run(fn, ctx);
}

// Count Block(InterpolatedString) nodes in the function.
int CountInterpolatedStrings(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Block &&
            static_cast<Block*>(inst)->Kind == ILSpy::Decompiler::IL::BlockKind::InterpolatedString)
            ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

} // namespace

// ---- Positive: the fold produces a Block(InterpolatedString) ----

TEST(InterpolatedStringTransform, FoldsAppendLiteralAndAppendFormattedToInterpolatedString) {
    auto v = MakeHandlerLocal("v");
    auto x = std::make_shared<ILVariable>(VariableKind::Local,
                                         std::make_shared<KnownType>(KnownTypeCode::Int32), 0);
    x->Name = "x";
    auto fn = MakeOneBlockFn({v, x});
    std::vector<std::unique_ptr<ILInstruction>> appends;
    appends.push_back(MakeAppendLiteral(v, "Hello"));
    appends.push_back(MakeAppendFormatted(v, std::make_unique<LdLoc>(x)));
    BuildInterpolatedStringShape(*fn, v, std::move(appends), MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountInterpolatedStrings(*fn), 0);

    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountInterpolatedStrings(*fn), 1)
        << "the fold must produce exactly one InterpolatedString block";
    EXPECT_EQ(v->Kind, VariableKind::InitializerTarget);
    auto& b0 = fn->Body->Blocks[0];
    // The InterpolatedString block replaced the stloc+Appends+ToStringAndClear.
    ASSERT_EQ(b0->Instructions.size(), 1u);
    ASSERT_EQ(b0->Instructions[0]->Op, OpCode::Block);
    auto* repl = static_cast<Block*>(b0->Instructions[0].get());
    EXPECT_EQ(repl->Kind, ILSpy::Decompiler::IL::BlockKind::InterpolatedString);
    // Instructions = [stloc v(newobj), AppendLiteral, AppendFormatted].
    EXPECT_EQ(repl->Instructions.size(), 3u);
    EXPECT_EQ(repl->Instructions[0]->Op, OpCode::StLoc);
    EXPECT_EQ(repl->Instructions[1]->Op, OpCode::Call);
    EXPECT_EQ(repl->Instructions[2]->Op, OpCode::Call);
    // The FinalInstruction is the ToStringAndClear call.
    ASSERT_TRUE(repl->FinalInstruction);
    ASSERT_EQ(repl->FinalInstruction->Op, OpCode::Call);
    auto* ts = static_cast<Call*>(repl->FinalInstruction.get());
    EXPECT_EQ(ts->MethodName, "System::DefaultInterpolatedStringHandler::ToStringAndClear");
}

TEST(InterpolatedStringTransform, SeedRendersInterpolatedString) {
    auto v = MakeHandlerLocal("v");
    auto x = std::make_shared<ILVariable>(VariableKind::Local,
                                         std::make_shared<KnownType>(KnownTypeCode::Int32), 0);
    x->Name = "x";
    auto fn = MakeOneBlockFn({v, x});
    std::vector<std::unique_ptr<ILInstruction>> appends;
    appends.push_back(MakeAppendLiteral(v, "Hello"));
    appends.push_back(MakeAppendFormatted(v, std::make_unique<LdLoc>(x)));
    BuildInterpolatedStringShape(*fn, v, std::move(appends), MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILSpy::Decompiler::IL::ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("$\"Hello{x}\""), std::string::npos)
        << "the seed must render the InterpolatedString block as $\"Hello{x}\"; got:\n"
        << text;
}

// ---- Negatives: each gate ----

TEST(InterpolatedStringTransform, RejectsNonLocalVariable) {
    // A StackSlot v (not a Local) -- the pattern requires v.Kind == Local.
    auto v = std::make_shared<ILVariable>(VariableKind::StackSlot, HandlerType(), -1);
    v->Name = "v";
    auto fn = MakeOneBlockFn({v});
    std::vector<std::unique_ptr<ILInstruction>> appends;
    appends.push_back(MakeAppendLiteral(v, "x"));
    BuildInterpolatedStringShape(*fn, v, std::move(appends), MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
    EXPECT_NE(v->Kind, VariableKind::InitializerTarget);
}

TEST(InterpolatedStringTransform, RejectsNonHandlerVariableType) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local,
                                          std::make_shared<KnownType>(KnownTypeCode::Object), 0);
    v->Name = "v";
    auto fn = MakeOneBlockFn({v});
    std::vector<std::unique_ptr<ILInstruction>> appends;
    appends.push_back(MakeAppendLiteral(v, "x"));
    BuildInterpolatedStringShape(*fn, v, std::move(appends), MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsNonNewObjValue) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    // The stloc's Value is a plain call (not a newobj) on the handler.
    auto notNewObj = MakeHandlerCtor(0, 0);
    notNewObj->IsNewObj = false;
    notNewObj->IsInstanceCall = true;  // an instance call, not newobj
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, std::move(notNewObj)));
    b0->Add(MakeAppendLiteral(v, "x"));
    b0->Add(MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsNonHandlerCtorDeclaringType) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    auto ctor = MakeHandlerCtor(0, 0);
    ctor->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Object);  // not the handler
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, std::move(ctor)));
    b0->Add(MakeAppendLiteral(v, "x"));
    b0->Add(MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsNonLdcI4CtorArg) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    auto ctor = MakeHandlerCtor(0, 0);
    ctor->SetChild(1, std::make_unique<LdStr>("not an int"));  // 2nd arg not LdcI4
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, std::move(ctor)));
    b0->Add(MakeAppendLiteral(v, "x"));
    b0->Add(MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsStaticAppendCall) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    auto append = MakeAppendLiteral(v, "x");
    append->IsInstanceCall = false;  // a static call -- the C# rejects !IsStatic
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, MakeHandlerCtor(1, 0)));
    b0->Add(std::move(append));
    b0->Add(MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsUnknownAppendName) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    auto append = MakeAppendLiteral(v, "x");
    append->MethodName = "System::DefaultInterpolatedStringHandler::NotAnAppend";  // unknown name
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, MakeHandlerCtor(1, 0)));
    b0->Add(std::move(append));
    b0->Add(MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsMissingToStringAndClear) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    // No ToStringAndClear after the Append -- the handler is left dangling.
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, MakeHandlerCtor(1, 0)));
    b0->Add(MakeAppendLiteral(v, "x"));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, RejectsWrongUsageCounts) {
    auto v = MakeHandlerLocal("v");
    auto fn = MakeOneBlockFn({v});
    // An extra load of v (ldloc v) somewhere makes LoadCount != 0.
    auto& b0 = fn->Body->Blocks[0];
    b0->Add(std::make_unique<StLoc>(v, MakeHandlerCtor(1, 0)));
    b0->Add(MakeAppendLiteral(v, "x"));
    b0->Add(MakeToStringAndClear(v));
    b0->Add(std::make_unique<LdLoc>(v));  // an extra load -- LoadCount becomes 1
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0);
}

TEST(InterpolatedStringTransform, StringInterpolationOffIsNoOp) {
    auto v = MakeHandlerLocal("v");
    auto x = std::make_shared<ILVariable>(VariableKind::Local,
                                         std::make_shared<KnownType>(KnownTypeCode::Int32), 0);
    x->Name = "x";
    auto fn = MakeOneBlockFn({v, x});
    std::vector<std::unique_ptr<ILInstruction>> appends;
    appends.push_back(MakeAppendLiteral(v, "Hello"));
    appends.push_back(MakeAppendFormatted(v, std::make_unique<LdLoc>(x)));
    BuildInterpolatedStringShape(*fn, v, std::move(appends), MakeToStringAndClear(v));
    ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
    RunInterpolatedStringTransform(*fn, /*stringInterpolation=*/false);
    fn->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(CountInterpolatedStrings(*fn), 0)
        << "with StringInterpolation off the fold must not fire";
}

// ---- mscorlib sweep: the transform fires 0 times on the .NET Framework 4 ----
// legacy-csc corpus (DefaultInterpolatedStringHandler is a .NET 6+ type, absent
// from it); the sweep asserts the ILAst invariant holds across the corpus.

TEST(InterpolatedStringTransform, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int totalFolds = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ILSpy::Decompiler::IL::ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // The pre-pipeline the CLI runs, minus the transforms that need a
        // MetadataFile handle the StatementTransform children don't carry. The
        // InterpolatedStringTransform runs inside the StatementTransform set.
        ILSpy::Decompiler::IL::ComputeVariableUsage(*fn);
        StatementTransform st;
        st.AddChild(std::make_unique<ILSpy::Decompiler::IL::InterpolatedStringTransform>());
        st.Run(*fn, ctx);
        fn->CheckInvariant(ILPhase::Normal);
        totalFolds += CountInterpolatedStrings(*fn);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // DefaultInterpolatedStringHandler is a .NET 6+ type, so the fold fires 0
    // times on the .NET Framework 4 legacy-csc mscorlib corpus; the per-method
    // CheckInvariant above verifies the transform does not crash or corrupt the
    // tree on the corpus.
    EXPECT_EQ(totalFolds, 0);
}
