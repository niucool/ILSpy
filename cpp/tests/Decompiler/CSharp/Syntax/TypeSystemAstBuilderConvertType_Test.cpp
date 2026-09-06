// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files in the "Software", to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
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

// Tests for the TypeSystemAstBuilder "Convert Type" region
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 266-768: ConvertType(IType) /
// ConvertType(FullTypeName) / ConvertTypeHelper (both overloads) / TypeMatches /
// TypeDefMatches / AddTypeArguments / ConvertNamespace (both overloads) /
// IsValidNamespace / AddTypeAnnotation / MakeSimpleType / MakeGlobal /
// MakeMemberType), plus the two TypeSystemExtensions prerequisites the region
// consumed (IsUnbound + GetTypeDefinition(IModule, FullTypeName)).
//
// The load-bearing cruxes:
//  (a) The TypeWithElementType dispatch: a pointer/array/by-reference type wraps the
//      element's rendering in a ComposedType (Make*), while a custom modifier (not
//      supported as a type in C#) UNWRAPS to the element with no wrapper;
//  (b) The NullabilityAnnotatedType unwrap + the `?` only for a Nullable annotation;
//  (c) The FunctionPointerType signature: the unmanaged/named calling conventions,
//      the CallConv* prefix strip, the ref-parameter ByReference unwrap, the
//      ref-readonly return's ReadOnly specifier, and the treated-as arm;
//  (d) The unbound-generic branch: ShowTypeParametersForUnboundTypes renders the
//      declared type-parameter NAMES, the default renders UnboundTypeArgument
//      placeholders;
//  (e) The ParameterizedType branch with the Nullable<T> -> T? short-circuit (gated
//      on UseNullableSpecifierForValueTypes);
//  (f) The 2-arg helper: the builtin keyword short-circuit, the nested-type
//      MemberType composition with the outer/inner type-argument split, the
//      global:: double-colon form for a namespace-less type, and the
//      namespace-equals-name global prefix;
//  (g) TypeMatches/TypeDefMatches: the unbound-arguments acceptance, the
//      element-wise parameterized-argument comparison, and the nesting-chain
//      recursion;
//  (h) AddTypeArguments with the ConvertUnboundTypeArguments parameter-name
//      rendering;
//  (i) ConvertType(FullTypeName): the resolver's per-module lookup vs the structural
//      fallback (the nesting levels);
//  (j) ConvertNamespace: the single/multi-part split, the valid/invalid namespace
//      forms, and the `_` -> `@_` escaping.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/FunctionPointerAstType.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/TupleAstType.hpp"
#include "Decompiler/CSharp/Syntax/TupleTypeElement.hpp"

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::AstType;
using ILSpy::Decompiler::CSharp::Syntax::Comment;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::FunctionPointerAstType;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::TupleAstType;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnboundTypeArgument;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupModule;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

namespace {

// A fresh per-test compilation (the CSharpConversions/UsingScope lifetime
// discipline: the per-compilation caches die with the compilation).
std::unique_ptr<LookupCompilation> MakeCompilation() {
    return std::make_unique<LookupCompilation>();
}

// A shared-managed type definition stub (every type fed to the region must be
// shared-managed -- AddTypeAnnotation recovers the owning handle via
// shared_from_this).
std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                              const std::string& ns,
                                              int typeParameterCount = 0,
                                              TypeKind kind = TypeKind::Class,
                                              KnownTypeCode knownTypeCode = KnownTypeCode::None,
                                              const ILSpy::Decompiler::TypeSystem::ICompilation&
                                                  compilation
                                              = *MakeCompilation()) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, typeParameterCount)), kind,
        Accessibility::Public, compilation, nullptr, knownTypeCode);
}

// A LookupTypeDefinition subclass whose DeclaringType() returns the configured
// owning handle (the shared stub returns {} -- the nested-type arm reads
// IType.DeclaringType, which for a definition is the declaring type definition).
class NestedTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetDeclaringType(ITypePtr declaring) { declaringType_ = std::move(declaring); }
    ITypePtr DeclaringType() const override { return declaringType_; }

private:
    ITypePtr declaringType_;
};

// A LookupTypeDefinition subclass with a configurable Nullability (the shared stub
// inherits the Oblivious default; the trailing-`?` arm reads type.Nullability).
class NullableDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    ILSpy::Decompiler::TypeSystem::Nullability Nullability() const override {
        return nullability_;
    }
    void SetNullability(ILSpy::Decompiler::TypeSystem::Nullability n) { nullability_ = n; }

private:
    ILSpy::Decompiler::TypeSystem::Nullability nullability_ =
        ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
};

// A FunctionPointerType subclass with a configurable GetDefinition (the treated-as
// arm; the ported FunctionPointerType inherits the nullptr default).
class TestFpt : public FunctionPointerType {
public:
    using FunctionPointerType::FunctionPointerType;
    const ITypeDefinition* GetDefinition() const override { return definition_; }
    void SetDefinition(const ITypeDefinition* def) { definition_ = def; }

private:
    const ITypeDefinition* definition_ = nullptr;
};

// A LookupModule subclass with a configurable GetTypeDefinition(TopLevelTypeName)
// table (the shared stub returns nullptr -- ConvertType(FullTypeName) resolves
// through the TypeSystemExtensions GetTypeDefinition(IModule, FullTypeName)
// extension).
class TestModule : public LookupModule {
public:
    using LookupModule::LookupModule;
    void AddTypeDef(const ITypeDefinition* def) { defs_.push_back(def); }
    const ITypeDefinition* GetTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::TopLevelTypeName& topLevelTypeName) const override {
        for (const ITypeDefinition* def : defs_) {
            if (def->Name() == topLevelTypeName.Name()
                && def->Namespace() == topLevelTypeName.Namespace()
                && def->TypeParameterCount() == topLevelTypeName.TypeParameterCount())
                return def;
        }
        return nullptr;
    }

private:
    std::vector<const ITypeDefinition*> defs_;
};

