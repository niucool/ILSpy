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
// PURPOSE NONINFRINGEMENT. SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the `CSharpConversions` anonymous-function-conversion region (CSharpConversions.cs
// lines 1280-1360, C# 9.0 spec section 10.7 "anonymous function conversions"):
//   * `Detail::UnpackExpressionTreeType` (line 1348) -- the `Expression<T>` wrapper stripper.
//   * `Detail::AnonymousFunctionConversion` (line 1280) -- the lambda/anonymous-method ->
//     delegate-type conversion.
// Plus the now-wired anonymous-function arm of the ResolveResult-based
// `Detail::ImplicitConversion` dispatch (D528) reached via the public
// `CSharpConversions::ImplicitConversion(ResolveResult, IType)` entry.
//
// CRUX STUB CONVENTIONS (carried from the D531/D532/D533 delegate-compatible tests):
//  * `MethodHostType` is a `LookupTypeDefinition` subclass whose `GetMethods(filter, options)`
//    returns a configured list (applying the filter faithfully). The delegate type is a
//    `MethodHostType` with `TypeKind::Delegate` whose method table holds the `Invoke` method
//    (named `"Invoke"` so the `GetDelegateInvokeMethod` filter matches). A non-delegate kind
//    (e.g. `Class`) pins the `Kind == Delegate` guard short-circuit -> null.
//  * `Def(ktc)` / `Ref(ktc)` are the value-type / reference-type `LookupTypeDefinition` stubs
//    (the D514 precedent); `StructuralEquals` is IDENTITY equality, so the identity-conversion
//    crux cases reuse the SAME instance for both sides.
//  * `TestParameter(type, refKind)` is the `IParameter` stub (the D524/D531 precedent); the
//    `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same
//    name for the rest of the class body (the D402 cross-scope name-hiding crux), so the enum
//    references are fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetReturnType` /
//    `SetParameters`); kept alive in a static vector so the raw `const IMethod*` outlives the
//    call. The delegate's `Invoke` is a `LookupMethod` named `"Invoke"`.
//  * `TestLambda` is a `LambdaResolveResult` subclass with a CONFIGURABLE `IsValid` return
//    (a fresh `LambdaConversion` for the success crux cases, `Conversions::None()` for the
//    failure sentinels) that RECORDS the `parameterTypes` / `returnType` it received -- the
//    D473 `TestLambdaResolveResult` precedent, extended with a configurable `IsValid`. The C#
//    `IsValid` returns the `LambdaConversion.Instance` singleton; the port's stub returns a
//    fresh `make_shared<LambdaConversion>()` for success (the singleton ownership is a
//    function-local static a `shared_ptr` constructed from `&Instance()` would unsafely
//    `delete`; a fresh instance carries the same flags and lets the test assert flags rather
//    than singleton pointer-identity).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public entry + Get)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::UnpackExpressionTreeType / Detail::AnonymousFunctionConversion
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"          // LambdaResolveResult (the TestLambda base) + LambdaConversion (the IsValid success result)
#include "Decompiler/Semantics/ConversionFactories.hpp"                // Conversions (the None singleton)
#include "Decompiler/Semantics/ResolveResult.hpp"                     // ResolveResult (the plain-ResolveResult dispatch sentinel)
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
using ILSpy::Decompiler::CSharp::Resolver::Detail::AnonymousFunctionConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::UnpackExpressionTreeType;
using ILSpy::Decompiler::CSharp::Resolver::LambdaConversion;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IAttribute;
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
// applying the filter faithfully (the D533 `MethodHostType` precedent). The delegate type is a
// `MethodHostType` with `TypeKind::Delegate`; the call count is recorded so the guard-ordering
// cruxes are pinnable.
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    int GetMethodsCallCount() const { return getMethodsCallCount_; }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
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
// qualified with `::ILSpy::Decompiler::TypeSystem::`. The D524/D531 `TestParameter` precedent.
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
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
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
// return type. Returns the same raw `const IMethod*` for chaining. The D531 `Configure`
// precedent (without the `ReturnTypeIsRefReadOnly` flag, which the anonymous-function conversion
// does not read).
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
// expression-tree wrapper). Constructed with `Name == "Expression"` (the C# type system strips
// the backtick arity in `Name`; the stub models this by passing the bare name) and
// `Namespace == "System.Linq.Expressions"`, arity 1. The D514 `MakeDef` shape with the real
// namespace/name.
std::shared_ptr<LookupTypeDefinition> ExpressionDef() {
    return std::make_shared<LookupTypeDefinition>(
        "Expression", "System.Linq.Expressions",
        FullTypeName(TopLevelTypeName("System.Linq.Expressions", "Expression", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
}

// `Expression<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `Expression`1` definition).
ITypePtr ExpressionOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(ExpressionDef(), std::vector<ITypePtr>{std::move(element)});
}

// A `LookupTypeDefinition` subclass that reports `GetDefinition() == nullptr` despite carrying
// `Name == "Expression"` -- pins the `UnpackExpressionTreeType` null-definition guard (a
// `ParameterizedType` whose generic resolves no definition passes through, faithfully matching
// the C# where the generic's `Namespace` would be empty for a non-definition).
class NonDefExpressionType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    const ITypeDefinition* GetDefinition() const override { return nullptr; }
};

// A `NonDefExpressionType` named `"Expression"` in `"System.Linq.Expressions"` (arity 1) -- the
// definitionless Expression-like generic for the null-guard crux.
std::shared_ptr<NonDefExpressionType> NonDefExpression() {
    return std::make_shared<NonDefExpressionType>(
        "Expression", "System.Linq.Expressions",
        FullTypeName(TopLevelTypeName("System.Linq.Expressions", "Expression", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
}

// The concrete `LambdaResolveResult` test subclass with a CONFIGURABLE `IsValid` return. The
// D473 `TestLambdaResolveResult` precedent, extended so `IsValid` records the
// `parameterTypes` / `returnType` it received and returns a caller-set `isValidResult` (a fresh
// `LambdaConversion` for the success crux cases, `Conversions::None()` for the failure
// sentinels). `ShallowClone`/`ClassName` override the D424 slicing-prevention / RTTI-name
// conventions.
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
    // The `IsValid` return: a fresh `LambdaConversion` (success) by default; tests set
    // `Conversions::None()` for the failure sentinels.
    std::shared_ptr<Conversion> isValidResult = std::make_shared<LambdaConversion>();
    // Records the `parameterTypes` / `returnType` the last `IsValid` call received.
    mutable std::vector<ITypePtr> lastIsValidParamTypes;
    mutable ITypePtr lastIsValidReturnType;

    bool HasParameterList() const override { return hasParameterList; }
    bool IsAnonymousMethod() const override { return isAnonymousMethod; }
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped; }
    bool IsAsync() const override { return isAsync; }
    ITypePtr GetInferredReturnType(const std::vector<ITypePtr>&) const override { return returnType; }
    std::vector<const IParameter*> Parameters() const override { return parameters; }
    const IType& ReturnType() const override { return *returnType; }
    std::shared_ptr<Conversion> IsValid(const std::vector<ITypePtr>& parameterTypes,
                                        const ITypePtr& returnTypeArg,
                                        CSharpConversions&) const override
    {
        lastIsValidParamTypes = parameterTypes;
        lastIsValidReturnType = returnTypeArg;
        return isValidResult;
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
// UnpackExpressionTreeType -- the Expression<T> wrapper stripper.
// ===========================================================================

// `Expression<int>` (a `ParameterizedType` over the `Expression`1` definition, arity 1) unpacks
// to its single type argument `int`. This pins the name + namespace + arity match and the
// type-argument return.
TEST(CSharpConversionsUnpackExpressionTreeTest, UnpacksExpressionTreeToTypeArgument)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto expr = ExpressionOf(ITypePtr(intT));
    const IType& result = UnpackExpressionTreeType(*expr);
    EXPECT_EQ(&result, intT.get());
}

// A `ParameterizedType` over a non-`Expression` generic (`List<int>`, namespace
// `System.Collections.Generic`) passes through unchanged -- the name/namespace check fails.
TEST(CSharpConversionsUnpackExpressionTreeTest, PassesThroughNonExpressionParameterizedType)
{
    auto listDef = std::make_shared<LookupTypeDefinition>(
        "List", "System.Collections.Generic",
        FullTypeName(TopLevelTypeName("System.Collections.Generic", "List", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    auto intT = Def(KnownTypeCode::Int32);
    auto listOfInt = std::make_shared<ParameterizedType>(listDef, std::vector<ITypePtr>{intT});
    const IType& result = UnpackExpressionTreeType(*listOfInt);
    EXPECT_EQ(&result, listOfInt.get());
}

// A non-`ParameterizedType` (a plain `Def(Int32)`) passes through unchanged -- the
// `dynamic_cast<ParameterizedType>` yields null.
TEST(CSharpConversionsUnpackExpressionTreeTest, PassesThroughNonParameterizedType)
{
    auto intT = Def(KnownTypeCode::Int32);
    const IType& result = UnpackExpressionTreeType(*intT);
    EXPECT_EQ(&result, intT.get());
}

// A `ParameterizedType` whose generic is named `"Expression"` but in the WRONG namespace
// (`System`, not `System.Linq.Expressions`) passes through -- the namespace check fails.
TEST(CSharpConversionsUnpackExpressionTreeTest, DoesNotUnpackWrongNamespace)
{
    auto wrongNs = std::make_shared<LookupTypeDefinition>(
        "Expression", "System",
        FullTypeName(TopLevelTypeName("System", "Expression", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    auto intT = Def(KnownTypeCode::Int32);
    auto expr = std::make_shared<ParameterizedType>(wrongNs, std::vector<ITypePtr>{intT});
    const IType& result = UnpackExpressionTreeType(*expr);
    EXPECT_EQ(&result, expr.get());
}

// A `ParameterizedType` whose generic has the right namespace but the WRONG name (`"Other"`,
// not `"Expression"`) passes through -- the name check fails.
TEST(CSharpConversionsUnpackExpressionTreeTest, DoesNotUnpackWrongName)
{
    auto wrongName = std::make_shared<LookupTypeDefinition>(
        "Other", "System.Linq.Expressions",
        FullTypeName(TopLevelTypeName("System.Linq.Expressions", "Other", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    auto intT = Def(KnownTypeCode::Int32);
    auto expr = std::make_shared<ParameterizedType>(wrongName, std::vector<ITypePtr>{intT});
    const IType& result = UnpackExpressionTreeType(*expr);
    EXPECT_EQ(&result, expr.get());
}

// A `ParameterizedType` over an `Expression`-named generic with the wrong arity (2 type
// arguments, so `TypeParameterCount == 2`) passes through -- the `== 1` arity check fails.
TEST(CSharpConversionsUnpackExpressionTreeTest, DoesNotUnpackWrongArity)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    // The `Expression`1` definition with TWO type arguments (a degenerate shape the stub ctor
    // permits; the real ctor would reject it) -- `TypeParameterCount == 2` fails the check.
    auto expr = std::make_shared<ParameterizedType>(ExpressionDef(),
        std::vector<ITypePtr>{intT, longT});
    const IType& result = UnpackExpressionTreeType(*expr);
    EXPECT_EQ(&result, expr.get());
}

// A `ParameterizedType` over a generic that carries `Name == "Expression"` + arity 1 but
// resolves NO definition (`GetDefinition() == nullptr`) passes through -- the null-definition
// guard fails (the namespace cannot be read). This pins the defensive null guard.
TEST(CSharpConversionsUnpackExpressionTreeTest, DoesNotUnpackDefinitionlessExpressionLikeType)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto expr = std::make_shared<ParameterizedType>(NonDefExpression(),
        std::vector<ITypePtr>{intT});
    const IType& result = UnpackExpressionTreeType(*expr);
    EXPECT_EQ(&result, expr.get());
}

// ===========================================================================
// AnonymousFunctionConversion -- the lambda/anonymous-method -> delegate conversion.
// ===========================================================================

// A lambda whose toType carries no `Invoke` (a non-delegate kind, e.g. `Class`) returns `None`
// -- `GetDelegateInvokeMethod` yields null.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenDelegateHasNoInvoke)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto lambda = std::make_shared<TestLambda>();
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    auto toType = MakeHost("C", TypeKind::Class);  // a Class, not a Delegate
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *toType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// A `HasParameterList` lambda whose parameter count differs from the delegate's `Invoke`
// parameter count returns `None` -- the count guard fires.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenParameterCountMismatches)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get(), MakeParam(longT).get()};  // 2 params vs 1
    lambda->returnType = intT;
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// An implicitly-typed lambda converting to a delegate whose `Invoke` has a `ref` parameter
// returns `None` -- the implicitly-typed `ReferenceKind != None` guard fires.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenImplicitlyTypedLambdaHasRefParam)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT, ReferenceKind::Ref);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// An explicitly-typed lambda whose parameter `ReferenceKind` differs from the delegate's
// (`ref` vs `out`) returns `None` -- the explicit-typed `ReferenceKind` mismatch guard fires.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenExplicitlyTypedLambdaReferenceKindMismatches)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT, ReferenceKind::Out);
    auto lambdaP = MakeParam(intT, ReferenceKind::Ref);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = false;
    lambda->parameters = {lambdaP.get()};
    lambda->returnType = intT;
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// An explicitly-typed lambda whose parameter type is NOT identity-convertible to the delegate's
// (`int` vs `long`) returns `None` -- the explicit-typed identity guard fires.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenExplicitlyTypedLambdaTypeNotIdentity)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto invokeP = MakeParam(intT);
    auto lambdaP = MakeParam(longT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = false;
    lambda->parameters = {lambdaP.get()};
    lambda->returnType = intT;
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// A parameter-list-less anonymous method converting to a delegate whose `Invoke` has an `out`
// parameter returns `None` -- the no-parameter-list `out` rejection fires.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenNoParameterListLambdaHasOutParam)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT, ReferenceKind::Out);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = false;  // no parameter list -> accepts any param list except `out`
    lambda->returnType = intT;
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// A valid implicitly-typed lambda converting to a matching delegate returns the
// `LambdaConversion` the lambda's `IsValid` yields -- the success crux (all guards pass, the
// body verdict is non-`None`).
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsLambdaConversionWhenIsValidSucceeds)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    lambda->isValidResult = std::make_shared<LambdaConversion>();  // success
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsAnonymousFunctionConversion());
    EXPECT_TRUE(result->IsImplicit());
}

// A lambda whose `IsValid` returns `None` (the body is invalid for the delegate shape) returns
// `None` -- the body-verdict sentinel.
TEST(CSharpConversionsAnonymousFunctionTest, ReturnsNoneWhenIsValidFails)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    lambda->isValidResult = Conversions::None();  // the body is invalid
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// The delegate's parameter types / return type are threaded to `IsValid` verbatim: the recorded
// `lastIsValidParamTypes` / `lastIsValidReturnType` match the delegate's `Invoke` shape. This
// pins the `dParamTypes` / `dReturnType` construction (the `shared_from_this` +
// `const_pointer_cast` handle build) reaches `IsValid`, not a short-circuit.
TEST(CSharpConversionsAnonymousFunctionTest, DelegatesIsValidReceivesDelegateParamTypesAndReturnType)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto invokeP0 = MakeParam(intT);
    auto invokeP1 = MakeParam(longT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP0, invokeP1}, longT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get(), MakeParam(longT).get()};
    lambda->returnType = longT;
    CSharpConversions conversions(Compilation());
    AnonymousFunctionConversion(conversions, *lambda, *delegateType);
    ASSERT_EQ(lambda->lastIsValidParamTypes.size(), 2u);
    EXPECT_EQ(lambda->lastIsValidParamTypes[0].get(), intT.get());
    EXPECT_EQ(lambda->lastIsValidParamTypes[1].get(), longT.get());
    EXPECT_EQ(lambda->lastIsValidReturnType.get(), longT.get());
}

// A lambda (NOT an anonymous method) converting to `Expression<DelegateWithIntInvoke>` unpacks
// the expression tree and resolves the INNER delegate's `Invoke` -- the success crux for the
// `UnpackExpressionTreeType` integration. Without the unpack, `GetDelegateInvokeMethod` on the
// `Expression<T>` (a `Class`, not a `Delegate`) yields null -> `None`; the unpack makes the
// inner delegate's `Invoke` visible.
TEST(CSharpConversionsAnonymousFunctionTest, UnpacksExpressionTreeForLambdaNotAnonymousMethod)
{
    auto intT = Def(KnownTypeCode::Int32);
    // The INNER delegate: a `MethodHostType` (Delegate kind) whose `Invoke(int)` returns `int`.
    auto innerDelegate = MakeHost("D");
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    innerDelegate->SetMethods({invoke});
    // The toType is `Expression<D>`.
    auto exprOfDelegate = ExpressionOf(ITypePtr(innerDelegate));
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->isAnonymousMethod = false;  // a lambda -> unpacks the expression tree
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    lambda->isValidResult = std::make_shared<LambdaConversion>();
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *exprOfDelegate);
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsAnonymousFunctionConversion());
}

// An anonymous method (`IsAnonymousMethod == true`) converting to `Expression<D>` does NOT
// unpack the expression tree -- `GetDelegateInvokeMethod` on the `Expression<T>` (a `Class`)
// yields null -> `None`. This pins the `!f.IsAnonymousMethod` guard (anonymous methods cannot
// convert to expression trees).
TEST(CSharpConversionsAnonymousFunctionTest, AnonymousMethodDoesNotUnpackExpressionTreeReturnsNone)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto innerDelegate = MakeHost("D");
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    innerDelegate->SetMethods({invoke});
    auto exprOfDelegate = ExpressionOf(ITypePtr(innerDelegate));
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->isAnonymousMethod = true;  // an anonymous method -> does NOT unpack
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    lambda->isValidResult = std::make_shared<LambdaConversion>();
    CSharpConversions conversions(Compilation());
    auto result = AnonymousFunctionConversion(conversions, *lambda, *exprOfDelegate);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// ===========================================================================
// The wired anonymous-function arm of the ResolveResult-based dispatch (D528).
// ===========================================================================

// The public `CSharpConversions::ImplicitConversion(ResolveResult, IType)` routes a lambda
// resolve result through the anonymous-function arm: the dispatch's `dynamic_cast` to
// `LambdaResolveResult` succeeds, the arm fires, and the `LambdaConversion` the lambda's
// `IsValid` yields is returned. This pins the dispatch wiring end to end.
TEST(CSharpConversionsAnonymousFunctionTest, PublicImplicitConversionRoutesLambdaThroughAnonymousFunctionArm)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto lambda = std::make_shared<TestLambda>();
    lambda->hasParameterList = true;
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    lambda->isValidResult = std::make_shared<LambdaConversion>();
    CSharpConversions conversions(Compilation());
    auto result = conversions.ImplicitConversion(*lambda, *delegateType);
    EXPECT_NE(result.get(), Conversions::None().get());
    EXPECT_TRUE(result->IsAnonymousFunctionConversion());
}

// A NON-lambda resolve result (a plain `ResolveResult(Def(Int32))`) does NOT fire the
// anonymous-function arm: the dispatch's `dynamic_cast` to `LambdaResolveResult` fails, the arm
// is skipped, and the dispatch falls through to the IType-based fallback (`int -> delegate` is
// no implicit conversion -> `None`). This pins the `dynamic_cast` guard.
TEST(CSharpConversionsAnonymousFunctionTest, PublicImplicitConversionFallsThroughForNonLambdaResolveResult)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    auto plainRR = std::make_shared<ResolveResult>(intT);
    CSharpConversions conversions(Compilation());
    auto result = conversions.ImplicitConversion(*plainRR, *delegateType);
    EXPECT_EQ(result.get(), Conversions::None().get());
    EXPECT_FALSE(result->IsAnonymousFunctionConversion());
}
