// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver operator-resolution helper region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 525 / 962-985 /
// 981-989 / 1253-1275 / 2435-2441): IsNullableTypeOrNonValueType / UnaryOperatorResolveResult
// / BinaryOperatorResolveResult / the three PointerArithmeticOperator overloads /
// ResolveNullCoalescingOperator / CreateOverloadResolution -- the non-recursive
// prerequisites the future ResolveUnaryOperator (line 326) / ResolveBinaryOperator (line
// 594) slices consume. Plus the TypeUtils.IsCSharpNativeIntegerType leaf (TypeUtils.cs
// line 147) both regions consult.
//
// The load-bearing cruxes:
//  (a) IsCSharpNativeIntegerType is KIND-based only -- NInt/NUInt are native integers,
//      (U)IntPtr (a Struct kind with the IntPtr/UIntPtr codes) is NOT (the C# doc
//      comment's explicit distinction), and a Nullable<nint> wrapper (Kind=Struct via the
//      generic) is NOT either;
//  (b) IsNullableTypeOrNonValueType's `IsReferenceType != false` accepts an INDETERMINATE
//      reference-ness (the lifted `bool? != false` is falsy only for a definite false) --
//      an UnknownType compares against the null literal;
//  (c) the two OperatorResolveResult factories thread the resolver's CheckForOverflow
//      flag into the BCL ExpressionType mapping (Negate vs NegateChecked, Add vs
//      AddChecked) and carry the operand order, the isLifted flag, and NO user-defined
//      method;
//  (d) PointerArithmeticOperator's KnownTypeCode conveniences resolve through
//      compilation.FindType (the REGISTERED instance -- pointer identity) and the IType
//      overload builds the plain BinaryOperatorMethod with the given return type, two
//      unnamed DefaultParameters, and the operand order;
//  (e) ResolveNullCoalescingOperator tries the rhs against the NULLABLE lhs's UNDERLYING
//      type FIRST (the result type is the underlying), then the rhs against the lhs's own
//      type, then the lhs against the rhs's type, else the ErrorResolveResult over the
//      lhs's type -- and a nullable lhs whose rhs fits NONE of the three arms falls all
//      the way through to the error;
//  (f) CreateOverloadResolution threads the resolver's CheckForOverflow flag into the
//      built resolution and forwards the argument/type-argument arrays.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::OperatorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// The shared compilation plus one REGISTERED instance per code the region resolves
// through FindType (the PointerArithmeticOperator conveniences and the TryConvert
// conversions). Every FindType target IS the accessor instance (the type-cache model
// from the numeric-promotion fixture; the conversions the null-coalescing TryConvert
// runs are the UNCACHED ResolveResult-based entry, so no test-local type can dangle in
// the per-compilation CSharpConversions instance's cache).
// A DEFINITE value-type definition stub (IsReferenceType == false definite -- a plain
// LookupTypeDefinition inherits the IType nullopt default, which the
// `IsReferenceType != false` semantics counts as eligible; the not-eligible case needs
// the definite false, the D519 ByRefLikeDef precedent).
class ValueTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return false; }
};

struct Fixture {
    LookupCompilation compilation;
    std::vector<std::shared_ptr<LookupTypeDefinition>> defs;
    std::shared_ptr<LookupTypeDefinition> nullableOfT;

    Fixture() {
        auto make = [this](KnownTypeCode code, const char* name,
                           TypeKind kind = TypeKind::Struct) {
            auto t = std::make_shared<LookupTypeDefinition>(
                name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
                Accessibility::Public, compilation, nullptr, code);
            compilation.RegisterKnownType(code, t.get());
            defs.push_back(t);
            return t;
        };
        nullableOfT = std::make_shared<LookupTypeDefinition>(
            "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
            TypeKind::Struct, Accessibility::Public, compilation, nullptr,
            KnownTypeCode::NullableOfT);
        compilation.RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT.get());
        defs.push_back(nullableOfT);
        make(KnownTypeCode::Int32, "Int32");
        make(KnownTypeCode::Int64, "Int64");
        make(KnownTypeCode::Byte, "Byte");
        make(KnownTypeCode::String, "String", TypeKind::Class);
        // (U)IntPtr: the managed wrapper STRUCTS -- the C# doc comment's explicit
        // not-a-native-integer pair (registered so a FindType-based construction could
        // reuse them, though the kind predicate itself reads only Kind()).
        make(KnownTypeCode::IntPtr, "IntPtr");
        make(KnownTypeCode::UIntPtr, "UIntPtr");
    }
};

