// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver condition / primitive / default-value / assignment
// regions (cpp/Decompiler/CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of
// CSharpResolver.cs lines 2671-2795 + 2798-2810 + 2814-2878 + 2941-2960).
//
// The load-bearing cruxes:
//  (a) RESOLVECONDITION: a bool operand converts by IDENTITY (the result IS the
//      input); an inconvertible operand with no `op_True` in its method table wraps
//      the invalid (None) conversion; a type with an `op_True` operator method falls
//      back to the USER-DEFINED conversion applied directly through `Convert`;
//  (b) RESOLVECONDITIONFALSE: a bool operand negates through
//      `ResolveUnaryOperator(Not, ...)` (an `OperatorResolveResult` over `Not`); a
//      type with an `op_False` operator method applies it DIRECTLY (a
//      `ConversionResolveResult`, NO negation on top);
//  (c) RESOLVECONDITIONAL: a constant bool condition with both constant branches
//      FOLDS to the selected branch; the better-conditional-conversion tiebreak picks
//      the convertible direction (the other branch is Convert-ed into the winner's
//      type); a same-instance typed pair is valid via the tie arm; an inconvertible
//      typed pair is the `ErrorResolveResult` over the true branch's type; a null
//      literal branch adopts the typed branch's type; a dynamic branch makes the
//      result dynamic; neither branch typed is the UnknownError singleton;
//  (d) RESOLVEPRIMITIVE: null is the null-literal type; a boxed value resolves
//      through its runtime TypeCode to the REGISTERED known type;
//  (e) GETDEFAULTVALUE: the per-known-type-code zeros; an ENUM reads its default
//      through its UNDERLYING type's definition; a non-primitive or definitionless
//      type has a null default;
//  (f) RESOLVEASSIGNMENT: the plain assignment wraps the Convert-ed rhs over the
//      lhs's type; a compound assignment re-shapes the two-operand binary
//      `OperatorResolveResult` (carrying the lhs and the binary result's SECOND
//      operand); a folded binary constant is returned AS-IS; a binary error is
//      returned as-is; the checkForOverflow flag threads into the LINQ node kind.

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentExpression;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::OperatorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ExpressionType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::Util::Decimal;

// A LookupTypeDefinition whose IsReferenceType is DEFINITE per its kind (the real
// metadata model: a struct/enum is a value type, everything else a reference type --
// the CSharpResolverThisBaseSizeOfTypeOf KindDef precedent).
class KindDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override {
        switch (Kind()) {
            case TypeKind::Struct:
            case TypeKind::Enum:
                return false;
            default:
                return true;
        }
    }
};

// A LookupTypeDefinition whose GetMethods(filter, options) returns a configured list,
// applying the filter faithfully (the CSharpResolverUserDefinedOperators
// MethodHostType precedent) -- the op_True / op_False scan hosts.
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        (void)options;
        if (!filter)
            return methods_;
        std::vector<const IMethod*> r;
        for (const IMethod* m : methods_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const IMethod*> methods_;
};

// A fresh shared compilation with every KnownTypeCode the region resolves through
// FindType registered as a shared-managed KindDef, plus one REGISTERED instance per
// code the identity assertions target (the type-cache model -- the
// CSharpResolverThisBaseSizeOfTypeOf Fixture precedent).
struct Fixture {
    std::unique_ptr<LookupCompilation> compilation =
        std::make_unique<LookupCompilation>();
    std::vector<std::shared_ptr<KindDef>> defs;

    static TypeKind KindForCode(KnownTypeCode code) {
        switch (code) {
            case KnownTypeCode::Object:
            case KnownTypeCode::DBNull:
            case KnownTypeCode::String:
            case KnownTypeCode::Type:
                return TypeKind::Class;
            default:
                return TypeKind::Struct;
        }
    }

