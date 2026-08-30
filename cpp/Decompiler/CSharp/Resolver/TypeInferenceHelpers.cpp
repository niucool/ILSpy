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

// Implementation of the `TypeInference` pure leaf helpers (see TypeInferenceHelpers.hpp for
// the region overview and the return conventions).

#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"

#include <algorithm>  // std::min (the CalculateDependencyMatrix common-min bound)

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions (the cached public ImplicitConversion the Fixing region calls)
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (the RTTI base)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter (the delegate-invoke parameter types)
#include "Decompiler/TypeSystem/IMethod.hpp"  // IMethod (Parameters() / ReturnType())
#include "Decompiler/TypeSystem/IType.hpp"  // IType / ParameterizedType (the unwrap)
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (Namespace via GetDefinition)
#include "Decompiler/TypeSystem/ITypeParameter.hpp"  // ITypeParameter / NullabilityAnnotatedTypeParameter (the OccursInVisitor Index read; the GetTPForType unwrap)
#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (the TypeSystemOptions span gate)
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"  // DummyTypeParameter (the GetBestCommonType dummy)
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"  // KnownTypeCode (SpanOfT / ReadOnlySpanOfT)
#include "Decompiler/TypeSystem/NullableType.hpp"  // IsNullable / GetUnderlyingType (the nullable-covariance arm)
#include "Decompiler/TypeSystem/TupleType.hpp"  // TupleUnderlyingTypeOrSelf (the tuple-unwrap rebind)
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"  // TypeParameterSubstitution (the GetSubstitutionForFixedTPs return)
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetDelegateInvokeMethod (the D533 free function); IsKnownType / IsArrayInterfaceType / GetAllBaseTypes / WithoutNullability (the MakeInference region)
#include "Decompiler/TypeSystem/VarianceModifier.hpp"  // VarianceModifier (the parameterized variance walk)

#include <cassert>  // assert (the Fix Debug.Assert)

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::GetAllBaseTypes;
using ILSpy::Decompiler::TypeSystem::GetDelegateInvokeMethod;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IsArrayInterfaceType;
using ILSpy::Decompiler::TypeSystem::IsKnownType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedTypeParameter;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TupleUnderlyingTypeOrSelf;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
using ILSpy::Decompiler::TypeSystem::UnknownType;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::WithoutNullability;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;

// The C# `static IMethod GetDelegateOrExpressionTreeSignature(IType t)` (TypeInference.cs,
// the Input Types / Output Types region) -- the `Expression<T>` unwrap followed by the
// delegate `Invoke` resolution.
const IMethod* GetDelegateOrExpressionTreeSignature(const IType& t)
{
    // C# `if (t.TypeParameterCount == 1 && t.Name == "Expression"
    //     && t.Namespace == "System.Linq.Expressions") { t = t.TypeArguments[0]; }`
    //
    // The namespace is read via `GetDefinition()->Namespace()` with a null guard (the C#
    // `IType : INamedElement` carries `Namespace`; the port's `IType` does not -- the
    // `UnpackExpressionTreeType` D534 convention). A `ParameterizedType` over the real
    // `Expression`1` generic resolves the definition itself, so the namespace check fires;
    // a definitionless type fails the check and skips the unwrap, faithfully matching the
    // C# where the non-definition's `Namespace` would be empty.
    const IType* current = &t;
    if (current->TypeParameterCount() == 1 && current->Name() == "Expression") {
        const ITypeDefinition* def = current->GetDefinition();
        if (def != nullptr && def->Namespace() == "System.Linq.Expressions") {
            // C# `t = t.TypeArguments[0]` -- the C# `IType.TypeArguments` returns the type
            // parameters for a bare generic definition, but the port's `TypeArguments` is
            // `ParameterizedType`-specific (not on the `IType` interface), so the rebind
            // goes through a `dynamic_cast`. For the only real matching shape --
            // `Expression<T>`, a `ParameterizedType` -- the cast succeeds. A bare
            // `Expression`1` DEFINITION (Kind=Class, not parameterized) cannot carry the
            // unwrap here, but resolves the same result: the C# would rebind to the `T`
            // type parameter (Kind=TypeParameter), and both are non-delegate kinds, so
            // `GetDelegateInvokeMethod` returns null either way.
            if (const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(current)) {
                // The temporary `ITypePtr` dies but the managed `IType` is owned by the
                // `ParameterizedType`'s `typeArgs_` reachable through the input `t`, so the
                // raw pointer outlives the call (the D516 ownership reasoning).
                if (ITypePtr arg = pt->GetTypeArgument(0))
                    current = arg.get();
            }
        }
    }
    // C# `return t.GetDelegateInvokeMethod();` -- the TypeSystemExtensions free function
    // (D533): the Kind==Delegate guard, then the first `Invoke`-named method (null when the
    // type is not a delegate or has no `Invoke`).
    return GetDelegateInvokeMethod(*current);
}

// The C# `IType[] InputTypes(ResolveResult e, IType t)` (C# spec draft-v11 section 12.6.3.4).
std::vector<const IType*> InputTypes(const ILSpy::Decompiler::Semantics::ResolveResult& e,
                                     const IType& t)
{
    // C# `LambdaResolveResult lrr = e as LambdaResolveResult;`
    const LambdaResolveResult* lrr = dynamic_cast<const LambdaResolveResult*>(&e);
    // C# `if (lrr != null && lrr.IsImplicitlyTyped || e is MethodGroupResolveResult)` -- the
    // operator precedence is `&&` over `||`, so the implicitly-typed-lambda arm and the
    // method-group arm are the two alternatives.
    if ((lrr != nullptr && lrr->IsImplicitlyTyped())
        || dynamic_cast<const MethodGroupResolveResult*>(&e) != nullptr) {
        // C# `IMethod m = GetDelegateOrExpressionTreeSignature(t); if (m != null) { ... }`
        const IMethod* m = GetDelegateOrExpressionTreeSignature(t);
        if (m != nullptr) {
            // C# `IType[] inputTypes = new IType[m.Parameters.Count];
            // for (int i = 0; i < inputTypes.Length; i++)
            //     inputTypes[i] = m.Parameters[i].Type;`
            //
            // The parameter types are owned by the delegate-invoke method (owned by the
            // delegate type reachable through `t`), so the non-owning `const IType*`
            // snapshot outlives the call (the `GetMethods` raw-pointer convention).
            auto params = m->Parameters();
            std::vector<const IType*> inputTypes;
            inputTypes.reserve(params.size());
            for (const IParameter* p : params)
                inputTypes.push_back(&p->Type());
            return inputTypes;
        }
    }
    // C# `return Empty<IType>.Array;` -- the empty snapshot.
    return {};
}

// The C# `IType[] OutputTypes(ResolveResult e, IType t)` (C# spec draft-v11 section 12.6.3.5).
std::vector<const IType*> OutputTypes(const ILSpy::Decompiler::Semantics::ResolveResult& e,
                                      const IType& t)
{
    // C# `LambdaResolveResult lrr = e as LambdaResolveResult; if (lrr != null ||
    // e is MethodGroupResolveResult)` -- ANY lambda (implicitly OR explicitly typed; no
    // `IsImplicitlyTyped` gate here, unlike `InputTypes`) or a method group.
    const LambdaResolveResult* lrr = dynamic_cast<const LambdaResolveResult*>(&e);
    if (lrr != nullptr || dynamic_cast<const MethodGroupResolveResult*>(&e) != nullptr) {
        const IMethod* m = GetDelegateOrExpressionTreeSignature(t);
        if (m != nullptr) {
            // C# `return new[] { m.ReturnType };` -- the one-element snapshot (the return
            // type is owned by the delegate-invoke method, reachable through `t`).
            return { &m->ReturnType() };
        }
    }
    // C# `return Empty<IType>.Array;`
    return {};
}

// ===========================================================================
// The ContainsUnfixed region (TypeInference.cs lines 217-271 + 442-465).
// ===========================================================================

// The C# `void AddExactBound(IType type)` (TypeInference.cs lines 240-248).
void TP::AddExactBound(ITypePtr type)
{
    // C# `if (ExactBound == null) { ExactBound = type; }`
    if (!ExactBound) {
        ExactBound = std::move(type);
        return;
    }
    // C# `else if (!ExactBound.Equals(type)) MultipleDifferentExactBounds = true;` -- the
    // null-`type` degenerate shape (never passed by a real caller) makes the C#
    // `Equals(null)` return false and take the `true` branch, so the port guards the deref
    // and does the same (the D516 safe-faithful-fallback convention).
    if (!type || !ExactBound->Equals(*type))
        MultipleDifferentExactBounds = true;
}

// The `tp.LowerBounds.Add(U)` call site (TypeInference.cs line 750, the
// `MakeLowerBoundInference` region) -- the `HashSet<IType>.Add` idempotence under
// `IType.Equals` (structural equality for the concrete types; `KnownType` compares the code,
// `ParameterizedType` the generic + the type arguments). A `HashSet<IType>` would also
// store a null; no real caller passes one, and the dedup compare needs both sides non-null.
void TP::AddLowerBound(ITypePtr type)
{
    for (const ITypePtr& b : LowerBounds) {
        if (b && type && b->Equals(*type))
            return; // already present -- the HashSet.Add no-op
    }
    LowerBounds.push_back(std::move(type));
}

// The `tp.UpperBounds.Add(U)` call site (TypeInference.cs line 879, the
// `MakeUpperBoundInference` region) -- the same `HashSet<IType>.Add` idempotence.
void TP::AddUpperBound(ITypePtr type)
{
    for (const ITypePtr& b : UpperBounds) {
        if (b && type && b->Equals(*type))
            return;
    }
    UpperBounds.push_back(std::move(type));
}

// The C# `public override IType VisitTypeParameter(ITypeParameter type)` (TypeInference.cs
// lines 262-270).
ITypePtr OccursInVisitor::VisitTypeParameter(
    ILSpy::Decompiler::TypeSystem::ITypeParameter& type)
{
    // C# `int index = type.Index; if (index < tp.Length && tp[index].TypeParameter == type)
    //     Occurs[index] = true;`
    //
    // The `tp[index].TypeParameter == type` is the C# reference-equality check -- a raw
    // pointer comparison: a DIFFERENT `ITypeParameter` instance that happens to carry the
    // same index does NOT count as occurring (the real inference's type parameters are the
    // very instances the method declares). The `index >= 0` guard is the defensive addition
    // (a negative index would throw in the C#; the port skips it, the D516 convention).
    const int index = type.Index();
    if (index >= 0 && static_cast<std::size_t>(index) < tp_.size()
        && tp_[static_cast<std::size_t>(index)].TypeParameter == &type) {
        occurs_[static_cast<std::size_t>(index)] = true;
    }
    // C# `return base.VisitTypeParameter(type);` -- the base default recurses through the
    // children (a type parameter has none) and returns the type itself.
    return TypeVisitor::VisitTypeParameter(type);
}

// The C# `bool AnyTypeContainsUnfixedParameter(IEnumerable<IType> types)` (TypeInference.cs
// line 453).
bool AnyTypeContainsUnfixedParameter(const std::vector<TP>& typeParameters,
                                     const std::vector<const IType*>& types)
{
    // C# `OccursInVisitor o = new OccursInVisitor(this); foreach (var type in types) {
    //     type.AcceptVisitor(o); }` -- `AcceptVisitor` is non-const (D406), so the non-owning
    // `const IType*` snapshots take the `const_cast` (the D517 convention; the underlying
    // type-system objects are mutable). A null entry would NRE in the C#; the port skips it
    // (the D516 degenerate-shape fallback).
    OccursInVisitor o(typeParameters);
    for (const IType* type : types) {
        if (type == nullptr)
            continue;
        const_cast<IType*>(type)->AcceptVisitor(o);
    }
    // C# `for (int i = 0; i < typeParameters.Length; i++) {
    //     if (!typeParameters[i].IsFixed && o.Occurs[i]) return true; }
    // return false;`
    for (std::size_t i = 0; i < typeParameters.size(); i++) {
        if (!typeParameters[i].IsFixed() && o.Occurs()[i])
            return true;
    }
    return false;
}

