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

// Tests for `MethodListWithDeclaringType` + `MethodGroupResolveResult` (the eleventh
// `cpp/Decompiler/CSharp/Resolver/` leaf, the port of
// ICSharpCode.Decompiler/CSharp/Resolver/MethodGroupResolveResult.cs). The
// `MethodListWithDeclaringType` is the per-declaring-type method bucket (a value-add
// `vector<const IParameterizedMember*>`); the `MethodGroupResolveResult : ResolveResult`
// represents a group of methods (a method reference used to create a delegate / a method
// group the resolver produced) -- the result has NO type (the base is
// `SpecialType.NoType`); to retrieve the chosen overload, look at the method-group
// conversion. The load-bearing cruxes are: the `NoType` base (NOT `UnknownType`), the
// `TargetType` null-target short-circuit to `SpecialType.UnknownType`, the `ToString`
// `"[MethodGroupResolveResult with N method(s)]"` custom format, and the
// `WithChosenMethod` runtime-type-preserving clone that returns a fresh instance with the
// chosen method set (the original untouched). The extension-method machinery
// (`GetExtensionMethods` / `GetEligibleExtensionMethods`) and `PerformOverloadResolution`
// are deferred (they need `OverloadResolution` / `TypeInference` / `CSharpConversions`,
// all unported); a resolver-less `GetExtensionMethods()` returns empty. The tests pin the
// ctor-stores-fields + forwards-`NoType`-to-base contract, the `Methods` flatten across
// declaring-type buckets (base types first), `MethodsGroupedByDeclaringType`, the
// `TypeArguments` snapshot (empty default), `TargetResult`/`TargetType` (`UnknownType`
// for a null target), `ChosenMethod` (null default) + `WithChosenMethod`, the custom
// `ToString`, `GetChildResults` (the target or empty), the deferred-state
// `GetExtensionMethods`, and the class-shape static-asserts.

#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

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

// A minimal concrete `ICompilation` stand-in (the D399 test stand-in pattern, identical
// in shape to the `TestCompilation` in `InvocationResolveResult_Test.cpp`).
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

// A minimal concrete `IMethod` for testing: holds a name and returns trivial defaults for
// every accessor. Only the pointer identity (the address) is exercised by the tests (the
// `Methods` enumeration yields the stored `const IMethod*` addresses; the type system
// owns the method, the group observes). The covariant `const IMethod* Specialize`
// override (inherited from `IMethod`) covers the `IMember::Specialize` slot.
class TestMethod : public TS::IMethod {
public:
    explicit TestMethod(std::string name, const TestCompilation& compilation = DefaultCompilation())
        : name_(std::move(name)), compilation_(compilation) {}

    // A shared, never-destroyed compilation for the common no-explicit-compilation case.
    static const TestCompilation& DefaultCompilation()
    {
        static const TestCompilation instance;
        return instance;
    }

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
    const TS::IType& ReturnType() const override { return returnType_; }
    std::vector<const TS::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    // NOTE: only the covariant `const IMethod* Specialize` override below is declared;
    // declaring the `const IMember*` overload would hide it. The covariant override covers
    // the inherited `IMember::Specialize` (covariant return), satisfying the interface.
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
    // 'Methods' body flattens into a vector, so expose a count for the ToString test.
    std::size_t ParamCount() const { return 0; }
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
    TS::MethodSemanticsAttributes AccessorKind() const override
    {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }
    const TS::IMethod* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
private:
    std::string name_;
    const TestCompilation& compilation_;
    TS::KnownType returnType_{TS::KnownTypeCode::Object};
};

} // namespace

// ===========================================================================
// Class shape -- is_base_of / not-final / polymorphic.
// ===========================================================================

static_assert(std::is_base_of<Sem::ResolveResult, Res::MethodGroupResolveResult>::value,
              "MethodGroupResolveResult derives from ResolveResult");
static_assert(!std::is_final<Res::MethodGroupResolveResult>::value,
              "MethodGroupResolveResult is not final (the C# class is unsealed)");
static_assert(std::is_polymorphic<Res::MethodGroupResolveResult>::value,
              "MethodGroupResolveResult is polymorphic (supports dynamic_cast)");
static_assert(std::has_virtual_destructor<Sem::ResolveResult>::value,
              "ResolveResult has a virtual destructor (deletion through base is safe)");

// ===========================================================================
// MethodListWithDeclaringType -- the per-declaring-type bucket.
// ===========================================================================

TEST(MethodListWithDeclaringTypeTest, ConstructorStoresDeclaringType)
{
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base);
    EXPECT_EQ(&list.DeclaringType(), base.get());
}

TEST(MethodListWithDeclaringTypeTest, ConstructorStoresDeclaringTypeAndMethods)
{
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    TestMethod m1("M");
    TestMethod m2("M2");
    Res::MethodListWithDeclaringType list(base, { &m1, &m2 });
    EXPECT_EQ(&list.DeclaringType(), base.get());
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], &m1);
    EXPECT_EQ(list[1], &m2);
}

