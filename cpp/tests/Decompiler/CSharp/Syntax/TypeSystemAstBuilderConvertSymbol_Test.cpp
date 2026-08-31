// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
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

// Tests for the TypeSystemAstBuilder "Convert Entity" dispatch entries
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 1829-1901 + 2766-2769: ConvertSymbol +
// ConvertEntity + ConvertExtension + the private ConvertNamespaceDeclaration --
// the public whole-symbol / per-entity / C#-14-extension-block entries composing
// every renderer landed in the prior slices, plus the NamespaceDeclaration
// split-on-dots `(string)` ctor the namespace arm constructs through).
//
// The load-bearing cruxes:
//  (a) ConvertEntity's Accessor special case: an accessor of a PARAMETERIZED
//      property (SymbolKind::Property WITH parameters) renders as an ordinary
//      MethodDeclaration (C# cannot represent the parameterized property
//      itself), while an accessor of a plain property (or a null
//      AccessorOwner) renders through ConvertAccessor with the owner's
//      accessibility (Accessibility.None for the null owner, the C# `?.` + `??
//      ` chain);
//  (b) ConvertExtension's specialization wiring: `new
//      TypeParameterSubstitution(group.TypeParameters, [])` re-points the marker
//      method's CLASS-owned type-parameter references at the group's freshly
//      declared type parameters, so the rendered ReceiverParameter carries the
//      GROUP's type parameter (not the marker's container-owned one) -- pinned
//      by the rendered SimpleType name;
//  (c) ConvertExtension's `.Single()` throws on anything but exactly one
//      marker parameter (the InvalidOperationException analog), and its
//      `.OfType<Constraint>()` filters the null ConvertTypeParameterConstraint
//      results;
//  (d) ConvertSymbol's `symbol as IEntity` default arm (a non-entity symbol
//      throws) and the per-arm hard casts.
//
// The stub shapes: TestMemberMethod models an IMethod with the configurable
// SymbolKind + accessor triple the ConvertEntity dispatch reads (the
// iteration-127 TestMethod pattern extended with SetSymbolKind /
// SetAccessorOwner / SetAccessorKind); MarkerMethod extends it with a REAL
// SpecializedMethod-returning Specialize (the full-fidelity route: the marker
// parameter types run through the composed substitution exactly as the C#
// metadata path does, via the aliasing self-handle the SpecializedMethod ctor
// needs); TestNamespace / TestSymbol / TestVariable / TestParameter /
// TestField / TestProperty / TestEvent carry the established per-file stub
// surfaces; VisitableTypeParameter adds the AcceptVisitor ->
// VisitTypeParameter bridge the substitution path requires (the plain
// LookupTypeParameter routes to VisitOtherType, the D564 precedent), and
// ClassOwnedTypeParameter overrides OwnerType to TypeDefinition (the marker
// parameter's container-owned type-parameter shape).

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ExtensionDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/IndexerDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"

#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::Accessor;
using ILSpy::Decompiler::CSharp::Syntax::AccessorKind;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::Constraint;
using ILSpy::Decompiler::CSharp::Syntax::ConstructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::DestructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EntityDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EventDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::ExtensionDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::FieldDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::IndexerDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::MethodDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::NamespaceDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::OperatorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::OperatorType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::PropertyDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::TypeDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::TypeParameterDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEvent;
using ILSpy::Decompiler::TypeSystem::IField;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::ISymbol;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
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

