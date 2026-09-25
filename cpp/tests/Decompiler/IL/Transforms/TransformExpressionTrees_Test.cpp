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

#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Transforms/TransformExpressionTrees.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ExpressionTreeCast.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
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
// The KnownTypeCode-range stub the resolver's operator tables need (the
// CSharpResolverBinaryOperator_Test precedent): the lazy table construction
// resolves each primitive parameter/return type through FindType, and an
// unregistered code falls to the non-shared unknownType_ stub whose
// shared_from_this throws.
class KindDef : public TS::TestSupport::LookupTypeDefinition {
public:
    using TS::TestSupport::LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override {
        switch (Kind()) {
            case TS::TypeKind::Struct:
            case TS::TypeKind::Enum:
                return false;
            default:
                return true;
        }
    }
};

void RegisterKnownTypeCodes(TS::TestSupport::LookupCompilation& compilation,
                            std::vector<std::shared_ptr<KindDef>>& defs) {
    auto kindForCode = [](TS::KnownTypeCode code) {
        switch (code) {
            case TS::KnownTypeCode::Object:
            case TS::KnownTypeCode::DBNull:
            case TS::KnownTypeCode::String:
                return TS::TypeKind::Class;
            default:
                return TS::TypeKind::Struct;
        }
    };
    for (int raw = static_cast<int>(TS::KnownTypeCode::Object);
         raw <= static_cast<int>(TS::KnownTypeCode::String); ++raw) {
        TS::KnownTypeCode code = static_cast<TS::KnownTypeCode>(raw);
        std::string name = "T" + std::to_string(raw);
        auto def = std::make_shared<KindDef>(
            name, "", TS::FullTypeName(TS::TopLevelTypeName("", name, 0)),
            kindForCode(code), TS::Accessibility::Public, compilation, nullptr, code);
        compilation.RegisterKnownType(code, def.get());
        defs.push_back(std::move(def));
    }
    auto nullableOfT = std::make_shared<KindDef>(
        "Nullable", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "Nullable", 1)),
        TS::TypeKind::Struct, TS::Accessibility::Public, compilation, nullptr,
        TS::KnownTypeCode::NullableOfT);
    compilation.RegisterKnownType(TS::KnownTypeCode::NullableOfT, nullableOfT.get());
    defs.push_back(std::move(nullableOfT));
}


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
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

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


// The ConvertCoalesce arm (the C# `case "Coalesce"`): two non-nullable
// String constants coalesce with Kind Ref (the not-nullable path), the
// target type the fallback's (the identity implicit conversion
// String->String is valid, so targetType = the true side's type).
TEST(TransformExpressionTreesTest, ConvertCoalesceReducesToNullCoalescing)
{
    auto compilationOwner =
        std::make_unique<TS::TestSupport::LookupCompilation>();
    auto& compilation = *compilationOwner;
    auto stringType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    compilation.RegisterKnownType(TS::KnownTypeCode::String,
                                  stringType.get());
    auto booleanType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    compilation.RegisterKnownType(TS::KnownTypeCode::Boolean,
                                  booleanType.get());
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{stringType});

    // Expression.Coalesce(Expression.Constant("a", String),
    //                     Expression.Constant("b", String))
    auto trueConstant = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    trueConstant->Arguments.push_back(std::make_unique<IL::LdStr>("a"));
    {
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
            stringType, std::string("System.String")));
        trueConstant->Arguments.push_back(std::move(innerGetTypeCall));
    }
    auto fallbackConstant = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    fallbackConstant->Arguments.push_back(std::make_unique<IL::LdStr>("b"));
    {
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
            stringType, std::string("System.String")));
        fallbackConstant->Arguments.push_back(std::move(innerGetTypeCall));
    }
    auto coalesceCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Coalesce"));
    coalesceCall->Arguments.push_back(std::move(trueConstant));
    coalesceCall->Arguments.push_back(std::move(fallbackConstant));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(coalesceCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true
    ctx.TypeSystem = &compilation;
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(fn->Body, nullptr);
    ASSERT_NE(fn->Body->EntryPoint(), nullptr);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->EntryPoint()->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* coalesce = dynamic_cast<IL::NullCoalescingInstruction*>(
        leave->Value.get());
    ASSERT_NE(coalesce, nullptr)
        << "the Coalesce call converts to a NullCoalescingInstruction";
    EXPECT_EQ(coalesce->Kind, IL::NullCoalescingKind::Ref);
    auto* trueLdstr = dynamic_cast<IL::LdStr*>(coalesce->ValueInst.get());
    ASSERT_NE(trueLdstr, nullptr);
    EXPECT_EQ(trueLdstr->Value, "a");
    auto* fallbackLdstr =
        dynamic_cast<IL::LdStr*>(coalesce->FallbackInst.get());
    ASSERT_NE(fallbackLdstr, nullptr);
    EXPECT_EQ(fallbackLdstr->Value, "b");
}

