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

// Tests for `ITypeDefinitionOrUnknown` (cpp/Decompiler/TypeSystem/ITypeDefinitionOrUnknown.hpp,
// the D377 port of ICSharpCode.Decompiler/TypeSystem/ITypeDefinitionOrUnknown.cs).
// `ITypeDefinitionOrUnknown : IType` is the base of `ITypeDefinition` (a resolved type
// definition) and the `UnknownType` placeholder: an `IType` that carries a full type name
// (a `TopLevelTypeName` plus zero or more nested-type segments, the reflection-name form
// that uniquely identifies a type definition within an assembly). It is the first piece of
// the `ITypeDefinition` hierarchy toward `IModule`/`IEntity`/`TypeSystemAstBuilder`/
// `CSharpAmbience`. The tests pin the interface contract -- the `FullTypeName` accessor,
// polymorphic dispatch through both an `ITypeDefinitionOrUnknown*` and the inherited `IType*`
// base, the nested-type name shape, and the virtual destructor -- via a test stub that
// derives from the new base and overrides every pure-virtual.

#include "Decompiler/TypeSystem/ITypeDefinitionOrUnknown.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace {

// A minimal concrete `ITypeDefinitionOrUnknown` for testing: holds a `FullTypeName`
// (the reflection-name form parsed from a string) and a `TypeKind`, and derives the
// inherited `IType` accessors (`Name`/`ReflectionName`/`TypeParameterCount`) from it --
// the shape a real type-definition-or-unknown (`MetadataTypeDefinition`, the
// `UnknownType` placeholder) will take. The `FullTypeName` accessor returns a const
// reference to the stored member (the `SimpleType::GetTopLevelTypeName()` precedent).
class TestTypeDefinitionOrUnknown : public ILSpy::Decompiler::TypeSystem::ITypeDefinitionOrUnknown {
public:
    explicit TestTypeDefinitionOrUnknown(ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName,
        ILSpy::Decompiler::TypeSystem::TypeKind kind = ILSpy::Decompiler::TypeSystem::TypeKind::Class)
        : fullTypeName_(std::move(fullTypeName)), kind_(kind) {}

    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override { return kind_; }
    std::string Name() const override { return fullTypeName_.Name(); }
    std::string ReflectionName() const override { return fullTypeName_.ReflectionName(); }
    int TypeParameterCount() const override { return fullTypeName_.TypeParameterCount(); }
    const ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override { return fullTypeName_; }

protected:
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType& other) const override {
        const auto& o = static_cast<const TestTypeDefinitionOrUnknown&>(other);
        return kind_ == o.kind_ && fullTypeName_ == o.fullTypeName_;
    }

private:
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    ILSpy::Decompiler::TypeSystem::TypeKind kind_;
};

} // namespace

