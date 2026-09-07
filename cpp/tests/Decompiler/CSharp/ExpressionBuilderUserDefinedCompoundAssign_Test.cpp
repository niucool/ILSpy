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

// The ExpressionBuilder VisitUserDefinedCompoundAssign suite: the assignment /
// unary / string-concat renders over real IMethods, the checked/unchecked
// target annotations, the LdObj dereference helper's arms, and the static
// support layer (CallBuilder::IsSpanBasedStringConcat,
// ReplaceMethodCallsWithOperators::HasCheckedEquivalent /
// RemoveRedundantToStringInConcat / ToStringIsKnownEffectFree /
// MatchToStringCallPattern, IL::MethodRequiresCopyForReadonlyLValue, and
// TypeUtils::IsCompatiblePointerTypeForMemoryAccess).

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/CallBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"

#include <gtest/gtest.h>

#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::ExpressionBuilder;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;

// The default settings bag (the C# `new DecompilerSettings()`).
DecompilerSettings DefaultSettings()
{
    return DecompilerSettings{};
}

// The BuilderFixture (the ExpressionBuilderSkeleton_Test shape).
struct BuilderFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;

    BuilderFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(DefaultSettings()), run(&settings, usingScope)
    {
    }

    // The root using scope over the compilation's global namespace.
    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope()
    {
        auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    ExpressionBuilder MakeBuilder()
    {
        return ExpressionBuilder(nullptr, compilation, FixtureContext(), &function_,
                                 &settings, &run);
    }

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(
            IL::VariableKind::Local,
            std::const_pointer_cast<TS::IType>(
                compilation.FindType(code).shared_from_this()));
        local->Name = name;
        return local;
    }

private:
    IL::ILFunction function_;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;

    const CSharp::TypeSystem::CSharpTypeResolveContext& FixtureContext()
    {
        if (!context_)
        {
            context_ = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
                compilation.MainModule(), usingScope);
        }
        return *context_;
    }
};

// The op-method fixture: a FakeMethod with the operator symbol kind, the name,
// the declaring type, the parameters, and the return type (the resolved-method
// the UserDefinedCompoundAssign node carries).
struct OperatorMethodFixture {
    std::shared_ptr<Impl::FakeMethod> method;
    std::vector<std::shared_ptr<const TS::IParameter>> parameters;

    OperatorMethodFixture(const TS::ICompilation& compilation, const char* name,
                          TS::ITypePtr declaringType, std::vector<TS::ITypePtr> paramTypes,
                          TS::ITypePtr returnType, bool isStatic = true)
    {
        method = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Operator);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        method->SetDeclaringType(std::move(declaringType));
        for (TS::ITypePtr& type : paramTypes) {
            parameters.push_back(
                std::make_shared<Impl::DefaultParameter>(type, std::string("arg")));
        }
        method->SetParameters(parameters);
        method->SetReturnType(std::move(returnType));
    }
};

// A string.Concat method fixture (SymbolKind::Method, not an operator).
std::shared_ptr<Impl::FakeMethod> MakeConcatMethod(
    const TS::ICompilation& compilation, TS::ITypePtr declaringType,
    std::vector<TS::ITypePtr> paramTypes, TS::ITypePtr returnType)
{
    auto method = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
    method->SetName("Concat");
    method->SetIsStatic(true);
    method->SetDeclaringType(std::move(declaringType));
    std::vector<std::shared_ptr<const TS::IParameter>> parameters;
    for (TS::ITypePtr& type : paramTypes) {
        parameters.push_back(
            std::make_shared<Impl::DefaultParameter>(type, std::string("arg")));
    }
    method->SetParameters(parameters);
    method->SetReturnType(std::move(returnType));
    return method;
}

} // namespace

// ---------------------------------------------------------------------------
// The support-layer statics
// ---------------------------------------------------------------------------

