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

// Port of the `TypeInference` PURE LEAF HELPERS -- the landed slices of the C# type-inference
// engine (ICSharpCode.Decompiler/CSharp/Resolver/TypeInference.cs, the ~1188-line long-pole
// blocker for the `OverloadResolution` `RunTypeInference` engine step and therefore for
// `CalculateCandidate`/`AddCandidate`/`AddMethodLists` and the deferred `MethodGroupConversion`
// arm). FIVE regions are landed, all lifted to `Detail::` free functions ahead of the
// `TypeInference` class skeleton (the `CSharpConversionsHelpers` / `OverloadResolutionHelpers`
// lift-to-free-functions precedent): the Input/Output Types region (C# spec draft-v11
// sections 12.6.3.4 + 12.6.3.5), the ContainsUnfixed region (the `TP` per-type-parameter
// state holder, the `OccursInVisitor`, `AnyTypeContainsUnfixedParameter`, and the
// `InputTypesContainsUnfixed`/`OutputTypeContainsUnfixed` wrappers), the DependsOn region
// (section 12.6.3.6: the dependency matrix and its Warshall closure), the MakeInference
// bound-inference core (the `GetTPForType` lookup, the three mutually recursive spec
// workers `MakeExactInference`/`MakeLowerBoundInference`/`MakeUpperBoundInference`, and
// the `MakeExplicitParameterTypeInference` phase-one entry) -- the instance state
// (`typeParameters`, and the `compilation` the span arms read `TypeSystemOptions` through)
// threads as parameters (the `CalculateDependencyMatrix` lift demonstrates the convention)
// -- and the MakeOutputTypeInference region (C# 4.0 spec section 7.5.2.6: the fourth
// worker over the LAMBDA argument shape plus the plain-expression arm, with the
// `GetSubstitutionForFixedTPs` fixed-TP substitution and the `IsValidType` gate; its
// METHOD-GROUP arm stays deferred on `PerformOverloadResolution`). The remaining regions
// (`InferTypeArguments`, the Inference Phases, `Fixing`, `GetBestCommonType`,
// `FindTypeInBounds`) need further instance state (`arguments`/`parameterTypes`) and land
// in later increments.
//
// RETURN CONVENTION: the C# `IType[]` returns fresh arrays of GC-owned references; the port
// returns `std::vector<const IType*>` non-owning raw-pointer snapshots (the `GetMethods` /
// `GetAllBaseTypes` "type system owns the entities, the caller holds raw pointers"
// convention) -- the delegate-invoke method owns its parameter/return types and is owned by
// the delegate type reachable through the parameter type `t`, which the caller keeps alive;
// the empty-vector is the C# `Empty<IType>.Array`. The future consumers
// (`InputTypesContainsUnfixed`/`OutputTypeContainsUnfixed`/`CalculateDependencyMatrix`) feed
// the elements to `AcceptVisitor` (non-const, D406) via `const_cast` (the D517 precedent).

#pragma once

#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"  // LambdaResolveResult (InputTypes/OutputTypes RTTI)
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"  // MethodGroupResolveResult (InputTypes/OutputTypes RTTI)
#include "Decompiler/TypeSystem/TypeVisitor.hpp"  // TypeVisitor (the OccursInVisitor base class)

#include <cstddef>  // std::size_t (the visitor's index arithmetic)
#include <memory>  // std::shared_ptr (the `CalculateDependencyMatrix` arguments parameter)
#include <optional>  // std::optional (the `GetSubstitutionForFixedTPs` classTypeArguments)
#include <vector>

// `ResolveResult` is included transitively by the two ResolveResult subclass headers above
// (their base class), so the `const ResolveResult&` parameters need no extra include.

// `IType` is included transitively by the ResolveResult hierarchy (the base `ResolveResult`
// holds an `ITypePtr`), so the `const IType&` parameter needs no extra include.

// `IMethod` is forward-declared (the `const IMethod*` return type is a nullable non-owning
// pointer -- a complete pointer type with the class incomplete; the .cpp includes the full
// header for `Parameters()`/`ReturnType()`).
namespace ILSpy::Decompiler::TypeSystem { class IMethod; }

