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

// Tests for the `TypeInference` DependsOn region (the third slice of the ~1188-line
// TypeInference long pole): `Detail::CalculateDependencyMatrix` + `Detail::DependsOn`
// (C# spec draft-v11 section 12.6.3.6 "Dependence").
//
// The load-bearing cruxes:
//  (a) the DIRECT-DEPENDENCE DIRECTION -- `dependencyMatrix[i, j] |= input.Occurs[j]
//      && output.Occurs[i]`: Xi depends on Xj when the argument's INPUT types contain Xj
//      AND its OUTPUT types contain Xi (`Invoke(T1) -> T2` makes T2 depend on T1, NOT the
//      other way around), and a `Func<T, T>`-shaped `Invoke(T) -> T` makes T depend on
//      ITSELF;
//  (b) the PLAIN-ARGUMENT exclusion -- only implicitly-typed-lambda / method-group
//      delegate signatures contribute input/output types, so a plain expression argument
//      produces NO dependence however much its formal parameter type mentions the type
//      parameters (and an EXPLICITLY-typed lambda loses the input side for the same
//      reason -- the `IsImplicitlyTyped` gate flowing through to the dependence relation);
//  (c) the OR-accumulation across arguments and the WARSHALL TRANSITIVE CLOSURE -- two
//      chained arguments (`T2 depends on T1`, `T3 depends on T2`) compose to
//      `T3 depends on T1` via the in-place closure;
//  (d) the common-min iteration bound -- the C# `InferTypeArguments` ctor sizes the
//      instance argument/parameter-type arrays to `Math.Min`, so only the leading common
//      prefix of the threaded vectors contributes.
//
// The stubs mirror the TypeInference test conventions: `MethodHostType` (a
// `LookupTypeDefinition` whose `GetMethods` returns a configured `Invoke`), `TestParameter`,
// `TestLambda` (the `InputOutputTypes` test's stubs), and the local `VisitableTypeParameter`
// (the `AcceptVisitor -> VisitTypeParameter` bridge the `OccursInVisitor` records through --
// the `ContainsUnfixed` test's precedent; a plain `LookupTypeParameter` routes to
// `VisitOtherType` and is never recorded). The delegate signature's parameter/return types
// REUSE the very `VisitableTypeParameter` instances the `TP` state wraps (the occurrence
// test is reference equality). Each test file carries its own anonymous-namespace stubs
// (the established convention).

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
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
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
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::CalculateDependencyMatrix;
using ILSpy::Decompiler::CSharp::Resolver::Detail::DependsOn;
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
// return type (the `Func`-like shape the input/output-type collectors resolve). The
// parameter/return types REUSE the caller's type-parameter instances (the occurrence test is
// reference equality -- the `OccursInVisitor` compares against the `TP` entries' pointers).
std::shared_ptr<MethodHostType> MakeDelegate(
    const std::vector<std::shared_ptr<TestParameter>>& params, ITypePtr returnType) {
    auto host = MakeHost("D");
    const IMethod* invoke = MakeMethod("Invoke");
    ConfigureMethod(invoke, params, returnType);
    host->SetMethods({invoke});
    return host;
}

// A concrete `LambdaResolveResult` stub with a configurable `IsImplicitlyTyped` (the flag the
// `InputTypes` arm reads). The D534 `TestLambda` precedent (public fields for
// configurability; `IsValid` is never invoked by the dependence computation, but the pure
// virtual must be implemented).
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

// An implicitly-typed lambda argument (kept alive in a static vector -- the arguments vector
// holds owning shared_ptrs, but keeping a static registry mirrors the shared stub-lifetime
// convention and makes the call sites terse).
std::shared_ptr<ResolveResult> ImplicitlyTypedLambda() {
    static std::vector<std::shared_ptr<ResolveResult>> keep;
    auto l = std::make_shared<TestLambda>();  // isImplicitlyTyped defaults true
    keep.push_back(l);
    return l;
}

// An EXPLICITLY-typed lambda argument (the `InputTypes` gate excludes its parameter types
// from the dependence computation).
std::shared_ptr<ResolveResult> ExplicitlyTypedLambda() {
    static std::vector<std::shared_ptr<ResolveResult>> keep;
    auto l = std::make_shared<TestLambda>();
    l->isImplicitlyTyped = false;
    keep.push_back(l);
    return l;
}

} // namespace

