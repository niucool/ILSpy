// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Detail::IsExactlyMatching` (CSharpConversions.cs line 1615, C# 8.0 spec section
// 12.6.4.6 "exactly matching expression") -- whether an expression `e` exactly matches a type
// `t`. The direct prerequisite for the deferred `BetterConversion(ResolveResult, IType, IType)`
// overload (line 1540, the "better conversion from expression"); `BetterConversion` calls
// `IsExactlyMatching` on both candidate targets before falling back to the type-based dispatch.
//
// CRUX STUB CONVENTIONS (carried from the D534 `AnonymousFunctionConversion` test):
//  * `MethodHostType` is a `LookupTypeDefinition` subclass whose `GetMethods(filter, options)`
//    returns a configured list (applying the filter faithfully). The delegate type is a
//    `MethodHostType` with `TypeKind::Delegate` whose method table holds the `Invoke` method
//    (named `"Invoke"` so the `GetDelegateInvokeMethod` filter matches). A non-delegate kind
//    (e.g. `Class`) pins the `Kind == Delegate` guard short-circuit -> null.
//  * `Def(ktc)` is the value-type `LookupTypeDefinition` stub (the D514 precedent);
//    `StructuralEquals` is IDENTITY equality, so the identity-conversion crux cases reuse the
//    SAME instance for both sides.
//  * `TestParameter(type, refKind)` is the `IParameter` stub; the `SymbolKind()` /
//    `ReferenceKind()` accessors hide the namespace-scope enums of the same name for the rest of
//    the class body (the D402 cross-scope name-hiding crux), so the enum references are fully
//    qualified with `::ILSpy::Decompiler::TypeSystem::`.
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetReturnType` /
//    `SetParameters`); kept alive in a static vector so the raw `const IMethod*` outlives the
//    call. The delegate's `Invoke` is a `LookupMethod` named `"Invoke"`.
//  * `TestLambda` is a `LambdaResolveResult` subclass with a CONFIGURABLE `GetInferredReturnType`
//    (returns the `returnType` field) -- the D473 / D534 `TestLambda` precedent. The lambda's
//    `IsAnonymousMethod` / `IsAsync` flags are configurable to pin the expression-tree unwrap
//    and the `Task<T>` unpack arms.
//  * `TaskOf(element, name)` builds `Task<T>` as a `ParameterizedType` over a `Task`1`
//    `LookupTypeDefinition` (`KnownTypeCode::TaskOfT`, arity 1). For the async-unpack-returns-true
//    crux, two DISTINCT `Task`1` definitions (different names, both `KnownTypeCode::TaskOfT`)
//    over the SAME element instance exercise the unpack+recursion code path: the wrappers are
//    NOT identity-equal (different definitions) but the unpacked inner types ARE (same instance).
//    This is a test-only construction isomorphic to the real scenario (a custom task-like type
//    vs the regular `Task<T>`, the only real pair of task-like types whose wrappers differ but
//    whose inner types can match); the code path under test -- "unpack both, recurse on the
//    inner types" -- is identical.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::IsExactlyMatching
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"          // LambdaResolveResult (the TestLambda base) + LambdaConversion (the IsValid stub result)
#include "Decompiler/Semantics/ConversionFactories.hpp"                // Conversions (unused here but keeps the LambdaConversion link)
#include "Decompiler/Semantics/ResolveResult.hpp"                     // ResolveResult (the plain-ResolveResult sentinel + the recursion re-wrap)
#include "Decompiler/Semantics/TypeResolveResult.hpp"                  // TypeResolveResult (the TestLambda Body stub type)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsExactlyMatching;
using ILSpy::Decompiler::CSharp::Resolver::LambdaConversion;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
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

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind`. The D514 `MakeDef` precedent; `StructuralEquals` is
// IDENTITY equality, so the identity-conversion crux cases reuse the SAME instance.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// A `LookupTypeDefinition` subclass whose `GetMethods(filter, options)` returns a configured list,
// applying the filter faithfully (the D533 / D534 `MethodHostType` precedent). The delegate type
// is a `MethodHostType` with `TypeKind::Delegate`.
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
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

// A minimal `IParameter` with a configurable type and reference kind. The `SymbolKind()` /
// `ReferenceKind()` accessors hide the namespace-scope enums of the same name for the rest of
// the class body (the D402 cross-scope name-hiding crux), so the enum references are fully
// qualified with `::ILSpy::Decompiler::TypeSystem::`. The D524/D531/D534 `TestParameter` precedent.
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
    const IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `LookupMethod` kept alive in a static vector so the raw `const IMethod*` outlives the call.
const IMethod* MakeMethod(std::string name = "M") {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A `TestParameter` kept alive in a static vector so the raw `const IParameter*` the method holds
// outlives the call.
std::shared_ptr<TestParameter> MakeParam(ITypePtr type, ReferenceKind rk = ReferenceKind::None) {
    static std::vector<std::shared_ptr<TestParameter>> keep;
    auto p = std::make_shared<TestParameter>(std::move(type), rk);
    keep.push_back(p);
    return p;
}

// Configure a method: set its parameters (from the kept `TestParameter` shared_ptrs) and its
// return type. Returns the same raw `const IMethod*` for chaining. The D531/D534 `Configure`
// precedent.
const IMethod* Configure(const IMethod* method, const std::vector<std::shared_ptr<TestParameter>>& params,
                         ITypePtr returnType) {
    auto* m = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(method));
    std::vector<const IParameter*> paramPtrs;
    for (const auto& p : params)
        paramPtrs.push_back(p.get());
    m->SetParameters(std::move(paramPtrs));
    m->SetReturnType(std::move(returnType));
    return method;
}

// A `LookupTypeDefinition` for the `System.Linq.Expressions.Expression`1` generic definition (the
// expression-tree wrapper). The D534 `ExpressionDef` precedent.
std::shared_ptr<LookupTypeDefinition> ExpressionDef() {
    return std::make_shared<LookupTypeDefinition>(
        "Expression", "System.Linq.Expressions",
        FullTypeName(TopLevelTypeName("System.Linq.Expressions", "Expression", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
}

// `Expression<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `Expression`1` definition). The D534 `ExpressionOf` precedent.
ITypePtr ExpressionOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(ExpressionDef(), std::vector<ITypePtr>{std::move(element)});
}

// A `LookupTypeDefinition` for a `System.Threading.Tasks.Task`1` generic definition
// (`KnownTypeCode::TaskOfT`, arity 1). The `name` parameter allows constructing DISTINCT
// `Task`1` definitions (the async-unpack crux -- two definitions over the same element exercise
// the unpack+recursion code path; the default name is the real `"Task`1"`).
std::shared_ptr<LookupTypeDefinition> TaskOfTDef(const std::string& name = "Task`1") {
    return std::make_shared<LookupTypeDefinition>(name, "System.Threading.Tasks",
        FullTypeName(TopLevelTypeName("System.Threading.Tasks", name, 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::TaskOfT);
}

// `Task<T>` over the supplied element type (a 1-arg `ParameterizedType` over a `Task`1`
// definition). The `name` selects which `Task`1` definition (the default is the real one).
ITypePtr TaskOf(ITypePtr element, const std::string& name = "Task`1") {
    return std::make_shared<ParameterizedType>(TaskOfTDef(name), std::vector<ITypePtr>{std::move(element)});
}

// The concrete `LambdaResolveResult` test subclass with a CONFIGURABLE `GetInferredReturnType`
// (returns the `returnType` field) and configurable `IsAnonymousMethod` / `IsAsync` flags. The
// D473 / D534 `TestLambda` precedent; `IsValid` is unused by `IsExactlyMatching` but the abstract
// base requires an override, so it returns a fresh `LambdaConversion`.
class TestLambda : public LambdaResolveResult {
public:
    bool hasParameterList = true;
    bool isAnonymousMethod = false;
    bool isImplicitlyTyped = true;
    bool isAsync = false;
    ITypePtr returnType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    std::vector<const IParameter*> parameters;
    std::shared_ptr<ResolveResult> body =
        std::make_shared<Sem::TypeResolveResult>(
            std::make_shared<TS::KnownType>(KnownTypeCode::Void));

    bool HasParameterList() const override { return hasParameterList; }
    bool IsAnonymousMethod() const override { return isAnonymousMethod; }
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped; }
    bool IsAsync() const override { return isAsync; }
    ITypePtr GetInferredReturnType(const std::vector<ITypePtr>&) const override { return returnType; }
    std::vector<const IParameter*> Parameters() const override { return parameters; }
    const IType& ReturnType() const override { return *returnType; }
    std::shared_ptr<Conversion> IsValid(const std::vector<ITypePtr>&,
                                        const ITypePtr&,
                                        CSharpConversions&) const override
    {
        return std::make_shared<LambdaConversion>();
    }
    ResolveResult& Body() const override { return *body; }
    std::unique_ptr<ResolveResult> ShallowClone() const override
    {
        return std::make_unique<TestLambda>(*this);
    }
protected:
    std::string ClassName() const override { return "TestLambda"; }
};

// Build a delegate type (a `MethodHostType` with `TypeKind::Delegate`) whose `Invoke` method has
// the supplied parameter types and return type. Returns the delegate type (the `Invoke` method
// is kept alive in the `MakeMethod` static vector).
std::shared_ptr<MethodHostType> MakeDelegate(ITypePtr returnType,
                                             const std::vector<std::shared_ptr<TestParameter>>& params = {}) {
    const IMethod* invoke = Configure(MakeMethod("Invoke"), params, std::move(returnType));
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    return delegateType;
}

} // namespace

// ===========================================================================
// Non-lambda resolve results -- the `IdentityConversion(e.Type, t)` check.
// ===========================================================================

// A plain (non-lambda) resolve result whose type identity-converts to `t` (the same instance)
// exactly matches. This is the common case (e.g. an `int` expression exactly matching `int`).
TEST(CSharpConversionsIsExactlyMatchingTest, NonLambdaIdentityMatchReturnsTrue)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto rr = std::make_shared<Sem::TypeResolveResult>(intT);
    EXPECT_TRUE(IsExactlyMatching(*rr, *intT));
}

// A plain resolve result whose type does NOT identity-convert to `t` (`int` vs `long`) does not
// exactly match -- the identity check fails and the resolve result is not a lambda.
TEST(CSharpConversionsIsExactlyMatchingTest, NonLambdaNonIdentityReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto rr = std::make_shared<Sem::TypeResolveResult>(intT);
    EXPECT_FALSE(IsExactlyMatching(*rr, *longT));
}

// ===========================================================================
// Lambda resolve results -- the delegate `Invoke` resolution.
// ===========================================================================

// A lambda whose target type is NOT a delegate (a `Class`, no `Invoke`) does not exactly match
// -- `GetDelegateInvokeMethod` yields null.
TEST(CSharpConversionsIsExactlyMatchingTest, LambdaWithNonDelegateTargetReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto lambda = std::make_shared<TestLambda>();
    lambda->returnType = intT;
    auto toType = MakeHost("C", TypeKind::Class);  // a Class, not a Delegate
    EXPECT_FALSE(IsExactlyMatching(*lambda, *toType));
}

// ===========================================================================
// Lambda resolve results -- the expression-tree unwrap (`!IsAnonymousMethod` guard).
// ===========================================================================

// A non-anonymous-method lambda converting to `Expression<D>` (where `D` is a delegate whose
// `Invoke` returns `int`) unwraps the `Expression<T>` wrapper before resolving the `Invoke`
// method, then the inferred return type (`int`) identity-matches the delegate return (`int`).
TEST(CSharpConversionsIsExactlyMatchingTest, LambdaUnwrapsExpressionTreeBeforeInvokeResolution)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto delegateType = MakeDelegate(intT);
    auto exprOfD = ExpressionOf(ITypePtr(delegateType));
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAnonymousMethod = false;
    lambda->returnType = intT;
    EXPECT_TRUE(IsExactlyMatching(*lambda, *exprOfD));
}

// An anonymous-method lambda (`IsAnonymousMethod == true`) converting to `Expression<D>` does
// NOT unwrap the wrapper -- `GetDelegateInvokeMethod(Expression<D>)` yields null (an
// `Expression<D>` is a `Class`, not a `Delegate`) -> does not exactly match. This pins the
// `!lambda.IsAnonymousMethod` guard (anonymous methods cannot convert to expression trees).
TEST(CSharpConversionsIsExactlyMatchingTest, AnonymousMethodDoesNotUnwrapExpressionTree)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto delegateType = MakeDelegate(intT);
    auto exprOfD = ExpressionOf(ITypePtr(delegateType));
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAnonymousMethod = true;
    lambda->returnType = intT;
    EXPECT_FALSE(IsExactlyMatching(*lambda, *exprOfD));
}