    void RegisterCode(KnownTypeCode code) {
        std::string name = "T" + std::to_string(static_cast<int>(code));
        auto def = std::make_shared<KindDef>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)),
            KindForCode(code), Accessibility::Public, *compilation, nullptr, code);
        compilation->RegisterKnownType(code, def.get());
        defs.push_back(std::move(def));
    }

    Fixture() {
        for (int raw = static_cast<int>(KnownTypeCode::Object);
             raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
            RegisterCode(static_cast<KnownTypeCode>(raw));
        }
        // The CSharpOperators parameter tables resolve NullableOfT through FindType
        // when the built-in operator tables build their lifted forms (the iteration-87
        // registration trap -- an unregistered code falls back to the compilation's
        // non-shared unknown stub whose shared_from_this throws bad_weak_ptr).
        auto nullableOfT = std::make_shared<KindDef>(
            "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
            TypeKind::Struct, Accessibility::Public, *compilation, nullptr,
            KnownTypeCode::NullableOfT);
        compilation->RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT.get());
        defs.push_back(std::move(nullableOfT));
    }
};

Fixture& Fix() {
    static Fixture fixture;
    return fixture;
}

LookupCompilation& Compilation() { return *Fix().compilation; }

// The REGISTERED definition for a known type code (the FindType identity target).
std::shared_ptr<KindDef> Def(KnownTypeCode code) {
    for (const auto& t : Fix().defs) {
        if (t->KnownTypeCode() == code)
            return t;
    }
    return nullptr;
}

// A shared-managed non-generic definition with the given name and kind.
std::shared_ptr<KindDef> MakeDef(const std::string& name, TypeKind kind) {
    return std::make_shared<KindDef>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// A shared-managed enum definition with the given registered underlying primitive.
std::shared_ptr<KindDef> MakeEnum(KnownTypeCode underlyingCode) {
    auto e = MakeDef("E_" + std::to_string(static_cast<int>(underlyingCode)),
                     TypeKind::Enum);
    e->SetEnumUnderlyingType(Def(underlyingCode));
    return e;
}

// A fresh shared-managed resolver (the make_shared discipline).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// A plain (non-constant) ResolveResult over the given type.
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// A null-literal ResolveResult (the SpecialType(TypeKind::Null, true) shape).
std::shared_ptr<ResolveResult> NullArg() {
    return std::make_shared<ResolveResult>(
        std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true)));
}

// A configured operator method over the compilation (the MakeOperatorMethod
// convention; the method and its return type are kept alive by the caller).
std::shared_ptr<LookupMethod> MakeOperatorMethod(const std::string& name,
                                                 ITypePtr returnType) {
    auto m = std::make_shared<LookupMethod>(name, Compilation());
    m->SetIsOperator(true);
    m->SetStatic(true);
    m->SetReturnType(std::move(returnType));
    return m;
}

