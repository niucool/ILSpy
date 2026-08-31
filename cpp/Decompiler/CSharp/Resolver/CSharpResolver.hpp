// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/CSharpResolver.cs lines 49-322 -- the
// class skeleton of the main resolver: the two public ctors + the private full ctor, the
// `Compilation` / `CurrentTypeResolveContext` / `CheckForOverflow` / `IsWithinLambda
// Expression` / `CurrentMember` / `CurrentUsingScope` / `CurrentTypeDefinition` property
// surface with the `With*` clone factories, the per-CurrentTypeDefinition cache, the
// `ImmutableStack`-based local-variable management, and the object-initializer context.
// The C# comment calls this class "the main resolver logic"; the 2986-line class resolves
// expressions, and every `With*` factory produces a NEW resolver (the immutable-resolver
// pattern -- the resolver state is never mutated in place; `AddVariables` /
// `PushObjectInitializer` also return clones). The C# class remark: "This class is
// thread-safe."
//
// This is the first slice of the CSharpResolver mega-class (the 2986-line long pole);
// the skeleton is fully landable now that its prerequisites are ported:
// `CSharpTypeResolveContext` / `UsingScope` (the iteration-94 pair), `CSharpConversions
// ::Get` (the per-compilation cached factory), `Util::ImmutableStack` (the IL-reader
// evaluation-stack spine), and `ErrorResolveResult::UnknownError` (the null
// object-initializer sentinel). The `Resolve*` arms land in later slices
// (`ResolveUnaryOperator` and `ResolveBinaryOperator` + the enum-handler trio, the
// Convert/ResolveCast region, the sizeof/this/base/typeof tail, the
// condition/primitive/default-value/assignment quartet, the simple-name lookup
// cluster, the extension-methods region, the member-access region, the
// invocation region, the ResolveForeach region, the ResolveIndexer region, and
// the ResolveObjectCreation region have landed; CanTransformToExtensionMethodCall
// ... follows).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `With*` clone factories return a `CSharpResolver` REFERENCE -- the C#
//      heap-allocated clone handed to the caller. The port returns an owning
//      `std::shared_ptr<CSharpResolver>` (the `UsingScope::WithNestedNamespace` /
//      `CSharpTypeResolveContext::WithUsingScope` concrete-clone-factory convention):
//      every consumer chains `With*` factories and `Resolve*` calls
//      (`resolver->WithCurrentTypeDefinition(td)->ResolveSimpleName(...)`), and the
//      shared ownership mirrors the C# GC references held by AST-node annotations.
//      Two factories return `this` when nothing would change (`WithCheckForOverflow`
//      with the same flag, `WithCurrentTypeDefinition` with the same definition) -- the
//      port implements the identity-preserving early-out through
//      `std::enable_shared_from_this` (`shared_from_this()` + `const_pointer_cast`, the
//      D529 convention), so a resolver MUST be shared-managed (`std::make_shared`) before
//      those two factories are called on it; the null weak back-reference would
//      otherwise throw `bad_weak_ptr` (the documented discipline shared by every
//      `enable_shared_from_this` port).
//  (b) The C# `public class CSharpResolver : ICodeContext` interface derivation is
//      DEFERRED (a documented shape deviation, no behavioral loss): deriving the ported
//      `ICodeContext` (TypeSystem/ICodeContext.hpp, ported with this skeleton) would
//      force the resolver's `WithCurrentTypeDefinition` / `WithCurrentMember` overrides
//      to the interface's `std::unique_ptr<ITypeResolveContext>` signature -- losing the
//      concrete `std::shared_ptr<CSharpResolver>` clone returns every future
//      `Resolve*`-arm and ExpressionBuilder port chains on. `ICodeContext` has NO other
//      C# consumer (the C# interface exists solely for `CSharpResolver` itself), and
//      the resolver already exposes the full interface contract structurally
//      (`LocalVariables()` + `IsWithinLambdaExpression()` + the four resolution slots).
//      The derivation can land later together with the first consumer that needs the
//      interface view (e.g. an adapter that wraps the shared resolver handle).
//  (c) The C# `internal readonly CSharpConversions conversions` field (read by
//      CallBuilder.cs / ExpressionBuilder.cs as `resolver.conversions`) ports to a
//      `CSharpConversions&` reference member plus a PUBLIC `Conversions()` accessor (the
//      port has no assembly-internal visibility level; the `CSharpConversions::Get`
//      factory returns a reference backed by the compilation's CacheManager, which
//      outlives the resolver because the resolver itself holds `const ICompilation&`).
//  (d) The local-variable stack is `ImmutableStack<Dictionary<string, IVariable>>` --
//      the port is `Util::ImmutableStack<std::shared_ptr<const VariableMap>>` where
//      `VariableMap` is the `Dictionary<string, IVariable>` (the values are owning
//      `shared_ptr<const IVariable>` handles; the map keys are the variable names).
//      `AddVariables` takes the CALLER'S dictionary as a `shared_ptr<const VariableMap>`
//      (the C# pushes the reference, so clones share the same dictionary -- the shared
//      handle preserves that sharing; the C# callers build a block's dictionary fully
//      before adding it, so the `const` restriction costs nothing). `LocalVariables()`
//      flattens the stack TOP-FIRST (the `ImmutableStack` enumerates LIFO, so the
//      innermost block's variables come first -- the C# `SelectMany` over the stack).
//  (e) The per-CurrentTypeDefinition cache (`TypeDefinitionCache`) and the
//      object-initializer context (`ObjectInitializerContext`) are the C# private
//      nested classes, ported as private nested classes. The cache's three
//      `Dictionary<string, ResolveResult>` lookup caches store NULLABLE results (a
//      stored null is a known-negative cache entry -- `TryGetValue(identifier, out r)`
//      can find a null), so the port maps are `std::unordered_map<std::string,
//      std::shared_ptr<ResolveResult>>` where an empty handle is the C# null. The
//      resolver holds the cache via `shared_ptr` (nullable -- null when there is no
//      current type definition) and clones SHARE the same cache instance (the C#
//      reference field), so the `LookupSimpleNameOrTypeName` arms' cache population is
//      visible across every clone sharing the cache.
//  (f) The C# `Throw` guards port per the established convention: `ArgumentNullException`
//      -> `std::invalid_argument` (the `IntersectionType::Create` precedent);
//      `InvalidOperationException` on popping an empty object-initializer stack ->
//      `std::runtime_error` (the `CreateResolveResult` precedent). The two
//      `CSharpResolver(ICompilation)` / `CSharpResolver(CSharpTypeResolveContext)`
//      ctor null guards: the compilation is a non-null reference (the D374 convention,
//      structurally unreachable), the context is a nullable `shared_ptr` parameter and
//      IS guarded.
//  (g) Namespace note: from this namespace (`ILSpy::Decompiler::CSharp::Resolver`), the
//      name `TypeSystem` resolves to the SIBLING `ILSpy::Decompiler::CSharp::TypeSystem`
//      (`CSharpTypeResolveContext` / `UsingScope` live there), NOT to
//      `ILSpy::Decompiler::TypeSystem` (`ICompilation` / `IVariable` / ... live there)
//      -- every cross-namespace reference below is fully qualified for that reason
//      (the iteration-94 MSVC namespace-reopening learning).

#pragma once