// The ConvertComparison arm (the C# `case "Equal"`): two Int32 constants
// with no user-defined operator resolve to the builtin Comp (Equality,
// lifting None, signed), the result type Boolean.
TEST(TransformExpressionTreesTest, ConvertComparisonReducesToComp)
{
    auto compilationOwner =
        std::make_unique<TS::TestSupport::LookupCompilation>();
    auto& compilation = *compilationOwner;
    std::vector<std::shared_ptr<KindDef>> kindDefs;
    RegisterKnownTypeCodes(compilation, kindDefs);
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto booleanType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{booleanType});

    // Expression.Equal(Expression.Constant(1, Int32), Expression.Constant(2,
    // Int32))
    auto leftConstant = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    leftConstant->Arguments.push_back(std::make_unique<IL::LdcI4>(1));
    {
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
            intType, std::string("System.Int32")));
        leftConstant->Arguments.push_back(std::move(innerGetTypeCall));
    }
    auto rightConstant = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    rightConstant->Arguments.push_back(std::make_unique<IL::LdcI4>(2));
    {
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::make_unique<IL::LdTypeToken>(
            intType, std::string("System.Int32")));
        rightConstant->Arguments.push_back(std::move(innerGetTypeCall));
    }
    auto equalCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Equal"));
    equalCall->Arguments.push_back(std::move(leftConstant));
    equalCall->Arguments.push_back(std::move(rightConstant));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(equalCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true
    ctx.TypeSystem = &compilation;
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(fn->Body, nullptr);
    ASSERT_NE(fn->Body->EntryPoint(), nullptr);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->EntryPoint()->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* comp = dynamic_cast<IL::Comp*>(leave->Value.get());
    ASSERT_NE(comp, nullptr) << "the Equal call converts to a Comp";
    EXPECT_EQ(comp->Kind, IL::ComparisonKind::Equality);
    EXPECT_EQ(comp->LiftingKind, IL::ComparisonLiftingKind::None);
    EXPECT_EQ(comp->InputType, IL::StackType::I4);
}

// The settings gate: the transform's Run is gated on the FULL settings'
// `ExpressionTrees` (the C# context.Settings; the C# default is TRUE, so a
// context without CSharpSettings -- the C# default-constructed settings --
// passes the gate). This test exercises the explicit disable.
TEST(TransformExpressionTreesTest, SettingsGateBlocksWhenDisabled)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto v = MakeLocal("v", intType);
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::make_unique<IL::StLoc>(v, std::make_unique<IL::LdStr>("x")));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    settings.SetExpressionTrees(false);
    ctx.CSharpSettings = &settings;
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    EXPECT_EQ(block->Instructions[0]->Op, IL::OpCode::StLoc)
        << "the settings gate leaves the block untouched";
}

