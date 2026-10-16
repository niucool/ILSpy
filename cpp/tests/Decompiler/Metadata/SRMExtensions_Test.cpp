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

// Tests for the SRMExtensions GetFullTypeName reader family (cpp/Decompiler/
// Metadata/SRMExtensions.{hpp,cpp} -- the EntityHandle/TypeDefinitionHandle/
// TypeReferenceHandle readers the catch-type writer and member-name rendering
// consume) plus the two prerequisites it composes: ReflectionHelper.
// SplitTypeParameterCountFromReflectionName (the arity split) and
// FullTypeName::NestedType (the nesting builder). Fixture facts verified
// against the .NET Framework 4 mscorlib and the GAC System.dll with the BCL
// MetadataReader:
//   - System.Object is a top-level TypeDef "System.Object";
//   - List`1 / Dictionary`2 carry arity 1 / 2; List`1+Enumerator nests once;
//   - Microsoft.Win32.Win32Native+WIN32_FIND_DATA+<_cFileName>e__FixedBuffer
//     nests two levels (an mscorlib fixed-buffer struct);
//   - mscorlib has NO TypeRef rows (self-contained root assembly), so the
//     reference-reader tests run against System.dll, whose 595 TypeRefs
//     include the top-level "System.Object" and the nested
//     "System.Collections.Generic.Dictionary`2/Enumerator".

#include "Decompiler/Metadata/SRMExtensions.hpp"
#include <cstdlib>
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

using ILSpy::Decompiler::Metadata::FieldValueSizeDecoder;
using ILSpy::Decompiler::Metadata::GetFullTypeName;
using ILSpy::Decompiler::Metadata::GetFullTypeNameFromDefinition;
using ILSpy::Decompiler::Metadata::GetFullTypeNameFromReference;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::MetadataGenericContext;
using ILSpy::Decompiler::Metadata::SignatureTypeProviderDecoder;
using ILSpy::Decompiler::Metadata::ToILNameString;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::SplitTypeParameterCountFromReflectionName;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;

namespace {

// A top-level name from its parts (the ctor is explicit, so the braced form
// reads the three fields in declaration order: namespace, name, arity).
FullTypeName TopLevel(const char* ns, const char* name, int tpc = 0) {
    return FullTypeName(TopLevelTypeName(ns, name, tpc));
}

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
           "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

// Find a top-level TypeDef token by namespace + authored name (with arity).
std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

// Find a nested TypeDef token by its own name and its declaring type's name
// (the nested rows carry an empty namespace column, so the declaring name is
// the discriminator).
std::uint32_t FindNestedType(MetadataFile& f, std::string_view name,
                             std::string_view declaringName) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Name != name) continue;
        auto info = f.GetTypeDefNameInfo(t.Token);
        if (!info || info->DeclaringTypeToken == 0) continue;
        auto declaring = f.GetTypeDefNameInfo(info->DeclaringTypeToken);
        if (declaring && declaring->Name == declaringName) return t.Token;
    }
    return 0;
}

// Find a top-level TypeRef token by namespace + name (a row whose resolution
// scope is not another TypeRef).
std::uint32_t FindTypeRef(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (std::uint32_t row = 1; row <= f.TypeRefCount(); ++row) {
        std::uint32_t token = (0x01u << 24) | row;
        auto info = f.GetTypeRefNameInfo(token);
        if (!info || info->Name != name || info->Namespace != ns) continue;
        if (info->DeclaringTypeRefToken == 0) return token;
    }
    return 0;
}

// Find a nested TypeRef token by its own name and its declaring TypeRef's
// name (the resolution-scope walk).
std::uint32_t FindNestedTypeRef(MetadataFile& f, std::string_view name,
                                std::string_view declaringName) {
    for (std::uint32_t row = 1; row <= f.TypeRefCount(); ++row) {
        std::uint32_t token = (0x01u << 24) | row;
        auto info = f.GetTypeRefNameInfo(token);
        if (!info || info->Name != name || info->DeclaringTypeRefToken == 0) continue;
        auto declaring = f.GetTypeRefNameInfo(info->DeclaringTypeRefToken);
        if (declaring && declaring->Name == declaringName) return token;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// ReflectionHelper.SplitTypeParameterCountFromReflectionName (both overloads).
// ---------------------------------------------------------------------------

TEST(SplitTypeParameterCountTest, OneArgStripsTrailingArity)
{
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("List`1"), "List");
}

TEST(SplitTypeParameterCountTest, OneArgKeepsNameWithoutBacktick)
{
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Object"), "Object");
}

TEST(SplitTypeParameterCountTest, OneArgStripsAtLastBacktickUnconditionally)
{
    // The 1-arg overload never checks the digits: everything after the LAST
    // backtick goes, so "A`1`B" keeps the FIRST arity suffix.
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("A`1`B"), "A`1");
}

