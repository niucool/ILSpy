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

// Tests for the three hand-written patterns in CustomPatterns.hpp (TypePattern,
// LdTokenPattern, TypeOfPattern). Each is exercised over synthetic AST nodes with the
// resolve-result / LdToken annotations the transforms attach, so the matching rules
// (the namespace + short-name type lookup, the composed-type base fallback, the
// ldtoken-annotation single-argument gate, the expanded typeof shape) are pinned
// without a resolver.

#include "Decompiler/CSharp/Transforms/CustomPatterns.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace PM = ::ILSpy::Decompiler::CSharp::Syntax::PatternMatching;

namespace {

// The `UnknownType` class and the `UnknownType()` convenience function share the name
// in `TypeSystem`; the elaborated-type-specifier picks the class.
TS::ITypePtr MakeNamedType(const std::string& namespaceName, const std::string& name) {
    return std::shared_ptr<class TS::UnknownType>(
        new class TS::UnknownType(namespaceName, name, 0));
}

// A `SimpleType` whose resolve-result annotation is a `TypeResolveResult` for the
// given (namespace, name).
Syntax::SimpleType* TypedType(const std::string& namespaceName, const std::string& name) {
    auto* type = new Syntax::SimpleType(name);
    type->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(MakeNamedType(namespaceName, name)));
    return type;
}

} // namespace

// A type node whose resolve-result type has the stored namespace and short name matches.
TEST(CustomPatternsTest, TypePatternMatchesNamespaceAndName)
{
    auto* type = TypedType("System.Reflection", "MethodInfo");
    Transforms::TypePattern pattern("System.Reflection", "MethodInfo");
    EXPECT_TRUE(pattern.DoMatch(type, PM::Match::CreateNew()));
    EXPECT_EQ(pattern.NamespaceName(), "System.Reflection");
    EXPECT_EQ(pattern.TypeName(), "MethodInfo");
}

// A different namespace or short name does not match.
TEST(CustomPatternsTest, TypePatternRejectsWrongNamespaceOrName)
{
    auto* type = TypedType("System.Reflection", "MethodInfo");
    Transforms::TypePattern wrongNamespace("System", "MethodInfo");
    EXPECT_FALSE(wrongNamespace.DoMatch(type, PM::Match::CreateNew()));
    Transforms::TypePattern wrongName("System.Reflection", "ConstructorInfo");
    EXPECT_FALSE(wrongName.DoMatch(type, PM::Match::CreateNew()));
}

// A node that is not an `AstType` cannot match, and a type with no resolve result
// cannot match.
TEST(CustomPatternsTest, TypePatternRejectsNonTypeAndUnresolved)
{
    Transforms::TypePattern pattern("System.Reflection", "MethodInfo");
    auto* notAType = new Syntax::IdentifierExpression("x");
    EXPECT_FALSE(pattern.DoMatch(notAType, PM::Match::CreateNew()));
    auto* unresolved = new Syntax::SimpleType("MethodInfo");
    EXPECT_FALSE(pattern.DoMatch(unresolved, PM::Match::CreateNew()));
}

// A `ComposedType` carrying no modifiers is inspected through its `BaseType` (the
// ILSpy left-over shape); one carrying a modifier is not.
TEST(CustomPatternsTest, TypePatternLooksThroughModifierlessComposedType)
{
    Transforms::TypePattern pattern("System.Reflection", "MethodInfo");
    auto* composed = new Syntax::ComposedType();
    composed->BaseType(TypedType("System.Reflection", "MethodInfo"));
    EXPECT_TRUE(pattern.DoMatch(composed, PM::Match::CreateNew()));

    auto* nullable = new Syntax::ComposedType();
    nullable->BaseType(TypedType("System.Reflection", "MethodInfo"));
    nullable->HasNullableSpecifier(true);
    EXPECT_FALSE(pattern.DoMatch(nullable, PM::Match::CreateNew()));
}