Fixture& Fix() {
    static Fixture fixture;
    return fixture;
}

LookupCompilation& Compilation() { return Fix().compilation; }

// The REGISTERED definition for a known type code (the FindType identity target).
std::shared_ptr<LookupTypeDefinition> Def(KnownTypeCode code) {
    for (const auto& t : Fix().defs) {
        if (t->KnownTypeCode() == code)
            return t;
    }
    return nullptr;
}

std::shared_ptr<LookupTypeDefinition> IntDef() { return Def(KnownTypeCode::Int32); }
std::shared_ptr<LookupTypeDefinition> LongDef() { return Def(KnownTypeCode::Int64); }
std::shared_ptr<LookupTypeDefinition> StringDef() { return Def(KnownTypeCode::String); }

// A fresh resolver over the shared compilation.
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// The C# `SpecialType.NInt` / `NUInt` shapes (Kind=NInt/NUInt, isReferenceType=false).
std::shared_ptr<SpecialType> NIntType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::NInt, false);
    return t;
}

std::shared_ptr<SpecialType> NUIntType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::NUInt, false);
    return t;
}

// A `Nullable<T>` over an element (the Create composition over the registered `Nullable`
// definition -- IsNullable resolves through FindType).
ITypePtr MakeNullableOf(const ITypePtr& element) {
    return ILSpy::Decompiler::TypeSystem::Create(Compilation(), *element);
}

// A plain (non-constant) resolve result over a type.
std::shared_ptr<ResolveResult> MakePlain(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// The dynamic_cast helper asserting the operator-result shape (the ASSERT-before-read
// discipline).
const OperatorResolveResult* AsOperator(const std::shared_ptr<ResolveResult>& r) {
    return dynamic_cast<const OperatorResolveResult*>(r.get());
}

const ErrorResolveResult* AsError(const std::shared_ptr<ResolveResult>& r) {
    return dynamic_cast<const ErrorResolveResult*>(r.get());
}

} // namespace

// The pointer-identity compare (the gtest EqHelper mixed-pointer-type learning:
// cast both sides to const IType* -- a LookupTypeDefinition*/SpecialType* argument
// cannot deduce against a const IType* otherwise).
void ExpectSameType(const IType* expected, const IType* actual) {
    EXPECT_EQ(expected, actual);
}

// ---- TypeUtils.IsCSharpNativeIntegerType ------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, NIntAndNUIntKindsAreNativeIntegerTypes) {
    EXPECT_TRUE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(NIntType().get()));
    EXPECT_TRUE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(NUIntType().get()));
}

TEST(CSharpResolverOperatorHelpersTest, IntPtrAndUIntPtrAreNotNativeIntegerTypes) {
    // The managed wrappers are Struct kinds -- the C# doc comment's explicit distinction
    // ("Returns false for (U)IntPtr").
    EXPECT_FALSE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(
        Def(KnownTypeCode::IntPtr).get()));
    EXPECT_FALSE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(
        Def(KnownTypeCode::UIntPtr).get()));
}

TEST(CSharpResolverOperatorHelpersTest, OtherKindsAreNotNativeIntegerTypes) {
    EXPECT_FALSE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(IntDef().get()));
    EXPECT_FALSE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(
        StringDef().get()));
}

TEST(CSharpResolverOperatorHelpersTest, NullInputIsNotNativeIntegerType) {
    EXPECT_FALSE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(nullptr));
}

TEST(CSharpResolverOperatorHelpersTest, NullableWrapperIsNotNativeIntegerType) {
    // A `Nullable<nint>` reports the generic's Struct kind (ParameterizedType::Kind
    // delegates to the generic), so the wrapper is NOT itself a native integer.
    ITypePtr wrapped = MakeNullableOf(NIntType());
    ASSERT_NE(wrapped, nullptr);
    EXPECT_FALSE(ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType(wrapped.get()));
}

// ---- IsNullableTypeOrNonValueType --------------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, NullableTypeIsEligible) {
    EXPECT_TRUE(CSharpResolver::IsNullableTypeOrNonValueType(*MakeNullableOf(IntDef())));
}

TEST(CSharpResolverOperatorHelpersTest, ReferenceTypeIsEligible) {
    EXPECT_TRUE(CSharpResolver::IsNullableTypeOrNonValueType(*StringDef()));
}

