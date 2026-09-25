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

// Tests for the resolver-free statics of `ReplaceMethodCallsWithOperators` -- the
// operator-name mapping tables the instance `ProcessInvocationExpression` consumes, plus
// `IsInstantiableTypeParameter`, and the `SyntaxExtensions.UnwrapInDirectionExpression`
// helper they share. These are pure functions (name strings and a settings flag in, an
// operator enum and a checked flag out), so the tests pin the complete mapping tables and
// the settings gates without any AST resolver.

#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

namespace {

using BinaryOperatorType = Syntax::BinaryOperatorType;
using UnaryOperatorType = Syntax::UnaryOperatorType;

// Asserts one binary-operator name maps to `expected` and leaves `isChecked` false.
void ExpectBinary(const std::string& name, BinaryOperatorType expected,
                  const DecompilerSettings& settings) {
    bool isChecked = true;
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                  name, isChecked, settings),
              std::optional<BinaryOperatorType>(expected))
        << name;
    EXPECT_FALSE(isChecked) << name;
}

// Asserts one unary-operator name maps to `expected` and leaves `isChecked` false.
void ExpectUnary(const std::string& name, UnaryOperatorType expected,
                 const DecompilerSettings& settings) {
    bool isChecked = true;
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
                  name, isChecked, settings),
              std::optional<UnaryOperatorType>(expected))
        << name;
    EXPECT_FALSE(isChecked) << name;
}

} // namespace

// The full binary-operator name table (`settings.CheckedOperators` off, so the checked
// names are not in this table).
TEST(ReplaceMethodCallsWithOperatorsTest, MapsEveryBinaryOperator)
{
    DecompilerSettings settings;
    ExpectBinary("op_Addition", BinaryOperatorType::Add, settings);
    ExpectBinary("op_Subtraction", BinaryOperatorType::Subtract, settings);
    ExpectBinary("op_Multiply", BinaryOperatorType::Multiply, settings);
    ExpectBinary("op_Division", BinaryOperatorType::Divide, settings);
    ExpectBinary("op_Modulus", BinaryOperatorType::Modulus, settings);
    ExpectBinary("op_BitwiseAnd", BinaryOperatorType::BitwiseAnd, settings);
    ExpectBinary("op_BitwiseOr", BinaryOperatorType::BitwiseOr, settings);
    ExpectBinary("op_ExclusiveOr", BinaryOperatorType::ExclusiveOr, settings);
    ExpectBinary("op_LeftShift", BinaryOperatorType::ShiftLeft, settings);
    ExpectBinary("op_RightShift", BinaryOperatorType::ShiftRight, settings);
    ExpectBinary("op_Equality", BinaryOperatorType::Equality, settings);
    ExpectBinary("op_Inequality", BinaryOperatorType::InEquality, settings);
    ExpectBinary("op_LessThan", BinaryOperatorType::LessThan, settings);
    ExpectBinary("op_LessThanOrEqual", BinaryOperatorType::LessThanOrEqual, settings);
    ExpectBinary("op_GreaterThan", BinaryOperatorType::GreaterThan, settings);
    ExpectBinary("op_GreaterThanOrEqual", BinaryOperatorType::GreaterThanOrEqual, settings);
}

// The full unary-operator name table (`settings.CheckedOperators` off).
TEST(ReplaceMethodCallsWithOperatorsTest, MapsEveryUnaryOperator)
{
    DecompilerSettings settings;
    ExpectUnary("op_LogicalNot", UnaryOperatorType::Not, settings);
    ExpectUnary("op_OnesComplement", UnaryOperatorType::BitNot, settings);
    ExpectUnary("op_UnaryNegation", UnaryOperatorType::Minus, settings);
    ExpectUnary("op_UnaryPlus", UnaryOperatorType::Plus, settings);
    ExpectUnary("op_Increment", UnaryOperatorType::Increment, settings);
    ExpectUnary("op_Decrement", UnaryOperatorType::Decrement, settings);
}

// A name that is not an operator method maps to nullopt and leaves `isChecked` false.
TEST(ReplaceMethodCallsWithOperatorsTest, UnknownNamesMapToNothing)
{
    DecompilerSettings settings;
    bool isChecked = true;
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                     "Concat", isChecked, settings)
                     .has_value());
    EXPECT_FALSE(isChecked);
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
                     "op_True", isChecked, settings)
                     .has_value());
    EXPECT_FALSE(isChecked);
}

