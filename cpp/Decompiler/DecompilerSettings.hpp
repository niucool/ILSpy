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
// Port of ICSharpCode.Decompiler/DecompilerSettings.cs: the settings bag the decompiler
// back end and the CLI consult. Every C# auto-property with the
// `if (field != value) { field = value; OnPropertyChanged(); }` setter body ports as a
// getter + `Set<Name>` pair carrying the same compare-then-write shape; the two C#
// irregular field spellings (`objectCollectionInitializers`,
// `introducePrivateProtectedAccessibilty` -- the source's own typo) are preserved.
// `LifetimeAnnotations` is the C# `[Obsolete]` forwarding alias of `ScopedRef`.
//
// C#-to-C++ porting decisions:
//  * The `INotifyPropertyChanged` surface (the `PropertyChanged` event and the
//    `OnPropertyChanged` raiser every setter calls) is a data-binding hook -- no ported
//    consumer subscribes (the BamlDecompilerSettings precedent), so the event stays a
//    documented deferral and the setters keep the compare-then-write shape the C# fire
//    arm sits inside.
//  * The `CSharpFormattingOptions` field is a C# REFERENCE the property lazily fills
//    (FormattingOptionsFactory.CreateAllman() + three mutations on first access); the
//    port holds an `std::optional` (the value-type stand-in for the C# reference) and
//    the getter materializes it, so the "same options object on every access" contract
//    is the same value. The setter's `ArgumentNullException()` null check ports to the
//    nullopt arm (the C# parameterless message "Value cannot be null.").
//  * `Clone()` is the C# `MemberwiseClone` + the formatting-options deep copy + the
//    event reset; with the event deferred the reset is a no-op, so the port is the
//    member-wise copy with the cloned formatting options.
//  * `[Category]` / `[Description]` / `[Browsable]` / `[Obsolete]` are designer
//    attributes with no runtime behavior.

#pragma once

#include <optional>

#include "Decompiler/CSharp/LanguageVersion.hpp"
#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"

namespace ILSpy::Decompiler {

// The C# `public class DecompilerSettings : INotifyPropertyChanged`.
class DecompilerSettings {
public:
	DecompilerSettings() = default;

	// The C# `public DecompilerSettings(CSharp.LanguageVersion languageVersion)` -- chains
	// through SetLanguageVersion on a fresh instance.
	explicit DecompilerSettings(CSharp::LanguageVersion languageVersion);

	// Deactivates all language features newer than the given version (the C# if-ladder
	// fires for every block the version falls below; nothing is re-enabled, so a second
	// call only ever narrows further).
	void SetLanguageVersion(CSharp::LanguageVersion languageVersion);

	// The highest C# version any enabled setting requires (the C# if-ladder; three gated
	// fields -- extensionMethods, useLambdaSyntax, useEnhancedUsing -- are absent from
	// every statement, so enabling them alone still answers CSharp1).
	CSharp::LanguageVersion GetMinimumRequiredVersion() const;

	// The C# `public CSharpFormattingOptions CSharpFormattingOptions` property -- the lazy
	// CreateAllman default the C# fills on first access. The self-named accessor hides the
	// type for the rest of this class, so every type position below spells it qualified.
	CSharp::OutputVisitor::CSharpFormattingOptions& CSharpFormattingOptions();
	void SetCSharpFormattingOptions(std::optional<CSharp::OutputVisitor::CSharpFormattingOptions> value);

	// The C# `public virtual DecompilerSettings Clone()` -- the member-wise copy with the
	// formatting options deep-copied.
	DecompilerSettings Clone() const;

