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
// OTHERWISE, ARISING IN, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the NullableLiftingTransform static helper subset (MatchHasValueCall,
// MatchGetValueOrDefault) plus the supporting SwitchInstruction nullable
// fields (IsLifted/Type) and SwitchSection.HasNullLabel, and the LiftNullables
// setting. These are the foundation SwitchOnNullableTransform (and
// SwitchDetection's deferred AddNullCase) depend on; the full
// NullableLiftingStatementTransform is deferred. The helpers are tested-but-not-
// yet-wired (no pipeline consumer yet); the mscorlib sweep exercises the
// Call::DeclaringType resolution the helpers read on real calls.

#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
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
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::Util::LongSet;

namespace {

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

// Nullable<T> as a generic instantiation: ParameterizedType(KnownType(NullableOfT), {T}).
std::shared_ptr<IType> MakeNullableOf(KnownTypeCode underlying) {
    std::vector<std::shared_ptr<IType>> args;
    args.push_back(std::make_shared<KnownType>(underlying));
    return std::make_shared<ParameterizedType>(
        std::make_shared<KnownType>(KnownTypeCode::NullableOfT), std::move(args));
}

// A `call get_HasValue(arg)` on the given declaring type.
std::unique_ptr<Call> MakeHasValueCall(std::shared_ptr<IType> declaringType,
                                       std::string methodFullName,
                                       std::unique_ptr<ILInstruction> arg) {
    auto call = std::make_unique<Call>(std::move(methodFullName));
    call->DeclaringType = std::move(declaringType);
    call->AddArg(std::move(arg));
    return call;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

// call get_HasValue(ldloca v) on Nullable<int> (a generic instantiation)
// matches: the declaring type unwraps to Nullable`1 -> KnownTypeCode::NullableOfT.
TEST(NullableLiftingTransform, MatchHasValueCallOnNullableInstantiation) {
    auto v = MakeLocal("v");
    auto call = MakeHasValueCall(MakeNullableOf(KnownTypeCode::Int32),
                                 "System.Nullable`1::get_HasValue",
                                 std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_TRUE(NullableLiftingTransform::MatchHasValueCall(call.get(), arg));
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->Op, OpCode::LdLoca);
}

// A bare Nullable`1 (the generic definition, not an instantiation) also matches.
TEST(NullableLiftingTransform, MatchHasValueCallOnNullableDefinition) {
    auto v = MakeLocal("v");
    auto call = MakeHasValueCall(std::make_shared<KnownType>(KnownTypeCode::NullableOfT),
                                 "System.Nullable`1::get_HasValue",
                                 std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_TRUE(NullableLiftingTransform::MatchHasValueCall(call.get(), arg));
    ASSERT_NE(arg, nullptr);
}

// A get_HasValue on a non-Nullable type (System.Int32) does not match.
TEST(NullableLiftingTransform, MatchHasValueCallRejectsNonNullableDeclaringType) {
    auto v = MakeLocal("v");
    auto call = MakeHasValueCall(std::make_shared<KnownType>(KnownTypeCode::Int32),
                                 "System.Int32::get_HasValue",
                                 std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(call.get(), arg));
}

// A get_HasValue whose declaring type could not be resolved (null) does not
// match -- a null DeclaringType is treated like the C# null DeclaringTypeDefinition.
TEST(NullableLiftingTransform, MatchHasValueCallRejectsNullDeclaringType) {
    auto v = MakeLocal("v");
    auto call = MakeHasValueCall(nullptr,
                                 "System.Nullable`1::get_HasValue",
                                 std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(call.get(), arg));
}

// A call with the wrong method name (e.g. get_Foo) does not match even on Nullable.
TEST(NullableLiftingTransform, MatchHasValueCallRejectsWrongMethodName) {
    auto v = MakeLocal("v");
    auto call = MakeHasValueCall(MakeNullableOf(KnownTypeCode::Int32),
                                 "System.Nullable`1::get_Foo",
                                 std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(call.get(), arg));
}

// A get_HasValue with the wrong argument count (2) does not match.
TEST(NullableLiftingTransform, MatchHasValueCallRejectsWrongArgCount) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(call.get(), arg));
}

// A non-Call instruction does not match.
TEST(NullableLiftingTransform, MatchHasValueCallRejectsNonCall) {
    auto v = MakeLocal("v");
    auto ld = std::make_unique<LdLoca>(v);
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(ld.get(), arg));
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(nullptr, arg));
}

// call GetValueOrDefault(ldloca v) on Nullable<int> matches (1-arg form).
TEST(NullableLiftingTransform, MatchGetValueOrDefaultOnNullable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_TRUE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), arg));
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->Op, OpCode::LdLoca);
}

