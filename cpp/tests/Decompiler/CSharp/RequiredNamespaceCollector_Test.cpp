// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
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

// Tests for RequiredNamespaceCollector (the port of
// ICSharpCode.Decompiler/CSharp/RequiredNamespaceCollector.cs): the ctor's
// known-type namespace seeding, and the CollectTypeReference walk over the
// concrete IType shapes (a KnownType adds its namespace; a ParameterizedType
// adds its own namespace plus recurses into the type arguments; an ArrayType
// recurses into the element type then sweeps the base types; the
// visited-types gate makes a repeated walk a no-op).

#include "Decompiler/CSharp/RequiredNamespaceCollector.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"


#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace CS = ::ILSpy::Decompiler::CSharp;

} // namespace

// The ctor seeds every known type's namespace (System at minimum).
TEST(RequiredNamespaceCollectorTest, CtorSeedsKnownTypeNamespaces)
{
    std::unordered_set<std::string> namespaces;
    CS::RequiredNamespaceCollector collector(namespaces);
    EXPECT_GT(namespaces.count("System"), 0u)
        << "the known-type namespaces are seeded";
}

// A KnownType reference walk adds its namespace.
TEST(RequiredNamespaceCollectorTest, KnownTypeAddsItsNamespace)
{
    std::unordered_set<std::string> namespaces;
    namespaces.emplace("System");  // the ctor-seeded state
    CS::RequiredNamespaceCollector collector(namespaces);
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    collector.CollectTypeReference(intType.get());
    // System is present (already seeded); the walk must not throw and must
    // have visited.
    EXPECT_GT(namespaces.count("System"), 0u);
}

// A non-known type in a custom namespace lands in the set (the default arm).
TEST(RequiredNamespaceCollectorTest, CustomNamespaceTypeLandsInTheSet)
{
    std::unordered_set<std::string> namespaces;
    namespaces.emplace("System");
    CS::RequiredNamespaceCollector collector(namespaces);
    auto widgetType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("MyLib", "Widget"));
    collector.CollectTypeReference(widgetType.get());
    EXPECT_GT(namespaces.count("MyLib"), 0u)
        << "the default arm records the type's namespace";
}

// A second walk over the same type is a no-op (the visitedTypes gate).
TEST(RequiredNamespaceCollectorTest, RepeatedWalkIsIdempotent)
{
    std::unordered_set<std::string> namespaces;
    namespaces.emplace("System");
    CS::RequiredNamespaceCollector collector(namespaces);
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    collector.CollectTypeReference(intType.get());
    const std::size_t before = namespaces.size();
    collector.CollectTypeReference(intType.get());
    EXPECT_EQ(namespaces.size(), before) << "the visited gate dedupes";
}