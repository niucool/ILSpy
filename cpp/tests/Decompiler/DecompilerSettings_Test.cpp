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
// Tests for the DecompilerSettings port (ICSharpCode.Decompiler/DecompilerSettings.cs).
// Every expectation comes from the generated TestFixtures/DecompilerSettingsGold.hpp
// tables extracted from the repo C# source, cross-checked against the shipped ilspycmd
// 11.0 engine where they agree (the fixture header records the divergences).

#include "Decompiler/DecompilerSettings.hpp"

#include <gtest/gtest.h>

#include "TestFixtures/DecompilerSettingsGold.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using ILSpy::Decompiler::CSharp::LanguageVersion;
using ILSpy::Decompiler::DecompilerSettings;

// The getter/setter member-pointer table the loop tests drive.
struct GetterRow {
	const char* Name;
	bool (DecompilerSettings::*Get)() const;
	void (DecompilerSettings::*Set)(bool);
};
constexpr GetterRow kGetters[] = {
	{ "NativeIntegers", &DecompilerSettings::NativeIntegers, &DecompilerSettings::SetNativeIntegers },
	{ "NumericIntPtr", &DecompilerSettings::NumericIntPtr, &DecompilerSettings::SetNumericIntPtr },
	{ "CovariantReturns", &DecompilerSettings::CovariantReturns, &DecompilerSettings::SetCovariantReturns },
	{ "InitAccessors", &DecompilerSettings::InitAccessors, &DecompilerSettings::SetInitAccessors },
	{ "RecordClasses", &DecompilerSettings::RecordClasses, &DecompilerSettings::SetRecordClasses },
	{ "RecordStructs", &DecompilerSettings::RecordStructs, &DecompilerSettings::SetRecordStructs },
	{ "StructDefaultConstructorsAndFieldInitializers", &DecompilerSettings::StructDefaultConstructorsAndFieldInitializers, &DecompilerSettings::SetStructDefaultConstructorsAndFieldInitializers },
	{ "WithExpressions", &DecompilerSettings::WithExpressions, &DecompilerSettings::SetWithExpressions },
	{ "UsePrimaryConstructorSyntax", &DecompilerSettings::UsePrimaryConstructorSyntax, &DecompilerSettings::SetUsePrimaryConstructorSyntax },
	{ "FunctionPointers", &DecompilerSettings::FunctionPointers, &DecompilerSettings::SetFunctionPointers },
	{ "ScopedRef", &DecompilerSettings::ScopedRef, &DecompilerSettings::SetScopedRef },
	{ "RequiredMembers", &DecompilerSettings::RequiredMembers, &DecompilerSettings::SetRequiredMembers },
	{ "SwitchExpressions", &DecompilerSettings::SwitchExpressions, &DecompilerSettings::SetSwitchExpressions },
	{ "FileScopedNamespaces", &DecompilerSettings::FileScopedNamespaces, &DecompilerSettings::SetFileScopedNamespaces },
	{ "AnonymousMethods", &DecompilerSettings::AnonymousMethods, &DecompilerSettings::SetAnonymousMethods },
	{ "AnonymousTypes", &DecompilerSettings::AnonymousTypes, &DecompilerSettings::SetAnonymousTypes },
	{ "UseLambdaSyntax", &DecompilerSettings::UseLambdaSyntax, &DecompilerSettings::SetUseLambdaSyntax },
	{ "ExpressionTrees", &DecompilerSettings::ExpressionTrees, &DecompilerSettings::SetExpressionTrees },
	{ "YieldReturn", &DecompilerSettings::YieldReturn, &DecompilerSettings::SetYieldReturn },
	{ "Dynamic", &DecompilerSettings::Dynamic, &DecompilerSettings::SetDynamic },
	{ "AsyncAwait", &DecompilerSettings::AsyncAwait, &DecompilerSettings::SetAsyncAwait },
	{ "AwaitInCatchFinally", &DecompilerSettings::AwaitInCatchFinally, &DecompilerSettings::SetAwaitInCatchFinally },
	{ "AsyncEnumerator", &DecompilerSettings::AsyncEnumerator, &DecompilerSettings::SetAsyncEnumerator },
	{ "DecimalConstants", &DecompilerSettings::DecimalConstants, &DecompilerSettings::SetDecimalConstants },
	{ "FixedBuffers", &DecompilerSettings::FixedBuffers, &DecompilerSettings::SetFixedBuffers },
	{ "StringConcat", &DecompilerSettings::StringConcat, &DecompilerSettings::SetStringConcat },
	{ "LiftNullables", &DecompilerSettings::LiftNullables, &DecompilerSettings::SetLiftNullables },
	{ "NullPropagation", &DecompilerSettings::NullPropagation, &DecompilerSettings::SetNullPropagation },
	{ "AutomaticProperties", &DecompilerSettings::AutomaticProperties, &DecompilerSettings::SetAutomaticProperties },
	{ "GetterOnlyAutomaticProperties", &DecompilerSettings::GetterOnlyAutomaticProperties, &DecompilerSettings::SetGetterOnlyAutomaticProperties },
	{ "AutomaticEvents", &DecompilerSettings::AutomaticEvents, &DecompilerSettings::SetAutomaticEvents },
	{ "UsingStatement", &DecompilerSettings::UsingStatement, &DecompilerSettings::SetUsingStatement },
	{ "UseEnhancedUsing", &DecompilerSettings::UseEnhancedUsing, &DecompilerSettings::SetUseEnhancedUsing },
	{ "AlwaysUseBraces", &DecompilerSettings::AlwaysUseBraces, &DecompilerSettings::SetAlwaysUseBraces },
	{ "ForEachStatement", &DecompilerSettings::ForEachStatement, &DecompilerSettings::SetForEachStatement },
	{ "ForEachWithGetEnumeratorExtension", &DecompilerSettings::ForEachWithGetEnumeratorExtension, &DecompilerSettings::SetForEachWithGetEnumeratorExtension },
	{ "ParamsCollections", &DecompilerSettings::ParamsCollections, &DecompilerSettings::SetParamsCollections },
	{ "LockStatement", &DecompilerSettings::LockStatement, &DecompilerSettings::SetLockStatement },
	{ "SwitchStatementOnString", &DecompilerSettings::SwitchStatementOnString, &DecompilerSettings::SetSwitchStatementOnString },
	{ "SparseIntegerSwitch", &DecompilerSettings::SparseIntegerSwitch, &DecompilerSettings::SetSparseIntegerSwitch },
	{ "UsingDeclarations", &DecompilerSettings::UsingDeclarations, &DecompilerSettings::SetUsingDeclarations },
	{ "ExtensionMethods", &DecompilerSettings::ExtensionMethods, &DecompilerSettings::SetExtensionMethods },
	{ "QueryExpressions", &DecompilerSettings::QueryExpressions, &DecompilerSettings::SetQueryExpressions },
	{ "UseImplicitMethodGroupConversion", &DecompilerSettings::UseImplicitMethodGroupConversion, &DecompilerSettings::SetUseImplicitMethodGroupConversion },
	{ "UseObjectCreationOfGenericTypeParameter", &DecompilerSettings::UseObjectCreationOfGenericTypeParameter, &DecompilerSettings::SetUseObjectCreationOfGenericTypeParameter },
	{ "AlwaysCastTargetsOfExplicitInterfaceImplementationCalls", &DecompilerSettings::AlwaysCastTargetsOfExplicitInterfaceImplementationCalls, &DecompilerSettings::SetAlwaysCastTargetsOfExplicitInterfaceImplementationCalls },
	{ "AlwaysQualifyMemberReferences", &DecompilerSettings::AlwaysQualifyMemberReferences, &DecompilerSettings::SetAlwaysQualifyMemberReferences },
	{ "AlwaysShowEnumMemberValues", &DecompilerSettings::AlwaysShowEnumMemberValues, &DecompilerSettings::SetAlwaysShowEnumMemberValues },
	{ "UseDebugSymbols", &DecompilerSettings::UseDebugSymbols, &DecompilerSettings::SetUseDebugSymbols },
	{ "ArrayInitializers", &DecompilerSettings::ArrayInitializers, &DecompilerSettings::SetArrayInitializers },
	{ "ObjectOrCollectionInitializers", &DecompilerSettings::ObjectOrCollectionInitializers, &DecompilerSettings::SetObjectOrCollectionInitializers },
	{ "DictionaryInitializers", &DecompilerSettings::DictionaryInitializers, &DecompilerSettings::SetDictionaryInitializers },
	{ "ExtensionMethodsInCollectionInitializers", &DecompilerSettings::ExtensionMethodsInCollectionInitializers, &DecompilerSettings::SetExtensionMethodsInCollectionInitializers },
	{ "UseRefLocalsForAccurateOrderOfEvaluation", &DecompilerSettings::UseRefLocalsForAccurateOrderOfEvaluation, &DecompilerSettings::SetUseRefLocalsForAccurateOrderOfEvaluation },
	{ "RefExtensionMethods", &DecompilerSettings::RefExtensionMethods, &DecompilerSettings::SetRefExtensionMethods },
	{ "StringInterpolation", &DecompilerSettings::StringInterpolation, &DecompilerSettings::SetStringInterpolation },
	{ "Utf8StringLiterals", &DecompilerSettings::Utf8StringLiterals, &DecompilerSettings::SetUtf8StringLiterals },
	{ "SwitchOnReadOnlySpanChar", &DecompilerSettings::SwitchOnReadOnlySpanChar, &DecompilerSettings::SetSwitchOnReadOnlySpanChar },
	{ "UnsignedRightShift", &DecompilerSettings::UnsignedRightShift, &DecompilerSettings::SetUnsignedRightShift },
	{ "CheckedOperators", &DecompilerSettings::CheckedOperators, &DecompilerSettings::SetCheckedOperators },
	{ "ShowXmlDocumentation", &DecompilerSettings::ShowXmlDocumentation, &DecompilerSettings::SetShowXmlDocumentation },
	{ "FoldBraces", &DecompilerSettings::FoldBraces, &DecompilerSettings::SetFoldBraces },
	{ "ExpandXmlDocumentationComments", &DecompilerSettings::ExpandXmlDocumentationComments, &DecompilerSettings::SetExpandXmlDocumentationComments },
	{ "ExpandMemberDefinitions", &DecompilerSettings::ExpandMemberDefinitions, &DecompilerSettings::SetExpandMemberDefinitions },
	{ "ExpandUsingDeclarations", &DecompilerSettings::ExpandUsingDeclarations, &DecompilerSettings::SetExpandUsingDeclarations },
	{ "DecompileMemberBodies", &DecompilerSettings::DecompileMemberBodies, &DecompilerSettings::SetDecompileMemberBodies },
	{ "UseExpressionBodyForCalculatedGetterOnlyProperties", &DecompilerSettings::UseExpressionBodyForCalculatedGetterOnlyProperties, &DecompilerSettings::SetUseExpressionBodyForCalculatedGetterOnlyProperties },
	{ "OutVariables", &DecompilerSettings::OutVariables, &DecompilerSettings::SetOutVariables },
	{ "Discards", &DecompilerSettings::Discards, &DecompilerSettings::SetDiscards },
	{ "IntroduceRefModifiersOnStructs", &DecompilerSettings::IntroduceRefModifiersOnStructs, &DecompilerSettings::SetIntroduceRefModifiersOnStructs },
	{ "IntroduceReadonlyAndInModifiers", &DecompilerSettings::IntroduceReadonlyAndInModifiers, &DecompilerSettings::SetIntroduceReadonlyAndInModifiers },
	{ "IntroducePrivateProtectedAccessibility", &DecompilerSettings::IntroducePrivateProtectedAccessibility, &DecompilerSettings::SetIntroducePrivateProtectedAccessibility },
	{ "ReadOnlyMethods", &DecompilerSettings::ReadOnlyMethods, &DecompilerSettings::SetReadOnlyMethods },
	{ "AsyncUsingAndForEachStatement", &DecompilerSettings::AsyncUsingAndForEachStatement, &DecompilerSettings::SetAsyncUsingAndForEachStatement },
	{ "IntroduceUnmanagedConstraint", &DecompilerSettings::IntroduceUnmanagedConstraint, &DecompilerSettings::SetIntroduceUnmanagedConstraint },
	{ "StackAllocInitializers", &DecompilerSettings::StackAllocInitializers, &DecompilerSettings::SetStackAllocInitializers },
	{ "PatternBasedFixedStatement", &DecompilerSettings::PatternBasedFixedStatement, &DecompilerSettings::SetPatternBasedFixedStatement },
	{ "TupleTypes", &DecompilerSettings::TupleTypes, &DecompilerSettings::SetTupleTypes },
	{ "ThrowExpressions", &DecompilerSettings::ThrowExpressions, &DecompilerSettings::SetThrowExpressions },
	{ "TupleConversions", &DecompilerSettings::TupleConversions, &DecompilerSettings::SetTupleConversions },
	{ "TupleComparisons", &DecompilerSettings::TupleComparisons, &DecompilerSettings::SetTupleComparisons },
	{ "NamedArguments", &DecompilerSettings::NamedArguments, &DecompilerSettings::SetNamedArguments },
	{ "NonTrailingNamedArguments", &DecompilerSettings::NonTrailingNamedArguments, &DecompilerSettings::SetNonTrailingNamedArguments },
	{ "OptionalArguments", &DecompilerSettings::OptionalArguments, &DecompilerSettings::SetOptionalArguments },
	{ "ExpandParamsArguments", &DecompilerSettings::ExpandParamsArguments, &DecompilerSettings::SetExpandParamsArguments },
	{ "LocalFunctions", &DecompilerSettings::LocalFunctions, &DecompilerSettings::SetLocalFunctions },
	{ "Deconstruction", &DecompilerSettings::Deconstruction, &DecompilerSettings::SetDeconstruction },
	{ "PatternMatching", &DecompilerSettings::PatternMatching, &DecompilerSettings::SetPatternMatching },
	{ "RecursivePatternMatching", &DecompilerSettings::RecursivePatternMatching, &DecompilerSettings::SetRecursivePatternMatching },
	{ "PatternCombinators", &DecompilerSettings::PatternCombinators, &DecompilerSettings::SetPatternCombinators },
	{ "RelationalPatterns", &DecompilerSettings::RelationalPatterns, &DecompilerSettings::SetRelationalPatterns },
	{ "StaticLocalFunctions", &DecompilerSettings::StaticLocalFunctions, &DecompilerSettings::SetStaticLocalFunctions },
	{ "Ranges", &DecompilerSettings::Ranges, &DecompilerSettings::SetRanges },
	{ "NullableReferenceTypes", &DecompilerSettings::NullableReferenceTypes, &DecompilerSettings::SetNullableReferenceTypes },
	{ "ShowDebugInfo", &DecompilerSettings::ShowDebugInfo, &DecompilerSettings::SetShowDebugInfo },
	{ "AssumeArrayLengthFitsIntoInt32", &DecompilerSettings::AssumeArrayLengthFitsIntoInt32, &DecompilerSettings::SetAssumeArrayLengthFitsIntoInt32 },
	{ "IntroduceIncrementAndDecrement", &DecompilerSettings::IntroduceIncrementAndDecrement, &DecompilerSettings::SetIntroduceIncrementAndDecrement },
	{ "MakeAssignmentExpressions", &DecompilerSettings::MakeAssignmentExpressions, &DecompilerSettings::SetMakeAssignmentExpressions },
	{ "RemoveDeadCode", &DecompilerSettings::RemoveDeadCode, &DecompilerSettings::SetRemoveDeadCode },
	{ "RemoveDeadStores", &DecompilerSettings::RemoveDeadStores, &DecompilerSettings::SetRemoveDeadStores },
	{ "LoadInMemory", &DecompilerSettings::LoadInMemory, &DecompilerSettings::SetLoadInMemory },
	{ "ThrowOnAssemblyResolveErrors", &DecompilerSettings::ThrowOnAssemblyResolveErrors, &DecompilerSettings::SetThrowOnAssemblyResolveErrors },
	{ "ApplyWindowsRuntimeProjections", &DecompilerSettings::ApplyWindowsRuntimeProjections, &DecompilerSettings::SetApplyWindowsRuntimeProjections },
	{ "AutoLoadAssemblyReferences", &DecompilerSettings::AutoLoadAssemblyReferences, &DecompilerSettings::SetAutoLoadAssemblyReferences },
	{ "ForStatement", &DecompilerSettings::ForStatement, &DecompilerSettings::SetForStatement },
	{ "DoWhileStatement", &DecompilerSettings::DoWhileStatement, &DecompilerSettings::SetDoWhileStatement },
	{ "RefReadOnlyParameters", &DecompilerSettings::RefReadOnlyParameters, &DecompilerSettings::SetRefReadOnlyParameters },
	{ "UsePrimaryConstructorSyntaxForNonRecordTypes", &DecompilerSettings::UsePrimaryConstructorSyntaxForNonRecordTypes, &DecompilerSettings::SetUsePrimaryConstructorSyntaxForNonRecordTypes },
	{ "InlineArrays", &DecompilerSettings::InlineArrays, &DecompilerSettings::SetInlineArrays },
	{ "ExtensionMembers", &DecompilerSettings::ExtensionMembers, &DecompilerSettings::SetExtensionMembers },
	{ "FirstClassSpanTypes", &DecompilerSettings::FirstClassSpanTypes, &DecompilerSettings::SetFirstClassSpanTypes },
	{ "SeparateLocalVariableDeclarations", &DecompilerSettings::SeparateLocalVariableDeclarations, &DecompilerSettings::SetSeparateLocalVariableDeclarations },
	{ "UseSdkStyleProjectFormat", &DecompilerSettings::UseSdkStyleProjectFormat, &DecompilerSettings::SetUseSdkStyleProjectFormat },
	{ "UseNestedDirectoriesForNamespaces", &DecompilerSettings::UseNestedDirectoriesForNamespaces, &DecompilerSettings::SetUseNestedDirectoriesForNamespaces },
	{ "AggressiveScalarReplacementOfAggregates", &DecompilerSettings::AggressiveScalarReplacementOfAggregates, &DecompilerSettings::SetAggressiveScalarReplacementOfAggregates },
	{ "AggressiveInlining", &DecompilerSettings::AggressiveInlining, &DecompilerSettings::SetAggressiveInlining },
	{ "AlwaysUseGlobal", &DecompilerSettings::AlwaysUseGlobal, &DecompilerSettings::SetAlwaysUseGlobal },
	{ "AlwaysMoveInitializer", &DecompilerSettings::AlwaysMoveInitializer, &DecompilerSettings::SetAlwaysMoveInitializer },
	{ "SortCustomAttributes", &DecompilerSettings::SortCustomAttributes, &DecompilerSettings::SetSortCustomAttributes },
	{ "SortSwitchSections", &DecompilerSettings::SortSwitchSections, &DecompilerSettings::SetSortSwitchSections },
	{ "CheckForOverflowUnderflow", &DecompilerSettings::CheckForOverflowUnderflow, &DecompilerSettings::SetCheckForOverflowUnderflow },
};
constexpr int kGetterCount = static_cast<int>(sizeof(kGetters) / sizeof(kGetters[0]));