// The C# `bool InputTypesContainsUnfixed(ResolveResult argument, IType parameterType)`
// (TypeInference.cs line 442).
bool InputTypesContainsUnfixed(const std::vector<TP>& typeParameters,
                               const ILSpy::Decompiler::Semantics::ResolveResult& argument,
                               const IType& parameterType)
{
    // C# `return AnyTypeContainsUnfixedParameter(InputTypes(argument, parameterType));`
    return AnyTypeContainsUnfixedParameter(typeParameters, InputTypes(argument, parameterType));
}

// The C# `bool OutputTypeContainsUnfixed(ResolveResult argument, IType parameterType)`
// (TypeInference.cs line 447).
bool OutputTypeContainsUnfixed(const std::vector<TP>& typeParameters,
                              const ILSpy::Decompiler::Semantics::ResolveResult& argument,
                              const IType& parameterType)
{
    // C# `return AnyTypeContainsUnfixedParameter(OutputTypes(argument, parameterType));`
    return AnyTypeContainsUnfixedParameter(typeParameters, OutputTypes(argument, parameterType));
}

// ===========================================================================
// The DependsOn region (TypeInference.cs lines 468-521).
// ===========================================================================

// The C# `void CalculateDependencyMatrix()` (TypeInference.cs lines 471-510) -- the
// occurrence accumulation over the arguments followed by the Warshall transitive closure.
std::vector<std::vector<bool>> CalculateDependencyMatrix(
    const std::vector<TP>& typeParameters,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<ITypePtr>& parameterTypes)
{
    using ILSpy::Decompiler::Semantics::ResolveResult;
    // C# `int n = typeParameters.Length; dependencyMatrix = new bool[n, n];` -- the n x n
    // false-initialized matrix (a row vector per row, the C# 2-D `bool[,]` array).
    const std::size_t n = typeParameters.size();
    std::vector<std::vector<bool>> dependencyMatrix(n, std::vector<bool>(n, false));
    // C# `for (int k = 0; k < arguments.Length; k++)` -- the `InferTypeArguments` ctor sized
    // the instance `arguments`/`parameterTypes` arrays to the common min of the caller's
    // lists, so `arguments.Length` IS that min here; the lift threads the caller's vectors
    // and takes the min itself (the faithful bound). A null entry was rejected by the C# ctor
    // (`ArgumentNullException`); the port skips one (the D516 degenerate-shape convention).
    const std::size_t argumentCount = std::min(arguments.size(), parameterTypes.size());
    for (std::size_t k = 0; k < argumentCount; k++)
    {
        // C# `OccursInVisitor input = new OccursInVisitor(this);
        //      OccursInVisitor output = new OccursInVisitor(this);` -- a FRESH pair of
        // occurrence recorders per argument (the C# ctor reads the instance
        // `typeParameters`; the lift threads the `TP` vector, the established convention).
        OccursInVisitor input(typeParameters);
        OccursInVisitor output(typeParameters);
        // C# `foreach (var type in InputTypes(arguments[k], parameterTypes[k]))
        //          type.AcceptVisitor(input);` -- the non-owning `const IType*` snapshots
        // take the `const_cast` for the non-const `AcceptVisitor` (D406; the D517 convention;
        // a null entry would NRE in the C# and is skipped, the D516 convention).
        if (arguments[k] && parameterTypes[k]) {
            for (const IType* type : InputTypes(*arguments[k], *parameterTypes[k])) {
                if (type != nullptr)
                    const_cast<IType*>(type)->AcceptVisitor(input);
            }
            // C# `foreach (var type in OutputTypes(arguments[k], parameterTypes[k]))
            //          type.AcceptVisitor(output);`
            for (const IType* type : OutputTypes(*arguments[k], *parameterTypes[k])) {
                if (type != nullptr)
                    const_cast<IType*>(type)->AcceptVisitor(output);
            }
        }
        // C# `for (int i = 0; i < n; i++) { for (int j = 0; j < n; j++) {
        //          dependencyMatrix[i, j] |= input.Occurs[j] && output.Occurs[i]; } }` --
        // "`Xi` depends on `Xj`": the argument's INPUT types contain `Xj` and its OUTPUT
        // types contain `Xi` (the spec's directly-depends-on relation; only the implicitly-
        // typed-lambda / method-group delegate signatures contribute input/output types, so
        // a plain expression argument never produces a dependence however much its formal
        // parameter type mentions the type parameters).
        for (std::size_t i = 0; i < n; i++)
        {
            for (std::size_t j = 0; j < n; j++)
            {
                if (input.Occurs()[j] && output.Occurs()[i])
                    dependencyMatrix[i][j] = true;
            }
        }
    }
    // C# "calculate transitive closure using Warshall's algorithm:" -- dependence composes
    // (`Xi` depends on `Xj` and `Xj` depends on `Xk` implies `Xi` depends on `Xk`); the
    // in-place update inside the loop is what makes it the closure (later reads see the
    // earlier writes).
    for (std::size_t i = 0; i < n; i++)
    {
        for (std::size_t j = 0; j < n; j++)
        {
            if (dependencyMatrix[i][j])
            {
                for (std::size_t k = 0; k < n; k++)
                {
                    if (dependencyMatrix[j][k])
                        dependencyMatrix[i][k] = true;
                }
            }
        }
    }
    return dependencyMatrix;
}

// The C# `bool DependsOn(TP x, TP y)` (TypeInference.cs lines 512-518) -- the matrix lookup
// ("x depends on y"). The C# lazy `if (dependencyMatrix == null) CalculateDependencyMatrix();`
// memoization becomes the compute-once-then-thread convention (see the header).
bool DependsOn(const std::vector<std::vector<bool>>& dependencyMatrix, const TP& x, const TP& y)
{
    // C# `return dependencyMatrix[x.TypeParameter.Index, y.TypeParameter.Index];` -- the
    // bounds guard is the defensive addition (the C# would throw
    // `IndexOutOfRangeException` on an out-of-range index; `InferTypeArguments` guarantees
    // `typeParameters[i].Index == i`, so the real indexes are always in bounds -- the D516
    // convention).
    const int xi = x.TypeParameter->Index();
    const int yi = y.TypeParameter->Index();
    if (xi < 0 || yi < 0
        || static_cast<std::size_t>(xi) >= dependencyMatrix.size()
        || static_cast<std::size_t>(yi) >= dependencyMatrix[static_cast<std::size_t>(xi)].size())
        return false;
    return dependencyMatrix[static_cast<std::size_t>(xi)][static_cast<std::size_t>(yi)];
}


// ===========================================================================
// The MakeInference region (TypeInference.cs lines 613-962 + 717-729) -- the
// bound-inference core. See TypeInferenceHelpers.hpp for the region overview.
// ===========================================================================

namespace {

// The C# `object.Equals(a, b)` over two `IType`s (the `object.Equals(pU.GenericType,
// pV.GenericType)` comparisons of the parameterized arms) -- reference equality OR both
// operands non-null and `a.Equals(b)`: `object.Equals` returns true when the two references
// are the same (including the degenerate both-null shape) and delegates to the first
// operand's `Equals` for two non-null operands (exactly one null yields false). The `Equals`
// is the `IType.Equals` -> `StructuralEquals` dispatch (value-based for the concrete leaves,
// identity for the test definitions).
bool ObjectEqualsType(const IType* a, const IType* b)
{
    if (a == b)
        return true;
    return a != nullptr && b != nullptr && a->Equals(*b);
}

// A `ParameterizedType`'s first type argument (the C# `TypeArguments[0]` of the span arms).
// The port's `GetTypeArgument` has no bounds check (`typeArgs_[index]`), so the
// empty-arguments guard comes first; the degenerate shape (a `Span<T>`-shaped parameterized
// type with no type arguments -- impossible for a faithfully-constructed type) would throw
// `IndexOutOfRangeException` in the C#, and the null return lets the arm skip its recursion
// while keeping the `return` (the D516 safe-faithful-fallback convention). The managed
// `IType` is owned by the parameterized type's `typeArgs_`, so the raw pointer outlives the
// call.
IType* FirstTypeArg(const ParameterizedType& pt)
{
    if (pt.TypeArguments().empty())
        return nullptr;
    return pt.GetTypeArgument(0).get();
}

// The shared nullability-strip preamble of the three bound-inference workers:
// C# `if (U.Nullability == V.Nullability) { U = U.WithoutNullability(); V =
// V.WithoutNullability(); }` -- when both sides carry the SAME annotation, erase it and
// continue with the un-annotated types (the annotation never takes part in the inference
// shapes themselves). The C# rebinds the two local parameters; the port keeps rebindable
// `IType*` locals (a C++ reference cannot be rebound). `WithoutNullability` in turn calls
// the NON-CONST `ChangeNullability` (which may return `shared_from_this()`, D406), which is
// why the workers take `IType&` non-const and why the types must be shared-managed; the
// returned handle differs only when an annotation was actually present, so the identity of
// an un-annotated pair is preserved through the strip.
void StripMatchingNullability(IType*& u, IType*& v)
{
    if (u->Nullability() == v->Nullability()) {
        if (ITypePtr stripped = WithoutNullability(*u))
            u = stripped.get();
        if (ITypePtr stripped = WithoutNullability(*v))
            v = stripped.get();
    }
}

} // namespace

// The C# `TP GetTPForType(IType v)` (TypeInference.cs lines 717-729).
TP* GetTPForType(std::vector<TP>& typeParameters, const IType& v)
{
    // C# `if (v is NullabilityAnnotatedTypeParameter natp) { v = natp.OriginalTypeParameter; }`
    // -- the annotated wrapper delegates to the wrapped parameter (the annotation does not
    // change the parameter's identity). A degenerate null `OriginalTypeParameter` leaves
    // `current` null, and the `is ITypeParameter` test below then fails (matching the C#
    // `null is ITypeParameter` -> false).
    const IType* current = &v;
    if (const NullabilityAnnotatedTypeParameter* natp =
            dynamic_cast<const NullabilityAnnotatedTypeParameter*>(current)) {
        current = natp->OriginalTypeParameter().get();
    }
    // C# `if (v is ITypeParameter p) { int index = p.Index;
    //     if (index < typeParameters.Length && typeParameters[index].TypeParameter == p)
    //         return typeParameters[index]; } return null;`
    //
    // The `tp[index].TypeParameter == p` is the C# reference-equality check -- a raw
    // pointer comparison (the OccursInVisitor precedent): a DIFFERENT parameter instance
    // carrying the same index does not resolve. The `index >= 0` half of the bounds guard
    // is the defensive addition (the C# `index < typeParameters.Length` admits a negative
    // index and then throws `IndexOutOfRangeException`; the port skips it, the D516
    // convention).
    if (const ILSpy::Decompiler::TypeSystem::ITypeParameter* p =
            dynamic_cast<const ILSpy::Decompiler::TypeSystem::ITypeParameter*>(current)) {
        const int index = p->Index();
        if (index >= 0 && static_cast<std::size_t>(index) < typeParameters.size()
            && typeParameters[static_cast<std::size_t>(index)].TypeParameter == p) {
            return &typeParameters[static_cast<std::size_t>(index)];
        }
    }
    // C# `return null;`
    return nullptr;
}

