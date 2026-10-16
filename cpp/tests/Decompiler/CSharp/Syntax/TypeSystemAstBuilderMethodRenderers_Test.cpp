// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
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

// Tests for the TypeSystemAstBuilder "Convert Entity" method renderers
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 2362-2500: ConvertMethod + ConvertOperator +
// ConvertConstructor + ConvertDestructor -- the method/operator/constructor/
// destructor renderers composing GetMemberModifiers, the own + `[return: ...]`
// attribute sections, ConvertTypeParameter / ConvertParameter, the
// extension-method `this` modifier, the nullability-disambiguation-vs-constraint
// split, GenerateBodyBlock, and GetExplicitInterfaceType).
//
// The load-bearing cruxes:
//  (a) ConvertMethod: the own + `[return: ...]` attribute-section pair, the
//      extension-method `this` modifier on the FIRST parameter gated on
//      IsExtensionMethod && ReducedFrom == null && a non-empty parameter list
//      (a REDUCED extension method's receiver is already gone, so no `this`),
//      and the override/explicit-interface constraint split (an override or
//      explicit interface implementation calls AddNullabilityDisambiguatingConstraints
//      instead of rendering the full per-type-parameter clauses -- C# inherits
//      those constraints from the base member and forbids restating them);
//  (b) ConvertOperator: the operator token is looked up from the method-name
//      tail after the LAST '.', with the three ConvertMethod fallbacks (a
//      non-operator name, an unsupported `>>>`, and an unsupported `checked`
//      operator all fall back to the MethodDeclaration form);
//  (c) ConvertConstructor: the name comes from the declaring type definition
//      (a constructor names itself after its type), and ONLY the own attribute
//      sections render (no `[return: ...]`, no explicit-interface type);
//  (d) ConvertDestructor: NO modifiers at all (never accessibility or static),
//      no parameters, no explicit-interface type -- only the attributes, the
//      name, the annotation, and the body.
//
// The stub shapes: TestMemberMethod models an IMethod with every flag the four
// renderers read (the shared-ISymbol diamond needs the Name()/SymbolKind()
// redeclarations on the IMethod interface itself; the iteration-128 TestMethod
// pattern extended with IsOverride/IsExplicitInterfaceImplementation/
// IsExtensionMethod/ReducedFrom/ReturnTypeIsRefReadOnly/TypeParameters/
// GetReturnTypeAttributes/ExplicitlyImplementedInterfaceMembers). TestParameter
// and TestAttribute carry over unchanged; MakeDef/MakeIntDef build the
// shared-managed LookupTypeDefinition instances every return-type / ConvertType
// consumer needs (the registered-instance type-cache model). The
// NullabilityAnnotatedTypeParameter wrapper over a RefTypeParameter models the
// `T?` signature shape the nullability-disambiguation arm records (the
// NullabilityDisambig fixture pattern).

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"

#include "Decompiler/Semantics/MemberResolveResult.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::ConstructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::DestructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EntityDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::MethodDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::OperatorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::OperatorType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedTypeParameter;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

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

// A shared-managed known-type definition (the registered-instance type-cache
// model: the builtin-keyword short-circuit reads the definition's own
// KnownTypeCode, so a Def(Int32) renders the `int` PrimitiveType).
std::shared_ptr<LookupTypeDefinition> MakeIntDef(const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        "Int32", "System", FullTypeName(TopLevelTypeName("System", "Int32", 0)),
        TypeKind::Struct, Accessibility::Public, compilation, nullptr,
        KnownTypeCode::Int32);
}

