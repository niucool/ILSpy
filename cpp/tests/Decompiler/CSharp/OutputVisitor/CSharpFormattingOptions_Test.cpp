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

// Tests for `CSharpFormattingOptions` (the formatting-settings data class) and the six
// formatting enums, plus `FormattingOptionsFactory` (the pre-defined-style factory) in
// OutputVisitor/CSharpFormattingOptions.hpp + OutputVisitor/FormattingOptionsFactory.hpp -- the
// second in-order Phase-5 piece of the OutputVisitor/ITextOutput/TokenWriter output stage per
// the D316 decision-log entry (the `TokenWriter`/`ILocatable`/`DecoratingTokenWriter` interface
// foundation landed in D316; this is the formatting settings the `CSharpOutputVisitor`
// pretty-printer consults to decide brace placement, spacing, wrapping, and blank-line policy).
// The data class is exercised via its default ctor (the C# all-default instance) and the seven
// factory styles (the C# `CreateEmpty`/`CreateMono`/`CreateKRStyle`/`CreateSharpDevelop`/
// `CreateAllman`/`CreateWhitesmiths`/`CreateGNU`), verifying the enum values, the default
// initializers, `Clone`, the per-style known values, and the style-derivation relationships.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <type_traits>

#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"
#include "Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using FormattingOptionsFactory::CreateEmpty;
using FormattingOptionsFactory::CreateMono;
using FormattingOptionsFactory::CreateKRStyle;
using FormattingOptionsFactory::CreateSharpDevelop;
using FormattingOptionsFactory::CreateAllman;
using FormattingOptionsFactory::CreateWhitesmiths;
using FormattingOptionsFactory::CreateGNU;

// The six C# `enum`s (no `: <type>`) port as `enum class` with the default underlying `int`.
TEST(CSharp_FormattingOptions, EnumUnderlyingTypes) {
	EXPECT_TRUE((std::is_same_v<std::underlying_type_t<BraceStyle>, int>));
	EXPECT_TRUE((std::is_same_v<std::underlying_type_t<PropertyFormatting>, int>));
	EXPECT_TRUE((std::is_same_v<std::underlying_type_t<Wrapping>, int>));
	EXPECT_TRUE((std::is_same_v<std::underlying_type_t<NewLinePlacement>, int>));
	EXPECT_TRUE((std::is_same_v<std::underlying_type_t<UsingPlacement>, int>));
	EXPECT_TRUE((std::is_same_v<std::underlying_type_t<EmptyLineFormatting>, int>));
}

// The six enums' member values mirror the C# declaration order (each member's numeric value is
// its declaration index -- the `ReferenceKind` D278 precedent).
TEST(CSharp_FormattingOptions, EnumValues) {
	EXPECT_EQ(static_cast<int>(BraceStyle::EndOfLine), 0);
	EXPECT_EQ(static_cast<int>(BraceStyle::EndOfLineWithoutSpace), 1);
	EXPECT_EQ(static_cast<int>(BraceStyle::NextLine), 2);
	EXPECT_EQ(static_cast<int>(BraceStyle::NextLineShifted), 3);
	EXPECT_EQ(static_cast<int>(BraceStyle::NextLineShifted2), 4);
	EXPECT_EQ(static_cast<int>(BraceStyle::BannerStyle), 5);

	EXPECT_EQ(static_cast<int>(PropertyFormatting::SingleLine), 0);
	EXPECT_EQ(static_cast<int>(PropertyFormatting::MultipleLines), 1);

	EXPECT_EQ(static_cast<int>(Wrapping::DoNotWrap), 0);
	EXPECT_EQ(static_cast<int>(Wrapping::WrapAlways), 1);
	EXPECT_EQ(static_cast<int>(Wrapping::WrapIfTooLong), 2);

	EXPECT_EQ(static_cast<int>(NewLinePlacement::DoNotCare), 0);
	EXPECT_EQ(static_cast<int>(NewLinePlacement::NewLine), 1);
	EXPECT_EQ(static_cast<int>(NewLinePlacement::SameLine), 2);

	EXPECT_EQ(static_cast<int>(UsingPlacement::TopOfFile), 0);
	EXPECT_EQ(static_cast<int>(UsingPlacement::InsideNamespace), 1);

	EXPECT_EQ(static_cast<int>(EmptyLineFormatting::DoNotChange), 0);
	EXPECT_EQ(static_cast<int>(EmptyLineFormatting::Indent), 1);
	EXPECT_EQ(static_cast<int>(EmptyLineFormatting::DoNotIndent), 2);
}