// An `ldtoken(...)` invocation carrying the marker annotation with exactly one
// argument matches and captures the argument under the group name.
TEST(CustomPatternsTest, LdTokenPatternCapturesSingleArgument)
{
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::IdentifierExpression("ldtoken"));
    invocation->AddAnnotation(
        std::make_shared<::ILSpy::Decompiler::CSharp::LdTokenAnnotation>());
    auto* argument = new Syntax::IdentifierExpression("token");
    invocation->Arguments().Add(argument);

    PM::Match match = PM::Match::CreateNew();
    Transforms::LdTokenPattern pattern("method");
    EXPECT_TRUE(pattern.DoMatch(invocation, match));
    ASSERT_EQ(match.Get("method").size(), 1U);
    EXPECT_EQ(match.Get("method").front(), argument);
}

// An invocation without the annotation, with the wrong argument count, or a
// non-invocation does not match.
TEST(CustomPatternsTest, LdTokenPatternRejectsUnannotatedOrWrongArity)
{
    Transforms::LdTokenPattern pattern("method");
    auto* plain = new Syntax::InvocationExpression(new Syntax::IdentifierExpression("f"));
    EXPECT_FALSE(pattern.DoMatch(plain, PM::Match::CreateNew()));

    auto* twoArgs = new Syntax::InvocationExpression(new Syntax::IdentifierExpression("f"));
    twoArgs->AddAnnotation(
        std::make_shared<::ILSpy::Decompiler::CSharp::LdTokenAnnotation>());
    twoArgs->Arguments().Add(new Syntax::IdentifierExpression("a"));
    twoArgs->Arguments().Add(new Syntax::IdentifierExpression("b"));
    EXPECT_FALSE(pattern.DoMatch(twoArgs, PM::Match::CreateNew()));

    auto* notInvocation = new Syntax::IdentifierExpression("x");
    EXPECT_FALSE(pattern.DoMatch(notInvocation, PM::Match::CreateNew()));
}

// The expanded `typeof` shape `Type.GetTypeFromHandle(typeof(T)).TypeHandle` matches
// and captures `T`.
TEST(CustomPatternsTest, TypeOfPatternMatchesExpandedTypeof)
{
    auto* typeReference = new Syntax::TypeReferenceExpression(TypedType("System", "Type"));
    auto* getTypeFromHandle = new Syntax::MemberReferenceExpression(
        typeReference, std::string("GetTypeFromHandle"));
    auto* invocation = new Syntax::InvocationExpression(getTypeFromHandle);
    auto* captured = TypedType("Foo", "Bar");
    invocation->Arguments().Add(new Syntax::TypeOfExpression(captured));
    auto* outer = new Syntax::MemberReferenceExpression(
        invocation, std::string("TypeHandle"));

    PM::Match match = PM::Match::CreateNew();
    Transforms::TypeOfPattern pattern("declaringType");
    EXPECT_TRUE(pattern.DoMatch(outer, match));
    ASSERT_EQ(match.Get("declaringType").size(), 1U);
    EXPECT_EQ(match.Get("declaringType").front(), captured);
}

// A member reference without the `TypeHandle` name does not match the expanded typeof
// shape.
TEST(CustomPatternsTest, TypeOfPatternRejectsNonTypeHandle)
{
    auto* typeReference = new Syntax::TypeReferenceExpression(TypedType("System", "Type"));
    auto* getTypeFromHandle = new Syntax::MemberReferenceExpression(
        typeReference, std::string("GetTypeFromHandle"));
    auto* invocation = new Syntax::InvocationExpression(getTypeFromHandle);
    invocation->Arguments().Add(
        new Syntax::TypeOfExpression(TypedType("Foo", "Bar")));

    Transforms::TypeOfPattern pattern("declaringType");
    EXPECT_FALSE(pattern.DoMatch(invocation, PM::Match::CreateNew()));
}
