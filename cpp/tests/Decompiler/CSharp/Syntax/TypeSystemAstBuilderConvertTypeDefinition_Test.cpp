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

// Tests for the TypeSystemAstBuilder "Convert Entity" type-definition renderers
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 1900-2141: the base-list nameability check
// BaseTypeAccessibleFrom + TypeDefinitionNameableInBaseList + the private
// BaseListNameabilityVisitor, the delegate renderer ConvertDelegate, and the
// type-definition renderer ConvertTypeDefinition) plus the AsParameterizedType
// TypeSystemExtensions prerequisite (TypeSystemExtensions.cs line 864).
//
// The load-bearing cruxes:
//  (a) TypeDefinitionNameableInBaseList: the own-nested / enclosing-type chain
//      arm fires BEFORE the IsAccessible fallback (a type may name its own
//      nested types regardless of accessibility), an inaccessible top-level
//      type is not nameable, and the recursion requires the ENCLOSING type to
//      be nameable too (naming 'A.I' also requires 'A');
//  (b) BaseTypeAccessibleFrom: the visitor walks the named type AND its nested
//      type arguments (IWrap<Hidden.IFoo> is not nameable when Hidden.IFoo
//      is not), and the C# `&=` is non-short-circuit;
//  (c) ConvertDelegate: the delegate-kind dispatch in ConvertTypeDefinition
//      (a DelegateDeclaration, not a TypeDeclaration), the Sealed bit clear,
//      the own + `[return: ...]` attribute-section pair (the delegate's own
//      attributes come from the DEFINITION, the return attributes from the
//      invoke method), the ref-readonly promotion onto the rendered
//      ComposedType, the outer-count type-parameter skip, and the parameters;
//  (d) ConvertTypeDefinition: the static-wins-over-abstract/sealed modifier
//      chain, the struct/enum/interface bit clears (a struct is never
//      `sealed`, an interface never `abstract`), the readonly/ref struct bits,
//      the record-class/record-struct shapes under their flags, the
//      enum-underlying base substitution (System.Enum renders the underlying
//      type unless it is int), the ValueType/Object base skips, the
//      interface-not-nameable DROP (a base class was always written explicitly
//      and STAYS even when not nameable -- the kind gate), and the record
//      IEquatable<R> base omission.
//
// The stub shapes: a local VisitableTypeDef (the AcceptVisitor ->
// VisitTypeDefinition bridge -- a plain LookupTypeDefinition routes to the
// VisitOtherType default so the nameability visitor never sees it, the D514
// trap), a DelegateHostType with a configurable GetMethods (the
// GetDelegateInvokeMethod filter surface), the shared LookupMethod with the
// new SetReturnTypeAttributes additive setter, and PlainAttribute (the
// ConvertParameter_Test minimal IAttribute shape).

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/DelegateDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"

#include "Decompiler/Semantics/TypeResolveResult.hpp"

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::DelegateDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::EntityDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::TypeDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

std::shared_ptr<LookupTypeDefinition> MakeDef(
    const std::string& name, const std::string& ns, TS::TypeKind kind,
    const ICompilation& compilation,
    TS::Accessibility accessibility = TS::Accessibility::Public,
    TS::KnownTypeCode knownTypeCode = TS::KnownTypeCode::None,
    int typeParameterCount = 0) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, typeParameterCount)),
        kind, accessibility, compilation, nullptr, knownTypeCode);
}

// A LookupTypeDefinition with the AcceptVisitor -> VisitTypeDefinition bridge
// (the D514 VisitableDefinition pattern): a plain LookupTypeDefinition routes
// to the IType VisitOtherType default, so the BaseListNameabilityVisitor's
// VisitTypeDefinition override never fires for it.
class VisitableTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    ITypePtr AcceptVisitor(TS::TypeVisitor& visitor) override {
        return visitor.VisitTypeDefinition(*this);
    }
};

