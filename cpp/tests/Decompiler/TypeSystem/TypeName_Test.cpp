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

// Phase 2 naming-primitive tests. These exercise the ports of TopLevelTypeName,
// FullTypeName, and the KnownTypeCode table -- the foundation the type system
// and signature decoder build on. Matches the C# reflection-name semantics.

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <gtest/gtest.h>

TEST(TopLevelTypeName, ParsesReflectionName) {
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName t("System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(t.Namespace(), "System.Collections.Generic");
    EXPECT_EQ(t.Name(), "Dictionary");
    EXPECT_EQ(t.TypeParameterCount(), 2);
    EXPECT_EQ(t.ReflectionName(), "System.Collections.Generic.Dictionary`2");
}

TEST(TopLevelTypeName, NoNamespaceNoGenerics) {
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName t("Foo");
    EXPECT_EQ(t.Namespace(), "");
    EXPECT_EQ(t.Name(), "Foo");
    EXPECT_EQ(t.TypeParameterCount(), 0);
    EXPECT_EQ(t.ReflectionName(), "Foo");
}

TEST(TopLevelTypeName, RoundTripsReflectionName) {
    std::string names[] = { "System.Object", "System.Void", "System.Action`3",
                            "Foo.Bar`1", "Baz" };
    for (const auto& n : names) {
        ILSpy::Decompiler::TypeSystem::TopLevelTypeName t(n);
        EXPECT_EQ(t.ReflectionName(), n);
    }
}

TEST(TopLevelTypeName, Equality) {
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName a("System.Action`1");
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName b("System", "Action", 1);
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName c("System.Action`2");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(FullTypeName, TopLevelAndNested) {
    using F = ILSpy::Decompiler::TypeSystem::FullTypeName;
    F top("System.Collections.Generic.Dictionary`2");
    ASSERT_FALSE(top.IsNested());
    EXPECT_EQ(top.NestingLevel(), 0);
    EXPECT_EQ(top.Name(), "Dictionary");
    EXPECT_EQ(top.TypeParameterCount(), 2);
    EXPECT_EQ(top.ReflectionName(), "System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(top.FullName(), "System.Collections.Generic.Dictionary");
    EXPECT_EQ(top.GetTopLevelTypeName().Namespace(), "System.Collections.Generic");

    F nested("System.Collections.Generic.Dictionary`2+Enumerator");
    ASSERT_TRUE(nested.IsNested());
    EXPECT_EQ(nested.NestingLevel(), 1);
    EXPECT_EQ(nested.Name(), "Enumerator");
    EXPECT_EQ(nested.ReflectionName(), "System.Collections.Generic.Dictionary`2+Enumerator");
    EXPECT_EQ(nested.FullName(), "System.Collections.Generic.Dictionary.Enumerator");
}

TEST(KnownTypeTable, CoversPrimitivesAndCoreTypes) {
    using K = ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using namespace ILSpy::Decompiler::TypeSystem;
    auto* obj = LookupKnownType(K::Object);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->Namespace, "System");
    EXPECT_EQ(obj->Name, "Object");
    EXPECT_EQ(obj->Kind, TypeKind::Class);
    auto* str = LookupKnownType(K::String);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->Name, "String");
    EXPECT_EQ(str->Kind, TypeKind::Class);
    auto* v = LookupKnownType(K::Void);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->Name, "Void");
    EXPECT_EQ(v->Kind, TypeKind::Void);
    auto* ienum = LookupKnownType(K::IEnumerableOfT);
    ASSERT_NE(ienum, nullptr);
    EXPECT_EQ(ienum->Name, "IEnumerable");
    EXPECT_EQ(ienum->TypeParameterCount, 1);
    EXPECT_EQ(LookupKnownType(K::None), nullptr);
    // The table is dense and ordered by KnownTypeCode; every code except None
    // resolves to a distinct entry. (A sentinel like C#'s Range is not in the
    // enum, so we do not assert an exact count here.)
}