// The version-name table the fixture rows parse through.
struct VersionRow {
	const char* Name;
	LanguageVersion Value;
};
constexpr VersionRow kVersions[] = {
	{ "CSharp1", LanguageVersion::CSharp1 },
	{ "CSharp2", LanguageVersion::CSharp2 },
	{ "CSharp3", LanguageVersion::CSharp3 },
	{ "CSharp4", LanguageVersion::CSharp4 },
	{ "CSharp5", LanguageVersion::CSharp5 },
	{ "CSharp6", LanguageVersion::CSharp6 },
	{ "CSharp7", LanguageVersion::CSharp7 },
	{ "CSharp7_1", LanguageVersion::CSharp7_1 },
	{ "CSharp7_2", LanguageVersion::CSharp7_2 },
	{ "CSharp7_3", LanguageVersion::CSharp7_3 },
	{ "CSharp8_0", LanguageVersion::CSharp8_0 },
	{ "CSharp9_0", LanguageVersion::CSharp9_0 },
	{ "CSharp10_0", LanguageVersion::CSharp10_0 },
	{ "CSharp11_0", LanguageVersion::CSharp11_0 },
	{ "CSharp12_0", LanguageVersion::CSharp12_0 },
	{ "CSharp13_0", LanguageVersion::CSharp13_0 },
	{ "CSharp14_0", LanguageVersion::CSharp14_0 },
	{ "CSharp15_0", LanguageVersion::CSharp15_0 },
	{ "Preview", LanguageVersion::Preview },
	{"Latest", LanguageVersion::Latest},
};

