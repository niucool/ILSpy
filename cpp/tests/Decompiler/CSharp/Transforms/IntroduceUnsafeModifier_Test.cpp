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

// Tests for the `IntroduceUnsafeModifier` AST transform: the per-shape unsafe detection
// (pointer / function-pointer / by-ref / array resolve results, `sizeof`, `*` and `&`,
// composed pointer types, `->`, `fixed`), the EntityDeclaration modifier insertion with
// the Accessor exclusion, and the two pointer-syntax rewrites (`*(ptr + int)` -> `ptr[int]`
// and `ptr->member` dereference -> `ptr->member`).

#include "Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/FixedVariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/FunctionPointerAstType.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <any>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the FixNameCollisions suite shape).
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A minimal named `IType` (the FixNameCollisions `StubType`).
class StubType : public TS::IType {
public:
    explicit StubType(std::string name) : name_(std::move(name)) {}
    TS::TypeKind Kind() const override { return TS::TypeKind::Unknown; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    bool StructuralEquals(const TS::IType& other) const override { return &other == this; }

private:
    std::string name_;
};

// A minimal `IParameter` over a configured type (the resolver-test `TestParameter`).
class TestParameter : public TS::IParameter {
public:
    explicit TestParameter(TS::ITypePtr type) : type_(std::move(type)) {}
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return "p"; }
    const TS::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::ReferenceKind ReferenceKind() const override { return TS::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const TS::IParameterizedMember* Owner() const override { return nullptr; }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    TS::ITypePtr type_;
};

std::shared_ptr<TS::IType> IntType() { return std::make_shared<StubType>("Int32"); }

std::shared_ptr<TS::PointerType> PointerTo(TS::ITypePtr element) {
    return std::make_shared<TS::PointerType>(std::move(element));
}

// A field stub to serve as the resolve-result member: the `MemberResolveResult` ctor
// dereferences `member->IsOverridable()`, so a null member is not constructible.
const TS::IMember* FakeMember() {
    static TestSupport::LookupCompilation compilation;
    static TestSupport::LookupField field("f", IntType(), compilation);
    return &field;
}

// A `MemberResolveResult` over a member with the given result type.
std::shared_ptr<Sem::MemberResolveResult> ResolveToMember(const TS::IMember* member,
                                                          TS::ITypePtr type) {
    return std::make_shared<Sem::MemberResolveResult>(nullptr, member != nullptr ? member : FakeMember(),
                                                      std::move(type));
}

// A method body of one expression statement carrying the given expression.
Syntax::MethodDeclaration* MakeMethodWith(Syntax::Expression* expression) {
    auto* method = new Syntax::MethodDeclaration();
    method->Name("M");
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(expression));
    method->Body(body);
    return method;
}

} // namespace

// An expression whose resolve result is a pointer type marks the enclosing method unsafe.
TEST(IntroduceUnsafeModifierTest, MarksMethodUnsafeForPointerTypedExpression)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* identifier = new Syntax::IdentifierExpression("p");
    identifier->AddAnnotation(ResolveToMember(nullptr, PointerTo(IntType())));
    auto* method = MakeMethodWith(identifier);

    Transforms::IntroduceUnsafeModifier transform;
    transform.Run(*method, context);

    EXPECT_TRUE(method->HasModifier(Syntax::Modifiers::Unsafe));
}

// The static `IsUnsafe` reports true for a pointer-typed expression and false otherwise.
TEST(IntroduceUnsafeModifierTest, IsUnsafeReflectsPointerTypedExpression)
{
    auto* pointerExpr = new Syntax::IdentifierExpression("p");
    pointerExpr->AddAnnotation(ResolveToMember(nullptr, PointerTo(IntType())));
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*pointerExpr));

    auto* plainExpr = new Syntax::IdentifierExpression("i");
    plainExpr->AddAnnotation(ResolveToMember(nullptr, IntType()));
    EXPECT_FALSE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*plainExpr));
}

// A by-reference-of-pointer type recurses into the element type.
TEST(IntroduceUnsafeModifierTest, ByReferenceOfPointerIsUnsafe)
{
    auto* identifier = new Syntax::IdentifierExpression("p");
    identifier->AddAnnotation(
        ResolveToMember(nullptr, std::make_shared<TS::ByReferenceType>(PointerTo(IntType()))));
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*identifier));
}

// An array-of-pointer type recurses into the element type.
TEST(IntroduceUnsafeModifierTest, ArrayOfPointerIsUnsafe)
{
    auto* identifier = new Syntax::IdentifierExpression("p");
    identifier->AddAnnotation(
        ResolveToMember(nullptr, std::make_shared<TS::ArrayType>(PointerTo(IntType()))));
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*identifier));
}

// `sizeof` requires an unsafe context.
TEST(IntroduceUnsafeModifierTest, SizeOfExpressionIsUnsafe)
{
    auto* sizeOf = new Syntax::SizeOfExpression();
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*sizeOf));
}

// A composed type with a positive pointer rank is unsafe; a plain one is not.
TEST(IntroduceUnsafeModifierTest, ComposedTypePointerRankIsUnsafe)
{
    auto* composed = new Syntax::ComposedType();
    composed->PointerRank(1);
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*composed));

    auto* plain = new Syntax::ComposedType();
    EXPECT_FALSE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*plain));
}

// A function-pointer type is unsafe.
TEST(IntroduceUnsafeModifierTest, FunctionPointerTypeIsUnsafe)
{
    auto* functionPointer = new Syntax::FunctionPointerAstType();
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*functionPointer));
}

// A pointer-reference expression (`p->x`) is unsafe.
TEST(IntroduceUnsafeModifierTest, PointerReferenceExpressionIsUnsafe)
{
    auto* pointerReference =
        new Syntax::PointerReferenceExpression(new Syntax::IdentifierExpression("p"), "x");
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*pointerReference));
}

