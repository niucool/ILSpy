// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `ForEachResolveResult` (cpp/Decompiler/Semantics/ForEachResolveResult.hpp,
// the D445 port of ICSharpCode.Decompiler/Semantics/ForEachResolveResult.cs) -- the
// result of a `foreach` loop. Derives directly from `ResolveResult` (D424) and adds six
// readonly fields: `GetEnumeratorCall` (a `ResolveResult`), `CollectionType` /
// `EnumeratorType` / `ElementType` (three `IType`s), and the nullable `CurrentProperty`
// (`IProperty`) / `MoveNextMethod` (`IMethod`); the `voidType` is forwarded to the base.
// The C# declares NO virtual overrides (it inherits the `ResolveResult` defaults), so the
// only overrides are the C++-only `ClassName` / `ShallowClone` convention pair.
//
// The tests pin the ctor-stores-all-fields contract (the four non-null asserts + the two
// nullable member back-references), the `GetEnumeratorCall` / `CollectionType` /
// `EnumeratorType` / `ElementType` / `CurrentProperty` / `MoveNextMethod` accessors
// (incl. the nullable-null case), the `ToString` subclass-class-name format, the
// `ShallowClone` runtime-type preservation plus shared-ownership of the `GetEnumeratorCall`
// and the three `IType`s plus pointer-copy of the two nullable members, the inherited
// `ResolveResult` defaults (`GetChildResults` empty, `IsError` false, etc.), virtual
// dispatch through the base pointer, and the `is_base_of` / `has_virtual_destructor` /
// `is_polymorphic` / not-`final` static-asserts.

#include "Decompiler/Semantics/ForEachResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// Convenience: a `KnownType(Void)` -- the foreach result type forwarded to the base
// (`ResolveResult::Type()`).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeVoidType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void);
}

// Convenience: a `KnownType(Int32)` -- the element / collection / enumerator type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: a `KnownType(Object)` -- a distinct collection type for the
// `GetEnumeratorCall`'s own result.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// of the `IProperty` / `IMethod` stubs can return a compilation (the D399 test stand-in
// pattern, identical in shape to the `TestCompilation` in `MemberResolveResult_Test.cpp`).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id), mainModule_(*this) {}
    int id() const { return id_; }
    const ILSpy::Decompiler::TypeSystem::IModule& MainModule() const override { return mainModule_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> Modules() const override { return {&mainModule_}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ReferencedModules() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetNamespaceForExternAlias(const std::string&) const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IType& FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode) const override { return knownType_; }
    const ILSpy::Decompiler::TypeSystem::StringComparer& NameComparer() const override { return ILSpy::Decompiler::TypeSystem::StringComparer::Ordinal(); }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override { return cacheManager_; }
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override { return ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None; }
private:
    int id_;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `IProperty` for testing the non-null `CurrentProperty` case: holds a
// `TestCompilation` reference (for the inherited `ICompilationProvider::Compilation`) and a
// return type (for the inherited `IMember::ReturnType`), and returns trivial defaults for
// every other accessor. Only pointer-identity (the address) is exercised by the
// `ForEachResolveResult` tests, so the field values are not configured.
class TestCurrentProperty : public ILSpy::Decompiler::TypeSystem::IProperty {
public:
    explicit TestCurrentProperty(const TestCompilation& compilation)
        : compilation_(compilation),
          returnType_(std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
              ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32)) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }
    std::string Name() const override { return "Current"; }

    // --- INamedElement ---
    std::string FullName() const override { return "Current"; }
    std::string ReflectionName() const override { return "Current"; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override { return {}; }

    // --- IProperty ---
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Getter() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Setter() const override { return nullptr; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
};

// A minimal concrete `IMethod` for testing the non-null `MoveNextMethod` case: holds a
// `TestCompilation` reference (for the inherited `ICompilationProvider::Compilation`) and a
// return type (for the inherited `IMember::ReturnType`), and returns trivial defaults for
// every other accessor. Only pointer-identity (the address) is exercised by the
// `ForEachResolveResult` tests, so the field values are not configured.
class TestMoveNextMethod : public ILSpy::Decompiler::TypeSystem::IMethod {
public:
    explicit TestMoveNextMethod(const TestCompilation& compilation)
        : compilation_(compilation),
          returnType_(std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
              ILSpy::Decompiler::TypeSystem::KnownTypeCode::Boolean)) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    std::string Name() const override { return "MoveNext"; }

    // --- INamedElement ---
    std::string FullName() const override { return "MoveNext"; }
    std::string ReflectionName() const override { return "MoveNext"; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    // The covariant `IMethod::Specialize` override (the D389 convention).
    const ILSpy::Decompiler::TypeSystem::IMethod* Specialize(const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override { return {}; }

    // --- IMethod ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetReturnTypeAttributes() const override { return {}; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> TypeParameters() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IMember* AccessorOwner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind() const override {
        return ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::None;
    }
    const ILSpy::Decompiler::TypeSystem::IMethod* ReducedFrom() const override { return nullptr; }

private:
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
};

// Convenience: build a `TypeResolveResult` over `KnownType(Object)` -- the
// `GetEnumeratorCall`.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> MakeGetEnumeratorCall()
{
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType());
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor forwards the void type to the base `ResolveResult` and stores the six fields.
// `Type()` is the void type (`KnownType(Void)`).
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, CtorStoresAllFields)
{
    auto getEnumeratorCall = MakeGetEnumeratorCall();
    auto* getEnumeratorCallPtr = getEnumeratorCall.get();
    auto collectionType = MakeInt32Type();
    auto* collectionTypePtr = collectionType.get();
    auto enumeratorType = MakeInt32Type();
    auto* enumeratorTypePtr = enumeratorType.get();
    auto elementType = MakeInt32Type();
    auto* elementTypePtr = elementType.get();
    auto voidType = MakeVoidType();
    auto* voidTypePtr = voidType.get();
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        getEnumeratorCall, collectionType, enumeratorType, elementType,
        /*currentProperty*/ nullptr, /*moveNextMethod*/ nullptr, voidType);
    EXPECT_EQ(&rr.Type(), voidTypePtr);
    EXPECT_EQ(rr.GetEnumeratorCall(), getEnumeratorCallPtr);
    EXPECT_EQ(&rr.CollectionType(), collectionTypePtr);
    EXPECT_EQ(&rr.EnumeratorType(), enumeratorTypePtr);
    EXPECT_EQ(&rr.ElementType(), elementTypePtr);
    EXPECT_EQ(rr.CurrentProperty(), nullptr);
    EXPECT_EQ(rr.MoveNextMethod(), nullptr);
}

