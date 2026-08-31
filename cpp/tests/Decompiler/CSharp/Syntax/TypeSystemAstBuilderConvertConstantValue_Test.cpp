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

// Tests for the TypeSystemAstBuilder "Convert Constant Value" CORE region
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 998-1078 + 1306-1480: the three
// ConvertConstantValue overloads + ConvertEnumValue) -- the mutually-recursive
// core consuming the iteration-119 support helpers (IsSpecialConstant +
// ConvertFloatingPointLiteral + MakeConstant).
//
// The load-bearing cruxes:
//  (a) The ResolveResult overload: the ConversionResolveResult unwrap (the boxing
//      flag wrapping a small-integer literal in a cast), the TypeOfResolveResult /
//      ArrayCreateResolveResult shapes (the array-specifier MoveTo + the size
//      argument / initializer element split), and the ErrorExpression fallback;
//  (b) The 3-arg overload: the null-literal split (NullReferenceExpression over a
//      reference / nullable / pointer target vs DefaultValueExpression over a
//      value type), the IType-boxed typeof constant, the params-array
//      CustomAttributeTypedArgument vector, the enum routing into ConvertEnumValue,
//      the special-constant / floating-point delegations, the small-integer int32
//      remap (with the cast wrap when the literal type mismatches the expected
//      type), the hexadecimal format flag, and the Unknown-kind cast wrap;
//  (c) ConvertEnumValue: the exact-match member reference, the [Flags]
//      bitwise-OR decomposition, the complement ~X form (with the
//      byte-based-inside-initializer complementCompiles gate), the
//      declaringEnumMember shape (unqualified identifiers, the declared-later
//      members staying numeric, the earlier-mask encoding staying numeric), and
//      the numeric-cast fallback.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"

#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::ArrayCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::ArrayInitializerExpression;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::CastExpression;
using ILSpy::Decompiler::CSharp::Syntax::DefaultValueExpression;
using ILSpy::Decompiler::CSharp::Syntax::ErrorExpression;
using ILSpy::Decompiler::CSharp::Syntax::Expression;
using ILSpy::Decompiler::CSharp::Syntax::IdentifierExpression;
using ILSpy::Decompiler::CSharp::Syntax::MemberReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::TypeOfExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::Semantics::ArrayCreateResolveResult;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeOfResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// A minimal IField stub with the configurable surface ConvertEnumValue reads:
// the name, the return type (the enum definition itself -- a make_shared'd
// handle, since the MemberResolveResult ComputeType takes shared_from_this over
// it), the IsConst flag, the boxed constant value, and the raw metadata token
// (the row number is the low 24 bits; the tests encode 1-based rows with a
// non-zero table-id byte so a row of 0 can never alias the int.MaxValue guard).
class EnumField : public IField {
public:
    EnumField(std::string name, ITypePtr returnType, std::any constantValue,
              std::uint32_t metadataToken, bool isConst = true)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          constantValue_(std::move(constantValue)), metadataToken_(metadataToken),
          isConst_(isConst) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override {
        return returnType_->GetDefinition()->Compilation();
    }
    std::uint32_t MetadataToken() const override { return metadataToken_; }
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
    bool IsStatic() const override { return true; }
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
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool = false) const override { return constantValue_; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    std::any constantValue_;
    std::uint32_t metadataToken_;
    bool isConst_;
};

// An enum definition stub: a LookupTypeDefinition with a configurable Fields()
// table (the enum members) and a configurable [Flags] attribute, over a
// configurable underlying primitive.
class EnumHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetFields(std::vector<const IField*> fields) { fields_ = std::move(fields); }
    void SetFlags(bool isFlags) { isFlags_ = isFlags; }

    std::vector<const IField*> Fields() const override { return fields_; }
    bool HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        return attribute == ::ILSpy::Decompiler::TypeSystem::KnownAttribute::Flags && isFlags_;
    }

private:
    std::vector<const IField*> fields_;
    bool isFlags_ = false;
};

