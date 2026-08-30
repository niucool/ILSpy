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

// Tests for the `TypeInference` ContainsUnfixed region (the second slice of the
// TypeInference long pole): `Detail::TP` (the per-type-parameter inference state holder,
// TypeInference.cs lines 217-250), `Detail::OccursInVisitor` (the type-parameter
// occurrence recorder, lines 256-271), and the three predicates (lines 442-465) --
// `Detail::AnyTypeContainsUnfixedParameter` plus the
// `Detail::InputTypesContainsUnfixed`/`Detail::OutputTypeContainsUnfixed` wrappers.
//
// The load-bearing cruxes:
//  (a) the occurrence test is `tp[index].TypeParameter == type` -- REFERENCE equality: a
//      different `ITypeParameter` instance carrying the same index does NOT count as
//      occurring (the real inference's type parameters are the very instances the method
//      declares);
//  (b) the `!IsFixed` guard -- a FIXED type parameter that occurs does not make the
//      predicate true (both inference phases use these predicates to find UNFIXED
//      dependents);
//  (c) the bounds state -- `AddExactBound` stores the FIRST exact bound and flags
//      differing later ones; the lower/upper bound adds dedup under `IType.Equals`
//      (the C# `HashSet<IType>.Add` idempotence);
//  (d) the wrappers route through the already-ported InputTypes/OutputTypes collectors:
//      an implicitly-typed lambda contributes the delegate's PARAMETER types, an
//      explicitly-typed lambda only the RETURN type (the no-gate asymmetry).
//
// The stubs mirror the TypeInferenceInputOutputTypes_Test conventions: `MethodHostType`,
// `LookupMethod` + `ConfigureMethod`, `TestParameter`/`MakeParam`, `MakeDelegate`,
// `TestLambda`, `MakeMethodGroup`, plus the local `VisitableTypeParameter` (the
// `AcceptVisitor` -> `VisitTypeParameter` bridge the visitor dispatch needs -- a plain
// `LookupTypeParameter` routes to `VisitOtherType`; the ValidateConstraints precedent)
// and the shared `LookupTypeParameter::SetIndex` (the additive-setter convention). Each
// test file carries its own anonymous-namespace stubs (the established convention).

#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::AnyTypeContainsUnfixedParameter;
using ILSpy::Decompiler::CSharp::Resolver::Detail::InputTypesContainsUnfixed;
using ILSpy::Decompiler::CSharp::Resolver::Detail::OccursInVisitor;
using ILSpy::Decompiler::CSharp::Resolver::Detail::OutputTypeContainsUnfixed;
using ILSpy::Decompiler::CSharp::Resolver::Detail::TP;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

