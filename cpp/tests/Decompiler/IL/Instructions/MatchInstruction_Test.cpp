// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for the MatchInstruction ILAst node -- the `is`-pattern node the next
// in-order transform (PatternMatchingTransform) builds from isinst + null-test
// blocks. This is a tested-but-not-yet-wired foundation (like LongSet /
// LoopContext / the NullableLifting helpers): no pipeline transform constructs
// it yet. The tests cover the node invariant/flags/ResultType/dump, IsVar /
// HasDesignator, the SubPatterns collection, the store-counting in
// ComputeVariableUsage (the node is an IStoreInstruction), the IsPatternMatch
// helper, the ILAstToCSharp seed rendering of `is` patterns, and a mscorlib
// sweep that constructs MatchInstructions from real decoded LdLoc operands.

#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <cstdint>
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
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

ILVariablePtr MakeLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, type, 0);
    v->Name = std::move(name);
    return v;
}

// A pattern-local (the kind PatternMatchingTransform sets on the captured var).
ILVariablePtr MakePatternLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::PatternLocal, type, 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

// The node: invariant, flags, ResultType, dump for the three pattern flavours.
TEST(MatchInstruction, NodeInvariantFlagsAndDump) {
    auto arg = MakeLocal("arg", Object());
    auto v = MakePatternLocal("x", String());
    auto m = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    EXPECT_EQ(m->Op, OpCode::MatchInstruction);
    EXPECT_EQ(m->ResultType(), StackType::I4);
    EXPECT_EQ(m->ChildCount(), 1);  // TestedOperand only
    EXPECT_TRUE(HasFlag(m->DirectFlags(), InstructionFlags::MayWriteLocals));
    EXPECT_TRUE(HasFlag(m->DirectFlags(), InstructionFlags::SideEffect));
    EXPECT_TRUE(HasFlag(m->DirectFlags(), InstructionFlags::MayThrow));
    EXPECT_TRUE(HasFlag(m->DirectFlags(), InstructionFlags::ControlFlow));
    // `match(x = arg)` -- the var pattern.
    EXPECT_TRUE(m->IsVar());
    EXPECT_NE(m->ToString().find("match("), std::string::npos) << m->ToString();
    m->CheckInvariant(ILPhase::Normal);

    // match.notnull(x = arg)
    auto m2 = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    m2->CheckNotNull = true;
    EXPECT_FALSE(m2->IsVar());
    EXPECT_NE(m2->ToString().find("match.notnull("), std::string::npos) << m2->ToString();
    m2->CheckInvariant(ILPhase::Normal);

    // match.type[Type](x = arg)
    auto m3 = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    m3->CheckType = true;
    EXPECT_FALSE(m3->IsVar());
    EXPECT_NE(m3->ToString().find("match.type["), std::string::npos) << m3->ToString();
    m3->CheckInvariant(ILPhase::Normal);
}

// IsVar is true only for the bare var capture; HasDesignator tracks whether the
// variable is used beyond the sub-patterns.
TEST(MatchInstruction, IsVarAndHasDesignator) {
    auto arg = MakeLocal("arg", Object());
    auto v = MakePatternLocal("x", String());

    auto m = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    EXPECT_TRUE(m->IsVar());
    // No loads, no addresses, no sub-patterns: 0 > 0 is false -- no designator.
    EXPECT_FALSE(m->HasDesignator());

    // A loaded pattern variable has a designator.
    v->LoadCount = 1;
    EXPECT_TRUE(m->HasDesignator());

    // CheckType clears IsVar.
    m->CheckType = true;
    EXPECT_FALSE(m->IsVar());
    // CheckNotNull clears IsVar too.
    auto m2 = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    m2->CheckNotNull = true;
    EXPECT_FALSE(m2->IsVar());
}