// The C# `void MakeExactInference(IType U, IType V)` (TypeInference.cs lines 635-728, C# 4.0
// spec section 7.5.2.8 "Exact inferences").
void MakeExactInference(const ICompilation& compilation, std::vector<TP>& typeParameters,
                        IType& u, IType& v)
{
    // The nullability strip (see the file-local helper above).
    IType* U = &u;
    IType* V = &v;
    StripMatchingNullability(U, V);

    // C# `TP tp = GetTPForType(V); if (tp != null && tp.IsFixed == false) { tp.AddExactBound(U);
    // return; }` -- an unfixed V-side type parameter takes U as its EXACT bound. The
    // `shared_from_this()` obtains the owning handle the `TP` bound containers store (the
    // D529 convention; the types must be shared-managed).
    if (TP* tp = GetTPForType(typeParameters, *V)) {
        if (!tp->IsFixed()) {
            tp->AddExactBound(U->shared_from_this());
            return;
        }
    }
    // C# `ByReferenceType brU = U as ByReferenceType; ByReferenceType brV = V as ByReferenceType;
    // if (brU != null && brV != null) { MakeExactInference(brU.ElementType, brV.ElementType);
    // return; }` -- two by-reference shapes match exactly on their elements (a degenerate
    // null element would NRE in the C#; the port skips the recursion but stays in the arm,
    // the D516 convention).
    const ByReferenceType* brU = dynamic_cast<const ByReferenceType*>(U);
    const ByReferenceType* brV = dynamic_cast<const ByReferenceType*>(V);
    if (brU != nullptr && brV != nullptr) {
        if (brU->Element() && brV->Element())
            MakeExactInference(compilation, typeParameters, *brU->Element(), *brV->Element());
        return;
    }
    // C# `U = U.TupleUnderlyingTypeOrSelf(); V = V.TupleUnderlyingTypeOrSelf();` -- BOTH
    // sides are tuple-unwrapped here (unlike the lower/upper-bound workers, which unwrap
    // only one side).
    if (ITypePtr unwrapped = TupleUnderlyingTypeOrSelf(*U))
        U = unwrapped.get();
    if (ITypePtr unwrapped = TupleUnderlyingTypeOrSelf(*V))
        V = unwrapped.get();
    // C# `switch ((U, V)) { ... }` -- the array and first-class-span pattern arms, in the
    // C# case order. The `Dimensions` property is the port's `ArrayType::Rank()` (the
    // number of dimensions, D538); the span arms are all gated on the
    // `FirstClassSpanTypes` option (the D538 HasFlag convention -- the `&` parenthesized
    // because C++ `enum class` `&` binds looser than `==`); a pair that matches no arm
    // falls through the whole switch to the parameterized/pointer checks below (so e.g.
    // an `(int[], Span<int>)` pair WITHOUT the flag makes no inference at all).
    const ArrayType* arrU = dynamic_cast<const ArrayType*>(U);
    const ArrayType* arrV = dynamic_cast<const ArrayType*>(V);
    const ParameterizedType* ptU = dynamic_cast<const ParameterizedType*>(U);
    const ParameterizedType* ptV = dynamic_cast<const ParameterizedType*>(V);
    const TypeSystemOptions options = compilation.TypeSystemOptions();
    const bool spanTypes =
        (options & TypeSystemOptions::FirstClassSpanTypes) == TypeSystemOptions::FirstClassSpanTypes;
    // C# `case (ArrayType arrU, ArrayType arrV) when arrU.Dimensions == arrV.Dimensions):`
    if (arrU != nullptr && arrV != nullptr && arrU->Rank() == arrV->Rank()) {
        if (arrU->Element() && arrV->Element())
            MakeExactInference(compilation, typeParameters, *arrU->Element(), *arrV->Element());
        return;
    }
    // C# `case (ArrayType arrU, ParameterizedType spanV) when ...spanV.IsKnownType(SpanOfT):`
    if (arrU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptV, KnownTypeCode::SpanOfT)) {
        if (arrU->Element()) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeExactInference(compilation, typeParameters, *arrU->Element(), *argV);
        }
        return;
    }
    // C# `case (ParameterizedType spanU, ParameterizedType spanV) when ...SpanOfT...SpanOfT:`
    if (ptU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptU, KnownTypeCode::SpanOfT) && IsKnownType(*ptV, KnownTypeCode::SpanOfT)) {
        if (IType* argU = FirstTypeArg(*ptU)) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeExactInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `case (ArrayType arrU, ParameterizedType rosV) when ...ReadOnlySpanOfT:`
    if (arrU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptV, KnownTypeCode::ReadOnlySpanOfT)) {
        if (arrU->Element()) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeExactInference(compilation, typeParameters, *arrU->Element(), *argV);
        }
        return;
    }
    // C# `case (ParameterizedType spanU, ParameterizedType rosV) when ...SpanOfT...
    // ReadOnlySpanOfT:`
    if (ptU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptU, KnownTypeCode::SpanOfT)
        && IsKnownType(*ptV, KnownTypeCode::ReadOnlySpanOfT)) {
        if (IType* argU = FirstTypeArg(*ptU)) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeExactInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `case (ParameterizedType rosU, ParameterizedType rosV) when ...ReadOnlySpanOfT...
    // ReadOnlySpanOfT:`
    if (ptU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptU, KnownTypeCode::ReadOnlySpanOfT)
        && IsKnownType(*ptV, KnownTypeCode::ReadOnlySpanOfT)) {
        if (IType* argU = FirstTypeArg(*ptU)) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeExactInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `if (U is ParameterizedType pU && V is ParameterizedType pV
    //     && object.Equals(pU.GenericType, pV.GenericType)
    //     && pU.TypeParameterCount == pV.TypeParameterCount) { for (...) MakeExactInference(
    //     pU.GetTypeArgument(i), pV.GetTypeArgument(i)); return; }` -- two parameterizations
    // of the SAME generic type with the SAME arity match argument-for-argument (the
    // `object.Equals` is the file-local reference-or-structural helper above; a degenerate
    // null type argument would NRE in the C# and is skipped, the D516 convention).
    if (ptU != nullptr && ptV != nullptr
        && ObjectEqualsType(ptU->GenericType().get(), ptV->GenericType().get())
        && ptU->TypeParameterCount() == ptV->TypeParameterCount()) {
        for (int i = 0; i < ptU->TypeParameterCount(); i++) {
            ITypePtr argU = ptU->GetTypeArgument(i);
            ITypePtr argV = ptV->GetTypeArgument(i);
            if (argU && argV)
                MakeExactInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `if (U is PointerType ptrU && V is PointerType ptrV) {
    //     MakeExactInference(ptrU.ElementType, ptrV.ElementType); return; }`
    const PointerType* ptrU = dynamic_cast<const PointerType*>(U);
    const PointerType* ptrV = dynamic_cast<const PointerType*>(V);
    if (ptrU != nullptr && ptrV != nullptr) {
        if (ptrU->Element() && ptrV->Element())
            MakeExactInference(compilation, typeParameters, *ptrU->Element(), *ptrV->Element());
        return;
    }
    // C# `if (U is FunctionPointerType fnPtrU && V is FunctionPointerType fnPtrV) {
    //     MakeExactInference(fnPtrU.ReturnType, fnPtrV.ReturnType);
    //     foreach (var (ptU, ptV) in fnPtrU.ParameterTypes.Zip(fnPtrV.ParameterTypes))
    //         MakeExactInference(ptU, ptV); return; }` -- the `Zip` stops at the shorter
    // list (the `std::min` loop bound, the D46 convention).
    const FunctionPointerType* fnU = dynamic_cast<const FunctionPointerType*>(U);
    const FunctionPointerType* fnV = dynamic_cast<const FunctionPointerType*>(V);
    if (fnU != nullptr && fnV != nullptr) {
        if (fnU->ReturnType() && fnV->ReturnType())
            MakeExactInference(compilation, typeParameters, *fnU->ReturnType(), *fnV->ReturnType());
        const std::size_t n = std::min(fnU->ParameterTypes().size(),
                                       fnV->ParameterTypes().size());
        for (std::size_t i = 0; i < n; i++) {
            if (fnU->ParameterTypes()[i] && fnV->ParameterTypes()[i])
                MakeExactInference(compilation, typeParameters, *fnU->ParameterTypes()[i],
                                   *fnV->ParameterTypes()[i]);
        }
        return;
    }
}

// The C# `void MakeLowerBoundInference(IType U, IType V)` (TypeInference.cs lines 736-857,
// spec draft-v11 section 12.6.3.11 "Lower-bound inferences").
void MakeLowerBoundInference(const ICompilation& compilation, std::vector<TP>& typeParameters,
                             IType& u, IType& v)
{
    // The nullability strip (the shared preamble).
    IType* U = &u;
    IType* V = &v;
    StripMatchingNullability(U, V);

    // C# `TP tp = GetTPForType(V); if (tp != null && tp.IsFixed == false) {
    // tp.LowerBounds.Add(U); return; }` -- an unfixed V-side type parameter takes U as a
    // LOWER bound (the C# `HashSet<IType>.Add` idempotence lives in `TP::AddLowerBound`).
    if (TP* tp = GetTPForType(typeParameters, *V)) {
        if (!tp->IsFixed()) {
            tp->AddLowerBound(U->shared_from_this());
            return;
        }
    }
    // C# `if (NullableType.IsNullable(U) && NullableType.IsNullable(V)) {
    //     MakeLowerBoundInference(NullableType.GetUnderlyingType(U),
    //                              NullableType.GetUnderlyingType(V)); return; }` -- the
    // nullable-covariance recursion: two `Nullable<...>` wrappers reduce to their
    // underlying types. The non-const `GetUnderlyingType` overload (the D515 pair) hands the
    // mutable underlying types straight to the recursion.
    if (IsNullable(*U) && IsNullable(*V)) {
        MakeLowerBoundInference(compilation, typeParameters, GetUnderlyingType(*U),
                                GetUnderlyingType(*V));
        return;
    }
    // C# by-reference arm -- NOTE: it recurses EXACT (two by-reference shapes match
    // exactly on their elements), not lower-bound.
    const ByReferenceType* brU = dynamic_cast<const ByReferenceType*>(U);
    const ByReferenceType* brV = dynamic_cast<const ByReferenceType*>(V);
    if (brU != nullptr && brV != nullptr) {
        if (brU->Element() && brV->Element())
            MakeExactInference(compilation, typeParameters, *brU->Element(), *brV->Element());
        return;
    }
    // C# `V = V.TupleUnderlyingTypeOrSelf();` -- ONLY the V side is tuple-unwrapped here
    // (the switch's U-side patterns read the ORIGINAL U; the asymmetry is the C# source's).
    if (ITypePtr unwrapped = TupleUnderlyingTypeOrSelf(*V))
        V = unwrapped.get();
    // C# `switch ((U, V)) { ... }` -- the array / span / array-interface arms in the C#
    // case order. The span arms gate on `FirstClassSpanTypes`; the array-interface arm (the
    // LAST case) does NOT -- so an `(U[], IEnumerable<U>)` pair fires it whether or not the
    // span option is set (a `Span<T>`/`ReadOnlySpan<T>` never matches it: different known
    // type codes than the five array-interface ones).
    const ArrayType* arrU = dynamic_cast<const ArrayType*>(U);
    const ArrayType* arrV = dynamic_cast<const ArrayType*>(V);
    const ParameterizedType* ptU = dynamic_cast<const ParameterizedType*>(U);
    const ParameterizedType* ptV = dynamic_cast<const ParameterizedType*>(V);
    const TypeSystemOptions options = compilation.TypeSystemOptions();
    const bool spanTypes =
        (options & TypeSystemOptions::FirstClassSpanTypes) == TypeSystemOptions::FirstClassSpanTypes;
    // C# `case (ArrayType arrU, ArrayType arrV) when arrU.Dimensions == arrV.Dimensions):`
    if (arrU != nullptr && arrV != nullptr && arrU->Rank() == arrV->Rank()) {
        if (arrU->Element() && arrV->Element())
            MakeLowerBoundInference(compilation, typeParameters, *arrU->Element(),
                                    *arrV->Element());
        return;
    }
    // C# `case (ArrayType arrU, ParameterizedType spanV) when ...SpanOfT:`
    if (arrU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptV, KnownTypeCode::SpanOfT)) {
        if (arrU->Element()) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeLowerBoundInference(compilation, typeParameters, *arrU->Element(), *argV);
        }
        return;
    }
    // C# `case (ParameterizedType spanU, ParameterizedType spanV) when ...SpanOfT...SpanOfT:`
    if (ptU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptU, KnownTypeCode::SpanOfT) && IsKnownType(*ptV, KnownTypeCode::SpanOfT)) {
        if (IType* argU = FirstTypeArg(*ptU)) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeLowerBoundInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `case (ArrayType arrU, ParameterizedType rosV) when ...ReadOnlySpanOfT:`
    if (arrU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptV, KnownTypeCode::ReadOnlySpanOfT)) {
        if (arrU->Element()) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeLowerBoundInference(compilation, typeParameters, *arrU->Element(), *argV);
        }
        return;
    }
    // C# `case (ParameterizedType spanU, ParameterizedType rosV) when ...SpanOfT...
    // ReadOnlySpanOfT:`
    if (ptU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptU, KnownTypeCode::SpanOfT)
        && IsKnownType(*ptV, KnownTypeCode::ReadOnlySpanOfT)) {
        if (IType* argU = FirstTypeArg(*ptU)) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeLowerBoundInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `case (ParameterizedType rosU, ParameterizedType rosV) when ...ReadOnlySpanOfT...
    // ReadOnlySpanOfT:`
    if (ptU != nullptr && ptV != nullptr && spanTypes
        && IsKnownType(*ptU, KnownTypeCode::ReadOnlySpanOfT)
        && IsKnownType(*ptV, KnownTypeCode::ReadOnlySpanOfT)) {
        if (IType* argU = FirstTypeArg(*ptU)) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeLowerBoundInference(compilation, typeParameters, *argU, *argV);
        }
        return;
    }
    // C# `case (ArrayType arrU, ParameterizedType arrIntfV) when arrIntfV.IsArrayInterfaceType()
    // && arrU.Dimensions == 1):` -- a one-dimensional array lower-bounds the array interface's
    // element type (an `IEnumerable<T>`/`IList<T>`/... parameter).
    if (arrU != nullptr && ptV != nullptr && IsArrayInterfaceType(*ptV) && arrU->Rank() == 1) {
        if (arrU->Element()) {
            if (IType* argV = FirstTypeArg(*ptV))
                MakeLowerBoundInference(compilation, typeParameters, *arrU->Element(), *argV);
        }
        return;
    }
    // C# `if (V is ParameterizedType pV) { ... }` -- the unique-base-type variance walk: find
    // the UNIQUE `ParameterizedType` among U's base types that shares V's generic type and
    // arity (a second match aborts the whole inference -- "it's not unique"), then reduce
    // argument-by-argument following the generic's VARIANCE: covariant arguments recurse
    // LOWER-bound, contravariant UPPER-bound, invariant EXACT. The variance comes from
    // `pV.TypeParameters[i]` -- the port's `ParameterizedType::TypeParameters()` override
    // (the generic definition's declared parameters); an out-of-range variance index (a
    // degenerate arity-mismatched stub) falls to the invariant exact recursion (the D516
    // convention).
    if (ptV != nullptr) {
        const ParameterizedType* uniqueBaseType = nullptr;
        for (const IType* baseU : GetAllBaseTypes(*U)) {
            if (baseU == nullptr)
                continue; // the D516 degenerate-shape skip
            // C# `ParameterizedType pU = baseU.TupleUnderlyingTypeOrSelf() as ParameterizedType;`
            // -- the `const_cast` feeds the non-const `TupleUnderlyingTypeOrSelf` (the
            // underlying type-system objects are mutable; the accessor's const is the
            // contract -- the D517 convention).
            const IType* unwrappedBase = baseU;
            if (ITypePtr unwrapped = TupleUnderlyingTypeOrSelf(*const_cast<IType*>(baseU)))
                unwrappedBase = unwrapped.get();
            const ParameterizedType* pU = dynamic_cast<const ParameterizedType*>(unwrappedBase);
            if (pU != nullptr
                && ObjectEqualsType(pU->GenericType().get(), ptV->GenericType().get())
                && pU->TypeParameterCount() == ptV->TypeParameterCount()) {
                if (uniqueBaseType == nullptr)
                    uniqueBaseType = pU;
                else
                    return; // cannot make an inference because it's not unique
            }
        }
        if (uniqueBaseType != nullptr) {
            // C# `ITypeParameter Xi = pV.TypeParameters[i]` -- read the list once (the port
            // accessor returns the vector by value).
            const std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> xiList =
                ptV->TypeParameters();
            for (int i = 0; i < uniqueBaseType->TypeParameterCount(); i++) {
                // C# `IType Ui = uniqueBaseType.GetTypeArgument(i); IType Vi =
                // pV.GetTypeArgument(i);`
                ITypePtr argU = uniqueBaseType->GetTypeArgument(i);
                ITypePtr argV = ptV->GetTypeArgument(i);
                if (!argU || !argV)
                    continue; // the D516 degenerate-shape skip
                // C# `if (Ui.IsReferenceType == true) { ... } else { MakeExactInference(Ui, Vi); }`
                // -- the `bool? == true` is true only for a DEFINITE true (an indeterminate
                // type reports `std::nullopt` and takes the exact arm).
                const std::optional<bool> argURef = argU->IsReferenceType();
                const ILSpy::Decompiler::TypeSystem::ITypeParameter* xi =
                    static_cast<std::size_t>(i) < xiList.size()
                        ? xiList[static_cast<std::size_t>(i)]
                        : nullptr;
                if (argURef.has_value() && *argURef == true && xi != nullptr) {
                    // C# `switch (Xi.Variance) { case Covariant: MakeLowerBoundInference(Ui, Vi);
                    // case Contravariant: MakeUpperBoundInference(Ui, Vi); default:
                    // MakeExactInference(Ui, Vi); }`
                    switch (xi->Variance()) {
                        case VarianceModifier::Covariant:
                            MakeLowerBoundInference(compilation, typeParameters, *argU, *argV);
                            break;
                        case VarianceModifier::Contravariant:
                            MakeUpperBoundInference(compilation, typeParameters, *argU, *argV);
                            break;
                        default: // invariant
                            MakeExactInference(compilation, typeParameters, *argU, *argV);
                            break;
                    }
                } else {
                    // not known to be a reference type (or no variance entry -- the degenerate
                    // stub): the invariant exact recursion.
                    MakeExactInference(compilation, typeParameters, *argU, *argV);
                }
            }
        }
        return;
    }
    // C# `if (U is PointerType ptrU && V is PointerType ptrV) { MakeExactInference(...);
    // return; }` -- pointer shapes match exactly.
    const PointerType* ptrU = dynamic_cast<const PointerType*>(U);
    const PointerType* ptrV = dynamic_cast<const PointerType*>(V);
    if (ptrU != nullptr && ptrV != nullptr) {
        if (ptrU->Element() && ptrV->Element())
            MakeExactInference(compilation, typeParameters, *ptrU->Element(), *ptrV->Element());
        return;
    }
    // C# `if (U is FunctionPointerType fnPtrU && V is FunctionPointerType fnPtrV) {
    //     MakeLowerBoundInference(fnPtrU.ReturnType, fnPtrV.ReturnType);
    //     foreach (var (ptU, ptV) in fnPtrU.ParameterTypes.Zip(fnPtrV.ParameterTypes))
    //         MakeUpperBoundInference(ptU, ptV); return; }` -- the function-pointer arm
    // SWAPS: the return recurses lower-bound, the parameters UPPER-bound (the mirror of
    // the upper-bound worker below).
    const FunctionPointerType* fnU = dynamic_cast<const FunctionPointerType*>(U);
    const FunctionPointerType* fnV = dynamic_cast<const FunctionPointerType*>(V);
    if (fnU != nullptr && fnV != nullptr) {
        if (fnU->ReturnType() && fnV->ReturnType())
            MakeLowerBoundInference(compilation, typeParameters, *fnU->ReturnType(),
                                     *fnV->ReturnType());
        const std::size_t n = std::min(fnU->ParameterTypes().size(),
                                       fnV->ParameterTypes().size());
        for (std::size_t i = 0; i < n; i++) {
            if (fnU->ParameterTypes()[i] && fnV->ParameterTypes()[i])
                MakeUpperBoundInference(compilation, typeParameters, *fnU->ParameterTypes()[i],
                                        *fnV->ParameterTypes()[i]);
        }
        return;
    }
}

