// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software, including without limitation, rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for `OutVarResolveResult` (cpp/Decompiler/Semantics/OutVarResolveResult.hpp,
// the D440 port of ICSharpCode.Decompiler/Semantics/OutVarResolveResult.cs) -- the
// `ResolveResult` (D424) for an implicitly-typed `out var` argument. The C# source
// declares a 1-arg ctor (`OutVarResolveResult(IType originalVariableType) : base(
// SpecialType.NoType)`) and one readonly field (`OriginalVariableType`); the C++ port
// additionally overrides `ClassName()` (the polymorphic `GetType().Name` crux in the
// inherited `ToString`) and `ShallowClone()` (the runtime-type-preserving clone,
// avoiding the C++-only slicing).
//
// The tests pin the `SpecialType.NoType` base-type forwarding crux (`Type().Kind() ==
// TypeKind::None`, distinct from `UnknownType()`'s `TypeKind::Unknown`), the
// `OriginalVariableType` field-stores-configured-type crux (including the nullable
// case the C# ctor allows), the `OriginalVariableType`-distinct-from-base-`Type`
// distinction (the base is `NoType`, the field is the configured type), the inherited
// base defaults the subclass does NOT override (`IsError` /
// `IsCompileTimeConstant` / `ConstantValue` / `GetChildResults`), the `ToString`
// subclass-class-name format (`[OutVarResolveResult ?]` -- the `?` is the
// `NoType()` SpecialType's `ReflectionName`), the `ShallowClone` runtime-type
// preservation (not sliced) plus shared-ownership of BOTH `IType` fields plus
// distinct-instance, the polymorphic `ClassName()` dispatch through a
// `ResolveResult*` base pointer, and the `is_base_of` / not-`final` (the C# class is
// unsealed) class-shape static-asserts.

#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// A concrete `IType`: `KnownType(String)` whose `ReflectionName()` is "System.String"
// and whose `Kind()` is `TypeKind::Class`. Used as the original variable type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeStringType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// A second distinct `IType`: `KnownType(Object)` whose `ReflectionName()` is
// "System.Object". Used to prove the ctor stores the configured type (not a
// hardcoded one).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

} // namespace

TEST(OutVarResolveResultTest, ConstructorForwardsNoTypeToBase)
{
    // The C# ctor forwards `SpecialType.NoType` (the `TypeKind::None` null object) to
    // the `ResolveResult` base. The C++ port uses the `NoType()` convenience (the D433
    // prerequisite this leaf consumes), so `Type().Kind()` is `TypeKind::None` (no type
    // at all -- an `out var` expression is special-cased to match any out-parameter),
    // DISTINCT from `UnknownType()`'s `TypeKind::Unknown` (the error-type null object).
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(type);
    EXPECT_EQ(ovrr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::None);
}

TEST(OutVarResolveResultTest, TypeReflectionNameIsNoTypeName)
{
    // The `NoType()` SpecialType's `ReflectionName()` is its `Name()` which the
    // `SpecialType::Name()` switch returns the C# `SpecialType.NoType` singleton's name "?" for `TypeKind::None` (the same
    // from `UnknownType()`'s "?" for `TypeKind::Unknown`).
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(type);
    EXPECT_EQ(ovrr.Type().ReflectionName(), "?");
}

TEST(OutVarResolveResultTest, OriginalVariableTypeReturnsConfiguredType)
{
    // The C# `public readonly IType OriginalVariableType` field stores the configured
    // type; the C++ `OriginalVariableType()` accessor returns the owning `ITypePtr`
    // handle by const reference (the `ResolveResult::TypePtr()` D428 precedent). The
    // stored handle is the same `IType` object the caller passed (shared ownership).
    auto type = MakeStringType();
    auto* raw = type.get();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    EXPECT_EQ(ovrr.OriginalVariableType().get(), raw);
    EXPECT_EQ(ovrr.OriginalVariableType()->ReflectionName(), "System.String");
}

TEST(OutVarResolveResultTest, OriginalVariableTypeStoresDistinctType)
{
    // The ctor stores the configured type (not a hardcoded one): a second distinct
    // type yields a distinct `OriginalVariableType()` return.
    auto objectType = MakeObjectType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(objectType));
    EXPECT_EQ(ovrr.OriginalVariableType()->ReflectionName(), "System.Object");
}

TEST(OutVarResolveResultTest, OriginalVariableTypeAcceptsNull)
{
    // The C# ctor does NOT guard `originalVariableType` with `ArgumentNullException`
    // (the field may be null), so the C++ port does NOT `assert` on it -- the faithful
    // null-handling distinct from the D424 base ctor which DOES assert on its `type`
    // parameter. An empty `ITypePtr` (shared_ptr) is the C# `null`, so
    // `OriginalVariableType().get()` is `nullptr`.
    ILSpy::Decompiler::TypeSystem::ITypePtr nullType;
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(nullType));
    EXPECT_EQ(ovrr.OriginalVariableType().get(), nullptr);
}

