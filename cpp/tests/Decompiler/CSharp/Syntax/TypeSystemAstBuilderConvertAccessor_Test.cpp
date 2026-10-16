// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the TypeSystemAstBuilder "Convert Entity" accessor-support cluster
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 2188-2297 + 2771-2782: GenerateBodyBlock +
// ConvertAccessor + MergeReadOnlyModifiers + GetExplicitInterfaceType -- the
// shared prerequisites the ConvertProperty / ConvertIndexer / ConvertEvent /
// ConvertMethod / ConvertOperator / ConvertTypeDefinition renderers consume).
//
// The load-bearing cruxes:
//  (a) GenerateBodyBlock: the `throw new NotImplementedException();` body under
//      the GenerateBody flag (a single ThrowStatement over an
//      ObjectCreateExpression whose type renders System.NotImplementedException),
//      null when the flag is off;
//  (b) ConvertAccessor: the MethodSemanticsAttributes -> AccessorKind mapping
//      (with the C# `_` arm folding every other / combined value to Any), the
//      init-only SETTER upgrade gated on SupportInitAccessors (only a setter
//      upgrades; a non-upgraded init-only accessor keeps the init-ness as a
//      trailing `/* init */` comment), the accessibility modifier only when the
//      accessor's differs from the owner's, the `readonly` bit through the
//      HasReadonlyModifier helper (ref-readonly-this on a NON-readonly declaring
//      type -- a readonly struct definition yields no bit), the attribute
//      sections (own + `[return:]` + the `[param:]` on the LAST parameter under
//      addParameterAttribute), the MemberResolveResult annotation, and the body
//      under GenerateBody;
//  (c) MergeReadOnlyModifiers: the `readonly` bit hoists onto the declaration
//      when carried by accessor1 alone (accessor2 null) or by BOTH accessors;
//      a single-sided bit (accessor2 present but not readonly) stays on the
//      accessor;
//  (d) GetExplicitInterfaceType: the FIRST explicitly-implemented interface
//      member's declaring type rendered through ConvertType for an explicit
//      implementation; null for an implicit implementation, an empty
//      implementation list, or a null declaring type.
//
// The stub shapes: TestMethod models an accessor method with every flag the
// cluster reads configurable; TestParameter models a parameter carrying
// attributes; TestAttribute models a decoded attribute; ReadOnlyDef models a
// `readonly struct` declaring type (the IsReadOnly == true side of the
// readonly gate); ExplicitMember models the explicit-implementation base member
// whose declaring type GetExplicitInterfaceType renders.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"

#include "Decompiler/Semantics/MemberResolveResult.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::Accessor;
using ILSpy::Decompiler::CSharp::Syntax::AccessorKind;
using ILSpy::Decompiler::CSharp::Syntax::Comment;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEntity;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                             const std::string& ns,
                                             TS::TypeKind kind,
                                             const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, 0)), kind,
        Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
}

// A `readonly struct` declaring type (the IsReadOnly == true side of the
// ConvertAccessor Readonly gate; LookupTypeDefinition hardcodes false).
class ReadOnlyDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    bool IsReadOnly() const override { return true; }
};

// A configurable `IMethod` stub (the ConvertModifiers TestMethod pattern
// extended with the accessor-cluster surface: the accessibility, the
// ThisIsRefReadOnly / IsInitOnly flags, the parameter / attribute tables, and
// the explicit-interface-implementation members). The return type must be set
// (SetReturnType over a make_shared'd type) for any test reaching the
// MemberResolveResult annotation -- ComputeType calls shared_from_this over it.
class TestMethod : public IMethod {
public:
    TestMethod(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetSymbolKind(TS::SymbolKind k) { kind_ = k; }
    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) { declaringTypeDefinition_ = d; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetThisIsRefReadOnly(bool v) { thisIsRefReadOnly_ = v; }
    void SetIsInitOnly(bool v) { isInitOnly_ = v; }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }
    void SetReturnTypeAttributes(std::vector<const IAttribute*> a) {
        returnTypeAttributes_ = std::move(a);
    }
    void SetIsExplicitInterfaceImplementation(bool v) {
        isExplicitInterfaceImplementation_ = v;
    }
    void SetExplicitlyImplementedInterfaceMembers(std::vector<const IMember*> m) {
        explicitlyImplemented_ = std::move(m);
    }
    void SetReturnType(ITypePtr t) { returnTypeOverride_ = std::move(t); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return declaringTypeDefinition_;
    }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override {
        if (returnTypeOverride_)
            return *returnTypeOverride_;
        return returnType_;
    }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return explicitlyImplemented_;
    }
    bool IsExplicitInterfaceImplementation() const override {
        return isExplicitInterfaceImplementation_;
    }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMethod* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IMethod ---
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return returnTypeAttributes_;
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return isInitOnly_; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return false; }
    const IMember* AccessorOwner() const override { return nullptr; }
    MethodSemanticsAttributes AccessorKind() const override {
        return MethodSemanticsAttributes::None;
    }
    const IMethod* ReducedFrom() const override { return nullptr; }