// A default-constructed `CSharpFormattingOptions` reproduces the C# `new
// CSharpFormattingOptions()` all-default instance: the three non-trivial defaults plus a
// sampling of the default-zero members (bool=false, int=0, enum=zero value, string?=nullopt).
TEST(CSharp_FormattingOptions, DefaultConstructedHasCSharpDefaults) {
	CSharpFormattingOptions o;
	// The three non-trivial defaults (the C# properties with explicit initializers).
	EXPECT_EQ(o.IndentationString, "\t");
	EXPECT_TRUE(o.AllowOneLinedArrayInitialziers);
	EXPECT_EQ(o.EmbeddedStatementPlacement, NewLinePlacement::NewLine);
	// The GUI/profile metadata (unused by the output visitor).
	EXPECT_FALSE(o.Name.has_value());
	EXPECT_FALSE(o.IsBuiltIn);
	// A sampling of the default-false bools.
	EXPECT_FALSE(o.IndentNamespaceBody);
	EXPECT_FALSE(o.SpaceAroundAssignment);
	EXPECT_FALSE(o.SpaceBeforeIfParentheses);
	EXPECT_FALSE(o.KeepCommentsAtFirstColumn);
	EXPECT_FALSE(o.RemoveEndOfLineWhiteSpace);
	// A sampling of the default-zero ints.
	EXPECT_EQ(o.MinimumBlankLinesBetweenTypes, 0);
	EXPECT_EQ(o.MinimumBlankLinesAfterUsings, 0);
	// A sampling of the default-zero-value enums.
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::EndOfLine);
	EXPECT_EQ(o.AutoPropertyFormatting, PropertyFormatting::SingleLine);
	EXPECT_EQ(o.ArrayInitializerWrapping, Wrapping::DoNotWrap);
	EXPECT_EQ(o.ElseNewLinePlacement, NewLinePlacement::DoNotCare);
	EXPECT_EQ(o.UsingPlacement, UsingPlacement::TopOfFile);
	EXPECT_EQ(o.EmptyLineFormatting, EmptyLineFormatting::DoNotChange);
}

// `Clone()` (`MemberwiseClone`) produces an equal but independent copy (the C++ copy ctor does
// the member-wise copy; mutating the clone does not affect the original).
TEST(CSharp_FormattingOptions, CloneIsEqualAndIndependent) {
	CSharpFormattingOptions o = CreateMono();
	CSharpFormattingOptions c = o.Clone();
	// Equal: a spot-check of the Mono-style values.
	EXPECT_EQ(c.IndentNamespaceBody, o.IndentNamespaceBody);
	EXPECT_EQ(c.NamespaceBraceStyle, o.NamespaceBraceStyle);
	EXPECT_EQ(c.SpaceBeforeIfParentheses, o.SpaceBeforeIfParentheses);
	EXPECT_EQ(c.MinimumBlankLinesBetweenTypes, o.MinimumBlankLinesBetweenTypes);
	EXPECT_EQ(c.NewLineBeforeNewQueryClause, o.NewLineBeforeNewQueryClause);
	EXPECT_EQ(c.IndentationString, o.IndentationString);
	// Independent: mutating the clone does not affect the original.
	c.IndentNamespaceBody = false;
	c.NamespaceBraceStyle = BraceStyle::EndOfLine;
	c.IndentationString = "  ";
	EXPECT_TRUE(o.IndentNamespaceBody);
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.IndentationString, "\t");
}

// `CreateEmpty()` is the all-default instance (the C# `CreateEmpty`).
TEST(CSharp_FormattingOptions, CreateEmptyIsAllDefaults) {
	CSharpFormattingOptions o = CreateEmpty();
	EXPECT_EQ(o.IndentationString, "\t");
	EXPECT_TRUE(o.AllowOneLinedArrayInitialziers);
	EXPECT_EQ(o.EmbeddedStatementPlacement, NewLinePlacement::NewLine);
	EXPECT_FALSE(o.IndentNamespaceBody);
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::EndOfLine);
	EXPECT_FALSE(o.SpaceBeforeIfParentheses);
	EXPECT_EQ(o.MinimumBlankLinesBetweenTypes, 0);
	EXPECT_EQ(o.NewLineBeforeNewQueryClause, NewLinePlacement::DoNotCare);
	EXPECT_EQ(o.UsingPlacement, UsingPlacement::TopOfFile);
}

