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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `DynamicMemberResolveResult` (the fifth `cpp/Decompiler/CSharp/Resolver/`
// leaf, the port of ICSharpCode.Decompiler/CSharp/Resolver/DynamicMemberResolveResult.cs
// -- the result of an access to a member of a `dynamic` object). The class derives from
// `ResolveResult` (D424) with a `SpecialType.Dynamic` base type, carries a
// `ResolveResult Target` (a held child), a `string Member` (the accessed name), and a
// nullable `IMember Symbol` (a navigability hint, never dereferenced by the class).
// The tests pin the ctor-stores-target/member/symbol + forwards-dynamic-type contract,
// the `Type()` is `TypeKind::Dynamic` (the `SpecialType.Dynamic` base) with
// `IsReferenceType() == true`, the `Target()`/`Member()`/`Symbol()` accessors
// (pointer-identity for Target/Symbol, value for Member), the custom `ToString` format
// `"[Dynamic member 'name']"`, the `GetChildResults` single-element snapshot, the
// inherited `IsError == false` base default, the `ShallowClone` runtime-type
// preservation + shared-target + value-copied-member + copied-symbol, the polymorphic
// dispatch through a `ResolveResult*` base pointer, and the `is_base_of` / not-`final`
// / has-virtual-destructor / is-polymorphic class-shape static-asserts.

#include "Decompiler/CSharp/Resolver/DynamicMemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
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

// A minimal concrete `ICompilation` stand-in so the `TestMember` stub's inherited
// `ICompilationProvider::Compilation()` can return a compilation (the D399 test
// stand-in pattern, identical in shape to the `TestCompilation` in
// `AmbiguousResolveResult_Test.cpp` / `MemberResolveResult_Test.cpp`). Holds a
// `TestSupport::TestModule` bound to `*this` so `MainModule()` / `RootNamespace()`
// return valid references.
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

// A minimal concrete `IMember` for testing the non-null `Symbol` case: holds a name and
// returns it from every accessor. The `Symbol` field is never dereferenced by
// `DynamicMemberResolveResult` itself (it is stored and returned verbatim), so the
// stub's accessors are dead code that merely needs to compile. The shape is identical
// to the `TestMember` in `AmbiguousResolveResult_Test.cpp` / `MemberResolveResult_Test.cpp`.
class TestMember : public TS::IMember {
public:
    TestMember(std::string name, const TestCompilation& compilation)
        : name_(std::move(name)), compilation_(compilation),
          returnType_(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object)) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Property; }
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
    const TS::IMember* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }

private:
    std::string name_;
    const TestCompilation& compilation_;
    TS::ITypePtr returnType_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as a target's type.
TS::ITypePtr MakeObjectType()
{
    return std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
}

} // namespace

// ===========================================================================
// ctor -- stores the target / member / symbol and forwards SpecialType.Dynamic.
// ===========================================================================

TEST(DynamicMemberResolveResultTest, ConstructorStoresTargetAndMemberAndSymbol)
{
    TestCompilation compilation;
    auto target = std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
    TestMember symbolMember("Count", compilation);

    Res::DynamicMemberResolveResult rr(target, "Length", &symbolMember);
    EXPECT_EQ(rr.Target(), target.get());
    EXPECT_EQ(rr.Member(), "Length");
    EXPECT_EQ(rr.Symbol(), &symbolMember);
}

TEST(DynamicMemberResolveResultTest, ConstructorDefaultsSymbolToNull)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "Foo");
    EXPECT_EQ(rr.Target(), target.get());
    EXPECT_EQ(rr.Member(), "Foo");
    EXPECT_EQ(rr.Symbol(), nullptr);
}

// ===========================================================================
// Type -- the base SpecialType.Dynamic (TypeKind::Dynamic, isReferenceType: true).
// ===========================================================================

TEST(DynamicMemberResolveResultTest, TypeIsSpecialTypeDynamic)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "M");
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::Dynamic);
}

TEST(DynamicMemberResolveResultTest, TypeIsReferenceType)
{
    // The C# SpecialType.Dynamic singleton carries isReferenceType: true; the C++ port
    // forwards std::make_shared<SpecialType>(TypeKind::Dynamic, true) to the base.
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "M");
    ASSERT_TRUE(rr.Type().IsReferenceType().has_value());
    EXPECT_TRUE(*rr.Type().IsReferenceType());
}