TEST(UserDefinedCompoundAssignSupportTest, IsSpanBasedStringConcatMatrix)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto charType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Char).shared_from_this());
    auto stringType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto spanDefinition = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::ReadOnlySpanOfT).shared_from_this());
    auto spanOfChar = std::make_shared<TS::ParameterizedType>(
        spanDefinition, std::vector<TS::ITypePtr>{charType});

    // The span-based string.Concat: static, System.String declaring type, every
    // parameter ReadOnlySpan<char>.
    OperatorMethodFixture spanConcat(compilation, "Concat", stringType,
                                     {spanOfChar, spanOfChar}, stringType);
    EXPECT_TRUE(CSharp::CallBuilder::IsSpanBasedStringConcat(*spanConcat.method));

    // The plain string.Concat(string, string): recognized by the IL node helper
    // but NOT span-based.
    OperatorMethodFixture plainConcat(compilation, "Concat", stringType,
                                      {stringType, stringType}, stringType);
    EXPECT_FALSE(CSharp::CallBuilder::IsSpanBasedStringConcat(*plainConcat.method));
    EXPECT_TRUE(IL::UserDefinedCompoundAssign::IsStringConcat(*plainConcat.method));

    // A non-Concat name, an instance method, and a non-String declaring type all
    // fail both checks.
    OperatorMethodFixture nonConcat(compilation, "Join", stringType,
                                    {spanOfChar, spanOfChar}, stringType);
    EXPECT_FALSE(CSharp::CallBuilder::IsSpanBasedStringConcat(*nonConcat.method));
    OperatorMethodFixture instanceConcat(compilation, "Concat", stringType,
                                         {spanOfChar, spanOfChar}, stringType, false);
    EXPECT_FALSE(CSharp::CallBuilder::IsSpanBasedStringConcat(*instanceConcat.method));
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    OperatorMethodFixture wrongDeclaring(compilation, "Concat", intType,
                                         {spanOfChar, spanOfChar}, intType);
    EXPECT_FALSE(CSharp::CallBuilder::IsSpanBasedStringConcat(*wrongDeclaring.method));
    EXPECT_FALSE(IL::UserDefinedCompoundAssign::IsStringConcat(*wrongDeclaring.method));

    // A span over a non-char element fails the element check.
    auto spanOfInt = std::make_shared<TS::ParameterizedType>(
        spanDefinition, std::vector<TS::ITypePtr>{intType});
    OperatorMethodFixture spanOfIntConcat(compilation, "Concat", stringType,
                                          {spanOfInt, spanOfInt}, stringType);
    EXPECT_FALSE(CSharp::CallBuilder::IsSpanBasedStringConcat(*spanOfIntConcat.method));
}

// A stub declaring type whose GetMethods returns a canned snapshot (the
// MemberEnumeration_Test EnumerationStubType precedent, minimized to the one
// family HasCheckedEquivalent walks).
class OperatorHostType : public TS::IType {
public:
    std::vector<const TS::IMethod*> methods;

    TS::TypeKind Kind() const override { return TS::TypeKind::Class; }
    std::string Name() const override { return "OperatorHost"; }
    std::string ReflectionName() const override { return "OperatorHost"; }
    int TypeParameterCount() const override { return 0; }
    std::vector<const TS::IMethod*> GetMethods(
        std::function<bool(const TS::IMethod*)> filter,
        TS::GetMemberOptions options) const override
    {
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

TEST(UserDefinedCompoundAssignSupportTest, HasCheckedEquivalentMatrix)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto stringType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto host = std::make_shared<OperatorHostType>();

    OperatorMethodFixture addition(compilation, "op_Addition", host,
                                   {stringType, stringType}, stringType);
    // Without a checked twin: no equivalent.
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
        *addition.method));

    // Declaring the op_CheckedAddition twin makes the check answer true.
    OperatorMethodFixture twin(compilation, "op_CheckedAddition", host,
                               {stringType, stringType}, stringType);
    host->methods.push_back(twin.method.get());
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
        *addition.method));

    // The op_ prefix rewrite: op_Decrement looks for op_CheckedDecrement (a
    // different twin than op_Addition's, and a non-matching twin must not leak
    // into the check).
    OperatorMethodFixture otherTwin(compilation, "op_CheckedSubtraction", host,
                                    {stringType, stringType}, stringType);
    host->methods.push_back(otherTwin.method.get());
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
        *addition.method));
    OperatorMethodFixture decrement(compilation, "op_Decrement", host,
                                    {stringType, stringType}, stringType);
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
        *decrement.method));
    OperatorMethodFixture decrementTwin(compilation, "op_CheckedDecrement", host,
                                        {stringType, stringType}, stringType);
    host->methods.push_back(decrementTwin.method.get());
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
        *decrement.method));
}

