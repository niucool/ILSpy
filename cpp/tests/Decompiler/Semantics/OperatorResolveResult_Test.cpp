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

// Tests for `OperatorResolveResult` (cpp/Decompiler/Semantics/OperatorResolveResult.hpp,
// the D449 port of ICSharpCode.Decompiler/Semantics/OperatorResolveResult.cs) -- the
// result of a unary / binary / ternary operator invocation. Derives directly from
// `ResolveResult` (D424) and carries the `OperatorType` (the BCL `ExpressionType` enum,
// D448), the nullable `UserDefinedOperatorMethod` (`IMethod`), the `Operands`
// (`ResolveResult` list), and the `IsLiftedOperator` flag; overrides `GetChildResults` to
// return the `Operands` directly.
//
// The tests pin the ctor-1-stores-all-fields contract (incl. the nullable-method and
// lifted-flag defaults), the ctor-2-stores-all-fields contract, the four accessors, the
// `GetChildResults`-returns-operands crux (in order, incl. the empty-operands variant),
// the `ToString` subclass-class-name format, the `ShallowClone` runtime-type preservation
// plus shared-ownership-of-operands plus pointer-copy-of-the-method plus value-copy-of-
// the-flag, virtual dispatch through the base pointer, the inherited `ResolveResult`
// defaults, and the `is_base_of` / `has_virtual_destructor` / `is_polymorphic` /
// not-`final` static-asserts.

#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Convenience: a `KnownType(Int32)` -- the operator result type forwarded to the base
// (`ResolveResult::Type()`; its `ReflectionName()` is "System.Int32").
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: a `KnownType(Boolean)` -- a distinct type for a second operand.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeBooleanType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Boolean);
}

// Convenience: a `KnownType(Object)` -- a distinct type for a ternary operand.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// Convenience: build a two-element operand list (a `TypeResolveResult` over
// `KnownType(Int32)` then one over `KnownType(Boolean)`) -- the shape of a binary
// operator `a + b`.
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> MakeTwoOperands()
{
    return {
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeInt32Type()),
        std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(MakeBooleanType())
    };
}

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// of the `IMethod` stub can return a compilation (the D399 test stand-in pattern, identical
// in shape to the `TestCompilation` in `ForEachResolveResult_Test.cpp`).
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

// A minimal concrete `IMethod` for testing the non-null `UserDefinedOperatorMethod` case:
// holds a `TestCompilation` reference (for the inherited `ICompilationProvider::Compilation`)
// and a return type (for the inherited `IMember::ReturnType`), and returns trivial defaults
// for every other accessor. Only pointer-identity (the address) is exercised by the
// `OperatorResolveResult` tests, so the field values are not configured.
class TestOperatorMethod : public ILSpy::Decompiler::TypeSystem::IMethod {
public:
    explicit TestOperatorMethod(const TestCompilation& compilation)
        : compilation_(compilation),
          returnType_(std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
              ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32)) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    std::string Name() const override { return "op_Addition"; }

    // --- INamedElement ---
    std::string FullName() const override { return "op_Addition"; }
    std::string ReflectionName() const override { return "op_Addition"; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return true; }
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
    // The covariant `IMethod::Specialize` override (the D389 convention).
    const ILSpy::Decompiler::TypeSystem::IMethod* Specialize(const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override { return {}; }

    // --- IMethod ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetReturnTypeAttributes() const override { return {}; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> TypeParameters() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return true; }
    bool HasBody() const override { return false; }
    bool IsAccessor() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IMember* AccessorOwner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind() const override {
        return ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::None;
    }
    const ILSpy::Decompiler::TypeSystem::IMethod* ReducedFrom() const override { return nullptr; }

private:
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
};

} // namespace

// ---------------------------------------------------------------------------
// Ctor 1 (the common ctor for a predefined operator): forwards the result type to the
// base `ResolveResult` and stores the `operatorType` and the `operands`. `Type()` is the
// result type (`KnownType(Int32)`).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, Ctor1StoresResultTypeOperatorTypeAndOperands)
{
    auto resultType = MakeInt32Type();
    auto* resultTypePtr = resultType.get();
    auto operands = MakeTwoOperands();
    auto* operand0 = operands[0].get();
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        resultType,
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        std::move(operands));
    EXPECT_EQ(&rr.Type(), resultTypePtr);
    EXPECT_EQ(rr.OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Add);
    ASSERT_EQ(rr.Operands().size(), 2u);
    EXPECT_EQ(rr.Operands()[0].get(), operand0);
}

// ---------------------------------------------------------------------------
// Ctor 1 defaults `UserDefinedOperatorMethod` to `nullptr` (a predefined operator has no
// user-defined method) and `IsLiftedOperator` to `false` (the C# field defaults via the
// ctor-1 omission).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, Ctor1DefaultsUserDefinedOperatorMethodToNullAndIsLiftedOperatorToFalse)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Subtract,
        MakeTwoOperands());
    EXPECT_EQ(rr.UserDefinedOperatorMethod(), nullptr);
    EXPECT_FALSE(rr.IsLiftedOperator());
}

