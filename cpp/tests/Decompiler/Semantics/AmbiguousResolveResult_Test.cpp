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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `AmbiguousResolveResult` (cpp/Decompiler/Semantics/AmbiguousResolveResult.hpp,
// the D442 port of ICSharpCode.Decompiler/Semantics/AmbiguousResolveResult.cs) -- the two
// `ResolveResult` subclasses representing an ambiguous resolution. The C# file declares:
//   * `AmbiguousTypeResolveResult : TypeResolveResult` -- an ambiguous type-name
//     resolution; the ctor forwards the `IType`; the `IsError` override is `true`.
//   * `AmbiguousMemberResolveResult : MemberResolveResult` -- an ambiguous
//     field/property/event access; the ctor forwards the target + member; the `IsError`
//     override is `true`.
//
// The tests pin the forwarding-ctor shape for each class, the `IsError => true`
// unconditional crux (direct + virtual dispatch through each base pointer layer --
// the load-bearing override distinguishing an ambiguous result from its base which
// is `false` or `TypeKind::Unknown`-conditional), the `ToString` subclass-class-name
// format, the polymorphic `ClassName()` dispatch through a base pointer, the
// `ShallowClone` runtime-type preservation (not sliced) plus shared-ownership clone
// plus distinct-instance, the inherited defaults / member accessors the subclasses
// do NOT override, and the `is_base_of` / `has_virtual_destructor` / `is_polymorphic`
// / not-`final` (the C# classes are unsealed) class-shape static-asserts.

#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider`
// base can return a compilation (the D399 test stand-in pattern, identical in shape to
// the `TestCompilation` in `MemberResolveResult_Test.cpp` / `IMember_Test.cpp`).
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

// A minimal concrete `IAttribute` for testing (the D386 pattern, identical in shape to
// the `TestAttribute` in `IEntity_Test.cpp` / `IMember_Test.cpp` / `MemberResolveResult_Test.cpp`).
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute kind)
        : kind_(kind), attributeType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}
    ILSpy::Decompiler::TypeSystem::KnownAttribute Kind() const { return kind_; }
    const ILSpy::Decompiler::TypeSystem::IType& AttributeType() const override { return attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> FixedArguments() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }
private:
    ILSpy::Decompiler::TypeSystem::KnownAttribute kind_;
    ILSpy::Decompiler::TypeSystem::KnownType attributeType_;
};

// A minimal concrete `IMember` for testing: holds the configured state and returns it
// from every accessor. Models a non-field member (a property whose `SymbolKind` is NOT
// `Field`, so the IField check in the `MemberResolveResult` common ctor does not fire),
// used as the ambiguous member the `AmbiguousMemberResolveResult` wraps. The shape is
// identical to the `TestMember` in `MemberResolveResult_Test.cpp`.
class TestMember : public ILSpy::Decompiler::TypeSystem::IMember {
public:
    TestMember(std::string name,
               ILSpy::Decompiler::TypeSystem::SymbolKind kind,
               const TestCompilation& compilation,
               ILSpy::Decompiler::TypeSystem::ITypePtr returnType,
               bool isOverridable)
        : name_(std::move(name)), kind_(kind), compilation_(compilation),
          returnType_(std::move(returnType)), isOverridable_(isOverridable) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return declaringType_; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return ILSpy::Decompiler::TypeSystem::Accessibility::Public; }
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
    bool IsOverridable() const override { return isOverridable_; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override { return obj == this; }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    bool isOverridable_;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as a type / return type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

} // namespace

// ===========================================================================
// AmbiguousTypeResolveResult : TypeResolveResult
// ===========================================================================

TEST(AmbiguousTypeResolveResultTest, ConstructorForwardsTypeToBase)
{
    // The C# ctor forwards the `IType` to the `TypeResolveResult` base. The base
    // `Type().Kind()` is the configured type's kind (here `Object`, a reference
    // type), NOT `TypeKind::Unknown` -- distinguishing this ambiguous result from
    // a `TypeResolveResult` whose `IsError` keys on the `Unknown` kind.
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    EXPECT_EQ(&atrr.Type(), type.get());
    EXPECT_EQ(atrr.Type().Name(), type->Name());
}