TEST(SplitTypeParameterCountTest, OneArgStripsNonNumericTail)
{
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Foo`bar"), "Foo");
}

TEST(SplitTypeParameterCountTest, OneArgStripsEmptyTail)
{
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Foo`"), "Foo");
}

TEST(SplitTypeParameterCountTest, TwoArgParsesTypeParameterCount)
{
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Dictionary`2", count), "Dictionary");
    EXPECT_EQ(count, 2);
}

TEST(SplitTypeParameterCountTest, TwoArgZeroCountWithoutBacktick)
{
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Object", count), "Object");
    EXPECT_EQ(count, 0);
}

TEST(SplitTypeParameterCountTest, TwoArgKeepsWholeNameOnNonNumericTail)
{
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Foo`bar", count), "Foo`bar");
    EXPECT_EQ(count, 0);
}

TEST(SplitTypeParameterCountTest, TwoArgKeepsWholeNameOnMultiBacktick)
{
    // Only the LAST backtick's tail is considered, and "B" does not parse.
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("A`1`B", count), "A`1`B");
    EXPECT_EQ(count, 0);
}

TEST(SplitTypeParameterCountTest, TwoArgKeepsWholeNameOnEmptyTail)
{
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Foo`", count), "Foo`");
    EXPECT_EQ(count, 0);
}

TEST(SplitTypeParameterCountTest, TwoArgParsesNegativeCount)
{
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Foo`-1", count), "Foo");
    EXPECT_EQ(count, -1);
}

TEST(SplitTypeParameterCountTest, TwoArgKeepsWholeNameOnOverflow)
{
    // int.TryParse fails on a count beyond int32; the C# out parameter is 0.
    int count = -99;
    EXPECT_EQ(SplitTypeParameterCountFromReflectionName("Foo`99999999999", count),
              "Foo`99999999999");
    EXPECT_EQ(count, 0);
}

// ---------------------------------------------------------------------------
// FullTypeName::NestedType (the nesting builder the readers compose).
// ---------------------------------------------------------------------------

TEST(FullTypeNameNestedTypeTest, MatchesCSharpDocExample)
{
    // The C# doc comment: new FullTypeName("NS.A+B").NestedType("C", 1)
    // returns new FullTypeName("NS.A+B+C`1").
    FullTypeName ft = FullTypeName("NS.A+B").NestedType("C", 1);
    EXPECT_EQ(ft.ReflectionName(), "NS.A+B+C`1");
}

TEST(FullTypeNameNestedTypeTest, AppendsToTopLevelName)
{
    FullTypeName ft = TopLevel("Ns", "T").NestedType("U", 2);
    EXPECT_EQ(ft.ReflectionName(), "Ns.T+U`2");
    EXPECT_TRUE(ft.IsNested());
    EXPECT_EQ(ft.NestingLevel(), 1);
}

TEST(FullTypeNameNestedTypeTest, ZeroCountRendersNoAritySuffix)
{
    FullTypeName ft = TopLevel("Ns", "T").NestedType("U", 0);
    EXPECT_EQ(ft.ReflectionName(), "Ns.T+U");
}

TEST(FullTypeNameNestedTypeTest, ChainsAcrossTwoLevels)
{
    FullTypeName ft = TopLevel("Ns", "A").NestedType("B", 1).NestedType("C", 2);
    EXPECT_EQ(ft.ReflectionName(), "Ns.A+B`1+C`2");
    EXPECT_EQ(ft.NestingLevel(), 2);
}

TEST(FullTypeNameNestedTypeTest, EqualsParsedReflectionName)
{
    FullTypeName built = TopLevel("System.Collections.Generic", "Dictionary", 2)
                             .NestedType("Enumerator", 0);
    FullTypeName parsed("System.Collections.Generic.Dictionary`2+Enumerator");
    EXPECT_TRUE(built == parsed);
    EXPECT_EQ(built.TypeParameterCount(), parsed.TypeParameterCount());
}