TEST(CSharpResolverOperatorHelpersTest, NonNullableValueTypeIsNotEligible) {
    // int is a definite value type (the ValueTypeDef stub carries the definite false a
    // plain LookupTypeDefinition lacks).
    auto int32Value = std::make_shared<ValueTypeDef>(
        "Int32", "", FullTypeName(TopLevelTypeName("", "Int32", 0)), TypeKind::Struct,
        Accessibility::Public, Compilation(), nullptr, KnownTypeCode::Int32);
    EXPECT_FALSE(CSharpResolver::IsNullableTypeOrNonValueType(*int32Value));
}

TEST(CSharpResolverOperatorHelpersTest, IndeterminateReferenceNessIsEligible) {
    // The C# `type.IsReferenceType != false` accepts an INDETERMINATE reference-ness
    // (the lifted `!= false` is falsy only for a definite false): the UnknownType
    // singleton compares against the null literal.
    EXPECT_TRUE(CSharpResolver::IsNullableTypeOrNonValueType(
        *ILSpy::Decompiler::TypeSystem::UnknownType()));
}

// ---- UnaryOperatorResolveResult ------------------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, UnaryFactoryBuildsNegateWithSingleOperand) {
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> operand = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> r = resolver->UnaryOperatorResolveResult(
        *LongDef(), UnaryOperatorType::Minus, operand);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Negate);
    ExpectSameType(LongDef().get(), &op->Type());
    ASSERT_EQ(op->Operands().size(), 1u);
    EXPECT_EQ(op->Operands()[0].get(), operand.get());
    EXPECT_FALSE(op->IsLiftedOperator());
    EXPECT_EQ(op->UserDefinedOperatorMethod(), nullptr);
}

TEST(CSharpResolverOperatorHelpersTest, UnaryFactoryThreadsCheckForOverflowIntoNegateChecked) {
    // The default resolver is unchecked (Negate); a WithCheckForOverflow(true) clone's
    // factory maps Minus to NegateChecked.
    auto uncheckedResolver = MakeResolver();
    std::shared_ptr<ResolveResult> r1 = uncheckedResolver->UnaryOperatorResolveResult(
        *LongDef(), UnaryOperatorType::Minus, MakePlain(IntDef()));
    const OperatorResolveResult* op1 = AsOperator(r1);
    ASSERT_NE(op1, nullptr);
    EXPECT_EQ(op1->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Negate);

    auto checkedResolver = MakeResolver()->WithCheckForOverflow(true);
    std::shared_ptr<ResolveResult> r2 = checkedResolver->UnaryOperatorResolveResult(
        *LongDef(), UnaryOperatorType::Minus, MakePlain(IntDef()));
    const OperatorResolveResult* op2 = AsOperator(r2);
    ASSERT_NE(op2, nullptr);
    EXPECT_EQ(op2->OperatorType(),
              ILSpy::Decompiler::TypeSystem::ExpressionType::NegateChecked);
}

TEST(CSharpResolverOperatorHelpersTest, UnaryFactoryCarriesIsLiftedFlag) {
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> r = resolver->UnaryOperatorResolveResult(
        *LongDef(), UnaryOperatorType::Minus, MakePlain(IntDef()), /*isLifted=*/ true);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    EXPECT_TRUE(op->IsLiftedOperator());
}

// ---- BinaryOperatorResolveResult ----------------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, BinaryFactoryBuildsAddWithTwoOperandsInOrder) {
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> lhs = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> rhs = MakePlain(LongDef());
    std::shared_ptr<ResolveResult> r = resolver->BinaryOperatorResolveResult(
        *LongDef(), lhs, BinaryOperatorType::Add, rhs);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Add);
    ExpectSameType(LongDef().get(), &op->Type());
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
    EXPECT_FALSE(op->IsLiftedOperator());
    EXPECT_EQ(op->UserDefinedOperatorMethod(), nullptr);
}

TEST(CSharpResolverOperatorHelpersTest, BinaryFactoryMapsNullCoalescingToCoalesce) {
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> r = resolver->BinaryOperatorResolveResult(
        *IntDef(), MakePlain(MakeNullableOf(IntDef())),
        BinaryOperatorType::NullCoalescing, MakePlain(IntDef()));
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Coalesce);
}