LanguageVersion ParseVersion(const char* name)
{
	for (const VersionRow& v : kVersions)
	{
		if (std::strcmp(v.Name, name) == 0)
			return v.Value;
	}
	ADD_FAILURE() << "unknown version " << name;
	return LanguageVersion::Latest;
}

bool TableHas(const char* csv, const char* name)
{
	std::string_view list(csv);
	size_t pos = 0;
	while (pos <= list.size())
	{
		size_t comma = list.find(',', pos);
		std::string_view item = list.substr(
			pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
		if (item == name)
			return true;
		if (comma == std::string_view::npos)
			break;
		pos = comma + 1;
	}
	return false;
}

const ILSpy::Tests::SettingsDefaultRow* FindDefault(const char* name)
{
	for (const ILSpy::Tests::SettingsDefaultRow& d : ILSpy::Tests::kSettingsDefaults)
	{
		if (std::strcmp(d.Name, name) == 0)
			return &d;
	}
	return nullptr;
}

const ILSpy::Tests::SettingsMinVerRow* FindMinVer(const char* name)
{
	for (const ILSpy::Tests::SettingsMinVerRow& m : ILSpy::Tests::kSettingsMinVersions)
	{
		if (std::strcmp(m.Name, name) == 0)
			return &m;
	}
	return nullptr;
}

} // namespace