// The C# `void MakeUpperBoundInference(IType U, IType V)` (TypeInference.cs lines 865-961,
// C# 4.0 spec section 7.5.2.10 "Upper-bound inferences").
void MakeUpperBoundInference(const ICompilation& compilation, std::vector<TP>& typeParameters,
                             IType& u, IType& v)
{
    // The nullability strip (the shared preamble).
    IType* U = &u;
    IType* V = &v;
    StripMatchingNullability(U, V);

    // C# `TP tp = GetTPForType(V); if (tp != null && tp.IsFixed == false) {
    // tp.UpperBounds.Add(U); return; }` -- an unfixed V-side type parameter takes U as an
    // UPPER bound.
    if (TP* tp = GetTPForType(typeParameters, *V)) {
        if (!tp->IsFixed()) {
            tp->AddUpperBound(U->shared_from_this());
            return;
        }
    }
    // C# `ArrayType arrU = U as ArrayType; ArrayType arrV = V as ArrayType;
    // ParameterizedType pU = U.TupleUnderlyingTypeOrSelf() as ParameterizedType;` -- NEITHER
    // side is rebound here (only the local `pU` reads through the tuple unwrap; `arrU` /
    // `arrV` read the ORIGINAL U / V).
    const ArrayType* arrU = dynamic_cast<const ArrayType*>(U);
    const ArrayType* arrV = dynamic_cast<const ArrayType*>(V);
    const ParameterizedType* pU = nullptr;
    if (ITypePtr unwrapped = TupleUnderlyingTypeOrSelf(*U))
        pU = dynamic_cast<const ParameterizedType*>(unwrapped.get());
    // C# `if (arrV != null && arrU != null && arrU.Dimensions == arrV.Dimensions) { ... }` --
    // two same-rank arrays match element-wise upper-bound.
    if (arrV != nullptr && arrU != nullptr && arrU->Rank() == arrV->Rank()) {
        if (arrU->Element() && arrV->Element())
            MakeUpperBoundInference(compilation, typeParameters, *arrU->Element(),
                                    *arrV->Element());
        return;
    }
    // C# `else if (arrV != null && pU.IsArrayInterfaceType() && arrV.Dimensions == 1) {
    // MakeUpperBoundInference(pU.GetTypeArgument(0), arrV.ElementType); return; }` -- an
    // array-interface U upper-bounds the V-side array's element: `IEnumerable<U>` is a
    // plausible upper bound for a `T[]` parameter's element. The C# extension call on a
    // null `pU` returns FALSE (the extension body's leading `type == null` check), so the
    // port's `pU != nullptr &&` guard is the faithful translation.
    if (arrV != nullptr && pU != nullptr && IsArrayInterfaceType(*pU) && arrV->Rank() == 1) {
        // `IsArrayInterfaceType` requires `TypeParameterCount == 1`, so the type-argument
        // index is in bounds; the element guards remain the D516 convention.
        if (pU->GetTypeArgument(0) && arrV->Element()) {
            ITypePtr argU = pU->GetTypeArgument(0);
            MakeUpperBoundInference(compilation, typeParameters, *argU, *arrV->Element());
        }
        return;
    }
    // C# `if (pU != null) { ... }` -- the unique-base-type variance walk (the mirror of the
    // lower-bound worker's): find the UNIQUE `ParameterizedType` among V's BASE types that
    // shares pU's generic type and arity, then reduce argument-by-argument -- the variance
    // comes from `pU.TypeParameters[i]` (the U side here, the V side in the lower-bound
    // worker), covariant recursing UPPER-bound, contravariant LOWER-bound, invariant exact.
    if (pU != nullptr) {
        const ParameterizedType* uniqueBaseType = nullptr;
        for (const IType* baseV : GetAllBaseTypes(*V)) {
            if (baseV == nullptr)
                continue; // the D516 degenerate-shape skip
            // C# `ParameterizedType pV = baseV.TupleUnderlyingTypeOrSelf() as ParameterizedType;`
            const IType* unwrappedBase = baseV;
            if (ITypePtr unwrapped = TupleUnderlyingTypeOrSelf(*const_cast<IType*>(baseV)))
                unwrappedBase = unwrapped.get();
            const ParameterizedType* pV = dynamic_cast<const ParameterizedType*>(unwrappedBase);
            if (pV != nullptr
                && ObjectEqualsType(pU->GenericType().get(), pV->GenericType().get())
                && pU->TypeParameterCount() == pV->TypeParameterCount()) {
                if (uniqueBaseType == nullptr)
                    uniqueBaseType = pV;
                else
                    return; // cannot make an inference because it's not unique
            }
        }
        if (uniqueBaseType != nullptr) {
            // C# `ITypeParameter Xi = pU.TypeParameters[i]` -- read the list once.
            const std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> xiList =
                pU->TypeParameters();
            for (int i = 0; i < uniqueBaseType->TypeParameterCount(); i++) {
                // C# `IType Ui = pU.GetTypeArgument(i); IType Vi =
                // uniqueBaseType.GetTypeArgument(i);`
                ITypePtr argU = pU->GetTypeArgument(i);
                ITypePtr argV = uniqueBaseType->GetTypeArgument(i);
                if (!argU || !argV)
                    continue; // the D516 degenerate-shape skip
                const std::optional<bool> argURef = argU->IsReferenceType();
                const ILSpy::Decompiler::TypeSystem::ITypeParameter* xi =
                    static_cast<std::size_t>(i) < xiList.size()
                        ? xiList[static_cast<std::size_t>(i)]
                        : nullptr;
                if (argURef.has_value() && *argURef == true && xi != nullptr) {
                    // C# `switch (Xi.Variance) { case Covariant: MakeUpperBoundInference(Ui, Vi);
                    // case Contravariant: MakeLowerBoundInference(Ui, Vi); default:
                    // MakeExactInference(Ui, Vi); }`
                    switch (xi->Variance()) {
                        case VarianceModifier::Covariant:
                            MakeUpperBoundInference(compilation, typeParameters, *argU, *argV);
                            break;
                        case VarianceModifier::Contravariant:
                            MakeLowerBoundInference(compilation, typeParameters, *argU, *argV);
                            break;
                        default: // invariant
                            MakeExactInference(compilation, typeParameters, *argU, *argV);
                            break;
                    }
                } else {
                    // not known to be a reference type (or no variance entry -- the degenerate
                    // stub): the invariant exact recursion.
                    MakeExactInference(compilation, typeParameters, *argU, *argV);
                }
            }
        }
        return;
    }
    // C# `if (U is PointerType ptrU && V is PointerType ptrV) { MakeExactInference(...);
    // return; }` -- pointer shapes match exactly.
    const PointerType* ptrU = dynamic_cast<const PointerType*>(U);
    const PointerType* ptrV = dynamic_cast<const PointerType*>(V);
    if (ptrU != nullptr && ptrV != nullptr) {
        if (ptrU->Element() && ptrV->Element())
            MakeExactInference(compilation, typeParameters, *ptrU->Element(), *ptrV->Element());
        return;
    }
    // C# `if (U is FunctionPointerType fnPtrU && V is FunctionPointerType fnPtrV) {
    //     MakeUpperBoundInference(fnPtrU.ReturnType, fnPtrV.ReturnType);
    //     foreach (var (ptU, ptV) in fnPtrU.ParameterTypes.Zip(fnPtrV.ParameterTypes))
    //         MakeLowerBoundInference(ptU, ptV); return; }` -- the opposite swap of the
    // lower-bound worker: the return recurses upper-bound, the parameters LOWER-bound.
    const FunctionPointerType* fnU = dynamic_cast<const FunctionPointerType*>(U);
    const FunctionPointerType* fnV = dynamic_cast<const FunctionPointerType*>(V);
    if (fnU != nullptr && fnV != nullptr) {
        if (fnU->ReturnType() && fnV->ReturnType())
            MakeUpperBoundInference(compilation, typeParameters, *fnU->ReturnType(),
                                    *fnV->ReturnType());
        const std::size_t n = std::min(fnU->ParameterTypes().size(),
                                       fnV->ParameterTypes().size());
        for (std::size_t i = 0; i < n; i++) {
            if (fnU->ParameterTypes()[i] && fnV->ParameterTypes()[i])
                MakeLowerBoundInference(compilation, typeParameters, *fnU->ParameterTypes()[i],
                                        *fnV->ParameterTypes()[i]);
        }
        return;
    }
}