LookupCompilation& Compilation() {
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

// A `LookupTypeParameter` whose `AcceptVisitor` dispatches to `visitor.VisitTypeParameter(*this)`
// (the `AbstractTypeParameter.AcceptVisitor` bridge): the base `IType::AcceptVisitor` default
// routes to `VisitOtherType`, so a plain `LookupTypeParameter` would never reach the
// `OccursInVisitor` override. The ValidateConstraints `VisitableTypeParameter` precedent (kept
// local so the shared stub's visitor behavior is untouched).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A named type parameter with the given index (the real `InferTypeArguments` contract is
// `typeParameters[i].Index == i`).
std::shared_ptr<VisitableTypeParameter> MakeTypeParam(std::string name, int index) {
    auto t = std::make_shared<VisitableTypeParameter>(std::move(name));
    t->SetIndex(index);
    return t;
}

// Build the `TP` state vector over the supplied type parameters (the vector order IS the
// index contract).
std::vector<TP> MakeState(const std::vector<std::shared_ptr<VisitableTypeParameter>>& tps) {
    std::vector<TP> state;
    state.reserve(tps.size());
    for (const auto& t : tps)
        state.emplace_back(*t);
    return state;
}

// A `List<T>`-shaped `ParameterizedType` over the supplied element type (the visitor
// recursion-through-type-arguments shape).
ITypePtr ListOf(ITypePtr element) {
    auto genericDef = std::make_shared<LookupTypeDefinition>(
        "List", "System.Collections.Generic",
        FullTypeName(TopLevelTypeName("System.Collections.Generic", "List", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    return std::make_shared<ParameterizedType>(std::move(genericDef),
                                               std::vector<ITypePtr>{std::move(element)});
}

// A `LookupTypeDefinition` subclass whose `GetMethods(filter, options)` returns a configured
// list, applying the filter faithfully (the D533 `MethodHostType` precedent). The delegate
// type is a `MethodHostType` with `TypeKind::Delegate`.
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

// A `MethodHostType` with the supplied `TypeKind` (Delegate by default). The D533 `MakeHost`
// precedent.
std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
    return std::make_shared<MethodHostType>(
        std::move(name), "",
        FullTypeName(TopLevelTypeName("", std::move(name), 0)),
        kind, Accessibility::Public, Compilation(), nullptr);
}

// A `LookupMethod` kept alive in a static vector so the raw `const IMethod*` the host holds
// outlives the call (the D533 precedent).
const IMethod* MakeMethod(std::string name = "Invoke") {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A minimal `IParameter` with a configurable type and reference kind. The
// `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same
// name for the rest of the class body (the D402 cross-scope name-hiding crux), so the enum
// references are fully qualified with `::ILSpy::Decompiler::TypeSystem::`. The D531/D534
// `TestParameter` precedent.
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

// A `TestParameter` kept alive in a static vector so the raw `const IParameter*` the method
// holds outlives the call (the D534 precedent).
std::shared_ptr<TestParameter> MakeParam(ITypePtr type, ReferenceKind rk = ReferenceKind::None) {
    static std::vector<std::shared_ptr<TestParameter>> keep;
    auto p = std::make_shared<TestParameter>(std::move(type), rk);
    keep.push_back(p);
    return p;
}

// Configure a method: set its parameters (from the kept `TestParameter` shared_ptrs) and its
// return type. Returns the same raw `const IMethod*` for chaining. The D531/D534 `Configure`
// precedent.
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

// A delegate type whose `Invoke` takes the supplied parameter types and returns the supplied
// return type (the `Func`-like shape the input/output-type collectors resolve).
std::shared_ptr<MethodHostType> MakeDelegate(
    const std::vector<std::shared_ptr<TestParameter>>& params, ITypePtr returnType) {
    auto host = MakeHost("D");
    const IMethod* invoke = MakeMethod("Invoke");
    ConfigureMethod(invoke, params, returnType);
    host->SetMethods({invoke});
    return host;
}

// A concrete `LambdaResolveResult` stub with a configurable `IsImplicitlyTyped` (the flag the
// `InputTypes` gate reads). The D534 `TestLambda` precedent (public fields for
// configurability; `IsValid` is never invoked by these predicates, but the pure virtual must
// be implemented).
class TestLambda : public LambdaResolveResult {
public:
    bool hasParameterList = true;
    bool isAnonymousMethod = false;
    bool isImplicitlyTyped = true;
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

// A `MethodGroupResolveResult` over an empty method list (the RTTI class is all the
// input/output-type collectors read; the method list is never dereferenced).
std::shared_ptr<MethodGroupResolveResult> MakeMethodGroup() {
    return std::make_shared<MethodGroupResolveResult>(
        std::make_shared<TypeResolveResult>(Def(KnownTypeCode::Object, TypeKind::Class)),
        "M",
        std::vector<ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType>{},
        std::vector<ITypePtr>{});
}

} // namespace

// ===========================================================================
// TP -- the per-type-parameter inference state (TypeInference.cs lines 217-250).
// ===========================================================================

// The ctor wires the type parameter; the fresh state is unfixed with no bounds.
TEST(TypeInferenceContainsUnfixedTest, TPWiresTypeParameterAndStartsUnfixed) {
    auto t = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t});
    EXPECT_EQ(state[0].TypeParameter, t.get());
    EXPECT_FALSE(state[0].IsFixed());
    EXPECT_FALSE(state[0].HasBounds());
    EXPECT_FALSE(state[0].ExactBound);
    EXPECT_FALSE(state[0].MultipleDifferentExactBounds);
    EXPECT_TRUE(state[0].LowerBounds.empty());
    EXPECT_TRUE(state[0].UpperBounds.empty());
}

// `IsFixed` tracks `FixedTo` (the C# `FixedTo != null`).
TEST(TypeInferenceContainsUnfixedTest, IsFixedTracksFixedTo) {
    auto t = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t});
    state[0].FixedTo = Def(KnownTypeCode::Int32);
    EXPECT_TRUE(state[0].IsFixed());
}

