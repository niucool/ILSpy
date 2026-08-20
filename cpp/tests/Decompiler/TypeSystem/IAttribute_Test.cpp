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

// Tests for `IAttribute` (cpp/Decompiler/TypeSystem/IAttribute.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IAttribute.cs) -- the interface for a resolved custom
// attribute. `IAttribute` carries the attribute's `AttributeType` (non-null), the resolved
// `Constructor` (nullable), the `HasDecodeErrors` flag, the positional `FixedArguments`, and
// the `NamedArguments`. The tests pin every accessor, the nullable-`Constructor` (null when
// no matching constructor was found) and non-null-`AttributeType` contracts, the by-value
// argument-vector snapshots, polymorphic dispatch through an `IAttribute*`, and the virtual
// destructor.
//
// The `IMethod` stand-in (the `Constructor` return type) is a minimal complete stand-in
// defined in the `ILSpy::Decompiler::TypeSystem` namespace (a TEST FIXTURE, NOT a faithful
// port of the full `IMethod` surface); a forward declaration would suffice for a null
// `Constructor` return, but the non-null case needs a concrete object to point at. It is
// IDENTICAL to any future stand-in that needs `IMethod` complete (ODR-safe across TUs).

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Minimal test stand-in for `IMethod` (the method interface). Only a virtual destructor; the
// real `IMethod` pulls `IParameterizedMember` / `ITypeParameter` / `IAttribute` /
// `MethodSemanticsAttributes` and the member family. A TEST FIXTURE, NOT a faithful port;
// dropped when the real `IMethod.hpp` lands. IDENTICAL to any future stand-in (ODR-safe).
class IMethod {
public:
    virtual ~IMethod() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::CustomAttributeNamedArgument;
using TS::CustomAttributeNamedArgumentKind;
using TS::CustomAttributeTypedArgument;
using TS::IAttribute;
using TS::IMethod;
using TS::IType;
using TS::KnownType;
using TS::KnownTypeCode;

namespace {

// A minimal concrete `IMethod` for testing the non-null `Constructor` slot.
class TestMethod : public IMethod {
public:
    explicit TestMethod(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A minimal concrete `IAttribute` for testing: holds the configured state and returns it from
// every accessor (the shape a real `DefaultAttribute` / `CustomAttribute` takes). The
// `AttributeType` is held as a non-null `ITypePtr` and returned by reference (the
// `IVariable::Type()` concrete-impl pattern); the ctor asserts non-null, mirroring the C#
// `DefaultAttribute` ctor's `ArgumentNullException` on a null `attributeType`.
class TestAttribute : public IAttribute {
public:
    TestAttribute(TS::ITypePtr attributeType, const IMethod* constructor,
                  bool hasDecodeErrors,
                  std::vector<CustomAttributeTypedArgument> fixedArguments,
                  std::vector<CustomAttributeNamedArgument> namedArguments)
        : attributeType_(std::move(attributeType)), constructor_(constructor),
          hasDecodeErrors_(hasDecodeErrors),
          fixedArguments_(std::move(fixedArguments)),
          namedArguments_(std::move(namedArguments))
    {
        assert(attributeType_ != nullptr);
    }

    const IType& AttributeType() const override { return *attributeType_; }
    const IMethod* Constructor() const override { return constructor_; }
    bool HasDecodeErrors() const override { return hasDecodeErrors_; }
    std::vector<CustomAttributeTypedArgument> FixedArguments() const override { return fixedArguments_; }
    std::vector<CustomAttributeNamedArgument> NamedArguments() const override { return namedArguments_; }

private:
    TS::ITypePtr attributeType_;
    const IMethod* constructor_;
    bool hasDecodeErrors_;
    std::vector<CustomAttributeTypedArgument> fixedArguments_;
    std::vector<CustomAttributeNamedArgument> namedArguments_;
};

// Builds a shared `KnownType` for the `AttributeType` slot (the decompiler resolves the
// attribute type from the metadata; a `KnownType` is a cheap concrete `IType` for tests).
TS::ITypePtr MakeType(KnownTypeCode code)
{
    return std::make_shared<KnownType>(code);
}

} // namespace

// ---------------------------------------------------------------------------
// `AttributeType` returns the configured non-null `IType` by reference (the
// `IVariable::Type()` non-null owned-type-reference convention). The reference is valid for
// the attribute's lifetime (the attribute owns the `ITypePtr`).
// ---------------------------------------------------------------------------
TEST(IAttributeTest, AttributeTypeReturnsConfiguredType)
{
    auto type = MakeType(KnownTypeCode::String);
    TestAttribute attr(type, nullptr, false, {}, {});
    EXPECT_EQ(&attr.AttributeType(), type.get());
    EXPECT_EQ(attr.AttributeType().Name(), std::string("String"));
}

// ---------------------------------------------------------------------------
// `Constructor` returns the configured `IMethod*` when a matching constructor was found.
// ---------------------------------------------------------------------------
TEST(IAttributeTest, ConstructorReturnsConfiguredMethod)
{
    TestMethod ctor(7);
    auto type = MakeType(KnownTypeCode::Object);
    TestAttribute attr(type, &ctor, false, {}, {});
    EXPECT_EQ(attr.Constructor(), &ctor);
}

// ---------------------------------------------------------------------------
// `Constructor` returns null when no matching constructor was found (the C# "may return null
// if no matching constructor was found" contract). A null pointer is the C# `null`.
// ---------------------------------------------------------------------------
TEST(IAttributeTest, ConstructorReturnsNullWhenUnresolved)
{
    auto type = MakeType(KnownTypeCode::Object);
    TestAttribute attr(type, nullptr, false, {}, {});
    EXPECT_EQ(attr.Constructor(), nullptr);
}

// ---------------------------------------------------------------------------
// `HasDecodeErrors` returns the configured flag (true when the attribute blob could not be
// decoded; the decompiler emits such an attribute as a comment rather than an `Attribute`
// node).
// ---------------------------------------------------------------------------
TEST(IAttributeTest, HasDecodeErrorsReturnsConfiguredFlag)
{
    auto type = MakeType(KnownTypeCode::Object);
    TestAttribute ok(type, nullptr, false, {}, {});
    EXPECT_FALSE(ok.HasDecodeErrors());

    auto type2 = MakeType(KnownTypeCode::Object);
    TestAttribute bad(type2, nullptr, true, {}, {});
    EXPECT_TRUE(bad.HasDecodeErrors());
}

// ---------------------------------------------------------------------------
// `FixedArguments` returns the configured positional-argument snapshot by value (the D385
// value-struct convention: the snapshot owns its `CustomAttributeTypedArgument` values
// directly). Each positional argument carries its decoded `Type` (`ITypePtr`) and boxed
// `Value` (`std::any`).
// ---------------------------------------------------------------------------
TEST(IAttributeTest, FixedArgumentsReturnsConfiguredSnapshot)
{
    auto argType = MakeType(KnownTypeCode::Int32);
    std::vector<CustomAttributeTypedArgument> fixedArgs{
        CustomAttributeTypedArgument(argType, std::int32_t(42)),
        CustomAttributeTypedArgument(MakeType(KnownTypeCode::String), std::string("x"))
    };
    auto type = MakeType(KnownTypeCode::Object);
    TestAttribute attr(type, nullptr, false, fixedArgs, {});

    const auto result = attr.FixedArguments();
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].Type(), argType);
    EXPECT_EQ(std::any_cast<std::int32_t>(result[0].Value()), 42);
    EXPECT_EQ(std::any_cast<std::string>(result[1].Value()), std::string("x"));
}

// ---------------------------------------------------------------------------
// `NamedArguments` returns the configured named-argument snapshot by value (the D385
// convention). Each named argument carries the member `Name`, the field-vs-property `Kind`,
// the argument `Type`, and the boxed `Value`.
// ---------------------------------------------------------------------------
TEST(IAttributeTest, NamedArgumentsReturnsConfiguredSnapshot)
{
    auto argType = MakeType(KnownTypeCode::Int32);
    std::vector<CustomAttributeNamedArgument> namedArgs{
        CustomAttributeNamedArgument("Field", CustomAttributeNamedArgumentKind::Field,
            argType, std::int32_t(1)),
        CustomAttributeNamedArgument("Property", CustomAttributeNamedArgumentKind::Property,
            MakeType(KnownTypeCode::String), std::string("y"))
    };
    auto type = MakeType(KnownTypeCode::Object);
    TestAttribute attr(type, nullptr, false, {}, namedArgs);

    const auto result = attr.NamedArguments();
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].Name(), std::string("Field"));
    EXPECT_EQ(result[0].Kind(), CustomAttributeNamedArgumentKind::Field);
    EXPECT_EQ(std::any_cast<std::int32_t>(result[0].Value()), 1);
    EXPECT_EQ(result[1].Name(), std::string("Property"));
    EXPECT_EQ(result[1].Kind(), CustomAttributeNamedArgumentKind::Property);
    EXPECT_EQ(std::any_cast<std::string>(result[1].Value()), std::string("y"));
}

