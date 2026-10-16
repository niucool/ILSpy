// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `LocalResolveResult` (cpp/Decompiler/Semantics/LocalResolveResult.hpp,
// the D444 port of ICSharpCode.Decompiler/Semantics/LocalResolveResult.cs) -- the
// resolved expression is a local variable or parameter. The C# source declares a
// forwarding ctor whose base type is `UnpackTypeIfByRefParameter(variable)` (the
// variable's type, unwrapped one level when the variable is a non-`None`-ref
// `IParameter` of `ByReference` type), a `Variable` field, an `IsParameter`
// accessor (`variable is IParameter`), and `IsCompileTimeConstant` /
// `ConstantValue` / `ToString` overrides.
//
// The tests pin the ctor-stores-variable + compute-type contract (the local case
// forwards the variable's type as-is), the `UnpackTypeIfByRefParameter` crux
// (unwrap for a non-`None`-ref `IParameter` of `ByReference` type; do NOT unwrap
// for a `None`-ref parameter or a non-parameter local of `ByReference` type), the
// `IsParameter` discrimination, the `IsCompileTimeConstant`/`ConstantValue`
// overrides (the constant-value null-for-parameter short-circuit), the `ToString`
// format, the `ShallowClone` runtime-type preservation, the virtual dispatch
// through a base pointer, the inherited defaults (the subclass does NOT override
// `IsError` or `GetChildResults`), and the `is_base_of` /
// `has_virtual_destructor` / `is_polymorphic` / not-`final` static-asserts.

#include "Decompiler/Semantics/LocalResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ILSpy::Decompiler::Semantics;

namespace {

// A minimal concrete `IVariable` for testing (a local variable, NOT a parameter):
// holds a `SymbolKind`, a `Name`, an `IType` (via `ITypePtr`), an `IsConst` flag, and
// a boxed `GetConstantValue`, and returns them from the pure-virtual accessors (the
// shape a real type-system local variable -- a `DefaultVariable` -- takes). The
// `SymbolKind` member/return type is qualified because the inherited
// `ISymbol::SymbolKind()` member function hides the namespace-scope `SymbolKind`
// enum inside this derived class (the D372 cross-scope name-hiding crux). The stub
// is identical in shape to the `TestVariable` in `IVariable_Test.cpp`.
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

// A minimal concrete `IParameter` for testing: holds the configured name, type,
// const-ness, boxed constant value, and reference kind, and returns them from every
// `IParameter` / `IVariable` / `ISymbol` accessor. The other `IParameter`-own
// accessors (`GetAttributes` / `Lifetime` / `IsParams` / `IsOptional` /
// `HasConstantValueInSignature` / `Owner`) return trivial defaults (they are not
// read by `LocalResolveResult`). The `SymbolKind` member/return type is qualified
// because the inherited `ISymbol::SymbolKind()` member function hides the
// namespace-scope `SymbolKind` enum (the D372 cross-scope name-hiding crux). The
// stub is a simplified form of the `TestParameter` in `IParameter_Test.cpp`.
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(std::string name, ILSpy::Decompiler::TypeSystem::ITypePtr type,
                  bool isConst, std::any constantValue,
                  ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind)
        : name_(std::move(name)), type_(std::move(type)),
          isConst_(isConst), constantValue_(std::move(constantValue)),
          referenceKind_(referenceKind) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }
    std::string Name() const override { return name_; }

    // --- IVariable ---
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override {
        return constantValue_;
    }

    // --- IParameter ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override {
        return {};
    }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override {
        return referenceKind_;
    }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override {
        return {};
    }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override {
        return nullptr;
    }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
    bool isConst_;
    std::any constantValue_;
    ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
};

} // namespace

// ---------------------------------------------------------------------------
// The ctor stores the variable and computes the result type as the variable's
// type (the local case, no `ByReference` unwrap): `Type()` is the variable's
// `IType` (identity), and `Variable()` returns the stored pointer.
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, CtorStoresVariableAndComputesType)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false, std::any());
    LocalResolveResult rr(&variable);
    EXPECT_EQ(rr.Variable(), &variable);
    EXPECT_EQ(&rr.Type(), intType.get());
    EXPECT_EQ(rr.Type().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// The `UnpackTypeIfByRefParameter` crux: a non-`None`-ref `IParameter` of
// `ByReference` type unwraps one ref level -- `Type()` is the element type (NOT
// the `ByReferenceType`).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, CtorUnwrapsByReferenceTypeForRefParameter)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto refType = std::make_shared<TS::ByReferenceType>(intType);
    TestParameter param("p", refType, /*isConst*/ false, std::any(), TS::ReferenceKind::Ref);
    LocalResolveResult rr(&param);
    EXPECT_EQ(&rr.Type(), intType.get());
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::Struct);
}

// ---------------------------------------------------------------------------
// The `UnpackTypeIfByRefParameter` crux: a `None`-ref `IParameter` of
// `ByReference` type does NOT unwrap (the `ReferenceKind != None` gate fails) --
// `Type()` is the `ByReferenceType` itself.
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, CtorDoesNotUnwrapByReferenceForPlainParameter)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto refType = std::make_shared<TS::ByReferenceType>(intType);
    TestParameter param("p", refType, /*isConst*/ false, std::any(), TS::ReferenceKind::None);
    LocalResolveResult rr(&param);
    EXPECT_EQ(&rr.Type(), refType.get());
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::ByReference);
}