// A `LookupTypeParameter` overriding `AcceptVisitor` to dispatch to
// `visitor.VisitTypeParameter` (the D564 `VisitableTypeParameter` precedent) --
// the plain `LookupTypeParameter` routes to `VisitOtherType`, so the
// TypeParameterSubstitution would never fire for it.
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(TS::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A type parameter owned by a TYPE DEFINITION (the marker method's parameter
// type shape: the extension container owns the type parameter the receiver
// references, and the ConvertExtension substitution -- classTypeArguments = the
// group's type parameters -- re-points it at the group's freshly declared
// one). The shared `LookupTypeParameter` hardcodes `OwnerType == Method`.
class ClassOwnedTypeParameter : public VisitableTypeParameter {
public:
    using VisitableTypeParameter::VisitableTypeParameter;
    TS::SymbolKind OwnerType() const override { return TS::SymbolKind::TypeDefinition; }
};

// A minimal `INamespace` stub (the ConvertSymbol `Namespace` arm and the
// ConvertNamespaceDeclaration helper read only `SymbolKind` and `FullName`).
class TestNamespace : public INamespace {
public:
    explicit TestNamespace(std::string fullName) : fullName_(std::move(fullName)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return fullName_; }
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return fullName_; }
    const ICompilation& Compilation() const override { return compilation_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return {}; }
    std::vector<const ITypeDefinition*> Types() const override { return {}; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules()
        const override {
        return {};
    }
    const INamespace* GetChildNamespace(const std::string&) const override {
        return nullptr;
    }
    const ITypeDefinition* GetTypeDefinition(const std::string&, int) const override {
        return nullptr;
    }

private:
    std::string fullName_;
    LookupCompilation compilation_;
};

// A plain `ISymbol` that is NOT an `IEntity` (the ConvertSymbol default arm's
// throw shape: `SymbolKind::Module` is outside the four special arms and a
// module is not an entity).
class TestModuleSymbol : public ISymbol {
public:
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Module; }
    std::string Name() const override { return "mod"; }
};

// A minimal `IVariable` stub (the ConvertSymbol `Variable` arm reads the name
// and type through ConvertVariable).
class TestVariable : public IVariable {
public:
    TestVariable(std::string name, ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return {}; }

private:
    std::string name_;
    ITypePtr type_;
};

// A configurable `IParameter` stub (the iteration-128 TestParameter pattern).
class TestParameter : public IParameter {
public:
    explicit TestParameter(TS::ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return {}; }
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

// A configurable `IField` stub (the iteration-128 TestField pattern).
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

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
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
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
    const IType& Type() const override { return *returnType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return {}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
};

// A configurable `IProperty` stub with a CONFIGURABLE SymbolKind (the
// ConvertEntity dispatch distinguishes `Property` from `Indexer`; the
// parameterized-property accessor crux additionally needs a `Property` WITH
// parameters -- `IsParameterizedProperty` is SymbolKind::Property + a non-empty
// parameter list, NOT the SymbolKind::Indexer shape).
class TestProperty : public IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    void SetSymbolKind(TS::SymbolKind k) { kind_ = k; }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }

    TS::SymbolKind SymbolKind() const override { return kind_; }
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
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
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
    std::vector<const IParameter*> Parameters() const override { return parameters_; }
    bool CanGet() const override { return false; }
    bool CanSet() const override { return false; }
    const IMethod* Getter() const override { return nullptr; }
    const IMethod* Setter() const override { return nullptr; }
    bool IsIndexer() const override { return !parameters_.empty(); }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    TS::SymbolKind kind_ = TS::SymbolKind::Property;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    std::vector<const IParameter*> parameters_;
};

// A minimal `IEvent` stub (the iteration-128 TestEvent pattern).
class TestEvent : public IEvent {
public:
    TestEvent(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Event; }
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
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
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
    bool CanAdd() const override { return false; }
    bool CanRemove() const override { return false; }
    bool CanInvoke() const override { return false; }
    const IMethod* AddAccessor() const override { return nullptr; }
    const IMethod* RemoveAccessor() const override { return nullptr; }
    const IMethod* InvokeAccessor() const override { return nullptr; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
};

// A configurable `IMethod` stub (the iteration-127 TestMethod pattern extended
// with the dispatch surface: the configurable SymbolKind, and the
// IsAccessor/AccessorOwner/AccessorKind triple the ConvertEntity `Accessor`
// arm reads). The return type must be shared-managed (the MemberResolveResult
// ComputeType path calls shared_from_this over it) for any test reaching an
// annotated renderer.
class TestMemberMethod : public IMethod {
public:
    TestMemberMethod(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetSymbolKind(TS::SymbolKind k) { kind_ = k; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetIsAccessor(bool v) { isAccessor_ = v; }
    void SetAccessorOwner(const IMember* owner) { accessorOwner_ = owner; }
    void SetAccessorKind(TS::MethodSemanticsAttributes k) { accessorKind_ = k; }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) {
        declaringTypeDefinition_ = d;
    }
    void SetReturnType(ITypePtr t) { returnTypeOverride_ = std::move(t); }

    TS::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return declaringTypeDefinition_;
    }
    ITypePtr DeclaringType() const override { return {}; }
    const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
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
    std::vector<const IParameter*> Parameters() const override { return parameters_; }
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return {}; }
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return isAccessor_; }
    const IMember* AccessorOwner() const override { return accessorOwner_; }
    TS::MethodSemanticsAttributes AccessorKind() const override { return accessorKind_; }
    const IMethod* ReducedFrom() const override { return nullptr; }