// The C# `void MakeExplicitParameterTypeInference(LambdaResolveResult e, IType t)`
// (TypeInference.cs lines 614-627, spec draft-v11 section 12.6.3.9 "Explicit parameter type
// inferences").
void MakeExplicitParameterTypeInference(const ICompilation& compilation,
                                        std::vector<TP>& typeParameters,
                                        const LambdaResolveResult& e, const IType& t)
{
    // C# `if (e.IsImplicitlyTyped || !e.HasParameterList) return;` -- only an EXPLICITLY-typed
    // lambda with a parameter list contributes: its declared parameter types are the
    // exactly-matching side of the inference (the implicitly-typed lambda's parameter types
    // are what inference produces, so there is nothing to match).
    if (e.IsImplicitlyTyped() || !e.HasParameterList())
        return;
    // C# `IMethod m = GetDelegateOrExpressionTreeSignature(t); if (m == null) return;` -- the
    // delegate / expression-tree signature (the D533 landing in this file).
    const IMethod* m = GetDelegateOrExpressionTreeSignature(t);
    if (m == nullptr)
        return;
    // C# `for (int i = 0; i < e.Parameters.Count && i < m.Parameters.Count; i++)
    //     MakeExactInference(e.Parameters[i].Type, m.Parameters[i].Type);` -- zip to the
    // shorter list. The parameter-type accessors return `const IType&` while
    // `MakeExactInference` takes non-const `IType&` (the non-const `ChangeNullability`,
    // D406), so the `const_cast` is the established safe pattern (the type-system objects
    // are mutable; the accessor's const is the contract -- the D515/D517 convention). A
    // degenerate null parameter entry would NRE in the C# and is skipped (the D516
    // convention).
    const std::vector<const IParameter*> eParams = e.Parameters();
    const std::vector<const IParameter*> mParams = m->Parameters();
    const std::size_t n = std::min(eParams.size(), mParams.size());
    for (std::size_t i = 0; i < n; i++) {
        if (eParams[i] == nullptr || mParams[i] == nullptr)
            continue;
        MakeExactInference(compilation, typeParameters, const_cast<IType&>(eParams[i]->Type()),
                           const_cast<IType&>(mParams[i]->Type()));
    }
}

// The C# `static bool IsValidType(IType type)` (TypeInference.cs lines 314-317).
bool IsValidType(const IType& type)
{
    // C# `return type.Kind != TypeKind.Unknown && type.Kind != TypeKind.Null &&
    //         type.Kind != TypeKind.None;` -- the three null-object kinds an expression's
    // type must NOT be for the type to participate in the inference (the error
    // `UnknownType`, the `null` literal, and `NoType`).
    return type.Kind() != TypeKind::Unknown && type.Kind() != TypeKind::Null
        && type.Kind() != TypeKind::None;
}

// The C# `TypeParameterSubstitution GetSubstitutionForFixedTPs()` (TypeInference.cs lines
// 602-611).
TypeParameterSubstitution GetSubstitutionForFixedTPs(
    const std::vector<TP>& typeParameters,
    const std::optional<std::vector<ITypePtr>>& classTypeArguments)
{
    // C# `IType[] fixedTypes = new IType[typeParameters.Length];
    //     for (int i = 0; i < fixedTypes.Length; i++)
    //         fixedTypes[i] = typeParameters[i].FixedTo ?? SpecialType.UnknownType;` -- the
    // fixed types form the METHOD type-argument list of the substitution (indexed by the
    // type parameter's `Index`; the `InferTypeArguments` contract `typeParameters[i].Index
    // == i` makes position i the parameter's own entry). An unfixed parameter substitutes
    // to `SpecialType.UnknownType` (the C# `??` fallback), so a nested lambda's parameter
    // types carry the OUTER inference's fixed decisions and mark the still-unresolved
    // positions.
    std::vector<ITypePtr> fixedTypes;
    fixedTypes.reserve(typeParameters.size());
    for (const TP& tp : typeParameters) {
        fixedTypes.push_back(tp.FixedTo ? tp.FixedTo : UnknownType());
    }
    // C# `return new TypeParameterSubstitution(classTypeArguments, fixedTypes);` -- the C#
    // heap allocation realized as a by-value return (the `Compose` convention). The
    // `fixedTypes` list is always PRESENT (it substitutes every method type parameter of
    // the inference); an ABSENT `classTypeArguments` (the C# `null`) keeps the class type
    // parameters unmodified.
    return TypeParameterSubstitution(classTypeArguments, std::move(fixedTypes));
}