TEST(FullTypeNameNestedTypeTest, PerSegmentAdditionalCounts)
{
    FullTypeName ft = TopLevel("Ns", "A").NestedType("B", 1).NestedType("C", 2);
    EXPECT_EQ(ft.GetNestedTypeAdditionalTypeParameterCount(0), 1);
    EXPECT_EQ(ft.GetNestedTypeAdditionalTypeParameterCount(1), 2);
}

TEST(FullTypeNameNestedTypeTest, LeavesTheSourceNameUnchanged)
{
    FullTypeName source("NS.A+B");
    FullTypeName extended = source.NestedType("C", 1);
    EXPECT_EQ(source.ReflectionName(), "NS.A+B");
    EXPECT_EQ(source.NestingLevel(), 1);
    EXPECT_EQ(extended.ReflectionName(), "NS.A+B+C`1");
}

// ---------------------------------------------------------------------------
// SRMExtensions.GetFullTypeName (the readers + the EntityHandle dispatch).
// ---------------------------------------------------------------------------

TEST(GetFullTypeNameTest, DefinitionReaderResolvesTopLevelType)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t token = FindType(f, "System", "Object");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromDefinition(f, token);
    EXPECT_FALSE(ft.IsNested());
    EXPECT_TRUE(ft.GetTopLevelTypeName() == TopLevelTypeName("System", "Object"));
    EXPECT_EQ(ft.ReflectionName(), "System.Object");
}

TEST(GetFullTypeNameTest, DefinitionReaderSplitsGenericArity)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t token = FindType(f, "System.Collections.Generic", "List`1");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromDefinition(f, token);
    EXPECT_TRUE(ft.GetTopLevelTypeName() ==
                TopLevelTypeName("System.Collections.Generic", "List", 1));
    EXPECT_EQ(ft.ReflectionName(), "System.Collections.Generic.List`1");
    EXPECT_EQ(ft.TypeParameterCount(), 1);
}

TEST(GetFullTypeNameTest, DefinitionReaderSplitsTwoArity)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t token = FindType(f, "System.Collections.Generic", "Dictionary`2");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromDefinition(f, token);
    EXPECT_TRUE(ft.GetTopLevelTypeName() ==
                TopLevelTypeName("System.Collections.Generic", "Dictionary", 2));
    EXPECT_EQ(ft.TypeParameterCount(), 2);
}

TEST(GetFullTypeNameTest, DefinitionReaderResolvesNestedTypeThroughDeclaringChain)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t token = FindNestedType(f, "Enumerator", "List`1");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromDefinition(f, token);
    EXPECT_TRUE(ft.IsNested());
    EXPECT_EQ(ft.NestingLevel(), 1);
    EXPECT_TRUE(ft.GetTopLevelTypeName() ==
                TopLevelTypeName("System.Collections.Generic", "List", 1));
    EXPECT_EQ(ft.GetNestedTypeName(0), "Enumerator");
    EXPECT_EQ(ft.ReflectionName(), "System.Collections.Generic.List`1+Enumerator");
}

TEST(GetFullTypeNameTest, DefinitionReaderResolvesTwoLevelNesting)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t token = FindNestedType(f, "<_cFileName>e__FixedBuffer", "WIN32_FIND_DATA");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromDefinition(f, token);
    EXPECT_EQ(ft.NestingLevel(), 2);
    EXPECT_TRUE(ft.GetTopLevelTypeName() == TopLevelTypeName("Microsoft.Win32", "Win32Native"));
    EXPECT_EQ(ft.GetNestedTypeName(0), "WIN32_FIND_DATA");
    EXPECT_EQ(ft.GetNestedTypeName(1), "<_cFileName>e__FixedBuffer");
    EXPECT_EQ(ft.ReflectionName(),
              "Microsoft.Win32.Win32Native+WIN32_FIND_DATA+<_cFileName>e__FixedBuffer");
}

TEST(GetFullTypeNameTest, DefinitionReaderNilTokenThrows)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_THROW(GetFullTypeNameFromDefinition(f, 0), std::invalid_argument);
}

TEST(GetFullTypeNameTest, DefinitionReaderOutOfRangeTokenThrows)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_THROW(GetFullTypeNameFromDefinition(f, 0x02000000u | 0x00FFFFFFu), std::out_of_range);
}