// A configurable INamespace (the CSharpResolverSimpleName ConfigurableNamespace
// pattern).
class ConfigurableNamespace : public INamespace {
public:
    ConfigurableNamespace(std::string name,
                          const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void AddChildNamespace(const INamespace* child) { children_.push_back(child); }

    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }
    std::string Name() const override { return name_; }
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override {
        return compilation_;
    }
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return children_; }
    std::vector<const ITypeDefinition*> Types() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules()
        const override {
        return {};
    }
    const INamespace* GetChildNamespace(const std::string& name) const override {
        for (const INamespace* child : children_) {
            if (child->Name() == name)
                return child;
        }
        return nullptr;
    }
    const ITypeDefinition* GetTypeDefinition(const std::string&, int) const override {
        return nullptr;
    }

private:
    std::string name_;
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    std::vector<const INamespace*> children_;
};

// A LookupCompilation subclass whose RootNamespace is configurable (the shared
// stub's TestNamespace has no children, so the resolver's namespace lookup can
// never succeed through it).
class NamespacedCompilation : public LookupCompilation {
public:
    NamespacedCompilation() : LookupCompilation(), rootNamespace_("", *this) {}
    const INamespace& RootNamespace() const override { return rootNamespace_; }
    ConfigurableNamespace& Root() { return rootNamespace_; }

private:
    ConfigurableNamespace rootNamespace_;
};

} // namespace

// ---------------------------------------------------------------------------
// ConvertType(IType): the annotation channel
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, ConvertTypeAnnotatesWithTypeResolveResult) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;

    AstType* result = builder.ConvertType(*def);

    ASSERT_NE(result, nullptr);
    const auto* annotation = result->Annotation<TypeResolveResult>();
    ASSERT_NE(annotation, nullptr);
    // The annotation carries the SAME type instance (the shared_from_this handle).
    EXPECT_EQ(&annotation->Type(), def.get());
}

TEST(TypeSystemAstBuilderConvertTypeTest, ConvertTypeWithoutAnnotationsAddsNone) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    TypeSystemAstBuilder builder; // AddResolveResultAnnotations defaults to false

    AstType* result = builder.ConvertType(*def);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Annotation<TypeResolveResult>(), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertTypeHelper(IType): the TypeWithElementType dispatch
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, PointerTypeWrapsElementInComposedPointer) {
    auto compilation = MakeCompilation();
    auto element = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    PointerType pointerType(element);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(pointerType);

    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(composed->PointerRank(), 1);
    // The element definition (namespace Ns) renders qualified through the
    // resolver-less builder.
    auto* base = dynamic_cast<MemberType*>(composed->BaseType());
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->MemberName(), "Foo");
}

TEST(TypeSystemAstBuilderConvertTypeTest, ArrayTypeCarriesRankSpecifier) {
    auto compilation = MakeCompilation();
    auto element = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    ArrayType arrayType(element, 2);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(arrayType);

    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    ASSERT_EQ(composed->ArraySpecifiers().Count(), 1);
    EXPECT_EQ(composed->ArraySpecifiers()[0]->Dimensions(), 2);
}

TEST(TypeSystemAstBuilderConvertTypeTest, ByReferenceTypeSetsRefSpecifier) {
    auto compilation = MakeCompilation();
    auto element = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    ByReferenceType byRefType(element);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(byRefType);

    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasRefSpecifier());
}

TEST(TypeSystemAstBuilderConvertTypeTest, ModifiedTypeUnwrapsToElementWithoutWrapper) {
    auto compilation = MakeCompilation();
    auto element = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto modifier = MakeDef("IsConst", "System.Runtime.CompilerServices", 0, TypeKind::Class,
                            KnownTypeCode::None, *compilation);
    ModifiedType modifiedType(modifier, element, /*isRequired*/ false);

    TypeSystemAstBuilder builder;
    // A custom modifier is not supported as a type in C#: the element renders
    // bare (NO ComposedType wrapper -- unlike the pointer/array/by-ref arms).
    AstType* result = builder.ConvertTypeHelper(modifiedType);

    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
    EXPECT_EQ(dynamic_cast<ComposedType*>(result), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertTypeHelper(IType): the NullabilityAnnotatedType unwrap
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, NullableAnnotationAppendsQuestionMark) {
    auto compilation = MakeCompilation();
    auto base = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    NullabilityAnnotatedType annotated(base, Nullability::Nullable);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(annotated);

    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
}

TEST(TypeSystemAstBuilderConvertTypeTest, NotNullableAnnotationKeepsBareType) {
    auto compilation = MakeCompilation();
    auto base = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    NullabilityAnnotatedType annotated(base, Nullability::NotNullable);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(annotated);

    // No ComposedType wrapper: the bare (qualified) type renders.
    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
    EXPECT_EQ(dynamic_cast<ComposedType*>(result), nullptr);
}

// ---------------------------------------------------------------------------
// ConvertTypeHelper(IType): the TupleType arm
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, TupleTypeRendersElementsWithNames) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto stringType = MakeDef("String", "System", 0, TypeKind::Class, KnownTypeCode::String,
                              *compilation);
    // The TupleType ctor takes the element types + names + the underlying
    // ValueTuple<...> (a pre-built ParameterizedType, the D405 minimal-port
    // convention).
    auto underlying = MakeDef("ValueTuple", "System", 2, TypeKind::Struct,
                              KnownTypeCode::None, *compilation);
    TupleType tuple(underlying, {intType, stringType}, {"Item1", "Name"});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(tuple);

    auto* tupleAstType = dynamic_cast<TupleAstType*>(result);
    ASSERT_NE(tupleAstType, nullptr);
    ASSERT_EQ(tupleAstType->Elements().Count(), 2);
    EXPECT_EQ(tupleAstType->Elements()[0]->Name(), std::optional<std::string>("Item1"));
    EXPECT_EQ(tupleAstType->Elements()[1]->Name(), std::optional<std::string>("Name"));
}