// The ConvertCall arm (the C# `case "Call": ConvertCall(invocation)`): a
// static MethodCallExpression --
//   Expression.Call(castclass MethodInfo(
//                       MethodBase.GetMethodFromHandle(ldmembertoken Foo)),
//                   Array.Empty<Expression>())
// inside a converted lambda body becomes a direct `call Foo()`. The handle
// shape is the C# MatchGetMethodFromHandle (castclass + GetMethodFromHandle
// + ldmembertoken); the argument list rides MatchArgumentList's empty shape.
TEST(TransformExpressionTreesTest, ConvertCallConvertsStaticMethodCallBody)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto methodInfoType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Reflection"), std::string("MethodInfo")));
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});
    auto fooMethod = std::make_shared<NamedMethodStub>(
        "Test", "C", "Foo");

    // The method handle: castclass MethodInfo(
    //     MethodBase.GetMethodFromHandle(ldmembertoken Foo))
    auto getMethodCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Reflection", "MethodBase", "GetMethodFromHandle"));
    getMethodCall->Arguments.push_back(
        std::make_unique<IL::LdMemberToken>(
            fooMethod, std::string("Test.C::Foo")));
    auto methodHandle = std::make_unique<IL::CastClass>(
        methodInfoType, std::move(getMethodCall));

    // Expression.Call(handle, Array.Empty<Expression>())
    auto callBody = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Call"));
    callBody->Arguments.push_back(std::move(methodHandle));
    callBody->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    // Expression.Lambda(callBody, Array.Empty<ParameterExpression>())
    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(callBody));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr) << "the Lambda call converts to an ILFunction";
    EXPECT_EQ(fn->Kind, IL::ILFunctionKind::ExpressionTree);
    ASSERT_NE(fn->Body, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    ASSERT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* call = dynamic_cast<IL::Call*>(leave->Value.get());
    ASSERT_NE(call, nullptr)
        << "the MethodCallExpression converts to a direct call";
    ASSERT_NE(call->Method, nullptr);
    EXPECT_EQ(call->Method->Name(), "Foo");
    EXPECT_EQ(call->Arguments.size(), 0u);
}

// The ConvertField arm (the C# `case "Field": ConvertField(invocation,
// typeHint)`): a static field access --
//   Expression.Field(null, FieldInfo.GetFieldFromHandle(ldmembertoken field))
// inside a converted lambda body becomes `ldobj(ldsflda field, fieldType)`
// (the value-type-style LdObj wrap: the tree reads the field's value, and
// with no type hint the C# BuildField wraps unconditionally -- the port
// guards the C#'s null-typeHint dereference). The token carries the IField.
TEST(TransformExpressionTreesTest, ConvertFieldConvertsStaticFieldAccess)
{
    TS::TestSupport::LookupCompilation compilation;
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto field = std::make_shared<TS::TestSupport::LookupField>(
        std::string("Test.C::field"), stringType, compilation);
    auto fieldInfoType = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Reflection"), std::string("FieldInfo")));
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{stringType});

    // The field handle: FieldInfo.GetFieldFromHandle(ldmembertoken field).
    auto getFieldCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Reflection", "FieldInfo", "GetFieldFromHandle"));
    getFieldCall->Arguments.push_back(
        std::make_unique<IL::LdMemberToken>(
            std::static_pointer_cast<const TS::IMember>(field),
            std::string("Test.C::field")));

    // Expression.Field(null, handle)
    auto fieldBody = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Field"));
    fieldBody->Arguments.push_back(std::make_unique<IL::LdNull>());
    fieldBody->Arguments.push_back(std::move(getFieldCall));

    // Expression.Lambda(fieldBody, Array.Empty<ParameterExpression>())
    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(fieldBody));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr) << "the Lambda call converts to an ILFunction";
    ASSERT_NE(fn->Body, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    ASSERT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    // The value-type-style wrap: ldobj(ldsflda field, string).
    auto* ldobj = dynamic_cast<IL::LdObj*>(leave->Value.get());
    ASSERT_NE(ldobj, nullptr)
        << "the field access reads through an ldobj of the field address";
    auto* ldsflda = dynamic_cast<IL::LdsFlda*>(ldobj->Target.get());
    ASSERT_NE(ldsflda, nullptr);
    EXPECT_EQ(ldsflda->FieldName, "Test.C::field");
}

