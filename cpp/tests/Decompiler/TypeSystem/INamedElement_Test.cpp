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

// Tests for `INamedElement` (cpp/Decompiler/TypeSystem/INamedElement.hpp, the D375
// port of ICSharpCode.Decompiler/TypeSystem/INamedElement.cs). `INamedElement` is the
// name-bearing base of `IType` and `IEntity`, exposing the four name strings every
// named type-system element carries: the fully qualified `FullName`, the short
// `Name`, the round-trippable `ReflectionName`, and the containing `Namespace`. It
// is a leaf TypeSystem dependency toward `IEntity` and `TypeSystemAstBuilder`. The
// tests pin the interface contract (a concrete subclass overriding every accessor,
// polymorphic dispatch through an `INamedElement*`, the empty-string cases a
// top-level element carries, the dotted full-name/namespace shape, and the virtual
// destructor) via a test stub.

#include "Decompiler/TypeSystem/INamedElement.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

// A minimal concrete `INamedElement` for testing: holds the four name strings and
// returns them from the pure-virtual accessors (the shape a real named element -- a
// `MetadataTypeDefinition`, `MetadataNamespace`, ... -- will take). None of the
// accessor names (`FullName`/`Name`/`ReflectionName`/`Namespace`) collide with a
// namespace-scope type name in the `TypeSystem` namespace, so no name-hiding
// qualification is needed (unlike the `ISymbol`/`IVariable` `SymbolKind` cases).
class TestNamedElement : public ILSpy::Decompiler::TypeSystem::INamedElement {
public:
    TestNamedElement(std::string fullName, std::string name,
                     std::string reflectionName, std::string namespaceName)
        : fullName_(std::move(fullName)), name_(std::move(name)),
          reflectionName_(std::move(reflectionName)),
          namespace_(std::move(namespaceName)) {}

    std::string FullName() const override { return fullName_; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return reflectionName_; }
    std::string Namespace() const override { return namespace_; }

private:
    std::string fullName_;
    std::string name_;
    std::string reflectionName_;
    std::string namespace_;
};

} // namespace

// ---------------------------------------------------------------------------
// INamedElement -- the four accessors return the configured values (the shape a
// real named element exposes: its full name, short name, reflection name, and
// containing namespace).
// ---------------------------------------------------------------------------
TEST(INamedElementTest, AccessorsReturnConfiguredValues)
{
    TestNamedElement element("System.Environment", "Environment",
        "System.Environment", "System");
    EXPECT_EQ(element.FullName(), "System.Environment");
    EXPECT_EQ(element.Name(), "Environment");
    EXPECT_EQ(element.ReflectionName(), "System.Environment");
    EXPECT_EQ(element.Namespace(), "System");
}

// ---------------------------------------------------------------------------
// INamedElement -- polymorphic dispatch through an `INamedElement*` (the dynamic
// dispatch the `TypeSystemAstBuilder` type- and member-printing paths rely on:
// they hold an `INamedElement` and read `FullName()`/`Namespace()` through the base).
// ---------------------------------------------------------------------------
TEST(INamedElementTest, DispatchesPolymorphicallyThroughBasePointer)
{
    auto owned = std::make_unique<TestNamedElement>(
        "System.Collections.Generic.List`1", "List",
        "System.Collections.Generic.List`1[[System.String]]",
        "System.Collections.Generic");
    ILSpy::Decompiler::TypeSystem::INamedElement* base = owned.get();
    EXPECT_EQ(base->FullName(), "System.Collections.Generic.List`1");
    EXPECT_EQ(base->Name(), "List");
    EXPECT_EQ(base->ReflectionName(), "System.Collections.Generic.List`1[[System.String]]");
    EXPECT_EQ(base->Namespace(), "System.Collections.Generic");
}

// ---------------------------------------------------------------------------
// INamedElement -- the empty-string cases: a top-level element (a type in the
// global namespace) carries an empty `Namespace` and an empty `FullName` prefix,
// and a reflection name may equal the full name when there are no type arguments.
// The interface imposes no non-empty contract on any of the four strings.
// ---------------------------------------------------------------------------
TEST(INamedElementTest, EmptyStringsAreAllowed)
{
    TestNamedElement global("Program", "Program", "Program", "");
    EXPECT_EQ(global.FullName(), "Program");
    EXPECT_EQ(global.Name(), "Program");
    EXPECT_EQ(global.ReflectionName(), "Program");
    EXPECT_EQ(global.Namespace(), "");
}

// ---------------------------------------------------------------------------
// INamedElement -- the dotted full-name / namespace shape: a nested type's
// `FullName` is `<namespace>.<outer>.<inner>` and its `Namespace` is just the
// `<namespace>` (not the outer type), matching the C# `INamedElement` doc comments
// (e.g. "System.Environment.SpecialFolder" / "SpecialFolder" / "System").
// ---------------------------------------------------------------------------
TEST(INamedElementTest, DottedFullNameAndNamespaceShape)
{
    TestNamedElement nested("System.Environment.SpecialFolder", "SpecialFolder",
        "System.Environment+SpecialFolder", "System");
    EXPECT_EQ(nested.FullName(), "System.Environment.SpecialFolder");
    EXPECT_EQ(nested.Name(), "SpecialFolder");
    EXPECT_EQ(nested.ReflectionName(), "System.Environment+SpecialFolder");
    EXPECT_EQ(nested.Namespace(), "System");
}

// ---------------------------------------------------------------------------
// INamedElement -- has a virtual destructor (a concrete subclass can be deleted
// through an `INamedElement*` and the derived destructor runs), the established
// abstract-base contract.
// ---------------------------------------------------------------------------
TEST(INamedElementTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::INamedElement>,
        "INamedElement must have a virtual destructor for abstract-base deletion");
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::INamedElement> owned =
        std::make_unique<TestNamedElement>("System.IO.File", "File",
            "System.IO.File", "System.IO");
    EXPECT_EQ(owned->Name(), "File");
    // destroying `owned` runs the `TestNamedElement` destructor through the virtual
    // `~INamedElement()`.
    owned.reset();
    SUCCEED();
}
