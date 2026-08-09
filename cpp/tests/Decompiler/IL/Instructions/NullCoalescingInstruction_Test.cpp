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

// Tests for the NullCoalescingInstruction ILAst node -- the C# `??` (null-
// coalescing) operator node the next in-order ExpressionTransforms.VisitCall
// piece (the `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold) and the
// later NullCoalescingTransform build from `if.notnull` block tails. This is a
// tested-but-not-yet-wired foundation (like MatchInstruction / UsingInstruction
// / the NullableLifting helpers): no pipeline transform constructs it yet. The
// tests cover the node invariant/flags/ResultType/dump, the three
// NullCoalescingKind flavours, the UnderlyingResultType field, the two-child
// tree (ValueInst slot 0 inlineable, FallbackInst slot 1), the ILAstToCSharp
// seed rendering of `value ?? fallback`, and a mscorlib sweep that constructs
// NullCoalescingInstructions over real decoded LdLoc operands.

#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

ILVariablePtr MakeLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, type, 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// Build a standalone ILFunction whose body stores a NullCoalescingInstruction
// into a local, so the seed-rendering test exercises a realistic tree (a StLoc
// whose Value is the coalescing expression).
std::unique_ptr<ILFunction> MakeCoalesceFn(ILVariablePtr result,
                                           std::unique_ptr<ILInstruction> value,
                                           std::unique_ptr<ILInstruction> fallback) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(result);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(result,
        std::make_unique<NullCoalescingInstruction>(NullCoalescingKind::Ref,
                                                     std::move(value),
                                                     std::move(fallback))));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

} // namespace

// ---- Node: invariant, flags, ResultType, dump ----

TEST(NullCoalescingInstruction, NodeInvariantFlagsAndDump) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto nc = std::make_unique<NullCoalescingInstruction>(NullCoalescingKind::Ref,
                                                           std::make_unique<LdLoc>(a),
                                                           std::make_unique<LdLoc>(b));
    EXPECT_EQ(nc->Op, OpCode::NullCoalescingInstruction);
    // ResultType is the fallback's ResultType (Object -> O).
    EXPECT_EQ(nc->ResultType(), StackType::O);
    EXPECT_TRUE(HasFlag(nc->DirectFlags(), InstructionFlags::ControlFlow));
    // The default Ref kind does not add a kind suffix to the dump (faithful to
    // the C# WriteToCore which renders `if.notnull(value, fallback)`).
    std::string dump = nc->ToString();
    EXPECT_NE(dump.find("if.notnull("), std::string::npos) << dump;
    EXPECT_EQ(dump.find(".nullable"), std::string::npos) << dump;
    EXPECT_EQ(dump.find(".value"), std::string::npos) << dump;
    ASSERT_EQ(nc->ChildCount(), 2);
    EXPECT_EQ(nc->GetChild(0)->Op, OpCode::LdLoc);
    EXPECT_EQ(nc->GetChild(1)->Op, OpCode::LdLoc);
    EXPECT_EQ(nc->GetChild(0)->Parent, nc.get());
    EXPECT_EQ(nc->GetChild(1)->Parent, nc.get());
    EXPECT_EQ(nc->GetChild(0)->ChildIndex, 0);
    EXPECT_EQ(nc->GetChild(1)->ChildIndex, 1);
    nc->CheckInvariant(ILPhase::Normal);
}

// DirectFlags is ControlFlow; Flags() additionally carries the value/fallback
// flags. A value with a MayThrow fallback propagates MayThrow through
// CombineBranches (the fallback may execute); the ControlFlow flag is always
// present. Mirrors the C# ComputeFlags: ControlFlow | valueInst.Flags |
// CombineBranches(None, fallbackInst.Flags).
TEST(NullCoalescingInstruction, FlagsCombineBranchesFallback) {
    auto a = MakeLocal("a", Object());
    auto nc = std::make_unique<NullCoalescingInstruction>(NullCoalescingKind::Ref,
                                                           std::make_unique<LdLoc>(a),
                                                           std::make_unique<LdNull>());
    InstructionFlags f = nc->Flags();
    EXPECT_TRUE(HasFlag(f, InstructionFlags::ControlFlow));
    // valueInst is a LdLoc -> MayReadLocals; the fallback is LdNull (pure).
    EXPECT_TRUE(HasFlag(f, InstructionFlags::MayReadLocals));
    nc->CheckInvariant(ILPhase::Normal);
}