// SubPatterns are owned children in the tree; AddSubPattern wires parent/index
// and the invariant holds with sub-patterns present.
TEST(MatchInstruction, SubPatternsAreChildren) {
    auto arg = MakeLocal("arg", Object());
    auto v = MakePatternLocal("x", String());
    auto m = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    // Sub-pattern: comp(ldloc v == ldc.i4 42) -- a constant relational pattern.
    auto sub = std::make_unique<Comp>(std::make_unique<LdLoc>(v),
                                      std::make_unique<LdcI4>(42),
                                      ComparisonKind::Equality);
    ILInstruction* subPtr = sub.get();
    m->AddSubPattern(std::move(sub));
    ASSERT_EQ(m->ChildCount(), 2);
    EXPECT_EQ(m->GetChild(1), subPtr);
    EXPECT_EQ(subPtr->Parent, m.get());
    EXPECT_EQ(subPtr->ChildIndex, 1);
    m->CheckInvariant(ILPhase::Normal);
    EXPECT_NE(m->ToString().find("match("), std::string::npos);
    EXPECT_NE(m->ToString().find("comp(eq"), std::string::npos);
}

// ComputeVariableUsage counts a MatchInstruction as a store to its variable
// (it is an IStoreInstruction in the C#; Connected() adds it to the store list).
TEST(MatchInstruction, CountsAsStoreInVariableUsage) {
    auto arg = MakeLocal("arg", Object());
    auto v = MakePatternLocal("x", String());

    auto fn = std::make_unique<ILFunction>();
    fn->Variables.push_back(arg);
    fn->Variables.push_back(v);
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    auto block = std::make_unique<Block>();
    // Use the matched variable once so HasDesignator holds and the load is real.
    v->LoadCount = 0;  // recomputed below
    auto m = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    m->CheckType = true;
    auto iff = std::make_unique<IfInstruction>(std::move(m),
                                              std::make_unique<Leave>(fn->Body.get()));
    block->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(block));

    ComputeVariableUsage(*fn);
    // The MatchInstruction store landed on the pattern variable.
    EXPECT_GE(v->StoreCount, 1);
    // The tested operand is a load of arg.
    EXPECT_GE(arg->LoadCount, 1);
    fn->CheckInvariant(ILPhase::Normal);
}

// IsPatternMatch: a MatchInstruction is itself a pattern (testedOperand =
// TestedOperand); a Comp with a constant right is a constant/relational
// pattern; logic.not of a pattern is a combinator; a string op_Equality call
// is a string constant pattern.
TEST(MatchInstruction, IsPatternMatchRecognisesPatterns) {
    ILTransformSettings settings;  // all features on (defaults)
    auto arg = MakeLocal("arg", Object());
    auto v = MakePatternLocal("x", String());

    // match.type[T](v = arg) is a pattern; testedOperand is the LdLoc(arg).
    auto m = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    m->CheckType = true;
    const ILInstruction* tested = nullptr;
    ASSERT_TRUE(MatchInstruction::IsPatternMatch(m.get(), tested, &settings));
    ASSERT_NE(tested, nullptr);
    EXPECT_EQ(tested->Op, OpCode::LdLoc);

    // comp(ldloc v == ldc.i4 42) is a constant pattern (Equality + constant).
    auto comp = std::make_unique<Comp>(std::make_unique<LdLoc>(v),
                                        std::make_unique<LdcI4>(42),
                                        ComparisonKind::Equality);
    tested = nullptr;
    EXPECT_TRUE(MatchInstruction::IsPatternMatch(comp.get(), tested, &settings));
    EXPECT_EQ(tested, comp->Left.get());

    // comp(ldloc v < ldc.i4 42) is a relational pattern with RelationalPatterns on.
    auto rel = std::make_unique<Comp>(std::make_unique<LdLoc>(v),
                                       std::make_unique<LdcI4>(42),
                                       ComparisonKind::LessThan);
    tested = nullptr;
    EXPECT_TRUE(MatchInstruction::IsPatternMatch(rel.get(), tested, &settings));

    // RelationalPatterns off: a LessThan is not a pattern (only == / != survive).
    settings.RelationalPatterns = false;
    tested = nullptr;
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(rel.get(), tested, &settings));
    settings.RelationalPatterns = true;

    // A Comp whose right is not a constant is not a pattern.
    auto nonConst = std::make_unique<Comp>(std::make_unique<LdLoc>(v),
                                            std::make_unique<LdLoc>(arg),
                                            ComparisonKind::Equality);
    tested = nullptr;
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(nonConst.get(), tested, &settings));

    // logic.not(pattern) = comp(eq, pattern, ldc.i4 0) is a combinator pattern
    // gated on PatternCombinators.
    auto inner = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    inner->CheckType = true;
    auto notPat = std::make_unique<Comp>(std::move(inner),
                                          std::make_unique<LdcI4>(0),
                                          ComparisonKind::Equality);
    tested = nullptr;
    EXPECT_TRUE(MatchInstruction::IsPatternMatch(notPat.get(), tested, &settings));
    EXPECT_EQ(tested->Op, OpCode::LdLoc);  // unwrapped to the inner's testedOperand
    // PatternCombinators off: the negated pattern is not recognized.
    settings.PatternCombinators = false;
    tested = nullptr;
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(notPat.get(), tested, &settings));
    settings.PatternCombinators = true;

    // null / unhandled kinds are not patterns.
    tested = nullptr;
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(nullptr, tested, &settings));
    auto ld = std::make_unique<LdLoc>(arg);
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(ld.get(), tested, &settings));
}