// A configurable `IMethod` stub (the iteration-128 TestMethod pattern extended
// with every flag the method-renderer quartet reads: the override/explicit-
// interface flags the constraint split branches on, the extension-method flag
// and the ReducedFrom pointer the `this`-modifier gate reads, the
// ReturnTypeIsRefReadOnly flag the trailing-readonly promotion reads, the
// type-parameter list, and the return-type attributes). The return type must
// be shared-managed (the MemberResolveResult ComputeType / ConvertType paths
// call shared_from_this over it).
class TestMemberMethod : public IMethod {
public:
    TestMemberMethod(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetIsStatic(bool v) { isStatic_ = v; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetIsOverride(bool v) { isOverride_ = v; }
    void SetIsExplicitInterfaceImplementation(bool v) {
        isExplicitInterfaceImplementation_ = v;
    }
    void SetIsExtensionMethod(bool v) { isExtensionMethod_ = v; }
    void SetReducedFrom(const IMethod* v) { reducedFrom_ = v; }
    void SetReturnTypeIsRefReadOnly(bool v) { returnTypeIsRefReadOnly_ = v; }
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) {
        typeParameters_ = std::move(tps);
    }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }
    void SetReturnTypeAttributes(std::vector<const IAttribute*> a) {
        returnTypeAttributes_ = std::move(a);
    }
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) {
        declaringTypeDefinition_ = d;
    }
    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }
    void SetExplicitlyImplemented(std::vector<const IMember*> m) {
        explicitlyImplemented_ = std::move(m);
    }
    void SetReturnType(ITypePtr t) { returnTypeOverride_ = std::move(t); }
    void SetHasBody(bool v) { hasBody_ = v; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
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
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
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
    bool IsOverride() const override { return isOverride_; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMethod* Specialize(const TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const IMember* obj, const TypeVisitor*) const override {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IMethod ---
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return returnTypeAttributes_;
    }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    std::vector<const ITypeParameter*> TypeParameters() const override {
        return typeParameters_;
    }
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return isExtensionMethod_; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return hasBody_; }
    bool IsAccessor() const override { return false; }
    const IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const IMethod* ReducedFrom() const override { return reducedFrom_; }

private:
    std::string name_;
    const ICompilation& compilation_;
    KnownType returnType_{ KnownTypeCode::Object };
    ITypePtr returnTypeOverride_;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    ITypePtr declaringType_;
    bool isStatic_ = false;
    bool isOverride_ = false;
    bool isExplicitInterfaceImplementation_ = false;
    bool isExtensionMethod_ = false;
    bool returnTypeIsRefReadOnly_ = false;
    bool hasBody_ = false;
    const IMethod* reducedFrom_ = nullptr;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    std::vector<const IParameter*> parameters_;
    std::vector<const ITypeParameter*> typeParameters_;
    std::vector<const IAttribute*> attributes_;
    std::vector<const IAttribute*> returnTypeAttributes_;
    std::vector<const IMember*> explicitlyImplemented_;
};

// A class-constrained type parameter (IsReferenceType == true, the `where T :
// class` shape GetNullabilityDisambiguator maps to "class" -- the
// NullabilityDisambig RefTypeParameter pattern).
class RefTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;

    std::optional<bool> IsReferenceType() const override { return true; }
};

// The wrapper up-cast to IType through the ITypeParameter base (the port's
// diamond makes the direct NATP -> IType conversion ambiguous -- the
// NullabilityDisambig AsITypeViaParameter pattern).
ITypePtr AsITypeViaParameter(
    const std::shared_ptr<NullabilityAnnotatedTypeParameter>& natp) {
    return std::static_pointer_cast<IType>(std::static_pointer_cast<ITypeParameter>(natp));
}

// A configurable `IParameter` stub (the iteration-128 TestParameter pattern: a
// parameter over a shared-managed type; ConvertParameter reads its type and
// name).
class TestParameter : public IParameter {
public:
    explicit TestParameter(TS::ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

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
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }

private:
    std::string name_;
    TS::ITypePtr type_;
};

// A minimal decoded `IAttribute` stub (the iteration-128 TestAttribute
// pattern; only the attribute type is read).
class TestAttribute : public IAttribute {
public:
    explicit TestAttribute(ITypePtr attributeType)
        : attributeType_(std::move(attributeType)) {}

    const IType& AttributeType() const override { return *attributeType_; }
    const IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
        return {};
    }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
        return {};
    }

private:
    ITypePtr attributeType_;
};

} // namespace