private:
    std::string name_;
    const ICompilation& compilation_;
    KnownType returnType_{ KnownTypeCode::Object };
    ITypePtr returnTypeOverride_;
    TS::SymbolKind kind_ = TS::SymbolKind::Method;
    ITypePtr declaringType_;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    bool thisIsRefReadOnly_ = false;
    bool isInitOnly_ = false;
    bool isExplicitInterfaceImplementation_ = false;
    std::vector<const IParameter*> parameters_;
    std::vector<const IAttribute*> attributes_;
    std::vector<const IAttribute*> returnTypeAttributes_;
    std::vector<const IMember*> explicitlyImplemented_;
};

// A configurable `IParameter` stub (the DefaultTestParameter pattern extended
// with a configurable attribute table -- the ConvertAccessor `[param:]` arm
// reads the LAST parameter's attributes). `IParameter : IVariable : ISymbol`
// carries no INamedElement / ICompilationProvider bases, so the stub surface
// is exactly the variable + parameter members.
class TestParameter : public IParameter {
public:
    explicit TestParameter(TS::ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }

    // --- IVariable ---
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return {}; }

    // --- IParameter ---
    TS::ReferenceKind ReferenceKind() const override { return TS::ReferenceKind::None; }
    const TS::IParameterizedMember* Owner() const override { return nullptr; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }

private:
    std::string name_;
    TS::ITypePtr type_;
    std::vector<const IAttribute*> attributes_;
};

// A minimal decoded `IAttribute` stub (the ConvertAttribute TestAttribute
// pattern; only the attribute type is read -- the sections render the type
// name and nothing else).
class TestAttribute : public IAttribute {
public:
    explicit TestAttribute(ITypePtr attributeType) : attributeType_(std::move(attributeType)) {}

    const IType& AttributeType() const override { return *attributeType_; }
    const IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override { return {}; }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    ITypePtr attributeType_;
};

} // namespace

// ---------------------------------------------------------------------------
// GenerateBodyBlock
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertAccessorTest, GenerateBodyBlockFalseYieldsNull) {
    TypeSystemAstBuilder builder;
    EXPECT_EQ(builder.GenerateBodyBlock(), nullptr);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, GenerateBodyBlockTrueYieldsThrowNotImplemented) {
    TypeSystemAstBuilder builder;
    builder.GenerateBody() = true;

    ILSpy::Decompiler::CSharp::Syntax::BlockStatement* block = builder.GenerateBodyBlock();
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->Statements().Count(), 1);
    auto* throwStatement =
        dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::ThrowStatement*>(block->Statements()[0]);
    ASSERT_NE(throwStatement, nullptr);
    ASSERT_NE(throwStatement->Expression(), nullptr);
    auto* objectCreate = dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::ObjectCreateExpression*>(
        throwStatement->Expression());
    ASSERT_NE(objectCreate, nullptr);
    ASSERT_NE(objectCreate->Type(), nullptr);
    // The resolver-less ConvertType renders System.NotImplementedException as
    // the qualified MemberType(System, NotImplementedException).
    auto* memberType =
        dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::MemberType*>(objectCreate->Type());
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "NotImplementedException");
    auto* target =
        dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::SimpleType*>(memberType->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Identifier(), std::optional<std::string>("System"));
}