TEST(GetFullTypeNameTest, DefinitionReaderWrongTableTokenThrows)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto methods = f.MethodDefs();
    ASSERT_FALSE(methods.empty());
    EXPECT_THROW(GetFullTypeNameFromDefinition(f, methods[0].Token), std::out_of_range);
}

TEST(GetFullTypeNameTest, ReferenceReaderResolvesTopLevelTypeReference)
{
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    ASSERT_GT(f.TypeRefCount(), 0u);
    std::uint32_t token = FindTypeRef(f, "System", "Object");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromReference(f, token);
    EXPECT_FALSE(ft.IsNested());
    EXPECT_TRUE(ft.GetTopLevelTypeName() == TopLevelTypeName("System", "Object"));
    EXPECT_EQ(ft.ReflectionName(), "System.Object");
}

TEST(GetFullTypeNameTest, ReferenceReaderResolvesNestedTypeReference)
{
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    ASSERT_GT(f.TypeRefCount(), 0u);
    std::uint32_t token = FindNestedTypeRef(f, "Enumerator", "Dictionary`2");
    ASSERT_NE(token, 0u);
    FullTypeName ft = GetFullTypeNameFromReference(f, token);
    EXPECT_TRUE(ft.IsNested());
    EXPECT_EQ(ft.NestingLevel(), 1);
    EXPECT_TRUE(ft.GetTopLevelTypeName() ==
                TopLevelTypeName("System.Collections.Generic", "Dictionary", 2));
    EXPECT_EQ(ft.GetNestedTypeName(0), "Enumerator");
    EXPECT_EQ(ft.ReflectionName(), "System.Collections.Generic.Dictionary`2+Enumerator");
}

TEST(GetFullTypeNameTest, ReferenceReaderNilTokenThrows)
{
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_THROW(GetFullTypeNameFromReference(f, 0), std::invalid_argument);
}

TEST(GetFullTypeNameTest, ReferenceReaderOutOfRangeTokenThrows)
{
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_THROW(GetFullTypeNameFromReference(f, 0x01000000u | 0x00FFFFFFu), std::out_of_range);
}

TEST(GetFullTypeNameTest, DispatchesTypeDefTokensToTheDefinitionReader)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t token = FindNestedType(f, "Enumerator", "Dictionary`2");
    ASSERT_NE(token, 0u);
    FullTypeName viaDispatch = GetFullTypeName(f, token);
    FullTypeName viaReader = GetFullTypeNameFromDefinition(f, token);
    EXPECT_TRUE(viaDispatch == viaReader);
    EXPECT_EQ(viaDispatch.ReflectionName(),
              "System.Collections.Generic.Dictionary`2+Enumerator");
}

TEST(GetFullTypeNameTest, DispatchesTypeRefTokensToTheReferenceReader)
{
    MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());
    ASSERT_GT(f.TypeRefCount(), 0u);
    std::uint32_t token = FindTypeRef(f, "System", "Object");
    ASSERT_NE(token, 0u);
    FullTypeName viaDispatch = GetFullTypeName(f, token);
    FullTypeName viaReader = GetFullTypeNameFromReference(f, token);
    EXPECT_TRUE(viaDispatch == viaReader);
    EXPECT_EQ(viaDispatch.ReflectionName(), "System.Object");
}

TEST(GetFullTypeNameTest, DispatchNilTokenThrows)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    EXPECT_THROW(GetFullTypeName(f, 0), std::invalid_argument);
}

TEST(GetFullTypeNameTest, DispatchWrongKindTokenThrows)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto methods = f.MethodDefs();
    ASSERT_FALSE(methods.empty());
    EXPECT_THROW(GetFullTypeName(f, methods[0].Token), std::out_of_range);
}

TEST(GetFullTypeNameTest, DispatchTypeSpecTokenDecodesTheSignatureBlob)
{
    // The TypeSpec arm decodes the row's signature blob through the
    // FullTypeNameSignatureDecoder semantics (a generic instantiation
    // shrinks to its generic head): System.String implements
    // IComparable<string> through a TypeSpec row, so the token resolves to
    // the System.IComparable`1 definition name.
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "String") {
            stringType = t.Token;
            break;
        }
    }
    ASSERT_NE(stringType, 0u);
    std::uint32_t typeSpec = 0;
    for (const auto& impl : f.GetInterfaceImplementations(stringType)) {
        if ((impl.InterfaceToken >> 24) == 0x1B) {
            typeSpec = impl.InterfaceToken;
            break;
        }
    }
    ASSERT_NE(typeSpec, 0u) << "String must implement an interface via TypeSpec";
    auto name = GetFullTypeName(f, typeSpec);
    // The instantiation's argument count must not double the arity (a bad
    // decode renders "System.IComparable`1`1").
    EXPECT_EQ(ToILNameString(name).find("`1`1"), std::string::npos);
    EXPECT_TRUE(ToILNameString(name) == "System.IComparable`1"
        || ToILNameString(name) == "System.Collections.Generic.IEnumerable`1"
        || ToILNameString(name) == "System.IEquatable`1")
        << "unexpected interface: " << ToILNameString(name);
}