// ---------------------------------------------------------------------------
// ConvertTypeHelper(IType): the FunctionPointerType arm
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerRendersManagedSignature) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    FunctionPointerType fpt(SignatureCallingConvention::Default, {}, intType,
                            /*returnIsRefReadOnly*/ false, {voidType},
                            {ReferenceKind::None});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    EXPECT_FALSE(fptAstType->HasUnmanagedCallingConvention());
    EXPECT_EQ(fptAstType->CallingConventions().Count(), 0);
    ASSERT_EQ(fptAstType->Parameters().Count(), 1);
    EXPECT_EQ(fptAstType->Parameters()[0]->ParameterModifier(), ReferenceKind::None);
    ASSERT_NE(fptAstType->ReturnType(), nullptr);
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerUnmanagedConventionBare) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    FunctionPointerType fpt(SignatureCallingConvention::Unmanaged, {}, intType, false,
                            {voidType}, {ReferenceKind::None});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    EXPECT_TRUE(fptAstType->HasUnmanagedCallingConvention());
    // The bare `unmanaged` form carries no named convention specifier.
    EXPECT_EQ(fptAstType->CallingConventions().Count(), 0);
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerNamedConventionSpecifier) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    FunctionPointerType fpt(SignatureCallingConvention::CDecl, {}, intType, false,
                            {voidType}, {ReferenceKind::None});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    EXPECT_TRUE(fptAstType->HasUnmanagedCallingConvention());
    ASSERT_EQ(fptAstType->CallingConventions().Count(), 1);
    auto* primitive = dynamic_cast<PrimitiveType*>(fptAstType->CallingConventions()[0]);
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), "Cdecl");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerCustomCallConvPrefixStripped) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    auto callConv = MakeDef("CallConvCdecl", "System.Runtime.CompilerServices", 0,
                            TypeKind::Class, KnownTypeCode::None, *compilation);
    FunctionPointerType fpt(SignatureCallingConvention::Unmanaged, {callConv}, intType,
                            false, {voidType}, {ReferenceKind::None});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    ASSERT_EQ(fptAstType->CallingConventions().Count(), 1);
    auto* primitive = dynamic_cast<PrimitiveType*>(fptAstType->CallingConventions()[0]);
    ASSERT_NE(primitive, nullptr);
    // The `CallConv` prefix is stripped: `CallConvCdecl` renders `Cdecl`.
    EXPECT_EQ(primitive->Keyword(), "Cdecl");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerCustomCallConvOtherNamespaceKept) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    auto otherConv = MakeDef("CallConvCdecl", "Other.Namespace", 0, TypeKind::Class,
                             KnownTypeCode::None, *compilation);
    FunctionPointerType fpt(SignatureCallingConvention::Unmanaged, {otherConv}, intType,
                            false, {voidType}, {ReferenceKind::None});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    ASSERT_EQ(fptAstType->CallingConventions().Count(), 1);
    // A non-System.Runtime.CompilerServices type converts as an ordinary type
    // reference (the qualified name, no prefix strip).
    auto* typeRef = dynamic_cast<MemberType*>(fptAstType->CallingConventions()[0]);
    ASSERT_NE(typeRef, nullptr);
    EXPECT_EQ(typeRef->MemberName(), "CallConvCdecl");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerRefParameterUnwrapsByReference) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    auto refInt = std::make_shared<ByReferenceType>(intType);
    FunctionPointerType fpt(SignatureCallingConvention::Default, {}, intType, false,
                            {refInt}, {ReferenceKind::Ref});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    ASSERT_EQ(fptAstType->Parameters().Count(), 1);
    EXPECT_EQ(fptAstType->Parameters()[0]->ParameterModifier(), ReferenceKind::Ref);
    // The ref parameter's ByReferenceType is unwrapped: the parameter type is
    // the ELEMENT (the `int` keyword primitive, not a ref ComposedType).
    auto* paramType = dynamic_cast<PrimitiveType*>(fptAstType->Parameters()[0]->Type());
    ASSERT_NE(paramType, nullptr);
    EXPECT_EQ(paramType->Keyword(), "int");
    EXPECT_EQ(dynamic_cast<ComposedType*>(fptAstType->Parameters()[0]->Type()), nullptr);
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerRefReadOnlyReturnSetsSpecifier) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    auto refInt = std::make_shared<ByReferenceType>(intType);
    FunctionPointerType fpt(SignatureCallingConvention::Default, {}, refInt,
                            /*returnIsRefReadOnly*/ true, {voidType}, {ReferenceKind::None});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    auto* fptAstType = dynamic_cast<FunctionPointerAstType*>(result);
    ASSERT_NE(fptAstType, nullptr);
    // The ref return type renders `ref` and the ref-readonly flag adds `readonly`.
    auto* returnType = dynamic_cast<ComposedType*>(fptAstType->ReturnType());
    ASSERT_NE(returnType, nullptr);
    EXPECT_TRUE(returnType->HasRefSpecifier());
    EXPECT_TRUE(returnType->HasReadOnlySpecifier());
}

TEST(TypeSystemAstBuilderConvertTypeTest, FunctionPointerTreatedAsRendersDefinition) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto voidType = MakeDef("Void", "System", 0, TypeKind::Struct, KnownTypeCode::Void,
                            *compilation);
    auto uintPtrDef = MakeDef("UIntPtr", "System", 0, TypeKind::Struct,
                              KnownTypeCode::UIntPtr, *compilation);
    TestFpt fpt(SignatureCallingConvention::Default, {}, intType, false, {voidType},
                {ReferenceKind::None});
    fpt.SetDefinition(uintPtrDef.get());

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(fpt);

    // The treated-as arm renders the DEFINITION (not the function-pointer
    // syntax; UIntPtr carries no C# keyword, so the qualified name renders).
    EXPECT_EQ(dynamic_cast<FunctionPointerAstType*>(result), nullptr);
    auto* typeRef = dynamic_cast<MemberType*>(result);
    ASSERT_NE(typeRef, nullptr);
    EXPECT_EQ(typeRef->MemberName(), "UIntPtr");

    // The trailing Comment carries the FUNCTION-POINTER AST's rendering (the C#
    // `astType.ToString()` over the fully-built function-pointer node, NOT the
    // treated-as `result`'s rendering): the parameter and return types resolve through
    // the builtin keywords, so the content is the full `delegate*` signature.
    ASSERT_EQ(typeRef->TrailingTrivia().size(), 1u);
    auto* comment = dynamic_cast<Comment*>(typeRef->TrailingTrivia()[0]);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->CommentType(), CommentType::MultiLine);
    EXPECT_EQ(comment->Content(), "delegate*<void, int>");
}