// Retrieves the int32/uint32 alternative a PrimitiveExpression's Value holds
// (a test-side convenience over the PrimitiveValue variant).
std::int32_t HeldInt32(const PrimitiveExpression* expression) {
    return std::get<std::int32_t>(expression->Value());
}
double HeldDouble(const PrimitiveExpression* expression) {
    return std::get<double>(expression->Value());
}
std::int64_t HeldInt64(const PrimitiveExpression* expression) {
    return std::get<std::int64_t>(expression->Value());
}
std::uint32_t HeldUInt32(const PrimitiveExpression* expression) {
    return std::get<std::uint32_t>(expression->Value());
}

// A shared-managed known-type definition host (the registered-instance
// type-cache model: FindType must return the SAME instance the tests build
// their constants over).
std::shared_ptr<LookupTypeDefinition> MakeHost(const std::string& name,
                                                const std::string& ns,
                                                KnownTypeCode code,
                                                const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(name, ns,
        FullTypeName(TopLevelTypeName(ns, name, 0)), TypeKind::Struct,
        Accessibility::Public, compilation, nullptr, code);
}

// A shared-managed enum host.
std::shared_ptr<EnumHostType> MakeEnum(const std::string& name,
                                       const ICompilation& compilation,
                                       TS::ITypePtr underlying) {
    auto host = std::make_shared<EnumHostType>(name, "MyNs",
        FullTypeName(TopLevelTypeName("MyNs", name, 0)), TypeKind::Enum,
        Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
    host->SetEnumUnderlyingType(std::move(underlying));
    return host;
}

// Builds an enum member field over the enum host with a 1-based metadata row
// (the table-id byte 0x04 keeps the row non-zero).
EnumField MakeMember(const std::string& name, std::int32_t value, int row,
                     const std::shared_ptr<EnumHostType>& enumHost) {
    return EnumField(name, enumHost, std::any(value), 0x04000000u | static_cast<std::uint32_t>(row));
}

} // namespace

namespace {

// The ConvertConstantValue(ResolveResult) suite.
class ConvertConstantValueResolveResultTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<LookupTypeDefinition> int32_;
    std::shared_ptr<LookupTypeDefinition> object_;
    TypeSystemAstBuilder builder_;

    ConvertConstantValueResolveResultTest()
        : int32_(MakeHost("Int32", "System", KnownTypeCode::Int32, *compilation_)),
          object_(MakeHost("Object", "System", KnownTypeCode::Object, *compilation_)) {
        compilation_->RegisterKnownType(KnownTypeCode::Int32, int32_.get());
        compilation_->RegisterKnownType(KnownTypeCode::Object, object_.get());
    }
};

// A TypeOfResolveResult renders a TypeOfExpression over the referenced type.
TEST_F(ConvertConstantValueResolveResultTest, TypeOfResolveResultRendersTypeOfExpression) {
    auto rr = std::make_shared<TypeOfResolveResult>(object_, int32_);

    Expression* expression = builder_.ConvertConstantValue(rr);
    const auto* typeofExpression = dynamic_cast<const TypeOfExpression*>(expression);
    ASSERT_NE(nullptr, typeofExpression);
    // The Int32 definition renders as the `int` keyword (the builtin
    // short-circuit, UseKeywordsForBuiltinTypes default true).
    const auto* primitiveType = dynamic_cast<const PrimitiveType*>(typeofExpression->Type());
    ASSERT_NE(nullptr, primitiveType);
    EXPECT_EQ("int", primitiveType->Keyword());
}