TEST(MethodListWithDeclaringTypeTest, IsAVectorOfParameterizedMemberPointers)
{
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base);
    // The bucket is a vector of non-owning `const IParameterizedMember*` pointers.
    static_assert(std::is_same<decltype(list)::value_type, const TS::IParameterizedMember*>::value,
                  "MethodListWithDeclaringType stores const IParameterizedMember* elements");
    SUCCEED();
}

// ===========================================================================
// Ctor -- the base type is NoType (a method group has NO type), not UnknownType.
// ===========================================================================

TEST(MethodGroupResolveResultTest, BaseTypeIsNoTypeNotUnknownType)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    // The base type is `SpecialType.NoType` (TypeKind::None) -- a method group has NO
    // type (the delegate type comes from the method-group conversion), NOT an unknown type.
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::None);
    EXPECT_NE(rr.Type().Kind(), TS::TypeKind::Unknown);
}

TEST(MethodGroupResolveResultTest, ConstructorStoresTargetNameListsAndTypeArguments)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    auto target = std::make_shared<Sem::TypeResolveResult>(base);
    auto typeArg = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::MethodGroupResolveResult rr(target, "M", { list }, { typeArg });
    EXPECT_EQ(rr.TargetResult(), target.get());
    EXPECT_EQ(rr.MethodName(), "M");
    ASSERT_EQ(rr.MethodsGroupedByDeclaringType().size(), 1u);
    EXPECT_EQ(&rr.MethodsGroupedByDeclaringType()[0].DeclaringType(), base.get());
    ASSERT_EQ(rr.TypeArguments().size(), 1u);
    EXPECT_EQ(rr.TypeArguments()[0].get(), typeArg.get());
}

// ===========================================================================
// TargetType -- the type of the reference to the target object (UnknownType for a null
// target, the target's Type() otherwise).
// ===========================================================================

TEST(MethodGroupResolveResultTest, TargetTypeIsTargetTypeWhenTargetPresent)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    auto target = std::make_shared<Sem::TypeResolveResult>(base);
    Res::MethodGroupResolveResult rr(target, "M", { list }, {});
    EXPECT_EQ(&rr.TargetType(), base.get());
}

TEST(MethodGroupResolveResultTest, TargetTypeIsUnknownTypeWhenTargetNull)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    // The C# `TargetType => targetResult != null ? targetResult.Type : SpecialType.UnknownType`;
    // a null target yields the `TypeKind::Unknown` null object.
    EXPECT_EQ(rr.TargetType().Kind(), TS::TypeKind::Unknown);
}

// ===========================================================================
// Methods -- flattens across declaring-type buckets (base types first), in bucket order.
// ===========================================================================

TEST(MethodGroupResolveResultTest, MethodsFlattensAcrossDeclaringTypeBuckets)
{
    TestMethod baseMethod("M");      // Base.M()
    TestMethod derivedMethod("M");   // Derived.M(int)
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto derived = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType baseList(base, { &baseMethod });
    Res::MethodListWithDeclaringType derivedList(derived, { &derivedMethod });
    Res::MethodGroupResolveResult rr(nullptr, "M", { baseList, derivedList }, {});
    auto methods = rr.Methods();
    ASSERT_EQ(methods.size(), 2u);
    // Base types come first in the list (the C# `Methods => methodLists.SelectMany(...)`).
    EXPECT_EQ(methods[0], &baseMethod);
    EXPECT_EQ(methods[1], &derivedMethod);
}

TEST(MethodGroupResolveResultTest, MethodsEmptyWhenNoLists)
{
    Res::MethodGroupResolveResult rr(nullptr, "M", {}, {});
    EXPECT_TRUE(rr.Methods().empty());
}

// ===========================================================================
// MethodsGroupedByDeclaringType -- the grouping (base types first).
// ===========================================================================

TEST(MethodGroupResolveResultTest, MethodsGroupedByDeclaringTypeExposesBuckets)
{
    TestMethod baseMethod("M");
    TestMethod derivedMethod("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto derived = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType baseList(base, { &baseMethod });
    Res::MethodListWithDeclaringType derivedList(derived, { &derivedMethod });
    Res::MethodGroupResolveResult rr(nullptr, "M", { baseList, derivedList }, {});
    auto groups = rr.MethodsGroupedByDeclaringType();
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(&groups[0].DeclaringType(), base.get());
    EXPECT_EQ(&groups[1].DeclaringType(), derived.get());
    ASSERT_EQ(groups[0].size(), 1u);
    EXPECT_EQ(groups[0][0], &baseMethod);
    ASSERT_EQ(groups[1].size(), 1u);
    EXPECT_EQ(groups[1][0], &derivedMethod);
}

// ===========================================================================
// TypeArguments -- the explicitly provided type arguments (empty by default).
// ===========================================================================

TEST(MethodGroupResolveResultTest, TypeArgumentsEmptyByDefault)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list });
    EXPECT_TRUE(rr.TypeArguments().empty());
}

// ===========================================================================
// ChosenMethod -- null by default; WithChosenMethod returns a fresh instance with it set.
// ===========================================================================

TEST(MethodGroupResolveResultTest, ChosenMethodNullByDefault)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    EXPECT_EQ(rr.ChosenMethod(), nullptr);
}

