// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
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

// Tests for the TypeSystemAstBuilder "Convert Attribute" + "Convert Attribute
// Type" regions (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp},
// the port of TypeSystemAstBuilder.cs lines 770-988: ConvertAttribute /
// ConvertAttributes / ConvertAttributeType / ApplyShortAttributeNameIfPossible
// / IsAttributeType).
//
// The load-bearing cruxes:
//  (a) ConvertAttribute: the trailing "Attribute" suffix stripped off the
//      rendered MemberType name (the resolver-less global-namespace shape) and
//      off the SimpleType identifier (the resolver short-name arm that left the
//      full name in place), the constructor MemberResolveResult annotation, the
//      fixed-argument loop with the constructor parameter type threaded as the
//      expected type (and the argument's own type beyond the parameter count),
//      the named arguments with the MemberForNamedArgument field/property
//      annotation, and the HasDecodeErrors ErrorExpression tail;
//  (b) ConvertAttributes: the per-attribute AttributeSection wrapping with the
//      order preserved (SortAttributes off) vs the CompareAttribute-stable sort
//      (on), and the optional target written into the section;
//  (c) ConvertAttributeType: the AlwaysUseShortTypeNames unconditional short
//      name (including the null-short-name CLEARING quirk -- the C#
//      `Identifier.CreateIfNotEmpty(null)` shape), and the resolver-driven
//      ApplyShort decisions (unknown short name -> use it; short name resolving
//      to an attribute type and long+Attribute also an attribute type -> the
//      `@` verbatim prefix; neither -> the full name untouched);
//  (d) IsAttributeType: the KnownTypeCode.Attribute derivation through the
//      non-interface base types (the interface bases skipped), and the
//      TypeResolveResult shape over the ResolveResult overload.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/Semantics/InitializedObjectResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Syntax::Attribute;
using ILSpy::Decompiler::CSharp::Syntax::AttributeSection;
using ILSpy::Decompiler::CSharp::Syntax::AstType;
using ILSpy::Decompiler::CSharp::Syntax::ErrorExpression;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::NamedExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::Semantics::InitializedObjectResolveResult;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument;
using ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgumentKind;
using ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// A shared-managed definition helper (the MakeHost convention: every type fed
// to the builder must be shared-managed for the shared_from_this-based
// annotation paths).
std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                              const std::string& ns,
                                              TypeKind kind,
                                              KnownTypeCode code,
                                              const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, 0)), kind,
        Accessibility::Public, compilation, nullptr, code);
}

// A LookupTypeDefinition subclass with configurable GetFields / GetProperties
// tables -- the IType member-enumeration virtuals the MemberForNamedArgument
// lookup reads (filtered by name). The MemberHostType convention: the stub
// overrides the IType virtuals, NOT the ITypeDefinition-own Fields()
// accessor (the C# forwards the former to GetMembersHelper only from the real
// MetadataTypeDefinition).
class AttributeHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetFields(std::vector<const IField*> fields) { fields_ = std::move(fields); }
    void SetProperties(std::vector<const IProperty*> properties) {
        properties_ = std::move(properties);
    }

    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter = nullptr,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options
        = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::None) const override {
        (void)options;
        if (!filter)
            return fields_;
        std::vector<const IField*> result;
        for (const IField* field : fields_)
            if (filter(field))
                result.push_back(field);
        return result;
    }

    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter = nullptr,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options
        = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::None) const override {
        (void)options;
        if (!filter)
            return properties_;
        std::vector<const IProperty*> result;
        for (const IProperty* property : properties_)
            if (filter(property))
                result.push_back(property);
        return result;
    }

private:
    std::vector<const IField*> fields_;
    std::vector<const IProperty*> properties_;
};

