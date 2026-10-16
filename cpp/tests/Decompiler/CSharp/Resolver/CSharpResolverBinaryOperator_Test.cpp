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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver.ResolveBinaryOperator region + the enum-handler trio
// (cpp/Decompiler/CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of
// CSharpResolver.cs lines 594-948 + 995-1053, C# 4.0 spec section 7.3.4 "Binary
// operator overload resolution").
//
// The load-bearing cruxes:
//  (a) the DYNAMIC arm: either operand dynamic converts BOTH operands to dynamic and
//      yields the dynamic OperatorResolveResult;
//  (b) the overloadable-name gate: `??` delegates to ResolveNullCoalescingOperator
//      (the operands pass through with pointer identity); `&&`/`||` fall through to
//      their bitwise names and resolve through the LOGICAL tables (bool && bool ->
//      Boolean, AndAlso); a nameless kind without a special arm (Range) returns the
//      UnknownError SINGLETON (pointer identity);
//  (c) the USER-DEFINED operator resolution runs FIRST and an applicable user-defined
//      operator wins over the built-in tables; the same-typed operands scan BOTH
//      operand types -- without the C# HashSet dedup the candidate would be added
//      TWICE and the resolution would turn ambiguous (the error path); an INAPPLICABLE
//      user-defined operator is PREFERRED over the built-in error (the informative
//      error path yields the invocation error result, not the builtin
//      ErrorResolveResult);
//  (d) the binary numeric promotion and the per-operator built-in table selection:
//      int + int through the int entry; a constant pair FOLDS through Invoke (an
//      ArithmeticException -- the checked overflow, the integer divide by zero --
//      downgrades to an ErrorResolveResult over the result type); a widening pair
//      (int + long) resolves the long entry; a nullable operand LIFTS the entry
//      (Nullable<int> + int -> Nullable<int>, IsLiftedOperator);
//  (e) the shift special case: each operand is unary-promoted INDEPENDENTLY (char <<
//      int promotes the char operand to int); `null << null` produces int? (the
//      lifted shift entry is the only applicable one for the null literals);
//  (f) the enum arms: E + U / E - E / E & E / E < E delegate to the handler trio --
//      a both-constant pair folds through the underlying type (E + 1 + 2 -> the
//      enum constant 3; E - E -> the UNDERLYING int constant; E < E -> the Boolean
//      constant), a non-constant pair yields the predefined OperatorResolveResult
//      over the enum (bitwise/addition) / the underlying (subtraction) / Boolean
//      (comparison, isNullable-propagating) type;
//  (g) the delegate combination (D + D keeps the delegate type), the pointer
//      comparison (int* == int* -> Boolean, operands unwrapped), the reference
//      comparison (string == string -> the operands convert to the Object
//      parameters), the null-literal comparison (null == string -> Boolean, the
//      operands keep pointer identity), the native-integer arms (nint == nint ->
//      Boolean; mixing nint + nuint is a binding error over the promoted type), and
//      the inapplicable-builtin error composition (int && int -> the ErrorResolveResult
//      over the best candidate's Boolean result type).

#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
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
#include <optional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::OperatorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ExpressionType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
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

// A LookupTypeDefinition whose IsReferenceType is DEFINITE per its kind (the real
// metadata model: a struct/enum is a value type, everything else a reference type).
// The plain stub's indeterminate nullopt would fail the reference-conversion guards
// the reference-comparison arm and the operand conversions depend on (the
// CSharpResolverNumericPromotion ValueTypeDef precedent, applied to the registered
// type-cache instances).
class KindDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override {
        switch (Kind()) {
            case TypeKind::Struct:
            case TypeKind::Enum:
                return false;
            default:
                return true;
        }
    }
};

// A fresh shared compilation with every KnownTypeCode the operator tables resolve
// registered as a shared-managed KindDef (the CSharpResolverUnaryOperator precedent --
// the lazy table construction resolves each primitive parameter/return type through
// FindType, and the lifted forms resolve NullableOfT), plus one REGISTERED instance per
// code the identity assertions target (the type-cache model: the FindType result IS
// the accessor instance).
struct Fixture {
    std::unique_ptr<LookupCompilation> compilation =
        std::make_unique<LookupCompilation>();
    std::vector<std::shared_ptr<KindDef>> defs;