std::shared_ptr<VisitableTypeDef> MakeVisitable(
    const std::string& name, const std::string& ns, TS::TypeKind kind,
    const ICompilation& compilation,
    TS::Accessibility accessibility = TS::Accessibility::Public,
    int typeParameterCount = 0) {
    return std::make_shared<VisitableTypeDef>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, typeParameterCount)),
        kind, accessibility, compilation, nullptr);
}

// A LookupTypeDefinition with a configurable GetMethods (the GetDelegateInvokeMethod
// filter surface -- the D533 MethodHostType pattern), for the delegate-kind dispatch.
class DelegateHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetMethodsForScan(std::vector<const IMethod*> m) { methods_ = std::move(m); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override {
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

// A minimal `IParameter` stub (the ConvertParameter_Test TestParameter pattern).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any(); }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    TS::ReferenceKind ReferenceKind() const override { return TS::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const TS::IParameterizedMember* Owner() const override { return nullptr; }
    TS::LifetimeAnnotation Lifetime() const override { return TS::LifetimeAnnotation(); }

private:
    std::string name_;
    ITypePtr type_;
};

// A minimal `IAttribute` stub (the ConvertParameter_Test PlainAttribute
// pattern): a type, no fixed / named arguments, no decode errors, no ctor.
class PlainAttribute : public IAttribute {
public:
    explicit PlainAttribute(ITypePtr attributeType)
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

bool HasBit(Modifiers modifiers, Modifiers bit) {
    return (modifiers & bit) != Modifiers::None;
}

} // namespace

// ---------------------------------------------------------------------------
// AsParameterizedType (TypeSystemExtensions.cs line 864)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AsParameterizedTypeNonGenericReturnsDefinition) {
    LookupCompilation compilation;
    auto td = MakeDef("Plain", "", TypeKind::Class, compilation);

    ITypePtr result = TS::AsParameterizedType(*td);
    ASSERT_NE(result, nullptr);
    // The C# `return td;` -- the definition itself (pointer identity).
    EXPECT_EQ(result.get(), td.get());
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AsParameterizedTypeGenericBuildsSelfParameterizedType) {
    LookupCompilation compilation;
    auto td = MakeDef("Pair", "", TypeKind::Class, compilation,
                     Accessibility::Public, KnownTypeCode::None, /*typeParameterCount=*/2);
    auto tp0 = std::make_shared<LookupTypeParameter>("T1");
    auto tp1 = std::make_shared<LookupTypeParameter>("T2");
    td->SetTypeParameters({tp0.get(), tp1.get()});

    ITypePtr result = TS::AsParameterizedType(*td);
    ASSERT_NE(result, nullptr);
    auto* pt = dynamic_cast<TS::ParameterizedType*>(result.get());
    ASSERT_NE(pt, nullptr);
    // The self-parameterized type: the definition as the generic...
    EXPECT_EQ(pt->GenericType().get(), td.get());
    // ...and its OWN type parameters as the type arguments (pointer identity).
    ASSERT_EQ(pt->TypeArguments().size(), static_cast<size_t>(2));
    EXPECT_EQ(pt->TypeArguments()[0].get(), tp0.get());
    EXPECT_EQ(pt->TypeArguments()[1].get(), tp1.get());
}

