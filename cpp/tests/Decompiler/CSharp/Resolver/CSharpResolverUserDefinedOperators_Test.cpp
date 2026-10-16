// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
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

// Tests for the CSharpResolver user-defined operator candidate region (cpp/Decompiler/
// CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 566-583 /
// 1207-1241 / 1278-1322): the two GetOverloadableOperatorName statics, GetUserDefined
// OperatorCandidates (+ the private LiftUserDefinedOperators, tested transitively), and
// CreateResolveResultForUserDefinedOperator -- the shared prerequisite machinery the
// ResolveUnaryOperator / ResolveBinaryOperator regions consume.
//
// The load-bearing cruxes:
//  (a) the overloadable-name mapping -- the pre- and post-increment forms SHARE
//      "op_Increment" (likewise the decrements), and the non-overloadable kinds (the
//      pointer operators, await, the conditional &&/||, ??, .., is) have NO metadata
//      name (nullptr, the C# null -- the future ResolveUnaryOperator /
//      ResolveBinaryOperator regions branch on it);
//  (b) the PRIMITIVE GATE -- a type whose TypeCode lies in the closed range
//      [Boolean..Decimal] is excluded even when its method table holds a MATCHING
//      operator (the .NET framework contains some of C#'s built-in operators as
//      user-defined operators; using them would skip numeric promotion), and the gate
//      fires BEFORE the method-table scan (GetMethods is never called); the gate's
//      upper boundary is Decimal -- DateTime and String are OUTSIDE and are scanned;
//  (c) the scan FILTER -- the resolver passes `m.IsOperator && m.Name == operatorName`
//      to IType::GetMethods (the stub applies whatever predicate it receives
//      faithfully, so the tests pin the resolver's own conjunct: a non-operator method
//      with a matching name is excluded);
//  (d) the LIFT -- each liftable operator's Nullable<T> form is appended AFTER ALL the
//      originals (the C# captures the original count as the loop bound first, so the
//      appended lifted forms are not themselves lifted and the result order is
//      [originals..., lifted-in-same-order...]); the lifted entries carry the
//      ILiftedOperator cross-cast surface and Nullable<T> parameter/return types;
//  (e) the CreateResolveResultForUserDefinedOperator dispatch -- the error path (a best
//      candidate with applicability errors) delegates to r.CreateResolveResult(null)
//      (a CSharpInvocationResolveResult, NOT an OperatorResolveResult), while the valid
//      path builds the OperatorResolveResult over the best candidate's method (pointer
//      identity), the ILiftedOperator cross-cast marking lifted best candidates, and
//      the conversion-wrapped operands (a non-identity argument conversion wraps the
//      operand in a ConversionResolveResult; an identity conversion does not).
//
// The stubs mirror the CSharpOperatorsUserDefined_Test conventions (the registered
// compilation with NullableOfT for the lift, the ValueTypeDef/RefDef definite
// reference-ness stubs, the keep-alive vectors behind MakeOperatorMethod -- the lifted
// operator borrows the non-lifted method, so every method, parameter, and type must
// outlive the lift) plus the GetDelegateInvokeMethod_Test MethodHostType (the
// GetMethods override applying the received filter faithfully, with the call count
// recording the gate-before-scan ordering).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
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
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh `LookupCompilation` with every `KnownTypeCode` the region resolves registered
// as a shared-managed `LookupTypeDefinition` (the CSharpOperatorsUserDefined_Test
// precedent -- the LIFT resolves `NullableOfT` through `FindType`).
struct RegisteredCompilation {
    std::unique_ptr<LookupCompilation> compilation;
    std::vector<std::shared_ptr<LookupTypeDefinition>> types;
    std::shared_ptr<LookupTypeDefinition> nullableOfT;
};

RegisteredCompilation MakeRegisteredCompilation() {
    RegisteredCompilation rc;
    rc.compilation = std::make_unique<LookupCompilation>();
    for (int raw = static_cast<int>(KnownTypeCode::Object);
         raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
        KnownTypeCode code = static_cast<KnownTypeCode>(raw);
        std::string name = "T" + std::to_string(raw);
        auto def = std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)),
            TypeKind::Struct, Accessibility::Public, *rc.compilation, nullptr, code);
        rc.compilation->RegisterKnownType(code, def.get());
        rc.types.push_back(std::move(def));
    }
    rc.nullableOfT = std::make_shared<LookupTypeDefinition>(
        "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
        TypeKind::Struct, Accessibility::Public, *rc.compilation, nullptr,
        KnownTypeCode::NullableOfT);
    rc.compilation->RegisterKnownType(KnownTypeCode::NullableOfT, rc.nullableOfT.get());
    return rc;
}

