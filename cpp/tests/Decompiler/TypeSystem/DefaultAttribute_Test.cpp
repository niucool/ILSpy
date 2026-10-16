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

// Tests for the DefaultAttribute port (ICSharpCode.Decompiler/TypeSystem/Implementation/
// DefaultAttribute.cs) -- the IAttribute implementation for already-resolved attributes.
// The suite pins: the ctor1 field storage (the attribute type, the positional / named
// argument snapshots), the HasDecodeErrors=false contract, the lazy Constructor
// resolution over AttributeType.GetConstructors (the parameter-type SequenceEqual match,
// the null for no matching candidate, and the one-scan CACHING across reads), and the
// ctor2 surface (the DeclaringType-derived attribute type, the SpecialType.UnknownType
// fallback for a null declaring type, the direct Constructor passthrough, and the
// positional-count ArgumentException mapped to std::invalid_argument with the exact
// message). The ctor null-guards are asserts (the D424 convention; documented, not
// death-tested -- the IAttribute_Test stub-ctor precedent).
//
// The lazy-Constructor tests need a type whose GetConstructors returns configured
// methods (the port's concrete KnownType/SimpleType inherit the empty default), so this
// file carries its own anonymous-namespace TestCtorType stub (the per-file stub
// convention); the method / parameter fixtures reuse the shared TestSupport::LookupMethod
// (LookupStubs.hpp, with this file's SetDeclaringType extension) and the port's real
// DefaultParameter (Implementation/DefaultParameter.hpp).

#include "Decompiler/TypeSystem/Implementation/DefaultAttribute.hpp"

#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ILSpy::Decompiler::TypeSystem;
namespace TS = ILSpy::Decompiler::TypeSystem;

// A minimal concrete IType whose GetConstructors returns the configured methods,
// applies the filter (the AbstractType/GetMembersHelper routing the C# performs), and
// counts the scans (the caching pin: the lazy Constructor must scan once, not per read).
class TestCtorType : public IType {
public:
    explicit TestCtorType(std::vector<const IMethod*> constructors)
        : constructors_(std::move(constructors))
    {
    }

    TypeKind Kind() const override { return TypeKind::Class; }
    std::string Name() const override { return "TestAttr"; }
    std::string ReflectionName() const override { return "TestNs.TestAttr"; }
    int TypeParameterCount() const override { return 0; }

    std::vector<const IMethod*> GetConstructors(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::IgnoreInheritedMembers) const override
    {
        (void)options;
        ++ctorScans_;
        std::vector<const IMethod*> result;
        for (const IMethod* m : constructors_) {
            if (!filter || filter(m))
                result.push_back(m);
        }
        return result;
    }

    int CtorScans() const { return ctorScans_; }

protected:
    bool StructuralEquals(const IType& other) const override
    {
        return this == &other;
    }

private:
    std::vector<const IMethod*> constructors_;
    mutable int ctorScans_ = 0;
};

// Builds a typed fixed argument (the C# `new CustomAttributeTypedArgument<IType>(
// type, value)`).
CustomAttributeTypedArgument Arg(ITypePtr type, std::string value)
{
    return CustomAttributeTypedArgument(std::move(type), std::any(std::move(value)));
}

// The shared compilation for the LookupMethod fixtures (LookupMethod keeps a
// compilation reference).
TestSupport::LookupCompilation& Compilation()
{
    static TestSupport::LookupCompilation compilation;
    return compilation;
}

} // namespace

