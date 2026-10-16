// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `MemberLookup` accessibility surface (the twelfth
// `cpp/Decompiler/CSharp/Resolver/` leaf, the port of the static-helper +
// ctor + IsAccessible region of
// ICSharpCode.Decompiler/CSharp/Resolver/MemberLookup.cs -- the surface
// `TypeSystemAstBuilder::TypeDefinitionNameableInBaseList` consumes as
// `lookup.IsAccessible(td, false)`). The load-bearing cruxes are: the
// `IsInvocable` symbol-kind dispatch (`member is IEvent || member is IMethod`
// then a Dynamic/Delegate/FunctionPointer return), the C# 4.0 spec 3.5.2
// accessibility switch (None/Private/Public/Protected/Internal/protected-or /
// protected-and internal), the private outer-class walk (a private member of
// an OUTER class is accessible), the protected derivation walk (an instance
// protected member additionally requires allowProtectedAccess; a STATIC
// member or a type definition forces it), the InternalsVisibleTo module arm
// (plus the null-module arms), and the IsProtectedAccessAllowed shape
// (ThisResolveResult always allows; an IType target unwraps a single type
// parameter to its EffectiveBaseClass; a null-definition target never
// allows). The LookupGroup/GetAccessibleMembers/LookupType/Lookup region is
// deferred (it needs the GetMembers/GetNestedTypes member-enumeration
// surface). The stub universe comes from `LookupStubs.hpp`: definitions wired
// into hand-built inheritance/declaring graphs, plain entities for the
// C#-`IEntity`-typed arguments, and a friend-aware module.

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"

#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupEntity;
using TS::TestSupport::LookupEvent;
using TS::TestSupport::LookupMember;
using TS::TestSupport::LookupMethod;
using TS::TestSupport::LookupModule;
using TS::TestSupport::LookupTypeDefinition;
using TS::TestSupport::LookupTypeParameter;

namespace {

// A kind-only `IType` for the IsInvocable return-type arms (the return member
// carries the type; only its Kind() is consulted).
class KindOnlyType : public TS::IType {
public:
    explicit KindOnlyType(TS::TypeKind kind) : kind_(kind) {}
    TS::TypeKind Kind() const override { return kind_; }
    std::string Name() const override { return {}; }
    std::string ReflectionName() const override { return {}; }
    int TypeParameterCount() const override { return 0; }
protected:
    bool StructuralEquals(const TS::IType& other) const override
    {
        return kind_ == static_cast<const KindOnlyType&>(other).kind_;
    }
private:
    TS::TypeKind kind_;
};

std::shared_ptr<LookupTypeDefinition> MakeDefinition(
    const TS::ICompilation& compilation,
    std::string name,
    std::string ns,
    TS::Accessibility accessibility = TS::Accessibility::Public,
    TS::TypeKind kind = TS::TypeKind::Class,
    const TS::IModule* module = nullptr,
    TS::KnownTypeCode knownTypeCode = TS::KnownTypeCode::None)
{
    return std::make_shared<LookupTypeDefinition>(
        ns + "." + name, ns, TS::FullTypeName(ns + "." + name), kind,
        accessibility, compilation, module, knownTypeCode);
}

} // namespace