// ---------------------------------------------------------------------------
// ConvertTypeHelper(IType): the unbound-generic / ParameterizedType / default arms
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, UnboundGenericRendersTypeParameterNames) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 1, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto typeParameter = std::make_shared<LookupTypeParameter>("T");
    def->SetTypeParameters({typeParameter.get()});

    TypeSystemAstBuilder builder;
    builder.ShowTypeParametersForUnboundTypes() = true;
    AstType* result = builder.ConvertTypeHelper(*def);

    auto* typeRef = dynamic_cast<MemberType*>(result);
    ASSERT_NE(typeRef, nullptr);
    EXPECT_EQ(typeRef->MemberName(), "Foo");
    ASSERT_EQ(typeRef->TypeArguments().Count(), 1);
    auto* argument = dynamic_cast<SimpleType*>(typeRef->TypeArguments()[0]);
    ASSERT_NE(argument, nullptr);
    // The declared type parameter's NAME renders (not the ? placeholder).
    EXPECT_EQ(*argument->Identifier(), "T");
}

TEST(TypeSystemAstBuilderConvertTypeTest, UnboundGenericRendersPlaceholdersByDefault) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 1, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto typeParameter = std::make_shared<LookupTypeParameter>("T");
    def->SetTypeParameters({typeParameter.get()});

    TypeSystemAstBuilder builder; // ShowTypeParametersForUnboundTypes defaults to false
    AstType* result = builder.ConvertTypeHelper(*def);

    auto* typeRef = dynamic_cast<MemberType*>(result);
    ASSERT_NE(typeRef, nullptr);
    ASSERT_EQ(typeRef->TypeArguments().Count(), 1);
    auto* argument = dynamic_cast<SimpleType*>(typeRef->TypeArguments()[0]);
    ASSERT_NE(argument, nullptr);
    // The UnboundTypeArgument placeholder's Name is "" (the C#
    // `SpecialType.UnboundTypeArgument` singleton's name) -- the SimpleType
    // gets a NULL identifier (the `CreateIfNotEmpty` empty-name rule), the
    // shape the C# `MakeSimpleType(type.Name)` default arm produces.
    EXPECT_FALSE(argument->Identifier().has_value());
}

TEST(TypeSystemAstBuilderConvertTypeTest, ParameterizedTypeRendersGenericWithArguments) {
    auto compilation = MakeCompilation();
    auto generic = MakeDef("List", "System.Collections.Generic", 1, TypeKind::Class,
                           KnownTypeCode::None, *compilation);
    auto typeParameter = std::make_shared<LookupTypeParameter>("T");
    generic->SetTypeParameters({typeParameter.get()});
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    ParameterizedType parameterized(generic, {intType});

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(parameterized);

    auto* typeRef = dynamic_cast<MemberType*>(result);
    ASSERT_NE(typeRef, nullptr);
    EXPECT_EQ(typeRef->MemberName(), "List");
    ASSERT_EQ(typeRef->TypeArguments().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertTypeTest, NullableOfTRendersQuestionMarkShortCircuit) {
    auto compilation = MakeCompilation();
    auto nullableDef = MakeDef("Nullable", "System", 1, TypeKind::Struct,
                               KnownTypeCode::NullableOfT, *compilation);
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    ParameterizedType parameterized(nullableDef, {intType});

    TypeSystemAstBuilder builder; // UseNullableSpecifierForValueTypes defaults to true
    AstType* result = builder.ConvertTypeHelper(parameterized);

    // The Nullable<T> short-circuit renders `T?` (a ComposedType over the element),
    // never `Nullable<T>`.
    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
}

TEST(TypeSystemAstBuilderConvertTypeTest, NullableOfTDisaggregatedWhenFlagOff) {
    auto compilation = MakeCompilation();
    auto nullableDef = MakeDef("Nullable", "System", 1, TypeKind::Struct,
                               KnownTypeCode::NullableOfT, *compilation);
    auto typeParameter = std::make_shared<LookupTypeParameter>("T");
    nullableDef->SetTypeParameters({typeParameter.get()});
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    ParameterizedType parameterized(nullableDef, {intType});

    TypeSystemAstBuilder builder;
    builder.UseNullableSpecifierForValueTypes() = false;
    AstType* result = builder.ConvertTypeHelper(parameterized);

    // With the flag off, the generic renders as an ordinary parameterized type.
    auto* typeRef = dynamic_cast<MemberType*>(result);
    ASSERT_NE(typeRef, nullptr);
    EXPECT_EQ(typeRef->MemberName(), "Nullable");
    ASSERT_EQ(typeRef->TypeArguments().Count(), 1);
}

TEST(TypeSystemAstBuilderConvertTypeTest, DynamicNIntNUIntRenderPrimitiveKeywords) {
    TypeSystemAstBuilder builder;
    auto dynamicType = std::make_shared<SpecialType>(TypeKind::Dynamic);
    auto* dynamicResult = builder.ConvertTypeHelper(*dynamicType);
    auto* dynamicPrimitive = dynamic_cast<PrimitiveType*>(dynamicResult);
    ASSERT_NE(dynamicPrimitive, nullptr);
    EXPECT_EQ(dynamicPrimitive->Keyword(), "dynamic");

    auto nintType = std::make_shared<SpecialType>(TypeKind::NInt);
    auto* nintResult = builder.ConvertTypeHelper(*nintType);
    auto* nintPrimitive = dynamic_cast<PrimitiveType*>(nintResult);
    ASSERT_NE(nintPrimitive, nullptr);
    EXPECT_EQ(nintPrimitive->Keyword(), "nint");

    auto nuintType = std::make_shared<SpecialType>(TypeKind::NUInt);
    auto* nuintResult = builder.ConvertTypeHelper(*nuintType);
    auto* nuintPrimitive = dynamic_cast<PrimitiveType*>(nuintResult);
    ASSERT_NE(nuintPrimitive, nullptr);
    EXPECT_EQ(nuintPrimitive->Keyword(), "nuint");
}

TEST(TypeSystemAstBuilderConvertTypeTest, NullableNullabilityTypeAppendsQuestionMark) {
    auto compilation = MakeCompilation();
    auto def = std::make_shared<NullableDef>(
        "Foo", "Ns", FullTypeName(TopLevelTypeName("Ns", "Foo", 0)), TypeKind::Class,
        Accessibility::Public, *compilation, nullptr, KnownTypeCode::None);
    def->SetNullability(Nullability::Nullable);

    TypeSystemAstBuilder builder;
    builder.AddResolveResultAnnotations() = true;
    AstType* result = builder.ConvertTypeHelper(*def);

    auto* composed = dynamic_cast<ComposedType*>(result);
    ASSERT_NE(composed, nullptr);
    EXPECT_TRUE(composed->HasNullableSpecifier());
    // The annotation carries the ChangeNullability(Oblivious) result -- the IType
    // default returns shared_from_this(), so the SAME instance.
    const auto* annotation = composed->BaseType()->Annotation<TypeResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(&annotation->Type(), def.get());
}

// ---------------------------------------------------------------------------
// ConvertTypeHelper(genericType, typeArguments): the named-type renderer
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, BuiltinKeywordShortCircuitsNameRendering) {
    auto compilation = MakeCompilation();
    auto intDef = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                          *compilation);

    TypeSystemAstBuilder builder; // UseKeywordsForBuiltinTypes defaults to true
    AstType* result = builder.ConvertTypeHelper(*intDef, {});

    auto* primitive = dynamic_cast<PrimitiveType*>(result);
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->Keyword(), "int");
}

