// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT AND IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for `NamedArgumentResolveResult` (cpp/Decompiler/Semantics/
// NamedArgumentResolveResult.hpp, the D446 port of
// ICSharpCode.Decompiler/Semantics/NamedArgumentResolveResult.cs) -- the result of a
// named argument expression (`name: argument`). Derives directly from `ResolveResult`
// (D424) and carries the owning `IParameterizedMember` (nullable), the `IParameter`
// (nullable -- set only by the first ctor), the `ParameterName` (a `string`), and the
// `Argument` (a non-null `ResolveResult`); overrides `GetChildResults` to return the
// single `Argument`.
//
// The tests pin both ctors' field-storage contracts (ctor 1 derives `ParameterName` from
// `parameter.Name` and stores the nullable `Member`/`Parameter`; ctor 2 takes
// `ParameterName` directly and leaves `Member`/`Parameter` null), the `Argument`/`Type`
// forwarding, the `GetChildResults` returns-the-single-`Argument` crux, the `ToString`
// subclass-class-name format, the `ShallowClone` runtime-type preservation plus shared-
// ownership-of-`Argument` plus value-copy-of-`ParameterName` plus copy-of-the-raw-
// pointers, virtual dispatch through the base pointer, the inherited `ResolveResult`
// defaults, and the `is_base_of` / `has_virtual_destructor` / `is_polymorphic` /
// not-`final` static-asserts.

#include "Decompiler/Semantics/NamedArgumentResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Convenience: a `KnownType(Int32)` (the argument's type forwarded to the base; its
// `ReflectionName()` is "System.Int32").
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: a `KnownType(String)` (a distinct argument type for the ctor-2 case).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// Convenience: build a `TypeResolveResult` over `KnownType(Int32)` -- the `Argument` value
// carried by the named-argument result. Held by `shared_ptr` (the D428/D431
// shared-ownership precedent the `NamedArgumentResolveResult` ctor takes).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> MakeInt32Argument()
{
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type());
}

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// of the `IParameterizedMember` stub can return a compilation (the D399 test stand-in
// pattern, identical in shape to the `TestCompilation` in `ForEachResolveResult_Test.cpp`).
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

// A minimal concrete `IParameterizedMember` for testing the non-null `Member` case: holds
// a `TestCompilation` reference (for the inherited `ICompilationProvider::Compilation`)
// and a return type (for the inherited `IMember::ReturnType`), and returns trivial defaults
// for every other accessor. Only pointer-identity (the address) is exercised by the
// `NamedArgumentResolveResult` tests, so the field values are not configured. The shape
// follows the `TestParameterizedMember` stub in `IParameter_Test.cpp` (the
// `IParameterizedMember` port's own test), adapted with a configurable id for
// pointer-identity.
class TestParameterizedMember : public ILSpy::Decompiler::TypeSystem::IParameterizedMember {
public:
    explicit TestParameterizedMember(int id)
        : id_(id),
          returnType_(std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
              ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void)) {}

    int id() const { return id_; }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    std::string Name() const override { return "method"; }

    // --- INamedElement ---
    std::string FullName() const override { return "method"; }
    std::string ReflectionName() const override { return "method"; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0u; }
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

private:
    int id_;
    TestCompilation compilation_{0};
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
};

// A minimal concrete `IParameter` for testing: holds a configurable name and type (the two
// fields the `NamedArgumentResolveResult` ctor 1 reads -- `Name()` for the derived
// `ParameterName`, and `Type()` is unused but required to be concrete), and returns trivial
// defaults for every other accessor. The shape follows the `TestParameter` stub in
// `IParameter_Test.cpp` (the `IParameter` port's own test), simplified to the two
// configurable fields the named-argument tests exercise.
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(std::string name, ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }
    std::string Name() const override { return name_; }

    // --- IVariable ---
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override { return {}; }

    // --- IParameter ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override {
        return ILSpy::Decompiler::TypeSystem::ReferenceKind::None;
    }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override {
        return ILSpy::Decompiler::TypeSystem::LifetimeAnnotation{};
    }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
};

} // namespace

// ---------------------------------------------------------------------------
// Ctor 1 stores all four fields: `Member` (the nullable member, here non-null),
// `Parameter` (the parameter), `ParameterName` (derived from `parameter->Name()`), and
// `Argument`. The base `Type()` is the argument's type (forwarded via `argument->Type()`).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, Ctor1StoresAllFieldsAndDerivesParameterName)
{
    auto argument = MakeInt32Argument();
    auto* argumentPtr = argument.get();
    auto parameter = std::make_unique<TestParameter>("value", MakeInt32Type());
    auto* parameterPtr = parameter.get();
    TestParameterizedMember member(7);
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        parameterPtr, argument, &member);
    EXPECT_EQ(rr.Member(), &member);
    EXPECT_EQ(rr.Parameter(), parameterPtr);
    EXPECT_EQ(rr.ParameterName(), "value");
    EXPECT_EQ(rr.Argument(), argumentPtr);
    EXPECT_EQ(&rr.Type(), &argumentPtr->Type());
}

// ---------------------------------------------------------------------------
// Ctor 1 with the default `member = nullptr` leaves `Member()` null (the C# field may be
// null; the first ctor's `member` param defaults to null).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, Ctor1DefaultsMemberToNull)
{
    auto parameter = std::make_unique<TestParameter>("value", MakeInt32Type());
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        parameter.get(), MakeInt32Argument());
    EXPECT_EQ(rr.Member(), nullptr);
}

