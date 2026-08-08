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

// Custom-attribute tests. Reads attributes from a real .NET assembly through
// MetadataFile::GetCustomAttributes and checks the relationships the type system
// and back end depend on: [Serializable] on System.String, [AttributeUsage] on
// the attribute classes (e.g. System.ObsoleteAttribute), and that attribute
// lookup degrades gracefully for an out-of-range token. Constructor/named-arg
// decoding is deferred; this slice exposes only the attribute type name.

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Metadata;

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

static std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

static bool HasAttribute(const std::vector<CustomAttributeInfo>& attrs,
                         std::string_view ns, std::string_view name) {
    return std::find_if(attrs.begin(), attrs.end(), [&](const CustomAttributeInfo& a) {
               return a.Namespace == ns && a.Name == name;
           }) != attrs.end();
}

TEST(CustomAttributes, SerializableFlagOnString) {
    // [Serializable] is a pseudo-attribute: the compiler sets the tdSerializable
    // flag (0x2000, ECMA-335 II.23.1.15; matches System.Reflection.TypeAttributes.Serializable)
    // in the TypeDef Flags rather than emitting a System.SerializableAttribute
    // CustomAttribute row. So the type system reads it from the flags, not from
    // GetCustomAttributes.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    auto tok = FindType(f, "System", "String");
    ASSERT_NE(tok, 0u);
    std::uint32_t flags = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Token == tok) { flags = t.Flags; break; }
    }
    EXPECT_TRUE(flags & 0x2000u) << "System.String should have the tdSerializable flag (0x2000)";
}

TEST(CustomAttributes, AttributeUsageOnObsoleteAttribute) {
    // System.ObsoleteAttribute is itself decorated with [AttributeUsage], which
    // the decompiler's back end reads to emit the allowed-targets list.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    auto tok = FindType(f, "System", "ObsoleteAttribute");
    ASSERT_NE(tok, 0u);
    auto attrs = f.GetCustomAttributes(tok);
    ASSERT_FALSE(attrs.empty());
    EXPECT_TRUE(HasAttribute(attrs, "System", "AttributeUsageAttribute"))
        << "System.ObsoleteAttribute should carry [AttributeUsage]";
}

TEST(CustomAttributes, ManyTypesHaveAttributes) {
    // Sanity: across the whole assembly, real CustomAttribute rows are present
    // on many types, and [AttributeUsage] (a real attribute) appears. [Serializable]
    // is a pseudo-attribute (flag), checked separately above, so it is not expected
    // in the CustomAttribute rows.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int withAttrs = 0;
    bool sawAttributeUsage = false;
    int sampled = 0;
    for (const auto& t : f.TypeDefs()) {
        auto attrs = f.GetCustomAttributes(t.Token);
        if (!attrs.empty()) ++withAttrs;
        for (const auto& a : attrs) {
            if (a.Namespace == "System" && a.Name == "AttributeUsageAttribute") sawAttributeUsage = true;
        }
        if (++sampled > 2000) break;
    }
    EXPECT_GT(withAttrs, 100) << "too few types with attributes";
    EXPECT_TRUE(sawAttributeUsage);
    // [Extension] is on methods, not types -- check System.Linq.Enumerable.
    auto enumerableTok = FindType(f, "System.Linq", "Enumerable");
    if (enumerableTok != 0) {
        bool foundExtension = false;
        for (const auto& m : f.GetMethods(enumerableTok)) {
            auto ma = f.GetCustomAttributes(m.Token);
            if (HasAttribute(ma, "System", "ExtensionAttribute")) { foundExtension = true; break; }
        }
        EXPECT_TRUE(foundExtension) << "no [Extension] method on System.Linq.Enumerable";
    }
}

TEST(CustomAttributes, InvalidTokenIsGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    EXPECT_TRUE(f.GetCustomAttributes(0x02000000u).empty());  // row 0
    EXPECT_TRUE(f.GetCustomAttributes(0x02FFFFFFu).empty());  // huge row
    EXPECT_TRUE(f.GetCustomAttributes(0x99000000u).empty());  // unknown table
}
