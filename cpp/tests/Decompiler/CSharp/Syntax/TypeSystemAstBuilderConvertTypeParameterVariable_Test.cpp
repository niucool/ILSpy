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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the TypeSystemAstBuilder "Convert Type Parameter" + "Convert Variable"
// regions (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port
// of TypeSystemAstBuilder.cs lines 2601-2741 + 2743-2761: ConvertTypeParameter /
// ConvertTypeParameterConstraint / ConvertVariable).
//
// The load-bearing cruxes:
//  (a) ConvertTypeParameter: the variance scalar and name are carried, and the
//      `ShowAttributes` gate hides the attribute sections by default;
//  (b) ConvertTypeParameterConstraint: the no-constraint early out (every flag
//      false and every direct base type an object/valuetype yields nullptr),
//      the `class` / `class?` (the MakeNullableType wrap) / `struct` /
//      `unmanaged` / `notnull` keyword arms, the TypeConstraints loop (a
//      non-object base type renders through ConvertType; an object base type
//      is skipped UNLESS it carries attributes, which wrap the rendered type
//      in a ComposedType), the `new()` arm and its struct-constraint skip, and
//      the `allows ref struct` arm;
//  (c) ConvertVariable: the `IsConst` flag into `Modifiers.Const`, the
//      const-gated initializer through the 2-arg ConvertConstantValue, and
//      the catch arm rendering an ErrorExpression when
//      `GetConstantValue(throwOnInvalidMetadata: true)` throws.
//
// The `IsObjectOrValueType` filter the constraint clause consumes is NOT
// re-tested here: it was already ported as a namespace-scope free function
// (the gnhf-112 D460 landing, tested in GetDefinition_Test.cpp's
// IsObjectOrValueTypeTest suite), and this region simply calls it.
//
// The faithful stub shape for the TypeConstraints loop: a REAL ITypeParameter's
// `DirectBaseTypes` is `TypeConstraints.Select(t => t.Type)` (the C#
// AbstractTypeParameter.cs line 245 derivation), so the tests wire the SAME
// types into both `SetDirectBaseTypes` and `SetTypeConstraints` (modeling the
// real derivation; the early-out's `DirectBaseTypes.All(IsObjectOrValueType)`
// then agrees with the constraint list exactly as it does for a real type
// parameter).

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::AstType;
using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::Constraint;
using ILSpy::Decompiler::CSharp::Syntax::ErrorExpression;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::TypeParameterDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::CSharp::Syntax::VariableDeclarationStatement;
using ILSpy::Decompiler::CSharp::Syntax::VariableInitializer;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

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

// A minimal `IAttribute` stub (the ConvertParameter TestParameter-file
// PlainAttribute pattern): a type, no fixed / named arguments, no decode
// errors.
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

// A fully configurable `IVariable` stub (the fields ConvertVariable reads:
// name, type, the const flag, and the boxed constant value).
class TestVariable : public IVariable {
public:
    TestVariable(std::string name, ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    void SetIsConst(bool v) { isConst_ = v; }
    void SetConstantValue(std::any v) { constantValue_ = std::move(v); }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool throwOnInvalidMetadata = false) const override {
        (void)throwOnInvalidMetadata;
        return constantValue_;
    }

private:
    std::string name_;
    ITypePtr type_;
    bool isConst_ = false;
    std::any constantValue_;
};

// A variable whose `GetConstantValue(throwOnInvalidMetadata: true)` throws
// (the metadata-decoder failure shape the C# `catch (BadImageFormatException)`
// arm guards; the port-side throw is the closest message-carrying exception).
class ThrowingVariable : public TestVariable {
public:
    using TestVariable::TestVariable;

    std::any GetConstantValue(bool throwOnInvalidMetadata) const override {
        if (throwOnInvalidMetadata)
            throw std::runtime_error("Bad binary format in custom attribute table");
        return std::any{};
    }
};