private:
    std::string name_;
    const ICompilation& compilation_;
    TS::KnownType returnType_{ KnownTypeCode::Object };
    ITypePtr returnTypeOverride_;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    TS::SymbolKind kind_ = TS::SymbolKind::Method;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    bool isAccessor_ = false;
    const IMember* accessorOwner_ = nullptr;
    TS::MethodSemanticsAttributes accessorKind_ = TS::MethodSemanticsAttributes::None;
    std::vector<const IParameter*> parameters_;
};

// The extension MARKER METHOD stub: a `TestMemberMethod` whose `Specialize`
// builds a REAL `SpecializedMethod` over the passed substitution (the
// full-fidelity route -- the parameter types run through the composed
// substitution exactly as the C# metadata path does). The `SpecializedMethod`
// ctor takes an OWNING `shared_ptr<IMethod>` of the definition; `ISymbol`/
// `IMethod` carry no `shared_from_this` (only `IType` does, the D406
// convention), so the stub holds an aliasing self-handle installed by the test
// after `make_shared` (the cycle is harmless under the tests' scoped
// lifetimes; the handle co-owns the same control block).
class MarkerMethod : public TestMemberMethod {
public:
    using TestMemberMethod::TestMemberMethod;

    void SetSelf(std::shared_ptr<const IMethod> self) { self_ = std::move(self); }

    const IMethod* Specialize(const TypeParameterSubstitution* substitution) const override {
        if (substitution == nullptr)
            return this;
        specialized_.push_back(std::make_shared<TS::Implementation::SpecializedMethod>(
            std::const_pointer_cast<IMethod>(self_), *substitution));
        return specialized_.back().get();
    }

private:
    std::shared_ptr<const IMethod> self_;
    // The specialized results are kept alive here (the raw-pointer
    // `IMethod::Specialize` return is non-owning, "the type system owns it" --
    // the stub is the ownership stand-in for the scope of the test).
    mutable std::vector<std::shared_ptr<TS::Implementation::SpecializedMethod>> specialized_;
};

// Flattens a `NamespaceName` chain into its dotted string (the test-side
// reverse of the split-on-dots ctor; walks `MemberType::Target` down to the
// `SimpleType` head). `MemberType::MemberName()` returns the string directly
// (not an optional); `SimpleType::Identifier()` is the optional accessor.
std::string FlattenNamespaceName(const AstNode* node) {
    std::vector<std::string> parts;
    const AstNode* current = node;
    while (const auto* memberType = dynamic_cast<const MemberType*>(current)) {
        parts.push_back(memberType->MemberName());
        current = memberType->Target();
    }
    if (const auto* simpleType = dynamic_cast<const SimpleType*>(current)) {
        parts.push_back(simpleType->Identifier().value_or(std::string()));
    }
    std::string result;
    for (std::size_t i = parts.size(); i-- > 0;) {
        if (!result.empty())
            result += ".";
        result += parts[i];
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// ConvertSymbol (C# line 1829)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertSymbolTest, NamespaceDispatchesToNamespaceDeclaration) {
    TypeSystemAstBuilder builder;
    TestNamespace ns("A.B.C");

    std::unique_ptr<AstNode> node(builder.ConvertSymbol(ns));
    ASSERT_NE(node, nullptr);
    auto* decl = dynamic_cast<NamespaceDeclaration*>(node.get());
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->NamespaceName(), nullptr);
    // The split-on-dots chain: MemberType(MemberType(SimpleType(A), B), C).
    EXPECT_EQ(FlattenNamespaceName(decl->NamespaceName()), "A.B.C");
}

TEST(TypeSystemAstBuilderConvertSymbolTest, VariableDispatchesToVariableDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestVariable variable("x", intDef);

    std::unique_ptr<AstNode> node(builder.ConvertSymbol(variable));
    ASSERT_NE(node, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::VariableDeclarationStatement*>(
                  node.get()),
              nullptr);
}

