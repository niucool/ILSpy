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

// Tests for the three BCL value-argument types a custom attribute carries, ported into the
// `TypeSystem` layer as the next leaf toward `IAttribute` (and through it `IMethod` /
// `IEvent` / `IField` / the member family / `TypeSystemAstBuilder` / `CSharpAmbience`):
//   - `CustomAttributeNamedArgumentKind` -- the byte tag distinguishing a named argument
//     that sets a field (0x53) from one that sets a property (0x54), ECMA-335 II.23.3.
//   - `CustomAttributeTypedArgument` -- one positional (fixed) argument: a decoded `IType`
//     and a boxed `std::any` value.
//   - `CustomAttributeNamedArgument` -- one named argument: the member name, the field-vs-
//     property tag, the decoded `IType`, and the boxed `std::any` value.
// The structs are the `<IType>` instantiations of the BCL
// `System.Reflection.Metadata.CustomAttributeTypedArgument<TType>` /
// `CustomAttributeNamedArgument<TType>` readonly structs, absorbed into the `TypeSystem`
// namespace (the D384 `MethodSemanticsAttributes` / D381 `IEntity.MetadataToken` BCL-
// absorption convention). The tests pin the enum literals, the struct ctors / accessors /
// defaults, the `std::any` boxing of every BCL `object?` value shape the decoder produces
// (primitive, string, null, a boxed nested typed argument, an array), and the decoder's
// construction pattern (`new CustomAttributeNamedArgument(name, kind, argument.Type,
// argument.Value)`).

#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::CustomAttributeNamedArgument;
using TS::CustomAttributeNamedArgumentKind;
using TS::CustomAttributeTypedArgument;
using TS::KnownType;

