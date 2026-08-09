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
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
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

// GetUnderlyingTypeOfNullable unwraps a Nullable<T> instantiation to T.
// Nullable<int> (a ParameterizedType over KnownType(NullableOfT)) -> Int32.
TEST(NullableLiftingTransform, GetUnderlyingTypeOfNullableUnwrapsInstantiation) {
    auto nullable = MakeNullableOf(KnownTypeCode::Int32);
    const IType* underlying = NullableLiftingTransform::GetUnderlyingTypeOfNullable(nullable.get());
    ASSERT_NE(underlying, nullptr);
    EXPECT_TRUE(NullableLiftingTransform::IsKnownType(underlying, KnownTypeCode::Int32));
}

// A bare Nullable`1 (the generic definition, no type argument) has no
// underlying type -- the C# NullableType.GetUnderlyingType returns the type
// arg, which a bare Nullable`1 lacks.
TEST(NullableLiftingTransform, GetUnderlyingTypeOfNullableBareDefinitionIsNull) {
    auto bare = std::make_shared<KnownType>(KnownTypeCode::NullableOfT);
    EXPECT_EQ(NullableLiftingTransform::GetUnderlyingTypeOfNullable(bare.get()), nullptr);
}

// A non-Nullable type (System.Int32, a KnownType) is not a Nullable<T>, so
// GetUnderlyingTypeOfNullable returns null; null input returns null.
TEST(NullableLiftingTransform, GetUnderlyingTypeOfNullableNonNullableIsNull) {
    auto i32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_EQ(NullableLiftingTransform::GetUnderlyingTypeOfNullable(i32.get()), nullptr);
    EXPECT_EQ(NullableLiftingTransform::GetUnderlyingTypeOfNullable(nullptr), nullptr);
}

// IsKnownType: a KnownType matches its own code, not a different code; a
// ParameterizedType (e.g. Nullable<int>) is not itself a known type; null is
// not a known type (matching the C# IsKnownType returning false for None).
TEST(NullableLiftingTransform, IsKnownTypeChecksCode) {
    auto i32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_TRUE(NullableLiftingTransform::IsKnownType(i32.get(), KnownTypeCode::Int32));
    EXPECT_FALSE(NullableLiftingTransform::IsKnownType(i32.get(), KnownTypeCode::Boolean));
    auto nullable = MakeNullableOf(KnownTypeCode::Boolean);
    EXPECT_FALSE(NullableLiftingTransform::IsKnownType(nullable.get(), KnownTypeCode::Boolean));
    EXPECT_FALSE(NullableLiftingTransform::IsKnownType(nullptr, KnownTypeCode::Int32));
}

// call get_HasValue(ldloca v) on Nullable<int> matches the ldloca-v overload
// and reports the variable.
TEST(NullableLiftingTransform, MatchHasValueCallLdLocaReportsVariable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    ILVariablePtr matched;
    EXPECT_TRUE(NullableLiftingTransform::MatchHasValueCall(call.get(), matched));
    ASSERT_NE(matched, nullptr);
    EXPECT_EQ(matched.get(), v.get());
}

// A get_HasValue whose argument is not a LdLoca (e.g. a LdLoc) does not match
// the ldloca-v overload -- the 1-arg form matches but the arg isn't a ldloca.
TEST(NullableLiftingTransform, MatchHasValueCallLdLocaRejectsNonLdLocaArg) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoc>(v));  // not a LdLoca
    ILVariablePtr matched;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(call.get(), matched));
}

// A get_HasValue on a non-Nullable declaring type does not match the ldloca-v
// overload (the 1-arg form rejects it first).
TEST(NullableLiftingTransform, MatchHasValueCallLdLocaRejectsNonNullable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Int32::get_HasValue");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    ILVariablePtr matched;
    EXPECT_FALSE(NullableLiftingTransform::MatchHasValueCall(call.get(), matched));
}

// call GetValueOrDefault(ldloca v) on Nullable<int> matches the ldloca-v
// overload and reports the variable.
TEST(NullableLiftingTransform, MatchGetValueOrDefaultLdLocaReportsVariable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    ILVariablePtr matched;
    EXPECT_TRUE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), matched));
    ASSERT_NE(matched, nullptr);
    EXPECT_EQ(matched.get(), v.get());
}

// The 2-argument GetValueOrDefault form (with a fallback) does not match the
// ldloca-v overload (the 1-arg form rejects the 2-arg call).
TEST(NullableLiftingTransform, MatchGetValueOrDefaultLdLocaRejectsTwoArgForm) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    ILVariablePtr matched;
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), matched));
}

