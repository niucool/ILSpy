// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for `MethodSemanticsAttributes` (cpp/Decompiler/TypeSystem/
// MethodSemanticsAttributes.hpp, the port of the BCL `[Flags]
// System.Reflection.MethodSemanticsAttributes` enum, the ECMA-335 II.22.15
// MethodSemantics-table flags that `IMethod.AccessorKind` returns). The flags name the role a
// method plays as a property/event accessor (`Setter`/`Getter`/`Adder`/`Remover`/`Raiser`/
// `Other`); `None` marks a plain method. The tests pin the enum values (matching the ECMA-335 /
// BCL literals), the `[Flags]` structural invariant (every non-`None` flag is a single bit), the
// `[Flags]` bitwise operators, and the consumer patterns: the `MethodSemanticsLookup`
// `csharpAccessors = Getter | Setter | Adder | Remover` composite + `(filter & Other) != 0` /
// `(semantics & filter) == 0` bit tests, and the IL-transform `method.AccessorKind == Getter`
// accessor-detection equality.

#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::MethodSemanticsAttributes;

// ---------------------------------------------------------------------------
// MethodSemanticsAttributes -- each member matches its ECMA-335 / BCL literal value.
// ---------------------------------------------------------------------------
TEST(MethodSemanticsAttributesTest, IndividualFlagsMatchCSharpLiterals)
{
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::None), 0u);
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::Setter), 0x1u);
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::Getter), 0x2u);
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::Other), 0x4u);
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::Adder), 0x8u);
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::Remover), 0x10u);
	EXPECT_EQ(static_cast<std::uint32_t>(MethodSemanticsAttributes::Raiser), 0x20u);
}

// ---------------------------------------------------------------------------
// MethodSemanticsAttributes -- a `[Flags]` enum: every non-`None` member is a single bit (the
// `flag & (flag - 1) == 0` power-of-two test), so the members combine without overlap. The
// metadata reader stores the raw ECMA flags, so this invariant is load-bearing for the
// accessor->association mapping.
// ---------------------------------------------------------------------------
TEST(MethodSemanticsAttributesTest, AllNonNoneFlagsArePowersOfTwo)
{
	auto isPowerOfTwo = [](std::uint32_t v) {
		return v != 0 && (v & (v - 1)) == 0;
	};
	EXPECT_TRUE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::Setter)));
	EXPECT_TRUE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::Getter)));
	EXPECT_TRUE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::Other)));
	EXPECT_TRUE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::Adder)));
	EXPECT_TRUE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::Remover)));
	EXPECT_TRUE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::Raiser)));
	// `None` is the zero value, NOT a power of two (it is the empty mask).
	EXPECT_FALSE(isPowerOfTwo(static_cast<std::uint32_t>(MethodSemanticsAttributes::None)));
}

// ---------------------------------------------------------------------------
// MethodSemanticsAttributes -- the `csharpAccessors = Getter | Setter | Adder | Remover`
// composite (MethodSemanticsLookup.cs line 35), the mask of accessor semantics the decompiler
// models. It deliberately OMITS `Other` and `Raiser` (the `Other` accessors are not exposed by
// the metadata reader; `Raiser` is recorded but not mapped to an AST `AccessorKind`).
// ---------------------------------------------------------------------------
TEST(MethodSemanticsAttributesTest, CsharpAccessorsCompositeOmitsOtherAndRaiser)
{
	const MethodSemanticsAttributes csharpAccessors =
		MethodSemanticsAttributes::Getter |
		MethodSemanticsAttributes::Setter |
		MethodSemanticsAttributes::Adder |
		MethodSemanticsAttributes::Remover;
	// The four modeled accessors are present.
	EXPECT_EQ(csharpAccessors & MethodSemanticsAttributes::Getter,
			  MethodSemanticsAttributes::Getter);
	EXPECT_EQ(csharpAccessors & MethodSemanticsAttributes::Setter,
			  MethodSemanticsAttributes::Setter);
	EXPECT_EQ(csharpAccessors & MethodSemanticsAttributes::Adder,
			  MethodSemanticsAttributes::Adder);
	EXPECT_EQ(csharpAccessors & MethodSemanticsAttributes::Remover,
			  MethodSemanticsAttributes::Remover);
	// `Other` and `Raiser` are deliberately absent (the bit test yields `None`).
	EXPECT_EQ(csharpAccessors & MethodSemanticsAttributes::Other,
			  MethodSemanticsAttributes::None);
	EXPECT_EQ(csharpAccessors & MethodSemanticsAttributes::Raiser,
			  MethodSemanticsAttributes::None);
	// The composite value (0x1B), pinned independently of the `|` form.
	EXPECT_EQ(static_cast<std::uint32_t>(csharpAccessors), 0x1Bu);
}

