// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver CanTransformToExtensionMethodCall region
// (cpp/Decompiler/CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of
// CSharpResolver.cs lines 2958-2983): the two public overloads deciding whether a
// static extension-method call `C.M(x, args)` can be re-rendered as the extension
// form `x.M(args)`.
//
// The load-bearing cruxes:
//  (a) the LAMBDA-target guard -- a `LambdaResolveResult` target returns false before
//      any lookup runs (a member access on a lambda yields no method group);
//  (b) the method-group gate -- a lookup that yields no `MethodGroupResolveResult`
//      (no instance member and no extension method in the using scope) returns false;
//  (c) the full success path -- the extension-method fallback group over the using
//      scope, the extension-method overload resolution (the receiver prepended as the
//      first argument), the unambiguous best candidate BEING the method, and the
//      identity-eligible `this` parameter: TRUE;
//  (d) the AMBIGUITY gate -- two identical-signature applicable overloads tie every
//      tiebreak and leave `IsAmbiguous` set: false;
//  (e) the best-candidate identity -- a DIFFERENT overload winning the better-
//      conversion comparison (int identity beats int->long widening) makes the
//      `method.Equals(best)` check fail: false (and the mirror with the winning
//      overload as the method: true -- the overload-selection crux pair);
//  (f) the ELIGIBILITY conjunction -- the method being the best candidate is not
//      enough: a `this` parameter type the target does not convert to (C -> string)
//      fails `IsEligibleExtensionMethod`: false;
//  (g) the convenience overload's synthesis -- the FIRST parameter's type becomes the
//      target, the remaining parameters' types the arguments: true through the full
//      pipeline; a zero-parameter method returns false before any synthesis;
//  (h) the `ignoreArgumentNames: false` quirk -- the synthesized names cover ALL the
//      method's parameters (the receiver's included), so in the extension-method
//      resolution the receiver's name lands on the FIRST argument: a trailing NAMED
//      argument routes the overload selection to the parameter NAMED for it (the
//      named "b" picks the long overload) while the positional default picks the
//      better-conversion int overload -- the divergent true/false pair;
//  (i) the `ignoreTypeArguments: false` threading -- the method's own type arguments
//      become the EXPLICIT type arguments of the resolution (a specialized generic
//      method): the explicit T=long ties the generic and non-generic long overloads
//      and the non-generic tiebreak wins, while the default (ignore, infer T=int)
//      makes the generic overload win by identity -- the divergent false/true pair.
//
// LIFETIME DISCIPLINE: the eligibility check reaches the per-compilation
// CSharpConversions instance (the public static resolves it via CSharpConversions::Get
// over the METHOD's compilation), so every test builds its resolver over a FRESH
// per-test LookupCompilation (the instance and its conversion cache die WITH the test
// -- the iteration-109 discipline). The extension methods' parameters are shared-
// managed in the test scope (the methods store non-owning `const IParameter*`).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// A namespace stub with a configurable type table (the iteration-108/110
// ConfigurableNamespace shape, local to this file -- the each-test-file-carries-its-own
// stubs convention). The extension-method scan enumerates Types().
class TestNamespace : public INamespace {
public:
    TestNamespace(std::string name, const TS::ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void AddTypeDefinition(const TS::ITypeDefinition* def) { types_.push_back(def); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return name_; }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override { return compilation_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return name_; }
    const INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const INamespace*> ChildNamespaces() const override { return {}; }
    std::vector<const TS::ITypeDefinition*> Types() const override { return types_; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const INamespace* GetChildNamespace(const std::string&) const override { return nullptr; }
    const TS::ITypeDefinition* GetTypeDefinition(const std::string&,
                                                 int) const override
    {
        return nullptr;
    }

private:
    std::string name_;
    const TS::ICompilation& compilation_;
    std::vector<const TS::ITypeDefinition*> types_;
};

// A method type parameter carrying the AcceptVisitor -> VisitTypeParameter bridge (the
// D564 VisitableTypeParameter precedent): the plain LookupTypeParameter routes to
// VisitOtherType, so the RunTypeInference substitution would leave the formal
// parameter type unsubstituted (the D564/D69 trap).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A definition with a DEFINITE reference-type IsReferenceType (the D517 RefDef
// precedent): the C -> string reference-conversion check in the eligibility verdict
// needs a definite true on both sides (the plain LookupTypeDefinition inherits the
// IType `std::nullopt` default, which fails the guard for the wrong reason).
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// The concrete test subclass driving the abstract `LambdaResolveResult` (the
// LambdaResolveResult_Test TestLambdaResolveResult shape, trimmed to the members the
// guard needs -- only the RTTI matters here; every pure virtual still needs an
// override for the class to be concrete).
class TestLambda : public ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult {
public:
    bool HasParameterList() const override { return true; }
    bool IsAnonymousMethod() const override { return false; }
    bool IsImplicitlyTyped() const override { return true; }
    bool IsAsync() const override { return false; }
    ITypePtr GetInferredReturnType(const std::vector<ITypePtr>&) const override
    {
        return std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    }
    std::vector<const TS::IParameter*> Parameters() const override { return {}; }
    const TS::IType& ReturnType() const override { return *returnType_; }
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> IsValid(
        const std::vector<ITypePtr>&, const ITypePtr&,
        ILSpy::Decompiler::CSharp::Resolver::CSharpConversions&) const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::Semantics::ResolveResult& Body() const override { return *body_; }

    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<TestLambda>(*this);
    }

private:
    ITypePtr returnType_ = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    std::shared_ptr<ResolveResult> body_ =
        std::make_shared<ResolveResult>(std::make_shared<TS::KnownType>(KnownTypeCode::Int32));
};

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline in the
// file header) plus the shared-managed definitions the conversion paths read (the
// type-cache model: one instance per primitive, shared by the targets, the extension
// methods' parameters, and the explicit type arguments).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> int64;
    std::shared_ptr<LookupTypeDefinition> stringDef;
    std::shared_ptr<LookupTypeDefinition> receiver;

