// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver.ResolveUnaryOperator region (cpp/Decompiler/CSharp/
// Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 326-530, C#
// spec draft-v11 section 12.4.4 "Unary operator overload resolution").
//
// The load-bearing cruxes:
//  (a) the DYNAMIC arm: a dynamic operand short-circuits everything into the dynamic
//      OperatorResolveResult, and `await` of a dynamic operand builds the dynamic
//      AwaitResolveResult shape (GetAwaiter as a DynamicMemberResolveResult, its
//      invocation, every awaiter-pattern member null -- a dynamic await skips the
//      member-presence check);
//  (b) the non-overloadable arms: `*` dereferences a pointer to its ELEMENT type;
//      `&` addresses to a fresh PointerType; a non-pointer `*` / a pattern-wildcard
//      operator returns the UnknownError SINGLETON (pointer identity); the non-dynamic
//      `await` throws (the C# arm's dead pre-throw work is deferred);
//  (c) the USER-DEFINED operator resolution runs FIRST and an applicable user-defined
//      operator wins over the built-in tables; the primitive [Boolean..Decimal] operand
//      gate EXCLUDES the user-defined scan before it even calls GetMethods;
//  (d) the unary numeric promotion re-shapes the operand (char -> int wraps the operand
//      in a ConversionResolveResult; uint -> long under Minus) and the resolved result
//      type is the PROMOTED type;
//  (e) the built-in table selection (checked vs unchecked Minus by the resolver's
//      CheckForOverflow flag), the Increment/Decrement family's operand-kind gate
//      (numeric/enum/pointer/native-int accepted, everything else an error over the
//      OPERAND type), and the native-int early returns that skip the tables entirely;
//  (f) the constant folding: a compile-time-constant operand under a
//      CanEvaluateAtCompileTime operator folds through Invoke (unchecked -INT32_MIN
//      wraps; an ArithmeticException under the CHECKED resolver downgrades to an
//      ErrorResolveResult over the RESULT type), and the BitNot-on-enum arm folds as
//      (E)(~(U)x) -- the underlying-type FindType, the SELF-RECURSION over the unpacked
//      constant, and the unchecked ResolveCast back into the enum;
//  (g) the error composition: an inapplicable built-in table yields the ErrorResolveResult
//      over the BEST candidate's RESULT type (Not on int -> Boolean), and an inapplicable
//      user-defined operator is PREFERRED over the built-in error (the informative-error
//      path yields the invocation error result, not the builtin ErrorResolveResult);
//  (h) the lifted forms: a Nullable<int> operand resolves through the lifted int?
//      table entry (IsLiftedOperator, the Nullable<T> result type).

#include "Decompiler/CSharp/Resolver/AwaitResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicMemberResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::AwaitResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::DynamicMemberResolveResult;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::OperatorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ExpressionType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh shared compilation with every KnownTypeCode the operator tables resolve
// registered as a shared-managed LookupTypeDefinition (the CSharpOperatorsUserDefined
// precedent -- the lazy table construction resolves each primitive parameter/return type
// through FindType, and the lifted forms resolve NullableOfT), plus one REGISTERED
// instance per code the identity assertions target (the type-cache model: the FindType
// result IS the accessor instance).
struct Fixture {
    std::unique_ptr<LookupCompilation> compilation =
        std::make_unique<LookupCompilation>();
    std::vector<std::shared_ptr<LookupTypeDefinition>> defs;

    Fixture() {
        for (int raw = static_cast<int>(KnownTypeCode::Object);
             raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
            KnownTypeCode code = static_cast<KnownTypeCode>(raw);
            std::string name = "T" + std::to_string(raw);
            auto def = std::make_shared<LookupTypeDefinition>(
                name, "", FullTypeName(TopLevelTypeName("", name, 0)),
                TypeKind::Struct, Accessibility::Public, *compilation, nullptr, code);
            compilation->RegisterKnownType(code, def.get());
            defs.push_back(std::move(def));
        }
        auto nullableOfT = std::make_shared<LookupTypeDefinition>(
            "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
            TypeKind::Struct, Accessibility::Public, *compilation, nullptr,
            KnownTypeCode::NullableOfT);
        compilation->RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT.get());
        defs.push_back(std::move(nullableOfT));
    }
};