// ---------------------------------------------------------------------------
// Class shape -- unsealed (NOT final), constructible (no virtual members: the
// C# class has none -- it is a value-style lookup context).
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, ClassIsNotFinalAndNotAbstract)
{
    static_assert(!std::is_final<Res::MemberLookup>::value,
                  "MemberLookup is not final (the C# is unsealed)");
    static_assert(!std::is_abstract<Res::MemberLookup>::value,
                  "MemberLookup is concrete (the C# has no abstract members)");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IsInvocable -- events and methods are invocable; other members iff the
// return type is a delegate, dynamic, or function pointer.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsInvocableEventIsTrue)
{
    LookupCompilation compilation;
    auto handlerType = std::make_shared<KindOnlyType>(TS::TypeKind::Delegate);
    LookupEvent ev("E", handlerType, compilation);
    EXPECT_TRUE(Res::MemberLookup::IsInvocable(ev));
}

TEST(MemberLookupTest, IsInvocableMethodIsTrue)
{
    LookupCompilation compilation;
    LookupMethod method("M", compilation);
    EXPECT_TRUE(Res::MemberLookup::IsInvocable(method));
}

TEST(MemberLookupTest, IsInvocableFieldWithStructReturnIsFalse)
{
    LookupCompilation compilation;
    auto int32 = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    LookupMember field("F", TS::SymbolKind::Field, int32, compilation);
    EXPECT_FALSE(Res::MemberLookup::IsInvocable(field));
}

TEST(MemberLookupTest, IsInvocableFieldWithDelegateReturnIsTrue)
{
    LookupCompilation compilation;
    auto delegateType = std::make_shared<KindOnlyType>(TS::TypeKind::Delegate);
    LookupMember field("F", TS::SymbolKind::Field, delegateType, compilation);
    EXPECT_TRUE(Res::MemberLookup::IsInvocable(field));
}

TEST(MemberLookupTest, IsInvocableFieldWithDynamicReturnIsTrue)
{
    LookupCompilation compilation;
    auto dynamicType = std::make_shared<TS::SpecialType>(TS::TypeKind::Dynamic, true);
    LookupMember field("F", TS::SymbolKind::Field, dynamicType, compilation);
    EXPECT_TRUE(Res::MemberLookup::IsInvocable(field));
}

TEST(MemberLookupTest, IsInvocablePropertyWithFunctionPointerReturnIsTrue)
{
    LookupCompilation compilation;
    auto fnPtrType = std::make_shared<KindOnlyType>(TS::TypeKind::FunctionPointer);
    LookupMember property("P", TS::SymbolKind::Property, fnPtrType, compilation);
    EXPECT_TRUE(Res::MemberLookup::IsInvocable(property));
}

TEST(MemberLookupTest, IsInvocablePropertyWithIntReturnIsFalse)
{
    LookupCompilation compilation;
    auto int32 = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    LookupMember property("P", TS::SymbolKind::Property, int32, compilation);
    EXPECT_FALSE(Res::MemberLookup::IsInvocable(property));
}

// ---------------------------------------------------------------------------
// IsAccessible -- the trivial arms (None, Public) and the null-context case.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsAccessibleNoneIsAlwaysFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    LookupEntity entity("M", TS::SymbolKind::Method, TS::Accessibility::None,
                        /*isStatic*/ false, current.get(), &module, compilation);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_FALSE(lookup.IsAccessible(entity, true));
    EXPECT_FALSE(lookup.IsAccessible(entity, false));
}

TEST(MemberLookupTest, IsAccessiblePublicIsTrueEvenWithoutContext)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    LookupEntity entity("M", TS::SymbolKind::Method, TS::Accessibility::Public,
                        /*isStatic*/ false, nullptr, &module, compilation);
    Res::MemberLookup lookup(nullptr, nullptr);
    EXPECT_TRUE(lookup.IsAccessible(entity, true));
}

// ---------------------------------------------------------------------------
// IsAccessible -- Private: same-class, nested-in, and outer-class walks.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsAccessiblePrivateInSameClassIsTrue)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    LookupEntity privMember("M", TS::SymbolKind::Method, TS::Accessibility::Private,
                            /*isStatic*/ false, current.get(), &module, compilation);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_TRUE(lookup.IsAccessible(privMember, false));
}

TEST(MemberLookupTest, IsAccessiblePrivateInUnrelatedClassIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    auto other = MakeDefinition(compilation, "Other", "N", TS::Accessibility::Public,
                                TS::TypeKind::Class, &module);
    LookupEntity privMember("M", TS::SymbolKind::Method, TS::Accessibility::Private,
                            /*isStatic*/ false, other.get(), &module, compilation);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_FALSE(lookup.IsAccessible(privMember, false));
}