// ---------------------------------------------------------------------------
// TypeDefinitionNameableInBaseList (C# line 2072)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, NullTypeDefinitionIsNameable) {
    LookupCompilation compilation;
    auto current = MakeDef("Current", "", TypeKind::Class, compilation);
    MemberLookup lookup(current.get(), nullptr);

    // The recursion's base case -- the C# `if (td == null) return true;`.
    EXPECT_TRUE(TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
        nullptr, *current, lookup));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, OwnNestedTypeIsNameableRegardlessOfAccessibility) {
    LookupCompilation compilation;
    auto current = MakeDef("F", "", TypeKind::Class, compilation);
    // A PRIVATE nested interface -- the enclosing-chain arm must fire BEFORE
    // the IsAccessible fallback ('class F : F.IFoo' is legal C#).
    auto nested = MakeDef("IFoo", "", TypeKind::Interface, compilation,
                          Accessibility::Private);
    nested->SetDeclaringTypeDefinition(current.get());
    MemberLookup lookup(current.get(), nullptr);

    EXPECT_TRUE(TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
        nested.get(), *current, lookup));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, EnclosingTypeNestedTypeIsNameable) {
    LookupCompilation compilation;
    auto outer = MakeDef("F", "", TypeKind::Class, compilation);
    auto inner = MakeDef("SubF", "", TypeKind::Class, compilation);
    inner->SetDeclaringTypeDefinition(outer.get());
    auto nested = MakeDef("IFoo", "", TypeKind::Interface, compilation,
                          Accessibility::Private);
    nested->SetDeclaringTypeDefinition(outer.get());
    MemberLookup lookup(inner.get(), nullptr);

    // The chain walks UP through the current type's enclosing types: a type
    // may name the nested types of its enclosing types too.
    EXPECT_TRUE(TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
        nested.get(), *inner, lookup));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, InaccessibleTopLevelTypeIsNotNameable) {
    LookupCompilation compilation;
    auto current = MakeDef("Unrelated", "", TypeKind::Class, compilation);
    auto hidden = MakeDef("HiddenIf", "", TypeKind::Interface, compilation,
                          Accessibility::Private);
    MemberLookup lookup(current.get(), nullptr);

    // A private top-level interface is neither declared by the current type's
    // chain (its DeclaringTypeDefinition is null) nor accessible.
    EXPECT_FALSE(TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
        hidden.get(), *current, lookup));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, NameableRequiresEnclosingTypeToBeNameable) {
    LookupCompilation compilation;
    auto current = MakeDef("Unrelated", "", TypeKind::Class, compilation);
    auto privateOuter = MakeDef("PrivOuter", "", TypeKind::Class, compilation,
                                Accessibility::Private);
    auto publicNested = MakeDef("PubNested", "", TypeKind::Interface, compilation,
                                Accessibility::Public);
    publicNested->SetDeclaringTypeDefinition(privateOuter.get());
    MemberLookup lookup(current.get(), nullptr);

    // The nested interface is itself public (IsAccessible true), but naming
    // 'PrivOuter.PubNested' also requires 'PrivOuter' to be nameable -- and a
    // private top-level class is not (the recursive enclosing check).
    EXPECT_FALSE(TypeSystemAstBuilder::TypeDefinitionNameableInBaseList(
        publicNested.get(), *current, lookup));
}

// ---------------------------------------------------------------------------
// BaseTypeAccessibleFrom (C# line 2053)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AccessibleInterfaceBaseIsNameable) {
    LookupCompilation compilation;
    auto current = MakeDef("C", "", TypeKind::Class, compilation);
    auto iface = MakeVisitable("IFoo", "", TypeKind::Interface, compilation);
    MemberLookup lookup(current.get(), nullptr);

    TypeSystemAstBuilder builder;
    // A public interface is nameable -- the VisitTypeDefinition bridge lets
    // the visitor see the definition (the D514 trap: a plain
    // LookupTypeDefinition base never reaches VisitTypeDefinition).
    EXPECT_TRUE(builder.BaseTypeAccessibleFrom(*iface, *current, lookup));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, InaccessibleInterfaceBaseIsNotNameable) {
    LookupCompilation compilation;
    auto current = MakeDef("C", "", TypeKind::Class, compilation);
    auto hidden = MakeVisitable("HiddenIf", "", TypeKind::Interface, compilation,
                                Accessibility::Private);
    MemberLookup lookup(current.get(), nullptr);

    TypeSystemAstBuilder builder;
    EXPECT_FALSE(builder.BaseTypeAccessibleFrom(*hidden, *current, lookup));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, NestedTypeArgumentThroughGenericIsVisited) {
    LookupCompilation compilation;
    auto current = MakeDef("C", "", TypeKind::Class, compilation);
    auto wrap = MakeVisitable("IWrap", "", TypeKind::Interface, compilation);
    auto hidden = MakeVisitable("HiddenIf", "", TypeKind::Interface, compilation,
                                Accessibility::Private);
    MemberLookup lookup(current.get(), nullptr);
    // IWrap<HiddenIf> -- the visitor must walk the ParameterizedType's type
    // arguments and find the hidden nested interface (every type the
    // base-list reference NAMES must be nameable, including type arguments).
    auto base = std::make_shared<TS::ParameterizedType>(
        wrap, std::vector<ITypePtr>{hidden});

    TypeSystemAstBuilder builder;
    EXPECT_FALSE(builder.BaseTypeAccessibleFrom(*base, *current, lookup));
}