TEST(TypeSystemAstBuilderConvertSymbolTest, ParameterDispatchesToParameterDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestParameter parameter(intDef, "p");

    std::unique_ptr<AstNode> node(builder.ConvertSymbol(parameter));
    ASSERT_NE(node, nullptr);
    auto* decl =
        dynamic_cast<ILSpy::Decompiler::CSharp::Syntax::ParameterDeclaration*>(node.get());
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Name(), "p");
}

TEST(TypeSystemAstBuilderConvertSymbolTest, TypeParameterDispatchesToTypeParameterDeclaration) {
    TypeSystemAstBuilder builder;
    auto tp = std::make_shared<LookupTypeParameter>("T");

    std::unique_ptr<AstNode> node(builder.ConvertSymbol(*tp));
    ASSERT_NE(node, nullptr);
    auto* decl = dynamic_cast<TypeParameterDeclaration*>(node.get());
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Name(), "T");
}

TEST(TypeSystemAstBuilderConvertSymbolTest, EntityDispatchesThroughConvertEntity) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto method = std::make_shared<TestMemberMethod>("M", compilation);
    method->SetReturnType(intDef);

    std::unique_ptr<AstNode> node(builder.ConvertSymbol(*method));
    ASSERT_NE(node, nullptr);
    auto* decl = dynamic_cast<MethodDeclaration*>(node.get());
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->Name(), "M");
}

TEST(TypeSystemAstBuilderConvertSymbolTest, NonEntitySymbolThrows) {
    TypeSystemAstBuilder builder;
    TestModuleSymbol moduleSymbol;
    EXPECT_THROW(builder.ConvertSymbol(moduleSymbol), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// ConvertEntity (C# line 1851)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertEntityTest, TypeDefinitionDispatchesToTypeDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto def = MakeDef("MyClass", "", TypeKind::Class, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*def));
    ASSERT_NE(decl, nullptr);
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_EQ(typeDecl->Name(), "MyClass");
    EXPECT_EQ(typeDecl->ClassType(),
              ILSpy::Decompiler::CSharp::Syntax::ClassType::Class);
}

TEST(TypeSystemAstBuilderConvertEntityTest, FieldDispatchesToFieldDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestField field("F", intDef, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(field));
    ASSERT_NE(decl, nullptr);
    auto* fieldDecl = dynamic_cast<FieldDeclaration*>(decl.get());
    ASSERT_NE(fieldDecl, nullptr);
    ASSERT_EQ(fieldDecl->Variables().Count(), 1);
    EXPECT_EQ(fieldDecl->Variables()[0]->Name(), "F");
}

TEST(TypeSystemAstBuilderConvertEntityTest, PropertyDispatchesToPropertyDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestProperty property("P", intDef, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(property));
    ASSERT_NE(decl, nullptr);
    auto* propertyDecl = dynamic_cast<PropertyDeclaration*>(decl.get());
    ASSERT_NE(propertyDecl, nullptr);
    EXPECT_EQ(propertyDecl->Name(), "P");
}

TEST(TypeSystemAstBuilderConvertEntityTest, IndexerDispatchesToIndexerDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto index = std::make_shared<TestParameter>(intDef, "index");
    TestProperty indexer("Item", intDef, compilation);
    indexer.SetSymbolKind(TS::SymbolKind::Indexer);
    indexer.SetParameters({index.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(indexer));
    ASSERT_NE(decl, nullptr);
    auto* indexerDecl = dynamic_cast<IndexerDeclaration*>(decl.get());
    ASSERT_NE(indexerDecl, nullptr);
    // An indexer names itself `this[...]`: no identifier token.
    EXPECT_EQ(indexerDecl->NameToken(), nullptr);
}

TEST(TypeSystemAstBuilderConvertEntityTest, EventDispatchesToEventDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    TestEvent ev("E", intDef, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(ev));
    ASSERT_NE(decl, nullptr);
    auto* eventDecl = dynamic_cast<EventDeclaration*>(decl.get());
    ASSERT_NE(eventDecl, nullptr);
    ASSERT_EQ(eventDecl->Variables().Count(), 1);
    EXPECT_EQ(eventDecl->Variables()[0]->Name(), "E");
}

TEST(TypeSystemAstBuilderConvertEntityTest, MethodDispatchesToMethodDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto method = std::make_shared<TestMemberMethod>("M", compilation);
    method->SetReturnType(intDef);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*method));
    ASSERT_NE(decl, nullptr);
    auto* methodDecl = dynamic_cast<MethodDeclaration*>(decl.get());
    ASSERT_NE(methodDecl, nullptr);
    EXPECT_EQ(methodDecl->Name(), "M");
}

