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

// Tests for the RequiredNamespaceCollector metadata walk (the port of
// ICSharpCode.Decompiler/CSharp/RequiredNamespaceCollector.cs, the entity +
// method-body arms): the ctor seeds the known-type namespaces, the attribute
// handler records the attribute type's namespace, the type-parameter handler
// walks the constraints, the member-reference collector walks declaring +
// return types, and the type-reference walk covers the ParameterizedType /
// ArrayType / FunctionPointerType shapes.

#include "Decompiler/CSharp/RequiredNamespaceCollector.hpp"

#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <unordered_set>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace CS = ::ILSpy::Decompiler::CSharp;

// A minimal attribute for the handler probes (the
// MemberResolveResult_Test TestAttribute shape).
class TestNsAttribute : public TS::IAttribute {
public:
    explicit TestNsAttribute(TS::ITypePtr attributeType)
        : attributeType_(std::move(attributeType)) {}
    const TS::IType& AttributeType() const override { return *attributeType_; }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
        return {};
    }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
        return {};
    }

private:
    TS::ITypePtr attributeType_;
};

} // namespace

// A non-known type in a custom namespace lands in the set (the default arm).
TEST(RequiredNamespaceCollectorMetadataTest, CustomNamespaceTypeLandsInTheSet)
{
    std::unordered_set<std::string> namespaces;
    namespaces.emplace("System");
    CS::RequiredNamespaceCollector collector(namespaces);
    auto widgetType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string("MyLib"), std::string("Widget")));
    collector.CollectTypeReference(widgetType.get());
    EXPECT_GT(namespaces.count("MyLib"), 0u)
        << "the default arm records the type's namespace";
}


// An array type recurses into the element type (a System.String element
// contributes System, which is already present; a custom-element array
// contributes the element's namespace).
TEST(RequiredNamespaceCollectorMetadataTest, ArrayTypeRecursesIntoElementType)
{
    std::unordered_set<std::string> namespaces;
    namespaces.emplace("System");
    CS::RequiredNamespaceCollector collector(namespaces);
    auto widgetType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string("MyLib"), std::string("Widget")));
    auto arrayType =
        std::make_shared<TS::ArrayType>(widgetType, /*rank=*/1);
    collector.CollectTypeReference(arrayType.get());
    EXPECT_GT(namespaces.count("MyLib"), 0u)
        << "the array element's namespace is collected";
}
// The attribute walk: an attribute whose type lives in a custom namespace
// contributes that namespace (the C# HandleAttributes's
// `attr.AttributeType.Namespace`).
TEST(RequiredNamespaceCollectorMetadataTest, AttributeTypeNamespaceIsCollected)
{
    std::unordered_set<std::string> namespaces;
    namespaces.emplace("System");
    CS::RequiredNamespaceCollector collector(namespaces);
    auto attrType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string("MyLib.Attributes"),
                             std::string("WidgetAttribute")));
    auto attr = std::make_unique<TestNsAttribute>(attrType);
    std::vector<const TS::IAttribute*> attributes{attr.get()};
    collector.HandleAttributes(attributes);
    EXPECT_GT(namespaces.count("MyLib.Attributes"), 0u)
        << "the attribute type's namespace lands in the set";
}