    // The real .NET kind per code: Object/DBNull/String are classes, the primitives and
    // DateTime are structs (the fixture the reference-conversion guards need -- a
    // struct-kind String would report a definite-false IsReferenceType and fail every
    // reference-flavored path).
    static TypeKind KindForCode(KnownTypeCode code) {
        switch (code) {
            case KnownTypeCode::Object:
            case KnownTypeCode::DBNull:
            case KnownTypeCode::String:
                return TypeKind::Class;
            default:
                return TypeKind::Struct;
        }
    }

    Fixture() {
        for (int raw = static_cast<int>(KnownTypeCode::Object);
             raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
            KnownTypeCode code = static_cast<KnownTypeCode>(raw);
            std::string name = "T" + std::to_string(raw);
            auto def = std::make_shared<KindDef>(
                name, "", FullTypeName(TopLevelTypeName("", name, 0)),
                KindForCode(code), Accessibility::Public, *compilation, nullptr, code);
            compilation->RegisterKnownType(code, def.get());
            defs.push_back(std::move(def));
        }
        auto nullableOfT = std::make_shared<KindDef>(
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
std::shared_ptr<KindDef> Def(KnownTypeCode code) {
    for (const auto& t : Fix().defs) {
        if (t->KnownTypeCode() == code)
            return t;
    }
    return nullptr;
}

std::shared_ptr<KindDef> IntDef() { return Def(KnownTypeCode::Int32); }
std::shared_ptr<KindDef> LongDef() { return Def(KnownTypeCode::Int64); }
std::shared_ptr<KindDef> CharDef() { return Def(KnownTypeCode::Char); }
std::shared_ptr<KindDef> BoolDef() { return Def(KnownTypeCode::Boolean); }
std::shared_ptr<KindDef> StringDef() { return Def(KnownTypeCode::String); }
std::shared_ptr<KindDef> ObjectDef() { return Def(KnownTypeCode::Object); }

// A shared-managed enum definition with the registered Int32 as its underlying type
// (an enum constant holds its UNDERLYING primitive value).
std::shared_ptr<KindDef> EEnumDef() {
    static const auto e = std::make_shared<KindDef>(
        "E", "", FullTypeName(TopLevelTypeName("", "E", 0)),
        TypeKind::Enum, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    e->SetEnumUnderlyingType(IntDef());
    return e;
}

// A shared-managed delegate definition.
std::shared_ptr<KindDef> DelegateDef() {
    static const auto d = std::make_shared<KindDef>(
        "D", "", FullTypeName(TopLevelTypeName("", "D", 0)),
        TypeKind::Delegate, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    return d;
}

// A fresh shared-managed resolver (the make_shared discipline).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// A fresh CHECKED resolver (the WithCheckForOverflow clone -- a distinct flag
// allocates the clone rather than returning this).
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

// The C# `SpecialType.NInt` / `SpecialType.NUInt` shapes.
std::shared_ptr<SpecialType> NIntType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::NInt, false);
    return t;
}

std::shared_ptr<SpecialType> NUIntType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::NUInt, false);
    return t;
}

// The C# `SpecialType.NullType` shape (a null LITERAL's type).
std::shared_ptr<SpecialType> NullType() {
    static const auto t = std::make_shared<SpecialType>(TypeKind::Null, true);
    return t;
}

// A LookupTypeDefinition whose GetMethods(filter, options) returns a configured
// list, applying the filter faithfully (the CSharpResolverUnaryOperator_Test stub),
// with the call count recording whether the user-defined scan ran at all.
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

// A host with the given name and KnownTypeCode (None by default -- a custom type with
// TypeCode::Empty is outside the primitive gate).
std::shared_ptr<MethodHostType> MakeHost(const std::string& name,
                                         KnownTypeCode ktc = KnownTypeCode::None,
                                         TypeKind kind = TypeKind::Struct) {
    return std::make_shared<MethodHostType>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A configured IParameter over a configured type (the established TestParameter
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
    ::ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override
    { return {}; }
private:
    ITypePtr type_;
};

// A configured operator method (the CSharpResolverUnaryOperator_Test factory): a
// LookupMethod with the metadata name, the IsOperator flag, the return type, and the
// parameter types, kept alive for the program's lifetime (static keep vectors).
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

// The dynamic_cast helper asserting the error-result shape.
const ErrorResolveResult* AsError(const std::shared_ptr<ResolveResult>& r) {
    const auto* e = dynamic_cast<const ErrorResolveResult*>(r.get());
    EXPECT_NE(e, nullptr);
    return e;
}

// ---------------------------------------------------------------------------
// The dynamic arm
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, DynamicBothOperandsYieldOperatorResultOverDynamic) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(DynamicType()), Arg(DynamicType()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(op->Type().Kind(), TypeKind::Dynamic);
    EXPECT_EQ(op->OperatorType(), ExpressionType::Add);
    EXPECT_FALSE(op->IsLiftedOperator());
    ASSERT_EQ(op->Operands().size(), 2u);
    // Both operands convert to dynamic (the ImplicitDynamicConversion wrap).
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(op->Operands()[0].get()),
              nullptr);
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(op->Operands()[1].get()),
              nullptr);
}

