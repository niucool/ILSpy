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

// Tests for `CSharpInvocationResolveResult` (the eighth `cpp/Decompiler/CSharp/Resolver/`
// leaf, the port of ICSharpCode.Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.cs
// -- the C#-specific information for a method/constructor/indexer invocation). The class
// derives from `InvocationResolveResult` (D438, which derives from `MemberResolveResult`
// D437) and adds the `OverloadResolutionErrors` (D468) mask, three C#-specific bools
// (`IsExtensionMethodInvocation` / `IsDelegateInvocation` / `IsExpandedForm`), and the
// optional `argumentToParameterMap`. The load-bearing crux is the `IsError` override
// (`OverloadResolutionErrors != None`), which exercises the D468 `[Flags]` enum directly
// (the `CSharpInvocationResolveResult` is the primary consumer of the mask). The
// `GetArgumentsForCall()` override (rebuilds the argument list for params-array calls) is
// deferred -- the base `InvocationResolveResult::GetArgumentsForCall()` default returns
// `Arguments` directly. The tests pin the ctor-stores-fields + forwards-to-base contract,
// the `IsError` crux (false for `None`, true for each individual flag and a combined mask),
// the `GetArgumentToParameterMap` optional (present + absent), the inherited `ToString`
// bracket form, the `ShallowClone` runtime-type preservation + copied fields, the
// polymorphic dispatch, and the class-shape static-asserts.

#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

// A minimal concrete `ICompilation` stand-in so the `TestParameterizedMember` stub's
// inherited `ICompilationProvider::Compilation()` can return a compilation (the D399 test
// stand-in pattern, identical in shape to the `TestCompilation` in
// `InvocationResolveResult_Test.cpp`).
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

// A minimal concrete `IParameterizedMember` for testing: holds a name, a kind, a
// compilation reference, a return type, and an overridable flag; returns trivial defaults
// for every other accessor. The shape is identical to `TestParameterizedMember` in
// `InvocationResolveResult_Test.cpp`. Only pointer-identity (the address) and the
// `SymbolKind`/`ReturnType`/`IsOverridable` are exercised by the tests.
class TestParameterizedMember : public TS::IParameterizedMember {
public:
    TestParameterizedMember(std::string name,
                            TS::SymbolKind kind,
                            const TestCompilation& compilation,
                            TS::ITypePtr returnType,
                            bool isOverridable)
        : name_(std::move(name)), kind_(kind), compilation_(compilation),
          returnType_(std::move(returnType)), isOverridable_(isOverridable) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return kind_; }
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
    bool IsOverridable() const override { return isOverridable_; }
    const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const TS::IMember* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
private:
    std::string name_;
    TS::SymbolKind kind_;
    const TestCompilation& compilation_;
    TS::ITypePtr returnType_;
    bool isOverridable_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as a return type.
TS::ITypePtr MakeObjectType()
{
    return std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
}

// A `TestParameterizedMember` modeling a non-overridable instance method "M" with an
// `Object` return type (the `MemberResolveResult` common-ctor `isVirtualCall` computation
// uses `IsOverridable()`).
std::unique_ptr<TestParameterizedMember> MakeMethodMember(const TestCompilation& compilation)
{
    return std::make_unique<TestParameterizedMember>(
        "M", TS::SymbolKind::Method, compilation, MakeObjectType(), false);
}

// A non-error `targetResult` (a `TypeResolveResult` over a known type).
std::shared_ptr<Sem::ResolveResult> MakeObjectTarget()
{
    return std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
}

} // namespace

// ===========================================================================
// ctor -- stores the C#-specific fields and forwards the base args.
// ===========================================================================

TEST(CSharpInvocationResolveResultTest, ConstructorStoresAllCSharpSpecificFields)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    auto arg = MakeObjectTarget();
    std::vector<int> argMap = { 0, 1, -1 };
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), { arg },
        Res::OverloadResolutionErrors::ArgumentTypeMismatch | Res::OverloadResolutionErrors::AmbiguousMatch,
        true, true, true, std::optional<std::vector<int>>(argMap));
    EXPECT_EQ(rr.OverloadResolutionErrors(),
              Res::OverloadResolutionErrors::ArgumentTypeMismatch | Res::OverloadResolutionErrors::AmbiguousMatch);
    EXPECT_TRUE(rr.IsExtensionMethodInvocation());
    EXPECT_TRUE(rr.IsExpandedForm());
    EXPECT_TRUE(rr.IsDelegateInvocation());
    ASSERT_TRUE(rr.GetArgumentToParameterMap().has_value());
    EXPECT_EQ(rr.GetArgumentToParameterMap()->size(), 3u);
    EXPECT_EQ((*rr.GetArgumentToParameterMap())[2], -1);
}

