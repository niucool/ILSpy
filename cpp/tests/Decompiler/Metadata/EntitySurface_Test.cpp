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

// Metadata entity-surface tests. Walks the TypeDef table and the member lists
// (methods/fields/properties) of a real .NET assembly through MetadataFile, and
// checks the relationships the type system depends on: System.Object has no base,
// System.String derives from System.Object, Object's methods include Equals/
// ToString/GetType, and String's character fields decode to System.Char. This
// is the Phase 2 MetadataModule bridge.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Metadata;
using ILSpy::Decompiler::TypeSystem::ITypePtr;

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

TEST(EntitySurface, TypeDefsAndBaseTypes) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    auto types = f.TypeDefs();
    ASSERT_FALSE(types.empty());

    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u) << "System.Object not found";
    // System.Object has no base type.
    for (const auto& t : types) {
        if (t.Token == objectTok) { EXPECT_EQ(t.BaseType, nullptr); break; }
    }

    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    for (const auto& t : types) {
        if (t.Token == stringTok) {
            ASSERT_NE(t.BaseType, nullptr);
            EXPECT_EQ(t.BaseType->ReflectionName(), "System.Object");
            break;
        }
    }
}

TEST(EntitySurface, ObjectMethodListContainsCoreMethods) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u);

    auto methods = f.GetMethods(objectTok);
    ASSERT_FALSE(methods.empty());
    std::vector<std::string> names;
    for (const auto& m : methods) names.push_back(m.Name);
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Equals") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "ToString") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "GetType") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "MemberwiseClone") != names.end());
    // Every method token should decode a signature without throwing.
    for (const auto& m : methods) {
        auto sig = f.GetMethodSignature(m.Token);
        EXPECT_TRUE(sig.has_value()) << "method " << m.Name << " signature did not decode";
    }
}

TEST(EntitySurface, StringFieldsDecodeToChar) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);

    auto fields = f.GetFields(stringTok);
    ASSERT_FALSE(fields.empty());
    // System.String has instance fields m_firstChar (char) and m_stringLength (int).
    bool foundCharField = false;
    for (const auto& fld : fields) {
        auto ty = f.GetFieldSignature(fld.Token);
        ASSERT_NE(ty, nullptr) << "field " << fld.Name << " signature is null";
        if (ty->ReflectionName() == "System.Char") foundCharField = true;
    }
    EXPECT_TRUE(foundCharField) << "no System.Char field on System.String";
}

TEST(EntitySurface, InvalidTokenIsGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    EXPECT_TRUE(f.GetMethods(0x02000000u).empty());
    EXPECT_TRUE(f.GetMethods(0x02FFFFFFu).empty());
    EXPECT_TRUE(f.GetFields(0x02FFFFFFu).empty());
    EXPECT_TRUE(f.GetProperties(0x02FFFFFFu).empty());
    EXPECT_EQ(f.GetFieldSignature(0x04000000u), nullptr);
    EXPECT_EQ(f.GetFieldSignature(0x04FFFFFFu), nullptr);
}
