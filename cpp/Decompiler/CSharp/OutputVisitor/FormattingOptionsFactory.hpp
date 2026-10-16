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

// Port of `FormattingOptionsFactory` (the pre-defined formatting-style factory) in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.cs -- the companion to
// `CSharpFormattingOptions` (the previous header). The C# `public static class
// FormattingOptionsFactory` declares seven `static` factory methods (`CreateEmpty`,
// `CreateMono`, `CreateKRStyle`, `CreateSharpDevelop`, `CreateAllman`, `CreateWhitesmiths`,
// `CreateGNU`) that each return a `CSharpFormattingOptions` configured for a named style; the
// `CSharpOutputVisitor` (the pretty-printer, not yet ported) is constructed with one of these
// (the `ilspycmd` CLI passes `FormattingOptionsFactory.CreateMono()` by default).
//
// The C# `public static class` ports as a C++ `namespace` of free functions (the idiomatic
// equivalent -- a static class is a container for static methods, and a namespace is the C++
// container for free functions). Each factory returns a `CSharpFormattingOptions` by value (the
// C# returns a `new CSharpFormattingOptions { ... }` instance; the port default-constructs then
// assigns the per-style members -- the C# object-initializer shape, with the copy elided on the
// return). The `CreateAllman`/`CreateWhitesmiths`/`CreateGNU`/`CreateSharpDevelop` styles are
// derived from `CreateKRStyle`/`CreateAllman` (the C# `var baseOptions = CreateKRStyle(); ...
// return baseOptions;` shape), so each ported factory calls its base factory then overrides the
// divergent members (the C# `baseOptions.X = Y` shape).
//
// Every member the C# factory sets is reproduced verbatim (the per-style values are the
// load-bearing public contract the output visitor reads); the members the C# factory does NOT set
// keep the `CSharpFormattingOptions` defaults (false / 0 / the zero enum value, set by the
// in-class member initializers in `CSharpFormattingOptions.hpp`). This header is header-only
// (the factory functions are small enough to inline; no CMake source listing is needed -- the
// `TokenWriter`/`CSharpFormattingOptions` header-only precedent), so the `CSharp` `CMakeLists.txt`
// is unchanged; only the test `.cpp` is added to `tests/CMakeLists.txt`.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_FORMATTINGOPTIONSFACTORY_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_FORMATTINGOPTIONSFACTORY_HPP

#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"

