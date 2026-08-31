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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `DefaultParameter` -- the default `IParameter` implementation
// (Implementation/DefaultParameter.cs) and the canonical parameter-signature
// renderer (`DefaultParameter.ToString(IParameter)`, the static the whole
// `IParameter` hierarchy shares).
//
// The tests pin:
//  (a) the ctor stores every field and defaults the full surface (owner null,
//      ReferenceKind None, IsParams/IsOptional false, HasConstantValueInSignature
//      false, IsConst false, Lifetime all-false, empty attributes, empty default
//      value, SymbolKind Parameter);
//  (b) the C# ArgumentNullException on a null type ports to
//      std::invalid_argument;
//  (c) the `HasConstantValueInSignature => IsOptional` coupling;
//  (d) the static ToString renderer: the reference-kind prefixes (""/ref /out /
//      in /ref readonly ), the `params ` prefix, the `name:ReflectionName` core,
//      and the optional-constant arm (the `IsOptional && HasConstantValueInSignature`
//      conjunction, the " = value" rendering of the boxed default, the " = null"
//      rendering of the empty default, and the omission when either flag is false);
//  (e) the primitive boxed-value spellings (int decimal digits, bool "True", the
//      shortest-round-trip float/double rendering, the string verbatim);
//  (f) the instance `ToString()` delegation to the static renderer, and the
//      `SpecializedParameter.ToString()` wiring (the deferred member that lands
//      through this leaf);
//  (g) the class shape: `DefaultParameter` IS-A `IParameter` and is final.

#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedParameter.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;
using ILSpy::Decompiler::TypeSystem::Implementation::SpecializedParameter;

ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Double() { return std::make_shared<KnownType>(KnownTypeCode::Double); }
ITypePtr Int32Array() { return std::make_shared<ArrayType>(Int32()); }

// A configurable concrete `IParameter` stand-in with INDEPENDENT
// IsOptional / HasConstantValueInSignature flags -- the shape the static
// ToString's conjunction test needs (a `DefaultParameter` couples the two flags,
// so the omission-when-optional-but-not-in-signature case needs this stub to
// report them independently). Each test file carries its own local stubs (the
// established convention).
class ConfigurableParameter : public IParameter {
public:
    ConfigurableParameter(ITypePtr type, std::string name,
                          ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind =
                              ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                          bool isParams = false, bool isOptional = false,
                          bool hasConstantValueInSignature = false,
                          std::any constantValue = std::any{})
        : type_(std::move(type)), name_(std::move(name)),
          referenceKind_(referenceKind), isParams_(isParams),
          isOptional_(isOptional),
          hasConstantValueInSignature_(hasConstantValueInSignature),
          constantValue_(std::move(constantValue)) {}

    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override {
        return referenceKind_;
    }
    LifetimeAnnotation Lifetime() const override { return LifetimeAnnotation{}; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override {
        return hasConstantValueInSignature_;
    }
    const IParameterizedMember* Owner() const override { return nullptr; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/) const override {
        return constantValue_;
    }
    std::string Name() const override { return name_; }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }

private:
    ITypePtr type_;
    std::string name_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
    bool isParams_;
    bool isOptional_;
    bool hasConstantValueInSignature_;
    std::any constantValue_;
};

} // namespace

// ---------------------------------------------------------------------------
// The 2-argument ctor shape stores the type and the name (the
// `CSharpOperators.InitParameterArrays` call shape).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, CtorStoresTypeAndName)
{
    DefaultParameter p(Int32(), "x");
    EXPECT_EQ(p.Type().Name(), "Int32");
    EXPECT_EQ(p.Name(), "x");
}

// ---------------------------------------------------------------------------
// The 2-argument ctor shape defaults the whole rest of the surface.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, CtorDefaultsTheFullSurface)
{
    DefaultParameter p(String(), "value");
    EXPECT_EQ(p.Owner(), nullptr);
    EXPECT_EQ(p.ReferenceKind(), ReferenceKind::None);
    EXPECT_FALSE(p.IsParams());
    EXPECT_FALSE(p.IsOptional());
    EXPECT_FALSE(p.HasConstantValueInSignature());
    EXPECT_FALSE(p.IsConst());
    EXPECT_FALSE(p.Lifetime().ScopedRef());
    EXPECT_TRUE(p.GetAttributes().empty());
    EXPECT_FALSE(p.GetConstantValue(false).has_value());
    EXPECT_EQ(p.SymbolKind(), SymbolKind::Parameter);
}

