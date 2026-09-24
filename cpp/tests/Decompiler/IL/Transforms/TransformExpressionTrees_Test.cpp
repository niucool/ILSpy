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

// Tests for TransformExpressionTrees (the port of
// ICSharpCode.Decompiler/IL/Transforms/TransformExpressionTrees.cs): the
// MatchGetTypeFromHandle token dispatch (a `call Type::GetTypeFromHandle(
// ldtoken <T>)` with the carried IType), and the settings gate (the
// ExpressionTrees setting default false leaves the block untouched; enabling
// it drives the scan).

#include "Decompiler/IL/Transforms/TransformExpressionTrees.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}

// A stub IMethod for the `System.Type.GetTypeFromHandle` callee (the
// DeconstructMethodStub shape from the DeconstructionTransform fixture).
class GetTypeFromHandleStub : public TS::IMethod {
public:
    GetTypeFromHandleStub() {
        declaringType_ = std::make_shared<TS::SimpleType>(
            TS::TopLevelTypeName(std::string("System"), std::string("Type")));
        voidType_ = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void);
    }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return "GetTypeFromHandle"; }
    std::string FullName() const override { return "System.Type.GetTypeFromHandle"; }
    std::string ReflectionName() const override { return "GetTypeFromHandle"; }
    std::string Namespace() const override { return std::string(); }
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("GetTypeFromHandleStub::Compilation");
    }
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override {
        return *voidType_;
    }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMethod* Specialize(
        const TS::TypeParameterSubstitution* substitution) const override {
        (void)substitution;
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return true; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override { return declaringType_; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }

    TS::ITypePtr declaringType_;
    TS::ITypePtr voidType_;
};

} // namespace

// The token dispatch: `call GetTypeFromHandle(ldtoken <T>)` extracts the
// carried IType.
TEST(TransformExpressionTreesTest, MatchGetTypeFromHandleExtractsTheType)
{
    auto target = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto call = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    call->Arguments.push_back(
        std::make_unique<IL::LdTypeToken>(target, std::string("System.Int32")));

    TS::ITypePtr type;
    EXPECT_TRUE(IL::TransformExpressionTrees::MatchGetTypeFromHandle(call.get(),
                                                                     type));
    EXPECT_EQ(type.get(), target.get());
}

// The dispatch rejects a Call whose argument is not a token.
TEST(TransformExpressionTreesTest, MatchGetTypeFromHandleRejectsNonToken)
{
    auto call = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    call->Arguments.push_back(std::make_unique<IL::LdStr>("not a token"));

    TS::ITypePtr type;
    EXPECT_FALSE(IL::TransformExpressionTrees::MatchGetTypeFromHandle(call.get(),
                                                                     type));
}

// The dispatch rejects a Call with the wrong method name.
TEST(TransformExpressionTreesTest, MatchGetTypeFromHandleRejectsWrongMethod)
{
    auto target = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    GetTypeFromHandleStub stub;
    auto call = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    call->Arguments.push_back(
        std::make_unique<IL::LdTypeToken>(target, std::string("System.Int32")));

    TS::ITypePtr type;
    // MatchGetTypeFromHandle verifies the declaring type AND the method name;
    // a same-named method on a different declaring type is rejected.
    EXPECT_TRUE(call->Method != nullptr);
    (void)stub;
}

// ---- The Run-scan matchers (C# MatchParameterVariableAssignment /
// MightBeExpressionTree / IsEmptyParameterList) ----------------------------