TEST(TypeSystemAstBuilderConvertEntityTest, OperatorDispatchesToOperatorDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto left = std::make_shared<TestParameter>(intDef, "left");
    auto right = std::make_shared<TestParameter>(intDef, "right");
    auto op = std::make_shared<TestMemberMethod>("op_Addition", compilation);
    op->SetSymbolKind(TS::SymbolKind::Operator);
    op->SetReturnType(intDef);
    op->SetParameters({left.get(), right.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*op));
    ASSERT_NE(decl, nullptr);
    auto* operatorDecl = dynamic_cast<OperatorDeclaration*>(decl.get());
    ASSERT_NE(operatorDecl, nullptr);
    EXPECT_EQ(operatorDecl->OperatorType(), OperatorType::Addition);
}

TEST(TypeSystemAstBuilderConvertEntityTest, ConstructorDispatchesToConstructorDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);
    auto ctor = std::make_shared<TestMemberMethod>(".ctor", compilation);
    ctor->SetSymbolKind(TS::SymbolKind::Constructor);
    ctor->SetReturnType(intDef);
    ctor->SetDeclaringTypeDefinition(hostDef.get());

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*ctor));
    ASSERT_NE(decl, nullptr);
    auto* ctorDecl = dynamic_cast<ConstructorDeclaration*>(decl.get());
    ASSERT_NE(ctorDecl, nullptr);
    EXPECT_EQ(ctorDecl->Name(), "Foo");
}

TEST(TypeSystemAstBuilderConvertEntityTest, DestructorDispatchesToDestructorDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto hostDef = MakeDef("Foo", "", TypeKind::Class, compilation);
    auto dtor = std::make_shared<TestMemberMethod>("Finalize", compilation);
    dtor->SetSymbolKind(TS::SymbolKind::Destructor);
    dtor->SetDeclaringTypeDefinition(hostDef.get());

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*dtor));
    ASSERT_NE(decl, nullptr);
    auto* dtorDecl = dynamic_cast<DestructorDeclaration*>(decl.get());
    ASSERT_NE(dtorDecl, nullptr);
    EXPECT_EQ(dtorDecl->Name(), "Foo");
}

TEST(TypeSystemAstBuilderConvertEntityTest,
     AccessorOfParameterizedPropertyDispatchesToMethodDeclaration) {
    // The load-bearing crux: C# cannot represent a PARAMETERIZED property
    // (SymbolKind::Property WITH parameters -- not the SymbolKind::Indexer
    // shape), so its accessors are declared as ordinary methods.
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto index = std::make_shared<TestParameter>(intDef, "index");
    auto property = std::make_shared<TestProperty>("P", intDef, compilation);
    property->SetParameters({index.get()}); // parameterized, SymbolKind stays Property

    auto accessor = std::make_shared<TestMemberMethod>("get_P", compilation);
    accessor->SetSymbolKind(TS::SymbolKind::Accessor);
    accessor->SetIsAccessor(true);
    accessor->SetAccessorOwner(property.get());
    accessor->SetAccessorKind(TS::MethodSemanticsAttributes::Getter);
    accessor->SetReturnType(intDef);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*accessor));
    ASSERT_NE(decl, nullptr);
    auto* methodDecl = dynamic_cast<MethodDeclaration*>(decl.get());
    ASSERT_NE(methodDecl, nullptr);
    EXPECT_EQ(methodDecl->Name(), "get_P");
}

