// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the ThreeValuedBoolAnd / ThreeValuedBoolOr ILAst nodes -- the C#
// three-valued logic `&` / `|` on `bool?` (Nullable<bool>) nodes the next
// in-order NullableLiftingTransform.Run(IfInstruction) piece (the `&` / `|` on
// bool? path) builds. This is a tested-but-not-yet-wired foundation (the
// NullCoalescingInstruction / MatchInstruction / UsingInstruction / NullableLift
// ing-helpers precedent): no pipeline transform constructs these nodes yet.
// The tests cover the node invariant/flags/ResultType/dump, IsLifted /
// UnderlyingResultType, the two-child tree (Left/Right slots), the
// ILAstToCSharp seed rendering of `left & right` / `left | right`, and a
// mscorlib sweep that constructs the nodes over real decoded LdLoc operands.

#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
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

// Build a standalone ILFunction whose body stores a ThreeValuedBool node into a
// local, so the seed-rendering test exercises a realistic tree (a StLoc whose
// Value is the three-valued expression).
std::unique_ptr<ILFunction> MakeTvFn(ILVariablePtr result, std::unique_ptr<ILInstruction> node) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(result);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(result, std::move(node)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

} // namespace

// ---- ThreeValuedBoolAnd: invariant, flags, ResultType, dump ----

TEST(ThreeValuedBoolInstructions, AndNodeInvariantFlagsAndDump) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto node = std::make_unique<ThreeValuedBoolAnd>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_EQ(node->Op, OpCode::ThreeValuedBoolAnd);
    // The result is always bool? (StackType::O).
    EXPECT_EQ(node->ResultType(), StackType::O);
    // DirectFlags is None (inherited from BinaryInstruction).
    EXPECT_FALSE(HasFlag(node->DirectFlags(), InstructionFlags::ControlFlow));
    EXPECT_FALSE(HasFlag(node->DirectFlags(), InstructionFlags::SideEffect));
    // The dump renders the faithful ILAst mnemonic `3vl.bool.and(left, right)`.
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("3vl.bool.and("), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloc"), std::string::npos) << dump;
}

TEST(ThreeValuedBoolInstructions, OrNodeInvariantFlagsAndDump) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto node = std::make_unique<ThreeValuedBoolOr>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_EQ(node->Op, OpCode::ThreeValuedBoolOr);
    EXPECT_EQ(node->ResultType(), StackType::O);
    EXPECT_FALSE(HasFlag(node->DirectFlags(), InstructionFlags::ControlFlow));
    EXPECT_FALSE(HasFlag(node->DirectFlags(), InstructionFlags::SideEffect));
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("3vl.bool.or("), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloc"), std::string::npos) << dump;
}

// ---- IsLifted / UnderlyingResultType (the ILiftableInterface impl) ----

TEST(ThreeValuedBoolInstructions, IsLiftedAndUnderlyingResultType) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto andNode = std::make_unique<ThreeValuedBoolAnd>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    // Faithful to ILiftableInstruction.IsLifted: a ThreeValuedBool node is
    // always a lifted operation (its result is the nullable bool?).
    EXPECT_TRUE(andNode->IsLifted());
    // Faithful to ILiftableInstruction.UnderlyingResultType: the underlying
    // (non-nullable) result is a Boolean (I4 on the eval stack).
    EXPECT_EQ(andNode->UnderlyingResultType(), StackType::I4);

    auto orNode = std::make_unique<ThreeValuedBoolOr>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_TRUE(orNode->IsLifted());
    EXPECT_EQ(orNode->UnderlyingResultType(), StackType::I4);
}

// ---- Children in typed slots (Left/Right) + re-parenting ----

TEST(ThreeValuedBoolInstructions, ChildrenAreInTypedSlots) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto left = std::make_unique<LdLoc>(a);
    auto right = std::make_unique<LdLoc>(b);
    ILInstruction* leftPtr = left.get();
    ILInstruction* rightPtr = right.get();
    auto node = std::make_unique<ThreeValuedBoolAnd>(std::move(left), std::move(right));
    node->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(node->ChildCount(), 2);
    EXPECT_EQ(node->GetChild(0), leftPtr);
    EXPECT_EQ(node->GetChild(1), rightPtr);
    // The base BinaryInstruction wires Parent + ChildIndex for both children.
    EXPECT_EQ(leftPtr->Parent, node.get());
    EXPECT_EQ(leftPtr->ChildIndex, 0);
    EXPECT_EQ(rightPtr->Parent, node.get());
    EXPECT_EQ(rightPtr->ChildIndex, 1);

    // SetChild re-parents: swapping the Right slot replaces the old occupant.
    auto c = MakeLocal("c", Object());
    auto newRight = std::make_unique<LdLoc>(c);
    ILInstruction* newRightPtr = newRight.get();
    node->SetChild(1, std::move(newRight));
    EXPECT_EQ(node->GetChild(1), newRightPtr);
    EXPECT_EQ(newRightPtr->Parent, node.get());
    EXPECT_EQ(newRightPtr->ChildIndex, 1);
    // The displaced old Right (rightPtr) is no longer a child.
    EXPECT_NE(node->GetChild(1), rightPtr);
    node->CheckInvariant(ILPhase::Normal);
}