// Builds a shared `IType` for an argument's `Type` slot (the decoder fills this from the
// blob's type code; the test uses a known framework type).
static ILSpy::Decompiler::TypeSystem::ITypePtr MakeType()
{
	return std::make_shared<KnownType>(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
}

// ===========================================================================
// CustomAttributeNamedArgumentKind -- the byte tag (ECMA-335 II.23.3 literals).
// ===========================================================================
TEST(CustomAttributeNamedArgumentKindTest, ValuesMatchCSharpLiterals)
{
	EXPECT_EQ(static_cast<std::uint32_t>(CustomAttributeNamedArgumentKind::Field), 0x53u);
	EXPECT_EQ(static_cast<std::uint32_t>(CustomAttributeNamedArgumentKind::Property), 0x54u);
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgumentKind -- the `: byte` underlying type (the kind is a single
// serialization byte the metadata reader casts a raw byte to), and the two values are
// distinct (the decoder rejects anything that is neither).
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentKindTest, IsByteBackedAndValuesAreDistinct)
{
	static_assert(std::is_same_v<std::underlying_type_t<CustomAttributeNamedArgumentKind>,
		std::uint8_t>,
		"CustomAttributeNamedArgumentKind must be byte-backed (the serialization code is a "
		"single byte)");
	EXPECT_NE(CustomAttributeNamedArgumentKind::Field, CustomAttributeNamedArgumentKind::Property);
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgumentKind -- a switch on the kind dispatches to the field vs the
// property branch (the `TypeSystemAstBuilder.ConvertAttribute` /
// `CustomAttribute.MemberForNamedArgument` consumer pattern: the kind selects the field
// vs the property of the attribute type). The decoder rejects any other byte.
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentKindTest, SwitchDispatchesByKind)
{
	auto classify = [](CustomAttributeNamedArgumentKind k) -> const char* {
		switch (k) {
		case CustomAttributeNamedArgumentKind::Field: return "field";
		case CustomAttributeNamedArgumentKind::Property: return "property";
		}
		return "rejected";
	};
	EXPECT_STREQ(classify(CustomAttributeNamedArgumentKind::Field), "field");
	EXPECT_STREQ(classify(CustomAttributeNamedArgumentKind::Property), "property");
}

// ===========================================================================
// CustomAttributeTypedArgument -- the ctor stores the decoded type and the boxed value.
// ===========================================================================
TEST(CustomAttributeTypedArgumentTest, ConstructorStoresTypeAndValue)
{
	auto type = MakeType();
	CustomAttributeTypedArgument arg(type, std::any{std::int32_t{42}});
	EXPECT_EQ(arg.Type(), type);
	ASSERT_TRUE(arg.Value().has_value());
	EXPECT_EQ(std::any_cast<std::int32_t>(arg.Value()), 42);
}

// ---------------------------------------------------------------------------
// CustomAttributeTypedArgument -- the default ctor (the C# implicit parameterless struct
// ctor) gives a null type and an empty value, the decoder's zero-fill sentinel that
// `ImmutableArray.CreateBuilder<T>(count)` produces and the decoder overwrites before use.
// ---------------------------------------------------------------------------
TEST(CustomAttributeTypedArgumentTest, DefaultConstructorGivesNullTypeAndEmptyValue)
{
	CustomAttributeTypedArgument arg;
	EXPECT_EQ(arg.Type(), nullptr);
	EXPECT_FALSE(arg.Value().has_value());
}

// ---------------------------------------------------------------------------
// CustomAttributeTypedArgument -- the `Value` (the C# `object?`) boxes every primitive the
// decoder produces. The `std::any` holds the boxed value; `std::any_cast` retrieves it
// (the C# `object` cast). The decoder emits Boolean/Byte/Char/Double/Int16/Int32/Int64/...;
// the test exercises an int32 and a bool.
// ---------------------------------------------------------------------------
TEST(CustomAttributeTypedArgumentTest, ValueHoldsBoxedPrimitives)
{
	CustomAttributeTypedArgument intArg(MakeType(), std::any{std::int32_t{7}});
	ASSERT_TRUE(intArg.Value().has_value());
	EXPECT_EQ(std::any_cast<std::int32_t>(intArg.Value()), 7);

	CustomAttributeTypedArgument boolArg(MakeType(), std::any{true});
	ASSERT_TRUE(boolArg.Value().has_value());
	EXPECT_EQ(std::any_cast<bool>(boolArg.Value()), true);
}

// ---------------------------------------------------------------------------
// CustomAttributeTypedArgument -- the `Value` boxes a string (the decoder's
// `SerializationTypeCode.String` arm reads a serialized string into the boxed value).
// ---------------------------------------------------------------------------
TEST(CustomAttributeTypedArgumentTest, ValueHoldsString)
{
	CustomAttributeTypedArgument arg(MakeType(), std::any{std::string{"hello"}});
	ASSERT_TRUE(arg.Value().has_value());
	EXPECT_EQ(std::any_cast<std::string>(arg.Value()), "hello");
}

// ---------------------------------------------------------------------------
// CustomAttributeTypedArgument -- the boxed-value case: the decoder boxes a nested
// `CustomAttributeTypedArgument` as the `Value` of an outer argument
// (`new CustomAttributeTypedArgument<TType>(outer.Type, new
// CustomAttributeTypedArgument<TType>(info.Type, value))`, CustomAttributeDecoder.cs line
// 196). A `std::any` holds the nested struct; `std::any_cast` retrieves it.
// ---------------------------------------------------------------------------
TEST(CustomAttributeTypedArgumentTest, ValueHoldsBoxedNestedTypedArgument)
{
	auto innerType = MakeType();
	CustomAttributeTypedArgument inner(innerType, std::any{std::int32_t{99}});
	auto outerType = std::make_shared<KnownType>(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
	CustomAttributeTypedArgument outer(outerType, std::any{inner});
	ASSERT_TRUE(outer.Value().has_value());
	auto retrieved = std::any_cast<CustomAttributeTypedArgument>(outer.Value());
	EXPECT_EQ(retrieved.Type(), innerType);
	EXPECT_EQ(std::any_cast<std::int32_t>(retrieved.Value()), 99);
}

// ---------------------------------------------------------------------------
// CustomAttributeTypedArgument -- the C# `null` value (an argument with no value, e.g. a
// malformed blob) ports to an empty `std::any` (`has_value()` false), the
// `IVariable::GetConstantValue` D374 `std::any`-for-`object?` convention.
// ---------------------------------------------------------------------------
TEST(CustomAttributeTypedArgumentTest, NullValueIsRepresentedAsEmptyAny)
{
	CustomAttributeTypedArgument arg(MakeType(), std::any{});
	EXPECT_FALSE(arg.Value().has_value());
}

// ---------------------------------------------------------------------------
// CustomAttributeTypedArgument -- the array case: the decoder boxes an array of typed
// arguments as the `Value` (`DecodeArrayArgument` returns the array, line 196). A
// `std::any` holds a `std::vector<CustomAttributeTypedArgument>`; `std::any_cast` retrieves
// it and the element accessors work.
// ---------------------------------------------------------------------------
TEST(CustomAttributeTypedArgumentTest, ValueHoldsArrayOfTypedArguments)
{
	auto elemType = MakeType();
	std::vector<CustomAttributeTypedArgument> elements{
		CustomAttributeTypedArgument{elemType, std::any{std::int32_t{1}}},
		CustomAttributeTypedArgument{elemType, std::any{std::int32_t{2}}},
	};
	auto arrayType = std::make_shared<KnownType>(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
	CustomAttributeTypedArgument arg(arrayType, std::any{elements});
	ASSERT_TRUE(arg.Value().has_value());
	auto retrieved = std::any_cast<std::vector<CustomAttributeTypedArgument>>(arg.Value());
	ASSERT_EQ(retrieved.size(), 2u);
	EXPECT_EQ(std::any_cast<std::int32_t>(retrieved[0].Value()), 1);
	EXPECT_EQ(std::any_cast<std::int32_t>(retrieved[1].Value()), 2);
}

// ===========================================================================
// CustomAttributeNamedArgument -- the ctor stores all four members.
// ===========================================================================
TEST(CustomAttributeNamedArgumentTest, ConstructorStoresAllFourMembers)
{
	auto type = MakeType();
	CustomAttributeNamedArgument arg("Foo", CustomAttributeNamedArgumentKind::Field, type,
		std::any{std::int32_t{42}});
	EXPECT_EQ(arg.Name(), "Foo");
	EXPECT_EQ(arg.Kind(), CustomAttributeNamedArgumentKind::Field);
	EXPECT_EQ(arg.Type(), type);
	ASSERT_TRUE(arg.Value().has_value());
	EXPECT_EQ(std::any_cast<std::int32_t>(arg.Value()), 42);
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgument -- the default ctor (the C# implicit parameterless struct
// ctor) gives an empty name, `Field`, a null type, and an empty value (the decoder's
// zero-fill sentinel that `ImmutableArray.CreateBuilder<T>(count)` produces).
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentTest, DefaultConstructorGivesEmptyNameFieldKindNullTypeEmptyValue)
{
	CustomAttributeNamedArgument arg;
	EXPECT_EQ(arg.Name(), "");
	EXPECT_EQ(arg.Kind(), CustomAttributeNamedArgumentKind::Field);
	EXPECT_EQ(arg.Type(), nullptr);
	EXPECT_FALSE(arg.Value().has_value());
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgument -- both `Kind` values are stored (the decoder reads the
// kind byte and casts it to `Field` or `Property`; the consumer selects the field vs the
// property of the attribute type accordingly).
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentTest, BothKindsAreStored)
{
	auto type = MakeType();
	CustomAttributeNamedArgument fieldArg("F", CustomAttributeNamedArgumentKind::Field, type,
		std::any{std::int32_t{1}});
	CustomAttributeNamedArgument propArg("P", CustomAttributeNamedArgumentKind::Property, type,
		std::any{std::int32_t{2}});
	EXPECT_EQ(fieldArg.Kind(), CustomAttributeNamedArgumentKind::Field);
	EXPECT_EQ(propArg.Kind(), CustomAttributeNamedArgumentKind::Property);
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgument -- the C# `string? Name` (nullable -- the decoder reads it
// from `ReadSerializedString()`, which returns null for a malformed blob) ports to an empty
// `std::string` for the null case (the D357 `AttributeTarget` `IsNullOrEmpty`-to-`.empty()`
// convention; the consumer treats the name as non-null via `Name!`).
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentTest, EmptyNameRepresentsNullMalformedCase)
{
	auto type = MakeType();
	CustomAttributeNamedArgument arg("", CustomAttributeNamedArgumentKind::Property, type,
		std::any{});
	EXPECT_EQ(arg.Name(), "");
	EXPECT_FALSE(arg.Value().has_value());
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgument -- the decoder's construction pattern: the decoder builds a
// named argument from a decoded typed argument's `Type` and `Value`
// (`CustomAttributeNamedArgument<TType>(name, kind, argument.Type, argument.Value)`,
// CustomAttributeDecoder.cs line 53); the named argument reuses the typed argument's type
// and value (pointer-identity on the shared `ITypePtr`).
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentTest, DecoderConstructionPatternReusesTypedArgumentTypeAndValue)
{
	auto type = MakeType();
	CustomAttributeTypedArgument typed(type, std::any{std::int32_t{42}});
	CustomAttributeNamedArgument named("Count", CustomAttributeNamedArgumentKind::Property,
		typed.Type(), typed.Value());
	EXPECT_EQ(named.Name(), "Count");
	EXPECT_EQ(named.Kind(), CustomAttributeNamedArgumentKind::Property);
	EXPECT_EQ(named.Type(), type);
	ASSERT_TRUE(named.Value().has_value());
	EXPECT_EQ(std::any_cast<std::int32_t>(named.Value()), 42);
}

// ---------------------------------------------------------------------------
// CustomAttributeNamedArgument -- a `std::vector<CustomAttributeNamedArgument>` (the C#
// `ImmutableArray<CustomAttributeNamedArgument<IType>>` that `IAttribute.NamedArguments`
// returns) default-constructs its elements (the `ImmutableArray.CreateBuilder<T>(count)`
// zero-fill) and round-trips through the vector.
// ---------------------------------------------------------------------------
TEST(CustomAttributeNamedArgumentTest, VectorRoundTripsAndDefaultConstructs)
{
	auto type = MakeType();
	std::vector<CustomAttributeNamedArgument> args{
		CustomAttributeNamedArgument{"A", CustomAttributeNamedArgumentKind::Field, type,
			std::any{std::int32_t{1}}},
		CustomAttributeNamedArgument{},
		CustomAttributeNamedArgument{"B", CustomAttributeNamedArgumentKind::Property, type,
			std::any{std::int32_t{2}}},
	};
	ASSERT_EQ(args.size(), 3u);
	EXPECT_EQ(args[0].Name(), "A");
	EXPECT_EQ(args[1].Name(), "");
	EXPECT_EQ(args[1].Kind(), CustomAttributeNamedArgumentKind::Field);
	EXPECT_EQ(args[1].Type(), nullptr);
	EXPECT_EQ(args[2].Name(), "B");
	EXPECT_EQ(args[2].Kind(), CustomAttributeNamedArgumentKind::Property);
}