// ===========================================================================
// CalculateDependencyMatrix -- the n x n dependence matrix.
// ===========================================================================

// With no type parameters the matrix is empty (the C# `new bool[0, 0]`).
TEST(TypeInferenceDependsOnTest, MatrixEmptyForNoTypeParameters) {
    std::vector<TP> state;  // n = 0
    std::vector<std::shared_ptr<ResolveResult>> arguments;
    std::vector<ITypePtr> parameterTypes;
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_TRUE(matrix.empty());
}

// The matrix dimensions are n x n regardless of the argument count (the C# `new bool[n, n]`).
TEST(TypeInferenceDependsOnTest, MatrixDimensionsMatchTypeParameters) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    std::vector<std::shared_ptr<ResolveResult>> arguments;
    std::vector<ITypePtr> parameterTypes;
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    ASSERT_EQ(matrix.size(), 2u);
    ASSERT_EQ(matrix[0].size(), 2u);
    ASSERT_EQ(matrix[1].size(), 2u);
    EXPECT_FALSE(matrix[0][0]);
    EXPECT_FALSE(matrix[0][1]);
    EXPECT_FALSE(matrix[1][0]);
    EXPECT_FALSE(matrix[1][1]);
}

// The PLAIN-ARGUMENT exclusion crux: a plain expression contributes NO input/output types
// (only implicitly-typed-lambda / method-group delegate signatures do), so the matrix stays
// all-false even though the FORMAL PARAMETER TYPE IS the type parameter itself
// (`M<T>(T x)` called with a plain `int` argument produces no dependence).
TEST(TypeInferenceDependsOnTest, PlainArgumentProducesNoDependence) {
    auto t1 = MakeTypeParam("T1", 0);
    auto state = MakeState({t1});
    std::shared_ptr<ResolveResult> plain =
        std::make_shared<TypeResolveResult>(Def(KnownTypeCode::Int32));
    std::vector<std::shared_ptr<ResolveResult>> arguments{plain};
    std::vector<ITypePtr> parameterTypes{t1};  // the formal parameter type IS T1
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    ASSERT_EQ(matrix.size(), 1u);
    EXPECT_FALSE(matrix[0][0]);
}

// The SELF-DEPENDENCY crux: `Invoke(T) -> T` (the `Func<T, T>` shape) makes the input and
// the output contain the SAME type parameter, so it depends on itself -- the fix round must
// not fix it while any unfixed partner remains.
TEST(TypeInferenceDependsOnTest, SelfDependencyForFuncTT) {
    auto t = MakeTypeParam("T", 0);
    auto state = MakeState({t});
    auto d = MakeDelegate({MakeParam(t)}, t);  // Invoke(T) -> T
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    ASSERT_EQ(matrix.size(), 1u);
    EXPECT_TRUE(matrix[0][0]);
}

// The DIRECT-DEPENDENCE DIRECTION crux: `Invoke(T1) -> T2` puts the dependence at
// [T2][T1] (Xi=T2 depends on Xj=T1: the input contains T1, the output contains T2) -- NOT at
// [T1][T2], and neither diagonal entry fires.
TEST(TypeInferenceDependsOnTest, OutputDependsOnInputForFuncT1T2) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, t2);  // Invoke(T1) -> T2
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    ASSERT_EQ(matrix.size(), 2u);
    EXPECT_TRUE(matrix[1][0]);   // T2 depends on T1
    EXPECT_FALSE(matrix[0][1]);  // T1 does NOT depend on T2
    EXPECT_FALSE(matrix[0][0]);
    EXPECT_FALSE(matrix[1][1]);
}

// An input side without any type parameter (`Invoke(int) -> T2`) produces no dependence:
// `input.Occurs[j]` is false for every j.
TEST(TypeInferenceDependsOnTest, NoDependenceWhenInputLacksTypeParameter) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(Def(KnownTypeCode::Int32))}, t2);  // Invoke(int) -> T2
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_FALSE(matrix[0][0]);
    EXPECT_FALSE(matrix[0][1]);
    EXPECT_FALSE(matrix[1][0]);
    EXPECT_FALSE(matrix[1][1]);
}