// `ICompilation` is forward-declared (the `const ICompilation&` parameter of the MakeInference
// region below -- the span arms of the three bound-inference workers read
// `compilation.TypeSystemOptions().HasFlag(TypeSystemOptions.FirstClassSpanTypes)`, the
// D538 `IsImplicitSpanConversion` gate; the .cpp includes the full header for the
// `TypeSystemOptions()` virtual).
namespace ILSpy::Decompiler::TypeSystem { class ICompilation; }

// `ITypeParameter` is forward-declared (the `TP` state holder stores a non-owning pointer and
// `OccursInVisitor::VisitTypeParameter` takes a reference; `TypeVisitor.hpp` declares it too
// -- kept here so the header is self-contained). The call sites hold complete type
// parameters (the compilation owns the real ones, the tests own the stubs).
namespace ILSpy::Decompiler::TypeSystem { class ITypeParameter; }

// `TypeParameterSubstitution` is forward-declared (the by-value return of the
// `GetSubstitutionForFixedTPs` declaration below needs only an incomplete type; the .cpp
// and the call sites include the full `TypeParameterSubstitution.hpp`).
namespace ILSpy::Decompiler::TypeSystem { class TypeParameterSubstitution; }

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

// The C# `static IMethod GetDelegateOrExpressionTreeSignature(IType t)` -- the target-type
// unwrap the input/output-type collectors use: an `Expression<T>` (a 1-type-argument type
// named "Expression" in namespace "System.Linq.Expressions") resolves the signature of the
// WRAPPED delegate `T`; any other type resolves its own delegate `Invoke` method (the
// TypeSystemExtensions `GetDelegateInvokeMethod`, D533). Returns null for a non-delegate type.
//
// The C# `t.Namespace` reads the `IType : INamedElement` `Namespace` property; the port's
// `IType` interface does not carry `Namespace` (it lives on `ITypeDefinition`), so the
// faithful port reads it via `t.GetDefinition()->Namespace()` with a null guard (the
// `UnpackExpressionTreeType` D534 convention -- a definitionless type yields a failed
// namespace check, no unwrap, matching the C# where the non-definition's `Namespace` would
// be empty). The C# `t.TypeArguments[0]` is `ParameterizedType`-specific in the port, so the
// rebind goes through a `dynamic_cast` (the D516 convention).
const ILSpy::Decompiler::TypeSystem::IMethod* GetDelegateOrExpressionTreeSignature(
    const ILSpy::Decompiler::TypeSystem::IType& t);

// The C# `IType[] InputTypes(ResolveResult e, IType t)` (C# spec draft-v11 section 12.6.3.4
// "Input types") -- the input types of an argument for the dependence computation: an
// IMPLICITLY-TYPED lambda (no explicit parameter types to infer from) or a method group
// contributes the delegate signature's PARAMETER types (via
// `GetDelegateOrExpressionTreeSignature`); anything else (an explicitly-typed lambda, a
// plain expression) contributes nothing (the empty array). The C#
// `lrr != null && lrr.IsImplicitlyTyped || e is MethodGroupResolveResult` operator precedence
// is `(lambda && IsImplicitlyTyped) || methodGroup` -- the port mirrors it with explicit
// parentheses.
std::vector<const ILSpy::Decompiler::TypeSystem::IType*> InputTypes(
    const ILSpy::Decompiler::Semantics::ResolveResult& e,
    const ILSpy::Decompiler::TypeSystem::IType& t);

// The C# `IType[] OutputTypes(ResolveResult e, IType t)` (C# spec draft-v11 section 12.6.3.5
// "Output types") -- the output types of an argument: ANY lambda (implicitly OR explicitly
// typed -- unlike `InputTypes`, whose implicit-typing requirement excludes the explicitly
// typed lambdas whose parameter types are already known) or a method group contributes the
// delegate signature's RETURN type (a one-element array); anything else contributes nothing.
std::vector<const ILSpy::Decompiler::TypeSystem::IType*> OutputTypes(
    const ILSpy::Decompiler::Semantics::ResolveResult& e,
    const ILSpy::Decompiler::TypeSystem::IType& t);

// ===========================================================================
// The ContainsUnfixed region (TypeInference.cs lines 217-271 + 442-465) -- the per-type-
// parameter inference state (`TP`), the visitor that records type-parameter occurrences
// (`OccursInVisitor`), and the three "does any UNFIXED type parameter occur" predicates the
// dependence computation (section 12.6.3.6) and both inference phases consult.
// ===========================================================================