TEST(CSharpResolverOperatorHelpersTest, BinaryFactoryThreadsCheckForOverflowIntoAddChecked) {
    auto checkedResolver = MakeResolver()->WithCheckForOverflow(true);
    std::shared_ptr<ResolveResult> r = checkedResolver->BinaryOperatorResolveResult(
        *LongDef(), MakePlain(IntDef()), BinaryOperatorType::Add, MakePlain(IntDef()));
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::AddChecked);
}

TEST(CSharpResolverOperatorHelpersTest, BinaryFactoryCarriesIsLiftedFlag) {
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> r = resolver->BinaryOperatorResolveResult(
        *LongDef(), MakePlain(IntDef()), BinaryOperatorType::Add, MakePlain(IntDef()),
        /*isLifted=*/ true);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    EXPECT_TRUE(op->IsLiftedOperator());
}

// ---- PointerArithmeticOperator -------------------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, PointerArithmeticKnownCodeSecondOperandResolvesThroughFindType) {
    auto resolver = MakeResolver();
    auto method = resolver->PointerArithmeticOperator(
        *Def(KnownTypeCode::IntPtr), *Def(KnownTypeCode::IntPtr), KnownTypeCode::Int32);
    ASSERT_NE(method, nullptr);
    // The second parameter's type is the REGISTERED Int32 instance (FindType identity).
    auto params = method->Parameters();
    ASSERT_EQ(params.size(), 2u);
    ExpectSameType(IntDef().get(), &params[1]->Type());
    ExpectSameType(Def(KnownTypeCode::IntPtr).get(), &params[0]->Type());
}

TEST(CSharpResolverOperatorHelpersTest, PointerArithmeticKnownCodeFirstOperandResolvesThroughFindType) {
    auto resolver = MakeResolver();
    auto method = resolver->PointerArithmeticOperator(
        *Def(KnownTypeCode::IntPtr), KnownTypeCode::Byte, *Def(KnownTypeCode::IntPtr));
    ASSERT_NE(method, nullptr);
    auto params = method->Parameters();
    ASSERT_EQ(params.size(), 2u);
    ExpectSameType(Def(KnownTypeCode::Byte).get(), &params[0]->Type());
    ExpectSameType(Def(KnownTypeCode::IntPtr).get(), &params[1]->Type());
}

TEST(CSharpResolverOperatorHelpersTest, PointerArithmeticBuildsReturnTypeAndParameterOrder) {
    auto resolver = MakeResolver();
    auto method = resolver->PointerArithmeticOperator(
        *Def(KnownTypeCode::IntPtr), *Def(KnownTypeCode::IntPtr), *LongDef());
    ASSERT_NE(method, nullptr);
    // The return type is the given result type (identity).
    ExpectSameType(Def(KnownTypeCode::IntPtr).get(), &method->ReturnType());
    auto params = method->Parameters();
    ASSERT_EQ(params.size(), 2u);
    ExpectSameType(Def(KnownTypeCode::IntPtr).get(), &params[0]->Type());
    ExpectSameType(LongDef().get(), &params[1]->Type());
}

TEST(CSharpResolverOperatorHelpersTest, PointerArithmeticParametersAreUnnamed) {
    auto resolver = MakeResolver();
    auto method = resolver->PointerArithmeticOperator(
        *Def(KnownTypeCode::IntPtr), *Def(KnownTypeCode::IntPtr), *LongDef());
    ASSERT_NE(method, nullptr);
    // The C# `string.Empty` names.
    for (const auto* p : method->Parameters()) {
        EXPECT_TRUE(p->Name().empty());
    }
}

// ---- ResolveNullCoalescingOperator ---------------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, NullableLhsTriesRhsAgainstTheUnderlyingType) {
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> lhs = MakePlain(MakeNullableOf(IntDef()));
    std::shared_ptr<ResolveResult> rhs = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> r = resolver->ResolveNullCoalescingOperator(lhs, rhs);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    // The result type is the UNDERLYING type (a0), not the Nullable wrapper.
    ExpectSameType(IntDef().get(), &op->Type());
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Coalesce);
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
}

TEST(CSharpResolverOperatorHelpersTest, NonNullableLhsConvertsRhsToTheLhsType) {
    auto resolver = MakeResolver();
    // short->int style: rhs converts to the lhs's own (non-nullable) type -- modeled
    // with the identity pair (int, int).
    std::shared_ptr<ResolveResult> lhs = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> rhs = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> r = resolver->ResolveNullCoalescingOperator(lhs, rhs);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    ExpectSameType(IntDef().get(), &op->Type());
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Coalesce);
}