// An ArrayCreateResolveResult with size arguments and no initializer renders
// the size arguments as the array-creation arguments, removing the array
// specifier the moved ComposedType carried (the size argument now owns it:
// `new int[5]`).
TEST_F(ConvertConstantValueResolveResultTest, ArrayCreateRendersSizeArguments) {
    auto arrayType = std::make_shared<ArrayType>(int32_);
    auto size = std::make_shared<ConstantResolveResult>(int32_, std::any(std::int32_t(5)));
    auto rr = std::make_shared<ArrayCreateResolveResult>(
        arrayType, std::vector<std::shared_ptr<ResolveResult>>{size}, std::nullopt);

    Expression* expression = builder_.ConvertConstantValue(rr);
    auto* ace = dynamic_cast<ArrayCreateExpression*>(expression);
    ASSERT_NE(nullptr, ace);
    EXPECT_EQ(1, ace->Arguments().Count());
    EXPECT_EQ(0, ace->AdditionalArraySpecifiers().Count());
    ASSERT_EQ(nullptr, ace->Initializer());
    const auto* argument = dynamic_cast<const PrimitiveExpression*>(ace->Arguments().At(0));
    ASSERT_NE(nullptr, argument);
    EXPECT_EQ(5, HeldInt32(argument));
    // The unwrapped element type: `new int[...]`.
    const auto* primitiveType = dynamic_cast<const PrimitiveType*>(ace->Type());
    ASSERT_NE(nullptr, primitiveType);
    EXPECT_EQ("int", primitiveType->Keyword());
}

// An ArrayCreateResolveResult with initializer elements renders the initializer
// (keeping the array specifier: `new int[] { 1, 2 }`).
TEST_F(ConvertConstantValueResolveResultTest, ArrayCreateRendersInitializerElements) {
    auto arrayType = std::make_shared<ArrayType>(int32_);
    auto first = std::make_shared<ConstantResolveResult>(int32_, std::any(std::int32_t(1)));
    auto second = std::make_shared<ConstantResolveResult>(int32_, std::any(std::int32_t(2)));
    auto rr = std::make_shared<ArrayCreateResolveResult>(
        arrayType, std::vector<std::shared_ptr<ResolveResult>>{},
        std::optional<std::vector<std::shared_ptr<ResolveResult>>>(
            std::vector<std::shared_ptr<ResolveResult>>{first, second}));

    Expression* expression = builder_.ConvertConstantValue(rr);
    auto* ace = dynamic_cast<ArrayCreateExpression*>(expression);
    ASSERT_NE(nullptr, ace);
    EXPECT_EQ(0, ace->Arguments().Count());
    EXPECT_EQ(1, ace->AdditionalArraySpecifiers().Count());
    ASSERT_NE(nullptr, ace->Initializer());
    EXPECT_EQ(2, ace->Initializer()->Elements().Count());
    const auto* element = dynamic_cast<const PrimitiveExpression*>(ace->Initializer()->Elements().At(0));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(1, HeldInt32(element));
}

// A plain compile-time constant renders its literal.
TEST_F(ConvertConstantValueResolveResultTest, ConstantResolveResultRendersLiteral) {
    auto rr = std::make_shared<ConstantResolveResult>(int32_, std::any(std::int32_t(5)));

    Expression* expression = builder_.ConvertConstantValue(rr);
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(5, HeldInt32(primitive));
}

// A boxing conversion of a small-integer constant wraps the int32 literal in a
// cast to the source type (C# has no small integer literal types), annotating
// the cast with the unwrapped input result.
TEST_F(ConvertConstantValueResolveResultTest, BoxingConversionOfSmallIntegerWrapsCast) {
    auto sbyte = std::make_shared<KnownType>(KnownTypeCode::SByte);
    auto input = std::make_shared<ConstantResolveResult>(sbyte, std::any(std::int8_t(5)));
    auto rr = std::make_shared<ConversionResolveResult>(
        object_, input, Sem::Conversions::BoxingConversion());
    builder_.AddResolveResultAnnotations() = true;

    Expression* expression = builder_.ConvertConstantValue(rr);
    const auto* cast = dynamic_cast<const CastExpression*>(expression);
    ASSERT_NE(nullptr, cast);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(5, HeldInt32(literal));
    // The annotation is the UNWRAPPED input (the ConstantResolveResult, whose
    // own constant value is the ORIGINAL sbyte box).
    const auto* annotation = cast->Annotation<ConstantResolveResult>();
    ASSERT_NE(nullptr, annotation);
    EXPECT_EQ(5, std::any_cast<std::int8_t>(annotation->ConstantValue()));
}

// A non-constant resolve result renders the ErrorExpression.
TEST_F(ConvertConstantValueResolveResultTest, NonConstantRendersErrorExpression) {
    auto rr = std::make_shared<ResolveResult>(int32_);

    Expression* expression = builder_.ConvertConstantValue(rr);
    EXPECT_NE(nullptr, dynamic_cast<const ErrorExpression*>(expression));
}

