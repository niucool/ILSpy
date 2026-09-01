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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for MetadataGenericContext (Metadata/MetadataGenericContext.cs, 96
// lines) and the two token-level MetadataFile reads it consumes
// (GetGenericParameters -- the SRM TypeDefinition/MethodDefinition
// GetGenericParameters analog -- and GetMethodDeclaringTypeToken -- the SRM
// MethodDefinition.GetDeclaringType analog). Fixture facts verified against
// the .NET Framework 4 mscorlib with the BCL MetadataReader:
//   - System.Collections.Generic.List`1 declares one type parameter "T"
//   - System.Collections.Generic.Dictionary`2 declares "TKey" (0) / "TValue" (1)
//   - System.Tuple.Create<T1>(T1) declares one method type parameter "T1"
//   - System.String / String.IsNullOrEmpty declare no generic parameters

#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using ILSpy::Decompiler::Metadata::GenericParameterInfo;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::MetadataGenericContext;

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindMethodToken(MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

std::uint32_t FindFieldToken(MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& fld : f.GetFields(typeToken)) {
        if (fld.Name == name) return fld.Token;
    }
    return 0;
}

// Find a method by name and generic-arity (the MethodDef signature's
// GenericParameterCount), so an overloaded generic method (Tuple.Create has
// eight overloads, one per arity) resolves deterministically.
std::uint32_t FindGenericMethod(MetadataFile& f, std::uint32_t typeToken,
                                std::string_view name, std::uint32_t gpCount) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name != name) continue;
        auto sig = f.GetMethodSignature(m.Token);
        if (sig && sig->GenericParameterCount == gpCount) return m.Token;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// MetadataFile::GetGenericParameters (the SRM row-method analog)
// ---------------------------------------------------------------------------

TEST(MetadataFileGenericParameters, TypeOwnedRowsForSingleParameterType) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto listTok = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listTok, 0u);
    auto rows = f.GetGenericParameters(listTok);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].Number, 0u);
    EXPECT_EQ(rows[0].Name, "T");
    // The token is a table-0x2A token with a non-nil row.
    EXPECT_EQ(rows[0].Token >> 24, 0x2Au);
    EXPECT_NE(rows[0].Token & 0x00FFFFFFu, 0u);
}

TEST(MetadataFileGenericParameters, TypeOwnedRowsInTableOrderForTwoParameters) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto dictTok = FindType(f, "System.Collections.Generic", "Dictionary`2");
    ASSERT_NE(dictTok, 0u);
    auto rows = f.GetGenericParameters(dictTok);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].Number, 0u);
    EXPECT_EQ(rows[0].Name, "TKey");
    EXPECT_EQ(rows[1].Number, 1u);
    EXPECT_EQ(rows[1].Name, "TValue");
    EXPECT_NE(rows[0].Token, rows[1].Token);
}

TEST(MetadataFileGenericParameters, MethodOwnedRows) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto tupleTok = FindType(f, "System", "Tuple");
    ASSERT_NE(tupleTok, 0u);
    auto createTok = FindGenericMethod(f, tupleTok, "Create", 1);
    ASSERT_NE(createTok, 0u);
    auto rows = f.GetGenericParameters(createTok);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].Number, 0u);
    EXPECT_EQ(rows[0].Name, "T1");
    EXPECT_EQ(rows[0].Token >> 24, 0x2Au);
}

TEST(MetadataFileGenericParameters, NonGenericTypeAndMethodHaveNoRows) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    EXPECT_TRUE(f.GetGenericParameters(stringTok).empty());
    auto isneTok = FindMethodToken(f, stringTok, "IsNullOrEmpty");
    ASSERT_NE(isneTok, 0u);
    EXPECT_TRUE(f.GetGenericParameters(isneTok).empty());
}

TEST(MetadataFileGenericParameters, WrongTableOrOutOfRangeYieldEmpty) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    // A FieldDef token: the owner of a GenericParam row is a TypeOrMethodDef.
    auto emptyTok = FindFieldToken(f, stringTok, "Empty");
    ASSERT_NE(emptyTok, 0u);
    EXPECT_TRUE(f.GetGenericParameters(emptyTok).empty());
    // A GenericParam token itself is not a valid owner.
    EXPECT_TRUE(f.GetGenericParameters(0x2A000001u).empty());
    // Out-of-range rows (mscorlib's tables hold thousands of rows, not 16M).
    EXPECT_TRUE(f.GetGenericParameters(0x02000000u | 0x00FFFFFFu).empty());
    EXPECT_TRUE(f.GetGenericParameters(0x06000000u | 0x00FFFFFFu).empty());
    // Nil tokens.
    EXPECT_TRUE(f.GetGenericParameters(0u).empty());
    EXPECT_TRUE(f.GetGenericParameters(0x02000000u).empty());
}