// ===========================================================================
// ToString -- the custom "[Dynamic member 'name']" format (NOT the inherited bracket).
// ===========================================================================

TEST(DynamicMemberResolveResultTest, ToStringUsesDynamicMemberFormat)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "Count");
    EXPECT_EQ(rr.ToString(), "[Dynamic member 'Count']");
}

TEST(DynamicMemberResolveResultTest, ToStringEmbedsMemberNameVerbatim)
{
    // The member name is embedded verbatim (no normalization); an empty name yields the
    // bare "[Dynamic member '']" form.
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, std::string());
    EXPECT_EQ(rr.ToString(), "[Dynamic member '']");
}

// ===========================================================================
// GetChildResults -- the single-element snapshot { Target }.
// ===========================================================================

TEST(DynamicMemberResolveResultTest, GetChildResultsReturnsTarget)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "M");
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], target.get());
}

// ===========================================================================
// IsError -- the inherited base default (false; a dynamic member access is never an
// error per the ResolveResult base).
// ===========================================================================

TEST(DynamicMemberResolveResultTest, IsErrorIsFalseByDefault)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "M");
    EXPECT_FALSE(rr.IsError());
}

// ===========================================================================
// ShallowClone -- preserves the runtime type, shares the Target, value-copies the
// Member, copies the Symbol.
// ===========================================================================

TEST(DynamicMemberResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestCompilation compilation;
    auto target = std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
    TestMember symbolMember("Count", compilation);
    Res::DynamicMemberResolveResult rr(target, "Length", &symbolMember);

    auto clone = rr.ShallowClone();
    EXPECT_NE(clone, nullptr);
    // The clone is a DynamicMemberResolveResult (not sliced to the ResolveResult base).
    EXPECT_EQ(dynamic_cast<Res::DynamicMemberResolveResult*>(clone.get()), clone.get());
}

TEST(DynamicMemberResolveResultTest, ShallowCloneSharesTarget)
{
    TestCompilation compilation;
    auto target = std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
    TestMember symbolMember("Count", compilation);
    Res::DynamicMemberResolveResult rr(target, "Length", &symbolMember);

    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::DynamicMemberResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    // The target shared_ptr is shared (pointer-identity): the clone's Target is the
    // same ResolveResult as the original's.
    EXPECT_EQ(cloned->Target(), target.get());
}

TEST(DynamicMemberResolveResultTest, ShallowCloneValueCopiesMemberAndSymbol)
{
    TestCompilation compilation;
    auto target = std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
    TestMember symbolMember("Count", compilation);
    Res::DynamicMemberResolveResult rr(target, "Length", &symbolMember);

    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::DynamicMemberResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->Member(), "Length");
    EXPECT_EQ(cloned->Symbol(), &symbolMember);
}

TEST(DynamicMemberResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "M");
    auto clone = rr.ShallowClone();
    EXPECT_NE(static_cast<Sem::ResolveResult*>(&rr), clone.get());
}

// ===========================================================================
// Polymorphic dispatch through a ResolveResult* base pointer.
// ===========================================================================

TEST(DynamicMemberResolveResultTest, DispatchesThroughResolveResultBasePointer)
{
    auto target = std::make_shared<Sem::TypeResolveResult>(TS::UnknownType());
    Res::DynamicMemberResolveResult rr(target, "Count");
    Sem::ResolveResult* base = &rr;
    EXPECT_EQ(base->Type().Kind(), TS::TypeKind::Dynamic);
    EXPECT_EQ(base->ToString(), "[Dynamic member 'Count']");
    auto children = base->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], target.get());
    EXPECT_FALSE(base->IsError());
}

// ===========================================================================
// Class shape -- is_base_of / not-final / has-virtual-destructor / is-polymorphic.
// ===========================================================================

static_assert(std::is_base_of<Sem::ResolveResult, Res::DynamicMemberResolveResult>::value,
              "DynamicMemberResolveResult derives from ResolveResult");
static_assert(!std::is_final<Res::DynamicMemberResolveResult>::value,
              "DynamicMemberResolveResult is not final (the C# class is unsealed)");
static_assert(std::has_virtual_destructor<Sem::ResolveResult>::value,
              "ResolveResult has a virtual destructor (deletion through base is safe)");
static_assert(std::is_polymorphic<Res::DynamicMemberResolveResult>::value,
              "DynamicMemberResolveResult is polymorphic (supports dynamic_cast)");
