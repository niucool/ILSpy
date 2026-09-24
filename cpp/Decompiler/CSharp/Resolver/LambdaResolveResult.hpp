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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/LambdaResolveResult.cs -- representing an
// anonymous method or lambda expression, together with the co-located `LambdaConversion`
// singleton. These are the ninth and tenth `cpp/Decompiler/CSharp/Resolver/` leaves toward
// the `CSharpResolver` leaf deps (the long-pole remaining blocker of `TypeSystemAstBuilder` /
// `CSharpAmbience`), after the D467 twin Alias `ResolveResult` subclasses, the D468 twin
// enum leaves (`NameLookupMode` + `OverloadResolutionErrors`), the D469
// `DynamicMemberResolveResult`, the D470 `AwaitResolveResult`, the D471
// `DynamicInvocationResolveResult` + `DynamicInvocationType`, and the D472
// `CSharpInvocationResolveResult`. The C# source declares three classes; this header ports
// TWO and defers one:
//   * PORTED: `public abstract class LambdaResolveResult : ResolveResult` -- the abstract
//     base. The C# surface:
//       - `protected LambdaResolveResult() : base(SpecialType.NoType)` -- forwards the
//         `TypeKind::None` null object to the `ResolveResult` base: the lambda has NO type
//         (the delegate type comes from the anonymous-function conversion, per the C# doc
//         comment).
//       - `abstract bool HasParameterList` -- always true for C# 3.0 lambdas, may be false
//         for C# 2.0 anonymous methods (`delegate { ... }`).
//       - `abstract bool IsAnonymousMethod` -- the C# 2.0 anonymous-method syntax form.
//       - `abstract bool IsImplicitlyTyped` -- false for anonymous methods without a
//         parameter list.
//       - `abstract bool IsAsync`.
//       - `abstract IType GetInferredReturnType(IType[] parameterTypes)` -- the return type
//         inferred from the body when the parameters are inferred to be `parameterTypes`
//         (used by C# type inference).
//       - `abstract IReadOnlyList<IParameter> Parameters`.
//       - `abstract IType ReturnType` -- the return type of the lambda (includes `Task<T>`
//         when async).
//       - `abstract Conversion IsValid(IType[] parameterTypes, IType returnType,
//         CSharpConversions conversions)` -- whether the lambda body is valid for the given
//         parameter/return types, yielding an anonymous-function conversion (or
//         `Conversion.None` when invalid).
//       - `abstract ResolveResult Body` -- the resolve result for the lambda body ('void'
//         for statement lambdas).
//       - `override IEnumerable<ResolveResult> GetChildResults()` => `new[] { this.Body }`
//         -- the ONE concrete member.
//   * PORTED: `class LambdaConversion : Conversion` (internal, NOT sealed) -- the singleton
//     anonymous-function conversion:
//       - `public static readonly LambdaConversion Instance = new LambdaConversion();`
//       - `override bool IsAnonymousFunctionConversion => true`
//       - `override bool IsImplicit => true`
//     It overrides NOTHING else (notably not `Equals`/`GetHashCode`: the base
//     reference-equality applies).
//   * DEFERRED: `sealed class DecompiledLambdaResolveResult : LambdaResolveResult` (the
//     concrete subclass the decompiler's back end constructs) -- blocked on two unported
//     prerequisites: (1) `ILFunction.IsAsync` / `ILFunction.Parameters` (as
//     `IReadOnlyList<IParameter>`) / `ILFunction.ReturnType` (as `IType`) -- the port's
//     `ILFunction` (Phase 3) carries none of these surfaces; and (2) `CSharpConversions`
//     (the ~2500-line conversion controller) whose `IdentityConversion` / `ImplicitConversion`
//     / `Conversion.None` results `DecompiledLambdaResolveResult.IsValid` composes. The
//     deferred subclass introduces those surfaces when it lands.
//
// All other deps are already ported: `ResolveResult` (D424, the base), `SpecialType.NoType`
// (the D433 `NoType()` `IType.hpp` convenience), `IType`/`ITypePtr` (D271), `IParameter`
// (D370), `Conversion` (D406, the `LambdaConversion` base), and `SpecialType.UnknownType`
// (the `Body` stub type, D271).
//
// KEY PORT CONVENTIONS:
//  * The C# `abstract class LambdaResolveResult` ports to a C++ class with PURE-VIRTUAL
//     members (mirroring the C# `abstract` members one-to-one). Unlike the D406
//     `Conversion` base (which uses a pure-virtual destructor to keep every method
//     defaulted), EVERY declared member here is pure-virtual -- the C# declares them all
//     `abstract` -- so the class stays abstract without the pure-virtual-destructor idiom.
//  * The C# `protected` ctor ports to a C++ `protected` default ctor forwarding
//     `ILSpy::Decompiler::TypeSystem::NoType()` (the D433 convenience) to the
//     `ResolveResult(ITypePtr)` base, which `assert`-guards it non-null (the D424 base-ctor
//     convention). Distinct from `UnknownType()` (`TypeKind::Unknown`): a lambda has NO
//     type at all, not an unknown one (the same `NoType`-vs-`UnknownType` distinction the
//     D443 `ThrowResolveResult` documents).
//  * The C# `bool` properties port to zero-arg `virtual bool Xxx() const = 0` getters
//     (the D467/D470 property-to-method convention).
//  * The C# `IType GetInferredReturnType(IType[] parameterTypes)` ports to
//     `virtual TS::ITypePtr GetInferredReturnType(const std::vector<TS::ITypePtr>&
//     parameterTypes) const = 0`: the `IType[]` parameter ports to a const-reference
//     `std::vector<ITypePtr>` (the read-only input-array convention; the callee only reads
//     the element types), and the `IType` return ports to `ITypePtr` BY VALUE (the shared
//     handle the C# reference return models; the D438 by-value-vector/handle precedent).
//  * The C# `IReadOnlyList<IParameter> Parameters` ports to
//     `virtual std::vector<const TS::IParameter*> Parameters() const = 0` (the
//     `IParameterizedMember::Parameters` convention -- a non-owning pointer snapshot
//     returned by value; the parameters are owned by the type system / the concrete
//     subclass). `IParameter` is FORWARD-DECLARED (not included) -- a vector of pointers to
//     an incomplete type needs only the forward declaration (the D445
//     IProperty/IMethod-forward-declaration precedent), keeping the include graph minimal.
//  * The C# `IType ReturnType { get; }` (a non-null reference) ports to
//     `virtual const TS::IType& ReturnType() const = 0` (the `IVariable::Type()` /
//     `IMember::ReturnType()` reference-return convention).
//  * The C# `Conversion IsValid(IType[], IType, CSharpConversions)` ports to
//     `virtual std::shared_ptr<Semantics::Conversion> IsValid(const
//     std::vector<TS::ITypePtr>&, const TS::ITypePtr&, CSharpConversions&) const = 0`:
//     the `Conversion` return (a reference type; `Conversion.None` or a `LambdaConversion`
//     instance) ports to a `shared_ptr<Conversion>` by value (the D438 shared-handle
//     convention), `returnType` to a const-ref `ITypePtr`, and the C#
//     `CSharpConversions conversions` (a mutable service object the implementation calls
//     `IdentityConversion` / `ImplicitConversion` on) to a NON-const `CSharpConversions&`.
//     `CSharpConversions` is FORWARD-DECLARED in this header (the class is unported --
//     ~2500 lines); a reference parameter of a pure-virtual needs only the forward
//     declaration. Callers cannot yet invoke `IsValid` (no `CSharpConversions` instance
//     exists); the deferred `CSharpConversions` port unblocks them.
//  * The C# `ResolveResult Body { get; }` (a non-null reference -- the C#
//     `GetChildResults()` returns `new[] { this.Body }` unconditionally) ports to
//     `virtual Semantics::ResolveResult& Body() const = 0` (a never-null reference return).
//     The concrete `DecompiledLambdaResolveResult` (deferred) holds the body as a
//     `shared_ptr<ResolveResult>` member and returns `*body_`.
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` => `new[] { this.Body }`
//     ports to the D424 `std::vector<const ResolveResult*>` snapshot (non-owning pointers)
//     with the single element `&Body()`.
//  * `ClassName()` is NOT overridden here: the C# base is abstract, so `GetType().Name` on
//     any instance yields the CONCRETE class name (e.g. "DecompiledLambdaResolveResult");
//     the generic base default "ResolveResult" stays inherited and each concrete subclass
//     overrides `ClassName()` for its own name (the D424/D443 convention).
//  * `ShallowClone()` is NOT overridden here either: the abstract base cannot copy-construct
//     itself (`make_unique<LambdaResolveResult>` is ill-formed). The C#
//     `MemberwiseClone`-based `ShallowClone` preserves the runtime type through the
//     inheritance chain; in C++ each concrete subclass MUST override `ShallowClone` itself
//     (the D424 slicing-prevention convention), as the deferred
//     `DecompiledLambdaResolveResult` will.
//  * The C# `class LambdaConversion : Conversion` (assembly-`internal`, NOT sealed) ports
//     to a PUBLIC C++ subclass (NOT `final`) of `Semantics::Conversion` (the D445
//     `internal`-to-public precedent; pinned by `static_assert(!std::is_final_v)`). The C#
//     has no declared ctor, so the implicit public default ctor ports to `= default`.
//  * The C# `public static readonly LambdaConversion Instance = new LambdaConversion();`
//     (a process-wide singleton field) ports to a Meyers singleton
//     `static const LambdaConversion& Instance()` (function-local `static const` instance);
//     a REFERENCE return preserves the C# reference-identity of the singleton
//     (`&Instance() == &Instance()`), mirroring how the C# field is one shared object.
//     (Unlike the D406 `Conversions` factory struct -- which holds `shared_ptr<Conversion>`
//     singletons because the C# statics live on the base `Conversion` class and the
//     subclasses are nested -- the C# `LambdaConversion.Instance` field lives on
//     `LambdaConversion` itself, so the singleton lives on the class directly.)
//  * The two overrides (`IsAnonymousFunctionConversion` true, `IsImplicit` true) port
//     verbatim; everything else inherits the D406 `Conversion` defaults (`IsValid` true,
//     reference-equality `Equals`, identity `GetHashCode`).

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_LAMBDARESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_LAMBDARESOLVERESULT_HPP

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
class IParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp::Resolver {
namespace Detail {
bool IdentityConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                        ILSpy::Decompiler::TypeSystem::IType& toType);
} // namespace Detail

using ::ILSpy::Decompiler::TypeSystem::IParameter;
using ::ILSpy::Decompiler::Semantics::Conversions;
} // namespace ILSpy::Decompiler::CSharp::Resolver

namespace ILSpy::Decompiler::CSharp::Resolver {

// The conversion controller the C# `IsValid` consults (ICSharpCode.Decompiler/CSharp/Resolver/
// CSharpConversions.cs, ~2500 lines -- unported). Forward-declared so the pure-virtual
// `IsValid` signature can reference it; call sites unblock with the `CSharpConversions`
// port.
class CSharpConversions;

// The C# `public abstract class LambdaResolveResult : ResolveResult` -- the result of an
// anonymous method or lambda expression. Note: the lambda has no type (the base type is
// `SpecialType.NoType`); to retrieve the delegate type, look at the anonymous-function
// conversion. Abstract: the C# decompiler back end constructs the (deferred)
// `DecompiledLambdaResolveResult` concrete subclass.
class LambdaResolveResult : public ILSpy::Decompiler::Semantics::ResolveResult {
public:
    // The C# `abstract bool HasParameterList` -- always true for C# 3.0 lambdas, but may be
    // false for C# 2.0 anonymous methods.
    virtual bool HasParameterList() const = 0;

    // The C# `abstract bool IsAnonymousMethod` -- whether this lambda uses the C# 2.0
    // anonymous-method syntax.
    virtual bool IsAnonymousMethod() const = 0;

    // The C# `abstract bool IsImplicitlyTyped` -- whether the lambda parameters are
    // implicitly typed (false for anonymous methods without a parameter list).
    virtual bool IsImplicitlyTyped() const = 0;

    // The C# `abstract bool IsAsync` -- whether the lambda is async.
    virtual bool IsAsync() const = 0;

    // The C# `abstract IType GetInferredReturnType(IType[] parameterTypes)` -- the return
    // type inferred when the parameter types are inferred to be `parameterTypes` (used as
    // part of C# type inference; use `ReturnType` for the actual return type as determined
    // by the target delegate type).
    virtual ILSpy::Decompiler::TypeSystem::ITypePtr GetInferredReturnType(
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& parameterTypes) const = 0;

    // The C# `abstract IReadOnlyList<IParameter> Parameters` -- the list of parameters.
    // Returns a non-owning pointer snapshot (the concrete subclass owns the parameters).
    virtual std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const = 0;

    // The C# `abstract IType ReturnType` -- the return type of the lambda (includes
    // `Task<T>` when the lambda is async). A never-null reference return.
    virtual const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const = 0;

    // The C# `abstract Conversion IsValid(IType[] parameterTypes, IType returnType,
    // CSharpConversions conversions)` -- whether the lambda body is valid for the given
    // parameter types and return type: an anonymous-function conversion
    // (`LambdaConversion::Instance()`) when valid, `Conversion.None` otherwise.
    // `CSharpConversions` is unported (forward-declared); call sites unblock with it.
    virtual std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> IsValid(
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& parameterTypes,
        const ILSpy::Decompiler::TypeSystem::ITypePtr& returnType,
        CSharpConversions& conversions) const = 0;

    // The C# `abstract ResolveResult Body` -- the resolve result for the lambda body ('void'
    // for statement lambdas). A never-null reference return (`GetChildResults` uses it
    // unconditionally).
    virtual ILSpy::Decompiler::Semantics::ResolveResult& Body() const = 0;

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` => `new[] { this.Body }`
    // -- a one-element snapshot of non-owning pointers (the D424 convention).
    std::vector<const ILSpy::Decompiler::Semantics::ResolveResult*> GetChildResults() const override
    {
        return { &Body() };
    }

protected:
    // The C# `protected LambdaResolveResult() : base(SpecialType.NoType)` -- forwards the
    // `TypeKind::None` null object: the lambda has NO type (the delegate type comes from
    // the anonymous-function conversion). Distinct from `UnknownType()`
    // (`TypeKind::Unknown`, the error-type null object).
    LambdaResolveResult()
        : ResolveResult(ILSpy::Decompiler::TypeSystem::NoType()) {}
};

// The C# `class LambdaConversion : Conversion` (internal, NOT sealed) -- the
// anonymous-function conversion singleton. Ports to a PUBLIC C++ subclass (NOT `final`) of
// `Semantics::Conversion` (D406). Overrides exactly `IsAnonymousFunctionConversion` and
// `IsImplicit` (both true); every other member inherits the `Conversion` base defaults
// (including the reference-equality `Equals` -- the singleton is reference-equal only to
// itself).
class LambdaConversion : public ILSpy::Decompiler::Semantics::Conversion {
public:
    // The C# implicit public default ctor (no ctor is declared in the C# source).
    LambdaConversion() = default;

    // The C# `public static readonly LambdaConversion Instance = new LambdaConversion();` --
    // a process-wide singleton. The reference return preserves the singleton's reference
    // identity (`&Instance() == &Instance()`).
    static const LambdaConversion& Instance()
    {
        static const LambdaConversion instance;
        return instance;
    }

    // The C# `override bool IsAnonymousFunctionConversion => true`.
    bool IsAnonymousFunctionConversion() const override { return true; }

    // The C# `override bool IsImplicit => true`.
    bool IsImplicit() const override { return true; }
};

// The C# `sealed class DecompiledLambdaResolveResult : LambdaResolveResult` (the
// concrete subclass the decompiler's back end constructs from a decompiled
// ILFunction). The two prerequisites the original deferral named have since
// landed: (1) the `ILFunction` surfaces (the pre-resolved `ReturnType`/
// `AsyncReturnType`/`Parameters` fields -- the C# `Parameters`/`ReturnType`
// access the `ILFunction.Method`'s surfaces, the pre-resolved-fields precedent)
// and (2) the `CSharpConversions` controller (the `IdentityConversion`/
// `ImplicitConversion` helpers its `IsValid` composes). The C# is `sealed`, so
// the port is `final` (the is-final static_assert below).
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

class DecompiledLambdaResolveResult final : public LambdaResolveResult {
public:
    // The C# ctor: `function ?? throw ArgumentNullException`,
    // `delegateType ?? throw`, `inferredReturnType ?? throw` (the C#
    // ArgumentNullException guards port to the assert-then-store convention,
    // the D424 base-ctor convention), then the three flags and the
    // `SpecialType.UnknownType` Body stub. The C# `IL.ILFunction function` (a
    // non-null reference) ports to a non-owning `const IL::ILFunction*` (the
    // C# GC reference is a mutable handle; the function outlives the result --
    // the decompiler's per-function decompile run owns both).
    DecompiledLambdaResolveResult(const IL::ILFunction& function,
                                  TS::ITypePtr delegateType,
                                  TS::ITypePtr inferredReturnType,
                                  bool hasParameterList, bool isAnonymousMethod,
                                  bool isImplicitlyTyped)
        : function_(&function),
          delegateType_(std::move(delegateType)),
          inferredReturnType_(std::move(inferredReturnType)),
          hasParameterList_(hasParameterList),
          isAnonymousMethod_(isAnonymousMethod),
          isImplicitlyTyped_(isImplicitlyTyped)
    {
        assert(function_ != nullptr
               && "DecompiledLambdaResolveResult: function must not be null");
        assert(delegateType_ != nullptr
               && "DecompiledLambdaResolveResult: delegateType must not be null");
        assert(inferredReturnType_ != nullptr
               && "DecompiledLambdaResolveResult: inferredReturnType must not be null");
        body_ = std::make_shared<Sem::ResolveResult>(TS::UnknownType());
    }

    // The C# `public readonly IType DelegateType` field -- the target delegate
    // type (public in the C# source).
    const TS::IType& DelegateType() const { return *delegateType_; }

    // The C# `public IType InferredReturnType` field (public, mutable in the C#
    // -- the return-type inference may refine it before consumption). The port
    // exposes the read-only reference; the C# field write sites are the
    // type-inference steps that run before the result is consumed.
    const TS::IType& InferredReturnType() const { return *inferredReturnType_; }

    bool HasParameterList() const override { return hasParameterList_; }
    bool IsAnonymousMethod() const override { return isAnonymousMethod_; }
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped_; }

    // The C# `override bool IsAsync => function.IsAsync`.
    bool IsAsync() const override { return function_->IsAsync(); }

    // The C# `override IReadOnlyList<IParameter> Parameters => function.Parameters`
    // -- a non-owning pointer snapshot (the IParameterizedMember::Parameters
    // convention; the parameters are owned by the ILFunction's pre-resolved
    // Parameters list).
    std::vector<const TS::IParameter*> Parameters() const override
    {
        std::vector<const TS::IParameter*> result;
        result.reserve(function_->Parameters.size());
        for (const auto& parameter : function_->Parameters)
            result.push_back(parameter.get());
        return result;
    }

    // The C# `override IType ReturnType => function.ReturnType`.
    const TS::IType& ReturnType() const override { return *function_->ReturnType; }

    TS::ITypePtr GetInferredReturnType(
        const std::vector<TS::ITypePtr>& /*parameterTypes*/) const override
    {
        // We don't know how to compute which type would be inferred if given
        // other parameter types. Let's hope this is good enough:
        return inferredReturnType_;
    }

    // The C# `override Conversion IsValid(IType[] parameterTypes, IType
    // returnType, CSharpConversions conversions)`: the parameter-count/identity
    // gates, then the return-type gates. The C# `Conversion.None` /
    // `LambdaConversion.Instance` singletons port to the D406 `Conversions::None()`
    // factory and the `LambdaConversion::Instance()` reference (the return is a
    // NON-OWNING aliasing `shared_ptr` over the singleton -- the C# GC reference
    // is a non-owning handle to the singleton object).
    std::shared_ptr<Sem::Conversion> IsValid(
        const std::vector<TS::ITypePtr>& parameterTypes,
        const TS::ITypePtr& returnType, CSharpConversions& conversions) const override
    {
        // Anonymous method expressions without parameter lists are applicable to
        // any parameter list.
        if (hasParameterList_) {
            if (Parameters().size() != parameterTypes.size())
                return Conversions::None();
            for (std::size_t i = 0; i < parameterTypes.size(); ++i) {
                const TS::IParameter* parameter = Parameters()[i];
                if (parameter == nullptr)
                    return Conversions::None();
                // The port's IdentityConversion takes non-const IType& (the
                // AcceptVisitor is non-const, the D514 convention); the
                // IParameter::Type() accessor is const.
                if (!Detail::IdentityConversion(
                        const_cast<TS::IType&>(parameter->Type()),
                        *parameterTypes[i])) {
                    if (isImplicitlyTyped_) {
                        // it's possible that different parameter types also
                        // lead to a valid conversion
                        return LambdaConversionShared();
                    }
                    else {
                        return Conversions::None();
                    }
                }
            }
        }
        TS::IType& inferred = *inferredReturnType_;
        TS::IType& target = *returnType;
        if (Detail::IdentityConversion(*function_->ReturnType, target)) {
            return LambdaConversionShared();
        }
        auto implicitConversion = conversions.ImplicitConversion(inferred, target);
        if (implicitConversion != nullptr && implicitConversion->IsValid()) {
            return LambdaConversionShared();
        }
        else {
            return Conversions::None();
        }
    }

    // The C# `override ResolveResult Body` (assigned in the ctor:
    // `new ResolveResult(SpecialType.UnknownType)`).
    Sem::ResolveResult& Body() const override { return *body_; }

    // The C# `ShallowClone` (the D424 slicing-prevention convention): the
    // default copy ctor shares the function handle and the type handles and
    // copies the flags.
    std::unique_ptr<Sem::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<DecompiledLambdaResolveResult>(*this);
    }

private:
    // The `LambdaConversion.Instance()` reference wrapped as a NON-OWNING
    // `shared_ptr` (the aliasing-constructor idiom with a null owner -- the C#
    // return of the process-lifetime singleton is a non-owning reference).
    static std::shared_ptr<Sem::Conversion> LambdaConversionShared()
    {
        // A NON-OWNING shared_ptr over the process-lifetime singleton (the
        // no-op deleter idiom -- the C# return of the static-readonly singleton
        // is a non-owning reference; the process-lifetime object is never
        // destroyed through it).
        static const LambdaConversion* const singleton = &LambdaConversion::Instance();
        return std::shared_ptr<Sem::Conversion>(
            std::shared_ptr<Sem::Conversion>(),
            const_cast<LambdaConversion*>(singleton));
    }

    const IL::ILFunction* function_;
    TS::ITypePtr delegateType_;
    TS::ITypePtr inferredReturnType_;
    bool hasParameterList_;
    bool isAnonymousMethod_;
    bool isImplicitlyTyped_;
    std::shared_ptr<Sem::ResolveResult> body_;
};

static_assert(std::is_final_v<DecompiledLambdaResolveResult>,
              "DecompiledLambdaResolveResult ports the C# sealed class");

} // namespace ILSpy::Decompiler::CSharp::Resolver} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_LAMBDARESOLVERESULT_HPP
