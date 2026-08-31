// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the TypeSystemAstBuilder "Convert Constant Value" SUPPORT region
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 1168-1249 + 1496-1582: IsSpecialConstant +
// ConvertFloatingPointLiteral + MakeConstant) -- the tested-but-not-yet-wired
// foundation ahead of the mutually-recursive ConvertConstantValue /
// ConvertEnumValue core (which lands as a later slice consuming these three).
//
// The load-bearing cruxes:
//  (a) IsSpecialConstant: a table value (int.MaxValue, double.NaN, ...) with a
//      resolvable BCL field renders as a TypeReferenceExpression.MemberName
//      member reference (gated on UseSpecialConstants), the expectedType NOT
//      being the known type routes through compilation.FindType (the registry),
//      and the three non-encodable floating-point values (+Infty / -Infty / NaN)
//      with NO resolvable field produce the equivalent arithmetic expression
//      (-1.0 / 0.0 &c.) with UNCONDITIONAL ConstantResolveResult annotations --
//      they fire even when UseSpecialConstants is disabled (the
//      `!UseSpecialConstants || field == null` gate);
//  (b) The SingleOrDefault contract: more than one same-name field throws
//      (the GetInlineArrayElementType SingleOrDefault-throw precedent);
//  (c) ConvertFloatingPointLiteral: a whole value or a short-decimal form
//      renders as a plain PrimitiveExpression, a long-decimal form as the exact
//      fraction num / den (FractionApprox / IsValidFraction / IsEqual), the
//      leading CSharpPrimitiveCast coercion (an int-boxed 0 embedded in a
//      double-typed constant signature), and the DEFERRED Math.PI / MathE
//      extraction (TryExtractExpression) falling through to the plain
//      PrimitiveExpression -- pinned by PI itself (355/113 is not a valid
//      fraction: 355 >= 113);
//  (d) MakeConstant: the long numerator/denominator cast through the type's
//      TypeCode with overflow CHECKING (an out-of-range term throws
//      Util::OverflowException), and the small-integer WIDENING of the
//      std::any -> PrimitiveValue boxing bridge (a sbyte-typed result reads
//      int32 5, the documented PrimitiveValue-alternative-set deviation).

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"

#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::Expression;
using ILSpy::Decompiler::CSharp::Syntax::MemberReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::TypeReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::Util::OverflowException;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

// A shared-managed type definition stub with a configurable `GetFields` table
// (the MethodHostType pattern: the shared LookupTypeDefinition inherits the
// IType empty default; the stored pointers are non-owning, the caller keeps the
// IField stubs alive).
class FieldHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetFields(std::vector<const IField*> fields) { fields_ = std::move(fields); }

    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter = nullptr,
        TS::GetMemberOptions options = TS::GetMemberOptions::None) const override {
        (void)options;
        std::vector<const IField*> result;
        for (const IField* field : fields_) {
            if (filter == nullptr || filter(field))
                result.push_back(field);
        }
        return result;
    }

private:
    std::vector<const IField*> fields_;
};

// A minimal `IField` stub (the CSharpResolverSimpleName TestField pattern):
// a configurable name over a non-null return type (the MemberResolveResult
// ComputeType contract) and a configurable compilation.
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return false;
    }
    const ::ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ::ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override
    {
        return this;
    }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override
    {
        return nullptr;
    }
    const ::ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override
    {
        return this;
    }
    bool Equals(const ::ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ::ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override
    {
        return obj == this;
    }
    const IType& Type() const override { return *returnType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
};

// A shared-managed known-type definition host (the registered-instance
// type-cache model: FindType must return the SAME instance the tests build
// their constants over).
std::shared_ptr<FieldHostType> MakeHost(const std::string& name,
                                        const std::string& ns,
                                        KnownTypeCode code,
                                        const ICompilation& compilation) {
    return std::make_shared<FieldHostType>(name, ns,
        FullTypeName(TopLevelTypeName(ns, name, 0)), TypeKind::Struct,
        Accessibility::Public, compilation, nullptr, code);
}

// Retrieves the double/float/int32 alternative a PrimitiveExpression's Value
// holds (a test-side convenience over the PrimitiveValue variant).
double HeldDouble(const PrimitiveExpression* expression) {
    return std::get<double>(expression->Value());
}
float HeldFloat(const PrimitiveExpression* expression) {
    return std::get<float>(expression->Value());
}
std::int32_t HeldInt32(const PrimitiveExpression* expression) {
    return std::get<std::int32_t>(expression->Value());
}

} // namespace

