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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the SortByNameProcessor port (cpp/Decompiler/Disassembler/
// SortByNameProcessor.{hpp,cpp} + IEntityProcessor.hpp) and the metadata reads
// its sort keys compose (MetadataFile::GetCustomAttributeTokens/
// GetCustomAttribute, GetInterfaceImplementation(s), GetPropertyName,
// GetEventName, SRMExtensions::GetDeclaringType, and the TypeSpec arm of
// SRMExtensions::GetFullTypeName). The mscorlib/System.dll fixtures and every
// pinned sort order were verified against .NET's System.Reflection.Metadata
// over the same files.

#include "Decompiler/Disassembler/IEntityProcessor.hpp"
#include "Decompiler/Disassembler/SortByNameProcessor.hpp"
#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "Decompiler/Util/StringComparers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DA = ILSpy::Decompiler::Disassembler;
namespace MD = ILSpy::Decompiler::Metadata;
namespace OUT = ILSpy::Decompiler::Output;

namespace {

#if defined(_WIN32)
const char* MscorlibPath() { return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll"; }
const char* SystemDllPath() {
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
        "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
}
#else
const char* MscorlibPath() { return "/usr/lib/mono/4.5/mscorlib.dll"; }
const char* SystemDllPath() { return "/usr/lib/mono/4.5/System.dll"; }
#endif

std::uint32_t FindTypeDefTokenIn(const MD::MetadataFile& f, std::string_view ns,
    std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindMethodIn(const MD::MetadataFile& f, std::uint32_t typeToken,
    std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

std::uint32_t FindFieldIn(const MD::MetadataFile& f, std::uint32_t typeToken,
    std::string_view name) {
    for (const auto& fd : f.GetFields(typeToken)) {
        if (fd.Name == name) return fd.Token;
    }
    return 0;
}

// The declaring type's full IL name of a custom attribute's constructor --
// the SortByNameProcessor.CustomAttribute sort key (System.Reflection.Metadata
// ground truth: System.String's three attributes are DefaultMemberAttribute,
// ComVisibleAttribute and __DynamicallyInvokableAttribute, all with MethodDef
// constructors inside mscorlib itself).
std::string AttributeKey(const MD::MetadataFile& f, std::uint32_t attributeToken) {
    auto row = f.GetCustomAttribute(attributeToken);
    EXPECT_TRUE(row.has_value());
    return MD::ToILNameString(MD::GetFullTypeName(
        f, MD::GetDeclaringType(f, row->ConstructorToken)));
}

// The sorted keys of a processed token vector.
std::vector<std::string> SortedKeys(const MD::MetadataFile& f,
    const std::vector<std::uint32_t>& sortedTokens, DA::ProcessedEntityKind kind) {
    std::vector<std::string> keys;
    keys.reserve(sortedTokens.size());
    for (std::uint32_t token : sortedTokens)
        keys.push_back(DA::SortByNameProcessor::GetSortKey(f, token, kind));
    return keys;
}

void ExpectNonDecreasing(const std::vector<std::string>& keys) {
    for (std::size_t i = 1; i < keys.size(); i++) {
        EXPECT_LE(ILSpy::Decompiler::Util::CompareInvariantCulture(
                      keys[i - 1], keys[i]), 0)
            << "key " << keys[i - 1] << " must not exceed " << keys[i];
    }
}

// The '|'-joined form the exact-sequence assertions compare against.
std::string Join(const std::vector<std::string>& parts) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += '|';
        result += parts[i];
    }
    return result;
}

template <typename T>
std::string Join(const std::vector<T>& parts, char sep) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += sep;
        result += std::to_string(parts[i]);
    }
    return result;
}

std::string JoinTokens(const std::vector<std::uint32_t>& tokens) {
    std::string result;
    char buf[16];
    for (std::size_t i = 0; i < tokens.size(); i++) {
        if (i > 0) result += '|';
        std::snprintf(buf, sizeof(buf), "%08X", tokens[i]);
        result += buf;
    }
    return result;
}

}  // namespace

