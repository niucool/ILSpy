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

// Port of `CSharpFormattingOptions` (the formatting-settings data class) and the six
// formatting enums (`BraceStyle`, `PropertyFormatting`, `Wrapping`, `NewLinePlacement`,
// `UsingPlacement`, `EmptyLineFormatting`) in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.cs -- the second in-order
// Phase-5 piece of the OutputVisitor/ITextOutput/TokenWriter output stage per the D316
// decision-log entry (the `TokenWriter`/`ILocatable`/`DecoratingTokenWriter` interface foundation
// landed in D316; this is the next piece -- the formatting settings the `CSharpOutputVisitor`
// (the pretty-printer, not yet ported) consults to decide brace placement, spacing, wrapping,
// and blank-line policy). The C# `CSharpFormattingOptions` is a plain data class whose ~120
// public get/set auto-properties hold the per-style settings; the `FormattingOptionsFactory`
// (the next header) constructs the pre-defined styles (`CreateMono`/`CreateKRStyle`/...).
//
// This is the cleanest output-stage piece: it is FULLY self-contained (no AST/`TokenWriter`/
// `PrimitiveValue` dependencies at all -- just the six enums and the data class), it is a direct
// prerequisite for the `CSharpOutputVisitor` ctor (which takes a `CSharpFormattingOptions`), and
// it is individually verifiable via the factory's known per-style values. The concrete
// `TextWriterTokenWriter` (the D316 plan's first-named output-stage piece) references the
// not-yet-ported `CSharpOutputVisitor.IsKeyword` static helper (the `@`-prefix-on-keyword
// identifier check), so it lands after the `CSharpOutputVisitor` skeleton (or with the `IsKeyword`
// helper factored out); `CSharpFormattingOptions` has no such tangle and ports cleanly now.
//
// C#-to-C++ porting decisions:
//  * The six C# `enum`s (no `[Flags]`, no `Any` member) port as `enum class` with the default
//    underlying `int` (the C# enums declare no `: <type>`, so the C# default is `int`; the
//    `SymbolKind`/`ReferenceKind` D271/D278 precedent used `std::uint8_t` only because the C#
//    was `: byte`). The member order and values mirror the C# exactly (each member's numeric
//    value is its declaration index, the `ReferenceKind` precedent).
//  * The C# `public class CSharpFormattingOptions` with ~120 `public ... { get; set; }`
//    auto-properties ports as a C++ `class` with PUBLIC data members -- the faithful equivalent
//    of public mutable auto-properties with trivial getters/setters (the `LongSet`/`LongInterval`
//    value-type precedent). Every member gets an IN-CLASS DEFAULT INITIALIZER so a
//    default-constructed `CSharpFormattingOptions` reproduces the C# defaults exactly: `bool`
//    -> `false` (the C# default for an uninitialized `bool` property), `int` -> `0`, the enum
//    members -> their zero value (the first member: `BraceStyle::EndOfLine`,
//    `PropertyFormatting::SingleLine`, `Wrapping::DoNotWrap`, `NewLinePlacement::DoNotCare`,
//    `UsingPlacement::TopOfFile`, `EmptyLineFormatting::DoNotChange`), with the three
//    non-trivial defaults set verbatim (`IndentationString = "\t"`,
//    `AllowOneLinedArrayInitialziers = true`, `EmbeddedStatementPlacement = NewLinePlacement::NewLine`
//    -- the two C# properties with explicit backing fields plus initializers, plus the
//    `IndentationString` initializer).
//  * The C# `string? Name` (nullable) ports as `std::optional<std::string>` (nullopt by
//    default -- the C# `string?` with no initializer is `null`); the C# `bool IsBuiltIn` ports
//    as `bool IsBuiltIn = false`.
//  * The C# `Clone()` (which uses `MemberwiseClone`, a shallow member-wise copy) ports as a
//    `Clone()` returning `*this` by value -- the C++ implicit copy ctor does a member-wise copy
//    (each member is a value type, so the copy is identical to `MemberwiseClone`), so
//    `CSharpFormattingOptions Clone() const { return *this; }` is the faithful equivalent.
//    (The C# `TypeConverter`/`ExpandableObjectConverter` attribute and the commented-out
//    `Load`/`Save`/`Equals` XML-serialization members are dropped -- they are GUI/property-grid
//    concerns, the `TextLocationConverter` D218 dropped-GUI precedent; the decompiler's
//    `CSharpOutputVisitor` only reads the formatting fields.)
//  * The C# `internal CSharpFormattingOptions()` ctor (constructible only via
//    `FormattingOptionsFactory`) ports as a public default ctor (`= default`) -- C++ has no
//    `internal` access, and the in-class member initializers make the default-constructed
//    instance match the C# `new CSharpFormattingOptions()` (all-default) instance. The
//    `FormattingOptionsFactory` (the next header) constructs the named styles by
//    default-constructing then assigning the per-style members (the C# object-initializer
//    `new CSharpFormattingOptions { ... }` shape).
//  * The C# property NAMES are preserved verbatim, INCLUDING the C# typos that the
//    `CSharpOutputVisitor` reads them by (`AllowOneLinedArrayInitialziers`,
//    `SpacesWithinCheckedExpressionParantheses`, `NewLineAferMethodCallOpenParentheses`, ...),
//    so the ported output visitor can reference the exact same names.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPFORMATTINGOPTIONS_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPFORMATTINGOPTIONS_HPP

#include <optional>
#include <string>

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

// The C# `public enum BraceStyle` -- where a brace (`{`/`}`) is placed relative to its owning
// construct (the `EndOfLine` zero value is the C# default for an uninitialized `BraceStyle`
// property).
enum class BraceStyle {
	EndOfLine,
	EndOfLineWithoutSpace,
	NextLine,
	NextLineShifted,
	NextLineShifted2,
	BannerStyle,
};

// The C# `public enum PropertyFormatting` -- whether a simple get/set accessor is printed on one
// line or across multiple lines (`SingleLine` is the zero value).
enum class PropertyFormatting {
	SingleLine,
	MultipleLines,
};

// The C# `public enum Wrapping` -- whether a construct's arguments wrap to the next line
// (`DoNotWrap` is the zero value).
enum class Wrapping {
	DoNotWrap,
	WrapAlways,
	WrapIfTooLong,
};

// The C# `public enum NewLinePlacement` -- whether a construct starts on a new line or stays on
// the same line (`DoNotCare` is the zero value).
enum class NewLinePlacement {
	DoNotCare,
	NewLine,
	SameLine,
};

// The C# `public enum UsingPlacement` -- whether `using` directives go at the top of the file or
// inside the namespace (`TopOfFile` is the zero value).
enum class UsingPlacement {
	TopOfFile,
	InsideNamespace,
};

// The C# `public enum EmptyLineFormatting` -- how empty lines are formatted (`DoNotChange` is the
// zero value).
enum class EmptyLineFormatting {
	DoNotChange,
	Indent,
	DoNotIndent,
};

// The C# `public class CSharpFormattingOptions` -- the formatting-settings data class. Every
// public get/set auto-property ports as a public data member with an in-class default initializer
// reproducing the C# default (false / 0 / the zero enum value, except the three non-trivial
// defaults). The members are grouped by the C# `#region`s in declaration order.
class CSharpFormattingOptions {
public:
	CSharpFormattingOptions() = default;
	CSharpFormattingOptions(const CSharpFormattingOptions&) = default;
	CSharpFormattingOptions& operator=(const CSharpFormattingOptions&) = default;

	// The C# `Clone()` (`MemberwiseClone`) -- the implicit copy ctor does the member-wise copy.
	CSharpFormattingOptions Clone() const { return *this; }

	// The C# `string? Name` / `bool IsBuiltIn` (GUI/profile metadata, unused by the output
	// visitor; `Name` is `null` by default).
	std::optional<std::string> Name;
	bool IsBuiltIn = false;