// A minimal IField stub: a name plus a make_shared'd type (the
// MemberResolveResult ComputeType takes shared_from_this over the member's
// return type -- the iteration-111 bad_weak_ptr convention).
class NamedField : public IField {
public:
    NamedField(std::string name, ITypePtr type, const ICompilation& compilation)
        : name_(std::move(name)), type_(std::move(type)), compilation_(compilation) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return false;
    }
    const IAttribute* GetAttribute(
        ::ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return true; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override {
        return this;
    }
    const IType& ReturnType() const override { return *type_; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution()
        const override {
        return nullptr;
    }
    const ::ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const ::ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ::ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override {
        return obj == this;
    }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool = false) const override { return {}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr type_;
    const ICompilation& compilation_;
};

// A minimal IProperty stub (the TestProperty convention).
class NamedProperty : public IProperty {
public:
    NamedProperty(std::string name, ITypePtr type, const ICompilation& compilation)
        : name_(std::move(name)), type_(std::move(type)), compilation_(compilation) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Property; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return false;
    }
    const IAttribute* GetAttribute(
        ::ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override {
        return this;
    }
    const IType& ReturnType() const override { return *type_; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution()
        const override {
        return &identitySubst_;
    }
    const ::ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const ::ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ::ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override {
        return obj == this;
    }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IParameter*> Parameters()
        const override {
        return {};
    }
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    const IMethod* Getter() const override { return nullptr; }
    const IMethod* Setter() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr type_;
    const ICompilation& compilation_;
    mutable ::ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution identitySubst_{
        std::nullopt, std::nullopt};
};

// A minimal IAttribute stub: the configurable surface ConvertAttribute reads.
class TestAttribute : public IAttribute {
public:
    TestAttribute(ITypePtr attributeType) : attributeType_(std::move(attributeType)) {}

    void SetConstructor(const IMethod* constructor) { constructor_ = constructor; }
    void SetFixedArguments(std::vector<CustomAttributeTypedArgument> args) {
        fixedArguments_ = std::move(args);
    }
    void SetNamedArguments(std::vector<CustomAttributeNamedArgument> args) {
        namedArguments_ = std::move(args);
    }
    void SetHasDecodeErrors(bool value) { hasDecodeErrors_ = value; }

    const IType& AttributeType() const override { return *attributeType_; }
    const IMethod* Constructor() const override { return constructor_; }
    bool HasDecodeErrors() const override { return hasDecodeErrors_; }
    std::vector<CustomAttributeTypedArgument> FixedArguments() const override {
        return fixedArguments_;
    }
    std::vector<CustomAttributeNamedArgument> NamedArguments() const override {
        return namedArguments_;
    }

private:
    ITypePtr attributeType_;
    const IMethod* constructor_ = nullptr;
    std::vector<CustomAttributeTypedArgument> fixedArguments_;
    std::vector<CustomAttributeNamedArgument> namedArguments_;
    bool hasDecodeErrors_ = false;
};

// A LookupTypeDefinition subclass with a configurable DeclaringType (the shared
// stub hardcodes it empty; the ConvertTypeHelper nested-type recursion and the
// ApplyShortAttributeNameIfPossible declaring-type arm read it).
class NestedType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetDeclaringType(ITypePtr declaringType) { declaringType_ = std::move(declaringType); }
    ITypePtr DeclaringType() const override { return declaringType_; }

private:
    ITypePtr declaringType_;
};

// A LookupTypeDefinition subclass with a configurable GetNestedTypes table
// (the ApplyShortAttributeNameIfPossible declaring-type arm scans the declaring
// definition's nested types through the IType::GetNestedTypes filter).
class NestedHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetNestedTypes(std::vector<ITypePtr> nested) { nested_ = std::move(nested); }

    std::vector<ITypePtr> GetNestedTypes(
        std::function<bool(const ITypeDefinition*)> filter,
        GetMemberOptions options) const override {
        (void)options;
        std::vector<ITypePtr> result;
        for (const ITypePtr& type : nested_) {
            const auto* def = dynamic_cast<const ITypeDefinition*>(type.get());
            if (def != nullptr && (filter == nullptr || filter(def)))
                result.push_back(type);
        }
        return result;
    }

private:
    std::vector<ITypePtr> nested_;
};