// ---------------------------------------------------------------------------
// ITypeDefinitionOrUnknown -- the FullTypeName accessor returns the configured
// value, and the inherited IType accessors (Name/ReflectionName/TypeParameterCount)
// derive from it (a top-level type "System.Collections.Generic.Dictionary`2" has
// Name "Dictionary`2", ReflectionName "System.Collections.Generic.Dictionary`2",
// and a two-argument generic arity).
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionOrUnknownTest, AccessorsReturnConfiguredValues)
{
    TestTypeDefinitionOrUnknown typeDef(
        ILSpy::Decompiler::TypeSystem::FullTypeName("System.Collections.Generic.Dictionary`2"),
        ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(typeDef.FullTypeName().ReflectionName(), "System.Collections.Generic.Dictionary`2");
    // Name() is the short name WITHOUT the arity backtick suffix (the IType::Name
    // convention: "List" not "List`1"); the arity lives only in ReflectionName().
    EXPECT_EQ(typeDef.Name(), "Dictionary");
    EXPECT_EQ(typeDef.ReflectionName(), "System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(typeDef.TypeParameterCount(), 2);
    EXPECT_EQ(typeDef.Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
}

// ---------------------------------------------------------------------------
// ITypeDefinitionOrUnknown -- polymorphic dispatch through an
// ITypeDefinitionOrUnknown* (the dynamic dispatch the type-system paths rely on:
// they hold an ITypeDefinitionOrUnknown and read FullTypeName() through the base).
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionOrUnknownTest, DispatchesPolymorphicallyThroughBasePointer)
{
    auto owned = std::make_unique<TestTypeDefinitionOrUnknown>(
        ILSpy::Decompiler::TypeSystem::FullTypeName("System.Int32"),
        ILSpy::Decompiler::TypeSystem::TypeKind::Struct);
    ILSpy::Decompiler::TypeSystem::ITypeDefinitionOrUnknown* base = owned.get();
    EXPECT_EQ(base->FullTypeName().ReflectionName(), "System.Int32");
    EXPECT_EQ(base->Name(), "Int32");
    EXPECT_EQ(base->Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Struct);
}

// ---------------------------------------------------------------------------
// ITypeDefinitionOrUnknown -- polymorphic dispatch through the inherited IType*
// base (ITypeDefinitionOrUnknown IS-A IType, so an IType* to a type definition
// dispatches FullTypeName() -- the IType::GetDefinitionOrUnknown() consumer shape,
// which holds an IType and may downcast to reach the full name).
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionOrUnknownTest, DispatchesPolymorphicallyThroughITypePointer)
{
    auto owned = std::make_unique<TestTypeDefinitionOrUnknown>(
        ILSpy::Decompiler::TypeSystem::FullTypeName("System.String"),
        ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    ILSpy::Decompiler::TypeSystem::IType* typeBase = owned.get();
    EXPECT_EQ(typeBase->Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(typeBase->Name(), "String");
    EXPECT_EQ(typeBase->ReflectionName(), "System.String");
    // The IType* can be downcast to ITypeDefinitionOrUnknown to reach FullTypeName.
    auto* defOrUnknown = dynamic_cast<ILSpy::Decompiler::TypeSystem::ITypeDefinitionOrUnknown*>(typeBase);
    ASSERT_NE(defOrUnknown, nullptr);
    EXPECT_EQ(defOrUnknown->FullTypeName().ReflectionName(), "System.String");
}

// ---------------------------------------------------------------------------
// ITypeDefinitionOrUnknown -- a nested type's FullTypeName carries the '+'
// segments (e.g. "System.Collections.Generic.Dictionary`2+Enumerator"), and the
// Name accessor returns the innermost segment ("Enumerator"), not the top-level
// name. This pins the nested-type shape the ITypeDefinition hierarchy exposes.
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionOrUnknownTest, NestedTypeFullTypeName)
{
    TestTypeDefinitionOrUnknown nested(
        ILSpy::Decompiler::TypeSystem::FullTypeName("System.Collections.Generic.Dictionary`2+Enumerator"),
        ILSpy::Decompiler::TypeSystem::TypeKind::Struct);
    EXPECT_TRUE(nested.FullTypeName().IsNested());
    EXPECT_EQ(nested.FullTypeName().NestingLevel(), 1);
    EXPECT_EQ(nested.Name(), "Enumerator");
    EXPECT_EQ(nested.FullTypeName().ReflectionName(), "System.Collections.Generic.Dictionary`2+Enumerator");
}

// ---------------------------------------------------------------------------
// ITypeDefinitionOrUnknown -- has a virtual destructor (a concrete subclass can
// be deleted through an ITypeDefinitionOrUnknown* and the derived destructor runs),
// the established abstract-base contract inherited from IType.
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionOrUnknownTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ITypeDefinitionOrUnknown>,
        "ITypeDefinitionOrUnknown must have a virtual destructor for abstract-base deletion");
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeDefinitionOrUnknown> owned =
        std::make_unique<TestTypeDefinitionOrUnknown>(
            ILSpy::Decompiler::TypeSystem::FullTypeName("System.IO.File"),
            ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(owned->Name(), "File");
    // destroying `owned` runs the TestTypeDefinitionOrUnknown destructor through the
    // virtual ~ITypeDefinitionOrUnknown() (inherited from ~IType()).
    owned.reset();
    SUCCEED();
}