// The C# `sealed class TP` (TypeInference.cs lines 217-250) -- the per-type-parameter
// inference state: the `ITypeParameter` this entry tracks, the fixed/unfixed state
// (`FixedTo`), and the accumulated bounds. The C# instance field `TP[] typeParameters`
// becomes the `std::vector<TP>` the lifted free functions thread as a parameter; the eventual
// `InferTypeArguments` port owns the vector.
struct TP {
    // The C# `readonly HashSet<IType> LowerBounds = new HashSet<IType>();` (and the
    // `UpperBounds` twin) -- the port stores the bounds as owning `ITypePtr`s in insertion
    // order with the `HashSet<IType>.Add` dedup semantics implemented in the `Add*Bound`
    // members (a `std::unordered_set` needs a hash and a `std::set` needs an ordering --
    // neither is on the `IType` surface; the bound lists are tiny, so a linear dedup scan is
    // the faithful behavior).
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> LowerBounds;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> UpperBounds;
    // The C# `IType ExactBound;` + `bool MultipleDifferentExactBounds;` -- the exact bound is
    // stored SEPARATELY from the lower/upper bounds, not folded into them (the C# comment at
    // line 242: exact bounds need separate storage, see icsharpcode/ILSpy issue #281).
    ILSpy::Decompiler::TypeSystem::ITypePtr ExactBound;
    bool MultipleDifferentExactBounds = false;
    // The C# `readonly ITypeParameter TypeParameter;` -- a non-owning raw pointer (the real
    // type parameters are owned by the compilation; the `OccursInVisitor` compares it with
    // the visited type parameter by pointer, the C# reference-equality `==`).
    const ILSpy::Decompiler::TypeSystem::ITypeParameter* TypeParameter;
    // The C# `IType FixedTo;` -- null until the parameter is fixed.
    ILSpy::Decompiler::TypeSystem::ITypePtr FixedTo;

    // The C# `bool IsFixed { get { return FixedTo != null; } }` (a property -> accessor).
    bool IsFixed() const { return FixedTo != nullptr; }

    // The C# `bool HasBounds { get { return LowerBounds.Count > 0 || UpperBounds.Count > 0
    // || ExactBound != null; } }`.
    bool HasBounds() const
    {
        return !LowerBounds.empty() || !UpperBounds.empty() || ExactBound != nullptr;
    }

    // The C# `TP(ITypeParameter typeParameter)` (the null check compiles out under the D374
    // non-null reference convention).
    explicit TP(const ILSpy::Decompiler::TypeSystem::ITypeParameter& typeParameter)
        : TypeParameter(&typeParameter) {}

    // The C# `void AddExactBound(IType type)` (lines 240-248) -- the FIRST exact bound is
    // stored; a later bound that does not EQUAL it raises `MultipleDifferentExactBounds`.
    void AddExactBound(ILSpy::Decompiler::TypeSystem::ITypePtr type);

    // The `tp.LowerBounds.Add(U)` / `tp.UpperBounds.Add(U)` call sites (lines 750/879, the
    // `MakeLowerBoundInference`/`MakeUpperBoundInference` regions to come) -- the
    // `HashSet<IType>.Add` idempotence under `IType.Equals`.
    void AddLowerBound(ILSpy::Decompiler::TypeSystem::ITypePtr type);
    void AddUpperBound(ILSpy::Decompiler::TypeSystem::ITypePtr type);
};

// The C# `sealed class OccursInVisitor : TypeVisitor` (TypeInference.cs lines 256-271) --
// records which of the inference's type parameters OCCUR in the visited types. The C#
// constructor reads the `TypeInference` instance's `typeParameters` field; the lift threads
// the `TP` vector as the constructor parameter.
class OccursInVisitor : public ILSpy::Decompiler::TypeSystem::TypeVisitor {
public:
    explicit OccursInVisitor(const std::vector<TP>& typeParameters)
        : tp_(typeParameters), occurs_(typeParameters.size(), false) {}

    // The C# `public readonly bool[] Occurs;` (a field the visitor itself writes -- the
    // accessor hands it back read-only).
    const std::vector<bool>& Occurs() const { return occurs_; }

    // The C# `public override IType VisitTypeParameter(ITypeParameter type)`.
    ILSpy::Decompiler::TypeSystem::ITypePtr VisitTypeParameter(
        ILSpy::Decompiler::TypeSystem::ITypeParameter& type) override;

private:
    const std::vector<TP>& tp_;
    std::vector<bool> occurs_;
};