Fixture& Fix() {
    static Fixture fixture;
    return fixture;
}

LookupCompilation& Compilation() { return *Fix().compilation; }

// The REGISTERED definition for a known type code (the FindType identity target).
std::shared_ptr<LookupTypeDefinition> Def(KnownTypeCode code) {
    for (const auto& t : Fix().defs) {
        if (t->KnownTypeCode() == code)
            return t;
    }
    return nullptr;
}

std::shared_ptr<LookupTypeDefinition> IntDef() { return Def(KnownTypeCode::Int32); }
std::shared_ptr<LookupTypeDefinition> LongDef() { return Def(KnownTypeCode::Int64); }
std::shared_ptr<LookupTypeDefinition> CharDef() { return Def(KnownTypeCode::Char); }
std::shared_ptr<LookupTypeDefinition> UintDef() { return Def(KnownTypeCode::UInt32); }
std::shared_ptr<LookupTypeDefinition> BoolDef() { return Def(KnownTypeCode::Boolean); }
std::shared_ptr<LookupTypeDefinition> StringDef() { return Def(KnownTypeCode::String); }

// A fresh shared-managed resolver (the make_shared discipline).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// A fresh CHECKED resolver (the WithCheckForOverflow clone -- a distinct flag allocates
// the clone rather than returning this).
std::shared_ptr<CSharpResolver> MakeCheckedResolver() {
    return MakeResolver()->WithCheckForOverflow(true);
}

// A plain (non-constant) resolve result over a type.
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// A compile-time-constant resolve result over a type.
std::shared_ptr<ResolveResult> Const(ITypePtr type, std::any value) {
    return std::make_shared<ConstantResolveResult>(std::move(type), std::move(value));
}

// A `Nullable<T>` over an element (the Create composition over the registered
// `Nullable` definition).
ITypePtr MakeNullableOf(const ITypePtr& element) {
    return ILSpy::Decompiler::TypeSystem::Create(Compilation(), *element);
}

// The `SpecialType.Dynamic` shape.
std::shared_ptr<SpecialType> DynamicType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::Dynamic, true);
    return t;
}

// The C# `SpecialType.NInt` shape.
std::shared_ptr<SpecialType> NIntType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::NInt, false);
    return t;
}