namespace ILSpy::Decompiler::CSharp::OutputVisitor::FormattingOptionsFactory {

// The C# `CreateEmpty()` -- a `CSharpFormattingOptions` with all defaults (the in-class member
// initializers in `CSharpFormattingOptions.hpp` reproduce the C# `new CSharpFormattingOptions()`
// all-default instance).
inline CSharpFormattingOptions CreateEmpty() {
	return CSharpFormattingOptions{};
}

// The C# `CreateMono()` -- the Mono indent style (the `ilspycmd` CLI default).
inline CSharpFormattingOptions CreateMono() {
	CSharpFormattingOptions o;
	o.IndentNamespaceBody = true;
	o.IndentClassBody = true;
	o.IndentInterfaceBody = true;
	o.IndentStructBody = true;
	o.IndentEnumBody = true;
	o.IndentMethodBody = true;
	o.IndentPropertyBody = true;
	o.IndentEventBody = true;
	o.IndentBlocks = true;
	o.IndentSwitchBody = false;
	o.IndentCaseBody = true;
	o.IndentBreakStatements = true;
	o.IndentPreprocessorDirectives = true;
	o.IndentBlocksInsideExpressions = false;
	o.NamespaceBraceStyle = BraceStyle::NextLine;
	o.ClassBraceStyle = BraceStyle::NextLine;
	o.InterfaceBraceStyle = BraceStyle::NextLine;
	o.StructBraceStyle = BraceStyle::NextLine;
	o.EnumBraceStyle = BraceStyle::NextLine;
	o.MethodBraceStyle = BraceStyle::NextLine;
	o.ConstructorBraceStyle = BraceStyle::NextLine;
	o.DestructorBraceStyle = BraceStyle::NextLine;
	o.AnonymousMethodBraceStyle = BraceStyle::EndOfLine;
	o.PropertyBraceStyle = BraceStyle::EndOfLine;
	o.PropertyGetBraceStyle = BraceStyle::EndOfLine;
	o.PropertySetBraceStyle = BraceStyle::EndOfLine;
	o.SimpleGetBlockFormatting = PropertyFormatting::SingleLine;
	o.SimpleSetBlockFormatting = PropertyFormatting::SingleLine;
	o.EventBraceStyle = BraceStyle::EndOfLine;
	o.EventAddBraceStyle = BraceStyle::EndOfLine;
	o.EventRemoveBraceStyle = BraceStyle::EndOfLine;
	o.AllowEventAddBlockInline = true;
	o.AllowEventRemoveBlockInline = true;
	o.StatementBraceStyle = BraceStyle::EndOfLine;
	o.ElseNewLinePlacement = NewLinePlacement::SameLine;
	o.ElseIfNewLinePlacement = NewLinePlacement::SameLine;
	o.CatchNewLinePlacement = NewLinePlacement::SameLine;
	o.FinallyNewLinePlacement = NewLinePlacement::SameLine;
	o.WhileNewLinePlacement = NewLinePlacement::SameLine;
	o.ArrayInitializerWrapping = Wrapping::WrapIfTooLong;
	o.ArrayInitializerBraceStyle = BraceStyle::EndOfLine;
	o.AllowOneLinedArrayInitialziers = true;
	o.SpaceBeforeMethodCallParentheses = true;
	o.SpaceBeforeMethodDeclarationParentheses = true;
	o.SpaceBeforeConstructorDeclarationParentheses = true;
	o.SpaceBeforeDelegateDeclarationParentheses = true;
	o.SpaceAfterMethodCallParameterComma = true;
	o.SpaceAfterConstructorDeclarationParameterComma = true;
	o.SpaceBeforeNewParentheses = true;
	o.SpacesWithinNewParentheses = false;
	o.SpacesBetweenEmptyNewParentheses = false;
	o.SpaceBeforeNewParameterComma = false;
	o.SpaceAfterNewParameterComma = true;
	o.SpaceBeforeIfParentheses = true;
	o.SpaceBeforeWhileParentheses = true;
	o.SpaceBeforeForParentheses = true;
	o.SpaceBeforeForeachParentheses = true;
	o.SpaceBeforeCatchParentheses = true;
	o.SpaceBeforeSwitchParentheses = true;
	o.SpaceBeforeLockParentheses = true;
	o.SpaceBeforeUsingParentheses = true;
	o.SpaceAroundAssignment = true;
	o.SpaceAroundLogicalOperator = true;
	o.SpaceAroundEqualityOperator = true;
	o.SpaceAroundRelationalOperator = true;
	o.SpaceAroundBitwiseOperator = true;
	o.SpaceAroundAdditiveOperator = true;
	o.SpaceAroundMultiplicativeOperator = true;
	o.SpaceAroundShiftOperator = true;
	o.SpaceAroundNullCoalescingOperator = true;
	o.SpacesWithinParentheses = false;
	o.SpaceWithinMethodCallParentheses = false;
	o.SpaceWithinMethodDeclarationParentheses = false;
	o.SpacesWithinIfParentheses = false;
	o.SpacesWithinWhileParentheses = false;
	o.SpacesWithinForParentheses = false;
	o.SpacesWithinForeachParentheses = false;
	o.SpacesWithinCatchParentheses = false;
	o.SpacesWithinSwitchParentheses = false;
	o.SpacesWithinLockParentheses = false;
	o.SpacesWithinUsingParentheses = false;
	o.SpacesWithinCastParentheses = false;
	o.SpacesWithinSizeOfParentheses = false;
	o.SpacesWithinTypeOfParentheses = false;
	o.SpacesWithinCheckedExpressionParantheses = false;
	o.SpaceBeforeConditionalOperatorCondition = true;
	o.SpaceAfterConditionalOperatorCondition = true;
	o.SpaceBeforeConditionalOperatorSeparator = true;
	o.SpaceAfterConditionalOperatorSeparator = true;
	o.SpacesWithinBrackets = false;
	o.SpacesBeforeBrackets = true;
	o.SpaceBeforeBracketComma = false;
	o.SpaceAfterBracketComma = true;
	o.SpaceBeforeForSemicolon = false;
	o.SpaceAfterForSemicolon = true;
	o.SpaceAfterTypecast = false;
	o.AlignEmbeddedStatements = true;
	o.SimplePropertyFormatting = PropertyFormatting::SingleLine;
	o.AutoPropertyFormatting = PropertyFormatting::SingleLine;
	o.EmptyLineFormatting = EmptyLineFormatting::DoNotIndent;
	o.SpaceBeforeMethodDeclarationParameterComma = false;
	o.SpaceAfterMethodDeclarationParameterComma = true;
	o.SpaceAfterDelegateDeclarationParameterComma = true;
	o.SpaceBeforeFieldDeclarationComma = false;
	o.SpaceAfterFieldDeclarationComma = true;
	o.SpaceBeforeLocalVariableDeclarationComma = false;
	o.SpaceAfterLocalVariableDeclarationComma = true;
	o.SpaceBeforeIndexerDeclarationBracket = true;
	o.SpaceWithinIndexerDeclarationBracket = false;
	o.SpaceBeforeIndexerDeclarationParameterComma = false;
	o.SpaceInNamedArgumentAfterDoubleColon = true;
	o.RemoveEndOfLineWhiteSpace = true;
	o.SpaceAfterIndexerDeclarationParameterComma = true;
	o.MinimumBlankLinesBeforeUsings = 0;
	o.MinimumBlankLinesAfterUsings = 1;
	o.UsingPlacement = UsingPlacement::TopOfFile;
	o.MinimumBlankLinesBeforeFirstDeclaration = 0;
	o.MinimumBlankLinesBetweenTypes = 1;
	o.MinimumBlankLinesBetweenFields = 0;
	o.MinimumBlankLinesBetweenEventFields = 0;
	o.MinimumBlankLinesBetweenMembers = 1;
	o.MinimumBlankLinesAroundRegion = 1;
	o.MinimumBlankLinesInsideRegion = 1;
	o.AlignToFirstIndexerArgument = false;
	o.AlignToFirstIndexerDeclarationParameter = true;
	o.AlignToFirstMethodCallArgument = false;
	o.AlignToFirstMethodDeclarationParameter = true;
	o.KeepCommentsAtFirstColumn = true;
	o.ChainedMethodCallWrapping = Wrapping::DoNotWrap;
	o.MethodCallArgumentWrapping = Wrapping::DoNotWrap;
	o.NewLineAferMethodCallOpenParentheses = NewLinePlacement::DoNotCare;
	o.MethodCallClosingParenthesesOnNewLine = NewLinePlacement::DoNotCare;
	o.IndexerArgumentWrapping = Wrapping::DoNotWrap;
	o.NewLineAferIndexerOpenBracket = NewLinePlacement::DoNotCare;
	o.IndexerClosingBracketOnNewLine = NewLinePlacement::DoNotCare;
	o.NewLineBeforeNewQueryClause = NewLinePlacement::NewLine;
	return o;
}

// The C# `CreateKRStyle()` -- the K&R style (the base for `CreateSharpDevelop`/`CreateAllman`/
// `CreateWhitesmiths`/`CreateGNU`).
inline CSharpFormattingOptions CreateKRStyle() {
	CSharpFormattingOptions o;
	o.IndentNamespaceBody = true;
	o.IndentClassBody = true;
	o.IndentInterfaceBody = true;
	o.IndentStructBody = true;
	o.IndentEnumBody = true;
	o.IndentMethodBody = true;
	o.IndentPropertyBody = true;
	o.IndentEventBody = true;
	o.IndentBlocks = true;
	o.IndentSwitchBody = true;
	o.IndentCaseBody = true;
	o.IndentBreakStatements = true;
	o.IndentPreprocessorDirectives = true;
	o.NamespaceBraceStyle = BraceStyle::NextLine;
	o.ClassBraceStyle = BraceStyle::NextLine;
	o.InterfaceBraceStyle = BraceStyle::NextLine;
	o.StructBraceStyle = BraceStyle::NextLine;
	o.EnumBraceStyle = BraceStyle::NextLine;
	o.MethodBraceStyle = BraceStyle::NextLine;
	o.ConstructorBraceStyle = BraceStyle::NextLine;
	o.DestructorBraceStyle = BraceStyle::NextLine;
	o.AnonymousMethodBraceStyle = BraceStyle::EndOfLine;
	o.PropertyBraceStyle = BraceStyle::EndOfLine;
	o.PropertyGetBraceStyle = BraceStyle::EndOfLine;
	o.PropertySetBraceStyle = BraceStyle::EndOfLine;
	o.SimpleGetBlockFormatting = PropertyFormatting::SingleLine;
	o.SimpleSetBlockFormatting = PropertyFormatting::SingleLine;
	o.EventBraceStyle = BraceStyle::EndOfLine;
	o.EventAddBraceStyle = BraceStyle::EndOfLine;
	o.EventRemoveBraceStyle = BraceStyle::EndOfLine;
	o.AllowEventAddBlockInline = true;
	o.AllowEventRemoveBlockInline = true;
	o.StatementBraceStyle = BraceStyle::EndOfLine;
	o.ElseNewLinePlacement = NewLinePlacement::SameLine;
	o.ElseIfNewLinePlacement = NewLinePlacement::SameLine;
	o.CatchNewLinePlacement = NewLinePlacement::SameLine;
	o.FinallyNewLinePlacement = NewLinePlacement::SameLine;
	o.WhileNewLinePlacement = NewLinePlacement::SameLine;
	o.ArrayInitializerWrapping = Wrapping::WrapIfTooLong;
	o.ArrayInitializerBraceStyle = BraceStyle::EndOfLine;
	o.SpaceBeforeMethodCallParentheses = false;
	o.SpaceBeforeMethodDeclarationParentheses = false;
	o.SpaceBeforeConstructorDeclarationParentheses = false;
	o.SpaceBeforeDelegateDeclarationParentheses = false;
	o.SpaceBeforeIndexerDeclarationBracket = false;
	o.SpaceAfterMethodCallParameterComma = true;
	o.SpaceAfterConstructorDeclarationParameterComma = true;
	o.NewLineBeforeConstructorInitializerColon = NewLinePlacement::NewLine;
	o.NewLineAfterConstructorInitializerColon = NewLinePlacement::SameLine;
	o.SpaceBeforeNewParentheses = false;
	o.SpacesWithinNewParentheses = false;
	o.SpacesBetweenEmptyNewParentheses = false;
	o.SpaceBeforeNewParameterComma = false;
	o.SpaceAfterNewParameterComma = true;
	o.SpaceBeforeIfParentheses = true;
	o.SpaceBeforeWhileParentheses = true;
	o.SpaceBeforeForParentheses = true;
	o.SpaceBeforeForeachParentheses = true;
	o.SpaceBeforeCatchParentheses = true;
	o.SpaceBeforeSwitchParentheses = true;
	o.SpaceBeforeLockParentheses = true;
	o.SpaceBeforeUsingParentheses = true;
	o.SpaceAroundAssignment = true;
	o.SpaceAroundLogicalOperator = true;
	o.SpaceAroundEqualityOperator = true;
	o.SpaceAroundRelationalOperator = true;
	o.SpaceAroundBitwiseOperator = true;
	o.SpaceAroundAdditiveOperator = true;
	o.SpaceAroundMultiplicativeOperator = true;
	o.SpaceAroundShiftOperator = true;
	o.SpaceAroundNullCoalescingOperator = true;
	o.SpacesWithinParentheses = false;
	o.SpaceWithinMethodCallParentheses = false;
	o.SpaceWithinMethodDeclarationParentheses = false;
	o.SpacesWithinIfParentheses = false;
	o.SpacesWithinWhileParentheses = false;
	o.SpacesWithinForParentheses = false;
	o.SpacesWithinForeachParentheses = false;
	o.SpacesWithinCatchParentheses = false;
	o.SpacesWithinSwitchParentheses = false;
	o.SpacesWithinLockParentheses = false;
	o.SpacesWithinUsingParentheses = false;
	o.SpacesWithinCastParentheses = false;
	o.SpacesWithinSizeOfParentheses = false;
	o.SpacesWithinTypeOfParentheses = false;
	o.SpacesWithinCheckedExpressionParantheses = false;
	o.SpaceBeforeConditionalOperatorCondition = true;
	o.SpaceAfterConditionalOperatorCondition = true;
	o.SpaceBeforeConditionalOperatorSeparator = true;
	o.SpaceAfterConditionalOperatorSeparator = true;
	o.SpaceBeforeArrayDeclarationBrackets = false;
	o.SpacesWithinBrackets = false;
	o.SpacesBeforeBrackets = false;
	o.SpaceBeforeBracketComma = false;
	o.SpaceAfterBracketComma = true;
	o.SpaceBeforeForSemicolon = false;
	o.SpaceAfterForSemicolon = true;
	o.SpaceAfterTypecast = false;
	o.AlignEmbeddedStatements = true;
	o.SimplePropertyFormatting = PropertyFormatting::SingleLine;
	o.AutoPropertyFormatting = PropertyFormatting::SingleLine;
	o.EmptyLineFormatting = EmptyLineFormatting::DoNotIndent;
	o.SpaceBeforeMethodDeclarationParameterComma = false;
	o.SpaceAfterMethodDeclarationParameterComma = true;
	o.SpaceAfterDelegateDeclarationParameterComma = true;
	o.SpaceBeforeFieldDeclarationComma = false;
	o.SpaceAfterFieldDeclarationComma = true;
	o.SpaceBeforeLocalVariableDeclarationComma = false;
	o.SpaceAfterLocalVariableDeclarationComma = true;
	o.SpaceWithinIndexerDeclarationBracket = false;
	o.SpaceBeforeIndexerDeclarationParameterComma = false;
	o.SpaceInNamedArgumentAfterDoubleColon = true;
	o.SpaceAfterIndexerDeclarationParameterComma = true;
	o.RemoveEndOfLineWhiteSpace = true;
	o.MinimumBlankLinesBeforeUsings = 0;
	o.MinimumBlankLinesAfterUsings = 1;
	o.MinimumBlankLinesBeforeFirstDeclaration = 0;
	o.MinimumBlankLinesBetweenTypes = 1;
	o.MinimumBlankLinesBetweenFields = 0;
	o.MinimumBlankLinesBetweenEventFields = 0;
	o.MinimumBlankLinesBetweenMembers = 1;
	o.MinimumBlankLinesAroundRegion = 1;
	o.MinimumBlankLinesInsideRegion = 1;
	o.KeepCommentsAtFirstColumn = true;
	o.ChainedMethodCallWrapping = Wrapping::DoNotWrap;
	o.MethodCallArgumentWrapping = Wrapping::DoNotWrap;
	o.NewLineAferMethodCallOpenParentheses = NewLinePlacement::DoNotCare;
	o.MethodCallClosingParenthesesOnNewLine = NewLinePlacement::DoNotCare;
	o.IndexerArgumentWrapping = Wrapping::DoNotWrap;
	o.NewLineAferIndexerOpenBracket = NewLinePlacement::DoNotCare;
	o.IndexerClosingBracketOnNewLine = NewLinePlacement::DoNotCare;
	o.NewLineBeforeNewQueryClause = NewLinePlacement::NewLine;
	return o;
}

// The C# `CreateSharpDevelop()` -- the SharpDevelop style (the K&R style, returned unchanged).
inline CSharpFormattingOptions CreateSharpDevelop() {
	return CreateKRStyle();
}

// The C# `CreateAllman()` -- the Allman indent style used in Visual Studio (the K&R style with
// the brace/catch/else placements shifted to `NextLine`).
inline CSharpFormattingOptions CreateAllman() {
	CSharpFormattingOptions o = CreateKRStyle();
	o.AnonymousMethodBraceStyle = BraceStyle::NextLine;
	o.PropertyBraceStyle = BraceStyle::NextLine;
	o.PropertyGetBraceStyle = BraceStyle::NextLine;
	o.PropertySetBraceStyle = BraceStyle::NextLine;
	o.EventBraceStyle = BraceStyle::NextLine;
	o.EventAddBraceStyle = BraceStyle::NextLine;
	o.EventRemoveBraceStyle = BraceStyle::NextLine;
	o.StatementBraceStyle = BraceStyle::NextLine;
	o.ArrayInitializerBraceStyle = BraceStyle::NextLine;
	o.CatchNewLinePlacement = NewLinePlacement::NewLine;
	o.ElseNewLinePlacement = NewLinePlacement::NewLine;
	o.ElseIfNewLinePlacement = NewLinePlacement::SameLine;
	o.FinallyNewLinePlacement = NewLinePlacement::NewLine;
	o.WhileNewLinePlacement = NewLinePlacement::DoNotCare;
	o.ArrayInitializerWrapping = Wrapping::DoNotWrap;
	o.IndentBlocksInsideExpressions = true;
	return o;
}

// The C# `CreateWhitesmiths()` -- the Whitesmiths style (the K&R style with the braces shifted to
// `NextLineShifted`).
inline CSharpFormattingOptions CreateWhitesmiths() {
	CSharpFormattingOptions o = CreateKRStyle();
	o.NamespaceBraceStyle = BraceStyle::NextLineShifted;
	o.ClassBraceStyle = BraceStyle::NextLineShifted;
	o.InterfaceBraceStyle = BraceStyle::NextLineShifted;
	o.StructBraceStyle = BraceStyle::NextLineShifted;
	o.EnumBraceStyle = BraceStyle::NextLineShifted;
	o.MethodBraceStyle = BraceStyle::NextLineShifted;
	o.ConstructorBraceStyle = BraceStyle::NextLineShifted;
	o.DestructorBraceStyle = BraceStyle::NextLineShifted;
	o.AnonymousMethodBraceStyle = BraceStyle::NextLineShifted;
	o.PropertyBraceStyle = BraceStyle::NextLineShifted;
	o.PropertyGetBraceStyle = BraceStyle::NextLineShifted;
	o.PropertySetBraceStyle = BraceStyle::NextLineShifted;
	o.EventBraceStyle = BraceStyle::NextLineShifted;
	o.EventAddBraceStyle = BraceStyle::NextLineShifted;
	o.EventRemoveBraceStyle = BraceStyle::NextLineShifted;
	o.StatementBraceStyle = BraceStyle::NextLineShifted;
	o.IndentBlocksInsideExpressions = true;
	return o;
}

// The C# `CreateGNU()` -- the GNU style (the Allman style with the statement braces shifted to
// `NextLineShifted2`).
inline CSharpFormattingOptions CreateGNU() {
	CSharpFormattingOptions o = CreateAllman();
	o.StatementBraceStyle = BraceStyle::NextLineShifted2;
	return o;
}

}  // namespace ILSpy::Decompiler::CSharp::OutputVisitor::FormattingOptionsFactory

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_FORMATTINGOPTIONSFACTORY_HPP