// `CreateMono()` -- the Mono indent style (the `ilspycmd` CLI default). Verifies a representative
// sampling of the per-style values the C# factory sets.
TEST(CSharp_FormattingOptions, CreateMonoStyle) {
	CSharpFormattingOptions o = CreateMono();
	EXPECT_TRUE(o.IndentNamespaceBody);
	EXPECT_TRUE(o.IndentClassBody);
	EXPECT_TRUE(o.IndentBlocks);
	EXPECT_FALSE(o.IndentSwitchBody);  // Mono differs from KRStyle here.
	EXPECT_TRUE(o.IndentCaseBody);
	EXPECT_TRUE(o.IndentBreakStatements);
	EXPECT_TRUE(o.IndentPreprocessorDirectives);
	EXPECT_FALSE(o.IndentBlocksInsideExpressions);
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.ClassBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.MethodBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.ConstructorBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.AnonymousMethodBraceStyle, BraceStyle::EndOfLine);
	EXPECT_EQ(o.PropertyBraceStyle, BraceStyle::EndOfLine);
	EXPECT_EQ(o.StatementBraceStyle, BraceStyle::EndOfLine);
	EXPECT_EQ(o.ElseNewLinePlacement, NewLinePlacement::SameLine);
	EXPECT_EQ(o.CatchNewLinePlacement, NewLinePlacement::SameLine);
	EXPECT_EQ(o.WhileNewLinePlacement, NewLinePlacement::SameLine);
	EXPECT_EQ(o.ArrayInitializerWrapping, Wrapping::WrapIfTooLong);
	EXPECT_EQ(o.ArrayInitializerBraceStyle, BraceStyle::EndOfLine);
	EXPECT_TRUE(o.AllowOneLinedArrayInitialziers);
	EXPECT_TRUE(o.SpaceBeforeMethodCallParentheses);  // Mono differs from KRStyle here.
	EXPECT_TRUE(o.SpaceBeforeMethodDeclarationParentheses);
	EXPECT_TRUE(o.SpaceBeforeIfParentheses);
	EXPECT_TRUE(o.SpaceAroundAssignment);
	EXPECT_TRUE(o.SpaceAroundLogicalOperator);
	EXPECT_FALSE(o.SpacesWithinParentheses);
	EXPECT_FALSE(o.SpacesWithinIfParentheses);
	EXPECT_TRUE(o.SpaceBeforeConditionalOperatorCondition);
	EXPECT_TRUE(o.SpaceAfterConditionalOperatorCondition);
	EXPECT_EQ(o.SpacesWithinBrackets, false);
	EXPECT_TRUE(o.SpacesBeforeBrackets);
	EXPECT_TRUE(o.AlignEmbeddedStatements);
	EXPECT_EQ(o.AutoPropertyFormatting, PropertyFormatting::SingleLine);
	EXPECT_EQ(o.EmptyLineFormatting, EmptyLineFormatting::DoNotIndent);
	EXPECT_TRUE(o.SpaceAfterMethodDeclarationParameterComma);
	EXPECT_TRUE(o.SpaceAfterConstructorDeclarationParameterComma);
	EXPECT_TRUE(o.SpaceBeforeIndexerDeclarationBracket);
	EXPECT_TRUE(o.RemoveEndOfLineWhiteSpace);
	EXPECT_EQ(o.MinimumBlankLinesAfterUsings, 1);
	EXPECT_EQ(o.MinimumBlankLinesBetweenTypes, 1);
	EXPECT_EQ(o.MinimumBlankLinesBetweenMembers, 1);
	EXPECT_EQ(o.UsingPlacement, UsingPlacement::TopOfFile);
	EXPECT_EQ(o.NewLineBeforeNewQueryClause, NewLinePlacement::NewLine);
	EXPECT_EQ(o.ChainedMethodCallWrapping, Wrapping::DoNotWrap);
	EXPECT_EQ(o.MethodCallArgumentWrapping, Wrapping::DoNotWrap);
	EXPECT_EQ(o.NewLineAferMethodCallOpenParentheses, NewLinePlacement::DoNotCare);
}