// ---------------------------------------------------------------------------
// MethodSemanticsAttributes -- the `[Flags]` bitwise operators (the C# `[Flags]` compiler
// generates them implicitly; the C++ `enum class` defines them as free functions).
// ---------------------------------------------------------------------------
TEST(MethodSemanticsAttributesTest, BitwiseOrCombinesFlags)
{
	auto combined = MethodSemanticsAttributes::Getter | MethodSemanticsAttributes::Setter;
	EXPECT_EQ(combined & MethodSemanticsAttributes::Getter, MethodSemanticsAttributes::Getter);
	EXPECT_EQ(combined & MethodSemanticsAttributes::Setter, MethodSemanticsAttributes::Setter);
	EXPECT_EQ(combined & MethodSemanticsAttributes::Adder, MethodSemanticsAttributes::None);
}

TEST(MethodSemanticsAttributesTest, BitwiseAndIntersectsFlags)
{
	auto a = MethodSemanticsAttributes::Getter | MethodSemanticsAttributes::Setter;
	auto b = MethodSemanticsAttributes::Setter | MethodSemanticsAttributes::Adder;
	EXPECT_EQ(a & b, MethodSemanticsAttributes::Setter);
}

TEST(MethodSemanticsAttributesTest, BitwiseXorTogglesFlags)
{
	auto a = MethodSemanticsAttributes::Getter | MethodSemanticsAttributes::Setter;
	auto toggled = a ^ MethodSemanticsAttributes::Setter;
	EXPECT_EQ(toggled, MethodSemanticsAttributes::Getter);
}

TEST(MethodSemanticsAttributesTest, BitwiseNotInvertsFlags)
{
	EXPECT_EQ(~MethodSemanticsAttributes::None,
			  static_cast<MethodSemanticsAttributes>(0xFFFFFFFFu));
}

// ---------------------------------------------------------------------------
// MethodSemanticsAttributes -- the consumer patterns. `MethodSemanticsLookup` ctor uses the
// `(filter & Other) != 0` gate (it throws if `Other` is requested, since the metadata reader
// does not expose `Other` accessors) and the `(semantics & filter) == 0` skip (record an accessor
// only if its semantics intersects the filter). The IL transforms use the
// `method.AccessorKind == Getter`/`Setter`/... equality to detect accessors.
// ---------------------------------------------------------------------------
TEST(MethodSemanticsAttributesTest, FlagTestIdiomMatchesMethodSemanticsUsage)
{
	// The `(filter & Other) != 0` gate: a filter WITHOUT `Other` (the `csharpAccessors`
	// composite) does not trip the gate; a filter WITH `Other` does.
	const MethodSemanticsAttributes csharpAccessors =
		MethodSemanticsAttributes::Getter |
		MethodSemanticsAttributes::Setter |
		MethodSemanticsAttributes::Adder |
		MethodSemanticsAttributes::Remover;
	const MethodSemanticsAttributes filterWithOther =
		csharpAccessors | MethodSemanticsAttributes::Other;
	EXPECT_FALSE((csharpAccessors & MethodSemanticsAttributes::Other) !=
				 MethodSemanticsAttributes::None);
	EXPECT_TRUE((filterWithOther & MethodSemanticsAttributes::Other) !=
				MethodSemanticsAttributes::None);

	// The `(semantics & filter) == 0` skip: a `Getter` accessor is recorded against the
	// `csharpAccessors` filter (the intersection is non-empty); an `Other` accessor is NOT
	// (the intersection is empty -- `Other` is outside the modeled set).
	EXPECT_FALSE((MethodSemanticsAttributes::Getter & csharpAccessors) ==
				 MethodSemanticsAttributes::None);
	EXPECT_TRUE((MethodSemanticsAttributes::Other & csharpAccessors) ==
				MethodSemanticsAttributes::None);

	// The IL-transform accessor-detection equality: a method's `AccessorKind` is compared
	// to a single named flag (never a combined mask) to detect its role.
	EXPECT_EQ(MethodSemanticsAttributes::Getter, MethodSemanticsAttributes::Getter);
	EXPECT_NE(MethodSemanticsAttributes::Getter, MethodSemanticsAttributes::Setter);
	EXPECT_NE(MethodSemanticsAttributes::Adder, MethodSemanticsAttributes::Remover);
	EXPECT_NE(MethodSemanticsAttributes::Raiser, MethodSemanticsAttributes::None);
}
