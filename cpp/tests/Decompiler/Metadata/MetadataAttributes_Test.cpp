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

// Per-row attribute-flag reads (the ILAmbience prerequisite): the raw
// II.23.1 attribute-flags columns the C# MetadataReader surfaces as the
// System.Reflection enums, read by token from a real .NET assembly. The tests
// pin the column reads against known metadata facts (System.Object is a
// public class, System.String::Empty is public static readonly, a .ctor
// carries RTSpecialName, ...) and against the already-exposed
// TypeDefInfo::Flags / GetMethodDefKindInfo surfaces reading the same columns.

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Metadata;

// ECMA-335 II.23.1 flag values -- identical to the BCL enum values the
// Disassembler attribute enums carry (FieldAttributes II.23.1.5,
// MethodAttributes II.23.1.10, TypeAttributes II.23.1.15, plus the
// unnumbered II.23.1 property/event flag families).
namespace {
constexpr std::uint32_t kTypeVisibilityMask = 0x00000007;  // TypeAttributes
constexpr std::uint32_t kTypePublic = 0x00000001;
constexpr std::uint32_t kTypeInterface = 0x00000020;
constexpr std::uint32_t kTypeSealed = 0x00000100;
constexpr std::uint32_t kFieldAccessMask = 0x00000007;    // FieldAttributes
constexpr std::uint32_t kFieldPublic = 0x00000006;
constexpr std::uint32_t kFieldPrivate = 0x00000001;
constexpr std::uint32_t kFieldStatic = 0x00000010;
constexpr std::uint32_t kFieldInitOnly = 0x00000020;
constexpr std::uint32_t kMethodMemberAccessMask = 0x00000007;  // MethodAttributes
constexpr std::uint32_t kMethodPublic = 0x00000006;
constexpr std::uint32_t kMethodStatic = 0x00000010;
constexpr std::uint32_t kMethodVirtual = 0x00000040;
constexpr std::uint32_t kMethodHideBySig = 0x00000080;
constexpr std::uint32_t kMethodSpecialName = 0x00000800;
constexpr std::uint32_t kMethodRTSpecialName = 0x00001000;
constexpr std::uint32_t kPropertySpecialName = 0x00000800;  // PropertyAttributes
constexpr std::uint32_t kEventSpecialName = 0x00000800;     // EventAttributes

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindFieldToken(MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& fld : f.GetFields(typeToken)) {
        if (fld.Name == name) return fld.Token;
    }
    return 0;
}

std::uint32_t FindMethodToken(MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

std::uint32_t FindPropertyToken(MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& p : f.GetProperties(typeToken)) {
        if (p.Name == name) return p.Token;
    }
    return 0;
}

std::uint32_t FindEventToken(MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& e : f.GetEvents(typeToken)) {
        if (e.Name == name) return e.Token;
    }
    return 0;
}
}  // namespace

TEST(MetadataAttributes, TypeDefFlagsMatchExposedTableRow) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u) << "System.Object not found";

    // The per-token read must agree with the all-rows TypeDefs() surface
    // (both read the TypeDef flags column, II.23.1.15).
    std::uint32_t rowFlags = 0;
    bool foundRow = false;
    for (const auto& t : f.TypeDefs()) {
        if (t.Token == objectTok) { rowFlags = t.Flags; foundRow = true; break; }
    }
    ASSERT_TRUE(foundRow);
    EXPECT_EQ(f.GetTypeDefAttributes(objectTok), rowFlags);

    // System.Object is a public, non-interface class.
    std::uint32_t flags = f.GetTypeDefAttributes(objectTok);
    EXPECT_EQ(flags & kTypeVisibilityMask, kTypePublic);
    EXPECT_EQ(flags & kTypeInterface, 0u);
}

TEST(MetadataAttributes, TypeDefFlagsOfInterfaceAndSealedClass) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // System.Collections.IEnumerable is a public interface.
    auto enumTok = FindType(f, "System.Collections", "IEnumerable");
    ASSERT_NE(enumTok, 0u) << "System.Collections.IEnumerable not found";
    std::uint32_t iflags = f.GetTypeDefAttributes(enumTok);
    EXPECT_EQ(iflags & kTypeInterface, kTypeInterface);
    EXPECT_EQ(iflags & kTypeVisibilityMask, kTypePublic);

    // System.String is a public sealed class.
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    std::uint32_t sflags = f.GetTypeDefAttributes(stringTok);
    EXPECT_EQ(sflags & kTypeSealed, kTypeSealed);
    EXPECT_EQ(sflags & kTypeVisibilityMask, kTypePublic);
    EXPECT_EQ(sflags & kTypeInterface, 0u);
}