// ===========================================================================
// Lambda resolve results -- the inferred-return identity check.
// ===========================================================================

// A lambda whose inferred return type identity-matches the delegate `Invoke` return type (the
// same `int` instance) exactly matches. This is the main lambda success path.
TEST(CSharpConversionsIsExactlyMatchingTest, LambdaInferredReturnIdentityMatchesDelegateReturn)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto delegateType = MakeDelegate(intT);
    auto lambda = std::make_shared<TestLambda>();
    lambda->returnType = intT;  // GetInferredReturnType -> int
    EXPECT_TRUE(IsExactlyMatching(*lambda, *delegateType));
}

// A lambda whose inferred return type does NOT identity-match the delegate return (`int` vs
// `long`), non-async, does not exactly match. The recursion `IsExactlyMatching(new
// ResolveResult(int), long)` re-checks identity on the plain resolve result (not a lambda) and
// returns false.
TEST(CSharpConversionsIsExactlyMatchingTest, LambdaInferredReturnNonIdentityNonAsyncReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto delegateType = MakeDelegate(longT);  // Invoke returns long
    auto lambda = std::make_shared<TestLambda>();
    lambda->returnType = intT;  // GetInferredReturnType -> int
    EXPECT_FALSE(IsExactlyMatching(*lambda, *delegateType));
}

