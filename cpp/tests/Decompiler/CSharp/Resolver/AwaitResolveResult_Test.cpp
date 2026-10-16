// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `AwaitResolveResult` (the sixth `cpp/Decompiler/CSharp/Resolver/` leaf, the
// port of ICSharpCode.Decompiler/CSharp/Resolver/AwaitResolveResult.cs -- the result of an
// `await` expression). The class derives from `ResolveResult` (D424), forwards `resultType`
// to the base, and carries a `ResolveResult GetAwaiterInvocation` (a held child), a non-null
// `IType AwaiterType` (may be `UnknownType`), and three nullable awaiter-pattern members
// (`IProperty IsCompletedProperty` / `IMethod OnCompletedMethod` / `IMethod GetResultMethod`).
// The load-bearing crux is the `IsError` override:
//   GetAwaiterInvocation.IsError || (AwaiterType.Kind != TypeKind.Dynamic &&
//   (IsCompletedProperty == null || OnCompletedMethod == null || GetResultMethod == null))
// -- the await is an error when the GetAwaiter() call is an error OR the awaiter type is
// NOT dynamic (a dynamic await skips the member-presence check) AND any of the three
// awaiter-pattern members is null. The tests pin the ctor-stores-fields contract, the four
// `IsError` branches (non-dynamic-all-present, non-dynamic-a-member-missing, dynamic-no-
// members, GetAwaiterInvocation-error), the `GetChildResults` snapshot, the inherited
// `ToString`, the `ShallowClone` runtime-type preservation + shared/copied fields, the
// polymorphic dispatch, and the class-shape static-asserts.

#include "Decompiler/CSharp/Resolver/AwaitResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

// A minimal concrete `ICompilation` stand-in so the `IProperty`/`IMethod` stubs' inherited
// `ICompilationProvider::Compilation()` can return a compilation (the D399 test stand-in
// pattern, identical in shape to the `TestCompilation` in `ForEachResolveResult_Test.cpp`).
class TestCompilation : public TS::ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}
    const TS::IModule& MainModule() const override { return mainModule_; }
    std::vector<const TS::IModule*> Modules() const override { return {&mainModule_}; }
    std::vector<const TS::IModule*> ReferencedModules() const override { return {}; }
    const TS::INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override { return nullptr; }
    const TS::IType& FindType(TS::KnownTypeCode) const override { return knownType_; }
    const TS::StringComparer& NameComparer() const override { return TS::StringComparer::Ordinal(); }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override { return cacheManager_; }
    TS::TypeSystemOptions TypeSystemOptions() const override { return TS::TypeSystemOptions::None; }
private:
    TS::TestSupport::TestModule mainModule_;
    TS::KnownType knownType_{TS::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `IProperty` for testing the non-null `IsCompletedProperty` case:
// holds a `TestCompilation` reference (for `ICompilationProvider::Compilation`) and a
// return type (for `IMember::ReturnType`); returns trivial defaults for every other
// accessor. Only pointer-identity (the address) is exercised by the tests. The shape is
// identical to `TestCurrentProperty` in `ForEachResolveResult_Test.cpp`.
class TestIsCompletedProperty : public TS::IProperty {
public:
    explicit TestIsCompletedProperty(const TestCompilation& compilation)
        : compilation_(compilation),
          returnType_(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean)) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Property; }
    std::string Name() const override { return "IsCompleted"; }
    // --- INamedElement ---
    std::string FullName() const override { return "IsCompleted"; }
    std::string ReflectionName() const override { return "IsCompleted"; }
    std::string Namespace() const override { return {}; }
    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override { return compilation_; }
    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    TS::ITypePtr DeclaringType() const override { return {}; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *returnType_; }
    std::vector<const TS::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const TS::IMember* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
    // --- IProperty ---
    bool CanGet() const override { return true; }
    bool CanSet() const override { return false; }
    const TS::IMethod* Getter() const override { return nullptr; }
    const TS::IMethod* Setter() const override { return nullptr; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
private:
    const TestCompilation& compilation_;
    TS::ITypePtr returnType_;
};

// A minimal concrete `IMethod` for testing the non-null `OnCompletedMethod` /
// `GetResultMethod` cases: holds a `TestCompilation` reference and a return type; returns
// trivial defaults for every other accessor. The shape is identical to
// `TestMoveNextMethod` in `ForEachResolveResult_Test.cpp`.
class TestAwaiterMethod : public TS::IMethod {
public:
    explicit TestAwaiterMethod(const TestCompilation& compilation, std::string name)
        : compilation_(compilation), name_(std::move(name)),
          returnType_(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void)) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return name_; }
    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override { return compilation_; }
    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    TS::ITypePtr DeclaringType() const override { return {}; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *returnType_; }
    std::vector<const TS::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const TS::IMethod* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
    // --- IMethod ---
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes() const override { return {}; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override { return {}; }
    std::vector<TS::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override { return TS::MethodSemanticsAttributes::None; }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }
private:
    const TestCompilation& compilation_;
    std::string name_;
    TS::ITypePtr returnType_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as a result / awaiter type.
TS::ITypePtr MakeObjectType()
{
    return std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
}

// A non-error `GetAwaiterInvocation` (a `TypeResolveResult` over a known type).
std::shared_ptr<Sem::ResolveResult> MakeNonErrorGetAwaiter()
{
    return std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
}

} // namespace

// ===========================================================================
// ctor -- stores the five fields and forwards resultType to the base.
// ===========================================================================

TEST(AwaitResolveResultTest, ConstructorStoresAllFields)
{
    TestCompilation compilation;
    auto getAwaiter = MakeNonErrorGetAwaiter();
    auto awaiterType = MakeObjectType();
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");

    Res::AwaitResolveResult rr(
        MakeObjectType(), getAwaiter, awaiterType,
        &isCompleted, &onCompleted, &getResult);

    EXPECT_EQ(rr.GetAwaiterInvocation(), getAwaiter.get());
    EXPECT_EQ(&rr.AwaiterType(), awaiterType.get());
    EXPECT_EQ(rr.IsCompletedProperty(), &isCompleted);
    EXPECT_EQ(rr.OnCompletedMethod(), &onCompleted);
    EXPECT_EQ(rr.GetResultMethod(), &getResult);
}

TEST(AwaitResolveResultTest, ConstructorAcceptsAllNullAwaiterMembers)
{
    // The three awaiter-pattern members are nullable (the C# does not guard them).
    auto getAwaiter = MakeNonErrorGetAwaiter();
    Res::AwaitResolveResult rr(
        MakeObjectType(), getAwaiter, MakeObjectType(),
        nullptr, nullptr, nullptr);
    EXPECT_EQ(rr.IsCompletedProperty(), nullptr);
    EXPECT_EQ(rr.OnCompletedMethod(), nullptr);
    EXPECT_EQ(rr.GetResultMethod(), nullptr);
}

TEST(AwaitResolveResultTest, ConstructorForwardsResultTypeToBase)
{
    auto getAwaiter = MakeNonErrorGetAwaiter();
    auto resultType = MakeObjectType();
    Res::AwaitResolveResult rr(
        resultType, getAwaiter, MakeObjectType(),
        nullptr, nullptr, nullptr);
    EXPECT_EQ(&rr.Type(), resultType.get());
}

// ===========================================================================
// IsError -- the load-bearing crux (four branches).
// ===========================================================================

TEST(AwaitResolveResultTest, IsErrorFalseWhenNonDynamicAndAllMembersPresent)
{
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), MakeObjectType(),
        &isCompleted, &onCompleted, &getResult);
    EXPECT_FALSE(rr.IsError());
}

TEST(AwaitResolveResultTest, IsErrorTrueWhenNonDynamicAndIsCompletedPropertyMissing)
{
    TestCompilation compilation;
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), MakeObjectType(),
        nullptr, &onCompleted, &getResult);
    EXPECT_TRUE(rr.IsError());
}

TEST(AwaitResolveResultTest, IsErrorTrueWhenNonDynamicAndOnCompletedMethodMissing)
{
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), MakeObjectType(),
        &isCompleted, nullptr, &getResult);
    EXPECT_TRUE(rr.IsError());
}

TEST(AwaitResolveResultTest, IsErrorTrueWhenNonDynamicAndGetResultMethodMissing)
{
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), MakeObjectType(),
        &isCompleted, &onCompleted, nullptr);
    EXPECT_TRUE(rr.IsError());
}

TEST(AwaitResolveResultTest, IsErrorFalseWhenDynamicAndNoMembersPresent)
{
    // A dynamic await skips the member-presence check (the AwaiterType.Kind ==
    // TypeKind::Dynamic short-circuit), so an await with no awaiter-pattern members is
    // NOT an error when the awaiter type is dynamic.
    auto dynamicType = std::make_shared<TS::SpecialType>(TS::TypeKind::Dynamic, true);
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), dynamicType,
        nullptr, nullptr, nullptr);
    EXPECT_FALSE(rr.IsError());
}

TEST(AwaitResolveResultTest, IsErrorTrueWhenGetAwaiterInvocationIsError)
{
    // The GetAwaiterInvocation.IsError short-circuit: an error GetAwaiter() call makes the
    // await an error regardless of the awaiter type or the members (an error
    // TypeResolveResult over UnknownType, which the D425 IsError override flags).
    auto errorGetAwaiter = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), errorGetAwaiter, MakeObjectType(),
        &isCompleted, &onCompleted, &getResult);
    EXPECT_TRUE(rr.IsError());
}

TEST(AwaitResolveResultTest, IsErrorFalseWhenDynamicAndAllMembersPresent)
{
    // The dynamic short-circuit does NOT flip IsError to true when the members ARE present
    // (it only skips the not-found check); a dynamic awaiter with all members is not an
    // error.
    auto dynamicType = std::make_shared<TS::SpecialType>(TS::TypeKind::Dynamic, true);
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), dynamicType,
        &isCompleted, &onCompleted, &getResult);
    EXPECT_FALSE(rr.IsError());
}

