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

// Tests for the TypeSystemAstBuilder "Convert Entity" member renderers
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 2143-2186 + 2252-2275 + 2294-2320 + 2322-2360:
// ConvertField + ConvertProperty + ConvertIndexer + ConvertEvent -- the four
// per-member renderers composing the iteration-127 accessor-support cluster
// with the already-landed attribute / parameter / constant-value / modifier
// helpers).
//
// The load-bearing cruxes:
//  (a) ConvertField: the IsConst bit REPLACES the static bit (a C# constant is
//      never `static const`; the if/else-if chain lets only one of
//      const/readonly/volatile fire, const first), the constant initializer
//      rendered under IsConst && ShowConstantValues over the field's
//      IVARIABLE::Type (not the IMember::ReturnType), the
//      BadImageFormatException-to-ErrorExpression catch, and the `ref readonly`
//      trailing-readonly promotion onto the rendered ComposedType;
//  (b) ConvertProperty: the name assignment, the getter/setter routing through
//      ConvertAccessor (only the setter arm carries addParameterAttribute --
//      its `[param:]` section), the explicit-interface type, and the
//      MergeReadOnlyModifiers hoist over both accessors;
//  (c) ConvertIndexer: the property shape with a parameter-list loop in place
//      of the name assignment (an indexer carries NO identifier);
//  (d) ConvertEvent: the UseCustomEvents flag selects the CustomEventDeclaration
//      (add/remove accessors, explicit-interface type, readonly hoist) vs the
//      plain field-like EventDeclaration (the name inside a VariableInitializer,
//      no accessors).
//
// The stub shapes: TestField models an IField with every flag the renderer
// reads (the shared-ISymbol diamond needs the Name()/SymbolKind()
// redeclarations on the IField interface itself); TestProperty models an
// IProperty (getter/setter back-references + the IParameterizedMember
// surface); TestEvent models an IEvent; TestMethod/TestParameter/TestAttribute
// carry over from the ConvertAccessor test (the accessor methods, the
// `[param:]`-carrying setter parameter, and the decoded attribute). MakeDef
// builds the shared-managed LookupTypeDefinition instances every return-type /
// ConvertType consumer needs (the registered-instance type-cache model).

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

#include "Decompiler/Semantics/MemberResolveResult.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::CustomEventDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EntityDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EventDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::FieldDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::IndexerDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::PropertyDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::Semantics::MemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEvent;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
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
using ILSpy::Decompiler::TypeSystem::TypeKind;
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

// A shared-managed known-type definition (the registered-instance type-cache
// model: the builtin-keyword short-circuit reads the definition's own
// KnownTypeCode, so a Def(Int32) renders the `int` PrimitiveType).
std::shared_ptr<LookupTypeDefinition> MakeIntDef(const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        "Int32", "System", FullTypeName(TopLevelTypeName("System", "Int32", 0)),
        TypeKind::Struct, Accessibility::Public, compilation, nullptr,
        KnownTypeCode::Int32);
}

// A configurable `IField` stub (the TypeSystemAstBuilderConstantValueSupport
// TestField pattern extended with every flag ConvertField reads: the
// const/readonly/volatile chain, the ref-readonly flag, the constant value,
// the accessibility, and the attribute table). The shared-ISymbol diamond
// needs the IField-own Name()/SymbolKind() redeclarations (the IField.hpp
// port-convention note); a single override is the final overrider for both
// ISymbol subobjects. The return type must be shared-managed (the
// MemberResolveResult ComputeType / ConvertType paths call shared_from_this
// over it).
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    void SetIsConst(bool v) { isConst_ = v; }
    void SetIsReadOnly(bool v) { isReadOnly_ = v; }
    void SetIsVolatile(bool v) { isVolatile_ = v; }
    void SetIsStatic(bool v) { isStatic_ = v; }
    void SetReturnTypeIsRefReadOnly(bool v) { returnTypeIsRefReadOnly_ = v; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }
    void SetConstantValue(std::any value) { constantValue_ = std::move(value); }
    void SetThrowOnConstantValue(bool v) { throwOnConstantValue_ = v; }
    void SetType(ITypePtr t) { typeOverride_ = std::move(t); }

    // --- ISymbol (the IField-own redeclarations) ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
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
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const IMember* obj, const TypeVisitor*) const override {
        return obj == this;
    }

    // --- IVariable ---
    const IType& Type() const override {
        if (typeOverride_)
            return *typeOverride_;
        return *returnType_;
    }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool) const override {
        if (throwOnConstantValue_)
            throw std::runtime_error("BadImageFormat");
        return constantValue_;
    }

    // --- IField ---
    bool IsReadOnly() const override { return isReadOnly_; }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsVolatile() const override { return isVolatile_; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    bool isConst_ = false;
    bool isReadOnly_ = false;
    bool isVolatile_ = false;
    bool isStatic_ = false;
    bool returnTypeIsRefReadOnly_ = false;
    bool throwOnConstantValue_ = false;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    std::vector<const IAttribute*> attributes_;
    std::any constantValue_;
    ITypePtr typeOverride_;
};