TEST(UserDefinedCompoundAssignSupportTest, MethodRequiresCopyForReadonlyLValueMatrix)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto stringType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    // A null method always requires the copy.
    EXPECT_TRUE(IL::MethodRequiresCopyForReadonlyLValue(nullptr));

    // A reference-type declaring type never requires the copy.
    Impl::FakeMethod stringMethod(compilation, TS::SymbolKind::Method);
    stringMethod.SetDeclaringType(stringType);
    EXPECT_FALSE(IL::MethodRequiresCopyForReadonlyLValue(&stringMethod));

    // A value-type declaring type requires the copy (the port's
    // ThisIsRefReadOnly reads false).
    Impl::FakeMethod intMethod(compilation, TS::SymbolKind::Method);
    intMethod.SetDeclaringType(intType);
    EXPECT_TRUE(IL::MethodRequiresCopyForReadonlyLValue(&intMethod));

    // The constrainedTo argument wins over the declaring type.
    EXPECT_FALSE(IL::MethodRequiresCopyForReadonlyLValue(&intMethod, stringType.get()));
}

TEST(UserDefinedCompoundAssignSupportTest, IsCompatiblePointerTypeForMemoryAccessMatrix)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    // Non-const locals: IsCompatiblePointerTypeForMemoryAccess takes non-const
    // refs (the D406 AcceptVisitor convention the memory-access comparison
    // composes).
    TS::IType& intType = const_cast<TS::IType&>(compilation.FindType(TS::KnownTypeCode::Int32));
    TS::IType& int64Type = const_cast<TS::IType&>(compilation.FindType(TS::KnownTypeCode::Int64));
    TS::IType& byteType = const_cast<TS::IType&>(compilation.FindType(TS::KnownTypeCode::Byte));
    TS::IType& sbyteType = const_cast<TS::IType&>(compilation.FindType(TS::KnownTypeCode::SByte));
    TS::IType& objectType = const_cast<TS::IType&>(compilation.FindType(TS::KnownTypeCode::Object));
    TS::IType& boolType = const_cast<TS::IType&>(compilation.FindType(TS::KnownTypeCode::Boolean));

    TS::PointerType intPointer(
        std::const_pointer_cast<TS::IType>(intType.shared_from_this()));
    TS::PointerType int64Pointer(
        std::const_pointer_cast<TS::IType>(int64Type.shared_from_this()));
    TS::PointerType bytePointer(
        std::const_pointer_cast<TS::IType>(byteType.shared_from_this()));
    TS::PointerType objectPointer(
        std::const_pointer_cast<TS::IType>(objectType.shared_from_this()));
    TS::ByReferenceType intByRef(
        std::const_pointer_cast<TS::IType>(intType.shared_from_this()));

    // int*/int and int&/int are compatible; int*/int64 is not (different sizes).
    EXPECT_TRUE(TS::IsCompatiblePointerTypeForMemoryAccess(intPointer, intType));
    EXPECT_TRUE(TS::IsCompatiblePointerTypeForMemoryAccess(intByRef, intType));
    EXPECT_FALSE(TS::IsCompatiblePointerTypeForMemoryAccess(int64Pointer, intType));

    // Same-stack-type same-size element types are compatible (byte*/sbyte).
    EXPECT_TRUE(TS::IsCompatiblePointerTypeForMemoryAccess(bytePointer, sbyteType));
    // Different stack types are not (byte*/int64: I4 vs I8). Note byte*/bool IS
    // compatible: bool loads as I4 with the same 1-byte size.
    EXPECT_FALSE(TS::IsCompatiblePointerTypeForMemoryAccess(bytePointer, int64Type));

    // Reference-type element types are compatible with reference accesses.
    EXPECT_TRUE(TS::IsCompatiblePointerTypeForMemoryAccess(objectPointer, objectType));

    // A non-pointer/non-byref type answers false.
    EXPECT_FALSE(TS::IsCompatiblePointerTypeForMemoryAccess(intType, intType));
}

// ---------------------------------------------------------------------------
// RemoveRedundantToStringInConcat / the ToString call pattern
// ---------------------------------------------------------------------------

// Builds the `target.ToString()` invocation the pattern matches, with the
// target's resolve result and the call's MemberResolveResult over a ToString
// method (GetSymbol reads the invocation's annotation).
struct ToStringCallFixture {
    Syntax::IdentifierExpression* identifier = nullptr;
    std::unique_ptr<Syntax::InvocationExpression> invocation;
    std::shared_ptr<Impl::FakeMethod> toStringMethod;
    bool nullConditional = false;
    std::unique_ptr<Syntax::UnaryOperatorExpression> outerInvocation;