// --- ResolveCondition ------------------------------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionOfBoolOperandReturnsItUnwrapped)
{
    auto resolver = MakeResolver();
    auto input = Arg(Def(KnownTypeCode::Boolean));
    auto result = resolver->ResolveCondition(input);
    // The identity conversion returns rr unwrapped (pointer identity).
    EXPECT_EQ(result.get(), input.get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionOfInconvertibleOperandWrapsTheNoneConversion)
{
    auto resolver = MakeResolver();
    // int has no implicit conversion to bool and no op_True in its (empty) method
    // table -- the result wraps the invalid (None) conversion over bool.
    auto result = resolver->ResolveCondition(Arg(Def(KnownTypeCode::Int32)));
    auto conversion = dynamic_cast<const ConversionResolveResult*>(result.get());
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Boolean).get());
    EXPECT_EQ(conversion->ConversionShared().get(), Conversions::None().get());
    EXPECT_TRUE(result->IsError());  // !None.IsValid
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionFallsBackToTheUserDefinedOpTrue)
{
    auto resolver = MakeResolver();
    auto host = std::make_shared<MethodHostType>(
        "DB", "", FullTypeName(TopLevelTypeName("", "DB", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    auto opTrue = MakeOperatorMethod("op_True", Def(KnownTypeCode::Boolean));
    host->SetMethods({ opTrue.get() });
    auto result = resolver->ResolveCondition(Arg(host));
    auto conversion = dynamic_cast<const ConversionResolveResult*>(result.get());
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Boolean).get());
    // The op_True fallback yields a USER-DEFINED implicit conversion over the method.
    EXPECT_TRUE(conversion->ConversionProperty()->IsUserDefined());
    EXPECT_TRUE(conversion->ConversionProperty()->IsImplicit());
    EXPECT_TRUE(conversion->ConversionProperty()->IsValid());
    EXPECT_FALSE(result->IsError());
}

// --- ResolveConditionFalse -------------------------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionFalseOfBoolOperandNegatesThroughNot)
{
    auto resolver = MakeResolver();
    // A NON-constant bool operand: the implicit cast to bool is valid, so the result
    // is the NEGATION -- the predefined OperatorResolveResult over Not.
    auto result = resolver->ResolveConditionFalse(Arg(Def(KnownTypeCode::Boolean)));
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(opResult->OperatorType(), ExpressionType::Not);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Boolean).get());
    ASSERT_EQ(opResult->Operands().size(), 1u);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionFalseFallsBackToTheUserDefinedOpFalseDirectly)
{
    auto resolver = MakeResolver();
    auto host = std::make_shared<MethodHostType>(
        "DB", "", FullTypeName(TopLevelTypeName("", "DB", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    auto opFalse = MakeOperatorMethod("op_False", Def(KnownTypeCode::Boolean));
    host->SetMethods({ opFalse.get() });
    // `input.operator false()` applies DIRECTLY -- a ConversionResolveResult, NOT an
    // OperatorResolveResult (no negation on top).
    auto result = resolver->ResolveConditionFalse(Arg(host));
    auto conversion = dynamic_cast<const ConversionResolveResult*>(result.get());
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(dynamic_cast<const OperatorResolveResult*>(result.get()), nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Boolean).get());
    EXPECT_TRUE(conversion->ConversionProperty()->IsUserDefined());
}

// --- IsBetterConditionalConversion / HasType -------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     BetterConditionalConversionValidBeatsInvalid)
{
    EXPECT_TRUE(CSharpResolver::IsBetterConditionalConversion(
        Conversions::ImplicitNumericConversion(), Conversions::None()));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     BetterConditionalConversionInvalidIsNeverBetter)
{
    EXPECT_FALSE(CSharpResolver::IsBetterConditionalConversion(
        Conversions::None(), Conversions::ImplicitNumericConversion()));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     BetterConditionalConversionNonConstantBeatsConstantExpression)
{
    // A valid non-constant conversion beats the ImplicitConstantExpressionConversion.
    EXPECT_TRUE(CSharpResolver::IsBetterConditionalConversion(
        Conversions::ImplicitNumericConversion(),
        Conversions::ImplicitConstantExpressionConversion()));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     BetterConditionalConversionConstantExpressionDoesNotBeatNonConstant)
{
    EXPECT_FALSE(CSharpResolver::IsBetterConditionalConversion(
        Conversions::ImplicitConstantExpressionConversion(),
        Conversions::ImplicitNumericConversion()));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     BetterConditionalConversionBothValidNonConstant)
{
    // Two valid non-constant conversions tie (neither is better).
    EXPECT_FALSE(CSharpResolver::IsBetterConditionalConversion(
        Conversions::ImplicitNumericConversion(),
        Conversions::ImplicitReferenceConversion()));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     HasTypeRejectsTheNoneAndNullKinds)
{
    EXPECT_FALSE(CSharpResolver::HasType(*Arg(
        std::make_shared<SpecialType>(TypeKind::None))));
    EXPECT_FALSE(CSharpResolver::HasType(*NullArg()));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest, HasTypeAcceptsOtherKinds)
{
    EXPECT_TRUE(CSharpResolver::HasType(*Arg(Def(KnownTypeCode::Int32))));
    EXPECT_TRUE(CSharpResolver::HasType(*Arg(
        std::make_shared<SpecialType>(TypeKind::Dynamic, true))));
}

// --- ResolveConditional ----------------------------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalFoldsAConstantTrueConditionToTheTrueBranch)
{
    auto resolver = MakeResolver();
    auto condition = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Boolean), std::any(true));
    auto trueBranch = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Int32), std::any(std::int32_t(1)));
    auto falseBranch = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Int32), std::any(std::int32_t(2)));
    auto result = resolver->ResolveConditional(condition, trueBranch, falseBranch);
    EXPECT_EQ(result.get(), trueBranch.get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalFoldsAConstantFalseConditionToTheFalseBranch)
{
    auto resolver = MakeResolver();
    auto condition = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Boolean), std::any(false));
    auto trueBranch = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Int32), std::any(std::int32_t(1)));
    auto falseBranch = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Int32), std::any(std::int32_t(2)));
    auto result = resolver->ResolveConditional(condition, trueBranch, falseBranch);
    EXPECT_EQ(result.get(), falseBranch.get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalNonConstantConditionYieldsTheConditionalOperator)
{
    auto resolver = MakeResolver();
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto trueBranch = Arg(Def(KnownTypeCode::Int32));
    auto falseBranch = Arg(Def(KnownTypeCode::Int32));
    auto result = resolver->ResolveConditional(condition, trueBranch, falseBranch);
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(opResult->OperatorType(), ExpressionType::Conditional);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int32).get());
    ASSERT_EQ(opResult->Operands().size(), 3u);
    // The condition was ResolveCondition-converted (identity for a bool operand) and
    // the same-instance typed pair keeps its branches unwrapped (the tie arm).
    EXPECT_EQ(opResult->Operands()[0].get(), condition.get());
    EXPECT_EQ(opResult->Operands()[1].get(), trueBranch.get());
    EXPECT_EQ(opResult->Operands()[2].get(), falseBranch.get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalTiebreakConvertsTheTrueBranchToTheFalseBranchType)
{
    auto resolver = MakeResolver();
    // int -> long is an implicit numeric widening; long -> int is not -- the
    // better-conditional-conversion tiebreak picks the FALSE branch's type.
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto trueBranch = Arg(Def(KnownTypeCode::Int32));
    auto falseBranch = Arg(Def(KnownTypeCode::Int64));
    auto result = resolver->ResolveConditional(condition, trueBranch, falseBranch);
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int64).get());
    // The TRUE branch was converted into the result type; the false branch was not.
    auto convertedTrue = dynamic_cast<const ConversionResolveResult*>(
        opResult->Operands()[1].get());
    ASSERT_NE(convertedTrue, nullptr);
    EXPECT_EQ(&convertedTrue->Type(), Def(KnownTypeCode::Int64).get());
    EXPECT_EQ(opResult->Operands()[2].get(), falseBranch.get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalTiebreakMirrorConvertsTheFalseBranch)
{
    auto resolver = MakeResolver();
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto trueBranch = Arg(Def(KnownTypeCode::Int64));
    auto falseBranch = Arg(Def(KnownTypeCode::Int32));
    auto result = resolver->ResolveConditional(condition, trueBranch, falseBranch);
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int64).get());
    EXPECT_EQ(opResult->Operands()[1].get(), trueBranch.get());
    auto convertedFalse = dynamic_cast<const ConversionResolveResult*>(
        opResult->Operands()[2].get());
    ASSERT_NE(convertedFalse, nullptr);
    EXPECT_EQ(&convertedFalse->Type(), Def(KnownTypeCode::Int64).get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalInconvertibleTypedPairIsErrorOverTheTrueBranchType)
{
    auto resolver = MakeResolver();
    // bool and string neither convert to each other nor are equal -- the tie arm with
    // an unequal type pair is invalid: the ErrorResolveResult over the TRUE branch's
    // type.
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto result = resolver->ResolveConditional(
        condition, Arg(Def(KnownTypeCode::Boolean)), Arg(Def(KnownTypeCode::String)));
    EXPECT_NE(dynamic_cast<const ErrorResolveResult*>(result.get()), nullptr);
    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Boolean).get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalNullLiteralBranchAdoptsTheTypedBranchType)
{
    auto resolver = MakeResolver();
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto trueBranch = Arg(Def(KnownTypeCode::String));
    auto result = resolver->ResolveConditional(condition, trueBranch, NullArg());
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::String).get());
    // The null literal was TryConvert-ed to the typed branch's type.
    auto convertedNull = dynamic_cast<const ConversionResolveResult*>(
        opResult->Operands()[2].get());
    ASSERT_NE(convertedNull, nullptr);
    EXPECT_EQ(&convertedNull->Type(), Def(KnownTypeCode::String).get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalDynamicBranchMakesTheResultDynamic)
{
    auto resolver = MakeResolver();
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto trueBranch = Arg(
        std::make_shared<SpecialType>(TypeKind::Dynamic, true));
    auto result = resolver->ResolveConditional(condition, trueBranch, NullArg());
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(result->Type().Kind(), TypeKind::Dynamic);
    // BOTH branches were TryConvert-ed to dynamic (the non-short-circuit &).
    auto convertedTrue = dynamic_cast<const ConversionResolveResult*>(
        opResult->Operands()[1].get());
    ASSERT_NE(convertedTrue, nullptr);
    EXPECT_EQ(convertedTrue->Type().Kind(), TypeKind::Dynamic);
    auto convertedFalse = dynamic_cast<const ConversionResolveResult*>(
        opResult->Operands()[2].get());
    ASSERT_NE(convertedFalse, nullptr);
    EXPECT_EQ(convertedFalse->Type().Kind(), TypeKind::Dynamic);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     ConditionalNeitherBranchTypedIsTheUnknownErrorSingleton)
{
    auto resolver = MakeResolver();
    auto condition = Arg(Def(KnownTypeCode::Boolean));
    auto result = resolver->ResolveConditional(condition, NullArg(), NullArg());
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

// --- ResolvePrimitive ------------------------------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     PrimitiveNullIsTheNullLiteralType)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolvePrimitive(std::any());
    EXPECT_EQ(result->Type().Kind(), TypeKind::Null);
    EXPECT_FALSE(result->IsCompileTimeConstant());
    EXPECT_EQ(dynamic_cast<const ConstantResolveResult*>(result.get()), nullptr);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     PrimitiveIntResolvesOverTheRegisteredInt32)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolvePrimitive(std::any(std::int32_t(5)));
    auto constant = dynamic_cast<const ConstantResolveResult*>(result.get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int32).get());
    EXPECT_EQ(std::any_cast<std::int32_t>(result->ConstantValue()), 5);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     PrimitiveStringResolvesOverTheRegisteredString)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolvePrimitive(std::any(std::string("x")));
    auto constant = dynamic_cast<const ConstantResolveResult*>(result.get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::String).get());
    EXPECT_EQ(std::any_cast<std::string>(result->ConstantValue()), "x");
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     PrimitiveBoolResolvesOverTheRegisteredBoolean)
{
    auto resolver = MakeResolver();
    auto result = resolver->ResolvePrimitive(std::any(true));
    auto constant = dynamic_cast<const ConstantResolveResult*>(result.get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Boolean).get());
    EXPECT_TRUE(std::any_cast<bool>(result->ConstantValue()));
}