TEST(AmbiguousTypeResolveResultTest, IsErrorIsAlwaysTrueDirect)
{
    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // ambiguous type-name resolution is always an error, unconditionally. A plain
    // `TypeResolveResult` over a KNOWN type (Object) would have `IsError == false`
    // (the kind is not `Unknown`), but the `AmbiguousTypeResolveResult` override
    // flips it to `true` regardless.
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    EXPECT_TRUE(atrr.IsError());
}

TEST(AmbiguousTypeResolveResultTest, IsErrorIsAlwaysTrueThroughTypeResolveResultBasePointer)
{
    // The `IsError` override dispatches through a `TypeResolveResult*` base pointer
    // (the virtual dispatch the resolver relies on): the base pointer reports
    // `true`, not the base `TypeResolveResult::IsError` (which would be `false` for
    // a non-`Unknown` type).
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    ILSpy::Decompiler::Semantics::TypeResolveResult* base = &atrr;
    EXPECT_TRUE(base->IsError());
}

TEST(AmbiguousTypeResolveResultTest, IsErrorIsAlwaysTrueThroughResolveResultBasePointer)
{
    // The `IsError` override dispatches through the `ResolveResult*` root base
    // pointer too (the two-level-up virtual dispatch).
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &atrr;
    EXPECT_TRUE(base->IsError());
}

TEST(AmbiguousTypeResolveResultTest, IsErrorOverridesTypeResolveResultBaseBehavior)
{
    // The crux: a `TypeResolveResult` over a KNOWN type has `IsError == false`
    // (the kind is not `Unknown`), but an `AmbiguousTypeResolveResult` over the
    // SAME type has `IsError == true` -- the override is load-bearing, not a
    // no-op re-statement. This pins the distinction the resolver relies on to
    // separate a single valid type-name resolution from an ambiguous one.
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::TypeResolveResult trr(type);
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    EXPECT_FALSE(trr.IsError());
    EXPECT_TRUE(atrr.IsError());
}

TEST(AmbiguousTypeResolveResultTest, InheritedDefaultsArePreserved)
{
    // AmbiguousTypeResolveResult does NOT override IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited ResolveResult base defaults
    // hold (an ambiguous type-name resolution is not a compile-time constant).
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    EXPECT_FALSE(atrr.IsCompileTimeConstant());
    EXPECT_FALSE(atrr.ConstantValue().has_value());
    EXPECT_TRUE(atrr.GetChildResults().empty());
}

TEST(AmbiguousTypeResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# ToString (inherited from ResolveResult via TypeResolveResult) uses
    // GetType().Name which is polymorphic and yields "AmbiguousTypeResolveResult".
    // The C++ port reproduces this via the ClassName() override so the inherited
    // ToString reports the subclass name (not the base "TypeResolveResult"). The
    // {type} field is the configured type's ReflectionName ("System.Object" -- the
    // KnownType ReflectionName is Namespace + '.' + Name).
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    EXPECT_EQ(atrr.ToString(), "[AmbiguousTypeResolveResult System.Object]");
}

TEST(AmbiguousTypeResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The ClassName() override dispatches through a ResolveResult* base pointer (the
    // virtual dispatch the C# resolver relies on in ToString via GetType().Name):
    // the base pointer's ToString reports the subclass name, not the base. The {type}
    // field is the configured type's ReflectionName ("System.Object").
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &atrr;
    EXPECT_EQ(base->ToString(), "[AmbiguousTypeResolveResult System.Object]");
}

TEST(AmbiguousTypeResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned AmbiguousTypeResolveResult stays an AmbiguousTypeResolveResult
    // (not sliced to the TypeResolveResult or ResolveResult base). The C++ override
    // reproduces this: the clone is an AmbiguousTypeResolveResult (dynamic_cast
    // succeeds), not a sliced ResolveResult.
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    auto clone = atrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult*>(clone.get()),
              nullptr);
}

TEST(AmbiguousTypeResolveResultTest, ShallowCloneSharesType)
{
    // The clone shares the IType via the shared_ptr member (faithful to
    // MemberwiseClone's reference copy): the same IType object backs both the
    // original and the clone.
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    auto clone = atrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &atrr.Type());
    EXPECT_EQ(clone->Type().Name(), type->Name());
    EXPECT_TRUE(clone->IsError());
}

TEST(AmbiguousTypeResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto type = MakeObjectType();
    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult atrr(type);
    auto clone = atrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &atrr);
}