TEST(MetadataAttributes, FieldFlagsOfStringEmptyAndPrivateInstanceField) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);

    // String::Empty is public static readonly.
    auto emptyTok = FindFieldToken(f, stringTok, "Empty");
    ASSERT_NE(emptyTok, 0u) << "String::Empty not found";
    std::uint32_t emptyFlags = f.GetFieldAttributes(emptyTok);
    EXPECT_EQ(emptyFlags & kFieldAccessMask, kFieldPublic);
    EXPECT_EQ(emptyFlags & kFieldStatic, kFieldStatic);
    EXPECT_EQ(emptyFlags & kFieldInitOnly, kFieldInitOnly);

    // String::m_firstChar is a private instance field.
    auto firstCharTok = FindFieldToken(f, stringTok, "m_firstChar");
    ASSERT_NE(firstCharTok, 0u) << "String::m_firstChar not found";
    std::uint32_t fcFlags = f.GetFieldAttributes(firstCharTok);
    EXPECT_EQ(fcFlags & kFieldAccessMask, kFieldPrivate);
    EXPECT_EQ(fcFlags & kFieldStatic, 0u);
}

TEST(MetadataAttributes, MethodFlagsOfObjectMembers) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u);

    // Object::ToString is a public virtual hidebysig instance method.
    auto toStringTok = FindMethodToken(f, objectTok, "ToString");
    ASSERT_NE(toStringTok, 0u) << "Object::ToString not found";
    std::uint32_t tsFlags = f.GetMethodAttributes(toStringTok);
    EXPECT_EQ(tsFlags & kMethodMemberAccessMask, kMethodPublic);
    EXPECT_EQ(tsFlags & kMethodVirtual, kMethodVirtual);
    EXPECT_EQ(tsFlags & kMethodHideBySig, kMethodHideBySig);
    EXPECT_EQ(tsFlags & kMethodStatic, 0u);

    // A constructor carries SpecialName|RTSpecialName (the flag pair
    // GetMethodDefKindInfo gates IsConstructor on).
    auto ctorTok = FindMethodToken(f, objectTok, ".ctor");
    ASSERT_NE(ctorTok, 0u) << "Object::.ctor not found";
    std::uint32_t ctorFlags = f.GetMethodAttributes(ctorTok);
    EXPECT_EQ(ctorFlags & kMethodRTSpecialName, kMethodRTSpecialName);
    EXPECT_EQ(ctorFlags & kMethodSpecialName, kMethodSpecialName);
    auto kind = f.GetMethodDefKindInfo(ctorTok);
    EXPECT_TRUE(kind.IsConstructor);
}

TEST(MetadataAttributes, MethodFlagsOfStaticMethodAgreeWithKindInfo) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);

    // String::IsNullOrEmpty is public static.
    auto isneTok = FindMethodToken(f, stringTok, "IsNullOrEmpty");
    ASSERT_NE(isneTok, 0u) << "String::IsNullOrEmpty not found";
    std::uint32_t isneFlags = f.GetMethodAttributes(isneTok);
    EXPECT_EQ(isneFlags & kMethodStatic, kMethodStatic);
    EXPECT_EQ(isneFlags & kMethodMemberAccessMask, kMethodPublic);
    EXPECT_TRUE(f.GetMethodDefKindInfo(isneTok).IsStatic);

    // Cross-check the whole method table: the Static bit of the flags column
    // must agree with GetMethodDefKindInfo (both read MethodDef flags).
    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u);
    for (const auto& m : f.GetMethods(objectTok)) {
        std::uint32_t flags = f.GetMethodAttributes(m.Token);
        bool staticBit = (flags & kMethodStatic) != 0;
        EXPECT_EQ(staticBit, f.GetMethodDefKindInfo(m.Token).IsStatic)
            << "method " << m.Name << " static bit disagrees with kind info";
    }
}