// The shared registered compilation (the registrations are kept alive by the static struct
// for the program's lifetime -- the LookupCompilation stores only raw pointers).
RegisteredCompilation& TheCompilation() {
    static RegisteredCompilation rc = MakeRegisteredCompilation();
    return rc;
}

LookupCompilation& Compilation() {
    return *TheCompilation().compilation;
}

// A `LookupTypeDefinition` that reports itself as a reference type (a definite
// `IsReferenceType == true`, unlike the inherited `std::nullopt` default) -- the string
// stub needs the DEFINITE reference-type answer.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A `LookupTypeDefinition` that reports itself as a value type (a definite
// `IsReferenceType == false`) -- the liftable operator shapes need the DEFINITE
// value-type answer (`IsNonNullableValueType`).
class ValueTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return false; }
};

// A `LookupTypeDefinition` whose `GetMethods(filter, options)` returns a configured
// list, applying the filter faithfully (the C# `GetMethodsImpl` runs the predicate over
// the method table), with the call count recording whether the scan ran at all (the
// primitive-gate-before-scan crux).
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    int GetMethodsCallCount() const { return getMethodsCallCount_; }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
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
                                         KnownTypeCode ktc = KnownTypeCode::None) {
    return std::make_shared<MethodHostType>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A configurable `IParameter` over a configured type (the AddCandidate_Test
// TestParameter precedent).
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
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override
    { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    ITypePtr type_;
};

// A configured operator method: a `LookupMethod` with the metadata name, the `IsOperator`
// flag, the return type, and the parameter types. The method, its parameters, and their
// types are kept alive for the program's lifetime (static keep vectors -- the lifted
// operator borrows the method handle, and the method's `Parameters()` snapshot is
// non-owning).
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

// The shared value-type singletons (the type-cache model -- the element instances are
// reused across the methods and arguments so the pointer-identity assertions hold and
// the lifted forms' `Nullable<T>` wrappers embed the same instances).
std::shared_ptr<ValueTypeDef> IntDef() {
    static auto t = std::make_shared<ValueTypeDef>(
        "VInt", "", FullTypeName(TopLevelTypeName("", "VInt", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::Int32);
    return t;
}

std::shared_ptr<ValueTypeDef> LongDef() {
    static auto t = std::make_shared<ValueTypeDef>(
        "VLong", "", FullTypeName(TopLevelTypeName("", "VLong", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::Int64);
    return t;
}

std::shared_ptr<RefDef> StringDef() {
    static auto t = std::make_shared<RefDef>(
        "VString", "", FullTypeName(TopLevelTypeName("", "VString", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::String);
    return t;
}

// A plain `ResolveResult` over the given type (the base class is concrete).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// A fresh shared-managed resolver over the registered compilation (the make_shared
// discipline the identity-preserving early-outs require; the candidate scan itself reads
// no resolver state, but the resolver must exist to call the member).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

} // namespace

// ---------------------------------------------------------------------------
// GetOverloadableOperatorName(UnaryOperatorType) -- the metadata-name mapping
// ---------------------------------------------------------------------------

TEST(CSharpResolverUserDefinedOperatorsTest, UnaryNamesForNotBitNotMinusPlus)
{
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Not),
                 "op_LogicalNot");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::BitNot),
                 "op_OnesComplement");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Minus),
                 "op_UnaryNegation");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Plus),
                 "op_UnaryPlus");
}

TEST(CSharpResolverUserDefinedOperatorsTest, UnaryIncrementVariantsShareOpIncrement)
{
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Increment),
                 "op_Increment");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::PostIncrement),
                 "op_Increment");
}

TEST(CSharpResolverUserDefinedOperatorsTest, UnaryDecrementVariantsShareOpDecrement)
{
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Decrement),
                 "op_Decrement");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::PostDecrement),
                 "op_Decrement");
}