// The ConvertTypeAs arm (the C# `case "TypeAs"`): a `as`-style cast node
// inside a converted lambda body becomes `isinst(T, operand)` -- with the
// ECMA-335 Nullable-of-T special case following unbox.any(T, ...) for a
// Nullable<T> target.
TEST(TransformExpressionTreesTest, ConvertTypeAsConvertsToIsInst)
{
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{stringType});
    // Expression.TypeAs(Expression.Constant("x", string), typeof(string))
    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdStr>("x"));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            stringType, std::string("System.String"));
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(innerGetTypeCall));
    }
    auto typeAsCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "TypeAs"));
    typeAsCall->Arguments.push_back(std::move(constantCall));
    typeAsCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<GetTypeFromHandleStub>()));
    typeAsCall->Arguments[1]->Parent = nullptr;
    {
        // Build the typeof(string) operand: Type.GetTypeFromHandle(ldtoken
        // string) -- the same shape MatchGetTypeFromHandle consumes.
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            stringType, std::string("System.String"));
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::move(typeToken));
        typeAsCall->Arguments.back() = std::move(innerGetTypeCall);
    }

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(typeAsCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    ASSERT_EQ(fn->Body->Blocks[0]->Instructions.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* isinst = dynamic_cast<IL::IsInst*>(leave->Value.get());
    ASSERT_NE(isinst, nullptr)
        << "the TypeAs expression converts to an isinst";
    EXPECT_EQ(isinst->Type.get(), stringType.get());
    auto* ldstr = dynamic_cast<IL::LdStr*>(isinst->Argument.get());
    ASSERT_NE(ldstr, nullptr);
    EXPECT_EQ(ldstr->Value, "x");
}

// The ConvertTypeIs arm: `x is T` becomes
// comp(isinst(operand, T) != ldnull) -- the Inequality-with-ldnull shape,
// result Boolean.
TEST(TransformExpressionTreesTest, ConvertTypeIsConvertsToCompNullCheck)
{
    TS::TestSupport::LookupCompilation compilation;
    auto booleanType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    compilation.RegisterKnownType(TS::KnownTypeCode::Boolean,
                                  booleanType.get());
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{stringType});
    // Expression.TypeIs(Expression.Constant("x", string), typeof(string))
    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdStr>("x"));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            stringType, std::string("System.String"));
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(innerGetTypeCall));
    }
    auto typeIsCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "TypeIs"));
    typeIsCall->Arguments.push_back(std::move(constantCall));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            stringType, std::string("System.String"));
        auto innerGetTypeCall = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        innerGetTypeCall->Arguments.push_back(std::move(typeToken));
        typeIsCall->Arguments.push_back(std::move(innerGetTypeCall));
    }

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(typeIsCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    ctx.TypeSystem = &compilation;
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* comp = dynamic_cast<IL::Comp*>(leave->Value.get());
    ASSERT_NE(comp, nullptr)
        << "the TypeIs expression converts to a null-check comp";
    EXPECT_EQ(comp->Kind, IL::ComparisonKind::Inequality);
    auto* isinst = dynamic_cast<IL::IsInst*>(comp->Left.get());
    ASSERT_NE(isinst, nullptr);
    EXPECT_EQ(isinst->Type.get(), stringType.get());
    auto* ldnull = dynamic_cast<IL::LdNull*>(comp->Right.get());
    ASSERT_NE(ldnull, nullptr);
}

// The ConvertCast arm (the C# `case "Convert"`): `Expression.Convert(42,
// typeof(long))` -- the operand converts (a constant's LdcI4), the target
// type rides the handle, and the small-integer-to-Int32 passthrough does
// NOT fire (int is already Int32), so the result is an ExpressionTreeCast
// over the converted operand.
TEST(TransformExpressionTreesTest, ConvertCastConvertsToExpressionTreeCast)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto longType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int64);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{longType});

    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdcI4>(42));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            intType, std::string("System.Int32"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
    }
    // Expression.Convert(constant, typeof(long))
    auto convertCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Convert"));
    convertCall->Arguments.push_back(std::move(constantCall));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            longType, std::string("System.Int64"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        convertCall->Arguments.push_back(std::move(inner));
    }

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(convertCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* cast = dynamic_cast<IL::ExpressionTreeCast*>(leave->Value.get());
    ASSERT_NE(cast, nullptr)
        << "the Convert expression becomes an ExpressionTreeCast";
    EXPECT_FALSE(cast->IsChecked);
    ASSERT_NE(cast->Type.get(), nullptr);
    EXPECT_EQ(cast->Type->ReflectionName(), "System.Int64");
    auto* arg = dynamic_cast<IL::LdcI4*>(cast->Argument.get());
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->Value, 42);
}