TEST(SortByNameProcessorTest, AttributeTokensEnumeratePerParent)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    auto tokens = f.GetCustomAttributeTokens(stringType);
    ASSERT_EQ(tokens.size(), 3u);
    // Rows come back in table order (the C# CustomAttributeHandleCollection
    // order): DefaultMemberAttribute, ComVisibleAttribute, __DynamicallyInvokable.
    std::vector<std::string> decls;
    for (std::uint32_t t : tokens) {
        auto row = f.GetCustomAttribute(t);
        ASSERT_TRUE(row.has_value());
        EXPECT_EQ(row->Token, t);
        decls.push_back(AttributeKey(f, t));
    }
    EXPECT_EQ(Join(decls),
        "System.Reflection.DefaultMemberAttribute|"
        "System.Runtime.InteropServices.ComVisibleAttribute|"
        "__DynamicallyInvokableAttribute");
}

TEST(SortByNameProcessorTest, CustomAttributeRowCarriesConstructorAndBlob)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    auto tokens = f.GetCustomAttributeTokens(stringType);
    ASSERT_EQ(tokens.size(), 3u);
    // The value-blob lengths verified against SRM: DefaultMember("Chars")
    // carries a 10-byte blob, ComVisible(true) 5 bytes, the argument-less
    // __DynamicallyInvokable 4 bytes (prolog + named-argument count).
    std::vector<std::size_t> lengths;
    for (std::uint32_t t : tokens) {
        auto row = f.GetCustomAttribute(t);
        ASSERT_TRUE(row.has_value());
        EXPECT_NE(row->ConstructorToken, 0u);
        std::uint32_t table = row->ConstructorToken >> 24;
        EXPECT_TRUE(table == 0x06 || table == 0x0A) << "unexpected ctor table";
        ASSERT_TRUE(row->ValueBlob.has_value());
        lengths.push_back(row->ValueBlob->size());
        // The blob starts with the II.23.3 custom-attribute prolog.
        EXPECT_GE(row->ValueBlob->size(), 2u);
        EXPECT_EQ((*row->ValueBlob)[0], 0x01);
        EXPECT_EQ((*row->ValueBlob)[1], 0x00);
    }
    EXPECT_EQ(Join(lengths, ','), "10,5,4");

    // An out-of-table token and a non-CustomAttribute token both read as
    // nullopt (the never-throw row convention).
    EXPECT_FALSE(f.GetCustomAttribute(0x0C000000u).has_value());
    EXPECT_FALSE(f.GetCustomAttribute(stringType).has_value());
}

TEST(SortByNameProcessorTest, AttributeTokensOfMethodModuleAndAssemblyParents)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    // The System.String::Join overloads: the first carries one attribute,
    // the rest two (ComVisibleAttribute + __DynamicallyInvokable, verified
    // against SRM over the same file).
    std::uint32_t join = 0;
    for (const auto& m : f.GetMethods(stringType)) {
        if (m.Name == "Join" && f.GetCustomAttributeTokens(m.Token).size() == 2) {
            join = m.Token;
            break;
        }
    }
    ASSERT_NE(join, 0u) << "a String::Join overload must carry two attributes";
    auto methodTokens = f.GetCustomAttributeTokens(join);
    ASSERT_EQ(methodTokens.size(), 2u);
    std::vector<std::string> keys;
    for (std::uint32_t t : methodTokens) keys.push_back(AttributeKey(f, t));
    std::sort(keys.begin(), keys.end());  // ordinal, for the set comparison
    EXPECT_EQ(Join(keys), "System.Runtime.InteropServices.ComVisibleAttribute|"
        "__DynamicallyInvokableAttribute");

    // The module row (0x00000001) and the assembly row (0x20000001) are
    // HasCustomAttribute parents too (the C# EntityHandle.ModuleDefinition /
    // AssemblyDefinition collections).
    EXPECT_EQ(f.GetCustomAttributeTokens(0x00000001u).size(), 1u);
    EXPECT_EQ(f.GetCustomAttributeTokens(0x20000001u).size(), 36u);
    // A parent with no attributes reads as an empty collection.
    EXPECT_TRUE(f.GetCustomAttributeTokens(0x23000001u).empty());
}