// The C# `void MakeOutputTypeInference(ResolveResult e, IType t)` (TypeInference.cs lines
// 522-600, C# 4.0 spec section 7.5.2.6 "Output type inferences").
void MakeOutputTypeInference(const ICompilation& compilation, std::vector<TP>& typeParameters,
                             const std::optional<std::vector<ITypePtr>>& classTypeArguments,
                             const ILSpy::Decompiler::Semantics::ResolveResult& e, IType& t)
{
    // C# `LambdaResolveResult lrr = e as LambdaResolveResult; if (lrr != null) {
    //     IMethod m = GetDelegateOrExpressionTreeSignature(t); if (m != null) { ... return; }
    // }` -- the lambda arm: a lower-bound inference from the lambda's INFERRED RETURN TYPE
    // to the delegate signature's return type. When the target is not a delegate (or an
    // expression tree over one), the lambda falls THROUGH to the plain-expression arm
    // below (its own type is `NoType`, which fails the `IsValidType` gate).
    const LambdaResolveResult* lrr = dynamic_cast<const LambdaResolveResult*>(&e);
    if (lrr != nullptr) {
        const IMethod* m = GetDelegateOrExpressionTreeSignature(t);
        if (m != nullptr) {
            ITypePtr inferredReturnType;
            if (lrr->IsImplicitlyTyped()) {
                // C# `if (m.Parameters.Count != lrr.Parameters.Count) return;` -- cannot
                // infer due to mismatched parameter lists.
                if (m->Parameters().size() != lrr->Parameters().size())
                    return;
                // C# `TypeParameterSubstitution substitution = GetSubstitutionForFixedTPs();
                //     IType[] inferredParameterTypes = new IType[m.Parameters.Count];
                //     for (...) inferredParameterTypes[i] =
                //         m.Parameters[i].Type.AcceptVisitor(substitution);` -- the delegate
                // signature's parameter types with the fixed-TP substitution applied, fed
                // to the lambda's return-type inference. The substitution is a non-const
                // `TypeVisitor` (`AcceptVisitor` takes a non-const reference, D406), so the
                // const `IParameter::Type()` accessor `const_cast`s (the D515/D517
                // convention -- the underlying type-system objects are mutable; the
                // accessor's const is the contract). A degenerate null parameter entry
                // would NRE in the C# and is skipped (the
                // `MakeExplicitParameterTypeInference` convention).
                TypeParameterSubstitution substitution =
                    GetSubstitutionForFixedTPs(typeParameters, classTypeArguments);
                std::vector<ITypePtr> inferredParameterTypes;
                inferredParameterTypes.reserve(m->Parameters().size());
                for (const IParameter* param : m->Parameters()) {
                    if (param == nullptr)
                        continue;
                    inferredParameterTypes.push_back(
                        const_cast<IType&>(param->Type()).AcceptVisitor(substitution));
                }
                inferredReturnType = lrr->GetInferredReturnType(inferredParameterTypes);
            } else {
                // C# `inferredReturnType = lrr.GetInferredReturnType(null);` -- the null
                // array ports to the empty vector (an explicitly-typed lambda already
                // knows its parameter types, so none are threaded).
                inferredReturnType = lrr->GetInferredReturnType({});
            }
            // C# `MakeLowerBoundInference(inferredReturnType, m.ReturnType); return;` --
            // the inferred return type lower-bounds the delegate return type. The null
            // guard is the safe faithful fallback (a real `GetInferredReturnType` never
            // returns null; the C# would NRE, which never occurs in practice).
            if (inferredReturnType) {
                MakeLowerBoundInference(compilation, typeParameters, *inferredReturnType,
                                        const_cast<IType&>(m->ReturnType()));
            }
            return;
        }
    }
    // C# `MethodGroupResolveResult mgrr = e as MethodGroupResolveResult; if (mgrr != null) {
    //     IMethod m = GetDelegateOrExpressionTreeSignature(t); if (m != null) { ... }
    //     return; }` -- the method-group arm: the synthetic delegate-signature arguments
    // (the parameter types with the fixed-TP substitution applied, a ref/in parameter
    // unwrapped to its element type) feed `mgrr.PerformOverloadResolution`, and the
    // resolved method's return type lower-bounds the delegate return type. DEFERRED:
    // `MethodGroupResolveResult.PerformOverloadResolution` -- the `OverloadResolution`
    // engine (`AddMethodLists` -> `AddCandidate` -> `CalculateCandidate` ->
    // `RunTypeInference`) it composes is now fully ported, so the arm is UNBLOCKED and lands
    // in a follow-up iteration. Until it lands, a method-group
    // argument makes NO output-type inference -- faithful to the C# whenever the overload
    // resolution finds no unambiguous applicable candidate (the
    // `or.FoundApplicableCandidate && or.BestCandidateAmbiguousWith == null` guard
    // failing). The unconditional `return` (inside the C# `mgrr` block, taken whether or
    // not the delegate signature resolves) keeps a method group from falling through to
    // the plain-expression arm below; its own type (`NoType`) would fail the `IsValidType`
    // gate anyway.
    if (dynamic_cast<const MethodGroupResolveResult*>(&e) != nullptr) {
        return;
    }
    // C# `if (IsValidType(e.Type)) MakeLowerBoundInference(e.Type, t);` -- the
    // plain-expression arm: a lower-bound inference from the expression's own type to the
    // parameter type. `ResolveResult::Type()` returns `const IType&` while
    // `MakeLowerBoundInference` takes non-const `IType&` (the non-const
    // `ChangeNullability`, D406), so the `const_cast` is the established safe pattern (the
    // D515/D517 convention).
    if (IsValidType(e.Type()))
        MakeLowerBoundInference(compilation, typeParameters,
                                const_cast<IType&>(e.Type()), t);
}

// ===========================================================================
// The Fixing / FindTypeInBounds / GetBestCommonType regions (TypeInference.cs lines
// 964-1186).
// ===========================================================================

// The C# `static IType GetFirstTypePreferNonInterfaces(IReadOnlyList<IType> result)`
// (TypeInference.cs lines 1055-1058).
ITypePtr GetFirstTypePreferNonInterfaces(const std::vector<ITypePtr>& result)
{
    // C# `result.FirstOrDefault(c => c.Kind != TypeKind.Interface) ??
    //     result.FirstOrDefault() ?? SpecialType.UnknownType;` -- the first NON-INTERFACE
    // candidate wins over an earlier interface candidate; an all-interface (or all-null)
    // list takes the first entry; an empty list takes the `UnknownType` null object. A
    // null entry cannot occur in the C# (the bounds are real types) and is skipped by the
    // non-interface scan (the D516 degenerate-shape convention).
    for (const ITypePtr& c : result) {
        if (c && c->Kind() != TypeKind::Interface)
            return c;
    }
    if (!result.empty())
        return result.front();
    return UnknownType();
}

// The C# `IReadOnlyList<IType> FindTypesInBounds(IReadOnlyList<IType> lowerBounds,
// IReadOnlyList<IType> upperBounds)` (TypeInference.cs lines 1059-1186).
std::vector<ITypePtr> FindTypesInBounds(CSharpConversions& conversions,
                                        const std::vector<ITypePtr>& lowerBounds,
                                        const std::vector<ITypePtr>& upperBounds,
                                        TypeInferenceAlgorithm algorithm, int nestingLevel)
{
    // C# `if (lowerBounds.Count == 0 && upperBounds.Count <= 1) return upperBounds;` and
    // the mirror -- if there's only a single type, return that single type; if both
    // inputs are empty, return the empty list. The returned vector is a copy of the input
    // handles (the C# returns the input list itself; the elements are the same types).
    if (lowerBounds.empty() && upperBounds.size() <= 1)
        return upperBounds;
    if (upperBounds.empty() && lowerBounds.size() <= 1)
        return lowerBounds;
    // C# `if (nestingLevel > maxNestingLevel) return EmptyList<IType>.Instance;` -- the
    // `maxNestingLevel` const is 5 (the guard against the infinite generic-recursion of
    // the Improved mode's `InferTypeArgumentsFromBounds` candidate construction).
    if (nestingLevel > 5)
        return {};

    // C# `List<IType> candidateTypes = lowerBounds.Union(upperBounds) ...` -- the Union
    // with the default equality comparer dedups under `IType.Equals`; the port keeps the
    // insertion order with a linear dedup scan (the `TP::AddLowerBound` convention -- no
    // hash or ordering is on the `IType` surface, and the bound lists are tiny).
    std::vector<ITypePtr> unioned;
    auto addUnique = [&unioned](const ITypePtr& t) {
        if (!t)
            return;
        for (const ITypePtr& e : unioned) {
            if (e->Equals(*t))
                return;
        }
        unioned.push_back(t);
    };
    for (const ITypePtr& b : lowerBounds)
        addUnique(b);
    for (const ITypePtr& b : upperBounds)
        addUnique(b);

    // C# `.Where(c => lowerBounds.All(b => conversions.ImplicitConversion(b, c).IsValid))
    //      .Where(c => upperBounds.All(b => conversions.ImplicitConversion(c, b).IsValid))`
    // -- a candidate must be convertible FROM every lower bound and TO every upper bound.
    // The cached public `ImplicitConversion(IType, IType)` entry (the C#
    // `conversions.ImplicitConversion` call site).
    std::vector<ITypePtr> candidateTypes;
    for (const ITypePtr& c : unioned) {
        bool keep = true;
        for (const ITypePtr& b : lowerBounds) {
            if (!conversions.ImplicitConversion(*b, *c)->IsValid()) {
                keep = false;
                break;
            }
        }
        if (!keep)
            continue;
        for (const ITypePtr& b : upperBounds) {
            if (!conversions.ImplicitConversion(*c, *b)->IsValid()) {
                keep = false;
                break;
            }
        }
        if (!keep)
            continue;
        candidateTypes.push_back(c);
    }

    // C# `candidateTypes = candidateTypes.Where(c => candidateTypes.All(o =>
    //     conversions.ImplicitConversion(o, c).IsValid)).ToList();` -- spec draft-v11
    // 12.6.3.13: the result is the unique candidate type to which there is an implicit
    // conversion from all the OTHER candidate types (a candidate always converts to
    // itself, so the self-pairing is a no-op).
    std::vector<ITypePtr> uniqueCandidates;
    for (const ITypePtr& c : candidateTypes) {
        bool keep = true;
        for (const ITypePtr& o : candidateTypes) {
            if (!conversions.ImplicitConversion(*o, *c)->IsValid()) {
                keep = false;
                break;
            }
        }
        if (keep)
            uniqueCandidates.push_back(c);
    }
    candidateTypes = std::move(uniqueCandidates);

    // C# `if (candidateTypes.Count == 1 || !(algorithm == Improved || algorithm ==
    //     ImprovedReturnAllResults)) return candidateTypes;` -- for the CSharp4 default
    // (and any single-candidate result) this IS the return.
    if (candidateTypes.size() == 1
        || !(algorithm == TypeInferenceAlgorithm::Improved
             || algorithm == TypeInferenceAlgorithm::ImprovedReturnAllResults)) {
        return candidateTypes;
    }

    // DEFERRED: the improved algorithm's refinement (the lower bounds'
    // base-type-definition intersection, the compilation-wide candidate scan when there
    // are no lower bounds, the upper-bound `IsDerivedFrom` filtering, and the
    // `InferTypeArgumentsFromBounds` recursion constructing generic candidates) needs
    // `ICompilation.GetAllTypeDefinitions` and the `InferTypeArgumentsFromBounds` engine
    // step, neither ported yet. Until it lands, the pre-refinement spec candidates
    // return for the improved algorithms too (a documented deviation -- the CSharp4
    // default is exact because the early return above already covered it).
    return candidateTypes;
}

// The C# `public IType FindTypeInBounds(IReadOnlyList<IType> lowerBounds,
// IReadOnlyList<IType> upperBounds)` (TypeInference.cs lines 1033-1053).
ITypePtr FindTypeInBounds(CSharpConversions& conversions,
                          const std::vector<ITypePtr>& lowerBounds,
                          const std::vector<ITypePtr>& upperBounds,
                          TypeInferenceAlgorithm algorithm)
{
    // C# `var result = FindTypesInBounds(lowerBounds, upperBounds);` -- the fresh-instance
    // nesting level (0).
    std::vector<ITypePtr> result =
        FindTypesInBounds(conversions, lowerBounds, upperBounds, algorithm,
                          /*nestingLevel*/ 0);
    // C# `if (algorithm == TypeInferenceAlgorithm.ImprovedReturnAllResults)
    //         return IntersectionType.Create(result);` -- DEFERRED (`IntersectionType` is
    // not yet ported); the documented fallback is the picker below, which is exact for 0
    // and 1 candidates (`IntersectionType.Create` maps an empty list to
    // `SpecialType.UnknownType` and a singleton to the single type itself). Only a
    // multi-candidate ambiguous result diverges.
    // C# `else return GetFirstTypePreferNonInterfaces(result);`
    return GetFirstTypePreferNonInterfaces(result);
}