// ---------------------------------------------------------------------------
// ConvertAccessor
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertAccessorTest, NullAccessorYieldsNull) {
    TypeSystemAstBuilder builder;
    EXPECT_EQ(builder.ConvertAccessor(nullptr, MethodSemanticsAttributes::Getter,
                                      Accessibility::Public, false),
              nullptr);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, KindGetterMapsToGetter) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Getter);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, KindSetterMapsToSetter) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Setter);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, KindAdderMapsToAdder) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("add_E", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Adder,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Adder);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, KindRemoverMapsToRemover) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("remove_E", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Remover,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Remover);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, KindOtherFoldsToAny) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("M", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    // The C# `_` arm: every other / combined value folds to Any.
    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Other,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Any);

    std::unique_ptr<Accessor> raiser(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Raiser,
                                Accessibility::Public, false));
    ASSERT_NE(raiser, nullptr);
    EXPECT_EQ(raiser->Kind(), AccessorKind::Any);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, InitOnlySetterUpgradesToInitUnderSupportInitAccessors) {
    TypeSystemAstBuilder builder;
    builder.SupportInitAccessors() = true;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetIsInitOnly(true);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Init);
    // An upgraded init accessor carries no trailing `/* init */` comment.
    EXPECT_TRUE(accessor->TrailingTrivia().empty());
}

TEST(TypeSystemAstBuilderConvertAccessorTest, InitOnlySetterWithoutFlagKeepsSetterAndComments) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetIsInitOnly(true);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Setter);
    // The init-ness survives as a trailing `/* init */` comment.
    ASSERT_EQ(accessor->TrailingTrivia().size(), 1u);
    auto* comment = dynamic_cast<Comment*>(accessor->TrailingTrivia()[0]);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->Content(), "init");
    EXPECT_EQ(comment->CommentType(), ILSpy::Decompiler::CSharp::Syntax::CommentType::MultiLine);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, InitOnlyGetterKeepsGetterAndComments) {
    TypeSystemAstBuilder builder;
    builder.SupportInitAccessors() = true;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetIsInitOnly(true);

    // Only a SETTER upgrades to the init accessor.
    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Kind(), AccessorKind::Getter);
    ASSERT_EQ(accessor->TrailingTrivia().size(), 1u);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, NonInitOnlySetterHasNoTrailingComment) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_TRUE(accessor->TrailingTrivia().empty());
}