TEST(SortByNameProcessorTest, AttributeSortKeySortsLikeTheDotnetOrderBy)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, f.GetCustomAttributeTokens(stringType),
        DA::ProcessedEntityKind::CustomAttribute);
    ASSERT_EQ(sorted.size(), 3u);
    // The linguistic comparison sorts the underscore-prefixed
    // __DynamicallyInvokableAttribute FIRST (ordinal would put it last).
    EXPECT_EQ(Join(SortedKeys(
            f, sorted, DA::ProcessedEntityKind::CustomAttribute)),
        "__DynamicallyInvokableAttribute|"
        "System.Reflection.DefaultMemberAttribute|"
        "System.Runtime.InteropServices.ComVisibleAttribute");
}

TEST(SortByNameProcessorTest, AttributeSortIsStableAcrossEqualKeys)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // The mscorlib assembly row carries nine InternalsVisibleToAttribute
    // rows -- nine equal sort keys. The C# OrderBy is a stable sort, so the
    // nine tokens must keep their table order.
    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, f.GetCustomAttributeTokens(0x20000001u),
        DA::ProcessedEntityKind::CustomAttribute);
    ASSERT_EQ(sorted.size(), 36u);

    std::vector<std::uint32_t> ivt;
    for (std::uint32_t t : sorted) {
        if (AttributeKey(f, t) ==
            "System.Runtime.CompilerServices.InternalsVisibleToAttribute") {
            ivt.push_back(t);
        }
    }
    ASSERT_EQ(ivt.size(), 9u);
    EXPECT_TRUE(std::is_sorted(ivt.begin(), ivt.end()))
        << "the nine equal-key attribute tokens must keep their row order";
    ExpectNonDecreasing(SortedKeys(
        f, sorted, DA::ProcessedEntityKind::CustomAttribute));
}

TEST(SortByNameProcessorTest, InterfaceImplementationSortKeyIsTheInterfaceTypeName)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    // System.String's seven InterfaceImpl rows: four plain TypeDef
    // interfaces and three TypeSpec generic instantiations (the
    // IComparable<string> / IEnumerable<char> / IEquatable<string> rows).
    auto impls = f.GetInterfaceImplementations(stringType);
    ASSERT_EQ(impls.size(), 7u);
    std::size_t typeSpecs = 0;
    std::vector<std::uint32_t> tokens;
    for (const auto& impl : impls) {
        auto row = f.GetInterfaceImplementation(impl.Token);
        ASSERT_TRUE(row.has_value());
        EXPECT_EQ(row->Token, impl.Token);
        EXPECT_EQ(row->InterfaceToken, impl.InterfaceToken);
        tokens.push_back(impl.Token);
        if ((impl.InterfaceToken >> 24) == 0x1B) typeSpecs++;
    }
    EXPECT_EQ(typeSpecs, 3u);
    // A bogus token reads as nullopt (the never-throw row convention).
    EXPECT_FALSE(f.GetInterfaceImplementation(0x09000000u).has_value());

    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, tokens,
        DA::ProcessedEntityKind::InterfaceImplementation);
    ASSERT_EQ(sorted.size(), 7u);
    EXPECT_EQ(Join(SortedKeys(
            f, sorted, DA::ProcessedEntityKind::InterfaceImplementation)),
        "System.Collections.Generic.IEnumerable`1|"
        "System.Collections.IEnumerable|System.ICloneable|"
        "System.IComparable|System.IComparable`1|System.IConvertible|"
        "System.IEquatable`1");
}