// A configurable `IProperty` stub (the ConvertAccessor TestMethod pattern
// extended to the IProperty surface: the getter/setter accessor
// back-references, the ref-readonly flag, the parameter list, and the
// explicit-interface members). Both ConvertProperty and ConvertIndexer
// consume it.
class TestProperty : public IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    void SetGetter(const IMethod* m) { getter_ = m; }
    void SetSetter(const IMethod* m) { setter_ = m; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetReturnTypeIsRefReadOnly(bool v) { returnTypeIsRefReadOnly_ = v; }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }
    void SetIsExplicitInterfaceImplementation(bool v) {
        isExplicitInterfaceImplementation_ = v;
    }
    void SetExplicitlyImplementedInterfaceMembers(std::vector<const IMember*> m) {
        explicitlyImplemented_ = std::move(m);
    }
    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Property; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
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
    const IMember* Specialize(const TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const IMember* obj, const TypeVisitor*) const override {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IProperty ---
    bool CanGet() const override { return getter_ != nullptr; }
    bool CanSet() const override { return setter_ != nullptr; }
    const IMethod* Getter() const override { return getter_; }
    const IMethod* Setter() const override { return setter_; }
    bool IsIndexer() const override { return !parameters_.empty(); }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    const IMethod* getter_ = nullptr;
    const IMethod* setter_ = nullptr;
    bool returnTypeIsRefReadOnly_ = false;
    bool isExplicitInterfaceImplementation_ = false;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    std::vector<const IParameter*> parameters_;
    std::vector<const IAttribute*> attributes_;
    std::vector<const IMember*> explicitlyImplemented_;
    ITypePtr declaringType_;
};

// A configurable `IEvent` stub (the TestProperty pattern over the IEvent
// surface: the add/remove accessor back-references).
class TestEvent : public IEvent {
public:
    TestEvent(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    void SetAddAccessor(const IMethod* m) { addAccessor_ = m; }
    void SetRemoveAccessor(const IMethod* m) { removeAccessor_ = m; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }
    void SetIsExplicitInterfaceImplementation(bool v) {
        isExplicitInterfaceImplementation_ = v;
    }
    void SetExplicitlyImplementedInterfaceMembers(std::vector<const IMember*> m) {
        explicitlyImplemented_ = std::move(m);
    }
    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Event; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *returnType_; }
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
    const IMember* Specialize(const TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const IMember* obj, const TypeVisitor*) const override {
        return obj == this;
    }

    // --- IEvent ---
    bool CanAdd() const override { return addAccessor_ != nullptr; }
    bool CanRemove() const override { return removeAccessor_ != nullptr; }
    bool CanInvoke() const override { return false; }
    const IMethod* AddAccessor() const override { return addAccessor_; }
    const IMethod* RemoveAccessor() const override { return removeAccessor_; }
    const IMethod* InvokeAccessor() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    const IMethod* addAccessor_ = nullptr;
    const IMethod* removeAccessor_ = nullptr;
    bool isExplicitInterfaceImplementation_ = false;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    std::vector<const IAttribute*> attributes_;
    std::vector<const IMember*> explicitlyImplemented_;
    ITypePtr declaringType_;
};

// A configurable `IMethod` stub (the ConvertAccessor TestMethod pattern; the
// accessor methods ConvertAccessor renders).
class TestMethod : public IMethod {
public:
    TestMethod(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetThisIsRefReadOnly(bool v) { thisIsRefReadOnly_ = v; }
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) {
        declaringTypeDefinition_ = d;
    }
    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    void SetAttributes(std::vector<const IAttribute*> a) { attributes_ = std::move(a); }
    void SetReturnType(ITypePtr t) { returnTypeOverride_ = std::move(t); }

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
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
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
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::ITypeParameter*> TypeParameters()
        const override {
        return {};
    }
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return true; }
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
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    bool thisIsRefReadOnly_ = false;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    ITypePtr declaringType_;
    std::vector<const IParameter*> parameters_;
    std::vector<const IAttribute*> attributes_;
};