// ---------------------------------------------------------------------------
// A null type throws std::invalid_argument (the C# ArgumentNullException).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, NullTypeThrowsInvalidArgument)
{
    EXPECT_THROW(DefaultParameter(nullptr, "x"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// The full ctor stores every field: the owner pointer (identity), the reference
// kind, the params/optional flags, the attributes snapshot, and the boxed
// default value (round-tripped via any_cast).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, FullCtorStoresEveryField)
{
    IParameterizedMember* owner = reinterpret_cast<IParameterizedMember*>(0x1);
    std::vector<const IAttribute*> attributes;
    DefaultParameter p(Int32(), "x", owner, attributes,
                       ReferenceKind::RefReadOnly, /*isParams*/ true,
                       /*isOptional*/ true, std::int32_t(42));
    EXPECT_EQ(p.Owner(), owner);
    EXPECT_EQ(p.ReferenceKind(), ReferenceKind::RefReadOnly);
    EXPECT_TRUE(p.IsParams());
    EXPECT_TRUE(p.IsOptional());
    EXPECT_TRUE(p.GetAttributes().empty());
    const std::int32_t* value = std::any_cast<std::int32_t>(&p.GetConstantValue(false));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
}

// ---------------------------------------------------------------------------
// HasConstantValueInSignature follows IsOptional (the C#
// `bool HasConstantValueInSignature => IsOptional` coupling).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, HasConstantValueInSignatureFollowsIsOptional)
{
    DefaultParameter p(Int32(), "x", nullptr, {},
                       ReferenceKind::None, /*isParams*/ false,
                       /*isOptional*/ true);
    EXPECT_TRUE(p.IsOptional());
    EXPECT_TRUE(p.HasConstantValueInSignature());
}

// ---------------------------------------------------------------------------
// Type() returns a reference to the same IType object passed in.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, TypeReturnsSameInstancePassedIn)
{
    auto type = Int32();
    DefaultParameter p(type, "x");
    EXPECT_EQ(&p.Type(), type.get());
}

// ---------------------------------------------------------------------------
// IsConst is always false (the C# `bool IVariable.IsConst => false`).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, IsConstIsAlwaysFalse)
{
    DefaultParameter p(Int32(), "x");
    EXPECT_FALSE(p.IsConst());
}

// ---------------------------------------------------------------------------
// The parameter dispatches polymorphically through the IParameter base.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, DispatchesThroughIParameterBase)
{
    DefaultParameter p(Int32(), "x", nullptr, {}, ReferenceKind::In);
    const IParameter& base = p;
    EXPECT_EQ(base.Name(), "x");
    EXPECT_EQ(base.SymbolKind(), SymbolKind::Parameter);
    EXPECT_EQ(base.ReferenceKind(), ReferenceKind::In);
}

// ---------------------------------------------------------------------------
// The static ToString renders a plain parameter: "name:ReflectionName".
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersPlainParameter)
{
    DefaultParameter p(Int32(), "x");
    EXPECT_EQ(DefaultParameter::ToString(p), "x:System.Int32");
}

// ---------------------------------------------------------------------------
// The static ToString renders the ref prefix.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersRefPrefix)
{
    DefaultParameter p(Int32(), "x", nullptr, {}, ReferenceKind::Ref);
    EXPECT_EQ(DefaultParameter::ToString(p), "ref x:System.Int32");
}

// ---------------------------------------------------------------------------
// The static ToString renders the out and in prefixes.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersOutAndInPrefixes)
{
    DefaultParameter out(Int32(), "x", nullptr, {}, ReferenceKind::Out);
    DefaultParameter in(Int32(), "y", nullptr, {}, ReferenceKind::In);
    EXPECT_EQ(DefaultParameter::ToString(out), "out x:System.Int32");
    EXPECT_EQ(DefaultParameter::ToString(in), "in y:System.Int32");
}

// ---------------------------------------------------------------------------
// The static ToString renders the ref readonly prefix.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersRefReadOnlyPrefix)
{
    DefaultParameter p(Int32(), "x", nullptr, {}, ReferenceKind::RefReadOnly);
    EXPECT_EQ(DefaultParameter::ToString(p), "ref readonly x:System.Int32");
}

// ---------------------------------------------------------------------------
// The static ToString renders the params prefix ahead of the name (over the
// array ReflectionName).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersParamsPrefix)
{
    DefaultParameter p(Int32Array(), "values", nullptr, {},
                       ReferenceKind::None, /*isParams*/ true);
    EXPECT_EQ(DefaultParameter::ToString(p), "params values:System.Int32[]");
}

// ---------------------------------------------------------------------------
// The static ToString renders the optional constant: " = 42".
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersOptionalConstantValue)
{
    DefaultParameter p(Int32(), "x", nullptr, {},
                       ReferenceKind::None, /*isParams*/ false,
                       /*isOptional*/ true, std::int32_t(42));
    EXPECT_EQ(DefaultParameter::ToString(p), "x:System.Int32 = 42");
}

// ---------------------------------------------------------------------------
// The static ToString renders the primitive spellings: bool "True", the
// shortest-round-trip double "42.5", the string verbatim, and the empty
// (null) default as the literal "null".
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringRendersPrimitiveAndNullConstants)
{
    DefaultParameter boolean(std::make_shared<KnownType>(KnownTypeCode::Boolean),
                             "b", nullptr, {}, ReferenceKind::None, false,
                             /*isOptional*/ true, true);
    EXPECT_EQ(DefaultParameter::ToString(boolean), "b:System.Boolean = True");

    DefaultParameter real(Double(), "d", nullptr, {}, ReferenceKind::None,
                          false, /*isOptional*/ true, 42.5);
    EXPECT_EQ(DefaultParameter::ToString(real), "d:System.Double = 42.5");

    DefaultParameter text(String(), "s", nullptr, {}, ReferenceKind::None,
                          false, /*isOptional*/ true, std::string("abc"));
    EXPECT_EQ(DefaultParameter::ToString(text), "s:System.String = abc");

    DefaultParameter none(Int32(), "x", nullptr, {}, ReferenceKind::None,
                          false, /*isOptional*/ true, std::any{});
    EXPECT_EQ(DefaultParameter::ToString(none), "x:System.Int32 = null");
}