// The fixture: a compilation with the Int32 / Object / ValueType definitions
// registered (the type-cache model -- the FindType targets and the types the
// tests build operands over are the SAME instances), a non-builtin interface
// definition for the TypeConstraints loop, an attribute type, and a
// resolver-less builder (the default configuration).
struct ConvertTypeParameterFixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> valueTypeDef;
    std::shared_ptr<LookupTypeDefinition> ifaceDef;
    std::shared_ptr<LookupTypeDefinition> attributeType;
    TypeSystemAstBuilder builder;

    ConvertTypeParameterFixture()
        : int32(MakeDef("Int32", "System", TypeKind::Struct, KnownTypeCode::Int32,
                        compilation)),
          objectDef(MakeDef("Object", "System", TypeKind::Class, KnownTypeCode::Object,
                            compilation)),
          valueTypeDef(MakeDef("ValueType", "System", TypeKind::Class,
                               KnownTypeCode::ValueType, compilation)),
          ifaceDef(MakeDef("IFoo", "N", TypeKind::Interface, KnownTypeCode::None,
                           compilation)),
          attributeType(MakeDef("MarkerAttribute", "N", TypeKind::Class,
                                KnownTypeCode::None, compilation)) {
        compilation.RegisterKnownType(KnownTypeCode::Int32, int32.get());
        compilation.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
        compilation.RegisterKnownType(KnownTypeCode::ValueType, valueTypeDef.get());
    }

    std::shared_ptr<LookupTypeParameter> MakeTypeParameter(const std::string& name = "T") const {
        return std::make_shared<LookupTypeParameter>(name);
    }
};

} // namespace

// ---- ConvertTypeParameter ------------------------------------------------------

// The basic shape: an invariant type parameter renders its name with no
// attributes (the ShowAttributes default is false).
TEST(ConvertTypeParameterTest, BasicTypeParameterRendersName)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter("T");

    std::unique_ptr<TypeParameterDeclaration> decl(fx.builder.ConvertTypeParameter(*tp));

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Variance(), VarianceModifier::Invariant);
    EXPECT_EQ(decl->Name(), "T");
    EXPECT_EQ(decl->Attributes().Count(), 0);
}

// The variance modifier is carried into the declaration scalar.
TEST(ConvertTypeParameterTest, VarianceIsCarried)
{
    ConvertTypeParameterFixture fx;
    LookupTypeParameter covariant("Out", VarianceModifier::Covariant);
    LookupTypeParameter contravariant("In", VarianceModifier::Contravariant);

    std::unique_ptr<TypeParameterDeclaration> outDecl(
        fx.builder.ConvertTypeParameter(covariant));
    EXPECT_EQ(outDecl->Variance(), VarianceModifier::Covariant);

    std::unique_ptr<TypeParameterDeclaration> inDecl(
        fx.builder.ConvertTypeParameter(contravariant));
    EXPECT_EQ(inDecl->Variance(), VarianceModifier::Contravariant);
}

// The ShowAttributes gate: the flag off (the default) omits the attribute
// sections even when the type parameter carries attributes.
TEST(ConvertTypeParameterTest, ShowAttributesOffOmitsAttributeSections)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    auto attribute = std::make_shared<PlainAttribute>(fx.attributeType);
    tp->SetAttributes({attribute.get()});

    std::unique_ptr<TypeParameterDeclaration> decl(fx.builder.ConvertTypeParameter(*tp));

    EXPECT_EQ(decl->Attributes().Count(), 0);
}

// The ShowAttributes gate: the flag on renders the attribute sections over
// GetAttributes.
TEST(ConvertTypeParameterTest, ShowAttributesOnRendersAttributeSections)
{
    ConvertTypeParameterFixture fx;
    fx.builder.ShowAttributes() = true;
    auto tp = fx.MakeTypeParameter();
    auto attribute = std::make_shared<PlainAttribute>(fx.attributeType);
    tp->SetAttributes({attribute.get()});

    std::unique_ptr<TypeParameterDeclaration> decl(fx.builder.ConvertTypeParameter(*tp));

    ASSERT_EQ(decl->Attributes().Count(), 1);
}

// ---- ConvertTypeParameterConstraint ---------------------------------------------

// The no-constraint early out: every flag false, the nullability not
// NotNullable, and no direct base types (the all-of-empty quantifier) yields
// nullptr (the C# null Constraint).
TEST(ConvertTypeParameterConstraintTest, NoConstraintYieldsNull)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();

    EXPECT_EQ(fx.builder.ConvertTypeParameterConstraint(*tp), nullptr);
}

// A direct-base-type list of purely object/valuetype definitions also takes
// the early out (the All(IsObjectOrValueType) quantifier).
TEST(ConvertTypeParameterConstraintTest, AllObjectBaseTypesYieldNull)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetDirectBaseTypes({fx.objectDef, fx.valueTypeDef});

    EXPECT_EQ(fx.builder.ConvertTypeParameterConstraint(*tp), nullptr);
}

// The `where T : class` arm renders the `class` keyword PrimitiveType.
TEST(ConvertTypeParameterConstraintTest, ClassConstraintRendersClassKeyword)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasReferenceTypeConstraint(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "class");
}

