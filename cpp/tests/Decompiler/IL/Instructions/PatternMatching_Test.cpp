// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the shared IL match helpers (Decompiler/IL/PatternMatching.hpp --
// the ports of the C# `ILInstruction.MatchLdThis` / `MatchBox` / `MatchLdObj`
// extension methods) and the `CallInstruction.ExpectedTypeForThisPointer`
// static (Decompiler/IL/Instructions/Call.hpp -- the C#
// `CallInstruction.ExpectedTypeForThisPointer`). The match helpers back the
// ExpressionBuilder TranslateTarget's local `MatchLdThis` (the struct-box
// shape); ExpectedTypeForThisPointer drives its pointer/ref type-hint
// machinery.

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameter;

namespace {

using ILSpy::Decompiler::TypeSystem::Implementation::FakeField;
using ILSpy::Decompiler::TypeSystem::Implementation::MinimalCorlib;
using ILSpy::Decompiler::TypeSystem::SimpleCompilation;

ILVariablePtr MakeVariable(VariableKind kind, ITypePtr type, std::int32_t index)
{
    auto v = std::make_shared<ILVariable>();
    v->Name = "v";
    v->Kind = kind;
    v->Type = std::move(type);
    v->Index = index;
    return v;
}

// A trivial compilation-backed fake field, so the field match helpers have a
// real IField identity to report (the resolved `Field` handle the IL reader
// leaves for tests/transforms to populate).
struct FieldFixture {
    SimpleCompilation compilation{ MinimalCorlib::Instance(), {} };
    std::shared_ptr<FakeField> makeField() {
        return std::make_shared<FakeField>(compilation);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// MatchLdThis

TEST(PatternMatchingTest, MatchLdThisAcceptsThisParameterLoad)
{
    auto thisVar = MakeVariable(VariableKind::Parameter, nullptr, -1);
    LdLoc load(thisVar);
    EXPECT_TRUE(MatchLdThis(&load));
}

TEST(PatternMatchingTest, MatchLdThisRejectsOtherVariables)
{
    // A local load: not a this-parameter load.
    auto local = MakeVariable(VariableKind::Local, nullptr, 0);
    LdLoc load(local);
    EXPECT_FALSE(MatchLdThis(&load));
    // A this-shaped variable with a non-negative index: the C# `Index < 0` test.
    auto param = MakeVariable(VariableKind::Parameter, nullptr, 0);
    LdLoc paramLoad(param);
    EXPECT_FALSE(MatchLdThis(&paramLoad));
    // A non-LdLoc node.
    LdNull nullLoad;
    EXPECT_FALSE(MatchLdThis(&nullLoad));
    // A null pointer.
    EXPECT_FALSE(MatchLdThis(nullptr));
}

// ---------------------------------------------------------------------------
// MatchBox

TEST(PatternMatchingTest, MatchBoxReturnsArgumentAndType)
{
    auto thisVar = MakeVariable(VariableKind::Parameter, nullptr, -1);
    auto boxType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    Box box(boxType, std::make_unique<LdLoc>(thisVar));
    ILInstruction* argument = nullptr;
    ITypePtr type;
    EXPECT_TRUE(MatchBox(&box, argument, type));
    EXPECT_EQ(argument, box.Argument.get());
    EXPECT_EQ(type, boxType);
}

TEST(PatternMatchingTest, MatchBoxRejectsOtherNodesAndClearsOutParams)
{
    LdNull nullLoad;
    ILInstruction* argument = nullptr;
    ITypePtr type;
    EXPECT_FALSE(MatchBox(&nullLoad, argument, type));
    EXPECT_EQ(argument, nullptr);
    EXPECT_EQ(type, nullptr);
    EXPECT_FALSE(MatchBox(nullptr, argument, type));
}

// ---------------------------------------------------------------------------
// MatchLdObj

TEST(PatternMatchingTest, MatchLdObjReturnsTargetAndType)
{
    auto thisVar = MakeVariable(VariableKind::Parameter, nullptr, -1);
    auto loadType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    LdObj load(std::make_unique<LdLoc>(thisVar), loadType);
    ILInstruction* target = nullptr;
    ITypePtr type;
    EXPECT_TRUE(MatchLdObj(&load, target, type));
    EXPECT_EQ(target, load.Target.get());
    EXPECT_EQ(type, loadType);
}

TEST(PatternMatchingTest, MatchLdObjRejectsOtherNodesAndClearsOutParams)
{
    LdNull nullLoad;
    ILInstruction* target = nullptr;
    ITypePtr type;
    EXPECT_FALSE(MatchLdObj(&nullLoad, target, type));
    EXPECT_EQ(target, nullptr);
    EXPECT_EQ(type, nullptr);
}

// The struct `box T(ldobj T(ldloc this))` shape the ExpressionBuilder
// TranslateTarget local MatchLdThis walks: MatchBox + MatchLdObj chain.
TEST(PatternMatchingTest, StructBoxShapeChainsThroughBothHelpers)
{
    auto thisVar = MakeVariable(VariableKind::Parameter, nullptr, -1);
    auto structType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    Box box(structType,
            std::make_unique<LdObj>(std::make_unique<LdLoc>(thisVar), structType));
    ILInstruction* boxArg = nullptr;
    ITypePtr boxType;
    ASSERT_TRUE(MatchBox(&box, boxArg, boxType));
    ILInstruction* ldobjTarget = nullptr;
    ITypePtr ldobjType;
    ASSERT_TRUE(MatchLdObj(boxArg, ldobjTarget, ldobjType));
    ASSERT_NE(ldobjTarget, nullptr);
    EXPECT_TRUE(MatchLdThis(ldobjTarget));
    EXPECT_TRUE(boxType->Equals(*ldobjType));
}

// ---------------------------------------------------------------------------
// ExpectedTypeForThisPointer

TEST(ExpectedTypeForThisPointerTest, ConstrainedToForcesRef)
{
    const ITypePtr int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    const ITypePtr str = std::make_shared<KnownType>(KnownTypeCode::String);
    // A constrained prefix forces the managed-reference form regardless of the
    // declaring type.
    EXPECT_EQ(ExpectedTypeForThisPointer(str.get(), int32.get()), StackType::Ref);
    EXPECT_EQ(ExpectedTypeForThisPointer(int32.get(), int32.get()), StackType::Ref);
}

TEST(ExpectedTypeForThisPointerTest, TypeParameterForcesRef)
{
    // The port's ITypeParameter class (the VAR/MVAR stand-in over an arbitrary
    // owner) carries TypeKind::TypeParameter.
    const ITypePtr typeParameter =
        std::make_shared<TypeParameter>(0, TypeParameter::OwnerKind::Class, "T");
    EXPECT_EQ(typeParameter->Kind(), TypeKind::TypeParameter);
    EXPECT_EQ(ExpectedTypeForThisPointer(typeParameter.get(), nullptr), StackType::Ref);
}

TEST(ExpectedTypeForThisPointerTest, ReferenceTypesPassAsObject)
{
    const ITypePtr str = std::make_shared<KnownType>(KnownTypeCode::String);
    EXPECT_EQ(str->IsReferenceType(), std::optional<bool>(true));
    EXPECT_EQ(ExpectedTypeForThisPointer(str.get(), nullptr), StackType::O);
}

TEST(ExpectedTypeForThisPointerTest, ValueTypesPassAsManagedReference)
{
    const ITypePtr int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_EQ(int32->IsReferenceType(), std::optional<bool>(false));
    EXPECT_EQ(ExpectedTypeForThisPointer(int32.get(), nullptr), StackType::Ref);
}

TEST(ExpectedTypeForThisPointerTest, UnknownReferenceNessIsUnknown)
{
    // A type whose IsReferenceType tri-state carries neither true nor false
    // (the C# `switch` default arm) answers StackType.Unknown.
    const ITypePtr unknownType = std::make_shared<KnownType>(KnownTypeCode::Object);
    (void)unknownType;
    class IndeterminateType final : public IType {
    public:
        TypeKind Kind() const override { return TypeKind::Other; }
        std::string Name() const override { return "Indeterminate"; }
        std::string ReflectionName() const override { return "Indeterminate"; }
        int TypeParameterCount() const override { return 0; }
        std::optional<bool> IsReferenceType() const override { return std::nullopt; }
    protected:
        bool StructuralEquals(const IType& other) const override { return this == &other; }
    };
    IndeterminateType indeterminate;
    EXPECT_EQ(ExpectedTypeForThisPointer(&indeterminate, nullptr), StackType::Unknown);
}

// ---------------------------------------------------------------------------
// MatchLdLoc / MatchStLoc

TEST(PatternMatchingTest, MatchLdLocMatchesOnlyTheGivenVariable)
{
    auto v = MakeVariable(VariableKind::Local, nullptr, 0);
    auto other = MakeVariable(VariableKind::Local, nullptr, 1);
    LdLoc load(v);
    EXPECT_TRUE(MatchLdLoc(&load, v.get()));
    EXPECT_FALSE(MatchLdLoc(&load, other.get()));
    EXPECT_FALSE(MatchLdLoc(&load, nullptr));
    LdNull nullLoad;
    EXPECT_FALSE(MatchLdLoc(&nullLoad, v.get()));
    EXPECT_FALSE(MatchLdLoc(nullptr, v.get()));
}

TEST(PatternMatchingTest, MatchStLocReportsVariable)
{
    auto v = MakeVariable(VariableKind::Local, nullptr, 0);
    StLoc store(v, std::make_unique<LdNull>());
    ILVariable* variable = nullptr;
    ASSERT_TRUE(MatchStLoc(&store, variable));
    EXPECT_EQ(variable, v.get());

    LdNull nullLoad;
    EXPECT_FALSE(MatchStLoc(&nullLoad, variable));
    EXPECT_EQ(variable, nullptr);
}

TEST(PatternMatchingTest, MatchStLocVariableReportsValue)
{
    auto v = MakeVariable(VariableKind::Local, nullptr, 0);
    auto other = MakeVariable(VariableKind::Local, nullptr, 1);
    StLoc store(v, std::make_unique<LdNull>());
    ILInstruction* value = nullptr;
    ASSERT_TRUE(MatchStLoc(&store, v.get(), value));
    EXPECT_EQ(value, store.Value.get());

    EXPECT_FALSE(MatchStLoc(&store, other.get(), value));
    EXPECT_EQ(value, nullptr);
}

// ---------------------------------------------------------------------------
// MatchLdsFld / MatchStsFld / MatchStFld / MatchLdsFlda / MatchLdFlda

TEST(PatternMatchingTest, MatchLdsFldReadsResolvedField)
{
    FieldFixture fixture;
    auto field = fixture.makeField();
    auto ldsflda = std::make_unique<LdsFlda>("F");
    ldsflda->Field = field;
    LdObj load(std::move(ldsflda), std::make_shared<KnownType>(KnownTypeCode::Int32));
    const ILSpy::Decompiler::TypeSystem::IField* matched = nullptr;
    ASSERT_TRUE(MatchLdsFld(&load, matched));
    EXPECT_EQ(matched, field.get());

    // An unaligned ldobj over the same address is not the plain field load.
    load.UnalignedPrefix = 4;
    EXPECT_FALSE(MatchLdsFld(&load, matched));
    EXPECT_EQ(matched, nullptr);
    load.UnalignedPrefix = 0;
    // A volatile ldobj is rejected too.
    load.IsVolatile = true;
    EXPECT_FALSE(MatchLdsFld(&load, matched));
}

TEST(PatternMatchingTest, MatchLdsFldRejectsInstanceFieldAddress)
{
    FieldFixture fixture;
    auto field = fixture.makeField();
    auto ldflda = std::make_unique<LdFlda>(std::make_unique<LdNull>(), "F");
    ldflda->Field = field;
    LdObj load(std::move(ldflda), std::make_shared<KnownType>(KnownTypeCode::Int32));
    const ILSpy::Decompiler::TypeSystem::IField* matched = nullptr;
    EXPECT_FALSE(MatchLdsFld(&load, matched));
    EXPECT_EQ(matched, nullptr);
}

TEST(PatternMatchingTest, MatchStsFldReadsFieldAndValue)
{
    FieldFixture fixture;
    auto field = fixture.makeField();
    auto ldsflda = std::make_unique<LdsFlda>("F");
    ldsflda->Field = field;
    auto value = std::make_unique<LdNull>();
    ILInstruction* rawValue = value.get();
    StObj store(std::move(ldsflda), std::move(value),
                std::make_shared<KnownType>(KnownTypeCode::Int32));
    const ILSpy::Decompiler::TypeSystem::IField* matched = nullptr;
    ILInstruction* matchedValue = nullptr;
    ASSERT_TRUE(MatchStsFld(&store, matched, matchedValue));
    EXPECT_EQ(matched, field.get());
    EXPECT_EQ(matchedValue, rawValue);

    store.IsVolatile = true;
    EXPECT_FALSE(MatchStsFld(&store, matched, matchedValue));
    EXPECT_EQ(matched, nullptr);
    EXPECT_EQ(matchedValue, nullptr);
}

TEST(PatternMatchingTest, MatchStFldReadsTargetFieldAndValue)
{
    FieldFixture fixture;
    auto field = fixture.makeField();
    auto target = std::make_unique<LdNull>();
    ILInstruction* rawTarget = target.get();
    auto ldflda = std::make_unique<LdFlda>(std::move(target), "F");
    ldflda->Field = field;
    auto value = std::make_unique<LdNull>();
    ILInstruction* rawValue = value.get();
    StObj store(std::move(ldflda), std::move(value),
                std::make_shared<KnownType>(KnownTypeCode::Int32));
    ILInstruction* matchedTarget = nullptr;
    const ILSpy::Decompiler::TypeSystem::IField* matched = nullptr;
    ILInstruction* matchedValue = nullptr;
    ASSERT_TRUE(MatchStFld(&store, matchedTarget, matched, matchedValue));
    EXPECT_EQ(matchedTarget, rawTarget);
    EXPECT_EQ(matched, field.get());
    EXPECT_EQ(matchedValue, rawValue);

    LdNull plainStore;
    EXPECT_FALSE(MatchStFld(&plainStore, matchedTarget, matched, matchedValue));
    EXPECT_EQ(matchedTarget, nullptr);
    EXPECT_EQ(matched, nullptr);
    EXPECT_EQ(matchedValue, nullptr);
}

TEST(PatternMatchingTest, MatchLdsFldaAndMatchLdFlda)
{
    FieldFixture fixture;
    auto field = fixture.makeField();
    LdsFlda staticAddress("F");
    staticAddress.Field = field;
    const ILSpy::Decompiler::TypeSystem::IField* matched = nullptr;
    ASSERT_TRUE(MatchLdsFlda(&staticAddress, matched));
    EXPECT_EQ(matched, field.get());
    EXPECT_FALSE(MatchLdsFlda(nullptr, matched));
    EXPECT_EQ(matched, nullptr);

    auto target = std::make_unique<LdNull>();
    ILInstruction* rawTarget = target.get();
    LdFlda instanceAddress(std::move(target), "F");
    instanceAddress.Field = field;
    ILInstruction* matchedTarget = nullptr;
    ASSERT_TRUE(MatchLdFlda(&instanceAddress, matchedTarget, matched));
    EXPECT_EQ(matchedTarget, rawTarget);
    EXPECT_EQ(matched, field.get());
    EXPECT_FALSE(MatchLdFlda(&staticAddress, matchedTarget, matched));
    EXPECT_EQ(matchedTarget, nullptr);
    EXPECT_EQ(matched, nullptr);
}

TEST(PatternMatchingTest, MatchLdLocRefMatchesReferenceTypedLdLoc)
{
    // A reference-typed variable's load: ldloc matches, ldloca does not.
    auto refVar = MakeVariable(VariableKind::Local,
                               std::make_shared<KnownType>(KnownTypeCode::String), 0);
    LdLoc refLoad(refVar);
    ILVariable* reported = nullptr;
    ASSERT_TRUE(MatchLdLocRef(&refLoad, reported));
    EXPECT_EQ(reported, refVar.get());
    ASSERT_TRUE(MatchLdLocRef(&refLoad, refVar.get()));

    LdLoca refAddress(refVar);
    EXPECT_FALSE(MatchLdLocRef(&refAddress, refVar.get()));

    // Another variable does not match.
    auto other = MakeVariable(VariableKind::Local,
                              std::make_shared<KnownType>(KnownTypeCode::String), 1);
    EXPECT_FALSE(MatchLdLocRef(&refLoad, other.get()));
}

TEST(PatternMatchingTest, MatchLdLocRefMatchesValueTypedLdLoca)
{
    // A value-typed variable's address: ldloca matches, ldloc does not.
    auto valVar = MakeVariable(VariableKind::Local,
                               std::make_shared<KnownType>(KnownTypeCode::Int32), 0);
    LdLoca valAddress(valVar);
    ILVariable* reported = nullptr;
    ASSERT_TRUE(MatchLdLocRef(&valAddress, reported));
    EXPECT_EQ(reported, valVar.get());
    ASSERT_TRUE(MatchLdLocRef(&valAddress, valVar.get()));

    LdLoc valLoad(valVar);
    EXPECT_FALSE(MatchLdLocRef(&valLoad, valVar.get()));
}

TEST(PatternMatchingTest, MatchLdLocRefTypeParameterAcceptsLdLoca)
{
    // A type parameter's reference-ness is unknown in the port, but its Kind
    // is TypeParameter, so the ldloca arm's `Kind == TypeParameter` clause
    // accepts the address load regardless.
    auto tp = std::make_shared<TypeParameter>(0, TypeParameter::OwnerKind::Class, "T");
    auto var = MakeVariable(VariableKind::Local, tp, 0);
    LdLoca address(var);
    EXPECT_TRUE(MatchLdLocRef(&address, var.get()));

    // A plain ldloc of it still fails (unknown reference-ness is not `true`).
    LdLoc load(var);
    EXPECT_FALSE(MatchLdLocRef(&load, var.get()));
}

TEST(PatternMatchingTest, MatchLdLocaMatchesOnlyTheGivenVariable)
{
    auto a = MakeVariable(VariableKind::Local,
                          std::make_shared<KnownType>(KnownTypeCode::Int32), 0);
    auto b = MakeVariable(VariableKind::Local,
                          std::make_shared<KnownType>(KnownTypeCode::Int32), 1);
    LdLoca address(a);
    EXPECT_TRUE(MatchLdLoca(&address, a.get()));
    EXPECT_FALSE(MatchLdLoca(&address, b.get()));
    LdLoc load(a);
    EXPECT_FALSE(MatchLdLoca(&load, a.get()));
}