TEST(SortByNameProcessorTest, FieldPropertyAndEventSortKeysAreTheNames)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t appDomain = FindTypeDefTokenIn(f, "System", "AppDomain");
    ASSERT_NE(stringType, 0u);
    ASSERT_NE(appDomain, 0u);

    DA::SortByNameProcessor processor;

    // Fields: names, sorted linguistically (m_-prefixed fields first).
    std::vector<std::uint32_t> fieldTokens;
    for (const auto& fd : f.GetFields(stringType)) fieldTokens.push_back(fd.Token);
    ASSERT_EQ(fieldTokens.size(), 8u);
    auto sortedFields = processor.Process(f, fieldTokens,
        DA::ProcessedEntityKind::FieldDefinition);
    EXPECT_EQ(Join(SortedKeys(
            f, sortedFields, DA::ProcessedEntityKind::FieldDefinition)),
        "alignConst|charPtrAlignConst|Empty|m_firstChar|m_stringLength|"
        "TrimBoth|TrimHead|TrimTail");
    // The per-token name read agrees with GetFields.
    EXPECT_EQ(f.GetFieldName(FindFieldIn(f, stringType, "m_firstChar")),
        "m_firstChar");

    // Properties: names.
    std::vector<std::uint32_t> propTokens;
    for (const auto& p : f.GetProperties(stringType)) propTokens.push_back(p.Token);
    ASSERT_EQ(propTokens.size(), 3u);
    auto sortedProps = processor.Process(f, propTokens,
        DA::ProcessedEntityKind::PropertyDefinition);
    EXPECT_EQ(Join(SortedKeys(
            f, sortedProps, DA::ProcessedEntityKind::PropertyDefinition)),
        "Chars|FirstChar|Length");

    // Events: names (System.AppDomain carries nine).
    std::vector<std::uint32_t> eventTokens;
    for (const auto& e : f.GetEvents(appDomain)) eventTokens.push_back(e.Token);
    ASSERT_EQ(eventTokens.size(), 9u);
    auto sortedEvents = processor.Process(f, eventTokens,
        DA::ProcessedEntityKind::EventDefinition);
    EXPECT_EQ(Join(SortedKeys(
            f, sortedEvents, DA::ProcessedEntityKind::EventDefinition)),
        "AssemblyLoad|AssemblyResolve|DomainUnload|FirstChanceException|"
        "ProcessExit|ReflectionOnlyAssemblyResolve|ResourceResolve|"
        "TypeResolve|UnhandledException");
    // The per-token reads behind the keys.
    EXPECT_EQ(f.GetPropertyName(propTokens[0]), "FirstChar");
    EXPECT_EQ(f.GetEventName(eventTokens[0]), "AssemblyLoad");
    EXPECT_EQ(f.GetEventName(0x14000000u), "");
    EXPECT_EQ(f.GetPropertyName(0x17000000u), "");
}

TEST(SortByNameProcessorTest, AppDomainPropertiesSortByName)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t appDomain = FindTypeDefTokenIn(f, "System", "AppDomain");
    ASSERT_NE(appDomain, 0u);

    std::vector<std::uint32_t> propTokens;
    for (const auto& p : f.GetProperties(appDomain)) propTokens.push_back(p.Token);
    ASSERT_EQ(propTokens.size(), 32u);
    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, propTokens,
        DA::ProcessedEntityKind::PropertyDefinition);
    EXPECT_EQ(Join(SortedKeys(
            f, sorted, DA::ProcessedEntityKind::PropertyDefinition)),
        "ActivationContext|ApplicationIdentity|ApplicationTrust|"
        "BaseDirectory|CurrentDomain|DomainManager|DynamicDirectory|"
        "Evidence|EvidenceNoDemand|Flags|FriendlyName|FusionStore|"
        "HostSecurityManager|Id|InternalEvidence|IsAppXNGen|IsFullyTrusted|"
        "IsHomogenous|IsLegacyCasPolicyEnabled|LocalStore|MonitoringIsEnabled|"
        "MonitoringSurvivedMemorySize|MonitoringSurvivedProcessMemorySize|"
        "MonitoringTotalAllocatedMemorySize|MonitoringTotalProcessorTime|"
        "PartialTrustVisibleAssemblies|PermissionSet|ProfileAPICheck|"
        "RelativeSearchPath|RemotingData|SetupInformation|ShadowCopyFiles");
}