// The four checked binary operators are recognized only with `CheckedOperators` on, and
// report `isChecked`. With the setting off they fall to nullopt.
TEST(ReplaceMethodCallsWithOperatorsTest, CheckedBinaryOperatorsGatedBySetting)
{
    DecompilerSettings checked;
    checked.SetCheckedOperators(true);
    auto checkedAdd = [](const char* name, BinaryOperatorType expected, DecompilerSettings& s) {
        bool isChecked = false;
        EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::
                      GetBinaryOperatorTypeFromMetadataName(name, isChecked, s),
                  std::optional<BinaryOperatorType>(expected))
            << name;
        EXPECT_TRUE(isChecked) << name;
    };
    checkedAdd("op_CheckedAddition", BinaryOperatorType::Add, checked);
    checkedAdd("op_CheckedSubtraction", BinaryOperatorType::Subtract, checked);
    checkedAdd("op_CheckedMultiply", BinaryOperatorType::Multiply, checked);
    checkedAdd("op_CheckedDivision", BinaryOperatorType::Divide, checked);

    DecompilerSettings off;
    off.SetCheckedOperators(false);
    bool isChecked = true;
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                     "op_CheckedAddition", isChecked, off)
                     .has_value());
    EXPECT_FALSE(isChecked);
}

// The checked unary operators group: the checked negation and the checked increment /
// decrement map only with `CheckedOperators` on.
TEST(ReplaceMethodCallsWithOperatorsTest, CheckedUnaryOperatorsGatedBySetting)
{
    DecompilerSettings checked;
    checked.SetCheckedOperators(true);
    auto checkedUnary = [](const char* name, UnaryOperatorType expected, DecompilerSettings& s) {
        bool isChecked = false;
        EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::
                      GetUnaryOperatorTypeFromMetadataName(name, isChecked, s),
                  std::optional<UnaryOperatorType>(expected))
            << name;
        EXPECT_TRUE(isChecked) << name;
    };
    checkedUnary("op_CheckedUnaryNegation", UnaryOperatorType::Minus, checked);
    checkedUnary("op_CheckedIncrement", UnaryOperatorType::Increment, checked);
    checkedUnary("op_CheckedDecrement", UnaryOperatorType::Decrement, checked);

    DecompilerSettings off;
    off.SetCheckedOperators(false);
    bool isChecked = true;
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
                     "op_CheckedIncrement", isChecked, off)
                     .has_value());
    EXPECT_FALSE(isChecked);
}

// `op_UnsignedRightShift` maps to `>>>` only with `UnsignedRightShift` on.
TEST(ReplaceMethodCallsWithOperatorsTest, UnsignedRightShiftGatedBySetting)
{
    DecompilerSettings on;
    on.SetUnsignedRightShift(true);
    bool isChecked = true;
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                  "op_UnsignedRightShift", isChecked, on),
              std::optional<BinaryOperatorType>(BinaryOperatorType::UnsignedShiftRight));
    EXPECT_FALSE(isChecked);

    DecompilerSettings off;
    off.SetUnsignedRightShift(false);
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
                     "op_UnsignedRightShift", isChecked, off)
                     .has_value());
}

// `IsInstantiableTypeParameter` accepts only a type parameter carrying the `new()`
// constraint; a plain type parameter without it and a non-type-parameter type both fail.
TEST(ReplaceMethodCallsWithOperatorsTest, IsInstantiableTypeParameter)
{
    auto withConstraint = std::make_shared<LookupTypeParameter>("T");
    withConstraint->SetHasDefaultConstructorConstraint(true);
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
        *withConstraint));

    auto withoutConstraint = std::make_shared<LookupTypeParameter>("T");
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
        *withoutConstraint));

    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
        compilation.FindType(TS::KnownTypeCode::Int32)));
}

