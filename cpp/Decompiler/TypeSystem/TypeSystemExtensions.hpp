// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/TypeSystemExtensions.cs -- the base-
// type traversal region of the extension-method class. The C# is a static class
// of extension methods; per the extension-method convention they port as free
// functions in the type's namespace (`ILSpy::Decompiler::TypeSystem`).
//
// PORTED here (the base-type region the C# `MemberLookup` / `IsDerivedFrom`
// consumers depend on): GetAllBaseTypes / GetNonInterfaceBaseTypes /
// GetAllBaseTypeDefinitions / IsDerivedFrom (both overloads), plus the
// `BaseTypeCollector` traversal under TypeSystem/Implementation/.
//
// The remaining 800+ lines of TypeSystemExtensions.cs (the Cecil/metadata
// helpers, the generic-instantiation/substitution visitors, the member
// lookup helpers the C# resolver uses, the OpenComponent helpers, ...) follow
// with their consumers.

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <stdexcept>
#include <optional>
#include <vector>

// Forward declarations for the new closure/default-value region's parameter types
// (pointer / reference declarations only -- the .cpp includes the full headers;
// `IEntity` / `IParameter` are not among the `IType.hpp` forward declarations).
namespace ILSpy::Decompiler::TypeSystem {
class IEntity;
class IParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::TypeSystem {

// The C# `IEnumerable<IType> GetAllBaseTypes(this IType type)`:
//
// "Gets all base types. This is the reflexive and transitive closure of
// `IType.DirectBaseTypes`. Note that this method does not return all
// supertypes - doing so is impossible due to contravariance (and undesirable
// for covariance as the list could become very large). The output is ordered
// so that base types occur before derived types."
//
// The C# extension's null-check / ArgumentNullException ports to a
// `const IType*` overload (raw pointers may be null only where documented);
// a `const IType&` convenience delegates to it. The returned raw pointers
// observe the visited types (the caller outlives them; the collector stores
// the addresses of the DirectBaseTypes() snapshot contents, which the port's
// type interfaces keep alive via the shared ownership of IType).
std::vector<const IType*> GetAllBaseTypes(const IType* type);
inline std::vector<const IType*> GetAllBaseTypes(const IType& type)
{
    return GetAllBaseTypes(&type);
}

// The C# `IEnumerable<IType> GetNonInterfaceBaseTypes(this IType type)`:
//
// "Gets all non-interface base types. When `type` is an interface, this method
// will also return base interfaces (return same output as GetAllBaseTypes()).
// The output is ordered so that base types occur before derived types."
std::vector<const IType*> GetNonInterfaceBaseTypes(const IType* type);
inline std::vector<const IType*> GetNonInterfaceBaseTypes(const IType& type)
{
    return GetNonInterfaceBaseTypes(&type);
}

// The C# `IEnumerable<ITypeDefinition> GetAllBaseTypeDefinitions(this IType type)`:
//
// "Gets all base type definitions. The output is ordered so that base types
// occur before derived types. This is equivalent to
// `type.GetAllBaseTypes().Select(t => t.GetDefinition()).Where(d => d != null).Distinct()`."
//
// The C# `.Distinct()` uses the default (reference) equality for ITypeDefinition
// (type definitions do not override Equals), so the port's pointer-identity
// dedup is exact.
std::vector<const ITypeDefinition*> GetAllBaseTypeDefinitions(const IType* type);
inline std::vector<const ITypeDefinition*> GetAllBaseTypeDefinitions(const IType& type)
{
    return GetAllBaseTypeDefinitions(&type);
}

// The C# `bool IsDerivedFrom(this ITypeDefinition type, ITypeDefinition baseType)`:
//
// "Gets whether this type definition is derived from the base type definition."
// A null baseType returns false; a mismatch of both entities'
// ICompilationProvider::Compilation() throws `std::runtime_error` (the
// SimpleCompilation precedent for a ported C# `InvalidOperationException`).
bool IsDerivedFrom(const ITypeDefinition& type, const ITypeDefinition* baseType);

// The C# `bool IsDerivedFrom(this ITypeDefinition type, KnownTypeCode baseType)`:
//
// "Gets whether this type definition is derived from a given known type."
// KnownTypeCode::None returns false (the C# has no known type to look up).
bool IsDerivedFrom(const ITypeDefinition& type, KnownTypeCode baseType);

// The C# `public static IEnumerable<ITypeDefinition> GetAllTypeDefinitions(this ICompilation
// compilation)` (TypeSystemExtensions.cs line 462, the `#region GetType/Member`):
//
// "Gets all type definitions in the compilation. This may include types from referenced
// assemblies that are not accessible in the main assembly."
//
// The C# `compilation.Modules.SelectMany(a => a.TypeDefinitions)` ports to the
// concatenation of every module's `TypeDefinitions()` snapshot, in module-list order
// (the main module first). The entries are non-owning `const ITypeDefinition*` snapshots
// (the type system owns the entities; the caller keeps the compilation alive). The
// first consumer is the TypeInference Improved `FindTypesInBounds` refinement's
// compilation-wide candidate scan.
std::vector<const ITypeDefinition*> GetAllTypeDefinitions(const ICompilation& compilation);

// The C# `public static IEnumerable<ITypeDefinition> GetTopLevelTypeDefinitions(
// this ICompilation compilation)` (TypeSystemExtensions.cs line 472) -- the same
// SelectMany over `TopLevelTypeDefinitions()` (all NON-NESTED types in each assembly,
// in module-list order). `GetAllTypeDefinitions` additionally includes the nested types.
std::vector<const ITypeDefinition*> GetTopLevelTypeDefinitions(const ICompilation& compilation);

// The C# `public static bool IsUnbound(this IType type)` (TypeSystemExtensions.cs
// line 230, the `IsOpen / IsUnbound / IsUnmanagedType / IsKnownType` region):
//
// "Gets whether the type is unbound. This is true for any type definition that has
// type parameters, e.g. `typeof(List<>)` or `typeof(Dictionary<,>)`. Note that
// `typeof(List<Dictionary<,>>)` is considered a bound type despite containing an
// unbound type. This method returns false for partially parameterized types
// (`Dictionary<string, >`)."
//
// The C# body is `(type is ITypeDefinition || type is UnknownType) &&
// type.TypeParameterCount > 0` -- the two RTTI is-tests port to `dynamic_cast`s
// (an `UnknownType` is NOT an `ITypeDefinition`, so both casts are needed). The
// C# null-check / ArgumentNullException is structurally unreachable through the
// reference parameter (the D374 convention). The first consumer is
// `TypeSystemAstBuilder.ConvertTypeHelper` (the unbound-generic branch that
// renders `List<>` either with its type-parameter names or with `UnboundTypeArgument`
// placeholders).
bool IsUnbound(const IType& type);

// The C# `public static ITypeDefinition GetTypeDefinition(this IModule module,
// FullTypeName fullTypeName)` (TypeSystemExtensions.cs line 519, the
// `IAssembly.GetTypeDefinition()` region):
//
// "Gets the type definition for the specified unresolved type. Returns null if the
// unresolved type does not belong to this assembly."
//
// Resolves the top-level name through `IModule::GetTypeDefinition(TopLevelTypeName)`,
// then walks the nesting chain (`FullTypeName::GetNestedTypeName` per level, the
// accumulated type-parameter count including each level's
// `GetNestedTypeAdditionalTypeParameterCount`) through the definition's
// `NestedTypes()` table (name + type-parameter-count match, the file-local
// `FindNestedType` helper). A missing top-level type or a broken nesting chain
// yields nullptr. The returned pointer is non-owning (the module's type table owns
// the definition). The first consumer is `TypeSystemAstBuilder.ConvertType(
// FullTypeName)` (the resolver-holding overload's per-module lookup).
const ITypeDefinition* GetTypeDefinition(const IModule& module, const FullTypeName& fullTypeName);

// The C# `public static bool IsKnownType(this IType type, KnownTypeCode knownType)`:
//
// "Gets whether the type is the specified known type. For generic known types, this returns true for any
// parameterization of the type (and also for the definition itself)." -- `type.GetDefinition()?.KnownTypeCode
// == knownType`. A null definition yields false.
bool IsKnownType(const IType& type, KnownTypeCode knownType);

// The C# `public static bool IsArrayInterfaceType(this IType type)` -- whether the type is one of the
// 5 generic collection interfaces (`IEnumerable<T>`/`ICollection<T>`/`IList<T>`/`IReadOnlyCollection<T>`/
// `IReadOnlyList<T>`) with exactly 1 type parameter. The `params`-array/Span expansion in
// `OverloadResolution.ResolveParameterTypes` uses this to unpack a `params IEnumerable<T>` into `T`.
bool IsArrayInterfaceType(const IType& type);

// The C# `public static bool IsAnyPointer(this TypeKind typeKind)` (TypeSystemExtensions.cs line 448) --
// true for `TypeKind.Pointer` or `TypeKind.FunctionPointer` (both are pointer-shaped kinds; the
// C# `switch` expression returns `true` for those two, `false` for every other kind). The first
// consumers are the `CSharpConversions` pointer-conversion helpers (`ImplicitPointerConversion` /
// `ExplicitPointerConversion`); the `Kind`-only check is a pure value test on the enum.
bool IsAnyPointer(TypeKind typeKind);

// The C# `public static IType SkipModifiers(this IType ty)` (TypeSystemExtensions.cs line 425) --
// unwraps `ModifiedType` (modopt/modreq custom-modifier) decorators, returning the underlying element
// type. Loops while the type is a `ModifiedType` (modifiers may nest). Returns the type itself when it
// carries no custom modifiers. The C# `this IType` extension throws `ArgumentNullException` on null;
// the `const IType&` port compiles that out (a reference cannot bind to null). A degenerate
// `ModifiedType` whose `Element()` is a null `shared_ptr` yields a null return (the C# would return null
// too); callers observe the result and guard for null before use. The first consumers are the
// `NullableType.IsNullable` / `GetUnderlyingType` helpers (which `SkipModifiers()` before the
// `as ParameterizedType` test).
const IType* SkipModifiers(const IType& type);

// The C# `public static IType WithoutNullability(this IType type)` (TypeSystemExtensions.cs
// line 799) -- `type.ChangeNullability(Nullability.Oblivious)`: the same type with the
// nullability annotation erased (the pre-C#-8 default annotation). The C# extension's
// null-check / ArgumentNullException compiles out under the `IType&` reference convention;
// the parameter is NON-CONST because `IType::ChangeNullability` is non-const (it may return
// `shared_from_this()`, the C# `return this` reference identity, D406). The returned handle
// shares ownership: for an already-Oblivious type it is the same managed object (every
// concrete port's `ChangeNullability` returns `shared_from_this()` when nothing changes),
// and for an annotated type it is the unwrapped base (`NullabilityAnnotatedType` forwards to
// the wrapped type's `ChangeNullability`). The first consumers are the `TypeInference`
// bound-inference workers (the `if (U.Nullability == V.Nullability) { U =
// U.WithoutNullability(); ... }` strip) and `TupleUnderlyingTypeOrSelf`.
ILSpy::Decompiler::TypeSystem::ITypePtr WithoutNullability(ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `public static IMethod GetDelegateInvokeMethod(this IType type)` (TypeSystemExtensions.cs
// line 414) -- "Gets the invoke method for a delegate type. Returns null if the type is not a
// delegate type; or if the invoke method could not be found." A delegate type's `Invoke` method is
// the method-shaped entry the delegate-conversion / method-group-conversion / anonymous-function-
// conversion helpers compare a candidate method against (the delegate's signature contract). The
// `Kind == TypeKind.Delegate` guard returns null for non-delegate kinds, and the
// `GetMethods(m => m.Name == "Invoke", GetMemberOptions.IgnoreInheritedMembers).FirstOrDefault()`
// returns the first `Invoke` method or null when none is found (an empty vector yields null). The
// C# null-check / `ArgumentNullException` compiles out (a `const IType&` reference cannot bind to
// null). The first consumers are the deferred `CSharpConversions` arms
// (`AnonymousFunctionConversion` / `MethodGroupConversion`) and the public
// `IsDelegateCompatible(IMethod, IType)` overload that resolves the delegate's invoke method.
const IMethod* GetDelegateInvokeMethod(const IType& type);

// The C# `public static bool IsInlineArrayType(this IType type)` (TypeSystemExtensions.cs
// line 340) -- true for a struct-kind type whose definition carries the `[InlineArray]`
// attribute (a C# 12 inline array; the compiler-enforced shape is a single instance field).
// The C# extension-method null-check / `ArgumentNullException` compiles out (a `const IType&`
// reference cannot bind to null). Pure (reads only `Kind` / `GetDefinition` /
// `HasAttribute`), so it takes `const IType&` like `IsKnownType`.
bool IsInlineArrayType(const IType& type);

// The C# `public static int? GetInlineArrayLength(this IType type)` (TypeSystemExtensions.cs
// line 352) -- the `[InlineArray(N)]` attribute's first fixed argument as a nullable int.
// Null (std::nullopt) for a non-struct kind, a definitionless type, a missing attribute, an
// empty fixed-argument list (`FirstOrDefault()` on empty yields null), or a first argument
// whose boxed value is not an int (the C# `as int?` yields null; the port's pointer-form
// `std::any_cast` returns null on a type mismatch -- the safe faithful fallback).
std::optional<int> GetInlineArrayLength(const IType& type);

// The C# `public static IType GetInlineArrayElementType(this IType arrayType)`
// (TypeSystemExtensions.cs line 361) -- the type of the SINGLE instance field
// (`arrayType?.GetFields(f => !f.IsStatic).SingleOrDefault()?.Type ?? SpecialType.UnknownType`).
// Returns an OWNING `ITypePtr`: the field's `Type()` is obtained via `shared_from_this()` +
// `std::const_pointer_cast` (the type-system objects are shared-managed; the accessor's
// `const` is the contract -- the `NullableType.Create` precedent), and the no-instance-field
// fallback is a fresh `UnknownType()` (the `SpecialType.UnknownType` null object).
//
// `SingleOrDefault()` throws `InvalidOperationException` on MORE than one match; the port
// throws `std::runtime_error` there (the `SimpleCompilation` / `CreateResolveResult`
// InvalidOperationException-analog convention) -- a real inline-array struct has exactly one
// instance field, so the throw guards the same metadata invariant the C# does.
ITypePtr GetInlineArrayElementType(const IType& arrayType);

// The C# `public static IType GetElementTypeFromIEnumerable(this IType collectionType,
// ICompilation compilation, bool allowIEnumerator, out bool? isGeneric)`
// (TypeSystemExtensions.cs line 757) -- the element type of a collection type: the type
// argument of the first `IEnumerable<T>` / (with `allowIEnumerator`) `IEnumerator<T>` in
// the type's base-type closure; the `Object` element type when only the non-generic
// `System.Collections.IEnumerable` / (with `allowIEnumerator`) `IEnumerator` is found
// (the C# comment: "System.Collections.IEnumerable found in type hierarchy -> Object is
// element type"); the `SpecialType.UnknownType` null object with `isGeneric = null` when
// neither is. Returns an OWNING `ITypePtr`: the parameterized-type arm returns the type
// argument's own handle (`GetTypeArgument(0)` returns the stored `ITypePtr` by value), the
// non-generic arm recovers the registered `FindType(KnownTypeCode.Object)` handle via
// `shared_from_this()` + `std::const_pointer_cast` (the registered known types must be
// shared-managed, the `NullableType.Create` precedent), and the null-object arm returns a
// fresh `UnknownType()` (the `GetInlineArrayElementType` precedent). The C# `out bool?
// isGeneric` ports to a `std::optional<bool>&` out-parameter assigned before every return
// (the C# `out` contract: `true` = a generic interface was found, `false` = only the
// non-generic one, `nullopt` = neither). A base type whose DEFINITION carries
// `IEnumerableOfT` but which is not itself a `ParameterizedType` (the bare open-generic
// definition) does not match -- the C# `pt != null` guard continues the walk; a degenerate
// `ParameterizedType` with no type arguments (the C# `GetTypeArgument(0)` would throw
// `ArgumentOutOfRangeException`, unreachable for real metadata) also continues under the
// D516 safe-fallback convention (the non-empty-`TypeArguments` guard, the TypeInference
// span-arms precedent). The first consumer is `CSharpResolver.CheckForEnumerableInterface`
// (the `ResolveForeach` region).
ITypePtr GetElementTypeFromIEnumerable(const IType& collectionType,
                                       const ICompilation& compilation,
                                       bool allowIEnumerator,
                                       std::optional<bool>& isGeneric);

// The C# `public static bool IsCompilerGeneratedOrIsInCompilerGeneratedClass(
// this IEntity entity)` (NRExtensions.cs lines 26-46, namespace ICSharpCode.Decompiler
// root) -- whether the entity itself or any type in its nesting chain (transitively
// through `DeclaringTypeDefinition`) carries `[CompilerGenerated]`. The C# home is
// NRExtensions.cs; the port keeps no translation units at the `ILSpy::Decompiler`
// root (only the per-area subdirectories), so the pure IEntity leaf lands here in
// TypeSystemExtensions next to its only ported consumer chain (the
// `IsDefaultValueAssignmentAllowed` region below) -- the land-a-leaf-where-its-
// dependencies-live convention (the IsAnyPointer precedent). The C# private
// `IsCompilerGenerated` sub-extension is inlined (`HasAttribute(CompilerGenerated)`,
// the `IEntity::HasAttribute` classified lookup); the C# null-accepting extension
// contract (`entity != null`) ports to a nullable pointer (null returns false). The
// recursion walks the nesting chain exactly like the C# (the chain is acyclic for
// real metadata; a malformed cyclic NestedClass table would infinitely recurse in
// the C# too, so no depth guard is added -- unlike the metadata-level
// `MetadataFile::IsFieldCompilerGeneratedOrInCompilerGeneratedClass` twin, which
// guards because it reads raw metadata rows).
bool IsCompilerGeneratedOrIsInCompilerGeneratedClass(const IEntity* entity);

// The C# `internal static bool IsPotentialClosure(ITypeDefinition
// decompiledTypeDefinition, ITypeDefinition potentialDisplayClass, bool
// allowTypeImplementingInterfaces = false)` (TransformDisplayClassUsage.cs) --
// whether the type is a compiler-generated display class (closure) belonging to
// the decompiled type's own nesting tree: `[CompilerGenerated]` on the type or its
// nesting chain, a Struct kind (or a Class kind whose direct base types are all
// `object`, unless `allowTypeImplementingInterfaces`), and the decompiled type or
// one of its ancestors being an ancestor of the display class (the C# comment:
// "Either decompiledTypeDefinition is an ancestor type of potentialDisplayClass or
// both have at least one common ancestor"). The C# home is the
// `TransformDisplayClassUsage` IL transform (not ported); the static is a pure
// type-system function whose only ported consumer is the closure-parameter check
// below, so it lands here with the same land-a-leaf convention. Both parameters are
// nullable (the C# null checks: a null display class is false; a null decompiled
// type walks zero ancestors and is false). The C# `HashSet<ITypeDefinition>` of
// the display class's ancestors uses reference equality (the default comparer for
// the reference type), so the port collects the strict ancestors into a
// pointer-keyed vector with a linear `Contains` (the chains are nesting-depth
// sized; the dedup-by-pointer-identity convention).
bool IsPotentialClosure(const ITypeDefinition* decompiledTypeDefinition,
                        const ITypeDefinition* potentialDisplayClass,
                        bool allowTypeImplementingInterfaces = false);

// The C# `internal static bool IsClosureParameter(IParameter parameter, ITypeDefinition
// currentTypeDefinition)` (LocalFunctionDecompiler.cs line 575, the ITypeDefinition
// overload -- the `ITypeResolveContext` overload adapts this one and is not needed by
// the ported consumer chain) -- whether the parameter is a hoisted closure variable
// passed to a local function: a by-reference parameter whose element type resolves to
// a Struct-kind definition that is a potential closure of the current type. The C#
// home is the `LocalFunctionDecompiler` IL transform (not ported); the static is a
// pure type-system function whose only ported consumer is
// `IsDefaultValueAssignmentAllowed` below, so it lands here with the same
// land-a-leaf convention. The C# `parameter.Type is not ByReferenceType brt` is a
// plain RTTI test (no modifier unwrap); the C# `brt.ElementType.GetDefinition()` may
// be null (a type without a definition). The `IsPotentialClosure` current-type
// argument is `otherParameter.Owner.DeclaringTypeDefinition` in the consumer -- the
// owner's declaring type definition, nullable.
bool IsClosureParameter(const IParameter* parameter,
                        const ITypeDefinition* currentTypeDefinition);

// The C# `public static bool IsDefaultValueAssignmentAllowed(this IParameter parameter)`
// (TypeSystemExtensions.cs line 681, the IParameter region) -- whether the parameter
// may carry a default value in decompiled output: the parameter itself must be
// optional, carry the constant value in its signature, and use a by-value / `in` /
// `ref readonly` reference kind (the C# local function
// `DefaultValueAssignmentAllowedIndividual`); and every SUBSEQUENT parameter (a later
// position in the owner's parameter list -- C# requires every parameter after an
// optional one to be optional, a `params` array, or one of the closure parameters the
// local-function transform removes) must individually allow a default, be a `params`
// array, or be a closure parameter (`IsClosureParameter` against the owner's declaring
// type definition). A parameter without an owner returns true after the individual
// check (the C# comment: "Shouldn't happen, but we need to check for it."). The C#
// `otherParameter.Owner.DeclaringTypeDefinition` derefs the subsequent parameter's
// owner unconditionally (the parameter comes from the owner's own list, so the owner
// is non-null in practice); the port guards the degenerate stub shape (a null owner or
// declaring type skips the closure check, falling through to the individual / params
// tests -- the D516 safe-fallback convention).
bool IsDefaultValueAssignmentAllowed(const IParameter& parameter);

} // namespace ILSpy::Decompiler::TypeSystem
