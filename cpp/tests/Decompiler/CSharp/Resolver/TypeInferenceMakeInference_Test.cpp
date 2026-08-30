// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `TypeInference` MakeInference region (the third slice of the TypeInference
// long pole): `Detail::GetTPForType` (TypeInference.cs lines 717-729), the three mutually
// recursive spec workers `Detail::MakeExactInference` (lines 635-728, C# 4.0 spec section
// 7.5.2.8), `Detail::MakeLowerBoundInference` (lines 736-857, spec draft-v11 section
// 12.6.3.11), `Detail::MakeUpperBoundInference` (lines 865-961, C# 4.0 spec section
// 7.5.2.10), and the phase-one entry `Detail::MakeExplicitParameterTypeInference` (lines
// 614-627, spec draft-v11 section 12.6.3.9).
//
// The load-bearing cruxes:
//  (a) the GetTPForType resolution is REFERENCE equality on the type parameter -- a
//      different parameter instance carrying the same index does not resolve (the
//      OccursInVisitor precedent), and a nullability-annotated parameter delegates to
//      its ORIGINAL parameter;
//  (b) the three bound KINDS route by worker: MakeExact -> `ExactBound`,
//      MakeLower -> `LowerBounds`, MakeUpper -> `UpperBounds`;
//  (c) the recursion SHAPES: by-reference and pointer pairs recurse EXACT in the
//      lower/upper workers; the function-pointer arms SWAP (lower: return -> lower,
//      params -> upper; upper: the mirror); the nullable pair recurses on the underlying
//      types; a tuple delegates to its underlying parameterized type;
//  (d) the span arms gate on `FirstClassSpanTypes` (the D538 flag gate) while the
//      lower-bound array-interface arm fires WITHOUT the flag;
//  (e) the unique-base-type variance walk: covariant arguments recurse in the worker's
//      own direction, contravariant in the opposite, invariant exact -- and a second
//      matching base type aborts the whole inference ("it's not unique");
//  (f) the MakeExplicitParameterTypeInference gates (implicitly-typed / no-parameter-list
//      lambdas contribute nothing) and its zip stops at the shorter parameter list.
//
// The stubs mirror the TypeInferenceContainsUnfixed_Test conventions (`Def`, `MakeState`,
// `MethodHostType`/`MakeMethod`/`ConfigureMethod`, `TestParameter`, `MakeDelegate`,
// `TestLambda`) plus the `SpanCompilation` flag override (the D538 precedent) and the
// `RefDef` reference-type stub (the D517 precedent). Every type fed to the workers as
// U/V is make_shared-managed: the nullability strip's `WithoutNullability` calls the
// non-const `ChangeNullability` (the `shared_from_this` D406 convention).

#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::GetTPForType;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MakeExactInference;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MakeExplicitParameterTypeInference;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MakeLowerBoundInference;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MakeUpperBoundInference;
using ILSpy::Decompiler::CSharp::Resolver::Detail::TP;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedTypeParameter;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// The first-class-span-types compilation (the D538 `SpanCompilation` precedent) -- the
// flag the three workers' span arms gate on.
class SpanCompilation : public LookupCompilation {
public:
    TS::TypeSystemOptions TypeSystemOptions() const override {
        return TS::TypeSystemOptions::FirstClassSpanTypes;
    }
};

SpanCompilation& Compilation() {
    static SpanCompilation c;
    return c;
}

