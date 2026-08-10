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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the UserDefinedLogicOperator ILAst node -- the C# user-defined
// short-circuiting `&&` / `||` operator node (the `op_BitwiseAnd` / `op_BitwiseOr`
// overloads paired with `op_True` / `op_False`) the next in-order
// UserDefinedLogicTransform (the `LegacyPattern` / `RoslynOptimized` folds) builds.
// This is a tested-but-not-yet-wired foundation (the NullCoalescingInstruction /
// MatchInstruction / UsingInstruction / NumericCompoundAssign /
// UserDefinedCompoundAssign precedent): no pipeline transform constructs this
// node yet. The tests cover the node invariant/flags/ResultType/dump, the
// two-child tree (Left/Right slots) with re-parenting, the Flags() CombineBranches
// propagation for the conditionally-evaluated Right, the ILAstToCSharp seed
// rendering of `left && right` / `left || right`, and a mscorlib sweep that
// constructs the node over real decoded LdLoc operands.

#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
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

// Build a standalone ILFunction whose body stores a UserDefinedLogicOperator
// into a local, so the seed-rendering test exercises a realistic tree (a StLoc
// whose Value is the user-defined logic expression).
std::unique_ptr<ILFunction> MakeLogicFn(ILVariablePtr result,
                                        std::unique_ptr<UserDefinedLogicOperator> node) {
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

// ---- Node invariant, flags, ResultType, dump ----

TEST(UserDefinedLogicOperator, InvariantFlagsAndDump) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto node = std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseAnd", Object(),
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_EQ(node->Op, OpCode::UserDefinedLogicOperator);
    // The result is always O (the operator's return type on the eval stack).
    EXPECT_EQ(node->ResultType(), StackType::O);
    // DirectFlags is MayThrow | SideEffect | ControlFlow (the C# generated
    // override -- a user-defined operator call can throw and short-circuits).
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::ControlFlow));
    // The dump renders the `user.logic` mnemonic + the method + the operands.
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("user.logic "), std::string::npos) << dump;
    EXPECT_NE(dump.find("System.DBNull::op_BitwiseAnd"), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloc"), std::string::npos) << dump;
}

// ---- Children in typed slots (Left/Right) + re-parenting ----

TEST(UserDefinedLogicOperator, ChildrenAreInTypedSlots) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto left = std::make_unique<LdLoc>(a);
    auto right = std::make_unique<LdLoc>(b);
    ILInstruction* leftPtr = left.get();
    ILInstruction* rightPtr = right.get();
    auto node = std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseAnd", Object(), std::move(left), std::move(right));
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

// ---- Flags propagation: DirectFlags | Left | CombineBranches(None, Right) ----

TEST(UserDefinedLogicOperator, FlagsPropagateFromChildren) {
    // LdLoc carries MayReadLocals; the node over two LdLocs should carry it
    // (from the always-evaluated Left) plus the DirectFlags.
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto node = std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseAnd", Object(),
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::MayReadLocals));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::MayThrow));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::ControlFlow));
}

// The Right operand is conditionally evaluated (short-circuit), so its
// EndPointUnreachable does NOT make the node's endpoint unreachable: the Left
// (always executed) keeps the endpoint reachable, and CombineBranches(None,
// right) preserves the union minus the EndPointUnreachable-conjunction rule.
// A node whose Right is a Branch (EndPointUnreachable) but whose Left is pure
// should still have a reachable endpoint (no EndPointUnreachable in Flags).
TEST(UserDefinedLogicOperator, RightShortCircuitDoesNotMakeEndpointUnreachable) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto target = std::make_unique<Block>();
    target->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    Block* targetPtr = target.get();
    fn->Body->AddBlock(std::move(target));
    // A Branch final carries EndPointUnreachable (DirectFlags MayBranch|
    // EndPointUnreachable); use it as the Right operand.
    auto branchRight = std::make_unique<Branch>(targetPtr);
    auto node = std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseOr", Object(),
        std::make_unique<LdLoc>(a), std::move(branchRight));
    // The Left (LdLoc) is always executed and reaches the end, so the node's
    // endpoint is reachable -- CombineBranches(None, EndPointUnreachable)
    // yields the union without the EndPointUnreachable conjunction.
    EXPECT_FALSE(HasFlag(node->Flags(), InstructionFlags::EndPointUnreachable));
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::ControlFlow));
}

// ---- Seed rendering ----

// The seed renders a UserDefinedLogicOperator with op_BitwiseAnd as
// `left && right`, faithful to the real back end's VisitUserDefinedLogicOperator
// (a BinaryOperatorExpression with the ConditionalAnd operator).
TEST(UserDefinedLogicOperator, SeedRendersBitwiseAnd) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = MakeLogicFn(a, std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseAnd", Object(),
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a, object b");
    EXPECT_NE(text.find("a && b"), std::string::npos) << text;
}

// The seed renders a UserDefinedLogicOperator with op_BitwiseOr as
// `left || right`, faithful to the real back end's VisitUserDefinedLogicOperator
// (a BinaryOperatorExpression with the ConditionalOr operator).
TEST(UserDefinedLogicOperator, SeedRendersBitwiseOr) {
    auto a = MakeLocal("a", Object());
    auto b = MakeLocal("b", Object());
    auto fn = MakeLogicFn(a, std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseOr", Object(),
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a, object b");
    EXPECT_NE(text.find("a || b"), std::string::npos) << text;
}

// A degenerate node with a null operand renders the (default) placeholder for
// that side (the seed never sees this from the pipeline, but it must not crash).
TEST(UserDefinedLogicOperator, SeedRendersNullOperand) {
    auto a = MakeLocal("a", Object());
    auto fn = MakeLogicFn(a, std::make_unique<UserDefinedLogicOperator>(
        "System.DBNull::op_BitwiseAnd", Object(),
        std::make_unique<LdLoc>(a), nullptr));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a");
    EXPECT_NE(text.find("&&"), std::string::npos) << text;
    EXPECT_NE(text.find("(default)"), std::string::npos) << text;
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods and, for each method whose body loads
// two distinct variables, construct a UserDefinedLogicOperator over two real
// LdLoc operands, asserting the node invariant holds and the dump renders.
// This exercises the node on thousands of real variables (the volume the
// foundation will see once UserDefinedLogicTransform is wired).
TEST(UserDefinedLogicOperator, MscorlibConstructFromRealLoadsSweep) {
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
        // left + right operands of the user-defined logic expression).
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
            auto node = std::make_unique<UserDefinedLogicOperator>(
                "System.DBNull::op_BitwiseAnd", Object(),
                std::make_unique<LdLoc>(firstVar), std::make_unique<LdLoc>(secondVar));
            node->CheckInvariant(ILPhase::Normal);
            std::string dump = node->ToString();
            EXPECT_NE(dump.find("user.logic"), std::string::npos) << dump;
            EXPECT_EQ(node->ResultType(), StackType::O);
            EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::ControlFlow));
            ++constructed;
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}