// A configurable `IParameter` stub (the ConvertAccessor TestParameter
// pattern: a parameter over a shared-managed type; the `[param:]`-section and
// the ConvertParameter consumers read its attributes).
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
// pattern; only the attribute type is read).
class TestAttribute : public IAttribute {
public:
    explicit TestAttribute(ITypePtr attributeType) : attributeType_(std::move(attributeType)) {}

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
// ConvertField
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldRendersNameTypeAndModifiers) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public);
    ASSERT_EQ(decl->Variables().Count(), 1);
    EXPECT_EQ(decl->Variables()[0]->Name(), "F");
    ASSERT_NE(decl->ReturnType(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveType*>(decl->ReturnType());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), std::optional<std::string>("int"));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ConstFieldReplacesStaticWithConst) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsConst(true);
    field.SetIsStatic(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    // The const bit REPLACES the static bit: a C# constant is never `static
    // const`.
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public | Modifiers::Const);
    EXPECT_FALSE(decl->HasModifier(Modifiers::Static));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, StaticNonConstFieldKeepsStatic) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsStatic(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public | Modifiers::Static);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ReadOnlyFieldGainsReadonly) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsReadOnly(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public | Modifiers::Readonly);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, VolatileFieldGainsVolatile) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsVolatile(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public | Modifiers::Volatile);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ConstWinsOverReadOnlyAndVolatile) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    // All three flags set: the if/else-if chain fires only the const arm.
    field.SetIsConst(true);
    field.SetIsReadOnly(true);
    field.SetIsVolatile(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public | Modifiers::Const);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldShowModifiersFalseYieldsNoModifiers) {
    TypeSystemAstBuilder builder;
    builder.ShowModifiers() = false;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::None);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ConstFieldRendersConstantInitializer) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsConst(true);
    field.SetConstantValue(std::int32_t(5));

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Variables().Count(), 1);
    ASSERT_NE(decl->Variables()[0]->Initializer(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveExpression*>(decl->Variables()[0]->Initializer());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(primitive->Value()), 5);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, NonConstFieldRendersNoInitializer) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetConstantValue(std::int32_t(5));

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Variables().Count(), 1);
    EXPECT_EQ(decl->Variables()[0]->Initializer(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ShowConstantValuesFalseSuppressesInitializer) {
    TypeSystemAstBuilder builder;
    builder.ShowConstantValues() = false;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsConst(true);
    field.SetConstantValue(std::int32_t(5));

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Variables().Count(), 1);
    EXPECT_EQ(decl->Variables()[0]->Initializer(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ThrowingConstantRendersErrorExpression) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);
    field.SetIsConst(true);
    field.SetThrowOnConstantValue(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Variables().Count(), 1);
    ASSERT_NE(decl->Variables()[0]->Initializer(), nullptr);
    auto* error = dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::ErrorExpression*>(
        decl->Variables()[0]->Initializer());
    ASSERT_NE(error, nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, RefReadOnlyFieldPromotesReadOnlySpecifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    // A `ref readonly int` field: the return type is the ByReferenceType over
    // the element, rendered as `ref int` (a ComposedType with HasRefSpecifier);
    // the ReturnTypeIsRefReadOnly flag promotes the readonly specifier.
    TestField field("F",
                    std::make_shared<ByReferenceType>(intDef), compilation);
    field.SetReturnTypeIsRefReadOnly(true);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    auto* composed = dynamic_cast<ComposedType*>(decl->ReturnType());
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_TRUE(composed->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderMemberRenderersTest, RefReadOnlyFlagOffLeavesNoReadOnlySpecifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F",
                    std::make_shared<ByReferenceType>(intDef), compilation);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    auto* composed = dynamic_cast<ComposedType*>(decl->ReturnType());
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_FALSE(composed->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldAttributesRenderUnderShowAttributes) {
    TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto attrDef = MakeDef("ObsoleteAttribute", "System", TypeKind::Class, compilation);
    TestAttribute attribute(attrDef);
    TestField field("F", intDef, compilation);
    field.SetAttributes({&attribute});

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Attributes().Count(), 1);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldShowAttributesFalseSkipsAttributes) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto attrDef = MakeDef("ObsoleteAttribute", "System", TypeKind::Class, compilation);
    TestAttribute attribute(attrDef);
    TestField field("F", intDef, compilation);
    field.SetAttributes({&attribute});

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Attributes().Count(), 0);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldAnnotationAttached) {
    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    const MemberResolveResult* annotation = decl->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), &field);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldNoAnnotationWithoutFlag) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);

    std::unique_ptr<FieldDeclaration> decl(builder.ConvertField(field));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Annotation<MemberResolveResult>(), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertProperty
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMemberRenderersTest, PropertyRendersNameTypeAndModifiers) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestProperty property("P", intDef, compilation);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public);
    EXPECT_EQ(decl->Name(), "P");
    ASSERT_NE(decl->ReturnType(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveType*>(decl->ReturnType());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), std::optional<std::string>("int"));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, PropertyGetterSetterRoutedThroughConvertAccessor) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestMethod getter("get_P", compilation);
    getter.SetReturnType(intDef);
    TestMethod setter("set_P", compilation);
    setter.SetReturnType(intDef);
    TestProperty property("P", intDef, compilation);
    property.SetGetter(&getter);
    property.SetSetter(&setter);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->Getter(), nullptr);
    EXPECT_EQ(decl->Getter()->Kind(), ILSpy::Decompiler::CSharp::Syntax::AccessorKind::Getter);
    ASSERT_NE(decl->Setter(), nullptr);
    EXPECT_EQ(decl->Setter()->Kind(), ILSpy::Decompiler::CSharp::Syntax::AccessorKind::Setter);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, GetOnlyPropertyHasNullSetter) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestMethod getter("get_P", compilation);
    getter.SetReturnType(intDef);
    TestProperty property("P", intDef, compilation);
    property.SetGetter(&getter);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->Getter(), nullptr);
    EXPECT_EQ(decl->Setter(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, PropertyExplicitInterfaceTypeRouted) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto ifaceDef = MakeDef("I", "", TypeKind::Interface, compilation);
    // The explicitly-implemented base member: a method on the interface whose
    // declaring type GetExplicitInterfaceType renders.
    TestMethod baseMember("P", compilation);
    baseMember.SetReturnType(intDef);
    baseMember.SetDeclaringType(ifaceDef);
    TestProperty property("P", intDef, compilation);
    property.SetIsExplicitInterfaceImplementation(true);
    property.SetExplicitlyImplementedInterfaceMembers({&baseMember});

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->PrivateImplementationType(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, ImplicitPropertyYieldsNullPrivateType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestProperty property("P", intDef, compilation);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->PrivateImplementationType(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, PropertyBothAccessorsReadonlyHoistsToDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Host", "", TypeKind::Class, compilation);
    // Ref-readonly-this accessors on a NON-readonly declaring type carry the
    // readonly modifier through ConvertAccessor (the HasReadonlyModifier gate);
    // the MergeReadOnlyModifiers hoist then lifts both onto the declaration.
    TestMethod getter("get_P", compilation);
    getter.SetReturnType(intDef);
    getter.SetThisIsRefReadOnly(true);
    getter.SetDeclaringTypeDefinition(hostDef.get());
    TestMethod setter("set_P", compilation);
    setter.SetReturnType(intDef);
    setter.SetThisIsRefReadOnly(true);
    setter.SetDeclaringTypeDefinition(hostDef.get());
    TestProperty property("P", intDef, compilation);
    property.SetGetter(&getter);
    property.SetSetter(&setter);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    EXPECT_TRUE(decl->HasModifier(Modifiers::Readonly));
    ASSERT_NE(decl->Getter(), nullptr);
    ASSERT_NE(decl->Setter(), nullptr);
    EXPECT_FALSE(decl->Getter()->HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(decl->Setter()->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, PropertyRefReadOnlyPromotesReadOnlySpecifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestProperty property("P",
                          std::make_shared<ByReferenceType>(intDef), compilation);
    property.SetReturnTypeIsRefReadOnly(true);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    auto* composed = dynamic_cast<ComposedType*>(decl->ReturnType());
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
    EXPECT_TRUE(composed->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderMemberRenderersTest, PropertyAnnotationAttached) {
    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestProperty property("P", intDef, compilation);

    std::unique_ptr<PropertyDeclaration> decl(builder.ConvertProperty(property));
    ASSERT_NE(decl, nullptr);
    const MemberResolveResult* annotation = decl->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), &property);
}

// ---------------------------------------------------------------------------
// ConvertIndexer
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMemberRenderersTest, IndexerRendersParametersAndAccessors) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestMethod getter("get_Item", compilation);
    getter.SetReturnType(intDef);
    TestMethod setter("set_Item", compilation);
    setter.SetReturnType(intDef);
    TestParameter index(intDef, "i");
    TestProperty indexer("Item", intDef, compilation);
    indexer.SetParameters({&index});
    indexer.SetGetter(&getter);
    indexer.SetSetter(&setter);

    std::unique_ptr<IndexerDeclaration> decl(builder.ConvertIndexer(indexer));
    ASSERT_NE(decl, nullptr);
    ASSERT_EQ(decl->Parameters().Count(), 1);
    EXPECT_EQ(decl->Parameters()[0]->Name(), "i");
    ASSERT_NE(decl->Parameters()[0]->Type(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveType*>(decl->Parameters()[0]->Type());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), std::optional<std::string>("int"));
    ASSERT_NE(decl->Getter(), nullptr);
    EXPECT_EQ(decl->Getter()->Kind(), ILSpy::Decompiler::CSharp::Syntax::AccessorKind::Getter);
    ASSERT_NE(decl->Setter(), nullptr);
    EXPECT_EQ(decl->Setter()->Kind(), ILSpy::Decompiler::CSharp::Syntax::AccessorKind::Setter);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, IndexerHasNoNameIdentifier) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestParameter index(intDef, "i");
    TestProperty indexer("Item", intDef, compilation);
    indexer.SetParameters({&index});

    std::unique_ptr<IndexerDeclaration> decl(builder.ConvertIndexer(indexer));
    ASSERT_NE(decl, nullptr);
    // An indexer names itself `this[...]`: no identifier slot is rendered.
    EXPECT_EQ(decl->NameToken(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, IndexerModifiersAndReturnType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestParameter index(intDef, "i");
    TestProperty indexer("Item", intDef, compilation);
    indexer.SetParameters({&index});

    std::unique_ptr<IndexerDeclaration> decl(builder.ConvertIndexer(indexer));
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Modifiers(), Modifiers::Public);
    ASSERT_NE(decl->ReturnType(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveType*>(decl->ReturnType());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), std::optional<std::string>("int"));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, IndexerExplicitInterfaceTypeRouted) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto ifaceDef = MakeDef("I", "", TypeKind::Interface, compilation);
    TestMethod baseMember("Item", compilation);
    baseMember.SetReturnType(intDef);
    baseMember.SetDeclaringType(ifaceDef);
    TestParameter index(intDef, "i");
    TestProperty indexer("Item", intDef, compilation);
    indexer.SetParameters({&index});
    indexer.SetIsExplicitInterfaceImplementation(true);
    indexer.SetExplicitlyImplementedInterfaceMembers({&baseMember});

    std::unique_ptr<IndexerDeclaration> decl(builder.ConvertIndexer(indexer));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->PrivateImplementationType(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, IndexerAnnotationAttached) {
    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestParameter index(intDef, "i");
    TestProperty indexer("Item", intDef, compilation);
    indexer.SetParameters({&index});

    std::unique_ptr<IndexerDeclaration> decl(builder.ConvertIndexer(indexer));
    ASSERT_NE(decl, nullptr);
    const MemberResolveResult* annotation = decl->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), &indexer);
}

