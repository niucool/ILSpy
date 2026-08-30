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

// Tests for the `TypeInference` Input/Output Types region (the first slice of the
// ~1188-line TypeInference long pole): `Detail::GetDelegateOrExpressionTreeSignature`,
// `Detail::InputTypes` (C# spec draft-v11 section 12.6.3.4), and `Detail::OutputTypes`
// (section 12.6.3.5).
//
// The load-bearing cruxes:
//  (a) the `Expression<T>` UNWRAP -- `GetDelegateOrExpressionTreeSignature` resolves the
//      signature of the WRAPPED delegate, not the wrapper (the outer `Expression`1` is a
//      Class, so no-unwrap would yield null);
//  (b) the InputTypes-vs-OutputTypes implicitly-typed asymmetry -- `InputTypes` requires
//      `IsImplicitlyTyped` (an explicitly-typed lambda's parameter types are already known,
//      so they are NOT input types for the dependence computation), while `OutputTypes`
//      fires for ANY lambda (the return type always needs inference);
//  (c) the namespace / null-definition guards of the unwrap -- a 1-arg type named
//      "Expression" in the WRONG namespace, or over a definitionless generic, is not an
//      expression-tree wrapper.
//
// The stubs mirror the D533/D534 test conventions: `MethodHostType` (a
// `LookupTypeDefinition` whose `GetMethods(filter, options)` returns a configured list,
// applying the filter faithfully), `LookupMethod` + `SetParameters`/`SetReturnType`
// (`ConfigureMethod`), `TestParameter` (a minimal `IParameter` with a configurable type),
// `ExpressionDef`/`ExpressionOf` (the `System.Linq.Expressions.Expression`1` generic and
// its 1-arg `ParameterizedType`), and `TestLambda` (a concrete `LambdaResolveResult` with a
// configurable `IsImplicitlyTyped`). Each test file carries its own anonymous-namespace
// stubs (the established convention).

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

using ILSpy::Decompiler::CSharp::Resolver::Detail::GetDelegateOrExpressionTreeSignature;
using ILSpy::Decompiler::CSharp::Resolver::Detail::InputTypes;
using ILSpy::Decompiler::CSharp::Resolver::Detail::OutputTypes;
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

// A `LookupTypeDefinition` for the `System.Linq.Expressions.Expression`1` generic definition
// (the expression-tree wrapper): `Name == "Expression"` (the arity is stripped from `Name`),
// `Namespace == "System.Linq.Expressions"`, arity 1. The D534 `ExpressionDef` precedent.
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

// A `LookupTypeDefinition` subclass that reports `GetDefinition() == nullptr` despite
// carrying `Name == "Expression"` (arity 1) -- pins the null-definition guard of the unwrap
// (a definitionless type fails the namespace check, no unwrap). The D534
// `NonDefExpressionType` precedent.
class NonDefExpressionType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetDefinition() const override {
        return nullptr;
    }
};

// A concrete `LambdaResolveResult` stub with a configurable `IsImplicitlyTyped` (the flag the
// `InputTypes` arm reads). The D534 `TestLambda` precedent (public fields for
// configurability; `IsValid` is never invoked by the input/output-type collectors, but the
// pure virtual must be implemented).
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
// GetDelegateOrExpressionTreeSignature -- the Expression<T> unwrap + Invoke resolution.
// ===========================================================================

// A delegate type resolves its own `Invoke` method (pointer identity).
TEST(TypeInferenceInputOutputTypesTest, SignatureResolvesInvokeForDelegateType) {
    auto d = MakeHost("D");
    const IMethod* invoke = MakeMethod("Invoke");
    d->SetMethods({invoke});
    EXPECT_EQ(GetDelegateOrExpressionTreeSignature(*d), invoke);
}

// `Expression<D>` (the 1-arg wrapper over the real `System.Linq.Expressions` namespace)
// resolves the signature of the WRAPPED delegate `D` -- the unwrap crux. Without the
// unwrap, the outer `Expression`1`-shaped `ParameterizedType` is a Class (the Kind delegates
// to the generic), so `GetDelegateInvokeMethod` would yield null.
TEST(TypeInferenceInputOutputTypesTest, SignatureUnwrapsExpressionTreeToInnerDelegate) {
    auto inner = MakeHost("InnerDelegate");
    const IMethod* invoke = MakeMethod("Invoke");
    inner->SetMethods({invoke});
    ITypePtr expr = ExpressionOf(ITypePtr(inner));
    EXPECT_EQ(GetDelegateOrExpressionTreeSignature(*expr), invoke);
}