TEST(ResolveBinaryOperatorTest, DynamicLhsConvertsTheIntRhsToDynamic) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(DynamicType()), Arg(IntDef()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(op->Type().Kind(), TypeKind::Dynamic);
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(op->Operands()[1].get()),
              nullptr);
}

// ---------------------------------------------------------------------------
// The overloadable-name gate
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, NullCoalescingDelegatesToTheNullCoalescingHandler) {
    const auto lhs = Arg(MakeNullableOf(IntDef()));
    const auto rhs = Arg(IntDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::NullCoalescing, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    // The nullable lhs's UNDERLYING type is the result (the rhs converts to it by
    // identity).
    EXPECT_EQ(&op->Type(), IntDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Coalesce);
    ASSERT_EQ(op->Operands().size(), 2u);
    // The identity conversion keeps both operand handles (pointer identity).
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, ConditionalAndFallsThroughToTheLogicalAndTable) {
    const auto lhs = Arg(BoolDef());
    const auto rhs = Arg(BoolDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::ConditionalAnd, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::AndAlso);
    ASSERT_EQ(op->Operands().size(), 2u);
    // bool -> bool is identity: the operands keep pointer identity.
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, RangeOperatorYieldsTheUnknownErrorSingleton) {
    // `Range` has no overloadable metadata name and no special-case arm -- the C#
    // default returns the UnknownError singleton.
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Range, Arg(IntDef()), Arg(IntDef()));
    EXPECT_TRUE(result->IsError());
    EXPECT_EQ(result.get(), &ErrorResolveResult::UnknownError());
}

// ---------------------------------------------------------------------------
// The user-defined operator resolution
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, ApplicableUserDefinedOperatorWinsOverTheBuiltinTables) {
    // A custom struct host with `op_Addition(S, S) -> S`: both operands convert by
    // identity, so the user-defined resolution finds an applicable candidate and the
    // result carries the user-defined method (the built-in tables never run).
    //
    // The same-typed operands scan BOTH operand types -- without the C# HashSet
    // dedup the candidate would be added TWICE and the resolution would turn
    // ambiguous (the error path); the clean resolution is the dedup's observable.
    const auto host = MakeHost("HostS");
    const auto op = MakeOperatorMethod("op_Addition", host, {host, host});
    host->SetMethods({op.get()});
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(host), Arg(host));
    const OperatorResolveResult* orr = AsOperator(result);
    EXPECT_EQ(orr->UserDefinedOperatorMethod(), op.get());
    EXPECT_EQ(&orr->Type(), host.get());
    EXPECT_EQ(orr->OperatorType(), ExpressionType::Add);
    EXPECT_FALSE(orr->IsLiftedOperator());
}