// A configurable INamespace with a GetTypeDefinition name table (the resolver's
// global-namespace type lookup reads it; the shared stub's TestNamespace has
// none).
class TableNamespace : public ::ILSpy::Decompiler::TypeSystem::INamespace {
public:
    TableNamespace(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void AddTypeDef(const ITypeDefinition* def) { defs_.push_back(def); }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return name_; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const ::ILSpy::Decompiler::TypeSystem::INamespace* ParentNamespace() const override {
        return nullptr;
    }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::INamespace*> ChildNamespaces()
        const override {
        return {};
    }
    std::vector<const ITypeDefinition*> Types() const override { return defs_; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules()
        const override {
        return {};
    }
    const ::ILSpy::Decompiler::TypeSystem::INamespace* GetChildNamespace(
        const std::string&) const override {
        return nullptr;
    }
    const ITypeDefinition* GetTypeDefinition(const std::string& name,
                                              int typeParameterCount) const override {
        for (const ITypeDefinition* def : defs_) {
            if (def->Name() == name && def->TypeParameterCount() == typeParameterCount)
                return def;
        }
        return nullptr;
    }

private:
    std::string name_;
    const ICompilation& compilation_;
    std::vector<const ITypeDefinition*> defs_;
};

// A LookupCompilation subclass whose RootNamespace is a TableNamespace (the
// shared stub's TestNamespace resolves no types, so the resolver's
// global-namespace lookup can never succeed through it).
class TableCompilation : public LookupCompilation {
public:
    TableCompilation() : LookupCompilation(), rootNamespace_("", *this) {}
    const ::ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override {
        return rootNamespace_;
    }
    TableNamespace& Root() { return rootNamespace_; }

private:
    TableNamespace rootNamespace_;
};

} // namespace

// ===========================================================================
// IsAttributeType -- the KnownTypeCode.Attribute derivation predicates.
// ===========================================================================

class TypeSystemAstBuilderIsAttributeTypeTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<LookupTypeDefinition> attributeBase_ =
        MakeDef("Attribute", "System", TypeKind::Class, KnownTypeCode::Attribute,
                *compilation_);
    std::shared_ptr<LookupTypeDefinition> attributeDerived_ =
        MakeDef("Foo", "", TypeKind::Class, KnownTypeCode::None, *compilation_);
    std::shared_ptr<LookupTypeDefinition> plain_ =
        MakeDef("Plain", "", TypeKind::Class, KnownTypeCode::None, *compilation_);
    std::shared_ptr<LookupTypeDefinition> interfaceBase_ =
        MakeDef("IEmpty", "", TypeKind::Interface, KnownTypeCode::None, *compilation_);
    TypeSystemAstBuilder builder_;

    TypeSystemAstBuilderIsAttributeTypeTest() {
        attributeDerived_->AddDirectBaseType(attributeBase_);
        plain_->AddDirectBaseType(interfaceBase_);
    }
};

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, NullIsFalse) {
    EXPECT_FALSE(builder_.IsAttributeType(nullptr));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, TrueForTheAttributeTypeItself) {
    // The type's own entry is in the non-interface base-type closure.
    EXPECT_TRUE(builder_.IsAttributeType(attributeBase_.get()));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, TrueForAttributeDerivedType) {
    EXPECT_TRUE(builder_.IsAttributeType(attributeDerived_.get()));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, FalseForPlainType) {
    EXPECT_FALSE(builder_.IsAttributeType(plain_.get()));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, FalseWhenOnlyInterfaceBases) {
    // The GetNonInterfaceBaseTypes walk skips the interface bases a class
    // implements (the SkipImplementedInterfaces collector flag), so an
    // interface named IEmpty never reaches the IsKnownType check.
    const std::vector<const IType*> bases =
        ::ILSpy::Decompiler::TypeSystem::GetNonInterfaceBaseTypes(*plain_);
    EXPECT_EQ(bases.size(), 1u); // the type itself, base-first/self-last order
    EXPECT_FALSE(builder_.IsAttributeType(plain_.get()));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, ResolveResultOverloadTrueForAttributeType) {
    auto rr = std::make_shared<TypeResolveResult>(attributeDerived_);
    EXPECT_TRUE(builder_.IsAttributeType(*rr));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, ResolveResultOverloadFalseForPlainType) {
    auto rr = std::make_shared<TypeResolveResult>(plain_);
    EXPECT_FALSE(builder_.IsAttributeType(*rr));
}

TEST_F(TypeSystemAstBuilderIsAttributeTypeTest, ResolveResultOverloadFalseForNonTypeResult) {
    // An InitializedObjectResolveResult is not a TypeResolveResult.
    auto rr = std::make_shared<InitializedObjectResolveResult>(plain_);
    EXPECT_FALSE(builder_.IsAttributeType(*rr));
}

// ===========================================================================
// ConvertAttribute -- the [...] node renderer.
// ===========================================================================

class TypeSystemAstBuilderConvertAttributeTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<LookupTypeDefinition> int32_ =
        MakeDef("Int32", "System", TypeKind::Struct, KnownTypeCode::Int32, *compilation_);
    std::shared_ptr<LookupTypeDefinition> string_ =
        MakeDef("String", "System", TypeKind::Class, KnownTypeCode::String, *compilation_);
    std::shared_ptr<AttributeHostType> host_ =
        std::make_shared<AttributeHostType>(
            "HostAttribute", "", FullTypeName(TopLevelTypeName("", "HostAttribute", 0)),
            TypeKind::Class, Accessibility::Public, *compilation_, nullptr,
            KnownTypeCode::None);
    TypeSystemAstBuilder builder_;

    TypeSystemAstBuilderConvertAttributeTest() {
        compilation_->RegisterKnownType(KnownTypeCode::Int32, int32_.get());
        compilation_->RegisterKnownType(KnownTypeCode::String, string_.get());
    }

    // A constructor stub with a make_shared'd return type (the MemberResolveResult
    // ComputeType takes shared_from_this over it, the iteration-111 convention).
    std::shared_ptr<LookupMethod> MakeMethod(const std::string& name) {
        auto method = std::make_shared<LookupMethod>(name, *compilation_);
        method->SetReturnType(int32_);
        return method;
    }
};