// ---------------------------------------------------------------------------
// The `GetEnumeratorCall()` accessor returns the stored `GetEnumeratorCall` by pointer
// identity.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, GetEnumeratorCallAccessorReturnsStoredResult)
{
    auto getEnumeratorCall = MakeGetEnumeratorCall();
    auto* ptr = getEnumeratorCall.get();
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        getEnumeratorCall, MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        nullptr, nullptr, MakeVoidType());
    EXPECT_EQ(rr.GetEnumeratorCall(), ptr);
}

// ---------------------------------------------------------------------------
// The `CollectionType()` / `EnumeratorType()` / `ElementType()` accessors return the
// stored `IType`s by reference identity.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, CollectionEnumeratorAndElementAccessorsReturnStoredTypes)
{
    auto collectionType = MakeInt32Type();
    auto* collectionTypePtr = collectionType.get();
    auto enumeratorType = MakeObjectType();
    auto* enumeratorTypePtr = enumeratorType.get();
    auto elementType = MakeInt32Type();
    auto* elementTypePtr = elementType.get();
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), collectionType, enumeratorType, elementType,
        nullptr, nullptr, MakeVoidType());
    EXPECT_EQ(&rr.CollectionType(), collectionTypePtr);
    EXPECT_EQ(&rr.EnumeratorType(), enumeratorTypePtr);
    EXPECT_EQ(&rr.ElementType(), elementTypePtr);
}

// ---------------------------------------------------------------------------
// The `CurrentProperty()` / `MoveNextMethod()` accessors return the stored nullable member
// back-references by pointer identity. The C# ctor does NOT guard these (they may be null
// if the property / method is not found); the non-null case stores a non-owning raw pointer.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, CurrentPropertyAndMoveNextMethodAccessorsReturnStoredMembers)
{
    TestCompilation compilation(1);
    TestCurrentProperty currentProperty(compilation);
    TestMoveNextMethod moveNextMethod(compilation);
    auto* currentPropertyPtr = &currentProperty;
    auto* moveNextMethodPtr = &moveNextMethod;
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        currentPropertyPtr, moveNextMethodPtr, MakeVoidType());
    EXPECT_EQ(rr.CurrentProperty(), currentPropertyPtr);
    EXPECT_EQ(rr.MoveNextMethod(), moveNextMethodPtr);
}