// ---- Flags propagation: None | Left | Right == the C# ComputeFlags ----

TEST(ThreeValuedBoolInstructions, FlagsPropagateFromChildren) {
    // A Call carries SideEffect|MayThrow (DirectFlags); a ThreeValuedBool node
    // over a Call operand should reflect the operand's flags (the base Flags()
    // = None | Left | Right), faithful to the C# ComputeFlags
    // (left.Flags | right.Flags | None).
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    // LdLoc carries MayReadLocals; a node over two LdLocs should carry it.
    auto node = std::make_unique<ThreeValuedBoolAnd>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::MayReadLocals));
    // DirectFlags is None, so a node over pure operands carries no
    // side-effect/throw/control-flow flags.
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::SideEffect));
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::MayThrow));
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::ControlFlow));
}

// ---- Seed rendering ----

// The ILAstToCSharp seed renders a ThreeValuedBoolAnd as `left & right` (the C#
// `&` on bool?), faithful to the real back end's VisitThreeValuedBoolAnd (a
// BinaryOperatorExpression with the BitwiseAnd operator). Unlike logic.and()
// this does not short-circuit.
TEST(ThreeValuedBoolInstructions, SeedRendersThreeValuedBoolAnd) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = MakeTvFn(a, std::make_unique<ThreeValuedBoolAnd>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a, object b");
    EXPECT_NE(text.find("a & b"), std::string::npos) << text;
}

// The seed renders a ThreeValuedBoolOr as `left | right` (the C# `|` on bool?).
TEST(ThreeValuedBoolInstructions, SeedRendersThreeValuedBoolOr) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = MakeTvFn(a, std::make_unique<ThreeValuedBoolOr>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a, object b");
    EXPECT_NE(text.find("a | b"), std::string::npos) << text;
}

// A degenerate node with a null operand renders the (default) placeholder
// for that side (the seed never sees this from the pipeline, but it must not
// crash -- the Expr path falls back to `(default)` for a null child).
TEST(ThreeValuedBoolInstructions, SeedRendersNullOperand) {
    auto a = MakeLocal("a", Object());
    auto fn = MakeTvFn(a, std::make_unique<ThreeValuedBoolAnd>(
        std::make_unique<LdLoc>(a), nullptr));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a");
    EXPECT_NE(text.find("&"), std::string::npos) << text;
    EXPECT_NE(text.find("(default)"), std::string::npos) << text;
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods and, for each method whose body loads
// two distinct variables, construct a ThreeValuedBoolAnd and a ThreeValuedBoolOr
// over two real LdLoc operands, asserting the node invariant holds and the dump
// renders. This exercises the nodes on thousands of real variables (the volume
// the foundation will see once the `&`/`|` on bool? path is wired).
TEST(ThreeValuedBoolInstructions, MscorlibConstructFromRealLoadsSweep) {
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
        // Find the first two LdLocs with resolvable, distinct variables (the
        // left + right operands of the three-valued expression).
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
            auto andNode = std::make_unique<ThreeValuedBoolAnd>(
                std::make_unique<LdLoc>(firstVar), std::make_unique<LdLoc>(secondVar));
            andNode->CheckInvariant(ILPhase::Normal);
            std::string andDump = andNode->ToString();
            EXPECT_NE(andDump.find("3vl.bool.and"), std::string::npos) << andDump;
            EXPECT_EQ(andNode->ResultType(), StackType::O);
            EXPECT_TRUE(andNode->IsLifted());
            EXPECT_EQ(andNode->UnderlyingResultType(), StackType::I4);

            auto orNode = std::make_unique<ThreeValuedBoolOr>(
                std::make_unique<LdLoc>(firstVar), std::make_unique<LdLoc>(secondVar));
            orNode->CheckInvariant(ILPhase::Normal);
            std::string orDump = orNode->ToString();
            EXPECT_NE(orDump.find("3vl.bool.or"), std::string::npos) << orDump;
            EXPECT_EQ(orNode->ResultType(), StackType::O);
            ++constructed;
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}