// An output side without any type parameter (`Invoke(T1) -> int`) produces no dependence:
// `output.Occurs[i]` is false for every i.
TEST(TypeInferenceDependsOnTest, NoDependenceWhenOutputLacksTypeParameter) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, Def(KnownTypeCode::Int32));  // Invoke(T1) -> int
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_FALSE(matrix[0][0]);
    EXPECT_FALSE(matrix[0][1]);
    EXPECT_FALSE(matrix[1][0]);
    EXPECT_FALSE(matrix[1][1]);
}

// The EXPLICITLY-TYPED-LAMBDA crux: the `InputTypes` `IsImplicitlyTyped` gate flows through
// to the dependence relation -- an explicitly-typed lambda's input types are already known,
// so no `input.Occurs[j]` fires and the matrix stays all-false (the return type alone cannot
// produce a dependence).
TEST(TypeInferenceDependsOnTest, ExplicitlyTypedLambdaProducesNoDependence) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, t2);  // Invoke(T1) -> T2
    std::vector<std::shared_ptr<ResolveResult>> arguments{ExplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_FALSE(matrix[0][0]);
    EXPECT_FALSE(matrix[0][1]);
    EXPECT_FALSE(matrix[1][0]);
    EXPECT_FALSE(matrix[1][1]);
}

// A METHOD GROUP contributes both the input and the output types of the delegate signature,
// producing the same dependence an implicitly-typed lambda would.
TEST(TypeInferenceDependsOnTest, MethodGroupProducesDependence) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, t2);  // Invoke(T1) -> T2
    std::vector<std::shared_ptr<ResolveResult>> arguments{MakeMethodGroup()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_TRUE(matrix[1][0]);   // T2 depends on T1
    EXPECT_FALSE(matrix[0][1]);
}

// The NESTED-OCCURRENCE crux: the type parameter inside a generic type argument
// (`Invoke(IList<T1>) -> T2`) still occurs -- the `OccursInVisitor` recursion through
// `ParameterizedType::VisitChildren` records it.
TEST(TypeInferenceDependsOnTest, OccurrenceThroughGenericArgumentProducesDependence) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    // IList<T1> -- a ParameterizedType over an interface-shaped generic (the Kind delegates
    // to the generic; only the visitor traversal matters here).
    auto generic = MakeHost("IList", TypeKind::Interface);
    ITypePtr listOfT1 = std::make_shared<ParameterizedType>(
        generic, std::vector<ITypePtr>{t1});
    auto d = MakeDelegate({MakeParam(listOfT1)}, t2);  // Invoke(IList<T1>) -> T2
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_TRUE(matrix[1][0]);   // T2 depends on T1 (nested in the input)
    EXPECT_FALSE(matrix[0][1]);
}

// The OR-ACCUMULATION + WARSHALL TRANSITIVE CLOSURE crux: two chained arguments
// (arg0: `Invoke(T1) -> T2`; arg1: `Invoke(T2) -> T3`) accumulate both direct dependences
// AND compose them -- `T3 depends on T1` exists ONLY via the closure (`T3`'s input mentions
// `T2`, never `T1`).
TEST(TypeInferenceDependsOnTest, OrAccumulationAndTransitiveClosure) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto t3 = MakeTypeParam("T3", 2);
    auto state = MakeState({t1, t2, t3});
    auto d1 = MakeDelegate({MakeParam(t1)}, t2);  // Invoke(T1) -> T2
    auto d2 = MakeDelegate({MakeParam(t2)}, t3);  // Invoke(T2) -> T3
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda(),
                                                          ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d1, d2};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    // The direct dependences accumulated across the two arguments:
    EXPECT_TRUE(matrix[1][0]);  // T2 depends on T1 (arg0)
    EXPECT_TRUE(matrix[2][1]);  // T3 depends on T2 (arg1)
    // The transitive closure composes them:
    EXPECT_TRUE(matrix[2][0]);  // T3 depends on T1 (via T2)
    // The reverse directions stay false:
    EXPECT_FALSE(matrix[0][1]);
    EXPECT_FALSE(matrix[0][2]);
    EXPECT_FALSE(matrix[1][2]);
}