// IsPatternMatch: a `string.op_Equality(x, "lit")` call is a string constant
// pattern (the C# `case "lit"` / `is "lit"` shape).
TEST(MatchInstruction, IsPatternMatchStringOpEquality) {
    ILTransformSettings settings;
    auto arg = MakeLocal("arg", String());
    auto call = std::make_unique<Call>("System.String::op_Equality");
    call->DeclaringType = String();
    call->AddArg(std::make_unique<LdLoc>(arg));
    call->AddArg(std::make_unique<LdStr>("lit"));
    const ILInstruction* tested = nullptr;
    ASSERT_TRUE(MatchInstruction::IsPatternMatch(call.get(), tested, &settings));
    ASSERT_NE(tested, nullptr);
    EXPECT_EQ(tested->Op, OpCode::LdLoc);

    // A non-String op_Equality is not a string pattern.
    auto call2 = std::make_unique<Call>("System.Int32::op_Equality");
    call2->DeclaringType = Int32();
    call2->AddArg(std::make_unique<LdLoc>(arg));
    call2->AddArg(std::make_unique<LdStr>("lit"));
    tested = nullptr;
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(call2.get(), tested, &settings));

    // A String op_Equality whose second arg is not a string literal is not a
    // string constant pattern.
    auto call3 = std::make_unique<Call>("System.String::op_Equality");
    call3->DeclaringType = String();
    call3->AddArg(std::make_unique<LdLoc>(arg));
    call3->AddArg(std::make_unique<LdLoc>(arg));
    tested = nullptr;
    EXPECT_FALSE(MatchInstruction::IsPatternMatch(call3.get(), tested, &settings));
}