	// The C# `public bool NativeIntegers`.
	bool NativeIntegers() const { return nativeIntegers_; }
	void SetNativeIntegers(bool value);
	// The C# `public bool NumericIntPtr`.
	bool NumericIntPtr() const { return numericIntPtr_; }
	void SetNumericIntPtr(bool value);
	// The C# `public bool CovariantReturns`.
	bool CovariantReturns() const { return covariantReturns_; }
	void SetCovariantReturns(bool value);
	// The C# `public bool InitAccessors`.
	bool InitAccessors() const { return initAccessors_; }
	void SetInitAccessors(bool value);
	// The C# `public bool RecordClasses`.
	bool RecordClasses() const { return recordClasses_; }
	void SetRecordClasses(bool value);
	// The C# `public bool RecordStructs`.
	bool RecordStructs() const { return recordStructs_; }
	void SetRecordStructs(bool value);
	// The C# `public bool StructDefaultConstructorsAndFieldInitializers`.
	bool StructDefaultConstructorsAndFieldInitializers() const { return structDefaultConstructorsAndFieldInitializers_; }
	void SetStructDefaultConstructorsAndFieldInitializers(bool value);
	// The C# `public bool WithExpressions`.
	bool WithExpressions() const { return withExpressions_; }
	void SetWithExpressions(bool value);
	// The C# `public bool UsePrimaryConstructorSyntax`.
	bool UsePrimaryConstructorSyntax() const { return usePrimaryConstructorSyntax_; }
	void SetUsePrimaryConstructorSyntax(bool value);
	// The C# `public bool FunctionPointers`.
	bool FunctionPointers() const { return functionPointers_; }
	void SetFunctionPointers(bool value);
	// The C# `public bool ScopedRef`.
	bool ScopedRef() const { return scopedRef_; }
	void SetScopedRef(bool value);
	// The C# `public bool RequiredMembers`.
	bool RequiredMembers() const { return requiredMembers_; }
	void SetRequiredMembers(bool value);
	// The C# `public bool SwitchExpressions`.
	bool SwitchExpressions() const { return switchExpressions_; }
	void SetSwitchExpressions(bool value);
	// The C# `public bool FileScopedNamespaces`.
	bool FileScopedNamespaces() const { return fileScopedNamespaces_; }
	void SetFileScopedNamespaces(bool value);
	// The C# `public bool AnonymousMethods`.
	bool AnonymousMethods() const { return anonymousMethods_; }
	void SetAnonymousMethods(bool value);
	// The C# `public bool AnonymousTypes`.
	bool AnonymousTypes() const { return anonymousTypes_; }
	void SetAnonymousTypes(bool value);
	// The C# `public bool UseLambdaSyntax`.
	bool UseLambdaSyntax() const { return useLambdaSyntax_; }
	void SetUseLambdaSyntax(bool value);
	// The C# `public bool ExpressionTrees`.
	bool ExpressionTrees() const { return expressionTrees_; }
	void SetExpressionTrees(bool value);
	// The C# `public bool YieldReturn`.
	bool YieldReturn() const { return yieldReturn_; }
	void SetYieldReturn(bool value);
	// The C# `public bool Dynamic`.
	bool Dynamic() const { return dynamic_; }
	void SetDynamic(bool value);
	// The C# `public bool AsyncAwait`.
	bool AsyncAwait() const { return asyncAwait_; }
	void SetAsyncAwait(bool value);
	// The C# `public bool AwaitInCatchFinally`.
	bool AwaitInCatchFinally() const { return awaitInCatchFinally_; }
	void SetAwaitInCatchFinally(bool value);
	// The C# `public bool AsyncEnumerator`.
	bool AsyncEnumerator() const { return asyncEnumerator_; }
	void SetAsyncEnumerator(bool value);
	// The C# `public bool DecimalConstants`.
	bool DecimalConstants() const { return decimalConstants_; }
	void SetDecimalConstants(bool value);
	// The C# `public bool FixedBuffers`.
	bool FixedBuffers() const { return fixedBuffers_; }
	void SetFixedBuffers(bool value);
	// The C# `public bool StringConcat`.
	bool StringConcat() const { return stringConcat_; }
	void SetStringConcat(bool value);
	// The C# `public bool LiftNullables`.
	bool LiftNullables() const { return liftNullables_; }
	void SetLiftNullables(bool value);
	// The C# `public bool NullPropagation`.
	bool NullPropagation() const { return nullPropagation_; }
	void SetNullPropagation(bool value);
	// The C# `public bool AutomaticProperties`.
	bool AutomaticProperties() const { return automaticProperties_; }
	void SetAutomaticProperties(bool value);
	// The C# `public bool GetterOnlyAutomaticProperties`.
	bool GetterOnlyAutomaticProperties() const { return getterOnlyAutomaticProperties_; }
	void SetGetterOnlyAutomaticProperties(bool value);
	// The C# `public bool AutomaticEvents`.
	bool AutomaticEvents() const { return automaticEvents_; }
	void SetAutomaticEvents(bool value);
	// The C# `public bool UsingStatement`.
	bool UsingStatement() const { return usingStatement_; }
	void SetUsingStatement(bool value);
	// The C# `public bool UseEnhancedUsing`.
	bool UseEnhancedUsing() const { return useEnhancedUsing_; }
	void SetUseEnhancedUsing(bool value);
	// The C# `public bool AlwaysUseBraces`.
	bool AlwaysUseBraces() const { return alwaysUseBraces_; }
	void SetAlwaysUseBraces(bool value);
	// The C# `public bool ForEachStatement`.
	bool ForEachStatement() const { return forEachStatement_; }
	void SetForEachStatement(bool value);
	// The C# `public bool ForEachWithGetEnumeratorExtension`.
	bool ForEachWithGetEnumeratorExtension() const { return forEachWithGetEnumeratorExtension_; }
	void SetForEachWithGetEnumeratorExtension(bool value);
	// The C# `public bool ParamsCollections`.
	bool ParamsCollections() const { return paramsCollections_; }
	void SetParamsCollections(bool value);
	// The C# `public bool LockStatement`.
	bool LockStatement() const { return lockStatement_; }
	void SetLockStatement(bool value);
	// The C# `public bool SwitchStatementOnString`.
	bool SwitchStatementOnString() const { return switchStatementOnString_; }
	void SetSwitchStatementOnString(bool value);
	// The C# `public bool SparseIntegerSwitch`.
	bool SparseIntegerSwitch() const { return sparseIntegerSwitch_; }
	void SetSparseIntegerSwitch(bool value);
	// The C# `public bool UsingDeclarations`.
	bool UsingDeclarations() const { return usingDeclarations_; }
	void SetUsingDeclarations(bool value);
	// The C# `public bool ExtensionMethods`.
	bool ExtensionMethods() const { return extensionMethods_; }
	void SetExtensionMethods(bool value);
	// The C# `public bool QueryExpressions`.
	bool QueryExpressions() const { return queryExpressions_; }
	void SetQueryExpressions(bool value);
	// The C# `public bool UseImplicitMethodGroupConversion`.
	bool UseImplicitMethodGroupConversion() const { return useImplicitMethodGroupConversion_; }
	void SetUseImplicitMethodGroupConversion(bool value);
	// The C# `public bool UseObjectCreationOfGenericTypeParameter`.
	bool UseObjectCreationOfGenericTypeParameter() const { return useObjectCreationOfGenericTypeParameter_; }
	void SetUseObjectCreationOfGenericTypeParameter(bool value);
	// The C# `public bool AlwaysCastTargetsOfExplicitInterfaceImplementationCalls`.
	bool AlwaysCastTargetsOfExplicitInterfaceImplementationCalls() const { return alwaysCastTargetsOfExplicitInterfaceImplementationCalls_; }
	void SetAlwaysCastTargetsOfExplicitInterfaceImplementationCalls(bool value);
	// The C# `public bool AlwaysQualifyMemberReferences`.
	bool AlwaysQualifyMemberReferences() const { return alwaysQualifyMemberReferences_; }
	void SetAlwaysQualifyMemberReferences(bool value);
	// The C# `public bool AlwaysShowEnumMemberValues`.
	bool AlwaysShowEnumMemberValues() const { return alwaysShowEnumMemberValues_; }
	void SetAlwaysShowEnumMemberValues(bool value);
	// The C# `public bool UseDebugSymbols`.
	bool UseDebugSymbols() const { return useDebugSymbols_; }
	void SetUseDebugSymbols(bool value);
	// The C# `public bool ArrayInitializers`.
	bool ArrayInitializers() const { return arrayInitializers_; }
	void SetArrayInitializers(bool value);
	// The C# `public bool ObjectOrCollectionInitializers`.
	bool ObjectOrCollectionInitializers() const { return objectCollectionInitializers_; }
	void SetObjectOrCollectionInitializers(bool value);
	// The C# `public bool DictionaryInitializers`.
	bool DictionaryInitializers() const { return dictionaryInitializers_; }
	void SetDictionaryInitializers(bool value);
	// The C# `public bool ExtensionMethodsInCollectionInitializers`.
	bool ExtensionMethodsInCollectionInitializers() const { return extensionMethodsInCollectionInitializers_; }
	void SetExtensionMethodsInCollectionInitializers(bool value);
	// The C# `public bool UseRefLocalsForAccurateOrderOfEvaluation`.
	bool UseRefLocalsForAccurateOrderOfEvaluation() const { return useRefLocalsForAccurateOrderOfEvaluation_; }
	void SetUseRefLocalsForAccurateOrderOfEvaluation(bool value);
	// The C# `public bool RefExtensionMethods`.
	bool RefExtensionMethods() const { return refExtensionMethods_; }
	void SetRefExtensionMethods(bool value);
	// The C# `public bool StringInterpolation`.
	bool StringInterpolation() const { return stringInterpolation_; }
	void SetStringInterpolation(bool value);
	// The C# `public bool Utf8StringLiterals`.
	bool Utf8StringLiterals() const { return utf8StringLiterals_; }
	void SetUtf8StringLiterals(bool value);
	// The C# `public bool SwitchOnReadOnlySpanChar`.
	bool SwitchOnReadOnlySpanChar() const { return switchOnReadOnlySpanChar_; }
	void SetSwitchOnReadOnlySpanChar(bool value);
	// The C# `public bool UnsignedRightShift`.
	bool UnsignedRightShift() const { return unsignedRightShift_; }
	void SetUnsignedRightShift(bool value);
	// The C# `public bool CheckedOperators`.
	bool CheckedOperators() const { return checkedOperators_; }
	void SetCheckedOperators(bool value);
	// The C# `public bool ShowXmlDocumentation`.
	bool ShowXmlDocumentation() const { return showXmlDocumentation_; }
	void SetShowXmlDocumentation(bool value);
	// The C# `public bool FoldBraces`.
	bool FoldBraces() const { return foldBraces_; }
	void SetFoldBraces(bool value);
	// The C# `public bool ExpandXmlDocumentationComments`.
	bool ExpandXmlDocumentationComments() const { return expandXmlDocumentationComments_; }
	void SetExpandXmlDocumentationComments(bool value);
	// The C# `public bool ExpandMemberDefinitions`.
	bool ExpandMemberDefinitions() const { return expandMemberDefinitions_; }
	void SetExpandMemberDefinitions(bool value);
	// The C# `public bool ExpandUsingDeclarations`.
	bool ExpandUsingDeclarations() const { return expandUsingDeclarations_; }
	void SetExpandUsingDeclarations(bool value);
	// The C# `public bool DecompileMemberBodies`.
	bool DecompileMemberBodies() const { return decompileMemberBodies_; }
	void SetDecompileMemberBodies(bool value);
	// The C# `public bool UseExpressionBodyForCalculatedGetterOnlyProperties`.
	bool UseExpressionBodyForCalculatedGetterOnlyProperties() const { return useExpressionBodyForCalculatedGetterOnlyProperties_; }
	void SetUseExpressionBodyForCalculatedGetterOnlyProperties(bool value);
	// The C# `public bool OutVariables`.
	bool OutVariables() const { return outVariables_; }
	void SetOutVariables(bool value);
	// The C# `public bool Discards`.
	bool Discards() const { return discards_; }
	void SetDiscards(bool value);
	// The C# `public bool IntroduceRefModifiersOnStructs`.
	bool IntroduceRefModifiersOnStructs() const { return introduceRefModifiersOnStructs_; }
	void SetIntroduceRefModifiersOnStructs(bool value);
	// The C# `public bool IntroduceReadonlyAndInModifiers`.
	bool IntroduceReadonlyAndInModifiers() const { return introduceReadonlyAndInModifiers_; }
	void SetIntroduceReadonlyAndInModifiers(bool value);
	// The C# `public bool IntroducePrivateProtectedAccessibility`.
	bool IntroducePrivateProtectedAccessibility() const { return introducePrivateProtectedAccessibilty_; }
	void SetIntroducePrivateProtectedAccessibility(bool value);
	// The C# `public bool ReadOnlyMethods`.
	bool ReadOnlyMethods() const { return readOnlyMethods_; }
	void SetReadOnlyMethods(bool value);
	// The C# `public bool AsyncUsingAndForEachStatement`.
	bool AsyncUsingAndForEachStatement() const { return asyncUsingAndForEachStatement_; }
	void SetAsyncUsingAndForEachStatement(bool value);
	// The C# `public bool IntroduceUnmanagedConstraint`.
	bool IntroduceUnmanagedConstraint() const { return introduceUnmanagedConstraint_; }
	void SetIntroduceUnmanagedConstraint(bool value);
	// The C# `public bool StackAllocInitializers`.
	bool StackAllocInitializers() const { return stackAllocInitializers_; }
	void SetStackAllocInitializers(bool value);
	// The C# `public bool PatternBasedFixedStatement`.
	bool PatternBasedFixedStatement() const { return patternBasedFixedStatement_; }
	void SetPatternBasedFixedStatement(bool value);
	// The C# `public bool TupleTypes`.
	bool TupleTypes() const { return tupleTypes_; }
	void SetTupleTypes(bool value);
	// The C# `public bool ThrowExpressions`.
	bool ThrowExpressions() const { return throwExpressions_; }
	void SetThrowExpressions(bool value);
	// The C# `public bool TupleConversions`.
	bool TupleConversions() const { return tupleConversions_; }
	void SetTupleConversions(bool value);
	// The C# `public bool TupleComparisons`.
	bool TupleComparisons() const { return tupleComparisons_; }
	void SetTupleComparisons(bool value);
	// The C# `public bool NamedArguments`.
	bool NamedArguments() const { return namedArguments_; }
	void SetNamedArguments(bool value);
	// The C# `public bool NonTrailingNamedArguments`.
	bool NonTrailingNamedArguments() const { return nonTrailingNamedArguments_; }
	void SetNonTrailingNamedArguments(bool value);
	// The C# `public bool OptionalArguments`.
	bool OptionalArguments() const { return optionalArguments_; }
	void SetOptionalArguments(bool value);
	// The C# `public bool ExpandParamsArguments`.
	bool ExpandParamsArguments() const { return expandParamsArguments_; }
	void SetExpandParamsArguments(bool value);
	// The C# `public bool LocalFunctions`.
	bool LocalFunctions() const { return localFunctions_; }
	void SetLocalFunctions(bool value);
	// The C# `public bool Deconstruction`.
	bool Deconstruction() const { return deconstruction_; }
	void SetDeconstruction(bool value);
	// The C# `public bool PatternMatching`.
	bool PatternMatching() const { return patternMatching_; }
	void SetPatternMatching(bool value);
	// The C# `public bool RecursivePatternMatching`.
	bool RecursivePatternMatching() const { return recursivePatternMatching_; }
	void SetRecursivePatternMatching(bool value);
	// The C# `public bool PatternCombinators`.
	bool PatternCombinators() const { return patternCombinators_; }
	void SetPatternCombinators(bool value);
	// The C# `public bool RelationalPatterns`.
	bool RelationalPatterns() const { return relationalPatterns_; }
	void SetRelationalPatterns(bool value);
	// The C# `public bool StaticLocalFunctions`.
	bool StaticLocalFunctions() const { return staticLocalFunctions_; }
	void SetStaticLocalFunctions(bool value);
	// The C# `public bool Ranges`.
	bool Ranges() const { return ranges_; }
	void SetRanges(bool value);
	// The C# `public bool NullableReferenceTypes`.
	bool NullableReferenceTypes() const { return nullableReferenceTypes_; }
	void SetNullableReferenceTypes(bool value);
	// The C# `public bool ShowDebugInfo`.
	bool ShowDebugInfo() const { return showDebugInfo_; }
	void SetShowDebugInfo(bool value);
	// The C# `public bool AssumeArrayLengthFitsIntoInt32`.
	bool AssumeArrayLengthFitsIntoInt32() const { return assumeArrayLengthFitsIntoInt32_; }
	void SetAssumeArrayLengthFitsIntoInt32(bool value);
	// The C# `public bool IntroduceIncrementAndDecrement`.
	bool IntroduceIncrementAndDecrement() const { return introduceIncrementAndDecrement_; }
	void SetIntroduceIncrementAndDecrement(bool value);
	// The C# `public bool MakeAssignmentExpressions`.
	bool MakeAssignmentExpressions() const { return makeAssignmentExpressions_; }
	void SetMakeAssignmentExpressions(bool value);
	// The C# `public bool RemoveDeadCode`.
	bool RemoveDeadCode() const { return removeDeadCode_; }
	void SetRemoveDeadCode(bool value);
	// The C# `public bool RemoveDeadStores`.
	bool RemoveDeadStores() const { return removeDeadStores_; }
	void SetRemoveDeadStores(bool value);
	// The C# `public bool LoadInMemory`.
	bool LoadInMemory() const { return loadInMemory_; }
	void SetLoadInMemory(bool value);
	// The C# `public bool ThrowOnAssemblyResolveErrors`.
	bool ThrowOnAssemblyResolveErrors() const { return throwOnAssemblyResolveErrors_; }
	void SetThrowOnAssemblyResolveErrors(bool value);
	// The C# `public bool ApplyWindowsRuntimeProjections`.
	bool ApplyWindowsRuntimeProjections() const { return applyWindowsRuntimeProjections_; }
	void SetApplyWindowsRuntimeProjections(bool value);
	// The C# `public bool AutoLoadAssemblyReferences`.
	bool AutoLoadAssemblyReferences() const { return autoLoadAssemblyReferences_; }
	void SetAutoLoadAssemblyReferences(bool value);
	// The C# `public bool ForStatement`.
	bool ForStatement() const { return forStatement_; }
	void SetForStatement(bool value);
	// The C# `public bool DoWhileStatement`.
	bool DoWhileStatement() const { return doWhileStatement_; }
	void SetDoWhileStatement(bool value);
	// The C# `public bool RefReadOnlyParameters`.
	bool RefReadOnlyParameters() const { return refReadOnlyParameters_; }
	void SetRefReadOnlyParameters(bool value);
	// The C# `public bool UsePrimaryConstructorSyntaxForNonRecordTypes`.
	bool UsePrimaryConstructorSyntaxForNonRecordTypes() const { return usePrimaryConstructorSyntaxForNonRecordTypes_; }
	void SetUsePrimaryConstructorSyntaxForNonRecordTypes(bool value);
	// The C# `public bool InlineArrays`.
	bool InlineArrays() const { return inlineArrays_; }
	void SetInlineArrays(bool value);
	// The C# `public bool ExtensionMembers`.
	bool ExtensionMembers() const { return extensionMembers_; }
	void SetExtensionMembers(bool value);
	// The C# `public bool FirstClassSpanTypes`.
	bool FirstClassSpanTypes() const { return firstClassSpanTypes_; }
	void SetFirstClassSpanTypes(bool value);
	// The C# `public bool SeparateLocalVariableDeclarations`.
	bool SeparateLocalVariableDeclarations() const { return separateLocalVariableDeclarations_; }
	void SetSeparateLocalVariableDeclarations(bool value);
	// The C# `public bool UseSdkStyleProjectFormat`.
	bool UseSdkStyleProjectFormat() const { return useSdkStyleProjectFormat_; }
	void SetUseSdkStyleProjectFormat(bool value);
	// The C# `public bool UseNestedDirectoriesForNamespaces`.
	bool UseNestedDirectoriesForNamespaces() const { return useNestedDirectoriesForNamespaces_; }
	void SetUseNestedDirectoriesForNamespaces(bool value);
	// The C# `public bool AggressiveScalarReplacementOfAggregates`.
	bool AggressiveScalarReplacementOfAggregates() const { return aggressiveScalarReplacementOfAggregates_; }
	void SetAggressiveScalarReplacementOfAggregates(bool value);
	// The C# `public bool AggressiveInlining`.
	bool AggressiveInlining() const { return aggressiveInlining_; }
	void SetAggressiveInlining(bool value);
	// The C# `public bool AlwaysUseGlobal`.
	bool AlwaysUseGlobal() const { return alwaysUseGlobal_; }
	void SetAlwaysUseGlobal(bool value);
	// The C# `public bool AlwaysMoveInitializer`.
	bool AlwaysMoveInitializer() const { return alwaysMoveInitializer_; }
	void SetAlwaysMoveInitializer(bool value);
	// The C# `public bool SortCustomAttributes`.
	bool SortCustomAttributes() const { return sortCustomAttributes_; }
	void SetSortCustomAttributes(bool value);
	// The C# `public bool SortSwitchSections`.
	bool SortSwitchSections() const { return sortSwitchSections_; }
	void SetSortSwitchSections(bool value);
	// The C# `public bool CheckForOverflowUnderflow`.
	bool CheckForOverflowUnderflow() const { return checkForOverflowUnderflow_; }
	void SetCheckForOverflowUnderflow(bool value);