#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/NameLookupMode.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/Util/ImmutableStack.hpp"

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward-declared at GLOBAL scope (the iteration-94 MSVC namespace-reopening learning:
// a qualified namespace-definition written inside the enclosing namespace below would
// resolve against the enclosing namespace's own members): the two AST operator enums the
// resolver's operator-resolution regions consume. They are defined in the Syntax
// expression headers (the C# `CSharpResolver.cs` itself `using`s
// `ICSharpCode.Decompiler.CSharp.Syntax` for exactly these enums); a scoped-enum forward
// declaration needs only the (implicit `int`) underlying type, keeping the AST headers
// out of the resolver's include graph -- the .cpp includes the full headers for the
// switch bodies.
namespace ILSpy::Decompiler::CSharp::Syntax {
enum class UnaryOperatorType;
enum class BinaryOperatorType;
enum class AssignmentOperatorType;
}

// Forward-declared at GLOBAL scope (the same iteration-94 convention): the conversion
// description the `Convert` / `TryConvert` members take by shared handle (the C#
// `Conversion` class reference; the shared_ptr declaration needs only a forward
// declaration -- the .cpp includes the full header for the flag reads) and the BCL
// `TypeCode` the `CSharpPrimitiveCast` wrapper takes (defined in ReflectionHelper.hpp;
// a scoped-enum forward declaration needs the underlying type, the
// CSharpConversionsHelpers.hpp precedent).
namespace ILSpy::Decompiler::Semantics {
class Conversion;
class ForEachResolveResult;
class NamespaceResolveResult;
class TypeResolveResult;
}
namespace ILSpy::Decompiler::TypeSystem { enum class TypeCode : std::uint8_t; }

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (same namespace, in CSharpConversions.hpp): the conversion
// controller the resolver resolves per-compilation (`CSharpConversions::Get`) and
// exposes through `Conversions()`. A reference return/member needs only a declaration;
// the .cpp includes the full header for the `Get` call.
class CSharpConversions;

// Forward-declared (same namespace, in OverloadResolution.hpp): the overload-resolution
// engine `CreateResolveResultForUserDefinedOperator` reads the best-candidate state of.
// A reference parameter needs only a declaration; the .cpp includes the full header for
// the `BestCandidateErrors` / `CreateResolveResult` / `BestCandidate` /
// `GetArgumentsWithConversions` calls.
class OverloadResolution;

// Forward-declared (same namespace, in CSharpOperators.hpp): the operator-method class
// the `PointerArithmeticOperator` factory below builds and returns by shared handle (the
// C# `CSharpOperators.BinaryOperatorMethod`). A shared_ptr return type needs only a
// declaration; the .cpp includes the full header for the construction.
class BinaryOperatorMethod;

// The C# `Dictionary<string, IVariable>` -- one block's local variables / lambda
// parameters keyed by name. The values are owning handles; the resolver's clones share
// the same dictionary (header convention (d)).
using VariableMap = std::unordered_map<
    std::string, std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IVariable>>;

// The C# `public class CSharpResolver : ICodeContext` (the interface derivation is
// deferred, header convention (b)) -- the main resolver logic.
class CSharpResolver : public std::enable_shared_from_this<CSharpResolver> {
public:
    // The C# `public CSharpResolver(ICompilation compilation)` -- resolves the
    // conversions via the per-compilation factory and starts from a fresh context over
    // the compilation's main module. The null-compilation `ArgumentNullException` is
    // structurally unreachable through the reference parameter (the D374 convention).
    // Construct via `std::make_shared` (the `enable_shared_from_this` discipline,
    // header convention (a)).
    explicit CSharpResolver(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation);

    // The C# `public CSharpResolver(CSharpTypeResolveContext context)` -- starts from
    // the given context, building the per-type-definition cache when the context
    // carries a current type definition. The C# null-context `ArgumentNullException`
    // ports to `std::invalid_argument` (header convention (f)).
    explicit CSharpResolver(
        std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context);

    // ---- Properties -------------------------------------------------------------------------
    // The C# `public ICompilation Compilation { get; }`.
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const { return compilation_; }

    // The C# `internal readonly CSharpConversions conversions` -- exposed as a public
    // accessor (header convention (c)); the `CallBuilder` / `ExpressionBuilder` ports
    // read the resolver's conversions through it.
    CSharpConversions& Conversions() const { return conversions_; }

    // The C# `public CSharpTypeResolveContext CurrentTypeResolveContext { get; }` --
    // returns the owning handle (clones share the context through it).
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>
    CurrentTypeResolveContext() const
    {
        return context_;
    }

    // The C# `public bool CheckForOverflow { get; }` -- whether the current context is
    // checked.
    bool CheckForOverflow() const { return checkForOverflow_; }

    // The C# `public CSharpResolver WithCheckForOverflow(bool)` -- a clone with the flag
    // replaced; returns `this` (identity-preserving) when the flag is unchanged.
    std::shared_ptr<CSharpResolver> WithCheckForOverflow(bool checkForOverflow) const;

    // The C# `public bool IsWithinLambdaExpression { get;}` -- whether the resolver is
    // currently within a lambda expression or anonymous method.
    bool IsWithinLambdaExpression() const { return isWithinLambdaExpression_; }

    // The C# `public CSharpResolver WithIsWithinLambdaExpression(bool)` -- a clone with
    // the flag replaced (no identity-preserving early-out in the C#).
    std::shared_ptr<CSharpResolver> WithIsWithinLambdaExpression(bool isWithinLambdaExpression) const;

    // The C# `public IMember CurrentMember { get; }` -- the current member definition
    // used to look up identifiers as parameters or type parameters (delegates to the
    // context's slot; nullable).
    const ILSpy::Decompiler::TypeSystem::IMember* CurrentMember() const
    {
        return context_->CurrentMember();
    }

    // The C# `public CSharpResolver WithCurrentMember(IMember member)` -- a clone whose
    // context has the member slot replaced (the C# remark: "Don't forget to also set
    // CurrentTypeDefinition when setting CurrentMember; setting one of the properties
    // does not automatically set the other"). The nullable C# parameter ports to a
    // nullable pointer (pass `nullptr` to clear).
    std::shared_ptr<CSharpResolver> WithCurrentMember(
        const ILSpy::Decompiler::TypeSystem::IMember* member) const;

    // The C# `public UsingScope CurrentUsingScope { get; }` -- the current using scope
    // used to look up identifiers as class names (delegates to the context's slot;
    // nullable).
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> CurrentUsingScope() const
    {
        return context_->CurrentUsingScope();
    }

    // The C# `public CSharpResolver WithCurrentUsingScope(UsingScope usingScope)` -- a
    // clone whose context has the using-scope slot replaced (nullable handle).
    std::shared_ptr<CSharpResolver> WithCurrentUsingScope(
        std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope) const;

    // The C# `public ITypeDefinition CurrentTypeDefinition { get; }` -- the current type
    // definition (delegates to the context's slot; nullable).
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition() const
    {
        return context_->CurrentTypeDefinition();
    }

    // The C# `public CSharpResolver WithCurrentTypeDefinition(ITypeDefinition
    // typeDefinition)` -- a clone whose context has the type-definition slot replaced
    // AND a freshly built per-type-definition cache (or a null cache when clearing);
    // returns `this` (identity-preserving) when the definition is unchanged.
    std::shared_ptr<CSharpResolver> WithCurrentTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition) const;

    // ---- Local Variable Management ----------------------------------------------------------
    // The C# `public CSharpResolver AddVariables(Dictionary<string, IVariable>
    // variables)` -- adds new variables or lambda parameters to the current block (the
    // caller's dictionary, shared with the clone -- header convention (d)).
    std::shared_ptr<CSharpResolver> AddVariables(
        std::shared_ptr<const VariableMap> variables) const;

    // The C# `public IEnumerable<IVariable> LocalVariables` -- all currently visible
    // local variables and lambda parameters (not method parameters), innermost block
    // first. Returns an owning snapshot.
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IVariable>> LocalVariables() const;

    // ---- Object Initializer Context ---------------------------------------------------------
    // The C# `public CSharpResolver PushObjectInitializer(ResolveResult
    // initializedObject)` -- pushes the object that is currently being initialized. The
    // C# null-argument `ArgumentNullException` ports to `std::invalid_argument` for a
    // null handle (header convention (f)).
    std::shared_ptr<CSharpResolver> PushObjectInitializer(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initializedObject) const;

    // The C# `public CSharpResolver PopObjectInitializer()` -- pops the innermost
    // object initializer. The C# empty-stack `InvalidOperationException` ports to
    // `std::runtime_error` (header convention (f)).
    std::shared_ptr<CSharpResolver> PopObjectInitializer() const;

    // The C# `public bool IsInObjectInitializer { get; }` -- whether this context is
    // within an object initializer.
    bool IsInObjectInitializer() const { return objectInitializerStack_ != nullptr; }

    // The C# `public ResolveResult CurrentObjectInitializer { get; }` -- the current
    // object initializer (usually an `InitializedObjectResolveResult` or a semantic tree
    // based on one); the `ErrorResolveResult.UnknownError` singleton when there is no
    // object initializer.
    const ILSpy::Decompiler::Semantics::ResolveResult& CurrentObjectInitializer() const;

    // The C# `public IType CurrentObjectInitializerType { get; }` -- the type of the
    // object currently being initialized (the C# `SharedTypes.Unknown` when no
    // initializer is open -- the null-initializer sentinel's `UnknownType`).
    const ILSpy::Decompiler::TypeSystem::IType& CurrentObjectInitializerType() const
    {
        return CurrentObjectInitializer().Type();
    }

    // ---- User-Defined Operator Candidates ---------------------------------------------------
    // (The `Get user-defined operator candidates` region, CSharpResolver.cs lines
    // 1278-1322, plus the two `GetOverloadableOperatorName` statics at lines 566-583 /
    // 1207-1241 -- the shared prerequisite machinery the `ResolveUnaryOperator` and
    // `ResolveBinaryOperator` regions consume; those regions land in later slices.)

    // The C# `static string GetOverloadableOperatorName(UnaryOperatorType op)` (line 567)
    // -- the metadata method name of the overloadable unary operator (`op_Increment` /
    // `op_Decrement` for the pre- AND post-forms), or the C# `null` for the
    // non-overloadable kinds (Any / Dereference / AddressOf / Await / the
    // null-conditional family / the pattern kinds). Private static in the C#; PUBLIC
    // static in the port for direct TDD (the port has no assembly-internal visibility
    // level -- the same widening applied to the CSharpOperators `internal` members).
    // The C# nullable string return ports to a `const char*` where `nullptr` is the C#
    // `null` (the fixed literal table, the `BinaryOperatorExpression::GetOperatorToken`
    // convention); the future `ResolveUnaryOperator` region's
    // `overloadableOperatorName == null` check ports to `name == nullptr`.
    static const char* GetOverloadableOperatorName(
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op);

    // The C# `static string GetOverloadableOperatorName(BinaryOperatorType op)` (line
    // 1208) -- the metadata method name of the overloadable binary operator (the 16
    // arithmetic / bitwise / shift / comparison names), or the C# `null` for the
    // non-overloadable kinds (Any / ConditionalAnd / ConditionalOr / NullCoalescing /
    // Range / IsPattern). Private static in the C#; PUBLIC static in the port (the unary
    // overload above).
    static const char* GetOverloadableOperatorName(
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op);

    // The C# `public IEnumerable<IParameterizedMember> GetUserDefinedOperatorCandidates(
    // IType type, string operatorName)` (line 1280) -- the candidate user-defined
    // operators (C# spec draft-v11 section 12.4.6) for the given operand type and
    // metadata operator name: the type's own `IsOperator` methods with the matching name,
    // each liftable one followed by its lifted `Nullable<T>` form (the originals all come
    // first, then the lifted forms in the same order). The primitive operand types are
    // EXCLUDED (the `TypeCode` [Boolean..Decimal] gate) even when the .NET framework
    // exposes built-in operators with those metadata names -- "we must not use those as
    // user-defined operators (we would skip numeric promotion)". The C# declared element
    // type is `IParameterizedMember` but every element is an `IMethod` (the early outs
    // return the `EmptyList<IMethod>.Instance` and the list is a `List<IMethod>`), so the
    // port returns `std::shared_ptr<IMethod>` elements directly.
    //
    // The C# `List<IMethod>` holds GC references with no ownership distinction -- the
    // originals are owned by the type system while the appended lifted forms are freshly
    // allocated `LiftedUserDefinedOperator` instances the list must keep alive. The
    // port's vector unifies the two: the originals are stored as NON-OWNING aliases (a
    // no-op deleter -- the type system owns them for the compilation's lifetime, the
    // `KnownTypeCache::SearchType` non-owning-alias convention; the `const_cast`
    // reconciles the `GetMethods` const-return contract, the D515 convention), while
    // the lifted forms are the OWNING handles `CSharpOperators::LiftUserDefinedOperator`
    // returns. A caller feeding the entries to `OverloadResolution::AddCandidate` must
    // hold the vector (and, per the engine's raw-pointer convention, the compilation) for
    // as long as the resolution state is read -- the same discipline every
    // `OverloadResolutionCandidate` caller already follows.
    //
    // The C# nullable `string operatorName` parameter ports to a `const char*` (nullptr
    // is the C# `null`; the empty-list early out). Reads no resolver instance state (only
    // `ReflectionHelper.GetTypeCode`, `IType::GetMethods`, and the static
    // `CSharpOperators::LiftUserDefinedOperator`), so the method is `const`.
    std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod>>
    GetUserDefinedOperatorCandidates(
        const ILSpy::Decompiler::TypeSystem::IType& type, const char* operatorName) const;

    // The C# `ResolveResult CreateResolveResultForUserDefinedOperator(OverloadResolution
    // r, ExpressionType operatorType)` (line 1309) -- the resolve result for an operator
    // resolved to a USER-DEFINED operator method: the error path delegates to
    // `r.CreateResolveResult(null)` (the invocation result carrying the applicability
    // errors), the success path builds an `OperatorResolveResult` over the best
    // candidate's method (the `ILiftedOperator` cross-cast marking the lifted forms),
    // with the conversion-wrapped operands. Private instance method in the C#; PUBLIC
    // STATIC in the port -- it reads no resolver instance state (only the
    // `OverloadResolution` public surface), and the widening makes the region directly
    // testable ahead of the `ResolveUnaryOperator` / `ResolveBinaryOperator` consumers
    // (the CSharpOperators `internal`-widening convention).
    static std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
    CreateResolveResultForUserDefinedOperator(
        ILSpy::Decompiler::CSharp::Resolver::OverloadResolution& r,
        ILSpy::Decompiler::TypeSystem::ExpressionType operatorType);

    // ---- Convert / ResolveCast ----------------------------------------------------------------
    // (The `ResolveCast` region, CSharpResolver.cs lines 1319-1470, plus the private
    // `GetEnumUnderlyingType` member from the "Enum helper methods" region at line 985 --
    // the conversion-application machinery the operator-resolution slices consume:
    // `TryConvert` / `TryConvertEnum` rebind an operand through an implicit conversion in
    // place, `Convert` wraps or constant-folds, `ResolveCast` is the public
    // cast-expression entry (the future `ResolveCastExpression` arm delegates to it),
    // and the `CSharpPrimitiveCast` wrapper threads the resolver's `CheckForOverflow`
    // flag into the `Util` constant converter.)

    // The C# `bool TryConvert(ref ResolveResult rr, IType targetType)` (line 1320) -- if
    // an implicit conversion of `rr` to `targetType` exists, applies it to `rr` and
    // returns true; otherwise returns false and leaves `rr` unmodified. Private instance
    // method in the C#; PUBLIC in the port for direct TDD ahead of the
    // `ResolveUnaryOperator` / `ResolveBinaryOperator` slices that consume it (the
    // CSharpOperators internal-widening convention -- the port has no visibility level
    // between the public surface and the untestable private one). The C# `ref ResolveResult
    // rr` ports to `std::shared_ptr<ResolveResult>&` -- the caller's rebindable variable
    // (the C# reassigns the caller's variable through the `ref`; the `rr = Convert(...)`
    // rebind is the port's shared_ptr assignment). Reads only `conversions_`, so the
    // method is `const`.
    bool TryConvert(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr,
                    ILSpy::Decompiler::TypeSystem::IType& targetType) const;

    // The C# `bool TryConvertEnum(ref ResolveResult rr, IType targetType, ref bool
    // isNullable, ref ResolveResult enumRR, bool allowConversionFromConstantZero = true)`
    // (line 1341) -- the enum-aware TryConvert: tries the non-nullable target first
    // (skipped when `isNullable` is already true), then rebinds the TARGET to its
    // `Nullable<T>` form (a LOCAL rebind -- the C# parameter is by-value, so the caller's
    // `targetType` is untouched; a C++ reference cannot rebind, so the port threads the
    // rebound target through a local `IType*`) and retries; on the nullable success also
    // wraps `enumRR` in the `ImplicitNullableConversion` unless it is already nullable,
    // and sets `isNullable = true`. The `allowConversionFromConstantZero` gate (default
    // true) rejects ENUMERATION conversions -- the implicit constant-0-to-enum conversion
    // -- when false (the user-defined-operator comparison context where the C# compiler
    // does not apply the constant-zero rule). Private in the C#; PUBLIC in the port for
    // direct TDD (the TryConvert widening convention). `rr` / `isNullable` / `enumRR` are
    // the C# `ref` parameters (the callers' rebindable variables); on failure all three
    // are left unmodified.
    bool TryConvertEnum(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr,
                        ILSpy::Decompiler::TypeSystem::IType& targetType,
                        bool& isNullable,
                        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& enumRR,
                        bool allowConversionFromConstantZero = true) const;

    // The C# `ResolveResult Convert(ResolveResult rr, IType targetType)` (line 1381) --
    // the convenience overload resolving the implicit conversion first, then delegating
    // to the 3-arg `Convert`. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> Convert(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr,
        ILSpy::Decompiler::TypeSystem::IType& targetType) const;

    // The C# `ResolveResult Convert(ResolveResult rr, IType targetType, Conversion c)`
    // (line 1386) -- the conversion application: an IDENTITY conversion returns `rr`
    // unwrapped (pointer identity -- the C# `c == Conversion.IdentityConversion`
    // reference comparison ports to singleton pointer identity, the D536 convention); a
    // COMPILE-TIME CONSTANT under a non-None non-user-defined conversion is
    // CONSTANT-FOLDED through `ResolveCast` (the constant re-resolves through the target
    // type); everything else wraps in a `ConversionResolveResult` carrying the resolver's
    // `checkForOverflow` flag. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> Convert(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr,
        ILSpy::Decompiler::TypeSystem::IType& targetType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> c) const;

    // The C# `public ResolveResult ResolveCast(IType targetType, ResolveResult
    // expression)` (line 1396, C# spec draft-v11 section 12.9.8 Cast expressions) -- the
    // cast-expression entry: resolves the EXPLICIT conversion first, then constant-folds
    // a compile-time constant under a non-user-defined conversion -- through the target's
    // enum-underlying `TypeCode` via `CSharpPrimitiveCast` (an `OverflowException` /
    // `InvalidCastException` downgrades to an `ErrorResolveResult`), with the
    // `string`-target passthrough (a null or string constant stays a constant; any other
    // constant to `string` is an error) and the native-integer arm (an `nint` / `nuint`
    // target casts the constant through the 32-bit code with `checkForOverflow: true` --
    // a C# HARDCODED flag, NOT the resolver's own -- and an overflow falls back to the
    // non-constant `ConversionResolveResult` because "the conversion is not a compile-time
    // constant"). A USER-DEFINED conversion or a non-constant expression skips the
    // folding entirely and wraps. `expression` is an owning handle (the C# reference the
    // GC owns; every fold path re-derives the folded constant from it, every wrap path
    // stores it as the `ConversionResolveResult` input).
    //
    // NOTE: the `targetType.GetEnumUnderlyingType()` call (line 1403) resolves to the
    // TypeUtils EXTENSION method (the receiver is the `IType` -- the resolver's own
    // same-name member below never applies to an `IType` receiver), so the port calls the
    // TypeUtils free function (iteration 99), NOT the member. The distinction is
    // observable: for a non-enum target the extension returns the target itself (the
    // folding reads the target's own `TypeCode`), while the member would return null (a
    // definition's `EnumUnderlyingType` is null for non-enums).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveCast(
        ILSpy::Decompiler::TypeSystem::IType& targetType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // The C# `internal object CSharpPrimitiveCast(TypeCode targetType, object input)`
    // (line 1467) -- the wrapper threading the resolver's `CheckForOverflow` flag into
    // the `Util` constant converter. Internal in the C#; PUBLIC in the port (the
    // assembly-internal widening convention). The C# `object` ports to `std::any` (the
    // D424 boxed-constant convention).
    std::any CSharpPrimitiveCast(ILSpy::Decompiler::TypeSystem::TypeCode targetType,
                                 const std::any& input) const;

    // The C# private `IType GetEnumUnderlyingType(IType enumType)` (line 985, the "Enum
    // helper methods" region) -- the resolver MEMBER variant: the definition's
    // `EnumUnderlyingType` when the definition resolves, else the `SpecialType.UnknownType`
    // null object. NOT the TypeUtils extension -- C# member lookup finds the private
    // member for the UNQUALIFIED `GetEnumUnderlyingType(enumType)` calls inside the class
    // (the enum-comparison handlers and the binary numeric promotions, later slices),
    // shadowing the extension; the extension applies only where the call is QUALIFIED on
    // an `IType` receiver, as in `ResolveCast` above. Unlike the extension, the member
    // does NOT unwrap custom modifiers and does NOT pass a non-enum through: a
    // definition-bearing NON-enum reports a NULL `EnumUnderlyingType` (the faithful C#
    // `ITypeDefinition.EnumUnderlyingType` for non-enums), a definitionless type reports
    // `UnknownType`. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention). The return is non-owning (the definition owns the
    // underlying handle, reachable through `enumType`; the `UnknownType` fallback is a
    // program-lifetime static singleton -- the minimal port's `UnknownType()` allocates a
    // fresh instance per call, so the singleton must be materialized once here).
    const ILSpy::Decompiler::TypeSystem::IType* GetEnumUnderlyingType(
        const ILSpy::Decompiler::TypeSystem::IType& enumType) const;

    // ---- Simple-name lookup -------------------------------------------------------------------
    // (The `ResolveSimpleName` region, CSharpResolver.cs lines 1462-1790: the
    // simple-name / type-name lookup cluster -- `ResolveSimpleName` +
    // `LookupSimpleNameOrTypeName` + `IsVariableReferenceWithSameType` + the private
    // `LookInCurrentType` / `LookInCurrentUsingScope` / `LookInUsingScopeNamespace` /
    // `TopLevelTypeDefinitionIsAccessible` / `ResolveExternAlias` helpers -- plus the
    // two `CreateMemberLookup` factories from the `ResolveMemberAccess` region (lines
    // 1887-1910) that `LookInCurrentType` consumes. Every prerequisite is already
    // ported: the local-variable stack and the `TypeDefinitionCache` (the skeleton),
    // `ResolveThisReference` (the sizeof/this/base/typeof region), `MemberLookup::
    // Lookup` / `LookupType` (D500), the `UsingScope` surface incl. the `ResolveCache`
    // (the iteration-94 pair), `INamespace::GetChildNamespace` / `GetTypeDefinition`,
    // `ICompilation::RootNamespace` / `GetNamespaceForExternAlias`, `IModule::
    // InternalsVisibleTo`, and the `LocalResolveResult` / `TypeResolveResult` /
    // `NamespaceResolveResult` / `AmbiguousTypeResolveResult` /
    // `UnknownIdentifierResolveResult` / `UnknownMemberResolveResult` result classes.)

    // The C# `public ResolveResult ResolveSimpleName(string identifier,
    // IReadOnlyList<IType> typeArguments, bool isInvocationTarget = false)` (line 1463,
    // C# 4.0 spec section 7.6.2 Simple Names) -- the expression-entry convenience
    // delegating to `LookupSimpleNameOrTypeName` with the `Expression` /
    // `InvocationTarget` mode. The C# `string` / `IReadOnlyList<IType>` parameters port
    // to a `std::string` value and a `std::vector<ITypePtr>` value (the C# null guards
    // are structurally unreachable through the value types, the D374 convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveSimpleName(
        std::string identifier,
        std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
        bool isInvocationTarget = false) const;

    // The C# `public ResolveResult LookupSimpleNameOrTypeName(string identifier,
    // IReadOnlyList<IType> typeArguments, NameLookupMode lookupMode)` (line 1473, C#
    // 4.0 spec sections 3.8 + 7.6.2) -- the full simple-name/type-name lookup: the
    // local variables and current-member parameters (Expression/InvocationTarget modes,
    // no type arguments), the current method's type parameters, the per-current-type-
    // definition cache (the three `TypeDefinitionCache` dictionaries, storing NULLABLE
    // results -- an empty handle is the C# known-negative `null` entry), the current
    // type and its declaring types (`LookInCurrentType`), the using-scope chain
    // (`LookInCurrentUsingScope`, with the per-scope `ResolveCache` memoization for the
    // no-type-arguments non-using-declaration shapes), the global namespace when no
    // scope is set, the `dynamic` keyword, and the `UnknownIdentifierResolveResult`
    // fallback. The C# `lock (cache)` guards around the `TypeDefinitionCache`
    // dictionaries are elided (a thread-safety measure with no single-threaded
    // behavioral effect; the `UsingScope::ResolveCacheMap` keeps its mutex because the
    // C# `ConcurrentDictionary` contract is its own surface). The `ShallowClone` on
    // every cache hit ports to the `unique_ptr`-to-`shared_ptr` conversion (the clone
    // preserves the runtime type, the D424 convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> LookupSimpleNameOrTypeName(
        std::string identifier,
        std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
        NameLookupMode lookupMode) const;

    // The C# `public bool IsVariableReferenceWithSameType(ResolveResult rr, string
    // identifier, out TypeResolveResult trr)` (line 1604) -- whether `rr` (a member or
    // local-variable result) has the same type as the TYPE the identifier resolves to
    // (the `Type`-mode lookup): the C# pattern the CSharpResolver uses to decide
    // whether an identifier in an expression position refers to a type or a variable
    // of the same name. The C# `out TypeResolveResult trr` ports to a `shared_ptr&`
    // out-param reset to null at the top (the `TaskType::IsCustomTask` out-param
    // convention); the C# `as TypeResolveResult` ports to `dynamic_pointer_cast`.
    bool IsVariableReferenceWithSameType(
        const ILSpy::Decompiler::Semantics::ResolveResult& rr,
        const std::string& identifier,
        std::shared_ptr<ILSpy::Decompiler::Semantics::TypeResolveResult>& trr) const;

    // The C# `public MemberLookup CreateMemberLookup()` (line 1887, the
    // `ResolveMemberAccess` region) -- the member-lookup factory over the resolver's
    // current settings: the current type definition, the compilation's main module,
    // and the enum-member-initializer flag (a field member inside an enum type).
    // `MemberLookup` is a copyable value object, so the factory returns it BY VALUE
    // (the C# heap allocation is an implementation detail). Landed with this region
    // because `LookInCurrentType` consumes it.
    MemberLookup CreateMemberLookup() const;

    // The C# `public MemberLookup CreateMemberLookup(NameLookupMode lookupMode)` (line
    // 1899) -- the mode-aware factory: a `BaseTypeReference` lookup treats the resolver
    // as being OUTSIDE the current type definition for accessibility purposes (the
    // C# remark: this avoids a stack overflow when referencing a protected class
    // nested inside the base class of a parent class --
    // NameLookupTests.InnerClassInheritingFromProtectedBaseInnerClassShouldNotCauseStackOverflow).
    MemberLookup CreateMemberLookup(NameLookupMode lookupMode) const;

    // The C# `public ResolveResult ResolveAlias(string identifier)` (line 1760) --
    // looks up an alias (the identifier in front of a `::` operator): the `global`
    // keyword yields the compilation's root namespace, then the using-scope chain's
    // extern aliases and using aliases. The port's `UsingScope` tracks no aliases (the
    // always-empty `ExternAliases` / `UsingAliases` surface), so only the `global` arm
    // and the `ErrorResult` fallback are reachable -- the loop is kept faithful for the
    // day the alias tracking lands.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveAlias(
        const std::string& identifier) const;

    // The C# `ResolveResult LookInCurrentType(string identifier, IReadOnlyList<IType>
    // typeArguments, NameLookupMode lookupMode, bool parameterizeResultType)` (line
    // 1621) -- the current type definition and its declaring types: the declared type
    // parameters (including those copied from outer classes, so the version with the
    // correct owner wins), then the member lookup (`Lookup` against a `this` /
    // type-reference target for the expression modes, `LookupType` for the type
    // modes), skipping the current type itself for a `BaseTypeReference` lookup, and
    // skipping past `UnknownMemberResolveResult` (but returning
    // `AmbiguousMemberResolveResult`). Private in the C#; PUBLIC in the port for direct
    // TDD (the TryConvert widening convention). Returns a NULLABLE handle (the empty
    // `shared_ptr` is the C# `null` not-found).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> LookInCurrentType(
        const std::string& identifier,
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
        NameLookupMode lookupMode,
        bool parameterizeResultType) const;

    // The C# `ResolveResult LookInCurrentUsingScope(string identifier,
    // IReadOnlyList<IType> typeArguments, bool isInUsingDeclaration, bool
    // parameterizeResultType)` (line 1668) -- the using-scope chain, innermost first:
    // the scope's own namespace (`LookInUsingScopeNamespace`), the extern/using aliases
    // (no type arguments; the using declaration's own scope skips its own aliases),
    // then the imported namespaces' types (the first accessible result wins; a SECOND
    // accessible type in a different imported namespace is an `AmbiguousTypeResolveResult`).
    // Private in the C#; PUBLIC in the port for direct TDD (the TryConvert widening
    // convention). Returns a nullable handle.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> LookInCurrentUsingScope(
        const std::string& identifier,
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
        bool isInUsingDeclaration,
        bool parameterizeResultType) const;

    // The C# `ResolveResult LookInUsingScopeNamespace(UsingScope usingScope,
    // INamespace n, string identifier, IReadOnlyList<IType> typeArguments, bool
    // parameterizeResultType)` (line 1707) -- one namespace: the child namespace (no
    // type arguments; an alias of the same name makes it ambiguous), then the type
    // definition (accessibility-gated; parameterized when requested and type arguments
    // are present; an alias of the same name makes it ambiguous). Both C# parameters
    // are nullable, so the port takes nullable pointers. Private in the C#; PUBLIC in
    // the port for direct TDD (the TryConvert widening convention). Returns a nullable
    // handle.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> LookInUsingScopeNamespace(
        const ILSpy::Decompiler::CSharp::TypeSystem::UsingScope* usingScope,
        const ILSpy::Decompiler::TypeSystem::INamespace* n,
        const std::string& identifier,
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
        bool parameterizeResultType) const;

    // The C# `bool TopLevelTypeDefinitionIsAccessible(ITypeDefinition typeDef)` (line
    // 1748) -- an `Internal` type is accessible only when its parent module grants
    // internals to the compilation's main module (`InternalsVisibleTo`); every other
    // accessibility is accessible. The C# parameter is a non-null reference, but the
    // `LookInCurrentUsingScope` call sites pass `GetDefinition()` results that are
    // null for degenerate definitionless types (where the C# would NRE), so the port
    // takes the nullable pointer with the documented not-accessible safe fallback (the
    // D516 convention). Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    bool TopLevelTypeDefinitionIsAccessible(
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDef) const;

    // The C# `ResolveResult ResolveExternAlias(string alias)` (line 1784) -- resolves
    // an extern alias to its namespace through `ICompilation.GetNamespaceForExternAlias`,
    // falling back to `ErrorResult`. Private in the C#; PUBLIC in the port for direct
    // TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveExternAlias(
        const std::string& alias) const;

    // ---- Extension methods --------------------------------------------------------------------
    // (The `GetExtensionMethods` region, CSharpResolver.cs lines 2018-2243: the two
    // public `GetExtensionMethods` filter entries (lines 2036/2065), the two static
    // `IsEligibleExtensionMethod` eligibility checks (lines 2123/2133), the private
    // `GetAllExtensionMethods` scope-chain walk (the `UsingScope.AllExtensionMethods`
    // LazyInit memoization field the iteration-94 pair landed), and the private
    // `GetExtensionMethods(MemberLookup, INamespace)` namespace scan. The region feeds
    // the `ResolveMemberAccess` arm (line 1834: the `UnknownMemberResolveResult`
    // extension-method fallback, landed with the member-access region) and the
    // `ResolveInvocation` region's extension-method resolution. Every dependency is
    // already ported: `CreateMemberLookup` (this region above), `MemberLookup::
    // IsAccessible`, `ITypeDefinition::IsStatic`/`HasExtensions`/`Methods`, `IMethod::
    // IsExtensionMethod`, `IMember::Specialize`, `Detail::InferTypeArguments` (the
    // TypeInference engine), `Detail::ValidateConstraints`, the cached public
    // `CSharpConversions::ImplicitConversion(IType, IType)`, and the `Conversion`
    // flag surface.)

    // The C# `public List<List<IMethod>> GetExtensionMethods(string name = null,
    // IReadOnlyList<IType> typeArguments = null)` (line 2036) -- the no-target thin
    // delegate ("all extension methods in the current context", eligibility-free since
    // `IsEligibleExtensionMethod` returns true for a null target). The C# nullable
    // `name`/`typeArguments` default-arguments port to `std::optional` parameters
    // (`nullopt` is the C# `null`, a present value is the non-null argument); the C#
    // `List<List<IMethod>>` ports to a `std::vector` of groups of NON-OWNING method
    // pointers (the `UsingScope::AllExtensionMethods` element convention -- the
    // compilation owns the methods, the caller holds raw handles).
    std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>
    GetExtensionMethods(
        const std::optional<std::string>& name = std::nullopt,
        const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& typeArguments
        = std::nullopt) const;

    // The C# `public List<List<IMethod>> GetExtensionMethods(IType targetType, string
    // name = null, IReadOnlyList<IType> typeArguments = null, bool
    // substituteInferredTypes = false)` (line 2065) -- the extension-method filter:
    // every group from `GetAllExtensionMethods` (grouped by using scope, innermost
    // first), filtered per method by name, accessibility, and `IsEligibleExtensionMethod`
    // (against the `this`-argument target type). A NON-EMPTY `typeArguments` list
    // re-specializes every arity-matching method with the explicit arguments and checks
    // the eligibility of the SPECIALIZED form (no inference); the empty/null shape runs
    // inference (`useTypeInference: true`) and, with `substituteInferredTypes`, stores
    // the method specialized over the INFERRED arguments. The C# nullable `targetType`
    // ports to a nullable pointer (a null target is eligible for every method).
    std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>
    GetExtensionMethods(
        const ILSpy::Decompiler::TypeSystem::IType* targetType,
        const std::optional<std::string>& name,
        const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& typeArguments,
        bool substituteInferredTypes) const;

    // The C# `public static bool IsEligibleExtensionMethod(IType targetType, IMethod
    // method, bool useTypeInference, out IType[] outInferredTypes)` (line 2123) -- the
    // public static entry: resolves the compilation from the METHOD's own compilation
    // and the `CSharpConversions` via `CSharpConversions::Get`. The C# `out IType[]
    // outInferredTypes` (null unless at least one type argument was inferred) ports to
    // an `std::optional<std::vector<ITypePtr>>&` out-param reset to `nullopt` at the top
    // (the TaskType::IsCustomTask out-param convention). The two C# `ArgumentNullException`
    // guards compile out (the `IMethod&` reference cannot bind to null, the D374
    // convention; the null `targetType` is a documented ELIGIBLE shape, not an error).
    static bool IsEligibleExtensionMethod(
        const ILSpy::Decompiler::TypeSystem::IType* targetType,
        const ILSpy::Decompiler::TypeSystem::IMethod& method,
        bool useTypeInference,
        std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& outInferredTypes);

    // The C# `static bool IsEligibleExtensionMethod(ICompilation compilation,
    // CSharpConversions conversions, IType targetType, IMethod method, bool
    // useTypeInference, out IType[] outInferredTypes)` (line 2133) -- the eligibility
    // body: the `this`-parameter type (with the `this in`/`this ref` ByReference
    // unwrap), the generic-method type inference over the single target-type argument
    // (a fresh `TypeInference` with the CSharp4 default algorithm, the inferred types
    // substituting the method type parameters in the final `ImplicitConversion`), the
    // per-inferred-argument constraint validation, and the conversion verdict (valid AND
    // identity/reference/boxing/implicit-span). Private in the C#; PUBLIC in the port
    // for direct TDD (the TryConvert widening convention). The C# `inferredTypes[i] =
    // method.TypeParameters[i]` fix-up leaves the UNINFERRED positions as the method's
    // own type parameters (the substitution keeps them intact); the C# substitution
    // aliases the `inferredTypes` ARRAY (the fix-ups are visible to every later use), so
    // the port re-constructs the substitution at each use from the vector's CURRENT
    // state -- reproducing the aliasing exactly.
    static bool IsEligibleExtensionMethod(
        const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
        CSharpConversions& conversions,
        const ILSpy::Decompiler::TypeSystem::IType* targetType,
        const ILSpy::Decompiler::TypeSystem::IMethod& method,
        bool useTypeInference,
        std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& outInferredTypes);

    // The C# `IList<List<IMethod>> GetAllExtensionMethods(MemberLookup lookup)` (line
    // 2188) -- ALL extension methods available in the current using scope (grouped by
    // scope, innermost first, INCLUDING inaccessible methods): the memoized
    // `UsingScope.AllExtensionMethods` LazyInit field when already computed, else the
    // scope-chain walk (the scope's own namespace, then its DISTINCT imported
    // namespaces) stored back into the field (the `LazyInit.GetOrSet` first-writer-wins
    // contract). A resolver with NO current using scope yields the shared empty list.
    // The C# `IList<List<IMethod>>` return ports to the field's own shared-handle type
    // (the memoized handle IS the returned value).
    std::shared_ptr<std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>>
    GetAllExtensionMethods(const MemberLookup& lookup) const;

    // The C# `IEnumerable<IMethod> GetExtensionMethods(MemberLookup lookup, INamespace
    // ns)` (line 2213) -- the per-namespace scan: the namespace's types filtered by
    // `IsStatic && HasExtensions && TypeParameters.Count == 0 && IsAccessible`, then
    // their `IsExtensionMethod` methods. Private in the C#; PUBLIC in the port for
    // direct TDD (the TryConvert widening convention). The C# `IEnumerable<IMethod>`
    // ports to a non-owning pointer snapshot (the `INamespace::Types` convention).
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> GetExtensionMethods(
        const MemberLookup& lookup,
        const ILSpy::Decompiler::TypeSystem::INamespace& ns) const;

    // ---- Member access ------------------------------------------------------------------------
    // (The `ResolveMemberAccess` region, CSharpResolver.cs lines 1795-1912: the
    // `ResolveMemberAccess` entry (line 1795, C# 4.0 spec section 7.6.4) + the private
    // `ResolveMemberAccessOnNamespace` helper (line 1877) + `ResolveIdentifierInObject
    // Initializer` (line 1906). Every prerequisite is already ported: the two
    // `CreateMemberLookup` factories (the simple-name region), `MemberLookup::Lookup` /
    // `LookupType` (D500), `NamespaceResolveResult` + `INamespace::GetChildNamespace` /
    // `GetTypeDefinition`, `DynamicMemberResolveResult`, the `GetExtensionMethods`
    // filter entry (this region above), `MethodGroupResolveResult` with the WIRED
    // `extensionMethods`/`resolver` fields, and `TypeResolveResult` /
    // `ParameterizedType`.)

    // The C# `public ResolveResult ResolveMemberAccess(ResolveResult target, string
    // identifier, IReadOnlyList<IType> typeArguments, NameLookupMode lookupMode =
    // NameLookupMode.Expression)` (line 1795, C# 4.0 spec section 7.6.4 -- member
    // access). The namespace-target delegation, the dynamic-target short-circuit, the
    // mode-switched member lookup (`Lookup` for the expression modes, `LookupType` for
    // the type modes, which skip the `UnknownMemberResolveResult`/
    // `MethodGroupResolveResult` processing that is only relevant for expressions), the
    // `UnknownMemberResolveResult` extension-method fallback (a fresh
    // `MethodGroupResolveResult` over the target with the `extensionMethods` set -- the
    // C# comment: ALL extension methods, not just the eligible ones, since proper
    // eligibility checking is only possible for the full invocation), and the
    // `MethodGroupResolveResult` resolver attachment (`mgrr.resolver = this`, making
    // `MethodGroupResolveResult.GetExtensionMethods()` work on demand). The C#
    // `ResolveResult`/`string`/`IReadOnlyList<IType>` parameters port to a `shared_ptr`
    // value, a `std::string` value, and a `std::vector<ITypePtr>` value (taken by value:
    // the fallback construction moves them into the fresh method group). The method is
    // `const` (it mutates the RESULT -- attaching the resolver -- never the resolver
    // itself; the `dynamic_pointer_cast` yields a mutable handle to the shared result).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveMemberAccess(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
        std::string identifier,
        std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
        NameLookupMode lookupMode = NameLookupMode::Expression) const;

    // The C# `ResolveResult ResolveMemberAccessOnNamespace(NamespaceResolveResult nrr,
    // string identifier, IReadOnlyList<IType> typeArguments, bool parameterizeResultType)`
    // (line 1877) -- the namespace-target member access: the child namespace (no type
    // arguments), then the type definition (parameterized with the given type arguments
    // when `parameterizeResultType` and any are present), else the `ErrorResult`. Private
    // in the C#; PUBLIC in the port for direct TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveMemberAccessOnNamespace(
        const ILSpy::Decompiler::Semantics::NamespaceResolveResult& nrr,
        const std::string& identifier,
        const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
        bool parameterizeResultType) const;

    // The C# `public ResolveResult ResolveIdentifierInObjectInitializer(string
    // identifier)` (line 1906) -- the identifier lookup against the current object
    // initializer target (`memberLookup.Lookup(this.CurrentObjectInitializer, identifier,
    // EmptyList<IType>.Instance, false)`). The C# `string` ports to a `std::string` value.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveIdentifierInObjectInitializer(
        std::string identifier) const;

    // ---- Invocation ---------------------------------------------------------------------------
    // (The `ResolveInvocation` region, CSharpResolver.cs lines 2227-2443: the private
    // `AddArgumentNamesIfNecessary` helper (line 2227), the private 4-arg
    // `ResolveInvocation` core (line 2244, C# 4.0 spec section 7.6.5) + the public
    // 3-arg entry (line 2336, delegating with `allowOptionalParameters: true`), and
    // the `CreateParameters` (line 2348) + static `GuessParameterName` (line 2398) +
    // static `MakeParameterName` (line 2426) helper trio. Every prerequisite is
    // already ported: `CreateOverloadResolution` (the operator-helpers region),
    // `MethodGroupResolveResult::PerformOverloadResolution` with the wired
    // extension-method machinery, `OverloadResolution::AddCandidate` /
    // `BestCandidate` / `IsExtensionMethodInvocation` / `CreateResolveResult` /
    // `GetArgumentsWithConversionsAndNames` / `BestCandidateErrors` /
    // `BestCandidateIsExpandedForm` / `GetArgumentToParameterMap`, the free
    // `IsApplicable(OverloadResolutionErrors)`, `DynamicInvocationResolveResult` (with
    // its `DynamicInvocationType` enum), `UnknownMethodResolveResult` (with the
    // owning-parameters ctor for the synthesized `CreateParameters` results),
    // `UnknownMemberResolveResult` / `UnknownIdentifierResolveResult`,
    // `TypeResolveResult`, `NamedArgumentResolveResult`, `CSharpInvocationResolveResult`,
    // `GetDelegateInvokeMethod`, and `DefaultParameter`.)

    // The C# `IList<ResolveResult> AddArgumentNamesIfNecessary(ResolveResult[]
    // arguments, string[] argumentNames)` (line 2227) -- wraps the arguments whose
    // `argumentNames` entry is non-null in `NamedArgumentResolveResult`s (the dynamic
    // invocation's argument list); a null `argumentNames` ARRAY returns the arguments
    // as-is. Private in the C#; PUBLIC in the port for direct TDD (the TryConvert
    // widening convention). The C# `string[]` null array ports to `std::nullopt` and a
    // null ENTRY to the empty string (the GetArgumentsWithConversions normalization);
    // an out-of-range entry (a mismatched-length `argumentNames` the C# would throw
    // `IndexOutOfRangeException` on) is treated as positional (the D516 safe-fallback
    // convention).
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
    AddArgumentNamesIfNecessary(
        const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
        const std::optional<std::vector<std::string>>& argumentNames) const;

    // The C# `private ResolveResult ResolveInvocation(ResolveResult target,
    // ResolveResult[] arguments, string[] argumentNames, bool allowOptionalParameters)`
    // (line 2244, C# 4.0 spec section 7.6.5) + the public 3-arg overload (line 2336,
    // delegating with `allowOptionalParameters: true`) -- collapsed into ONE public
    // method with the default argument (the StandardImplicitConversion
    // allowTuple-collapse convention; the C# public entry is the only caller of the
    // private core). The invocation resolution: the dynamic-target arm (a
    // `DynamicInvocationResolveResult` over the named-wrapped arguments), the method
    // group arm (the dynamic-arguments sub-arm building a dynamic invocation when MORE
    // THAN ONE method is applicable -- the static-methods-with-value-target re-target
    // to a `TypeResolveResult` over the group's target type; then
    // `PerformOverloadResolution` with the resolver's `checkForOverflow`/`conversions`
    // and the given `allowOptionalParameters`, re-targeting a static non-extension
    // invocation over a value target to the `TypeResolveResult`, else the
    // `CreateResolveResult` composition with the dynamic return-type override; an
    // EMPTY method group yields the `UnknownMethodResolveResult` with the synthesized
    // parameters), the `UnknownMemberResolveResult` / `UnknownIdentifierResolveResult`
    // fallbacks (the `UnknownMethodResolveResult` over the target type / the current
    // type definition), the delegate-invoke arm (the target type's `Invoke` method
    // through a fresh `OverloadResolution`, composed into the
    // `CSharpInvocationResolveResult` marking `isDelegateInvocation`), else the
    // `ErrorResult` singleton.
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `ResolveResult[] arguments` array the resolver "may mutate ... to wrap
    //    elements in `ConversionResolveResult`s" ports to a by-value `std::vector`
    //    (the port's `OverloadResolution` ctor takes the arguments by value and the
    //    wrapped arguments come back through `GetArgumentsWithConversionsAndNames` /
    //    `CreateResolveResult` -- the caller observes the wrapping through the RESULT,
    //    not through array mutation; a documented divergence of the port's engine).
    //  * The C# `mgrr.TargetResult` reference passed to `CreateResolveResult` ports to
    //    the ALIASING `shared_ptr` (co-owning the method group while pointing at the
    //    target result -- the `LiftedUserDefinedOperator` convention); the C#
    //    `new TypeResolveResult(mgrr.TargetType)` recovers the owning handle via
    //    `shared_from_this` + `const_pointer_cast` (the D529 convention).
    //  * The C# `returnTypeOverride: isDynamic ? SpecialType.Dynamic : null` ports to
    //    `std::make_shared<SpecialType>(TypeKind::Dynamic, true)` (the D469
    //    DynamicMemberResolveResult precedent) or the null handle.
    //  * The C# `l[l.Count - 1].DeclaringType != m.DeclaringType` bucket-boundary check
    //    is a REFERENCE comparison -- the port compares the addresses.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveInvocation(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
        std::optional<std::vector<std::string>> argumentNames = std::nullopt,
        bool allowOptionalParameters = true) const;

    // The C# `List<IParameter> CreateParameters(ResolveResult[] arguments, string[]
    // argumentNames)` (line 2348) -- invents a parameter per argument for the
    // `UnknownMethodResolveResult` shape: the name is the given `argumentNames` entry
    // or the GUESSED name (disambiguated with a numeric suffix when the guessed name
    // already occurs in the list), and the type is the argument's type (a
    // by-reference argument keeps its `ReferenceKind`; a null-literal/none-typed
    // argument becomes `object`). Private in the C#; PUBLIC in the port for direct
    // TDD. The C# `List<IParameter>` of freshly-created parameters ports to OWNING
    // handles (the synthesized-parameter convention) so the parameters outlive the
    // call (the C# GC owns them); the `UnknownMethodResolveResult` owning-parameters
    // ctor keeps them alive for the result's lifetime. The C# `ArgumentException` on
    // a mismatched-length `argumentNames` ports to `std::invalid_argument`.
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>>
    CreateParameters(
        const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
        const std::optional<std::vector<std::string>>& argumentNames) const;

    // The C# `static string GuessParameterName(ResolveResult rr)` (line 2398) -- the
    // guessed parameter name: the member's / unknown member's / method group's name,
    // the local variable's name (normalized), the type's name (normalized), or
    // "parameter". Private static in the C#; PUBLIC static in the port for direct TDD
    // (the TryConvert widening convention).
    static std::string GuessParameterName(
        const ILSpy::Decompiler::Semantics::ResolveResult& rr);

    // The C# `static string MakeParameterName(string variableName)` (line 2426) -- the
    // camelCase normalization: empty -> "parameter"; a leading '_' on a multi-char
    // name stripped; the first letter lower-cased. Private static in the C#; PUBLIC
    // static in the port for direct TDD.
    static std::string MakeParameterName(const std::string& variableName);

    // ---- Numeric promotion -------------------------------------------------------------------
    // (The unary/binary numeric-promotion region -- CSharpResolver.cs lines 536-561
    // (`UnaryNumericPromotion`, C# spec draft-v11 section 12.4.7.2) plus lines 1055-1230
    // (`MakeNullable`, `BinaryNumericPromotion` section 12.4.7.3, `IsSigned`, the two
    // `CastTo` overloads) -- the operand-shaping machinery the future `ResolveUnaryOperator`
    // (line 420) / `ResolveBinaryOperator` (line 667) slices consume after the built-in
    // operator overload resolution.)

    // The C# private `IType MakeNullable(IType type, bool isNullable)` (line 1055) -- the
    // `Nullable<T>` wrapper factory: the nullable form via `NullableType.Create`, the input
    // itself otherwise. The C# `IType` return is an owning handle in the port (the
    // `Create` arm builds a fresh `ParameterizedType` that must outlive the call; the
    // passthrough arm recovers the input's own owning handle via `shared_from_this` +
    // `const_pointer_cast` -- the D529 convention: the const is the accessor's contract,
    // the underlying type-system object is shared-managed; a non-shared-managed input
    // would throw `bad_weak_ptr`, the documented stub discipline). Private in the C#;
    // PUBLIC in the port for direct TDD (the TryConvert widening convention).
    ILSpy::Decompiler::TypeSystem::ITypePtr MakeNullable(
        const ILSpy::Decompiler::TypeSystem::IType& type, bool isNullable) const;

    // The C# private `ResolveResult UnaryNumericPromotion(UnaryOperatorType op, ref IType
    // type, bool isNullable, ResolveResult expression)` (line 536, spec section 12.4.7.2
    // -- the unary promotions: `-` on `uint` promotes to `long`; `+`/`~` on the small
    // unsigned types [char..ushort] promotes to `int`; a nullable null literal is treated
    // as `sbyte` so the promotion to `int32` fires for it too). The C# `ref IType type`
    // rebinds the CALLER's type variable to the promoted type -- a C++ reference cannot
    // rebind, so the port threads the caller's rebindable pointer (`const IType*&` -- the
    // rebinds target `FindType` results, whose return is `const IType&`; the reads are all
    // const). The C# enum relational comparisons port through `static_cast<int>` (the
    // D514 convention). Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> UnaryNumericPromotion(
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
        const ILSpy::Decompiler::TypeSystem::IType*& type,
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // The C# private `bool IsSigned(TypeCode code, ResolveResult rr)` (line 1190) -- the
    // signed-primitive test the `UInt64`/`UInt32` promotion arms consult, with the
    // implicit-constant-expression-conversion exceptions: a NON-NEGATIVE `int`/`long`
    // compile-time constant counts as unsigned (`(int)rr.ConstantValue >= 0`). STATIC in
    // the port -- it reads no resolver instance state (the
    // `CreateResolveResultForUserDefinedOperator` convention). The C# unbox `(int)` /
    // `(long)` ports to the pointer-form `std::any_cast` (nullptr on a held-type mismatch,
    // the safe faithful fallback for a shape the C# would throw `InvalidCastException` on
    // -- the mismatched box counts as signed). Private in the C#; PUBLIC in the port for
    // direct TDD (the TryConvert widening convention).
    static bool IsSigned(ILSpy::Decompiler::TypeSystem::TypeCode code,
                         const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr);

    // The C# private `ResolveResult CastTo(TypeCode targetType, bool isNullable,
    // ResolveResult expression, bool allowNullableConstants)` (line 1214) -- delegates to
    // the `IType` overload through `FindType` (the `ReflectionHelper` extension -- the
    // `ICompilation.FindType` takes a `KnownTypeCode`, so the `TypeCode` receiver resolves
    // to the extension, iteration 87). Private in the C#; PUBLIC in the port for direct
    // TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> CastTo(
        ILSpy::Decompiler::TypeSystem::TypeCode targetType,
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
        bool allowNullableConstants) const;

    // The C# private `ResolveResult CastTo(IType targetType, bool isNullable, ResolveResult
    // expression, bool allowNullableConstants)` (line 1219) -- the promotion conversion:
    // an operand already in the target shape returns UNCHANGED; a compile-time constant
    // under `allowNullableConstants` folds through `ResolveCast` (a null constant folds to
    // a null constant over the target shape; an error or non-constant fall-through skips
    // the fold); everything else wraps through `Convert` with the
    // `ImplicitNullableConversion` / `ImplicitNumericConversion` singleton. `targetType`
    // is non-const (the `ResolveCast` / `Convert` calls take non-const `IType&`, the
    // iteration-100 signatures). Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> CastTo(
        ILSpy::Decompiler::TypeSystem::IType& targetType,
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
        bool allowNullableConstants) const;

    // The C# private `bool BinaryNumericPromotion(bool isNullable, ref ResolveResult lhs,
    // ref ResolveResult rhs, bool allowNullableConstants)` (line 1065, spec section
    // 12.4.7.3 -- the binary promotions over the underlying types: the `decimal` target
    // (with the float/double binding error), `double`, `float`, `uint64` (with the
    // signed-operand binding error), the native-integer `nuint`/`nint` targets, the
    // `uint`-with-signed-operand `long` promotion, `long`, and the default `int`; the null
    // literal promotes to the OTHER operand's type code first). The C# `ref ResolveResult`
    // ports to `std::shared_ptr<ResolveResult>&` (the caller's rebindable variables, the
    // TryConvert convention). Returns `!bindingError`. Private in the C#; PUBLIC in the
    // port for direct TDD (the TryConvert widening convention).
    bool BinaryNumericPromotion(
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rhs,
        bool allowNullableConstants) const;

    // ---- Operator-resolution helpers ----------------------------------------------------------
    // (The non-recursive helper region the `ResolveUnaryOperator` (line 326, landed) /
    // `ResolveBinaryOperator` (line 594, a later slice) regions consume -- CSharpResolver.cs
    // lines 525/ 962-985/981-989/1253-1275/2435-2441: the two `OperatorResolveResult`
    // factories, the pointer-arithmetic operator factory, the null-coalescing handler, the
    // nullable-or- non-value-type test, and the `OverloadResolution` construction helper. The
    // mutually recursive enum handlers (`HandleEnumComparison` / `HandleEnumSubtraction` /
    // `HandleEnumOperator`) land below with `ResolveBinaryOperator` itself -- this region
    // is everything they call that is NOT self-recursive.)

    // The C# private `bool IsNullableTypeOrNonValueType(IType type)` (line 981) -- the
    // null-literal comparison guard: a nullable type OR a non-value type (a reference
    // type OR an INDETERMINATE one) compares against the null literal. The C#
    // `type.IsReferenceType != false` is true for BOTH a definite true and an
    // INDETERMINATE `null` (the lifted `bool? != false` yields null [falsy] only for a
    // definite false), so the port is `IsNullable(type) || !(opt.has_value() && !*opt)`.
    // STATIC in the port -- reads no resolver instance state (the `IsSigned` convention).
    // Private in the C#; PUBLIC in the port for direct TDD (the TryConvert widening
    // convention).
    static bool IsNullableTypeOrNonValueType(
        const ILSpy::Decompiler::TypeSystem::IType& type);

    // The C# private `OperatorResolveResult UnaryOperatorResolveResult(IType resultType,
    // UnaryOperatorType op, ResolveResult expression, bool isLifted = false)` (line 525)
    // -- the predefined unary-operator result factory: an `OperatorResolveResult` over
    // the mapped BCL `ExpressionType` (`UnaryOperatorExpression.GetLinqNodeType(op,
    // this.CheckForOverflow)` -- the checked/unchecked distinction threads the resolver's
    // own flag), NO user-defined method, the `isLifted` flag, and the SINGLE operand. The
    // C# `IType` reference becomes an owning handle via `shared_from_this()` (the D529
    // convention; the input is a type-system-owned `IType&`). Private in the C#; PUBLIC in
    // the port for direct TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> UnaryOperatorResolveResult(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
        bool isLifted = false) const;

    // The C# private `ResolveResult BinaryOperatorResolveResult(IType resultType,
    // ResolveResult lhs, BinaryOperatorType op, ResolveResult rhs, bool isLifted = false)`
    // (line 983) -- the predefined binary-operator result factory: the mirror of the
    // unary factory with TWO operands (the C# `new[] { lhs, rhs }` order). Private in the
    // C#; PUBLIC in the port for direct TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> BinaryOperatorResolveResult(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs,
        bool isLifted = false) const;

    // The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
    // resultType, IType inputType1, KnownTypeCode inputType2)` (line 962) -- the two Known-
    // TypeCode-carrying conveniences delegating through `compilation.FindType` (the
    // pointer-arithmetic operands `byte`/`sbyte`/... resolve to the registered known
    // types). Private in the C#; PUBLIC in the port for direct TDD (the TryConvert
    // widening convention).
    std::shared_ptr<BinaryOperatorMethod> PointerArithmeticOperator(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        const ILSpy::Decompiler::TypeSystem::IType& inputType1,
        ILSpy::Decompiler::TypeSystem::KnownTypeCode inputType2) const;

    // The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
    // resultType, KnownTypeCode inputType1, IType inputType2)` (line 967) -- the mirror
    // convenience (the FIRST operand's code).
    std::shared_ptr<BinaryOperatorMethod> PointerArithmeticOperator(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        ILSpy::Decompiler::TypeSystem::KnownTypeCode inputType1,
        const ILSpy::Decompiler::TypeSystem::IType& inputType2) const;

    // The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
    // resultType, IType inputType1, IType inputType2)` (line 972) -- builds the plain
    // built-in operator method over the resolver's compilation with the given return type
    // and the two unnamed `DefaultParameter`s (`string.Empty` names; the C# object-
    // initializer collection-add over the internal `parameters` list ports to the
    // `OperatorMethod::AddParameter` member, the `SetReturnType` internal-setter
    // convention). The parameter type handles are recovered via `shared_from_this()` +
    // `const_pointer_cast` (the D529 convention). Private in the C#; PUBLIC in the port
    // for direct TDD (the TryConvert widening convention).
    std::shared_ptr<BinaryOperatorMethod> PointerArithmeticOperator(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        const ILSpy::Decompiler::TypeSystem::IType& inputType1,
        const ILSpy::Decompiler::TypeSystem::IType& inputType2) const;

    // The C# private `ResolveResult ResolveNullCoalescingOperator(ResolveResult lhs,
    // ResolveResult rhs)` (line 1253, spec section 12.13) -- the `??` handler: a NULLABLE
    // lhs first tries the rhs against the UNDERLYING type (the result type is the
    // underlying); then the rhs against the lhs's own type; then the lhs against the rhs's
    // type; else an `ErrorResolveResult` over the lhs's type. Every `TryConvert` rebind
    // mutates only the LOCAL parameter copy (the C# parameters are value copies of the
    // caller's references -- the `ref` rebinds do not escape), so the port takes the
    // handles by value. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveNullCoalescingOperator(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `OverloadResolution CreateOverloadResolution(ResolveResult[] arguments,
    // string[] argumentNames = null, IType[] typeArguments = null)` (line 2435) -- the
    // resolver's own `OverloadResolution` construction helper threading the instance's
    // compilation, conversions, and `CheckForOverflow` flag (every operator-resolution /
    // invocation region builds its resolution through it). The C# returns a class
    // instance; the port returns an owning `unique_ptr` (the
    // `MethodGroupResolveResult::PerformOverloadResolution` convention). The C# argument
    // arrays port to a `std::vector` / `std::optional<std::vector>` pair (the ctor's
    // null-default normalization). Private in the C#; PUBLIC in the port for direct TDD
    // (the TryConvert widening convention).
    std::unique_ptr<OverloadResolution> CreateOverloadResolution(
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
        std::optional<std::vector<std::string>> argumentNames = std::nullopt,
        std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> typeArguments =
            std::nullopt) const;

    // ---- ResolveUnaryOperator ----------------------------------------------------------------
    // (The `ResolveUnaryOperator` region, CSharpResolver.cs lines 326-530, C# spec
    // draft-v11 section 12.4.4 "Unary operator overload resolution" -- the first of
    // the two big operator-resolution entry points. Every non-recursive helper it
    // consumes is already ported: the two `GetOverloadableOperatorName` statics, the
    // user-defined candidate scan + lift, `CreateResolveResultForUserDefinedOperator`,
    // `CreateOverloadResolution`, `UnaryNumericPromotion`, the `CSharpOperators`
    // operator tables + their `Invoke` constant-evaluation virtuals, the two
    // `OperatorResolveResult` factories, `Convert`/`ResolveCast`, `MakeNullable`, and
    // the `TypeUtils.IsCSharpNativeIntegerType` leaf.)

    // The C# `public ResolveResult ResolveUnaryOperator(UnaryOperatorType op,
    // ResolveResult expression)` (line 326) -- the unary-operator resolution: the
    // dynamic-operand arm (an `await` of a dynamic expression builds the dynamic
    // `AwaitResolveResult` shape; every other operator over a dynamic operand is the
    // dynamic `OperatorResolveResult`), the non-overloadable arms (`*` dereferences a
    // pointer to its element type; `&` addresses to a fresh `PointerType`; the
    // non-dynamic `await` throws (the documented deferral below); anything else is the
    // `UnknownError` singleton), then the overloadable path: the user-defined operator
    // overload resolution first (an applicable user-defined operator wins), the unary
    // numeric promotion, the per-operator built-in table resolution, and the result
    // composition -- constant-folding a compile-time constant operand through the
    // operator's `Invoke` (an `ArithmeticException` downgrades to an
    // `ErrorResolveResult`), else wrapping the operand through `Convert` into the
    // predefined `OperatorResolveResult` (marking the lifted forms).
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `IType type = NullableType.GetUnderlyingType(expression.Type)` LOCAL is
    //    rebound by `UnaryNumericPromotion(op, ref type, ...)` -- the port threads it as a
    //    local `const IType*` (a C++ reference cannot rebind; the `UnaryNumericPromotion`
    //    `const IType*&` signature takes the pointer by reference). Every read below
    //    dereferences the local pointer.
    //  * The C# `catch (ArithmeticException)` around `m.Invoke(this, expression
    //    .ConstantValue)` (line 510) ports to `catch (const Util::ArithmeticException&)`:
    //    the operand cast inside `Invoke` throws `Util::OverflowException` /
    //    `Util::InvalidCastException` and the table bodies throw the typed
    //    `Util::OverflowException` / `Util::DivideByZeroException` -- the family base
    //    swallows exactly the C# `ArithmeticException` family, and `InvalidCastException`
    //    (deliberately NOT a family member) propagates out, faithfully.
    //  * The C# `compilation.FindType(expression.ConstantValue.GetType())` in the `BitNot`
    //    enum constant-folding arm (line 451) ports to the TypeCode-based `FindType`
    //    over `Util::TypeCodeOfBoxedValue` (an enum constant holds its UNDERLYING
    //    primitive value, so the boxed value's runtime type is that primitive's
    //    `TypeCode`).
    //  * The C# hard cast `(CSharpOperators.UnaryOperatorMethod)builtinOperatorOR
    //    .BestCandidate` ports to `dynamic_cast<const UnaryOperatorMethod*>` with the
    //    documented safe fallback (an empty/foreign best candidate is impossible through
    //    the builtin-table call site -- every table entry IS a `UnaryOperatorMethod` and
    //    the first `AddCandidate` always folds a best; the fallback returns the
    //    `ErrorResolveResult` over the operand's type, the D516 convention).
    //  * The C# `static readonly ResolveResult ErrorResult = ErrorResolveResult
    //    .UnknownError` returns of the non-overloadable arms port to a NON-OWNING
    //    aliasing `shared_ptr` over the program-lifetime singleton (the empty-owner
    //    aliasing constructor -- no deleter ever runs; the singleton is never destroyed).
    //  * The C# non-dynamic `await` arm (lines 353-389) computes a chain of
    //    `ResolveMemberAccess` / `ResolveInvocation` / `CreateMemberLookup` results that
    //    the arm then DISCARDS -- the C# unconditionally ends in `throw new
    //    NotImplementedException()` (the C# comment: "I believe this is dead code for
    //    ILSpy anyways"). The port documents that pre-throw work as DEFERRED and throws
    //    `std::logic_error` directly at the arm entry (the `NotImplementedException`
    //    convention; observationally identical -- the dead values are discarded and the
    //    C# member lookups have no side effects). The port lands with the
    //    `ResolveMemberAccess` / `ResolveInvocation` regions still unported; wiring the
    //    arm's machinery would change nothing observable.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveUnaryOperator(
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // ---- ResolveBinaryOperator + enum handlers -----------------------------------------------
    // (The `ResolveBinaryOperator` region, CSharpResolver.cs lines 594-948, C# 4.0 spec
    // section 7.3.4 "Binary operator overload resolution", plus the mutually recursive
    // enum-handler trio from the "Enum helper methods" region at lines 995-1053 -- each
    // handler calls `ResolveBinaryOperator` back on the enum's underlying operands, so
    // the four members land together. Every non-recursive helper the region consumes is
    // already ported: the two `GetOverloadableOperatorName` statics, the user-defined
    // candidate scan + lift, `CreateResolveResultForUserDefinedOperator`,
    // `CreateOverloadResolution`, `UnaryNumericPromotion`, `BinaryNumericPromotion`,
    // `MakeNullable`, `TryConvert` / `TryConvertEnum`, the member `GetEnumUnderlyingType`,
    // `ResolveNullCoalescingOperator`, `IsNullableTypeOrNonValueType`, the `CSharpOperators`
    // operator tables + their `Invoke` constant-evaluation virtuals, the
    // `BinaryOperatorResolveResult` factory, `Convert`/`ResolveCast`, and the
    // `TypeUtils.IsCSharpNativeIntegerType` leaf.)

    // The C# `public ResolveResult ResolveBinaryOperator(BinaryOperatorType op,
    // ResolveResult lhs, ResolveResult rhs)` (line 594) -- the binary-operator
    // resolution: the dynamic arm (either operand dynamic converts BOTH to dynamic and
    // yields the dynamic `OperatorResolveResult`), the overloadable-name gate
    // (`ConditionalAnd`/`ConditionalOr` fall through to their bitwise names; the
    // null-coalescing operator delegates to `ResolveNullCoalescingOperator`; anything
    // else without a name is the `UnknownError` singleton), then the overloadable path:
    // the nullable strip (a nullable operand contributes its UNDERLYING type; the
    // null-literal-vs-value-type pair forces `isNullable`), the user-defined operator
    // overload resolution FIRST (an applicable user-defined operator wins; the scan runs
    // over BOTH operands' types, deduplicated by member identity -- the C# `HashSet`
    // reference-equality `UnionWith`), the shift special case (each operand independently
    // unary-promoted; `null << null` produces `int?`), the binary numeric promotion
    // (with the equality/inequality `allowNullableConstants` gate; a binding error is an
    // `ErrorResolveResult` over the lhs's type), the per-operator built-in table
    // selection with the inline enum/delegate/null-literal arms (`E + U`, `E - E`, `E -
    // U`, `E & E`, enum comparisons, delegate combination/separation, the reference
    // comparison and null-literal comparison special cases), the native-integer arms
    // (equal native integers keep the type, mixing them is an error), and the result
    // composition -- constant-folding both compile-time-constant operands through the
    // operator's `Invoke` (an `ArithmeticException` downgrades to an `ErrorResolveResult`),
    // else wrapping both operands through `Convert` into the predefined
    // `OperatorResolveResult` (marking the lifted forms).
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `IType lhsType` / `rhsType` LOCALS are rebound by the shift arm's
    //    `UnaryNumericPromotion(UnaryOperatorType.Plus, ref lhsType, ...)` and re-read
    //    after the promotion -- the port threads them as local `const IType*` pointers
    //    (a C++ reference cannot rebind; the `UnaryNumericPromotion` `const IType*&`
    //    signature takes the pointer by reference). Every read below dereferences the
    //    local pointer.
    //  * The C# `HashSet<IParameterizedMember> userOperatorCandidates` + `UnionWith`
    //    dedups by the default reference equality -- the port collects into a
    //    `std::vector<const IParameterizedMember*>` filtered by POINTER identity (the
    //    `GetApplicableConversionOperators` dedup convention).
    //  * The C# `lhsType.IsReferenceType == false` null-vs-value-type check is a
    //    DEFINITE-false check (the nullable equality never propagates null -- the
    //    iteration-103 corrected semantics), so the port is `opt.has_value() && !*opt`.
    //  * The C# `conversions.IdentityConversion(lhsType, rhsType)` has no public method
    //    on the port's `CSharpConversions` -- the port calls the `Detail::IdentityConversion`
    //    free function (the iteration-47 convention). The `conversions.ExplicitConversion
    //    (lhsType, rhsType).IsReferenceConversion` calls the public (uncached)
    //    `ExplicitConversion(IType, IType)` method.
    //  * The C# hard cast `(CSharpOperators.BinaryOperatorMethod)builtinOperatorOR
    //    .BestCandidate` ports to `dynamic_cast<const BinaryOperatorMethod*>` with the
    //    documented safe fallback (an empty/foreign best candidate is impossible through
    //    the builtin-table call site -- every table entry IS a `BinaryOperatorMethod` and
    //    the first `AddCandidate` always folds a best; the fallback returns the
    //    `ErrorResolveResult` over the lhs's type, the D516 convention).
    //  * The enum arms' `GetEnumUnderlyingType` member result can be null for a
    //    degenerate enum-definition stub without a configured underlying (the C# NREs on
    //    the subsequent deref); the port's documented safe fallback treats the enum arm
    //    as not firing (the null-member convention).
    //  * The C# `catch (ArithmeticException)` around `m.Invoke(this, lhs.ConstantValue,
    //    rhs.ConstantValue)` ports to `catch (const Util::ArithmeticException&)` (the
    //    `ResolveUnaryOperator` family-catch convention: the operand casts throw
    //    `Util::OverflowException`/`Util::InvalidCastException`, the table bodies throw
    //    the typed family members, and `InvalidCastException` -- deliberately NOT a
    //    family member -- propagates out, faithfully).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveBinaryOperator(
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `ResolveResult HandleEnumComparison(BinaryOperatorType op, IType
    // enumType, bool isNullable, ResolveResult lhs, ResolveResult rhs)` (line 995) --
    // "bool operator op(E x, E y)" evaluated as `((U)x op (U)y`: a both-constant,
    // non-nullable, non-enum-underlying pair re-resolves through the underlying type and
    // keeps the folded constant when it stays one; everything else is the predefined
    // `OperatorResolveResult` over `bool` marking `isNullable`. Private in the C#;
    // PUBLIC in the port for direct TDD (the TryConvert widening convention). The
    // operands are owning handles (the C# value-copy references the GC owns).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> HandleEnumComparison(
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        const ILSpy::Decompiler::TypeSystem::IType& enumType, bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `ResolveResult HandleEnumSubtraction(bool isNullable, IType
    // enumType, ResolveResult lhs, ResolveResult rhs)` (line 1013) -- "U operator
    // -(E x, E y)" evaluated as `(U)((U)x - (U)y)`: the both-constant fold re-resolves
    // through the underlying type and re-casts UNCHECKED back into the UNDERLYING (not
    // the enum); everything else is the predefined `OperatorResolveResult` over the
    // (nullable) underlying type. Private in the C#; PUBLIC in the port for direct TDD
    // (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> HandleEnumSubtraction(
        bool isNullable, const ILSpy::Decompiler::TypeSystem::IType& enumType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `ResolveResult HandleEnumOperator(bool isNullable, IType enumType,
    // BinaryOperatorType op, ResolveResult lhs, ResolveResult rhs)` (line 1037) -- "E
    // operator +(E x, U y)" / "E operator +(U x, E y)" / "E operator -(E x, U y)" / the
    // enum bitwise operators, evaluated as `(E)((U)x op (U)y)`: the both-constant fold
    // re-resolves through the underlying type and re-casts UNCHECKED back into the ENUM;
    // everything else is the predefined `OperatorResolveResult` over the (nullable) enum
    // type. Private in the C#; PUBLIC in the port for direct TDD (the TryConvert
    // widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> HandleEnumOperator(
        bool isNullable, const ILSpy::Decompiler::TypeSystem::IType& enumType,
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // ---- sizeof / this / base / typeof --------------------------------------------------------
    // (The expression-resolution tail regions of CSharpResolver.cs: "ResolveSizeOf"
    // lines 2591-2626, "Resolve This/Base Reference" lines 2628-2667, and
    // `ResolveTypeOf` lines 2935-2937 -- four self-contained resolver entry points
    // every prerequisite of which is already ported: `SizeOfResolveResult` (D427),
    // `ThisResolveResult` (D426), `TypeOfResolveResult` (D427),
    // `ReflectionHelper.GetTypeCode` (D513), the `ICompilation.FindType(KnownTypeCode)`
    // member, `ParameterizedType`, and the `CurrentTypeDefinition` context slot.)

    // The C# `public ResolveResult ResolveSizeOf(IType type)` (line 2591) -- the
    // `sizeof` resolution: the result type is the registered `System.Int32`, and the
    // compile-time-known size comes from the primitive `TypeCode` table
    // (bool/sbyte/byte -> 1, char/int16/uint16 -> 2, int32/uint32/single -> 4,
    // int64/uint64/double -> 8; everything else -- `decimal`, `DateTime`, pointers,
    // structs without a primitive code -- has NO constant size). An ENUM reads its
    // size through its UNDERLYING type (the `type.Kind == TypeKind.Enum` ternary). The
    // `IsError` of the returned `SizeOfResolveResult` reports a `sizeof` of a
    // reference type (or a type of indeterminate reference-ness).
    //
    // PORT CONVENTIONS: the C# `type.GetDefinition().EnumUnderlyingType` would NRE
    // for an enum-kind type whose definition does not resolve or whose underlying is
    // not configured -- the port's documented safe fallback treats the enum arm as NOT
    // firing and reads the type's own `TypeCode` instead (the D516 null-guard
    // convention). The `IType` parameter is non-const (the `shared_from_this`
    // result-handle convention) and the `int32` handle is recovered from the `FindType`
    // reference through `const_pointer_cast` (the registered known types are
    // shared-managed, the D517 convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveSizeOf(
        ILSpy::Decompiler::TypeSystem::IType& type) const;

    // The C# `public ResolveResult ResolveThisReference()` (line 2628) -- the `this`
    // reference: a current type definition WITH type parameters self-parameterizes
    // (`new ThisResolveResult(new ParameterizedType(t, t.TypeParameters))` -- `this`
    // inside a generic `C<T,U>` has type `C<T,U>` with the DECLARED type parameters as
    // the type arguments); a non-generic current type definition yields
    // `ThisResolveResult(t)` directly; no current type definition is the `ErrorResult`
    // singleton (pointer-identical to `ErrorResolveResult::UnknownError`).
    //
    // PORT CONVENTIONS: the C# `new ParameterizedType(t, t.TypeParameters)` passes the
    // declared type parameters as the type arguments, but the port's
    // `IType::TypeParameters()` yields NON-OWNING `const ITypeParameter*` while the
    // `ParameterizedType` ctor takes owning `ITypePtr` handles -- the port recovers
    // each handle through `shared_from_this` + `const_pointer_cast` (every type
    // parameter is shared-managed in the D271 handle model; the
    // `SpecializedMember::DeclaringType` arm 2 documented this same conversion gap and
    // fell back, here the self-parameterization is the entire point and must happen).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveThisReference() const;

    // The C# `public ResolveResult ResolveBaseReference()` (line 2649) -- the `base`
    // reference: the FIRST direct base type whose kind is neither `Unknown` nor
    // `Interface` (the runtime base class), as a `ThisResolveResult` marking
    // `causesNonVirtualInvocation: true` (member invocations through `base` are
    // non-virtual); no current type definition -- or a current type definition whose
    // direct bases are all unknown/interfaces -- is the `ErrorResult` singleton.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveBaseReference() const;

    // The C# `public ResolveResult ResolveTypeOf(IType referencedType)` (line 2935) --
    // the `typeof` resolution: a `TypeOfResolveResult` whose own type is the
    // registered `System.Type` and whose `ReferencedType` is the named type. The
    // `IType` parameter is non-const (the `shared_from_this` result-handle
    // convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveTypeOf(
        ILSpy::Decompiler::TypeSystem::IType& referencedType) const;

    // ---- condition / primitive / default value / assignment ---------------------------------
    // (The condition / primitive / default-value / assignment quartet of
    // CSharpResolver.cs: "ResolveConditional" lines 2671-2795 (with the private
    // `IsBetterConditionalConversion` / `HasType` helpers), "ResolvePrimitive" lines
    // 2798-2810, "ResolveDefaultValue" lines 2814-2878 (the `GetDefaultValue` static
    // included), and "ResolveAssignment" lines 2941-2960 -- every prerequisite of
    // which is already ported: the ResolveResult-based `ImplicitConversion` (D529),
    // `Convert` / `TryConvert` (the Convert region), `ResolveUnaryOperator` /
    // `ResolveBinaryOperator` (the operator regions),
    // `AssignmentExpression.GetLinqNodeType` / `GetCorrespondingBinaryOperator`
    // (the mapping layer), `Util::TypeCodeOfBoxedValue` + the TypeCode-based `FindType`
    // (the primitive-cast / ReflectionHelper leaves), and the `OperatorResolveResult` /
    // `ConstantResolveResult` / `ErrorResolveResult` factories.)

    // The C# `public ResolveResult ResolveCondition(ResolveResult input)` (line 2671)
    // -- converts the input to `bool` using the rules for boolean expressions: a
    // regular implicit conversion to `bool` if one exists, else the type's
    // `operator true` (an `op_True`-named operator method found in the type's method
    // table) wrapped in a user-defined conversion with NO before/after conversions,
    // applied through `Convert`. The C# `ArgumentNullException` on a null input ports
    // to `std::invalid_argument` (the context-ctor convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveCondition(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> input) const;

    // The C# `public ResolveResult ResolveConditionFalse(ResolveResult input)` (line
    // 2693) -- converts the NEGATED input to `bool`: `!(bool)input` via
    // `ResolveUnaryOperator(Not, Convert(...))` if the implicit cast to `bool` is
    // valid; otherwise the type's `operator false` (an `op_False`-named operator
    // method) applied directly through `Convert`. The C# `ArgumentNullException` on
    // a null input ports to `std::invalid_argument` (the context-ctor convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveConditionFalse(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> input) const;

    // The C# `public ResolveResult ResolveConditional(ResolveResult condition,
    // ResolveResult trueExpression, ResolveResult falseExpression)` (line 2711, C#
    // 4.0 spec section 7.14) -- the ternary conditional operator: the dynamic arm
    // (either branch dynamic makes the result dynamic, both branches TryConvert-ed
    // with the C# NON-SHORT-CIRCUIT `&` so both convert), the both-typed arm (the
    // better-conditional-conversion tiebreak in each direction; a tie keeps the
    // true-branch's type with validity by type equivalence), the one-sided arms (the
    // typed branch's type with the other branch TryConvert-ed), the neither-typed
    // early `ErrorResult`, then the result composition over the
    // `ResolveCondition`-converted condition: a constant condition with both constant
    // branches folds to the selected branch, else the predefined
    // `OperatorResolveResult` over `ExpressionType.Conditional` with the three
    // operands; invalid yields the `ErrorResolveResult` over the result type. The C#
    // rebinds of `condition` / `trueExpression` / `falseExpression` are all LOCAL (the
    // parameters are value copies of the caller's references), so the port's by-value
    // handles rebind freely. Private helper predicates `IsBetterConditionalConversion`
    // and `HasType` land below as public statics for direct TDD.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveConditional(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> condition,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> trueExpression,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> falseExpression) const;

    // The C# private `bool IsBetterConditionalConversion(Conversion c1, Conversion c2)`
    // (line 2786) -- the conditional tiebreak: "Valid is better than
    // ImplicitConstantExpressionConversion is better than invalid". The C# reference
    // comparisons against the `ImplicitConstantExpressionConversion` singleton port
    // to POINTER identity (the D536 singleton convention). Private in the C#; PUBLIC
    // static in the port for direct TDD (the TryConvert widening convention).
    static bool IsBetterConditionalConversion(
        const std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>& c1,
        const std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>& c2);

    // The C# private `bool HasType(ResolveResult r)` (line 2791) -- whether the result
    // carries a usable type (neither the None nor the Null null-object kind). Private
    // in the C#; PUBLIC static in the port for direct TDD (the TryConvert widening
    // convention).
    static bool HasType(const ILSpy::Decompiler::Semantics::ResolveResult& r);

    // The C# `public ResolveResult ResolvePrimitive(object value)` (line 2798) -- the
    // primitive-literal resolution: `null` is a plain `ResolveResult` over the
    // null-literal type (`SpecialType.NullType`); any other boxed value resolves
    // through its runtime `TypeCode` (the `Util::TypeCodeOfBoxedValue` mapping) to the
    // registered known type and yields a `ConstantResolveResult` carrying the value.
    // The C# `object value` ports to `const std::any&` (an EMPTY any is the C# null).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolvePrimitive(
        const std::any& value) const;

    // The C# `public ResolveResult ResolveDefaultValue(IType type)` (line 2814) -- the
    // `default(T)` resolution: a `ConstantResolveResult` over the type carrying the
    // type's default value (a null `ConstantValue` for types without a primitive
    // default). The `IType` parameter is non-const (the `shared_from_this`
    // result-handle convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveDefaultValue(
        ILSpy::Decompiler::TypeSystem::IType& type) const;

    // The C# `public static object GetDefaultValue(IType type)` (line 2819) -- the
    // default value of the type: the DEFINITION's per-known-type-code zero (false /
    // '\0' / the integral and floating zeros / the decimal zero), an ENUM reading its
    // default through its UNDERLYING type's definition; null for a type without a
    // resolvable definition or without a primitive default. The C# `object` return
    // ports to `std::any` (an empty any is the C# null). The C#
    // `typeDef.EnumUnderlyingType.GetDefinition()` NREs for a degenerate enum without
    // an underlying; the port's null check returns the null default (the D516
    // safe-fallback convention).
    static std::any GetDefaultValue(const ILSpy::Decompiler::TypeSystem::IType& type);

    // The C# `public ResolveResult ResolveAssignment(AssignmentOperatorType op,
    // ResolveResult lhs, ResolveResult rhs)` (line 2941) -- the assignment resolution:
    // the plain assignment is a two-operand `OperatorResolveResult` over the lhs's
    // type with the rhs converted to it; a COMPOUND assignment resolves the underlying
    // binary operation through `ResolveBinaryOperator` and, when that yields a
    // two-operand `OperatorResolveResult`, re-shapes it into the assignment form over
    // the lhs's type (carrying the binary result's user-defined method and lifted
    // flag, with the lhs and the binary result's SECOND operand as the operands);
    // anything else (an error result, a constant fold, a delegate combination ...) is
    // returned as-is.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveAssignment(
        ILSpy::Decompiler::CSharp::Syntax::AssignmentOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // ---- ResolveForeach ------------------------------------------------------------------------
    // (The `ResolveForeach` region, CSharpResolver.cs lines 1913-2018, C# 4.0 spec
    // section 8.8.4 "The foreach statement": the public `ResolveForeach` entry (line
    // 1914) + the private `CheckForEnumerableInterface` helper (line 1991). Every
    // prerequisite is already ported: `CreateMemberLookup` (the simple-name region),
    // `MemberLookup::Lookup` (D500), `MethodGroupResolveResult::
    // PerformOverloadResolution` (iteration 79), the `OverloadResolution` output
    // properties + `CreateResolveResult` + `GetBestCandidateWithSubstitutedTypeArguments`
    // (the output-properties/wrappers regions), `ResolveCast` / `ResolveMemberAccess` /
    // `ResolveInvocation` (the convert/member-access/invocation regions), and the
    // `GetElementTypeFromIEnumerable` TypeSystemExtensions leaf (the direct
    // prerequisite this region ports alongside).)

    // The C# `public ForEachResolveResult ResolveForeach(ResolveResult expression)`
    // (line 1914) -- resolves the foreach pattern over the collection expression:
    // the ARRAY / DYNAMIC arm casts the collection to the non-generic
    // `System.Collections.IEnumerable` (the element type is the array's element /
    // dynamic) and resolves `GetEnumerator` through the cast chain
    // (ResolveCast -> ResolveMemberAccess -> ResolveInvocation over the registered
    // IEnumerable); otherwise the ENUMERATOR PATTERN first (`GetEnumerator` looked up
    // on the collection, overload-resolved with the three allow-flags pinned false,
    // kept only when an applicable, unambiguous, PUBLIC INSTANCE method was found --
    // the `Current` property then supplies the element type), falling back to
    // `CheckForEnumerableInterface` (the `IEnumerable<T>` / `IEnumerable` interface
    // pattern) when the lookup finds no method group or the guard rejects it. The
    // `MoveNext` method is resolved on the enumerator type through its own method
    // group + `GetBestCandidateWithSubstitutedTypeArguments`, and the `Current`
    // property (re-looked-up when the enumerator-pattern arm did not supply it)
    // contributes the `IProperty`. The method is `const` (it reads the resolver's
    // compilation and clones nothing). The C# `ResolveResult` parameter ports to an
    // owning `std::shared_ptr` (the C# GC reference; the enumerator-pattern arm passes
    // it to `CreateResolveResult`, which co-owns it via the aliasing-shared_ptr
    // convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ForEachResolveResult> ResolveForeach(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // The C# `void CheckForEnumerableInterface(ResolveResult expression, out IType
    // collectionType, out IType enumeratorType, out IType elementType, out ResolveResult
    // getEnumeratorInvocation)` (line 1991) -- the interface-pattern fallback: the
    // element type comes from `GetElementTypeFromIEnumerable` (generic `true` builds the
    // `IEnumerable<T>` / `IEnumerator<T>` PARAMETERIZED types over the registered
    // open-generic definitions, generic `false` uses the registered non-generic
    // interfaces, neither yields the `UnknownType` null object), then the same
    // ResolveCast -> ResolveMemberAccess -> ResolveInvocation `GetEnumerator` chain
    // runs over the synthesized collection type. Private in the C#; PUBLIC in the port
    // for direct TDD (the TryConvert widening convention). The four C# `out` parameters
    // port to reference out-params; the three `IType` outs are OWNING `ITypePtr`s (the
    // C# GC references) and the invocation out is an owning `shared_ptr<ResolveResult>`.
    // The `expression` parameter is a CONST reference (the C# passes the caller's
    // reference through to `ResolveCast` unchanged, and `ResolveCast` copies it).
    void CheckForEnumerableInterface(
        const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& expression,
        ILSpy::Decompiler::TypeSystem::ITypePtr& collectionType,
        ILSpy::Decompiler::TypeSystem::ITypePtr& enumeratorType,
        ILSpy::Decompiler::TypeSystem::ITypePtr& elementType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& getEnumeratorInvocation) const;

    // ---- ResolveIndexer ------------------------------------------------------------------------
    // (The `ResolveIndexer` region, CSharpResolver.cs lines 2456-2522, C# 4.0 spec
    // sections 7.6.6.1 (array access) / 18.5.3 (pointer element access) / 7.6.6.2
    // (indexer access): the public `ResolveIndexer` entry (line 2456) + the private
    // `AdjustArrayAccessArguments` helper (line 2513). Every prerequisite is already
    // ported: `AddArgumentNamesIfNecessary` (the invocation region), the free
    // `IsApplicable(OverloadResolutionErrors)`, `CreateMemberLookup` + `MemberLookup::
    // LookupIndexers` (the simple-name region / D-something), `CreateOverloadResolution`
    // + `OverloadResolution::AddMethodLists` + `CreateResolveResult` (the
    // operator-helpers region / the OverloadResolution engine), `TryConvert` / `Convert`
    // (the convert region), `ArrayAccessResolveResult` (Semantics), and
    // `DynamicInvocationResolveResult` with `DynamicInvocationType::Indexing` (D469).)

    // The C# `public ResolveResult ResolveIndexer(ResolveResult target,
    // ResolveResult[] arguments, string[] argumentNames = null)` (line 2456) -- the
    // indexer-access resolution. The arms on the TARGET's kind: a DYNAMIC target is a
    // `DynamicInvocationResolveResult` with `DynamicInvocationType::Indexing` over the
    // named-wrapped arguments; an ARRAY / POINTER target is an `ArrayAccessResolveResult`
    // over the element type (the arguments first adjusted to int/uint/long/ulong); else
    // the INDEXER ACCESS -- `MemberLookup::LookupIndexers` supplies the candidate lists
    // (a DYNAMIC argument makes the invocation dynamic when more than one indexer is
    // applicable -- the throwaway resolution counts the applicable candidates),
    // `AddMethodLists` folds them into the best-candidate state, and the best candidate
    // composes through `CreateResolveResult` with the target; no best candidate yields
    // the `ErrorResult` singleton. The method is `const` (it reads the resolver's
    // compilation / conversions and clones nothing).
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `ResolveResult[] arguments` array the resolver "may mutate ... to wrap
    //    elements in `ConversionResolveResult`s" ports to a by-value `std::vector`;
    //    the array arm's adjustment and the indexer arm's conversion wraps are observed
    //    through the RESULT (the `ArrayAccessResolveResult::Indexes` / the
    //    `CreateResolveResult` argument list), not through array mutation (the
    //    ResolveInvocation convention).
    //  * The C# `((TypeWithElementType)target.Type).ElementType` is the flattened
    //    TypeWithElementType dispatch (the `ElementTypeOf` convention): the Kind guard
    //    guarantees Array or Pointer, so the `dynamic_cast` resolves the concrete leaf;
    //    a degenerate leaf with a null element (never produced by the real type system)
    //    falls back to the `UnknownType` null object (the D516 convention -- the
    //    `ResolveResult` base ctor asserts the element type non-null).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveIndexer(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
        std::optional<std::vector<std::string>> argumentNames = std::nullopt) const;

    // The C# `void AdjustArrayAccessArguments(ResolveResult[] arguments)` (line 2513)
    // -- "Converts all arguments to int, uint, long or ulong": the first `TryConvert`
    // that succeeds rebinds the caller's argument in place (the short-circuiting
    // int32/uint32/int64/uint64 chain); when none applies the argument is `Convert`ed to
    // the registered Int32 under `Conversion.None` (the error-preserving wrap; a
    // compile-time constant re-folds through the target). Private in the C#; PUBLIC in
    // the port for direct TDD (the TryConvert widening convention). The C# array
    // mutation ports to the by-reference vector (the helper's observable IS the mutated
    // list -- unlike `ResolveIndexer`, which observes through its result). The method is
    // `const` (it reads only the compilation / conversions).
    void AdjustArrayAccessArguments(
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments) const;

    // ---- ResolveObjectCreation ----------------------------------------------------------------
    // (The `ResolveObjectCreation` region, CSharpResolver.cs lines 2539-2585: the
    // public `ResolveObjectCreation` entry (line 2539). Every prerequisite is already
    // ported: the free `GetDelegateInvokeMethod` (TypeSystemExtensions),
    // `MethodGroupResolveResult` + `MethodListWithDeclaringType` (with the
    // extension-method machinery), `Convert` (the convert region),
    // `CreateOverloadResolution` + both `OverloadResolution::AddCandidate` overloads +
    // `CreateResolveResult` (the operator-helpers region / the OverloadResolution
    // engine), `CreateMemberLookup` + `MemberLookup::IsAccessible` (the simple-name
    // region), the free `IsApplicable(OverloadResolutionErrors)`, and
    // `AddArgumentNamesIfNecessary` (the invocation region), `IType::GetConstructors`
    // (the member-enumeration surface), and `DynamicInvocationResolveResult` with
    // `DynamicInvocationType::ObjectCreation` (D469).)

    // The C# `public ResolveResult ResolveObjectCreation(IType type, ResolveResult[]
    // arguments, string[] argumentNames = null, bool allowProtectedAccess = false,
    // IList<ResolveResult> initializerStatements = null)` (line 2539) -- the
    // object-creation resolution. The DELEGATE arm (a Delegate-kind target with
    // exactly one argument): the argument's type resolves its `Invoke` method through
    // `GetDelegateInvokeMethod` and the argument is re-wrapped as a
    // `MethodGroupResolveResult` over that invoke (a delegate-to-delegate conversion
    // routes through the method-group conversion machinery), then `Convert`ed to the
    // target; an argument whose type resolves no invoke method converts as-is. The
    // CONSTRUCTOR scan: every constructor `type.GetConstructors()` yields is added
    // through `AddCandidate` -- the accessible ones plainly, the inaccessible ones with
    // the `Inaccessible` additional error (the `allowProtectedAccess` flag threads into
    // `MemberLookup::IsAccessible`; the C# doc: "This should be false except when
    // resolving constructor initializers"). A DYNAMIC argument makes the creation a
    // `DynamicInvocationResolveResult` with `DynamicInvocationType::ObjectCreation`
    // when MORE THAN ONE constructor is applicable (the method group over the
    // applicable constructors named after the first, the named-wrapped arguments, and
    // the initializer statements). The best candidate composes through
    // `CreateResolveResult` with a NULL target (constructors have no target result)
    // carrying the initializer statements; no best candidate yields a FRESH
    // `ErrorResolveResult` over the type (NOT the `UnknownError` singleton -- the error
    // carries the creation type). The method is `const` (it reads the resolver's
    // compilation / conversions and clones nothing).
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `ResolveResult[] arguments` array (which the C# doc says the resolver
    //    "may mutate ... to wrap elements in `ConversionResolveResult`s") ports to a
    //    by-value `std::vector` (the ResolveInvocation convention -- this body does not
    //    mutate the array; the conversion wraps are observed through the RESULT).
    //  * The C# `string[] argumentNames = null` ports to `std::optional<std::vector<
    //    std::string>>` (the null-array normalization); the C# `IList<ResolveResult>
    //    initializerStatements = null` ports to a by-value `std::vector` (the
    //    `DynamicInvocationResolveResult` ctor convention; the C# null list and the
    //    empty list are observationally identical through the result).
    //  * The C# `IType type` reference parameter ports to a non-const `IType&` (the
    //    `Convert`/`ResolveCast` convention); the owning `ITypePtr` handles the
    //    `MethodListWithDeclaringType` / `ErrorResolveResult` constructions need are
    //    recovered through `shared_from_this` (the D529 convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveObjectCreation(
        ILSpy::Decompiler::TypeSystem::IType& type,
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
        std::optional<std::vector<std::string>> argumentNames = std::nullopt,
        bool allowProtectedAccess = false,
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            initializerStatements = {}) const;