    Fixture()
        : int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          int64(MakeDef("Int64", KnownTypeCode::Int64, TypeKind::Struct)),
          stringDef(MakeDef("String", KnownTypeCode::String, TypeKind::Class)),
          receiver(MakeRefDef("C"))
    {
    }

    // An extension-method host: a static, HasExtensions class over the methods (the
    // iteration-109/110 MakeHost shape).
    std::shared_ptr<LookupTypeDefinition> MakeHost(
        const std::vector<const TS::IMethod*>& methods) const
    {
        auto host = MakeDef("Extensions", KnownTypeCode::None, TypeKind::Class);
        host->SetStatic(true);
        host->SetHasExtensions(true);
        host->SetMethods(methods);
        return host;
    }

    // A two-parameter extension method `name(this firstType firstName, secondType
    // secondName)`: the method and BOTH parameters are shared-managed (the method
    // stores non-owning `const IParameter*` -- the iteration-50 lifetime convention),
    // and the return type is a make_shared'd instance (the ComputeType
    // shared_from_this trap).
    std::tuple<std::shared_ptr<LookupMethod>,
               std::shared_ptr<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>,
               std::shared_ptr<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>>
    MakeExtMethod(const std::string& name, ITypePtr firstParamType,
                  const std::string& firstName, ITypePtr secondParamType,
                  const std::string& secondName) const
    {
        auto method = std::make_shared<LookupMethod>(name, compilation);
        method->SetIsExtensionMethod(true);
        auto first = std::make_shared<
            ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
            std::move(firstParamType), firstName);
        auto second = std::make_shared<
            ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
            std::move(secondParamType), secondName);
        method->SetParameters({first.get(), second.get()});
        method->SetReturnType(std::make_shared<TS::KnownType>(KnownTypeCode::Void));
        return {std::move(method), std::move(first), std::move(second)};
    }

    // A resolver over the fixture's compilation with a using scope whose OWN namespace
    // is the caller's `ns` (the iteration-109/110 scope wiring). LIFETIME: the
    // `UsingScope` holds a NON-OWNING `const INamespace*` (the compilation owns real
    // namespaces; the test stub must therefore stay alive in the CALLER's scope for
    // the whole resolution -- the iteration-112 non-owning-target convention), which
    // is why the namespace is a parameter and not a helper local.
    std::shared_ptr<CSharpResolver> MakeScopedResolver(
        const std::shared_ptr<TestNamespace>& ns)
    {
        auto resolver = std::make_shared<CSharpResolver>(compilation);
        auto context = std::make_shared<CSharpTypeResolveContext>(compilation.MainModule());
        auto scope = std::make_shared<UsingScope>(
            context, *ns, std::vector<const INamespace*>{});
        return resolver->WithCurrentUsingScope(scope);
    }

    // A namespace carrying the host (the extension-method scan enumerates Types()).
    std::shared_ptr<TestNamespace> MakeNamespace(
        const std::shared_ptr<LookupTypeDefinition>& host)
    {
        auto ns = std::make_shared<TestNamespace>("NS", compilation);
        ns->AddTypeDefinition(host.get());
        return ns;
    }

    // A plain expression over the given type (the argument/target shape).
    static std::shared_ptr<ResolveResult> MakeExpression(ITypePtr type)
    {
        return std::make_shared<ResolveResult>(std::move(type));
    }

