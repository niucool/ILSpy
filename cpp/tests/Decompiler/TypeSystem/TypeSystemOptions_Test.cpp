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

// Tests for `TypeSystemOptions` (cpp/Decompiler/TypeSystem/TypeSystemOptions.hpp, the D376
// port of the `[Flags] TypeSystemOptions` enum from
// ICSharpCode.Decompiler/TypeSystem/DecompilerTypeSystem.cs). `TypeSystemOptions` controls
// how metadata is represented in the type system (dynamic/tuple/extension-method/nint/
// function-pointer/ref-readonly/params materialization, public-API-only loading, caching,
// ...); `DecompilerTypeSystem.GetOptions` builds the mask from a `DecompilerSettings`, and
// `ICompilation.TypeSystemOptions` (the next `TypeSystem` leaf toward `ICompilationProvider`
// -> `IEntity` -> `TypeSystemAstBuilder` -> `CSharpAmbience`) exposes it to every type-system
// consumer. The tests pin the enum values (matching the C# literals, the `Default`
// composite, the `[Flags]` bitwise operators) and the flag-test idiom the type-system
// consumers use.

#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TypeSystemOptions;

// ---------------------------------------------------------------------------
// TypeSystemOptions -- each individual flag matches its C# literal value.
// ---------------------------------------------------------------------------
TEST(TypeSystemOptionsTest, IndividualFlagsMatchCSharpLiterals)
{
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::None), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::Dynamic), 0x1u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::Tuple), 0x2u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::ExtensionMethods), 0x4u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::OnlyPublicAPI), 0x8u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::Uncached), 0x10u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::DecimalConstants), 0x20u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::KeepModifiers), 0x40u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::ReadOnlyStructsAndParameters), 0x80u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::RefStructs), 0x100u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::UnmanagedConstraints), 0x200u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::NullabilityAnnotations), 0x400u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::ReadOnlyMethods), 0x800u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::NativeIntegers), 0x1000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::FunctionPointers), 0x2000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::ScopedRef), 0x4000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::NativeIntegersWithoutAttribute), 0x8000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::RefReadOnlyParameters), 0x10000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::ParamsCollections), 0x20000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::FirstClassSpanTypes), 0x40000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::ExtensionMembers), 0x80000u);
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::RuntimeAsync), 0x100000u);
}

// ---------------------------------------------------------------------------
// TypeSystemOptions -- `Default` is the bitwise OR of its 19 constituents (the C#
// `Dynamic | Tuple | ExtensionMethods | ... | RuntimeAsync` composite). It does NOT include
// `OnlyPublicAPI` (0x8), `Uncached` (0x10), or `KeepModifiers` (0x40).
// ---------------------------------------------------------------------------
TEST(TypeSystemOptionsTest, DefaultIsBitwiseOrOfConstituents)
{
    const TypeSystemOptions expected =
        TypeSystemOptions::Dynamic |
        TypeSystemOptions::Tuple |
        TypeSystemOptions::ExtensionMethods |
        TypeSystemOptions::DecimalConstants |
        TypeSystemOptions::ReadOnlyStructsAndParameters |
        TypeSystemOptions::RefStructs |
        TypeSystemOptions::UnmanagedConstraints |
        TypeSystemOptions::NullabilityAnnotations |
        TypeSystemOptions::ReadOnlyMethods |
        TypeSystemOptions::NativeIntegers |
        TypeSystemOptions::FunctionPointers |
        TypeSystemOptions::ScopedRef |
        TypeSystemOptions::NativeIntegersWithoutAttribute |
        TypeSystemOptions::RefReadOnlyParameters |
        TypeSystemOptions::ParamsCollections |
        TypeSystemOptions::FirstClassSpanTypes |
        TypeSystemOptions::ExtensionMembers |
        TypeSystemOptions::RuntimeAsync;
    EXPECT_EQ(TypeSystemOptions::Default, expected);
    // The C# computed value (0x1FFFFA7), pinned independently of the `|` form.
    EXPECT_EQ(static_cast<std::uint32_t>(TypeSystemOptions::Default), 0x1FFFA7u);
}