// The C# property defaults match the generated table (extracted from the source and
// cross-checked against the shipped engine's D section).
TEST(DecompilerSettingsTest, DefaultsMatchTheSourceTable)
{
	for (const GetterRow& row : kGetters)
	{
		const ILSpy::Tests::SettingsDefaultRow* expected = FindDefault(row.Name);
		ASSERT_NE(expected, nullptr) << row.Name;
		DecompilerSettings s;
		EXPECT_EQ(expected->Value, (s.*(row.Get))()) << row.Name;
	}
}

// The getter count matches the fixture table length (a dropped accessor pair fails here
// before any value comparison can pass vacuously).
TEST(DecompilerSettingsTest, GetterTableCoversEveryFixtureRow)
{
	int fixtureCount = 0;
	for (const ILSpy::Tests::SettingsDefaultRow& d : ILSpy::Tests::kSettingsDefaults)
	{
		(void)d;
		++fixtureCount;
	}
	EXPECT_EQ(fixtureCount, kGetterCount);
}

// SetLanguageVersion disables every block STRICTLY NEWER than the version (the value
// comparison the C# if-ladder performs), leaving the false-default fields false.
TEST(DecompilerSettingsTest, SetLanguageVersionDisablesNewerFeatureBlocks)
{
	for (const ILSpy::Tests::SettingsVersionRow& v : ILSpy::Tests::kSettingsVersions)
	{
		DecompilerSettings s;
		s.SetLanguageVersion(ParseVersion(v.Version));
		for (const GetterRow& row : kGetters)
		{
			const ILSpy::Tests::SettingsDefaultRow* d = FindDefault(row.Name);
			ASSERT_NE(d, nullptr) << row.Name;
			const bool expected = d->Value && !TableHas(v.Disabled, row.Name);
			EXPECT_EQ(expected, (s.*(row.Get))()) << v.Version << " " << row.Name;
		}
	}
}