// A null rr throws the ArgumentNullException analog.
TEST_F(ConvertConstantValueResolveResultTest, NullResolveResultThrows) {
    EXPECT_THROW(builder_.ConvertConstantValue(nullptr), std::invalid_argument);
}

} // namespace

namespace {

// The ConvertConstantValue(expectedType, type, constantValue) suite.
class ConvertConstantValueThreeArgTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<LookupTypeDefinition> int32_;
    std::shared_ptr<LookupTypeDefinition> double_;
    std::shared_ptr<LookupTypeDefinition> nullableOfT_;
    TypeSystemAstBuilder builder_;

    ConvertConstantValueThreeArgTest()
        : int32_(MakeHost("Int32", "System", KnownTypeCode::Int32, *compilation_)),
          double_(MakeHost("Double", "System", KnownTypeCode::Double, *compilation_)),
          nullableOfT_(MakeHost("Nullable", "System", KnownTypeCode::NullableOfT, *compilation_)) {
        compilation_->RegisterKnownType(KnownTypeCode::Int32, int32_.get());
        compilation_->RegisterKnownType(KnownTypeCode::Double, double_.get());
        compilation_->RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT_.get());
    }
};

// A null constant over a reference type renders the null literal.
TEST_F(ConvertConstantValueThreeArgTest, NullOverReferenceTypeRendersNullReference) {
    auto string_ = std::make_shared<KnownType>(KnownTypeCode::String);
    builder_.AddResolveResultAnnotations() = true;

    Expression* expression = builder_.ConvertConstantValue(*string_, *string_, std::any());
    ASSERT_NE(nullptr, dynamic_cast<const NullReferenceExpression*>(expression));
    const auto* annotation = expression->Annotation<ConstantResolveResult>();
    ASSERT_NE(nullptr, annotation);
    EXPECT_EQ(TypeKind::Null, annotation->Type().Kind());
}

// A null constant over a value type renders the default-value expression over
// the converted type.
TEST_F(ConvertConstantValueThreeArgTest, NullOverValueTypeRendersDefaultValue) {
    Expression* expression = builder_.ConvertConstantValue(*int32_, *int32_, std::any());
    const auto* defaultValue = dynamic_cast<const DefaultValueExpression*>(expression);
    ASSERT_NE(nullptr, defaultValue);
    const auto* primitiveType = dynamic_cast<const PrimitiveType*>(defaultValue->Type());
    ASSERT_NE(nullptr, primitiveType);
    EXPECT_EQ("int", primitiveType->Keyword());
}

// A null constant over a nullable wrapper renders the null literal (the
// IsKnownType(NullableOfT) arm).
TEST_F(ConvertConstantValueThreeArgTest, NullOverNullableRendersNullReference) {
    auto nullable = std::make_shared<ParameterizedType>(nullableOfT_,
                                                        std::vector<ITypePtr>{int32_});

    Expression* expression = builder_.ConvertConstantValue(*nullable, *nullable, std::any());
    ASSERT_NE(nullptr, dynamic_cast<const NullReferenceExpression*>(expression));
}

// A null constant over a pointer type renders the null literal (the
// Kind.IsAnyPointer arm).
TEST_F(ConvertConstantValueThreeArgTest, NullOverPointerRendersNullReference) {
    auto pointer = std::make_shared<PointerType>(int32_);

    Expression* expression = builder_.ConvertConstantValue(*pointer, *pointer, std::any());
    ASSERT_NE(nullptr, dynamic_cast<const NullReferenceExpression*>(expression));
}

// An IType-boxed constant renders the TypeOfExpression (the
// typeof-constant-in-a-default-value shape).
TEST_F(ConvertConstantValueThreeArgTest, TypeOfConstantRendersTypeOfExpression) {
    Expression* expression =
        builder_.ConvertConstantValue(*int32_, *int32_, std::any(ITypePtr(int32_)));
    const auto* typeofExpression = dynamic_cast<const TypeOfExpression*>(expression);
    ASSERT_NE(nullptr, typeofExpression);
    const auto* primitiveType = dynamic_cast<const PrimitiveType*>(typeofExpression->Type());
    ASSERT_NE(nullptr, primitiveType);
    EXPECT_EQ("int", primitiveType->Keyword());
}