namespace {

// The IsSpecialConstant suite.
class IsSpecialConstantTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_;
    std::shared_ptr<FieldHostType> int32_;
    std::shared_ptr<FieldHostType> double_;
    TypeSystemAstBuilder builder_;

    IsSpecialConstantTest()
        : compilation_(std::make_unique<LookupCompilation>()),
          int32_(MakeHost("Int32", "System", KnownTypeCode::Int32, *compilation_)),
          double_(MakeHost("Double", "System", KnownTypeCode::Double, *compilation_)) {
        compilation_->RegisterKnownType(KnownTypeCode::Int32, int32_.get());
        compilation_->RegisterKnownType(KnownTypeCode::Double, double_.get());
    }
};

// A non-table value is not a special constant.
TEST_F(IsSpecialConstantTest, NonSpecialConstantReturnsFalse) {
    Expression* expression = reinterpret_cast<Expression*>(0x1); // garbage, must be reset
    EXPECT_FALSE(builder_.IsSpecialConstant(*int32_, std::any(std::int32_t(5)), expression));
    EXPECT_EQ(nullptr, expression);
}

// A table value with a resolvable field renders as the member reference.
TEST_F(IsSpecialConstantTest, SpecialConstantWithFieldRendersMemberReference) {
    const TestField maxValue("MaxValue", int32_, *compilation_);
    int32_->SetFields({&maxValue});

    Expression* expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *int32_, std::any(std::numeric_limits<std::int32_t>::max()), expression));
    ASSERT_NE(nullptr, expression);
    const auto* memberReference = dynamic_cast<const MemberReferenceExpression*>(expression);
    ASSERT_NE(nullptr, memberReference);
    EXPECT_EQ("MaxValue", memberReference->MemberName());
    const auto* typeReference = dynamic_cast<const TypeReferenceExpression*>(memberReference->Target());
    ASSERT_NE(nullptr, typeReference);
    // The resolver-less builder with UseKeywordsForBuiltinTypes (the default)
    // renders the Int32 definition as the `int` keyword PrimitiveType.
    const auto* primitiveType = dynamic_cast<const PrimitiveType*>(typeReference->Type());
    ASSERT_NE(nullptr, primitiveType);
    EXPECT_EQ("int", primitiveType->Keyword());
}

// The annotations are gated on AddResolveResultAnnotations; the member reference
// carries a MemberResolveResult over a TypeResolveResult target.
TEST_F(IsSpecialConstantTest, MemberReferenceAnnotationGatedOnFlag) {
    const TestField maxValue("MaxValue", int32_, *compilation_);
    int32_->SetFields({&maxValue});
    builder_.AddResolveResultAnnotations() = true;

    Expression* expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *int32_, std::any(std::numeric_limits<std::int32_t>::max()), expression));
    ASSERT_NE(nullptr, expression);
    const auto* memberResolveResult =
        expression->Annotation<MemberResolveResult>();
    ASSERT_NE(nullptr, memberResolveResult);
    const auto* target = dynamic_cast<const TypeResolveResult*>(memberResolveResult->TargetResult());
    ASSERT_NE(nullptr, target);
    EXPECT_EQ(static_cast<const IType*>(int32_.get()), &target->Type());
    EXPECT_EQ(static_cast<const TestField*>(&maxValue),
              dynamic_cast<const TestField*>(memberResolveResult->Member()));
}

// An expectedType that is not the constant's known type routes the constant
// type through compilation.FindType (the registry), not the expectedType.
TEST_F(IsSpecialConstantTest, ResolvesConstantTypeThroughCompilation) {
    const TestField maxValue("MaxValue", int32_, *compilation_);
    int32_->SetFields({&maxValue});
    auto custom = MakeHost("MyType", "My", KnownTypeCode::None, *compilation_);

    Expression* expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *custom, std::any(std::numeric_limits<std::int32_t>::max()), expression));
    ASSERT_NE(nullptr, expression);
    const auto* memberReference = dynamic_cast<const MemberReferenceExpression*>(expression);
    ASSERT_NE(nullptr, memberReference);
    // The rendered type is the REGISTERED Int32 definition (FindType), not MyType:
    // the builtin-keyword short-circuit renders `int` (the custom MyType would
    // render as a MemberType over its namespace instead).
    const auto* typeReference = dynamic_cast<const TypeReferenceExpression*>(memberReference->Target());
    ASSERT_NE(nullptr, typeReference);
    const auto* primitiveType = dynamic_cast<const PrimitiveType*>(typeReference->Type());
    ASSERT_NE(nullptr, primitiveType);
    EXPECT_EQ("int", primitiveType->Keyword());
}

