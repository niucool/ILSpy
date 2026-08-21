// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including limitation, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ITypeReference` (cpp/Decompiler/TypeSystem/ITypeReference.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/ITypeReference.cs lines 27-49). `ITypeReference` is
// the 1-method interface that a type reference implements to resolve itself to an `IType`
// against an `ITypeResolveContext`. It is the base of `KnownTypeReference` (which
// `TypeSystemAstBuilder` reaches to look up the metadata name of a known type) and a leaf
// TypeSystem dependency toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`).
//
// `ITypeResolveContext` (the `Resolve` parameter type) is NOT yet ported -- it is only
// forward-declared in `ITypeReference.hpp` -- so this test file provides a minimal
// complete stand-in `ITypeResolveContext` in the `ILSpy::Decompiler::TypeSystem` namespace
// (a virtual destructor only) so the `TestTypeReference::Resolve` override can take a
// `const ITypeResolveContext&`. This stand-in is a TEST FIXTURE, NOT a faithful port of
// the full `ITypeResolveContext` surface (which extends `ICompilationProvider` with the
// current-module / current-type-definition / current-member slots and two `With*`
// factories); the real `ITypeResolveContext` interface lands as a separate later leaf, at
// which point this stand-in is dropped in favour of the real header (the established
// stand-in-reconciliation step, the D379 `ICompilation` stand-in precedent). No other
// translation unit in the test build defines `ITypeResolveContext`, so this stand-in is
// the sole definition (ODR-safe for the test executable).
//
// The test's `TestTypeReference` IGNORES the context (`Resolve` returns its configured
// `KnownType` member, never reading the context), which is why the dtor-only stand-in
// suffices: the stand-in is only needed so the `const ITypeResolveContext&` parameter
// binds to a complete type. The production `KnownTypeReference::Resolve` (not yet ported)
// WILL read the context (`context.Compilation.FindType(knownTypeCode)`), so it will
// require the real `ITypeResolveContext` -- the deferred-consumer shape the
// stand-in-reconciliation handles.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// A minimal complete stand-in for `ITypeResolveContext` (not yet ported). The real
// interface extends `ICompilationProvider` with three nullable accessors and two `With*`
// factories; this stand-in is a virtual destructor only, sufficient for the test's
// `TestTypeReference::Resolve` to take a `const ITypeResolveContext&` it ignores. It is
// the SOLE `ITypeResolveContext` definition in the test build (ODR-safe), dropped when the
// real `ITypeResolveContext.hpp` lands.
class ITypeResolveContext {
public:
    virtual ~ITypeResolveContext() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace {

// A concrete `ITypeReference` for testing: holds a `KnownType` member (the resolved type)
// and returns it from `Resolve`, ignoring the context. This is the shape a real
// `KnownTypeReference` will take (it resolves via `context.Compilation.FindType(...)`,
// but the test stub short-circuits to a pre-built `KnownType` so it needs no
// `ICompilation` wiring). The `KnownType` is held by value and returned by const
// reference (the non-null-reference `Resolve` contract; the reference is valid for the
// reference's lifetime).
class TestTypeReference : public ILSpy::Decompiler::TypeSystem::ITypeReference {
public:
    explicit TestTypeReference(ILSpy::Decompiler::TypeSystem::KnownTypeCode code)
        : knownType_(code) {}

    const ILSpy::Decompiler::TypeSystem::IType& Resolve(
        const ILSpy::Decompiler::TypeSystem::ITypeResolveContext& /*context*/) const override
    {
        return knownType_;
    }

private:
    ILSpy::Decompiler::TypeSystem::KnownType knownType_;
};

} // namespace

// ---------------------------------------------------------------------------
// ITypeReference -- `Resolve` returns the configured `IType` (the crux: the reference's
// `Resolve` dispatches to the override and returns the `IType` the reference was
// constructed to resolve to). Pinned by the `KnownType` `Kind()` / `Name()` the configured
// code maps to.
// ---------------------------------------------------------------------------
TEST(ITypeReferenceTest, ResolveReturnsConfiguredType)
{
    TestTypeReference ref(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
    ILSpy::Decompiler::TypeSystem::ITypeResolveContext standIn;
    const auto& result = ref.Resolve(standIn);
    EXPECT_EQ(result.Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(result.Name(), "Object");
}

// ---------------------------------------------------------------------------
// ITypeReference -- `Resolve` returns a non-null reference (the C# doc comment
// "Never returns null"; an unresolvable reference returns an `UnknownType`, not null).
// The non-null-reference convention (the `ICompilation::FindType` precedent) means the
// returned reference is always usable as an `IType`.
// ---------------------------------------------------------------------------
TEST(ITypeReferenceTest, ResolveReturnsNonNullReference)
{
    TestTypeReference ref(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
    ILSpy::Decompiler::TypeSystem::ITypeResolveContext standIn;
    const auto* ptr = &ref.Resolve(standIn);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(ptr->Name(), "String");
}

// ---------------------------------------------------------------------------
// ITypeReference -- `Resolve` dispatches polymorphically through an `ITypeReference*`
// (the dynamic dispatch the type-system paths rely on: the type reader / resolver hold a
// type reference as an `ITypeReference` and reach the resolved `IType` through the base
// pointer, without knowing the concrete `KnownTypeReference` / `ArrayTypeReference` /
// ... kind).
// ---------------------------------------------------------------------------
TEST(ITypeReferenceTest, ResolveDispatchesPolymorphicallyThroughBasePointer)
{
    TestTypeReference ref(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void);
    const ILSpy::Decompiler::TypeSystem::ITypeReference* base = &ref;
    ILSpy::Decompiler::TypeSystem::ITypeResolveContext standIn;
    EXPECT_EQ(base->Resolve(standIn).Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Void);
    EXPECT_EQ(base->Resolve(standIn).Name(), "Void");
}

// ---------------------------------------------------------------------------
// ITypeReference -- `Resolve` is `const`-callable (the override reads the reference
// without modifying it; the C# instance method ports to a `const` member). A
// `const ITypeReference&` dispatches to the `const Resolve` overload.
// ---------------------------------------------------------------------------
TEST(ITypeReferenceTest, ResolveIsCallableOnConstReference)
{
    const TestTypeReference ref(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
    ILSpy::Decompiler::TypeSystem::ITypeResolveContext standIn;
    const ILSpy::Decompiler::TypeSystem::ITypeReference& base = ref;
    EXPECT_EQ(base.Resolve(standIn).Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Struct);
    EXPECT_EQ(base.Resolve(standIn).Name(), "Int32");
}

// ---------------------------------------------------------------------------
// ITypeReference -- two references constructed with different `KnownTypeCode`s resolve to
// distinct `IType`s (the references are independent; each carries its own resolved
// type). Pinned by the `Kind()` / `Name()` of each reference's resolved `IType`.
// ---------------------------------------------------------------------------
TEST(ITypeReferenceTest, TwoDistinctReferencesReturnDistinctTypes)
{
    TestTypeReference refObject(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
    TestTypeReference refString(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
    ILSpy::Decompiler::TypeSystem::ITypeResolveContext standIn;
    EXPECT_EQ(refObject.Resolve(standIn).Name(), "Object");
    EXPECT_EQ(refString.Resolve(standIn).Name(), "String");
    // The two references' resolved ITypes are distinct instances.
    EXPECT_NE(&refObject.Resolve(standIn), &refString.Resolve(standIn));
}

// ---------------------------------------------------------------------------
// ITypeReference -- the interface is abstract (cannot be instantiated) and has a virtual
// destructor; a concrete subclass overriding the one pure-virtual is instantiable. The
// `ITypeResolveContext` parameter type is forward-declared in the header, so the
// interface compiles with the context incomplete.
// ---------------------------------------------------------------------------
TEST(ITypeReferenceTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ITypeReference>,
                  "ITypeReference must have a virtual destructor for abstract-base deletion.");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::ITypeReference>,
                  "ITypeReference must be abstract (one pure-virtual Resolve accessor).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::ITypeReference>,
                  "ITypeReference must be polymorphic.");
    // A concrete subclass overriding the one pure-virtual is instantiable.
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeReference> p =
        std::make_unique<TestTypeReference>(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
    EXPECT_NE(p, nullptr);
    p.reset();
    SUCCEED();
}