// ---------------------------------------------------------------------------
// ctor1 (the already-resolved shape) stores the attribute type and the two argument
// snapshots; every accessor round-trips them; HasDecodeErrors is always false.
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, Ctor1StoresTypeAndArguments)
{
    auto attributeType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto argType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    std::vector<CustomAttributeTypedArgument> fixedArguments;
    fixedArguments.push_back(Arg(argType, "42"));
    fixedArguments.push_back(Arg(argType, "43"));
    std::vector<CustomAttributeNamedArgument> namedArguments;
    namedArguments.emplace_back("Name",
                                CustomAttributeNamedArgumentKind::Property,
                                argType, std::any(std::string("Value")));

    Implementation::DefaultAttribute attribute(attributeType, fixedArguments, namedArguments);

    EXPECT_EQ(attribute.AttributeType().ReflectionName(), "System.String");
    ASSERT_EQ(attribute.FixedArguments().size(), 2u);
    EXPECT_EQ(attribute.FixedArguments()[0].Type()->ReflectionName(), "System.Int32");
    EXPECT_EQ(std::any_cast<std::string>(attribute.FixedArguments()[0].Value()), "42");
    EXPECT_EQ(std::any_cast<std::string>(attribute.FixedArguments()[1].Value()), "43");
    ASSERT_EQ(attribute.NamedArguments().size(), 1u);
    EXPECT_EQ(attribute.NamedArguments()[0].Name(), "Name");
    EXPECT_EQ(attribute.NamedArguments()[0].Kind(), CustomAttributeNamedArgumentKind::Property);
    EXPECT_EQ(attribute.NamedArguments()[0].Type()->ReflectionName(), "System.Int32");
    EXPECT_EQ(std::any_cast<std::string>(attribute.NamedArguments()[0].Value()), "Value");
    EXPECT_FALSE(attribute.HasDecodeErrors());
}

// ---------------------------------------------------------------------------
// The lazy Constructor over a type with no constructors (the port's concrete KnownType
// inherits the IType empty default) resolves to null.
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, ConstructorIsNullWhenTypeHasNoConstructors)
{
    auto attributeType = std::make_shared<KnownType>(KnownTypeCode::String);

    Implementation::DefaultAttribute attribute(
        attributeType, std::vector<CustomAttributeTypedArgument>(),
        std::vector<CustomAttributeNamedArgument>());

    EXPECT_EQ(attribute.Constructor(), nullptr);
}

// ---------------------------------------------------------------------------
// The lazy Constructor finds the first candidate whose parameter types SequenceEqual the
// fixed-argument types, and CACHES the result: GetConstructors is consulted once across
// repeated reads (the C# `this.constructor = ctor` write-back).
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, ConstructorResolvesLazilyAndCaches)
{
    auto& compilation = Compilation();
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestSupport::LookupMethod method("TestAttr", compilation);
    auto parameter = std::make_unique<Implementation::DefaultParameter>(
        stringType, "value");
    method.SetParameters({parameter.get()});

    TestCtorType attributeType({&method});

    std::vector<CustomAttributeTypedArgument> fixedArguments;
    fixedArguments.push_back(Arg(stringType, "x"));

    Implementation::DefaultAttribute attribute(
        std::make_shared<TestCtorType>(std::move(attributeType)),
        std::move(fixedArguments), std::vector<CustomAttributeNamedArgument>());

    EXPECT_EQ(attribute.Constructor(), &method);
    EXPECT_EQ(attribute.Constructor(), &method);
    EXPECT_EQ(static_cast<const TestCtorType&>(attribute.AttributeType()).CtorScans(), 1);
}

// ---------------------------------------------------------------------------
// A candidate whose parameter TYPE differs from the fixed-argument type never matches:
// Constructor stays null (a null fixed-argument type is likewise a non-match).
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, ConstructorIsNullOnParameterTypeMismatch)
{
    auto& compilation = Compilation();
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestSupport::LookupMethod method("TestAttr", compilation);
    auto parameter = std::make_unique<Implementation::DefaultParameter>(
        intType, "value");
    method.SetParameters({parameter.get()});

    TestCtorType attributeType({&method});

    std::vector<CustomAttributeTypedArgument> fixedArguments;
    fixedArguments.push_back(Arg(stringType, "x"));

    Implementation::DefaultAttribute attribute(
        std::make_shared<TestCtorType>(std::move(attributeType)),
        std::move(fixedArguments), std::vector<CustomAttributeNamedArgument>());

    EXPECT_EQ(attribute.Constructor(), nullptr);
}