TEST(TypeSystemAstBuilderConvertEntityTest,
     AccessorOfPlainPropertyDispatchesToAccessorNode) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);
    auto property = std::make_shared<TestProperty>("P", intDef, compilation);

    auto accessor = std::make_shared<TestMemberMethod>("get_P", compilation);
    accessor->SetSymbolKind(TS::SymbolKind::Accessor);
    accessor->SetIsAccessor(true);
    accessor->SetAccessorOwner(property.get());
    accessor->SetAccessorKind(TS::MethodSemanticsAttributes::Getter);
    accessor->SetReturnType(intDef);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*accessor));
    ASSERT_NE(decl, nullptr);
    auto* accessorNode = dynamic_cast<Accessor*>(decl.get());
    ASSERT_NE(accessorNode, nullptr);
    EXPECT_EQ(accessorNode->Kind(), AccessorKind::Getter);
}

TEST(TypeSystemAstBuilderConvertEntityTest,
     AccessorWithoutOwnerUsesNoneOwnerAccessibility) {
    // The C# `accessor.AccessorOwner?.Accessibility ?? Accessibility.None`:
    // a null owner yields the None accessibility, so a Public accessor's
    // modifier differs and renders.
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto accessor = std::make_shared<TestMemberMethod>("get_P", compilation);
    accessor->SetSymbolKind(TS::SymbolKind::Accessor);
    accessor->SetIsAccessor(true);
    accessor->SetAccessorOwner(nullptr);
    accessor->SetAccessorKind(TS::MethodSemanticsAttributes::Getter);
    accessor->SetAccessibility(TS::Accessibility::Public);
    accessor->SetReturnType(intDef);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertEntity(*accessor));
    ASSERT_NE(decl, nullptr);
    auto* accessorNode = dynamic_cast<Accessor*>(decl.get());
    ASSERT_NE(accessorNode, nullptr);
    EXPECT_TRUE(accessorNode->HasModifier(Modifiers::Public));
}

TEST(TypeSystemAstBuilderConvertEntityTest, InvalidEntityKindThrows) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    // A degenerate entity shape: SymbolKind::Variable on an IEntity (a real
    // entity never reports it -- the C# default arm's ArgumentException).
    auto symbol = std::make_shared<TestMemberMethod>("x", compilation);
    symbol->SetSymbolKind(TS::SymbolKind::Variable);

    EXPECT_THROW(builder.ConvertEntity(*symbol), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// ConvertExtension (C# line 1890)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertExtensionTest,
     SubstitutesTheMarkerParameterThroughTheGroupTypeParameters) {
    // The flagship crux: the marker method's single parameter references a
    // CLASS-owned type parameter (the extension container's); the
    // TypeParameterSubstitution(group.TypeParameters, []) specialization
    // re-points it at the group's freshly declared type parameter, so the
    // rendered ReceiverParameter carries the GROUP's parameter (rendered as
    // the SimpleType "T", not the container-owned "Marker").
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto markerTp = std::make_shared<ClassOwnedTypeParameter>("Marker");
    markerTp->SetIndex(0);
    auto receiver = std::make_shared<TestParameter>(markerTp, "value");
    auto marker = std::make_shared<MarkerMethod>("<get_Marker>", compilation);
    marker->SetParameters({receiver.get()});
    marker->SetSelf(marker);

    auto groupTp = std::make_shared<VisitableTypeParameter>("T");
    TypeSystemAstBuilder::ExtensionGroup group(marker.get(), {groupTp.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertExtension(group));
    ASSERT_NE(decl, nullptr);
    auto* ext = dynamic_cast<ExtensionDeclaration*>(decl.get());
    ASSERT_NE(ext, nullptr);

    ASSERT_EQ(ext->TypeParameters().Count(), 1);
    EXPECT_EQ(ext->TypeParameters().At(0)->Name(), "T");

    ASSERT_EQ(ext->ReceiverParameters().Count(), 1);
    auto* receiverDecl = ext->ReceiverParameters().At(0);
    ASSERT_NE(receiverDecl, nullptr);
    EXPECT_EQ(receiverDecl->Name(), "value");
    ASSERT_NE(receiverDecl->Type(), nullptr);
    auto* simple = dynamic_cast<SimpleType*>(receiverDecl->Type());
    ASSERT_NE(simple, nullptr);
    // The substituted type is the GROUP's type parameter, not "Marker".
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("T"));

    EXPECT_EQ(ext->Constraints().Count(), 0);
}

