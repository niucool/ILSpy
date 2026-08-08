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

// TypeKind-derivation tests. Checks that MetadataFile::TypeDefs() derives the
// Kind for canonical mscorlib types exactly as the C# engine does
// (MetadataTypeDefinition.cs + SRMExtensions): Object -> Class, Int32 -> Struct,
// Void -> Void, Enum -> Class, IEnumerable -> Interface, Delegate/
// MulticastDelegate -> Class, Action -> Delegate, an enum -> Enum.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace ILSpy::Decompiler::Metadata;
using ILSpy::Decompiler::TypeSystem::TypeKind;

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

static TypeKind KindOf(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Kind;
    }
    return TypeKind::Unknown;
}

TEST(TypeKindDerivation, CanonicalMscorlibTypes) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    EXPECT_EQ(KindOf(f, "System", "Object"), TypeKind::Class);
    EXPECT_EQ(KindOf(f, "System", "String"), TypeKind::Class);
    EXPECT_EQ(KindOf(f, "System", "Int32"), TypeKind::Struct);
    EXPECT_EQ(KindOf(f, "System", "Boolean"), TypeKind::Struct);
    EXPECT_EQ(KindOf(f, "System", "Void"), TypeKind::Void);
    EXPECT_EQ(KindOf(f, "System", "Enum"), TypeKind::Class);      // System.Enum itself is a class
    EXPECT_EQ(KindOf(f, "System", "ValueType"), TypeKind::Class); // System.ValueType is a class
    EXPECT_EQ(KindOf(f, "System.Collections", "IEnumerable"), TypeKind::Interface);
    EXPECT_EQ(KindOf(f, "System", "Delegate"), TypeKind::Class);
    EXPECT_EQ(KindOf(f, "System", "MulticastDelegate"), TypeKind::Class);
    EXPECT_EQ(KindOf(f, "System", "Action"), TypeKind::Delegate);
    // DayOfWeek is a canonical enum whose base is System.Enum.
    EXPECT_EQ(KindOf(f, "System", "DayOfWeek"), TypeKind::Enum);
}

TEST(TypeKindDerivation, KindDistributionIsSane) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int classes = 0, structs = 0, interfaces = 0, enums = 0, delegates = 0;
    for (const auto& t : f.TypeDefs()) {
        switch (t.Kind) {
            case TypeKind::Class: ++classes; break;
            case TypeKind::Struct: ++structs; break;
            case TypeKind::Interface: ++interfaces; break;
            case TypeKind::Enum: ++enums; break;
            case TypeKind::Delegate: ++delegates; break;
            default: break;
        }
    }
    // mscorlib has thousands of classes, hundreds of structs/enums, many
    // interfaces and delegates. Fail loudly if any bucket is empty.
    EXPECT_GT(classes, 1000);
    EXPECT_GT(structs, 100);
    EXPECT_GT(interfaces, 50);
    EXPECT_GT(enums, 50);
    EXPECT_GT(delegates, 50);
}