// ===========================================================================
// Async lambdas -- the `Task<T>` unpack arm.
// ===========================================================================

// An async lambda whose inferred return type identity-matches the delegate return type (both
// `Task<int>` over the SAME definition and element instance) exactly matches via the FIRST
// identity check (the async unpack arm does not fire -- the wrappers already match).
TEST(CSharpConversionsIsExactlyMatchingTest, AsyncLambdaFirstCheckIdentityReturnsTrue)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto taskOfInt = TaskOf(ITypePtr(intT));
    auto delegateType = MakeDelegate(taskOfInt);  // Invoke returns Task<int>
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAsync = true;
    lambda->returnType = taskOfInt;  // GetInferredReturnType -> Task<int> (same instance)
    EXPECT_TRUE(IsExactlyMatching(*lambda, *delegateType));
}

// An async lambda whose inferred return and delegate return unpack to DISTINCT inner types
// (`Task<int_A>` vs `Task<int_B>`, different element instances) does not exactly match: the
// first identity check fails (different elements), the async unpack yields `int_A` / `int_B`,
// and the recursion `IsExactlyMatching(ResolveResult(int_A), int_B)` re-checks identity (false).
TEST(CSharpConversionsIsExactlyMatchingTest, AsyncLambdaUnpackMismatchReturnsFalse)
{
    auto intA = Def(KnownTypeCode::Int32);
    auto intB = Def(KnownTypeCode::Int32);
    auto taskOfA = TaskOf(ITypePtr(intA));
    auto taskOfB = TaskOf(ITypePtr(intB));
    auto delegateType = MakeDelegate(taskOfB);  // Invoke returns Task<int_B>
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAsync = true;
    lambda->returnType = taskOfA;  // GetInferredReturnType -> Task<int_A>
    EXPECT_FALSE(IsExactlyMatching(*lambda, *delegateType));
}