// A resolver-less builder over a definitionless expectedType has no compilation
// to resolve the constant type through: not special (the documented null
// safe-fallback for the C# NRE-on-null-Owner shape).
TEST_F(IsSpecialConstantTest, DefinitionlessExpectedTypeWithoutCompilationReturnsFalse) {
    TS::SpecialType definitionless(TypeKind::Unknown);
    Expression* expression = nullptr;
    EXPECT_FALSE(builder_.IsSpecialConstant(
        definitionless, std::any(std::numeric_limits<std::int32_t>::max()), expression));
    EXPECT_EQ(nullptr, expression);
}

// The three non-encodable doubles (+Infty / -Infty / NaN) with no resolvable
// field produce the equivalent arithmetic expressions, with UNCONDITIONAL
// ConstantResolveResult annotations (the C# WithRR is not flag-gated here).
TEST_F(IsSpecialConstantTest, DoubleInfinitiesAndNaNWithoutFieldRenderArithmeticExpressions) {
    Expression* expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *double_, std::any(-std::numeric_limits<double>::infinity()), expression));
    ASSERT_NE(nullptr, expression);
    auto* division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(BinaryOperatorType::Divide, division->Operator());
    EXPECT_EQ(-1.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(0.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Right())));
    const auto* resultRR = division->Annotation<ConstantResolveResult>();
    ASSERT_NE(nullptr, resultRR);
    const double resultValue = std::any_cast<double>(resultRR->ConstantValue());
    EXPECT_TRUE(std::isinf(resultValue) && resultValue < 0);

    expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *double_, std::any(std::numeric_limits<double>::infinity()), expression));
    division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(1.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(0.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Right())));

    expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *double_, std::any(std::numeric_limits<double>::quiet_NaN()), expression));
    division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(0.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(0.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Right())));
    const auto* nanRR = division->Annotation<ConstantResolveResult>();
    ASSERT_NE(nullptr, nanRR);
    EXPECT_TRUE(std::isnan(std::any_cast<double>(nanRR->ConstantValue())));
    // The unconditional annotations fire even with the flag off.
    EXPECT_FALSE(builder_.AddResolveResultAnnotations());
    EXPECT_NE(nullptr, division->Left()->Annotation<ConstantResolveResult>());
    EXPECT_NE(nullptr, division->Right()->Annotation<ConstantResolveResult>());
}