TEST(TypeSystemAstBuilderConvertTypeTest, BuiltinKeywordDisabledRendersQualifiedName) {
    auto compilation = MakeCompilation();
    auto intDef = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                          *compilation);

    TypeSystemAstBuilder builder;
    builder.UseKeywordsForBuiltinTypes() = false;
    AstType* result = builder.ConvertTypeHelper(*intDef, {});

    // Without the keyword, the resolver-less builder renders the qualified
    // MemberType over the namespace.
    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Int32");
    auto* target = dynamic_cast<SimpleType*>(memberType->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(*target->Identifier(), "System");
}

TEST(TypeSystemAstBuilderConvertTypeTest, AlwaysUseShortTypeNamesRendersSimpleName) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Some.Long.Namespace", 0, TypeKind::Class,
                       KnownTypeCode::None, *compilation);

    TypeSystemAstBuilder builder;
    builder.AlwaysUseShortTypeNames() = true;
    AstType* result = builder.ConvertTypeHelper(*def, {});

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(*simple->Identifier(), "Foo");
}

TEST(TypeSystemAstBuilderConvertTypeTest, TopLevelTypeRendersNamespaceMemberType) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(*def, {});

    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
    EXPECT_FALSE(memberType->IsDoubleColon());
    auto* target = dynamic_cast<SimpleType*>(memberType->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(*target->Identifier(), "Ns");
}

TEST(TypeSystemAstBuilderConvertTypeTest, NamespaceLessTypeRendersGlobalDoubleColon) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "", 0, TypeKind::Class, KnownTypeCode::None, *compilation);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(*def, {});

    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
    EXPECT_TRUE(memberType->IsDoubleColon());
    auto* target = dynamic_cast<SimpleType*>(memberType->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(*target->Identifier(), "global");
}

TEST(TypeSystemAstBuilderConvertTypeTest, NamespaceEqualsNameGetsGlobalPrefix) {
    auto compilation = MakeCompilation();
    // A type named `Foo` in the namespace `Foo`: the namespace reference requires
    // the global prefix (it would otherwise be ambiguous with the type name).
    auto def = MakeDef("Foo", "Foo", 0, TypeKind::Class, KnownTypeCode::None, *compilation);

    TypeSystemAstBuilder builder;
    AstType* result = builder.ConvertTypeHelper(*def, {});

    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
    // The NAMESPACE side renders `global::Foo` (the requiresGlobalPrefix form).
    auto* namespaceRef = dynamic_cast<MemberType*>(memberType->Target());
    ASSERT_NE(namespaceRef, nullptr);
    EXPECT_TRUE(namespaceRef->IsDoubleColon());
    EXPECT_EQ(namespaceRef->MemberName(), "Foo");
}