// ---------------------------------------------------------------------------
// TypeSystemOptions -- `Default` deliberately omits the three non-default flags.
// ---------------------------------------------------------------------------
TEST(TypeSystemOptionsTest, DefaultOmitsOnlyPublicAPIUncachedKeepModifiers)
{
    EXPECT_EQ(TypeSystemOptions::Default & TypeSystemOptions::OnlyPublicAPI,
              TypeSystemOptions::None);
    EXPECT_EQ(TypeSystemOptions::Default & TypeSystemOptions::Uncached,
              TypeSystemOptions::None);
    EXPECT_EQ(TypeSystemOptions::Default & TypeSystemOptions::KeepModifiers,
              TypeSystemOptions::None);
}

// ---------------------------------------------------------------------------
// TypeSystemOptions -- the `[Flags]` bitwise operators (the C# `[Flags]` compiler generates
// them implicitly; the C++ `enum class` defines them as free functions).
// ---------------------------------------------------------------------------
TEST(TypeSystemOptionsTest, BitwiseOrCombinesFlags)
{
    auto combined = TypeSystemOptions::Dynamic | TypeSystemOptions::Tuple;
    EXPECT_EQ(combined & TypeSystemOptions::Dynamic, TypeSystemOptions::Dynamic);
    EXPECT_EQ(combined & TypeSystemOptions::Tuple, TypeSystemOptions::Tuple);
    EXPECT_EQ(combined & TypeSystemOptions::ExtensionMethods, TypeSystemOptions::None);
}

TEST(TypeSystemOptionsTest, BitwiseAndIntersectsFlags)
{
    auto a = TypeSystemOptions::Dynamic | TypeSystemOptions::Tuple;
    auto b = TypeSystemOptions::Tuple | TypeSystemOptions::ExtensionMethods;
    EXPECT_EQ(a & b, TypeSystemOptions::Tuple);
}

TEST(TypeSystemOptionsTest, BitwiseXorTogglesFlags)
{
    auto a = TypeSystemOptions::Dynamic | TypeSystemOptions::Tuple;
    auto toggled = a ^ TypeSystemOptions::Tuple;
    EXPECT_EQ(toggled, TypeSystemOptions::Dynamic);
}

TEST(TypeSystemOptionsTest, BitwiseNotInvertsFlags)
{
    EXPECT_EQ(~TypeSystemOptions::None,
              static_cast<TypeSystemOptions>(0xFFFFFFFFu));
}

// ---------------------------------------------------------------------------
// TypeSystemOptions -- the `(options & flag) == flag` flag-test idiom
// `DecompilerTypeSystem.GetOptions` and the type-system consumers use.
// ---------------------------------------------------------------------------
TEST(TypeSystemOptionsTest, FlagTestIdiomMatchesTypeSystemUsage)
{
    auto options = TypeSystemOptions::Default;
    // Dynamic IS a constituent of Default, so the flag-test idiom is true.
    EXPECT_TRUE((options & TypeSystemOptions::Dynamic) == TypeSystemOptions::Dynamic);
    EXPECT_TRUE((options & TypeSystemOptions::FunctionPointers) ==
                TypeSystemOptions::FunctionPointers);
    EXPECT_TRUE((options & TypeSystemOptions::RuntimeAsync) ==
                TypeSystemOptions::RuntimeAsync);
    // A flag NOT in Default (OnlyPublicAPI), so the idiom is false.
    EXPECT_FALSE((options & TypeSystemOptions::OnlyPublicAPI) ==
                 TypeSystemOptions::OnlyPublicAPI);
    EXPECT_FALSE((options & TypeSystemOptions::KeepModifiers) ==
                 TypeSystemOptions::KeepModifiers);
}