// A `LookupTypeDefinition` whose `GetMethods(filter, options)` returns a configured
// list, applying the filter faithfully (the CSharpResolverUserDefinedOperators_Test
// stub), with the call count recording whether the user-defined scan ran at all (the
// primitive-gate-before-scan crux).
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    int GetMethodsCallCount() const { return getMethodsCallCount_; }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override {
        (void)options;
        ++getMethodsCallCount_;
        if (!filter)
            return methods_;
        std::vector<const IMethod*> r;
        for (const IMethod* m : methods_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const IMethod*> methods_;
    mutable int getMethodsCallCount_ = 0;
};

// A host with the given name and `KnownTypeCode` (None by default -- a custom type with
// `TypeCode::Empty` is outside the primitive gate).
std::shared_ptr<MethodHostType> MakeHost(const std::string& name,
                                         KnownTypeCode ktc = KnownTypeCode::None,
                                         TypeKind kind = TypeKind::Struct) {
    return std::make_shared<MethodHostType>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A configured `IParameter` over a configured type (the established TestParameter
// pattern).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type)
        : type_(std::move(type)) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return "p"; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ::ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ::ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override
    { return nullptr; }
    ::ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    ITypePtr type_;
};

// A configured operator method: a `LookupMethod` with the metadata name, the `IsOperator`
// flag, the return type, and the parameter types. The method, its parameters, and their
// types are kept alive for the program's lifetime (static keep vectors -- the candidate's
// Parameters() snapshot is non-owning, and the user-defined scan stores the originals as
// non-owning aliases).
std::shared_ptr<LookupMethod> MakeOperatorMethod(
    const std::string& name,
    ITypePtr returnType,
    std::vector<ITypePtr> paramTypes,
    bool isOperator = true)
{
    static std::vector<std::shared_ptr<LookupMethod>> methods;
    static std::vector<std::vector<std::shared_ptr<TestParameter>>> params;
    static std::vector<ITypePtr> types;
    types.insert(types.end(), paramTypes.begin(), paramTypes.end());
    types.push_back(returnType);
    auto m = std::make_shared<LookupMethod>(name, Compilation());
    m->SetIsOperator(isOperator);
    m->SetReturnType(std::move(returnType));
    std::vector<std::shared_ptr<TestParameter>> owned;
    std::vector<const IParameter*> raw;
    for (auto& t : paramTypes) {
        owned.push_back(std::make_shared<TestParameter>(t));
        raw.push_back(owned.back().get());
    }
    m->SetParameters(std::move(raw));
    params.push_back(std::move(owned));
    methods.push_back(m);
    return m;
}

// The dynamic_cast helper asserting the operator-result shape (the ASSERT-before-read
// discipline).
const OperatorResolveResult* AsOperator(const std::shared_ptr<ResolveResult>& r) {
    const auto* op = dynamic_cast<const OperatorResolveResult*>(r.get());
    EXPECT_NE(op, nullptr);
    return op;
}

// The dynamic_cast helper asserting the constant-result shape.
const ConstantResolveResult* AsConstant(const std::shared_ptr<ResolveResult>& r) {
    const auto* c = dynamic_cast<const ConstantResolveResult*>(r.get());
    EXPECT_NE(c, nullptr);
    return c;
}

// ---------------------------------------------------------------------------
// The dynamic arm
// ---------------------------------------------------------------------------

TEST(ResolveUnaryOperatorTest, DynamicOperandNonAwaitYieldsOperatorResultOverDynamic) {
    const auto input = Arg(DynamicType());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus, input);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(op->Type().Kind(), TypeKind::Dynamic);
    EXPECT_EQ(op->OperatorType(), ExpressionType::Negate);
    EXPECT_FALSE(op->IsLiftedOperator());
    ASSERT_EQ(op->Operands().size(), 1u);
    // The operand passes through un-wrapped (a dynamic operand never converts).
    EXPECT_EQ(op->Operands()[0].get(), input.get());
}

TEST(ResolveUnaryOperatorTest, DynamicOperandAwaitBuildsTheDynamicAwaitShape) {
    auto input = Arg(DynamicType());
    auto result = MakeResolver()->ResolveUnaryOperator(UnaryOperatorType::Await, input);
    const auto* await = dynamic_cast<const AwaitResolveResult*>(result.get());
    ASSERT_NE(await, nullptr);
    // A dynamic await skips the member-presence check: the awaiter type is dynamic and
    // every awaiter-pattern member is null, yet the await is NOT an error.
    EXPECT_EQ(await->Type().Kind(), TypeKind::Dynamic);
    EXPECT_EQ(await->AwaiterType().Kind(), TypeKind::Dynamic);
    EXPECT_EQ(await->IsCompletedProperty(), nullptr);
    EXPECT_EQ(await->OnCompletedMethod(), nullptr);
    EXPECT_EQ(await->GetResultMethod(), nullptr);
    EXPECT_FALSE(await->IsError());
    const auto* invocation =
        dynamic_cast<const DynamicInvocationResolveResult*>(await->GetAwaiterInvocation());
    ASSERT_NE(invocation, nullptr);
    const auto* member =
        dynamic_cast<const DynamicMemberResolveResult*>(invocation->Target());
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->Member(), "GetAwaiter");
}

// ---------------------------------------------------------------------------
// The non-overloadable arms
// ---------------------------------------------------------------------------

TEST(ResolveUnaryOperatorTest, DereferenceOnPointerYieldsTheElementResultType) {
    const ITypePtr element = IntDef();
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Dereference,
        Arg(std::make_shared<PointerType>(element)));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), element.get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Extension);
    EXPECT_FALSE(result->IsError());
}

TEST(ResolveUnaryOperatorTest, DereferenceOnNonPointerYieldsTheUnknownErrorSingleton) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Dereference, Arg(IntDef()));
    EXPECT_TRUE(result->IsError());
    // The C# `ErrorResult` static: the SAME UnknownError singleton instance.
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