// The match-against-v overload (the D98 MatchGetValueOrDefault(inst, ILVariable
// v) port): `call GetValueOrDefault(ldloca v)` matches when the call's variable
// is the given `v` (the C# `MatchGetValueOrDefault(inst, out v2) && v == v2`).
// Disjoint from the report-variable overload by the shared_ptr/raw-pointer split.
TEST(NullableLiftingTransform, MatchGetValueOrDefaultLdLocaMatchesGivenVariable) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    EXPECT_TRUE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), v.get()))
        << "a GetValueOrDefault call on v must match against v";
}

// The match-against-v overload rejects a GetValueOrDefault call on a different
// variable (the C# `v == v2` check fails) and a non-LdLoca argument.
TEST(NullableLiftingTransform, MatchGetValueOrDefaultLdLocaRejectsDifferentVariable) {
    auto v = MakeLocal("v");
    auto w = MakeLocal("w");
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(w));  // on w, not v
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(call.get(), v.get()))
        << "a GetValueOrDefault call on w must not match against v";
    // A non-LdLoca argument (a bare LdLoc) does not match the ldloca-v overload.
    auto call2 = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call2->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call2->AddArg(std::make_unique<LdLoc>(v));  // not a LdLoca
    EXPECT_FALSE(NullableLiftingTransform::MatchGetValueOrDefault(call2.get(), v.get()));
}

// logic.not(call get_HasValue(ldloca v)) -- the reader's brfalse shape
// comp(Equality, call get_HasValue(ldloca v), ldc.i4(0)) -- matches
// MatchNegatedHasValueCall for v.
TEST(NullableLiftingTransform, MatchNegatedHasValueCallMatchesLogicNot) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    auto neg = std::make_unique<Comp>(std::move(call),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Equality, false);
    EXPECT_TRUE(NullableLiftingTransform::MatchNegatedHasValueCall(neg.get(), v.get()));
}

// MatchNegatedHasValueCall rejects a non-negated HasValue call (no logic.not
// wrapper) and a logic.not wrapping a call on a different variable.
TEST(NullableLiftingTransform, MatchNegatedHasValueCallRejectsNonNegatedAndDifferentVar) {
    auto v = MakeLocal("v");
    auto w = MakeLocal("w");
    // A bare HasValue call (no logic.not) does not match.
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    EXPECT_FALSE(NullableLiftingTransform::MatchNegatedHasValueCall(call.get(), v.get()));
    // A logic.not wrapping a HasValue call on a different variable does not match for v.
    auto call2 = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call2->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call2->AddArg(std::make_unique<LdLoca>(w));
    auto neg = std::make_unique<Comp>(std::move(call2),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Equality, false);
    EXPECT_FALSE(NullableLiftingTransform::MatchNegatedHasValueCall(neg.get(), v.get()));
}

// newobj Nullable<bool>(ldloc v) matches MatchNullableCtor: reports the
// underlying type (Boolean) and the argument.
TEST(NullableLiftingTransform, MatchNullableCtorMatchesNewObj) {
    auto v = MakeLocal("v");
    auto call = std::make_unique<Call>("System.Nullable`1::.ctor");
    call->IsNewObj = true;
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    auto argInst = std::make_unique<LdLoc>(v);
    ILInstruction* argPtr = argInst.get();
    call->AddArg(std::move(argInst));
    const IType* underlying = nullptr;
    ILInstruction* arg = nullptr;
    EXPECT_TRUE(NullableLiftingTransform::MatchNullableCtor(call.get(), underlying, arg));
    ASSERT_NE(underlying, nullptr);
    EXPECT_TRUE(NullableLiftingTransform::IsKnownType(underlying, KnownTypeCode::Boolean));
    EXPECT_EQ(arg, argPtr);
}