TEST_F(TypeSystemAstBuilderConvertAttributeTest, StripsSuffixFromMemberTypeName) {
    // Resolver-less: a top-level type in the GLOBAL namespace renders as the
    // `global::Name` MemberType (the empty-namespace arm), so the strip hits the
    // MemberType branch.
    auto type = MakeDef("FooAttribute", "", TypeKind::Class, KnownTypeCode::None,
                        *compilation_);
    TestAttribute attribute(type);

    Attribute* result = builder_.ConvertAttribute(attribute);

    auto* memberType = dynamic_cast<MemberType*>(result->Type());
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, LeavesPlainNestedNameUnchanged) {
    auto type = MakeDef("Foo", "", TypeKind::Class, KnownTypeCode::None, *compilation_);
    TestAttribute attribute(type);

    Attribute* result = builder_.ConvertAttribute(attribute);

    auto* memberType = dynamic_cast<MemberType*>(result->Type());
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, AnnotatesConstructorWhenEnabled) {
    builder_.AddResolveResultAnnotations() = true;
    auto ctor = MakeMethod(".ctor");
    TestAttribute attribute(host_);
    attribute.SetConstructor(ctor.get());

    Attribute* result = builder_.ConvertAttribute(attribute);

    const auto* annotation = result->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), ctor.get());
    EXPECT_EQ(annotation->TargetResult(), nullptr); // the C# null target
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, NoConstructorAnnotationWhenDisabledOrAbsent) {
    auto ctor = MakeMethod(".ctor");
    TestAttribute attribute(host_);
    attribute.SetConstructor(ctor.get());

    // AddResolveResultAnnotations defaults to false: no annotation.
    Attribute* result = builder_.ConvertAttribute(attribute);
    EXPECT_EQ(result->Annotation<MemberResolveResult>(), nullptr);

    // Annotations enabled but the constructor absent: no annotation.
    builder_.AddResolveResultAnnotations() = true;
    TestAttribute noCtor(host_);
    Attribute* result2 = builder_.ConvertAttribute(noCtor);
    EXPECT_EQ(result2->Annotation<MemberResolveResult>(), nullptr);
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, ConvertsFixedArgumentsWithParameterTypes) {
    auto ctor = MakeMethod(".ctor");
    auto parameter = std::make_shared<
        ::ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(int32_, "value");
    ctor->SetParameters({parameter.get()});
    TestAttribute attribute(host_);
    attribute.SetConstructor(ctor.get());
    // Two fixed arguments but ONE constructor parameter: the first takes the
    // parameter's type as its expected type, the second falls back to its own.
    attribute.SetFixedArguments({
        CustomAttributeTypedArgument(int32_, std::any(std::int32_t(42))),
        CustomAttributeTypedArgument(string_, std::any(std::string("x"))),
    });

    Attribute* result = builder_.ConvertAttribute(attribute);

    ASSERT_EQ(result->Arguments().Count(), 2);
    const auto* first = dynamic_cast<const PrimitiveExpression*>(result->Arguments()[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(first->Value()), 42);
    const auto* second = dynamic_cast<const PrimitiveExpression*>(result->Arguments()[1]);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(std::get<std::string>(second->Value()), "x");
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, ConvertsNamedArgumentsWithFieldMember) {
    builder_.AddResolveResultAnnotations() = true;
    auto flagField = std::make_shared<NamedField>("Flag", int32_, *compilation_);
    host_->SetFields({flagField.get()});
    TestAttribute attribute(host_);
    attribute.SetNamedArguments({CustomAttributeNamedArgument(
        "Flag", CustomAttributeNamedArgumentKind::Field, int32_,
        std::any(std::int32_t(42)))});

    Attribute* result = builder_.ConvertAttribute(attribute);

    ASSERT_EQ(result->Arguments().Count(), 1);
    auto* named = dynamic_cast<NamedExpression*>(result->Arguments()[0]);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->Name(), "Flag");
    const auto* value = dynamic_cast<const PrimitiveExpression*>(named->Expression());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(value->Value()), 42);
    // The MemberForNamedArgument field lookup annotates the named expression.
    const auto* annotation = named->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), flagField.get());
    // The annotation's target is the shared InitializedObjectResolveResult over
    // the attribute type.
    const auto* target = dynamic_cast<const InitializedObjectResolveResult*>(
        annotation->TargetResult());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(&target->Type(), host_.get());
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, ConvertsNamedArgumentsWithPropertyMember) {
    builder_.AddResolveResultAnnotations() = true;
    auto prop = std::make_shared<NamedProperty>("Prop", int32_, *compilation_);
    host_->SetProperties({prop.get()});
    TestAttribute attribute(host_);
    attribute.SetNamedArguments({CustomAttributeNamedArgument(
        "Prop", CustomAttributeNamedArgumentKind::Property, int32_,
        std::any(std::int32_t(7)))});

    Attribute* result = builder_.ConvertAttribute(attribute);

    auto* named = dynamic_cast<NamedExpression*>(result->Arguments()[0]);
    ASSERT_NE(named, nullptr);
    const auto* annotation = named->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), prop.get());
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, NamedArgumentWithoutMemberAddsNoAnnotation) {
    builder_.AddResolveResultAnnotations() = true;
    TestAttribute attribute(host_);
    attribute.SetNamedArguments({CustomAttributeNamedArgument(
        "Missing", CustomAttributeNamedArgumentKind::Field, int32_,
        std::any(std::int32_t(1)))});

    Attribute* result = builder_.ConvertAttribute(attribute);

    auto* named = dynamic_cast<NamedExpression*>(result->Arguments()[0]);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->Annotation<MemberResolveResult>(), nullptr);
}