// The `in`-direction wrapper is unwrapped and its operand detached; `ref`/`out`
// wrappers and unwrapped expressions pass through unchanged.
TEST(SyntaxExtensionsTest, UnwrapInDirectionExpression)
{
    auto* inner = new Syntax::IdentifierExpression("x");
    auto* inDir = new Syntax::DirectionExpression(Syntax::FieldDirection::In, inner);
    Syntax::Expression* unwrapped = Syntax::UnwrapInDirectionExpression(inDir);
    EXPECT_EQ(unwrapped, inner);
    EXPECT_EQ(inner->Parent(), nullptr);
    EXPECT_EQ(inDir->Expression(), nullptr);

    auto* refDir = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Ref, new Syntax::IdentifierExpression("y"));
    EXPECT_EQ(Syntax::UnwrapInDirectionExpression(refDir), refDir);
    EXPECT_NE(refDir->Expression(), nullptr);

    auto* outDir = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Out, new Syntax::IdentifierExpression("z"));
    EXPECT_EQ(Syntax::UnwrapInDirectionExpression(outDir), outDir);

    auto* plain = new Syntax::IdentifierExpression("w");
    EXPECT_EQ(Syntax::UnwrapInDirectionExpression(plain), plain);
}

// ---------------------------------------------------------------------------
// The instance transform suite: `Run` / `VisitInvocationExpression` /
// `ProcessInvocationExpression` over a resolved method symbol. The fixture mirrors the
// PatternStatementTransform suite (a `SimpleCompilation` + `DecompileRun` +
// `TypeSystemAstBuilder` bag), and the resolved method is a `FakeMethod` attached through
// an `InvocationResolveResult` annotation.
// ---------------------------------------------------------------------------

namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;

// The TransformContext fixture (the PatternStatementTransform suite shape).
struct InstanceFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    ::ILSpy::Decompiler::DecompileRun run;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    InstanceFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<
                  ::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<
            ::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }

    TS::ITypePtr FindType(TS::KnownTypeCode code) {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }
};

// A `FakeMethod` whose `FullName` is set explicitly (the C# switch in
// `ProcessInvocationExpression` matches the full metadata name, which `FakeMethod`'s
// declaring-type-based default cannot express without a synthetic declaring type).
class FullNameMethod : public Impl::FakeMethod {
public:
    FullNameMethod(const TS::ICompilation& compilation, TS::SymbolKind kind)
        : FakeMethod(compilation, kind) {}

    std::string DeclaredFullName;
    std::string FullName() const override { return DeclaredFullName; }
};

// A stub declaring type whose `GetMethods` returns a canned snapshot (the
// HasCheckedEquivalent twin detection fixture).
class OperatorHostType : public TS::IType {
public:
    std::vector<const TS::IMethod*> methods;

    TS::TypeKind Kind() const override { return TS::TypeKind::Class; }
    std::string Name() const override { return "OperatorHost"; }
    std::string ReflectionName() const override { return "OperatorHost"; }
    int TypeParameterCount() const override { return 0; }
    std::vector<const TS::IMethod*> GetMethods(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override {
        (void)options;
        if (!filter)
            return methods;
        std::vector<const TS::IMethod*> result;
        for (const TS::IMethod* m : methods) {
            if (filter(m))
                result.push_back(m);
        }
        return result;
    }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }
};

// Attaches the method as the invocation's resolve-result symbol.
void ResolveInvocation(Syntax::InvocationExpression* invocation,
                       const TS::IMethod* method) {
    invocation->AddAnnotation(
        std::make_shared<Sem::InvocationResolveResult>(nullptr, method));
}

// Builds `op_Name` on `declaringType` with the given parameter/return types.
std::shared_ptr<Impl::FakeMethod> MakeOperatorMethod(
    InstanceFixture& fixture, const char* name, TS::ITypePtr declaringType,
    std::vector<TS::ITypePtr> parameterTypes, TS::ITypePtr returnType) {
    auto method = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Operator);
    method->SetName(name);
    method->SetIsStatic(true);
    if (declaringType != nullptr)
        method->SetDeclaringType(std::move(declaringType));
    method->SetReturnType(std::move(returnType));
    std::vector<std::shared_ptr<const TS::IParameter>> parameters;
    for (TS::ITypePtr& type : parameterTypes) {
        parameters.push_back(std::make_shared<Impl::DefaultParameter>(
            type, std::string("arg")));
    }
    method->SetParameters(std::move(parameters));
    return method;
}