	// #region Indentation
	std::string IndentationString = "\t";
	bool IndentNamespaceBody = false;
	bool IndentClassBody = false;
	bool IndentInterfaceBody = false;
	bool IndentStructBody = false;
	bool IndentEnumBody = false;
	bool IndentMethodBody = false;
	bool IndentPropertyBody = false;
	bool IndentEventBody = false;
	bool IndentBlocks = false;
	bool IndentSwitchBody = false;
	bool IndentCaseBody = false;
	bool IndentBreakStatements = false;
	bool AlignEmbeddedStatements = false;
	bool AlignElseInIfStatements = false;
	PropertyFormatting AutoPropertyFormatting = PropertyFormatting::SingleLine;
	PropertyFormatting SimplePropertyFormatting = PropertyFormatting::SingleLine;
	EmptyLineFormatting EmptyLineFormatting = EmptyLineFormatting::DoNotChange;
	bool IndentPreprocessorDirectives = false;
	bool AlignToMemberReferenceDot = false;
	bool IndentBlocksInsideExpressions = false;

	// #region Braces
	BraceStyle NamespaceBraceStyle = BraceStyle::EndOfLine;
	BraceStyle ClassBraceStyle = BraceStyle::EndOfLine;
	BraceStyle InterfaceBraceStyle = BraceStyle::EndOfLine;
	BraceStyle StructBraceStyle = BraceStyle::EndOfLine;
	BraceStyle EnumBraceStyle = BraceStyle::EndOfLine;
	BraceStyle MethodBraceStyle = BraceStyle::EndOfLine;
	BraceStyle AnonymousMethodBraceStyle = BraceStyle::EndOfLine;
	BraceStyle ConstructorBraceStyle = BraceStyle::EndOfLine;
	BraceStyle DestructorBraceStyle = BraceStyle::EndOfLine;
	BraceStyle PropertyBraceStyle = BraceStyle::EndOfLine;
	BraceStyle PropertyGetBraceStyle = BraceStyle::EndOfLine;
	BraceStyle PropertySetBraceStyle = BraceStyle::EndOfLine;
	PropertyFormatting SimpleGetBlockFormatting = PropertyFormatting::SingleLine;
	PropertyFormatting SimpleSetBlockFormatting = PropertyFormatting::SingleLine;
	BraceStyle EventBraceStyle = BraceStyle::EndOfLine;
	BraceStyle EventAddBraceStyle = BraceStyle::EndOfLine;
	BraceStyle EventRemoveBraceStyle = BraceStyle::EndOfLine;
	bool AllowEventAddBlockInline = false;
	bool AllowEventRemoveBlockInline = false;
	BraceStyle StatementBraceStyle = BraceStyle::EndOfLine;
	bool AllowIfBlockInline = false;
	bool AllowOneLinedArrayInitialziers = true;

	// #region NewLines
	NewLinePlacement ElseNewLinePlacement = NewLinePlacement::DoNotCare;
	NewLinePlacement ElseIfNewLinePlacement = NewLinePlacement::DoNotCare;
	NewLinePlacement CatchNewLinePlacement = NewLinePlacement::DoNotCare;
	NewLinePlacement FinallyNewLinePlacement = NewLinePlacement::DoNotCare;
	NewLinePlacement WhileNewLinePlacement = NewLinePlacement::DoNotCare;
	NewLinePlacement EmbeddedStatementPlacement = NewLinePlacement::NewLine;