// The COMMON-MIN bound crux: the C# `InferTypeArguments` ctor sizes the instance arrays to
// `Math.Min(arguments.Count, parameterTypes.Count)`, so only the leading common prefix
// contributes -- the second argument's would-be dependence is absent when the parameter-type
// list is shorter.
TEST(TypeInferenceDependsOnTest, CommonMinBoundLimitsIteration) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d1 = MakeDelegate({MakeParam(t1)}, t2);  // Invoke(T1) -> T2
    auto d2 = MakeDelegate({MakeParam(t2)}, t1);  // Invoke(T2) -> T1 (the reverse direction)
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda(),
                                                          ImplicitlyTypedLambda()};
    // Only ONE parameter type: the second argument (d2's dependence [T1][T2]) is beyond the
    // common min and never contributes.
    std::vector<ITypePtr> parameterTypes{d1};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_TRUE(matrix[1][0]);  // T2 depends on T1 (arg0)
    EXPECT_FALSE(matrix[0][1]); // the unprocessed arg1 direction stays false
}

// ===========================================================================
// DependsOn -- the matrix lookup by `TP` index.
// ===========================================================================

// `DependsOn(x, y)` reads the matrix at `[x.Index, y.Index]` -- the T2-depends-on-T1
// direction of `Invoke(T1) -> T2`, and NOT the reverse.
TEST(TypeInferenceDependsOnTest, DependsOnLooksUpByTPIndex) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, t2);  // Invoke(T1) -> T2
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_TRUE(DependsOn(matrix, state[1], state[0]));  // T2 depends on T1
    EXPECT_FALSE(DependsOn(matrix, state[0], state[1])); // T1 does not depend on T2
    EXPECT_FALSE(DependsOn(matrix, state[0], state[0]));
    EXPECT_FALSE(DependsOn(matrix, state[1], state[1]));
}

// The self-dependency of `Invoke(T) -> T` reads through `DependsOn(T, T)`.
TEST(TypeInferenceDependsOnTest, DependsOnSelfForFuncTT) {
    auto t = MakeTypeParam("T", 0);
    auto state = MakeState({t});
    auto d = MakeDelegate({MakeParam(t)}, t);  // Invoke(T) -> T
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    EXPECT_TRUE(DependsOn(matrix, state[0], state[0]));
}

// An empty matrix (n = 0) makes every lookup false: the bounds guard (the C# would throw
// `IndexOutOfRangeException`; the port returns false -- the documented D516 deviation).
TEST(TypeInferenceDependsOnTest, DependsOnEmptyMatrixReturnsFalse) {
    std::vector<std::vector<bool>> matrix;  // n = 0
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    std::vector<TP> state = MakeState({t1, t2});
    EXPECT_FALSE(DependsOn(matrix, state[0], state[1]));
    EXPECT_FALSE(DependsOn(matrix, state[1], state[0]));
}

// An out-of-range `TP` index (beyond the matrix size) returns false via the bounds guard
// (the C# would throw; the documented D516 deviation). The `TP` need not be a member of the
// inference's state vector -- `DependsOn` reads only the two `Index` values.
TEST(TypeInferenceDependsOnTest, DependsOnOutOfRangeIndexReturnsFalse) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, t2);
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    auto strayHigh = MakeTypeParam("TStray", 5);  // beyond the n=2 matrix
    TP stray(*strayHigh);
    EXPECT_FALSE(DependsOn(matrix, stray, state[0]));
    EXPECT_FALSE(DependsOn(matrix, state[0], stray));
}

// A negative `TP` index returns false via the bounds guard (the C# would throw; the
// documented D516 deviation).
TEST(TypeInferenceDependsOnTest, DependsOnNegativeIndexReturnsFalse) {
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    auto state = MakeState({t1, t2});
    auto d = MakeDelegate({MakeParam(t1)}, t2);
    std::vector<std::shared_ptr<ResolveResult>> arguments{ImplicitlyTypedLambda()};
    std::vector<ITypePtr> parameterTypes{d};
    auto matrix = CalculateDependencyMatrix(state, arguments, parameterTypes);
    auto strayNeg = MakeTypeParam("TNeg", -1);
    TP stray(*strayNeg);
    EXPECT_FALSE(DependsOn(matrix, stray, state[0]));
    EXPECT_FALSE(DependsOn(matrix, state[0], stray));
}