// ---------------------------------------------------------------------------
// A freshly-constructed attribute with no arguments returns empty `FixedArguments` and
// `NamedArguments` vectors (the `ImmutableArray<T>.Empty` default).
// ---------------------------------------------------------------------------
TEST(IAttributeTest, EmptyArgumentsByDefault)
{
    auto type = MakeType(KnownTypeCode::Object);
    TestAttribute attr(type, nullptr, false, {}, {});
    EXPECT_TRUE(attr.FixedArguments().empty());
    EXPECT_TRUE(attr.NamedArguments().empty());
}

// ---------------------------------------------------------------------------
// Every accessor dispatches polymorphically through an `IAttribute*` base pointer (the
// resolved-attribute interface is consumed through the base throughout the decompiler).
// ---------------------------------------------------------------------------
TEST(IAttributeTest, DispatchesPolymorphicallyThroughIAttributePointer)
{
    TestMethod ctor(3);
    auto argType = MakeType(KnownTypeCode::Int32);
    std::vector<CustomAttributeTypedArgument> fixedArgs{
        CustomAttributeTypedArgument(argType, std::int32_t(9))
    };
    std::vector<CustomAttributeNamedArgument> namedArgs{
        CustomAttributeNamedArgument("P", CustomAttributeNamedArgumentKind::Property,
            argType, std::int32_t(9))
    };
    auto type = MakeType(KnownTypeCode::String);
    TestAttribute concrete(type, &ctor, true, fixedArgs, namedArgs);
    const IAttribute* attr = &concrete;

    EXPECT_EQ(&attr->AttributeType(), type.get());
    EXPECT_EQ(attr->Constructor(), &ctor);
    EXPECT_TRUE(attr->HasDecodeErrors());
    EXPECT_EQ(attr->FixedArguments().size(), 1u);
    EXPECT_EQ(attr->NamedArguments().size(), 1u);
}

// ---------------------------------------------------------------------------
// `IAttribute` has a virtual destructor so a concrete attribute can be deleted through an
// `IAttribute*` (the resolved-attribute interface is held by pointer in the type system).
// ---------------------------------------------------------------------------
TEST(IAttributeTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<IAttribute>,
        "IAttribute must have a virtual destructor");
    static_assert(std::is_abstract_v<IAttribute>, "IAttribute must be abstract");
    static_assert(std::is_polymorphic_v<IAttribute>, "IAttribute must be polymorphic");

    auto type = MakeType(KnownTypeCode::Object);
    std::unique_ptr<IAttribute> attr = std::make_unique<TestAttribute>(
        type, nullptr, false, std::vector<CustomAttributeTypedArgument>{},
        std::vector<CustomAttributeNamedArgument>{});
    attr.reset(); // does not crash (virtual dtor)
    SUCCEED();
}