    ToStringCallFixture(const TS::ICompilation& compilation, const TS::IType& targetType,
                        bool nullConditionalShape)
        : nullConditional(nullConditionalShape)
    {
        identifier = new Syntax::IdentifierExpression("value");
        identifier->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(TS::ITypePtr(
            const_cast<TS::IType&>(targetType).shared_from_this())));
        toStringMethod =
            std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
        toStringMethod->SetName("ToString");
        toStringMethod->SetDeclaringType(TS::ITypePtr(
            const_cast<TS::IType&>(targetType).shared_from_this()));
        const TS::IMember* memberAsMember =
            static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(
                toStringMethod.get()));

        auto memberResolveResult = [&targetType, &memberAsMember]() {
            return std::make_shared<Sem::MemberResolveResult>(
                std::make_shared<Sem::TypeResolveResult>(TS::ITypePtr(
                    const_cast<TS::IType&>(targetType).shared_from_this())),
                memberAsMember);
        };
        if (nullConditional) {
            // target?.ToString(): the NullConditional receiver wrapped in the
            // NullConditionalRewrap unary over the invocation.
            auto* nullCheck = new Syntax::UnaryOperatorExpression(
                identifier, Syntax::UnaryOperatorType::NullConditional);
            auto* memberRef2 =
                new Syntax::MemberReferenceExpression(nullCheck, std::string("ToString"));
            auto* innerInvocation = new Syntax::InvocationExpression(memberRef2);
            innerInvocation->AddAnnotation(memberResolveResult());
            outerInvocation = std::make_unique<Syntax::UnaryOperatorExpression>(
                innerInvocation, Syntax::UnaryOperatorType::NullConditionalRewrap);
        } else {
            auto* memberRef =
                new Syntax::MemberReferenceExpression(identifier, std::string("ToString"));
            invocation = std::make_unique<Syntax::InvocationExpression>(memberRef);
            invocation->AddAnnotation(memberResolveResult());
        }
    }

    // The expression handed to RemoveRedundantToStringInConcat.
    Syntax::Expression* Expression() const
    {
        if (nullConditional)
            return static_cast<Syntax::Expression*>(outerInvocation.get());
        return static_cast<Syntax::Expression*>(invocation.get());
    }
};

TEST(RemoveRedundantToStringTest, PatternMatchesBothShapes)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    const TS::IType& intType = compilation.FindType(TS::KnownTypeCode::Int32);
    ToStringCallFixture plain(compilation, intType, false);
    auto match =
        Transforms::ReplaceMethodCallsWithOperators::MatchToStringCallPattern(
            plain.Expression());
    ASSERT_TRUE(match.call != nullptr);
    ASSERT_TRUE(match.target != nullptr);
    EXPECT_EQ(match.target, plain.identifier);
    EXPECT_FALSE(match.nullConditional);

    ToStringCallFixture nullCond(compilation, intType, true);
    auto nullMatch =
        Transforms::ReplaceMethodCallsWithOperators::MatchToStringCallPattern(
            nullCond.Expression());
    ASSERT_TRUE(nullMatch.call != nullptr);
    ASSERT_TRUE(nullMatch.target != nullptr);
    EXPECT_EQ(nullMatch.target, nullCond.identifier);
    EXPECT_TRUE(nullMatch.nullConditional);

    // A non-ToString invocation does not match.
    auto* identifier = new Syntax::IdentifierExpression("other");
    Syntax::InvocationExpression other(identifier);
    auto noMatch =
        Transforms::ReplaceMethodCallsWithOperators::MatchToStringCallPattern(&other);
    EXPECT_TRUE(noMatch.call == nullptr);
}

TEST(RemoveRedundantToStringTest, EffectFreeValueTypeCallIsRemoved)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto stringType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    // The gate: a string-parameter-only Concat overload (the C#
    // `concatMethod.Parameters.All(IsStringParameter)` guard).
    OperatorMethodFixture concat(compilation, "Concat", intType,
                                 {stringType, stringType}, intType);
    ToStringCallFixture call(compilation, *intType, false);

    Syntax::Expression* result =
        Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
            call.Expression(), *concat.method, true);
    // The ToString() call is eliminated: the target identifier comes back.
    EXPECT_EQ(result, call.identifier);

    // A string-typed parameter overload over an effect-free value type removes
    // the call from the OTHER shape too.
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
                  call.Expression(), *concat.method, false),
              call.identifier);
}