TEST(SortByNameProcessorTest, MethodSortKeyPinsNameArityAndParameterList)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    ASSERT_NE(stringType, 0u);
    ASSERT_NE(arrayType, 0u);

    // The C# GetSortKey(MethodDefinitionHandle): the method name, the
    // backtick-arity for generic methods, then the parameter list at the
    // disassembler signature syntax.
    EXPECT_EQ(DA::SortByNameProcessor::GetSortKey(f,
        FindMethodIn(f, stringType, "Copy"),
        DA::ProcessedEntityKind::MethodDefinition), "Copy(string)");
    EXPECT_EQ(DA::SortByNameProcessor::GetSortKey(f,
        FindMethodIn(f, stringType, "IsNullOrEmpty"),
        DA::ProcessedEntityKind::MethodDefinition), "IsNullOrEmpty(string)");
    EXPECT_EQ(DA::SortByNameProcessor::GetSortKey(f,
        FindMethodIn(f, stringType, "get_Chars"),
        DA::ProcessedEntityKind::MethodDefinition), "get_Chars(int32)");
    EXPECT_EQ(DA::SortByNameProcessor::GetSortKey(f,
        FindMethodIn(f, arrayType, "Empty"),
        DA::ProcessedEntityKind::MethodDefinition), "Empty`1()");
}

TEST(SortByNameProcessorTest, MethodSortOrdersTheWholeStringMethodSet)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    std::vector<std::uint32_t> methodTokens;
    for (const auto& m : f.GetMethods(stringType)) methodTokens.push_back(m.Token);
    ASSERT_GT(methodTokens.size(), 20u);

    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, methodTokens,
        DA::ProcessedEntityKind::MethodDefinition);
    ASSERT_EQ(sorted.size(), methodTokens.size());
    auto keys = SortedKeys(f, sorted, DA::ProcessedEntityKind::MethodDefinition);
    ExpectNonDecreasing(keys);
    // ".ctor" (leading punctuation) sorts before every letter-prefixed name.
    EXPECT_EQ(keys.front().substr(0, 5), ".ctor");
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "Copy(string)") != keys.end());
}

TEST(SortByNameProcessorTest, TypeDefinitionSortKeyIsTheFullTypeName)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t object = FindTypeDefTokenIn(f, "System", "Object");
    std::uint32_t int32 = FindTypeDefTokenIn(f, "System", "Int32");
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(object, 0u);
    ASSERT_NE(int32, 0u);
    ASSERT_NE(stringType, 0u);

    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, { stringType, int32, object },
        DA::ProcessedEntityKind::TypeDefinition);
    EXPECT_EQ(JoinTokens(sorted), JoinTokens({ int32, object, stringType }));
    EXPECT_EQ(Join(SortedKeys(
            f, sorted, DA::ProcessedEntityKind::TypeDefinition)),
        "System.Int32|System.Object|System.String");
}

TEST(SortByNameProcessorTest, EmptyCollectionStaysEmpty)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    DA::SortByNameProcessor processor;
    EXPECT_TRUE(processor.Process(f, {},
        DA::ProcessedEntityKind::MethodDefinition).empty());
    // A single element is returned unchanged.
    auto one = processor.Process(f, { 0x02000001u },
        DA::ProcessedEntityKind::TypeDefinition);
    EXPECT_EQ(JoinTokens(one), "02000001");
}