TEST(TypeSystemAstBuilderConvertTypeTest, NestedTypeRendersDeclaringTypeChain) {
    auto compilation = MakeCompilation();
    auto outer = std::make_shared<NestedTypeDef>(
        "Outer", "Ns", FullTypeName(TopLevelTypeName("Ns", "Outer", 1)), TypeKind::Class,
        Accessibility::Public, *compilation, nullptr, KnownTypeCode::None);
    auto inner = std::make_shared<NestedTypeDef>(
        "Inner", "Ns", FullTypeName(TopLevelTypeName("Ns", "Inner", 2)), TypeKind::Class,
        Accessibility::Public, *compilation, nullptr, KnownTypeCode::None);
    inner->SetDeclaringType(outer);
    inner->SetDeclaringTypeDefinition(outer.get());
    auto outerTypeParameter = std::make_shared<LookupTypeParameter>("T");
    outer->SetTypeParameters({outerTypeParameter.get()});
    auto innerTypeParameter = std::make_shared<LookupTypeParameter>("U");
    inner->SetTypeParameters(
        {outerTypeParameter.get(), innerTypeParameter.get()});

    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto stringType = MakeDef("String", "System", 0, TypeKind::Class, KnownTypeCode::String,
                              *compilation);

    TypeSystemAstBuilder builder;
    // Two type arguments: the first belongs to the OUTER type, the second to the
    // inner (the outerTypeParameterCount split).
    AstType* result = builder.ConvertTypeHelper(*inner, {intType, stringType});

    auto* innerRef = dynamic_cast<MemberType*>(result);
    ASSERT_NE(innerRef, nullptr);
    EXPECT_EQ(innerRef->MemberName(), "Inner");
    // The inner type carries only ITS OWN type argument (the second).
    ASSERT_EQ(innerRef->TypeArguments().Count(), 1);
    // The outer rendering carries the FIRST type argument.
    auto* outerRef = dynamic_cast<MemberType*>(innerRef->Target());
    ASSERT_NE(outerRef, nullptr);
    EXPECT_EQ(outerRef->MemberName(), "Outer");
    ASSERT_EQ(outerRef->TypeArguments().Count(), 1);
}

// ---------------------------------------------------------------------------
// TypeMatches / TypeDefMatches
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, TypeMatchesNonGenericDelegatesToTypeDefMatches) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto sameName = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto otherName = MakeDef("Bar", "Ns", 0, TypeKind::Class, KnownTypeCode::None,
                             *compilation);

    TypeSystemAstBuilder builder;
    EXPECT_TRUE(builder.TypeMatches(*sameName, *def, {}));
    EXPECT_FALSE(builder.TypeMatches(*otherName, *def, {}));
}

TEST(TypeSystemAstBuilderConvertTypeTest, TypeMatchesUnboundArgumentsAccepted) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 1, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto plain = MakeDef("Foo", "Ns", 1, TypeKind::Class, KnownTypeCode::None, *compilation);

    TypeSystemAstBuilder builder;
    // A non-parameterized type over a generic def: every type argument must be the
    // UnboundTypeArgument placeholder.
    EXPECT_TRUE(builder.TypeMatches(*plain, *def, {UnboundTypeArgument()}));
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    EXPECT_FALSE(builder.TypeMatches(*plain, *def, {intType}));
}

TEST(TypeSystemAstBuilderConvertTypeTest, TypeMatchesParameterizedArgumentsElementWise) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 1, TypeKind::Class, KnownTypeCode::None, *compilation);
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto stringType = MakeDef("String", "System", 0, TypeKind::Class, KnownTypeCode::String,
                              *compilation);
    ParameterizedType overInt(def, {intType});
    ParameterizedType overString(def, {stringType});

    TypeSystemAstBuilder builder;
    EXPECT_TRUE(builder.TypeMatches(overInt, *def, {intType}));
    EXPECT_FALSE(builder.TypeMatches(overString, *def, {intType}));
}

TEST(TypeSystemAstBuilderConvertTypeTest, TypeDefMatchesNestingChainRecursion) {
    auto compilation = MakeCompilation();
    auto outerDef = MakeDef("Outer", "Ns", 0, TypeKind::Class, KnownTypeCode::None,
                            *compilation);
    auto nestedDef = MakeDef("Inner", "Ns", 0, TypeKind::Class, KnownTypeCode::None,
                             *compilation);
    nestedDef->SetDeclaringTypeDefinition(outerDef.get());

    // A matching nested type: same name/namespace/count AND a matching declaring
    // type (the recursion).
    auto outerType = std::make_shared<NestedTypeDef>(
        "Outer", "Ns", FullTypeName(TopLevelTypeName("Ns", "Outer", 0)), TypeKind::Class,
        Accessibility::Public, *compilation, nullptr, KnownTypeCode::None);
    auto nestedType = std::make_shared<NestedTypeDef>(
        "Inner", "Ns", FullTypeName(TopLevelTypeName("Ns", "Inner", 0)), TypeKind::Class,
        Accessibility::Public, *compilation, nullptr, KnownTypeCode::None);
    nestedType->SetDeclaringType(outerType);
    nestedType->SetDeclaringTypeDefinition(outerType.get());

    // A top-level type with the same name does NOT match the nested definition.
    auto topLevel = MakeDef("Inner", "Ns", 0, TypeKind::Class, KnownTypeCode::None,
                            *compilation);

    TypeSystemAstBuilder builder;
    EXPECT_TRUE(builder.TypeDefMatches(*nestedDef, nestedType.get()));
    EXPECT_FALSE(builder.TypeDefMatches(*nestedDef, topLevel.get()));
    EXPECT_FALSE(builder.TypeDefMatches(*nestedDef, nullptr));
}

// ---------------------------------------------------------------------------
// AddTypeArguments
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, AddTypeArgumentsConvertsEachArgument) {
    auto compilation = MakeCompilation();
    auto intType = MakeDef("Int32", "System", 0, TypeKind::Struct, KnownTypeCode::Int32,
                           *compilation);
    auto stringType = MakeDef("String", "System", 0, TypeKind::Class, KnownTypeCode::String,
                              *compilation);
    // The C# Debug.Assert contract: endIndex <= typeParameters.Count (the
    // parameters list covers every rendered argument).
    auto typeParameter1 = std::make_shared<LookupTypeParameter>("T1");
    auto typeParameter2 = std::make_shared<LookupTypeParameter>("T2");
    auto* result = new SimpleType("Foo");

    TypeSystemAstBuilder builder;
    builder.AddTypeArguments(*result, {typeParameter1.get(), typeParameter2.get()},
                             {intType, stringType}, 0, 2);

    ASSERT_EQ(result->TypeArguments().Count(), 2);
}

