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

// Port of the `TypeInference` PURE LEAF HELPERS -- the first slice of the C# type-inference
// engine (ICSharpCode.Decompiler/CSharp/Resolver/TypeInference.cs, the ~1188-line long-pole
// blocker for the `OverloadResolution` `RunTypeInference` engine step and therefore for
// `CalculateCandidate`/`AddCandidate`/`AddMethodLists` and the deferred `MethodGroupConversion`
// arm). The Input/Output Types region (C# spec draft-v11 sections 12.6.3.4 + 12.6.3.5) is PURE
// -- it reads only the argument `ResolveResult`'s runtime type and the delegate-or-expression-
// tree signature resolved from the parameter type -- so it lifts to `Detail::` free functions
// ahead of the `TypeInference` class skeleton (the `CSharpConversionsHelpers` /
// `OverloadResolutionHelpers` lift-to-free-functions precedent). The remaining regions
// (`InferTypeArguments`, the `TP` fixed-point state, the `OccursInVisitor`, the Inference
// Phases, `MakeOutputTypeInference`/`MakeExactInference`/`MakeLowerBoundInference`/
// `MakeUpperBoundInference`, `Fixing`, `GetBestCommonType`, `FindTypeInBounds`) need the
// `TypeInference` instance state (`typeParameters`/`arguments`/`parameterTypes`/
// `dependencyMatrix`) and land with the class skeleton in later increments.
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

#include <vector>

// `ResolveResult` is included transitively by the two ResolveResult subclass headers above
// (their base class), so the `const ResolveResult&` parameters need no extra include.

// `IType` is included transitively by the ResolveResult hierarchy (the base `ResolveResult`
// holds an `ITypePtr`), so the `const IType&` parameter needs no extra include.

// `IMethod` is forward-declared (the `const IMethod*` return type is a nullable non-owning
// pointer -- a complete pointer type with the class incomplete; the .cpp includes the full
// header for `Parameters()`/`ReturnType()`).
namespace ILSpy::Decompiler::TypeSystem { class IMethod; }

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