// A plain `LookupCompilation` (no flags) for the flag-gate tests.
LookupCompilation& NoFlagCompilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind`. The D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// A `LookupTypeDefinition` that is definitely a REFERENCE type (the D517 `RefDef`
// precedent) -- the variance walk's `Ui.IsReferenceType == true` gate needs a definite
// true, not the plain definition's indeterminate `std::nullopt`.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

std::shared_ptr<RefDef> MakeRefDef(std::string name) {
    return std::make_shared<RefDef>(
        std::move(name), "",
        FullTypeName(TopLevelTypeName("", std::move(name), 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
}

// A named type parameter with the given index (the real `InferTypeArguments` contract is
// `typeParameters[i].Index == i`). This region reads no visitors, so the plain
// `LookupTypeParameter` suffices (no `AcceptVisitor` bridge).
std::shared_ptr<LookupTypeParameter> MakeTypeParam(std::string name, int index) {
    auto t = std::make_shared<LookupTypeParameter>(std::move(name));
    t->SetIndex(index);
    return t;
}

// Build the `TP` state vector over the supplied type parameters (the vector order IS the
// index contract).
std::vector<TP> MakeState(const std::vector<std::shared_ptr<LookupTypeParameter>>& tps) {
    std::vector<TP> state;
    state.reserve(tps.size());
    for (const auto& t : tps)
        state.emplace_back(*t);
    return state;
}

// A `Nullable<T>`-shaped `ParameterizedType` over the supplied element type (the D515
// shape -- `IsNullable` reads the `NullableOfT` known-type code through the definition).
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(Def(KnownTypeCode::NullableOfT),
                                               std::vector<ITypePtr>{std::move(element)});
}

// A one-dimensional array of the supplied element type.
ITypePtr ArrayOf(ITypePtr element) {
    return std::make_shared<TS::ArrayType>(std::move(element));
}

// A function pointer with the supplied return and parameter types.
ITypePtr FnPtr(ITypePtr ret, std::vector<ITypePtr> params) {
    std::vector<ReferenceKind> rks(params.size(), ReferenceKind::None);
    return std::make_shared<FunctionPointerType>(
        SignatureCallingConvention::Default, std::vector<ITypePtr>(), std::move(ret), false,
        std::move(params), std::move(rks));
}

// An `IGen<X>`-shaped generic definition whose declared type parameter carries the
// supplied variance (the definition's `SetTypeParameters` keeps the NON-OWNING raw
// pointer -- the caller keeps the parameter alive through the out-param).
std::shared_ptr<LookupTypeDefinition> MakeGenericDef(
    VarianceModifier variance, std::shared_ptr<LookupTypeParameter>& genParamOut)
{
    genParamOut = std::make_shared<LookupTypeParameter>("TGen", variance);
    auto def = std::make_shared<LookupTypeDefinition>(
        "IGen", "",
        FullTypeName(TopLevelTypeName("", "IGen", 1)),
        TypeKind::Interface, Accessibility::Public, Compilation(), nullptr);
    def->SetTypeParameters({genParamOut.get()});
    return def;
}

ITypePtr Parameterize(std::shared_ptr<LookupTypeDefinition> def, ITypePtr arg) {
    return std::make_shared<ParameterizedType>(std::move(def),
                                               std::vector<ITypePtr>{std::move(arg)});
}

// A class definition whose direct base types are the supplied list (the unique-base-type
// variance walk reads them through `GetAllBaseTypes`).
std::shared_ptr<LookupTypeDefinition> MakeDerivedDef(std::vector<ITypePtr> bases) {
    auto def = std::make_shared<LookupTypeDefinition>(
        "Derived", "",
        FullTypeName(TopLevelTypeName("", "Derived", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    for (ITypePtr& b : bases)
        def->AddDirectBaseType(std::move(b));
    return def;
}

// A `LookupTypeDefinition` subclass whose `GetMethods(filter, options)` returns a
// configured list, applying the filter faithfully (the D533 `MethodHostType` precedent).
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        (void)options;
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
};

std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
    return std::make_shared<MethodHostType>(
        std::move(name), "",
        FullTypeName(TopLevelTypeName("", std::move(name), 0)),
        kind, Accessibility::Public, Compilation(), nullptr);
}

// A `LookupMethod` kept alive in a static vector so the raw `const IMethod*` the host
// holds outlives the call (the D533 precedent).
const IMethod* MakeMethod(std::string name = "Invoke") {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A minimal `IParameter` with a configurable type and reference kind (the D531/D534
// `TestParameter` precedent; the enum references are fully qualified against the
// cross-scope name-hiding crux).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type,
                           ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                           std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `TestParameter` kept alive in a static vector so the raw `const IParameter*` the
// method holds outlives the call (the D534 precedent).
std::shared_ptr<TestParameter> MakeParam(ITypePtr type, ReferenceKind rk = ReferenceKind::None) {
    static std::vector<std::shared_ptr<TestParameter>> keep;
    auto p = std::make_shared<TestParameter>(std::move(type), rk);
    keep.push_back(p);
    return p;
}

const IMethod* ConfigureMethod(const IMethod* method,
                               const std::vector<std::shared_ptr<TestParameter>>& params,
                               ITypePtr returnType) {
    auto* m = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(method));
    std::vector<const IParameter*> paramPtrs;
    for (const auto& p : params)
        paramPtrs.push_back(p.get());
    m->SetParameters(std::move(paramPtrs));
    m->SetReturnType(std::move(returnType));
    return method;
}

// A delegate type whose `Invoke` takes the supplied parameter types and returns the
// supplied return type (the `Func`-like shape `GetDelegateOrExpressionTreeSignature`
// resolves).
std::shared_ptr<MethodHostType> MakeDelegate(
    const std::vector<std::shared_ptr<TestParameter>>& params, ITypePtr returnType) {
    auto host = MakeHost("D");
    const IMethod* invoke = MakeMethod("Invoke");
    ConfigureMethod(invoke, params, returnType);
    host->SetMethods({invoke});
    return host;
}

// A concrete `LambdaResolveResult` stub with configurable explicit-typing flags and
// parameters (the D534 `TestLambda` precedent; `IsValid` is never invoked by
// `MakeExplicitParameterTypeInference`, but the pure virtual must be implemented).
class TestLambda : public LambdaResolveResult {
public:
    bool hasParameterList = true;
    bool isAnonymousMethod = false;
    bool isImplicitlyTyped = false;
    bool isAsync = false;
    ITypePtr returnType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    std::vector<const IParameter*> parameters;
    std::shared_ptr<ResolveResult> body =
        std::make_shared<TypeResolveResult>(
            std::make_shared<TS::KnownType>(KnownTypeCode::Void));

    bool HasParameterList() const override { return hasParameterList; }
    bool IsAnonymousMethod() const override { return isAnonymousMethod; }
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped; }
    bool IsAsync() const override { return isAsync; }
    ITypePtr GetInferredReturnType(const std::vector<ITypePtr>&) const override { return returnType; }
    std::vector<const IParameter*> Parameters() const override { return parameters; }
    const IType& ReturnType() const override { return *returnType; }
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> IsValid(
        const std::vector<ITypePtr>&, const ITypePtr&,
        ILSpy::Decompiler::CSharp::Resolver::CSharpConversions&) const override
    {
        return std::make_shared<ILSpy::Decompiler::CSharp::Resolver::LambdaConversion>();
    }
    ResolveResult& Body() const override { return *body; }
    std::unique_ptr<ResolveResult> ShallowClone() const override
    {
        return std::make_unique<TestLambda>(*this);
    }
protected:
    std::string ClassName() const override { return "TestLambda"; }
};

} // namespace

// ===========================================================================
// GetTPForType (TypeInference.cs lines 717-729).
// ===========================================================================

// The tracked type parameter resolves to its own `TP` entry (reference identity by
// index).
TEST(TypeInferenceMakeInferenceTest, ResolvesTrackedTypeParameterToItsEntry)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    TP* tp = GetTPForType(state, *t0);
    ASSERT_NE(tp, nullptr);
    EXPECT_EQ(tp, &state[0]);
    EXPECT_EQ(tp->TypeParameter, t0.get());
}

// A DIFFERENT parameter instance carrying the same index does not resolve (the C#
// reference-equality `typeParameters[index].TypeParameter == p`).
TEST(TypeInferenceMakeInferenceTest, DifferentInstanceWithSameIndexDoesNotResolve)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    auto other = MakeTypeParam("T", 0);
    EXPECT_EQ(GetTPForType(state, *other), nullptr);
}

// A non-type-parameter type resolves nothing.
TEST(TypeInferenceMakeInferenceTest, NonTypeParameterResolvesNothing)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    EXPECT_EQ(GetTPForType(state, *int32), nullptr);
}

// An out-of-range index resolves nothing.
TEST(TypeInferenceMakeInferenceTest, OutOfRangeIndexResolvesNothing)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    auto t5 = MakeTypeParam("U", 5);
    EXPECT_EQ(GetTPForType(state, *t5), nullptr);
}

// A nullability-annotated type parameter delegates to its ORIGINAL parameter (the C#
// `v = natp.OriginalTypeParameter`).
TEST(TypeInferenceMakeInferenceTest, AnnotatedParameterDelegatesToOriginal)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    auto natp = std::make_shared<NullabilityAnnotatedTypeParameter>(t0, Nullability::Nullable);
    // The natp has two `IType` subobjects (the diamond), so the upcast is explicit through
    // the single `ITypeParameter` base.
    const ITypeParameter& asParameter = *natp;
    TP* tp = GetTPForType(state, asParameter);
    ASSERT_NE(tp, nullptr);
    EXPECT_EQ(tp, &state[0]);
    EXPECT_EQ(tp->TypeParameter, t0.get());
}

// ===========================================================================
// MakeExactInference (TypeInference.cs lines 635-728, C# 4.0 spec section 7.5.2.8).
// ===========================================================================

// An unfixed V-side type parameter takes U as its EXACT bound (the bound holds the very
// U instance passed in).
TEST(TypeInferenceMakeInferenceTest, ExactUnfixedTypeParameterTakesExactBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    MakeExactInference(NoFlagCompilation(), state, *int32, *t0);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
    EXPECT_TRUE(state[0].LowerBounds.empty());
    EXPECT_TRUE(state[0].UpperBounds.empty());
}

// A FIXED type parameter takes no further bound and the fall-through arms match nothing
// (an `int` vs type-parameter pair has no shape).
TEST(TypeInferenceMakeInferenceTest, ExactFixedTypeParameterTakesNoBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    state[0].FixedTo = Def(KnownTypeCode::Int64);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    MakeExactInference(NoFlagCompilation(), state, *int32, *t0);
    EXPECT_FALSE(state[0].ExactBound);
}

// Two by-reference shapes recurse EXACT on their elements.
TEST(TypeInferenceMakeInferenceTest, ExactByReferencePairRecursesOnElements)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr brInt = std::make_shared<TS::ByReferenceType>(int32);
    ITypePtr brT = std::make_shared<TS::ByReferenceType>(t0);
    MakeExactInference(NoFlagCompilation(), state, *brInt, *brT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// Two same-rank arrays recurse on their elements.
TEST(TypeInferenceMakeInferenceTest, ExactArrayPairRecursesOnElements)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr arrT = ArrayOf(t0);
    MakeExactInference(NoFlagCompilation(), state, *arrInt, *arrT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// A rank mismatch makes no inference (the `when arrU.Dimensions == arrV.Dimensions`
// guard fails and no later arm matches an array-vs-array pair).
TEST(TypeInferenceMakeInferenceTest, ExactArrayRankMismatchMakesNoInference)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr arr2T = std::make_shared<TS::ArrayType>(t0, 2);
    MakeExactInference(NoFlagCompilation(), state, *arrInt, *arr2T);
    EXPECT_FALSE(state[0].HasBounds());
}

// With the first-class-span-types flag, an array exact-infers against `Span<T>`'s element.
TEST(TypeInferenceMakeInferenceTest, ExactArrayToSpanWithFlagRecursesOnElement)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr spanT = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::SpanOfT), std::vector<ITypePtr>{t0});
    MakeExactInference(Compilation(), state, *arrInt, *spanT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// Without the flag, the same pair makes no inference at all (the span arm's gate).
TEST(TypeInferenceMakeInferenceTest, ExactArrayToSpanWithoutFlagMakesNoInference)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr spanT = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::SpanOfT), std::vector<ITypePtr>{t0});
    MakeExactInference(NoFlagCompilation(), state, *arrInt, *spanT);
    EXPECT_FALSE(state[0].HasBounds());
}

// Two parameterizations of the SAME generic with the same arity match
// argument-for-argument.
TEST(TypeInferenceMakeInferenceTest, ExactSameGenericRecursesPerArgument)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    auto listDef = MakeDef(KnownTypeCode::IListOfT, TypeKind::Class);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr listOfInt = Parameterize(listDef, int32);
    ITypePtr listOfT = Parameterize(listDef, t0);
    MakeExactInference(NoFlagCompilation(), state, *listOfInt, *listOfT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// Two DIFFERENT generics make no inference (the `object.Equals` generic-type check).
TEST(TypeInferenceMakeInferenceTest, ExactDifferentGenericsMakeNoInference)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr listOfInt = Parameterize(MakeDef(KnownTypeCode::IListOfT, TypeKind::Class), int32);
    ITypePtr otherOfT = Parameterize(MakeDef(KnownTypeCode::IEnumerableOfT, TypeKind::Interface), t0);
    MakeExactInference(NoFlagCompilation(), state, *listOfInt, *otherOfT);
    EXPECT_FALSE(state[0].HasBounds());
}

// Two pointer shapes recurse exactly on their elements.
TEST(TypeInferenceMakeInferenceTest, ExactPointerPairRecursesOnElements)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr ptrInt = std::make_shared<TS::PointerType>(int32);
    ITypePtr ptrT = std::make_shared<TS::PointerType>(t0);
    MakeExactInference(NoFlagCompilation(), state, *ptrInt, *ptrT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// Two function pointers recurse on the return and the zipped parameters.
TEST(TypeInferenceMakeInferenceTest, ExactFunctionPointerPairRecursesOnParams)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    ITypePtr fnInt = FnPtr(voidType, {int32});
    ITypePtr fnT = FnPtr(voidType, {t0});
    MakeExactInference(NoFlagCompilation(), state, *fnInt, *fnT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// A tuple delegates to its underlying parameterized type (the BOTH-sides
// `TupleUnderlyingTypeOrSelf` rebind) before the parameterized arm matches.
TEST(TypeInferenceMakeInferenceTest, ExactTupleUnwrapsToUnderlyingParameterized)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    auto listDef = MakeDef(KnownTypeCode::IListOfT, TypeKind::Class);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr listOfInt = Parameterize(listDef, int32);
    ITypePtr tuple = std::make_shared<TupleType>(listOfInt, std::vector<ITypePtr>{int32});
    ITypePtr listOfT = Parameterize(listDef, t0);
    MakeExactInference(NoFlagCompilation(), state, *tuple, *listOfT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// ===========================================================================
// MakeLowerBoundInference (TypeInference.cs lines 736-857, spec draft-v11 section
// 12.6.3.11).
// ===========================================================================

// An unfixed V-side type parameter takes U as a LOWER bound.
TEST(TypeInferenceMakeInferenceTest, LowerUnfixedTypeParameterTakesLowerBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    MakeLowerBoundInference(NoFlagCompilation(), state, *int32, *t0);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
    EXPECT_FALSE(state[0].ExactBound);
    EXPECT_TRUE(state[0].UpperBounds.empty());
}

// Two `Nullable<...>` wrappers recurse on their underlying types (the nullable-covariance
// arm).
TEST(TypeInferenceMakeInferenceTest, LowerNullablePairRecursesOnUnderlying)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(int32);
    ITypePtr nullableT = NullableOf(t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *nullableInt, *nullableT);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
}

// Two same-rank arrays recurse LOWER on their elements.
TEST(TypeInferenceMakeInferenceTest, LowerArrayPairRecursesOnElements)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr arrT = ArrayOf(t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *arrInt, *arrT);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
}

// With the flag, an array lower-bounds `ReadOnlySpan<T>`'s element.
TEST(TypeInferenceMakeInferenceTest, LowerArrayToReadOnlySpanWithFlagRecurses)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr rosT = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::ReadOnlySpanOfT), std::vector<ITypePtr>{t0});
    MakeLowerBoundInference(Compilation(), state, *arrInt, *rosT);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
}

// Without the flag, an array-to-span pair makes no inference.
TEST(TypeInferenceMakeInferenceTest, LowerArrayToSpanWithoutFlagMakesNoInference)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr spanT = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::SpanOfT), std::vector<ITypePtr>{t0});
    MakeLowerBoundInference(NoFlagCompilation(), state, *arrInt, *spanT);
    EXPECT_FALSE(state[0].HasBounds());
}

// A one-dimensional array lower-bounds the ARRAY-INTERFACE's element type -- the arm has
// NO flag gate, so it fires without `FirstClassSpanTypes`.
TEST(TypeInferenceMakeInferenceTest, LowerArrayToArrayInterfaceWithoutFlagRecurses)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arrInt = ArrayOf(int32);
    ITypePtr enumerableT = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::IEnumerableOfT, TypeKind::Interface),
        std::vector<ITypePtr>{t0});
    MakeLowerBoundInference(NoFlagCompilation(), state, *arrInt, *enumerableT);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
}

// A two-dimensional array does NOT match the array-interface arm (`arrU.Dimensions == 1`).
TEST(TypeInferenceMakeInferenceTest, LowerRank2ArrayToArrayInterfaceMakesNoInference)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr arr2Int = std::make_shared<TS::ArrayType>(int32, 2);
    ITypePtr enumerableT = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::IEnumerableOfT, TypeKind::Interface),
        std::vector<ITypePtr>{t0});
    MakeLowerBoundInference(NoFlagCompilation(), state, *arr2Int, *enumerableT);
    EXPECT_FALSE(state[0].HasBounds());
}

// The unique-base-type variance walk with a COVARIANT type parameter recurses LOWER on
// the argument pair.
TEST(TypeInferenceMakeInferenceTest, LowerCovariantWalkGivesLowerBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    std::shared_ptr<LookupTypeParameter> genParam;
    auto genDef = MakeGenericDef(VarianceModifier::Covariant, genParam);
    ITypePtr str = MakeRefDef("String");
    ITypePtr derived = MakeDerivedDef({Parameterize(genDef, str)});
    ITypePtr genOfT = Parameterize(genDef, t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *derived, *genOfT);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), str.get());
}

// The variance walk with a CONTRAVARIANT type parameter recurses in the OPPOSITE
// direction (UPPER bound).
TEST(TypeInferenceMakeInferenceTest, LowerContravariantWalkGivesUpperBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    std::shared_ptr<LookupTypeParameter> genParam;
    auto genDef = MakeGenericDef(VarianceModifier::Contravariant, genParam);
    ITypePtr str = MakeRefDef("String");
    ITypePtr derived = MakeDerivedDef({Parameterize(genDef, str)});
    ITypePtr genOfT = Parameterize(genDef, t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *derived, *genOfT);
    ASSERT_EQ(state[0].UpperBounds.size(), 1u);
    EXPECT_EQ(state[0].UpperBounds[0].get(), str.get());
    EXPECT_TRUE(state[0].LowerBounds.empty());
}

// The variance walk with an INVARIANT type parameter recurses EXACT.
TEST(TypeInferenceMakeInferenceTest, LowerInvariantWalkGivesExactBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    std::shared_ptr<LookupTypeParameter> genParam;
    auto genDef = MakeGenericDef(VarianceModifier::Invariant, genParam);
    ITypePtr str = MakeRefDef("String");
    ITypePtr derived = MakeDerivedDef({Parameterize(genDef, str)});
    ITypePtr genOfT = Parameterize(genDef, t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *derived, *genOfT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), str.get());
}

// A SECOND matching base type aborts the whole inference ("it's not unique") -- no
// bounds at all.
TEST(TypeInferenceMakeInferenceTest, LowerNonUniqueBaseTypeAbortsInference)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    std::shared_ptr<LookupTypeParameter> genParam;
    auto genDef = MakeGenericDef(VarianceModifier::Covariant, genParam);
    ITypePtr str = MakeRefDef("String");
    ITypePtr obj = MakeRefDef("Object");
    ITypePtr derived = MakeDerivedDef({Parameterize(genDef, str), Parameterize(genDef, obj)});
    ITypePtr genOfT = Parameterize(genDef, t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *derived, *genOfT);
    EXPECT_FALSE(state[0].HasBounds());
}

// The lower-bound function-pointer arm SWAPS: the parameters recurse UPPER-bound.
TEST(TypeInferenceMakeInferenceTest, LowerFunctionPointerSwapsParamsToUpper)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    ITypePtr fnInt = FnPtr(voidType, {int32});
    ITypePtr fnT = FnPtr(voidType, {t0});
    MakeLowerBoundInference(NoFlagCompilation(), state, *fnInt, *fnT);
    ASSERT_EQ(state[0].UpperBounds.size(), 1u);
    EXPECT_EQ(state[0].UpperBounds[0].get(), int32.get());
    EXPECT_TRUE(state[0].LowerBounds.empty());
}

// The lower-bound pointer arm recurses EXACT (pointer shapes match exactly, not
// lower-bound).
TEST(TypeInferenceMakeInferenceTest, LowerPointerPairUsesExactRecursion)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr ptrInt = std::make_shared<TS::PointerType>(int32);
    ITypePtr ptrT = std::make_shared<TS::PointerType>(t0);
    MakeLowerBoundInference(NoFlagCompilation(), state, *ptrInt, *ptrT);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
    EXPECT_TRUE(state[0].LowerBounds.empty());
}

// ===========================================================================
// MakeUpperBoundInference (TypeInference.cs lines 865-961, C# 4.0 spec section
// 7.5.2.10).
// ===========================================================================

// An unfixed V-side type parameter takes U as an UPPER bound.
TEST(TypeInferenceMakeInferenceTest, UpperUnfixedTypeParameterTakesUpperBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    MakeUpperBoundInference(NoFlagCompilation(), state, *int32, *t0);
    ASSERT_EQ(state[0].UpperBounds.size(), 1u);
    EXPECT_EQ(state[0].UpperBounds[0].get(), int32.get());
    EXPECT_TRUE(state[0].LowerBounds.empty());
    EXPECT_FALSE(state[0].ExactBound);
}

// An ARRAY-INTERFACE U upper-bounds the V-side array's element (the
// `pU.IsArrayInterfaceType() && arrV.Dimensions == 1` arm).
TEST(TypeInferenceMakeInferenceTest, UpperArrayInterfaceUpperBoundsArrayElement)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr enumerableInt = std::make_shared<ParameterizedType>(
        Def(KnownTypeCode::IEnumerableOfT, TypeKind::Interface),
        std::vector<ITypePtr>{int32});
    ITypePtr arrT = ArrayOf(t0);
    MakeUpperBoundInference(NoFlagCompilation(), state, *enumerableInt, *arrT);
    ASSERT_EQ(state[0].UpperBounds.size(), 1u);
    EXPECT_EQ(state[0].UpperBounds[0].get(), int32.get());
}

// The upper-bound variance walk with a COVARIANT type parameter recurses UPPER on the
// argument pair (pU is the U-side parameterization; the unique base is found among V's
// BASE types).
TEST(TypeInferenceMakeInferenceTest, UpperCovariantWalkGivesUpperBound)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    std::shared_ptr<LookupTypeParameter> genParam;
    auto genDef = MakeGenericDef(VarianceModifier::Covariant, genParam);
    ITypePtr str = MakeRefDef("String");
    ITypePtr genOfStr = Parameterize(genDef, str);
    ITypePtr derivedWithGenOfT = MakeDerivedDef({Parameterize(genDef, t0)});
    MakeUpperBoundInference(NoFlagCompilation(), state, *genOfStr, *derivedWithGenOfT);
    ASSERT_EQ(state[0].UpperBounds.size(), 1u);
    EXPECT_EQ(state[0].UpperBounds[0].get(), str.get());
}

// The upper-bound function-pointer arm swaps in the OPPOSITE direction of the
// lower-bound one: the parameters recurse LOWER-bound.
TEST(TypeInferenceMakeInferenceTest, UpperFunctionPointerSwapsParamsToLower)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    ITypePtr fnInt = FnPtr(voidType, {int32});
    ITypePtr fnT = FnPtr(voidType, {t0});
    MakeUpperBoundInference(NoFlagCompilation(), state, *fnInt, *fnT);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
    EXPECT_TRUE(state[0].UpperBounds.empty());
}

// ===========================================================================
// MakeExplicitParameterTypeInference (TypeInference.cs lines 614-627, spec draft-v11
// section 12.6.3.9).
// ===========================================================================

// An explicitly-typed lambda exact-infers its declared parameter types against the
// delegate signature's parameter types.
TEST(TypeInferenceMakeInferenceTest, ExplicitLambdaMatchesDelegateParameters)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    auto lambdaParam = MakeParam(int32);
    auto delegateParam = MakeParam(t0);
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    lambda.hasParameterList = true;
    lambda.parameters = {lambdaParam.get()};
    auto host = MakeDelegate({delegateParam}, voidType);
    MakeExplicitParameterTypeInference(NoFlagCompilation(), state, lambda, *host);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
}

// An IMPLICITLY-typed lambda contributes nothing (its parameter types are what inference
// produces).
TEST(TypeInferenceMakeInferenceTest, ImplicitlyTypedLambdaSkips)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    auto lambdaParam = MakeParam(int32);
    auto delegateParam = MakeParam(t0);
    TestLambda lambda;
    lambda.isImplicitlyTyped = true;
    lambda.hasParameterList = true;
    lambda.parameters = {lambdaParam.get()};
    auto host = MakeDelegate({delegateParam}, voidType);
    MakeExplicitParameterTypeInference(NoFlagCompilation(), state, lambda, *host);
    EXPECT_FALSE(state[0].HasBounds());
}

// A lambda WITHOUT a parameter list (the C# 2.0 anonymous-method form) contributes
// nothing.
TEST(TypeInferenceMakeInferenceTest, LambdaWithoutParameterListSkips)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    auto lambdaParam = MakeParam(int32);
    auto delegateParam = MakeParam(t0);
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    lambda.hasParameterList = false;
    lambda.parameters = {lambdaParam.get()};
    auto host = MakeDelegate({delegateParam}, voidType);
    MakeExplicitParameterTypeInference(NoFlagCompilation(), state, lambda, *host);
    EXPECT_FALSE(state[0].HasBounds());
}

// A non-delegate target (no `Invoke` signature) contributes nothing.
TEST(TypeInferenceMakeInferenceTest, NonDelegateTargetSkips)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    auto lambdaParam = MakeParam(int32);
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    lambda.hasParameterList = true;
    lambda.parameters = {lambdaParam.get()};
    ITypePtr notADelegate = Def(KnownTypeCode::Object, TypeKind::Class);
    MakeExplicitParameterTypeInference(NoFlagCompilation(), state, lambda, *notADelegate);
    EXPECT_FALSE(state[0].HasBounds());
}

// The parameter-list loop stops at the shorter of the two lists: a two-parameter lambda
// against a one-parameter delegate infers only the first pair (a wrong third quarter would
// raise `MultipleDifferentExactBounds`).
TEST(TypeInferenceMakeInferenceTest, ExplicitLambdaZipStopsAtShorterList)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    ITypePtr voidType = Def(KnownTypeCode::Void);
    auto p1 = MakeParam(int32);
    auto p2 = MakeParam(int64);
    auto delegateParam = MakeParam(t0);
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    lambda.hasParameterList = true;
    lambda.parameters = {p1.get(), p2.get()};
    auto host = MakeDelegate({delegateParam}, voidType);
    MakeExplicitParameterTypeInference(NoFlagCompilation(), state, lambda, *host);
    ASSERT_TRUE(state[0].ExactBound);
    EXPECT_EQ(state[0].ExactBound.get(), int32.get());
    EXPECT_FALSE(state[0].MultipleDifferentExactBounds);
}