// The C# `bool AnyTypeContainsUnfixedParameter(IEnumerable<IType> types)` (TypeInference.cs
// line 453) -- the shared worker behind the two ContainsUnfixed entry points: visits every
// type with the `OccursInVisitor`, then reports whether any UNFIXED type parameter occurred.
bool AnyTypeContainsUnfixedParameter(
    const std::vector<TP>& typeParameters,
    const std::vector<const ILSpy::Decompiler::TypeSystem::IType*>& types);

// The C# `bool InputTypesContainsUnfixed(ResolveResult argument, IType parameterType)`
// (TypeInference.cs line 442) -- whether any unfixed type parameter occurs among the
// argument's input types (the `InputTypes` collector above).
bool InputTypesContainsUnfixed(
    const std::vector<TP>& typeParameters,
    const ILSpy::Decompiler::Semantics::ResolveResult& argument,
    const ILSpy::Decompiler::TypeSystem::IType& parameterType);

// The C# `bool OutputTypeContainsUnfixed(ResolveResult argument, IType parameterType)`
// (TypeInference.cs line 447) -- whether any unfixed type parameter occurs among the
// argument's output types (the `OutputTypes` collector above).
bool OutputTypeContainsUnfixed(
    const std::vector<TP>& typeParameters,
    const ILSpy::Decompiler::Semantics::ResolveResult& argument,
    const ILSpy::Decompiler::TypeSystem::IType& parameterType);

// ===========================================================================
// The DependsOn region (TypeInference.cs lines 468-521) -- the dependence relation of
// C# spec draft-v11 section 12.6.3.6 "Dependence": `Xi` depends on `Xj` if for some
// argument `Ek`, `Xj` occurs in an INPUT type of `Ek` and `Xi` occurs in an OUTPUT type of
// `Ek` (`dependencyMatrix[i, j] |= input.Occurs[j] && output.Occurs[i]`), closed transitively
// with Warshall's algorithm. Both inference phases consult the relation (the first
// `PhaseTwo` fix-round fixes the type parameters that depend on nothing unfixed).
// ===========================================================================

// The C# `void CalculateDependencyMatrix()` (TypeInference.cs lines 471-510) -- the
// per-argument occurrence accumulation followed by the Warshall transitive closure. The
// C# `bool[,] dependencyMatrix` instance field becomes the RETURNED `n x n` matrix (a row
// vector per row -- the C# 2-D `bool[,]` array; row index = the DEPENDING type parameter's
// `Index`, column index = the depended-on one), because the C# `DependsOn` lazily computes
// the matrix on the first call and memoizes it in the instance field for the rest of the
// inference, while the lift computes it ONCE at the call site and threads it into
// `DependsOn` (the instance-state-threading convention, the `ConsiderIfNewCandidateIsBest`
// precedent) -- behavior-identical for the inference phases, which only ever read the
// completed matrix.
//
// The C# reads the `arguments`/`parameterTypes` INSTANCE fields, which the
// `InferTypeArguments` constructor sized to the COMMON MINIMUM of the caller's lists
// (`this.parameterTypes = new IType[Math.Min(arguments.Count, parameterTypes.Count)]`) with
// a null entry rejected outright (`ArgumentNullException`); so the lift threads them as
// parameters and iterates to the common min (the faithful bound -- the C#
// `arguments.Length == parameterTypes.Length` always holds inside the class), and a null
// entry (never passed by a real caller) is SKIPPED (the D516 degenerate-shape convention).
std::vector<std::vector<bool>> CalculateDependencyMatrix(
    const std::vector<TP>& typeParameters,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& parameterTypes);

// The C# `bool DependsOn(TP x, TP y)` (TypeInference.cs lines 512-518) -- the matrix lookup:
// "x depends on y" (`dependencyMatrix[x.TypeParameter.Index, y.TypeParameter.Index]`; the
// C# lazy `if (dependencyMatrix == null) CalculateDependencyMatrix();` memoization becomes
// the compute-once-then-thread convention above). The bounds guard is the defensive
// addition: the C# indexes unconditionally and `InferTypeArguments` guarantees
// `typeParameters[i].Index == i`, so the real indexes are always in bounds; a degenerate
// out-of-range index would throw `IndexOutOfRangeException` in the C# and returns false
// here (the D516 convention).
bool DependsOn(const std::vector<std::vector<bool>>& dependencyMatrix, const TP& x, const TP& y);