// An async lambda whose inferred return and delegate return are DIFFERENT task-like wrappers
// (two distinct `Task`1` definitions) over the SAME element instance exactly matches: the first
// identity check fails (the wrappers are not identity-equal -- different definitions), the
// async unpack yields the SAME inner type from both, and the recursion
// `IsExactlyMatching(ResolveResult(int), int)` identity-matches. This is the load-bearing crux
// pinning the async unpack+recursion arm (the only path where the unpack changes the verdict
// from the first identity check); the two-`Task`1`-definition construction is isomorphic to the
// real custom-task-vs-`Task<T>` scenario for the code path under test.
TEST(CSharpConversionsIsExactlyMatchingTest, AsyncLambdaUnpackMatchReturnsTrue)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto taskA = TaskOf(ITypePtr(intT), "Task`1");     // inferred return: Task<int> over def A
    auto taskB = TaskOf(ITypePtr(intT), "Task`1_alt"); // delegate return: Task<int> over def B
    auto delegateType = MakeDelegate(taskB);          // Invoke returns Task<int> (def B)
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAsync = true;
    lambda->returnType = taskA;  // GetInferredReturnType -> Task<int> (def A)
    EXPECT_TRUE(IsExactlyMatching(*lambda, *delegateType));
}

// An async lambda whose inferred return type is NOT a task-like type (`int`, not `Task<int>`)
// does not exactly match: the first identity check fails (`int` vs `Task<int>`), the async unpack
// nulls the inferred return (`UnpackTask(int) == null` -- `int` is not a task), and the
// `x != null && y != null` guard fails. This pins the null guard on the unpacked inferred return.
TEST(CSharpConversionsIsExactlyMatchingTest, AsyncLambdaNonTaskInferredReturnReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto taskOfInt = TaskOf(ITypePtr(intT));
    auto delegateType = MakeDelegate(taskOfInt);  // Invoke returns Task<int>
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAsync = true;
    lambda->returnType = intT;  // GetInferredReturnType -> int (NOT a task)
    EXPECT_FALSE(IsExactlyMatching(*lambda, *delegateType));
}

// A non-async lambda whose inferred return type is a `Task<T>` while the delegate return is the
// UNWRAPPED `T` does not exactly match: the first identity check fails (`Task<int>` vs `int`),
// the async unpack does NOT fire (non-async), and the recursion
// `IsExactlyMatching(ResolveResult(Task<int>), int)` re-checks identity (false). This pins that
// the unpack arm fires ONLY for async lambdas.
TEST(CSharpConversionsIsExactlyMatchingTest, NonAsyncLambdaDoesNotUnpackTaskReturn)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto taskOfInt = TaskOf(ITypePtr(intT));
    auto delegateType = MakeDelegate(intT);  // Invoke returns int (unwrapped)
    auto lambda = std::make_shared<TestLambda>();
    lambda->isAsync = false;
    lambda->returnType = taskOfInt;  // GetInferredReturnType -> Task<int>
    EXPECT_FALSE(IsExactlyMatching(*lambda, *delegateType));
}