TEST(TypeSystemAstBuilderConvertAccessorTest, DifferingAccessibilityRendersModifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetAccessibility(Accessibility::Private);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Modifiers(), Modifiers::Private);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, SameAccessibilityRendersNoModifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetAccessibility(Accessibility::Public);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Modifiers(), Modifiers::None);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, AccessibilitySuppressedWithoutShowAccessibility) {
    TypeSystemAstBuilder builder;
    builder.ShowAccessibility() = false;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("set_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetAccessibility(Accessibility::Private);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Modifiers(), Modifiers::None);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, RefReadOnlyOnNonReadOnlyStructGainsReadonly) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto declaringType = MakeDef("S", "Ns", TS::TypeKind::Struct, compilation);
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetDeclaringTypeDefinition(declaringType.get());
    method->SetThisIsRefReadOnly(true);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_TRUE(accessor->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderConvertAccessorTest, RefReadOnlyOnReadOnlyStructGainsNoReadonly) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto declaringType = std::make_shared<ReadOnlyDef>(
        "S", "Ns", FullTypeName(TopLevelTypeName("Ns", "S", 0)), TS::TypeKind::Struct,
        Accessibility::Public, compilation, nullptr,
        KnownTypeCode::None);
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetDeclaringTypeDefinition(declaringType.get());
    method->SetThisIsRefReadOnly(true);

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_FALSE(accessor->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderConvertAccessorTest, AttributesSectionsRendered) {
    TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    LookupCompilation compilation;
    const ICompilation& comp = compilation;
    auto attributeType = MakeDef("AttrA", "Ns", TS::TypeKind::Class, comp);
    TestAttribute attribute(attributeType);
    TestAttribute returnAttribute(attributeType);
    auto parameterType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    auto parameter = std::make_shared<TestParameter>(parameterType, "value");
    TestAttribute paramAttribute(attributeType);
    parameter->SetAttributes({&paramAttribute});

    auto method = std::make_shared<TestMethod>("set_X", comp);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetAttributes({&attribute});
    method->SetReturnTypeAttributes({&returnAttribute});
    method->SetParameters({parameter.get()});

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, /*addParameterAttribute=*/true));
    ASSERT_NE(accessor, nullptr);
    // The own, [return:], and [param:] sections all render.
    ASSERT_EQ(accessor->Attributes().Count(), 3);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, ParamAttributeRequiresAddParameterAttribute) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    const ICompilation& comp = compilation;
    auto attributeType = MakeDef("AttrA", "Ns", TS::TypeKind::Class, comp);
    TestAttribute paramAttribute(attributeType);
    auto parameterType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    auto parameter = std::make_shared<TestParameter>(parameterType, "value");
    parameter->SetAttributes({&paramAttribute});

    auto method = std::make_shared<TestMethod>("set_X", comp);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetParameters({parameter.get()});

    // addParameterAttribute false: the [param:] section does not render (the
    // accessor has no attributes of its own either).
    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, /*addParameterAttribute=*/false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Attributes().Count(), 0);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, ParamAttributeRequiresParameters) {
    TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    LookupCompilation compilation;
    const ICompilation& comp = compilation;
    auto attributeType = MakeDef("AttrA", "Ns", TS::TypeKind::Class, comp);
    TestAttribute attribute(attributeType);

    auto method = std::make_shared<TestMethod>("set_X", comp);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetAttributes({&attribute});
    // No parameters: the [param:] arm cannot fire even with the flag on.

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, /*addParameterAttribute=*/true));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Attributes().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, AttributesSuppressedWithoutShowAttributes) {
    TypeSystemAstBuilder builder;
    builder.ShowAttributes() = false;
    LookupCompilation compilation;
    const ICompilation& comp = compilation;
    auto attributeType = MakeDef("AttrA", "Ns", TS::TypeKind::Class, comp);
    TestAttribute attribute(attributeType);

    auto method = std::make_shared<TestMethod>("set_X", comp);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetAttributes({&attribute});

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Setter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Attributes().Count(), 0);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, MemberResolveResultAnnotationAttached) {
    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    const MemberResolveResult* annotation = accessor->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), method.get());
}

TEST(TypeSystemAstBuilderConvertAccessorTest, NoAnnotationWithoutAddResolveResultAnnotations) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Annotation<MemberResolveResult>(), nullptr);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, BodyGeneratedUnderGenerateBody) {
    TypeSystemAstBuilder builder;
    builder.GenerateBody() = true;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    ASSERT_NE(accessor->Body(), nullptr);
    EXPECT_EQ(accessor->Body()->Statements().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, NoBodyWithoutGenerateBody) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    std::unique_ptr<Accessor> accessor(
        builder.ConvertAccessor(method.get(), MethodSemanticsAttributes::Getter,
                                Accessibility::Public, false));
    ASSERT_NE(accessor, nullptr);
    EXPECT_EQ(accessor->Body(), nullptr);
}