// `CreateKRStyle()` -- the K&R style (the base for SharpDevelop/Allman/Whitesmiths/GNU). Verifies
// the values that distinguish it from Mono and the per-style values the C# factory sets.
TEST(CSharp_FormattingOptions, CreateKRStyleValues) {
	CSharpFormattingOptions o = CreateKRStyle();
	EXPECT_TRUE(o.IndentNamespaceBody);
	EXPECT_TRUE(o.IndentSwitchBody);  // KRStyle differs from Mono here.
	EXPECT_TRUE(o.IndentCaseBody);
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.AnonymousMethodBraceStyle, BraceStyle::EndOfLine);
	EXPECT_EQ(o.StatementBraceStyle, BraceStyle::EndOfLine);
	EXPECT_FALSE(o.SpaceBeforeMethodCallParentheses);  // KRStyle differs from Mono here.
	EXPECT_FALSE(o.SpaceBeforeMethodDeclarationParentheses);
	EXPECT_FALSE(o.SpaceBeforeConstructorDeclarationParentheses);
	EXPECT_FALSE(o.SpaceBeforeDelegateDeclarationParentheses);
	EXPECT_FALSE(o.SpaceBeforeIndexerDeclarationBracket);
	EXPECT_TRUE(o.SpaceAfterMethodCallParameterComma);
	EXPECT_TRUE(o.SpaceAfterConstructorDeclarationParameterComma);
	EXPECT_EQ(o.NewLineBeforeConstructorInitializerColon, NewLinePlacement::NewLine);
	EXPECT_EQ(o.NewLineAfterConstructorInitializerColon, NewLinePlacement::SameLine);
	EXPECT_FALSE(o.SpaceBeforeNewParentheses);
	EXPECT_TRUE(o.SpaceBeforeIfParentheses);
	EXPECT_TRUE(o.SpaceAroundAssignment);
	EXPECT_FALSE(o.SpacesWithinParentheses);
	EXPECT_FALSE(o.SpaceWithinMethodCallParentheses);
	EXPECT_TRUE(o.SpaceAfterForSemicolon);
	EXPECT_FALSE(o.SpaceBeforeForSemicolon);
	EXPECT_FALSE(o.SpaceBeforeArrayDeclarationBrackets);
	EXPECT_FALSE(o.SpacesBeforeBrackets);  // KRStyle differs from Mono here.
	EXPECT_EQ(o.ArrayInitializerWrapping, Wrapping::WrapIfTooLong);
	EXPECT_EQ(o.ArrayInitializerBraceStyle, BraceStyle::EndOfLine);
	EXPECT_TRUE(o.AlignEmbeddedStatements);
	EXPECT_TRUE(o.RemoveEndOfLineWhiteSpace);
	EXPECT_EQ(o.MinimumBlankLinesBetweenTypes, 1);
	EXPECT_EQ(o.NewLineBeforeNewQueryClause, NewLinePlacement::NewLine);
	// The C# factory does NOT set these (the CSharpFormattingOptions defaults apply).
	EXPECT_FALSE(o.IndentBlocksInsideExpressions);
	EXPECT_EQ(o.NewLineBeforeConstructorInitializerColon, NewLinePlacement::NewLine);
}

// `CreateSharpDevelop()` returns the K&R style unchanged (the C# `var baseOptions =
// CreateKRStyle(); return baseOptions;`).
TEST(CSharp_FormattingOptions, CreateSharpDevelopEqualsKRStyle) {
	CSharpFormattingOptions sd = CreateSharpDevelop();
	CSharpFormattingOptions kr = CreateKRStyle();
	EXPECT_EQ(sd.IndentNamespaceBody, kr.IndentNamespaceBody);
	EXPECT_EQ(sd.NamespaceBraceStyle, kr.NamespaceBraceStyle);
	EXPECT_EQ(sd.SpaceBeforeMethodCallParentheses, kr.SpaceBeforeMethodCallParentheses);
	EXPECT_EQ(sd.NewLineBeforeNewQueryClause, kr.NewLineBeforeNewQueryClause);
	EXPECT_EQ(sd.IndentationString, kr.IndentationString);
}