TEST(CSharpResolverUserDefinedOperatorsTest, UnaryNonOverloadableKindsHaveNoName)
{
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Any), nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Dereference),
              nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::AddressOf),
              nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(UnaryOperatorType::Await), nullptr);
}

// ---------------------------------------------------------------------------
// GetOverloadableOperatorName(BinaryOperatorType) -- the metadata-name mapping
// ---------------------------------------------------------------------------

TEST(CSharpResolverUserDefinedOperatorsTest, BinaryArithmeticNames)
{
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Add),
                 "op_Addition");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Subtract),
                 "op_Subtraction");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Multiply),
                 "op_Multiply");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Divide),
                 "op_Division");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Modulus),
                 "op_Modulus");
}

TEST(CSharpResolverUserDefinedOperatorsTest, BinaryBitwiseAndShiftNames)
{
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::BitwiseAnd),
                 "op_BitwiseAnd");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::BitwiseOr),
                 "op_BitwiseOr");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::ExclusiveOr),
                 "op_ExclusiveOr");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::ShiftLeft),
                 "op_LeftShift");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::ShiftRight),
                 "op_RightShift");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::UnsignedShiftRight),
                 "op_UnsignedRightShift");
}

TEST(CSharpResolverUserDefinedOperatorsTest, BinaryComparisonNames)
{
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Equality),
                 "op_Equality");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::InEquality),
                 "op_Inequality");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::GreaterThan),
                 "op_GreaterThan");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::LessThan),
                 "op_LessThan");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(
                     BinaryOperatorType::GreaterThanOrEqual),
                 "op_GreaterThanOrEqual");
    EXPECT_STREQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::LessThanOrEqual),
                 "op_LessThanOrEqual");
}

TEST(CSharpResolverUserDefinedOperatorsTest, BinaryNonOverloadableKindsHaveNoName)
{
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Any), nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::ConditionalAnd),
              nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::ConditionalOr),
              nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::NullCoalescing),
              nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::Range), nullptr);
    EXPECT_EQ(CSharpResolver::GetOverloadableOperatorName(BinaryOperatorType::IsPattern),
              nullptr);
}

// ---------------------------------------------------------------------------
// GetUserDefinedOperatorCandidates -- the primitive gate, the scan filter, and the lift
// ---------------------------------------------------------------------------

// The C# null operatorName yields the empty list before anything else runs.
TEST(CSharpResolverUserDefinedOperatorsTest, NullOperatorNameYieldsNoCandidates)
{
    auto host = MakeHost("H");
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    host->SetMethods({op.get()});
    auto candidates = MakeResolver()->GetUserDefinedOperatorCandidates(*host, nullptr);
    EXPECT_TRUE(candidates.empty());
    EXPECT_EQ(host->GetMethodsCallCount(), 0);
}

// A primitive operand type (TypeCode in the closed [Boolean..Decimal] range) is excluded
// even when its method table holds a MATCHING operator -- and the gate fires BEFORE the
// method-table scan (GetMethods is never called).
TEST(CSharpResolverUserDefinedOperatorsTest, PrimitiveOperandTypeIsExcludedWithoutScanning)
{
    auto host = MakeHost("H", KnownTypeCode::Int32);
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    host->SetMethods({op.get()});
    auto candidates =
        MakeResolver()->GetUserDefinedOperatorCandidates(*host, "op_Addition");
    EXPECT_TRUE(candidates.empty());
    EXPECT_EQ(host->GetMethodsCallCount(), 0);
}

// The gate covers exactly the closed range [Boolean..Decimal]: both boundary type codes
// are excluded.
TEST(CSharpResolverUserDefinedOperatorsTest, PrimitiveGateExcludesBooleanAndDecimal)
{
    auto boolHost = MakeHost("HB", KnownTypeCode::Boolean);
    auto decimalHost = MakeHost("HD", KnownTypeCode::Decimal);
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    boolHost->SetMethods({op.get()});
    decimalHost->SetMethods({op.get()});
    auto resolver = MakeResolver();
    EXPECT_TRUE(resolver->GetUserDefinedOperatorCandidates(*boolHost, "op_Addition").empty());
    EXPECT_EQ(boolHost->GetMethodsCallCount(), 0);
    EXPECT_TRUE(resolver->GetUserDefinedOperatorCandidates(*decimalHost, "op_Addition").empty());
    EXPECT_EQ(decimalHost->GetMethodsCallCount(), 0);
}