// ---------------------------------------------------------------------------
// ConvertDelegate (C# line 2094, through the ConvertTypeDefinition dispatch)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateKindRendersDelegateDeclaration) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 0)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);
    auto invoke = std::make_shared<LookupMethod>("Invoke", compilation);
    invoke->SetDeclaringTypeDefinition(delegateDef.get());
    invoke->SetReturnType(intDef);
    delegateDef->SetMethodsForScan({invoke.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    ASSERT_NE(decl, nullptr);
    auto* dd = dynamic_cast<DelegateDeclaration*>(decl.get());
    ASSERT_NE(dd, nullptr);
    EXPECT_EQ(dd->Name(), "MyDel");
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateWithoutInvokeFallsBackToClassType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    // A delegate-kind definition whose method table holds no Invoke -- the C#
    // `goto default;` falls through to the plain `class` shape.
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 0)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    ASSERT_NE(decl, nullptr);
    auto* td = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(td->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::Class);
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateSealedBitCleared) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 0)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);
    delegateDef->SetIsSealed(true);
    auto invoke = std::make_shared<LookupMethod>("Invoke", compilation);
    invoke->SetDeclaringTypeDefinition(delegateDef.get());
    invoke->SetReturnType(intDef);
    delegateDef->SetMethodsForScan({invoke.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    auto* dd = dynamic_cast<DelegateDeclaration*>(decl.get());
    ASSERT_NE(dd, nullptr);
    // The C# `modifiers & ~Modifiers.Sealed` -- a delegate is never rendered
    // `sealed` (the sealed-CLASS twin below keeps the bit).
    EXPECT_FALSE(HasBit(dd->Modifiers(), Modifiers::Sealed));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, SealedClassKeepsSealedBit) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto sealedClass = MakeDef("SealedClass", "", TypeKind::Class, compilation);
    sealedClass->SetIsSealed(true);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*sealedClass));
    auto* td = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(td, nullptr);
    EXPECT_TRUE(HasBit(td->Modifiers(), Modifiers::Sealed));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateOwnAndReturnAttributeSections) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    builder.ShowAttributes() = true;
    auto attrType = MakeDef("MarkerAttribute", "", TypeKind::Class, compilation);
    auto retAttrType = MakeDef("ReturnAttr", "", TypeKind::Class, compilation);
    PlainAttribute ownAttr(attrType);
    PlainAttribute returnAttr(retAttrType);
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 0)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);
    delegateDef->SetAttributes({&ownAttr});
    auto invoke = std::make_shared<LookupMethod>("Invoke", compilation);
    invoke->SetDeclaringTypeDefinition(delegateDef.get());
    invoke->SetReturnType(intDef);
    invoke->SetReturnTypeAttributes({&returnAttr});
    delegateDef->SetMethodsForScan({invoke.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    auto* dd = dynamic_cast<DelegateDeclaration*>(decl.get());
    ASSERT_NE(dd, nullptr);
    ASSERT_EQ(dd->Attributes().Count(), 2);
    // The delegate's OWN attribute sections come from the DEFINITION; the
    // `[return: ...]` sections from the invoke method's return-type attributes.
    EXPECT_EQ(dd->Attributes()[0]->AttributeTarget(), "");
    EXPECT_EQ(dd->Attributes()[1]->AttributeTarget(), "return");
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateReturnTypeAndRefReadOnlyPromotion) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 0)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);
    auto invoke = std::make_shared<LookupMethod>("Invoke", compilation);
    invoke->SetDeclaringTypeDefinition(delegateDef.get());
    // A `ref` return type renders a ComposedType with the ref specifier; the
    // ReturnTypeIsRefReadOnly flag promotes the readonly specifier onto it.
    invoke->SetReturnType(std::make_shared<TS::ByReferenceType>(intDef));
    invoke->SetReturnTypeIsRefReadOnly(true);
    delegateDef->SetMethodsForScan({invoke.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    auto* dd = dynamic_cast<DelegateDeclaration*>(decl.get());
    ASSERT_NE(dd, nullptr);
    auto* ct = dynamic_cast<ComposedType*>(dd->ReturnType());
    ASSERT_NE(ct, nullptr);
    EXPECT_TRUE(ct->HasRefSpecifier());
    EXPECT_TRUE(ct->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateTypeParameterSkipParametersAndConstraints) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    // A NESTED generic delegate: the outer type declares one type parameter
    // (not redeclared); the delegate declares [TOuter, TInner] in its list,
    // so only TInner renders -- and TInner's constraint clause with it.
    auto outer = MakeDef("Outer", "", TypeKind::Class, compilation,
                         Accessibility::Public, KnownTypeCode::None, /*tpc=*/1);
    auto tOuter = std::make_shared<LookupTypeParameter>("TOuter");
    outer->SetTypeParameters({tOuter.get()});
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 2)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);
    delegateDef->SetDeclaringTypeDefinition(outer.get());
    auto tInner = std::make_shared<LookupTypeParameter>("TInner");
    auto constraintType = MakeDef("IBase", "", TypeKind::Interface, compilation);
    tInner->SetDirectBaseTypes({constraintType});
    tInner->SetTypeConstraints({TS::TypeConstraint(constraintType)});
    delegateDef->SetTypeParameters({tOuter.get(), tInner.get()});
    auto invoke = std::make_shared<LookupMethod>("Invoke", compilation);
    invoke->SetDeclaringTypeDefinition(delegateDef.get());
    invoke->SetReturnType(intDef);
    auto param = std::make_shared<TestParameter>(intDef, "arg");
    invoke->SetParameters({param.get()});
    delegateDef->SetMethodsForScan({invoke.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    auto* dd = dynamic_cast<DelegateDeclaration*>(decl.get());
    ASSERT_NE(dd, nullptr);
    // Only the delegate's OWN type parameters render (the outer skip).
    ASSERT_EQ(dd->TypeParameters().Count(), 1);
    // The invoke method's parameters render through ConvertParameter.
    ASSERT_EQ(dd->Parameters().Count(), 1);
    // TInner's constraint clause (a non-object base type).
    ASSERT_EQ(dd->Constraints().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DelegateAnnotationAttached) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    builder.AddResolveResultAnnotations() = true;
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    auto delegateDef = std::make_shared<DelegateHostType>(
        "MyDel", "", FullTypeName(TopLevelTypeName("", "MyDel", 0)),
        TypeKind::Delegate, Accessibility::Public, compilation, nullptr);
    auto invoke = std::make_shared<LookupMethod>("Invoke", compilation);
    invoke->SetDeclaringTypeDefinition(delegateDef.get());
    invoke->SetReturnType(intDef);
    delegateDef->SetMethodsForScan({invoke.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*delegateDef));
    auto* dd = dynamic_cast<DelegateDeclaration*>(decl.get());
    ASSERT_NE(dd, nullptr);
    const TypeResolveResult* annotation = dd->Annotation<TypeResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(&annotation->Type(), delegateDef.get());
}

// ---------------------------------------------------------------------------
// ConvertTypeDefinition (C# line 1900)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AccessibilityBitUnderShowAccessibility) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto td = MakeDef("C", "", TypeKind::Class, compilation, Accessibility::Internal);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Internal));
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Static));
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Abstract));
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Sealed));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, StaticWinsOverAbstractAndSealed) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto td = MakeDef("C", "", TypeKind::Class, compilation);
    td->SetStatic(true);
    td->SetIsAbstract(true);
    td->SetIsSealed(true);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    // The C# if/else-if chain: `static` wins over `abstract` and `sealed`.
    EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Static));
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Abstract));
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Sealed));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AbstractAndSealedWhenNotStatic) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    {
        auto td = MakeDef("A", "", TypeKind::Class, compilation);
        td->SetIsAbstract(true);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Abstract));
        EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Sealed));
    }
    {
        auto td = MakeDef("S", "", TypeKind::Class, compilation);
        td->SetIsSealed(true);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Abstract));
        EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Sealed));
    }
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, ShowModifiersOffRendersAccessibilityOnly) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    builder.ShowModifiers() = false;
    auto td = MakeDef("C", "", TypeKind::Class, compilation, Accessibility::Internal);
    td->SetStatic(true);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Internal));
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Static));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, StructKindClearsSealedAndAddsReadonlyRef) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto td = MakeDef("S", "", TypeKind::Struct, compilation);
    td->SetIsSealed(true);
    td->SetIsReadOnly(true);
    td->SetIsByRefLike(true);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::Struct);
    // A struct is never rendered `sealed` (implicitly sealed)...
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Sealed));
    // ...but a readonly / by-ref-like struct renders `readonly` / `ref`.
    EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Readonly));
    EXPECT_TRUE(HasBit(typeDecl->Modifiers(), Modifiers::Ref));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, EnumKindClearsSealed) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto td = MakeDef("E", "", TypeKind::Enum, compilation);
    td->SetIsSealed(true);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::Enum);
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Sealed));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, InterfaceKindClearsAbstract) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto td = MakeDef("I", "", TypeKind::Interface, compilation);
    td->SetIsAbstract(true);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::Interface);
    EXPECT_FALSE(HasBit(typeDecl->Modifiers(), Modifiers::Abstract));
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, RecordShapesUnderTheirFlags) {
    LookupCompilation compilation;
    {
        TypeSystemAstBuilder builder;
        builder.SupportRecordStructs() = true;
        auto td = MakeDef("RS", "", TypeKind::Struct, compilation);
        td->SetIsRecord(true);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::RecordStruct);
    }
    {
        // The flag off -- the same shape stays a plain struct.
        TypeSystemAstBuilder builder;
        auto td = MakeDef("RS", "", TypeKind::Struct, compilation);
        td->SetIsRecord(true);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::Struct);
    }
    {
        TypeSystemAstBuilder builder;
        builder.SupportRecordClasses() = true;
        auto td = MakeDef("RC", "", TypeKind::Class, compilation);
        td->SetIsRecord(true);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::RecordClass);
    }
    {
        // The flag off -- the same shape stays a plain class.
        TypeSystemAstBuilder builder;
        auto td = MakeDef("RC", "", TypeKind::Class, compilation);
        td->SetIsRecord(true);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->ClassType(), ILSpy::Decompiler::CSharp::Syntax::ClassType::Class);
    }
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AttributesAnnotationAndName) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    builder.ShowAttributes() = true;
    builder.AddResolveResultAnnotations() = true;
    auto attrType = MakeDef("MarkerAttribute", "", TypeKind::Class, compilation);
    PlainAttribute attr(attrType);
    auto td = MakeDef("Named", "", TypeKind::Class, compilation);
    td->SetAttributes({&attr});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_EQ(typeDecl->Attributes().Count(), 1);
    EXPECT_EQ(typeDecl->Name(), "Named");
    const TypeResolveResult* annotation = typeDecl->Annotation<TypeResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(&annotation->Type(), td.get());
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, DiscardNameRendersVerbatim) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    // The C# `decl.Name = typeDefinition.Name == "_" ? "@_" : ...` -- the `_`
    // discard identifier renders escaped; the `Name` setter routes through
    // `Identifier::Create`, which strips the `@` into the verbatim flag.
    auto td = MakeDef("_", "", TypeKind::Class, compilation);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*td));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    EXPECT_EQ(typeDecl->Name(), "_");
    ASSERT_NE(typeDecl->NameToken(), nullptr);
    EXPECT_TRUE(typeDecl->NameToken()->IsVerbatim());
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, TypeParameterSkipAndConstraints) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto outer = MakeDef("Outer", "", TypeKind::Class, compilation,
                         Accessibility::Public, KnownTypeCode::None, /*tpc=*/1);
    auto tOuter = std::make_shared<LookupTypeParameter>("TOuter");
    outer->SetTypeParameters({tOuter.get()});
    auto inner = MakeDef("Inner", "", TypeKind::Class, compilation);
    inner->SetDeclaringTypeDefinition(outer.get());
    auto tInner = std::make_shared<LookupTypeParameter>("TInner");
    auto constraintType = MakeDef("IBase", "", TypeKind::Interface, compilation);
    tInner->SetDirectBaseTypes({constraintType});
    tInner->SetTypeConstraints({TS::TypeConstraint(constraintType)});
    inner->SetTypeParameters({tOuter.get(), tInner.get()});

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*inner));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    // The outer type's parameters are not redeclared (the Skip).
    ASSERT_EQ(typeDecl->TypeParameters().Count(), 1);
    // TInner's constraint clause renders under ShowTypeParameterConstraints.
    ASSERT_EQ(typeDecl->Constraints().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, EnumEnumBaseRendersUnderlyingType) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto byteDef = MakeDef("Byte", "System", TypeKind::Struct, compilation,
                           Accessibility::Public, KnownTypeCode::Byte);
    auto intDef = MakeDef("Int32", "System", TypeKind::Struct, compilation,
                          Accessibility::Public, KnownTypeCode::Int32);
    auto enumBase = MakeDef("Enum", "System", TypeKind::Class, compilation,
                            Accessibility::Public, KnownTypeCode::Enum);
    {
        // An enum whose underlying type is NOT int: the System.Enum base
        // renders the underlying type instead.
        auto enumDef = MakeDef("E", "", TypeKind::Enum, compilation);
        enumDef->SetEnumUnderlyingType(byteDef);
        enumDef->AddDirectBaseType(enumBase);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*enumDef));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        ASSERT_EQ(typeDecl->BaseTypes().Count(), 1);
        auto* prim = dynamic_cast<PrimitiveType*>(typeDecl->BaseTypes()[0]);
        ASSERT_NE(prim, nullptr);
        EXPECT_EQ(prim->Keyword(), "byte");
    }
    {
        // The default int underlying renders nothing.
        auto enumDef = MakeDef("E", "", TypeKind::Enum, compilation);
        enumDef->SetEnumUnderlyingType(intDef);
        enumDef->AddDirectBaseType(enumBase);
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*enumDef));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->BaseTypes().Count(), 0);
    }
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, StructSkipsValueTypeAndObjectBases) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto valueTypeDef = MakeDef("ValueType", "System", TypeKind::Class, compilation,
                                Accessibility::Public, KnownTypeCode::ValueType);
    auto objectDef = MakeDef("Object", "System", TypeKind::Class, compilation,
                             Accessibility::Public, KnownTypeCode::Object);
    auto iface = MakeVisitable("IFoo", "", TypeKind::Interface, compilation);
    auto structDef = MakeDef("S", "", TypeKind::Struct, compilation);
    structDef->AddDirectBaseType(valueTypeDef);
    structDef->AddDirectBaseType(objectDef);
    structDef->AddDirectBaseType(iface);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*structDef));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    // System.ValueType and System.Object never render; the interface stays.
    ASSERT_EQ(typeDecl->BaseTypes().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, InaccessibleInterfaceBaseDroppedClassBaseKept) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto current = MakeDef("C", "", TypeKind::Class, compilation);
    // A PRIVATE top-level interface -- not nameable in the base list (the
    // visitor must see it, so it carries the AcceptVisitor bridge).
    auto hiddenIface = MakeVisitable("HiddenIf", "", TypeKind::Interface, compilation,
                                     Accessibility::Private);
    // A private CLASS base -- the C# comment: "a base class was always
    // written explicitly and stays even if C# could not name it" (the kind
    // gate: only interface-kind bases are checked for nameability).
    auto privateClass = MakeDef("KeptClass", "", TypeKind::Class, compilation,
                                Accessibility::Private);
    current->AddDirectBaseType(hiddenIface);
    current->AddDirectBaseType(privateClass);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*current));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    ASSERT_EQ(typeDecl->BaseTypes().Count(), 1);
    auto* mt = dynamic_cast<MemberType*>(typeDecl->BaseTypes()[0]);
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->MemberName(), "KeptClass");
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, AccessibleInterfaceBaseKept) {
    TypeSystemAstBuilder builder;
    LookupCompilation compilation;
    auto current = MakeDef("C", "", TypeKind::Class, compilation);
    auto iface = MakeVisitable("IFoo", "", TypeKind::Interface, compilation);
    current->AddDirectBaseType(iface);

    std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*current));
    auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
    ASSERT_NE(typeDecl, nullptr);
    // A nameable interface base stays.
    EXPECT_EQ(typeDecl->BaseTypes().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertTypeDefinitionTest, RecordIEquatableBaseOmitted) {
    LookupCompilation compilation;
    auto ieqDef = MakeDef("IEquatable", "System", TypeKind::Interface, compilation);
    auto MakeRecord = [&]() {
        return MakeDef("R", "", TypeKind::Class, compilation);
    };
    {
        // A record class with the IEquatable<R> base: omitted.
        TypeSystemAstBuilder builder;
        builder.SupportRecordClasses() = true;
        auto record = MakeRecord();
        record->SetIsRecord(true);
        record->AddDirectBaseType(std::make_shared<TS::ParameterizedType>(
            ieqDef, std::vector<ITypePtr>{record}));
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*record));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->BaseTypes().Count(), 0);
    }
    {
        // A NON-record with the same base: kept.
        TypeSystemAstBuilder builder;
        builder.SupportRecordClasses() = true;
        auto notRecord = MakeRecord();
        notRecord->AddDirectBaseType(std::make_shared<TS::ParameterizedType>(
            ieqDef, std::vector<ITypePtr>{notRecord}));
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*notRecord));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->BaseTypes().Count(), 1);
    }
    {
        // A record with the flag off: kept.
        TypeSystemAstBuilder builder;
        auto record = MakeRecord();
        record->SetIsRecord(true);
        record->AddDirectBaseType(std::make_shared<TS::ParameterizedType>(
            ieqDef, std::vector<ITypePtr>{record}));
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*record));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->BaseTypes().Count(), 1);
    }
    {
        // A record whose IEquatable type argument is a DIFFERENT type: kept.
        TypeSystemAstBuilder builder;
        builder.SupportRecordClasses() = true;
        auto record = MakeRecord();
        record->SetIsRecord(true);
        auto other = MakeDef("Other", "", TypeKind::Class, compilation);
        record->AddDirectBaseType(std::make_shared<TS::ParameterizedType>(
            ieqDef, std::vector<ITypePtr>{other}));
        std::unique_ptr<EntityDeclaration> decl(builder.ConvertTypeDefinition(*record));
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(decl.get());
        ASSERT_NE(typeDecl, nullptr);
        EXPECT_EQ(typeDecl->BaseTypes().Count(), 1);
    }
}