// `HasBounds` fires for each of the three bound kinds independently (the C#
// `LowerBounds.Count > 0 || UpperBounds.Count > 0 || ExactBound != null`).
TEST(TypeInferenceContainsUnfixedTest, HasBoundsTracksEachBoundKind) {
    auto t = MakeTypeParam("T", 0);

    std::vector<TP> withExact = MakeState({t});
    withExact[0].AddExactBound(Def(KnownTypeCode::Int32));
    EXPECT_TRUE(withExact[0].HasBounds());

    std::vector<TP> withLower = MakeState({t});
    withLower[0].AddLowerBound(Def(KnownTypeCode::Int32));
    EXPECT_TRUE(withLower[0].HasBounds());

    std::vector<TP> withUpper = MakeState({t});
    withUpper[0].AddUpperBound(Def(KnownTypeCode::Int32));
    EXPECT_TRUE(withUpper[0].HasBounds());
}

// `AddExactBound` stores the FIRST bound by identity; a structurally EQUAL later bound (a
// distinct `KnownType` instance with the same code) does not raise the flag; a DIFFERENT
// later bound does.
TEST(TypeInferenceContainsUnfixedTest, AddExactBoundStoresFirstAndFlagsDifferingLaterBounds) {
    auto t = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t});
    auto first = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    state[0].AddExactBound(first);
    EXPECT_EQ(state[0].ExactBound.get(), first.get());
    EXPECT_FALSE(state[0].MultipleDifferentExactBounds);

    // A distinct instance of the SAME type is not a differing bound (KnownType equality is
    // value-based, so `IType.Equals` holds across instances).
    state[0].AddExactBound(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    EXPECT_FALSE(state[0].MultipleDifferentExactBounds);

    // A DIFFERENT type raises the flag.
    state[0].AddExactBound(std::make_shared<TS::KnownType>(KnownTypeCode::Int64));
    EXPECT_TRUE(state[0].MultipleDifferentExactBounds);
}

// `AddLowerBound` is idempotent under `IType.Equals` (the C# `HashSet<IType>.Add`): a second
// structurally-equal bound is a no-op, a different bound is appended.
TEST(TypeInferenceContainsUnfixedTest, AddLowerBoundDedupsUnderEquals) {
    auto t = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t});
    state[0].AddLowerBound(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    state[0].AddLowerBound(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
    EXPECT_EQ(state[0].LowerBounds.size(), 1u);
    state[0].AddLowerBound(std::make_shared<TS::KnownType>(KnownTypeCode::Int64));
    EXPECT_EQ(state[0].LowerBounds.size(), 2u);
}

// `AddUpperBound` mirrors the lower-bound dedup.
TEST(TypeInferenceContainsUnfixedTest, AddUpperBoundDedupsUnderEquals) {
    auto t = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t});
    state[0].AddUpperBound(std::make_shared<TS::KnownType>(KnownTypeCode::String));
    state[0].AddUpperBound(std::make_shared<TS::KnownType>(KnownTypeCode::String));
    EXPECT_EQ(state[0].UpperBounds.size(), 1u);
    state[0].AddUpperBound(std::make_shared<TS::KnownType>(KnownTypeCode::Object));
    EXPECT_EQ(state[0].UpperBounds.size(), 2u);
}

// ===========================================================================
// OccursInVisitor -- the type-parameter occurrence recorder (lines 256-271).
// ===========================================================================

// Visiting a tracked type parameter records the occurrence at its index.
TEST(TypeInferenceContainsUnfixedTest, OccursInRecordsMatchingTypeParameter) {
    auto t0 = MakeTypeParam("T0", 0);
    auto t1 = MakeTypeParam("T1", 1);
    std::vector<TP> state = MakeState({t0, t1});
    OccursInVisitor v(state);
    t1->AcceptVisitor(v);
    EXPECT_FALSE(v.Occurs()[0]);
    EXPECT_TRUE(v.Occurs()[1]);
}

// A DIFFERENT type-parameter instance with the SAME index does not count -- the
// `tp[index].TypeParameter == type` REFERENCE-equality crux.
TEST(TypeInferenceContainsUnfixedTest, OccursInIgnoresDifferentInstanceWithSameIndex) {
    auto t0 = MakeTypeParam("T0", 0);
    auto other = MakeTypeParam("Other", 0);  // same index, different instance
    std::vector<TP> state = MakeState({t0});
    OccursInVisitor v(state);
    other->AcceptVisitor(v);
    EXPECT_FALSE(v.Occurs()[0]);
}