TEST(RemoveRedundantToStringTest, NonStringConcatParameterKeepsTheCall)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto objectType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    // An object-parameter Concat overload: string.Concat() itself calls
    // ToString(), so the compiler-generated call must stay.
    OperatorMethodFixture concat(compilation, "Concat", intType, {intType, objectType},
                                 intType);
    ToStringCallFixture call(compilation, *intType, false);
    Syntax::Expression* result =
        Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
            call.Expression(), *concat.method, true);
    EXPECT_EQ(result, call.Expression());
}

TEST(RemoveRedundantToStringTest, ReferenceTypeCallIsKeptWithoutNullConditional)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto stringType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    OperatorMethodFixture concat(compilation, "Concat", stringType,
                                 {stringType, stringType}, stringType);

    // ToString() on a reference type might throw NRE; the builtin operator+ does
    // not, so the call stays.
    ToStringCallFixture plain(compilation, *stringType, false);
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
                  plain.Expression(), *concat.method, true),
              plain.Expression());

    // The null-conditional access cannot throw, so the call is removed.
    ToStringCallFixture nullCond(compilation, *stringType, true);
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
                  nullCond.Expression(), *concat.method, true),
              nullCond.identifier);
}

TEST(RemoveRedundantToStringTest, EffectFullTypeNeedsTheCopyCheck)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto doubleType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Double).shared_from_this());
    auto stringType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    // An effect-FREE type (double) has no ordering concern, so the call is
    // removed in BOTH positions.
    OperatorMethodFixture concat(compilation, "Concat", intType,
                                 {stringType, stringType}, intType);
    ToStringCallFixture doubleCall(compilation, *doubleType, false);
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
                  doubleCall.Expression(), *concat.method, false),
              doubleCall.identifier);
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
                  doubleCall.Expression(), *concat.method, true),
              doubleCall.identifier);

    // ToStringIsKnownEffectFree over the primitive + String table.
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
        *doubleType));
    EXPECT_TRUE(Transforms::ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
        *stringType));
    auto objectType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    EXPECT_FALSE(Transforms::ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
        *objectType));

    // The struct-mutation arm: an effect-FULL VALUE type (DateTime is not in the
    // effect-free table) whose ToString method may mutate the struct -- the call
    // stays even as the last argument.
    auto dateTimeType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::DateTime).shared_from_this());
    ToStringCallFixture dateTimeCall(compilation, *dateTimeType, false);
    EXPECT_EQ(Transforms::ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
                  dateTimeCall.Expression(), *concat.method, true),
              dateTimeCall.Expression());
}

// ---------------------------------------------------------------------------
// The VisitUserDefinedCompoundAssign drives
// ---------------------------------------------------------------------------

TEST(UserDefinedCompoundAssignTest, TwoParameterOperatorRendersAddAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    OperatorMethodFixture addition(fixture.compilation, "op_Addition", stringType,
                                   {stringType, stringType}, stringType);
    auto local = fixture.MakeLocal(TS::KnownTypeCode::String, "text");

    IL::UserDefinedCompoundAssign node(
        addition.method, IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdLoc>(local), IL::CompoundTargetKind::Property,
        std::make_unique<IL::LdStr>("!"));
    auto expr = builder.Translate(&node);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "text");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    const std::string* value = std::get_if<std::string>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "!");

    // The resolve result: the AddAssign OperatorResolveResult over the method's
    // return type, carrying the user-defined method and both operands.
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::AddAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.String");
    EXPECT_EQ(operatorRR->UserDefinedOperatorMethod(), addition.method.get());
    EXPECT_FALSE(operatorRR->IsLiftedOperator());
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(2));

    // The assignment node carries the UserDefinedCompoundAssign IL annotation.
    const auto ilInstructions = expr.ILInstructions();
    ASSERT_EQ(ilInstructions.size(), std::size_t(1));
    EXPECT_EQ(ilInstructions[0], &node);
    // No checked/unchecked annotation: op_Addition has no checked twin and the
    // name is not a checked operator.
    EXPECT_EQ(left->Annotation<Transforms::CheckedUncheckedAnnotation>(), nullptr);
}