TEST_F(TypeSystemAstBuilderConvertAttributeTest, DecodeErrorsSetArgumentListWithErrorExpression) {
    TestAttribute attribute(host_);
    attribute.SetHasDecodeErrors(true);

    Attribute* result = builder_.ConvertAttribute(attribute);

    EXPECT_TRUE(result->HasArgumentList());
    ASSERT_EQ(result->Arguments().Count(), 1);
    auto* error = dynamic_cast<ErrorExpression*>(result->Arguments()[0]);
    ASSERT_NE(error, nullptr);
    const auto trailing = error->TrailingTrivia();
    ASSERT_EQ(trailing.size(), 1u);
    auto* comment = dynamic_cast<
        ::ILSpy::Decompiler::CSharp::Syntax::Comment*>(trailing[0]);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->Content(), "Could not decode attribute arguments.");
}

// ===========================================================================
// ConvertAttributes -- the section list.
// ===========================================================================

class TypeSystemAstBuilderConvertAttributesTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    TypeSystemAstBuilder builder_;

    TestAttribute MakeAttribute(const std::string& name) {
        auto type =
            MakeDef(name, "Ns", TypeKind::Class, KnownTypeCode::None, *compilation_);
        return TestAttribute(type);
    }
};

TEST_F(TypeSystemAstBuilderConvertAttributesTest, WrapsEachAttributeWithOrderPreserved) {
    TestAttribute first = MakeAttribute("ZedAttribute");
    TestAttribute second = MakeAttribute("AlphaAttribute");

    std::vector<AttributeSection*> sections =
        builder_.ConvertAttributes({&first, &second});

    ASSERT_EQ(sections.size(), 2u);
    ASSERT_EQ(sections[0]->Attributes().Count(), 1);
    ASSERT_EQ(sections[1]->Attributes().Count(), 1);
    // SortAttributes defaults to false: the input order is preserved.
    auto* firstAttr = sections[0]->Attributes()[0];
    auto* secondAttr = sections[1]->Attributes()[0];
    auto* firstType = dynamic_cast<MemberType*>(firstAttr->Type());
    auto* secondType = dynamic_cast<MemberType*>(secondAttr->Type());
    ASSERT_NE(firstType, nullptr);
    ASSERT_NE(secondType, nullptr);
    EXPECT_EQ(firstType->MemberName(), "Zed");
    EXPECT_EQ(secondType->MemberName(), "Alpha");
}

TEST_F(TypeSystemAstBuilderConvertAttributesTest, SortsByAttributeTypeWhenEnabled) {
    builder_.SortAttributes() = true;
    TestAttribute first = MakeAttribute("ZedAttribute");
    TestAttribute second = MakeAttribute("AlphaAttribute");

    std::vector<AttributeSection*> sections =
        builder_.ConvertAttributes({&first, &second});

    // The CompareAttribute ordering keys on the attribute TYPE's reflection
    // name first: Ns.AlphaAttribute precedes Ns.ZedAttribute.
    ASSERT_EQ(sections.size(), 2u);
    auto* firstType = dynamic_cast<MemberType*>(sections[0]->Attributes()[0]->Type());
    auto* secondType = dynamic_cast<MemberType*>(sections[1]->Attributes()[0]->Type());
    ASSERT_NE(firstType, nullptr);
    ASSERT_NE(secondType, nullptr);
    EXPECT_EQ(firstType->MemberName(), "Alpha");
    EXPECT_EQ(secondType->MemberName(), "Zed");
}