// --- GetDefaultValue / ResolveDefaultValue ---------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     DefaultValueOfPrimitivesFoldsTheZeros)
{
    EXPECT_FALSE(std::any_cast<bool>(CSharpResolver::GetDefaultValue(
        *Def(KnownTypeCode::Boolean))));
    EXPECT_EQ(std::any_cast<char16_t>(
        CSharpResolver::GetDefaultValue(*Def(KnownTypeCode::Char))), char16_t(0));
    EXPECT_EQ(std::any_cast<std::int32_t>(
        CSharpResolver::GetDefaultValue(*Def(KnownTypeCode::Int32))), 0);
    EXPECT_EQ(std::any_cast<std::uint8_t>(
        CSharpResolver::GetDefaultValue(*Def(KnownTypeCode::Byte))), std::uint8_t(0));
    EXPECT_EQ(std::any_cast<std::uint64_t>(
        CSharpResolver::GetDefaultValue(*Def(KnownTypeCode::UInt64))), std::uint64_t(0));
    EXPECT_EQ(std::any_cast<double>(
        CSharpResolver::GetDefaultValue(*Def(KnownTypeCode::Double))), 0.0);
    Decimal decimalZero = std::any_cast<Decimal>(
        CSharpResolver::GetDefaultValue(*Def(KnownTypeCode::Decimal)));
    EXPECT_EQ(decimalZero.mantissa, 0);
    EXPECT_EQ(decimalZero.scale, 0);
    EXPECT_FALSE(decimalZero.isNegative);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     DefaultValueOfEnumReadsThroughItsUnderlyingType)
{
    // An enum whose underlying is Byte reads the BYTE default (uint8_t 0), not null.
    auto overByte = MakeEnum(KnownTypeCode::Byte);
    auto value = CSharpResolver::GetDefaultValue(*overByte);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(std::any_cast<std::uint8_t>(value), std::uint8_t(0));
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     DefaultValueOfNonPrimitiveIsNull)
{
    auto structDef = MakeDef("S", TypeKind::Struct);
    EXPECT_FALSE(CSharpResolver::GetDefaultValue(*structDef).has_value());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     DefaultValueOfDefinitionlessTypeIsNull)
{
    // A SpecialType has no definition -- the C# null default.
    auto unknown = std::make_shared<SpecialType>(TypeKind::Struct, false);
    EXPECT_FALSE(CSharpResolver::GetDefaultValue(*unknown).has_value());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     DefaultValueExpressionCarriesTheTypeAndTheZero)
{
    auto resolver = MakeResolver();
    auto int32 = Def(KnownTypeCode::Int32);
    auto result = resolver->ResolveDefaultValue(*int32);
    auto constant = dynamic_cast<const ConstantResolveResult*>(result.get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(&result->Type(), int32.get());
    EXPECT_TRUE(result->IsCompileTimeConstant());
    EXPECT_EQ(std::any_cast<std::int32_t>(result->ConstantValue()), 0);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     DefaultValueExpressionOfNonPrimitiveIsNullValuedButConstant)
{
    auto resolver = MakeResolver();
    auto structDef = MakeDef("S", TypeKind::Struct);
    auto result = resolver->ResolveDefaultValue(*structDef);
    auto constant = dynamic_cast<const ConstantResolveResult*>(result.get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(&result->Type(), structDef.get());
    // The default of a non-primitive struct is a NULL ConstantValue, but the result
    // is still a compile-time constant (the ConstantResolveResult override).
    EXPECT_TRUE(result->IsCompileTimeConstant());
    EXPECT_FALSE(result->ConstantValue().has_value());
}

// --- ResolveAssignment -----------------------------------------------------------------------

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     PlainAssignmentConvertsTheRhsToTheLhsType)
{
    auto resolver = MakeResolver();
    auto lhs = Arg(Def(KnownTypeCode::Int64));
    auto rhs = Arg(Def(KnownTypeCode::Int32));
    auto result = resolver->ResolveAssignment(
        AssignmentOperatorType::Assign, lhs, rhs);
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(opResult->OperatorType(), ExpressionType::Assign);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int64).get());
    ASSERT_EQ(opResult->Operands().size(), 2u);
    EXPECT_EQ(opResult->Operands()[0].get(), lhs.get());
    // The rhs was converted to the lhs's type (int -> long numeric widening).
    auto convertedRhs = dynamic_cast<const ConversionResolveResult*>(
        opResult->Operands()[1].get());
    ASSERT_NE(convertedRhs, nullptr);
    EXPECT_EQ(&convertedRhs->Type(), Def(KnownTypeCode::Int64).get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     CompoundAssignmentReshapesTheBinaryOperatorResult)
{
    auto resolver = MakeResolver();
    auto lhs = Arg(Def(KnownTypeCode::Int32));
    auto rhs = Arg(Def(KnownTypeCode::Int32));
    auto result = resolver->ResolveAssignment(
        AssignmentOperatorType::Add, lhs, rhs);
    auto opResult = dynamic_cast<const OperatorResolveResult*>(result.get());
    ASSERT_NE(opResult, nullptr);
    EXPECT_EQ(opResult->OperatorType(), ExpressionType::AddAssign);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int32).get());
    ASSERT_EQ(opResult->Operands().size(), 2u);
    // The operands are the ORIGINAL lhs and the binary result's SECOND operand (the
    // identity-converted rhs -- the registered-instance type-cache model).
    EXPECT_EQ(opResult->Operands()[0].get(), lhs.get());
    EXPECT_EQ(opResult->Operands()[1].get(), rhs.get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     CompoundAssignmentThreadsTheCheckForOverflowFlag)
{
    auto lhs = Arg(Def(KnownTypeCode::Int32));
    auto rhs = Arg(Def(KnownTypeCode::Int32));
    auto uncheckedResult = MakeResolver()->ResolveAssignment(
        AssignmentOperatorType::Add, lhs, rhs);
    auto uncheckedOp = dynamic_cast<const OperatorResolveResult*>(uncheckedResult.get());
    ASSERT_NE(uncheckedOp, nullptr);
    EXPECT_EQ(uncheckedOp->OperatorType(), ExpressionType::AddAssign);

    auto checkedResult = MakeResolver()->WithCheckForOverflow(true)->ResolveAssignment(
        AssignmentOperatorType::Add, lhs, rhs);
    auto checkedOp = dynamic_cast<const OperatorResolveResult*>(checkedResult.get());
    ASSERT_NE(checkedOp, nullptr);
    EXPECT_EQ(checkedOp->OperatorType(), ExpressionType::AddAssignChecked);
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     CompoundAssignmentOfTwoConstantsFoldsThroughTheBinaryResult)
{
    auto resolver = MakeResolver();
    auto lhs = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Int32), std::any(std::int32_t(5)));
    auto rhs = std::make_shared<ConstantResolveResult>(
        Def(KnownTypeCode::Int32), std::any(std::int32_t(3)));
    // The binary resolution FOLDS both constants (5 + 3 = 8) -- the folded
    // ConstantResolveResult is NOT an OperatorResolveResult, so the assignment
    // returns it AS-IS.
    auto result = resolver->ResolveAssignment(
        AssignmentOperatorType::Add, lhs, rhs);
    auto constant = dynamic_cast<const ConstantResolveResult*>(result.get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(std::any_cast<std::int32_t>(result->ConstantValue()), 8);
    EXPECT_EQ(&result->Type(), Def(KnownTypeCode::Int32).get());
}

TEST(CSharpResolverConditionPrimitiveDefaultAssignmentTest,
     CompoundAssignmentOfAnInconvertiblePairReturnsTheBinaryError)
{
    auto resolver = MakeResolver();
    auto lhs = Arg(Def(KnownTypeCode::String));
    auto rhs = Arg(Def(KnownTypeCode::String));
    // string % string has no applicable remainder operator -- the binary resolution
    // is an ErrorResolveResult, returned AS-IS (not an OperatorResolveResult).
    auto result = resolver->ResolveAssignment(
        AssignmentOperatorType::Modulus, lhs, rhs);
    EXPECT_NE(dynamic_cast<const ErrorResolveResult*>(result.get()), nullptr);
    EXPECT_TRUE(result->IsError());
}

} // namespace