// Runs the transform over a fresh block holding one expression statement and returns the
// (possibly replaced) statement expression.
Syntax::Expression* RunOnExpression(InstanceFixture& fixture,
                                    Syntax::Expression* expression) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    auto* statement = new Syntax::ExpressionStatement(expression);
    block->Statements().Add(statement);
    Transforms::ReplaceMethodCallsWithOperators transform;
    transform.Run(*block, context);
    return statement->Expression();
}

// Builds an invocation whose target is the given name with the given arguments.
Syntax::InvocationExpression* Invoke(const char* name,
                                     std::vector<Syntax::Expression*> arguments) {
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(
            new Syntax::IdentifierExpression("Host"), name));
    for (Syntax::Expression* argument : arguments)
        invocation->Arguments().Add(argument);
    return invocation;
}

// Attaches a `TypeResolveResult` to an expression (the argument/`ToString`-target
// resolve-result fixture).
Syntax::Expression* WithType(Syntax::Expression* expression, TS::ITypePtr type) {
    expression->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(std::move(type)));
    return expression;
}

} // namespace

// `System.Type.GetTypeFromHandle(typeof(T).TypeHandle)` becomes `typeof(T)`.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, GetTypeFromHandleBecomesTypeOf)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("GetTypeFromHandle");
    method->DeclaredFullName = "System.Type.GetTypeFromHandle";
    auto* typeHandle = new Syntax::MemberReferenceExpression(
        new Syntax::TypeOfExpression(new Syntax::SimpleType("int")), "TypeHandle");
    auto* invocation = Invoke("GetTypeFromHandle", {typeHandle});
    ResolveInvocation(invocation, method.get());
    Syntax::Expression* expectedTarget = typeHandle->Target();

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, expectedTarget);
}