TEST(MemberLookupTest, IsAccessiblePrivateOfOuterClassFromNestedClassIsTrue)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto outer = MakeDefinition(compilation, "Outer", "N", TS::Accessibility::Public,
                                TS::TypeKind::Class, &module);
    auto nested = MakeDefinition(compilation, "Nested", "N", TS::Accessibility::Public,
                                 TS::TypeKind::Class, &module);
    nested->SetDeclaringTypeDefinition(outer.get());
    auto deeplyNested = MakeDefinition(compilation, "DeeplyNested", "N",
                                       TS::Accessibility::Public, TS::TypeKind::Class, &module);
    deeplyNested->SetDeclaringTypeDefinition(nested.get());
    LookupEntity privMember("M", TS::SymbolKind::Method, TS::Accessibility::Private,
                            /*isStatic*/ false, outer.get(), &module, compilation);
    // A private member of an OUTER class can be accessed from the nested class
    // (and transitively from a deeper nesting).
    EXPECT_TRUE(Res::MemberLookup(nested.get(), &module).IsAccessible(privMember, false));
    EXPECT_TRUE(Res::MemberLookup(deeplyNested.get(), &module).IsAccessible(privMember, false));
}

TEST(MemberLookupTest, IsAccessiblePrivateWithoutCurrentTypeIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto other = MakeDefinition(compilation, "Other", "N", TS::Accessibility::Public,
                                TS::TypeKind::Class, &module);
    LookupEntity privMember("M", TS::SymbolKind::Method, TS::Accessibility::Private,
                            /*isStatic*/ false, other.get(), &module, compilation);
    Res::MemberLookup lookup(nullptr, &module);
    EXPECT_FALSE(lookup.IsAccessible(privMember, true));
}

// ---------------------------------------------------------------------------
// IsAccessible -- Protected (IsProtectedAccessible): derivation walk, the
// allowProtectedAccess gate, and the static/type-definition force.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsAccessibleProtectedInstanceInDerivedClassRequiresAllowProtectedAccess)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &module);
    auto derived = MakeDefinition(compilation, "Derived", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    derived->AddDirectBaseType(base);
    LookupEntity protMember("M", TS::SymbolKind::Method, TS::Accessibility::Protected,
                            /*isStatic*/ false, base.get(), &module, compilation);
    Res::MemberLookup lookup(derived.get(), &module);
    // Instance protected: accessible from a derived context only when the
    // qualifying reference is derived (allowProtectedAccess).
    EXPECT_TRUE(lookup.IsAccessible(protMember, /*allowProtectedAccess*/ true));
    EXPECT_FALSE(lookup.IsAccessible(protMember, /*allowProtectedAccess*/ false));
}

TEST(MemberLookupTest, IsAccessibleProtectedInstanceInSameClassIsTrueRegardlessOfAllow)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    LookupEntity protMember("M", TS::SymbolKind::Method, TS::Accessibility::Protected,
                            /*isStatic*/ false, current.get(), &module, compilation);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_TRUE(lookup.IsAccessible(protMember, false));
    EXPECT_TRUE(lookup.IsAccessible(protMember, true));
}

TEST(MemberLookupTest, IsAccessibleProtectedStaticForcesAllowProtectedAccess)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &module);
    auto derived = MakeDefinition(compilation, "Derived", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    derived->AddDirectBaseType(base);
    LookupEntity protStatic("M", TS::SymbolKind::Field, TS::Accessibility::Protected,
                            /*isStatic*/ true, base.get(), &module, compilation);
    Res::MemberLookup lookup(derived.get(), &module);
    // Protected static members may be accessible even if allowProtectedAccess
    // is false (the C# forces it for static members).
    EXPECT_TRUE(lookup.IsAccessible(protStatic, /*allowProtectedAccess*/ false));
}

