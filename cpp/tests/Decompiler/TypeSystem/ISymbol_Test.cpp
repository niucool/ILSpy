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

// Tests for `ISymbol` (cpp/Decompiler/TypeSystem/ISymbol.hpp, the D372 port of
// ICSharpCode.Decompiler/TypeSystem/ISymbol.cs -- the `ISymbol` interface half; the
// `SymbolKind` enum half is already in `SymbolKind.hpp`). `ISymbol` is the root interface
// every type-system symbol implements, exposing `SymbolKind()` (which kind) and `Name()`
// (the short name); it is the leaf TypeSystem dependency of `CSharpAmbience.ConvertSymbol`.
// The tests pin the interface contract (a concrete subclass overriding both accessors, the
// polymorphic dispatch through an `ISymbol*`, and each `SymbolKind` value) via a test stub.

#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

// A minimal concrete `ISymbol` for testing: holds a `SymbolKind` and a `Name` and returns
// them from the two pure-virtual accessors (the shape a real type-system symbol -- a
// `MetadataField`, `MetadataMethod`, `MetadataNamespace`, ... -- will take).
class TestSymbol : public ILSpy::Decompiler::TypeSystem::ISymbol {
public:
    TestSymbol(ILSpy::Decompiler::TypeSystem::SymbolKind kind, std::string name)
        : kind_(kind), name_(std::move(name)) {}
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }
private:
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    std::string name_;
};

} // namespace

// ---------------------------------------------------------------------------
// ISymbol -- the two accessors return the configured values.
// ---------------------------------------------------------------------------
TEST(ISymbolTest, AccessorsReturnConfiguredValues)
{
    TestSymbol symbol(ILSpy::Decompiler::TypeSystem::SymbolKind::Field, "count");
    EXPECT_EQ(symbol.SymbolKind(), ILSpy::Decompiler::TypeSystem::SymbolKind::Field);
    EXPECT_EQ(symbol.Name(), "count");
}

// ---------------------------------------------------------------------------
// ISymbol -- polymorphic dispatch through an ISymbol* (the dynamic dispatch the
// `CSharpAmbience.ConvertSymbol(ISymbol)` and its `symbol.SymbolKind` switch rely on).
// ---------------------------------------------------------------------------
TEST(ISymbolTest, DispatchesPolymorphicallyThroughBasePointer)
{
    auto owned = std::make_unique<TestSymbol>(
        ILSpy::Decompiler::TypeSystem::SymbolKind::Method, "ToString");
    ILSpy::Decompiler::TypeSystem::ISymbol* base = owned.get();
    EXPECT_EQ(base->SymbolKind(), ILSpy::Decompiler::TypeSystem::SymbolKind::Method);
    EXPECT_EQ(base->Name(), "ToString");
}

// ---------------------------------------------------------------------------
// ISymbol -- each SymbolKind value is a distinct value (the C# `: byte` enum ports to
// `std::uint8_t` with the declaration-index values; `ISymbol.SymbolKind` returns one of
// these, so the interface contract covers every kind a real symbol may report).
// ---------------------------------------------------------------------------
TEST(ISymbolTest, EachSymbolKindIsDistinct)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    TestSymbol module(TS::SymbolKind::Module, "MyAssembly");
    TestSymbol typeDef(TS::SymbolKind::TypeDefinition, "Foo");
    TestSymbol field(TS::SymbolKind::Field, "count");
    TestSymbol property(TS::SymbolKind::Property, "Count");
    TestSymbol indexer(TS::SymbolKind::Indexer, "Item");
    TestSymbol event_(TS::SymbolKind::Event, "Clicked");
    TestSymbol method(TS::SymbolKind::Method, "Bar");
    TestSymbol op(TS::SymbolKind::Operator, "op_Addition");
    TestSymbol ctor(TS::SymbolKind::Constructor, ".ctor");
    TestSymbol dtor(TS::SymbolKind::Destructor, "Finalize");
    TestSymbol accessor(TS::SymbolKind::Accessor, "get_Count");
    TestSymbol ns(TS::SymbolKind::Namespace, "System");
    TestSymbol variable(TS::SymbolKind::Variable, "x");
    TestSymbol parameter(TS::SymbolKind::Parameter, "value");
    TestSymbol typeParam(TS::SymbolKind::TypeParameter, "T");
    TestSymbol constraint(TS::SymbolKind::Constraint, "where");
    TestSymbol retType(TS::SymbolKind::ReturnType, "ReturnType");
    EXPECT_EQ(module.SymbolKind(), TS::SymbolKind::Module);
    EXPECT_EQ(typeDef.SymbolKind(), TS::SymbolKind::TypeDefinition);
    EXPECT_EQ(field.SymbolKind(), TS::SymbolKind::Field);
    EXPECT_EQ(property.SymbolKind(), TS::SymbolKind::Property);
    EXPECT_EQ(indexer.SymbolKind(), TS::SymbolKind::Indexer);
    EXPECT_EQ(event_.SymbolKind(), TS::SymbolKind::Event);
    EXPECT_EQ(method.SymbolKind(), TS::SymbolKind::Method);
    EXPECT_EQ(op.SymbolKind(), TS::SymbolKind::Operator);
    EXPECT_EQ(ctor.SymbolKind(), TS::SymbolKind::Constructor);
    EXPECT_EQ(dtor.SymbolKind(), TS::SymbolKind::Destructor);
    EXPECT_EQ(accessor.SymbolKind(), TS::SymbolKind::Accessor);
    EXPECT_EQ(ns.SymbolKind(), TS::SymbolKind::Namespace);
    EXPECT_EQ(variable.SymbolKind(), TS::SymbolKind::Variable);
    EXPECT_EQ(parameter.SymbolKind(), TS::SymbolKind::Parameter);
    EXPECT_EQ(typeParam.SymbolKind(), TS::SymbolKind::TypeParameter);
    EXPECT_EQ(constraint.SymbolKind(), TS::SymbolKind::Constraint);
    EXPECT_EQ(retType.SymbolKind(), TS::SymbolKind::ReturnType);
}

// ---------------------------------------------------------------------------
// ISymbol -- has a virtual destructor (a concrete subclass can be deleted through an
// `ISymbol*` and the derived destructor runs), the established abstract-base contract.
// ---------------------------------------------------------------------------
TEST(ISymbolTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ISymbol>,
        "ISymbol must have a virtual destructor for abstract-base deletion");
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ISymbol> owned = std::make_unique<TestSymbol>(
        ILSpy::Decompiler::TypeSystem::SymbolKind::Event, "Changed");
    EXPECT_EQ(owned->Name(), "Changed");
    // destroying `owned` runs the `TestSymbol` destructor through the virtual `~ISymbol()`
    owned.reset();
    SUCCEED();
}
