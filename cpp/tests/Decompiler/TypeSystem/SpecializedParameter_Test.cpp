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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `SpecializedParameter` (D480) -- the sealed `IParameter` a
// `SpecializedParameterizedMember` builds its substituted parameter list out of
// (Implementation/SpecializedParameter.cs). It wraps a base `IParameter` and a new
// `IType`, delegating the whole `IParameter` / `IVariable` / `ISymbol` surface to the
// base parameter EXCEPT `Type` (the new type) and `Owner` (the new owning member).
// `SpecializedParameterizedMember.CreateParameters` constructs one per parameter:
// `new SpecializedParameter(p, p.Type.AcceptVisitor(substitution), this)`.
//
// The tests pin:
//  (a) `Type()` returns the NEW type (not the base parameter's type), and the
//      returned reference is to the same `IType` object passed in (shared identity);
//  (b) `Owner()` returns the NEW owner (not the base parameter's owner), and is
//      nullable (nullptr when constructed with a null owner);
//  (c) every delegated member forwards to the base parameter: `Name`, `SymbolKind`
//      (Parameter), `ReferenceKind`, `Lifetime`, `IsParams`, `IsOptional`,
//      `HasConstantValueInSignature`, `GetAttributes`, `IsConst`, `GetConstantValue`;
//  (d) the class shape: `SpecializedParameter` IS-A `IParameter` (and is final).

#include "Decompiler/TypeSystem/Implementation/SpecializedParameter.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedParameter;

ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A configurable concrete `IParameter` stand-in (the "base parameter" the
// `SpecializedParameter` wraps). Holds the type, name, reference kind, lifetime, the
// params/optional/has-constant-value-in-signature/const flags, the owning member, and
// the attributes list; every accessor delegates to the held values. Constructed via
// `make_shared` so the `SpecializedParameter` can hold it as a `shared_ptr<IParameter>`.
class TestBaseParameter : public IParameter {
public:
    TestBaseParameter(ITypePtr type, std::string name,
                      ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                      LifetimeAnnotation lifetime = LifetimeAnnotation{},
                      bool isParams = false, bool isOptional = false,
                      bool hasConstantValueInSignature = false, bool isConst = false,
                      const IParameterizedMember* owner = nullptr,
                      std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes = {},
                      std::any constantValue = std::any{})
        : type_(std::move(type)), name_(std::move(name)),
          referenceKind_(referenceKind), lifetime_(lifetime),
          isParams_(isParams), isOptional_(isOptional),
          hasConstantValueInSignature_(hasConstantValueInSignature), isConst_(isConst),
          owner_(owner), attributes_(std::move(attributes)),
          constantValue_(std::move(constantValue)) {}

    // --- IParameter ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return attributes_;
    }
    // The return type is globally qualified: the inherited `IParameter::ReferenceKind`
    // member name shadows the namespace-scope `ReferenceKind` enum in MSVC's
    // complete-class lookup (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return referenceKind_; }
    LifetimeAnnotation Lifetime() const override { return lifetime_; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return hasConstantValueInSignature_; }
    const IParameterizedMember* Owner() const override { return owner_; }

    // --- IVariable ---
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/) const override
    {
        return constantValue_;
    }

    // --- ISymbol ---
    std::string Name() const override { return name_; }
    // The return type is globally qualified: the inherited `ISymbol::SymbolKind` member
    // name shadows the namespace-scope `SymbolKind` enum (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }

private:
    ITypePtr type_;
    std::string name_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
    LifetimeAnnotation lifetime_;
    bool isParams_;
    bool isOptional_;
    bool hasConstantValueInSignature_;
    bool isConst_;
    const IParameterizedMember* owner_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    std::any constantValue_;
};

} // namespace

// ---------------------------------------------------------------------------
// Type() returns the NEW type (not the base parameter's type).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, TypeReturnsNewType)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x");
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_EQ(sp->Type().Name(), "Int32");
    EXPECT_NE(sp->Type().Name(), "String"); // not the base's type
}

// ---------------------------------------------------------------------------
// Type() returns a reference to the same IType object passed in (shared identity).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, TypeReturnsSameInstancePassedIn)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x");
    auto newType = Int32();
    auto sp = std::make_shared<SpecializedParameter>(base.get(), newType, /*owner*/ nullptr);
    EXPECT_EQ(&sp->Type(), newType.get());
}

// ---------------------------------------------------------------------------
// Name() delegates to the base parameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, NameDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "value");
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_EQ(sp->Name(), "value");
}

// ---------------------------------------------------------------------------
// SymbolKind() is Parameter (the C# `ISymbol.SymbolKind => SymbolKind.Parameter`).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, SymbolKindIsParameter)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x");
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_EQ(sp->SymbolKind(), SymbolKind::Parameter);
}

// ---------------------------------------------------------------------------
// ReferenceKind() delegates to the base parameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, ReferenceKindDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::Ref);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_EQ(sp->ReferenceKind(), ReferenceKind::Ref);
}