TEST(MemberLookupTest, IsAccessibleProtectedTypeDefinitionForcesAllowProtectedAccess)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &module);
    auto derived = MakeDefinition(compilation, "Derived", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    derived->AddDirectBaseType(base);
    // A protected NESTED TYPE (SymbolKind::TypeDefinition) is accessible from a
    // derived context even with allowProtectedAccess = false (the C# forces
    // allowProtectedAccess for type definitions); this is the arm
    // TypeDefinitionNameableInBaseList exercises via IsAccessible(td, false).
    LookupEntity protNestedType("Inner", TS::SymbolKind::TypeDefinition, TS::Accessibility::Protected,
                                /*isStatic*/ false, base.get(), &module, compilation);
    Res::MemberLookup lookup(derived.get(), &module);
    EXPECT_TRUE(lookup.IsAccessible(protNestedType, /*allowProtectedAccess*/ false));
}

TEST(MemberLookupTest, IsAccessibleProtectedInUnrelatedClassIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &module);
    auto unrelated = MakeDefinition(compilation, "Unrelated", "N", TS::Accessibility::Public,
                                    TS::TypeKind::Class, &module);
    LookupEntity protMember("M", TS::SymbolKind::Method, TS::Accessibility::Protected,
                            /*isStatic*/ false, base.get(), &module, compilation);
    LookupEntity protStatic("S", TS::SymbolKind::Field, TS::Accessibility::Protected,
                            /*isStatic*/ true, base.get(), &module, compilation);
    Res::MemberLookup lookup(unrelated.get(), &module);
    EXPECT_FALSE(lookup.IsAccessible(protMember, true));
    EXPECT_FALSE(lookup.IsAccessible(protMember, false));
    // A static member is not enough by itself: some inheritance or same-class
    // link to the declaring type is still required.
    EXPECT_FALSE(lookup.IsAccessible(protStatic, false));
}

TEST(MemberLookupTest, IsAccessibleProtectedViaNestedClassInheritanceChainIsTrue)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto outer = MakeDefinition(compilation, "Outer", "N", TS::Accessibility::Public,
                                TS::TypeKind::Class, &module);
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &module);
    auto nestedDerived = MakeDefinition(compilation, "NestedDerived", "N",
                                        TS::Accessibility::Public, TS::TypeKind::Class, &module);
    nestedDerived->SetDeclaringTypeDefinition(outer.get());
    nestedDerived->AddDirectBaseType(base);
    LookupEntity protMember("M", TS::SymbolKind::Method, TS::Accessibility::Protected,
                            /*isStatic*/ false, base.get(), &module, compilation);
    Res::MemberLookup lookup(nestedDerived.get(), &module);
    // The walk visits the nested class itself then its outer class; the nested
    // class derives from Base, so protected access through it is granted.
    EXPECT_TRUE(lookup.IsAccessible(protMember, /*allowProtectedAccess*/ true));
}

// ---------------------------------------------------------------------------
// IsAccessible -- Internal / ProtectedOrInternal / ProtectedAndInternal: the
// InternalsVisibleTo module arm and its combination with protected access.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsAccessibleInternalInSameModuleIsTrue)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    LookupEntity intMember("M", TS::SymbolKind::Method, TS::Accessibility::Internal,
                           /*isStatic*/ false, nullptr, &module, compilation);
    Res::MemberLookup lookup(nullptr, &module);
    EXPECT_TRUE(lookup.IsAccessible(intMember, false));
}

TEST(MemberLookupTest, IsAccessibleInternalInFriendlessOtherModuleIsFalse)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupModule entityModule(compilation, "Ent");
    LookupEntity intMember("M", TS::SymbolKind::Method, TS::Accessibility::Internal,
                           /*isStatic*/ false, nullptr, &entityModule, compilation);
    Res::MemberLookup lookup(nullptr, &currentModule);
    EXPECT_FALSE(lookup.IsAccessible(intMember, false));
}