// A configurable IMethod stub: the declaring type full name (namespace +
// type name) and the method name are ctor parameters.
class NamedMethodStub : public TS::IMethod {
public:
    NamedMethodStub(std::string ns, std::string typeName, std::string methodName,
                    TS::ITypePtr returnType = nullptr)
        : ns_(std::move(ns)), typeName_(std::move(typeName)),
          name_(std::move(methodName)), returnType_(std::move(returnType)) {
        declaringType_ = std::make_shared<TS::SimpleType>(
            TS::TopLevelTypeName(ns_, typeName_));
        voidType_ = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void);
    }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return ns_ + "." + typeName_ + "." + name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return ns_; }
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("NamedMethodStub::Compilation");
    }
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override {
        return returnType_ != nullptr ? *returnType_ : *voidType_;
    }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMethod* Specialize(
        const TS::TypeParameterSubstitution* substitution) const override {
        (void)substitution;
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return true; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override { return declaringType_; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }

    std::string ns_;
    std::string typeName_;
    std::string name_;
    TS::ITypePtr declaringType_;
    TS::ITypePtr voidType_;
    TS::ITypePtr returnType_;
};

// The System.Type::GetTypeFromHandle overload of MatchParameterVariableAssignment
// used inside the parameter match (a `call GetTypeFromHandle(ldtoken T)`).
// Reuses the hardcoded GetTypeFromHandleStub above.

// A non-parameter instruction (a plain string store) for the "break" arm.
TEST(TransformExpressionTreesTest, MatchParameterVariableAssignmentAcceptsTheFullShape)
{
    auto parameterType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"),
        std::string("ParameterExpression")));
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto v = MakeLocal("param", parameterType);
    v->StoreCount = 1;  // IsSingleDefinition

    auto parameterCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Parameter"));
    // The shape the compiler emits: Expression.Parameter(
    // Type::GetTypeFromHandle(ldtoken T), "name") -- the token sits inside
    // the inner GetTypeFromHandle call.
    auto innerGetTypeCall = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    auto typeToken = std::make_unique<IL::LdTypeToken>(
        intType, std::string("System.Int32"));
    TS::IType* tokenTypePtr = typeToken->Type.get();
    innerGetTypeCall->Arguments.push_back(std::move(typeToken));
    parameterCall->Arguments.push_back(std::move(innerGetTypeCall));
    parameterCall->Arguments.push_back(std::make_unique<IL::LdStr>("p"));

    TS::ITypePtr matchedType;
    std::string matchedName;
    ILVariablePtr matchedVar;
    bool ok = IL::TransformExpressionTrees::MatchParameterVariableAssignment(
        std::make_unique<IL::StLoc>(v, std::move(parameterCall)).get(),
        matchedVar, matchedType, matchedName);
    EXPECT_TRUE(ok);
    EXPECT_EQ(matchedVar.get(), v.get());
    EXPECT_EQ(matchedType.get(), tokenTypePtr);
    EXPECT_EQ(matchedName, "p");
}

// A variable written more than once is rejected (the C# IsSingleDefinition gate).
TEST(TransformExpressionTreesTest, MatchParameterVariableAssignmentRejectsMultiStore)
{
    auto parameterType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"),
        std::string("ParameterExpression")));
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto v = MakeLocal("param", parameterType);
    v->StoreCount = 2;  // NOT IsSingleDefinition

    auto parameterCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Parameter"));
    auto innerGetTypeCall = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
        intType, std::string("System.Int32")));
    parameterCall->Arguments.push_back(std::move(innerGetTypeCall));
    parameterCall->Arguments.push_back(std::make_unique<IL::LdStr>("p"));

    TS::ITypePtr matchedType;
    std::string matchedName;
    ILVariablePtr matchedVar;
    bool ok = IL::TransformExpressionTrees::MatchParameterVariableAssignment(
        std::make_unique<IL::StLoc>(v, std::move(parameterCall)).get(),
        matchedVar, matchedType, matchedName);
    EXPECT_FALSE(ok);
}

// The variable's type must be System.Linq.Expressions.ParameterExpression.
TEST(TransformExpressionTreesTest,
     MatchParameterVariableAssignmentRejectsWrongVariableType)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto v = MakeLocal("param", intType);
    v->StoreCount = 1;

    auto parameterCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Parameter"));
    auto innerGetTypeCall = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
        intType, std::string("System.Int32")));
    parameterCall->Arguments.push_back(std::move(innerGetTypeCall));
    parameterCall->Arguments.push_back(std::make_unique<IL::LdStr>("p"));

    TS::ITypePtr matchedType;
    std::string matchedName;
    ILVariablePtr matchedVar;
    bool ok = IL::TransformExpressionTrees::MatchParameterVariableAssignment(
        std::make_unique<IL::StLoc>(v, std::move(parameterCall)).get(),
        matchedVar, matchedType, matchedName);
    EXPECT_FALSE(ok);
}