TEST(ResolveBinaryOperatorTest, InapplicableUserDefinedOperatorIsPreferredOverTheBuiltinError) {
    // S + bool: the user-defined op_Addition(S, S) is inapplicable (bool does not
    // convert to S) and so is every built-in AdditionOperators entry -- the
    // informative-error path prefers the USER-DEFINED resolution result (the
    // invocation error result), not the builtin ErrorResolveResult.
    const auto host = MakeHost("HostS");
    const auto op = MakeOperatorMethod("op_Addition", host, {host, host});
    host->SetMethods({op.get()});
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(host), Arg(BoolDef()));
    const auto* invocation =
        dynamic_cast<const CSharpInvocationResolveResult*>(result.get());
    ASSERT_NE(invocation, nullptr);
    EXPECT_TRUE(result->IsError());
}

// ---------------------------------------------------------------------------
// The built-in tables and the numeric promotions
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, IntegerAdditionResolvesThroughTheBuiltinTable) {
    const auto lhs = Arg(IntDef());
    const auto rhs = Arg(IntDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), IntDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Add);
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, ConstantPairFoldsThroughInvoke) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Const(IntDef(), 2), Const(IntDef(), 3));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), IntDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(c->ConstantValue()), 5);
}

TEST(ResolveBinaryOperatorTest, CheckedOverflowFoldDowngradesToError) {
    // The checked resolver's func-pair selection throws OverflowException at the
    // int32 boundary -- the ArithmeticException catch downgrades to the
    // ErrorResolveResult over the result type.
    auto result = MakeCheckedResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Const(IntDef(), std::numeric_limits<int>::max()),
        Const(IntDef(), 1));
    const ErrorResolveResult* e = AsError(result);
    EXPECT_EQ(&e->Type(), IntDef().get());
}

TEST(ResolveBinaryOperatorTest, DivideByZeroFoldDowngradesToError) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Divide, Const(IntDef(), 1), Const(IntDef(), 0));
    const ErrorResolveResult* e = AsError(result);
    EXPECT_EQ(&e->Type(), IntDef().get());
}

TEST(ResolveBinaryOperatorTest, NumericWideningResolvesTheLongEntry) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(IntDef()), Arg(LongDef()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), LongDef().get());
    ASSERT_EQ(op->Operands().size(), 2u);
    // The int operand is wrapped by the widening conversion; the long operand keeps
    // pointer identity.
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(op->Operands()[0].get()),
              nullptr);
}

TEST(ResolveBinaryOperatorTest, NullableLhsLiftsTheBuiltinEntry) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(MakeNullableOf(IntDef())), Arg(IntDef()));
    const OperatorResolveResult* op = AsOperator(result);
    // The lifted int? entry's return type: a Nullable<int> over the registered
    // definition with the registered Int32 element.
    EXPECT_TRUE(IsNullable(op->Type()));
    EXPECT_EQ(&GetUnderlyingType(op->Type()), IntDef().get());
    EXPECT_TRUE(op->IsLiftedOperator());
}

// ---------------------------------------------------------------------------
// The shift special case
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, NullNullShiftProducesNullableInt) {
    // "var x = null << null" produces int?: the shift special case forces isNullable,
    // and the LIFTED int? entry is the only applicable one for the null literals
    // (the null literal converts to int? but not to int).
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::ShiftLeft, Arg(NullType()), Arg(NullType()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_TRUE(IsNullable(op->Type()));
    EXPECT_EQ(&GetUnderlyingType(op->Type()), IntDef().get());
    EXPECT_TRUE(op->IsLiftedOperator());
}

TEST(ResolveBinaryOperatorTest, CharShiftPromotesTheLhsIndependently) {
    const auto rhs = Arg(IntDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::ShiftLeft, Arg(CharDef()), rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), IntDef().get());
    ASSERT_EQ(op->Operands().size(), 2u);
    // The char operand is unary-promoted to int (wrapped by the conversion); the int
    // operand keeps pointer identity (the independent unary promotion).
    EXPECT_NE(dynamic_cast<const ConversionResolveResult*>(op->Operands()[0].get()),
              nullptr);
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

// ---------------------------------------------------------------------------
// The enum arms
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, EnumAdditionConstantFoldsThroughTheUnderlyingType) {
    // E + U evaluates as (E)((U)x + (U)y): the enum constant 1 and the underlying
    // constant 2 fold to the enum constant 3.
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Const(EEnumDef(), 1), Const(IntDef(), 2));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), EEnumDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(c->ConstantValue()), 3);
}

