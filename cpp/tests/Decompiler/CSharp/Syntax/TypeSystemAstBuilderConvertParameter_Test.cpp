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
// PURPOSE AND NONINFRINGEMENT. IN NO AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the TypeSystemAstBuilder "Convert Parameter" region
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 1786-1826: ConvertParameter).
//
// The load-bearing cruxes:
//  (a) the scalar surface: the reference kind into `ParameterModifier`, the
//      `IsParams` flag, and the `Lifetime().ScopedRef()` C# 11 annotation into
//      `IsScopedRef`;
//  (b) the `ShowAttributes`-gated attribute sections (hidden by default,
//      rendered when the flag is on);
//  (c) the "avoid 'out ref'" unwrap: a by-reference parameter type renders as
//      its ELEMENT type (the ref-ness is already carried by the modifier);
//  (d) the `ShowParameterNames`-gated name;
//  (e) the `IsDefaultValueAssignmentAllowed` + `ShowConstantValues`-gated
//      default expression through the 2-arg `ConvertConstantValue` (an
//      optional parameter's `= 5` renders a PrimitiveExpression; a later
//      required parameter blocks the default; the flag off omits it), and the
//      catch arm rendering an `ErrorExpression` over the exception message
//      when `GetConstantValue(throwOnInvalidMetadata: true)` throws.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::AstType;
using ILSpy::Decompiler::CSharp::Syntax::ParameterDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                              const std::string& ns,
                                              TypeKind kind,
                                              KnownTypeCode code,
                                              const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, 0)), kind,
        Accessibility::Public, compilation, nullptr, code);
}

// A fully configurable `IParameter` stub (the fields ConvertParameter reads:
// type, name, reference kind, IsParams / IsOptional / constant-in-signature,
// lifetime, owner, constant value, attributes).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

    void SetOwner(const IParameterizedMember* owner) { owner_ = owner; }
    void SetReferenceKind(TS::ReferenceKind rk) { referenceKind_ = rk; }
    void SetIsParams(bool v) { isParams_ = v; }
    void SetIsOptional(bool v) { isOptional_ = v; }
    void SetHasConstantValueInSignature(bool v) { hasConstantValueInSignature_ = v; }
    void SetLifetimeScopedRef(bool v) { lifetime_.ScopedRef(v); }
    void SetConstantValue(std::any v) { constantValue_ = std::move(v); }
    void SetAttributes(std::vector<const IAttribute*> attributes) {
        attributes_ = std::move(attributes);
    }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return constantValue_; }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    TS::ReferenceKind ReferenceKind() const override { return referenceKind_; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return hasConstantValueInSignature_; }
    const IParameterizedMember* Owner() const override { return owner_; }
    LifetimeAnnotation Lifetime() const override { return lifetime_; }

private:
    std::string name_;
    ITypePtr type_;
    const IParameterizedMember* owner_ = nullptr;
    TS::ReferenceKind referenceKind_ = TS::ReferenceKind::None;
    bool isParams_ = false;
    bool isOptional_ = false;
    bool hasConstantValueInSignature_ = false;
    LifetimeAnnotation lifetime_;
    std::any constantValue_;
    std::vector<const IAttribute*> attributes_;
};

// A parameter whose `GetConstantValue(throwOnInvalidMetadata: true)` throws
// (the metadata-decoder failure shape the C# `catch (BadImageFormatException)`
// arm guards; the port-side throw is the closest message-carrying exception).
class ThrowingParameter : public TestParameter {
public:
    using TestParameter::TestParameter;

    std::any GetConstantValue(bool throwOnInvalidMetadata) const override {
        if (throwOnInvalidMetadata)
            throw std::runtime_error("Bad binary format in custom attribute table");
        return std::any{};
    }
};

// A minimal `IAttribute` stub (the ConvertAttribute TestAttribute pattern):
// a type, no fixed / named arguments, no decode errors.
class PlainAttribute : public IAttribute {
public:
    explicit PlainAttribute(ITypePtr attributeType) : attributeType_(std::move(attributeType)) {}

    const IType& AttributeType() const override { return *attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override {
        return nullptr;
    }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument>
    FixedArguments() const override {
        return {};
    }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument>
    NamedArguments() const override {
        return {};
    }

private:
    ITypePtr attributeType_;
};

// The fixture: a compilation with the Int32 definition registered (the
// type-cache model -- the FindType target and the parameter's type are the
// SAME instance) and a resolver-less builder (the default configuration).
struct ConvertParameterFixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> attributeType;
    TypeSystemAstBuilder builder;

