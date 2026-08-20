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

// Tests for `IVariable` (cpp/Decompiler/TypeSystem/IVariable.hpp, the D374 port of
// ICSharpCode.Decompiler/TypeSystem/IVariable.cs). `IVariable : ISymbol` is the
// name/type pair for a local variable or parameter, adding `Type` (the variable's
// `IType`), `IsConst`, and `GetConstantValue` (the boxed constant value or parameter
// default value, or null). It is the base of `IParameter` and the local-variable
// hierarchy and a leaf TypeSystem dependency toward `TypeSystemAstBuilder`. The tests
// pin the interface contract (a concrete subclass overriding every accessor,
// polymorphic dispatch through both an `IVariable*` and an `ISymbol*` -- `IVariable`
// IS-A `ISymbol` -- the `std::any` null and boxed-value cases, and the virtual
// destructor) via a test stub. The stub qualifies the inherited `ISymbol::SymbolKind()`
// member's return type because the inherited member function hides the
// namespace-scope `SymbolKind` enum (the D372 cross-scope name-hiding crux).

#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>

namespace {

// A minimal concrete `IVariable` for testing: holds a `SymbolKind`, a `Name`, an
// `IType` (via `ITypePtr`), an `IsConst` flag, and a boxed `GetConstantValue`, and
// returns them from the pure-virtual accessors (the shape a real type-system
// variable -- a `DefaultVariable`, `DefaultParameter`, ... -- will take). The
// `SymbolKind` member/return type is qualified because the inherited
// `ISymbol::SymbolKind()` member function hides the namespace-scope `SymbolKind` enum
// inside this derived class (the D372 cross-scope name-hiding crux).
class TestVariable : public ILSpy::Decompiler::TypeSystem::IVariable {
public:
    TestVariable(ILSpy::Decompiler::TypeSystem::SymbolKind kind, std::string name,
                  ILSpy::Decompiler::TypeSystem::ITypePtr type, bool isConst,
                  std::any constantValue)
        : kind_(kind), name_(std::move(name)), type_(std::move(type)),
          isConst_(isConst), constantValue_(std::move(constantValue)) {}

    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override {
        return constantValue_;
    }

private:
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
    bool isConst_;
    std::any constantValue_;
};

} // namespace

// ---------------------------------------------------------------------------
// IVariable -- every accessor returns the configured value (the shape a real
// type-system variable exposes: its kind, name, type, const-ness, and boxed
// constant value).
// ---------------------------------------------------------------------------
TEST(IVariableTest, AccessorsReturnConfiguredValues)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false,
        std::any());
    EXPECT_EQ(variable.SymbolKind(), TS::SymbolKind::Variable);
    EXPECT_EQ(variable.Name(), "x");
    EXPECT_EQ(&variable.Type(), intType.get());
    EXPECT_EQ(variable.Type().Name(), "Int32");
    EXPECT_FALSE(variable.IsConst());
    EXPECT_FALSE(variable.GetConstantValue().has_value());
}

// ---------------------------------------------------------------------------
// IVariable -- polymorphic dispatch through an `IVariable*` (the dynamic dispatch
// the `TypeSystemAstBuilder` field-initializer / parameter-default paths rely on:
// they hold an `IVariable` and read `Type()`/`GetConstantValue()` through the base).
// ---------------------------------------------------------------------------
TEST(IVariableTest, DispatchesPolymorphicallyThroughIVariablePointer)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto owned = std::make_unique<TestVariable>(
        TS::SymbolKind::Parameter, "name", stringType, /*isConst*/ false,
        std::any(std::string("hello")));
    TS::IVariable* base = owned.get();
    EXPECT_EQ(base->SymbolKind(), TS::SymbolKind::Parameter);
    EXPECT_EQ(base->Name(), "name");
    EXPECT_EQ(base->Type().Name(), "String");
    EXPECT_FALSE(base->IsConst());
    const auto cv = base->GetConstantValue();
    ASSERT_TRUE(cv.has_value());
    ASSERT_EQ(cv.type(), typeid(std::string));
    EXPECT_EQ(std::any_cast<std::string>(cv), "hello");
}