TEST(TypeSystemAstBuilderConvertTypeTest, AddTypeArgumentsUnboundRendersParameterName) {
    auto compilation = MakeCompilation();
    auto typeParameter = std::make_shared<LookupTypeParameter>("T");
    auto* result = new SimpleType("Foo");

    TypeSystemAstBuilder builder;
    builder.ConvertUnboundTypeArguments() = true;
    builder.AddTypeArguments(*result, {typeParameter.get()}, {UnboundTypeArgument()}, 0, 1);

    ASSERT_EQ(result->TypeArguments().Count(), 1);
    auto* argument = dynamic_cast<SimpleType*>(result->TypeArguments()[0]);
    ASSERT_NE(argument, nullptr);
    // The UnboundTypeArgument placeholder renders as the type PARAMETER's name.
    EXPECT_EQ(*argument->Identifier(), "T");
}

// ---------------------------------------------------------------------------
// ConvertType(FullTypeName)
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, FullTypeNameThroughResolverFoundDefinition) {
    auto compilation = MakeCompilation();
    auto def = MakeDef("Foo", "Ns", 0, TypeKind::Class, KnownTypeCode::None, *compilation);
    TestModule module(*compilation, "TestModule");
    module.AddTypeDef(def.get());
    compilation->AddModule(&module);

    auto resolver = std::make_shared<CSharpResolver>(*compilation);
    TypeSystemAstBuilder builder(resolver);
    builder.AlwaysUseShortTypeNames() = true;

    AstType* result = builder.ConvertType(
        FullTypeName(TopLevelTypeName("Ns", "Foo", 0)));

    // The module's table resolved the name: the DEFINITION converts (the short-name
    // arm through AlwaysUseShortTypeNames).
    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(*simple->Identifier(), "Foo");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FullTypeNameStructuralFallbackNoNamespace) {
    TypeSystemAstBuilder builder; // no resolver: the structural fallback

    AstType* result = builder.ConvertType(
        FullTypeName(TopLevelTypeName("", "Foo", 0)));

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(*simple->Identifier(), "Foo");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FullTypeNameStructuralFallbackWithNamespace) {
    TypeSystemAstBuilder builder;

    AstType* result = builder.ConvertType(
        FullTypeName(TopLevelTypeName("Ns", "Foo", 0)));

    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_EQ(memberType->MemberName(), "Foo");
    auto* target = dynamic_cast<SimpleType*>(memberType->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(*target->Identifier(), "Ns");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FullTypeNameStructuralFallbackNestedLevels) {
    TypeSystemAstBuilder builder;

    // The reflection-name ctor parses the '+' nesting separator.
    AstType* result = builder.ConvertType(FullTypeName("Ns.Outer+Inner"));

    auto* inner = dynamic_cast<MemberType*>(result);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->MemberName(), "Inner");
    auto* outer = dynamic_cast<MemberType*>(inner->Target());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->MemberName(), "Outer");
    auto* ns = dynamic_cast<SimpleType*>(outer->Target());
    ASSERT_NE(ns, nullptr);
    EXPECT_EQ(*ns->Identifier(), "Ns");
}

TEST(TypeSystemAstBuilderConvertTypeTest, FullTypeNameUnderscoreNameEscaped) {
    TypeSystemAstBuilder builder;

    AstType* result = builder.ConvertType(
        FullTypeName(TopLevelTypeName("", "_", 0)));

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    // The `_` discard identifier renders escaped: the `@`-prefix form. The
    // `Identifier.Create` factory strips the `@` into the token's IsVerbatim
    // flag, so the string accessor yields the bare `_` and the TOKEN is verbatim
    // (the faithful C# `new SimpleType("@_")` shape).
    EXPECT_EQ(*simple->Identifier(), "_");
    ASSERT_NE(simple->IdentifierToken(), nullptr);
    EXPECT_TRUE(simple->IdentifierToken()->IsVerbatim());
}

// ---------------------------------------------------------------------------
// ConvertNamespace / IsValidNamespace
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, ConvertNamespaceSinglePartWithoutResolver) {
    TypeSystemAstBuilder builder;
    std::shared_ptr<NamespaceResolveResult> nrr;

    AstType* result = builder.ConvertNamespace("System", nrr);

    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(*simple->Identifier(), "System");
    // Without a resolver every namespace is assumed valid, but no result is
    // produced (there is nothing to resolve against).
    EXPECT_EQ(nrr, nullptr);
}

TEST(TypeSystemAstBuilderConvertTypeTest, ConvertNamespaceMultiPartRecurses) {
    TypeSystemAstBuilder builder;
    std::shared_ptr<NamespaceResolveResult> nrr;

    AstType* result = builder.ConvertNamespace("System.Collections.Generic", nrr);

    auto* generic = dynamic_cast<MemberType*>(result);
    ASSERT_NE(generic, nullptr);
    EXPECT_EQ(generic->MemberName(), "Generic");
    auto* collections = dynamic_cast<MemberType*>(generic->Target());
    ASSERT_NE(collections, nullptr);
    EXPECT_EQ(collections->MemberName(), "Collections");
    auto* system = dynamic_cast<SimpleType*>(collections->Target());
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(*system->Identifier(), "System");
}