// A params-array CustomAttributeTypedArgument vector renders the array creation
// with the initializer elements.
TEST_F(ConvertConstantValueThreeArgTest, ParamsArrayRendersArrayCreateWithInitializer) {
    auto arrayType = std::make_shared<ArrayType>(int32_);
    std::vector<CustomAttributeTypedArgument> arguments{
        CustomAttributeTypedArgument(int32_, std::any(std::int32_t(1))),
        CustomAttributeTypedArgument(int32_, std::any(std::int32_t(2))),
    };

    Expression* expression =
        builder_.ConvertConstantValue(*arrayType, *arrayType, std::any(std::move(arguments)));
    auto* ace = dynamic_cast<ArrayCreateExpression*>(expression);
    ASSERT_NE(nullptr, ace);
    ASSERT_NE(nullptr, ace->Initializer());
    EXPECT_EQ(2, ace->Initializer()->Elements().Count());
    const auto* element = dynamic_cast<const PrimitiveExpression*>(ace->Initializer()->Elements().At(0));
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(1, HeldInt32(element));
}

// The array's element type threads into the per-element conversion as the
// EXPECTED type: a small-integer element whose own type differs from the
// element type gets the mismatch cast (the integerTypeMismatch arm).
TEST_F(ConvertConstantValueThreeArgTest, ParamsArrayElementTypeThreadsIntoSmallIntegerCast) {
    auto arrayType = std::make_shared<ArrayType>(int32_);
    auto sbyte = std::make_shared<KnownType>(KnownTypeCode::SByte);
    std::vector<CustomAttributeTypedArgument> arguments{
        CustomAttributeTypedArgument(sbyte, std::any(std::int8_t(5))),
    };

    Expression* expression =
        builder_.ConvertConstantValue(*arrayType, *arrayType, std::any(std::move(arguments)));
    auto* ace = dynamic_cast<ArrayCreateExpression*>(expression);
    ASSERT_NE(nullptr, ace);
    ASSERT_NE(nullptr, ace->Initializer());
    const auto* cast = dynamic_cast<const CastExpression*>(ace->Initializer()->Elements().At(0));
    ASSERT_NE(nullptr, cast);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(5, HeldInt32(literal));
}

// A small-integer constant re-boxes through int32 (C# has no small integer
// literals) -- the same-type pair adds no cast.
TEST_F(ConvertConstantValueThreeArgTest, SmallIntegerRemapsToInt32Literal) {
    auto sbyte = std::make_shared<KnownType>(KnownTypeCode::SByte);

    Expression* expression = builder_.ConvertConstantValue(*sbyte, *sbyte, std::any(std::int8_t(5)));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(5, HeldInt32(primitive));
    EXPECT_EQ(nullptr, dynamic_cast<const CastExpression*>(expression));
}

// A small-integer constant whose expected type differs from its own type gets
// the mismatch cast.
TEST_F(ConvertConstantValueThreeArgTest, SmallIntegerWithDifferentExpectedTypeWrapsCast) {
    auto sbyte = std::make_shared<KnownType>(KnownTypeCode::SByte);

    Expression* expression =
        builder_.ConvertConstantValue(*int32_, *sbyte, std::any(std::int8_t(5)));
    const auto* cast = dynamic_cast<const CastExpression*>(expression);
    ASSERT_NE(nullptr, cast);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(5, HeldInt32(literal));
}

// The PrintIntegralValuesAsHex flag renders the hexadecimal literal format (and
// skips the special-constant check for the primitive integer types).
TEST_F(ConvertConstantValueThreeArgTest, PrintIntegralValuesAsHexRendersHexFormat) {
    builder_.PrintIntegralValuesAsHex() = true;

    Expression* expression =
        builder_.ConvertConstantValue(*int32_, *int32_, std::any(std::int32_t(255)));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(ILSpy::Decompiler::CSharp::Syntax::LiteralFormat::HexadecimalNumber,
              primitive->Format());
    EXPECT_EQ(255, HeldInt32(primitive));
}