// A `GetTypeFromHandle` call whose argument is not a `typeof(...).TypeHandle` shape is
// left alone.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, GetTypeFromHandleNonMatchingTargetKept)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("GetTypeFromHandle");
    method->DeclaredFullName = "System.Type.GetTypeFromHandle";
    auto* invocation = Invoke("GetTypeFromHandle",
                              {new Syntax::IdentifierExpression("runtimeType")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// `System.Activator.CreateInstance<T>()` with a `new()`-constrained `T` becomes `new T()`.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, CreateInstanceBecomesObjectCreate)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("CreateInstance");
    method->DeclaredFullName = "System.Activator.CreateInstance";
    auto typeParameter =
        std::make_shared<TS::TestSupport::LookupTypeParameter>("T");
    typeParameter->SetHasDefaultConstructorConstraint(true);
    method->SetTypeParameters({typeParameter});
    auto* invocation = Invoke("CreateInstance", {});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_NE(dynamic_cast<Syntax::ObjectCreateExpression*>(result), nullptr);
}

// With `UseObjectCreationOfGenericTypeParameter` off the call is kept.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, CreateInstanceSettingOffKept)
{
    InstanceFixture fixture;
    fixture.settings.SetUseObjectCreationOfGenericTypeParameter(false);
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("CreateInstance");
    method->DeclaredFullName = "System.Activator.CreateInstance";
    auto typeParameter =
        std::make_shared<TS::TestSupport::LookupTypeParameter>("T");
    typeParameter->SetHasDefaultConstructorConstraint(true);
    method->SetTypeParameters({typeParameter});
    auto* invocation = Invoke("CreateInstance", {});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// `RuntimeHelpers.GetSubArray(array, range)` becomes `array[range]`.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, GetSubArrayBecomesIndexer)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("GetSubArray");
    method->DeclaredFullName =
        "System.Runtime.CompilerServices.RuntimeHelpers.GetSubArray";
    auto* array = new Syntax::IdentifierExpression("array");
    auto* range = new Syntax::IdentifierExpression("range");
    auto* invocation = Invoke("GetSubArray", {array, range});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(result);
    ASSERT_NE(indexer, nullptr);
    EXPECT_EQ(indexer->Target(), array);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    EXPECT_EQ(indexer->Arguments()[0], range);
}

// With `Ranges` off the call is kept.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, GetSubArraySettingOffKept)
{
    InstanceFixture fixture;
    fixture.settings.SetRanges(false);
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("GetSubArray");
    method->DeclaredFullName =
        "System.Runtime.CompilerServices.RuntimeHelpers.GetSubArray";
    auto* invocation = Invoke("GetSubArray", {new Syntax::IdentifierExpression("array"),
                                              new Syntax::IdentifierExpression("range")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A binary operator method with two arguments becomes the operator expression.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, BinaryOperatorMethodBecomesOperator)
{
    InstanceFixture fixture;
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto method = MakeOperatorMethod(fixture, "op_Addition", nullptr,
                                     {intType, intType}, intType);
    auto* left = new Syntax::IdentifierExpression("a");
    auto* right = new Syntax::IdentifierExpression("b");
    auto* invocation = Invoke("op_Addition", {left, right});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Add);
    EXPECT_EQ(binary->Left(), left);
    EXPECT_EQ(binary->Right(), right);
}

// A checked operator method is recognized only with `CheckedOperators` on and gets the
// checked annotation (consumed by AddCheckedBlocks).
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, CheckedBinaryOperatorAnnotatesChecked)
{
    InstanceFixture fixture;
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto method = MakeOperatorMethod(fixture, "op_CheckedAddition", nullptr,
                                     {intType, intType}, intType);
    auto* invocation = Invoke("op_CheckedAddition",
                              {new Syntax::IdentifierExpression("a"),
                               new Syntax::IdentifierExpression("b")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    auto* annotation = binary->Annotation<Transforms::CheckedUncheckedAnnotation>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_TRUE(annotation->IsChecked);
}

// A checked operator method whose declaring type also declares the unchecked twin gets
// the unchecked annotation.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, UncheckedOperatorAnnotatesUnchecked)
{
    InstanceFixture fixture;
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto host = std::make_shared<OperatorHostType>();
    auto method = MakeOperatorMethod(fixture, "op_Addition", host,
                                     {intType, intType}, intType);
    auto twin = MakeOperatorMethod(fixture, "op_CheckedAddition", host,
                                   {intType, intType}, intType);
    host->methods.push_back(twin.get());
    auto* invocation = Invoke("op_Addition", {new Syntax::IdentifierExpression("a"),
                                              new Syntax::IdentifierExpression("b")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    auto* annotation = binary->Annotation<Transforms::CheckedUncheckedAnnotation>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_FALSE(annotation->IsChecked);
}

// A unary operator method with one argument becomes the unary operator expression.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, UnaryOperatorMethodBecomesOperator)
{
    InstanceFixture fixture;
    auto boolType = fixture.FindType(TS::KnownTypeCode::Boolean);
    auto method = MakeOperatorMethod(fixture, "op_LogicalNot", nullptr, {boolType},
                                     boolType);
    auto* argument = new Syntax::IdentifierExpression("flag");
    auto* invocation = Invoke("op_LogicalNot", {argument});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(result);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
    EXPECT_EQ(unary->Expression(), argument);
}

// An explicit conversion operator becomes a cast to the method's return type.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, ExplicitConversionBecomesCast)
{
    InstanceFixture fixture;
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto method = MakeOperatorMethod(fixture, "op_Explicit", nullptr, {intType},
                                     intType);
    auto* argument = new Syntax::IdentifierExpression("value");
    auto* invocation = Invoke("op_Explicit", {argument});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(result);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->Expression(), argument);
}

// `op_Increment` on a non-decimal type is kept (it is not equivalent to `++a`).
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, IncrementOperatorOnIntKept)
{
    InstanceFixture fixture;
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto method = MakeOperatorMethod(fixture, "op_Increment", nullptr, {intType},
                                     intType);
    auto* invocation = Invoke("op_Increment",
                              {new Syntax::IdentifierExpression("counter")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// `op_True(x)` in a condition slot is replaced by `x`.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, OpTrueConditionRemoved)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Operator);
    method->SetName("op_True");
    auto* argument = new Syntax::IdentifierExpression("condition");
    auto* invocation = Invoke("op_True", {argument});
    ResolveInvocation(invocation, method.get());

    Transforms::TransformContext context = fixture.MakeContext();
    auto* ifElse = new Syntax::IfElseStatement();
    ifElse->Condition(invocation);
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(ifElse);
    Transforms::ReplaceMethodCallsWithOperators transform;
    transform.Run(*block, context);

    EXPECT_EQ(ifElse->Condition(), argument);
}

// `op_True(x)` outside a condition slot is kept.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, OpTrueOutsideConditionKept)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Operator);
    method->SetName("op_True");
    auto* invocation = Invoke("op_True", {new Syntax::IdentifierExpression("condition")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A method that is not one of the recognized shapes is left alone.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, NonOperatorMethodKept)
{
    InstanceFixture fixture;
    auto method = std::make_shared<FullNameMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("DoWork");
    method->DeclaredFullName = "Host.DoWork";
    auto* invocation = Invoke("DoWork", {new Syntax::IdentifierExpression("a")});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// An invocation with no resolve-result symbol is left alone.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, UnresolvedInvocationKept)
{
    InstanceFixture fixture;
    auto* invocation = Invoke("DoWork", {new Syntax::IdentifierExpression("a")});

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// ---------------------------------------------------------------------------
// The `String.Concat(a, b)` -> `a + b` reduction (`CheckArgumentsForStringConcat`
// plus the argument/`ToString` reductions it drives).
// ---------------------------------------------------------------------------

// `String.Concat(a, b)` with two string arguments becomes `a + b`.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatBecomesAddition)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {stringType, stringType}, stringType);
    auto* a = WithType(new Syntax::IdentifierExpression("a"), stringType);
    auto* b = WithType(new Syntax::IdentifierExpression("b"), stringType);
    auto* invocation = Invoke("Concat", {a, b});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Add);
    EXPECT_EQ(binary->Left(), a);
    EXPECT_EQ(binary->Right(), b);
}

// With `StringConcat` off the `String.Concat` call is kept.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatSettingOffKept)
{
    InstanceFixture fixture;
    fixture.settings.SetStringConcat(false);
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {stringType, stringType}, stringType);
    auto* invocation = Invoke(
        "Concat",
        {WithType(new Syntax::IdentifierExpression("a"), stringType),
         WithType(new Syntax::IdentifierExpression("b"), stringType)});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A named argument anywhere in the call keeps the `String.Concat` call.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatNamedArgumentKept)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {stringType, stringType}, stringType);
    auto* named = new Syntax::NamedArgumentExpression(
        "arg", WithType(new Syntax::IdentifierExpression("b"), stringType));
    named->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(stringType));
    auto* invocation = Invoke(
        "Concat", {WithType(new Syntax::IdentifierExpression("a"), stringType),
                   named});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A call whose first two arguments are not string-typed keeps the `String.Concat`
// call (the `+` operator would not resolve to a string concatenation).
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatNonStringFirstTwoKept)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {intType, intType}, stringType);
    auto* invocation = Invoke(
        "Concat",
        {WithType(new Syntax::IdentifierExpression("a"), intType),
         WithType(new Syntax::IdentifierExpression("b"), intType)});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A non-last argument whose type is not known to have an effect-free ToString()
// keeps the `String.Concat` call (the evaluation order would change).
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatSideEffectingArgumentKept)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto objectType = fixture.FindType(TS::KnownTypeCode::Object);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {objectType, stringType}, stringType);
    auto* invocation = Invoke(
        "Concat",
        {WithType(new Syntax::IdentifierExpression("a"), objectType),
         WithType(new Syntax::IdentifierExpression("b"), stringType)});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A by-ref-like argument cannot be converted to object for use with `+`, so the