private:
    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }

    std::shared_ptr<LookupTypeDefinition> MakeRefDef(const std::string& name) const
    {
        return std::make_shared<RefDef>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Class,
            Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
    }
};

} // namespace

// ===========================================================================
// The five-argument overload
// ===========================================================================

TEST(CSharpResolverCanTransformTest, LambdaTargetNeverTransforms)
{
    // The C# `if (target is LambdaResolveResult) return false;` -- the guard fires
    // before any lookup (a member access on a lambda yields no method group).
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto target = std::make_shared<TestLambda>();

    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(
        *std::make_shared<LookupMethod>("M", fix.compilation), {},
        std::move(target), std::vector<std::shared_ptr<ResolveResult>>{}));
}

TEST(CSharpResolverCanTransformTest, NoMethodGroupYieldsFalse)
{
    // No using scope -> no extension methods in scope -> the member access yields the
    // UnknownMemberResolveResult (not a MethodGroupResolveResult) -> false.
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto target = Fixture::MakeExpression(fix.receiver);

    auto method = std::make_shared<LookupMethod>("M", fix.compilation);
    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(
        *method, {}, std::move(target),
        std::vector<std::shared_ptr<ResolveResult>>{Fixture::MakeExpression(fix.int32)}));
}

TEST(CSharpResolverCanTransformTest, TransformsWhenTheMethodIsTheUnambiguousBest)
{
    // The flagship success path: the extension-method fallback group over the using
    // scope, the extension-method overload resolution (the receiver prepended), the
    // unambiguous best candidate BEING the method, and the identity-eligible `this`
    // parameter (C -> C on the same instance).
    Fixture fix;
    auto [method, p1, p2] = fix.MakeExtMethod(
        "M", fix.receiver, "c", fix.int32, "x");
    auto host = fix.MakeHost({method.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);
    auto target = Fixture::MakeExpression(fix.receiver);

    EXPECT_TRUE(resolver->CanTransformToExtensionMethodCall(
        *method, {}, std::move(target),
        std::vector<std::shared_ptr<ResolveResult>>{Fixture::MakeExpression(fix.int32)}));
}

TEST(CSharpResolverCanTransformTest, AmbiguousIdenticalOverloadsYieldFalse)
{
    // Two identical-signature applicable overloads tie every tiebreak (the shared
    // parameter-type instances make parameterTypesEqual true, and every tiebreak arm
    // returns 0) -> IsAmbiguous -> false.
    Fixture fix;
    auto [m1, m1p1, m1p2] = fix.MakeExtMethod("M", fix.receiver, "c", fix.int32, "x");
    auto [m2, m2p1, m2p2] = fix.MakeExtMethod("M", fix.receiver, "c", fix.int32, "x");
    auto host = fix.MakeHost({m1.get(), m2.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);
    auto target = Fixture::MakeExpression(fix.receiver);

    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(
        *m1, {}, std::move(target),
        std::vector<std::shared_ptr<ResolveResult>>{Fixture::MakeExpression(fix.int32)}));
}

TEST(CSharpResolverCanTransformTest, DifferentBestCandidateYieldsFalse)
{
    // The overload-selection crux pair: with an int argument, the int overload wins
    // the better-conversion comparison (int -> int identity beats int -> long
    // widening), so the method passed as the LONG overload fails the
    // `method.Equals(best)` check while the INT overload (the actual best) passes.
    Fixture fix;
    auto [mInt, intP1, intP2] = fix.MakeExtMethod("M", fix.receiver, "c", fix.int32, "x");
    auto [mLong, longP1, longP2] = fix.MakeExtMethod("M", fix.receiver, "c", fix.int64, "y");
    auto host = fix.MakeHost({mInt.get(), mLong.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);

    auto longTarget = Fixture::MakeExpression(fix.receiver);
    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(
        *mLong, {}, std::move(longTarget),
        std::vector<std::shared_ptr<ResolveResult>>{Fixture::MakeExpression(fix.int32)}));

    auto intTarget = Fixture::MakeExpression(fix.receiver);
    EXPECT_TRUE(resolver->CanTransformToExtensionMethodCall(
        *mInt, {}, std::move(intTarget),
        std::vector<std::shared_ptr<ResolveResult>>{Fixture::MakeExpression(fix.int32)}));
}

TEST(CSharpResolverCanTransformTest, IneligibleThisParameterYieldsFalse)
{
    // The eligibility conjunction: the method IS the best candidate (the only one --
    // the extension resolution keeps it as the best even though the receiver does not
    // convert to the `this string` parameter), but C -> string is no implicit
    // conversion, so IsEligibleExtensionMethod fails -> false.
    Fixture fix;
    auto [method, p1, p2] = fix.MakeExtMethod(
        "M", fix.stringDef, "s", fix.int32, "x");
    auto host = fix.MakeHost({method.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);
    auto target = Fixture::MakeExpression(fix.receiver);

    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(
        *method, {}, std::move(target),
        std::vector<std::shared_ptr<ResolveResult>>{Fixture::MakeExpression(fix.int32)}));
}

// ===========================================================================
// The convenience overload
// ===========================================================================

TEST(CSharpResolverCanTransformTest, ConvenienceOverloadZeroParametersYieldsFalse)
{
    // The C# `if (method.Parameters.Count == 0) return false;` -- an extension method
    // needs at least the `this` parameter; the guard fires before any synthesis.
    Fixture fix;
    auto resolver = std::make_shared<CSharpResolver>(fix.compilation);
    auto method = std::make_shared<LookupMethod>("M", fix.compilation);

    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(*method));
}

