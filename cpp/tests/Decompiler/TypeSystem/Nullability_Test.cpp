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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Nullability` (cpp/Decompiler/TypeSystem/Nullability.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/Nullability.cs). `Nullability` is the
// three-state nullable-reference annotation and a leaf dependency of
// `TypeSystemAstBuilder` (the direct `CSharpAmbience` dependency), which
// compares `type.Nullability` / `tp.NullabilityConstraint` against the named
// values to decide whether to append a `?` and to emit nullability
// disambiguating constraints. The tests pin the enum's byte backing, its three
// values in C# declaration order (the consumers switch on the named values, and
// ITypeDefinition / ITypeParameter default-construct to Oblivious=0), and the
// equality-comparison pattern the consumers actually use.

#include "Decompiler/TypeSystem/Nullability.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// The enum is `: byte` with three members in declaration order. The values are
// pinned to 0/1/2 explicitly: ITypeDefinition::NullableContext and
// ITypeParameter::NullabilityConstraint default-construct to Oblivious (the
// value-zero), and the TypeSystemAstBuilder consumers switch on the named
// values, so reordering would silently change the default-constructed state.
// ---------------------------------------------------------------------------
TEST(NullabilityTest, EnumValuesMatchCSharpDeclarationOrder)
{
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Nullability::Oblivious), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Nullability::NotNullable), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::Nullability::Nullable), 2);
}

// ---------------------------------------------------------------------------
// The C# `: byte` backing ports to std::uint8_t. The enum is exactly one byte,
// the size its use as a default-constructible field on ITypeDefinition /
// ITypeParameter (and the value-zero default) depends on.
// ---------------------------------------------------------------------------
TEST(NullabilityTest, IsByteBacked)
{
    static_assert(sizeof(TS::Nullability) == 1, "Nullability must be byte-backed");
    static_assert(static_cast<std::uint8_t>(TS::Nullability{}) == 0,
                  "value-initialized Nullability must be Oblivious (the zero)");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// The TypeSystemAstBuilder consumers compare for equality against the named
// values (e.g. `type.Nullability == Nullability.Nullable` to append a `?`,
// `tp.NullabilityConstraint != Nullability.NotNullable` to gate a constraint).
// The three values are distinct, so every equality comparison resolves to the
// expected arm.
// ---------------------------------------------------------------------------
TEST(NullabilityTest, EqualityComparisonMatchesConsumerPattern)
{
    EXPECT_TRUE(TS::Nullability::Nullable == TS::Nullability::Nullable);
    EXPECT_FALSE(TS::Nullability::Nullable == TS::Nullability::Oblivious);
    EXPECT_FALSE(TS::Nullability::Nullable == TS::Nullability::NotNullable);
    EXPECT_TRUE(TS::Nullability::NotNullable != TS::Nullability::Nullable);
    EXPECT_TRUE(TS::Nullability::Oblivious != TS::Nullability::NotNullable);
    EXPECT_FALSE(TS::Nullability::Oblivious != TS::Nullability::Oblivious);
}

// ---------------------------------------------------------------------------
// The three states are distinguishable: a switch over the enum (the
// TypeSystemAstBuilder `type.Nullability == Nullability.Nullable` /
// `tp.NullabilityConstraint == Nullability.NotNullable` pattern) reaches a
// distinct arm for each value.
// ---------------------------------------------------------------------------
TEST(NullabilityTest, ThreeStatesAreDistinguishable)
{
    for (auto value : {TS::Nullability::Oblivious, TS::Nullability::NotNullable, TS::Nullability::Nullable}) {
        int arm = -1;
        switch (value) {
            case TS::Nullability::Oblivious: arm = 0; break;
            case TS::Nullability::NotNullable: arm = 1; break;
            case TS::Nullability::Nullable: arm = 2; break;
        }
        EXPECT_EQ(arm, static_cast<int>(static_cast<std::uint8_t>(value)));
    }
}