// The float twins of the arithmetic fallbacks.
TEST_F(IsSpecialConstantTest, SingleInfinitiesAndNaNWithoutFieldRenderArithmeticExpressions) {
    auto single = MakeHost("Single", "System", KnownTypeCode::Single, *compilation_);
    compilation_->RegisterKnownType(KnownTypeCode::Single, single.get());

    Expression* expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *single, std::any(-std::numeric_limits<float>::infinity()), expression));
    ASSERT_NE(nullptr, expression);
    auto* division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(BinaryOperatorType::Divide, division->Operator());
    EXPECT_EQ(-1.0f, HeldFloat(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(0.0f, HeldFloat(dynamic_cast<const PrimitiveExpression*>(division->Right())));

    expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *single, std::any(std::numeric_limits<float>::quiet_NaN()), expression));
    division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(0.0f, HeldFloat(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(0.0f, HeldFloat(dynamic_cast<const PrimitiveExpression*>(division->Right())));
}

// A table value that is NOT one of the three non-encodable values (MinValue /
// MaxValue / Epsilon) with no resolvable field is NOT special: the C# switch
// falls through and the method returns false.
TEST_F(IsSpecialConstantTest, MinValueWithoutFieldIsNotSpecial) {
    Expression* expression = nullptr;
    EXPECT_FALSE(builder_.IsSpecialConstant(
        *double_, std::any(-std::numeric_limits<double>::max()), expression));
    EXPECT_EQ(nullptr, expression);
}

// The UseSpecialConstants=false flag skips the member reference (the
// `!UseSpecialConstants` gate) but the three non-encodable values still produce
// their arithmetic expressions (the gate ENTERS the fallback block).
TEST_F(IsSpecialConstantTest, UseSpecialConstantsDisabledSkipsMemberReferenceOnly) {
    const TestField maxValue("MaxValue", int32_, *compilation_);
    int32_->SetFields({&maxValue});
    builder_.UseSpecialConstants() = false;

    Expression* expression = nullptr;
    EXPECT_FALSE(builder_.IsSpecialConstant(
        *int32_, std::any(std::numeric_limits<std::int32_t>::max()), expression));
    EXPECT_EQ(nullptr, expression);

    expression = nullptr;
    EXPECT_TRUE(builder_.IsSpecialConstant(
        *double_, std::any(std::numeric_limits<double>::quiet_NaN()), expression));
    EXPECT_NE(nullptr, dynamic_cast<BinaryOperatorExpression*>(expression));
}

// More than one same-name field violates the SingleOrDefault contract: the C#
// throws InvalidOperationException, the port std::runtime_error.
TEST_F(IsSpecialConstantTest, AmbiguousFieldsThrow) {
    const TestField first("MaxValue", int32_, *compilation_);
    const TestField second("MaxValue", int32_, *compilation_);
    int32_->SetFields({&first, &second});

    Expression* expression = nullptr;
    EXPECT_THROW(builder_.IsSpecialConstant(
        *int32_, std::any(std::numeric_limits<std::int32_t>::max()), expression),
                 std::runtime_error);
}

} // namespace

namespace {

// The ConvertFloatingPointLiteral + MakeConstant suite.
class ConvertFloatingPointTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<FieldHostType> double_ =
        MakeHost("Double", "System", KnownTypeCode::Double, *compilation_);
    std::shared_ptr<FieldHostType> single_ =
        MakeHost("Single", "System", KnownTypeCode::Single, *compilation_);
    std::shared_ptr<FieldHostType> int32_ =
        MakeHost("Int32", "System", KnownTypeCode::Int32, *compilation_);
    TypeSystemAstBuilder builder_;

    ConvertFloatingPointTest() {
        compilation_->RegisterKnownType(KnownTypeCode::Double, double_.get());
        compilation_->RegisterKnownType(KnownTypeCode::Single, single_.get());
        compilation_->RegisterKnownType(KnownTypeCode::Int32, int32_.get());
    }
};

// A whole double renders as a plain literal (no fraction).
TEST_F(ConvertFloatingPointTest, WholeDoubleRendersPrimitiveExpression) {
    auto* expression = builder_.ConvertFloatingPointLiteral(*double_, std::any(100.0));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(100.0, HeldDouble(primitive));
}

// A whole float renders as a plain literal.
TEST_F(ConvertFloatingPointTest, WholeFloatRendersPrimitiveExpression) {
    auto* expression = builder_.ConvertFloatingPointLiteral(*single_, std::any(3.0f));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(3.0f, HeldFloat(primitive));
}

// A short-decimal form (useFraction false) renders as a plain literal.
TEST_F(ConvertFloatingPointTest, ShortDecimalRendersPrimitiveExpression) {
    auto* expression = builder_.ConvertFloatingPointLiteral(*double_, std::any(1.5));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(1.5, HeldDouble(primitive));
}

// A long-decimal double with an exact small-denominator fraction renders as
// `num / den` (the FractionApprox / IsValidFraction / IsEqual pipeline).
TEST_F(ConvertFloatingPointTest, LongDecimalDoubleRendersFraction) {
    auto* expression = builder_.ConvertFloatingPointLiteral(*double_, std::any(1.0 / 3.0));
    auto* division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(BinaryOperatorType::Divide, division->Operator());
    EXPECT_EQ(1.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(3.0, HeldDouble(dynamic_cast<const PrimitiveExpression*>(division->Right())));
}

// The float twin of the fraction arm (the 200 max-denominator search).
TEST_F(ConvertFloatingPointTest, LongDecimalFloatRendersFraction) {
    auto* expression = builder_.ConvertFloatingPointLiteral(*single_, std::any(1.0f / 3.0f));
    auto* division = dynamic_cast<BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, division);
    EXPECT_EQ(BinaryOperatorType::Divide, division->Operator());
    EXPECT_EQ(1.0f, HeldFloat(dynamic_cast<const PrimitiveExpression*>(division->Left())));
    EXPECT_EQ(3.0f, HeldFloat(dynamic_cast<const PrimitiveExpression*>(division->Right())));
}