// ---------------------------------------------------------------------------
// FieldValueSizeDecoder (the provider behind GetInitialValue) + the
// MetadataFile reads the ReflectionDisassembler field renderer composes
// (GetFieldRVA / GetFieldOffset / GetFieldMarshallingDescriptor /
// GetTypeLayoutSize / GetContainingSectionIndex / GetSectionName /
// GetFieldInitialValue). Fixture facts verified against the .NET Framework
// 4.8 Framework64 mscorlib with the BCL MetadataReader and PEReader:
//   - the '<PrivateImplementationDetails>' field
//     '001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB' has
//     RVA 0x4E8FF8, its signature decodes to the nested
//     '__StaticArrayInitTypeSize=40' struct (ClassLayout ClassSize 40), and
//     its initial value is the 40 bytes ff*8 0d 00 00 00 04 00 00 00 ff*12
//     0f 00 00 00 ff*4 0c 00 00 00;
//   - RVA 0x4E8FF8 lands in section 0 (.text, VA 0x2000); section 1 is
//     .rsrc (VA 0x4FA000);
//   - CLAIM_SECURITY_ATTRIBUTE_INFORMATION_V1::pAttributeV1 carries the
//     FieldLayout offset 0 and an IntPtr (native int) signature;
//   - CLAIM_SECURITY_ATTRIBUTE_FQBN_VALUE::Name carries the FieldMarshal
//     blob 0x15 (lpwstr) and a String signature.
// ---------------------------------------------------------------------------

TEST(FieldValueSizeDecoderTest, PrimitiveAndCompositeMatrix)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // One full type decoded through the size provider (the GetInitialValue
    // drive: the field signature's single type at the nil context).
    auto sizeOf = [&](std::vector<std::uint8_t> blob) {
        FieldValueSizeDecoder provider(f);
        SignatureTypeProviderDecoder<FieldValueSizeDecoder> decoder(provider, f);
        return decoder.DecodeType(blob.data(), blob.size(),
            MetadataGenericContext{});
    };

    // The C# primitive table: Boolean/Byte/SByte 1, Char/Int16/UInt16 2,
    // Int32/UInt32/Single 4, Int64/UInt64/Double 8, IntPtr/UIntPtr the
    // pointer size (8, the null-typeSystem IntPtr.Size shape); Void/String/
    // TypedReference/Object 0 (no inline initial value).
    EXPECT_EQ(sizeOf({0x02}), 1);   // bool
    EXPECT_EQ(sizeOf({0x04}), 1);  // int8
    EXPECT_EQ(sizeOf({0x05}), 1);  // unsigned int8
    EXPECT_EQ(sizeOf({0x03}), 2);   // char
    EXPECT_EQ(sizeOf({0x06}), 2);  // int16
    EXPECT_EQ(sizeOf({0x07}), 2);  // unsigned int16
    EXPECT_EQ(sizeOf({0x08}), 4);   // int32
    EXPECT_EQ(sizeOf({0x09}), 4);  // unsigned int32
    EXPECT_EQ(sizeOf({0x0C}), 4);  // float32
    EXPECT_EQ(sizeOf({0x0A}), 8);   // int64
    EXPECT_EQ(sizeOf({0x0B}), 8);  // unsigned int64
    EXPECT_EQ(sizeOf({0x0D}), 8);  // float64
    EXPECT_EQ(sizeOf({0x18}), 8);   // native int (IntPtr)
    EXPECT_EQ(sizeOf({0x19}), 8);  // unsigned native int
    EXPECT_EQ(sizeOf({0x01}), 0);   // void
    EXPECT_EQ(sizeOf({0x0E}), 0);   // string
    EXPECT_EQ(sizeOf({0x16}), 0);  // typedref
    EXPECT_EQ(sizeOf({0x1C}), 0);   // object

    // Ptr/ByRef/FnPtr read the pointer size; SZArray/Array read
    // GetPrimitiveType(Object) = 0; VAR/MVAR read 0.
    EXPECT_EQ(sizeOf({0x0F, 0x08}), 8);       // int32*
    EXPECT_EQ(sizeOf({0x0F, 0x1D, 0x08}), 8);  // int32[]*
    EXPECT_EQ(sizeOf({0x1D, 0x08}), 0);        // int32[]
    EXPECT_EQ(sizeOf({0x14, 0x08, 0x01, 0x01, 0x01, 0x00}), 0);  // int32[0...0...]
    EXPECT_EQ(sizeOf({0x13, 0x00}), 0);        // !0
    EXPECT_EQ(sizeOf({0x1E, 0x00}), 0);        // !!0
    // void() function pointer: FNPTR + header + count 0 + void return.
    EXPECT_EQ(sizeOf({0x1B, 0x00, 0x00, 0x01}), 8);

    // pinned/cmod fold to the unmodified/element size (GetPinnedType =>
    // element; GetModifiedType => unmodified).
    EXPECT_EQ(sizeOf({0x45, 0x08}), 4);  // pinned int32
}