TEST_F(TypeSystemAstBuilderConvertAttributesTest, PropagatesTargetToSections) {
    TestAttribute first = MakeAttribute("FooAttribute");
    TestAttribute second = MakeAttribute("BarAttribute");

    std::vector<AttributeSection*> withTarget =
        builder_.ConvertAttributes({&first, &second}, std::string("return"));
    std::vector<AttributeSection*> withoutTarget =
        builder_.ConvertAttributes({&first, &second});

    ASSERT_EQ(withTarget.size(), 2u);
    EXPECT_EQ(withTarget[0]->AttributeTarget(), "return");
    EXPECT_EQ(withTarget[1]->AttributeTarget(), "return");
    ASSERT_EQ(withoutTarget.size(), 2u);
    // A null target leaves the section's AttributeTarget empty.
    EXPECT_EQ(withoutTarget[0]->AttributeTarget(), "");
    EXPECT_EQ(withoutTarget[0]->AttributeTargetToken(), nullptr);
}

// ===========================================================================
// ConvertAttributeType -- the attribute-type renderer with the short-name
// handling.
// ===========================================================================

class TypeSystemAstBuilderConvertAttributeTypeTest : public ::testing::Test {
protected:
    // The TableCompilation must outlive the resolver (the resolver holds the
    // compilation reference); the members are ordered accordingly.
    TableCompilation compilation_;
    std::shared_ptr<LookupTypeDefinition> attributeBase_ =
        MakeDef("Attribute", "System", TypeKind::Class, KnownTypeCode::Attribute,
                compilation_);
    std::shared_ptr<LookupTypeDefinition> fooAttribute_ =
        MakeDef("FooAttribute", "", TypeKind::Class, KnownTypeCode::None, compilation_);
    std::shared_ptr<LookupTypeDefinition> attributeFoo_ =
        MakeDef("Foo", "", TypeKind::Class, KnownTypeCode::None, compilation_);
    std::shared_ptr<LookupTypeDefinition> plainFoo_ =
        MakeDef("Foo", "", TypeKind::Class, KnownTypeCode::None, compilation_);
    std::shared_ptr<LookupTypeDefinition> attributeFooAttributeAttribute_ =
        MakeDef("FooAttributeAttribute", "", TypeKind::Class, KnownTypeCode::None,
                compilation_);
    std::shared_ptr<CSharpResolver> resolver_ =
        std::make_shared<CSharpResolver>(compilation_);
    TypeSystemAstBuilder builder_{resolver_};