	// #region Spaces
	bool SpaceBetweenParameterAttributeSections = false;
	bool SpaceBeforeMethodDeclarationParentheses = false;
	bool SpaceBetweenEmptyMethodDeclarationParentheses = false;
	bool SpaceBeforeMethodDeclarationParameterComma = false;
	bool SpaceAfterMethodDeclarationParameterComma = false;
	bool SpaceWithinMethodDeclarationParentheses = false;
	bool SpaceBeforeMethodCallParentheses = false;
	bool SpaceBetweenEmptyMethodCallParentheses = false;
	bool SpaceBeforeMethodCallParameterComma = false;
	bool SpaceAfterMethodCallParameterComma = false;
	bool SpaceWithinMethodCallParentheses = false;
	bool SpaceBeforeFieldDeclarationComma = false;
	bool SpaceAfterFieldDeclarationComma = false;
	bool SpaceBeforeLocalVariableDeclarationComma = false;
	bool SpaceAfterLocalVariableDeclarationComma = false;
	bool SpaceBeforeConstructorDeclarationParentheses = false;
	bool SpaceBetweenEmptyConstructorDeclarationParentheses = false;
	bool SpaceBeforeConstructorDeclarationParameterComma = false;
	bool SpaceAfterConstructorDeclarationParameterComma = false;
	bool SpaceWithinConstructorDeclarationParentheses = false;
	NewLinePlacement NewLineBeforeConstructorInitializerColon = NewLinePlacement::DoNotCare;
	NewLinePlacement NewLineAfterConstructorInitializerColon = NewLinePlacement::DoNotCare;
	bool SpaceBeforeIndexerDeclarationBracket = false;
	bool SpaceWithinIndexerDeclarationBracket = false;
	bool SpaceBeforeIndexerDeclarationParameterComma = false;
	bool SpaceAfterIndexerDeclarationParameterComma = false;
	bool SpaceBeforeDelegateDeclarationParentheses = false;
	bool SpaceBetweenEmptyDelegateDeclarationParentheses = false;
	bool SpaceBeforeDelegateDeclarationParameterComma = false;
	bool SpaceAfterDelegateDeclarationParameterComma = false;
	bool SpaceWithinDelegateDeclarationParentheses = false;
	bool SpaceBeforeNewParentheses = false;
	bool SpaceBeforeIfParentheses = false;
	bool SpaceBeforeWhileParentheses = false;
	bool SpaceBeforeForParentheses = false;
	bool SpaceBeforeForeachParentheses = false;
	bool SpaceBeforeCatchParentheses = false;
	bool SpaceBeforeSwitchParentheses = false;
	bool SpaceBeforeLockParentheses = false;
	bool SpaceBeforeUsingParentheses = false;
	bool SpaceAroundAssignment = false;
	bool SpaceAroundLogicalOperator = false;
	bool SpaceAroundEqualityOperator = false;
	bool SpaceAroundRelationalOperator = false;
	bool SpaceAroundBitwiseOperator = false;
	bool SpaceAroundAdditiveOperator = false;
	bool SpaceAroundMultiplicativeOperator = false;
	bool SpaceAroundShiftOperator = false;
	bool SpaceAroundNullCoalescingOperator = false;
	bool SpaceAfterUnsafeAddressOfOperator = false;
	bool SpaceAfterUnsafeAsteriskOfOperator = false;
	bool SpaceAroundUnsafeArrowOperator = false;
	bool SpacesWithinParentheses = false;
	bool SpacesWithinIfParentheses = false;
	bool SpacesWithinWhileParentheses = false;
	bool SpacesWithinForParentheses = false;
	bool SpacesWithinForeachParentheses = false;
	bool SpacesWithinCatchParentheses = false;
	bool SpacesWithinSwitchParentheses = false;
	bool SpacesWithinLockParentheses = false;
	bool SpacesWithinUsingParentheses = false;
	bool SpacesWithinCastParentheses = false;
	bool SpacesWithinSizeOfParentheses = false;
	bool SpaceBeforeSizeOfParentheses = false;
	bool SpacesWithinTypeOfParentheses = false;
	bool SpacesWithinNewParentheses = false;
	bool SpacesBetweenEmptyNewParentheses = false;
	bool SpaceBeforeNewParameterComma = false;
	bool SpaceAfterNewParameterComma = false;
	bool SpaceBeforeTypeOfParentheses = false;
	bool SpacesWithinCheckedExpressionParantheses = false;
	bool SpaceBeforeConditionalOperatorCondition = false;
	bool SpaceAfterConditionalOperatorCondition = false;
	bool SpaceBeforeConditionalOperatorSeparator = false;
	bool SpaceAfterConditionalOperatorSeparator = false;
	bool SpaceBeforeAnonymousMethodParentheses = false;
	bool SpaceWithinAnonymousMethodParentheses = false;
	bool SpacesWithinBrackets = false;
	bool SpacesBeforeBrackets = false;
	bool SpaceBeforeBracketComma = false;
	bool SpaceAfterBracketComma = false;
	bool SpaceBeforeForSemicolon = false;
	bool SpaceAfterForSemicolon = false;
	bool SpaceAfterTypecast = false;
	bool SpaceBeforeArrayDeclarationBrackets = false;
	bool SpaceInNamedArgumentAfterDoubleColon = false;
	bool RemoveEndOfLineWhiteSpace = false;
	bool SpaceBeforeSemicolon = false;