// ---------------------------------------------------------------------------
// The nullable members may be null (the C# "Returns null if the property / method is not
// found" case); the ctor accepts null pointers with no assert.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, NullableMembersAcceptNull)
{
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        nullptr, nullptr, MakeVoidType());
    EXPECT_EQ(rr.CurrentProperty(), nullptr);
    EXPECT_EQ(rr.MoveNextMethod(), nullptr);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the void type (the inherited
// `ResolveResult::ToString` uses the polymorphic `ClassName()` which the override returns
// "ForEachResolveResult"; the void type is `KnownType(Void)` whose `ReflectionName()` is
// "System.Void" (the `Namespace.Name` form).
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, ToStringReportsSubclassClassNameAndVoidType)
{
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        nullptr, nullptr, MakeVoidType());
    EXPECT_EQ(rr.ToString(), "[ForEachResolveResult System.Void]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is a `ForEachResolveResult`, not the
// `ResolveResult` base a non-overriding clone would slice to). The clone's `ToString()`
// reports "ForEachResolveResult" (the virtual dispatch through the clone).
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, ShallowClonePreservesRuntimeType)
{
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        nullptr, nullptr, MakeVoidType());
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[ForEachResolveResult System.Void]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `GetEnumeratorCall` and the three `IType`s (the default copy
// ctor shares the `getEnumeratorCall_` shared_ptr and the three `ITypePtr`s faithfully
// mirroring the C# `MemberwiseClone` reference-copy). The clone's fields are pointer-
// identical to the original's.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, ShallowCloneSharesGetEnumeratorCallAndTypes)
{
    auto getEnumeratorCall = MakeGetEnumeratorCall();
    auto* getEnumeratorCallPtr = getEnumeratorCall.get();
    auto collectionType = MakeInt32Type();
    auto* collectionTypePtr = collectionType.get();
    auto enumeratorType = MakeInt32Type();
    auto* enumeratorTypePtr = enumeratorType.get();
    auto elementType = MakeInt32Type();
    auto* elementTypePtr = elementType.get();
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        getEnumeratorCall, collectionType, enumeratorType, elementType,
        nullptr, nullptr, MakeVoidType());
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::ForEachResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->GetEnumeratorCall(), getEnumeratorCallPtr);
    EXPECT_EQ(&derived->CollectionType(), collectionTypePtr);
    EXPECT_EQ(&derived->EnumeratorType(), enumeratorTypePtr);
    EXPECT_EQ(&derived->ElementType(), elementTypePtr);
}

// ---------------------------------------------------------------------------
// `ShallowClone` copies the two nullable member back-references (raw pointers are copied by
// the default copy ctor); a non-null `CurrentProperty` / `MoveNextMethod` round-trips through
// the clone by pointer identity.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, ShallowCloneCopiesNullableMemberBackReferences)
{
    TestCompilation compilation(1);
    TestCurrentProperty currentProperty(compilation);
    TestMoveNextMethod moveNextMethod(compilation);
    auto* currentPropertyPtr = &currentProperty;
    auto* moveNextMethodPtr = &moveNextMethod;
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        currentPropertyPtr, moveNextMethodPtr, MakeVoidType());
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::ForEachResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->CurrentProperty(), currentPropertyPtr);
    EXPECT_EQ(derived->MoveNextMethod(), moveNextMethodPtr);
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the original).
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::ForEachResolveResult>(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        nullptr, nullptr, MakeVoidType());
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `ResolveResult*` base pointer: the inherited
// `ResolveResult` defaults are reached through the base pointer (the C# resolver reaches
// the `ForEachResolveResult` through a `ResolveResult` reference). The `ClassName()` override
// dispatches through the base pointer in `ToString()`.
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, VirtualDispatchThroughBasePointer)
{
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::ForEachResolveResult>(
            MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
            nullptr, nullptr, MakeVoidType());
    EXPECT_EQ(rr->ToString(), "[ForEachResolveResult System.Void]");
    EXPECT_FALSE(rr->IsError());
}

// ---------------------------------------------------------------------------
// The inherited `ResolveResult` defaults are preserved (the C# declares NO virtual
// overrides): `GetChildResults` is empty (a `foreach` loop's `GetEnumeratorCall` is a
// field, not a child result), `IsCompileTimeConstant` is false, `ConstantValue` is empty,
// and `IsError` is false (the base defaults).
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, InheritedResolveResultDefaultsArePreserved)
{
    ILSpy::Decompiler::Semantics::ForEachResolveResult rr(
        MakeGetEnumeratorCall(), MakeInt32Type(), MakeInt32Type(), MakeInt32Type(),
        nullptr, nullptr, MakeVoidType());
    EXPECT_EQ(rr.GetChildResults().size(), 0u);
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `ForEachResolveResult` IS-A `ResolveResult`, is polymorphic with a
// virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(ForEachResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::ForEachResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::ForEachResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::ForEachResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::ForEachResolveResult>);
}