TEST(AmbiguousTypeResolveResultTest, ClassShapeStaticAsserts)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::TypeResolveResult,
                                    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>,
                  "AmbiguousTypeResolveResult derives from TypeResolveResult.");
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>,
                  "AmbiguousTypeResolveResult derives from ResolveResult (via TypeResolveResult).");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>,
                  "AmbiguousTypeResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>,
                  "AmbiguousTypeResolveResult is polymorphic (the IsError/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `AmbiguousTypeResolveResult` is NOT sealed (the C# class is unsealed),
    // so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult>,
                  "AmbiguousTypeResolveResult is not final (the C# class is unsealed).");
}

// ===========================================================================
// AmbiguousMemberResolveResult : MemberResolveResult
// ===========================================================================

namespace {
// A helper that builds an AmbiguousMemberResolveResult over a non-field property
// member "Count" (SymbolKind::Property, overridable, ReturnType Object), with a
// plain TypeResolveResult target. Models the ambiguous field/property/event
// access shape the C# `AmbiguousMemberResolveResult` represents.
struct AmbiguousMemberFixture {
    TestCompilation compilation{1};
    TestMember member;
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target;
    std::unique_ptr<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult> rr;

    AmbiguousMemberFixture()
        : member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                 compilation, MakeObjectType(), /*isOverridable*/true),
          target(std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType())),
          rr(std::make_unique<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>(
              target, &member)) {}
};
} // namespace

TEST(AmbiguousMemberResolveResultTest, ConstructorForwardsTargetAndMemberToBase)
{
    // The C# ctor forwards the target and member to the MemberResolveResult common
    // ctor (D437 ctor 1), which computes the result type via ComputeType(*member)
    // and isVirtualCall from the member and target. The inherited TargetResult()
    // and Member() accessors return the forwarded values.
    AmbiguousMemberFixture f;
    ASSERT_NE(f.rr, nullptr);
    EXPECT_EQ(f.rr->TargetResult(), f.target.get());
    EXPECT_EQ(f.rr->Member(), &f.member);
}

TEST(AmbiguousMemberResolveResultTest, IsErrorIsAlwaysTrueDirect)
{
    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // ambiguous member access is always an error, unconditionally. A plain
    // MemberResolveResult over the SAME target/member would have IsError == false
    // (the inherited base default), but the AmbiguousMemberResolveResult override
    // flips it to true regardless.
    AmbiguousMemberFixture f;
    EXPECT_TRUE(f.rr->IsError());
}

TEST(AmbiguousMemberResolveResultTest, IsErrorIsAlwaysTrueThroughMemberResolveResultBasePointer)
{
    // The IsError override dispatches through a MemberResolveResult* base pointer
    // (the virtual dispatch the resolver relies on): the base pointer reports
    // true, not the base MemberResolveResult::IsError (the inherited false default).
    AmbiguousMemberFixture f;
    ILSpy::Decompiler::Semantics::MemberResolveResult* base = f.rr.get();
    EXPECT_TRUE(base->IsError());
}

TEST(AmbiguousMemberResolveResultTest, IsErrorIsAlwaysTrueThroughResolveResultBasePointer)
{
    // The IsError override dispatches through the ResolveResult* root base pointer
    // too (the two-level-up virtual dispatch).
    AmbiguousMemberFixture f;
    ILSpy::Decompiler::Semantics::ResolveResult* base = f.rr.get();
    EXPECT_TRUE(base->IsError());
}

TEST(AmbiguousMemberResolveResultTest, IsErrorOverridesMemberResolveResultBaseBehavior)
{
    // The crux: a plain MemberResolveResult over the SAME target/member has
    // IsError == false (the inherited base default), but an
    // AmbiguousMemberResolveResult over the SAME target/member has IsError == true
    // -- the override is load-bearing, not a no-op re-statement. This pins the
    // distinction the resolver relies on to separate a single valid member access
    // from an ambiguous one.
    AmbiguousMemberFixture f;
    ILSpy::Decompiler::Semantics::MemberResolveResult plain(f.target, &f.member);
    EXPECT_FALSE(plain.IsError());
    EXPECT_TRUE(f.rr->IsError());
}

