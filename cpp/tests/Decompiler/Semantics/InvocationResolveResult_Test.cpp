// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software, including without limitation, the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `InvocationResolveResult` (cpp/Decompiler/Semantics/InvocationResolveResult.hpp,
// the D438 port of ICSharpCode.Decompiler/Semantics/InvocationResolveResult.cs) -- the
// result of a method, constructor or indexer invocation. Derives from `MemberResolveResult`
// (D437) and adds the call's `Arguments` and `InitializerStatements` (both
// `IList<ResolveResult>`), a `new IParameterizedMember Member` hiding downcast, a virtual
// `GetArgumentsForCall`, and a `GetChildResults` override that concats the base target
// result with the arguments then the initializer statements.
//
// The tests pin the ctor-stores-all-fields contract, the `Member` downcast-to-
// `IParameterizedMember` crux, the `Arguments`/`InitializerStatements` default-empty
// and configured shapes, the `GetArgumentsForCall`-returns-`Arguments` crux, the
// `GetChildResults` concat crux (base + arguments + initializers), the `ToString`
// subclass-class-name format, the `ShallowClone` runtime-type preservation plus
// shared-ownership of the argument/initializer lists, virtual dispatch through the
// base pointer, the inherited `MemberResolveResult` defaults, and the `is_base_of` /
// `has_virtual_dector` / `is_polymorphic` / not-`final` static-asserts.

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
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// can return a compilation (the D399 test stand-in pattern, identical in shape to the
// `TestCompilation` in `MemberResolveResult_Test.cpp` / `IParameterizedMember_Test.cpp`).
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
// the `TestAttribute` in `MemberResolveResult_Test.cpp` / `IMember_Test.cpp`).
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

// A minimal concrete `IParameter` for testing (the shape `IParameterizedMember::Parameters`
// holds; only `Name` is exercised, the rest return simple defaults so the stub compiles).
// IDENTICAL in shape to the `TestParameter` in `IParameterizedMember_Test.cpp`.
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(ILSpy::Decompiler::TypeSystem::SymbolKind kind, std::string name,
                  ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : kind_(kind), name_(std::move(name)), type_(std::move(type)) {}
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return ILSpy::Decompiler::TypeSystem::LifetimeAnnotation{}; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
private:
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
};

// A minimal concrete `IParameterizedMember` for testing: holds the configured scalar/pointer
// state and returns it from every accessor (the shape a real `MetadataMethod` takes). The
// ctor takes the essential fields (`name`, `kind`, `compilation`, `returnType`,
// `isOverridable`); the remaining members return simple defaults so the stub compiles.
// `Name()` is overridden ONCE and satisfies the `ISymbol::Name()` / `INamedElement::Name()`
// / `IEntity::Name()` contracts (the diamond is disambiguated by `IEntity`'s redeclaration,
// and a single override is the final overrider for all three). The three long-pole-dep
// members (`Substitution` / `Specialize` / `Equals`) use the `nullptr` / `this` / identity
// stand-ins (the documented "forward-declared-IMember, deferring the long-pole deps" shell
// strategy, the `IParameterizedMember_Test` precedent). `Parameters` returns the
// configured non-owning snapshot.
class TestParameterizedMember : public ILSpy::Decompiler::TypeSystem::IParameterizedMember {
public:
    TestParameterizedMember(std::string name,
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
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return nullptr; }
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

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override { return parameters_; }

    void AddParameter(const ILSpy::Decompiler::TypeSystem::IParameter* p) { parameters_.push_back(p); }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    bool isOverridable_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as a return type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// Convenience: a `KnownType(Int32)` (a value type) for use as an argument/parameter type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: build a `TypeResolveResult` over `KnownType(Object)` (a plain ResolveResult
// target that is NOT a `ThisResolveResult`, so the base `MemberResolveResult` ctor 1
// computes `isVirtualCall = member.IsOverridable && !CausesNonVirtualInvocation` as
// `IsOverridable && !(false && ...)` = `IsOverridable`).
std::shared_ptr<ILSpy::Decompiler::Semantics::TypeResolveResult> MakeObjectTarget()
{
    return std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeObjectType());
}

// Convenience: build a two-argument call (two `TypeResolveResult`s over `KnownType(Int32)`).
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> MakeTwoArgs()
{
    return {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type()),
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type())
    };
}

} // namespace