// The gate's upper boundary is Decimal: DateTime (TypeCode 16) and String (17) lie
// OUTSIDE the range and ARE scanned (matching operators are returned). The operator
// shape is deliberately NON-LIFTABLE (a reference-type parameter) so this test isolates
// the gate from the lift.
TEST(CSharpResolverUserDefinedOperatorsTest, DateTimeAndStringLieOutsideThePrimitiveGate)
{
    auto dateTimeHost = MakeHost("HDT", KnownTypeCode::DateTime);
    auto stringHost = MakeHost("HS", KnownTypeCode::String);
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), StringDef()});
    dateTimeHost->SetMethods({op.get()});
    stringHost->SetMethods({op.get()});
    auto resolver = MakeResolver();
    auto fromDateTime = resolver->GetUserDefinedOperatorCandidates(*dateTimeHost, "op_Addition");
    ASSERT_EQ(fromDateTime.size(), 1u);
    EXPECT_EQ(fromDateTime[0].get(), op.get());
    auto fromString = resolver->GetUserDefinedOperatorCandidates(*stringHost, "op_Addition");
    ASSERT_EQ(fromString.size(), 1u);
    EXPECT_EQ(fromString[0].get(), op.get());
}

// The scan passes `m.IsOperator && m.Name == operatorName` to GetMethods: a non-operator
// method with a matching name is excluded, and a right-named operator is returned with
// pointer identity (the borrowed alias preserves the type-system instance). The
// operator shapes are deliberately NON-LIFTABLE (a reference-type parameter) so this
// test isolates the filter from the lift.
TEST(CSharpResolverUserDefinedOperatorsTest, ScanAppliesTheIsOperatorAndNameFilter)
{
    auto host = MakeHost("H");
    auto opAddition = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), StringDef()});
    auto notAnOperator = MakeOperatorMethod("op_Multiply", IntDef(), {IntDef(), StringDef()},
                                            /*isOperator=*/false);
    auto opSubtraction = MakeOperatorMethod("op_Subtraction", IntDef(), {IntDef(), StringDef()});
    host->SetMethods({opAddition.get(), notAnOperator.get(), opSubtraction.get()});
    auto resolver = MakeResolver();
    auto addition = resolver->GetUserDefinedOperatorCandidates(*host, "op_Addition");
    ASSERT_EQ(addition.size(), 1u);
    EXPECT_EQ(addition[0].get(), opAddition.get());
    EXPECT_TRUE(resolver->GetUserDefinedOperatorCandidates(*host, "op_Multiply").empty());
    auto subtraction = resolver->GetUserDefinedOperatorCandidates(*host, "op_Subtraction");
    ASSERT_EQ(subtraction.size(), 1u);
    EXPECT_EQ(subtraction[0].get(), opSubtraction.get());
}

// No method matching the operator name yields the empty list.
TEST(CSharpResolverUserDefinedOperatorsTest, NoMatchingOperatorYieldsNoCandidates)
{
    auto host = MakeHost("H");
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    host->SetMethods({op.get()});
    auto candidates =
        MakeResolver()->GetUserDefinedOperatorCandidates(*host, "op_UnaryPlus");
    EXPECT_TRUE(candidates.empty());
    EXPECT_EQ(host->GetMethodsCallCount(), 1); // the scan ran and found nothing
}