TEST(MemberLookupTest, IsAccessibleInternalInFriendModuleIsTrue)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupModule entityModule(compilation, "Ent");
    entityModule.AddFriendAssembly("Cur");
    LookupEntity intMember("M", TS::SymbolKind::Method, TS::Accessibility::Internal,
                           /*isStatic*/ false, nullptr, &entityModule, compilation);
    Res::MemberLookup lookup(nullptr, &currentModule);
    EXPECT_TRUE(lookup.IsAccessible(intMember, false));
}

TEST(MemberLookupTest, IsAccessibleInternalWithoutCurrentModuleIsFalse)
{
    LookupCompilation compilation;
    LookupModule entityModule(compilation, "Ent");
    LookupEntity intMember("M", TS::SymbolKind::Method, TS::Accessibility::Internal,
                           /*isStatic*/ false, nullptr, &entityModule, compilation);
    Res::MemberLookup lookup(nullptr, nullptr);
    EXPECT_FALSE(lookup.IsAccessible(intMember, true));
}

TEST(MemberLookupTest, IsAccessibleInternalWithNullEntityModuleIsFalse)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupEntity intMember("M", TS::SymbolKind::Method, TS::Accessibility::Internal,
                           /*isStatic*/ false, nullptr, nullptr, compilation);
    Res::MemberLookup lookup(nullptr, &currentModule);
    EXPECT_FALSE(lookup.IsAccessible(intMember, false));
}

TEST(MemberLookupTest, IsAccessibleProtectedOrInternalNeitherArmIsFalse)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupModule entityModule(compilation, "Ent");
    LookupEntity member("M", TS::SymbolKind::Method, TS::Accessibility::ProtectedOrInternal,
                        /*isStatic*/ false, nullptr, &entityModule, compilation);
    // An unrelated context in a non-friend module has neither the internal nor
    // the protected arm.
    EXPECT_FALSE(Res::MemberLookup(nullptr, &currentModule).IsAccessible(member, true));
}

TEST(MemberLookupTest, IsAccessibleProtectedOrInternalViaInternalArmOnlyIsTrue)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupModule entityModule(compilation, "Ent");
    entityModule.AddFriendAssembly("Cur");
    LookupEntity member("M", TS::SymbolKind::Method, TS::Accessibility::ProtectedOrInternal,
                        /*isStatic*/ false, nullptr, &entityModule, compilation);
    // A friend-module context (the internal arm) suffices even without any
    // inheritance link to the declaring type.
    EXPECT_TRUE(Res::MemberLookup(nullptr, &currentModule).IsAccessible(member, false));
}

TEST(MemberLookupTest, IsAccessibleProtectedOrInternalViaProtectedArmOnlyIsTrue)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupModule entityModule(compilation, "Ent");
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &entityModule);
    auto derived = MakeDefinition(compilation, "Derived", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &currentModule);
    derived->AddDirectBaseType(base);
    LookupEntity member("M", TS::SymbolKind::Method, TS::Accessibility::ProtectedOrInternal,
                        /*isStatic*/ false, base.get(), &entityModule, compilation);
    // A derived context (the protected arm) suffices even without the internal
    // arm (a non-friend module).
    EXPECT_TRUE(Res::MemberLookup(derived.get(), &currentModule)
                    .IsAccessible(member, /*allowProtectedAccess*/ true));
}