// ---------------------------------------------------------------------------
// The ctor stores the target, the member, and the (possibly empty) arguments and
// initializer statements. The base `MemberResolveResult` ctor 1 computes
// `isVirtualCall` from `member.IsOverridable` and the target (a plain `TypeResolveResult`
// is NOT a `ThisResolveResult`, so `isVirtualCall = isOverridable && !(false) = true`).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, CtorStoresTargetAndMemberAndComputesVirtualCall)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ true);
    auto target = MakeObjectTarget();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(target, &method);
    EXPECT_EQ(rr.TargetResult(), target.get());
    EXPECT_EQ(static_cast<const ILSpy::Decompiler::TypeSystem::IMember*>(&method),
              static_cast<const ILSpy::Decompiler::TypeSystem::IMember*>(rr.Member()));
    EXPECT_TRUE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// The `new Member` accessor crux: through an `InvocationResolveResult*`, `Member()`
// returns a `const IParameterizedMember*` (the downcast the C# `new` performs), NOT the
// base `const IMember*`. The pointer identity matches the original member.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, MemberDowncastReturnsIParameterizedMember)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto target = MakeObjectTarget();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(target, &method);
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* member =
        rr.Member();
    EXPECT_EQ(member, &method);
    // The downcast exposes the `IParameterizedMember`-own `Parameters` accessor.
    EXPECT_EQ(member->Parameters().size(), 0u);
}

// ---------------------------------------------------------------------------
// The ctor stores the configured `Arguments` (a non-empty shared_ptr vector) and the
// test can read the elements back through the `Arguments()` accessor by reference.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, CtorStoresConfiguredArguments)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto args = MakeTwoArgs();
    auto* arg0 = args[0].get();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        MakeObjectTarget(), &method, std::move(args));
    ASSERT_EQ(rr.Arguments().size(), 2u);
    EXPECT_EQ(rr.Arguments()[0].get(), arg0);
}

// ---------------------------------------------------------------------------
// The ctor defaults `Arguments` to empty (the C# `?? EmptyList<ResolveResult>.Instance`)
// when no arguments vector is passed.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, CtorDefaultsArgumentsToEmpty)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(MakeObjectTarget(), &method);
    EXPECT_EQ(rr.Arguments().size(), 0u);
}

// ---------------------------------------------------------------------------
// The ctor defaults `InitializerStatements` to empty (the C# `?? EmptyList`).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, CtorDefaultsInitializerStatementsToEmpty)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(MakeObjectTarget(), &method);
    EXPECT_EQ(rr.InitializerStatements().size(), 0u);
}

// ---------------------------------------------------------------------------
// The ctor stores the configured `InitializerStatements`.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, CtorStoresConfiguredInitializerStatements)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto initializers = MakeTwoArgs();
    auto* init0 = initializers[0].get();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        MakeObjectTarget(), &method, /*arguments*/ {}, std::move(initializers));
    ASSERT_EQ(rr.InitializerStatements().size(), 2u);
    EXPECT_EQ(rr.InitializerStatements()[0].get(), init0);
}

// ---------------------------------------------------------------------------
// The `returnTypeOverride` crux: when passed, the base type is the override (NOT the
// member's `ReturnType` which `ComputeType` would compute). Pinned by checking `Type()`
// is the override `KnownType(String)` distinct from the member's `KnownType(Object)`.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, CtorReturnTypeOverrideTakesPrecedence)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto overrideType = std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        MakeObjectTarget(), &method, /*arguments*/ {}, /*initializers*/ {},
        overrideType);
    EXPECT_EQ(rr.Type().Kind(), overrideType->Kind());
    EXPECT_EQ(rr.Type().Name(), overrideType->Name());
}