    TypeSystemAstBuilderConvertAttributeTypeTest() {
        attributeFoo_->AddDirectBaseType(attributeBase_);
        attributeFooAttributeAttribute_->AddDirectBaseType(attributeBase_);
        compilation_.Root().AddTypeDef(fooAttribute_.get());
        compilation_.Root().AddTypeDef(attributeFoo_.get());
        compilation_.Root().AddTypeDef(attributeFooAttributeAttribute_.get());
    }
};

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest,
       AlwaysUseShortTypeNamesStripsSuffix) {
    LookupCompilation compilation;
    TypeSystemAstBuilder builder; // resolver-less: only the AlwaysUse arm runs
    builder.AlwaysUseShortTypeNames() = true;
    auto type = MakeDef("FooAttribute", "Ns", TypeKind::Class, KnownTypeCode::None,
                        compilation);

    AstType* result = builder.ConvertAttributeType(*type);

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("Foo"));
}

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest,
       AlwaysUseShortTypeNamesClearsIdentifierWithoutSuffix) {
    LookupCompilation compilation;
    TypeSystemAstBuilder builder;
    builder.AlwaysUseShortTypeNames() = true;
    auto type = MakeDef("Foo", "Ns", TypeKind::Class, KnownTypeCode::None,
                        compilation);

    AstType* result = builder.ConvertAttributeType(*type);

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    // The C# `st.Identifier = shortName` with a NULL short name clears the
    // identifier (Identifier.CreateIfNotEmpty(null) yields no token).
    EXPECT_EQ(simple->Identifier(), std::nullopt);
    EXPECT_EQ(simple->IdentifierToken(), nullptr);
}

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest, AnnotatesTheRenderedType) {
    builder_.AddResolveResultAnnotations() = true;

    AstType* result = builder_.ConvertAttributeType(*fooAttribute_);

    const auto* annotation = result->Annotation<TypeResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(&annotation->Type(), fooAttribute_.get());
}

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest, ResolverPathUsesShortNameWhenUnknown) {
    // "Foo" is not registered in the root namespace: the short-name lookup
    // yields the UnknownIdentifierResolveResult, so the short name is safe.
    TableCompilation compilation;
    auto fooAttribute =
        MakeDef("FooAttribute", "", TypeKind::Class, KnownTypeCode::None, compilation);
    compilation.Root().AddTypeDef(fooAttribute.get());
    auto resolver = std::make_shared<CSharpResolver>(compilation);
    TypeSystemAstBuilder builder(resolver);

    AstType* result = builder.ConvertAttributeType(*fooAttribute);

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("Foo"));
}

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest,
       ResolverPathUsesShortNameWhenResolvedTypeIsNotAttribute) {
    // "Foo" resolves to a PLAIN type (not attribute-derived): the short name
    // is still safe (the `!IsAttributeType(shortRR)` arm).
    TableCompilation compilation;
    auto fooAttribute =
        MakeDef("FooAttribute", "", TypeKind::Class, KnownTypeCode::None, compilation);
    auto plainFoo = MakeDef("Foo", "", TypeKind::Class, KnownTypeCode::None, compilation);
    compilation.Root().AddTypeDef(fooAttribute.get());
    compilation.Root().AddTypeDef(plainFoo.get());
    auto resolver = std::make_shared<CSharpResolver>(compilation);
    TypeSystemAstBuilder builder(resolver);

    AstType* result = builder.ConvertAttributeType(*fooAttribute);

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("Foo"));
}

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest,
       ResolverPathAddsVerbatimPrefixWhenBothAreAttributeTypes) {
    // "Foo" resolves to an attribute-derived type (the short name is taken)
    // AND "FooAttributeAttribute" resolves to an attribute-derived type (the
    // long name would capture it) -> the `@` verbatim prefix disables the
    // implicit "Attribute" suffix.
    AstType* result = builder_.ConvertAttributeType(*fooAttribute_);

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    // The `Identifier.Create` factory strips the `@` into the token's
    // IsVerbatim flag (the observable is the bare name + the verbatim token).
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("FooAttribute"));
    ASSERT_NE(simple->IdentifierToken(), nullptr);
    EXPECT_TRUE(simple->IdentifierToken()->IsVerbatim());
}

TEST_F(TypeSystemAstBuilderConvertAttributeTypeTest,
       ConvertAttributeStripsSuffixFromResolverRenderedSimpleName) {
    // The composed flow: "Foo" resolves to an attribute type (ApplyShort keeps
    // the full name) while "FooAttributeAttribute" does not (no `@` prefix) --
    // ConvertAttribute's own SimpleType strip then removes the suffix.
    TestAttribute attribute(fooAttribute_);

    Attribute* result = builder_.ConvertAttribute(attribute);

    auto* simple = dynamic_cast<SimpleType*>(result->Type());
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("Foo"));
    // The strip was the plain suffix removal (no `@` verbatim escape).
    ASSERT_NE(simple->IdentifierToken(), nullptr);
    EXPECT_FALSE(simple->IdentifierToken()->IsVerbatim());
}

// ===========================================================================
// ApplyShortAttributeNameIfPossible -- the MemberType arms (the SimpleType arm
// is covered through the ConvertAttributeType resolver tests above).
// ===========================================================================

class TypeSystemAstBuilderApplyShortAttributeNameTest : public ::testing::Test {
protected:
    std::unique_ptr<LookupCompilation> compilation_ = std::make_unique<LookupCompilation>();
    std::shared_ptr<LookupTypeDefinition> attributeBase_ =
        MakeDef("Attribute", "System", TypeKind::Class, KnownTypeCode::Attribute,
                *compilation_);
    TypeSystemAstBuilder builder_; // resolver-less: the MemberType arms never deref it

    std::shared_ptr<NestedHostType> MakeOuter() {
        return std::make_shared<NestedHostType>(
            "Outer", "", FullTypeName(TopLevelTypeName("", "Outer", 0)), TypeKind::Class,
            Accessibility::Public, *compilation_, nullptr, KnownTypeCode::None);
    }

    std::shared_ptr<NestedType> MakeNested(const std::string& name,
                                           const std::shared_ptr<NestedHostType>& outer) {
        auto nested = std::make_shared<NestedType>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Class,
            Accessibility::Public, *compilation_, nullptr, KnownTypeCode::None);
        nested->SetDeclaringType(outer);
        return nested;
    }

    std::shared_ptr<LookupTypeDefinition> MakeAttributeDerived(const std::string& name) {
        auto def =
            MakeDef(name, "", TypeKind::Class, KnownTypeCode::None, *compilation_);
        def->AddDirectBaseType(attributeBase_);
        return def;
    }
};