// The settings-ctor chain agrees with the direct SetLanguageVersion call.
TEST(DecompilerSettingsTest, CtorChainsThroughSetLanguageVersion)
{
	for (const ILSpy::Tests::SettingsVersionRow& v : ILSpy::Tests::kSettingsVersions)
	{
		DecompilerSettings chained(ParseVersion(v.Version));
		DecompilerSettings direct;
		direct.SetLanguageVersion(ParseVersion(v.Version));
		for (const GetterRow& row : kGetters)
		{
			ASSERT_EQ((chained.*(row.Get))(), (direct.*(row.Get))()) << v.Version << " " << row.Name;
		}
	}
}

// The C# "the call is not reversible" contract: a later higher-version call never
// re-enables anything a lower-version call disabled.
TEST(DecompilerSettingsTest, NothingIsReEnabledByALaterCall)
{
	DecompilerSettings s;
	s.SetLanguageVersion(LanguageVersion::CSharp1);
	DecompilerSettings snapshot = s;
	s.SetLanguageVersion(LanguageVersion::Latest);
	for (const GetterRow& row : kGetters)
	{
		EXPECT_EQ((snapshot.*(row.Get))(), (s.*(row.Get))()) << row.Name;
	}
}

// The per-setting minimum-version table (each property enabled alone from ctor(CSharp1)).
TEST(DecompilerSettingsTest, GetMinimumRequiredVersionPerSetting)
{
	for (const GetterRow& row : kGetters)
	{
		const ILSpy::Tests::SettingsMinVerRow* expected = FindMinVer(row.Name);
		ASSERT_NE(expected, nullptr) << row.Name;
		DecompilerSettings s(LanguageVersion::CSharp1);
		(s.*(row.Set))(true);
		EXPECT_EQ(ParseVersion(expected->Version), s.GetMinimumRequiredVersion()) << row.Name;
	}
}