// ---------------------------------------------------------------------------
// `GetArgumentsForCall` returns `Arguments` (a copy of the stored shared_ptr vector).
// The base returns the same elements as the `Arguments()` accessor.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, GetArgumentsForCallReturnsArguments)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto args = MakeTwoArgs();
    auto* arg0 = args[0].get();
    auto* arg1 = args[1].get();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        MakeObjectTarget(), &method, std::move(args));
    auto forCall = rr.GetArgumentsForCall();
    ASSERT_EQ(forCall.size(), 2u);
    EXPECT_EQ(forCall[0].get(), arg0);
    EXPECT_EQ(forCall[1].get(), arg1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: concats the base's target result (one element, the
// `targetResult`) with `Arguments` then `InitializerStatements`. A call with a target
// and two arguments and one initializer yields 1 + 2 + 1 = 4 children in order.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, GetChildResultsConcatsTargetAndArgumentsAndInitializers)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto target = MakeObjectTarget();
    auto* targetPtr = target.get();
    auto args = MakeTwoArgs();
    auto* arg0 = args[0].get();
    auto* arg1 = args[1].get();
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> initializers = {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type())
    };
    auto* init0 = initializers[0].get();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        target, &method, std::move(args), std::move(initializers));
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 4u);
    EXPECT_EQ(children[0], targetPtr);
    EXPECT_EQ(children[1], arg0);
    EXPECT_EQ(children[2], arg1);
    EXPECT_EQ(children[3], init0);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with no target, no arguments, and no initializers is empty (the
// base `MemberResolveResult::GetChildResults` returns empty when the target is null).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, GetChildResultsEmptyWhenNoTargetAndNoArgsAndNoInitializers)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        /*target*/ nullptr, &method);
    EXPECT_EQ(rr.GetChildResults().size(), 0u);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with a target but no arguments and no initializers yields just the
// target (the base's one-element snapshot, no arguments/initializers to append).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, GetChildResultsTargetOnlyWhenNoArgsAndNoInitializers)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto target = MakeObjectTarget();
    auto* targetPtr = target.get();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(target, &method);
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], targetPtr);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the member name (the inherited
// `MemberResolveResult::ToString` uses the polymorphic `ClassName()` which the override
// returns "InvocationResolveResult"; the member name is `member->Name()`).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, ToStringReportsSubclassClassNameAndMemberName)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(MakeObjectTarget(), &method);
    EXPECT_EQ(rr.ToString(), "[InvocationResolveResult Add]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is an `InvocationResolveResult`,
// not the `MemberResolveResult` base a non-overriding clone would slice to). The clone's
// `ClassName()` reports "InvocationResolveResult" (the virtual dispatch through the clone).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(MakeObjectTarget(), &method);
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[InvocationResolveResult Add]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `Arguments` and `InitializerStatements` (the default copy
// ctor shares the shared_ptr vectors faithfully mirroring the C# `MemberwiseClone`
// reference-copy). The clone's elements are pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, ShallowCloneSharesArgumentsAndInitializerStatements)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto args = MakeTwoArgs();
    auto* arg0 = args[0].get();
    auto initializers = MakeTwoArgs();
    auto* init0 = initializers[0].get();
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(
        MakeObjectTarget(), &method, std::move(args), std::move(initializers));
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::InvocationResolveResult*>(
        clone.get());
    ASSERT_EQ(derived->Arguments().size(), 2u);
    EXPECT_EQ(derived->Arguments()[0].get(), arg0);
    ASSERT_EQ(derived->InitializerStatements().size(), 2u);
    EXPECT_EQ(derived->InitializerStatements()[0].get(), init0);
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the
// original). The `TargetResult` pointers (the base `targetResult_` shared_ptr) are
// shared but the `InvocationResolveResult` objects are distinct.
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, ShallowCloneIsDistinctInstance)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::InvocationResolveResult>(
        MakeObjectTarget(), &method);
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `MemberResolveResult*` base pointer: the `GetChildResults`
// override is dispatched through the base pointer (the C# resolver reaches the
// `InvocationResolveResult` overrides through a `MemberResolveResult` reference).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, VirtualDispatchThroughBasePointer)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    auto args = MakeTwoArgs();
    auto* arg0 = args[0].get();
    std::unique_ptr<ILSpy::Decompiler::Semantics::MemberResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::InvocationResolveResult>(
            MakeObjectTarget(), &method, std::move(args));
    auto children = rr->GetChildResults();
    ASSERT_EQ(children.size(), 3u);
    EXPECT_EQ(children[1], arg0);
}

// ---------------------------------------------------------------------------
// The inherited `MemberResolveResult` defaults are preserved: an invocation is not a
// compile-time constant, and its `ConstantValue` is empty (the base defaults).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, InheritedMemberResolveResultDefaultsArePreserved)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestParameterizedMember method("Add", ILSpy::Decompiler::TypeSystem::SymbolKind::Method,
                                    compilation, returnType, /*isOverridable*/ false);
    ILSpy::Decompiler::Semantics::InvocationResolveResult rr(MakeObjectTarget(), &method);
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `InvocationResolveResult` IS-A `MemberResolveResult` / `ResolveResult`,
// is polymorphic with a virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(InvocationResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::MemberResolveResult,
        ILSpy::Decompiler::Semantics::InvocationResolveResult>);
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::InvocationResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::InvocationResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::InvocationResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::InvocationResolveResult>);
}