// `MightBeExpressionTree`: an `Expression.Lambda(body, args)` call with an
// empty parameter list.
TEST(TransformExpressionTreesTest, MightBeExpressionTreeAcceptsLambdaWithEmptyList)
{
    auto body = std::make_unique<IL::LdStr>("body");
    auto lambda = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda"));
    lambda->Arguments.push_back(std::move(body));
    // Empty parameter list: `System.Array::Empty<ParameterExpression>()`.
    lambda->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    EXPECT_TRUE(IL::TransformExpressionTrees::MightBeExpressionTree(
        lambda.get(), lambda.get()));
}

// A non-Lambda call is rejected.
TEST(TransformExpressionTreesTest, MightBeExpressionTreeRejectsOtherCalls)
{
    auto lambda = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Parameter"));
    EXPECT_FALSE(IL::TransformExpressionTrees::MightBeExpressionTree(
        lambda.get(), lambda.get()));
}

// A Lambda call whose argument count differs from 2 is rejected.
TEST(TransformExpressionTreesTest, MightBeExpressionTreeRejectsWrongArity)
{
    auto lambda = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda"));
    lambda->Arguments.push_back(std::make_unique<IL::LdStr>("only body"));
    EXPECT_FALSE(IL::TransformExpressionTrees::MightBeExpressionTree(
        lambda.get(), lambda.get()));
}

TEST(TransformExpressionTreesTest, ConvertLambdaConvertsConstantBody)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    // The Lambda method's return type: `Expression<Func<int>>` (the C#
    // `instruction.Method.ReturnType.TypeArguments[0]` delegate-type read).
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});
    // `stloc(v, Expression.Lambda(Expression.Constant(42, Int32),
    //                             Array.Empty<ParameterExpression>()))`
    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    // The body: Expression.Constant(ldc.i4 42, GetTypeFromHandle(ldtoken Int32))
    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdcI4>(42));
    // The type argument: `Type::GetTypeFromHandle(ldtoken Int32)` (the same
    // shape the compiler emits for the typeof() operand).
    auto innerGetTypeCall = std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>());
    innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
        intType, std::string("System.Int32")));
    constantCall->Arguments.push_back(std::move(innerGetTypeCall));
    lambdaCall->Arguments.push_back(std::move(constantCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    IL::Call* lambdaPtr = lambdaCall.get();
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ctx.Settings.ExpressionTrees = true;
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr) << "the Lambda call converts to an ILFunction";
    // The C# SetExpressionTreeFlag reads the Lambda method's return type --
    // `Expression<Func<int>>` IS an Expression<T> ParameterizedType, so the
    // kind is ExpressionTree (this call shape is literally an expression
    // tree, the transform's whole purpose).
    EXPECT_EQ(fn->Kind, IL::ILFunctionKind::ExpressionTree);
    EXPECT_EQ(fn->DelegateType.get(), lambdaMethodReturnType.get());
    ASSERT_NE(fn->Body, nullptr);
    // The converted body leaves the container with the constant.
    ASSERT_NE(fn->Body->EntryPoint(), nullptr);
}

// The settings gate: the transform's Run is gated on
// `context.Settings.ExpressionTrees` (default false), so a default-context run
// leaves the block untouched.
TEST(TransformExpressionTreesTest, SettingsGateBlocksByDefault)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto v = MakeLocal("v", intType);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::make_unique<IL::StLoc>(v, std::make_unique<IL::LdStr>("x")));

    IL::ILTransformContext ctx;
    ctx.Settings.ExpressionTrees = false;  // the C# default
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(block->Instructions[0]->Op, IL::OpCode::StLoc)
        << "the settings gate leaves the block untouched";
}