    ConvertParameterFixture()
        : int32(MakeDef("Int32", "System", TypeKind::Struct, KnownTypeCode::Int32,
                        compilation)),
          attributeType(MakeDef("MarkerAttribute", "N", TypeKind::Class,
                                KnownTypeCode::None, compilation)) {
        compilation.RegisterKnownType(KnownTypeCode::Int32, int32.get());
    }

    std::shared_ptr<TestParameter> MakeIntParameter(const std::string& name = "p") const {
        return std::make_shared<TestParameter>(int32, name);
    }
};

} // namespace

// The basic shape: a by-value int parameter with a name renders the `int`
// keyword PrimitiveType and the name, with all flags off and no slots set.
TEST(ConvertParameterTest, BasicParameterRendersTypeAndName)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter("value");

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->ParameterModifier(), ReferenceKind::None);
    EXPECT_FALSE(decl->IsParams());
    EXPECT_FALSE(decl->IsScopedRef());
    ASSERT_NE(decl->Type(), nullptr);
    auto* primitiveType = dynamic_cast<PrimitiveType*>(decl->Type());
    ASSERT_NE(primitiveType, nullptr);
    EXPECT_EQ(primitiveType->Keyword(), "int");
    ASSERT_TRUE(decl->Name().has_value());
    EXPECT_EQ(*decl->Name(), "value");
    EXPECT_EQ(decl->DefaultExpression(), nullptr);
}

// The reference kinds are carried into `ParameterModifier`.
TEST(ConvertParameterTest, ReferenceKindIsCarriedIntoParameterModifier)
{
    ConvertParameterFixture fx;
    TestParameter inParam(fx.int32);
    inParam.SetReferenceKind(ReferenceKind::In);
    std::unique_ptr<ParameterDeclaration> inDecl(fx.builder.ConvertParameter(inParam));
    EXPECT_EQ(inDecl->ParameterModifier(), ReferenceKind::In);

    TestParameter outParam(fx.int32);
    outParam.SetReferenceKind(ReferenceKind::Out);
    std::unique_ptr<ParameterDeclaration> outDecl(fx.builder.ConvertParameter(outParam));
    EXPECT_EQ(outDecl->ParameterModifier(), ReferenceKind::Out);

    TestParameter refParam(fx.int32);
    refParam.SetReferenceKind(ReferenceKind::Ref);
    std::unique_ptr<ParameterDeclaration> refDecl(fx.builder.ConvertParameter(refParam));
    EXPECT_EQ(refDecl->ParameterModifier(), ReferenceKind::Ref);
}

// The `params` flag is carried.
TEST(ConvertParameterTest, IsParamsFlagIsCarried)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    parameter->SetIsParams(true);

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));
    EXPECT_TRUE(decl->IsParams());
}

// The C# 11 `scoped ref` annotation is carried into `IsScopedRef`.
TEST(ConvertParameterTest, ScopedRefLifetimeIsCarried)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    parameter->SetLifetimeScopedRef(true);

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));
    EXPECT_TRUE(decl->IsScopedRef());
}

// `ShowAttributes` is false by default: the parameter's attributes are not
// rendered.
TEST(ConvertParameterTest, AttributesHiddenByDefault)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    PlainAttribute attribute(fx.attributeType);
    parameter->SetAttributes({&attribute});

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));
    EXPECT_EQ(decl->Attributes().Count(), 0);
}

// With `ShowAttributes` on, each attribute is wrapped into an
// `AttributeSection` on the declaration.
TEST(ConvertParameterTest, AttributesRenderedWhenShown)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    PlainAttribute attribute(fx.attributeType);
    parameter->SetAttributes({&attribute});

    fx.builder.ShowAttributes() = true;
    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));

    ASSERT_EQ(decl->Attributes().Count(), 1);
    ASSERT_NE(decl->Attributes().At(0), nullptr);
    EXPECT_EQ(decl->Attributes().At(0)->Attributes().Count(), 1);
}

// The "avoid 'out ref'" unwrap: a by-reference parameter type renders as its
// ELEMENT type (the ref-ness is already carried by the modifier).
TEST(ConvertParameterTest, ByReferenceTypeUnwrapsToElement)
{
    ConvertParameterFixture fx;
    TestParameter refParam(
        std::make_shared<ByReferenceType>(fx.int32), "x");
    refParam.SetReferenceKind(ReferenceKind::In);

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(refParam));

    ASSERT_NE(decl->Type(), nullptr);
    // The rendered type is the element's `int` keyword PrimitiveType, not a
    // by-reference/composed shape over it.
    auto* primitiveType = dynamic_cast<PrimitiveType*>(decl->Type());
    ASSERT_NE(primitiveType, nullptr);
    EXPECT_EQ(primitiveType->Keyword(), "int");
}