// The `where T : class?` arm renders the `class` keyword wrapped in a trailing
// `?` (the MakeNullableType wrap: a ComposedType with HasNullableSpecifier
// over the PrimitiveType).
TEST(ConvertTypeParameterConstraintTest, NullableClassConstraintRendersClassQuestionMark)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasReferenceTypeConstraint(true);
    tp->SetNullabilityConstraint(Nullability::Nullable);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* composed = dynamic_cast<ComposedType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
    auto* keyword = dynamic_cast<PrimitiveType*>(composed->BaseType());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "class");
}

// The `where T : struct` arm renders the `struct` keyword.
TEST(ConvertTypeParameterConstraintTest, StructConstraintRendersStructKeyword)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasValueTypeConstraint(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "struct");
}

// The `where T : unmanaged` arm renders the `unmanaged` keyword (the
// value-type constraint plus the unmanaged flag).
TEST(ConvertTypeParameterConstraintTest, UnmanagedConstraintRendersUnmanagedKeyword)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasValueTypeConstraint(true);
    tp->SetHasUnmanagedConstraint(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "unmanaged");
}

// The `where T : notnull` arm (a NotNullable nullability without a class or
// value-type constraint) renders the `notnull` keyword.
TEST(ConvertTypeParameterConstraintTest, NotNullableRendersNotNullKeyword)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetNullabilityConstraint(Nullability::NotNullable);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "notnull");
}

// The clause's TypeParameter is the SimpleType naming the type parameter.
TEST(ConvertTypeParameterConstraintTest, TypeParameterIsTheSimpleName)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter("TItem");
    tp->SetHasReferenceTypeConstraint(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    auto* typeParameter = constraint->TypeParameter();
    ASSERT_NE(typeParameter, nullptr);
    ASSERT_TRUE(typeParameter->Identifier().has_value());
    EXPECT_EQ(*typeParameter->Identifier(), "TItem");
}

// The TypeConstraints loop: a non-object base type renders through ConvertType
// (a namespaced interface definition renders as the qualified MemberType).
TEST(ConvertTypeParameterConstraintTest, TypeConstraintRendersThroughConvertType)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    // The faithful shape: a real ITypeParameter's DirectBaseTypes IS the
    // TypeConstraints' types (AbstractTypeParameter.cs line 245), so both are
    // wired with the same interface instance.
    tp->SetDirectBaseTypes({fx.ifaceDef});
    tp->SetTypeConstraints({TypeConstraint(fx.ifaceDef)});

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* memberType = dynamic_cast<MemberType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "IFoo");
}

// The TypeConstraints loop: an object-typed constraint is SKIPPED (it carries
// no information) -- with no other constraint or flag the clause renders no
// base types at all.
TEST(ConvertTypeParameterConstraintTest, ObjectTypeConstraintIsSkipped)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    // The new() flag keeps the early out from firing so the loop is reached;
    // the object-typed constraint itself is skipped by the
    // IsObjectOrValueType filter.
    tp->SetHasDefaultConstructorConstraint(true);
    tp->SetTypeConstraints({TypeConstraint(fx.objectDef)});

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    // Only the `new` keyword -- the object constraint contributed nothing.
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "new");
}

// The TypeConstraints loop: a constraint carrying attributes wraps the
// rendered type in a ComposedType holding the attribute section (the C# 8.5
// `[Attr] Base` form).
TEST(ConvertTypeParameterConstraintTest, ConstraintWithAttributesWrapsInComposedType)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    auto attribute = std::make_shared<PlainAttribute>(fx.attributeType);
    tp->SetDirectBaseTypes({fx.ifaceDef});
    tp->SetTypeConstraints({TypeConstraint(fx.ifaceDef, {attribute.get()})});

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* composed = dynamic_cast<ComposedType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(composed, nullptr);
    ASSERT_EQ(composed->Attributes().Count(), 1);
    auto* inner = dynamic_cast<MemberType*>(composed->BaseType());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->MemberName(), "IFoo");
}

// The TypeConstraints loop: an OBJECT-typed constraint carrying attributes
// still renders (the `|| t.Attributes.Count > 0` disjunct) -- the attributes
// make the otherwise-informationless constraint meaningful.
TEST(ConvertTypeParameterConstraintTest, ObjectConstraintWithAttributesStillRenders)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    auto attribute = std::make_shared<PlainAttribute>(fx.attributeType);
    tp->SetHasDefaultConstructorConstraint(true);
    tp->SetTypeConstraints({TypeConstraint(fx.objectDef, {attribute.get()})});

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    // The wrapped object constraint plus the `new` keyword.
    ASSERT_EQ(constraint->BaseTypes().Count(), 2);
    auto* composed = dynamic_cast<ComposedType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(composed, nullptr);
    ASSERT_EQ(composed->Attributes().Count(), 1);
}