// `String.Concat` call is kept.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatByRefLikeArgumentKept)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto byRefType = std::make_shared<TS::ByReferenceType>(stringType);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {stringType, byRefType}, stringType);
    auto* invocation = Invoke(
        "Concat", {WithType(new Syntax::IdentifierExpression("a"), stringType),
                   WithType(new Syntax::IdentifierExpression("b"), byRefType)});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A nested `String.Concat` argument keeps the outer call (the compiler would
// wrongly flatten the nested call's evaluation order).
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatNestedConcatKept)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {stringType, stringType}, stringType);
    auto inner = MakeOperatorMethod(fixture, "Concat", stringType,
                                    {stringType, stringType}, stringType);
    auto* nested = new Syntax::IdentifierExpression("nested");
    nested->AddAnnotation(
        std::make_shared<Sem::InvocationResolveResult>(nullptr, inner.get()));
    auto* invocation = Invoke(
        "Concat", {WithType(new Syntax::IdentifierExpression("a"), stringType),
                   nested});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    EXPECT_EQ(result, invocation);
}

// A single `params`-array argument expands into its elements before the reduction,
// so `String.Concat(new[] { a, b })` becomes `a + b`.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatParamsArrayExpanded)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto arrayType = std::make_shared<TS::ArrayType>(stringType);
    auto method = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("Concat");
    method->SetIsStatic(true);
    method->SetDeclaringType(stringType);
    method->SetReturnType(stringType);
    std::vector<std::shared_ptr<const TS::IParameter>> parameters;
    parameters.push_back(std::make_shared<Impl::DefaultParameter>(
        arrayType, std::string("values"), nullptr,
        std::vector<const TS::IAttribute*>{}, TS::ReferenceKind::None, true));
    method->SetParameters(std::move(parameters));
    auto* a = WithType(new Syntax::IdentifierExpression("a"), stringType);
    auto* b = WithType(new Syntax::IdentifierExpression("b"), stringType);
    auto* initializer = new Syntax::ArrayInitializerExpression();
    initializer->Elements().Add(a);
    initializer->Elements().Add(b);
    auto* arrayCreate = new Syntax::ArrayCreateExpression();
    arrayCreate->Initializer(initializer);
    auto* invocation = Invoke("Concat", {arrayCreate});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Left(), a);
    EXPECT_EQ(binary->Right(), b);
}