TEST(MetadataAttributes, PropertyAndEventFlagsAndEventEnumeration) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    auto lengthTok = FindPropertyToken(f, stringTok, "Length");
    ASSERT_NE(lengthTok, 0u) << "String::Length not found";

    // mscorlib's property rows carry no flags: the C# compilers do not emit
    // SpecialName on properties. Verified against the BCL MetadataReader
    // (the exact API the C# ILAmbience reads through) over both the .NET
    // Framework mscorlib and Roslyn-compiled System.Private.CoreLib -- zero
    // non-zero property/event flags in either assembly. The read still pins
    // the column: a miswired read yields a non-flags value, not 0.
    EXPECT_EQ(f.GetPropertyAttributes(lengthTok), 0u);

    // System.AppDomain declares events (AssemblyLoad, DomainUnload, ...).
    // Every enumerated token comes from the Event table (0x14).
    auto appDomainTok = FindType(f, "System", "AppDomain");
    ASSERT_NE(appDomainTok, 0u) << "System.AppDomain not found";
    auto events = f.GetEvents(appDomainTok);
    ASSERT_FALSE(events.empty()) << "System.AppDomain declares no events";
    for (const auto& e : events) {
        EXPECT_EQ(e.Token >> 24, 0x14u) << "event " << e.Name << " token is not from table 0x14";
    }
    auto assemblyLoadTok = FindEventToken(f, appDomainTok, "AssemblyLoad");
    ASSERT_NE(assemblyLoadTok, 0u) << "AppDomain::AssemblyLoad not found";

    // Same verification as the property above: the event rows carry no flags.
    EXPECT_EQ(f.GetEventAttributes(assemblyLoadTok), 0u);
}

TEST(MetadataAttributes, OutOfRangeAndWrongTableTokensYieldZero) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // Null tokens.
    EXPECT_EQ(f.GetTypeDefAttributes(0), 0u);
    EXPECT_EQ(f.GetFieldAttributes(0), 0u);
    EXPECT_EQ(f.GetMethodAttributes(0), 0u);
    EXPECT_EQ(f.GetPropertyAttributes(0), 0u);
    EXPECT_EQ(f.GetEventAttributes(0), 0u);
    EXPECT_TRUE(f.GetEvents(0).empty());

    // Out-of-range rows (mscorlib's tables hold thousands of rows, not 16M).
    EXPECT_EQ(f.GetTypeDefAttributes(0x02000000u | 0x00FFFFFFu), 0u);
    EXPECT_EQ(f.GetFieldAttributes(0x04000000u | 0x00FFFFFFu), 0u);
    EXPECT_EQ(f.GetMethodAttributes(0x06000000u | 0x00FFFFFFu), 0u);
    EXPECT_EQ(f.GetPropertyAttributes(0x17000000u | 0x00FFFFFFu), 0u);
    EXPECT_EQ(f.GetEventAttributes(0x14000000u | 0x00FFFFFFu), 0u);
    EXPECT_TRUE(f.GetEvents(0x02000000u | 0x00FFFFFFu).empty());

    // Wrong-table tokens: a MethodDef token read through the Field surface,
    // and vice versa. The getters reject tokens from other tables outright.
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    auto isneTok = FindMethodToken(f, stringTok, "IsNullOrEmpty");
    ASSERT_NE(isneTok, 0u);
    EXPECT_EQ(f.GetFieldAttributes(isneTok), 0u);
    auto emptyTok = FindFieldToken(f, stringTok, "Empty");
    ASSERT_NE(emptyTok, 0u);
    EXPECT_EQ(f.GetMethodAttributes(emptyTok), 0u);
}

TEST(MetadataAttributes, InvalidFileYieldsZeroAndEmpty) {
    MetadataFile f("this-is-not-an-assembly.txt");
    ASSERT_FALSE(f.IsValid());
    EXPECT_EQ(f.GetTypeDefAttributes(0x02000001u), 0u);
    EXPECT_EQ(f.GetFieldAttributes(0x04000001u), 0u);
    EXPECT_EQ(f.GetMethodAttributes(0x06000001u), 0u);
    EXPECT_EQ(f.GetPropertyAttributes(0x17000001u), 0u);
    EXPECT_EQ(f.GetEventAttributes(0x14000001u), 0u);
    EXPECT_TRUE(f.GetEvents(0x02000001u).empty());
}