// ---------------------------------------------------------------------------
// ConvertEvent
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderMemberRenderersTest, DefaultUseCustomEventsFalseRendersFieldLikeEvent) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestEvent ev("E", intDef, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEvent(ev));
    ASSERT_NE(decl, nullptr);
    auto* eventDecl = dynamic_cast<EventDeclaration*>(decl.get());
    ASSERT_NE(eventDecl, nullptr);
    ASSERT_EQ(eventDecl->Variables().Count(), 1);
    EXPECT_EQ(eventDecl->Variables()[0]->Name(), "E");
    ASSERT_NE(eventDecl->ReturnType(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveType*>(eventDecl->ReturnType());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), std::optional<std::string>("int"));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, FieldLikeEventHasNoAccessors) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestMethod addAccessor("add_E", compilation);
    addAccessor.SetReturnType(intDef);
    TestMethod removeAccessor("remove_E", compilation);
    removeAccessor.SetReturnType(intDef);
    TestEvent ev("E", intDef, compilation);
    ev.SetAddAccessor(&addAccessor);
    ev.SetRemoveAccessor(&removeAccessor);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEvent(ev));
    ASSERT_NE(decl, nullptr);
    // The default (flag off) arm ignores the accessors: a field-like event
    // carries none.
    auto* eventDecl = dynamic_cast<EventDeclaration*>(decl.get());
    ASSERT_NE(eventDecl, nullptr);
    ASSERT_EQ(eventDecl->Variables().Count(), 1);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, UseCustomEventsRendersCustomEvent) {
    TypeSystemAstBuilder builder;
    builder.UseCustomEvents() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestMethod addAccessor("add_E", compilation);
    addAccessor.SetReturnType(intDef);
    TestMethod removeAccessor("remove_E", compilation);
    removeAccessor.SetReturnType(intDef);
    TestEvent ev("E", intDef, compilation);
    ev.SetAddAccessor(&addAccessor);
    ev.SetRemoveAccessor(&removeAccessor);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEvent(ev));
    ASSERT_NE(decl, nullptr);
    auto* customDecl = dynamic_cast<CustomEventDeclaration*>(decl.get());
    ASSERT_NE(customDecl, nullptr);
    EXPECT_EQ(customDecl->Name(), "E");
    ASSERT_NE(customDecl->ReturnType(), nullptr);
    ASSERT_NE(customDecl->AddAccessor(), nullptr);
    EXPECT_EQ(customDecl->AddAccessor()->Kind(),
              ILSpy::Decompiler::CSharp::Syntax::AccessorKind::Adder);
    ASSERT_NE(customDecl->RemoveAccessor(), nullptr);
    EXPECT_EQ(customDecl->RemoveAccessor()->Kind(),
              ILSpy::Decompiler::CSharp::Syntax::AccessorKind::Remover);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, CustomEventExplicitInterfaceTypeRouted) {
    TypeSystemAstBuilder builder;
    builder.UseCustomEvents() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto ifaceDef = MakeDef("I", "", TypeKind::Interface, compilation);
    TestMethod addAccessor("add_E", compilation);
    addAccessor.SetReturnType(intDef);
    TestMethod removeAccessor("remove_E", compilation);
    removeAccessor.SetReturnType(intDef);
    TestMethod baseMember("E", compilation);
    baseMember.SetReturnType(intDef);
    baseMember.SetDeclaringType(ifaceDef);
    TestEvent ev("E", intDef, compilation);
    ev.SetAddAccessor(&addAccessor);
    ev.SetRemoveAccessor(&removeAccessor);
    ev.SetIsExplicitInterfaceImplementation(true);
    ev.SetExplicitlyImplementedInterfaceMembers({&baseMember});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEvent(ev));
    ASSERT_NE(decl, nullptr);
    auto* customDecl = dynamic_cast<CustomEventDeclaration*>(decl.get());
    ASSERT_NE(customDecl, nullptr);
    ASSERT_NE(customDecl->PrivateImplementationType(), nullptr);
}