// The address-of operator requires an unsafe context.
TEST(IntroduceUnsafeModifierTest, AddressOfIsUnsafe)
{
    auto* unary = new Syntax::UnaryOperatorExpression(
        new Syntax::IdentifierExpression("i"), Syntax::UnaryOperatorType::AddressOf);
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*unary));
}

// A fixed variable initializer requires an unsafe context.
TEST(IntroduceUnsafeModifierTest, FixedVariableInitializerIsUnsafe)
{
    auto* fixedInit = new Syntax::FixedVariableInitializer("p", new Syntax::IdentifierExpression("n"));
    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*fixedInit));
}

// A member whose parameter is a pointer type marks the call unsafe through its
// parameterized member.
TEST(IntroduceUnsafeModifierTest, MemberWithPointerParameterIsUnsafe)
{
    TransformFixture fixture;
    TestSupport::LookupMethod method("M", fixture.compilation);
    auto* parameter = new TestParameter(PointerTo(IntType()));
    method.SetParameters({parameter});
    auto* invocation = new Syntax::InvocationExpression(nullptr);
    invocation->AddAnnotation(ResolveToMember(&method, IntType()));

    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*invocation));
}

// A method-group resolve result whose chosen method returns a pointer marks the reference
// unsafe.
TEST(IntroduceUnsafeModifierTest, MethodGroupWithPointerReturnIsUnsafe)
{
    TransformFixture fixture;
    TestSupport::LookupMethod method("M", fixture.compilation);
    method.SetReturnType(PointerTo(IntType()));

    auto group = std::make_shared<::ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult>(
        nullptr, "M",
        std::vector<::ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType>{});
    auto chosen = group->WithChosenMethod(&method);
    std::shared_ptr<::ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult> chosenShared(
        std::move(chosen));

    auto* identifier = new Syntax::IdentifierExpression("M");
    identifier->AddAnnotation(chosenShared);

    EXPECT_TRUE(Transforms::IntroduceUnsafeModifier::IsUnsafe(*identifier));
}

// `*(ptr + int)` is rewritten to `ptr[int]`.
TEST(IntroduceUnsafeModifierTest, DereferenceOfPointerAdditionRewritesToIndexer)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* pointer = new Syntax::IdentifierExpression("ptr");
    auto* index = new Syntax::IdentifierExpression("i");
    auto* addition = new Syntax::BinaryOperatorExpression(
        pointer, Syntax::BinaryOperatorType::Add, index);
    std::vector<std::shared_ptr<Sem::ResolveResult>> operands;
    operands.push_back(std::make_shared<Sem::ResolveResult>(PointerTo(IntType())));
    operands.push_back(std::make_shared<Sem::ResolveResult>(IntType()));
    addition->AddAnnotation(std::make_shared<Sem::OperatorResolveResult>(
        IntType(), TS::ExpressionType::Add, operands));

    auto* dereference = new Syntax::UnaryOperatorExpression(
        addition, Syntax::UnaryOperatorType::Dereference);
    auto* statement = new Syntax::ExpressionStatement(dereference);

    Transforms::IntroduceUnsafeModifier transform;
    transform.Run(*statement, context);

    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(statement->Expression());
    ASSERT_NE(indexer, nullptr);
    EXPECT_EQ(indexer->Target(), pointer);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    EXPECT_EQ(indexer->Arguments().At(0), index);
}

// `(*p).x` is rewritten to `p->x`.
TEST(IntroduceUnsafeModifierTest, DereferenceMemberAccessRewritesToPointerReference)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* pointer = new Syntax::IdentifierExpression("p");
    auto* dereference =
        new Syntax::UnaryOperatorExpression(pointer, Syntax::UnaryOperatorType::Dereference);
    auto* memberReference = new Syntax::MemberReferenceExpression(dereference, "Field");
    auto* statement = new Syntax::ExpressionStatement(memberReference);

    Transforms::IntroduceUnsafeModifier transform;
    transform.Run(*statement, context);

    auto* pointerReference = dynamic_cast<Syntax::PointerReferenceExpression*>(statement->Expression());
    ASSERT_NE(pointerReference, nullptr);
    EXPECT_EQ(pointerReference->Target(), pointer);
    EXPECT_EQ(pointerReference->MemberName(), "Field");
}

// An `Accessor` reports unsafe but is not itself marked; the enclosing type gets the
// modifier.
TEST(IntroduceUnsafeModifierTest, AccessorIsNotMarkedButOwnerIs)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* identifier = new Syntax::IdentifierExpression("p");
    identifier->AddAnnotation(ResolveToMember(nullptr, PointerTo(IntType())));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(identifier));
    auto* accessor = new Syntax::Accessor(Syntax::AccessorKind::Getter);
    accessor->Body(body);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(accessor);

    Transforms::IntroduceUnsafeModifier transform;
    transform.Run(*typeDecl, context);

    EXPECT_TRUE(typeDecl->HasModifier(Syntax::Modifiers::Unsafe));
    EXPECT_FALSE(accessor->HasModifier(Syntax::Modifiers::Unsafe));
}

// A plain expression leaves the enclosing entity without the unsafe modifier.
TEST(IntroduceUnsafeModifierTest, KeepsEntityWithoutUnsafeWhenNoUnsafeChild)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* identifier = new Syntax::IdentifierExpression("i");
    identifier->AddAnnotation(ResolveToMember(nullptr, IntType()));
    auto* method = MakeMethodWith(identifier);

    Transforms::IntroduceUnsafeModifier transform;
    transform.Run(*method, context);

    EXPECT_FALSE(method->HasModifier(Syntax::Modifiers::Unsafe));
}