	// The C# `[Obsolete] public bool LifetimeAnnotations` -- the forwarding alias of ScopedRef.
	bool LifetimeAnnotations() const { return ScopedRef(); }
	void SetLifetimeAnnotations(bool value) { SetScopedRef(value); }

private:
	bool nativeIntegers_ = true;
	bool numericIntPtr_ = true;
	bool covariantReturns_ = true;
	bool initAccessors_ = true;
	bool recordClasses_ = true;
	bool recordStructs_ = true;
	bool structDefaultConstructorsAndFieldInitializers_ = true;
	bool withExpressions_ = true;
	bool usePrimaryConstructorSyntax_ = true;
	bool functionPointers_ = true;
	bool scopedRef_ = true;
	bool requiredMembers_ = true;
	bool switchExpressions_ = true;
	bool fileScopedNamespaces_ = true;
	bool anonymousMethods_ = true;
	bool anonymousTypes_ = true;
	bool useLambdaSyntax_ = true;
	bool expressionTrees_ = true;
	bool yieldReturn_ = true;
	bool dynamic_ = true;
	bool asyncAwait_ = true;
	bool awaitInCatchFinally_ = true;
	bool asyncEnumerator_ = true;
	bool decimalConstants_ = true;
	bool fixedBuffers_ = true;
	bool stringConcat_ = true;
	bool liftNullables_ = true;
	bool nullPropagation_ = true;
	bool automaticProperties_ = true;
	bool getterOnlyAutomaticProperties_ = true;
	bool automaticEvents_ = true;
	bool usingStatement_ = true;
	bool useEnhancedUsing_ = true;
	bool alwaysUseBraces_ = true;
	bool forEachStatement_ = true;
	bool forEachWithGetEnumeratorExtension_ = true;
	bool paramsCollections_ = true;
	bool lockStatement_ = true;
	bool switchStatementOnString_ = true;
	bool sparseIntegerSwitch_ = true;
	bool usingDeclarations_ = true;
	bool extensionMethods_ = true;
	bool queryExpressions_ = true;
	bool useImplicitMethodGroupConversion_ = true;
	bool useObjectCreationOfGenericTypeParameter_ = true;
	bool alwaysCastTargetsOfExplicitInterfaceImplementationCalls_ = false;
	bool alwaysQualifyMemberReferences_ = false;
	bool alwaysShowEnumMemberValues_ = false;
	bool useDebugSymbols_ = true;
	bool arrayInitializers_ = true;
	bool objectCollectionInitializers_ = true;
	bool dictionaryInitializers_ = true;
	bool extensionMethodsInCollectionInitializers_ = true;
	bool useRefLocalsForAccurateOrderOfEvaluation_ = true;
	bool refExtensionMethods_ = true;
	bool stringInterpolation_ = true;
	bool utf8StringLiterals_ = true;
	bool switchOnReadOnlySpanChar_ = true;
	bool unsignedRightShift_ = true;
	bool checkedOperators_ = true;
	bool showXmlDocumentation_ = true;
	bool foldBraces_ = false;
	bool expandXmlDocumentationComments_ = false;
	bool expandMemberDefinitions_ = false;
	bool expandUsingDeclarations_ = false;
	bool decompileMemberBodies_ = true;
	bool useExpressionBodyForCalculatedGetterOnlyProperties_ = true;
	bool outVariables_ = true;
	bool discards_ = true;
	bool introduceRefModifiersOnStructs_ = true;
	bool introduceReadonlyAndInModifiers_ = true;
	bool introducePrivateProtectedAccessibilty_ = true;
	bool readOnlyMethods_ = true;
	bool asyncUsingAndForEachStatement_ = true;
	bool introduceUnmanagedConstraint_ = true;
	bool stackAllocInitializers_ = true;
	bool patternBasedFixedStatement_ = true;
	bool tupleTypes_ = true;
	bool throwExpressions_ = true;
	bool tupleConversions_ = true;
	bool tupleComparisons_ = true;
	bool namedArguments_ = true;
	bool nonTrailingNamedArguments_ = true;
	bool optionalArguments_ = true;
	bool expandParamsArguments_ = true;
	bool localFunctions_ = true;
	bool deconstruction_ = true;
	bool patternMatching_ = true;
	bool recursivePatternMatching_ = true;
	bool patternCombinators_ = true;
	bool relationalPatterns_ = true;
	bool staticLocalFunctions_ = true;
	bool ranges_ = true;
	bool nullableReferenceTypes_ = true;
	bool showDebugInfo_ = false;
	bool assumeArrayLengthFitsIntoInt32_ = true;
	bool introduceIncrementAndDecrement_ = true;
	bool makeAssignmentExpressions_ = true;
	bool removeDeadCode_ = false;
	bool removeDeadStores_ = false;
	bool loadInMemory_ = false;
	bool throwOnAssemblyResolveErrors_ = true;
	bool applyWindowsRuntimeProjections_ = true;
	bool autoLoadAssemblyReferences_ = true;
	bool forStatement_ = true;
	bool doWhileStatement_ = true;
	bool refReadOnlyParameters_ = true;
	bool usePrimaryConstructorSyntaxForNonRecordTypes_ = true;
	bool inlineArrays_ = true;
	bool extensionMembers_ = true;
	bool firstClassSpanTypes_ = true;
	bool separateLocalVariableDeclarations_ = false;
	bool useSdkStyleProjectFormat_ = true;
	bool useNestedDirectoriesForNamespaces_ = false;
	bool aggressiveScalarReplacementOfAggregates_ = false;
	bool aggressiveInlining_ = false;
	bool alwaysUseGlobal_ = false;
	bool alwaysMoveInitializer_ = false;
	bool sortCustomAttributes_ = false;
	bool sortSwitchSections_ = false;
	bool checkForOverflowUnderflow_ = false;

	// The C# `CSharpFormattingOptions csharpFormattingOptions` reference -- the null state
	// is the disengaged optional; the getter materializes the CreateAllman default.
	std::optional<CSharp::OutputVisitor::CSharpFormattingOptions> csharpFormattingOptions_;
};

} // namespace ILSpy::Decompiler