TEST(ResolveBinaryOperatorTest, EnumSubtractionConstantFoldsToTheUnderlyingType) {
    // E - E evaluates as (U)((U)x - (U)y): the fold reports the UNDERLYING int
    // constant, not the enum.
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Subtract, Const(EEnumDef(), 5), Const(EEnumDef(), 2));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), IntDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(c->ConstantValue()), 3);
}

TEST(ResolveBinaryOperatorTest, EnumComparisonConstantFoldsToBoolean) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::LessThan, Const(EEnumDef(), 1), Const(EEnumDef(), 2));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), BoolDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<bool>(c->ConstantValue()), true);
}

TEST(ResolveBinaryOperatorTest, EnumSubtractionNonConstantYieldsTheUnderlyingResultType) {
    const auto lhs = Arg(EEnumDef());
    const auto rhs = Arg(EEnumDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Subtract, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    // "U operator -(E x, E y)": the result type is the UNDERLYING type.
    EXPECT_EQ(&op->Type(), IntDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Subtract);
    EXPECT_FALSE(op->IsLiftedOperator());
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, EnumBitwiseOrNonConstantKeepsTheEnumResultType) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::BitwiseOr, Arg(EEnumDef()), Arg(EEnumDef()));
    const OperatorResolveResult* op = AsOperator(result);
    // "E operator |(E x, E y)": the result type is the ENUM type.
    EXPECT_EQ(&op->Type(), EEnumDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Or);
    EXPECT_FALSE(op->IsLiftedOperator());
}

TEST(ResolveBinaryOperatorTest, EnumAdditionNonConstantKeepsTheEnumResultType) {
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(EEnumDef()), Arg(IntDef()));
    // "E operator +(E x, U y)": the underlying constant converts to the enum's
    // underlying type, so the enum operator handler keeps the ENUM result type.
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), EEnumDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Add);
}

TEST(ResolveBinaryOperatorTest, NullableEnumComparisonPropagatesIsLifted) {
    // E? < E: the nullable operand forces isNullable, and the comparison handler
    // propagates it into the predefined result's IsLiftedOperator flag.
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::LessThan, Arg(MakeNullableOf(EEnumDef())), Arg(EEnumDef()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    EXPECT_TRUE(op->IsLiftedOperator());
}

// ---------------------------------------------------------------------------
// The delegate / pointer / reference / null-literal / native-integer arms
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, DelegateCombinationYieldsTheDelegateType) {
    const auto lhs = Arg(DelegateDef());
    const auto rhs = Arg(DelegateDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), DelegateDef().get());
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, PointerComparisonYieldsBooleanUnwrapped) {
    const auto lhs = Arg(std::make_shared<PointerType>(IntDef()));
    const auto rhs = Arg(std::make_shared<PointerType>(IntDef()));
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Equality, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::Equal);
    EXPECT_FALSE(op->IsLiftedOperator());
    ASSERT_EQ(op->Operands().size(), 2u);
    // The pointer-comparison arm returns the predefined result directly -- no
    // operand conversion.
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, ReferenceComparisonConvertsTheStringOperandToObject) {
    // string == object: the reference-comparison arm fires (the explicit
    // String-to-Object conversion IS a reference conversion), and the reference-equality
    // table's (Object, Object) entry is the only applicable one -- the String operand is
    // wrapped by the implicit reference conversion to the registered Object definition
    // while the Object operand converts by identity (pointer identity). (For the
    // same-typed string == string the (String, String) entry would instead win by
    // identity and convert nothing.)
    const auto rhs = Arg(ObjectDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Equality, Arg(StringDef()), rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    ASSERT_EQ(op->Operands().size(), 2u);
    const auto* lhsConv =
        dynamic_cast<const ConversionResolveResult*>(op->Operands()[0].get());
    ASSERT_NE(lhsConv, nullptr);
    EXPECT_EQ(&lhsConv->Type(), ObjectDef().get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, NullLiteralComparisonKeepsTheOperands) {
    const auto lhs = Arg(NullType());
    const auto rhs = Arg(StringDef());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Equality, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    ASSERT_EQ(op->Operands().size(), 2u);
    // The null-literal comparison arm returns the predefined result directly -- the
    // operands keep pointer identity.
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, NativeIntegerEqualityYieldsBoolean) {
    const auto lhs = Arg(NIntType());
    const auto rhs = Arg(NIntType());
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Equality, lhs, rhs);
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    EXPECT_FALSE(op->IsLiftedOperator());
    ASSERT_EQ(op->Operands().size(), 2u);
    EXPECT_EQ(op->Operands()[0].get(), lhs.get());
    EXPECT_EQ(op->Operands()[1].get(), rhs.get());
}

TEST(ResolveBinaryOperatorTest, MixingNativeIntegersIsABindingError) {
    // nint + nuint: the NUInt promotion arm casts both operands to nuint and reports
    // the signed-operand binding error -- the ErrorResolveResult over the PROMOTED
    // lhs type.
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::Add, Arg(NIntType()), Arg(NUIntType()));
    const ErrorResolveResult* e = AsError(result);
    EXPECT_EQ(e->Type().Kind(), TypeKind::NUInt);
}