// The `where T : new()` arm renders the `new` keyword.
TEST(ConvertTypeParameterConstraintTest, NewConstraintRendersNewKeyword)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasDefaultConstructorConstraint(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "new");
}

// The `new()` arm is SKIPPED when a value-type constraint already implies it
// (`where T : struct` guarantees the default constructor).
TEST(ConvertTypeParameterConstraintTest, NewConstraintIsSkippedForValueTypeConstraint)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasValueTypeConstraint(true);
    tp->SetHasDefaultConstructorConstraint(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "struct");
}

// The C# 11 `allows ref struct` arm renders the keyword phrase.
TEST(ConvertTypeParameterConstraintTest, AllowsRefLikeTypeRendersAllowsRefStruct)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetAllowsRefLikeType(true);

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "allows ref struct");
}

// The arms compose: `where T : class, Base, new()` renders the class keyword,
// the base type, and the new keyword in order.
TEST(ConvertTypeParameterConstraintTest, ArmsComposeInOrder)
{
    ConvertTypeParameterFixture fx;
    auto tp = fx.MakeTypeParameter();
    tp->SetHasReferenceTypeConstraint(true);
    tp->SetHasDefaultConstructorConstraint(true);
    tp->SetDirectBaseTypes({fx.ifaceDef});
    tp->SetTypeConstraints({TypeConstraint(fx.ifaceDef)});

    std::unique_ptr<Constraint> constraint(fx.builder.ConvertTypeParameterConstraint(*tp));

    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 3);
    auto* classKeyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(classKeyword, nullptr);
    EXPECT_EQ(classKeyword->Keyword(), "class");
    auto* memberType = dynamic_cast<MemberType*>(constraint->BaseTypes().At(1));
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "IFoo");
    auto* newKeyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(2));
    ASSERT_NE(newKeyword, nullptr);
    EXPECT_EQ(newKeyword->Keyword(), "new");
}

// ---- ConvertVariable ------------------------------------------------------------

// A non-const variable renders its type and name with no modifier and no
// initializer.
TEST(ConvertVariableTest, NonConstVariableRendersTypeAndName)
{
    ConvertTypeParameterFixture fx;
    TestVariable variable("count", fx.int32);

    std::unique_ptr<VariableDeclarationStatement> decl(fx.builder.ConvertVariable(variable));

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::None);
    ASSERT_NE(decl->Type(), nullptr);
    auto* primitiveType = dynamic_cast<PrimitiveType*>(decl->Type());
    ASSERT_NE(primitiveType, nullptr);
    EXPECT_EQ(primitiveType->Keyword(), "int");
    ASSERT_EQ(decl->Variables().Count(), 1);
    EXPECT_EQ(decl->Variables().At(0)->Name(), "count");
    EXPECT_EQ(decl->Variables().At(0)->Initializer(), nullptr);
}

// A const variable renders the const modifier and the initializer through the
// 2-arg ConvertConstantValue over the boxed constant value.
TEST(ConvertVariableTest, ConstVariableRendersConstModifierAndInitializer)
{
    ConvertTypeParameterFixture fx;
    TestVariable variable("Max", fx.int32);
    variable.SetIsConst(true);
    variable.SetConstantValue(std::int32_t{5});

    std::unique_ptr<VariableDeclarationStatement> decl(fx.builder.ConvertVariable(variable));

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Const);
    ASSERT_EQ(decl->Variables().Count(), 1);
    auto* initializer = decl->Variables().At(0)->Initializer();
    ASSERT_NE(initializer, nullptr);
    auto* primitive = dynamic_cast<PrimitiveExpression*>(initializer);
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(primitive->Value()), 5);
}

// A const variable whose GetConstantValue(throwOnInvalidMetadata: true)
// throws renders an ErrorExpression over the exception message (the C# catch
// of the metadata decoder's BadImageFormatException).
TEST(ConvertVariableTest, ThrowingConstantValueRendersErrorExpression)
{
    ConvertTypeParameterFixture fx;
    ThrowingVariable variable("Bad", fx.int32);
    variable.SetIsConst(true);

    std::unique_ptr<VariableDeclarationStatement> decl(fx.builder.ConvertVariable(variable));

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Const);
    auto* initializer = decl->Variables().At(0)->Initializer();
    ASSERT_NE(initializer, nullptr);
    auto* error = dynamic_cast<ErrorExpression*>(initializer);
    ASSERT_NE(error, nullptr);
}