TEST(CSharpInvocationResolveResultTest, ConstructorDefaultsCSharpSpecificFields)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::CSharpInvocationResolveResult rr(MakeObjectTarget(), member.get());
    EXPECT_EQ(rr.OverloadResolutionErrors(), Res::OverloadResolutionErrors::None);
    EXPECT_FALSE(rr.IsExtensionMethodInvocation());
    EXPECT_FALSE(rr.IsExpandedForm());
    EXPECT_FALSE(rr.IsDelegateInvocation());
    EXPECT_FALSE(rr.GetArgumentToParameterMap().has_value());
}

TEST(CSharpInvocationResolveResultTest, ConstructorForwardsBaseArgs)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    auto target = MakeObjectTarget();
    auto arg = MakeObjectTarget();
    auto init = MakeObjectTarget();
    auto returnType = MakeObjectType();
    Res::CSharpInvocationResolveResult rr(
        target, member.get(), { arg }, Res::OverloadResolutionErrors::None,
        false, false, false, std::nullopt, { init }, returnType);
    // The base `InvocationResolveResult` ctor stores the target/member/arguments/initializer/
    // returnTypeOverride.
    EXPECT_EQ(rr.TargetResult(), target.get());
    EXPECT_EQ(rr.Member(), member.get());
    ASSERT_EQ(rr.Arguments().size(), 1u);
    EXPECT_EQ(rr.Arguments()[0].get(), arg.get());
    ASSERT_EQ(rr.InitializerStatements().size(), 1u);
    EXPECT_EQ(rr.InitializerStatements()[0].get(), init.get());
    EXPECT_EQ(&rr.Type(), returnType.get());
}

// ===========================================================================
// IsError -- the load-bearing crux (OverloadResolutionErrors != None).
// ===========================================================================

TEST(CSharpInvocationResolveResultTest, IsErrorFalseWhenOverloadResolutionErrorsIsNone)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), {},
        Res::OverloadResolutionErrors::None);
    EXPECT_FALSE(rr.IsError());
}

TEST(CSharpInvocationResolveResultTest, IsErrorTrueForEachIndividualErrorFlag)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::OverloadResolutionErrors flags[] = {
        Res::OverloadResolutionErrors::TooManyPositionalArguments,
        Res::OverloadResolutionErrors::NoParameterFoundForNamedArgument,
        Res::OverloadResolutionErrors::TypeInferenceFailed,
        Res::OverloadResolutionErrors::WrongNumberOfTypeArguments,
        Res::OverloadResolutionErrors::ConstructedTypeDoesNotSatisfyConstraint,
        Res::OverloadResolutionErrors::MissingArgumentForRequiredParameter,
        Res::OverloadResolutionErrors::MultipleArgumentsForSingleParameter,
        Res::OverloadResolutionErrors::ParameterPassingModeMismatch,
        Res::OverloadResolutionErrors::ArgumentTypeMismatch,
        Res::OverloadResolutionErrors::AmbiguousMatch,
        Res::OverloadResolutionErrors::Inaccessible,
        Res::OverloadResolutionErrors::MethodConstraintsNotSatisfied,
        Res::OverloadResolutionErrors::OutVarTypeMismatch,
    };
    for (auto flag : flags) {
        Res::CSharpInvocationResolveResult rr(MakeObjectTarget(), member.get(), {}, flag);
        EXPECT_TRUE(rr.IsError()) << "flag " << static_cast<std::int32_t>(flag) << " should be an error";
    }
}

TEST(CSharpInvocationResolveResultTest, IsErrorTrueForCombinedMask)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    auto mask = Res::OverloadResolutionErrors::ArgumentTypeMismatch |
                Res::OverloadResolutionErrors::TypeInferenceFailed |
                Res::OverloadResolutionErrors::AmbiguousMatch;
    Res::CSharpInvocationResolveResult rr(MakeObjectTarget(), member.get(), {}, mask);
    EXPECT_TRUE(rr.IsError());
}

// ===========================================================================
// GetArgumentToParameterMap -- the optional (present + absent).
// ===========================================================================

TEST(CSharpInvocationResolveResultTest, GetArgumentToParameterMapAbsentWhenNotProvided)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::CSharpInvocationResolveResult rr(MakeObjectTarget(), member.get());
    EXPECT_FALSE(rr.GetArgumentToParameterMap().has_value());
}

TEST(CSharpInvocationResolveResultTest, GetArgumentToParameterMapPresentWhenProvided)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    std::vector<int> argMap = { 0, 1, 2, -1 };
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), {},
        Res::OverloadResolutionErrors::None, false, false, false,
        std::optional<std::vector<int>>(argMap));
    ASSERT_TRUE(rr.GetArgumentToParameterMap().has_value());
    EXPECT_EQ(rr.GetArgumentToParameterMap()->size(), 4u);
    EXPECT_EQ((*rr.GetArgumentToParameterMap())[3], -1);
}

// ===========================================================================
// ToString -- the inherited ResolveResult::ToString bracket form (the C# does NOT
// override ToString; the base InvocationResolveResult also does NOT, so the inherited
// chain uses the most-derived ClassName).
// ===========================================================================