// A liftable operator gets its lifted Nullable<T> form appended: the entry carries the
// ILiftedOperator cross-cast surface and Nullable<T> parameter and return types.
TEST(CSharpResolverUserDefinedOperatorsTest, LiftableOperatorsGetLiftedFormsAppended)
{
    auto host = MakeHost("H");
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    host->SetMethods({op.get()});
    auto candidates =
        MakeResolver()->GetUserDefinedOperatorCandidates(*host, "op_Addition");
    ASSERT_EQ(candidates.size(), 2u);
    EXPECT_EQ(candidates[0].get(), op.get()); // the original comes first
    auto* lifted = dynamic_cast<const ILiftedOperator*>(candidates[1].get());
    ASSERT_NE(lifted, nullptr);
    const IMethod* liftedMethod = dynamic_cast<const IMethod*>(candidates[1].get());
    ASSERT_NE(liftedMethod, nullptr);
    EXPECT_TRUE(IsNullable(liftedMethod->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(liftedMethod->ReturnType()), IntDef().get());
    ASSERT_EQ(liftedMethod->Parameters().size(), 2u);
    EXPECT_TRUE(IsNullable(liftedMethod->Parameters()[0]->Type()));
    EXPECT_EQ(&GetUnderlyingType(liftedMethod->Parameters()[0]->Type()), IntDef().get());
    // The ILiftedOperator surface delegates to the non-lifted method.
    EXPECT_EQ(lifted->NonLiftedParameters().size(), 2u);
    EXPECT_EQ(&lifted->NonLiftedReturnType(), IntDef().get());
}

// A non-liftable operator (a reference-type parameter) comes back alone -- the lift
// returns null and nothing is appended.
TEST(CSharpResolverUserDefinedOperatorsTest, NonLiftableOperatorsAreNotLifted)
{
    auto host = MakeHost("H");
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), StringDef()});
    host->SetMethods({op.get()});
    auto candidates =
        MakeResolver()->GetUserDefinedOperatorCandidates(*host, "op_Addition");
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0].get(), op.get());
}

// The lifted forms append AFTER ALL the originals, in the same order as the originals
// (the C# captures the original count as the loop bound first).
TEST(CSharpResolverUserDefinedOperatorsTest, LiftedFormsAppendAfterAllOriginalsInOrder)
{
    auto host = MakeHost("H");
    auto intOp = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto longOp = MakeOperatorMethod("op_Addition", LongDef(), {LongDef(), LongDef()});
    host->SetMethods({intOp.get(), longOp.get()});
    auto candidates =
        MakeResolver()->GetUserDefinedOperatorCandidates(*host, "op_Addition");
    ASSERT_EQ(candidates.size(), 4u);
    EXPECT_EQ(candidates[0].get(), intOp.get());
    EXPECT_EQ(candidates[1].get(), longOp.get());
    // [2] is the lift of the int overload, [3] the lift of the long overload -- the
    // Nullable<T> return types distinguish them.
    ASSERT_NE(dynamic_cast<const ILiftedOperator*>(candidates[2].get()), nullptr);
    ASSERT_NE(dynamic_cast<const ILiftedOperator*>(candidates[3].get()), nullptr);
    const IMethod* liftedInt = dynamic_cast<const IMethod*>(candidates[2].get());
    const IMethod* liftedLong = dynamic_cast<const IMethod*>(candidates[3].get());
    ASSERT_NE(liftedInt, nullptr);
    ASSERT_NE(liftedLong, nullptr);
    EXPECT_TRUE(IsNullable(liftedInt->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(liftedInt->ReturnType()), IntDef().get());
    EXPECT_TRUE(IsNullable(liftedLong->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(liftedLong->ReturnType()), LongDef().get());
}

// ---------------------------------------------------------------------------
// CreateResolveResultForUserDefinedOperator -- the user-defined operator resolve result
// ---------------------------------------------------------------------------

// A best candidate with applicability errors delegates to r.CreateResolveResult(null):
// the result is the invocation resolve result carrying the errors, NOT an
// OperatorResolveResult.
TEST(CSharpResolverUserDefinedOperatorsTest, ErrorBestCandidateYieldsTheInvocationResult)
{
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(StringDef()), Arg(IntDef())};
    OverloadResolution resolution(Compilation(), arguments, std::nullopt, std::nullopt,
                                  &conversions);
    EXPECT_NE(resolution.AddCandidate(*op), OverloadResolutionErrors::None);
    auto rr = CSharpResolver::CreateResolveResultForUserDefinedOperator(
        resolution, ExpressionType::Add);
    ASSERT_NE(rr, nullptr);
    EXPECT_NE(dynamic_cast<CSharpInvocationResolveResult*>(rr.get()), nullptr);
    EXPECT_EQ(dynamic_cast<OperatorResolveResult*>(rr.get()), nullptr);
}

// A valid best candidate yields the OperatorResolveResult over the candidate's method:
// pointer identity with the method, the given operator type, the non-lifted flag, the
// method's return type, and the (identity-unwrapped) operands.
TEST(CSharpResolverUserDefinedOperatorsTest, ValidBestCandidateYieldsTheOperatorResult)
{
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(IntDef()), Arg(IntDef())};
    OverloadResolution resolution(Compilation(), arguments, std::nullopt, std::nullopt,
                                  &conversions);
    EXPECT_EQ(resolution.AddCandidate(*op), OverloadResolutionErrors::None);
    auto rr = CSharpResolver::CreateResolveResultForUserDefinedOperator(
        resolution, ExpressionType::Add);
    auto* operatorResult = dynamic_cast<OperatorResolveResult*>(rr.get());
    ASSERT_NE(operatorResult, nullptr);
    EXPECT_EQ(operatorResult->UserDefinedOperatorMethod(), op.get());
    EXPECT_FALSE(operatorResult->IsLiftedOperator());
    EXPECT_EQ(operatorResult->OperatorType(), ExpressionType::Add);
    EXPECT_EQ(&operatorResult->Type(), IntDef().get());
    ASSERT_EQ(operatorResult->Operands().size(), 2u);
    // Identity conversions do not wrap: the operands are the arguments themselves.
    EXPECT_EQ(operatorResult->Operands()[0].get(), arguments[0].get());
    EXPECT_EQ(operatorResult->Operands()[1].get(), arguments[1].get());
}