TEST(TypeSystemAstBuilderMemberRenderersTest, CustomEventBothAccessorsReadonlyHoists) {
    TypeSystemAstBuilder builder;
    builder.UseCustomEvents() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Host", "", TypeKind::Class, compilation);
    TestMethod addAccessor("add_E", compilation);
    addAccessor.SetReturnType(intDef);
    addAccessor.SetThisIsRefReadOnly(true);
    addAccessor.SetDeclaringTypeDefinition(hostDef.get());
    TestMethod removeAccessor("remove_E", compilation);
    removeAccessor.SetReturnType(intDef);
    removeAccessor.SetThisIsRefReadOnly(true);
    removeAccessor.SetDeclaringTypeDefinition(hostDef.get());
    TestEvent ev("E", intDef, compilation);
    ev.SetAddAccessor(&addAccessor);
    ev.SetRemoveAccessor(&removeAccessor);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEvent(ev));
    ASSERT_NE(decl, nullptr);
    auto* customDecl = dynamic_cast<CustomEventDeclaration*>(decl.get());
    ASSERT_NE(customDecl, nullptr);
    EXPECT_TRUE(customDecl->HasModifier(Modifiers::Readonly));
    ASSERT_NE(customDecl->AddAccessor(), nullptr);
    ASSERT_NE(customDecl->RemoveAccessor(), nullptr);
    EXPECT_FALSE(customDecl->AddAccessor()->HasModifier(Modifiers::Readonly));
    EXPECT_FALSE(customDecl->RemoveAccessor()->HasModifier(Modifiers::Readonly));
}

TEST(TypeSystemAstBuilderMemberRenderersTest, EventAnnotationAttached) {
    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestEvent ev("E", intDef, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEvent(ev));
    ASSERT_NE(decl, nullptr);
    const MemberResolveResult* annotation = decl->Annotation<MemberResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Member(), &ev);
}