private:
    // The C# private nested `sealed class ObjectInitializerContext` -- the linked stack
    // of objects being initialized (`prev` is the enclosing initializer; nullable).
    struct ObjectInitializerContext {
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initializedObject;
        std::shared_ptr<ObjectInitializerContext> prev;

        ObjectInitializerContext(
            std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initialized,
            std::shared_ptr<ObjectInitializerContext> previous)
            : initializedObject(std::move(initialized)), prev(std::move(previous)) {}
    };

    // The C# private nested `sealed class TypeDefinitionCache` -- the per-current-type-
    // definition lookup caches the `LookupSimpleNameOrTypeName` arms populate (the
    // simple-name lookup region). The three
    // dictionaries store NULLABLE results (an empty `shared_ptr` is the C# stored-null
    // known-negative entry, header convention (e)).
    class TypeDefinitionCache {
    public:
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition& TypeDefinition;
        std::unordered_map<std::string, std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            SimpleNameLookupCacheExpression;
        std::unordered_map<std::string, std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            SimpleNameLookupCacheInvocationTarget;
        std::unordered_map<std::string, std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            SimpleTypeLookupCache;

        explicit TypeDefinitionCache(
            const ILSpy::Decompiler::TypeSystem::ITypeDefinition& typeDefinition)
            : TypeDefinition(typeDefinition) {}
    };

    // The C# private full ctor -- the single construction path the `With*` clone
    // factories thread every field through. `make_shared` cannot reach it (its
    // construction happens outside the class's private access, the
    // `UsingScope::WithNestedNamespace` precedent), so the factories allocate via
    // `new`.
    CSharpResolver(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                   CSharpConversions& conversions,
                   std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context,
                   bool checkForOverflow,
                   bool isWithinLambdaExpression,
                   std::shared_ptr<TypeDefinitionCache> currentTypeDefinitionCache,
                   Util::ImmutableStack<std::shared_ptr<const VariableMap>> localVariableStack,
                   std::shared_ptr<ObjectInitializerContext> objectInitializerStack);

    // The C# private `CSharpResolver WithContext(CSharpTypeResolveContext newContext)`
    // -- the shared clone helper the `With*` factories that replace a context slot call.
    std::shared_ptr<CSharpResolver> WithContext(
        std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> newContext) const;

    // The C# private `CSharpResolver WithLocalVariableStack(...)` -- the clone helper
    // `AddVariables` calls.
    std::shared_ptr<CSharpResolver> WithLocalVariableStack(
        Util::ImmutableStack<std::shared_ptr<const VariableMap>> stack) const;

    // The C# private `CSharpResolver WithObjectInitializerStack(...)` -- the clone
    // helper `PushObjectInitializer` / `PopObjectInitializer` call.
    std::shared_ptr<CSharpResolver> WithObjectInitializerStack(
        std::shared_ptr<ObjectInitializerContext> stack) const;

    // The C# private `void LiftUserDefinedOperators(List<IMethod> operators)` (line
    // 1296) -- appends the lifted `Nullable<T>` form of every operator in the list,
    // capturing the ORIGINAL count as the loop bound first so the freshly-appended
    // lifted forms are themselves not lifted again (a lifted operator's parameters are
    // `Nullable<T>`, so a second lift would find nothing liftable -- the fixed bound
    // makes that structural, never even calling the lift). Mutates only the caller's
    // vector; reads no resolver instance state (only the static `CSharpOperators::
    // LiftUserDefinedOperator`), so the method is `const`. Private like the C#; tested
    // transitively through the public `GetUserDefinedOperatorCandidates`.
    void LiftUserDefinedOperators(
        std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod>>& operators) const;

    // The `enable_shared_from_this` bridge for the two C# `return this` early-outs
    // (header convention (a)).
    std::shared_ptr<CSharpResolver> Self() const;

    // The C# field set (all `readonly`; the immutable-resolver pattern).
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    CSharpConversions& conversions_;
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    bool checkForOverflow_ = false;
    bool isWithinLambdaExpression_ = false;
    std::shared_ptr<TypeDefinitionCache> currentTypeDefinitionCache_;
    Util::ImmutableStack<std::shared_ptr<const VariableMap>> localVariableStack_;
    std::shared_ptr<ObjectInitializerContext> objectInitializerStack_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