TEST(SortByNameProcessorTest, ReflectionDisassemblerProcessGatesOnEntityProcessor)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);

    // Without an EntityProcessor the C# Process overloads return the items
    // unchanged (EntityProcessor?.Process(...) ?? items).
    std::vector<std::uint32_t> tokens = f.GetCustomAttributeTokens(stringType);
    auto unchanged = rd.Process(f, tokens, DA::ProcessedEntityKind::CustomAttribute);
    EXPECT_EQ(unchanged, tokens);

    // With the SortByNameProcessor set, the order flips: the underscore-keyed
    // attribute moves to the front.
    DA::SortByNameProcessor processor;
    rd.EntityProcessor(&processor);
    EXPECT_EQ(rd.EntityProcessor(), &processor);
    auto sorted = rd.Process(f, tokens, DA::ProcessedEntityKind::CustomAttribute);
    EXPECT_EQ(sorted.size(), tokens.size());
    EXPECT_NE(sorted, tokens);
    EXPECT_EQ(AttributeKey(f, sorted[0]), "__DynamicallyInvokableAttribute");

    // Unsetting restores the passthrough.
    rd.EntityProcessor(nullptr);
    EXPECT_EQ(rd.Process(f, tokens, DA::ProcessedEntityKind::CustomAttribute),
        tokens);
}

TEST(GetDeclaringTypeTest, MethodAndFieldArmsResolveTheOwningTypeDef)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    std::uint32_t firstChar = FindFieldIn(f, stringType, "m_firstChar");
    ASSERT_NE(copy, 0u);
    ASSERT_NE(firstChar, 0u);

    EXPECT_EQ(MD::GetDeclaringType(f, copy), stringType);
    EXPECT_EQ(MD::GetDeclaringType(f, firstChar), stringType);
}

TEST(GetDeclaringTypeTest, TypeDefinitionArmResolvesNesting)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Find any nested TypeDef (a row with a declaring type).
    std::uint32_t nested = 0, parent = 0;
    for (const auto& t : f.TypeDefs()) {
        auto info = f.GetTypeDefNameInfo(t.Token);
        if (info && info->DeclaringTypeToken != 0) {
            nested = t.Token;
            parent = info->DeclaringTypeToken;
            break;
        }
    }
    ASSERT_NE(nested, 0u) << "mscorlib must carry nested types";
    EXPECT_EQ(MD::GetDeclaringType(f, nested), parent);
    // A top-level type's declaring type is the nil handle (0).
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    EXPECT_EQ(MD::GetDeclaringType(f, stringType), 0u);
}

TEST(GetDeclaringTypeTest, TypeReferenceArmResolvesTheDeclaringTypeRef)
{
    if (!std::filesystem::exists(SystemDllPath())) GTEST_SKIP()
        << "fixture not present";
    MD::MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    // A nested TypeRef (a resolution scope that is another TypeRef -- the
    // cross-module nested-type reference shape; mscorlib itself carries
    // none, System.dll carries eleven).
    std::uint32_t nested = 0, parent = 0;
    for (std::uint32_t row = 1; row <= f.TypeRefCount(); row++) {
        std::uint32_t token = (0x01u << 24) | row;
        auto info = f.GetTypeRefNameInfo(token);
        if (info && info->DeclaringTypeRefToken != 0) {
            nested = token;
            parent = info->DeclaringTypeRefToken;
            break;
        }
    }
    ASSERT_NE(nested, 0u) << "System.dll must carry nested TypeRefs";
    EXPECT_EQ(MD::GetDeclaringType(f, nested), parent);
    // A top-level TypeRef resolves to the nil handle.
    std::uint32_t topLevel = 0x01000001u;
    auto info = f.GetTypeRefNameInfo(topLevel);
    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->DeclaringTypeRefToken, 0u);
    EXPECT_EQ(MD::GetDeclaringType(f, topLevel), 0u);
}

TEST(GetDeclaringTypeTest, MemberReferenceArmReturnsTheParent)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // The first MemberRef with a type parent: the arm returns mr.Parent.
    for (const auto& mr : f.MemberRefs()) {
        if (mr.ParentToken != 0) {
            EXPECT_EQ(MD::GetDeclaringType(f, mr.Token), mr.ParentToken);
            return;
        }
    }
    FAIL() << "mscorlib must carry MemberRefs with parents";
}