// ---------------------------------------------------------------------------
// The static ToString omits the constant when the parameter is not optional.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringOmitsConstantWhenNotOptional)
{
    DefaultParameter p(Int32(), "x", nullptr, {}, ReferenceKind::None,
                       /*isParams*/ false, /*isOptional*/ false,
                       std::int32_t(42));
    EXPECT_EQ(DefaultParameter::ToString(p), "x:System.Int32");
}

// ---------------------------------------------------------------------------
// The static ToString omits the constant when the parameter is optional but
// its constant is not presented in the signature -- the conjunction is
// load-bearing (a wrapper IParameter reports the two flags independently; the
// ConfigurableParameter stub pins the shape a DefaultParameter cannot).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, ToStringOmitsConstantWhenOptionalButNotInSignature)
{
    ConfigurableParameter p(Int32(), "x", ReferenceKind::None, false,
                            /*isOptional*/ true,
                            /*hasConstantValueInSignature*/ false,
                            std::int32_t(42));
    EXPECT_EQ(DefaultParameter::ToString(p), "x:System.Int32");
}

// ---------------------------------------------------------------------------
// The instance ToString() delegates to the static renderer (the C#
// `public override string ToString() => ToString(this)`).
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, InstanceToStringDelegatesToStaticFormatter)
{
    DefaultParameter p(Int32(), "x", nullptr, {}, ReferenceKind::Ref,
                       /*isParams*/ true);
    EXPECT_EQ(p.ToString(), "ref params x:System.Int32");
    EXPECT_EQ(p.ToString(), DefaultParameter::ToString(p));
}

// ---------------------------------------------------------------------------
// The SpecializedParameter's ToString (the member deferred from that leaf)
// delegates to the DefaultParameter static renderer: the NAME comes from the
// base parameter, the TYPE from the new type.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, SpecializedParameterToStringDelegatesToDefaultParameter)
{
    DefaultParameter base(String(), "x", nullptr, {}, ReferenceKind::Ref);
    SpecializedParameter sp(&base, Int32(), /*owner*/ nullptr);
    EXPECT_EQ(sp.ToString(), "ref x:System.Int32");
    EXPECT_EQ(sp.ToString(), DefaultParameter::ToString(sp));
}

// ---------------------------------------------------------------------------
// The class shape: final, and IS-A IParameter.
// ---------------------------------------------------------------------------
TEST(DefaultParameterTest, IsFinalAndIParameterBase)
{
    static_assert(std::is_final_v<DefaultParameter>,
                  "DefaultParameter mirrors the C# sealed class");
    static_assert(std::is_base_of_v<IParameter, DefaultParameter>,
                  "DefaultParameter implements IParameter");
    SUCCEED();
}
