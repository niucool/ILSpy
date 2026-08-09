// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the NullableRewrap / NullableUnwrap ILAst nodes -- the C#
// null-conditional (`?.`) operator nodes the next in-order transform
// (NullPropagationTransform) builds. `x?.Member` lowers to
// `nullable.rewrap(Member(nullable.unwrap(x)))`: NullableUnwrap is the `?.`
// deref (carries MayUnwrapNull so the surrounding rewrap can find it), and
// NullableRewrap is the join point (evaluates to null when an inner unwrap
// took the null branch, else its argument). This is a tested-but-not-yet-wired
// foundation (the NullCoalescingInstruction / MatchInstruction /
// UsingInstruction / ThreeValuedBool precedent): no pipeline transform
// constructs these nodes yet. The tests cover the node invariant/flags/
// ResultType/dump, the MayUnwrapNull propagation, the RefInput flavour, the
// single-child tree (Argument slot), the ILAstToCSharp seed rendering, and a
// mscorlib sweep that constructs the nodes over real decoded LdLoc operands.

#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
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

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// Build a standalone ILFunction whose body stores a node into a local, so the
// seed-rendering test exercises a realistic tree (a StLoc whose Value is the
// null-conditional expression).
std::unique_ptr<ILFunction> MakeFn(ILVariablePtr result, std::unique_ptr<ILInstruction> node) {
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

// ---- NullableRewrap: invariant, flags, ResultType, dump ----

TEST(NullableInstructions, RewrapNodeInvariantFlagsAndDump) {
    auto a = MakeLocal("a", Object());
    auto node = std::make_unique<NullableRewrap>(std::make_unique<LdLoc>(a));
    EXPECT_EQ(node->Op, OpCode::NullableRewrap);
    // The result is O for a non-void Argument (the nullable result).
    EXPECT_EQ(node->ResultType(), StackType::O);
    // DirectFlags is ControlFlow (faithful to the C# NullableRewrap.DirectFlags).
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::ControlFlow));
    // The dump renders the faithful ILAst mnemonic `nullable.rewrap(arg)`.
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("nullable.rewrap("), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloc"), std::string::npos) << dump;
}

// NullableRewrap.ResultType is Void for a void Argument (a `?.` statement
// whose value is discarded, e.g. `x?.M();`), faithful to the C# ResultType.
TEST(NullableInstructions, RewrapResultTypeIsVoidForVoidArgument) {
    // A Call with ReturnType Void models a void-returning call (`x?.M();`).
    auto call = std::make_unique<Call>("System.Foo::M");
    call->ReturnType = StackType::Void;
    auto node = std::make_unique<NullableRewrap>(std::move(call));
    EXPECT_EQ(node->ResultType(), StackType::Void);
}

// NullableRewrap.Flags strips the Argument's MayUnwrapNull and
// EndPointUnreachable (the unwrap's null branch is handled here, not a real
// branch past the rewrap) and adds ControlFlow -- faithful to the C#
// ComputeFlags.
TEST(NullableInstructions, RewrapFlagsStripMayUnwrapNullAndAddControlFlow) {
    // A NullableUnwrap argument carries MayUnwrapNull. The rewrap should strip
    // it (the null branch is the rewrap's own control flow) and add ControlFlow.
    auto a = MakeLocal("a", Object());
    auto unwrap = std::make_unique<NullableUnwrap>(StackType::O, std::make_unique<LdLoc>(a));
    auto node = std::make_unique<NullableRewrap>(std::move(unwrap));
    InstructionFlags f = node->Flags();
    // The rewrap adds ControlFlow.
    EXPECT_TRUE(HasFlag(f, InstructionFlags::ControlFlow));
    // The rewrap strips the Argument's MayUnwrapNull (the unwrap's null branch
    // is the rewrap's branch, not a flag that propagates past it).
    EXPECT_FALSE(HasFlag(f, InstructionFlags::MayUnwrapNull));
    // The Argument's MayReadLocals (from the LdLoc) propagates up.
    EXPECT_TRUE(HasFlag(f, InstructionFlags::MayReadLocals));
}