TEST(CSharpResolverOperatorHelpersTest, LhsConvertsToTheRhsTypeAsTheThirdArm) {
    auto resolver = MakeResolver();
    // int -> long is an implicit widening, so the THIRD arm fires (the lhs converts to
    // the rhs's type); the result type is the rhs's type.
    std::shared_ptr<ResolveResult> lhs = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> rhs = MakePlain(LongDef());
    std::shared_ptr<ResolveResult> r = resolver->ResolveNullCoalescingOperator(lhs, rhs);
    const OperatorResolveResult* op = AsOperator(r);
    ASSERT_NE(op, nullptr);
    ExpectSameType(LongDef().get(), &op->Type());
    EXPECT_EQ(op->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Coalesce);
}

TEST(CSharpResolverOperatorHelpersTest, NoConversionInEitherDirectionYieldsError) {
    auto resolver = MakeResolver();
    // string vs int: no implicit conversion in either direction.
    std::shared_ptr<ResolveResult> lhs = MakePlain(StringDef());
    std::shared_ptr<ResolveResult> rhs = MakePlain(IntDef());
    std::shared_ptr<ResolveResult> r = resolver->ResolveNullCoalescingOperator(lhs, rhs);
    const ErrorResolveResult* err = AsError(r);
    ASSERT_NE(err, nullptr);
    ExpectSameType(StringDef().get(), &err->Type());
}

TEST(CSharpResolverOperatorHelpersTest, NullableLhsWithNonConvertibleRhsFallsThroughToError) {
    auto resolver = MakeResolver();
    // Nullable<int> vs string: the underlying arm fails (string->int), the rhs-to-lhs
    // arm fails (string->Nullable<int>), and the lhs-to-rhs arm fails
    // (Nullable<int>->string) -- the nullable lhs still reaches the error.
    std::shared_ptr<ResolveResult> lhs = MakePlain(MakeNullableOf(IntDef()));
    std::shared_ptr<ResolveResult> rhs = MakePlain(StringDef());
    std::shared_ptr<ResolveResult> r = resolver->ResolveNullCoalescingOperator(lhs, rhs);
    const ErrorResolveResult* err = AsError(r);
    ASSERT_NE(err, nullptr);
    ExpectSameType(&lhs->Type(), &err->Type());
}

// ---- CreateOverloadResolution ---------------------------------------------------------------

TEST(CSharpResolverOperatorHelpersTest, CreateOverloadResolutionCarriesTheArguments) {
    auto resolver = MakeResolver();
    std::vector<std::shared_ptr<ResolveResult>> arguments = {
        MakePlain(IntDef()), MakePlain(LongDef())};
    std::unique_ptr<OverloadResolution> resolution =
        resolver->CreateOverloadResolution(arguments);
    ASSERT_NE(resolution, nullptr);
    // The arguments snapshot shares the caller's handles (pointer identity).
    const auto& stored = resolution->Arguments();
    ASSERT_EQ(stored.size(), 2u);
    EXPECT_EQ(stored[0].get(), arguments[0].get());
    EXPECT_EQ(stored[1].get(), arguments[1].get());
    EXPECT_FALSE(resolution->CheckForOverflow());
}

TEST(CSharpResolverOperatorHelpersTest, CreateOverloadResolutionThreadsCheckForOverflow) {
    auto checkedResolver = MakeResolver()->WithCheckForOverflow(true);
    std::unique_ptr<OverloadResolution> resolution = checkedResolver->CreateOverloadResolution(
        std::vector<std::shared_ptr<ResolveResult>>{MakePlain(IntDef())});
    ASSERT_NE(resolution, nullptr);
    EXPECT_TRUE(resolution->CheckForOverflow());
}

TEST(CSharpResolverOperatorHelpersTest, CreateOverloadResolutionThreadsTypeArguments) {
    auto resolver = MakeResolver();
    std::unique_ptr<OverloadResolution> resolution = resolver->CreateOverloadResolution(
        std::vector<std::shared_ptr<ResolveResult>>{MakePlain(IntDef())},
        std::nullopt,
        std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{LongDef()}});
    ASSERT_NE(resolution, nullptr);
    const auto& given = resolution->ExplicitlyGivenTypeArguments();
    ASSERT_TRUE(given.has_value());
    ASSERT_EQ(given->size(), 1u);
    ExpectSameType(LongDef().get(), (*given)[0].get());
}