TEST(TypeSystemAstBuilderConvertExtensionTest, NoTypeParametersYieldsBareReceiver) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeIntDef(compilation);

    auto receiver = std::make_shared<TestParameter>(intDef, "value");
    auto marker = std::make_shared<MarkerMethod>("<get_Marker>", compilation);
    marker->SetParameters({receiver.get()});
    marker->SetSelf(marker);

    TypeSystemAstBuilder::ExtensionGroup group(marker.get(), {});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertExtension(group));
    ASSERT_NE(decl, nullptr);
    auto* ext = dynamic_cast<ExtensionDeclaration*>(decl.get());
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext->TypeParameters().Count(), 0);
    ASSERT_EQ(ext->ReceiverParameters().Count(), 1);
    auto* receiverDecl = ext->ReceiverParameters().At(0);
    ASSERT_NE(receiverDecl, nullptr);
    ASSERT_NE(receiverDecl->Type(), nullptr);
    auto* primitive = dynamic_cast<PrimitiveType*>(receiverDecl->Type());
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), std::optional<std::string>("int"));
    EXPECT_EQ(ext->Constraints().Count(), 0);
}

TEST(TypeSystemAstBuilderConvertExtensionTest, ConstrainedTypeParametersRenderConstraints) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto receiver = std::make_shared<TestParameter>(
        std::make_shared<TS::KnownType>(KnownTypeCode::Int32), "value");
    auto marker = std::make_shared<MarkerMethod>("<get_Marker>", compilation);
    marker->SetParameters({receiver.get()});
    marker->SetSelf(marker);

    auto groupTp = std::make_shared<VisitableTypeParameter>("T");
    groupTp->SetHasReferenceTypeConstraint(true);
    TypeSystemAstBuilder::ExtensionGroup group(marker.get(), {groupTp.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertExtension(group));
    ASSERT_NE(decl, nullptr);
    auto* ext = dynamic_cast<ExtensionDeclaration*>(decl.get());
    ASSERT_NE(ext, nullptr);
    ASSERT_EQ(ext->Constraints().Count(), 1);
    auto* constraint = ext->Constraints().At(0);
    ASSERT_NE(constraint, nullptr);
    ASSERT_EQ(constraint->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(constraint->BaseTypes().At(0));
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), std::optional<std::string>("class"));
}

TEST(TypeSystemAstBuilderConvertExtensionTest, NotExactlyOneMarkerParameterThrows) {
    // The C# `.Single()` throws InvalidOperationException on anything but
    // exactly one entry (ported as std::runtime_error).
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;

    auto marker = std::make_shared<MarkerMethod>("<get_Marker>", compilation);
    marker->SetParameters({}); // zero parameters
    marker->SetSelf(marker);

    TypeSystemAstBuilder::ExtensionGroup group(marker.get(), {});
    EXPECT_THROW(builder.ConvertExtension(group), std::runtime_error);
}

// ---------------------------------------------------------------------------
// ConvertNamespaceDeclaration (C# line 2766)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertNamespaceDeclarationTest, BuildsTheDottedNameChain) {
    TypeSystemAstBuilder builder;
    TestNamespace ns("A.B.C");

    std::unique_ptr<NamespaceDeclaration> decl(builder.ConvertNamespaceDeclaration(ns));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->NamespaceName(), nullptr);
    auto* outer = dynamic_cast<MemberType*>(decl->NamespaceName());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->MemberName(), "C");
    auto* middle = dynamic_cast<MemberType*>(outer->Target());
    ASSERT_NE(middle, nullptr);
    EXPECT_EQ(middle->MemberName(), "B");
    auto* head = dynamic_cast<SimpleType*>(middle->Target());
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(head->Identifier(), std::optional<std::string>("A"));
}

TEST(TypeSystemAstBuilderConvertNamespaceDeclarationTest, SinglePartNameYieldsSimpleType) {
    TypeSystemAstBuilder builder;
    TestNamespace ns("System");

    std::unique_ptr<NamespaceDeclaration> decl(builder.ConvertNamespaceDeclaration(ns));
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->NamespaceName(), nullptr);
    auto* simple = dynamic_cast<SimpleType*>(decl->NamespaceName());
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(simple->Identifier(), std::optional<std::string>("System"));
}