// ---------------------------------------------------------------------------
// Lifetime() delegates to the base parameter (the scoped-ref annotation).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, LifetimeDelegatesToBase)
{
    LifetimeAnnotation scoped;
    scoped.ScopedRef(true);
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    scoped);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_TRUE(sp->Lifetime().ScopedRef());
}

// ---------------------------------------------------------------------------
// IsParams() delegates to the base parameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, IsParamsDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, /*isParams*/ true);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_TRUE(sp->IsParams());
}

// ---------------------------------------------------------------------------
// IsOptional() delegates to the base parameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, IsOptionalDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, /*isParams*/ false,
                                                    /*isOptional*/ true);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_TRUE(sp->IsOptional());
}

// ---------------------------------------------------------------------------
// HasConstantValueInSignature() delegates to the base parameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, HasConstantValueInSignatureDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, /*isParams*/ false,
                                                    /*isOptional*/ true,
                                                    /*hasConstantValueInSignature*/ true);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_TRUE(sp->HasConstantValueInSignature());
}

// ---------------------------------------------------------------------------
// GetAttributes() delegates to the base parameter (returns the base's snapshot).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, GetAttributesDelegatesToBase)
{
    // The base returns a non-empty snapshot (the pointers are opaque; the test pins
    // that the SpecializedParameter forwards the base's vector verbatim).
    const ILSpy::Decompiler::TypeSystem::IAttribute* attrA =
        reinterpret_cast<const ILSpy::Decompiler::TypeSystem::IAttribute*>(0x10);
    const ILSpy::Decompiler::TypeSystem::IAttribute* attrB =
        reinterpret_cast<const ILSpy::Decompiler::TypeSystem::IAttribute*>(0x20);
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, false, false, false,
                                                    false, nullptr,
                                                    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>{attrA, attrB});
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    auto attrs = sp->GetAttributes();
    ASSERT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0], attrA);
    EXPECT_EQ(attrs[1], attrB);
}

// ---------------------------------------------------------------------------
// IsConst() delegates to the base parameter.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, IsConstDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, false, false, false,
                                                    /*isConst*/ true);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_TRUE(sp->IsConst());
}

// ---------------------------------------------------------------------------
// GetConstantValue() delegates to the base parameter (forwards the boxed value).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, GetConstantValueDelegatesToBase)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, false,
                                                    /*isOptional*/ true, false, false, nullptr,
                                                    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>{},
                                                    std::any(42));
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    // The default `throwOnInvalidMetadata = false` lives on the `IVariable` interface
    // (the port convention: defaults are not repeated in overrides); call with the
    // explicit value the C# default would supply.
    auto val = sp->GetConstantValue(false);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(std::any_cast<int>(val), 42);
}

// ---------------------------------------------------------------------------
// Owner() returns the NEW owner (not the base parameter's owner).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, OwnerReturnsNewOwner)
{
    // The base has a non-null owner; the SpecializedParameter's owner is a DIFFERENT
    // member (the specializing member). The test uses distinct opaque pointers so the
    // forwarded owner is distinguishable from the base's.
    const IParameterizedMember* baseOwner =
        reinterpret_cast<const IParameterizedMember*>(0x100);
    const IParameterizedMember* newOwner =
        reinterpret_cast<const IParameterizedMember*>(0x200);
    auto base = std::make_shared<TestBaseParameter>(String(), "x", ReferenceKind::None,
                                                    LifetimeAnnotation{}, false, false, false,
                                                    false, baseOwner);
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), newOwner);
    EXPECT_EQ(sp->Owner(), newOwner);
    EXPECT_NE(sp->Owner(), baseOwner); // not the base's owner
}

// ---------------------------------------------------------------------------
// Owner() is nullable: a null new owner is returned as nullptr (the IParameter.Owner
// contract -- "May return null" for lambda/anonymous-method parameters).
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, OwnerIsNullableWhenConstructedWithNull)
{
    auto base = std::make_shared<TestBaseParameter>(String(), "x"); // base owner is null too
    auto sp = std::make_shared<SpecializedParameter>(base.get(), Int32(), /*owner*/ nullptr);
    EXPECT_EQ(sp->Owner(), nullptr);
}

// ---------------------------------------------------------------------------
// The class shape: SpecializedParameter IS-A IParameter, and is final.
// ---------------------------------------------------------------------------
TEST(SpecializedParameterTest, IsAnIParameterAndFinal)
{
    static_assert(std::is_base_of_v<IParameter, SpecializedParameter>);
    static_assert(std::is_final_v<SpecializedParameter>);
    // A SpecializedParameter is usable through an IParameter base reference.
    auto base = std::make_shared<TestBaseParameter>(String(), "x");
    std::shared_ptr<IParameter> asParam =
        std::make_shared<SpecializedParameter>(base.get(), Int32(), nullptr);
    EXPECT_EQ(asParam->Name(), "x");
    EXPECT_EQ(asParam->Type().Name(), "Int32");
    EXPECT_EQ(asParam->SymbolKind(), SymbolKind::Parameter);
}