TEST(MethodGroupResolveResultTest, WithChosenMethodReturnsFreshInstanceWithChosenMethodSet)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    auto withChosen = rr.WithChosenMethod(&m);
    ASSERT_NE(withChosen, nullptr);
    EXPECT_EQ(withChosen->ChosenMethod(), &m);
    // The original is untouched (the C# `ShallowClone`; `chosenMethod` set only on the clone).
    EXPECT_EQ(rr.ChosenMethod(), nullptr);
}

TEST(MethodGroupResolveResultTest, WithChosenMethodPreservesRuntimeType)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    auto withChosen = rr.WithChosenMethod(&m);
    EXPECT_EQ(dynamic_cast<Res::MethodGroupResolveResult*>(withChosen.get()), withChosen.get());
}

TEST(MethodGroupResolveResultTest, WithChosenMethodCopiesGroupFields)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    auto target = std::make_shared<Sem::TypeResolveResult>(base);
    auto typeArg = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::MethodGroupResolveResult rr(target, "M", { list }, { typeArg });
    auto withChosen = rr.WithChosenMethod(&m);
    // The shallow-cloned fields are preserved (target, name, lists, type arguments).
    EXPECT_EQ(withChosen->TargetResult(), target.get());
    EXPECT_EQ(withChosen->MethodName(), "M");
    ASSERT_EQ(withChosen->MethodsGroupedByDeclaringType().size(), 1u);
    ASSERT_EQ(withChosen->TypeArguments().size(), 1u);
    EXPECT_EQ(withChosen->TypeArguments()[0].get(), typeArg.get());
}

// ===========================================================================
// ToString -- the custom "[MethodGroupResolveResult with N method(s)]" format.
// ===========================================================================

TEST(MethodGroupResolveResultTest, ToStringUsesCustomFormat)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    EXPECT_EQ(rr.ToString(), "[MethodGroupResolveResult with 1 method(s)]");
}

TEST(MethodGroupResolveResultTest, ToStringCountsAllMethodsAcrossBuckets)
{
    TestMethod m1("M");
    TestMethod m2("M");
    TestMethod m3("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto derived = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType baseList(base, { &m1, &m2 });
    Res::MethodListWithDeclaringType derivedList(derived, { &m3 });
    Res::MethodGroupResolveResult rr(nullptr, "M", { baseList, derivedList }, {});
    EXPECT_EQ(rr.ToString(), "[MethodGroupResolveResult with 3 method(s)]");
}

TEST(MethodGroupResolveResultTest, ToStringIsNotTheInheritedBracketForm)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    // The custom ToString is NOT the inherited `ResolveResult::ToString` "[Name Type]" form.
    EXPECT_NE(rr.ToString().find("method(s)"), std::string::npos);
}

// ===========================================================================
// GetChildResults -- the target (when present), empty otherwise.
// ===========================================================================

TEST(MethodGroupResolveResultTest, GetChildResultsIsTargetWhenPresent)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    auto target = std::make_shared<Sem::TypeResolveResult>(base);
    Res::MethodGroupResolveResult rr(target, "M", { list }, {});
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], target.get());
}

TEST(MethodGroupResolveResultTest, GetChildResultsEmptyWhenTargetNull)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    EXPECT_TRUE(rr.GetChildResults().empty());
}

// ===========================================================================
// GetExtensionMethods -- the deferred state (resolver-less => empty; the one-shot
// resolver-fetch + the eligibility filter land with OverloadResolution/TypeInference).
// ===========================================================================

TEST(MethodGroupResolveResultTest, GetExtensionMethodsEmptyWhenNoResolver)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    // With no resolver attached, the deferred state yields no candidate extension methods.
    EXPECT_TRUE(rr.GetExtensionMethods().empty());
}

// ===========================================================================
// ShallowClone -- preserves the runtime type, copies the group fields.
// ===========================================================================

TEST(MethodGroupResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(dynamic_cast<Res::MethodGroupResolveResult*>(clone.get()), clone.get());
}

TEST(MethodGroupResolveResultTest, ShallowCloneCopiesGroupFields)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    auto target = std::make_shared<Sem::TypeResolveResult>(base);
    Res::MethodGroupResolveResult rr(target, "M", { list }, {});
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::MethodGroupResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->TargetResult(), target.get());
    EXPECT_EQ(cloned->MethodName(), "M");
    ASSERT_EQ(cloned->MethodsGroupedByDeclaringType().size(), 1u);
}

// ===========================================================================
// Polymorphic dispatch through a ResolveResult* base pointer.
// ===========================================================================

TEST(MethodGroupResolveResultTest, DispatchesThroughResolveResultBasePointer)
{
    TestMethod m("M");
    auto base = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::MethodListWithDeclaringType list(base, { &m });
    Res::MethodGroupResolveResult rr(nullptr, "M", { list }, {});
    Sem::ResolveResult* basePtr = &rr;
    EXPECT_EQ(basePtr->ToString(), "[MethodGroupResolveResult with 1 method(s)]");
    EXPECT_TRUE(basePtr->GetChildResults().empty());
}