// The ILAstToCSharp seed renders a MatchInstruction condition as the C# `is`
// pattern: `expr is var x`, `expr is T x`, `expr is {} x`, `expr is T`.
TEST(MatchInstruction, SeedRendersIsPatterns) {
    auto arg = MakeLocal("arg", Object());
    auto makeFn = [&](std::unique_ptr<MatchInstruction> m) {
        auto fn = std::make_unique<ILFunction>();
        fn->Body = std::make_unique<BlockContainer>();
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        auto b0 = std::make_unique<Block>();
        b0->SetFinal(std::make_unique<IfInstruction>(std::move(m),
                      std::make_unique<Branch>(static_cast<std::uint32_t>(0x20))));
        fn->Body->AddBlock(std::move(b0));
        auto b1 = std::make_unique<Block>();
        b1->StartILOffset = 0x20;
        b1->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        fn->Body->AddBlock(std::move(b1));
        return fn;
    };

    // `arg is var x`
    auto v = MakePatternLocal("x", Object());
    auto m = std::make_unique<MatchInstruction>(v, std::make_unique<LdLoc>(arg));
    auto fn = makeFn(std::move(m));
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "object arg");
    EXPECT_NE(text.find("if (arg is var x)"), std::string::npos) << text;

    // `arg is string x` (CheckType + the variable is loaded once -> designator)
    auto v2 = MakePatternLocal("x", String());
    v2->LoadCount = 1;
    auto m2 = std::make_unique<MatchInstruction>(v2, std::make_unique<LdLoc>(arg));
    m2->CheckType = true;
    auto fn2 = makeFn(std::move(m2));
    fn2->CheckInvariant(ILPhase::Normal);
    std::string text2 = ILAstToCSharp(*fn2, "void", "M", "object arg");
    EXPECT_NE(text2.find("if (arg is string x)"), std::string::npos) << text2;

    // `arg is {} x` (CheckNotNull, no type)
    auto v3 = MakePatternLocal("x", Object());
    v3->LoadCount = 1;
    auto m3 = std::make_unique<MatchInstruction>(v3, std::make_unique<LdLoc>(arg));
    m3->CheckNotNull = true;
    auto fn3 = makeFn(std::move(m3));
    fn3->CheckInvariant(ILPhase::Normal);
    std::string text3 = ILAstToCSharp(*fn3, "void", "M", "object arg");
    EXPECT_NE(text3.find("if (arg is {} x)"), std::string::npos) << text3;

    // `arg is string` (CheckType, no designator: no loads/addresses)
    auto v4 = MakePatternLocal("x", String());
    auto m4 = std::make_unique<MatchInstruction>(v4, std::make_unique<LdLoc>(arg));
    m4->CheckType = true;
    auto fn4 = makeFn(std::move(m4));
    fn4->CheckInvariant(ILPhase::Normal);
    std::string text4 = ILAstToCSharp(*fn4, "void", "M", "object arg");
    EXPECT_NE(text4.find("if (arg is string)"), std::string::npos) << text4;
    // The no-designator form must not splice the variable name in.
    EXPECT_EQ(text4.find("if (arg is string x)"), std::string::npos) << text4;
}

// The pattern-matching settings default to true (matching DecompilerSettings).
TEST(MatchInstruction, PatternMatchingSettingsDefaultTrue) {
    ILTransformSettings settings;
    EXPECT_TRUE(settings.PatternMatching);
    EXPECT_TRUE(settings.RecursivePatternMatching);
    EXPECT_TRUE(settings.PatternCombinators);
    EXPECT_TRUE(settings.RelationalPatterns);
}

// A mscorlib sweep: decode real methods and, for each method that loads a
// variable, construct a MatchInstruction from that real LdLoc operand (a new
// LdLoc of the same variable). The node invariant must hold, IsPatternMatch
// must recognize it, and the dump must render the match head. This exercises
// the node on thousands of real variables/types (the volume the foundation
// will see once PatternMatchingTransform is wired).
TEST(MatchInstruction, MscorlibConstructFromRealLoadsSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    ILTransformSettings settings;
    int processed = 0;
    int constructed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Find the first LdLoc with a resolvable variable.
        ILVariablePtr firstVar;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (firstVar) return;
            if (inst && inst->Op == OpCode::LdLoc) {
                auto* ld = static_cast<LdLoc*>(inst);
                if (ld->Variable) firstVar = ld->Variable;
            }
        });
        if (firstVar) {
            auto patVar = std::make_shared<ILVariable>(VariableKind::PatternLocal,
                                                      firstVar->Type, 0);
            patVar->Name = firstVar->Name + "_p";
            auto m2 = std::make_unique<MatchInstruction>(
                patVar, std::make_unique<LdLoc>(firstVar));
            m2->CheckType = true;
            m2->CheckInvariant(ILPhase::Normal);
            const ILInstruction* tested = nullptr;
            ASSERT_TRUE(MatchInstruction::IsPatternMatch(m2.get(), tested, &settings));
            ASSERT_NE(tested, nullptr);
            EXPECT_EQ(tested->Op, OpCode::LdLoc);
            EXPECT_NE(m2->ToString().find("match.type["), std::string::npos);
            ++constructed;
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}