// A double constant routes into ConvertFloatingPointLiteral (the plain
// literal for a short-decimal form).
TEST_F(ConvertConstantValueThreeArgTest, DoubleConstantRendersFloatingPointLiteral) {
    Expression* expression =
        builder_.ConvertConstantValue(*double_, *double_, std::any(0.5));
    const auto* primitive = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, primitive);
    EXPECT_EQ(0.5, HeldDouble(primitive));
}

// A constant over an Unknown-kind type gets the cast wrap (the
// `underlyingType.Kind == TypeKind.Unknown` arm).
TEST_F(ConvertConstantValueThreeArgTest, UnknownKindWrapsCast) {
    SpecialType unknown(TypeKind::Unknown);

    Expression* expression = builder_.ConvertConstantValue(unknown, unknown, std::any(std::int32_t(5)));
    const auto* cast = dynamic_cast<const CastExpression*>(expression);
    ASSERT_NE(nullptr, cast);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(5, HeldInt32(literal));
}

// The annotation over a registered known-type instance carries the literal type
// and the re-boxed constant value.
TEST_F(ConvertConstantValueThreeArgTest, AnnotationCarriesLiteralTypeAndValue) {
    builder_.AddResolveResultAnnotations() = true;

    Expression* expression =
        builder_.ConvertConstantValue(*int32_, *int32_, std::any(std::int32_t(5)));
    const auto* annotation = expression->Annotation<ConstantResolveResult>();
    ASSERT_NE(nullptr, annotation);
    EXPECT_EQ(static_cast<const IType*>(int32_.get()), &annotation->Type());
    EXPECT_EQ(5, std::any_cast<std::int32_t>(annotation->ConstantValue()));
}

} // namespace

namespace {

// The ConvertEnumValue suite.
class ConvertEnumValueTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<LookupTypeDefinition> int32_;
    std::shared_ptr<LookupTypeDefinition> byte_;
    std::shared_ptr<EnumHostType> enum_;
    TypeSystemAstBuilder builder_;

    ConvertEnumValueTest()
        : int32_(MakeHost("Int32", "System", KnownTypeCode::Int32, *compilation_)),
          byte_(MakeHost("Byte", "System", KnownTypeCode::Byte, *compilation_)),
          enum_(MakeEnum("MyEnum", *compilation_, int32_)) {}

    // Wraps the member-stub vector into the non-owning Fields() table (the
    // members live in the caller's scope).
    void SetMembers(std::vector<EnumField*> members) {
        std::vector<const IField*> fields(members.begin(), members.end());
        enum_->SetFields(std::move(fields));
    }
};

// An exact member match renders the qualified member reference.
TEST_F(ConvertEnumValueTest, ExactMatchRendersQualifiedMemberReference) {
    EnumField memberA = MakeMember("A", 1, 1, enum_);
    EnumField memberB = MakeMember("B", 5, 2, enum_);
    SetMembers({&memberA, &memberB});

    Expression* expression = builder_.ConvertEnumValue(*enum_, 5);
    const auto* memberReference = dynamic_cast<const MemberReferenceExpression*>(expression);
    ASSERT_NE(nullptr, memberReference);
    EXPECT_EQ("B", memberReference->MemberName());
    const auto* typeReference = dynamic_cast<const TypeReferenceExpression*>(memberReference->Target());
    ASSERT_NE(nullptr, typeReference);
}

// No matching member renders the numeric cast `(EnumType)value` over the
// underlying primitive.
TEST_F(ConvertEnumValueTest, NoMatchRendersNumericCast) {
    EnumField memberA = MakeMember("A", 1, 1, enum_);
    SetMembers({&memberA});

    Expression* expression = builder_.ConvertEnumValue(*enum_, 7);
    const auto* cast = dynamic_cast<const CastExpression*>(expression);
    ASSERT_NE(nullptr, cast);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(7, HeldInt32(literal));
}