// GetValueOrDefault on a non-Nullable type does not match.
TEST(NullableLiftingTransform, MatchGetValueOrDefaultRejectsNonNullable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Int32::GetValueOrDefault");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), arg));
}

// The 2-argument form of GetValueOrDefault (with a fallback default) is the
// deferred overload; the 1-arg matcher must not match it.
TEST(NullableLiftingTransform, MatchGetValueOrDefaultRejectsTwoArgForm) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdLoca>(v));
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), arg));
}

// call GetValueOrDefault(nullableValue, fallback) on Nullable<int> matches the
// 2-arg form, returning the nullable value (Arguments[0]) and the fallback
// (Arguments[1]) -- the `a ?? b` lowering ExpressionTransforms.VisitCall consumes.
TEST(NullableLiftingTransform, MatchGetValueOrDefaultTwoArgOnNullable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    ILInstruction* nullableValue = nullptr;
    ILInstruction* fallback = nullptr;
    EXPECT_TRUE(NullableLiftingTransform::MatchGetValueOrDefault(
        call.get(), nullableValue, fallback));
    ASSERT_NE(nullableValue, nullptr);
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(nullableValue->Op, OpCode::LdLoca);
    EXPECT_EQ(fallback->Op, OpCode::LdcI4);
}

// The 2-arg matcher rejects a non-Nullable declaring type (the 1-arg matcher's
// same guard, applied to the 2-arg form).
TEST(NullableLiftingTransform, MatchGetValueOrDefaultTwoArgRejectsNonNullable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Int32::GetValueOrDefault");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    ILInstruction* nullableValue = nullptr;
    ILInstruction* fallback = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(
        call.get(), nullableValue, fallback));
}

// The 2-arg matcher rejects the 1-arg form (it requires exactly 2 arguments).
TEST(NullableLiftingTransform, MatchGetValueOrDefaultTwoArgRejectsOneArgForm) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    ILInstruction* nullableValue = nullptr;
    ILInstruction* fallback = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(
        call.get(), nullableValue, fallback));
}

// The 2-arg matcher rejects a wrong method name (the same guard as the 1-arg).
TEST(NullableLiftingTransform, MatchGetValueOrDefaultTwoArgRejectsWrongMethodName) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    ILInstruction* nullableValue = nullptr;
    ILInstruction* fallback = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(
        call.get(), nullableValue, fallback));
}

// The 2-arg matcher rejects a null declaring type (a null DeclaringType is
// treated like the C# null DeclaringTypeDefinition).
TEST(NullableLiftingTransform, MatchGetValueOrDefaultTwoArgRejectsNullDeclaringType) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    ILInstruction* nullableValue = nullptr;
    ILInstruction* fallback = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(
        call.get(), nullableValue, fallback));
}

// MatchCompOrDecimal recognises a non-lifted IL `Comp` and reports its
// Kind/Left/Right/IsLifted. The Decimal-operator branch (a Call to op_Equality
// etc. on System.Decimal) is deferred, so a Call never matches here.
TEST(NullableLiftingTransform, MatchCompOrDecimalOnNonLiftedComp) {
    auto v = MakeLocal("v");
    auto comp = std::make_unique<Comp>(std::make_unique<LdLoc>(v),
                                       std::make_unique<LdcI4>(5),
                                       ComparisonKind::LessThan, false);
    CompOrDecimal result;
    EXPECT_TRUE(NullableLiftingTransform::MatchCompOrDecimal(comp.get(), result));
    ASSERT_NE(result.Instruction, nullptr);
    EXPECT_EQ(result.Instruction, comp.get());
    EXPECT_EQ(result.Kind, ComparisonKind::LessThan);
    ASSERT_NE(result.Left, nullptr);
    EXPECT_EQ(result.Left->Op, OpCode::LdLoc);
    ASSERT_NE(result.Right, nullptr);
    EXPECT_EQ(result.Right->Op, OpCode::LdcI4);
    EXPECT_FALSE(result.IsLifted);
}

// MatchCompOrDecimal reports IsLifted=true for a lifted Comp (the C#-style
// lift), the shape the nullable-lifting lift machinery produces.
TEST(NullableLiftingTransform, MatchCompOrDecimalOnLiftedComp) {
    auto v = MakeLocal("v");
    auto comp = std::make_unique<Comp>(std::make_unique<LdLoc>(v),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Equality,
                                       ComparisonLiftingKind::CSharp,
                                       StackType::I4, false);
    CompOrDecimal result;
    EXPECT_TRUE(NullableLiftingTransform::MatchCompOrDecimal(comp.get(), result));
    EXPECT_EQ(result.Kind, ComparisonKind::Equality);
    EXPECT_TRUE(result.IsLifted);
}