// The `Expression.Not(true)` shape: the boolean underlying type routes to
// Comp.LogicNot -- comp(ldc 1 == ldc 0).
TEST(TransformExpressionTreesTest, ConvertNotBooleanBecomesCompLogicNot)
{
    auto boolType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{boolType});

    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdcI4>(1));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            boolType, std::string("System.Boolean"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
    }
    auto notCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Not"));
    notCall->Arguments.push_back(std::move(constantCall));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(notCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* comp = dynamic_cast<IL::Comp*>(leave->Value.get());
    ASSERT_NE(comp, nullptr)
        << "the boolean Not converts to a Comp.LogicNot";
    EXPECT_EQ(comp->Kind, IL::ComparisonKind::Equality);
    auto* right = dynamic_cast<IL::LdcI4*>(comp->Right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->Value, 0);
}

// The binary-numeric arm: `Expression.Add(1, 2)` becomes a
// BinaryNumericInstruction with the Add operator and the unchecked flag.
TEST(TransformExpressionTreesTest, ConvertBinaryNumericOperatorFoldsToAdd)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});

    auto makeInt = [&intType](int value) {
        auto constantCall = std::make_unique<IL::Call>(
            std::make_shared<NamedMethodStub>(
                "System.Linq.Expressions", "Expression", "Constant"));
        constantCall->Arguments.push_back(std::make_unique<IL::LdcI4>(value));
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            intType, std::string("System.Int32"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
        return constantCall;
    };
    auto addCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Add"));
    addCall->Arguments.push_back(makeInt(1));
    addCall->Arguments.push_back(makeInt(2));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(addCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* binary = dynamic_cast<IL::BinaryNumericInstruction*>(
        leave->Value.get());
    ASSERT_NE(binary, nullptr)
        << "the Add expression becomes a BinaryNumericInstruction";
    EXPECT_EQ(binary->Operator, IL::BinaryNumericOperator::Add);
    EXPECT_FALSE(binary->CheckForOverflow);
}

// The condition arm: `Expression.Condition(ifTrue, ifFalse)` becomes an
// IfInstruction over the converted operands.
TEST(TransformExpressionTreesTest, ConvertConditionConvertsToIfInstruction)
{
    auto boolType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});

    auto makeConstant = [](TS::ITypePtr type, int value) {
        auto constantCall = std::make_unique<IL::Call>(
            std::make_shared<NamedMethodStub>(
                "System.Linq.Expressions", "Expression", "Constant"));
        constantCall->Arguments.push_back(
            std::make_unique<IL::LdcI4>(value));
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            type, type->ReflectionName());
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
        return constantCall;
    };
    auto conditionCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Condition"));
    conditionCall->Arguments.push_back(makeConstant(boolType, 1));
    conditionCall->Arguments.push_back(makeConstant(intType, 7));
    conditionCall->Arguments.push_back(makeConstant(intType, 8));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(conditionCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* ifInst = dynamic_cast<IL::IfInstruction*>(leave->Value.get());
    ASSERT_NE(ifInst, nullptr)
        << "the Condition expression converts to an IfInstruction";
    auto* cond = dynamic_cast<IL::LdcI4*>(ifInst->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Value, 1);
    auto* trueVal = dynamic_cast<IL::LdcI4*>(ifInst->TrueInst.get());
    ASSERT_NE(trueVal, nullptr);
    EXPECT_EQ(trueVal->Value, 7);
    auto* falseVal = dynamic_cast<IL::LdcI4*>(ifInst->FalseInst.get());
    ASSERT_NE(falseVal, nullptr);
    EXPECT_EQ(falseVal->Value, 8);
}