TEST(MemberLookupTest, IsAccessibleProtectedAndInternalRequiresBothArms)
{
    LookupCompilation compilation;
    LookupModule currentModule(compilation, "Cur");
    LookupModule entityModule(compilation, "Ent");
    entityModule.AddFriendAssembly("Cur");
    auto base = MakeDefinition(compilation, "Base", "N", TS::Accessibility::Public,
                               TS::TypeKind::Class, &entityModule);
    auto derived = MakeDefinition(compilation, "Derived", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &currentModule);
    derived->AddDirectBaseType(base);
    LookupEntity member("M", TS::SymbolKind::Method, TS::Accessibility::ProtectedAndInternal,
                        /*isStatic*/ false, base.get(), &entityModule, compilation);

    // Internal arm only (friend module, unrelated context): false.
    auto unrelated = MakeDefinition(compilation, "Unrelated", "N", TS::Accessibility::Public,
                                    TS::TypeKind::Class, &currentModule);
    EXPECT_FALSE(Res::MemberLookup(unrelated.get(), &currentModule).IsAccessible(member, false));

    // Protected arm only (derived context, non-friend module): false.
    LookupCompilation compilation2;
    LookupModule currentModule2(compilation2, "Cur");
    LookupModule entityModule2(compilation2, "Ent");
    auto base2 = MakeDefinition(compilation2, "Base", "N", TS::Accessibility::Public,
                                TS::TypeKind::Class, &entityModule2);
    auto derived2 = MakeDefinition(compilation2, "Derived", "N", TS::Accessibility::Public,
                                   TS::TypeKind::Class, &currentModule2);
    derived2->AddDirectBaseType(base2);
    LookupEntity member2("M", TS::SymbolKind::Method, TS::Accessibility::ProtectedAndInternal,
                         /*isStatic*/ false, base2.get(), &entityModule2, compilation2);
    EXPECT_FALSE(Res::MemberLookup(derived2.get(), &currentModule2)
                     .IsAccessible(member2, /*allowProtectedAccess*/ true));

    // Both arms (derived context, friend module): true.
    EXPECT_TRUE(Res::MemberLookup(derived.get(), &currentModule)
                    .IsAccessible(member, /*allowProtectedAccess*/ true));
}

// ---------------------------------------------------------------------------
// IsProtectedAccessAllowed(ResolveResult) -- a `this` reference always allows;
// any other result defers to the IType overload.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsProtectedAccessAllowedThisResolveResultIsTrueWithoutContext)
{
    auto selfType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Sem::ThisResolveResult thisResult(selfType);
    Res::MemberLookup lookup(nullptr, nullptr);
    EXPECT_TRUE(lookup.IsProtectedAccessAllowed(thisResult));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedNonThisResultDefersToTypeOverload)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    auto unrelated = MakeDefinition(compilation, "Unrelated", "N", TS::Accessibility::Public,
                                    TS::TypeKind::Class, &module);
    Sem::TypeResolveResult unrelatedResult(unrelated);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_FALSE(lookup.IsProtectedAccessAllowed(unrelatedResult));
    Sem::TypeResolveResult currentResult(current);
    EXPECT_TRUE(lookup.IsProtectedAccessAllowed(currentResult));
}