// MatchNullableCtor rejects a non-newobj call (call/callvirt, not a
// constructor), a non-Nullable declaring type, the wrong arg count, and a
// null declaring type.
TEST(NullableLiftingTransform, MatchNullableCtorRejects) {
    auto v = MakeLocal("v");
    // Not a newobj (a plain call to .ctor -- IsNewObj is false).
    auto call = std::make_unique<Call>("System.Nullable`1::.ctor");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    call->AddArg(std::make_unique<LdLoc>(v));
    const IType* ut = nullptr;
    ILInstruction* arg = nullptr;
    EXPECT_FALSE(NullableLiftingTransform::MatchNullableCtor(call.get(), ut, arg));
    // A newobj on a non-Nullable declaring type.
    auto call2 = std::make_unique<Call>("System.String::.ctor");
    call2->IsNewObj = true;
    call2->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::String);
    call2->AddArg(std::make_unique<LdLoc>(v));
    EXPECT_FALSE(NullableLiftingTransform::MatchNullableCtor(call2.get(), ut, arg));
    // A newobj on Nullable with 2 arguments (the ctor must take exactly 1).
    auto call3 = std::make_unique<Call>("System.Nullable`1::.ctor");
    call3->IsNewObj = true;
    call3->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    call3->AddArg(std::make_unique<LdLoc>(v));
    call3->AddArg(std::make_unique<LdLoc>(v));
    EXPECT_FALSE(NullableLiftingTransform::MatchNullableCtor(call3.get(), ut, arg));
    // A newobj on Nullable whose declaring type could not be resolved (null).
    auto call4 = std::make_unique<Call>("System.Nullable`1::.ctor");
    call4->IsNewObj = true;
    call4->AddArg(std::make_unique<LdLoc>(v));
    EXPECT_FALSE(NullableLiftingTransform::MatchNullableCtor(call4.get(), ut, arg));
}

// default(Nullable<int>) matches MatchNull: reports the underlying type (Int32).
TEST(NullableLiftingTransform, MatchNullMatchesDefaultNullable) {
    auto dv = std::make_unique<DefaultValue>(MakeNullableOf(KnownTypeCode::Int32));
    const IType* underlying = nullptr;
    EXPECT_TRUE(NullableLiftingTransform::MatchNull(dv.get(), underlying));
    ASSERT_NE(underlying, nullptr);
    EXPECT_TRUE(NullableLiftingTransform::IsKnownType(underlying, KnownTypeCode::Int32));
}

// MatchNull rejects default(int) (not a Nullable<T>) and a non-DefaultValue
// instruction (e.g. a LdLoc) and null.
TEST(NullableLiftingTransform, MatchNullRejectsNonNullableDefaultAndNonDefaultValue) {
    const IType* underlying = nullptr;
    auto dvInt = std::make_unique<DefaultValue>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    EXPECT_FALSE(NullableLiftingTransform::MatchNull(dvInt.get(), underlying));
    auto v = MakeLocal("v");
    auto ld = std::make_unique<LdLoc>(v);
    EXPECT_FALSE(NullableLiftingTransform::MatchNull(ld.get(), underlying));
    EXPECT_FALSE(NullableLiftingTransform::MatchNull(nullptr, underlying));
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
    int hasValueLdLocaMatches = 0;
    int getValueOrDefaultLdLocaMatches = 0;
    int nullableCtorMatches = 0;
    int nullDefaultMatches = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (!inst) return;
            if (inst->Op == OpCode::Call) {
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
                // The ldloca-v overloads: a HasValue/GetValueOrDefault call whose
                // argument is a `ldloca v` reports the variable. Every match must
                // yield a non-null variable (the helper never misfires).
                ILVariablePtr v;
                if (NullableLiftingTransform::MatchHasValueCall(call, v)) {
                    ASSERT_NE(v, nullptr);
                    ++hasValueLdLocaMatches;
                }
                if (NullableLiftingTransform::MatchGetValueOrDefault(call, v)) {
                    ASSERT_NE(v, nullptr);
                    ++getValueOrDefaultLdLocaMatches;
                }
                // A newobj Nullable<T>(arg) on a real Nullable constructor.
                const IType* underlying = nullptr;
                ILInstruction* ctorArg = nullptr;
                if (NullableLiftingTransform::MatchNullableCtor(call, underlying, ctorArg)) {
                    ASSERT_NE(underlying, nullptr);
                    ASSERT_NE(ctorArg, nullptr);
                    ++nullableCtorMatches;
                }
            } else if (inst->Op == OpCode::DefaultValue) {
                const IType* underlying = nullptr;
                if (NullableLiftingTransform::MatchNull(inst, underlying)) {
                    ASSERT_NE(underlying, nullptr);
                    ++nullDefaultMatches;
                }
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
    (void)hasValueLdLocaMatches;
    (void)getValueOrDefaultLdLocaMatches;
    (void)nullableCtorMatches;
    (void)nullDefaultMatches;
}