// ---------------------------------------------------------------------------
// Ctor 1 accepts an empty operand list (a unary operator has 1 operand, but the ctor
// accepts any count; the faithful equivalent of a non-null empty `ResolveResult[]`). The
// C# `operands == null` `ArgumentNullException` guard has no C++ counterpart (a
// `std::vector` is never null, the D441 precedent).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, Ctor1AcceptsEmptyOperands)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Negate,
        /*operands*/ {});
    EXPECT_EQ(rr.Operands().size(), 0u);
}

// ---------------------------------------------------------------------------
// Ctor 2 (the full ctor for a user-defined / lifted operator): stores all four fields --
// the `operatorType`, the `userDefinedOperatorMethod`, the `isLiftedOperator`, and the
// `operands`. The non-null `UserDefinedOperatorMethod` is pointer-identical to the passed
// method.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, Ctor2StoresAllFields)
{
    TestCompilation compilation(1);
    auto method = std::make_unique<TestOperatorMethod>(compilation);
    const auto* methodPtr = method.get();
    auto operands = MakeTwoOperands();
    auto* operand0 = operands[0].get();
    auto* operand1 = operands[1].get();
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        methodPtr,
        /*isLiftedOperator*/ true,
        std::move(operands));
    EXPECT_EQ(rr.OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Add);
    EXPECT_EQ(rr.UserDefinedOperatorMethod(), methodPtr);
    EXPECT_TRUE(rr.IsLiftedOperator());
    ASSERT_EQ(rr.Operands().size(), 2u);
    EXPECT_EQ(rr.Operands()[0].get(), operand0);
    EXPECT_EQ(rr.Operands()[1].get(), operand1);
}

// ---------------------------------------------------------------------------
// Ctor 2 accepts a null `UserDefinedOperatorMethod` (a predefined operator passed through
// the full ctor with `isLiftedOperator` set) and a null `operands` is not expressible (a
// `std::vector` is never null).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, Ctor2AcceptsNullUserDefinedOperatorMethod)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Multiply,
        /*userDefinedOperatorMethod*/ nullptr,
        /*isLiftedOperator*/ true,
        MakeTwoOperands());
    EXPECT_EQ(rr.UserDefinedOperatorMethod(), nullptr);
    EXPECT_TRUE(rr.IsLiftedOperator());
}

// ---------------------------------------------------------------------------
// The `OperatorType()` accessor returns the stored `ExpressionType` value across the BCL
// enum's representative spread (arithmetic / comparison / compound-assignment).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, OperatorTypeAccessorReturnsStoredValue)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rrAdd(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        MakeTwoOperands());
    EXPECT_EQ(rrAdd.OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Add);

    ILSpy::Decompiler::Semantics::OperatorResolveResult rrGt(
        MakeBooleanType(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::GreaterThan,
        MakeTwoOperands());
    EXPECT_EQ(rrGt.OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::GreaterThan);

    ILSpy::Decompiler::Semantics::OperatorResolveResult rrAddAssign(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::AddAssign,
        MakeTwoOperands());
    EXPECT_EQ(rrAddAssign.OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::AddAssign);
}