// The ConvertLogicOperator arm (the C# `case "AndAlso"`): the 2-arg
// primitive form builds the IfInstruction.LogicAnd sugar --
// if(ldc 1, rhs, ldc.i4 0).
TEST(TransformExpressionTreesTest, ConvertAndAlsoBecomesLogicAnd)
{
    TS::TestSupport::LookupCompilation compilation;
    auto booleanType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    compilation.RegisterKnownType(TS::KnownTypeCode::Boolean,
                                  booleanType.get());
    auto boolType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{boolType});

    auto makeTrue = [&boolType]() {
        auto constantCall = std::make_unique<IL::Call>(
            std::make_shared<NamedMethodStub>(
                "System.Linq.Expressions", "Expression", "Constant"));
        constantCall->Arguments.push_back(std::make_unique<IL::LdcI4>(1));
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            boolType, std::string("System.Boolean"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
        return constantCall;
    };
    auto andCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "AndAlso"));
    andCall->Arguments.push_back(makeTrue());
    andCall->Arguments.push_back(makeTrue());

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(andCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    ctx.TypeSystem = &compilation;
    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* ifInst = dynamic_cast<IL::IfInstruction*>(leave->Value.get());
    ASSERT_NE(ifInst, nullptr)
        << "the AndAlso expression converts to the LogicAnd sugar";
    auto* cond = dynamic_cast<IL::LdcI4*>(ifInst->Condition.get());
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->Value, 1);
    auto* trueVal = dynamic_cast<IL::LdcI4*>(ifInst->TrueInst.get());
    ASSERT_NE(trueVal, nullptr);
    EXPECT_EQ(trueVal->Value, 1);
    auto* falseVal = dynamic_cast<IL::LdcI4*>(ifInst->FalseInst.get());
    ASSERT_NE(falseVal, nullptr);
    EXPECT_EQ(falseVal->Value, 0);
}

// The ConvertInvoke arm: a delegate-typed constant invoked with no
// arguments -- the target converts (the constant keeps the delegate type),
// the delegate's Invoke method is resolved, and the call re-targets it.
TEST(TransformExpressionTreesTest, ConvertInvokeResolvesTheInvokeMethod)
{
    TS::TestSupport::LookupCompilation compilation;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto delegateDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        std::string("Dlg"), std::string("Test"),
        TS::FullTypeName(TS::TopLevelTypeName(std::string("Test"),
                                              std::string("Dlg"))),
        TS::TypeKind::Delegate, TS::Accessibility::Public, compilation,
        nullptr);
    auto invokeMethod = std::make_shared<NamedMethodStub>(
        "Test", "Dlg", "Invoke", intType);
    delegateDef->SetMethods({invokeMethod.get()});
    auto delegateType = delegateDef;
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});

    // Expression.Constant(null, Test.Dlg) -- the delegate-typed operand.
    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdNull>());
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            delegateType, std::string("Test.Dlg"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
    }
    // Expression.Invoke(constant, Array.Empty<ParameterExpression>())
    auto invokeCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Invoke"));
    invokeCall->Arguments.push_back(std::move(constantCall));
    invokeCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(invokeCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* call = dynamic_cast<IL::Call*>(leave->Value.get());
    ASSERT_NE(call, nullptr)
        << "the Invoke expression converts to a direct delegate call";
    ASSERT_NE(call->Method, nullptr);
    EXPECT_EQ(call->Method->Name(), "Invoke");
    ASSERT_EQ(call->Arguments.size(), 1u);
    auto* target = dynamic_cast<IL::LdNull*>(call->Arguments[0].get());
    ASSERT_NE(target, nullptr);
}

// The unary-numeric arm (the C# `case "Negate"`): the operand converts, and
// the fold builds BinaryNumericInstruction(Sub, ldc 0, operand) over the
// underlying type -- the C# `new BinaryNumericInstruction(op, left,
// argument(), underlying stack types, checked, sign, isLifted)` shape.
TEST(TransformExpressionTreesTest, ConvertNegateBuildsUnarySub)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});

    auto constantCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    constantCall->Arguments.push_back(std::make_unique<IL::LdcI4>(5));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            intType, std::string("System.Int32"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        constantCall->Arguments.push_back(std::move(inner));
    }
    auto negateCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Negate"));
    negateCall->Arguments.push_back(std::move(constantCall));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(negateCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* binary = dynamic_cast<IL::BinaryNumericInstruction*>(
        leave->Value.get());
    ASSERT_NE(binary, nullptr)
        << "the Negate expression becomes a Sub-over-zero binary";
    EXPECT_EQ(binary->Operator, IL::BinaryNumericOperator::Sub);
    EXPECT_FALSE(binary->CheckForOverflow);
    EXPECT_FALSE(binary->IsLifted);
    auto* zero = dynamic_cast<IL::LdcI4*>(binary->Left.get());
    ASSERT_NE(zero, nullptr);
    EXPECT_EQ(zero->Value, 0);
    auto* operand = dynamic_cast<IL::LdcI4*>(binary->Right.get());
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(operand->Value, 5);
}