// PI itself: the best fraction (355/113) is NOT a valid fraction (355 >= 113),
// and the Math.PI/E extraction (TryExtractExpression) is deferred -- so the
// plain PrimitiveExpression fallback fires. This test PINs the documented
// deferred-arm divergence (the C# would render Math.PI here).
TEST_F(ConvertFloatingPointTest, PiRendersAsPlainPrimitiveExpression) {
    auto* expression = builder_.ConvertFloatingPointLiteral(
        *double_, std::any(3.141592653589793));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(3.141592653589793, HeldDouble(primitive));
}

// The leading CSharpPrimitiveCast coercion: an int-boxed 0 embedded in a
// double-typed constant signature becomes the double literal 0.0.
TEST_F(ConvertFloatingPointTest, IntConstantCoercedToDouble) {
    auto* expression = builder_.ConvertFloatingPointLiteral(*double_, std::any(std::int32_t(0)));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(0.0, HeldDouble(primitive));
}

// The ConstantResolveResult annotation is gated on AddResolveResultAnnotations
// and carries the type + the coerced value.
TEST_F(ConvertFloatingPointTest, AnnotationGatedOnFlag) {
    builder_.AddResolveResultAnnotations() = true;
    auto* expression = builder_.ConvertFloatingPointLiteral(*double_, std::any(2.5));
    const auto* rr = expression->Annotation<ConstantResolveResult>();
    ASSERT_NE(nullptr, rr);
    EXPECT_EQ(static_cast<const IType*>(double_.get()), &rr->Type());
    EXPECT_EQ(2.5, std::any_cast<double>(rr->ConstantValue()));
}

// MakeConstant casts the long through the type's TypeCode.
TEST_F(ConvertFloatingPointTest, MakeConstantCastsThroughTypeCode) {
    auto* doubleConstant = builder_.MakeConstant(*double_, 5);
    const auto* doublePrimitive = dynamic_cast<const PrimitiveExpression*>(doubleConstant);
    ASSERT_NE(nullptr, doublePrimitive);
    EXPECT_EQ(5.0, HeldDouble(doublePrimitive));

    auto* floatConstant = builder_.MakeConstant(*single_, 3);
    const auto* floatPrimitive = dynamic_cast<const PrimitiveExpression*>(floatConstant);
    ASSERT_NE(nullptr, floatPrimitive);
    EXPECT_EQ(3.0f, HeldFloat(floatPrimitive));

    auto* intConstant = builder_.MakeConstant(*int32_, 7);
    const auto* intPrimitive = dynamic_cast<const PrimitiveExpression*>(intConstant);
    ASSERT_NE(nullptr, intPrimitive);
    EXPECT_EQ(7, HeldInt32(intPrimitive));
}

// MakeConstant checks for overflow (the C# checkForOverflow: true): an
// out-of-range fraction term throws the OverflowException.
TEST_F(ConvertFloatingPointTest, MakeConstantChecksForOverflow) {
    EXPECT_THROW(builder_.MakeConstant(*int32_, 3000000000LL), OverflowException);
}

// A small-integer target WIDENS to the int/uint PrimitiveValue alternatives
// (the documented boxing-bridge deviation: the C# holds a boxed sbyte 5, the
// port's variant has no sbyte alternative and reads int32 5).
TEST_F(ConvertFloatingPointTest, MakeConstantWidensSmallIntegers) {
    auto sbyte = MakeHost("SByte", "System", KnownTypeCode::SByte, *compilation_);
    compilation_->RegisterKnownType(KnownTypeCode::SByte, sbyte.get());
    auto* constant = builder_.MakeConstant(*sbyte, 5);
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(constant);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(5, HeldInt32(primitive));
}

} // namespace