// ---- NullableUnwrap: invariant, flags, ResultType, dump ----

TEST(NullableInstructions, UnwrapNodeInvariantFlagsAndDump) {
    auto a = MakeLocal("a", Object());
    auto node = std::make_unique<NullableUnwrap>(StackType::O, std::make_unique<LdLoc>(a));
    EXPECT_EQ(node->Op, OpCode::NullableUnwrap);
    // The result is the constructor-set unwrapped type.
    EXPECT_EQ(node->ResultType(), StackType::O);
    EXPECT_EQ(node->ResultTypeField, StackType::O);
    // DirectFlags carries MayUnwrapNull (faithful to the C# NullableUnwrap.
    // DirectFlags = base | MayUnwrapNull).
    EXPECT_TRUE(HasFlag(node->DirectFlags(), InstructionFlags::MayUnwrapNull));
    // Flags() propagates MayUnwrapNull up (the base union | MayUnwrapNull), so
    // a surrounding NullableRewrap can find it.
    EXPECT_TRUE(HasFlag(node->Flags(), InstructionFlags::MayUnwrapNull));
    EXPECT_FALSE(node->RefInput);
    EXPECT_FALSE(node->RefOutput());
    // The dump renders `nullable.unwrap.<ResultType>(arg)`.
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("nullable.unwrap.O("), std::string::npos) << dump;
    EXPECT_NE(dump.find("ldloc"), std::string::npos) << dump;
}

// NullableUnwrap carries the RefInput flag (the C# compiler sometimes passes a
// managed reference to the nullable to avoid copying the whole struct before
// the null-check). The dump renders `nullable.unwrap.refinput.<ResultType>`.
TEST(NullableInstructions, UnwrapRefInputFlavour) {
    auto a = MakeLocal("a", Object());
    auto node = std::make_unique<NullableUnwrap>(StackType::O, std::make_unique<LdLoc>(a), true);
    EXPECT_TRUE(node->RefInput);
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("nullable.unwrap.refinput.O("), std::string::npos) << dump;
}

// NullableUnwrap.RefOutput is true when the ResultType is Ref (the generic case
// where the input reference is returned, not the value).
TEST(NullableInstructions, UnwrapRefOutput) {
    auto a = MakeLocal("a", Object());
    auto node = std::make_unique<NullableUnwrap>(StackType::Ref, std::make_unique<LdLoc>(a), true);
    EXPECT_TRUE(node->RefOutput());
    std::string dump = node->ToString();
    EXPECT_NE(dump.find("nullable.unwrap.refinput.Ref("), std::string::npos) << dump;
}

// ---- Children in typed slots (Argument) + re-parenting ----

TEST(NullableInstructions, ChildrenAreInTypedSlots) {
    auto a = MakeLocal("a", Object());
    auto arg = std::make_unique<LdLoc>(a);
    ILInstruction* argPtr = arg.get();
    auto node = std::make_unique<NullableUnwrap>(StackType::O, std::move(arg));
    node->CheckInvariant(ILPhase::Normal);
    EXPECT_EQ(node->ChildCount(), 1);
    EXPECT_EQ(node->GetChild(0), argPtr);
    EXPECT_EQ(argPtr->Parent, node.get());
    EXPECT_EQ(argPtr->ChildIndex, 0);

    // SetChild re-parents: replacing the Argument slot.
    auto b = MakeLocal("b", Object());
    auto newArg = std::make_unique<LdLoc>(b);
    ILInstruction* newArgPtr = newArg.get();
    node->SetChild(0, std::move(newArg));
    EXPECT_EQ(node->GetChild(0), newArgPtr);
    EXPECT_EQ(newArgPtr->Parent, node.get());
    EXPECT_EQ(newArgPtr->ChildIndex, 0);
    EXPECT_NE(node->GetChild(0), argPtr);
    node->CheckInvariant(ILPhase::Normal);
}

// ---- Seed rendering ----