// ---------------------------------------------------------------------------
// ConvertMethod
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodRendersNameTypeAndModifiers) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod method("M", compilation);
    method.SetIsStatic(true);
    method.SetReturnType(intDef);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Name(), "M");
    auto* rt = dynamic_cast<PrimitiveType*>(decl->ReturnType());
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(rt->Keyword(), "int");
    EXPECT_TRUE(decl->HasModifier(Modifiers::Static));
    EXPECT_TRUE(decl->HasModifier(Modifiers::Public));
    // The default body flag off leaves the body slot null.
    EXPECT_EQ(decl->Body(), nullptr);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodRendersOwnAndReturnAttributes) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto attrDef = MakeDef("ObsoleteAttribute", "System", TypeKind::Class, compilation);

    TestMemberMethod method("M", compilation);
    method.SetAttributes({new TestAttribute(attrDef)});
    method.SetReturnTypeAttributes({new TestAttribute(attrDef)});
    builder.ShowAttributes() = true;

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    // One own section (no target) + one `[return: ...]` section.
    ASSERT_EQ(decl->Attributes().Count(), 2);
    EXPECT_EQ(decl->Attributes()[0]->AttributeTarget(), "");
    EXPECT_EQ(decl->Attributes()[1]->AttributeTarget(), "return");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodAttributesSkippedWithoutFlag) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto attrDef = MakeDef("ObsoleteAttribute", "System", TypeKind::Class, compilation);

    TestMemberMethod method("M", compilation);
    method.SetAttributes({new TestAttribute(attrDef)});
    method.SetReturnTypeAttributes({new TestAttribute(attrDef)});

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Attributes().Count(), 0);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodAnnotationAttached) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    builder.AddResolveResultAnnotations() = true;

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    const MemberResolveResult* annotation = decl->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), &method);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodAnnotationAbsentWithoutFlag) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Annotation<MemberResolveResult>(), nullptr);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodRefReadOnlyPromotesReadOnlySpecifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    // A `ref readonly int` method: the return type is the ByReferenceType over
    // the element, rendered as `ref int` (a ComposedType with
    // HasRefSpecifier); the ReturnTypeIsRefReadOnly flag promotes the readonly
    // specifier (the iteration-128 ConvertField crux shape).
    TestMemberMethod method("M", compilation);
    method.SetReturnType(std::make_shared<ByReferenceType>(intDef));
    method.SetReturnTypeIsRefReadOnly(true);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    auto* composed = dynamic_cast<ComposedType*>(decl->ReturnType());
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_TRUE(composed->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodRefReadOnlyFlagOffLeavesNoReadOnlySpecifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod method("M", compilation);
    method.SetReturnType(std::make_shared<ByReferenceType>(intDef));

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    auto* composed = dynamic_cast<ComposedType*>(decl->ReturnType());
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_FALSE(composed->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodTypeParametersRenderUnderShowTypeParameters) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto tp = std::make_shared<LookupTypeParameter>("T");
    TestMemberMethod method("M", compilation);
    method.SetTypeParameters({tp.get()});

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->TypeParameters().Count(), 1);
    EXPECT_EQ(decl->TypeParameters()[0]->Name(), "T");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodTypeParametersSkippedWithoutFlag) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto tp = std::make_shared<LookupTypeParameter>("T");
    TestMemberMethod method("M", compilation);
    method.SetTypeParameters({tp.get()});
    builder.ShowTypeParameters() = false;

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->TypeParameters().Count(), 0);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodParametersRender) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto p1 = std::make_shared<TestParameter>(intDef, "a");
    auto p2 = std::make_shared<TestParameter>(intDef, "b");
    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    method.SetParameters({p1.get(), p2.get()});

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Parameters().Count(), 2);
    EXPECT_EQ(decl->Parameters()[0]->Name(), "a");
    EXPECT_EQ(decl->Parameters()[1]->Name(), "b");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ExtensionMethodMarksFirstParameterThis) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto p1 = std::make_shared<TestParameter>(intDef, "thisArg");
    auto p2 = std::make_shared<TestParameter>(intDef, "arg");
    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    method.SetParameters({p1.get(), p2.get()});
    method.SetIsExtensionMethod(true);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Parameters().Count(), 2);
    EXPECT_TRUE(decl->Parameters()[0]->HasThisModifier());
    EXPECT_FALSE(decl->Parameters()[1]->HasThisModifier());
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ReducedExtensionMethodDoesNotMarkThis) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto p1 = std::make_shared<TestParameter>(intDef, "arg");
    TestMemberMethod original("M", compilation);
    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    method.SetParameters({p1.get()});
    method.SetIsExtensionMethod(true);
    // A reduced extension method's receiver parameter is already gone, so
    // re-adding `this` would point at a wrong parameter.
    method.SetReducedFrom(&original);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Parameters().Count(), 1);
    EXPECT_FALSE(decl->Parameters()[0]->HasThisModifier());
}