// A 1-arg type named "Expression" in the WRONG namespace is not an expression-tree wrapper:
// the unwrap is skipped, and the outer Class-shaped wrapper yields no delegate signature.
TEST(TypeInferenceInputOutputTypesTest, SignatureSkipsUnwrapForWrongNamespace) {
    auto wrongNsDef = std::make_shared<LookupTypeDefinition>(
        "Expression", "System",
        FullTypeName(TopLevelTypeName("System", "Expression", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    ITypePtr wrapper = std::make_shared<ParameterizedType>(
        wrongNsDef, std::vector<ITypePtr>{Def(KnownTypeCode::Int32)});
    EXPECT_EQ(GetDelegateOrExpressionTreeSignature(*wrapper), nullptr);
}

// A `ParameterizedType` over a definitionless generic named "Expression" (arity 1) skips the
// unwrap: the namespace is read via `GetDefinition()->Namespace()` with a null guard, so the
// definitionless shape fails the check (matching the C# where the non-definition's
// `Namespace` would be empty).
TEST(TypeInferenceInputOutputTypesTest, SignatureSkipsUnwrapForDefinitionlessGeneric) {
    auto nonDef = std::make_shared<NonDefExpressionType>(
        "Expression", "",
        FullTypeName(TopLevelTypeName("", "Expression", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
    ITypePtr wrapper = std::make_shared<ParameterizedType>(
        nonDef, std::vector<ITypePtr>{Def(KnownTypeCode::Int32)});
    EXPECT_EQ(GetDelegateOrExpressionTreeSignature(*wrapper), nullptr);
}

// A NON-generic delegate (`TypeParameterCount == 0`) fails the unwrap guard but still
// resolves its own `Invoke`.
TEST(TypeInferenceInputOutputTypesTest, SignatureResolvesNonGenericDelegateWithoutUnwrap) {
    auto d = MakeHost("PlainDelegate");
    const IMethod* invoke = MakeMethod("Invoke");
    d->SetMethods({invoke});
    EXPECT_EQ(GetDelegateOrExpressionTreeSignature(*d), invoke);
}

// A non-delegate type yields null (the `GetDelegateInvokeMethod` Kind guard).
TEST(TypeInferenceInputOutputTypesTest, SignatureReturnsNullForNonDelegateType) {
    auto s = Def(KnownTypeCode::Int32);
    EXPECT_EQ(GetDelegateOrExpressionTreeSignature(*s), nullptr);
}

// ===========================================================================
// InputTypes (section 12.6.3.4) -- the delegate signature's parameter types.
// ===========================================================================

// An IMPLICITLY-TYPED lambda contributes the delegate signature's parameter types (pointer
// identity against the configured `TestParameter` type instances, in order).
TEST(TypeInferenceInputOutputTypesTest, InputTypesYieldDelegateParameterTypesForImplicitlyTypedLambda) {
    auto intT = Def(KnownTypeCode::Int32);
    auto strT = Def(KnownTypeCode::String, TypeKind::Class);
    auto p0 = MakeParam(intT);
    auto p1 = MakeParam(strT);
    auto d = MakeDelegate({p0, p1}, Def(KnownTypeCode::Int32));
    TestLambda lambda;  // isImplicitlyTyped defaults true
    std::vector<const IType*> result = InputTypes(lambda, *d);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], intT.get());
    EXPECT_EQ(result[1], strT.get());
}

// An EXPLICITLY-TYPED lambda yields NO input types -- the `IsImplicitlyTyped` gate (the
// explicitly-typed lambda's parameter types are already known, so they contribute nothing to
// the dependence computation). This is the InputTypes-vs-OutputTypes crux.
TEST(TypeInferenceInputOutputTypesTest, InputTypesEmptyForExplicitlyTypedLambda) {
    auto intT = Def(KnownTypeCode::Int32);
    auto p0 = MakeParam(intT);
    auto d = MakeDelegate({p0}, Def(KnownTypeCode::Int32));
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    std::vector<const IType*> result = InputTypes(lambda, *d);
    EXPECT_TRUE(result.empty());
}

// A method group contributes the delegate signature's parameter types.
TEST(TypeInferenceInputOutputTypesTest, InputTypesYieldDelegateParameterTypesForMethodGroup) {
    auto intT = Def(KnownTypeCode::Int32);
    auto p0 = MakeParam(intT);
    auto d = MakeDelegate({p0}, Def(KnownTypeCode::Int32));
    auto mg = MakeMethodGroup();
    std::vector<const IType*> result = InputTypes(*mg, *d);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], intT.get());
}

// An implicitly-typed lambda with a NON-delegate target yields nothing (the signature
// resolves null).
TEST(TypeInferenceInputOutputTypesTest, InputTypesEmptyForNonDelegateTarget) {
    auto s = Def(KnownTypeCode::Int32);
    TestLambda lambda;
    EXPECT_TRUE(InputTypes(lambda, *s).empty());
}

// A plain (non-lambda, non-method-group) resolve result yields nothing (the RTTI gate).
TEST(TypeInferenceInputOutputTypesTest, InputTypesEmptyForPlainResolveResult) {
    TypeResolveResult plain(Def(KnownTypeCode::Int32));
    auto d = MakeDelegate({MakeParam(Def(KnownTypeCode::Int32))}, Def(KnownTypeCode::Int32));
    EXPECT_TRUE(InputTypes(plain, *d).empty());
}

// A parameterless delegate yields the PRESENT-but-empty input-type list (the C#
// `new IType[0]`, distinct from the no-signature empty).
TEST(TypeInferenceInputOutputTypesTest, InputTypesEmptyForParameterlessDelegate) {
    auto d = MakeDelegate({}, Def(KnownTypeCode::Int32));
    TestLambda lambda;
    std::vector<const IType*> result = InputTypes(lambda, *d);
    EXPECT_TRUE(result.empty());
}

// An implicitly-typed lambda with an `Expression<D>` target contributes the WRAPPED
// delegate's parameter types (the unwrap flows through the input-type collection).
TEST(TypeInferenceInputOutputTypesTest, InputTypesUnwrapExpressionTreeToInnerDelegate) {
    auto intT = Def(KnownTypeCode::Int32);
    auto p0 = MakeParam(intT);
    auto inner = MakeDelegate({p0}, Def(KnownTypeCode::Int32));
    ITypePtr expr = ExpressionOf(ITypePtr(inner));
    TestLambda lambda;
    std::vector<const IType*> result = InputTypes(lambda, *expr);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], intT.get());
}