// ---------------------------------------------------------------------------
// IVariable -- polymorphic dispatch through an `ISymbol*` (an `IVariable` IS-A
// `ISymbol`, so the inherited `SymbolKind()`/`Name()` contract dispatches through
// the `ISymbol` base -- the dynamic dispatch `CSharpAmbience.ConvertSymbol(ISymbol)`
// relies on, which receives any symbol including an `IVariable`).
// ---------------------------------------------------------------------------
TEST(IVariableTest, DispatchesPolymorphicallyThroughISymbolPointer)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto owned = std::make_unique<TestVariable>(
        TS::SymbolKind::Field, "count", intType, /*isConst*/ true,
        std::any(std::int32_t(42)));
    TS::ISymbol* symbol = owned.get();
    EXPECT_EQ(symbol->SymbolKind(), TS::SymbolKind::Field);
    EXPECT_EQ(symbol->Name(), "count");
    // destroying `owned` runs the `TestVariable` destructor through the virtual
    // `~ISymbol()` (inherited by `IVariable`).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IVariable.GetConstantValue -- the `std::any` null case: an empty `std::any`
// represents the C# `object?` null (no constant value). The default-constructed
// variable and an explicit `std::any()` both report no value.
// ---------------------------------------------------------------------------
TEST(IVariableTest, GetConstantValueIsNullWhenNoConstant)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "y", intType, /*isConst*/ false,
        std::any());
    EXPECT_FALSE(variable.GetConstantValue().has_value());
    // the throwOnInvalidMetadata argument is accepted but does not change the null result
    EXPECT_FALSE(variable.GetConstantValue(/*throwOnInvalidMetadata*/ true).has_value());
}

// ---------------------------------------------------------------------------
// IVariable.GetConstantValue -- the `std::any` boxed-value cases: the boxed
// constant value is recoverable by `std::any_cast` for each primitive type a
// const field or parameter default may carry (an int, a string, a bool, a double),
// faithful to the C# `object?` holding any boxed literal.
// ---------------------------------------------------------------------------
TEST(IVariableTest, GetConstantValueHoldsBoxedPrimitive)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);

    TestVariable intConst(TS::SymbolKind::Field, "max", intType, /*isConst*/ true,
        std::any(std::int32_t(42)));
    const auto intCv = intConst.GetConstantValue();
    ASSERT_TRUE(intCv.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(intCv), 42);

    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    TestVariable strConst(TS::SymbolKind::Field, "greeting", stringType, /*isConst*/ true,
        std::any(std::string("hi")));
    const auto strCv = strConst.GetConstantValue();
    ASSERT_TRUE(strCv.has_value());
    EXPECT_EQ(std::any_cast<std::string>(strCv), "hi");

    TestVariable boolConst(TS::SymbolKind::Field, "flag", intType, /*isConst*/ true,
        std::any(true));
    const auto boolCv = boolConst.GetConstantValue();
    ASSERT_TRUE(boolCv.has_value());
    EXPECT_EQ(std::any_cast<bool>(boolCv), true);

    TestVariable doubleConst(TS::SymbolKind::Field, "pi", intType, /*isConst*/ true,
        std::any(3.14));
    const auto dblCv = doubleConst.GetConstantValue();
    ASSERT_TRUE(dblCv.has_value());
    EXPECT_DOUBLE_EQ(std::any_cast<double>(dblCv), 3.14);
}

// ---------------------------------------------------------------------------
// IVariable -- has a virtual destructor (a concrete subclass can be deleted
// through an `IVariable*` or an `ISymbol*` and the derived destructor runs),
// the established abstract-base contract.
// ---------------------------------------------------------------------------
TEST(IVariableTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IVariable>,
        "IVariable must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ISymbol>,
        "ISymbol (the base of IVariable) must have a virtual destructor");
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    std::unique_ptr<TS::IVariable> owned = std::make_unique<TestVariable>(
        TS::SymbolKind::Variable, "z", intType, /*isConst*/ false, std::any());
    EXPECT_EQ(owned->Name(), "z");
    owned.reset();
    SUCCEED();
}