// ===========================================================================
// GetChildResults -- the single-element snapshot { GetAwaiterInvocation }.
// ===========================================================================

TEST(AwaitResolveResultTest, GetChildResultsReturnsGetAwaiterInvocation)
{
    auto getAwaiter = MakeNonErrorGetAwaiter();
    Res::AwaitResolveResult rr(
        MakeObjectType(), getAwaiter, MakeObjectType(),
        nullptr, nullptr, nullptr);
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], getAwaiter.get());
}

// ===========================================================================
// ToString -- the inherited ResolveResult::ToString bracket form (the C# does NOT
// override ToString), yielding "[AwaitResolveResult <resultType ReflectionName>]".
// ===========================================================================

TEST(AwaitResolveResultTest, ToStringUsesInheritedBracketForm)
{
    auto getAwaiter = MakeNonErrorGetAwaiter();
    auto resultType = MakeObjectType();
    Res::AwaitResolveResult rr(
        resultType, getAwaiter, MakeObjectType(),
        nullptr, nullptr, nullptr);
    // The inherited ResolveResult::ToString => "[" + ClassName() + " " +
    // type_->ReflectionName() + "]"; ClassName() is "AwaitResolveResult"; a
    // KnownType(Object)'s ReflectionName is "System.Object".
    EXPECT_EQ(rr.ToString(), "[AwaitResolveResult System.Object]");
}

// ===========================================================================
// ShallowClone -- preserves the runtime type, shares the GetAwaiterInvocation, shares
// the AwaiterType, copies the three raw pointers.
// ===========================================================================

TEST(AwaitResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), MakeObjectType(),
        &isCompleted, &onCompleted, &getResult);

    auto clone = rr.ShallowClone();
    EXPECT_NE(clone, nullptr);
    EXPECT_EQ(dynamic_cast<Res::AwaitResolveResult*>(clone.get()), clone.get());
}

TEST(AwaitResolveResultTest, ShallowCloneSharesGetAwaiterInvocationAndAwaiterType)
{
    auto getAwaiter = MakeNonErrorGetAwaiter();
    auto awaiterType = MakeObjectType();
    Res::AwaitResolveResult rr(
        MakeObjectType(), getAwaiter, awaiterType,
        nullptr, nullptr, nullptr);

    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::AwaitResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->GetAwaiterInvocation(), getAwaiter.get());
    EXPECT_EQ(&cloned->AwaiterType(), awaiterType.get());
}

TEST(AwaitResolveResultTest, ShallowCloneCopiesAwaiterMemberPointers)
{
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    Res::AwaitResolveResult rr(
        MakeObjectType(), MakeNonErrorGetAwaiter(), MakeObjectType(),
        &isCompleted, &onCompleted, &getResult);

    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::AwaitResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->IsCompletedProperty(), &isCompleted);
    EXPECT_EQ(cloned->OnCompletedMethod(), &onCompleted);
    EXPECT_EQ(cloned->GetResultMethod(), &getResult);
}

TEST(AwaitResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto getAwaiter = MakeNonErrorGetAwaiter();
    Res::AwaitResolveResult rr(
        MakeObjectType(), getAwaiter, MakeObjectType(),
        nullptr, nullptr, nullptr);
    auto clone = rr.ShallowClone();
    EXPECT_NE(static_cast<Sem::ResolveResult*>(&rr), clone.get());
}

// ===========================================================================
// Polymorphic dispatch through a ResolveResult* base pointer.
// ===========================================================================

TEST(AwaitResolveResultTest, DispatchesThroughResolveResultBasePointer)
{
    TestCompilation compilation;
    TestIsCompletedProperty isCompleted(compilation);
    TestAwaiterMethod onCompleted(compilation, "OnCompleted");
    TestAwaiterMethod getResult(compilation, "GetResult");
    auto getAwaiter = MakeNonErrorGetAwaiter();
    Res::AwaitResolveResult rr(
        MakeObjectType(), getAwaiter, MakeObjectType(),
        &isCompleted, &onCompleted, &getResult);
    Sem::ResolveResult* base = &rr;
    EXPECT_FALSE(base->IsError());
    auto children = base->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], getAwaiter.get());
}

// ===========================================================================
// Class shape -- is_base_of / not-final / has-virtual-destructor / is-polymorphic.
// ===========================================================================

static_assert(std::is_base_of<Sem::ResolveResult, Res::AwaitResolveResult>::value,
              "AwaitResolveResult derives from ResolveResult");
static_assert(!std::is_final<Res::AwaitResolveResult>::value,
              "AwaitResolveResult is not final (the C# class is unsealed)");
static_assert(std::has_virtual_destructor<Sem::ResolveResult>::value,
              "ResolveResult has a virtual destructor (deletion through base is safe)");
static_assert(std::is_polymorphic<Res::AwaitResolveResult>::value,
              "AwaitResolveResult is polymorphic (supports dynamic_cast)");