TEST(GetDeclaringTypeTest, MethodSpecificationArmRecursesToTheMethod)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    for (const auto& ms : f.MethodSpecs()) {
        if (ms.MethodToken == 0) continue;
        EXPECT_EQ(MD::GetDeclaringType(f, ms.Token),
            MD::GetDeclaringType(f, ms.MethodToken));
        return;
    }
    FAIL() << "mscorlib must carry MethodSpecs";
}

TEST(GetDeclaringTypeTest, UnknownKindAndNilTokenThrow)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // An AssemblyRef is not a member the C# resolves (ArgumentOutOfRangeException
    // in the default arm; the port's deferred arms land there too).
    EXPECT_THROW((void)MD::GetDeclaringType(f, 0x23000001u), std::out_of_range);
    EXPECT_THROW((void)MD::GetDeclaringType(f, 0), std::invalid_argument);
}

TEST(GetFullTypeNameTypeSpecTest, GenericInstantiationResolvesToTheGenericHead)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    // System.String's three TypeSpec interface rows decode to the generic
    // definitions' full names (the FullTypeNameSignatureDecoder semantics:
    // the instantiation is shrunk to its generic head).
    std::vector<std::string> names;
    for (const auto& impl : f.GetInterfaceImplementations(stringType)) {
        if ((impl.InterfaceToken >> 24) == 0x1B) {
            names.push_back(MD::ToILNameString(
                MD::GetFullTypeName(f, impl.InterfaceToken)));
        }
    }
    ASSERT_EQ(names.size(), 3u);
    std::sort(names.begin(), names.end());
    EXPECT_EQ(Join(names), "System.Collections.Generic.IEnumerable`1|"
        "System.IComparable`1|System.IEquatable`1");
}

TEST(GetFullTypeNameTypeSpecTest, SyntheticBlobWalksTheFullShapeMatrix)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t int32 = FindTypeDefTokenIn(f, "System", "Int32");
    std::uint32_t listGeneric = FindTypeDefTokenIn(f,
        "System.Collections.Generic", "List`1");
    ASSERT_NE(stringType, 0u);
    ASSERT_NE(int32, 0u);
    if (listGeneric == 0) GTEST_SKIP() << "List`1 TypeDef not found";

    auto coded = [](std::uint32_t tag, std::uint32_t row) {
        return ((row << 2) | tag);
    };
    std::uint32_t stringRow = stringType & 0x00FFFFFFu;
    std::uint32_t listRow = listGeneric & 0x00FFFFFFu;

    // GENERICINST class List`1<string>: the head wins, the type arguments
    // are discarded (the C# GetGenericInstantiation returns genericType).
    {
        std::vector<std::uint8_t> blob = { 0x15, 0x12 };
        std::uint32_t codedIdx = coded(0, listRow);
        if (codedIdx < 0x80) {
            blob.push_back(static_cast<std::uint8_t>(codedIdx));
        } else {
            blob.push_back(static_cast<std::uint8_t>(0x80 | (codedIdx >> 8)));
            blob.push_back(static_cast<std::uint8_t>(codedIdx & 0xFF));
        }
        blob.push_back(0x01);  // one type argument
        blob.push_back(0x0E);  // ... of string
        EXPECT_EQ(MD::ToILNameString(MD::GetFullTypeNameFromSpecification(
            f, blob.data(), blob.size())),
            "System.Collections.Generic.List`1");
    }

    // SZARRAY string: stripped to the element type.
    {
        std::vector<std::uint8_t> blob = { 0x1D, 0x12 };
        std::uint32_t codedIdx = coded(0, stringRow);
        if (codedIdx < 0x80) {
            blob.push_back(static_cast<std::uint8_t>(codedIdx));
        } else {
            blob.push_back(static_cast<std::uint8_t>(0x80 | (codedIdx >> 8)));
            blob.push_back(static_cast<std::uint8_t>(codedIdx & 0xFF));
        }
        EXPECT_EQ(MD::ToILNameString(MD::GetFullTypeNameFromSpecification(
            f, blob.data(), blob.size())), "System.String");
    }

    // A primitive TypeSpec decodes to the known type's full name.
    {
        std::vector<std::uint8_t> blob = { 0x08 };  // ELEMENT_TYPE_I4
        EXPECT_EQ(MD::ToILNameString(MD::GetFullTypeNameFromSpecification(
            f, blob.data(), blob.size())), "System.Int32");
    }

    // A class TypeSpec (no GENERICINST wrapper) decodes directly.
    {
        std::vector<std::uint8_t> blob = { 0x12 };
        std::uint32_t codedIdx = coded(0, stringRow);
        if (codedIdx < 0x80) {
            blob.push_back(static_cast<std::uint8_t>(codedIdx));
        } else {
            blob.push_back(static_cast<std::uint8_t>(0x80 | (codedIdx >> 8)));
            blob.push_back(static_cast<std::uint8_t>(codedIdx & 0xFF));
        }
        EXPECT_EQ(MD::ToILNameString(MD::GetFullTypeNameFromSpecification(
            f, blob.data(), blob.size())), "System.String");
    }
}