// ===========================================================================
// OutputTypes (section 12.6.3.5) -- the delegate signature's return type.
// ===========================================================================

// An implicitly-typed lambda contributes the delegate signature's return type (the
// one-element snapshot, pointer identity).
TEST(TypeInferenceInputOutputTypesTest, OutputTypesYieldDelegateReturnTypeForImplicitlyTypedLambda) {
    auto retT = Def(KnownTypeCode::Int64);
    auto d = MakeDelegate({MakeParam(Def(KnownTypeCode::Int32))}, retT);
    TestLambda lambda;
    std::vector<const IType*> result = OutputTypes(lambda, *d);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], retT.get());
}

// An EXPLICITLY-TYPED lambda ALSO contributes the return type -- unlike `InputTypes`, there
// is no `IsImplicitlyTyped` gate here (the return type always needs inference). This is the
// InputTypes-vs-OutputTypes crux.
TEST(TypeInferenceInputOutputTypesTest, OutputTypesAlsoYieldDelegateReturnTypeForExplicitlyTypedLambda) {
    auto retT = Def(KnownTypeCode::Int64);
    auto d = MakeDelegate({MakeParam(Def(KnownTypeCode::Int32))}, retT);
    TestLambda lambda;
    lambda.isImplicitlyTyped = false;
    std::vector<const IType*> result = OutputTypes(lambda, *d);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], retT.get());
}

// A method group contributes the delegate signature's return type.
TEST(TypeInferenceInputOutputTypesTest, OutputTypesYieldDelegateReturnTypeForMethodGroup) {
    auto retT = Def(KnownTypeCode::Int64);
    auto d = MakeDelegate({}, retT);
    auto mg = MakeMethodGroup();
    std::vector<const IType*> result = OutputTypes(*mg, *d);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], retT.get());
}

// A lambda with a NON-delegate target yields nothing.
TEST(TypeInferenceInputOutputTypesTest, OutputTypesEmptyForNonDelegateTarget) {
    auto s = Def(KnownTypeCode::Int32);
    TestLambda lambda;
    EXPECT_TRUE(OutputTypes(lambda, *s).empty());
}

// A plain (non-lambda, non-method-group) resolve result yields nothing (the RTTI gate).
TEST(TypeInferenceInputOutputTypesTest, OutputTypesEmptyForPlainResolveResult) {
    TypeResolveResult plain(Def(KnownTypeCode::Int32));
    auto d = MakeDelegate({}, Def(KnownTypeCode::Int64));
    EXPECT_TRUE(OutputTypes(plain, *d).empty());
}

// A lambda with an `Expression<D>` target contributes the WRAPPED delegate's return type
// (the unwrap flows through the output-type collection).
TEST(TypeInferenceInputOutputTypesTest, OutputTypesUnwrapExpressionTreeToInnerDelegate) {
    auto retT = Def(KnownTypeCode::Int64);
    auto inner = MakeDelegate({MakeParam(Def(KnownTypeCode::Int32))}, retT);
    ITypePtr expr = ExpressionOf(ITypePtr(inner));
    TestLambda lambda;
    std::vector<const IType*> result = OutputTypes(lambda, *expr);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], retT.get());
}