// An out-of-range index records nothing (the `index < tp.Length` guard; the defensive
// `index >= 0` addition skips a negative index without crashing).
TEST(TypeInferenceContainsUnfixedTest, OccursInIgnoresOutOfRangeIndex) {
    auto t0 = MakeTypeParam("T0", 0);
    auto farAway = MakeTypeParam("Far", 5);
    std::vector<TP> state = MakeState({t0});
    OccursInVisitor v(state);
    farAway->AcceptVisitor(v);
    EXPECT_FALSE(v.Occurs()[0]);
}

// The visitor recurses through a generic type's type arguments (the `VisitChildren` default
// of `VisitParameterizedType`), so a `List<T>`-shaped type records `T` occurring.
TEST(TypeInferenceContainsUnfixedTest, OccursInRecursesThroughTypeArguments) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr listOfT = ListOf(ITypePtr(t0));
    OccursInVisitor v(state);
    listOfT->AcceptVisitor(v);
    EXPECT_TRUE(v.Occurs()[0]);
}

// The occurrences accumulate across multiple visited types.
TEST(TypeInferenceContainsUnfixedTest, OccursInAccumulatesAcrossVisitedTypes) {
    auto t0 = MakeTypeParam("T0", 0);
    auto t1 = MakeTypeParam("T1", 1);
    std::vector<TP> state = MakeState({t0, t1});
    OccursInVisitor v(state);
    Def(KnownTypeCode::Int32)->AcceptVisitor(v);
    t1->AcceptVisitor(v);
    t0->AcceptVisitor(v);
    EXPECT_TRUE(v.Occurs()[0]);
    EXPECT_TRUE(v.Occurs()[1]);
}

// A non-type-parameter type records nothing.
TEST(TypeInferenceContainsUnfixedTest, OccursInRecordsNothingForNonTypeParameter) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    OccursInVisitor v(state);
    Def(KnownTypeCode::Int32)->AcceptVisitor(v);
    EXPECT_FALSE(v.Occurs()[0]);
}

// `VisitTypeParameter` returns the type itself (the C# `base.VisitTypeParameter` chain: the
// base default recurses through the children and returns `this`).
TEST(TypeInferenceContainsUnfixedTest, VisitTypeParameterReturnsTheTypeItself) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    OccursInVisitor v(state);
    ITypePtr t0i = t0;
    ITypePtr r = t0->AcceptVisitor(v);
    EXPECT_EQ(r.get(), t0i.get());
}

// ===========================================================================
// AnyTypeContainsUnfixedParameter -- the shared worker (line 453).
// ===========================================================================

// An unfixed type parameter occurring in the types makes the predicate true.
TEST(TypeInferenceContainsUnfixedTest, TrueWhenUnfixedTypeParameterOccurs) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr t0i = t0;
    EXPECT_TRUE(AnyTypeContainsUnfixedParameter(state, {t0i.get()}));
}

// A FIXED type parameter occurring does NOT make the predicate true -- the `!IsFixed` guard
// crux (the phases use these predicates to find unfixed dependents).
TEST(TypeInferenceContainsUnfixedTest, FalseWhenOccurringTypeParameterIsFixed) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    state[0].FixedTo = Def(KnownTypeCode::Int32);
    ITypePtr t0i = t0;
    EXPECT_FALSE(AnyTypeContainsUnfixedParameter(state, {t0i.get()}));
}

// Types containing no tracked type parameter yield false.
TEST(TypeInferenceContainsUnfixedTest, FalseWhenNothingOccurs) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    auto intT = Def(KnownTypeCode::Int32);
    EXPECT_FALSE(AnyTypeContainsUnfixedParameter(state, {intT.get()}));
}

// An empty type list yields false.
TEST(TypeInferenceContainsUnfixedTest, FalseForEmptyTypeList) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    EXPECT_FALSE(AnyTypeContainsUnfixedParameter(state, {}));
}

// The fixed parameter occurring while the unfixed one does not yields false -- the per-entry
// conjunction (`!IsFixed && Occurs`).
TEST(TypeInferenceContainsUnfixedTest, FalseWhenOnlyTheFixedParameterOccurs) {
    auto t0 = MakeTypeParam("T0", 0);
    auto t1 = MakeTypeParam("T1", 1);
    std::vector<TP> state = MakeState({t0, t1});
    state[0].FixedTo = Def(KnownTypeCode::Int32);
    auto intT = Def(KnownTypeCode::Int64);
    ITypePtr t0i = t0;
    EXPECT_FALSE(AnyTypeContainsUnfixedParameter(state, {t0i.get(), intT.get()}));
}