TEST(CSharpInvocationResolveResultTest, ToStringUsesInheritedBracketForm)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    auto returnType = MakeObjectType();
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), {}, Res::OverloadResolutionErrors::None,
        false, false, false, std::nullopt, {}, returnType);
    // The inherited MemberResolveResult::ToString (which CSharpInvocationResolveResult
    // does NOT override -- it only overrides ClassName) => "[" + ClassName() + " " +
    // member->Name() + "]"; ClassName() is "CSharpInvocationResolveResult" (the
    // most-derived); the TestParameterizedMember's Name() is "M".
    EXPECT_EQ(rr.ToString(), "[CSharpInvocationResolveResult M]");
}

// ===========================================================================
// ShallowClone -- preserves the runtime type, copies the C#-specific fields.
// ===========================================================================

TEST(CSharpInvocationResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), {},
        Res::OverloadResolutionErrors::AmbiguousMatch, true, true, true);
    auto clone = rr.ShallowClone();
    EXPECT_NE(clone, nullptr);
    EXPECT_EQ(dynamic_cast<Res::CSharpInvocationResolveResult*>(clone.get()), clone.get());
}

TEST(CSharpInvocationResolveResultTest, ShallowCloneCopiesCSharpSpecificFields)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    std::vector<int> argMap = { 0, -1 };
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), {},
        Res::OverloadResolutionErrors::ArgumentTypeMismatch, true, true, true,
        std::optional<std::vector<int>>(argMap));
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::CSharpInvocationResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->OverloadResolutionErrors(), Res::OverloadResolutionErrors::ArgumentTypeMismatch);
    EXPECT_TRUE(cloned->IsExtensionMethodInvocation());
    EXPECT_TRUE(cloned->IsExpandedForm());
    EXPECT_TRUE(cloned->IsDelegateInvocation());
    ASSERT_TRUE(cloned->GetArgumentToParameterMap().has_value());
    EXPECT_EQ(cloned->GetArgumentToParameterMap()->size(), 2u);
}

TEST(CSharpInvocationResolveResultTest, ShallowCloneIsDistinctInstance)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::CSharpInvocationResolveResult rr(MakeObjectTarget(), member.get());
    auto clone = rr.ShallowClone();
    EXPECT_NE(static_cast<Sem::ResolveResult*>(&rr), clone.get());
}

// ===========================================================================
// Polymorphic dispatch through a ResolveResult* base pointer (and an
// InvocationResolveResult* base pointer).
// ===========================================================================

TEST(CSharpInvocationResolveResultTest, DispatchesThroughResolveResultBasePointer)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), {},
        Res::OverloadResolutionErrors::AmbiguousMatch);
    Sem::ResolveResult* base = &rr;
    EXPECT_TRUE(base->IsError());
    // The inherited MemberResolveResult::ToString uses the member name ("M"), not the
    // type ReflectionName (the CSharpInvocationResolveResult does NOT override ToString).
    EXPECT_EQ(base->ToString(), "[CSharpInvocationResolveResult M]");
}

TEST(CSharpInvocationResolveResultTest, DispatchesThroughInvocationResolveResultBasePointer)
{
    TestCompilation compilation;
    auto member = MakeMethodMember(compilation);
    auto arg = MakeObjectTarget();
    Res::CSharpInvocationResolveResult rr(
        MakeObjectTarget(), member.get(), { arg },
        Res::OverloadResolutionErrors::None);
    Sem::InvocationResolveResult* base = &rr;
    // The base GetArgumentsForCall returns Arguments directly (the deferred override does
    // NOT rebuild for params-arrays here; the base default applies).
    EXPECT_FALSE(base->IsError());
    auto callArgs = base->GetArgumentsForCall();
    ASSERT_EQ(callArgs.size(), 1u);
    EXPECT_EQ(callArgs[0].get(), arg.get());
}

// ===========================================================================
// Class shape -- is_base_of / not-final / has-virtual-destructor / is-polymorphic.
// ===========================================================================

static_assert(std::is_base_of<Sem::InvocationResolveResult,
              Res::CSharpInvocationResolveResult>::value,
              "CSharpInvocationResolveResult derives from InvocationResolveResult");
static_assert(std::is_base_of<Sem::ResolveResult, Res::CSharpInvocationResolveResult>::value,
              "CSharpInvocationResolveResult derives from ResolveResult");
static_assert(!std::is_final<Res::CSharpInvocationResolveResult>::value,
              "CSharpInvocationResolveResult is not final (the C# class is unsealed)");
static_assert(std::has_virtual_destructor<Sem::ResolveResult>::value,
              "ResolveResult has a virtual destructor (deletion through base is safe)");
static_assert(std::is_polymorphic<Res::CSharpInvocationResolveResult>::value,
              "CSharpInvocationResolveResult is polymorphic (supports dynamic_cast)");