// `ShowParameterNames` is on by default; turning it off omits the name.
TEST(ConvertParameterTest, ShowParameterNamesFalseOmitsName)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter("value");

    fx.builder.ShowParameterNames() = false;
    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));

    EXPECT_FALSE(decl->Name().has_value());
    EXPECT_EQ(decl->NameToken(), nullptr);
}

// An optional parameter with a constant value renders `= 5` (a
// PrimitiveExpression default over the unwrapped parameter type).
TEST(ConvertParameterTest, OptionalParameterDefaultRendered)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    parameter->SetIsOptional(true);
    parameter->SetHasConstantValueInSignature(true);
    parameter->SetConstantValue(std::any(std::int32_t(5)));

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));

    ASSERT_NE(decl->DefaultExpression(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveExpression*>(decl->DefaultExpression());
    ASSERT_NE(primitive, nullptr);
    ASSERT_TRUE(std::holds_alternative<std::int32_t>(primitive->Value()));
    EXPECT_EQ(std::get<std::int32_t>(primitive->Value()), 5);
}

// `ShowConstantValues` off omits the default expression.
TEST(ConvertParameterTest, ShowConstantValuesFalseOmitsDefault)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    parameter->SetIsOptional(true);
    parameter->SetHasConstantValueInSignature(true);
    parameter->SetConstantValue(std::any(std::int32_t(5)));

    fx.builder.ShowConstantValues() = false;
    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));

    EXPECT_EQ(decl->DefaultExpression(), nullptr);
}

// A non-optional parameter (or one whose constant is not in the signature)
// does not get a default expression even with the flag on -- the
// `IsDefaultValueAssignmentAllowed` gate.
TEST(ConvertParameterTest, NonOptionalParameterOmitsDefault)
{
    ConvertParameterFixture fx;
    auto parameter = fx.MakeIntParameter();
    parameter->SetIsOptional(false);
    parameter->SetHasConstantValueInSignature(true);
    parameter->SetConstantValue(std::any(std::int32_t(5)));

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*parameter));
    EXPECT_EQ(decl->DefaultExpression(), nullptr);

    auto signatureLess = fx.MakeIntParameter();
    signatureLess->SetIsOptional(true);
    signatureLess->SetHasConstantValueInSignature(false);
    signatureLess->SetConstantValue(std::any(std::int32_t(5)));

    std::unique_ptr<ParameterDeclaration> decl2(
        fx.builder.ConvertParameter(*signatureLess));
    EXPECT_EQ(decl2->DefaultExpression(), nullptr);
}

// A later required parameter blocks the default (the
// `IsDefaultValueAssignmentAllowed` subsequent-parameter walk).
TEST(ConvertParameterTest, LaterRequiredParameterBlocksTheDefault)
{
    ConvertParameterFixture fx;
    auto owner = std::make_shared<LookupMethod>("M", fx.compilation);
    auto target = fx.MakeIntParameter("a");
    target->SetIsOptional(true);
    target->SetHasConstantValueInSignature(true);
    target->SetConstantValue(std::any(std::int32_t(5)));
    target->SetOwner(owner.get());
    auto laterRequired = fx.MakeIntParameter("b");
    laterRequired->SetOwner(owner.get());
    owner->SetParameters({target.get(), laterRequired.get()});

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(*target));

    EXPECT_EQ(decl->DefaultExpression(), nullptr);
}

// A throwing `GetConstantValue(throwOnInvalidMetadata: true)` renders the
// catch arm: an `ErrorExpression` carrying the exception message (the text is
// the trailing multi-line `Comment`, the ErrorExpression ctor convention).
TEST(ConvertParameterTest, ThrowingGetConstantValueYieldsErrorExpression)
{
    ConvertParameterFixture fx;
    ThrowingParameter parameter(fx.int32);
    parameter.SetIsOptional(true);
    parameter.SetHasConstantValueInSignature(true);

    std::unique_ptr<ParameterDeclaration> decl(fx.builder.ConvertParameter(parameter));

    ASSERT_NE(decl->DefaultExpression(), nullptr);
    auto* error = dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::ErrorExpression*>(
        decl->DefaultExpression());
    ASSERT_NE(error, nullptr);
    const auto trailing = error->TrailingTrivia();
    ASSERT_EQ(trailing.size(), 1u);
    auto* comment = dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::Comment*>(trailing[0]);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->Content(), "Bad binary format in custom attribute table");
}