// ---------------------------------------------------------------------------
// The count filter (`m.Parameters.Count == FixedArguments.Length`) excludes candidates
// with a different parameter count: Constructor stays null.
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, ConstructorIsNullOnParameterCountMismatch)
{
    auto& compilation = Compilation();
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestSupport::LookupMethod method("TestAttr", compilation);
    auto p0 = std::make_unique<Implementation::DefaultParameter>(stringType, "a");
    auto p1 = std::make_unique<Implementation::DefaultParameter>(stringType, "b");
    method.SetParameters({p0.get(), p1.get()});

    TestCtorType attributeType({&method});

    std::vector<CustomAttributeTypedArgument> fixedArguments;
    fixedArguments.push_back(Arg(stringType, "x"));

    Implementation::DefaultAttribute attribute(
        std::make_shared<TestCtorType>(std::move(attributeType)),
        std::move(fixedArguments), std::vector<CustomAttributeNamedArgument>());

    EXPECT_EQ(attribute.Constructor(), nullptr);
}

// ---------------------------------------------------------------------------
// ctor2 derives the attribute type from the constructor's DECLARING type and returns the
// constructor directly (no lazy scan).
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, Ctor2DerivesAttributeTypeFromDeclaringType)
{
    auto& compilation = Compilation();
    auto declaringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestSupport::LookupMethod method("Ctor", compilation);
    method.SetDeclaringType(declaringType);

    Implementation::DefaultAttribute attribute(
        &method, std::vector<CustomAttributeTypedArgument>(),
        std::vector<CustomAttributeNamedArgument>());

    EXPECT_EQ(attribute.AttributeType().ReflectionName(), "System.String");
    EXPECT_EQ(attribute.Constructor(), &method);
}

// ---------------------------------------------------------------------------
// ctor2 falls back to the SpecialType.UnknownType null object for a constructor without
// a declaring type (the C# `constructor.DeclaringType ?? SpecialType.UnknownType`).
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, Ctor2FallsBackToUnknownTypeForNullDeclaringType)
{
    auto& compilation = Compilation();
    TestSupport::LookupMethod method("Ctor", compilation);

    Implementation::DefaultAttribute attribute(
        &method, std::vector<CustomAttributeTypedArgument>(),
        std::vector<CustomAttributeNamedArgument>());

    EXPECT_EQ(attribute.AttributeType().ReflectionName(), "?");
}

// ---------------------------------------------------------------------------
// ctor2 validates the positional-argument count against the constructor's parameter
// count: the ArgumentException maps to std::invalid_argument carrying the exact message.
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, Ctor2ThrowsOnPositionalArgumentCountMismatch)
{
    auto& compilation = Compilation();
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestSupport::LookupMethod method("Ctor", compilation);
    auto parameter = std::make_unique<Implementation::DefaultParameter>(
        stringType, "value");
    method.SetParameters({parameter.get()});

    std::vector<CustomAttributeTypedArgument> fixedArguments;
    fixedArguments.push_back(Arg(stringType, "x"));
    fixedArguments.push_back(Arg(stringType, "y"));

    try {
        Implementation::DefaultAttribute attribute(
            &method, std::move(fixedArguments),
            std::vector<CustomAttributeNamedArgument>());
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
                     "Positional argument count must match the constructor's parameter count");
    }
}

// ---------------------------------------------------------------------------
// The class is a polymorphic IAttribute with a virtual destructor and is not final
// (the C# is unsealed).
// ---------------------------------------------------------------------------
TEST(DefaultAttributeTest, PolymorphicDispatchAndTypeTraits)
{
    static_assert(!std::is_final_v<Implementation::DefaultAttribute>,
                  "the C# DefaultAttribute is unsealed");
    static_assert(std::is_polymorphic_v<Implementation::DefaultAttribute>,
                  "IAttribute is a polymorphic interface");
    static_assert(std::has_virtual_destructor_v<Implementation::DefaultAttribute>,
                  "the IAttribute base has a virtual destructor");

    auto attributeType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto attribute = std::make_unique<Implementation::DefaultAttribute>(
        attributeType, std::vector<CustomAttributeTypedArgument>(),
        std::vector<CustomAttributeNamedArgument>());

    const IAttribute* asInterface = attribute.get();
    EXPECT_EQ(asInterface->AttributeType().ReflectionName(), "System.String");
    EXPECT_EQ(asInterface->Constructor(), nullptr);
    EXPECT_FALSE(asInterface->HasDecodeErrors());
    EXPECT_TRUE(asInterface->FixedArguments().empty());
    EXPECT_TRUE(asInterface->NamedArguments().empty());
}