TEST(MetadataFileGenericParameters, InvalidFileYieldsEmpty) {
    MetadataFile f("this-is-not-an-assembly.txt");
    ASSERT_FALSE(f.IsValid());
    EXPECT_TRUE(f.GetGenericParameters(0x02000001u).empty());
    EXPECT_TRUE(f.GetGenericParameters(0x06000001u).empty());
}

// ---------------------------------------------------------------------------
// MetadataFile::GetMethodDeclaringTypeToken (the SRM row-method analog)
// ---------------------------------------------------------------------------

TEST(MetadataFileDeclaringType, ResolvesDeclaringTypeOfMethod) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto listTok = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listTok, 0u);
    auto addTok = FindMethodToken(f, listTok, "Add");
    ASSERT_NE(addTok, 0u);
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(addTok), listTok);
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    auto isneTok = FindMethodToken(f, stringTok, "IsNullOrEmpty");
    ASSERT_NE(isneTok, 0u);
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(isneTok), stringTok);
}

TEST(MetadataFileDeclaringType, WrongTableOrOutOfRangeYieldZero) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    // A TypeDef token is not a MethodDef token.
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(stringTok), 0u);
    // A FieldDef token likewise.
    auto emptyTok = FindFieldToken(f, stringTok, "Empty");
    ASSERT_NE(emptyTok, 0u);
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(emptyTok), 0u);
    // Out-of-range and nil rows.
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(0x06000000u | 0x00FFFFFFu), 0u);
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(0u), 0u);
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(0x06000000u), 0u);
    // An invalid module.
    MetadataFile bad("this-is-not-an-assembly.txt");
    ASSERT_FALSE(bad.IsValid());
    EXPECT_EQ(bad.GetMethodDeclaringTypeToken(0x06000001u), 0u);
}

// ---------------------------------------------------------------------------
// MetadataGenericContext (the C# struct port)
// ---------------------------------------------------------------------------

TEST(MetadataGenericContextTest, TypeContextResolvesTypeParameterName) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto listTok = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listTok, 0u);
    auto ctx = MetadataGenericContext::ForType(listTok, f);
    EXPECT_EQ(ctx.GetGenericTypeParameterName(0), "T");
    // The handle is the positional row's token.
    auto rows = f.GetGenericParameters(listTok);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(ctx.GetGenericTypeParameterHandleOrNull(0), rows[0].Token);
    EXPECT_NE(ctx.GetGenericTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, TypeContextOutOfRangeAndNegativeIndices) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto listTok = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listTok, 0u);
    auto ctx = MetadataGenericContext::ForType(listTok, f);
    EXPECT_EQ(ctx.GetGenericTypeParameterName(1), "1");
    EXPECT_EQ(ctx.GetGenericTypeParameterHandleOrNull(1), 0u);
    EXPECT_EQ(ctx.GetGenericTypeParameterName(-1), "-1");
    EXPECT_EQ(ctx.GetGenericTypeParameterHandleOrNull(-1), 0u);
}

TEST(MetadataGenericContextTest, TypeContextMethodQueriesTakeNilFallback) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto listTok = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listTok, 0u);
    auto ctx = MetadataGenericContext::ForType(listTok, f);
    // The type-context ctor leaves the method nil: the !!N queries fall back.
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, DictionaryTwoParameters) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto dictTok = FindType(f, "System.Collections.Generic", "Dictionary`2");
    ASSERT_NE(dictTok, 0u);
    auto ctx = MetadataGenericContext::ForType(dictTok, f);
    EXPECT_EQ(ctx.GetGenericTypeParameterName(0), "TKey");
    EXPECT_EQ(ctx.GetGenericTypeParameterName(1), "TValue");
    auto h0 = ctx.GetGenericTypeParameterHandleOrNull(0);
    auto h1 = ctx.GetGenericTypeParameterHandleOrNull(1);
    EXPECT_NE(h0, 0u);
    EXPECT_NE(h1, 0u);
    EXPECT_NE(h0, h1);
}

TEST(MetadataGenericContextTest, MethodContextResolvesDeclaringTypeParameters) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto listTok = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(listTok, 0u);
    auto addTok = FindMethodToken(f, listTok, "Add");
    ASSERT_NE(addTok, 0u);
    auto ctx = MetadataGenericContext::ForMethod(addTok, f);
    // The method context resolves the declaring type from the method row, so
    // the class's !N query answers "T" even though Add itself is not generic.
    EXPECT_EQ(ctx.GetGenericTypeParameterName(0), "T");
    EXPECT_NE(ctx.GetGenericTypeParameterHandleOrNull(0), 0u);
    // Add declares no method type parameters of its own.
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, MethodContextResolvesOwnMethodParameters) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto tupleTok = FindType(f, "System", "Tuple");
    ASSERT_NE(tupleTok, 0u);
    auto createTok = FindGenericMethod(f, tupleTok, "Create", 1);
    ASSERT_NE(createTok, 0u);
    auto ctx = MetadataGenericContext::ForMethod(createTok, f);
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterName(0), "T1");
    auto rows = f.GetGenericParameters(createTok);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterHandleOrNull(0), rows[0].Token);
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterName(1), "1");
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterHandleOrNull(1), 0u);
    // System.Tuple declares no type parameters, so the class query falls back.
    EXPECT_EQ(ctx.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, NonGenericMethodOnNonGenericType) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    auto isneTok = FindMethodToken(f, stringTok, "IsNullOrEmpty");
    ASSERT_NE(isneTok, 0u);
    auto ctx = MetadataGenericContext::ForMethod(isneTok, f);
    EXPECT_EQ(ctx.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericTypeParameterHandleOrNull(0), 0u);
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, DefaultConstructedContextTakesNilFallbacks) {
    // The C# default(MetadataGenericContext) shape: a null metadata reader,
    // so every query takes the nil-handle fallbacks.
    MetadataGenericContext ctx;
    EXPECT_EQ(ctx.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericTypeParameterName(1), "1");
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(ctx.GetGenericTypeParameterHandleOrNull(0), 0u);
    EXPECT_EQ(ctx.GetGenericMethodTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, InvalidTokensYieldNilContext) {
    MetadataFile f(FixturePath());
    ASSERT_TRUE(f.IsValid());
    // The C# ctor's GetRow would throw for these; the port's never-throw
    // convention yields the nil context (all queries take the fallbacks).
    auto nilMethod = MetadataGenericContext::ForMethod(0u, f);
    EXPECT_EQ(nilMethod.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(nilMethod.GetGenericMethodTypeParameterHandleOrNull(0), 0u);
    EXPECT_EQ(nilMethod.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(nilMethod.GetGenericTypeParameterHandleOrNull(0), 0u);
    auto badMethod = MetadataGenericContext::ForMethod(0x06000000u | 0x00FFFFFFu, f);
    EXPECT_EQ(badMethod.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(badMethod.GetGenericTypeParameterName(0), "0");
    auto nilType = MetadataGenericContext::ForType(0u, f);
    EXPECT_EQ(nilType.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(nilType.GetGenericTypeParameterHandleOrNull(0), 0u);
    auto badType = MetadataGenericContext::ForType(0x02000000u | 0x00FFFFFFu, f);
    EXPECT_EQ(badType.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(badType.GetGenericTypeParameterHandleOrNull(0), 0u);
}

TEST(MetadataGenericContextTest, InvalidFileYieldsNilFallbacks) {
    MetadataFile f("this-is-not-an-assembly.txt");
    ASSERT_FALSE(f.IsValid());
    auto methodCtx = MetadataGenericContext::ForMethod(0x06000001u, f);
    EXPECT_EQ(methodCtx.GetGenericMethodTypeParameterName(0), "0");
    EXPECT_EQ(methodCtx.GetGenericMethodTypeParameterHandleOrNull(0), 0u);
    EXPECT_EQ(methodCtx.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(methodCtx.GetGenericTypeParameterHandleOrNull(0), 0u);
    auto typeCtx = MetadataGenericContext::ForType(0x02000001u, f);
    EXPECT_EQ(typeCtx.GetGenericTypeParameterName(0), "0");
    EXPECT_EQ(typeCtx.GetGenericTypeParameterHandleOrNull(0), 0u);
}