// The ILAstToCSharp seed renders a NullableRewrap as its argument (the rewrap
// is implicit in the `?.` surface syntax, like how box is implicit in C#).
TEST(NullableInstructions, SeedRendersNullableRewrap) {
    auto a = MakeLocal("a", Object());
    // nullable.rewrap(call M(ldloc a)) -- the access chain is a single call.
    auto call = std::make_unique<Call>("System.Foo::M");
    call->ReturnType = StackType::O;
    call->AddArg(std::make_unique<LdLoc>(a));
    auto fn = MakeFn(a, std::make_unique<NullableRewrap>(std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a");
    // The rewrap is implicit; the access chain (the call) renders directly as
    // the flattened `System.Foo.M(a)` (the seed's CallText replaces `::` with `.`).
    EXPECT_NE(text.find("System.Foo.M(a)"), std::string::npos) << text;
    // The rewrap mnemonic must NOT appear in the C# output.
    EXPECT_EQ(text.find("nullable.rewrap"), std::string::npos) << text;
}

// The seed renders a NullableUnwrap with a trailing `?` (the null-conditional),
// a placeholder until the real back end lands (the `?.` postfix sits on the
// receiver of the surrounding member access, which the seed has no context for).
TEST(NullableInstructions, SeedRendersNullableUnwrap) {
    auto a = MakeLocal("a", Object());
    auto fn = MakeFn(a, std::make_unique<NullableUnwrap>(StackType::O, std::make_unique<LdLoc>(a)));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a");
    EXPECT_NE(text.find("a?"), std::string::npos) << text;
    EXPECT_EQ(text.find("nullable.unwrap"), std::string::npos) << text;
}

// A degenerate node with a null argument renders the (default) placeholder
// (the seed never sees this from the pipeline, but it must not crash).
TEST(NullableInstructions, SeedRendersNullArgument) {
    auto a = MakeLocal("a", Object());
    auto fn = MakeFn(a, std::make_unique<NullableRewrap>(nullptr));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object a");
    EXPECT_NE(text.find("(default)"), std::string::npos) << text;
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods and, for each method whose body loads
// at least one variable, construct a NullableRewrap(NullableUnwrap(ldloc v))
// `?.` chain, asserting the node invariant holds, the MayUnwrapNull flag
// propagates from the unwrap to the rewrap's Flags (where it is stripped), and
// the dump renders. This exercises the nodes on thousands of real variables
// (the volume the foundation will see once NullPropagationTransform is wired).
TEST(NullableInstructions, MscorlibConstructFromRealLoadsSweep) {
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
        // Find the first LdLoc with a resolvable variable (the receiver of the
        // `?.` access chain).
        ILVariablePtr firstVar;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (firstVar) return;
            if (inst && inst->Op == OpCode::LdLoc) {
                auto* ld = static_cast<LdLoc*>(inst);
                if (ld->Variable) firstVar = ld->Variable;
            }
        });
        if (firstVar) {
            // x?.M  =>  nullable.rewrap(call M(nullable.unwrap(ldloc x)))
            auto unwrap = std::make_unique<NullableUnwrap>(
                StackType::O, std::make_unique<LdLoc>(firstVar));
            auto call = std::make_unique<Call>("System.Foo::M");
            call->ReturnType = StackType::O;
            call->AddArg(std::move(unwrap));
            auto rewrap = std::make_unique<NullableRewrap>(std::move(call));
            rewrap->CheckInvariant(ILPhase::Normal);
            // The rewrap strips the unwrap's MayUnwrapNull and adds ControlFlow.
            EXPECT_TRUE(HasFlag(rewrap->Flags(), InstructionFlags::ControlFlow));
            EXPECT_FALSE(HasFlag(rewrap->Flags(), InstructionFlags::MayUnwrapNull));
            EXPECT_EQ(rewrap->ResultType(), StackType::O);
            std::string dump = rewrap->ToString();
            EXPECT_NE(dump.find("nullable.rewrap("), std::string::npos) << dump;
            EXPECT_NE(dump.find("nullable.unwrap.O("), std::string::npos) << dump;
            ++constructed;
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}