TEST(AmbiguousMemberResolveResultTest, InheritedMemberAccessorsArePreserved)
{
    // AmbiguousMemberResolveResult does NOT redeclare TargetResult / Member /
    // IsVirtualCall (it inherits them from MemberResolveResult), so the inherited
    // accessors hold. A non-field property with IsOverridable=true and a plain
    // (non-ThisResolveResult) target yields IsVirtualCall == true (the D437
    // IsOverridable && !CausesNonVirtualInvocation computation).
    AmbiguousMemberFixture f;
    EXPECT_EQ(f.rr->TargetResult(), f.target.get());
    EXPECT_EQ(f.rr->Member(), &f.member);
    EXPECT_TRUE(f.rr->IsVirtualCall());
}

TEST(AmbiguousMemberResolveResultTest, InheritedDefaultsArePreserved)
{
    // AmbiguousMemberResolveResult does NOT override IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited MemberResolveResult
    // behavior holds (a non-field member is not a compile-time constant; the
    // GetChildResults returns the one-element target snapshot).
    AmbiguousMemberFixture f;
    EXPECT_FALSE(f.rr->IsCompileTimeConstant());
    EXPECT_FALSE(f.rr->ConstantValue().has_value());
    auto children = f.rr->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], f.target.get());
}

TEST(AmbiguousMemberResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# ToString (inherited from ResolveResult via MemberResolveResult) uses
    // GetType().Name which is polymorphic and yields "AmbiguousMemberResolveResult".
    // The C++ port reproduces this via the ClassName() override. The {member} field
    // is the member's Name() ("Count") -- the D437 documented deviation (the C#
    // uses member.ToString() which the C++ IMember interface does not expose; Name()
    // is the closest meaningful representation).
    AmbiguousMemberFixture f;
    EXPECT_EQ(f.rr->ToString(), "[AmbiguousMemberResolveResult Count]");
}

TEST(AmbiguousMemberResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The ClassName() override dispatches through a ResolveResult* base pointer
    // (the virtual dispatch the C# resolver relies on in ToString via
    // GetType().Name): the base pointer's ToString reports the subclass name, not
    // the base "MemberResolveResult".
    AmbiguousMemberFixture f;
    ILSpy::Decompiler::Semantics::ResolveResult* base = f.rr.get();
    EXPECT_EQ(base->ToString(), "[AmbiguousMemberResolveResult Count]");
}

TEST(AmbiguousMemberResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned AmbiguousMemberResolveResult stays an
    // AmbiguousMemberResolveResult (not sliced to the MemberResolveResult or
    // ResolveResult base). The C++ override reproduces this: the clone is an
    // AmbiguousMemberResolveResult (dynamic_cast succeeds), not a sliced
    // ResolveResult.
    AmbiguousMemberFixture f;
    auto clone = f.rr->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult*>(clone.get()),
              nullptr);
}

TEST(AmbiguousMemberResolveResultTest, ShallowCloneSharesTarget)
{
    // The clone shares the target ResolveResult via the shared_ptr member (faithful
    // to MemberwiseClone's reference copy): the same TargetResult object backs both
    // the original and the clone. ShallowClone returns a unique_ptr<ResolveResult> (the
    // base return type), so the MemberResolveResult-own TargetResult()/Member()
    // accessors are reached via a dynamic_cast downcast (the D433
    // subclass-own-accessor-through-base-pointer precedent).
    AmbiguousMemberFixture f;
    auto clone = f.rr->ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* mrr = dynamic_cast<ILSpy::Decompiler::Semantics::MemberResolveResult*>(clone.get());
    ASSERT_NE(mrr, nullptr);
    EXPECT_EQ(mrr->TargetResult(), f.target.get());
    EXPECT_EQ(mrr->Member(), &f.member);
    EXPECT_TRUE(clone->IsError());
}

TEST(AmbiguousMemberResolveResultTest, ShallowCloneIsDistinctInstance)
{
    AmbiguousMemberFixture f;
    auto clone = f.rr->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), f.rr.get());
}

TEST(AmbiguousMemberResolveResultTest, ClassShapeStaticAsserts)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::MemberResolveResult,
                                    ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>,
                  "AmbiguousMemberResolveResult derives from MemberResolveResult.");
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>,
                  "AmbiguousMemberResolveResult derives from ResolveResult (via MemberResolveResult).");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>,
                  "AmbiguousMemberResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>,
                  "AmbiguousMemberResolveResult is polymorphic (the IsError/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `AmbiguousMemberResolveResult` is NOT sealed (the C# class is unsealed),
    // so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::AmbiguousMemberResolveResult>,
                  "AmbiguousMemberResolveResult is not final (the C# class is unsealed).");
}