// The Kind flavours the ILAst dump (Nullable -> `.nullable`, NullableWithValue-
// Fallback -> `.value`), faithful to the semantic distinction the C# back end
// lowers differently. The default Ref kind renders bare (no suffix).
TEST(NullCoalescingInstruction, KindFlavoursDump) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto refNc = std::make_unique<NullCoalescingInstruction>(
        NullCoalescingKind::Ref, std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_NE(refNc->ToString().find("if.notnull("), std::string::npos);

    auto nullableNc = std::make_unique<NullCoalescingInstruction>(
        NullCoalescingKind::Nullable, std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    std::string nullableDump = nullableNc->ToString();
    EXPECT_NE(nullableDump.find("if.notnull.nullable("), std::string::npos) << nullableDump;

    auto valueNc = std::make_unique<NullCoalescingInstruction>(
        NullCoalescingKind::NullableWithValueFallback, std::make_unique<LdLoc>(a),
        std::make_unique<LdLoc>(b));
    std::string valueDump = valueNc->ToString();
    EXPECT_NE(valueDump.find("if.notnull.value("), std::string::npos) << valueDump;
}

// UnderlyingResultType defaults to O (the C# `= StackType.O`); it is the stack
// type the back end uses to recover the result type when Kind == Nullable.
// ResultType is the fallback's ResultType (faithful to the C# getter).
TEST(NullCoalescingInstruction, UnderlyingResultTypeDefaultsToObject) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto nc = std::make_unique<NullCoalescingInstruction>(NullCoalescingKind::Ref,
                                                           std::make_unique<LdLoc>(a),
                                                           std::make_unique<LdLoc>(b));
    EXPECT_EQ(nc->UnderlyingResultType, StackType::O);
    // Mutating UnderlyingResultType does not change ResultType (the getter reads
    // the fallback); the C# CheckInvariant asserts the relation only for
    // Kind == Nullable, which the non-virtual port CheckInvariant does not.
    nc->UnderlyingResultType = StackType::I4;
    EXPECT_EQ(nc->ResultType(), StackType::O);
    nc->CheckInvariant(ILPhase::Normal);
}

// The two children live in typed slots: ValueInst (slot 0) and FallbackInst
// (slot 1). Both are required in the C# (GetChildCount returns 2); this port
// accepts a null slot (a degenerate node the invariant still accepts), but the
// normal construction wires both.
TEST(NullCoalescingInstruction, ChildrenAreInTypedSlots) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto nc = std::make_unique<NullCoalescingInstruction>(NullCoalescingKind::Ref,
                                                           std::make_unique<LdLoc>(a),
                                                           std::make_unique<LdLoc>(b));
    ASSERT_EQ(nc->ChildCount(), 2);
    EXPECT_EQ(nc->GetChild(0)->Op, OpCode::LdLoc);  // ValueInst
    EXPECT_EQ(nc->GetChild(1)->Op, OpCode::LdLoc);  // FallbackInst
    // Re-parenting via SetChildRaw moves the old child out and wires the new
    // one's Parent/ChildIndex (mirrors the other nodes' SetChildRaw contract).
    auto c = MakeLocal("c", Object());
    nc->SetChild(1, std::make_unique<LdLoc>(c));
    EXPECT_EQ(nc->GetChild(1)->Op, OpCode::LdLoc);
    EXPECT_EQ(nc->GetChild(1)->Parent, nc.get());
    EXPECT_EQ(nc->GetChild(1)->ChildIndex, 1);
    nc->CheckInvariant(ILPhase::Normal);
}