// ---------------------------------------------------------------------------
// IsProtectedAccessAllowed(IType) -- derivation walk against the current type
// (and its outer chain), the type-parameter EffectiveBaseClass unwrap, and
// the null-definition / null-context short-circuits.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeDerivedFromCurrentIsTrue)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    auto derivedFromCurrent = MakeDefinition(compilation, "Kid", "N", TS::Accessibility::Public,
                                             TS::TypeKind::Class, &module);
    derivedFromCurrent->AddDirectBaseType(current);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_TRUE(lookup.IsProtectedAccessAllowed(*derivedFromCurrent));
    // The current type itself qualifies (IsDerivedFrom is reflexive --
    // protected members are accessible through a reference of the SAME type).
    EXPECT_TRUE(lookup.IsProtectedAccessAllowed(*current));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeUnrelatedToCurrentIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    auto unrelated = MakeDefinition(compilation, "Unrelated", "N", TS::Accessibility::Public,
                                    TS::TypeKind::Class, &module);
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_FALSE(lookup.IsProtectedAccessAllowed(*unrelated));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeDerivedFromOuterClassOfCurrentIsTrue)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto outer = MakeDefinition(compilation, "Outer", "N", TS::Accessibility::Public,
                                TS::TypeKind::Class, &module);
    auto nestedCurrent = MakeDefinition(compilation, "Nested", "N", TS::Accessibility::Public,
                                        TS::TypeKind::Class, &module);
    nestedCurrent->SetDeclaringTypeDefinition(outer.get());
    auto derivedFromOuter = MakeDefinition(compilation, "Kid", "N", TS::Accessibility::Public,
                                           TS::TypeKind::Class, &module);
    derivedFromOuter->AddDirectBaseType(outer);
    Res::MemberLookup lookup(nestedCurrent.get(), &module);
    // The c-walk visits the nested current AND its outer class; a target
    // derived from the outer class qualifies.
    EXPECT_TRUE(lookup.IsProtectedAccessAllowed(*derivedFromOuter));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeWithNullDefinitionIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    TS::KnownType knownBase(TS::KnownTypeCode::Object); // GetDefinition() inherited nullptr
    Res::MemberLookup lookup(current.get(), &module);
    EXPECT_FALSE(lookup.IsProtectedAccessAllowed(knownBase));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedWithoutCurrentTypeIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    Res::MemberLookup lookup(nullptr, &module);
    EXPECT_FALSE(lookup.IsProtectedAccessAllowed(*current));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeParameterUnwrapsEffectiveBaseClass)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    auto derivedFromCurrent = MakeDefinition(compilation, "Kid", "N", TS::Accessibility::Public,
                                             TS::TypeKind::Class, &module);
    derivedFromCurrent->AddDirectBaseType(current);
    Res::MemberLookup lookup(current.get(), &module);

    LookupTypeParameter tp("T");
    tp.SetEffectiveBaseClass(derivedFromCurrent);
    EXPECT_TRUE(lookup.IsProtectedAccessAllowed(tp));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeParameterWithUnrelatedEffectiveBaseClassIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    auto unrelated = MakeDefinition(compilation, "Unrelated", "N", TS::Accessibility::Public,
                                    TS::TypeKind::Class, &module);
    Res::MemberLookup lookup(current.get(), &module);

    LookupTypeParameter tp("T");
    tp.SetEffectiveBaseClass(unrelated);
    EXPECT_FALSE(lookup.IsProtectedAccessAllowed(tp));
}

TEST(MemberLookupTest, IsProtectedAccessAllowedTypeParameterWithNullEffectiveBaseClassIsFalse)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    Res::MemberLookup lookup(current.get(), &module);

    LookupTypeParameter tp("T"); // no effective base class wired (null promise)
    EXPECT_FALSE(lookup.IsProtectedAccessAllowed(tp));
}

// ---------------------------------------------------------------------------
// The C# default-argument ctor (isInEnumMemberInitializer = false) + the
// nullable currentTypeDefinition / currentModule halves.
// ---------------------------------------------------------------------------

TEST(MemberLookupTest, CtorDefaultArgumentsAndNullableHalves)
{
    LookupCompilation compilation;
    LookupModule module(compilation, "Cur");
    auto current = MakeDefinition(compilation, "Current", "N", TS::Accessibility::Public,
                                  TS::TypeKind::Class, &module);
    LookupEntity intMember("M", TS::SymbolKind::Method, TS::Accessibility::Internal,
                           /*isStatic*/ false, nullptr, &module, compilation);
    // The one-argument form (both context halves nullable defaults exercised
    // separately): a non-null current type with a null module cannot see
    // internals; a null current type with the module sees same-module internals.
    Res::MemberLookup typeOnly(current.get(), nullptr);
    EXPECT_FALSE(typeOnly.IsAccessible(intMember, false));
    Res::MemberLookup moduleOnly(nullptr, &module);
    EXPECT_TRUE(moduleOnly.IsAccessible(intMember, false));
}