TEST_F(TypeSystemAstBuilderApplyShortAttributeNameTest,
       NestedDeclaringTypeUsesShortNameWhenNestedIsNotAttribute) {
    auto outer = MakeOuter();
    auto nested = MakeNested("FooAttribute", outer);
    auto* astType = new MemberType(new SimpleType("Outer"), "FooAttribute");

    builder_.ApplyShortAttributeNameIfPossible(*nested, *astType,
                                               std::optional<std::string>("Foo"));

    // No nested "Foo" in the declaring type: the short name is safe.
    EXPECT_EQ(astType->MemberName(), "Foo");
}

TEST_F(TypeSystemAstBuilderApplyShortAttributeNameTest,
       NestedDeclaringTypeKeepsNameWhenNestedShortNameIsAttribute) {
    auto outer = MakeOuter();
    auto nested = MakeNested("FooAttribute", outer);
    auto nestedFoo = MakeAttributeDerived("Foo");
    outer->SetNestedTypes({nestedFoo});
    auto* astType = new MemberType(new SimpleType("Outer"), "FooAttribute");

    builder_.ApplyShortAttributeNameIfPossible(*nested, *astType,
                                               std::optional<std::string>("Foo"));

    // The declaring type has an attribute-derived nested "Foo": the short name
    // would capture it -> the name stays untouched (and no nested
    // "FooAttributeAttribute" exists, so no `@` prefix either).
    EXPECT_EQ(astType->MemberName(), "FooAttribute");
}

TEST_F(TypeSystemAstBuilderApplyShortAttributeNameTest,
       NestedDeclaringTypeAddsVerbatimPrefixWhenNestedAttributeExists) {
    auto outer = MakeOuter();
    auto nested = MakeNested("FooAttribute", outer);
    auto nestedLong = MakeAttributeDerived("FooAttributeAttribute");
    outer->SetNestedTypes({nestedLong});
    auto* astType = new MemberType(new SimpleType("Outer"), "FooAttribute");

    // A null short name skips the first arm (the C# `shortName != null` guard).
    builder_.ApplyShortAttributeNameIfPossible(*nested, *astType, std::nullopt);

    // The declaring type has an attribute-derived nested "FooAttributeAttribute"
    // (`name + "Attribute"`): the `@` verbatim prefix disables the implicit
    // "Attribute" suffix (the factory strips the `@` into IsVerbatim).
    EXPECT_EQ(astType->MemberName(), "FooAttribute");
    ASSERT_NE(astType->MemberNameToken(), nullptr);
    EXPECT_TRUE(astType->MemberNameToken()->IsVerbatim());
}

TEST_F(TypeSystemAstBuilderApplyShortAttributeNameTest,
       NamespaceTargetUsesShortNameWhenNotAttribute) {
    TableNamespace ns("Ns", *compilation_);
    auto nested = MakeDef("FooAttribute", "", TypeKind::Class, KnownTypeCode::None,
                          *compilation_);
    auto* target = new SimpleType("Ns");
    target->AddAnnotation(std::make_shared<NamespaceResolveResult>(&ns));
    auto* astType = new MemberType(target, "FooAttribute");

    builder_.ApplyShortAttributeNameIfPossible(*nested, *astType,
                                              std::optional<std::string>("Foo"));

    // The namespace holds no "Foo": the short name is safe.
    EXPECT_EQ(astType->MemberName(), "Foo");
}

TEST_F(TypeSystemAstBuilderApplyShortAttributeNameTest,
       NamespaceTargetAddsVerbatimPrefixWhenAttributeExists) {
    TableNamespace ns("Ns", *compilation_);
    auto attributeLong = MakeAttributeDerived("FooAttributeAttribute");
    ns.AddTypeDef(attributeLong.get());
    auto nested = MakeDef("FooAttribute", "", TypeKind::Class, KnownTypeCode::None,
                          *compilation_);
    auto* target = new SimpleType("Ns");
    target->AddAnnotation(std::make_shared<NamespaceResolveResult>(&ns));
    auto* astType = new MemberType(target, "FooAttribute");

    // A null short name skips the first arm; the namespace resolves
    // "FooAttributeAttribute" to an attribute type -> the `@` prefix.
    builder_.ApplyShortAttributeNameIfPossible(*nested, *astType, std::nullopt);

    EXPECT_EQ(astType->MemberName(), "FooAttribute");
    ASSERT_NE(astType->MemberNameToken(), nullptr);
    EXPECT_TRUE(astType->MemberNameToken()->IsVerbatim());
}