// ===========================================================================
// The MakeInference region (TypeInference.cs lines 613-962 + 717-729) -- the
// bound-inference CORE of the type-inference engine: the per-type-parameter lookup
// (`GetTPForType`), the three mutually recursive spec workers
// (`MakeExactInference` C# 4.0 spec section 7.5.2.8, `MakeLowerBoundInference` spec
// draft-v11 section 12.6.3.11, `MakeUpperBoundInference` C# 4.0 spec section 7.5.2.10),
// and the phase-one explicitly-typed-lambda entry (`MakeExplicitParameterTypeInference`,
// spec draft-v11 section 12.6.3.9). These are the workers BOTH public entries consume:
// `InferTypeArguments` (via `PhaseOne`/`PhaseTwo`) and `InferTypeArgumentsFromBounds` (which
// feeds its lower/upper bounds straight into the two bound workers). `MakeOutputTypeInference`
// (lines 522-611) -- the fourth entry, over the LAMBDA/METHOD-GROUP argument shapes -- is
// the NEXT region below: its lambda arm needs the now-landed `GetSubstitutionForFixedTPs`
// (the `TypeParameterSubstitution` over the fixed TPs plus `classTypeArguments`); only its
// method-group arm needs `MethodGroupResolveResult.PerformOverloadResolution` (the
// `OverloadResolution` engine long pole) and stays deferred there.
//
// The C# workers are instance methods reading the `typeParameters` field and the
// `compilation` field; the lift threads both as parameters (the established convention --
// the `TP` state MUTATES through the bound adds, so it threads as `std::vector<TP>&`).
// ===========================================================================

// The C# `TP GetTPForType(IType v)` (TypeInference.cs lines 717-729) -- the inference state
// entry for a type: a nullability-annotated type parameter delegates to its ORIGINAL
// (un-annotated) parameter, and a type parameter whose `Index` selects a `TP` entry tracking
// THAT VERY PARAMETER resolves to it (the reference-equality `typeParameters[index]
// .TypeParameter == p` -- a different parameter instance with the same index does not
// resolve). Returns a MUTABLE pointer into the threaded vector (the callers add bounds
// through it) or null.
TP* GetTPForType(std::vector<TP>& typeParameters,
                const ILSpy::Decompiler::TypeSystem::IType& v);

// The C# `void MakeExplicitParameterTypeInference(LambdaResolveResult e, IType t)` (TypeInference.cs
// lines 614-627, spec draft-v11 section 12.6.3.9 "Explicit parameter type inferences") -- the
// phase-one entry for an EXPLICITLY-typed lambda against a delegate/expression-tree target:
// an exact inference from each explicitly-declared lambda parameter type to the corresponding
// delegate-signature parameter type. The implicitly-typed / no-parameter-list lambdas return
// without inferring (their parameter types are what inference produces). The port takes the
// verified `const LambdaResolveResult&` (the D534 convention: the dispatch owns the RTTI -- the
// phase-one caller dynamic_casts before calling).
void MakeExplicitParameterTypeInference(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                        std::vector<TP>& typeParameters,
                                        const LambdaResolveResult& e,
                                        const ILSpy::Decompiler::TypeSystem::IType& t);

// The C# `void MakeExactInference(IType U, IType V)` (TypeInference.cs lines 635-728, C# 4.0
// spec section 7.5.2.8 "Exact inferences") -- U must match V EXACTLY: an unfixed V-side type
// parameter takes U as an exact bound; by-reference/array/span/parameterized/pointer/
// function-pointer pairs recurse element-wise (the span arms gate on
// `FirstClassSpanTypes`; the parameterized arm requires the same generic type and arity).
// `U`/`V` are NON-CONST: the nullability strip calls `WithoutNullability` (`ChangeNullability`
// is non-const, the `shared_from_this` D406 convention) and the bound adds need owning handles
// (`shared_from_this`, the D529 convention).
void MakeExactInference(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                        std::vector<TP>& typeParameters,
                        ILSpy::Decompiler::TypeSystem::IType& u,
                        ILSpy::Decompiler::TypeSystem::IType& v);