TEST(FieldValueSizeDecoderTest, RealFieldSignatureShapes)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t pic = 0, win32 = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "" && t.Name == "<PrivateImplementationDetails>")
            pic = t.Token;
        if (t.Namespace == "Microsoft.Win32" && t.Name == "Win32Native")
            win32 = t.Token;
    }
    ASSERT_NE(pic, 0u);
    ASSERT_NE(win32, 0u);
    std::uint32_t information = FindNestedType(f,
        "CLAIM_SECURITY_ATTRIBUTE_INFORMATION_V1", "Win32Native");
    std::uint32_t fqbnType = FindNestedType(f,
        "CLAIM_SECURITY_ATTRIBUTE_FQBN_VALUE", "Win32Native");
    ASSERT_NE(information, 0u);
    ASSERT_NE(fqbnType, 0u);

    std::uint32_t dataField = 0, nativeIntField = 0, stringField = 0;
    for (const auto& fd : f.GetFields(pic)) {
        if (fd.Name == "001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB")
            dataField = fd.Token;
    }
    for (const auto& fd : f.GetFields(information)) {
        if (fd.Name == "pAttributeV1") nativeIntField = fd.Token;
    }
    for (const auto& fd : f.GetFields(fqbnType)) {
        if (fd.Name == "Name") stringField = fd.Token;
    }
    ASSERT_NE(dataField, 0u);
    ASSERT_NE(nativeIntField, 0u);
    ASSERT_NE(stringField, 0u);

    // The GetInitialValue drive: the field signature's kind nibble then one
    // full type at the nil context.
    auto fieldSize = [&](std::uint32_t fieldToken) {
        auto blob = f.GetSignatureBlob(fieldToken);
        EXPECT_TRUE(blob.has_value());
        FieldValueSizeDecoder provider(f);
        SignatureTypeProviderDecoder<FieldValueSizeDecoder> decoder(provider, f);
        return decoder.DecodeType(blob->data() + 1, blob->size() - 1,
            MetadataGenericContext{});
    };

    // The data field's valuetype '__StaticArrayInitTypeSize=40' reads the
    // ClassLayout ClassSize; the native int reads the pointer size (8); the
    // string reads 0.
    EXPECT_EQ(fieldSize(dataField), 40);
    EXPECT_EQ(fieldSize(nativeIntField), 8);
    EXPECT_EQ(fieldSize(stringField), 0);
}