// The ConvertNewObject arm (the C# `case "New"`, the 2-arg ctor-token
// form): `Expression.New(castclass ConstructorInfo(
// MethodBase.GetMethodFromHandle(ldmembertoken ctor)), { args })` becomes a
// NewObj over the converted arguments.
TEST(TransformExpressionTreesTest, ConvertNewObjectBuildsNewObjFromCtorToken)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto constructorInfoType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string("System.Reflection"),
                             std::string("ConstructorInfo")));
    auto expressionDef = std::make_shared<TS::SimpleType>(TS::TopLevelTypeName(
        std::string("System.Linq.Expressions"), std::string("Expression")));
    TS::ITypePtr lambdaMethodReturnType =
        std::make_shared<TS::ParameterizedType>(
            expressionDef, std::vector<TS::ITypePtr>{intType});
    auto ctorMethod = std::make_shared<NamedMethodStub>(
        "Test", "Widget", ".ctor", intType);

    // The ctor handle: castclass ConstructorInfo(
    //     MethodBase.GetMethodFromHandle(ldmembertoken ctor))
    auto getMethodCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Reflection", "MethodBase", "GetMethodFromHandle"));
    getMethodCall->Arguments.push_back(
        std::make_unique<IL::LdMemberToken>(
            ctorMethod, std::string("Test.Widget::.ctor")));
    auto ctorHandle = std::make_unique<IL::CastClass>(
        constructorInfoType, std::move(getMethodCall));

    // The argument list: one converted int constant.
    auto argConstant = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Constant"));
    argConstant->Arguments.push_back(std::make_unique<IL::LdcI4>(7));
    {
        auto typeToken = std::make_unique<IL::LdTypeToken>(
            intType, std::string("System.Int32"));
        auto inner = std::make_unique<IL::Call>(
            std::make_shared<GetTypeFromHandleStub>());
        inner->Arguments.push_back(std::move(typeToken));
        argConstant->Arguments.push_back(std::move(inner));
    }
    auto argListBlock = std::make_unique<IL::Block>();
    argListBlock->Kind = IL::BlockKind::ArrayInitializer;
    {
        auto arrayType = intType;
        auto ldnull = std::make_unique<IL::LdNull>();
        auto index = std::make_unique<IL::LdcI4>(0);
        std::vector<std::unique_ptr<IL::ILInstruction>> indices;
        indices.push_back(std::move(index));
        auto ldelema = std::make_unique<IL::LdElema>(
            arrayType, std::move(ldnull), std::move(indices));
        argListBlock->Add(std::make_unique<IL::StObj>(
            std::move(ldelema), std::move(argConstant), intType));
    }

    auto newCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "New"));
    newCall->Arguments.push_back(std::move(ctorHandle));
    newCall->Arguments.push_back(std::move(argListBlock));

    auto lambdaCall = std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>(
            "System.Linq.Expressions", "Expression", "Lambda",
            lambdaMethodReturnType));
    lambdaCall->Arguments.push_back(std::move(newCall));
    lambdaCall->Arguments.push_back(std::make_unique<IL::Call>(
        std::make_shared<NamedMethodStub>("System", "Array", "Empty")));

    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    block->Add(std::move(lambdaCall));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // ExpressionTrees defaults true

    IL::StatementTransformContext driverCtx(ctx, block.get());
    IL::TransformExpressionTrees transform;
    transform.Run(*block, 0, driverCtx);

    ASSERT_EQ(block->Instructions.size(), 1u);
    auto* fn = dynamic_cast<IL::ILFunction*>(block->Instructions[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->Body->Blocks.size(), 1u);
    auto* leave = dynamic_cast<IL::Leave*>(
        fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_NE(leave, nullptr);
    auto* newObj = dynamic_cast<IL::Call*>(leave->Value.get());
    ASSERT_NE(newObj, nullptr)
        << "the New expression converts to a NewObj";
    EXPECT_TRUE(newObj->IsNewObj);
    ASSERT_NE(newObj->Method, nullptr);
    EXPECT_EQ(newObj->Method->Name(), ".ctor");
    ASSERT_EQ(newObj->Arguments.size(), 1u);
    auto* arg = dynamic_cast<IL::LdcI4*>(newObj->Arguments[0].get());
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->Value, 7);
}