// A redundant `int.ToString()` on the last argument is removed by the reduction
// (`String.Concat(s, n.ToString())` becomes `s + n`).
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, StringConcatRemovesRedundantToString)
{
    InstanceFixture fixture;
    auto stringType = fixture.FindType(TS::KnownTypeCode::String);
    auto intType = fixture.FindType(TS::KnownTypeCode::Int32);
    auto method = MakeOperatorMethod(fixture, "Concat", stringType,
                                     {stringType, stringType}, stringType);
    auto* s = WithType(new Syntax::IdentifierExpression("s"), stringType);
    auto* target = WithType(new Syntax::IdentifierExpression("n"), intType);
    auto* toStringCall = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(target, "ToString"));
    WithType(toStringCall, stringType);
    auto* invocation = Invoke("Concat", {s, toStringCall});
    ResolveInvocation(invocation, method.get());

    Syntax::Expression* result = RunOnExpression(fixture, invocation);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Right(), target);
}

// ---------------------------------------------------------------------------
// The methodof rewrite (`VisitCastExpression` over the
// `getMethodOrConstructorFromHandlePattern` built from the custom
// `TypePattern`/`LdTokenPattern` nodes). The token argument carries the method
// symbol through an `InvocationResolveResult` annotation, and the cast's type /
// `MethodBase` targets carry `TypeResolveResult`s for the pattern's type lookup.
// ---------------------------------------------------------------------------

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;

TS::ITypePtr NamedReflectionType(const std::string& namespaceName,
                                 const std::string& name) {
    return std::shared_ptr<class TS::UnknownType>(
        new class TS::UnknownType(namespaceName, name, 0));
}

// A `SimpleType` whose resolve-result annotation is a `TypeResolveResult` for the
// given (namespace, name).
Syntax::SimpleType* ResolvedType(const std::string& namespaceName,
                                 const std::string& name) {
    auto* type = new Syntax::SimpleType(name);
    type->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(NamedReflectionType(namespaceName, name)));
    return type;
}

// Builds the input methodof cast
// `(castTypeName)MethodBase.GetMethodFromHandle(ldtoken(token).MethodHandle
//      [, typeof(declaring).TypeHandle])`.
Syntax::CastExpression* BuildMethodofCast(const char* castTypeName,
                                          Syntax::Expression* tokenArgument,
                                          bool withDeclaringType) {
    auto* ldtoken = new Syntax::InvocationExpression(
        new Syntax::IdentifierExpression("ldtoken"));
    ldtoken->AddAnnotation(std::make_shared<CS::LdTokenAnnotation>());
    ldtoken->Arguments().Add(tokenArgument);
    auto* ldtokenMember = new Syntax::MemberReferenceExpression(
        ldtoken, std::string("MethodHandle"));
    auto* target = new Syntax::MemberReferenceExpression(
        new Syntax::TypeReferenceExpression(
            ResolvedType("System.Reflection", "MethodBase")),
        std::string("GetMethodFromHandle"));
    auto* invocation = new Syntax::InvocationExpression(target);
    invocation->Arguments().Add(ldtokenMember);
    if (withDeclaringType) {
        invocation->Arguments().Add(new Syntax::MemberReferenceExpression(
            new Syntax::TypeOfExpression(ResolvedType("Some", "Declaring")),
            std::string("TypeHandle")));
    }
    return new Syntax::CastExpression(
        ResolvedType("System.Reflection", castTypeName), invocation);
}