// A [Flags] combined value with no single member renders the bitwise-OR
// decomposition of its single-bit components.
TEST_F(ConvertEnumValueTest, FlagsCombinationRendersBitwiseOr) {
    EnumField memberA = MakeMember("A", 1, 1, enum_);
    EnumField memberB = MakeMember("B", 2, 2, enum_);
    SetMembers({&memberA, &memberB});
    enum_->SetFlags(true);

    Expression* expression = builder_.ConvertEnumValue(*enum_, 3);
    const auto* combination = dynamic_cast<const BinaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, combination);
    EXPECT_EQ(BinaryOperatorType::BitwiseOr, combination->Operator());
    const auto* left = dynamic_cast<const MemberReferenceExpression*>(combination->Left());
    ASSERT_NE(nullptr, left);
    EXPECT_EQ("A", left->MemberName());
    const auto* right = dynamic_cast<const MemberReferenceExpression*>(combination->Right());
    ASSERT_NE(nullptr, right);
    EXPECT_EQ("B", right->MemberName());
}

// A [Flags] exact multi-bit member renders directly (the declaringEnumMember ==
// null direct-match arm fires for any weight).
TEST_F(ConvertEnumValueTest, FlagsExactMultiBitMemberRendersDirectly) {
    EnumField memberA = MakeMember("A", 1, 1, enum_);
    EnumField memberAll = MakeMember("All", 3, 2, enum_);
    SetMembers({&memberA, &memberAll});
    enum_->SetFlags(true);

    Expression* expression = builder_.ConvertEnumValue(*enum_, 3);
    const auto* memberReference = dynamic_cast<const MemberReferenceExpression*>(expression);
    ASSERT_NE(nullptr, memberReference);
    EXPECT_EQ("All", memberReference->MemberName());
}

// The complement form ~X wins when it is strictly smaller than the forward
// decomposition (a byte-based enum masks the negation to the underlying range).
TEST_F(ConvertEnumValueTest, FlagsComplementRendersBitNot) {
    // A byte-based [Flags] enum { A=1, B=4, X=0xFA }: value 5 decomposes forward
    // as A|B (3 nodes) but the complement is the single member X (1 node).
    auto byteEnum = MakeEnum("ByteEnum", *compilation_, byte_);
    EnumField memberA = MakeMember("A", 1, 1, byteEnum);
    EnumField memberB = MakeMember("B", 4, 2, byteEnum);
    EnumField memberX = MakeMember("X", 0xFA, 3, byteEnum);
    std::vector<const IField*> fields{&memberA, &memberB, &memberX};
    byteEnum->SetFields(std::move(fields));
    byteEnum->SetFlags(true);

    Expression* expression = builder_.ConvertEnumValue(*byteEnum, 5);
    const auto* bitNot = dynamic_cast<const UnaryOperatorExpression*>(expression);
    ASSERT_NE(nullptr, bitNot);
    EXPECT_EQ(UnaryOperatorType::BitNot, bitNot->Operator());
    const auto* memberReference = dynamic_cast<const MemberReferenceExpression*>(bitNot->Expression());
    ASSERT_NE(nullptr, memberReference);
    EXPECT_EQ("X", memberReference->MemberName());
}

// Inside an enum member initializer (declaringEnumMember set) an earlier member
// renders as the unqualified identifier; the annotations carry a
// MemberResolveResult with a null target.
TEST_F(ConvertEnumValueTest, DeclaringMemberRendersUnqualifiedIdentifier) {
    EnumField memberA = MakeMember("A", 1, 1, enum_);
    EnumField memberC = MakeMember("C", 0, 3, enum_);
    SetMembers({&memberA, &memberC});
    builder_.AddResolveResultAnnotations() = true;

    Expression* expression = builder_.ConvertEnumValue(*enum_, 1, &memberC);
    const auto* identifier = dynamic_cast<const IdentifierExpression*>(expression);
    ASSERT_NE(nullptr, identifier);
    EXPECT_EQ("A", identifier->Identifier());
    const auto* annotation = expression->Annotation<MemberResolveResult>();
    ASSERT_NE(nullptr, annotation);
    EXPECT_EQ(static_cast<const IMember*>(&memberA), annotation->Member());
}