// ---------------------------------------------------------------------------
// MergeReadOnlyModifiers
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertAccessorTest, MergeReadOnlyNullAccessor1LeavesEverything) {
    auto* decl = new ILSpy::Decompiler::CSharp::Syntax::Accessor();
    auto* accessor2 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Setter);
    accessor2->Modifiers(Modifiers::Readonly);
    TypeSystemAstBuilder::MergeReadOnlyModifiers(*decl, nullptr, accessor2);
    EXPECT_EQ(decl->Modifiers(), Modifiers::None);
    EXPECT_TRUE(accessor2->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderConvertAccessorTest, MergeReadOnlySingleAccessorHoistsToDeclaration) {
    auto* decl = new ILSpy::Decompiler::CSharp::Syntax::Accessor();
    auto* accessor1 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Getter);
    accessor1->Modifiers(Modifiers::Readonly | Modifiers::Private);
    TypeSystemAstBuilder::MergeReadOnlyModifiers(*decl, accessor1, nullptr);
    EXPECT_FALSE(accessor1->HasModifier(Modifiers::Readonly));
    EXPECT_TRUE(accessor1->HasModifier(Modifiers::Private));
    EXPECT_TRUE(decl->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderConvertAccessorTest, MergeReadOnlyBothAccessorsHoistsToDeclaration) {
    auto* decl = new ILSpy::Decompiler::CSharp::Syntax::Accessor();
    auto* accessor1 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Getter);
    auto* accessor2 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Setter);
    accessor1->Modifiers(Modifiers::Readonly);
    accessor2->Modifiers(Modifiers::Readonly);
    TypeSystemAstBuilder::MergeReadOnlyModifiers(*decl, accessor1, accessor2);
    EXPECT_FALSE(accessor1->HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(accessor2->HasModifier(Modifiers::Readonly));
    EXPECT_TRUE(decl->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderConvertAccessorTest, MergeReadOnlyOneSidedStaysOnAccessor) {
    auto* decl = new ILSpy::Decompiler::CSharp::Syntax::Accessor();
    auto* accessor1 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Getter);
    auto* accessor2 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Setter);
    accessor1->Modifiers(Modifiers::Readonly);
    TypeSystemAstBuilder::MergeReadOnlyModifiers(*decl, accessor1, accessor2);
    // accessor2 is present but not readonly: the single-sided bit stays.
    EXPECT_TRUE(accessor1->HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(decl->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderConvertAccessorTest, MergeReadOnlyNonReadonlyAccessorsChangeNothing) {
    auto* decl = new ILSpy::Decompiler::CSharp::Syntax::Accessor();
    decl->Modifiers(Modifiers::Private);
    auto* accessor1 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Getter);
    auto* accessor2 = new ILSpy::Decompiler::CSharp::Syntax::Accessor(AccessorKind::Setter);
    TypeSystemAstBuilder::MergeReadOnlyModifiers(*decl, accessor1, accessor2);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Private);
}

// ---------------------------------------------------------------------------
// GetExplicitInterfaceType
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertAccessorTest, ImplicitImplementationYieldsNull) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));

    EXPECT_EQ(builder.GetExplicitInterfaceType(*method), nullptr);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, ExplicitImplementationRendersDeclaringType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    const ICompilation& comp = compilation;
    auto interfaceType = MakeDef("IFoo", "System", TS::TypeKind::Interface, comp);

    auto baseMember = std::make_shared<TestMethod>("get_X", comp);
    baseMember->SetDeclaringType(interfaceType);

    auto method = std::make_shared<TestMethod>("Ns_IFoo.get_X", comp);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetIsExplicitInterfaceImplementation(true);
    method->SetExplicitlyImplementedInterfaceMembers({baseMember.get()});

    ILSpy::Decompiler::CSharp::Syntax::AstType* type = builder.GetExplicitInterfaceType(*method);
    ASSERT_NE(type, nullptr);
    auto* memberType = dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::MemberType*>(type);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "IFoo");
}

TEST(TypeSystemAstBuilderConvertAccessorTest, EmptyImplementationListYieldsNull) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto method = std::make_shared<TestMethod>("Ns_IFoo.get_X", compilation);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetIsExplicitInterfaceImplementation(true);
    // The implementation list is empty (the C# FirstOrDefault default).

    EXPECT_EQ(builder.GetExplicitInterfaceType(*method), nullptr);
}

TEST(TypeSystemAstBuilderConvertAccessorTest, ExplicitImplementationWithNullDeclaringTypeYieldsNull) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    const ICompilation& comp = compilation;

    auto baseMember = std::make_shared<TestMethod>("get_X", comp);
    // DeclaringType stays null (a top-level entity).

    auto method = std::make_shared<TestMethod>("Ns_IFoo.get_X", comp);
    method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    method->SetIsExplicitInterfaceImplementation(true);
    method->SetExplicitlyImplementedInterfaceMembers({baseMember.get()});

    EXPECT_EQ(builder.GetExplicitInterfaceType(*method), nullptr);
}