TEST(ResolveUnaryOperatorTest, AddressOfYieldsAPointerToTheOperandType) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::AddressOf, Arg(IntDef()));
    const OperatorResolveResult* op = AsOperator(result);
    ASSERT_EQ(op->Type().Kind(), TypeKind::Pointer);
    const auto* pointer = dynamic_cast<const PointerType*>(&op->Type());
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(pointer->Element().get(), IntDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Extension);
}

TEST(ResolveUnaryOperatorTest, AwaitOnNonDynamicThrowsNotImplemented) {
    EXPECT_THROW(
        static_cast<void>(MakeResolver()->ResolveUnaryOperator(
            UnaryOperatorType::Await, Arg(IntDef()))),
        std::logic_error);
}

TEST(ResolveUnaryOperatorTest, PatternWildcardOperatorYieldsTheUnknownErrorSingleton) {
    // `Any` has no overloadable metadata name and no special-case arm -- the C# default
    // returns the UnknownError singleton.
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Any, Arg(IntDef()));
    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

// ---------------------------------------------------------------------------
// The user-defined operator resolution
// ---------------------------------------------------------------------------

TEST(ResolveUnaryOperatorTest, ApplicableUserDefinedOperatorWinsOverTheBuiltinTables) {
    // A custom struct host with `op_OnesComplement(S) -> S`: the operand over S converts
    // by identity, so the user-defined resolution finds an applicable candidate and the
    // result carries the user-defined method (the built-in tables never run).
    const auto host = MakeHost("HostS");
    const auto op = MakeOperatorMethod("op_OnesComplement", host, {host});
    host->SetMethods({op.get()});
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::BitNot, Arg(host));
    const OperatorResolveResult* orr = AsOperator(result);
    EXPECT_EQ(orr->UserDefinedOperatorMethod(), op.get());
    EXPECT_EQ(&orr->Type(), host.get());
    EXPECT_EQ(orr->OperatorType(), ExpressionType::OnesComplement);
    EXPECT_FALSE(orr->IsLiftedOperator());
}

TEST(ResolveUnaryOperatorTest, PrimitiveOperandGateSkipsTheUserDefinedScan) {
    // The unregistered Int32-carrying host has a MATCHING op_UnaryNegation in its method
    // table, but the primitive gate excludes primitive operand types BEFORE the scan (the
    // .NET framework's built-in operators must not be used as user-defined operators) --
    // the builtin Minus table resolves the operand instead: the result carries NO
    // user-defined method and the result type is the REGISTERED Int32 instance the table
    // entries resolve through (NOT the operand's own unregistered instance -- had the
    // user-defined operator won, the result type would be the host's own return type and
    // the method would be non-null).
    const auto host = MakeHost("HostInt", KnownTypeCode::Int32);
    const auto op = MakeOperatorMethod("op_UnaryNegation", host, {host});
    host->SetMethods({op.get()});
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus, Arg(host));
    const OperatorResolveResult* orr = AsOperator(result);
    EXPECT_EQ(orr->UserDefinedOperatorMethod(), nullptr);
    EXPECT_EQ(&orr->Type(), IntDef().get());
    EXPECT_EQ(orr->OperatorType(), ExpressionType::Negate);
    EXPECT_EQ(host->GetMethodsCallCount(), 0);
    // The operand converts to the builtin int entry's parameter type (the implicit
    // int-to-int numeric conversion between the two distinct Int32-carrying instances).
    ASSERT_EQ(orr->Operands().size(), 1u);
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(orr->Operands()[0].get()),
              nullptr);
}

// ---------------------------------------------------------------------------
// The unary numeric promotion and the Increment/Decrement family
// ---------------------------------------------------------------------------

TEST(ResolveUnaryOperatorTest, UnaryPlusOnCharPromotesToIntAndWrapsTheOperand) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Plus, Arg(CharDef()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), IntDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::UnaryPlus);
    ASSERT_EQ(op->Operands().size(), 1u);
    // The promoted operand is wrapped in the char -> int implicit-numeric conversion.
    const auto* converted =
        dynamic_cast<const ConversionResolveResult*>(op->Operands()[0].get());
    ASSERT_NE(converted, nullptr);
}

