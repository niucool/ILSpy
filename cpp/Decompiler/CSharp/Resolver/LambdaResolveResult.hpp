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
//   * PORTED: `sealed class DecompiledLambdaResolveResult : LambdaResolveResult` (the
//     concrete subclass the decompiler's back end constructs). The C# surface:
//       - the ctor `(ILFunction function, IType delegateType, IType inferredReturnType,
//         bool hasParameterList, bool isAnonymousMethod, bool isImplicitlyTyped)` -- the
//         C# `?? throw new ArgumentNullException` guards port to `assert`s (the D424 ctor
//         convention); the `Body` is a fresh `new ResolveResult(SpecialType.UnknownType)`.
//       - the `public readonly IType DelegateType` field and the `public IType
//         InferredReturnType` field (the latter MUTABLE -- `CallBuilder.ModifyReturnTypeOfLambda`
//         assigns it).
//       - the three ctor-captured bools (`HasParameterList` / `IsAnonymousMethod` /
//         `IsImplicitlyTyped`), the `IsAsync` / `Parameters` / `ReturnType` projections over
//         the held `ILFunction`, the `GetInferredReturnType` (which returns the stored
//         `InferredReturnType` -- "we don't know how to compute which type would be inferred
//         if given other parameter types"), and the `IsValid` conversion check.
//     `ILFunction.Parameters` (the new `IReadOnlyList<IParameter>` field) and
//     `CSharpConversions` (landed) are the prerequisites that previously blocked this class;
//     `IsValid` reads the conversions through the `Detail::IdentityConversion` free function
//     (the port has no public `IdentityConversion` method, the D547 precedent) and the public
//     `CSharpConversions::ImplicitConversion`.
//
// All other deps are already ported: `ResolveResult` (D424, the base), `SpecialType.NoType`
// (the D433 `NoType()` `IType.hpp` convenience), `IType`/`ITypePtr` (D271), `IParameter`
// (D370), `Conversion` (D406, the `LambdaConversion` base), and `SpecialType.UnknownType`
// (the `Body` stub type, D271).
//
// KEY PORT CONVENTIONS:
//  * The C# `abstract class LambdaResolveResult` ports to a C++ class with PURE-VIRTUAL
//     members (mirroring the C# `abstract` members one-to-one); the concrete
//     `DecompiledLambdaResolveResult` subclass is below. Unlike the D406
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
//     `CSharpConversions` (landed) now exists; the base `IsValid` remains callable only by
//     concrete subclasses (the abstract base cannot construct one).
//  * The C# `ResolveResult Body { get; }` (a non-null reference -- the C#
//     `GetChildResults()` returns `new[] { this.Body }` unconditionally) ports to
//     `virtual Semantics::ResolveResult& Body() const = 0` (a never-null reference return).
//     The concrete `DecompiledLambdaResolveResult` holds the body as a
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
//     (the D424 slicing-prevention convention), as
//     `DecompiledLambdaResolveResult` does.
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

#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
class IParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::IL {
class ILFunction;
} // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::CSharp::Resolver {

// The conversion controller the C# `IsValid` consults (ICSharpCode.Decompiler/CSharp/Resolver/
// CSharpConversions.cs, ~2500 lines -- unported). Forward-declared so the pure-virtual
// `IsValid` signature can reference it; call sites unblock with the `CSharpConversions`
// port.
class CSharpConversions;

// The C# `public abstract class LambdaResolveResult : ResolveResult` -- the result of an
// anonymous method or lambda expression. Note: the lambda has no type (the base type is
// `SpecialType.NoType`); to retrieve the delegate type, look at the anonymous-function
// conversion. Abstract: the C# decompiler back end constructs the concrete
// `DecompiledLambdaResolveResult` subclass.
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

    // An owning handle to the same process-wide singleton, for callers that model the C#
    // `Conversion` reference as a `shared_ptr` (the D406 Conversion-handle convention --
    // `DecompiledLambdaResolveResult.IsValid` returns it). The `shared_ptr` shares the
    // never-destroyed function-local static `Instance()` returns, its no-op deleter leaves
    // that static untouched, and every call yields a handle to the same object, so the
    // C# singleton reference-identity is preserved.
    static std::shared_ptr<LambdaConversion> InstancePtr()
    {
        static std::shared_ptr<LambdaConversion> instance(
            const_cast<LambdaConversion*>(&Instance()), [](LambdaConversion*) {});
        return instance;
    }
};