// The default instance answers the highest gated version (extensionMembers and
// firstClassSpanTypes are on), matching the shipped engine's X/Latest row.
TEST(DecompilerSettingsTest, GetMinimumRequiredVersionOnDefaultsIsCSharp14)
{
	DecompilerSettings s;
	EXPECT_EQ(LanguageVersion::CSharp14_0, s.GetMinimumRequiredVersion());
}

// The [Obsolete] LifetimeAnnotations alias forwards to ScopedRef both ways.
TEST(DecompilerSettingsTest, AliasPropertyForwardsToScopedRef)
{
	DecompilerSettings s;
	s.SetLifetimeAnnotations(false);
	EXPECT_FALSE(s.ScopedRef());
	EXPECT_FALSE(s.LifetimeAnnotations());
	s.SetScopedRef(true);
	EXPECT_TRUE(s.LifetimeAnnotations());
	EXPECT_TRUE(s.ScopedRef());
}

// Clone isolates the settings and deep-copies the formatting options.
TEST(DecompilerSettingsTest, CloneIsolatesSettings)
{
	DecompilerSettings s;
	s.SetLiftNullables(false);
	s.CSharpFormattingOptions().IndentSwitchBody = false;
	DecompilerSettings c = s.Clone();
	c.SetLiftNullables(true);
	c.CSharpFormattingOptions().IndentSwitchBody = true;
	EXPECT_FALSE(s.LiftNullables());
	EXPECT_TRUE(c.LiftNullables());
	EXPECT_FALSE(s.CSharpFormattingOptions().IndentSwitchBody);
	EXPECT_TRUE(c.CSharpFormattingOptions().IndentSwitchBody);
}