TEST(GetFullTypeNameTypeSpecTest, SystemDotDllAttributesSortThroughMemberRefCtors)
{
    if (!std::filesystem::exists(SystemDllPath())) GTEST_SKIP()
        << "fixture not present";
    MD::MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());

    // Microsoft.Win32.IInternetSecurityManager carries three attributes whose
    // constructors are MemberRefs into mscorlib (the cross-module attribute
    // shape): the sort key goes ctor -> declaring TypeRef -> full type name.
    std::uint32_t type = FindTypeDefTokenIn(f, "Microsoft.Win32",
        "IInternetSecurityManager");
    ASSERT_NE(type, 0u);
    auto tokens = f.GetCustomAttributeTokens(type);
    ASSERT_EQ(tokens.size(), 3u);
    for (std::uint32_t t : tokens) {
        auto row = f.GetCustomAttribute(t);
        ASSERT_TRUE(row.has_value());
        EXPECT_EQ(row->ConstructorToken >> 24, 0x0Au)
            << "the fixture's ctors are MemberRefs";
    }
    DA::SortByNameProcessor processor;
    auto sorted = processor.Process(f, tokens,
        DA::ProcessedEntityKind::CustomAttribute);
    EXPECT_EQ(Join(SortedKeys(
            f, sorted, DA::ProcessedEntityKind::CustomAttribute)),
        "System.Runtime.InteropServices.ComVisibleAttribute|"
        "System.Runtime.InteropServices.GuidAttribute|"
        "System.Runtime.InteropServices.InterfaceTypeAttribute");
}

TEST(MethodSignatureGenericParameterCountTest, CapturesTheSignatureArity)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    ASSERT_NE(stringType, 0u);
    ASSERT_NE(arrayType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    std::uint32_t empty = FindMethodIn(f, arrayType, "Empty");
    ASSERT_NE(copy, 0u);
    ASSERT_NE(empty, 0u);

    // The decoder previously discarded the generic parameter count; the sort
    // key needs it (System.Array::Empty<T> is generic, String::Copy is not).
    // Provider and decoder stay alive with the signature's deferred writers
    // (the provider-outlives-writers contract).
    auto countOf = [&f](std::uint32_t methodToken) {
        auto blob = f.GetSignatureBlob(methodToken);
        OUT::PlainTextOutput output;
        DA::DisassemblerSignatureTypeProvider provider(f, output);
        MD::SignatureTypeProviderDecoder decoder(provider, f);
        MD::MetadataGenericContext context =
            MD::MetadataGenericContext::ForMethod(methodToken, f);
        auto signature = decoder.DecodeMethodSignature(
            blob->data(), blob->size(), context);
        return signature.GenericParameterCount;
    };
    EXPECT_EQ(countOf(copy), 0u);
    EXPECT_EQ(countOf(empty), 1u);
}