// An unfixed type parameter occurring INSIDE a generic type argument yields true (the
// visitor recursion flowing through the predicate).
TEST(TypeInferenceContainsUnfixedTest, TrueWhenUnfixedOccursInsideGenericType) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr listOfT = ListOf(ITypePtr(t0));
    EXPECT_TRUE(AnyTypeContainsUnfixedParameter(state, {listOfT.get()}));
}

// ===========================================================================
// InputTypesContainsUnfixed / OutputTypeContainsUnfixed -- the wrappers (lines 442-451).
// ===========================================================================

// An implicitly-typed lambda against a delegate whose PARAMETER type is the unfixed type
// parameter: the input types contain it.
TEST(TypeInferenceContainsUnfixedTest, InputTypesContainsUnfixedForImplicitlyTypedLambda) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    auto d = MakeDelegate({MakeParam(ITypePtr(t0))}, Def(KnownTypeCode::Int32));
    TestLambda lambda;  // isImplicitlyTyped defaults true
    EXPECT_TRUE(InputTypesContainsUnfixed(state, lambda, *d));
}

// An EXPLICITLY-typed lambda contributes no input types (the `IsImplicitlyTyped` gate), so
// the wrapper yields false even though the delegate's parameter type is the type parameter.
TEST(TypeInferenceContainsUnfixedTest, InputTypesContainsUnfixedFalseForExplicitlyTypedLambda) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    auto d = MakeDelegate({MakeParam(ITypePtr(t0))}, Def(KnownTypeCode::Int32));
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    EXPECT_FALSE(InputTypesContainsUnfixed(state, lambda, *d));
}

// A FIXED type parameter in the delegate's parameter type yields false (the `!IsFixed`
// guard through the wrapper).
TEST(TypeInferenceContainsUnfixedTest, InputTypesContainsUnfixedFalseWhenFixed) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    state[0].FixedTo = Def(KnownTypeCode::Int32);
    auto d = MakeDelegate({MakeParam(ITypePtr(t0))}, Def(KnownTypeCode::Int64));
    TestLambda lambda;
    EXPECT_FALSE(InputTypesContainsUnfixed(state, lambda, *d));
}

// An EXPLICITLY-typed lambda still contributes the delegate's RETURN type (the OutputTypes
// no-gate asymmetry flowing through the wrapper) -- a delegate returning the unfixed type
// parameter makes the output-side predicate true.
TEST(TypeInferenceContainsUnfixedTest, OutputTypeContainsUnfixedForExplicitlyTypedLambda) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    auto d = MakeDelegate({MakeParam(Def(KnownTypeCode::Int32))}, ITypePtr(t0));
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    EXPECT_TRUE(OutputTypeContainsUnfixed(state, lambda, *d));
}

// The same output shape with a FIXED type parameter yields false.
TEST(TypeInferenceContainsUnfixedTest, OutputTypeContainsUnfixedFalseWhenFixed) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    state[0].FixedTo = Def(KnownTypeCode::Int32);
    auto d = MakeDelegate({MakeParam(Def(KnownTypeCode::Int64))}, ITypePtr(t0));
    TestLambda lambda;
    EXPECT_FALSE(OutputTypeContainsUnfixed(state, lambda, *d));
}

// A plain (non-lambda, non-method-group) resolve result contributes neither input nor
// output types, so both wrappers yield false.
TEST(TypeInferenceContainsUnfixedTest, BothWrappersFalseForPlainResolveResult) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    auto d = MakeDelegate({MakeParam(ITypePtr(t0))}, ITypePtr(t0));
    TypeResolveResult plain(Def(KnownTypeCode::Int32));
    EXPECT_FALSE(InputTypesContainsUnfixed(state, plain, *d));
    EXPECT_FALSE(OutputTypeContainsUnfixed(state, plain, *d));
}

// A method group against the delegate contributes the delegate's parameter types (the
// InputTypes method-group arm), so the input-side predicate is true.
TEST(TypeInferenceContainsUnfixedTest, InputTypesContainsUnfixedForMethodGroup) {
    auto t0 = MakeTypeParam("T0", 0);
    std::vector<TP> state = MakeState({t0});
    auto d = MakeDelegate({MakeParam(ITypePtr(t0))}, Def(KnownTypeCode::Int32));
    auto mg = MakeMethodGroup();
    EXPECT_TRUE(InputTypesContainsUnfixed(state, *mg, *d));
}