// A degenerate node with a null FallbackInst: ResultType falls back to
// UnderlyingResultType (the C# getter returns fallbackInst.ResultType, but a
// null fallback is a degenerate construction the port accepts -- the invariant
// checks tree consistency, not the C# per-node non-null assertion).
TEST(NullCoalescingInstruction, NullFallbackUsesUnderlyingResultType) {
    auto a = MakeLocal("a", Object());
    auto nc = std::make_unique<NullCoalescingInstruction>(NullCoalescingKind::Ref,
                                                           std::make_unique<LdLoc>(a),
                                                           nullptr);
    EXPECT_EQ(nc->ResultType(), StackType::O);  // UnderlyingResultType default
    ASSERT_EQ(nc->ChildCount(), 1);
    EXPECT_EQ(nc->GetChild(0)->Op, OpCode::LdLoc);
    nc->CheckInvariant(ILPhase::Normal);
}

// ---- Seed rendering ----

// The ILAstToCSharp seed renders a NullCoalescingInstruction as `value ?? fallback`
// (the C# null-coalescing operator), matching the real back end's
// VisitNullCoalescingInstruction (a BinaryOperatorExpression with the
// NullCoalescing operator). The Kind is a semantic flavour the back end uses to
// pick the conversion; the surface syntax is the same `??` for all three.
TEST(NullCoalescingInstruction, SeedRendersNullCoalescing) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = MakeCoalesceFn(a, std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a, object b");
    EXPECT_NE(text.find("a ?? b"), std::string::npos) << text;
}

// The NullableWithValueFallback kind (the kind the
// `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold produces) renders the
// same `??` surface syntax; the value side is the nullable and the fallback is
// the non-nullable value type.
TEST(NullCoalescingInstruction, SeedRendersValueFallbackKind) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(a,
        std::make_unique<NullCoalescingInstruction>(
            NullCoalescingKind::NullableWithValueFallback,
            std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b))));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a, object b");
    EXPECT_NE(text.find("a ?? b"), std::string::npos) << text;
}

// A `value ?? null` (the degenerate form with a null fallback) renders the null
// fallback expression.
TEST(NullCoalescingInstruction, SeedRendersNullFallback) {
    auto a = MakeLocal("a", Object());
    auto fn = MakeCoalesceFn(a, std::make_unique<LdLoc>(a), std::make_unique<LdNull>());
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a");
    EXPECT_NE(text.find("a ?? null"), std::string::npos) << text;
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods and, for each method whose body loads a
// variable, construct a NullCoalescingInstruction over two real LdLoc operands
// (new LdLocs of two resolvable variables), set the Kind to each of the three
// flavours, and assert the node invariant holds and the dump renders. This
// exercises the node on thousands of real variables (the volume the foundation
// will see once VisitCall / NullCoalescingTransform are wired).
TEST(NullCoalescingInstruction, MscorlibConstructFromRealLoadsSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int constructed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Find the first two LdLocs with resolvable variables (the value + the
        // fallback of the coalescing expression).
        ILVariablePtr firstVar;
        ILVariablePtr secondVar;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (inst && inst->Op == OpCode::LdLoc) {
                auto* ld = static_cast<LdLoc*>(inst);
                if (ld->Variable) {
                    if (!firstVar) firstVar = ld->Variable;
                    else if (!secondVar && ld->Variable != firstVar) secondVar = ld->Variable;
                }
            }
        });
        if (firstVar && secondVar) {
            for (NullCoalescingKind kind : {NullCoalescingKind::Ref,
                                            NullCoalescingKind::Nullable,
                                            NullCoalescingKind::NullableWithValueFallback}) {
                auto nc = std::make_unique<NullCoalescingInstruction>(kind,
                    std::make_unique<LdLoc>(firstVar), std::make_unique<LdLoc>(secondVar));
                nc->CheckInvariant(ILPhase::Normal);
                std::string dump = nc->ToString();
                EXPECT_NE(dump.find("if.notnull"), std::string::npos) << dump;
            }
            ++constructed;
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}