// The C# `bool Fix(TP tp)` (TypeInference.cs lines 967-994, spec draft-v11 section
// 12.6.3.13 "Fixing").
bool Fix(CSharpConversions& conversions, TP& tp, TypeInferenceAlgorithm algorithm,
         int nestingLevel)
{
    // C# `Debug.Assert(!tp.IsFixed);` (debug-only).
    assert(!tp.IsFixed());
    // C# `if (tp.ExactBound != null) {` -- the exact bound will always be the result.
    if (tp.ExactBound != nullptr) {
        // C# `tp.FixedTo = tp.ExactBound;`
        tp.FixedTo = tp.ExactBound;
        // C# `if (tp.MultipleDifferentExactBounds) return false;`
        if (tp.MultipleDifferentExactBounds)
            return false;
        // C# `return tp.LowerBounds.All(b => conversions.ImplicitConversion(b,
        //         tp.FixedTo).IsValid) && tp.UpperBounds.All(b =>
        //         conversions.ImplicitConversion(tp.FixedTo, b).IsValid);` -- every lower
        // bound must still convert TO the fixed type and the fixed type to every upper
        // bound. `*b` / `*tp.FixedTo` are `IType&` (the `shared_ptr<IType>` deref feeds the
        // cached public entry's non-const parameters directly).
        for (const ITypePtr& b : tp.LowerBounds) {
            if (!conversions.ImplicitConversion(*b, *tp.FixedTo)->IsValid())
                return false;
        }
        for (const ITypePtr& b : tp.UpperBounds) {
            if (!conversions.ImplicitConversion(*tp.FixedTo, *b)->IsValid())
                return false;
        }
        return true;
    }
    // C# `var types = CreateNestedInstance().FindTypesInBounds(tp.LowerBounds.ToArray(),
    //         tp.UpperBounds.ToArray());` -- the nested instance bumps the nesting level
    // by one (the C# `CreateNestedInstance`), so the call threads `nestingLevel + 1`.
    std::vector<ITypePtr> types = FindTypesInBounds(conversions, tp.LowerBounds,
                                                    tp.UpperBounds, algorithm,
                                                    nestingLevel + 1);
    // C# `if (algorithm == TypeInferenceAlgorithm.ImprovedReturnAllResults) {
    //         tp.FixedTo = IntersectionType.Create(types);
    //         return types.Count >= 1; }` -- DEFERRED (`IntersectionType` is not yet
    // ported); the documented fallback below is exact for 0 and 1 candidates.
    // C# `else { tp.FixedTo = GetFirstTypePreferNonInterfaces(types);
    //          return types.Count == 1; }`
    tp.FixedTo = GetFirstTypePreferNonInterfaces(types);
    return types.size() == 1;
}

// The C# `public IType GetBestCommonType(IList<ResolveResult> expressions, out bool
// success)` (TypeInference.cs lines 1001-1026, spec draft-v11 section 12.6.3.17).
ITypePtr GetBestCommonType(
    const ICompilation& compilation, CSharpConversions& conversions,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& expressions,
    bool& success, TypeInferenceAlgorithm algorithm)
{
    // C# `if (expressions.Count == 1) { success = IsValidType(expressions[0].Type);
    //         return expressions[0].Type; }` -- a single expression needs no inference;
    // its own type IS the best common type (valid or not).
    if (expressions.size() == 1) {
        success = IsValidType(expressions[0]->Type());
        return expressions[0]->TypePtr();
    }
    // C# `ITypeParameter tp = DummyTypeParameter.GetMethodTypeParameter(0);
    //     this.typeParameters = new TP[1] { new TP(tp) };` -- a one-entry inference over
    // the cached dummy METHOD type parameter 0 (Index 0, OwnerType Method -- the
    // `GetTPForType` index contract: the lower bounds the inference accumulates land on
    // this very dummy).
    std::shared_ptr<ILSpy::Decompiler::TypeSystem::ITypeParameter> tp =
        ILSpy::Decompiler::TypeSystem::Implementation::DummyTypeParameter::GetMethodTypeParameter(0);
    std::vector<TP> typeParameters;
    typeParameters.emplace_back(*tp);
    // C# `foreach (ResolveResult r in expressions) MakeOutputTypeInference(r, tp);` --
    // every expression's output type lower-bounds the dummy (a lambda contributes its
    // inferred return only against a delegate target -- the dummy is not one, so a
    // lambda's NoType fails the `IsValidType` gate and contributes nothing; a plain
    // expression contributes its own type). The C# instance's `classTypeArguments` is
    // always null here (a fresh/reset instance -- `InferTypeArguments` nulls it in its
    // `finally`), so the call threads `std::nullopt`.
    for (const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& r : expressions) {
        MakeOutputTypeInference(compilation, typeParameters, std::nullopt, *r, *tp);
    }
    // C# `success = Fix(typeParameters[0]); return typeParameters[0].FixedTo ??
    //         SpecialType.UnknownType;` -- the fix decides the success; an unfixed TP
    // reports the `UnknownType` null object.
    success = Fix(conversions, typeParameters[0], algorithm, /*nestingLevel*/ 0);
    return typeParameters[0].FixedTo ? typeParameters[0].FixedTo : UnknownType();
}

// ===========================================================================
// The InferTypeArguments region (TypeInference.cs lines 116-170 + 179-208 + 277-380) --
// the main entry and the two private phases it drives (the region overview is in the
// header).
// ===========================================================================

// The C# `void PhaseOne()` (TypeInference.cs lines 277-311, C# 4.0 spec section 7.5.2.1
// "The first phase").
void PhaseOne(const ICompilation& compilation, std::vector<TP>& typeParameters,
              const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
              const std::vector<ITypePtr>& parameterTypes,
              const std::optional<std::vector<ITypePtr>>& classTypeArguments)
{
    // The C# iterates the instance `arguments` array (already the common-min size, no
    // nulls); the lift iterates to the common min of the two threaded vectors and skips a
    // null entry (the `CalculateDependencyMatrix` convention -- the main entry has
    // already soft-failed on nulls, so the skip covers only a degenerate direct call).
    const std::size_t n = std::min(arguments.size(), parameterTypes.size());
    for (std::size_t i = 0; i < n; i++) {
        if (!arguments[i] || !parameterTypes[i])
            continue;
        // C# `ResolveResult Ei = arguments[i]; IType Ti = parameterTypes[i];`
        const ILSpy::Decompiler::Semantics::ResolveResult& Ei = *arguments[i];
        IType& Ti = *parameterTypes[i];

        // C# `LambdaResolveResult lrr = Ei as LambdaResolveResult;` -- the RTTI test the
        // dispatch owns (the `dynamic_cast`).
        const LambdaResolveResult* lrr = dynamic_cast<const LambdaResolveResult*>(&Ei);
        // C# `if (lrr != null) { MakeExplicitParameterTypeInference(lrr, Ti); }`
        if (lrr != nullptr) {
            MakeExplicitParameterTypeInference(compilation, typeParameters, *lrr, Ti);
        }
        // C# `if (lrr != null || Ei is MethodGroupResolveResult) {
        //         if (OutputTypeContainsUnfixed(Ei, Ti) && !InputTypesContainsUnfixed(Ei, Ti))
        //             MakeOutputTypeInference(Ei, Ti); }` -- the C# source comment
        // "this is not in the spec???" -- an argument whose output types mention an
        // unfixed parameter while its input types do not contributes an output-type
        // inference up front (the implicitly-typed lambda whose return type closes over a
        // type parameter of the same inference).
        if (lrr != nullptr || dynamic_cast<const MethodGroupResolveResult*>(&Ei) != nullptr) {
            if (OutputTypeContainsUnfixed(typeParameters, Ei, Ti)
                && !InputTypesContainsUnfixed(typeParameters, Ei, Ti)) {
                MakeOutputTypeInference(compilation, typeParameters, classTypeArguments, Ei, Ti);
            }
        }

        // C# `if (IsValidType(Ei.Type)) {
        //         if (Ti is ByReferenceType) MakeExactInference(Ei.Type, Ti);
        //         else MakeLowerBoundInference(Ei.Type, Ti); }` -- a plain expression's
        // own type bounds the parameter type (EXACT against a by-ref parameter shape,
        // LOWER otherwise). `Ei.Type()` returns `const IType&` while the two workers take
        // non-const `IType&` (the non-const `ChangeNullability`, D406), so the
        // `const_cast` is the established safe pattern (the D515/D517 convention).
        if (IsValidType(Ei.Type())) {
            if (dynamic_cast<const ByReferenceType*>(&Ti) != nullptr) {
                MakeExactInference(compilation, typeParameters,
                                   const_cast<IType&>(Ei.Type()), Ti);
            } else {
                MakeLowerBoundInference(compilation, typeParameters,
                                        const_cast<IType&>(Ei.Type()), Ti);
            }
        }
    }
}