// ---------------------------------------------------------------------------
// The `Operands()` accessor returns the stored shared_ptr vector by reference; the
// elements are pointer-identical to the originals.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, OperandsAccessorReturnsStoredOperands)
{
    auto operands = MakeTwoOperands();
    auto* operand0 = operands[0].get();
    auto* operand1 = operands[1].get();
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        std::move(operands));
    ASSERT_EQ(rr.Operands().size(), 2u);
    EXPECT_EQ(rr.Operands()[0].get(), operand0);
    EXPECT_EQ(rr.Operands()[1].get(), operand1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` crux: returns the `Operands` directly, in order. A binary operator
// with two operands yields 2 children in order: operand0, operand1.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, GetChildResultsReturnsOperandsInOrder)
{
    auto operands = MakeTwoOperands();
    auto* operand0 = operands[0].get();
    auto* operand1 = operands[1].get();
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        std::move(operands));
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], operand0);
    EXPECT_EQ(children[1], operand1);
}

// ---------------------------------------------------------------------------
// `GetChildResults` with an empty operand list yields an empty snapshot (no operands to
// return). A unary operator constructed through ctor 1 with an empty operand list still
// yields 0 children.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, GetChildResultsEmptyWhenNoOperands)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Negate,
        /*operands*/ {});
    auto children = rr.GetChildResults();
    EXPECT_EQ(children.size(), 0u);
}

// ---------------------------------------------------------------------------
// `ToString` reports the subclass class name and the result type (the inherited
// `ResolveResult::ToString` uses the polymorphic `ClassName()` which the override returns
// "OperatorResolveResult"; the result type is `KnownType(Int32)` whose `ReflectionName()`
// is "System.Int32" (the `Namespace.Name` form)).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, ToStringReportsSubclassClassNameAndResultType)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        MakeTwoOperands());
    EXPECT_EQ(rr.ToString(), "[OperatorResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (the clone is an `OperatorResolveResult`, not
// the `ResolveResult` base a non-overriding clone would slice to). The clone's `ToString()`
// reports "OperatorResolveResult" (the virtual dispatch through the clone).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, ShallowClonePreservesRuntimeType)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        MakeTwoOperands());
    auto clone = rr.ShallowClone();
    EXPECT_EQ(clone->ToString(), "[OperatorResolveResult System.Int32]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` shares the `Operands` (the default copy ctor copies the `operands_`
// shared_ptr vector element-wise faithfully mirroring the C# `MemberwiseClone`
// reference-copy). The clone's operand elements are pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, ShallowCloneSharesOperands)
{
    auto operands = MakeTwoOperands();
    auto* operand0 = operands[0].get();
    TestCompilation compilation(1);
    auto method = std::make_unique<TestOperatorMethod>(compilation);
    const auto* methodPtr = method.get();
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        methodPtr,
        /*isLiftedOperator*/ true,
        std::move(operands));
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::OperatorResolveResult*>(
        clone.get());
    ASSERT_EQ(derived->Operands().size(), 2u);
    EXPECT_EQ(derived->Operands()[0].get(), operand0);
}

// ---------------------------------------------------------------------------
// `ShallowClone` copies the `UserDefinedOperatorMethod` raw pointer (the default copy ctor
// copies the non-owning raw pointer) and the `OperatorType` value and the `IsLiftedOperator`
// flag. The clone's method is pointer-identical to the original's.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, ShallowCloneCopiesMethodAndOperatorTypeAndLiftedFlag)
{
    TestCompilation compilation(1);
    auto method = std::make_unique<TestOperatorMethod>(compilation);
    const auto* methodPtr = method.get();
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        methodPtr,
        /*isLiftedOperator*/ true,
        MakeTwoOperands());
    auto clone = rr.ShallowClone();
    auto* derived = static_cast<const ILSpy::Decompiler::Semantics::OperatorResolveResult*>(
        clone.get());
    EXPECT_EQ(derived->UserDefinedOperatorMethod(), methodPtr);
    EXPECT_EQ(derived->OperatorType(), ILSpy::Decompiler::TypeSystem::ExpressionType::Add);
    EXPECT_TRUE(derived->IsLiftedOperator());
}

// ---------------------------------------------------------------------------
// `ShallowClone` is a distinct instance (the clone is a separate object, not the
// original). The `Type()` (the base `type_` shared_ptr) is shared but the
// `OperatorResolveResult` objects are distinct.
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto rr = std::make_shared<ILSpy::Decompiler::Semantics::OperatorResolveResult>(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        MakeTwoOperands());
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through the `ResolveResult*` base pointer: the `GetChildResults`
// override is dispatched through the base pointer (the C# resolver reaches the
// `OperatorResolveResult` overrides through a `ResolveResult` reference).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, VirtualDispatchThroughBasePointer)
{
    auto operands = MakeTwoOperands();
    auto* operand0 = operands[0].get();
    auto* operand1 = operands[1].get();
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr =
        std::make_unique<ILSpy::Decompiler::Semantics::OperatorResolveResult>(
            MakeInt32Type(),
            ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
            std::move(operands));
    auto children = rr->GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], operand0);
    EXPECT_EQ(children[1], operand1);
}

// ---------------------------------------------------------------------------
// The inherited `ResolveResult` defaults are preserved: an operator is not a compile-time
// constant, its `ConstantValue` is empty, and it is not an error (the base defaults).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, InheritedResolveResultDefaultsArePreserved)
{
    ILSpy::Decompiler::Semantics::OperatorResolveResult rr(
        MakeInt32Type(),
        ILSpy::Decompiler::TypeSystem::ExpressionType::Add,
        MakeTwoOperands());
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: `OperatorResolveResult` IS-A `ResolveResult`, is polymorphic with a
// virtual destructor, and is NOT `final` (the C# class is unsealed).
// ---------------------------------------------------------------------------
TEST(OperatorResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<
        ILSpy::Decompiler::Semantics::ResolveResult,
        ILSpy::Decompiler::Semantics::OperatorResolveResult>);
    static_assert(std::has_virtual_destructor_v<
        ILSpy::Decompiler::Semantics::OperatorResolveResult>);
    static_assert(std::is_polymorphic_v<
        ILSpy::Decompiler::Semantics::OperatorResolveResult>);
    static_assert(!std::is_final_v<
        ILSpy::Decompiler::Semantics::OperatorResolveResult>);
}