// `CreateAllman()` -- the Allman style (the K&R style with the brace/catch/else placements
// shifted to `NextLine`).
TEST(CSharp_FormattingOptions, CreateAllmanStyle) {
	CSharpFormattingOptions o = CreateAllman();
	// Inherited from KRStyle.
	EXPECT_TRUE(o.IndentNamespaceBody);
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::NextLine);
	EXPECT_FALSE(o.SpaceBeforeMethodCallParentheses);
	// Overridden from KRStyle.
	EXPECT_EQ(o.AnonymousMethodBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.PropertyBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.PropertyGetBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.PropertySetBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.EventBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.EventAddBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.EventRemoveBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.StatementBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.ArrayInitializerBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.CatchNewLinePlacement, NewLinePlacement::NewLine);
	EXPECT_EQ(o.ElseNewLinePlacement, NewLinePlacement::NewLine);
	EXPECT_EQ(o.ElseIfNewLinePlacement, NewLinePlacement::SameLine);
	EXPECT_EQ(o.FinallyNewLinePlacement, NewLinePlacement::NewLine);
	EXPECT_EQ(o.WhileNewLinePlacement, NewLinePlacement::DoNotCare);
	EXPECT_EQ(o.ArrayInitializerWrapping, Wrapping::DoNotWrap);
	EXPECT_TRUE(o.IndentBlocksInsideExpressions);
}

// `CreateWhitesmiths()` -- the Whitesmiths style (the K&R style with the braces shifted to
// `NextLineShifted`).
TEST(CSharp_FormattingOptions, CreateWhitesmithsStyle) {
	CSharpFormattingOptions o = CreateWhitesmiths();
	// Inherited from KRStyle.
	EXPECT_TRUE(o.IndentNamespaceBody);
	EXPECT_FALSE(o.SpaceBeforeMethodCallParentheses);
	// Overridden from KRStyle.
	EXPECT_EQ(o.NamespaceBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.ClassBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.InterfaceBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.StructBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.EnumBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.MethodBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.ConstructorBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.DestructorBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.AnonymousMethodBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.PropertyBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.PropertyGetBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.PropertySetBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.EventBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.EventAddBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.EventRemoveBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_EQ(o.StatementBraceStyle, BraceStyle::NextLineShifted);
	EXPECT_TRUE(o.IndentBlocksInsideExpressions);
}

// `CreateGNU()` -- the GNU style (the Allman style with the statement braces shifted to
// `NextLineShifted2`).
TEST(CSharp_FormattingOptions, CreateGNUStyle) {
	CSharpFormattingOptions o = CreateGNU();
	// Inherited from Allman.
	EXPECT_EQ(o.PropertyBraceStyle, BraceStyle::NextLine);
	EXPECT_EQ(o.CatchNewLinePlacement, NewLinePlacement::NewLine);
	EXPECT_EQ(o.ArrayInitializerBraceStyle, BraceStyle::NextLine);
	EXPECT_TRUE(o.IndentBlocksInsideExpressions);
	// Overridden from Allman.
	EXPECT_EQ(o.StatementBraceStyle, BraceStyle::NextLineShifted2);
}

// The seven factory styles are DISTINCT (a sampling of the distinguishing values).
TEST(CSharp_FormattingOptions, FactoryStylesAreDistinct) {
	CSharpFormattingOptions empty = CreateEmpty();
	CSharpFormattingOptions mono = CreateMono();
	CSharpFormattingOptions kr = CreateKRStyle();
	CSharpFormattingOptions allman = CreateAllman();
	CSharpFormattingOptions whitesmiths = CreateWhitesmiths();
	CSharpFormattingOptions gnu = CreateGNU();
	// Empty vs Mono: NamespaceBraceStyle.
	EXPECT_NE(empty.NamespaceBraceStyle, mono.NamespaceBraceStyle);
	// Mono vs KRStyle: SpaceBeforeMethodCallParentheses / IndentSwitchBody.
	EXPECT_NE(mono.SpaceBeforeMethodCallParentheses, kr.SpaceBeforeMethodCallParentheses);
	EXPECT_NE(mono.IndentSwitchBody, kr.IndentSwitchBody);
	// KRStyle vs Allman: PropertyBraceStyle.
	EXPECT_NE(kr.PropertyBraceStyle, allman.PropertyBraceStyle);
	// KRStyle vs Whitesmiths: NamespaceBraceStyle.
	EXPECT_NE(kr.NamespaceBraceStyle, whitesmiths.NamespaceBraceStyle);
	// Allman vs GNU: StatementBraceStyle.
	EXPECT_NE(allman.StatementBraceStyle, gnu.StatementBraceStyle);
	// Allman vs Whitesmiths: the brace styles diverge (NextLine vs NextLineShifted).
	EXPECT_NE(allman.PropertyBraceStyle, whitesmiths.PropertyBraceStyle);
}