	// #region Blank Lines
	int MinimumBlankLinesBeforeUsings = 0;
	int MinimumBlankLinesAfterUsings = 0;
	int MinimumBlankLinesBeforeFirstDeclaration = 0;
	int MinimumBlankLinesBetweenTypes = 0;
	int MinimumBlankLinesBetweenFields = 0;
	int MinimumBlankLinesBetweenEventFields = 0;
	int MinimumBlankLinesBetweenMembers = 0;
	int MinimumBlankLinesAroundRegion = 0;
	int MinimumBlankLinesInsideRegion = 0;

	// #region Keep formatting
	bool KeepCommentsAtFirstColumn = false;

	// #region Wrapping
	Wrapping ArrayInitializerWrapping = Wrapping::DoNotWrap;
	BraceStyle ArrayInitializerBraceStyle = BraceStyle::EndOfLine;
	Wrapping ChainedMethodCallWrapping = Wrapping::DoNotWrap;
	Wrapping MethodCallArgumentWrapping = Wrapping::DoNotWrap;
	NewLinePlacement NewLineAferMethodCallOpenParentheses = NewLinePlacement::DoNotCare;
	NewLinePlacement MethodCallClosingParenthesesOnNewLine = NewLinePlacement::DoNotCare;
	Wrapping IndexerArgumentWrapping = Wrapping::DoNotWrap;
	NewLinePlacement NewLineAferIndexerOpenBracket = NewLinePlacement::DoNotCare;
	NewLinePlacement IndexerClosingBracketOnNewLine = NewLinePlacement::DoNotCare;
	Wrapping MethodDeclarationParameterWrapping = Wrapping::DoNotWrap;
	NewLinePlacement NewLineAferMethodDeclarationOpenParentheses = NewLinePlacement::DoNotCare;
	NewLinePlacement MethodDeclarationClosingParenthesesOnNewLine = NewLinePlacement::DoNotCare;
	Wrapping IndexerDeclarationParameterWrapping = Wrapping::DoNotWrap;
	NewLinePlacement NewLineAferIndexerDeclarationOpenBracket = NewLinePlacement::DoNotCare;
	NewLinePlacement IndexerDeclarationClosingBracketOnNewLine = NewLinePlacement::DoNotCare;
	bool AlignToFirstIndexerArgument = false;
	bool AlignToFirstIndexerDeclarationParameter = false;
	bool AlignToFirstMethodCallArgument = false;
	bool AlignToFirstMethodDeclarationParameter = false;
	NewLinePlacement NewLineBeforeNewQueryClause = NewLinePlacement::DoNotCare;

	// #region Using Declarations
	UsingPlacement UsingPlacement = UsingPlacement::TopOfFile;
};

}  // namespace ILSpy::Decompiler::CSharp::OutputVisitor

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPFORMATTINGOPTIONS_HPP
