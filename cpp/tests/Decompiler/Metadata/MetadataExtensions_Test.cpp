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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the MetadataExtensions ToILNameString port (cpp/Decompiler/Metadata/
// MetadataExtensions.{hpp,cpp} -- the ILAsm type-name rendering the catch-type
// writer, the ReflectionDisassembler member-name rendering, and the
// SortByNameProcessor sort keys consume) and the FullTypeName::GetDeclaringType
// member it recurses through. The rendering contract:
//   * top-level: "Namespace.Name" (+"`N" unless omitGenerics);
//   * nested: the declaring type's rendering, '/', the innermost name carrying
//     ONLY that segment's own additional type-parameter count (the per-segment
//     arity, not the total);
//   * every composed top-level name and each nested name pass through
//     DisassemblerHelpers.Escape (ILAsm-keyword names render quoted).

#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using ILSpy::Decompiler::Metadata::ToILNameString;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;

namespace {

// A top-level name from its parts (the ctor is explicit, so the braced form
// reads the three fields in declaration order: namespace, name, arity).
FullTypeName TopLevel(const char* ns, const char* name, int tpc = 0) {
    return FullTypeName(TopLevelTypeName(ns, name, tpc));
}

} // namespace

// ---------------------------------------------------------------------------
// FullTypeName::GetDeclaringType (the member ToILNameString recurses through).
// ---------------------------------------------------------------------------

TEST(FullTypeNameGetDeclaringTypeTest, ThrowsForTopLevelName)
{
    FullTypeName ft = TopLevel("Ns", "T");
    EXPECT_THROW((void)ft.GetDeclaringType(), std::logic_error);
}

TEST(FullTypeNameGetDeclaringTypeTest, SingleLevelYieldsTheTopLevelName)
{
    // "Ns.Outer`1+Inner": the declaring type is the top-level "Ns.Outer`1"
    // (nesting stripped, arity of the top level retained).
    FullTypeName ft("Ns.Outer`1+Inner");
    FullTypeName declaring = ft.GetDeclaringType();
    EXPECT_FALSE(declaring.IsNested());
    EXPECT_EQ(declaring.GetTopLevelTypeName().Namespace(), "Ns");
    EXPECT_EQ(declaring.GetTopLevelTypeName().Name(), "Outer");
    EXPECT_EQ(declaring.TypeParameterCount(), 1);
}

TEST(FullTypeNameGetDeclaringTypeTest, TwoLevelsYieldAOneLevelChain)
{
    FullTypeName ft("Ns.A+B+C");
    FullTypeName declaring = ft.GetDeclaringType();
    EXPECT_TRUE(declaring.IsNested());
    EXPECT_EQ(declaring.NestingLevel(), 1);
    EXPECT_EQ(declaring.Name(), "B");
    EXPECT_EQ(declaring.ReflectionName(), "Ns.A+B");
}

TEST(FullTypeNameGetDeclaringTypeTest, OriginalIsUnchangedAfterTheCall)
{
    FullTypeName ft("Ns.A`1+B+C");
    (void)ft.GetDeclaringType();
    EXPECT_TRUE(ft.IsNested());
    EXPECT_EQ(ft.NestingLevel(), 2);
    EXPECT_EQ(ft.Name(), "C");
}

TEST(FullTypeNameGetDeclaringTypeTest, DeclaringTypeRoundTripsThroughReflectionName)
{
    FullTypeName ft("System.Collections.Generic.Dictionary`2+Enumerator");
    EXPECT_EQ(ft.GetDeclaringType().ReflectionName(),
        "System.Collections.Generic.Dictionary`2");
}

// ---------------------------------------------------------------------------
// ToILNameString -- top-level names.
// ---------------------------------------------------------------------------

TEST(MetadataExtensionsToILNameStringTest, TopLevelWithNamespaceRendersDottedName)
{
    EXPECT_EQ(ToILNameString(TopLevel("System", "String")), "System.String");
}

TEST(MetadataExtensionsToILNameStringTest, TopLevelWithArityAppendsBacktickCount)
{
    EXPECT_EQ(ToILNameString(TopLevel("System.Collections.Generic", "List", 1)),
        "System.Collections.Generic.List`1");
}

TEST(MetadataExtensionsToILNameStringTest, TopLevelWithoutNamespaceRendersBareName)
{
    EXPECT_EQ(ToILNameString(TopLevel("", "Program")), "Program");
}

TEST(MetadataExtensionsToILNameStringTest, TopLevelWithoutNamespaceWithArity)
{
    EXPECT_EQ(ToILNameString(TopLevel("", "Pair", 2)), "Pair`2");
}

TEST(MetadataExtensionsToILNameStringTest, OmitGenericsDropsTheTopLevelArity)
{
    EXPECT_EQ(ToILNameString(TopLevel("System.Collections.Generic", "List", 1),
        /*omitGenerics=*/true),
        "System.Collections.Generic.List");
}

TEST(MetadataExtensionsToILNameStringTest, TopLevelNoNamespaceILAsmKeywordNameIsQuoted)
{
    // "class" is an ILAsm keyword, so the composed no-namespace name escapes
    // through DisassemblerHelpers.Escape ('...' quoting).
    EXPECT_EQ(ToILNameString(TopLevel("", "class")), "'class'");
}

// ---------------------------------------------------------------------------
// ToILNameString -- nested names (the '/' separator and the per-segment arity).
// ---------------------------------------------------------------------------

TEST(MetadataExtensionsToILNameStringTest, NestedRendersSlashSeparatedChain)
{
    EXPECT_EQ(ToILNameString(FullTypeName("System.Collections.Generic.Dictionary`2+Enumerator")),
        "System.Collections.Generic.Dictionary`2/Enumerator");
}

TEST(MetadataExtensionsToILNameStringTest, NestedInnermostSegmentCarriesItsOwnArity)
{
    // The innermost segment carries its OWN additional count (2), not the
    // chain total (3) -- the per-segment arity crux.
    EXPECT_EQ(ToILNameString(FullTypeName("Ns.Outer`1+Inner`2")), "Ns.Outer`1/Inner`2");
}

TEST(MetadataExtensionsToILNameStringTest, NestedZeroAdditionalCountAppendsNoArity)
{
    // A nested segment with no additional type parameters of its own renders
    // bare (the parent's arity does not bleed into it).
    EXPECT_EQ(ToILNameString(FullTypeName("Ns.Outer`1+Inner")), "Ns.Outer`1/Inner");
}

TEST(MetadataExtensionsToILNameStringTest, DeeperNestingRendersEverySegment)
{
    EXPECT_EQ(ToILNameString(FullTypeName("Ns.A`1+B+C")), "Ns.A`1/B/C");
}

TEST(MetadataExtensionsToILNameStringTest, NestedOmitGenericsDropsEveryArity)
{
    EXPECT_EQ(ToILNameString(FullTypeName("System.Collections.Generic.Dictionary`2+Enumerator"),
        /*omitGenerics=*/true),
        "System.Collections.Generic.Dictionary/Enumerator");
}

TEST(MetadataExtensionsToILNameStringTest, NestedILAsmKeywordNameIsQuoted)
{
    // Only the nested NAME passes through Escape here ("Ns.Outer" is a valid
    // composed identifier; "class" is an ILAsm keyword).
    EXPECT_EQ(ToILNameString(FullTypeName("Ns.Outer+class")), "Ns.Outer/'class'");
}

TEST(MetadataExtensionsToILNameStringTest, NestedInvalidIdentifierNameIsQuoted)
{
    // A space is not a valid identifier character, so the nested name renders
    // quoted (a plain space passes through EscapeString raw).
    EXPECT_EQ(ToILNameString(FullTypeName("Ns.Outer+Inner Name")), "Ns.Outer/'Inner Name'");
}

TEST(MetadataExtensionsToILNameStringTest, DiffersFromTheReflectionNameSeparator)
{
    // The reflection name nests with '+'; the IL name nests with '/' -- the two
    // renderings of the same FullTypeName are observably distinct.
    FullTypeName ft("Ns.Outer`1+Inner`2");
    EXPECT_EQ(ft.ReflectionName(), "Ns.Outer`1+Inner`2");
    EXPECT_EQ(ToILNameString(ft), "Ns.Outer`1/Inner`2");
}