// The C# `sealed class DecompiledLambdaResolveResult : LambdaResolveResult` -- the concrete
// lambda resolve result the decompiler's back end constructs. It projects the lambda's async /
// parameter / return-type surface over the held `ILFunction` and answers `IsValid` through the
// `CSharpConversions` controller.
class DecompiledLambdaResolveResult final : public LambdaResolveResult {
public:
    // The C# `public readonly IType DelegateType` field -- the delegate type the lambda body is
    // converted to (the anonymous-function conversion target).
    const ILSpy::Decompiler::TypeSystem::ITypePtr DelegateType;

    // The C# `public IType InferredReturnType` field -- the return type inferred from the lambda
    // body, which can differ from `ReturnType` when a return statement performs an implicit
    // conversion. MUTABLE: `CallBuilder.ModifyReturnTypeOfLambda` assigns it.
    ILSpy::Decompiler::TypeSystem::ITypePtr InferredReturnType;

    // The C# ctor. The `?? throw new ArgumentNullException` guards on `function` / `delegateType`
    // / `inferredReturnType` port to `assert`s (the D424 ctor convention); `Body` is a fresh
    // `new ResolveResult(SpecialType.UnknownType)`.
    DecompiledLambdaResolveResult(const ILSpy::Decompiler::IL::ILFunction* function,
                                  ILSpy::Decompiler::TypeSystem::ITypePtr delegateType,
                                  ILSpy::Decompiler::TypeSystem::ITypePtr inferredReturnType,
                                  bool hasParameterList,
                                  bool isAnonymousMethod,
                                  bool isImplicitlyTyped);

    // The C# `override bool HasParameterList { get; }` -- the ctor-captured value.
    bool HasParameterList() const override { return hasParameterList_; }

    // The C# `override bool IsAnonymousMethod { get; }` -- the ctor-captured value.
    bool IsAnonymousMethod() const override { return isAnonymousMethod_; }

    // The C# `override bool IsImplicitlyTyped { get; }` -- the ctor-captured value.
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped_; }

    // The C# `override bool IsAsync => function.IsAsync`.
    bool IsAsync() const override;

    // The C# `override IType GetInferredReturnType(IType[] parameterTypes)` -- the stored
    // `InferredReturnType` (the parameter types are ignored: "We don't know how to compute which
    // type would be inferred if given other parameter types.").
    ILSpy::Decompiler::TypeSystem::ITypePtr GetInferredReturnType(
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& parameterTypes) const override;

    // The C# `override IReadOnlyList<IParameter> Parameters => function.Parameters`.
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override;

    // The C# `override IType ReturnType => function.ReturnType`.
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override;

    // The C# `override Conversion IsValid(IType[] parameterTypes, IType returnType,
    // CSharpConversions conversions)`: with a parameter list, a count mismatch yields
    // `Conversion.None` and a parameter-type identity mismatch yields `LambdaConversion.Instance`
    // for an implicitly typed lambda (another parameter typing might still be valid) or
    // `Conversion.None` for an explicitly typed one; otherwise an identity conversion between the
    // lambda return type and `returnType`, or an implicit conversion from the inferred return
    // type, yields `LambdaConversion.Instance`, else `Conversion.None`.
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> IsValid(
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& parameterTypes,
        const ILSpy::Decompiler::TypeSystem::ITypePtr& returnType,
        CSharpConversions& conversions) const override;

    // The C# `override ResolveResult Body { get; }` -- the ctor-created unknown-type result.
    ILSpy::Decompiler::Semantics::ResolveResult& Body() const override { return *body_; }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `body_` shared_ptr is shared, the `ITypePtr` fields are
    // shared, the held `ILFunction` pointer and the three bools are copied, the base `type_`
    // shared_ptr is shared). The default copy ctor reproduces this.
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<DecompiledLambdaResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields (the C# does NOT override
    // `ToString`; the inherited `ResolveResult::ToString` reads this).
    std::string ClassName() const override { return "DecompiledLambdaResolveResult"; }

private:
    // The lambda body's `ILFunction` (the C# `readonly IL.ILFunction function`). Non-owning:
    // the tree owns it, the resolve result observes it (the C# lambda resolve result lives as
    // an annotation on an AST node built from the same function).
    const ILSpy::Decompiler::IL::ILFunction* function_;
    bool hasParameterList_;
    bool isAnonymousMethod_;
    bool isImplicitlyTyped_;
    // The C# `override ResolveResult Body { get; }` (`new ResolveResult(SpecialType.UnknownType)`).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> body_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_LAMBDARESOLVERESULT_HPP