// Locates the `ldtoken(token).MethodHandle` member reference inside a methodof cast.
Syntax::MemberReferenceExpression* MethodofTokenMember(Syntax::CastExpression* cast) {
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(cast->Expression());
    if (invocation == nullptr)
        return nullptr;
    return dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Arguments()[0]);
}

} // namespace

// With a `typeof(declaringType).TypeHandle` argument the token argument is replaced
// by a `declaringType.Method(parameters)` invocation, and the cast by the
// `ldtoken(...).MethodHandle` member reference.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, MethodofWithDeclaringTypeRewrites)
{
    InstanceFixture fixture;
    auto method = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("Target");
    std::vector<std::shared_ptr<const TS::IParameter>> parameters;
    parameters.push_back(std::make_shared<Impl::DefaultParameter>(
        fixture.FindType(TS::KnownTypeCode::Int32), std::string("x")));
    method->SetParameters(std::move(parameters));
    auto* tokenArgument = new Syntax::IdentifierExpression("Token");
    tokenArgument->AddAnnotation(
        std::make_shared<Sem::InvocationResolveResult>(nullptr, method.get()));
    auto* cast = BuildMethodofCast("MethodInfo", tokenArgument, true);
    Syntax::MemberReferenceExpression* ldtokenMember = MethodofTokenMember(cast);

    Syntax::Expression* result = RunOnExpression(fixture, cast);

    EXPECT_EQ(result, ldtokenMember);
    auto* ldtoken = dynamic_cast<Syntax::InvocationExpression*>(ldtokenMember->Target());
    ASSERT_NE(ldtoken, nullptr);
    ASSERT_EQ(ldtoken->Arguments().Count(), 1);
    auto* newNode = dynamic_cast<Syntax::InvocationExpression*>(ldtoken->Arguments()[0]);
    ASSERT_NE(newNode, nullptr);
    auto* newMember = dynamic_cast<Syntax::MemberReferenceExpression*>(newNode->Target());
    ASSERT_NE(newMember, nullptr);
    EXPECT_EQ(newMember->MemberName(), "Target");
    EXPECT_NE(dynamic_cast<Syntax::TypeReferenceExpression*>(newMember->Target()), nullptr);
    ASSERT_EQ(newNode->Arguments().Count(), 1);
    auto* parameterReference =
        dynamic_cast<Syntax::TypeReferenceExpression*>(newNode->Arguments()[0]);
    ASSERT_NE(parameterReference, nullptr);
    EXPECT_NE(parameterReference->Type(), nullptr);
}

// Without the declaring-type argument only the cast is replaced; the token argument
// (which need not resolve to a method) stays in place.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, MethodofWithoutDeclaringTypeReplacesCastOnly)
{
    InstanceFixture fixture;
    auto* tokenArgument = new Syntax::IdentifierExpression("Token");
    auto* cast = BuildMethodofCast("ConstructorInfo", tokenArgument, false);
    Syntax::MemberReferenceExpression* ldtokenMember = MethodofTokenMember(cast);

    Syntax::Expression* result = RunOnExpression(fixture, cast);

    EXPECT_EQ(result, ldtokenMember);
    auto* ldtoken = dynamic_cast<Syntax::InvocationExpression*>(ldtokenMember->Target());
    ASSERT_NE(ldtoken, nullptr);
    ASSERT_EQ(ldtoken->Arguments().Count(), 1);
    EXPECT_EQ(ldtoken->Arguments()[0], tokenArgument);
}

// A cast whose type is neither `MethodInfo` nor `ConstructorInfo` does not match the
// methodof pattern and is kept.
TEST(ReplaceMethodCallsWithOperatorsInstanceTest, MethodofNonReflectionTypeKept)
{
    InstanceFixture fixture;
    auto* tokenArgument = new Syntax::IdentifierExpression("Token");
    auto* cast = BuildMethodofCast("Int32", tokenArgument, false);

    Syntax::Expression* result = RunOnExpression(fixture, cast);

    EXPECT_EQ(result, cast);
}