TEST(TypeSystemAstBuilderMethodRenderersTest, NonExtensionMethodDoesNotMarkThis) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto p1 = std::make_shared<TestParameter>(intDef, "arg");
    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    method.SetParameters({p1.get()});

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Parameters().Count(), 1);
    EXPECT_FALSE(decl->Parameters()[0]->HasThisModifier());
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ExtensionMethodWithoutParametersNoThis) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    method.SetIsExtensionMethod(true);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Parameters().Count(), 0);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodConstraintsRenderForTypeParameters) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto tp = std::make_shared<LookupTypeParameter>("T");
    tp->SetHasValueTypeConstraint(true);
    TestMemberMethod method("M", compilation);
    method.SetTypeParameters({tp.get()});

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Constraints().Count(), 1);
    auto* c = decl->Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(c->BaseTypes().FirstOrNull());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "struct");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodConstraintFlagOffSkipsConstraints) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto tp = std::make_shared<LookupTypeParameter>("T");
    tp->SetHasValueTypeConstraint(true);
    TestMemberMethod method("M", compilation);
    method.SetTypeParameters({tp.get()});
    builder.ShowTypeParameterConstraints() = false;

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Constraints().Count(), 0);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, OverrideMethodUsesNullabilityDisambiguation) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto t0 = std::make_shared<RefTypeParameter>("T");
    auto natp = std::make_shared<NullabilityAnnotatedTypeParameter>(t0, Nullability::Nullable);
    TestMemberMethod method("M", compilation);
    method.SetTypeParameters({t0.get()});
    method.SetReturnType(AsITypeViaParameter(natp));
    method.SetIsOverride(true);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    // The override path renders the DISAMBIGUATION clause (`where T : class`)
    // instead of the full constraint clause (the C# inherits the constraints
    // from the base member and forbids restating them).
    ASSERT_EQ(decl->Constraints().Count(), 1);
    auto* c = decl->Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(c->BaseTypes().FirstOrNull());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "class");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ExplicitInterfaceMethodUsesNullabilityDisambiguation) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto t0 = std::make_shared<RefTypeParameter>("T");
    auto natp = std::make_shared<NullabilityAnnotatedTypeParameter>(t0, Nullability::Nullable);
    TestMemberMethod method("M", compilation);
    method.SetTypeParameters({t0.get()});
    method.SetReturnType(AsITypeViaParameter(natp));
    method.SetIsExplicitInterfaceImplementation(true);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Constraints().Count(), 1);
    auto* c = decl->Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(c->BaseTypes().FirstOrNull());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "class");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodExplicitInterfaceTypeRouted) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto ifaceDef = MakeDef("I", "", TypeKind::Interface, compilation);
    auto hostDef = MakeDef("Host", "", TypeKind::Class, compilation);

    TestMemberMethod method("M", compilation);
    method.SetReturnType(hostDef);
    method.SetIsExplicitInterfaceImplementation(true);
    method.SetExplicitlyImplemented({&method});
    method.SetDeclaringType(ifaceDef);

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    EXPECT_NE(decl->PrivateImplementationType(), nullptr);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, MethodBodyGeneratedUnderGenerateBody) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod method("M", compilation);
    method.SetReturnType(intDef);
    builder.GenerateBody() = true;

    std::unique_ptr<MethodDeclaration> decl(builder.ConvertMethod(method));
    ASSERT_NE(decl, nullptr);
    // The unconditional `decl.Body = GenerateBodyBlock()` produces the
    // throw-NotImplementedException body under the flag.
    ASSERT_NE(decl->Body(), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertOperator
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMethodRenderersTest, OperatorRendersOperatorTypeAndParameters) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto p1 = std::make_shared<TestParameter>(intDef, "left");
    auto p2 = std::make_shared<TestParameter>(intDef, "right");
    TestMemberMethod op("op_Addition", compilation);
    op.SetReturnType(intDef);
    op.SetParameters({p1.get(), p2.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    auto* operatorDecl = dynamic_cast<OperatorDeclaration*>(decl.get());
    ASSERT_NE(operatorDecl, nullptr);
    EXPECT_EQ(operatorDecl->OperatorType(), OperatorType::Addition);
    // The operator's name is DERIVED from the OperatorType scalar.
    EXPECT_EQ(operatorDecl->Name(), "op_Addition");
    EXPECT_EQ(operatorDecl->Parameters().Count(), 2);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, NonOperatorNameFallsBackToMethodDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("M", compilation);
    op.SetReturnType(intDef);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    EXPECT_NE(dynamic_cast<MethodDeclaration*>(decl.get()), nullptr);
    EXPECT_EQ(decl->Name(), "M");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ExplicitInterfaceOperatorNameTailIsUsed) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("Ns.I.op_Addition", compilation);
    op.SetReturnType(intDef);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    auto* operatorDecl = dynamic_cast<OperatorDeclaration*>(decl.get());
    ASSERT_NE(operatorDecl, nullptr);
    EXPECT_EQ(operatorDecl->OperatorType(), OperatorType::Addition);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, UnsignedRightShiftFallsBackWhenUnsupported) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("op_UnsignedRightShift", compilation);
    op.SetReturnType(intDef);
    // SupportUnsignedRightShift defaults false.
    ASSERT_FALSE(builder.SupportUnsignedRightShift());

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    EXPECT_NE(dynamic_cast<MethodDeclaration*>(decl.get()), nullptr);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, UnsignedRightShiftRendersWhenSupported) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("op_UnsignedRightShift", compilation);
    op.SetReturnType(intDef);
    builder.SupportUnsignedRightShift() = true;

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    auto* operatorDecl = dynamic_cast<OperatorDeclaration*>(decl.get());
    ASSERT_NE(operatorDecl, nullptr);
    EXPECT_EQ(operatorDecl->OperatorType(), OperatorType::UnsignedRightShift);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, CheckedOperatorFallsBackWhenUnsupported) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("op_CheckedAddition", compilation);
    op.SetReturnType(intDef);
    // SupportOperatorChecked defaults false.
    ASSERT_FALSE(builder.SupportOperatorChecked());

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    EXPECT_NE(dynamic_cast<MethodDeclaration*>(decl.get()), nullptr);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, CheckedOperatorRendersWhenSupported) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("op_CheckedAddition", compilation);
    op.SetReturnType(intDef);
    builder.SupportOperatorChecked() = true;

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    auto* operatorDecl = dynamic_cast<OperatorDeclaration*>(decl.get());
    ASSERT_NE(operatorDecl, nullptr);
    EXPECT_EQ(operatorDecl->OperatorType(), OperatorType::CheckedAddition);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, OperatorBodyGeneratedUnderGenerateBody) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod op("op_Addition", compilation);
    op.SetReturnType(intDef);
    builder.GenerateBody() = true;

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertOperator(op));
    ASSERT_NE(decl, nullptr);
    auto* operatorDecl = dynamic_cast<OperatorDeclaration*>(decl.get());
    ASSERT_NE(operatorDecl, nullptr);
    ASSERT_NE(operatorDecl->Body(), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertConstructor
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMethodRenderersTest, ConstructorRendersNameFromDeclaringType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod ctor(".ctor", compilation);
    ctor.SetReturnType(intDef);
    ctor.SetDeclaringTypeDefinition(hostDef.get());

    std::unique_ptr<ConstructorDeclaration> decl(builder.ConvertConstructor(ctor));
    ASSERT_NE(decl, nullptr);
    // A constructor names itself after its declaring type.
    EXPECT_EQ(decl->Name(), "Foo");
    EXPECT_TRUE(decl->HasModifier(Modifiers::Public));
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ConstructorWithoutDefinitionRendersNoName) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    TestMemberMethod ctor(".ctor", compilation);
    ctor.SetReturnType(intDef);

    std::unique_ptr<ConstructorDeclaration> decl(builder.ConvertConstructor(ctor));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Name(), "");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ConstructorRendersParameters) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    auto p1 = std::make_shared<TestParameter>(intDef, "x");
    TestMemberMethod ctor(".ctor", compilation);
    ctor.SetReturnType(intDef);
    ctor.SetDeclaringTypeDefinition(hostDef.get());
    ctor.SetParameters({p1.get()});

    std::unique_ptr<ConstructorDeclaration> decl(builder.ConvertConstructor(ctor));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Parameters().Count(), 1);
    EXPECT_EQ(decl->Parameters()[0]->Name(), "x");
    // A constructor node carries no explicit-interface-type slot at all (the
    // C# ConstructorDeclaration has no PrivateImplementationType member).
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ConstructorRendersNoReturnAttributes) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto attrDef = MakeDef("ObsoleteAttribute", "System", TypeKind::Class, compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod ctor(".ctor", compilation);
    ctor.SetDeclaringTypeDefinition(hostDef.get());
    ctor.SetAttributes({new TestAttribute(attrDef)});
    ctor.SetReturnTypeAttributes({new TestAttribute(attrDef)});
    builder.ShowAttributes() = true;

    std::unique_ptr<ConstructorDeclaration> decl(builder.ConvertConstructor(ctor));
    ASSERT_NE(decl, nullptr);
    // ONLY the own attribute sections render (a constructor has no return
    // type, so no `[return: ...]` sections).
    ASSERT_EQ(decl->Attributes().Count(), 1);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, ConstructorBodyGeneratedUnderGenerateBody) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod ctor(".ctor", compilation);
    ctor.SetReturnType(intDef);
    ctor.SetDeclaringTypeDefinition(hostDef.get());
    builder.GenerateBody() = true;

    std::unique_ptr<ConstructorDeclaration> decl(builder.ConvertConstructor(ctor));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->Body(), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertDestructor
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMethodRenderersTest, DestructorRendersNameFromDeclaringType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod dtor("Finalize", compilation);
    dtor.SetDeclaringTypeDefinition(hostDef.get());

    std::unique_ptr<DestructorDeclaration> decl(builder.ConvertDestructor(dtor));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Name(), "Foo");
}