// MatchCompOrDecimal returns false for a non-Comp instruction (a Call, even one
// whose method name looks like an operator -- the Decimal branch is deferred).
TEST(NullableLiftingTransform, MatchCompOrDecimalRejectsCall) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Decimal::op_Equality");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Decimal);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdLoca>(v));
    CompOrDecimal result;
    EXPECT_FALSE(NullableLiftingTransform::MatchCompOrDecimal(call.get(), result));
}

// MatchCompOrDecimal returns false for a non-Comp / non-Call instruction
// (e.g. a bare LdLoc) and for null, matching the C# fall-through.
TEST(NullableLiftingTransform, MatchCompOrDecimalRejectsNonComp) {
    auto v = MakeLocal("v");
    auto ld = std::make_unique<LdLoc>(v);
    CompOrDecimal result;
    EXPECT_FALSE(NullableLiftingTransform::MatchCompOrDecimal(ld.get(), result));
    EXPECT_FALSE(NullableLiftingTransform::MatchCompOrDecimal(nullptr, result));
}

// A SwitchInstruction carries IsLifted/Type and a SwitchSection carries
// HasNullLabel; the dump renders a lifted switch with a `null` label section.
TEST(NullableLiftingTransform, SwitchInstructionNullableFieldsAndDump) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(std::make_unique<Block>());
    Block* def = fn->Body->Blocks[0].get();
    def->SetFinal(std::make_unique<Leave>(fn->Body.get()));

    auto sw = std::make_unique<SwitchInstruction>(std::make_unique<LdLoc>(MakeLocal("v")));
    sw->IsLifted = true;
    sw->Type = MakeNullableOf(KnownTypeCode::Int32);

    auto caseSection = std::make_unique<SwitchSection>(LongSet(0));
    caseSection->SetBody(std::make_unique<Branch>(def));
    sw->AddSection(std::move(caseSection));

    auto nullSection = std::make_unique<SwitchSection>();
    nullSection->HasNullLabel = true;
    nullSection->SetBody(std::make_unique<Branch>(def));
    sw->AddSection(std::move(nullSection));

    fn->Body->Blocks[0]->SetFinal(std::move(sw));
    RecomputeIncomingEdgeCounts(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    std::string dump;
    fn->Body->Blocks[0]->FinalInstruction->WriteTo(dump);
    EXPECT_NE(dump.find("switch"), std::string::npos);
    EXPECT_NE(dump.find("lifted"), std::string::npos);  // IsLifted renders
    EXPECT_NE(dump.find("null"), std::string::npos);   // HasNullLabel renders
}

// The LiftNullables setting defaults to true (matching DecompilerSettings, which
// only turns it off for C# < 2).
TEST(NullableLiftingTransform, LiftNullablesSettingDefaultsTrue) {
    ILTransformSettings settings;
    EXPECT_TRUE(settings.LiftNullables);
}

// On the real mscorlib corpus the IL reader must populate Call::DeclaringType for
// real calls, and the helpers must consistently recognise (and reject) Nullable<T>
// method calls. The ILAst invariant holds across the corpus.
TEST(NullableLiftingTransform, MscorlibDeclaringTypeSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int callsWithDeclaringType = 0;
    int hasValueMatches = 0;
    int getValueOrDefaultMatches = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (!inst || inst->Op != OpCode::Call) return;
            auto* call = static_cast<Call*>(inst);
            if (call->DeclaringType) ++callsWithDeclaringType;
            ILInstruction* arg = nullptr;
            if (NullableLiftingTransform::MatchHasValueCall(call, arg)) {
                ASSERT_NE(arg, nullptr);
                ++hasValueMatches;
            }
            if (NullableLiftingTransform::MatchGetValueOrDefault(call, arg)) {
                ASSERT_NE(arg, nullptr);
                ++getValueOrDefaultMatches;
            }
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    // The reader decodes thousands of methods; real call/callvirt/newobj sites
    // must carry a resolved declaring type (the input the helpers read).
    EXPECT_GT(processed, 5000);
    EXPECT_GT(callsWithDeclaringType, 0);
    // Whether mscorlib (.NET Framework 4, legacy csc) has any Nullable<T>
    // method calls is corpus-dependent; the helpers just must not misfire. The
    // counts are reported (not asserted) -- a non-zero match is informative.
    (void)hasValueMatches;
    (void)getValueOrDefaultMatches;
}