// ---------------------------------------------------------------------------
// The `UnpackTypeIfByRefParameter` crux: a non-parameter `IVariable` of
// `ByReference` type does NOT unwrap (the `variable is IParameter` gate fails) --
// `Type()` is the `ByReferenceType` itself.
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, CtorDoesNotUnwrapByReferenceForLocalVariable)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto refType = std::make_shared<TS::ByReferenceType>(intType);
    TestVariable variable(TS::SymbolKind::Variable, "x", refType, /*isConst*/ false, std::any());
    LocalResolveResult rr(&variable);
    EXPECT_EQ(&rr.Type(), refType.get());
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::ByReference);
}

// ---------------------------------------------------------------------------
// `IsParameter` returns true when the stored variable is an `IParameter`.
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, IsParameterReturnsTrueForParameter)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestParameter param("p", intType, /*isConst*/ false, std::any(), TS::ReferenceKind::None);
    LocalResolveResult rr(&param);
    EXPECT_TRUE(rr.IsParameter());
}

// ---------------------------------------------------------------------------
// `IsParameter` returns false when the stored variable is NOT an `IParameter`
// (a plain local variable).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, IsParameterReturnsFalseForLocalVariable)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false, std::any());
    LocalResolveResult rr(&variable);
    EXPECT_FALSE(rr.IsParameter());
}

// ---------------------------------------------------------------------------
// `IsCompileTimeConstant` reflects the variable's `IsConst` (a `const local` is a
// compile-time constant).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, IsCompileTimeConstantReflectsVariableIsConst)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable constVar(TS::SymbolKind::Variable, "c", intType, /*isConst*/ true,
                          std::any(std::int32_t(42)));
    LocalResolveResult constRr(&constVar);
    EXPECT_TRUE(constRr.IsCompileTimeConstant());

    TestVariable nonConstVar(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false, std::any());
    LocalResolveResult nonConstRr(&nonConstVar);
    EXPECT_FALSE(nonConstRr.IsCompileTimeConstant());
}

// ---------------------------------------------------------------------------
// `ConstantValue` returns the variable's boxed constant value for a local (the
// `IsParameter ? null` short-circuit does NOT fire for a local).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, ConstantValueReturnsVariableConstantValueForLocal)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "c", intType, /*isConst*/ true,
                          std::any(std::int32_t(42)));
    LocalResolveResult rr(&variable);
    const auto cv = rr.ConstantValue();
    ASSERT_TRUE(cv.has_value());
    ASSERT_EQ(cv.type(), typeid(std::int32_t));
    EXPECT_EQ(std::any_cast<std::int32_t>(cv), 42);
}

// ---------------------------------------------------------------------------
// `ConstantValue` returns null (empty `any`) for a parameter EVEN when the
// parameter has a constant value (the `IsParameter ? null` short-circuit fires;
// a parameter has no compile-time constant value even when it has a default).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, ConstantValueReturnsNullForParameter)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestParameter param("p", intType, /*isConst*/ true, std::any(std::int32_t(7)),
                        TS::ReferenceKind::None);
    LocalResolveResult rr(&param);
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

// ---------------------------------------------------------------------------
// `ToString` is `"[LocalResolveResult <variable name>]"` (the C# uses
// `variable.ToString()`; the C++ port uses `variable.Name()` as the closest
// meaningful representation).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, ToStringFormat)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false, std::any());
    LocalResolveResult rr(&variable);
    EXPECT_EQ(rr.ToString(), "[LocalResolveResult x]");
}

// ---------------------------------------------------------------------------
// `ShallowClone` preserves the runtime type (not sliced to the `ResolveResult`
// base) and copies the non-owning `variable_` pointer (the clone's `Variable()`
// is the same pointer as the original's).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, ShallowClonePreservesRuntimeTypeAndSharesVariable)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false, std::any());
    LocalResolveResult rr(&variable);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* cloned = dynamic_cast<LocalResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->Variable(), &variable);
    EXPECT_EQ(cloned->Type().Name(), "Int32");
    EXPECT_TRUE(cloned->IsParameter() == rr.IsParameter());
}

// ---------------------------------------------------------------------------
// The `IsCompileTimeConstant` / `ConstantValue` / `ToString` / `ClassName`
// overrides dispatch through a `ResolveResult*` base pointer (the virtual dispatch
// the C# resolver relies on when it holds a result as a `ResolveResult`).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, VirtualDispatchThroughBasePointer)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "c", intType, /*isConst*/ true,
                          std::any(std::int32_t(42)));
    LocalResolveResult rr(&variable);
    ResolveResult* base = &rr;
    EXPECT_TRUE(base->IsCompileTimeConstant());
    const auto cv = base->ConstantValue();
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(cv), 42);
    EXPECT_EQ(base->ToString(), "[LocalResolveResult c]");
}

// ---------------------------------------------------------------------------
// The subclass does NOT override `IsError` or `GetChildResults`, so the inherited
// `ResolveResult` base defaults hold: `IsError` is false, `GetChildResults` is
// empty.
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, InheritedDefaultsArePreserved)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestVariable variable(TS::SymbolKind::Variable, "x", intType, /*isConst*/ false, std::any());
    LocalResolveResult rr(&variable);
    EXPECT_FALSE(rr.IsError());
    EXPECT_TRUE(rr.GetChildResults().empty());
}

// ---------------------------------------------------------------------------
// Class-shape static-asserts: `LocalResolveResult` IS-A `ResolveResult`, has a
// virtual destructor, is polymorphic, and is NOT `final` (the C# class is
// unsealed).
// ---------------------------------------------------------------------------
TEST(LocalResolveResultTest, ClassShapeStaticAsserts)
{
    using T = LocalResolveResult;
    static_assert(std::is_base_of_v<ResolveResult, T>);
    static_assert(std::has_virtual_destructor_v<T>);
    static_assert(std::is_polymorphic_v<T>);
    static_assert(!std::is_final_v<T>);
}
