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
// arm). Two regions are landed: the Input/Output Types region (C# spec draft-v11 sections
// 12.6.3.4 + 12.6.3.5) and the ContainsUnfixed region (the `TP` per-type-parameter state
// holder, the `OccursInVisitor`, `AnyTypeContainsUnfixedParameter`, and the
// `InputTypesContainsUnfixed`/`OutputTypeContainsUnfixed` wrappers) -- both are PURE (they
// read only the argument `ResolveResult`'s runtime type, the delegate-or-expression-tree
// signature resolved from the parameter type, and the `TP` state threaded as a parameter),
// so they lift to `Detail::` free functions ahead of the `TypeInference` class skeleton (the
// `CSharpConversionsHelpers` / `OverloadResolutionHelpers` lift-to-free-functions precedent).
// The remaining regions (`InferTypeArguments`, the Inference Phases, `MakeOutputTypeInference`/
// `MakeExactInference`/`MakeLowerBoundInference`/`MakeUpperBoundInference`, `Fixing`,
// `GetBestCommonType`, `FindTypeInBounds`) need the remaining
// `TypeInference` instance state (`arguments`/`parameterTypes` -- now threadable as
// parameters, as the `CalculateDependencyMatrix` lift below demonstrates) and land with the
// class skeleton in later increments.
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
#include <vector>

// `ResolveResult` is included transitively by the two ResolveResult subclass headers above
// (their base class), so the `const ResolveResult&` parameters need no extra include.

// `IType` is included transitively by the ResolveResult hierarchy (the base `ResolveResult`
// holds an `ITypePtr`), so the `const IType&` parameter needs no extra include.

// `IMethod` is forward-declared (the `const IMethod*` return type is a nullable non-owning
// pointer -- a complete pointer type with the class incomplete; the .cpp includes the full
// header for `Parameters()`/`ReturnType()`).
namespace ILSpy::Decompiler::TypeSystem { class IMethod; }

// `ITypeParameter` is forward-declared (the `TP` state holder stores a non-owning pointer and
// `OccursInVisitor::VisitTypeParameter` takes a reference; `TypeVisitor.hpp` declares it too
// -- kept here so the header is self-contained). The call sites hold complete type
// parameters (the compilation owns the real ones, the tests own the stubs).
namespace ILSpy::Decompiler::TypeSystem { class ITypeParameter; }

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