TEST(ResolveBinaryOperatorTest, InapplicableBuiltinYieldsTheBestCandidateResultType) {
    // int && int: the LogicalAnd table's single bool entry is inapplicable for the
    // int operands, and the primitive gate excluded the user-defined scan -- the
    // ErrorResolveResult over the best candidate's Boolean result type.
    auto result = MakeResolver()->ResolveBinaryOperator(
        BinaryOperatorType::ConditionalAnd, Arg(IntDef()), Arg(IntDef()));
    const ErrorResolveResult* e = AsError(result);
    EXPECT_EQ(&e->Type(), BoolDef().get());
}

// ---------------------------------------------------------------------------
// The enum handlers directly (the public-for-TDD surface)
// ---------------------------------------------------------------------------

TEST(ResolveBinaryOperatorTest, HandleEnumComparisonDirectNonConstant) {
    auto result = MakeResolver()->HandleEnumComparison(
        BinaryOperatorType::LessThan, *EEnumDef(), /*isNullable=*/false,
        Arg(EEnumDef()), Arg(EEnumDef()));
    const OperatorResolveResult* op = AsOperator(result);
    EXPECT_EQ(&op->Type(), BoolDef().get());
    EXPECT_EQ(op->OperatorType(), ExpressionType::LessThan);
    EXPECT_FALSE(op->IsLiftedOperator());
}

TEST(ResolveBinaryOperatorTest, HandleEnumSubtractionDirectNullable) {
    auto result = MakeResolver()->HandleEnumSubtraction(
        /*isNullable=*/true, *EEnumDef(), Arg(EEnumDef()), Arg(EEnumDef()));
    const OperatorResolveResult* op = AsOperator(result);
    // The nullable underlying result type (int?).
    EXPECT_TRUE(IsNullable(op->Type()));
    EXPECT_EQ(&GetUnderlyingType(op->Type()), IntDef().get());
    EXPECT_TRUE(op->IsLiftedOperator());
}

TEST(ResolveBinaryOperatorTest, HandleEnumOperatorDirectConstantFold) {
    // (E)((U)x op (U)y) with the unchecked ResolveCast back into the enum.
    auto result = MakeResolver()->HandleEnumOperator(
        /*isNullable=*/false, *EEnumDef(), BinaryOperatorType::BitwiseAnd,
        Const(EEnumDef(), 5), Const(EEnumDef(), 3));
    const ConstantResolveResult* c = AsConstant(result);
    EXPECT_EQ(&c->Type(), EEnumDef().get());
    ASSERT_TRUE(c->ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(c->ConstantValue()), 1);
}

} // namespace