// The C# `void MakeLowerBoundInference(IType U, IType V)` (TypeInference.cs lines 736-857,
// spec draft-v11 section 12.6.3.11 "Lower-bound inferences") -- U is at most V: an unfixed
// V-side type parameter takes U as a LOWER bound; the nullable covariance recursion, the
// array/span/array-interface element recursions, and the unique-base-type parameterized
// variance walk (covariant arguments recurse lower-bound, contravariant upper-bound, invariant
// exact) reduce U into V's shape. Note the by-ref and pointer arms recurse EXACT
// (reference shapes match exactly), and the function-pointer arm SWAPS: the return recurses
// lower-bound, the parameters upper-bound.
void MakeLowerBoundInference(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                             std::vector<TP>& typeParameters,
                             ILSpy::Decompiler::TypeSystem::IType& u,
                             ILSpy::Decompiler::TypeSystem::IType& v);

// The C# `void MakeUpperBoundInference(IType U, IType V)` (TypeInference.cs lines 865-961,
// C# 4.0 spec section 7.5.2.10 "Upper-bound inferences") -- U is at least V: an unfixed
// V-side type parameter takes U as an UPPER bound; the array-to-array-interface
// element recursion (an `IEnumerable<U>` upper-bounds a `U[]` parameter), and the
// unique-base-type parameterized variance walk (the mirror of the lower-bound one:
// covariant upper, contravariant lower). The function-pointer arm swaps in the opposite
// direction: the return recurses upper-bound, the parameters lower-bound.
void MakeUpperBoundInference(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                             std::vector<TP>& typeParameters,
                             ILSpy::Decompiler::TypeSystem::IType& u,
                             ILSpy::Decompiler::TypeSystem::IType& v);

// ===========================================================================
// The MakeOutputTypeInference region (TypeInference.cs lines 314-317 + 522-611, C# 4.0
// spec section 7.5.2.6 "Output type inferences") -- a lower-bound inference from an
// argument's OUTPUT type to the parameter type: a LAMBDA contributes its inferred return
// type (the implicitly-typed arm threads the fixed-TP-substituted delegate parameter
// types into `LambdaResolveResult.GetInferredReturnType`; the explicitly-typed arm
// passes none), a METHOD GROUP its resolved overload's return type (DEFERRED -- the
// `PerformOverloadResolution` engine long pole), and a plain expression its own type
// (gated on `IsValidType`). The C# reads the `typeParameters` / `classTypeArguments`
// instance fields; the lift threads both as parameters (the established convention).
// ===========================================================================

// The C# `static bool IsValidType(IType type)` (TypeInference.cs lines 314-317) -- a type
// is valid for inference when it is none of the three null-object kinds (the error
// `Unknown`, the `null` literal, and `NoType`). `PhaseOne`, `MakeOutputTypeInference`, and
// `GetBestCommonType` all gate an expression's type on it before feeding it to the bound
// workers.
bool IsValidType(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `TypeParameterSubstitution GetSubstitutionForFixedTPs()` (TypeInference.cs lines
// 602-611) -- the substitution threading the inference's FIXED decisions into the
// delegate-signature parameter types a nested lambda's `GetInferredReturnType` receives:
// every METHOD type parameter of the inference substitutes to its `FixedTo` (an UNFIXED
// parameter substitutes to `SpecialType.UnknownType`), while the CLASS type arguments
// thread from the `InferTypeArguments` caller. The C# heap allocation ports to a by-value
// return (the `TypeParameterSubstitution.Compose` convention).
ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution GetSubstitutionForFixedTPs(
    const std::vector<TP>& typeParameters,
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& classTypeArguments);

// The C# `void MakeOutputTypeInference(ResolveResult e, IType t)` (TypeInference.cs lines
// 522-600) -- the fourth bound-inference entry (the plain-expression counterpart of the
// three workers above): a lower-bound inference from the argument's output type to the
// parameter type. `t` is NON-CONST: the plain arm feeds it to `MakeLowerBoundInference` as
// the V side (the non-const `ChangeNullability`, D406); `e` is const (every member the
// live arms read -- `IsImplicitlyTyped` / `Parameters` / `GetInferredReturnType` / `Type`
// -- is const).
void MakeOutputTypeInference(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                             std::vector<TP>& typeParameters,
                             const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& classTypeArguments,
                             const ILSpy::Decompiler::Semantics::ResolveResult& e,
                             ILSpy::Decompiler::TypeSystem::IType& t);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