// The C# `bool PhaseTwo()` (TypeInference.cs lines 321-380, spec draft-v11 section
// 12.6.3.3 "The second phase").
bool PhaseTwo(const ICompilation& compilation, CSharpConversions& conversions,
              std::vector<TP>& typeParameters,
              const std::vector<std::vector<bool>>& dependencyMatrix,
              const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
              const std::vector<ITypePtr>& parameterTypes,
              const std::optional<std::vector<ITypePtr>>& classTypeArguments,
              TypeInferenceAlgorithm algorithm)
{
    // C# `List<TP> typeParametersToFix = new List<TP>();
    //     foreach (TP Xi in typeParameters) {
    //         if (Xi.IsFixed == false) {
    //             if (!typeParameters.Any((TP Xj) => !Xj.IsFixed && DependsOn(Xi, Xj)))
    //                 typeParametersToFix.Add(Xi); } }`
    // -- "All unfixed type variables Xi which do not depend on any Xj are fixed": the
    // `Any` over the unfixed partners is the early-out loop below.
    std::vector<TP*> typeParametersToFix;
    for (TP& Xi : typeParameters) {
        if (Xi.IsFixed())
            continue;
        bool dependsOnUnfixed = false;
        for (const TP& Xj : typeParameters) {
            if (!Xj.IsFixed() && DependsOn(dependencyMatrix, Xi, Xj)) {
                dependsOnUnfixed = true;
                break;
            }
        }
        if (!dependsOnUnfixed)
            typeParametersToFix.push_back(&Xi);
    }
    // C# `if (typeParametersToFix.Count == 0) { foreach (TP Xi in typeParameters) {
    //         if (!Xi.IsFixed && Xi.HasBounds) {
    //             if (typeParameters.Any((TP Xj) => DependsOn(Xj, Xi)))
    //                 typeParametersToFix.Add(Xi); } } }`
    // -- "If no such type variables exist, all unfixed type variables Xi are fixed for
    // which all of the following hold: Xi has a non-empty set of bounds, and there is at
    // least one type variable Xj that depends on Xi" -- the cycle-breaking fallback
    // (a dependency cycle leaves no parameter depending on nothing, so the bounded
    // members of a cycle get fixed to break it).
    if (typeParametersToFix.empty()) {
        for (TP& Xi : typeParameters) {
            if (Xi.IsFixed() || !Xi.HasBounds())
                continue;
            bool anyDependsOnXi = false;
            for (const TP& Xj : typeParameters) {
                if (DependsOn(dependencyMatrix, Xj, Xi)) {
                    anyDependsOnXi = true;
                    break;
                }
            }
            if (anyDependsOnXi)
                typeParametersToFix.push_back(&Xi);
        }
    }
    // C# `bool errorDuringFix = false; foreach (TP tp in typeParametersToFix) {
    //         if (!Fix(tp)) errorDuringFix = true; }
    //     if (errorDuringFix) return false;` -- every candidate is fixed (no early
    // exit); a single failing fix fails the phase. The nesting level is 0: `PhaseTwo`
    // only ever runs on a fresh top-level instance (a nested instance exists only inside
    // `Fix`/`FindTypesInBounds`, which never re-enter the phases).
    bool errorDuringFix = false;
    for (TP* tp : typeParametersToFix) {
        if (!Fix(conversions, *tp, algorithm, /*nestingLevel*/ 0))
            errorDuringFix = true;
    }
    if (errorDuringFix)
        return false;
    // C# `bool unfixedTypeVariablesExist = typeParameters.Any((TP X) => X.IsFixed == false);`
    bool unfixedTypeVariablesExist = false;
    for (const TP& X : typeParameters) {
        if (!X.IsFixed()) {
            unfixedTypeVariablesExist = true;
            break;
        }
    }
    // C# `if (typeParametersToFix.Count == 0 && unfixedTypeVariablesExist) {
    //         Log.WriteLine("Type inference fails: there are still unfixed TPs remaining");
    //         return false; }` -- nothing was fixable but unfixed parameters remain: the
    // inference fails.
    if (typeParametersToFix.empty() && unfixedTypeVariablesExist)
        return false;
    // C# `else if (!unfixedTypeVariablesExist) { return true; }` -- everything is fixed.
    if (!unfixedTypeVariablesExist)
        return true;
    // C# `else { for (int i = 0; i < arguments.Length; i++) { ResolveResult Ei =
    //         arguments[i]; IType Ti = parameterTypes[i];
    //         if (OutputTypeContainsUnfixed(Ei, Ti) && !InputTypesContainsUnfixed(Ei, Ti))
    //             MakeOutputTypeInference(Ei, Ti); }
    //     return PhaseTwo(); }` -- the output-type-inference loop over the remaining
    // arguments (the newly-fixed parameters may have unblocked an argument whose output
    // types were previously entangled), then the phase REPEATS. The common-min bound and
    // the null-entry skip are the `PhaseOne` lift convention (the main entry has already
    // rejected nulls).
    const std::size_t n = std::min(arguments.size(), parameterTypes.size());
    for (std::size_t i = 0; i < n; i++) {
        if (!arguments[i] || !parameterTypes[i])
            continue;
        const ILSpy::Decompiler::Semantics::ResolveResult& Ei = *arguments[i];
        IType& Ti = *parameterTypes[i];
        if (OutputTypeContainsUnfixed(typeParameters, Ei, Ti)
            && !InputTypesContainsUnfixed(typeParameters, Ei, Ti)) {
            MakeOutputTypeInference(compilation, typeParameters, classTypeArguments, Ei, Ti);
        }
    }
    return PhaseTwo(compilation, conversions, typeParameters, dependencyMatrix, arguments,
                    parameterTypes, classTypeArguments, algorithm);
}

// The C# `public IType[] InferTypeArguments(IReadOnlyList<ITypeParameter> typeParameters,
// IReadOnlyList<ResolveResult> arguments, IReadOnlyList<IType> parameterTypes, out bool
// success, IReadOnlyList<IType> classTypeArguments = null)` (TypeInference.cs lines
// 116-170).
std::vector<ITypePtr> InferTypeArguments(
    const ICompilation& compilation, CSharpConversions& conversions,
    const std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*>& typeParameters,
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::vector<ITypePtr>& parameterTypes, bool& success,
    const std::optional<std::vector<ITypePtr>>& classTypeArguments,
    TypeInferenceAlgorithm algorithm)
{
    // C# `if (typeParameters == null) throw new ArgumentNullException(...)` (and the
    // `arguments`/`parameterTypes` twins) -- the D374 non-null-reference convention: the
    // C++ references and vectors cannot be null.
    //
    // C# `this.typeParameters = new TP[typeParameters.Count];
    //     for (int i = 0; i < this.typeParameters.Length; i++) {
    //         if (i != typeParameters[i].Index)
    //             throw new ArgumentException("Type parameter has wrong index");
    //         if (typeParameters[i].OwnerType != SymbolKind.Method)
    //             throw new ArgumentException("Type parameter must be owned by a method");
    //         this.typeParameters[i] = new TP(typeParameters[i]); }`
    // -- the contract checks take the documented SOFT FAILURE (the region overview in the
    // header): `success = false` and the all-`UnknownType` result (the C# aborts with an
    // exception the port cannot throw; the violations are unreachable through the real
    // `IMethod::TypeParameters()` surface, whose entries always carry `Index == i` and
    // `OwnerType == Method`).
    std::vector<TP> state;
    state.reserve(typeParameters.size());
    // The helper producing the failed-inference shape: every position reports the
    // `SpecialType.UnknownType` null object (the C# `tp.FixedTo ??
    // SpecialType.UnknownType` report of an unfixed parameter).
    auto reportUnfixedAll = [&typeParameters]() {
        std::vector<ITypePtr> result;
        result.reserve(typeParameters.size());
        for (std::size_t i = 0; i < typeParameters.size(); i++)
            result.push_back(UnknownType());
        return result;
    };
    for (std::size_t i = 0; i < typeParameters.size(); i++) {
        if (typeParameters[i] == nullptr
            || typeParameters[i]->Index() != static_cast<int>(i)
            || typeParameters[i]->OwnerType() != ILSpy::Decompiler::TypeSystem::SymbolKind::Method) {
            success = false;
            return reportUnfixedAll();
        }
        state.emplace_back(*typeParameters[i]);
    }
    // C# `this.parameterTypes = new IType[Math.Min(arguments.Count, parameterTypes.Count)];
    //     this.arguments = new ResolveResult[this.parameterTypes.Length];
    //     for (int i = 0; i < this.parameterTypes.Length; i++) {
    //         if (arguments[i] == null || parameterTypes[i] == null)
    //             throw new ArgumentNullException();
    //         this.arguments[i] = arguments[i]; this.parameterTypes[i] = parameterTypes[i]; }`
    // -- the common-min arrays; a null entry takes the soft failure (the C# throw).
    const std::size_t count = std::min(arguments.size(), parameterTypes.size());
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> args;
    std::vector<ITypePtr> paramTypes;
    args.reserve(count);
    paramTypes.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        if (!arguments[i] || !parameterTypes[i]) {
            success = false;
            return reportUnfixedAll();
        }
        args.push_back(arguments[i]);
        paramTypes.push_back(parameterTypes[i]);
    }
    // C# `this.classTypeArguments = classTypeArguments;` -- the threaded optional (the C#
    // null list ports to `std::nullopt`).
    //
    // C# `PhaseOne(); success = PhaseTwo();`
    PhaseOne(compilation, state, args, paramTypes, classTypeArguments);
    // The C# `DependsOn` lazily computes the dependency matrix on the first call (inside
    // `PhaseTwo`) and memoizes it for the whole inference; the lift computes it ONCE here
    // and threads it into both `PhaseTwo` recursions (the matrix inputs never change
    // during the phases -- behavior-identical, the `CalculateDependencyMatrix` lift).
    const std::vector<std::vector<bool>> dependencyMatrix =
        CalculateDependencyMatrix(state, args, paramTypes);
    success = PhaseTwo(compilation, conversions, state, dependencyMatrix, args, paramTypes,
                       classTypeArguments, algorithm);
    // C# `return this.typeParameters.Select(tp => tp.FixedTo ??
    //         SpecialType.UnknownType).ToArray();` -- the inferred type arguments; an
    // unfixed parameter reports the null object. The `Reset()` cleanup is moot in the
    // lift (the local state dies at return; the result holds owning handles).
    std::vector<ITypePtr> result;
    result.reserve(state.size());
    for (const TP& tp : state)
        result.push_back(tp.FixedTo ? tp.FixedTo : UnknownType());
    return result;
}

// The C# `public IType[] InferTypeArgumentsFromBounds(IReadOnlyList<ITypeParameter>
// typeParameters, IType targetType, IEnumerable<IType> lowerBounds, IEnumerable<IType>
// upperBounds, out bool success)` (TypeInference.cs lines 179-208).
std::vector<ITypePtr> InferTypeArgumentsFromBounds(
    const ICompilation& compilation, CSharpConversions& conversions,
    const std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*>& typeParameters,
    IType& targetType, const std::vector<ITypePtr>& lowerBounds,
    const std::vector<ITypePtr>& upperBounds, bool& success,
    TypeInferenceAlgorithm algorithm)
{
    // C# `if (typeParameters == null) throw new ArgumentNullException(...)` (and the
    // `targetType`/`lowerBounds`/`upperBounds` twins) -- the D374 non-null-reference
    // convention.
    //
    // C# `this.typeParameters = new TP[typeParameters.Count];
    //     for (int i = 0; i < this.typeParameters.Length; i++) {
    //         if (i != typeParameters[i].Index)
    //             throw new ArgumentException("Type parameter has wrong index");
    //         this.typeParameters[i] = new TP(typeParameters[i]); }`
    // -- NO `OwnerType` check here (only the index validation); the violation takes the
    // documented soft failure (the region overview in the header).
    std::vector<TP> state;
    state.reserve(typeParameters.size());
    for (std::size_t i = 0; i < typeParameters.size(); i++) {
        if (typeParameters[i] == nullptr
            || typeParameters[i]->Index() != static_cast<int>(i)) {
            success = false;
            std::vector<ITypePtr> result;
            result.reserve(typeParameters.size());
            for (std::size_t j = 0; j < typeParameters.size(); j++)
                result.push_back(UnknownType());
            return result;
        }
        state.emplace_back(*typeParameters[i]);
    }
    // C# `foreach (IType b in lowerBounds) { MakeLowerBoundInference(b, targetType); }` --
    // every lower bound lower-bound-infers against the target type. A degenerate null
    // bound entry would NRE in the C# and is skipped (the D516 convention -- the Improved
    // `FindTypesInBounds` refinement passes its own candidate lists, never null).
    for (const ITypePtr& b : lowerBounds) {
        if (b)
            MakeLowerBoundInference(compilation, state, *b, targetType);
    }
    // C# `foreach (IType b in upperBounds) { MakeUpperBoundInference(b, targetType); }`
    for (const ITypePtr& b : upperBounds) {
        if (b)
            MakeUpperBoundInference(compilation, state, *b, targetType);
    }
    // C# `IType[] result = new IType[this.typeParameters.Length];
    //     success = true;
    //     for (int i = 0; i < result.Length; i++) {
    //         success &= Fix(this.typeParameters[i]);
    //         result[i] = this.typeParameters[i].FixedTo ?? SpecialType.UnknownType; }`
    // -- the C# `&=` is NON-short-circuit: `Fix` runs for EVERY parameter even after a
    // failure, and `success` accumulates all the results. The fresh-instance nesting
    // level is 0.
    std::vector<ITypePtr> result;
    result.reserve(state.size());
    success = true;
    for (TP& tp : state) {
        const bool fixedOk = Fix(conversions, tp, algorithm, /*nestingLevel*/ 0);
        success = success && fixedOk;
        result.push_back(tp.FixedTo ? tp.FixedTo : UnknownType());
    }
    // C# `Reset(); return result;` -- the cleanup is moot in the lift.
    return result;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