// Inside an enum member initializer a LATER member (a higher metadata row)
// stays numeric -- a member initializer cannot forward-reference a later member.
TEST_F(ConvertEnumValueTest, DeclaringMemberSkipsLaterMembersNumerically) {
    EnumField memberA = MakeMember("A", 1, 1, enum_);
    EnumField memberB = MakeMember("B", 2, 2, enum_);
    SetMembers({&memberA, &memberB});

    // memberA (row 1) is the declaring member; memberB (row 2) is later.
    Expression* expression = builder_.ConvertEnumValue(*enum_, 2, &memberA);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(2, HeldInt32(literal));
    EXPECT_EQ(nullptr, dynamic_cast<const CastExpression*>(expression));
}

// A multi-bit value inside a larger, previously declared member is a field
// encoding inside that mask, not a union of independent flags: inside a member
// initializer it stays numeric (the isEncodedInEarlierMask guard).
TEST_F(ConvertEnumValueTest, DeclaringMemberEncodedInEarlierMaskStaysNumeric) {
    // { Mask=7 (row 1), A=1 (row 2), B=2 (row 3) } with a row-4 declaring
    // member: value 3 is covered by A|B but lies entirely within Mask.
    EnumField memberMask = MakeMember("Mask", 7, 1, enum_);
    EnumField memberA = MakeMember("A", 1, 2, enum_);
    EnumField memberB = MakeMember("B", 2, 3, enum_);
    EnumField memberSelf = MakeMember("Self", 0, 4, enum_);
    SetMembers({&memberMask, &memberA, &memberB, &memberSelf});
    enum_->SetFlags(true);

    Expression* expression = builder_.ConvertEnumValue(*enum_, 3, &memberSelf);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(3, HeldInt32(literal));
}

// The byte-based complement form does not compile inside a member initializer
// (the complementCompiles gate) -- the value stays numeric.
TEST_F(ConvertEnumValueTest, ByteBasedComplementDoesNotCompileInsideInitializer) {
    // A byte-based [Flags] enum { X=0xFA } inside a row-2 declaring member:
    // value 5 would render ~X outside an initializer but stays numeric inside.
    auto byteEnum = MakeEnum("ByteEnum2", *compilation_, byte_);
    EnumField memberX = MakeMember("X", 0xFA, 1, byteEnum);
    EnumField memberSelf = MakeMember("Self", 0, 2, byteEnum);
    std::vector<const IField*> fields{&memberX, &memberSelf};
    byteEnum->SetFields(std::move(fields));
    byteEnum->SetFlags(true);

    Expression* expression = builder_.ConvertEnumValue(*byteEnum, 5, &memberSelf);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(expression);
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(5, HeldUInt32(literal));
}

// The enum routing of the 3-arg ConvertConstantValue: an enum-typed constant
// converts through ConvertEnumValue (the member reference).
TEST_F(ConvertEnumValueTest, EnumTypedConstantRoutesThroughConvertEnumValue) {
    EnumField memberB = MakeMember("B", 5, 1, enum_);
    SetMembers({&memberB});

    Expression* expression = builder_.ConvertConstantValue(*enum_, *enum_, std::any(std::int32_t(5)));
    const auto* memberReference = dynamic_cast<const MemberReferenceExpression*>(expression);
    ASSERT_NE(nullptr, memberReference);
    EXPECT_EQ("B", memberReference->MemberName());
}

// A definitionless enum-kind type gets the documented safe fallback: the plain
// numeric cast over the raw value (the raw val is boxed as int64).
TEST_F(ConvertEnumValueTest, DefinitionlessEnumRendersRawNumericCast) {
    SpecialType definitionless(TypeKind::Enum);

    Expression* expression = builder_.ConvertEnumValue(definitionless, 5);
    const auto* cast = dynamic_cast<const CastExpression*>(expression);
    ASSERT_NE(nullptr, cast);
    const auto* literal = dynamic_cast<const PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(nullptr, literal);
    EXPECT_EQ(5, HeldInt64(literal));
}

} // namespace