TEST(CSharpResolverCanTransformTest, ConvenienceOverloadSynthesizesTheArguments)
{
    // The synthesis: the FIRST parameter's type (C) becomes the target, the remaining
    // parameter's type (int) the single argument, no names (the ignoreArgumentNames
    // default), no type arguments (the non-generic method's own empty list) -- the
    // full success path through the five-argument overload.
    Fixture fix;
    auto [method, p1, p2] = fix.MakeExtMethod(
        "M", fix.receiver, "c", fix.int32, "x");
    auto host = fix.MakeHost({method.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);

    EXPECT_TRUE(resolver->CanTransformToExtensionMethodCall(*method));
}

TEST(CSharpResolverCanTransformTest, ConvenienceOverloadIgnoreArgumentNamesThreadsTheNames)
{
    // The name-synthesis quirk: `ignoreArgumentNames: false` synthesizes the names of
    // ALL the method's parameters (["c", "b"] -- the receiver's included) while the
    // synthesized arguments SKIP the receiver (n-1 entries) -- a LENGTH MISMATCH the
    // OverloadResolution ctor rejects with the ArgumentException analog (the C# names
    // array looks like it was meant to Skip(1) the receiver; no C# caller passes
    // ignoreArgumentNames: false -- every caller keeps the default). The positional
    // default instead runs the full pipeline over the method's own shape (the target
    // and the arguments are synthesized from the parameters, so the method itself is
    // always applicable by identity): a competing overload with IDENTICAL parameter
    // types ties every tiebreak -> AMBIGUOUS -> false.
    Fixture fix;
    auto [mLong, longP1, longP2] = fix.MakeExtMethod("M", fix.receiver, "c", fix.int64, "b");
    auto [mTwin, twinP1, twinP2] = fix.MakeExtMethod("M", fix.receiver, "c", fix.int64, "b2");
    auto host = fix.MakeHost({mLong.get(), mTwin.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);

    // The names cover every parameter (the receiver's included) while the arguments
    // skip the receiver: the length mismatch throws.
    EXPECT_THROW(resolver->CanTransformToExtensionMethodCall(
                     *mLong, /*ignoreTypeArguments*/ false,
                     /*ignoreArgumentNames*/ false),
                 std::invalid_argument);

    // The default (ignoreArgumentNames: true): the synthesized long argument applies
    // to both identical-signature overloads by identity -> every tiebreak ties ->
    // ambiguous -> false.
    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(*mLong));
}

TEST(CSharpResolverCanTransformTest, ConvenienceOverloadIgnoreTypeArgumentsThreadsTheTypeArguments)
{
    // The type-argument threading: a SPECIALIZED generic method M<T>(this C c, T x)
    // carrying the explicit type argument long, plus a competing generic overload
    // M<U>(this C c, long y) whose type parameter is UNUSED (an arity-matching
    // sibling). With `ignoreTypeArguments: false` (the C# default) the method's own
    // type arguments flow into the member-access lookup as the EXPLICIT type
    // arguments: the extension-method filter keeps only the arity-matching generic
    // methods, the explicit T=long/U=long make BOTH overloads convert the int
    // argument by widening with identical parameter types, and every tiebreak ties
    // -> AMBIGUOUS -> false. With `ignoreTypeArguments: true` the empty list lets the
    // filter keep both methods and the resolution INFER: T is fixed to int (the
    // identity conversion) while U cannot be inferred at all (TypeInferenceFailed ->
    // the competing overload is inapplicable) -> the method IS the best -> true.
    Fixture fix;
    // The type parameter T: visitable (the substitution bridge) with the
    // LookupTypeParameter defaults Index=0 / OwnerType=Method (the InferTypeArguments
    // contract), used BOTH as the method's TypeParameters[0] and as the second
    // parameter's type (the substitution replaces it).
    auto t = std::make_shared<VisitableTypeParameter>("T");
    auto [mGeneric, gP1, gP2] = fix.MakeExtMethod(
        "M", fix.receiver, "c", t, "x");
    mGeneric->SetTypeParameters({t.get()});
    mGeneric->SetTypeArguments({fix.int64});
    // The competing generic overload: an UNUSED type parameter U over a long second
    // parameter (the arity matches the explicit type arguments, so the filter keeps
    // it; the inference can never fix U).
    auto u = std::make_shared<VisitableTypeParameter>("U");
    auto [mCompeting, cP1, cP2] = fix.MakeExtMethod(
        "M", fix.receiver, "c", fix.int64, "y");
    mCompeting->SetTypeParameters({u.get()});
    auto host = fix.MakeHost({mGeneric.get(), mCompeting.get()});
    auto ns = fix.MakeNamespace(host);
    auto resolver = fix.MakeScopedResolver(ns);

    // The explicit type arguments make both overloads applicable with identical
    // [C, long] parameter types -> every tiebreak ties -> ambiguous -> false.
    EXPECT_FALSE(resolver->CanTransformToExtensionMethodCall(
        *mGeneric, /*ignoreTypeArguments*/ false, /*ignoreArgumentNames*/ true));

    // The inferred T=int makes the generic overload applicable by identity while the
    // competing overload's U cannot be inferred (TypeInferenceFailed) -> the method
    // is the best -> true.
    EXPECT_TRUE(resolver->CanTransformToExtensionMethodCall(
        *mGeneric, /*ignoreTypeArguments*/ true, /*ignoreArgumentNames*/ true));
}