TEST(ResolveUnaryOperatorTest, MinusOnUintPromotesToLong) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus, Arg(UintDef()));
    const OperatorResolveResult* op = AsOperator(result);
    // The C# unary numeric promotion: uint -> long under Minus.
    EXPECT_EQ(&op->Type(), LongDef().get());
    ASSERT_EQ(op->Operands().size(), 1u);
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(op->Operands()[0].get()),
              nullptr);
}

TEST(ResolveUnaryOperatorTest, IncrementOnIntYieldsOperatorOverTheOperandType) {
    const auto input = Arg(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Increment, input);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), IntDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::PreIncrementAssign);
    // No promotion fires for int, so the operand passes through identity-unwrapped.
    ASSERT_EQ(op->Operands().size(), 1u);
    EXPECT_EQ(op->Operands()[0].get(), input.get());
}

TEST(ResolveUnaryOperatorTest, IncrementOnStringYieldsErrorOverTheStringType) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::PostIncrement, Arg(StringDef()));
    const auto* error = dynamic_cast<const ErrorResolveResult*>(result.get());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(&error->Type(), StringDef().get());
}

TEST(ResolveUnaryOperatorTest, IncrementOnEnumYieldsOperatorOverTheEnumType) {
    const auto enumDef = std::make_shared<LookupTypeDefinition>(
        "E", "", FullTypeName(TopLevelTypeName("", "E", 0)),
        TypeKind::Enum, Accessibility::Public, Compilation(), nullptr);
    enumDef->SetEnumUnderlyingType(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Increment, Arg(enumDef));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), enumDef.get());
}

TEST(ResolveUnaryOperatorTest, IncrementOnPointerYieldsOperatorOverThePointerType) {
    const ITypePtr pointer = std::make_shared<PointerType>(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Decrement, Arg(pointer));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), pointer.get());
}

TEST(ResolveUnaryOperatorTest, IncrementOnNativeIntYieldsOperatorOverTheNativeIntType) {
    const auto nint = NIntType();
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Increment, Arg(nint));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), nint.get());
}

TEST(ResolveUnaryOperatorTest, MinusOnNativeIntShortCircuitsBeforeTheTables) {
    // The native-int arm returns the operand's own type directly -- the builtin Minus
    // tables (whose parameter types are the primitive codes) never apply.
    const auto nint = NIntType();
    const auto input = Arg(nint);
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus, input);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), nint.get());
    ASSERT_EQ(op->Operands().size(), 1u);
    EXPECT_EQ(op->Operands()[0].get(), input.get());
}

// ---------------------------------------------------------------------------
// The constant folding
// ---------------------------------------------------------------------------

TEST(ResolveUnaryOperatorTest, UncheckedMinusConstantFoldsThroughTheOperatorInvoke) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus, Const(IntDef(), std::int32_t(-5)));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), IntDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(c->ConstantValue()), 5);
}

TEST(ResolveUnaryOperatorTest, CheckedMinusOnIntMinConstantYieldsErrorOverTheResultType) {
    // The CHECKED resolver selects the checked Minus table; the int entry's `Invoke`
    // throws OverflowException at the INT32_MIN boundary, and the catch arm downgrades
    // to an ErrorResolveResult over the operator's RESULT type.
    auto result = MakeCheckedResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus,
        Const(IntDef(), std::numeric_limits<std::int32_t>::min()));
    const auto* error = dynamic_cast<const ErrorResolveResult*>(result.get());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(&error->Type(), IntDef().get());
}

TEST(ResolveUnaryOperatorTest, UncheckedMinusOnIntMinConstantWraps) {
    // The unchecked table's int entry negates through the two's-complement wrap:
    // unchecked(-INT32_MIN) == INT32_MIN.
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Minus,
        Const(IntDef(), std::numeric_limits<std::int32_t>::min()));
    const ConstantResolveResult* c = AsConstant(result);
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(c->ConstantValue()),
              std::numeric_limits<std::int32_t>::min());
}

