// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// The `IType::Namespace()` surface (the AbstractType default plus the concrete
// overrides): KnownType, SimpleType, ParameterizedType, the decorator family,
// TupleType, UnknownType, and the resolved type definition (the
// `ITypeDefinition`-vs-`INamedElement` diamond dispatch).

#include "Decompiler/TypeSystem/IType.hpp"

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

} // namespace

TEST(ITypeNamespaceTest, KnownTypeReportsTheKnownTypeReferenceNamespace)
{
    TS::KnownType stringType(TS::KnownTypeCode::String);
    EXPECT_EQ(stringType.Namespace(), "System");

    TS::KnownType enumerableType(TS::KnownTypeCode::IEnumerableOfT);
    EXPECT_EQ(enumerableType.Namespace(), "System.Collections.Generic");
}

TEST(ITypeNamespaceTest, SimpleTypeReportsTheTopLevelNamespace)
{
    TS::SimpleType listType(
        TS::TopLevelTypeName("System.Collections.Generic", "List", 1));
    EXPECT_EQ(listType.Namespace(), "System.Collections.Generic");

    TS::SimpleType globalType(TS::TopLevelTypeName("", "GlobalThing", 0));
    EXPECT_EQ(globalType.Namespace(), "");
}

TEST(ITypeNamespaceTest, ParameterizedTypeDelegatesToTheGeneric)
{
    auto generic = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("System.Collections.Generic", "List", 1));
    auto parameterized = std::make_shared<TS::ParameterizedType>(
        generic, std::vector<TS::ITypePtr>{std::make_shared<TS::KnownType>(
                      TS::KnownTypeCode::Int32)});
    EXPECT_EQ(parameterized->Namespace(), "System.Collections.Generic");
}

TEST(ITypeNamespaceTest, DecoratorsDelegateToTheElement)
{
    auto element = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("System", "Object", 0));
    EXPECT_EQ(std::make_shared<TS::ArrayType>(element)->Namespace(), "System");
    EXPECT_EQ(std::make_shared<TS::ByReferenceType>(element)->Namespace(), "System");
    EXPECT_EQ(std::make_shared<TS::PointerType>(element)->Namespace(), "System");

    auto modifier = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("System.Runtime.CompilerServices", "IsVolatile", 0));
    auto modified = std::make_shared<TS::ModifiedType>(modifier, element, false);
    EXPECT_EQ(modified->Namespace(), "System");

    auto annotated = std::make_shared<TS::NullabilityAnnotatedType>(
        element, TS::Nullability::NotNullable);
    EXPECT_EQ(annotated->Namespace(), "System");
}

TEST(ITypeNamespaceTest, TupleTypeDelegatesToTheUnderlyingValueTuple)
{
    auto underlying = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("System", "ValueTuple", 2));
    std::vector<TS::ITypePtr> elements{
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32),
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String)};
    auto tuple = std::make_shared<TS::TupleType>(underlying, elements);
    EXPECT_EQ(tuple->Namespace(), "System");
}

TEST(ITypeNamespaceTest, UnknownTypeReportsTheFullTypeNameNamespace)
{
    auto unknown = std::make_shared<class TS::UnknownType>(
        TS::FullTypeName(TS::TopLevelTypeName("System.Missing", "Ghost", 0)));
    EXPECT_EQ(unknown->Namespace(), "System.Missing");
}

TEST(ITypeNamespaceTest, ResolvedTypeDefinitionDispatchesThroughTheITypeView)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    // The diamond dispatch: Namespace() reached through the IType view and the
    // ITypeDefinition view resolves to the single concrete override.
    const TS::IType& asType = compilation.FindType(TS::KnownTypeCode::String);
    EXPECT_EQ(asType.Namespace(), "System");
    const TS::ITypeDefinition* definition =
        compilation.FindType(TS::KnownTypeCode::String).GetDefinition();
    ASSERT_TRUE(definition != nullptr);
    EXPECT_EQ(definition->Namespace(), "System");
}

} // namespace ILSpy::Tests