// A LIFTED best candidate is marked IsLiftedOperator, carries the lifted method, and the
// lifted Nullable<T> return type (the int arguments match the lifted Nullable<int>
// parameters through the lifted-identity conversion).
TEST(CSharpResolverUserDefinedOperatorsTest, LiftedBestCandidateMarksIsLiftedOperator)
{
    auto op = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto lifted = CSharpOperators::LiftUserDefinedOperator(op);
    ASSERT_NE(lifted, nullptr);
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(IntDef()), Arg(IntDef())};
    OverloadResolution resolution(Compilation(), arguments, std::nullopt, std::nullopt,
                                  &conversions);
    EXPECT_EQ(resolution.AddCandidate(*lifted), OverloadResolutionErrors::None);
    auto rr = CSharpResolver::CreateResolveResultForUserDefinedOperator(
        resolution, ExpressionType::Add);
    auto* operatorResult = dynamic_cast<OperatorResolveResult*>(rr.get());
    ASSERT_NE(operatorResult, nullptr);
    EXPECT_TRUE(operatorResult->IsLiftedOperator());
    EXPECT_EQ(operatorResult->UserDefinedOperatorMethod(), lifted.get());
    EXPECT_TRUE(IsNullable(operatorResult->Type()));
    EXPECT_EQ(&GetUnderlyingType(operatorResult->Type()), IntDef().get());
}

// A non-identity argument conversion wraps the operand in a ConversionResolveResult
// carrying the original argument and the target parameter type; an identity conversion
// leaves the operand unwrapped.
TEST(CSharpResolverUserDefinedOperatorsTest, OperandsCarryTheArgumentConversions)
{
    auto op = MakeOperatorMethod("op_Addition", LongDef(), {LongDef(), LongDef()});
    CSharpConversions conversions(Compilation());
    auto first = Arg(IntDef());
    auto second = Arg(LongDef());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {first, second};
    OverloadResolution resolution(Compilation(), arguments, std::nullopt, std::nullopt,
                                  &conversions);
    EXPECT_EQ(resolution.AddCandidate(*op), OverloadResolutionErrors::None);
    auto rr = CSharpResolver::CreateResolveResultForUserDefinedOperator(
        resolution, ExpressionType::Add);
    auto* operatorResult = dynamic_cast<OperatorResolveResult*>(rr.get());
    ASSERT_NE(operatorResult, nullptr);
    ASSERT_EQ(operatorResult->Operands().size(), 2u);
    auto* wrapped = dynamic_cast<ConversionResolveResult*>(operatorResult->Operands()[0].get());
    ASSERT_NE(wrapped, nullptr); // int -> long is a real conversion: wrapped
    EXPECT_EQ(wrapped->Input(), first.get());
    EXPECT_EQ(&wrapped->Type(), LongDef().get());
    EXPECT_EQ(dynamic_cast<ConversionResolveResult*>(operatorResult->Operands()[1].get()),
              nullptr); // long -> long is identity: not wrapped
    EXPECT_EQ(operatorResult->Operands()[1].get(), second.get());
}