TEST(ResolveUnaryOperatorTest, NotOnBoolConstantFolds) {
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Not, Const(BoolDef(), true));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), BoolDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<bool>(c->ConstantValue()), false);
}

TEST(ResolveUnaryOperatorTest, BitNotOnEnumConstantFoldsThroughTheUnderlyingRecursion) {
    // The flagship enum fold: (E)(~(U)x) -- the constant 5 over E resolves through the
    // underlying int (FindType over the boxed value's runtime TypeCode), folds ~5 = -6
    // over int, then the unchecked ResolveCast back into E.
    const auto enumDef = std::make_shared<LookupTypeDefinition>(
        "E", "", FullTypeName(TopLevelTypeName("", "E", 0)),
        TypeKind::Enum, Accessibility::Public, Compilation(), nullptr);
    enumDef->SetEnumUnderlyingType(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::BitNot, Const(enumDef, std::int32_t(5)));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), enumDef.get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(c->ConstantValue()), -6);
}

TEST(ResolveUnaryOperatorTest, BitNotOnEnumNonConstantYieldsOperatorOverTheEnumType) {
    const auto enumDef = std::make_shared<LookupTypeDefinition>(
        "E", "", FullTypeName(TopLevelTypeName("", "E", 0)),
        TypeKind::Enum, Accessibility::Public, Compilation(), nullptr);
    enumDef->SetEnumUnderlyingType(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::BitNot, Arg(enumDef));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), enumDef.get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::OnesComplement);
    EXPECT_FALSE(op->IsLiftedOperator());
}

TEST(ResolveUnaryOperatorTest, BitNotOnIntNonConstantYieldsOperatorWithIdentityOperand) {
    const auto input = Arg(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::BitNot, input);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), IntDef().get());
    ASSERT_EQ(op->Operands().size(), 1u);
    EXPECT_EQ(op->Operands()[0].get(), input.get());
}

TEST(ResolveUnaryOperatorTest, BitNotOnNullableIntResolvesTheLiftedOperator) {
    const ITypePtr nullableInt = MakeNullableOf(IntDef());
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::BitNot, Arg(nullableInt));
    const OperatorResolveResult* op = AsOperator(result);
    // The lifted int? table entry wins: the result type is Nullable<T> and the result
    // is marked lifted (the ILiftedOperator cross-cast).
    EXPECT_TRUE(IsNullable(op->Type()));
    EXPECT_TRUE(op->IsLiftedOperator());
}

// ---------------------------------------------------------------------------
// The error composition
// ---------------------------------------------------------------------------

TEST(ResolveUnaryOperatorTest, NotOnIntYieldsErrorOverTheBooleanResultType) {
    // The not table holds only the bool entry (plus its lift); an int operand matches
    // neither, so the best candidate errors and the error result carries the BEST
    // candidate's RESULT type (Boolean), not the operand's type.
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::Not, Arg(IntDef()));
    const auto* error = dynamic_cast<const ErrorResolveResult*>(result.get());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(&error->Type(), BoolDef().get());
}

TEST(ResolveUnaryOperatorTest, InapplicableUserDefinedOperatorPreferredOverBuiltinError) {
    // The host carries an op_OnesComplement taking a DIFFERENT type, so the user-defined
    // candidate exists but is inapplicable; the builtin BitwiseComplement table also
    // finds nothing applicable for the host. The user-defined error is PREFERRED (the
    // more informative error): the result is the invocation error result, not the
    // builtin ErrorResolveResult over the best builtin's return type.
    const auto host = MakeHost("HostH");
    const auto other = MakeHost("HostOther");
    const auto op = MakeOperatorMethod("op_OnesComplement", other, {other});
    host->SetMethods({op.get()});
    auto result = MakeResolver()->ResolveUnaryOperator(
        UnaryOperatorType::BitNot, Arg(host));
    EXPECT_TRUE(result->IsError());
    const auto* invocation =
        dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    // The user-defined operator method is the resolved (inapplicable) candidate.
    EXPECT_EQ(invocation->Member(), op.get());
}

} // namespace