TEST(UserDefinedCompoundAssignTest, UnaryIncrementRendersPostfixForm)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    OperatorMethodFixture increment(fixture.compilation, "op_Increment", intType,
                                    {intType}, intType);
    auto local = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");

    // The post-increment form: the EvaluatesToOldValue mode over an Address
    // target (the ldloca the compiler emits).
    IL::UserDefinedCompoundAssign node(
        increment.method, IL::CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<IL::LdLoca>(local), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::PostIncrement);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(identifier->Identifier(), "num");

    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::PostIncrementAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->UserDefinedOperatorMethod(), increment.method.get());
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(1));
    const auto ilInstructions = expr.ILInstructions();
    ASSERT_EQ(ilInstructions.size(), std::size_t(1));
    EXPECT_EQ(ilInstructions[0], &node);
}

TEST(UserDefinedCompoundAssignTest, UnaryIncrementNewValueRendersPrefixForm)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    OperatorMethodFixture increment(fixture.compilation, "op_Increment", intType,
                                    {intType}, intType);
    auto local = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");

    IL::UserDefinedCompoundAssign node(
        increment.method, IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdLoca>(local), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Increment);
}

TEST(UserDefinedCompoundAssignTest, CheckedOperatorAnnotatesTheTargetChecked)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    OperatorMethodFixture checkedIncrement(fixture.compilation, "op_CheckedIncrement",
                                           intType, {intType}, intType);
    auto local = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");

    IL::UserDefinedCompoundAssign node(
        checkedIncrement.method, IL::CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<IL::LdLoca>(local), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression());
    ASSERT_TRUE(identifier != nullptr);
    // The checked operator name annotates the TARGET expression checked.
    const auto* annotation =
        identifier->Annotation<Transforms::CheckedUncheckedAnnotation>();
    ASSERT_TRUE(annotation != nullptr);
    EXPECT_TRUE(annotation->IsChecked);
    EXPECT_FALSE(annotation->IsExplicit);
}

TEST(UserDefinedCompoundAssignTest, HasCheckedEquivalentTwinAnnotatesUnchecked)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto host = std::make_shared<OperatorHostType>();
    OperatorMethodFixture decrement(fixture.compilation, "op_Decrement", host, {intType},
                                    intType);
    // The op_CheckedDecrement twin on the declaring type.
    OperatorMethodFixture twin(fixture.compilation, "op_CheckedDecrement", host,
                               {intType}, intType);
    host->methods.push_back(twin.method.get());
    auto local = fixture.MakeLocal(TS::KnownTypeCode::Int32, "num");

    IL::UserDefinedCompoundAssign node(
        decrement.method, IL::CompoundEvalMode::EvaluatesToOldValue,
        std::make_unique<IL::LdLoca>(local), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdcI4>(1));
    auto expr = builder.Translate(&node);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression());
    ASSERT_TRUE(identifier != nullptr);
    // The unchecked-operator twin annotates the target UNCHECKED.
    const auto* annotation =
        identifier->Annotation<Transforms::CheckedUncheckedAnnotation>();
    ASSERT_TRUE(annotation != nullptr);
    EXPECT_FALSE(annotation->IsChecked);
    EXPECT_FALSE(annotation->IsExplicit);
}

// A test-local stand-in for the C# `AddressOf` unary instruction (the port has
// no dedicated node class yet): a Ref-typed unary over the wrapped computation.
class TestAddressOf final : public IL::UnaryInstruction {
public:
    explicit TestAddressOf(std::unique_ptr<IL::ILInstruction> argument)
        : IL::UnaryInstruction(IL::OpCode::AddressOf, std::move(argument))
    {
    }
    IL::StackType ResultType() const override { return IL::StackType::Ref; }
    void WriteTo(std::string& out) const override
    {
        out += "addressof(";
        if (Argument)
            Argument->WriteTo(out);
        out += ")";
    }
};

TEST(UserDefinedCompoundAssignTest, StringConcatRendersAddAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    // The plain string.Concat(string, string) shape: recognized by
    // IsStringConcat (not span-based), rendered as `target += value`.
    auto concat = MakeConcatMethod(fixture.compilation, stringType,
                                   {stringType, stringType}, stringType);
    auto local = fixture.MakeLocal(TS::KnownTypeCode::String, "text");

    IL::UserDefinedCompoundAssign node(
        concat, IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdLoc>(local), IL::CompoundTargetKind::Property,
        std::make_unique<IL::LdStr>("!"));
    auto expr = builder.Translate(&node);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "text");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    const std::string* value = std::get_if<std::string>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "!");
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::AddAssign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.String");
    EXPECT_EQ(operatorRR->UserDefinedOperatorMethod(), concat.get());
}

TEST(UserDefinedCompoundAssignTest, SpanBasedConcatConvertsTheValueToChar)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto charType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Char).shared_from_this());
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto spanDefinition = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::ReadOnlySpanOfT)
            .shared_from_this());
    auto spanOfChar = std::make_shared<TS::ParameterizedType>(
        spanDefinition, std::vector<TS::ITypePtr>{charType});

    auto concat = MakeConcatMethod(fixture.compilation, stringType,
                                   {spanOfChar, spanOfChar}, stringType);
    auto local = fixture.MakeLocal(TS::KnownTypeCode::Char, "ch");
    // The span-based value shape: `new string(...(char local))` -- a NewObj
    // whose single argument is an AddressOf over the char computation.
    auto newObj = std::make_unique<IL::Call>(std::string("System.String::Concat"));
    newObj->IsNewObj = true;
    newObj->AddArg(std::make_unique<TestAddressOf>(std::make_unique<IL::LdLoc>(local)));
    IL::UserDefinedCompoundAssign node(
        concat, IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdLoc>(local), IL::CompoundTargetKind::Property,
        std::move(newObj));
    auto expr = builder.Translate(&node);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    // The value renders as the char computation converted to char (the
    // span-based operand translation).
    auto* right = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Right());
    ASSERT_TRUE(right != nullptr);
    EXPECT_EQ(right->Identifier(), "ch");
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.String");
}

TEST(UserDefinedCompoundAssignTest, SeedStandInNodeThrowsLoudDeferral)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The seed string stand-in construction form (no resolved IMethod): the C#
    // Visit consumes the method unconditionally, so the loud deferral throws.
    IL::UserDefinedCompoundAssign node(std::string("Test::op_Addition"), nullptr,
                                       IL::StackType::I4,
                                       IL::CompoundEvalMode::EvaluatesToNewValue,
                                       std::make_unique<IL::LdcI4>(1),
                                       IL::CompoundTargetKind::Address,
                                       std::make_unique<IL::LdcI4>(2));
    EXPECT_THROW(builder.Translate(&node), std::logic_error);
}

TEST(UserDefinedCompoundAssignTest, ResultTypeDerivesFromTheResolvedMethod)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto int64Type = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    OperatorMethodFixture addition(compilation, "op_Addition", int64Type,
                                   {int64Type, int64Type}, int64Type);
    IL::UserDefinedCompoundAssign node(
        addition.method, IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdcI4>(1), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdcI4>(2));
    // The C# `ResultType => Method.ReturnType.GetStackType()`.
    EXPECT_EQ(node.ResultType(), IL::StackType::I8);
    // The dump stand-ins derive from the resolved method.
    EXPECT_EQ(node.MethodName, "System.Int64::op_Addition");
}

TEST(UserDefinedCompoundAssignTest, UnsafeReadArmRendersForIncompatibleManagedLoad)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    OperatorMethodFixture addition(fixture.compilation, "op_Addition", stringType,
                                   {stringType, stringType}, stringType);
    // A byte* local loaded by value: the pointer does not match the managed
    // string load type, and System.String is not unmanaged -> the Unsafe.Read
    // intrinsic arm.
    auto bytePointerType = std::make_shared<TS::PointerType>(
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Byte).shared_from_this()));
    auto pointerLocal = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            std::static_pointer_cast<TS::IType>(bytePointerType)));
    pointerLocal->Name = "ptr";

    IL::UserDefinedCompoundAssign node(
        addition.method, IL::CompoundEvalMode::EvaluatesToNewValue,
        std::make_unique<IL::LdLoc>(pointerLocal), IL::CompoundTargetKind::Address,
        std::make_unique<IL::LdStr>("!"));
    auto expr = builder.Translate(&node);

    // The target dereference renders as the Unsafe.Read<string>(ptr) intrinsic
    // (the incompatible-pointer arm over a managed load type), and the compound
    // assignment wraps it: `Unsafe.Read<string>(ptr) += "!"`.
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assignment != nullptr);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(assignment->Left());
    if (invocation == nullptr)
        GTEST_FAIL() << "actual node: "
                     << assignment->Left()->ToString(nullptr);
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Read");
    EXPECT_EQ(invocation->Arguments().Count(), std::size_t(1));
}

} // namespace ILSpy::Tests