// ---------------------------------------------------------------------------
// Ctor 2 stores the `ParameterName` (taken directly) and the `Argument`, and leaves
// `Member` and `Parameter` at the `nullptr` default (ctor 2 does not set them).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, Ctor2StoresParameterNameAndArgumentAndLeavesMemberParameterNull)
{
    auto argument = MakeInt32Argument();
    auto* argumentPtr = argument.get();
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("name"), argument);
    EXPECT_EQ(rr.ParameterName(), "name");
    EXPECT_EQ(rr.Argument(), argumentPtr);
    EXPECT_EQ(rr.Member(), nullptr);
    EXPECT_EQ(rr.Parameter(), nullptr);
}

// ---------------------------------------------------------------------------
// Ctor 2's `ParameterName` is the passed string (NOT derived from a parameter's `Name` --
// ctor 2 has no `IParameter`), so a name distinct from any parameter's name is stored
// verbatim.
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, Ctor2ParameterNameIsThePassedString)
{
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("customName"), MakeInt32Argument());
    EXPECT_EQ(rr.ParameterName(), "customName");
}

// ---------------------------------------------------------------------------
// The `Argument` accessor returns a non-owning raw pointer that is pointer-identical to
// the original `shared_ptr`'s `get()` (the `shared_ptr` keeps the `ResolveResult` alive).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ArgumentAccessorReturnsStoredPointer)
{
    auto argument = MakeInt32Argument();
    auto* argumentPtr = argument.get();
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("x"), argument);
    EXPECT_EQ(rr.Argument(), argumentPtr);
}

// ---------------------------------------------------------------------------
// The base `Type()` is the argument's type (forwarded to the `ResolveResult` base via
// `argument->Type()` in both ctors). Ctor 2 over a `String` argument yields
// `KnownType(String)`.
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, TypeIsForwardedFromArgument)
{
    auto argument = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeStringType());
    auto* argumentPtr = argument.get();
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("s"), argument);
    EXPECT_EQ(&rr.Type(), &argumentPtr->Type());
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: returns the single `Argument` (a one-element snapshot). The
// base default returns empty; the override returns the argument's non-owning pointer.
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, GetChildResultsReturnsSingleArgument)
{
    auto argument = MakeInt32Argument();
    auto* argumentPtr = argument.get();
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("x"), argument);
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], argumentPtr);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the argument's type (the inherited
// `ResolveResult::ToString` uses the polymorphic `ClassName()` which the override returns
// "NamedArgumentResolveResult"; the type is the argument's `KnownType(Int32)` whose
// `ReflectionName()` is "System.Int32").
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ToStringReportsSubclassClassNameAndArgumentType)
{
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("x"), MakeInt32Argument());
    EXPECT_EQ(rr.ToString(), "[NamedArgumentResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is a `NamedArgumentResolveResult`,
// not the `ResolveResult` base a non-overriding clone would slice to). The clone's
// `ToString()` reports "NamedArgumentResolveResult" (the virtual dispatch through the
// clone).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ShallowClonePreservesRuntimeType)
{
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("x"), MakeInt32Argument());
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[NamedArgumentResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `Argument` (the default copy ctor shares the `argument_`
// shared_ptr faithfully mirroring the C# `MemberwiseClone` reference-copy). The clone's
// `Argument` is pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ShallowCloneSharesArgument)
{
    auto argument = MakeInt32Argument();
    auto* argumentPtr = argument.get();
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("x"), argument);
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::NamedArgumentResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->Argument(), argumentPtr);
}

// ---------------------------------------------------------------------------
// `ShallowClone` value-copies the `ParameterName` (the default copy ctor value-copies the
// `std::string` member). The clone's parameter name equals the original's.
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ShallowCloneCopiesParameterName)
{
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("myName"), MakeInt32Argument());
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::NamedArgumentResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->ParameterName(), "myName");
}

// ---------------------------------------------------------------------------
// `ShallowClone` copies the non-owning raw pointers `Member` and `Parameter` (the default
// copy ctor copies them faithfully mirroring the C# `MemberwiseClone` reference-copy). The
// clone's `Member`/`Parameter` are pointer-identical to the original's (ctor 1 case).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ShallowCloneCopiesMemberAndParameterPointers)
{
    auto parameter = std::make_unique<TestParameter>("value", MakeInt32Type());
    auto* parameterPtr = parameter.get();
    TestParameterizedMember member(7);
    auto* memberPtr = &member;
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        parameterPtr, MakeInt32Argument(), memberPtr);
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::NamedArgumentResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->Member(), memberPtr);
    EXPECT_EQ(derived->Parameter(), parameterPtr);
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the
// original). The `Type()` (the base `type_` shared_ptr) is shared but the
// `NamedArgumentResolveResult` objects are distinct.
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::NamedArgumentResolveResult>(
        std::string("x"), MakeInt32Argument());
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `ResolveResult*` base pointer: the `GetChildResults`
// override is dispatched through the base pointer (the C# resolver reaches the
// `NamedArgumentResolveResult` overrides through a `ResolveResult` reference).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, VirtualDispatchThroughBasePointer)
{
    auto argument = MakeInt32Argument();
    auto* argumentPtr = argument.get();
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::NamedArgumentResolveResult>(
            std::string("x"), std::move(argument));
    auto children = rr->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], argumentPtr);
}

// ---------------------------------------------------------------------------
// The inherited `ResolveResult` defaults are preserved: a named argument is not a
// compile-time constant, its `ConstantValue` is empty, and it is not an error (the base
// defaults).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, InheritedResolveResultDefaultsArePreserved)
{
    ILSpy::Decompiler::Semantics::NamedArgumentResolveResult rr(
        std::string("x"), MakeInt32Argument());
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `NamedArgumentResolveResult` IS-A `ResolveResult`, is polymorphic with a
// virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(NamedArgumentResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::NamedArgumentResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::NamedArgumentResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::NamedArgumentResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::NamedArgumentResolveResult>);
}
