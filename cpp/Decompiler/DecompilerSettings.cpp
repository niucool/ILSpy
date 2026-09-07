// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//
// The out-of-line halves of the DecompilerSettings port (the accessor pairs and the
// version-gating if-ladder are mechanical; the definitions live here so the ~121-member
// class keeps one compare-then-write site per setter and the header stays declarative).

#include "Decompiler/DecompilerSettings.hpp"

#include <stdexcept>

#include "Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.hpp"

namespace ILSpy::Decompiler {

DecompilerSettings::DecompilerSettings(CSharp::LanguageVersion languageVersion)
{
	SetLanguageVersion(languageVersion);
}

void DecompilerSettings::SetLanguageVersion(CSharp::LanguageVersion languageVersion)
{
	// By default, all decompiler features are enabled.
	// Disable some of them based on language version:
	if (languageVersion < CSharp::LanguageVersion::CSharp2)
	{
		anonymousMethods_ = false;
		liftNullables_ = false;
		yieldReturn_ = false;
		useImplicitMethodGroupConversion_ = false;
		useObjectCreationOfGenericTypeParameter_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp3)
	{
		anonymousTypes_ = false;
		useLambdaSyntax_ = false;
		objectCollectionInitializers_ = false;
		automaticProperties_ = false;
		extensionMethods_ = false;
		queryExpressions_ = false;
		expressionTrees_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp4)
	{
		dynamic_ = false;
		namedArguments_ = false;
		optionalArguments_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp5)
	{
		asyncAwait_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp6)
	{
		awaitInCatchFinally_ = false;
		useExpressionBodyForCalculatedGetterOnlyProperties_ = false;
		nullPropagation_ = false;
		stringInterpolation_ = false;
		dictionaryInitializers_ = false;
		extensionMethodsInCollectionInitializers_ = false;
		getterOnlyAutomaticProperties_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp7)
	{
		outVariables_ = false;
		throwExpressions_ = false;
		tupleTypes_ = false;
		tupleConversions_ = false;
		discards_ = false;
		localFunctions_ = false;
		deconstruction_ = false;
		patternMatching_ = false;
		useRefLocalsForAccurateOrderOfEvaluation_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp7_2)
	{
		introduceReadonlyAndInModifiers_ = false;
		introduceRefModifiersOnStructs_ = false;
		nonTrailingNamedArguments_ = false;
		refExtensionMethods_ = false;
		introducePrivateProtectedAccessibilty_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp7_3)
	{
		introduceUnmanagedConstraint_ = false;
		stackAllocInitializers_ = false;
		tupleComparisons_ = false;
		patternBasedFixedStatement_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp8_0)
	{
		nullableReferenceTypes_ = false;
		readOnlyMethods_ = false;
		asyncUsingAndForEachStatement_ = false;
		asyncEnumerator_ = false;
		useEnhancedUsing_ = false;
		staticLocalFunctions_ = false;
		ranges_ = false;
		switchExpressions_ = false;
		recursivePatternMatching_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp9_0)
	{
		nativeIntegers_ = false;
		initAccessors_ = false;
		functionPointers_ = false;
		forEachWithGetEnumeratorExtension_ = false;
		recordClasses_ = false;
		withExpressions_ = false;
		usePrimaryConstructorSyntax_ = false;
		covariantReturns_ = false;
		relationalPatterns_ = false;
		patternCombinators_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp10_0)
	{
		fileScopedNamespaces_ = false;
		recordStructs_ = false;
		structDefaultConstructorsAndFieldInitializers_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp11_0)
	{
		scopedRef_ = false;
		requiredMembers_ = false;
		numericIntPtr_ = false;
		utf8StringLiterals_ = false;
		unsignedRightShift_ = false;
		checkedOperators_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp12_0)
	{
		refReadOnlyParameters_ = false;
		usePrimaryConstructorSyntaxForNonRecordTypes_ = false;
		inlineArrays_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp13_0)
	{
		paramsCollections_ = false;
	}
	if (languageVersion < CSharp::LanguageVersion::CSharp14_0)
	{
		extensionMembers_ = false;
		firstClassSpanTypes_ = false;
	}
}

CSharp::LanguageVersion DecompilerSettings::GetMinimumRequiredVersion() const
{
	if (extensionMembers_ || firstClassSpanTypes_)
		return CSharp::LanguageVersion::CSharp14_0;
	if (paramsCollections_)
		return CSharp::LanguageVersion::CSharp13_0;
	if (refReadOnlyParameters_ || usePrimaryConstructorSyntaxForNonRecordTypes_ || inlineArrays_)
		return CSharp::LanguageVersion::CSharp12_0;
	if (scopedRef_ || requiredMembers_ || numericIntPtr_ || utf8StringLiterals_ || unsignedRightShift_ || checkedOperators_)
		return CSharp::LanguageVersion::CSharp11_0;
	if (fileScopedNamespaces_ || recordStructs_ || structDefaultConstructorsAndFieldInitializers_)
		return CSharp::LanguageVersion::CSharp10_0;
	if (nativeIntegers_ || initAccessors_ || functionPointers_ || forEachWithGetEnumeratorExtension_ || recordClasses_ || withExpressions_ || usePrimaryConstructorSyntax_ || covariantReturns_ || relationalPatterns_ || patternCombinators_)
		return CSharp::LanguageVersion::CSharp9_0;
	if (nullableReferenceTypes_ || readOnlyMethods_ || asyncEnumerator_ || asyncUsingAndForEachStatement_ || staticLocalFunctions_ || ranges_ || switchExpressions_ || recursivePatternMatching_)
		return CSharp::LanguageVersion::CSharp8_0;
	if (introduceUnmanagedConstraint_ || tupleComparisons_ || stackAllocInitializers_ || patternBasedFixedStatement_)
		return CSharp::LanguageVersion::CSharp7_3;
	if (introduceRefModifiersOnStructs_ || introduceReadonlyAndInModifiers_ || nonTrailingNamedArguments_ || refExtensionMethods_ || introducePrivateProtectedAccessibilty_)
		return CSharp::LanguageVersion::CSharp7_2;
	if (outVariables_ || throwExpressions_ || tupleTypes_ || tupleConversions_ || discards_ || localFunctions_ || deconstruction_ || patternMatching_ || useRefLocalsForAccurateOrderOfEvaluation_)
		return CSharp::LanguageVersion::CSharp7;
	if (awaitInCatchFinally_ || useExpressionBodyForCalculatedGetterOnlyProperties_ || nullPropagation_ || stringInterpolation_ || dictionaryInitializers_ || extensionMethodsInCollectionInitializers_ || getterOnlyAutomaticProperties_)
		return CSharp::LanguageVersion::CSharp6;
	if (asyncAwait_)
		return CSharp::LanguageVersion::CSharp5;
	if (dynamic_ || namedArguments_ || optionalArguments_)
		return CSharp::LanguageVersion::CSharp4;
	if (anonymousTypes_ || objectCollectionInitializers_ || automaticProperties_ || queryExpressions_ || expressionTrees_)
		return CSharp::LanguageVersion::CSharp3;
	if (anonymousMethods_ || liftNullables_ || yieldReturn_ || useImplicitMethodGroupConversion_ || useObjectCreationOfGenericTypeParameter_)
		return CSharp::LanguageVersion::CSharp2;
	return CSharp::LanguageVersion::CSharp1;
}

CSharp::OutputVisitor::CSharpFormattingOptions& DecompilerSettings::CSharpFormattingOptions()
{
	if (!csharpFormattingOptions_)
	{
		csharpFormattingOptions_ = CSharp::OutputVisitor::FormattingOptionsFactory::CreateAllman();
		csharpFormattingOptions_->IndentSwitchBody = false;
		csharpFormattingOptions_->ArrayInitializerWrapping = CSharp::OutputVisitor::Wrapping::WrapIfTooLong;
		csharpFormattingOptions_->AutoPropertyFormatting = CSharp::OutputVisitor::PropertyFormatting::SingleLine;
	}
	return *csharpFormattingOptions_;
}

void DecompilerSettings::SetCSharpFormattingOptions(std::optional<CSharp::OutputVisitor::CSharpFormattingOptions> value)
{
	// The C# `if (value == null) throw new ArgumentNullException();` (the parameterless
	// ctor, Message "Value cannot be null.").
	if (!value)
		throw std::invalid_argument("Value cannot be null.");
	// The C# `if (csharpFormattingOptions != value)` compares REFERENCES; the port's
	// value-type stand-in has no identity, so the guarded assign reduces to a plain
	// write (the PropertyChanged fire the guard feeds is the documented deferral).
	csharpFormattingOptions_ = value;
}

DecompilerSettings DecompilerSettings::Clone() const
{
	// The C# `MemberwiseClone` + the formatting-options deep copy (`csharpFormattingOptions
	// != null` is the engaged state) + the PropertyChanged reset (the event is the
	// documented deferral, so the reset is a no-op).
	DecompilerSettings settings = *this;
	if (csharpFormattingOptions_)
		settings.csharpFormattingOptions_ = csharpFormattingOptions_->Clone();
	return settings;
}

void DecompilerSettings::SetNativeIntegers(bool value)
{
	if (nativeIntegers_ != value)
	{
		nativeIntegers_ = value;
	}
}

void DecompilerSettings::SetNumericIntPtr(bool value)
{
	if (numericIntPtr_ != value)
	{
		numericIntPtr_ = value;
	}
}

void DecompilerSettings::SetCovariantReturns(bool value)
{
	if (covariantReturns_ != value)
	{
		covariantReturns_ = value;
	}
}

void DecompilerSettings::SetInitAccessors(bool value)
{
	if (initAccessors_ != value)
	{
		initAccessors_ = value;
	}
}

void DecompilerSettings::SetRecordClasses(bool value)
{
	if (recordClasses_ != value)
	{
		recordClasses_ = value;
	}
}

void DecompilerSettings::SetRecordStructs(bool value)
{
	if (recordStructs_ != value)
	{
		recordStructs_ = value;
	}
}

void DecompilerSettings::SetStructDefaultConstructorsAndFieldInitializers(bool value)
{
	if (structDefaultConstructorsAndFieldInitializers_ != value)
	{
		structDefaultConstructorsAndFieldInitializers_ = value;
	}
}

void DecompilerSettings::SetWithExpressions(bool value)
{
	if (withExpressions_ != value)
	{
		withExpressions_ = value;
	}
}

void DecompilerSettings::SetUsePrimaryConstructorSyntax(bool value)
{
	if (usePrimaryConstructorSyntax_ != value)
	{
		usePrimaryConstructorSyntax_ = value;
	}
}

void DecompilerSettings::SetFunctionPointers(bool value)
{
	if (functionPointers_ != value)
	{
		functionPointers_ = value;
	}
}

void DecompilerSettings::SetScopedRef(bool value)
{
	if (scopedRef_ != value)
	{
		scopedRef_ = value;
	}
}

void DecompilerSettings::SetRequiredMembers(bool value)
{
	if (requiredMembers_ != value)
	{
		requiredMembers_ = value;
	}
}

void DecompilerSettings::SetSwitchExpressions(bool value)
{
	if (switchExpressions_ != value)
	{
		switchExpressions_ = value;
	}
}

void DecompilerSettings::SetFileScopedNamespaces(bool value)
{
	if (fileScopedNamespaces_ != value)
	{
		fileScopedNamespaces_ = value;
	}
}

void DecompilerSettings::SetAnonymousMethods(bool value)
{
	if (anonymousMethods_ != value)
	{
		anonymousMethods_ = value;
	}
}

void DecompilerSettings::SetAnonymousTypes(bool value)
{
	if (anonymousTypes_ != value)
	{
		anonymousTypes_ = value;
	}
}

void DecompilerSettings::SetUseLambdaSyntax(bool value)
{
	if (useLambdaSyntax_ != value)
	{
		useLambdaSyntax_ = value;
	}
}

void DecompilerSettings::SetExpressionTrees(bool value)
{
	if (expressionTrees_ != value)
	{
		expressionTrees_ = value;
	}
}

void DecompilerSettings::SetYieldReturn(bool value)
{
	if (yieldReturn_ != value)
	{
		yieldReturn_ = value;
	}
}

void DecompilerSettings::SetDynamic(bool value)
{
	if (dynamic_ != value)
	{
		dynamic_ = value;
	}
}

void DecompilerSettings::SetAsyncAwait(bool value)
{
	if (asyncAwait_ != value)
	{
		asyncAwait_ = value;
	}
}

void DecompilerSettings::SetAwaitInCatchFinally(bool value)
{
	if (awaitInCatchFinally_ != value)
	{
		awaitInCatchFinally_ = value;
	}
}

void DecompilerSettings::SetAsyncEnumerator(bool value)
{
	if (asyncEnumerator_ != value)
	{
		asyncEnumerator_ = value;
	}
}

void DecompilerSettings::SetDecimalConstants(bool value)
{
	if (decimalConstants_ != value)
	{
		decimalConstants_ = value;
	}
}

void DecompilerSettings::SetFixedBuffers(bool value)
{
	if (fixedBuffers_ != value)
	{
		fixedBuffers_ = value;
	}
}

void DecompilerSettings::SetStringConcat(bool value)
{
	if (stringConcat_ != value)
	{
		stringConcat_ = value;
	}
}

void DecompilerSettings::SetLiftNullables(bool value)
{
	if (liftNullables_ != value)
	{
		liftNullables_ = value;
	}
}

void DecompilerSettings::SetNullPropagation(bool value)
{
	if (nullPropagation_ != value)
	{
		nullPropagation_ = value;
	}
}

void DecompilerSettings::SetAutomaticProperties(bool value)
{
	if (automaticProperties_ != value)
	{
		automaticProperties_ = value;
	}
}

void DecompilerSettings::SetGetterOnlyAutomaticProperties(bool value)
{
	if (getterOnlyAutomaticProperties_ != value)
	{
		getterOnlyAutomaticProperties_ = value;
	}
}

void DecompilerSettings::SetAutomaticEvents(bool value)
{
	if (automaticEvents_ != value)
	{
		automaticEvents_ = value;
	}
}

void DecompilerSettings::SetUsingStatement(bool value)
{
	if (usingStatement_ != value)
	{
		usingStatement_ = value;
	}
}

void DecompilerSettings::SetUseEnhancedUsing(bool value)
{
	if (useEnhancedUsing_ != value)
	{
		useEnhancedUsing_ = value;
	}
}

void DecompilerSettings::SetAlwaysUseBraces(bool value)
{
	if (alwaysUseBraces_ != value)
	{
		alwaysUseBraces_ = value;
	}
}

void DecompilerSettings::SetForEachStatement(bool value)
{
	if (forEachStatement_ != value)
	{
		forEachStatement_ = value;
	}
}

void DecompilerSettings::SetForEachWithGetEnumeratorExtension(bool value)
{
	if (forEachWithGetEnumeratorExtension_ != value)
	{
		forEachWithGetEnumeratorExtension_ = value;
	}
}

void DecompilerSettings::SetParamsCollections(bool value)
{
	if (paramsCollections_ != value)
	{
		paramsCollections_ = value;
	}
}

void DecompilerSettings::SetLockStatement(bool value)
{
	if (lockStatement_ != value)
	{
		lockStatement_ = value;
	}
}

void DecompilerSettings::SetSwitchStatementOnString(bool value)
{
	if (switchStatementOnString_ != value)
	{
		switchStatementOnString_ = value;
	}
}

void DecompilerSettings::SetSparseIntegerSwitch(bool value)
{
	if (sparseIntegerSwitch_ != value)
	{
		sparseIntegerSwitch_ = value;
	}
}

void DecompilerSettings::SetUsingDeclarations(bool value)
{
	if (usingDeclarations_ != value)
	{
		usingDeclarations_ = value;
	}
}

void DecompilerSettings::SetExtensionMethods(bool value)
{
	if (extensionMethods_ != value)
	{
		extensionMethods_ = value;
	}
}

void DecompilerSettings::SetQueryExpressions(bool value)
{
	if (queryExpressions_ != value)
	{
		queryExpressions_ = value;
	}
}

void DecompilerSettings::SetUseImplicitMethodGroupConversion(bool value)
{
	if (useImplicitMethodGroupConversion_ != value)
	{
		useImplicitMethodGroupConversion_ = value;
	}
}

void DecompilerSettings::SetUseObjectCreationOfGenericTypeParameter(bool value)
{
	if (useObjectCreationOfGenericTypeParameter_ != value)
	{
		useObjectCreationOfGenericTypeParameter_ = value;
	}
}

void DecompilerSettings::SetAlwaysCastTargetsOfExplicitInterfaceImplementationCalls(bool value)
{
	if (alwaysCastTargetsOfExplicitInterfaceImplementationCalls_ != value)
	{
		alwaysCastTargetsOfExplicitInterfaceImplementationCalls_ = value;
	}
}

void DecompilerSettings::SetAlwaysQualifyMemberReferences(bool value)
{
	if (alwaysQualifyMemberReferences_ != value)
	{
		alwaysQualifyMemberReferences_ = value;
	}
}

void DecompilerSettings::SetAlwaysShowEnumMemberValues(bool value)
{
	if (alwaysShowEnumMemberValues_ != value)
	{
		alwaysShowEnumMemberValues_ = value;
	}
}

void DecompilerSettings::SetUseDebugSymbols(bool value)
{
	if (useDebugSymbols_ != value)
	{
		useDebugSymbols_ = value;
	}
}

void DecompilerSettings::SetArrayInitializers(bool value)
{
	if (arrayInitializers_ != value)
	{
		arrayInitializers_ = value;
	}
}

void DecompilerSettings::SetObjectOrCollectionInitializers(bool value)
{
	if (objectCollectionInitializers_ != value)
	{
		objectCollectionInitializers_ = value;
	}
}

void DecompilerSettings::SetDictionaryInitializers(bool value)
{
	if (dictionaryInitializers_ != value)
	{
		dictionaryInitializers_ = value;
	}
}

void DecompilerSettings::SetExtensionMethodsInCollectionInitializers(bool value)
{
	if (extensionMethodsInCollectionInitializers_ != value)
	{
		extensionMethodsInCollectionInitializers_ = value;
	}
}

void DecompilerSettings::SetUseRefLocalsForAccurateOrderOfEvaluation(bool value)
{
	if (useRefLocalsForAccurateOrderOfEvaluation_ != value)
	{
		useRefLocalsForAccurateOrderOfEvaluation_ = value;
	}
}

void DecompilerSettings::SetRefExtensionMethods(bool value)
{
	if (refExtensionMethods_ != value)
	{
		refExtensionMethods_ = value;
	}
}

void DecompilerSettings::SetStringInterpolation(bool value)
{
	if (stringInterpolation_ != value)
	{
		stringInterpolation_ = value;
	}
}

void DecompilerSettings::SetUtf8StringLiterals(bool value)
{
	if (utf8StringLiterals_ != value)
	{
		utf8StringLiterals_ = value;
	}
}

void DecompilerSettings::SetSwitchOnReadOnlySpanChar(bool value)
{
	if (switchOnReadOnlySpanChar_ != value)
	{
		switchOnReadOnlySpanChar_ = value;
	}
}

void DecompilerSettings::SetUnsignedRightShift(bool value)
{
	if (unsignedRightShift_ != value)
	{
		unsignedRightShift_ = value;
	}
}

void DecompilerSettings::SetCheckedOperators(bool value)
{
	if (checkedOperators_ != value)
	{
		checkedOperators_ = value;
	}
}

void DecompilerSettings::SetShowXmlDocumentation(bool value)
{
	if (showXmlDocumentation_ != value)
	{
		showXmlDocumentation_ = value;
	}
}

void DecompilerSettings::SetFoldBraces(bool value)
{
	if (foldBraces_ != value)
	{
		foldBraces_ = value;
	}
}

void DecompilerSettings::SetExpandXmlDocumentationComments(bool value)
{
	if (expandXmlDocumentationComments_ != value)
	{
		expandXmlDocumentationComments_ = value;
	}
}

void DecompilerSettings::SetExpandMemberDefinitions(bool value)
{
	if (expandMemberDefinitions_ != value)
	{
		expandMemberDefinitions_ = value;
	}
}

void DecompilerSettings::SetExpandUsingDeclarations(bool value)
{
	if (expandUsingDeclarations_ != value)
	{
		expandUsingDeclarations_ = value;
	}
}

void DecompilerSettings::SetDecompileMemberBodies(bool value)
{
	if (decompileMemberBodies_ != value)
	{
		decompileMemberBodies_ = value;
	}
}

void DecompilerSettings::SetUseExpressionBodyForCalculatedGetterOnlyProperties(bool value)
{
	if (useExpressionBodyForCalculatedGetterOnlyProperties_ != value)
	{
		useExpressionBodyForCalculatedGetterOnlyProperties_ = value;
	}
}

void DecompilerSettings::SetOutVariables(bool value)
{
	if (outVariables_ != value)
	{
		outVariables_ = value;
	}
}

void DecompilerSettings::SetDiscards(bool value)
{
	if (discards_ != value)
	{
		discards_ = value;
	}
}

void DecompilerSettings::SetIntroduceRefModifiersOnStructs(bool value)
{
	if (introduceRefModifiersOnStructs_ != value)
	{
		introduceRefModifiersOnStructs_ = value;
	}
}

void DecompilerSettings::SetIntroduceReadonlyAndInModifiers(bool value)
{
	if (introduceReadonlyAndInModifiers_ != value)
	{
		introduceReadonlyAndInModifiers_ = value;
	}
}

void DecompilerSettings::SetIntroducePrivateProtectedAccessibility(bool value)
{
	if (introducePrivateProtectedAccessibilty_ != value)
	{
		introducePrivateProtectedAccessibilty_ = value;
	}
}

void DecompilerSettings::SetReadOnlyMethods(bool value)
{
	if (readOnlyMethods_ != value)
	{
		readOnlyMethods_ = value;
	}
}

void DecompilerSettings::SetAsyncUsingAndForEachStatement(bool value)
{
	if (asyncUsingAndForEachStatement_ != value)
	{
		asyncUsingAndForEachStatement_ = value;
	}
}

void DecompilerSettings::SetIntroduceUnmanagedConstraint(bool value)
{
	if (introduceUnmanagedConstraint_ != value)
	{
		introduceUnmanagedConstraint_ = value;
	}
}

void DecompilerSettings::SetStackAllocInitializers(bool value)
{
	if (stackAllocInitializers_ != value)
	{
		stackAllocInitializers_ = value;
	}
}

void DecompilerSettings::SetPatternBasedFixedStatement(bool value)
{
	if (patternBasedFixedStatement_ != value)
	{
		patternBasedFixedStatement_ = value;
	}
}

void DecompilerSettings::SetTupleTypes(bool value)
{
	if (tupleTypes_ != value)
	{
		tupleTypes_ = value;
	}
}

void DecompilerSettings::SetThrowExpressions(bool value)
{
	if (throwExpressions_ != value)
	{
		throwExpressions_ = value;
	}
}

void DecompilerSettings::SetTupleConversions(bool value)
{
	if (tupleConversions_ != value)
	{
		tupleConversions_ = value;
	}
}

void DecompilerSettings::SetTupleComparisons(bool value)
{
	if (tupleComparisons_ != value)
	{
		tupleComparisons_ = value;
	}
}

void DecompilerSettings::SetNamedArguments(bool value)
{
	if (namedArguments_ != value)
	{
		namedArguments_ = value;
	}
}

void DecompilerSettings::SetNonTrailingNamedArguments(bool value)
{
	if (nonTrailingNamedArguments_ != value)
	{
		nonTrailingNamedArguments_ = value;
	}
}

void DecompilerSettings::SetOptionalArguments(bool value)
{
	if (optionalArguments_ != value)
	{
		optionalArguments_ = value;
	}
}

void DecompilerSettings::SetExpandParamsArguments(bool value)
{
	if (expandParamsArguments_ != value)
	{
		expandParamsArguments_ = value;
	}
}

void DecompilerSettings::SetLocalFunctions(bool value)
{
	if (localFunctions_ != value)
	{
		localFunctions_ = value;
	}
}

void DecompilerSettings::SetDeconstruction(bool value)
{
	if (deconstruction_ != value)
	{
		deconstruction_ = value;
	}
}

void DecompilerSettings::SetPatternMatching(bool value)
{
	if (patternMatching_ != value)
	{
		patternMatching_ = value;
	}
}

void DecompilerSettings::SetRecursivePatternMatching(bool value)
{
	if (recursivePatternMatching_ != value)
	{
		recursivePatternMatching_ = value;
	}
}

void DecompilerSettings::SetPatternCombinators(bool value)
{
	if (patternCombinators_ != value)
	{
		patternCombinators_ = value;
	}
}

void DecompilerSettings::SetRelationalPatterns(bool value)
{
	if (relationalPatterns_ != value)
	{
		relationalPatterns_ = value;
	}
}

void DecompilerSettings::SetStaticLocalFunctions(bool value)
{
	if (staticLocalFunctions_ != value)
	{
		staticLocalFunctions_ = value;
	}
}

void DecompilerSettings::SetRanges(bool value)
{
	if (ranges_ != value)
	{
		ranges_ = value;
	}
}

void DecompilerSettings::SetNullableReferenceTypes(bool value)
{
	if (nullableReferenceTypes_ != value)
	{
		nullableReferenceTypes_ = value;
	}
}

void DecompilerSettings::SetShowDebugInfo(bool value)
{
	if (showDebugInfo_ != value)
	{
		showDebugInfo_ = value;
	}
}

void DecompilerSettings::SetAssumeArrayLengthFitsIntoInt32(bool value)
{
	if (assumeArrayLengthFitsIntoInt32_ != value)
	{
		assumeArrayLengthFitsIntoInt32_ = value;
	}
}

void DecompilerSettings::SetIntroduceIncrementAndDecrement(bool value)
{
	if (introduceIncrementAndDecrement_ != value)
	{
		introduceIncrementAndDecrement_ = value;
	}
}

void DecompilerSettings::SetMakeAssignmentExpressions(bool value)
{
	if (makeAssignmentExpressions_ != value)
	{
		makeAssignmentExpressions_ = value;
	}
}

void DecompilerSettings::SetRemoveDeadCode(bool value)
{
	if (removeDeadCode_ != value)
	{
		removeDeadCode_ = value;
	}
}

void DecompilerSettings::SetRemoveDeadStores(bool value)
{
	if (removeDeadStores_ != value)
	{
		removeDeadStores_ = value;
	}
}

void DecompilerSettings::SetLoadInMemory(bool value)
{
	if (loadInMemory_ != value)
	{
		loadInMemory_ = value;
	}
}

void DecompilerSettings::SetThrowOnAssemblyResolveErrors(bool value)
{
	if (throwOnAssemblyResolveErrors_ != value)
	{
		throwOnAssemblyResolveErrors_ = value;
	}
}

void DecompilerSettings::SetApplyWindowsRuntimeProjections(bool value)
{
	if (applyWindowsRuntimeProjections_ != value)
	{
		applyWindowsRuntimeProjections_ = value;
	}
}

void DecompilerSettings::SetAutoLoadAssemblyReferences(bool value)
{
	if (autoLoadAssemblyReferences_ != value)
	{
		autoLoadAssemblyReferences_ = value;
	}
}

void DecompilerSettings::SetForStatement(bool value)
{
	if (forStatement_ != value)
	{
		forStatement_ = value;
	}
}

void DecompilerSettings::SetDoWhileStatement(bool value)
{
	if (doWhileStatement_ != value)
	{
		doWhileStatement_ = value;
	}
}

void DecompilerSettings::SetRefReadOnlyParameters(bool value)
{
	if (refReadOnlyParameters_ != value)
	{
		refReadOnlyParameters_ = value;
	}
}

void DecompilerSettings::SetUsePrimaryConstructorSyntaxForNonRecordTypes(bool value)
{
	if (usePrimaryConstructorSyntaxForNonRecordTypes_ != value)
	{
		usePrimaryConstructorSyntaxForNonRecordTypes_ = value;
	}
}

void DecompilerSettings::SetInlineArrays(bool value)
{
	if (inlineArrays_ != value)
	{
		inlineArrays_ = value;
	}
}

void DecompilerSettings::SetExtensionMembers(bool value)
{
	if (extensionMembers_ != value)
	{
		extensionMembers_ = value;
	}
}

void DecompilerSettings::SetFirstClassSpanTypes(bool value)
{
	if (firstClassSpanTypes_ != value)
	{
		firstClassSpanTypes_ = value;
	}
}

void DecompilerSettings::SetSeparateLocalVariableDeclarations(bool value)
{
	if (separateLocalVariableDeclarations_ != value)
	{
		separateLocalVariableDeclarations_ = value;
	}
}

void DecompilerSettings::SetUseSdkStyleProjectFormat(bool value)
{
	if (useSdkStyleProjectFormat_ != value)
	{
		useSdkStyleProjectFormat_ = value;
	}
}

void DecompilerSettings::SetUseNestedDirectoriesForNamespaces(bool value)
{
	if (useNestedDirectoriesForNamespaces_ != value)
	{
		useNestedDirectoriesForNamespaces_ = value;
	}
}

void DecompilerSettings::SetAggressiveScalarReplacementOfAggregates(bool value)
{
	if (aggressiveScalarReplacementOfAggregates_ != value)
	{
		aggressiveScalarReplacementOfAggregates_ = value;
	}
}

void DecompilerSettings::SetAggressiveInlining(bool value)
{
	if (aggressiveInlining_ != value)
	{
		aggressiveInlining_ = value;
	}
}

void DecompilerSettings::SetAlwaysUseGlobal(bool value)
{
	if (alwaysUseGlobal_ != value)
	{
		alwaysUseGlobal_ = value;
	}
}

void DecompilerSettings::SetAlwaysMoveInitializer(bool value)
{
	if (alwaysMoveInitializer_ != value)
	{
		alwaysMoveInitializer_ = value;
	}
}

void DecompilerSettings::SetSortCustomAttributes(bool value)
{
	if (sortCustomAttributes_ != value)
	{
		sortCustomAttributes_ = value;
	}
}

void DecompilerSettings::SetSortSwitchSections(bool value)
{
	if (sortSwitchSections_ != value)
	{
		sortSwitchSections_ = value;
	}
}

void DecompilerSettings::SetCheckForOverflowUnderflow(bool value)
{
	if (checkForOverflowUnderflow_ != value)
	{
		checkForOverflowUnderflow_ = value;
	}
}

} // namespace ILSpy::Decompiler
