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

// Implementation of ILSpyX/Settings/DecompilerSettings.hpp (the porting
// decisions are on the header).

#include "ILSpyX/Settings/DecompilerSettings.hpp"

#include "Decompiler/Xml/XElement.hpp"

#include <cstddef>
#include <stdexcept>

namespace ILSpy::ILSpyX::Settings {
namespace {

// The reflected surface of the C# (the Browsable public bool properties,
// in declaration order): 110 flags.
struct FlagRow {
    const char* name;
    bool (EngineSettings::*getter)() const;
    void (EngineSettings::*setter)(bool);
};

const FlagRow kFlagRows[] = {
    {"NativeIntegers", &EngineSettings::NativeIntegers, &EngineSettings::SetNativeIntegers},
    {"NumericIntPtr", &EngineSettings::NumericIntPtr, &EngineSettings::SetNumericIntPtr},
    {"CovariantReturns", &EngineSettings::CovariantReturns, &EngineSettings::SetCovariantReturns},
    {"InitAccessors", &EngineSettings::InitAccessors, &EngineSettings::SetInitAccessors},
    {"RecordClasses", &EngineSettings::RecordClasses, &EngineSettings::SetRecordClasses},
    {"RecordStructs", &EngineSettings::RecordStructs, &EngineSettings::SetRecordStructs},
    {"StructDefaultConstructorsAndFieldInitializers", &EngineSettings::StructDefaultConstructorsAndFieldInitializers, &EngineSettings::SetStructDefaultConstructorsAndFieldInitializers},
    {"WithExpressions", &EngineSettings::WithExpressions, &EngineSettings::SetWithExpressions},
    {"UsePrimaryConstructorSyntax", &EngineSettings::UsePrimaryConstructorSyntax, &EngineSettings::SetUsePrimaryConstructorSyntax},
    {"FunctionPointers", &EngineSettings::FunctionPointers, &EngineSettings::SetFunctionPointers},
    {"ScopedRef", &EngineSettings::ScopedRef, &EngineSettings::SetScopedRef},
    {"RequiredMembers", &EngineSettings::RequiredMembers, &EngineSettings::SetRequiredMembers},
    {"SwitchExpressions", &EngineSettings::SwitchExpressions, &EngineSettings::SetSwitchExpressions},
    {"FileScopedNamespaces", &EngineSettings::FileScopedNamespaces, &EngineSettings::SetFileScopedNamespaces},
    {"AnonymousMethods", &EngineSettings::AnonymousMethods, &EngineSettings::SetAnonymousMethods},
    {"AnonymousTypes", &EngineSettings::AnonymousTypes, &EngineSettings::SetAnonymousTypes},
    {"UseLambdaSyntax", &EngineSettings::UseLambdaSyntax, &EngineSettings::SetUseLambdaSyntax},
    {"ExpressionTrees", &EngineSettings::ExpressionTrees, &EngineSettings::SetExpressionTrees},
    {"YieldReturn", &EngineSettings::YieldReturn, &EngineSettings::SetYieldReturn},
    {"Dynamic", &EngineSettings::Dynamic, &EngineSettings::SetDynamic},
    {"AsyncAwait", &EngineSettings::AsyncAwait, &EngineSettings::SetAsyncAwait},
    {"AwaitInCatchFinally", &EngineSettings::AwaitInCatchFinally, &EngineSettings::SetAwaitInCatchFinally},
    {"AsyncEnumerator", &EngineSettings::AsyncEnumerator, &EngineSettings::SetAsyncEnumerator},
    {"DecimalConstants", &EngineSettings::DecimalConstants, &EngineSettings::SetDecimalConstants},
    {"FixedBuffers", &EngineSettings::FixedBuffers, &EngineSettings::SetFixedBuffers},
    {"StringConcat", &EngineSettings::StringConcat, &EngineSettings::SetStringConcat},
    {"LiftNullables", &EngineSettings::LiftNullables, &EngineSettings::SetLiftNullables},
    {"NullPropagation", &EngineSettings::NullPropagation, &EngineSettings::SetNullPropagation},
    {"AutomaticProperties", &EngineSettings::AutomaticProperties, &EngineSettings::SetAutomaticProperties},
    {"GetterOnlyAutomaticProperties", &EngineSettings::GetterOnlyAutomaticProperties, &EngineSettings::SetGetterOnlyAutomaticProperties},
    {"AutomaticEvents", &EngineSettings::AutomaticEvents, &EngineSettings::SetAutomaticEvents},
    {"UsingStatement", &EngineSettings::UsingStatement, &EngineSettings::SetUsingStatement},
    {"UseEnhancedUsing", &EngineSettings::UseEnhancedUsing, &EngineSettings::SetUseEnhancedUsing},
    {"AlwaysUseBraces", &EngineSettings::AlwaysUseBraces, &EngineSettings::SetAlwaysUseBraces},
    {"ForEachStatement", &EngineSettings::ForEachStatement, &EngineSettings::SetForEachStatement},
    {"ForEachWithGetEnumeratorExtension", &EngineSettings::ForEachWithGetEnumeratorExtension, &EngineSettings::SetForEachWithGetEnumeratorExtension},
    {"ParamsCollections", &EngineSettings::ParamsCollections, &EngineSettings::SetParamsCollections},
    {"LockStatement", &EngineSettings::LockStatement, &EngineSettings::SetLockStatement},
    {"SwitchStatementOnString", &EngineSettings::SwitchStatementOnString, &EngineSettings::SetSwitchStatementOnString},
    {"SparseIntegerSwitch", &EngineSettings::SparseIntegerSwitch, &EngineSettings::SetSparseIntegerSwitch},
    {"UsingDeclarations", &EngineSettings::UsingDeclarations, &EngineSettings::SetUsingDeclarations},
    {"ExtensionMethods", &EngineSettings::ExtensionMethods, &EngineSettings::SetExtensionMethods},
    {"QueryExpressions", &EngineSettings::QueryExpressions, &EngineSettings::SetQueryExpressions},
    {"UseImplicitMethodGroupConversion", &EngineSettings::UseImplicitMethodGroupConversion, &EngineSettings::SetUseImplicitMethodGroupConversion},
    {"UseObjectCreationOfGenericTypeParameter", &EngineSettings::UseObjectCreationOfGenericTypeParameter, &EngineSettings::SetUseObjectCreationOfGenericTypeParameter},
    {"AlwaysCastTargetsOfExplicitInterfaceImplementationCalls", &EngineSettings::AlwaysCastTargetsOfExplicitInterfaceImplementationCalls, &EngineSettings::SetAlwaysCastTargetsOfExplicitInterfaceImplementationCalls},
    {"AlwaysQualifyMemberReferences", &EngineSettings::AlwaysQualifyMemberReferences, &EngineSettings::SetAlwaysQualifyMemberReferences},
    {"AlwaysShowEnumMemberValues", &EngineSettings::AlwaysShowEnumMemberValues, &EngineSettings::SetAlwaysShowEnumMemberValues},
    {"UseDebugSymbols", &EngineSettings::UseDebugSymbols, &EngineSettings::SetUseDebugSymbols},
    {"ArrayInitializers", &EngineSettings::ArrayInitializers, &EngineSettings::SetArrayInitializers},
    {"ObjectOrCollectionInitializers", &EngineSettings::ObjectOrCollectionInitializers, &EngineSettings::SetObjectOrCollectionInitializers},
    {"DictionaryInitializers", &EngineSettings::DictionaryInitializers, &EngineSettings::SetDictionaryInitializers},
    {"ExtensionMethodsInCollectionInitializers", &EngineSettings::ExtensionMethodsInCollectionInitializers, &EngineSettings::SetExtensionMethodsInCollectionInitializers},
    {"UseRefLocalsForAccurateOrderOfEvaluation", &EngineSettings::UseRefLocalsForAccurateOrderOfEvaluation, &EngineSettings::SetUseRefLocalsForAccurateOrderOfEvaluation},
    {"RefExtensionMethods", &EngineSettings::RefExtensionMethods, &EngineSettings::SetRefExtensionMethods},
    {"StringInterpolation", &EngineSettings::StringInterpolation, &EngineSettings::SetStringInterpolation},
    {"Utf8StringLiterals", &EngineSettings::Utf8StringLiterals, &EngineSettings::SetUtf8StringLiterals},
    {"SwitchOnReadOnlySpanChar", &EngineSettings::SwitchOnReadOnlySpanChar, &EngineSettings::SetSwitchOnReadOnlySpanChar},
    {"UnsignedRightShift", &EngineSettings::UnsignedRightShift, &EngineSettings::SetUnsignedRightShift},
    {"CheckedOperators", &EngineSettings::CheckedOperators, &EngineSettings::SetCheckedOperators},
    {"ShowXmlDocumentation", &EngineSettings::ShowXmlDocumentation, &EngineSettings::SetShowXmlDocumentation},
    {"UseExpressionBodyForCalculatedGetterOnlyProperties", &EngineSettings::UseExpressionBodyForCalculatedGetterOnlyProperties, &EngineSettings::SetUseExpressionBodyForCalculatedGetterOnlyProperties},
    {"OutVariables", &EngineSettings::OutVariables, &EngineSettings::SetOutVariables},
    {"Discards", &EngineSettings::Discards, &EngineSettings::SetDiscards},
    {"IntroduceRefModifiersOnStructs", &EngineSettings::IntroduceRefModifiersOnStructs, &EngineSettings::SetIntroduceRefModifiersOnStructs},
    {"IntroduceReadonlyAndInModifiers", &EngineSettings::IntroduceReadonlyAndInModifiers, &EngineSettings::SetIntroduceReadonlyAndInModifiers},
    {"IntroducePrivateProtectedAccessibility", &EngineSettings::IntroducePrivateProtectedAccessibility, &EngineSettings::SetIntroducePrivateProtectedAccessibility},
    {"ReadOnlyMethods", &EngineSettings::ReadOnlyMethods, &EngineSettings::SetReadOnlyMethods},
    {"AsyncUsingAndForEachStatement", &EngineSettings::AsyncUsingAndForEachStatement, &EngineSettings::SetAsyncUsingAndForEachStatement},
    {"IntroduceUnmanagedConstraint", &EngineSettings::IntroduceUnmanagedConstraint, &EngineSettings::SetIntroduceUnmanagedConstraint},
    {"StackAllocInitializers", &EngineSettings::StackAllocInitializers, &EngineSettings::SetStackAllocInitializers},
    {"PatternBasedFixedStatement", &EngineSettings::PatternBasedFixedStatement, &EngineSettings::SetPatternBasedFixedStatement},
    {"TupleTypes", &EngineSettings::TupleTypes, &EngineSettings::SetTupleTypes},
    {"ThrowExpressions", &EngineSettings::ThrowExpressions, &EngineSettings::SetThrowExpressions},
    {"TupleConversions", &EngineSettings::TupleConversions, &EngineSettings::SetTupleConversions},
    {"TupleComparisons", &EngineSettings::TupleComparisons, &EngineSettings::SetTupleComparisons},
    {"NamedArguments", &EngineSettings::NamedArguments, &EngineSettings::SetNamedArguments},
    {"NonTrailingNamedArguments", &EngineSettings::NonTrailingNamedArguments, &EngineSettings::SetNonTrailingNamedArguments},
    {"OptionalArguments", &EngineSettings::OptionalArguments, &EngineSettings::SetOptionalArguments},
    {"ExpandParamsArguments", &EngineSettings::ExpandParamsArguments, &EngineSettings::SetExpandParamsArguments},
    {"LocalFunctions", &EngineSettings::LocalFunctions, &EngineSettings::SetLocalFunctions},
    {"Deconstruction", &EngineSettings::Deconstruction, &EngineSettings::SetDeconstruction},
    {"PatternMatching", &EngineSettings::PatternMatching, &EngineSettings::SetPatternMatching},
    {"RecursivePatternMatching", &EngineSettings::RecursivePatternMatching, &EngineSettings::SetRecursivePatternMatching},
    {"PatternCombinators", &EngineSettings::PatternCombinators, &EngineSettings::SetPatternCombinators},
    {"RelationalPatterns", &EngineSettings::RelationalPatterns, &EngineSettings::SetRelationalPatterns},
    {"StaticLocalFunctions", &EngineSettings::StaticLocalFunctions, &EngineSettings::SetStaticLocalFunctions},
    {"Ranges", &EngineSettings::Ranges, &EngineSettings::SetRanges},
    {"NullableReferenceTypes", &EngineSettings::NullableReferenceTypes, &EngineSettings::SetNullableReferenceTypes},
    {"RemoveDeadCode", &EngineSettings::RemoveDeadCode, &EngineSettings::SetRemoveDeadCode},
    {"RemoveDeadStores", &EngineSettings::RemoveDeadStores, &EngineSettings::SetRemoveDeadStores},
    {"ApplyWindowsRuntimeProjections", &EngineSettings::ApplyWindowsRuntimeProjections, &EngineSettings::SetApplyWindowsRuntimeProjections},
    {"AutoLoadAssemblyReferences", &EngineSettings::AutoLoadAssemblyReferences, &EngineSettings::SetAutoLoadAssemblyReferences},
    {"ForStatement", &EngineSettings::ForStatement, &EngineSettings::SetForStatement},
    {"DoWhileStatement", &EngineSettings::DoWhileStatement, &EngineSettings::SetDoWhileStatement},
    {"RefReadOnlyParameters", &EngineSettings::RefReadOnlyParameters, &EngineSettings::SetRefReadOnlyParameters},
    {"UsePrimaryConstructorSyntaxForNonRecordTypes", &EngineSettings::UsePrimaryConstructorSyntaxForNonRecordTypes, &EngineSettings::SetUsePrimaryConstructorSyntaxForNonRecordTypes},
    {"InlineArrays", &EngineSettings::InlineArrays, &EngineSettings::SetInlineArrays},
    {"ExtensionMembers", &EngineSettings::ExtensionMembers, &EngineSettings::SetExtensionMembers},
    {"FirstClassSpanTypes", &EngineSettings::FirstClassSpanTypes, &EngineSettings::SetFirstClassSpanTypes},
    {"SeparateLocalVariableDeclarations", &EngineSettings::SeparateLocalVariableDeclarations, &EngineSettings::SetSeparateLocalVariableDeclarations},
    {"UseSdkStyleProjectFormat", &EngineSettings::UseSdkStyleProjectFormat, &EngineSettings::SetUseSdkStyleProjectFormat},
    {"UseNestedDirectoriesForNamespaces", &EngineSettings::UseNestedDirectoriesForNamespaces, &EngineSettings::SetUseNestedDirectoriesForNamespaces},
    {"AggressiveScalarReplacementOfAggregates", &EngineSettings::AggressiveScalarReplacementOfAggregates, &EngineSettings::SetAggressiveScalarReplacementOfAggregates},
    {"AggressiveInlining", &EngineSettings::AggressiveInlining, &EngineSettings::SetAggressiveInlining},
    {"AlwaysUseGlobal", &EngineSettings::AlwaysUseGlobal, &EngineSettings::SetAlwaysUseGlobal},
    {"AlwaysMoveInitializer", &EngineSettings::AlwaysMoveInitializer, &EngineSettings::SetAlwaysMoveInitializer},
    {"SortCustomAttributes", &EngineSettings::SortCustomAttributes, &EngineSettings::SetSortCustomAttributes},
    {"SortSwitchSections", &EngineSettings::SortSwitchSections, &EngineSettings::SetSortSwitchSections},
    {"CheckForOverflowUnderflow", &EngineSettings::CheckForOverflowUnderflow, &EngineSettings::SetCheckForOverflowUnderflow},
};

constexpr std::size_t kFlagRowCount = sizeof(kFlagRows) / sizeof(kFlagRows[0]);

// The C# `(bool?)XAttribute` explicit cast: XmlConvert.ToBoolean's
// accepted forms, and the FormatException for anything else.
bool ParseXmlBoolean(const std::string& text)
{
    if (text == "true" || text == "1")
        return true;
    if (text == "false" || text == "0")
        return false;
    throw std::runtime_error("The string '" + text
        + "' is not a valid Boolean value.");
}

}  // namespace

void DecompilerSettings::LoadFromXml(
    const Decompiler::Xml::XElement& section)
{
    for (const FlagRow& row : kFlagRows) {
        const Decompiler::Xml::XAttribute* attribute =
            section.Attribute(Decompiler::Xml::XName(row.name));
        if (attribute == nullptr)
            continue;
        (this->*row.setter)(ParseXmlBoolean(attribute->Value()));
    }
}

std::shared_ptr<Decompiler::Xml::XElement> DecompilerSettings::SaveToXml()
    const
{
    auto section = std::make_shared<Decompiler::Xml::XElement>(
        Decompiler::Xml::XName("DecompilerSettings"));
    for (const FlagRow& row : kFlagRows) {
        section->SetAttributeValue(Decompiler::Xml::XName(row.name),
            (this->*row.getter)() ? std::string("true") : std::string("false"));
    }
    return section;
}

bool DecompilerSettings::IsKnownOption(const std::string& name)
{
    for (const FlagRow& row : kFlagRows) {
        if (name == row.name)
            return true;
    }
    return false;
}

Decompiler::TypeSystem::TypeSystemOptions
DecompilerSettings::GetOptions(const EngineSettings& settings)
{
    // The C# DecompilerTypeSystem.GetOptions mapping, flag for flag.
    using TypeSystemOptions = Decompiler::TypeSystem::TypeSystemOptions;
    TypeSystemOptions typeSystemOptions = TypeSystemOptions::None;
    if (settings.Dynamic()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::Dynamic;
    }
    if (settings.TupleTypes()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::Tuple;
    }
    if (settings.ExtensionMethods()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::ExtensionMethods;
    }
    if (settings.DecimalConstants()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::DecimalConstants;
    }
    if (settings.IntroduceRefModifiersOnStructs()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::RefStructs;
    }
    if (settings.IntroduceReadonlyAndInModifiers()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::ReadOnlyStructsAndParameters;
    }
    if (settings.IntroduceUnmanagedConstraint()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::UnmanagedConstraints;
    }
    if (settings.NullableReferenceTypes()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::NullabilityAnnotations;
    }
    if (settings.ReadOnlyMethods()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::ReadOnlyMethods;
    }
    if (settings.NativeIntegers()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::NativeIntegers;
    }
    if (settings.FunctionPointers()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::FunctionPointers;
    }
    if (settings.ScopedRef()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::ScopedRef;
    }
    if (settings.NumericIntPtr()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::NativeIntegersWithoutAttribute;
    }
    if (settings.RefReadOnlyParameters()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::RefReadOnlyParameters;
    }
    if (settings.ParamsCollections()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::ParamsCollections;
    }
    if (settings.FirstClassSpanTypes()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::FirstClassSpanTypes;
    }
    if (settings.ExtensionMembers()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::ExtensionMembers;
    }
    if (settings.AsyncAwait()) {
        typeSystemOptions = typeSystemOptions | TypeSystemOptions::RuntimeAsync;
    }
    return typeSystemOptions;
}

}  // namespace ILSpy::ILSpyX::Settings