TEST(OutVarResolveResultTest, OriginalVariableTypeIsDistinctFromBaseType)
{
    // The base `Type()` is `NoType()` (the `TypeKind::None` null object forwarded to the
    // `ResolveResult` base ctor); the `OriginalVariableType()` field is the configured
    // type (a distinct `IType`). The two accessors yield DIFFERENT `IType` objects,
    // pinning the structural distinction between the NoType base type and the original
    // variable type the `out var` falls back to.
    auto type = MakeStringType();
    auto* raw = type.get();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    EXPECT_NE(&ovrr.Type(), raw);
    EXPECT_EQ(ovrr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::None);
    EXPECT_EQ(ovrr.OriginalVariableType()->Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
}

TEST(OutVarResolveResultTest, InheritedDefaultsArePreserved)
{
    // OutVarResolveResult does NOT override IsError / IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited ResolveResult base defaults
    // hold. An `out var` expression is not an error, not a compile-time constant.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    EXPECT_FALSE(ovrr.IsError());
    EXPECT_FALSE(ovrr.IsCompileTimeConstant());
    EXPECT_FALSE(ovrr.ConstantValue().has_value());
    EXPECT_TRUE(ovrr.GetChildResults().empty());
}

TEST(OutVarResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# ToString (inherited from ResolveResult) uses GetType().Name which is
    // polymorphic and yields "OutVarResolveResult". The C++ port reproduces this via
    // the ClassName() override so the inherited ToString reports the subclass name (not
    // the base "ResolveResult"). The {type} field is the NoType() SpecialType's
    // ReflectionName "?".
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    EXPECT_EQ(ovrr.ToString(), "[OutVarResolveResult ?]");
}

TEST(OutVarResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The ClassName() override dispatches through a ResolveResult* base pointer (the
    // virtual dispatch the C# resolver relies on in ToString via GetType().Name): the
    // base pointer's ToString reports the subclass name, not the base.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    ILSpy::Decompiler::Semantics::ResolveResult* base = &ovrr;
    EXPECT_EQ(base->ToString(), "[OutVarResolveResult ?]");
}

TEST(OutVarResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# ShallowClone (inherited, uses MemberwiseClone) preserves the runtime
    // type, so a cloned OutVarResolveResult stays an OutVarResolveResult (not sliced to
    // the ResolveResult base). The C++ override reproduces this: the clone is an
    // OutVarResolveResult (dynamic_cast succeeds), not a sliced ResolveResult.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    auto clone = ovrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::OutVarResolveResult*>(clone.get()),
              nullptr);
}

TEST(OutVarResolveResultTest, ShallowCloneSharesTypeAndOriginalVariableType)
{
    // The clone shares BOTH IType fields via the shared_ptr members (faithful to
    // MemberwiseClone's reference copy): the same NoType() SpecialType backs the base
    // `Type()` of both the original and the clone, AND the same original variable type
    // backs the `OriginalVariableType()` of both. The default copy ctor shares the
    // `type_` shared_ptr (the base NoType) and the `originalVariableType_` shared_ptr.
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    auto clone = ovrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    // The base NoType() is shared (same IType object backs both Type() returns).
    EXPECT_EQ(&clone->Type(), &ovrr.Type());
    EXPECT_EQ(clone->Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::None);
    // The original variable type is shared (same IType object backs both
    // OriginalVariableType() returns). `OriginalVariableType()` is a non-virtual
    // `OutVarResolveResult`-own accessor (NOT inherited from `ResolveResult`), so it
    // is reached through the clone via a `dynamic_cast` downcast (the D433
    // `NamespaceResolveResult` subclass-own-accessor-through-base-pointer precedent).
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::OutVarResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->OriginalVariableType().get(), ovrr.OriginalVariableType().get());
    EXPECT_EQ(cloned->OriginalVariableType()->ReflectionName(), "System.String");
}

TEST(OutVarResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto type = MakeStringType();
    ILSpy::Decompiler::Semantics::OutVarResolveResult ovrr(std::move(type));
    auto clone = ovrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &ovrr);
}

TEST(OutVarResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::OutVarResolveResult>,
                  "OutVarResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::OutVarResolveResult>,
                  "OutVarResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::OutVarResolveResult>,
                  "OutVarResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `OutVarResolveResult` is NOT sealed (no subclass derives from it but it
    // is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::OutVarResolveResult>,
                  "OutVarResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