TEST(TypeSystemAstBuilderMethodRenderersTest, DestructorRendersNoModifiers) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod dtor("Finalize", compilation);
    dtor.SetDeclaringTypeDefinition(hostDef.get());
    // Even a static destructor renders NO modifiers (never accessibility or
    // static -- the NeedsAccessibility destructor case; ConvertDestructor does
    // not even call GetMemberModifiers).
    dtor.SetIsStatic(true);

    std::unique_ptr<DestructorDeclaration> decl(builder.ConvertDestructor(dtor));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::None);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, DestructorAttributesRenderUnderShowAttributes) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto attrDef = MakeDef("ObsoleteAttribute", "System", TypeKind::Class, compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod dtor("Finalize", compilation);
    dtor.SetDeclaringTypeDefinition(hostDef.get());
    dtor.SetAttributes({new TestAttribute(attrDef)});
    builder.ShowAttributes() = true;

    std::unique_ptr<DestructorDeclaration> decl(builder.ConvertDestructor(dtor));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Attributes().Count(), 1);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, DestructorBodyGeneratedUnderGenerateBody) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod dtor("Finalize", compilation);
    dtor.SetDeclaringTypeDefinition(hostDef.get());
    builder.GenerateBody() = true;

    std::unique_ptr<DestructorDeclaration> decl(builder.ConvertDestructor(dtor));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->Body(), nullptr);
}

TEST(TypeSystemAstBuilderMethodRenderersTest, DestructorAnnotationAttached) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);

    TestMemberMethod dtor("Finalize", compilation);
    dtor.SetDeclaringTypeDefinition(hostDef.get());
    // The MemberResolveResult annotation computes over the method's return
    // type, so it must be shared-managed (the bad_weak_ptr trap -- the stub's
    // default KnownType member is not shared-managed).
    dtor.SetReturnType(intDef);
    builder.AddResolveResultAnnotations() = true;

    std::unique_ptr<DestructorDeclaration> decl(builder.ConvertDestructor(dtor));
    ASSERT_NE(decl, nullptr);
    const MemberResolveResult* annotation = decl->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), &dtor);
}