// The lazy CSharpFormattingOptions default is the CreateAllman instance with the three
// mutations, materialized once (the same object on every access).
TEST(DecompilerSettingsTest, CSharpFormattingOptionsLazyDefault)
{
	DecompilerSettings s;
	ILSpy::Decompiler::CSharp::OutputVisitor::CSharpFormattingOptions& fmt =
		s.CSharpFormattingOptions();
	EXPECT_FALSE(fmt.IndentSwitchBody);
	EXPECT_EQ(ILSpy::Decompiler::CSharp::OutputVisitor::Wrapping::WrapIfTooLong,
		fmt.ArrayInitializerWrapping);
	EXPECT_EQ(ILSpy::Decompiler::CSharp::OutputVisitor::PropertyFormatting::SingleLine,
		fmt.AutoPropertyFormatting);
	EXPECT_EQ(&fmt, &s.CSharpFormattingOptions());
}

// The setter's ArgumentNullException() null check (the C# parameterless message).
TEST(DecompilerSettingsTest, SetCSharpFormattingOptionsThrowsOnNull)
{
	DecompilerSettings s;
	try
	{
		s.SetCSharpFormattingOptions(std::nullopt);
		FAIL() << "expected the null-formatting throw";
	}
	catch (const std::invalid_argument& e)
	{
		EXPECT_STREQ("Value cannot be null.", e.what());
	}
}

// A custom formatting-options value replaces the lazy default.
TEST(DecompilerSettingsTest, SetCSharpFormattingOptionsReplacesValue)
{
	DecompilerSettings s;
	ILSpy::Decompiler::CSharp::OutputVisitor::CSharpFormattingOptions custom;
	custom.IndentSwitchBody = true;
	s.SetCSharpFormattingOptions(custom);
	EXPECT_TRUE(s.CSharpFormattingOptions().IndentSwitchBody);
	// The setter assigns by value (the value-type stand-in for the C# reference), so the
	// getter hands back the STORED copy, not the caller's local.
	EXPECT_NE(&custom, &s.CSharpFormattingOptions());
}

// A setter round-trip (the compare-then-write shape; the PropertyChanged fire arm is the
// documented deferral, so the write is the only observable).
TEST(DecompilerSettingsTest, SetterRoundTrip)
{
	DecompilerSettings s;
	for (const GetterRow& row : kGetters)
	{
		const bool before = (s.*(row.Get))();
		(s.*(row.Set))(!before);
		ASSERT_EQ(!before, (s.*(row.Get))()) << row.Name;
		(s.*(row.Set))(before);
		EXPECT_EQ(before, (s.*(row.Get))()) << row.Name;
	}
}