TEST(TypeSystemAstBuilderConvertTypeTest, ConvertNamespaceInvalidRendersGlobalForm) {
    auto compilation = std::make_unique<LookupCompilation>();
    auto resolver = std::make_shared<CSharpResolver>(*compilation);
    TypeSystemAstBuilder builder(resolver);
    std::shared_ptr<NamespaceResolveResult> nrr;

    // The empty compilation resolves no namespace: `System` is invalid, so the
    // global:: form renders.
    AstType* result = builder.ConvertNamespace("System", nrr);

    auto* memberType = dynamic_cast<MemberType*>(result);
    ASSERT_NE(memberType, nullptr);
    EXPECT_TRUE(memberType->IsDoubleColon());
    EXPECT_EQ(memberType->MemberName(), "System");
    auto* target = dynamic_cast<SimpleType*>(memberType->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(*target->Identifier(), "global");
}

TEST(TypeSystemAstBuilderConvertTypeTest, ConvertNamespaceValidThroughResolver) {
    auto compilation = std::make_unique<NamespacedCompilation>();
    ConfigurableNamespace systemNamespace("System", *compilation);
    compilation->Root().AddChildNamespace(&systemNamespace);
    auto resolver = std::make_shared<CSharpResolver>(*compilation);
    TypeSystemAstBuilder builder(resolver);
    std::shared_ptr<NamespaceResolveResult> nrr;

    AstType* result = builder.ConvertNamespace("System", nrr);

    // The resolver's global-namespace lookup finds the child: the plain SimpleType
    // form (not the global:: fallback).
    auto* simple = dynamic_cast<SimpleType*>(result);
    ASSERT_NE(simple, nullptr);
    EXPECT_EQ(*simple->Identifier(), "System");
}

TEST(TypeSystemAstBuilderConvertTypeTest, IsValidNamespaceWithoutResolverAssumesValid) {
    TypeSystemAstBuilder builder;
    std::shared_ptr<NamespaceResolveResult> nrr;

    EXPECT_TRUE(builder.IsValidNamespace("Anything", nrr));
    EXPECT_EQ(nrr, nullptr);
}

TEST(TypeSystemAstBuilderConvertTypeTest, IsValidNamespaceWithResolverResolves) {
    auto compilation = std::make_unique<NamespacedCompilation>();
    ConfigurableNamespace systemNamespace("System", *compilation);
    compilation->Root().AddChildNamespace(&systemNamespace);
    auto resolver = std::make_shared<CSharpResolver>(*compilation);
    TypeSystemAstBuilder builder(resolver);
    std::shared_ptr<NamespaceResolveResult> nrr;

    EXPECT_TRUE(builder.IsValidNamespace("System", nrr));
    ASSERT_NE(nrr, nullptr);
    EXPECT_EQ(nrr->NamespaceName(), "System");

    std::shared_ptr<NamespaceResolveResult> otherNrr;
    EXPECT_FALSE(builder.IsValidNamespace("NotThere", otherNrr));
    EXPECT_EQ(otherNrr, nullptr);
}

// ---------------------------------------------------------------------------
// The TypeSystemExtensions prerequisites
// ---------------------------------------------------------------------------

TEST(TypeSystemAstBuilderConvertTypeTest, IsUnboundRequiresDefinitionOrUnknownWithParameters) {
    auto compilation = MakeCompilation();
    auto genericDef = MakeDef("Foo", "Ns", 1, TypeKind::Class, KnownTypeCode::None,
                              *compilation);
    auto plainDef = MakeDef("Bar", "Ns", 0, TypeKind::Class, KnownTypeCode::None,
                            *compilation);
    // The elaborated-type-specifier (`class UnknownType`) targets the CLASS (the
    // `make_shared<UnknownType>`-resolves-to-the-function MSVC quirk, D417).
    auto unknown = ILSpy::Decompiler::TypeSystem::ITypePtr(
        new class ILSpy::Decompiler::TypeSystem::UnknownType(
            std::optional<std::string>("Ns"), std::string("Baz"), 1));

    using ILSpy::Decompiler::TypeSystem::IsUnbound;
    EXPECT_TRUE(IsUnbound(*genericDef));
    EXPECT_TRUE(IsUnbound(*unknown));
    EXPECT_FALSE(IsUnbound(*plainDef));
    // A parameterized type is never unbound (partially parameterized types are
    // excluded by the C# contract).
    ParameterizedType parameterized(genericDef, {plainDef});
    EXPECT_FALSE(IsUnbound(parameterized));
}

TEST(TypeSystemAstBuilderConvertTypeTest, GetTypeDefinitionByFullTypeNameWalksNesting) {
    auto compilation = MakeCompilation();
    auto outer = MakeDef("Outer", "Ns", 0, TypeKind::Class, KnownTypeCode::None,
                         *compilation);
    auto inner = MakeDef("Inner", "Ns", 1, TypeKind::Class, KnownTypeCode::None,
                         *compilation);
    // The nested type's DeclaringTypeDefinition chain drives the nesting walk.
    inner->SetDeclaringTypeDefinition(outer.get());
    // NestedTypes: the ITypeDefinition virtual (the shared stub returns {}); a
    // local subclass configures it.
    struct NestedHost : LookupTypeDefinition {
        using LookupTypeDefinition::LookupTypeDefinition;
        void SetNested(std::vector<const ITypeDefinition*> nested) { nested_ = std::move(nested); }
        std::vector<const ITypeDefinition*> NestedTypes() const override { return nested_; }
        std::vector<const ITypeDefinition*> nested_;
    };
    auto outerHost = std::make_shared<NestedHost>(
        "Outer", "Ns", FullTypeName(TopLevelTypeName("Ns", "Outer", 0)), TypeKind::Class,
        Accessibility::Public, *compilation, nullptr, KnownTypeCode::None);
    outerHost->SetNested({inner.get()});

    TestModule module(*compilation, "TestModule");
    module.AddTypeDef(outerHost.get());

    using ILSpy::Decompiler::TypeSystem::GetTypeDefinition;
    // The top-level name resolves.
    EXPECT_EQ(GetTypeDefinition(module, FullTypeName(TopLevelTypeName("Ns", "Outer", 0))),
              outerHost.get());
    // The nested name walks the NestedTypes table (name + accumulated
    // type-parameter count).
    EXPECT_EQ(GetTypeDefinition(module, FullTypeName("Ns.Outer+Inner`1")),
              inner.get());
    // A missing top-level name yields null.
    EXPECT_EQ(GetTypeDefinition(module, FullTypeName(TopLevelTypeName("Ns", "Missing", 0))),
              nullptr);
}