TEST(FieldValueSizeDecoderTest, FieldRvaLayoutMarshalClassLayoutSectionReads)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t pic = 0, win32 = 0, stringType = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "" && t.Name == "<PrivateImplementationDetails>")
            pic = t.Token;
        if (t.Namespace == "Microsoft.Win32" && t.Name == "Win32Native")
            win32 = t.Token;
        if (t.Namespace == "System" && t.Name == "String")
            stringType = t.Token;
    }
    ASSERT_NE(pic, 0u);
    ASSERT_NE(win32, 0u);
    ASSERT_NE(stringType, 0u);
    std::uint32_t information = FindNestedType(f,
        "CLAIM_SECURITY_ATTRIBUTE_INFORMATION_V1", "Win32Native");
    std::uint32_t fqbnType = FindNestedType(f,
        "CLAIM_SECURITY_ATTRIBUTE_FQBN_VALUE", "Win32Native");
    std::uint32_t staticArray40 = FindNestedType(f,
        "__StaticArrayInitTypeSize=40", "<PrivateImplementationDetails>");
    ASSERT_NE(information, 0u);
    ASSERT_NE(fqbnType, 0u);
    ASSERT_NE(staticArray40, 0u);

    std::uint32_t dataField = 0, emptyField = 0, nativeIntField = 0,
        nameField = 0;
    for (const auto& fd : f.GetFields(pic)) {
        if (fd.Name == "001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB")
            dataField = fd.Token;
    }
    for (const auto& fd : f.GetFields(stringType)) {
        if (fd.Name == "Empty") emptyField = fd.Token;
    }
    for (const auto& fd : f.GetFields(information)) {
        if (fd.Name == "pAttributeV1") nativeIntField = fd.Token;
    }
    for (const auto& fd : f.GetFields(fqbnType)) {
        if (fd.Name == "Name") nameField = fd.Token;
    }
    ASSERT_NE(dataField, 0u);
    ASSERT_NE(emptyField, 0u);
    ASSERT_NE(nativeIntField, 0u);
    ASSERT_NE(nameField, 0u);

    // GetFieldRVA: the FieldRVA table row's RVA; 0 without one.
    EXPECT_EQ(f.GetFieldRVA(dataField), 0x4E8FF8u);
    EXPECT_EQ(f.GetFieldRVA(emptyField), 0u);
    // GetFieldOffset: the FieldLayout table row's Offset; -1 without one.
    EXPECT_EQ(f.GetFieldOffset(nativeIntField), 0);
    EXPECT_EQ(f.GetFieldOffset(emptyField), -1);
    // GetFieldMarshallingDescriptor: the FieldMarshal blob (0x15 = lpwstr).
    auto marshal = f.GetFieldMarshallingDescriptor(nameField);
    ASSERT_TRUE(marshal.has_value());
    EXPECT_EQ(*marshal, (std::vector<std::uint8_t>{0x15}));
    EXPECT_FALSE(f.GetFieldMarshallingDescriptor(emptyField).has_value());
    // GetTypeLayoutSize: the ClassLayout ClassSize; 0 without a row.
    EXPECT_EQ(f.GetTypeLayoutSize(staticArray40), 40u);
    EXPECT_EQ(f.GetTypeLayoutSize(stringType), 0u);

    // The PE-section reads: .text is section 0 (VA 0x2000), .rsrc section 1
    // (VA 0x4FA000); an RVA past every section reads -1.
    EXPECT_EQ(f.GetContainingSectionIndex(0x4E8FF8), 0);
    EXPECT_EQ(f.GetSectionName(0), ".text");
    EXPECT_EQ(f.GetSectionName(1), ".rsrc");
    EXPECT_EQ(f.GetContainingSectionIndex(0x7FFFFFFF), -1);
    EXPECT_EQ(f.GetSectionName(99), "");
}

TEST(FieldValueSizeDecoderTest, GetFieldInitialValueDataFieldFortyBytes)
{
    MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t pic = 0, stringType = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "" && t.Name == "<PrivateImplementationDetails>")
            pic = t.Token;
        if (t.Namespace == "System" && t.Name == "String")
            stringType = t.Token;
    }
    ASSERT_NE(pic, 0u);
    ASSERT_NE(stringType, 0u);
    std::uint32_t dataField = 0, emptyField = 0;
    for (const auto& fd : f.GetFields(pic)) {
        if (fd.Name == "001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB")
            dataField = fd.Token;
    }
    for (const auto& fd : f.GetFields(stringType)) {
        if (fd.Name == "Empty") emptyField = fd.Token;
    }
    ASSERT_NE(dataField, 0u);
    ASSERT_NE(emptyField, 0u);

    // The 40 bytes at the RVA (the ClassLayout size of the field's
    // '__StaticArrayInitTypeSize=40' valuetype, sliced out of .text).
    std::vector<std::uint8_t> expected = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x0d, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0x0c, 0x00, 0x00, 0x00,
    };
    EXPECT_EQ(f.GetFieldInitialValue(dataField), expected);
    // A field without the HasFieldRVA flag reads empty.
    EXPECT_EQ(f.GetFieldInitialValue(emptyField),
        (std::vector<std::uint8_t>{}));
}
